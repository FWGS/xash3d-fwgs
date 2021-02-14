#include "vk_brush.h"

#include "vk_core.h"
#include "vk_const.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_framectl.h"
#include "vk_math.h"
#include "vk_textures.h"
#include "vk_lightmap.h"
#include "vk_scene.h"
#include "vk_render.h"

#include "ref_params.h"
#include "eiface.h"

#include <math.h>
#include <memory.h>

typedef struct vk_brush_model_surface_s {
	int texture_num;
	msurface_t *surf;

	// Offset into g_brush.index_buffer in vertices
	uint32_t index_offset;
	uint16_t index_count;
} vk_brush_model_surface_t;

typedef struct vk_brush_model_s {
	//model_t *model;

	// Offset into g_brush.vertex_buffer in vertices
	uint32_t vertex_offset;

	int num_surfaces;
	vk_brush_model_surface_t surfaces[];
} vk_brush_model_t;

static struct {
	struct {
		int num_vertices, num_indices;
	} stat;

	int rtable[MOD_FRAMES][MOD_FRAMES];
} g_brush;

void VK_InitRandomTable( void )
{
	int	tu, tv;

	// make random predictable
	gEngine.COM_SetRandomSeed( 255 );

	for( tu = 0; tu < MOD_FRAMES; tu++ )
	{
		for( tv = 0; tv < MOD_FRAMES; tv++ )
		{
			g_brush.rtable[tu][tv] = gEngine.COM_RandomLong( 0, 0x7FFF );
		}
	}

	gEngine.COM_SetRandomSeed( 0 );
}

qboolean VK_BrushInit( void )
{
	VK_InitRandomTable ();

	return true;
}

