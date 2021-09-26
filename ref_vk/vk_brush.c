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
#include "vk_light.h"

#include "ref_params.h"
#include "eiface.h"

#include <math.h>
#include <memory.h>

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

// speed up sin calculations
static const float r_turbsin[] =
{
	#include "warpsin.h"
};

#define SUBDIVIDE_SIZE	64
#define TURBSCALE		( 256.0f / ( M_PI2 ))

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
static void EmitWaterPolys( const cl_entity_t *ent, const msurface_t *warp, qboolean reverse )
{
	const float time = gpGlobals->time;
	float	*v, nv, waveHeight;
	float	s, t, os, ot;
	glpoly_t	*p;
	int	i;
	int num_vertices = 0, num_indices = 0;
	xvk_render_buffer_allocation_t vertex_buffer, index_buffer = {0};
	int vertex_offset = 0;
	vk_vertex_t *gpu_vertices;
	uint16_t *indices;

#define MAX_WATER_VERTICES 16
	vk_vertex_t poly_vertices[MAX_WATER_VERTICES];

	const qboolean useQuads = FBitSet( warp->flags, SURF_DRAWTURB_QUADS );

	if( !warp->polys ) return;

	// set the current waveheight
	// FIXME VK if( warp->polys->verts[0][2] >= RI.vieworg[2] )
	// 	waveHeight = -ent->curstate.scale;
	// else
	waveHeight = ent->curstate.scale;

	// reset fog color for nonlightmapped water
	// FIXME VK GL_ResetFogColor();

	// Compute vertex count
	for( p = warp->polys; p; p = p->next ) {
		const int triangles = p->numverts - 2;
		num_vertices += p->numverts;
		num_indices += triangles * 3;
	}

	vertex_buffer = XVK_RenderBufferAllocAndLock( sizeof(vk_vertex_t), num_vertices );
	index_buffer = XVK_RenderBufferAllocAndLock( sizeof(uint16_t), num_indices );
	if (vertex_buffer.ptr == NULL || index_buffer.ptr == NULL)
	{
		// TODO should we free one of the above if it still succeeded?
		gEngine.Con_Printf(S_ERROR "Ran out of buffer space\n");
		return;
	}

	gpu_vertices = vertex_buffer.ptr;
	indices = index_buffer.ptr;

	for( p = warp->polys; p; p = p->next )
	{
		ASSERT(p->numverts <= MAX_WATER_VERTICES);

		if( reverse )
			v = p->verts[0] + ( p->numverts - 1 ) * VERTEXSIZE;
		else v = p->verts[0];

		for( i = 0; i < p->numverts; i++ )
		{
			if( waveHeight )
			{
				nv = r_turbsin[(int)(time * 160.0f + v[1] + v[0]) & 255] + 8.0f;
				nv = (r_turbsin[(int)(v[0] * 5.0f + time * 171.0f - v[1]) & 255] + 8.0f ) * 0.8f + nv;
				nv = nv * waveHeight + v[2];
			}
			else nv = v[2];

			os = v[3];
			ot = v[4];

			s = os + r_turbsin[(int)((ot * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			s *= ( 1.0f / SUBDIVIDE_SIZE );

			t = ot + r_turbsin[(int)((os * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			t *= ( 1.0f / SUBDIVIDE_SIZE );

			poly_vertices[i].pos[0] = v[0];
			poly_vertices[i].pos[1] = v[1];
			poly_vertices[i].pos[2] = nv;

			poly_vertices[i].gl_tc[0] = s;
			poly_vertices[i].gl_tc[1] = t;

			poly_vertices[i].lm_tc[0] = 0;
			poly_vertices[i].lm_tc[1] = 0;

#define WATER_NORMALS
			poly_vertices[i].normal[0] = 0;
			poly_vertices[i].normal[1] = 0;
#ifdef WATER_NORMALS
			poly_vertices[i].normal[2] = 0;
#else
			poly_vertices[i].normal[2] = 1;
#endif

			// Ray tracing apparently expects triangle list only (although spec is not very clear about this kekw)
			if (i > 1) {
#ifdef WATER_NORMALS
				vec3_t e0, e1, normal;
				VectorSubtract( poly_vertices[i - 1].pos, poly_vertices[0].pos, e0 );
				VectorSubtract( poly_vertices[i].pos, poly_vertices[0].pos, e1 );
				CrossProduct( e1, e0, normal );
				//VectorNormalize(normal);

				VectorAdd(normal, poly_vertices[0].normal, poly_vertices[0].normal);
				VectorAdd(normal, poly_vertices[i].normal, poly_vertices[i].normal);
				VectorAdd(normal, poly_vertices[i - 1].normal, poly_vertices[i - 1].normal);
#endif
				*(indices++) = (uint16_t)(vertex_offset);
				*(indices++) = (uint16_t)(vertex_offset + i - 1);
				*(indices++) = (uint16_t)(vertex_offset + i);
			}

			if( reverse )
				v -= VERTEXSIZE;
			else
				v += VERTEXSIZE;
		}

#ifdef WATER_NORMALS
		for( i = 0; i < p->numverts; i++ ) {
			VectorNormalize(poly_vertices[i].normal);
		}
#endif

		memcpy(gpu_vertices + vertex_offset, poly_vertices, sizeof(vk_vertex_t) * p->numverts);
		vertex_offset += p->numverts;
	}

	XVK_RenderBufferUnlock( vertex_buffer.buffer );
	XVK_RenderBufferUnlock( index_buffer.buffer );

	// Render
	{
		const vk_render_geometry_t geometry = {
			.texture = warp->texinfo->texture->gl_texturenum, // FIXME assert >= 0
			.material = kXVkMaterialWater,
			.surf = warp,

			.max_vertex = num_vertices,
			.vertex_offset = vertex_buffer.buffer.unit.offset,

			.element_count = num_indices,
			.index_offset = index_buffer.buffer.unit.offset,
		};

		VK_RenderModelDynamicAddGeometry( &geometry );
	}

	// FIXME VK GL_SetupFogColorForSurfaces();
}

void XVK_DrawWaterSurfaces( const cl_entity_t *ent )
{
	const model_t *model = ent->model;
	vec3_t		mins, maxs;

	if( !VectorIsNull( ent->angles ))
	{
		for( int i = 0; i < 3; i++ )
		{
			mins[i] = ent->origin[i] - model->radius;
			maxs[i] = ent->origin[i] + model->radius;
		}
		//rotated = true;
	}
	else
	{
		VectorAdd( ent->origin, model->mins, mins );
		VectorAdd( ent->origin, model->maxs, maxs );
		//rotated = false;
	}

	// if( R_CullBox( mins, maxs ))
	// 	return;

	VK_RenderModelDynamicBegin( ent->curstate.rendermode, "%s water", model->name );

	// Iterate through all surfaces, find *TURB*
	for( int i = 0; i < model->nummodelsurfaces; i++ )
	{
		const msurface_t *surf = model->surfaces + model->firstmodelsurface + i;

		if( !FBitSet( surf->flags, SURF_DRAWTURB ) && !FBitSet( surf->flags, SURF_DRAWTURB_QUADS) )
			continue;

		if( surf->plane->type != PLANE_Z && !FBitSet( ent->curstate.effects, EF_WATERSIDES ))
			continue;

		if( mins[2] + 1.0f >= surf->plane->dist )
			continue;

		EmitWaterPolys( ent, surf, false );
	}

	// submit as dynamic model
	VK_RenderModelDynamicCommit();

	// TODO:
	// - upload water geometry only once, animate in compute/vertex shader
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and surface
===============
*/
texture_t *R_TextureAnimation( const cl_entity_t *ent, const msurface_t *s )
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

	if (bmodel->num_water_surfaces) {
		XVK_DrawWaterSurfaces(ent);
	}

	if (bmodel->render_model.num_geometries == 0)
		return;

	for (int i = 0; i < bmodel->render_model.num_geometries; ++i) {
		texture_t *t = R_TextureAnimation(ent, bmodel->render_model.geometries[i].surf);
		if (t->gl_texturenum < 0)
			continue;

		bmodel->render_model.geometries[i].texture = t->gl_texturenum;
	}

	bmodel->render_model.render_mode = render_mode;
	VK_RenderModelDraw(&bmodel->render_model);
}

static qboolean renderableSurface( const msurface_t *surf, int i ) {
// 	if ( i >= 0 && (surf->flags & ~(SURF_PLANEBACK | SURF_UNDERWATER | SURF_TRANSPARENT)) != 0)
// 	{
// 		gEngine.Con_Reportf("\t%d flags: ", i);
// #define PRINTFLAGS(X) \
// 	X(SURF_PLANEBACK) \
// 	X(SURF_DRAWSKY) \
// 	X(SURF_DRAWTURB_QUADS) \
// 	X(SURF_DRAWTURB) \
// 	X(SURF_DRAWTILED) \
// 	X(SURF_CONVEYOR) \
// 	X(SURF_UNDERWATER) \
// 	X(SURF_TRANSPARENT)

// #define PRINTFLAG(f) if (FBitSet(surf->flags, f)) gEngine.Con_Reportf(" %s", #f);
// 		PRINTFLAGS(PRINTFLAG)
// 		gEngine.Con_Reportf("\n");
// 	}

	//if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) ) {
	if( surf->flags & ( SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) ) {
	//if( surf->flags & ( SURF_DRAWSKY | SURF_CONVEYOR ) ) {
		// FIXME don't print this on second sort-by-texture pass
		//gEngine.Con_Reportf("Skipping surface %d because of flags %08x\n", i, surf->flags);
		return false;
	}

	if( FBitSet( surf->flags, SURF_DRAWSKY )) {
		return false;
	}

	if( FBitSet( surf->flags, SURF_DRAWTILED )) {
		//gEngine.Con_Reportf("Skipping surface %d because of tiled flag\n", i);
		return false;
	}

	return true;
}

typedef struct {
	int num_surfaces, num_vertices, num_indices;
	int max_texture_id;
	int water_surfaces;
	//int sky_surfaces;
} model_sizes_t;

static model_sizes_t computeSizes( const model_t *mod ) {
	model_sizes_t sizes = {0};

	for( int i = 0; i < mod->nummodelsurfaces; ++i)
	{
		const msurface_t *surf = mod->surfaces + mod->firstmodelsurface + i;

		sizes.water_surfaces += !!(surf->flags & (SURF_DRAWTURB | SURF_DRAWTURB_QUADS));
		//sizes.sky_surfaces += !!(surf->flags & SURF_DRAWSKY);

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
	int num_geometries = 0;
	xvk_render_buffer_allocation_t vertex_buffer, index_buffer;
	vk_vertex_t *bvert = NULL;
	uint16_t *bind = NULL;
	uint32_t index_offset = 0;

	vertex_buffer = XVK_RenderBufferAllocAndLock( sizeof(vk_vertex_t), sizes.num_vertices );
	index_buffer = XVK_RenderBufferAllocAndLock( sizeof(uint16_t), sizes.num_indices );
	if (vertex_buffer.ptr == NULL || index_buffer.ptr == NULL) {
		gEngine.Con_Printf(S_ERROR "Ran out of buffer space\n");
		return false;
	}

	bvert = vertex_buffer.ptr;
	bind = index_buffer.ptr;

	index_offset = index_buffer.buffer.unit.offset;

	// Load sorted by gl_texturenum
	for (int t = 0; t <= sizes.max_texture_id; ++t)
	{
		for( int i = 0; i < mod->nummodelsurfaces; ++i)
		{
			const int surface_index = mod->firstmodelsurface + i;
			msurface_t *surf = mod->surfaces + surface_index;
			mextrasurf_t	*info = surf->info;
			vk_render_geometry_t *model_geometry = bmodel->render_model.geometries + num_geometries;
			const float sample_size = gEngine.Mod_SampleSizeForFace( surf );
			int index_count = 0;

			if (!renderableSurface(surf, -1))
				continue;

			if (t != surf->texinfo->texture->gl_texturenum)
				continue;

			++num_geometries;

			//gEngine.Con_Reportf( "surface %d: numverts=%d numedges=%d\n", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );

			if (vertex_offset + surf->numedges >= UINT16_MAX)
			{
				gEngine.Con_Printf(S_ERROR "Model %s indices don't fit into 16 bits\n", mod->name);
				// FIXME unlock and free buffers
				return false;
			}

			model_geometry->surf = surf;
			model_geometry->texture = surf->texinfo->texture->gl_texturenum;

			model_geometry->vertex_offset = vertex_buffer.buffer.unit.offset;
			model_geometry->max_vertex = vertex_offset + surf->numedges;

			model_geometry->index_offset = index_offset;

			if( FBitSet( surf->flags, SURF_DRAWSKY )) {
				model_geometry->material = kXVkMaterialSky;
			} else {
				model_geometry->material = kXVkMaterialRegular;
				VK_CreateSurfaceLightmap( surf, mod );
			}

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

				// Ray tracing apparently expects triangle list only (although spec is not very clear about this kekw)
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

	XVK_RenderBufferUnlock( index_buffer.buffer );
	XVK_RenderBufferUnlock( vertex_buffer.buffer );

	ASSERT(sizes.num_surfaces == num_geometries);
	bmodel->render_model.num_geometries = num_geometries;

	return true;
}

qboolean VK_BrushModelLoad( model_t *mod, qboolean map )
{
	if (mod->cache.data)
	{
		gEngine.Con_Reportf( S_WARN "Model %s was already loaded\n", mod->name );
		return true;
	}

	gEngine.Con_Reportf("%s: %s flags=%08x\n", __FUNCTION__, mod->name, mod->flags);

	{
		const model_sizes_t sizes = computeSizes( mod );
		const size_t model_size =
			sizeof(vk_brush_model_t) +
			sizeof(vk_render_geometry_t) * sizes.num_surfaces;

		vk_brush_model_t *bmodel = Mem_Calloc(vk_core.pool, model_size);
		mod->cache.data = bmodel;
		Q_strncpy(bmodel->render_model.debug_name, mod->name, sizeof(bmodel->render_model.debug_name));
		bmodel->render_model.render_mode = kRenderNormal;
		bmodel->render_model.static_map = map;

		bmodel->num_water_surfaces = sizes.water_surfaces;

		if (sizes.num_surfaces != 0) {
			bmodel->render_model.geometries = (vk_render_geometry_t*)((char*)(bmodel + 1));

			if (!loadBrushSurfaces(sizes, mod) || !VK_RenderModelInit(&bmodel->render_model)) {
				gEngine.Con_Printf(S_ERROR "Could not load model %s\n", mod->name);
				Mem_Free(bmodel);
				return false;
			}
		}

		g_brush.stat.num_indices += sizes.num_indices;
		g_brush.stat.num_vertices += sizes.num_vertices;

		gEngine.Con_Reportf("Model %s loaded surfaces: %d (of %d); total vertices: %u, total indices: %u\n", mod->name, bmodel->render_model.num_geometries, mod->nummodelsurfaces, g_brush.stat.num_vertices, g_brush.stat.num_indices);
	}

	return true;
}

void VK_BrushModelDestroy( model_t *mod ) {
	vk_brush_model_t *bmodel = mod->cache.data;
	ASSERT(mod->type == mod_brush);
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
