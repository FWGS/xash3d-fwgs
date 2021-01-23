#include "vk_core.h"
#include "vk_buffer.h"

typedef struct map_vertex_s
{
	vec3_t pos;
	/* vec2_t gl_tc; */
	/* vec2_t lm_tc; */
} map_vertex_t;

static struct {
	// TODO merge these into a single buffer
	vk_buffer_t vertex_buffer;
	uint32_t num_vertices;

	vk_buffer_t index_buffer;
	uint32_t num_indices;
} gmap;

qboolean VK_MapInit( void )
{
	const uint32_t vertex_buffer_size = MAX_MAP_VERTS * sizeof(map_vertex_t);
	const uint32_t index_buffer_size = MAX_MAP_VERTS * sizeof(uint16_t) * 3; // TODO count this properly

	// TODO device memory and friends (e.g. handle mobile memory ...)

	if (!createBuffer(&gmap.vertex_buffer, vertex_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createBuffer(&gmap.index_buffer, index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	return true;
}

void VK_MapShutdown( void )
{
	destroyBuffer( &gmap.vertex_buffer );
	destroyBuffer( &gmap.index_buffer );
}

// tell the renderer what new map is started
void R_NewMap( void )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);

	const model_t *world = gEngine.pfnGetModelByIndex(1);
	map_vertex_t *bvert = gmap.vertex_buffer.mapped;
	uint32_t *bind = gmap.index_buffer.mapped;

	// Free previous map data
	gmap.num_vertices = 0;
	gmap.num_indices = 0;

	for( int i = 0; i < world->numsurfaces; ++i)
	{
		const uint16_t first_vertex_index = gmap.num_vertices;
		const msurface_t *surf = world->surfaces + i;

		if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) )
			continue;

		//gEngine.Con_Reportf( "surface %d: numverts=%d numedges=%d\n", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );

		if (surf->numedges + gmap.num_vertices > MAX_MAP_VERTS)
		{
			gEngine.Con_Printf(S_ERROR "Ran out of buffer vertex space\n");
			break;
		}

		for( int k = 0; k < surf->numedges; k++ )
		{
			const int iedge = world->surfedges[surf->firstedge + k];
			const medge_t *edge = world->edges + (iedge >= 0 ? iedge : -iedge);
			const mvertex_t *vertex = world->vertexes + (iedge >= 0 ? edge->v[0] : edge->v[1]);

			bvert[gmap.num_vertices++] = (map_vertex_t){
				{vertex->position[0], vertex->position[1], vertex->position[2]}
			};

			// TODO contemplate triangle_strip (or fan?) + primitive restart
			if (k > 1) {
				ASSERT(gmap.num_indices < (MAX_MAP_VERTS * 3 - 3));
				ASSERT(first_vertex_index + k < UINT16_MAX);
				bind[gmap.num_indices++] = (uint16_t)(first_vertex_index + 0);
				bind[gmap.num_indices++] = (uint16_t)(first_vertex_index + k);
				bind[gmap.num_indices++] = (uint16_t)(first_vertex_index + k - 1);
			}
		}
	}

	gEngine.Con_Reportf("Loaded surfaces: %d, vertices: %u\n, indices: %u", world->numsurfaces, gmap.num_vertices, gmap.num_indices);
}

void R_RenderScene( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