void VK_BrushShutdown( void )
{
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and surface
===============
*/
texture_t *R_TextureAnimation( const cl_entity_t *ent, msurface_t *s )
{
	texture_t	*base = s->texinfo->texture;
	int	count, reletive;

	if( ent && ent->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;

	if( base->name[0] == '-' )
	{
		int	tx = (int)((s->texturemins[0] + (base->width << 16)) / base->width) % MOD_FRAMES;
		int	ty = (int)((s->texturemins[1] + (base->height << 16)) / base->height) % MOD_FRAMES;

		reletive = g_brush.rtable[tx][ty] % base->anim_total;
	}
	else
	{
		int	speed;

		// Quake1 textures uses 10 frames per second
		if( FBitSet( findTexture( base->gl_texturenum )->flags, TF_QUAKEPAL ))
			speed = 10;
		else speed = 20;

		reletive = (int)(gpGlobals->time * speed) % base->anim_total;
	}

	count = 0;

	while( base->anim_min > reletive || base->anim_max <= reletive )
	{
		base = base->anim_next;

		if( !base || ++count > MOD_FRAMES )
			return s->texinfo->texture;
	}

	return base;
}

void VK_BrushDrawModel( const cl_entity_t *ent, int render_mode, int ubo_index )
{
	// Expect all buffers to be bound
	const model_t *mod = ent->model;
	const vk_brush_model_t *bmodel = mod->cache.data;
	int current_texture = -1;
	int index_count = 0;
	int index_offset = -1;

	if (!bmodel) {
		gEngine.Con_Printf( S_ERROR "Model %s wasn't loaded\n", mod->name);
		return;
	}

	if (vk_core.debug) {
		VkDebugUtilsLabelEXT label = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			.pLabelName = mod->name,
		};
		vkCmdBeginDebugUtilsLabelEXT(vk_core.cb, &label);
	}

	for (int i = 0; i < bmodel->num_surfaces; ++i) {
		const vk_brush_model_surface_t *bsurf = bmodel->surfaces + i;
		texture_t *t = R_TextureAnimation(ent, bsurf->surf);

		if (t->gl_texturenum < 0)
			continue;

		if (current_texture != t->gl_texturenum)
		{
			if (index_count) {
				const render_draw_t draw = {
					.ubo_index = ubo_index,
					.lightmap = tglob.lightmapTextures[0],
					.texture = current_texture,
					.render_mode = render_mode,
					.element_count = index_count,
					.vertex_offset = bmodel->vertex_offset,
					.index_offset = index_offset,
				};

				VK_RenderDraw( &draw );
			}

			current_texture = t->gl_texturenum;
			index_count = 0;
			index_offset = -1;
		}

		if (index_offset < 0)
			index_offset = bsurf->index_offset;
		// Make sure that all surfaces are concatenated in buffers
		ASSERT(index_offset + index_count == bsurf->index_offset);
		index_count += bsurf->index_count;
	}

	if (index_count) {
		const render_draw_t draw = {
			.ubo_index = ubo_index,
			.lightmap = tglob.lightmapTextures[0],
			.texture = current_texture,
			.render_mode = render_mode,
			.element_count = index_count,
			.vertex_offset = bmodel->vertex_offset,
			.index_offset = index_offset,
		};

		VK_RenderDraw( &draw );
	}

	if (vk_core.debug)
		vkCmdEndDebugUtilsLabelEXT(vk_core.cb);
}

qboolean VK_BrushRenderBegin( void )
{
	if (!tglob.lightmapTextures[0])
	{
		gEngine.Con_Printf( S_ERROR "Don't have a lightmap texture\n");
		return false;
	}

	return true;
}

static int loadBrushSurfaces( const model_t *mod, vk_brush_model_surface_t *out_surfaces) {
	vk_brush_model_t *bmodel = mod->cache.data;
	uint32_t vertex_offset = 0;
	int num_surfaces = 0;
	vk_buffer_alloc_t vertex_alloc, index_alloc;
	brush_vertex_t *bvert = NULL;
	uint16_t *bind = NULL;
	uint32_t index_offset = 0;

	int num_indices = 0, num_vertices = 0, max_texture_id = 0;
	for( int i = 0; i < mod->nummodelsurfaces; ++i)
	{
		const msurface_t *surf = mod->surfaces + mod->firstmodelsurface + i;
		vk_brush_model_surface_t *bsurf = out_surfaces + num_surfaces;

		if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) ) {
			gEngine.Con_Reportf("Skipping surface %d because of flags %08x\n", i, surf->flags);
			continue;
		}

		if( FBitSet( surf->flags, SURF_DRAWTILED )) {
			gEngine.Con_Reportf("Skipping surface %d because of tiled flag\n", i);
			continue;
		}

		num_vertices += surf->numedges;
		num_indices += 3 * (surf->numedges - 1);
		if (surf->texinfo->texture->gl_texturenum > max_texture_id)
			max_texture_id = surf->texinfo->texture->gl_texturenum;
	}

	vertex_alloc = VK_RenderBufferAlloc( sizeof(brush_vertex_t), num_vertices );
	bvert = vertex_alloc.ptr;
	bmodel->vertex_offset = vertex_alloc.buffer_offset_in_units;
	if (!bvert)
	{
		gEngine.Con_Printf(S_ERROR "Ran out of buffer vertex space\n");
		return -1;
	}

	index_alloc = VK_RenderBufferAlloc( sizeof(uint16_t), num_indices );
	bind = index_alloc.ptr;
	index_offset = index_alloc.buffer_offset_in_units;
	if (!bind)
	{
		gEngine.Con_Printf(S_ERROR "Ran out of buffer index space\n");
		return -1;
	}

	g_brush.stat.num_indices += num_indices;
	g_brush.stat.num_vertices += num_vertices;

	// Load sorted by gl_texturenum
	for (int t = 0; t <= max_texture_id; ++t)
	{
		for( int i = 0; i < mod->nummodelsurfaces; ++i)
		{
			msurface_t *surf = mod->surfaces + mod->firstmodelsurface + i;
			vk_brush_model_surface_t *bsurf = out_surfaces + num_surfaces;
			mextrasurf_t	*info = surf->info;
			const float sample_size = gEngine.Mod_SampleSizeForFace( surf );

			if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) )
				continue;

			if( FBitSet( surf->flags, SURF_DRAWTILED ))
				continue;

			if (t != surf->texinfo->texture->gl_texturenum)
				continue;

			++num_surfaces;

			//gEngine.Con_Reportf( "surface %d: numverts=%d numedges=%d\n", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );


			if (vertex_offset + surf->numedges >= UINT16_MAX)
			{
				gEngine.Con_Printf(S_ERROR "Model %s indices don't fit into 16 bits\n", mod->name);
				return -1;
			}

			bsurf->surf = surf;
			bsurf->texture_num = surf->texinfo->texture->gl_texturenum;
			bsurf->index_offset = index_offset;
			bsurf->index_count = 0;

			VK_CreateSurfaceLightmap( surf, mod );

			for( int k = 0; k < surf->numedges; k++ )
			{
				const int iedge = mod->surfedges[surf->firstedge + k];
				const medge_t *edge = mod->edges + (iedge >= 0 ? iedge : -iedge);
				const mvertex_t *in_vertex = mod->vertexes + (iedge >= 0 ? edge->v[0] : edge->v[1]);
				brush_vertex_t vertex = {
					{in_vertex->position[0], in_vertex->position[1], in_vertex->position[2]},
				};

				float s = DotProduct( in_vertex->position, surf->texinfo->vecs[0] ) + surf->texinfo->vecs[0][3];
				float t = DotProduct( in_vertex->position, surf->texinfo->vecs[1] ) + surf->texinfo->vecs[1][3];

				s /= surf->texinfo->texture->width;
				t /= surf->texinfo->texture->height;

				vertex.gl_tc[0] = s;
				vertex.gl_tc[1] = t;

				// lightmap texture coordinates
				s = DotProduct( in_vertex->position, info->lmvecs[0] ) + info->lmvecs[0][3];
				s -= info->lightmapmins[0];
				s += surf->light_s * sample_size;
				s += sample_size * 0.5f;
				s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

				t = DotProduct( in_vertex->position, info->lmvecs[1] ) + info->lmvecs[1][3];
				t -= info->lightmapmins[1];
				t += surf->light_t * sample_size;
				t += sample_size * 0.5f;
				t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

				vertex.lm_tc[0] = s;
				vertex.lm_tc[1] = t;

				//gEngine.Con_Printf("VERT %u %f %f %f\n", g_brush.num_vertices, in_vertex->position[0], in_vertex->position[1], in_vertex->position[2]);

				*(bvert++) = vertex;

				// TODO contemplate triangle_strip (or fan?) + primitive restart
				if (k > 1) {
					*(bind++) = (uint16_t)(vertex_offset + 0);
					*(bind++) = (uint16_t)(vertex_offset + k - 1);
					*(bind++) = (uint16_t)(vertex_offset + k);
					bsurf->index_count += 3;
					index_offset += 3;
				}
			}

			vertex_offset += surf->numedges;
		}
	}

	return num_surfaces;
}

