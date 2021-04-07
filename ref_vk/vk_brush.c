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

typedef struct vk_brush_model_s {
	vk_render_model_t render_model;

	// Surfaces for getting animated textures.
	// Each surface corresponds to a single geometry within render_model with the same index.
	msurface_t *surf[];
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

void VK_BrushModelDraw( const cl_entity_t *ent, int render_mode )
{
	// Expect all buffers to be bound
	const model_t *mod = ent->model;
	vk_brush_model_t *bmodel = mod->cache.data;

	if (!bmodel) {
		gEngine.Con_Printf( S_ERROR "Model %s wasn't loaded\n", mod->name);
		return;
	}

	if (!tglob.lightmapTextures[0])
	{
		gEngine.Con_Printf( S_ERROR "Don't have a lightmap texture\n");
		return;
	}

	for (int i = 0; i < bmodel->render_model.num_geometries; ++i) {
		texture_t *t = R_TextureAnimation(ent, bmodel->surf[i]);
		if (t->gl_texturenum < 0)
			continue;

		bmodel->render_model.geometries[i].texture = t->gl_texturenum;
	}

	bmodel->render_model.render_mode = render_mode;
	VK_RenderModelDraw(&bmodel->render_model);
}

static qboolean renderableSurface( const msurface_t *surf, int i ) {
	if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) ) {
		gEngine.Con_Reportf("Skipping surface %d because of flags %08x\n", i, surf->flags);
		return false;
	}

	if( FBitSet( surf->flags, SURF_DRAWTILED )) {
		gEngine.Con_Reportf("Skipping surface %d because of tiled flag\n", i);
		return false;
	}

	return true;
}

typedef struct {
	int num_surfaces, num_vertices, num_indices;
	int max_texture_id;
} model_sizes_t;

static model_sizes_t computeSizes( const model_t *mod ) {
	model_sizes_t sizes = {0};

	for( int i = 0; i < mod->nummodelsurfaces; ++i)
	{
		const msurface_t *surf = mod->surfaces + mod->firstmodelsurface + i;

		if (!renderableSurface(surf, i))
			continue;

		++sizes.num_surfaces;
		sizes.num_vertices += surf->numedges;
		sizes.num_indices += 3 * (surf->numedges - 1);
		if (surf->texinfo->texture->gl_texturenum > sizes.max_texture_id)
			sizes.max_texture_id = surf->texinfo->texture->gl_texturenum;
	}

	return sizes;
}

static qboolean loadBrushSurfaces( model_sizes_t sizes, const model_t *mod ) {
	vk_brush_model_t *bmodel = mod->cache.data;
	uint32_t vertex_offset = 0;
	int num_surfaces = 0;
	vk_buffer_handle_t vertex_buffer, index_buffer;
	vk_buffer_lock_t vertex_lock, index_lock;
	vk_vertex_t *bvert = NULL;
	uint16_t *bind = NULL;
	uint32_t index_offset = 0;

	vertex_buffer = VK_RenderBufferAlloc( sizeof(vk_vertex_t), sizes.num_vertices, LifetimeMap );
	index_buffer = VK_RenderBufferAlloc( sizeof(uint16_t), sizes.num_indices, LifetimeMap );
	if (vertex_buffer == InvalidHandle || index_buffer == InvalidHandle)
	{
		// TODO should we free one of the above if it still succeeded?
		gEngine.Con_Printf(S_ERROR "Ran out of buffer space\n");
		return false;
	}

	vertex_lock = VK_RenderBufferLock( vertex_buffer );
	index_lock = VK_RenderBufferLock( index_buffer );
	bvert = vertex_lock.ptr;
	bind = index_lock.ptr;

	// Load sorted by gl_texturenum
	for (int t = 0; t <= sizes.max_texture_id; ++t)
	{
		for( int i = 0; i < mod->nummodelsurfaces; ++i)
		{
			msurface_t *surf = mod->surfaces + mod->firstmodelsurface + i;
			mextrasurf_t	*info = surf->info;
			vk_render_geometry_t *model_geometry = bmodel->render_model.geometries + num_surfaces;
			const float sample_size = gEngine.Mod_SampleSizeForFace( surf );
			int index_count = 0;

			if (!renderableSurface(surf, i))
				continue;

			if (t != surf->texinfo->texture->gl_texturenum)
				continue;

			bmodel->surf[num_surfaces] = surf;

			++num_surfaces;

			//gEngine.Con_Reportf( "surface %d: numverts=%d numedges=%d\n", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );

			if (vertex_offset + surf->numedges >= UINT16_MAX)
			{
				gEngine.Con_Printf(S_ERROR "Model %s indices don't fit into 16 bits\n", mod->name);
				// FIXME unlock and free buffers
				return false;
			}

			model_geometry->index_offset = index_offset;
			model_geometry->vertex_offset = 0;
			model_geometry->texture = t;

			VK_CreateSurfaceLightmap( surf, mod );

			for( int k = 0; k < surf->numedges; k++ )
			{
				const int iedge = mod->surfedges[surf->firstedge + k];
				const medge_t *edge = mod->edges + (iedge >= 0 ? iedge : -iedge);
				const mvertex_t *in_vertex = mod->vertexes + (iedge >= 0 ? edge->v[0] : edge->v[1]);
				vk_vertex_t vertex = {
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

				if( FBitSet( surf->flags, SURF_PLANEBACK ))
					VectorNegate( surf->plane->normal, vertex.normal );
				else VectorCopy( surf->plane->normal, vertex.normal );

				vertex.lm_tc[0] = s;
				vertex.lm_tc[1] = t;

				*(bvert++) = vertex;

				// TODO contemplate triangle_strip (or fan?) + primitive restart
				if (k > 1) {
					*(bind++) = (uint16_t)(vertex_offset + 0);
					*(bind++) = (uint16_t)(vertex_offset + k - 1);
					*(bind++) = (uint16_t)(vertex_offset + k);
					index_count += 3;
					index_offset += 3;
				}
			}

			model_geometry->element_count = index_count;
			vertex_offset += surf->numedges;
		}
	}

	VK_RenderBufferUnlock( index_buffer );
	VK_RenderBufferUnlock( vertex_buffer );

	ASSERT(sizes.num_surfaces == num_surfaces);
	bmodel->render_model.num_geometries = num_surfaces;
	bmodel->render_model.index_buffer = index_buffer;
	bmodel->render_model.vertex_buffer = vertex_buffer;

	return num_surfaces;
}

qboolean VK_BrushModelLoad( model_t *mod )
{
	if (mod->cache.data)
	{
		gEngine.Con_Reportf( S_WARN "Model %s was already loaded\n", mod->name );
		return true;
	}

	gEngine.Con_Reportf("%s: %s flags=%08x\n", __FUNCTION__, mod->name, mod->flags);

	{
		const model_sizes_t sizes = computeSizes( mod );
		vk_brush_model_t *bmodel = Mem_Malloc(vk_core.pool, sizeof(vk_brush_model_t) + (sizeof(msurface_t*) + sizeof(vk_render_geometry_t)) * sizes.num_surfaces);
		mod->cache.data = bmodel;
		bmodel->render_model.debug_name = mod->name;
		bmodel->render_model.render_mode = kRenderNormal;
		bmodel->render_model.geometries = (vk_render_geometry_t*)((char*)(bmodel + 1) + sizeof(msurface_t*) * sizes.num_surfaces);

		if (!loadBrushSurfaces(sizes, mod) || !VK_RenderModelInit(&bmodel->render_model)) {
			gEngine.Con_Printf(S_ERROR "Could not load model %s\n", mod->name);
			Mem_Free(bmodel);
			return false;
		}

		g_brush.stat.num_indices += sizes.num_indices;
		g_brush.stat.num_vertices += sizes.num_vertices;

		gEngine.Con_Reportf("Model %s loaded surfaces: %d (of %d); total vertices: %u, total indices: %u\n", mod->name, bmodel->render_model.num_geometries, mod->nummodelsurfaces, g_brush.stat.num_vertices, g_brush.stat.num_indices);
	}

	return true;
}

void VK_BrushModelDestroy( model_t *mod ) {
	vk_brush_model_t *bmodel = mod->cache.data;
	if (!bmodel)
		return;

	VK_RenderModelDestroy(&bmodel->render_model);
	Mem_Free(bmodel);
	mod->cache.data = NULL;
}

void VK_BrushStatsClear( void )
{
	// Free previous map data
	g_brush.stat.num_vertices = 0;
	g_brush.stat.num_indices = 0;
}