qboolean VK_LoadBrushModel( model_t *mod, const byte *buffer )
{
	vk_brush_model_t *bmodel;

	if (mod->cache.data)
	{
		gEngine.Con_Reportf( S_WARN "Model %s was already loaded\n", mod->name );
		return true;
	}

	gEngine.Con_Reportf("%s: %s flags=%08x\n", __FUNCTION__, mod->name, mod->flags);

	bmodel = Mem_Malloc(vk_core.pool, sizeof(vk_brush_model_t) + sizeof(vk_brush_model_surface_t) * mod->nummodelsurfaces);
	mod->cache.data = bmodel;

	bmodel->num_surfaces = loadBrushSurfaces( mod, bmodel->surfaces );
	if (bmodel->num_surfaces < 0) {
		gEngine.Con_Reportf( S_ERROR "Model %s was not loaded\n", mod->name );
		return false;
	}

	/* gEngine.Con_Reportf("Model %s, vertex_offset=%d, first surface index_offset=%d\n", */
	/* 		mod->name, */
	/* 		bmodel->vertex_offset, bmodel->num_surfaces ? bmodel->surfaces[0].index_offset : -1); */
	/* for (int i = 0; i < bmodel->num_surfaces; ++i) */
	/* 	gEngine.Con_Reportf("\t%d: tex=%d, off=%d, cnt=%d\n", i, */
	/* 			bmodel->surfaces[i].texture_num, */
	/* 			bmodel->surfaces[i].index_offset, */
	/* 			bmodel->surfaces[i].index_count); */

	gEngine.Con_Reportf("Model %s loaded surfaces: %d (of %d); total vertices: %u, total indices: %u\n", mod->name, bmodel->num_surfaces, mod->nummodelsurfaces, g_brush.stat.num_vertices, g_brush.stat.num_indices);
	return true;
}

void VK_BrushClear( void )
{
	// Free previous map data
	g_brush.stat.num_vertices = 0;
	g_brush.stat.num_indices = 0;
}
