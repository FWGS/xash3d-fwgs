#include "vk_light.h"
#include "vk_buffer.h"
#include "vk_mapents.h"
#include "vk_textures.h"
#include "vk_lightmap.h"
#include "vk_cvar.h"
#include "vk_common.h"
#include "shaders/ray_interop.h"
#include "bitarray.h"
#include "profiler.h"
#include "vk_staging.h"
#include "r_speeds.h"

#include "mod_local.h"
#include "xash3d_mathlib.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h> // isalnum...

#include "camera.h"
#include "pm_defs.h"
#include "pmtrace.h"

#define PROFILER_SCOPES(X) \
	X(finalize , "RT_LightsFrameEnd"); \
	X(emissive_surface, "VK_LightsAddEmissiveSurface"); \
	X(static_lights, "add static lights"); \
	X(dlights, "add dlights"); \
	//X(canSurfaceLightAffectAABB, "canSurfaceLightAffectAABB"); \

#define SCOPE_DECLARE(scope, name) APROF_SCOPE_DECLARE(scope)
PROFILER_SCOPES(SCOPE_DECLARE)
#undef SCOPE_DECLARE

typedef struct {
	vec3_t emissive;
	qboolean set;
} vk_emissive_texture_t;

static struct {
	struct {
		vk_emissive_texture_t emissive_textures[MAX_TEXTURES];
	} map;

	vk_buffer_t buffer;

	int num_polygons;
	rt_light_polygon_t polygons[MAX_SURFACE_LIGHTS];

	int num_point_lights;
	vk_point_light_t point_lights[MAX_POINT_LIGHTS];

	int num_polygon_vertices;
	vec3_t polygon_vertices[MAX_SURFACE_LIGHTS * 7];

	struct {
		int point_lights;
		int polygons;
		int polygon_vertices;
	} num_static;

	bit_array_t visited_cells;

	uint32_t frame_sequence;

	struct {
		r_speeds_metric_t *dirty_cells;
		r_speeds_metric_t *dirty_cells_size;
		r_speeds_metric_t *ranges_uploaded;
	} stats;
} g_lights_;

static struct {
	qboolean enabled;
	char name_filter[256];
} debug_dump_lights;

static void debugDumpLights( void ) {
	debug_dump_lights.enabled = true;
	if (gEngine.Cmd_Argc() > 1) {
		Q_strncpy(debug_dump_lights.name_filter, gEngine.Cmd_Argv(1), sizeof(debug_dump_lights.name_filter));
	} else {
		debug_dump_lights.name_filter[0] = '\0';
	}
}

vk_lights_t g_lights = {0};

qboolean VK_LightsInit( void ) {
	PROFILER_SCOPES(APROF_SCOPE_INIT);

	gEngine.Cmd_AddCommand("vk_lights_dump", debugDumpLights, "Dump all light sources for next frame");

	const int buffer_size = sizeof(struct LightsMetadata) + sizeof(struct LightCluster) * MAX_LIGHT_CLUSTERS;

	if (!VK_BufferCreate("rt lights buffer", &g_lights_.buffer, buffer_size,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
		// FIXME complain, handle
		return false;
	}

	g_lights_.stats.dirty_cells = R_SpeedsRegisterMetric("lights_dirty_cells", "");
	g_lights_.stats.dirty_cells_size = R_SpeedsRegisterMetric("lights_dirty_cells_size", "KiB");
	g_lights_.stats.ranges_uploaded = R_SpeedsRegisterMetric("lights_ranges_uploaded", "");

	return true;
}

void VK_LightsShutdown( void ) {
	VK_BufferDestroy(&g_lights_.buffer);

	gEngine.Cmd_RemoveCommand("vk_lights_dump");
	bitArrayDestroy(&g_lights_.visited_cells);
}

typedef struct {
	int num;
	int leafs[];
} vk_light_leaf_set_t;

typedef struct {
	vk_light_leaf_set_t *potentially_visible_leafs;
} vk_surface_metadata_t;

static struct {
	// Worldmodel surfaces
	int num_surfaces;
	vk_surface_metadata_t *surfaces;

	// Used for accumulating potentially visible leafs
	struct {
		int count;

		// This buffer space is used for two things:
		// As a growing array of u16 leaf indexes (low 16 bits)
		// As a bit field for marking added leafs (highest {31st} bit)
		uint32_t leafs[MAX_MAP_LEAFS];

		byte visbytes[(MAX_MAP_LEAFS+7)/8];
	} accum;

} g_lights_bsp = {0};

static void loadRadData( const model_t *map, const char *fmt, ... ) {
	fs_offset_t size;
	char *data;
	byte *buffer;
	char filename[1024];

	va_list argptr;
	va_start( argptr, fmt );
	vsnprintf( filename, sizeof filename, fmt, argptr );
	va_end( argptr );

	buffer = gEngine.fsapi->LoadFile( filename, &size, false);

	if (!buffer) {
		gEngine.Con_Printf(S_ERROR "Couldn't load RAD data from file %s, the map will be completely black\n", filename);
		return;
	}

	gEngine.Con_Reportf("Loading RAD data from file %s\n", filename);

	data = (char*)buffer;
	for (;;) {
		string name;
		float r=0, g=0, b=0, scale=0;
		int num;
		char* line_end;

		while (*data != '\0' && isspace(*data)) ++data;
		if (*data == '\0')
			break;

		line_end = Q_strchr(data, '\n');
		if (line_end) *line_end = '\0';

		name[0] = '\0';
		num = sscanf(data, "%s %f %f %f %f", name, &r, &g, &b, &scale);
		gEngine.Con_Printf("raw rad entry (%d): %s %f %f %f %f\n", num, name, r, g, b, scale);
		if (Q_strstr(name, "//") != NULL) {
			num = 0;
		}

		if (num == 2) {
			r = g = b;
		} else if (num == 5) {
			scale /= 255.f;
			r *= scale;
			g *= scale;
			b *= scale;
		} else if (num == 4) {
			// Ok, rgb only, no scaling
		} else {
			gEngine.Con_Printf( "skipping rad entry %s\n", name[0] ? name : "(empty)" );
			num = 0;
		}

		if (num != 0) {
			gEngine.Con_Printf("rad entry (%d): %s %f %f %f (%f)\n", num, name, r, g, b, scale);

			{
				const char *wad_name = NULL;
				char *texture_name = Q_strchr(name, '/');
				string texname;
				int tex_id;
				const qboolean enabled = (r != 0 || g != 0 || b != 0);

				if (!texture_name) {
					texture_name = name;
				} else {
					// name is now just a wad name
					texture_name[0] = '\0';
					wad_name = name;

					texture_name += 1;
				}

				// FIXME replace this with findTexturesNamedLike from vk_materials.c

				// Try bsp texture first
				tex_id = XVK_TextureLookupF("#%s:%s.mip", map->name, texture_name);

				// Try wad texture if bsp is not there
				if (!tex_id && wad_name) {
					tex_id = XVK_TextureLookupF("%s.wad/%s.mip", wad_name, texture_name);
				}

				if (!tex_id) {
					const char *wad = g_map_entities.wadlist;
					for (; *wad;) {
						const char *const wad_end = Q_strchr(wad, ';');
						tex_id = XVK_TextureLookupF("%.*s/%s.mip", wad_end - wad, wad, texture_name);
						if (tex_id)
							break;
						wad = wad_end + 1;
					}
				}

				if (tex_id) {
					vk_emissive_texture_t *const etex = g_lights_.map.emissive_textures + tex_id;
					ASSERT(tex_id < MAX_TEXTURES);

					etex->emissive[0] = r;
					etex->emissive[1] = g;
					etex->emissive[2] = b;
					etex->set = enabled;

					// See DIRECT_SCALE in qrad/lightmap.c
					VectorScale(etex->emissive, 0.1f, etex->emissive);

					if (!enabled)
						gEngine.Con_Reportf("rad entry %s disabled due to zero intensity\n", name);
				}
			}
		}

		if (!line_end)
			break;

		data = line_end + 1;
	}

	Mem_Free(buffer);
}

static void leafAccumPrepare( void ) {
	memset(&g_lights_bsp.accum, 0, sizeof(g_lights_bsp.accum));
}

#define LEAF_ADDED_BIT 0x8000000ul

static qboolean leafAccumAdd( uint16_t leaf_index ) {
	// Check whether this leaf was already added
	if (g_lights_bsp.accum.leafs[leaf_index] & LEAF_ADDED_BIT)
		return false;

	g_lights_bsp.accum.leafs[leaf_index] |= LEAF_ADDED_BIT;

	g_lights_bsp.accum.leafs[g_lights_bsp.accum.count++] |= leaf_index;
	return true;
}

static void leafAccumFinalize( void ) {
	for (int i = 0; i < g_lights_bsp.accum.count; ++i)
		g_lights_bsp.accum.leafs[i] &= 0xffffu;
}

static int leafAccumAddPotentiallyVisibleFromLeaf(const model_t *const map, const mleaf_t *leaf, qboolean print_debug) {
	int pvs_leaf_index = 0;
	int leafs_added = 0;
	ASSERT(leaf->compressed_vis);
	const byte *pvs = leaf->compressed_vis;
	for (;pvs_leaf_index < map->numleafs; ++pvs) {
		uint8_t bits = pvs[0];

		// PVS is RLE encoded
		if (bits == 0) {
			const int skip = pvs[1];
			pvs_leaf_index += skip * 8;
			++pvs;
			continue;
		}

		for (int k = 0; k < 8; ++k, ++pvs_leaf_index, bits >>= 1) {
			if ((bits&1) == 0)
				continue;

			if (leafAccumAdd( pvs_leaf_index + 1 )) {
				leafs_added++;
				if (print_debug)
					gEngine.Con_Reportf(" .%d", pvs_leaf_index + 1);
			}
		}
	}

	return leafs_added;
}

vk_light_leaf_set_t *getMapLeafsAffectedByMapSurface( const msurface_t *surf ) {
	const model_t *const map = gEngine.pfnGetModelByIndex( 1 );
	const int surf_index = surf - map->surfaces;
	vk_surface_metadata_t * const smeta = g_lights_bsp.surfaces + surf_index;
	const qboolean verbose_debug = false;

	if (surf_index < 0 || surf_index >= g_lights_bsp.num_surfaces) {
		gEngine.Con_Printf(S_ERROR "FIXME not implemented: attempting to add non-static polygon light\n");
		return NULL;
	}

	ASSERT(surf_index >= 0);
	ASSERT(surf_index < g_lights_bsp.num_surfaces);

	// Check if PVL hasn't been collected yet
	if (!smeta->potentially_visible_leafs) {
		int leafs_direct = 0, leafs_pvs = 0;
		leafAccumPrepare();

		// Enumerate all the map leafs and pick ones that have this surface referenced
		if (verbose_debug)
			gEngine.Con_Reportf("Collecting visible leafs for surface %d:", surf_index);
		for (int i = 1; i <= map->numleafs; ++i) {
			const mleaf_t *leaf = map->leafs + i;
			//if (verbose_debug) gEngine.Con_Reportf("    leaf %d(c%d)/%d:", i, leaf->cluster, map->numleafs);
			for (int j = 0; j < leaf->nummarksurfaces; ++j) {
				const msurface_t *leaf_surf = leaf->firstmarksurface[j];
				if (leaf_surf != surf) {
					/* if (verbose_debug) { */
					/* 	const int leaf_surf_index = leaf_surf - map->surfaces; */
					/* 	gEngine.Con_Reportf(" !%d", leaf_surf_index); */
					/* } */
					continue;
				}

				// FIXME split direct leafs marking from pvs propagation
				leafs_direct++;
				if (leafAccumAdd( i )) {
					if (verbose_debug) gEngine.Con_Reportf(" %d", i);
				} else {
					// This leaf was already added earlier by PVS
					// but it really should be counted as direct
					--leafs_pvs;
				}

				// Get all PVS leafs
				leafs_pvs += leafAccumAddPotentiallyVisibleFromLeaf(map, leaf, verbose_debug);
			}

			//if (verbose_debug) gEngine.Con_Reportf("\n");
		}
		if (verbose_debug)
			gEngine.Con_Reportf(" (sum=%d, direct=%d, pvs=%d)\n", g_lights_bsp.accum.count, leafs_direct, leafs_pvs);

		leafAccumFinalize();

		smeta->potentially_visible_leafs = (vk_light_leaf_set_t*)Mem_Malloc(vk_core.pool, sizeof(smeta->potentially_visible_leafs[0]) + sizeof(int) * g_lights_bsp.accum.count);
		smeta->potentially_visible_leafs->num = g_lights_bsp.accum.count;

		for (int i = 0; i < g_lights_bsp.accum.count; ++i) {
			smeta->potentially_visible_leafs->leafs[i] = g_lights_bsp.accum.leafs[i];
		}
	}

	return smeta->potentially_visible_leafs;
}

int RT_LightCellIndex( const int light_cell[3] ) {
	if (light_cell[0] < 0 || light_cell[1] < 0 || light_cell[2] < 0
		|| (light_cell[0] >= g_lights.map.grid_size[0])
		|| (light_cell[1] >= g_lights.map.grid_size[1])
		|| (light_cell[2] >= g_lights.map.grid_size[2]))
		return -1;

	return light_cell[0] + light_cell[1] * g_lights.map.grid_size[0] + light_cell[2] * g_lights.map.grid_size[0] * g_lights.map.grid_size[1];
}

vk_light_leaf_set_t *getMapLeafsAffectedByMovingSurface( const msurface_t *surf, const matrix3x4 *transform_row ) {
	const model_t *const map = gEngine.pfnGetModelByIndex( 1 );
	const mextrasurf_t *const extra = surf->info;

	// This is a very conservative way to construct a bounding sphere. It's not great.
	const vec3_t bbox_center = {
		(extra->mins[0] + extra->maxs[0]) / 2.f,
		(extra->mins[1] + extra->maxs[1]) / 2.f,
		(extra->mins[2] + extra->maxs[2]) / 2.f,
	};

	const vec3_t bbox_size = {
		extra->maxs[0] - extra->mins[0],
		extra->maxs[1] - extra->mins[1],
		extra->maxs[2] - extra->mins[2],
	};

	int leafs_direct = 0, leafs_pvs = 0;

	const float radius = .5f * VectorLength(bbox_size);
	vec3_t origin;

	Matrix3x4_VectorTransform(*transform_row, bbox_center, origin);

	if (debug_dump_lights.enabled) {
		gEngine.Con_Reportf("\torigin = %f, %f, %f, R = %f\n",
			origin[0], origin[1], origin[2], radius
		);
	}

	leafAccumPrepare();

	// TODO it's possible to somehow more efficiently traverse the bsp and collect only the affected leafs
	// (origin + radius will accidentally touch leafs that are really should not be affected)
	gEngine.R_FatPVS(origin, radius, g_lights_bsp.accum.visbytes, /*merge*/ false, /*fullvis*/ false);
	if (debug_dump_lights.enabled)
		gEngine.Con_Reportf("Collecting visible leafs for moving surface %p: %f,%f,%f %f: ", surf,
			origin[0], origin[1], origin[2], radius);

	for (int i = 0; i <= map->numleafs; ++i) {
		const mleaf_t *leaf = map->leafs + i;
		if( !CHECKVISBIT( g_lights_bsp.accum.visbytes, i ))
			continue;

		leafs_direct++;

		if (leafAccumAdd( i + 1 )) {
			if (debug_dump_lights.enabled)
				gEngine.Con_Reportf(" %d", i + 1);
		} else {
			// This leaf was already added earlier by PVS
			// but it really should be counted as direct
			leafs_pvs--;
		}
	}

	if (debug_dump_lights.enabled)
		gEngine.Con_Reportf(" (sum=%d, direct=%d, pvs=%d)\n", g_lights_bsp.accum.count, leafs_direct, leafs_pvs);

	leafAccumFinalize();

	// ...... oh no
	return (vk_light_leaf_set_t*)&g_lights_bsp.accum.count;
}

static void prepareSurfacesLeafVisibilityCache( const struct model_s *map ) {
	if (g_lights_bsp.surfaces != NULL) {
		for (int i = 0; i < g_lights_bsp.num_surfaces; ++i) {
			vk_surface_metadata_t *smeta = g_lights_bsp.surfaces + i;
			if (smeta->potentially_visible_leafs)
				Mem_Free(smeta->potentially_visible_leafs);
		}
		Mem_Free(g_lights_bsp.surfaces);
	}

	g_lights_bsp.num_surfaces = map->numsurfaces;
	g_lights_bsp.surfaces = Mem_Malloc(vk_core.pool, g_lights_bsp.num_surfaces * sizeof(vk_surface_metadata_t));
	for (int i = 0; i < g_lights_bsp.num_surfaces; ++i)
		g_lights_bsp.surfaces[i].potentially_visible_leafs = NULL;
}

void RT_LightsNewMap( const struct model_s *map ) {
	// 1. Determine map bounding box (and optimal grid size?)
		// map->mins, maxs
	vec3_t map_size, min_cell, max_cell;
	VectorSubtract(map->maxs, map->mins, map_size);

	VectorDivide(map->mins, LIGHT_GRID_CELL_SIZE, min_cell);
	min_cell[0] = floorf(min_cell[0]);
	min_cell[1] = floorf(min_cell[1]);
	min_cell[2] = floorf(min_cell[2]);
	VectorCopy(min_cell, g_lights.map.grid_min_cell);

	VectorDivide(map->maxs, LIGHT_GRID_CELL_SIZE, max_cell);
	max_cell[0] = ceilf(max_cell[0]);
	max_cell[1] = ceilf(max_cell[1]);
	max_cell[2] = ceilf(max_cell[2]);

	VectorSubtract(max_cell, min_cell, g_lights.map.grid_size);
	g_lights.map.grid_cells = g_lights.map.grid_size[0] * g_lights.map.grid_size[1] * g_lights.map.grid_size[2];

	ASSERT(g_lights.map.grid_cells < MAX_LIGHT_CLUSTERS);

	gEngine.Con_Reportf("Map mins:(%f, %f, %f), maxs:(%f, %f, %f), size:(%f, %f, %f), min_cell:(%f, %f, %f) cells:(%d, %d, %d); total: %d\n",
		map->mins[0], map->mins[1], map->mins[2],
		map->maxs[0], map->maxs[1], map->maxs[2],
		map_size[0], map_size[1], map_size[2],
		min_cell[0], min_cell[1], min_cell[2],
		g_lights.map.grid_size[0],
		g_lights.map.grid_size[1],
		g_lights.map.grid_size[2],
		g_lights.map.grid_cells
	);

	bitArrayDestroy(&g_lights_.visited_cells);
	g_lights_.visited_cells = bitArrayCreate(g_lights.map.grid_cells);

	prepareSurfacesLeafVisibilityCache( map );
}

static qboolean addSurfaceLightToCell( int cell_index, int polygon_light_index ) {
	vk_lights_cell_t *const cluster = g_lights.cells + cell_index;

	if (cluster->num_polygons == MAX_VISIBLE_SURFACE_LIGHTS) {
		return false;
	}

	if (debug_dump_lights.enabled) {
		gEngine.Con_Reportf("    adding polygon light %d to cell %d (count=%d)\n", polygon_light_index, cell_index, cluster->num_polygons+1);
	}

	cluster->polygons[cluster->num_polygons++] = polygon_light_index;
	if (cluster->frame_sequence != g_lights_.frame_sequence) {
		++g_lights_.stats.dirty_cells->value;
		cluster->frame_sequence = g_lights_.frame_sequence;
	}
	return true;
}

static qboolean addLightToCell( int cell_index, int light_index ) {
	vk_lights_cell_t *const cluster = g_lights.cells + cell_index;

	if (cluster->num_point_lights == MAX_VISIBLE_POINT_LIGHTS)
		return false;

	if (debug_dump_lights.enabled) {
		gEngine.Con_Reportf("    adding point light %d to cell %d (count=%d)\n", light_index, cell_index, cluster->num_point_lights+1);
	}

	cluster->point_lights[cluster->num_point_lights++] = light_index;

	if (cluster->frame_sequence != g_lights_.frame_sequence) {
		++g_lights_.stats.dirty_cells->value;
		cluster->frame_sequence = g_lights_.frame_sequence;
	}
	return true;
}

static qboolean canSurfaceLightAffectAABB(const model_t *mod, const msurface_t *surf, const vec3_t emissive, const float minmax[6]) {
	//APROF_SCOPE_BEGIN_EARLY(canSurfaceLightAffectAABB); // DO NOT DO THIS. We have like 600k of these calls per frame :feelsbadman:
	qboolean retval = true;
	// FIXME transform surface
	// this here only works for static map model

	// Use bbox center for normal culling estimation
	const vec3_t bbox_center = {
		(minmax[0] + minmax[3]) / 2.f,
		(minmax[1] + minmax[4]) / 2.f,
		(minmax[2] + minmax[5]) / 2.f,
	};

	float bbox_plane_dist = PlaneDiff(bbox_center, surf->plane);
	if( FBitSet( surf->flags, SURF_PLANEBACK ))
		bbox_plane_dist = -bbox_plane_dist;

	if (bbox_plane_dist < 0.f) {
		// Fast conservative estimate by max distance from bbox center
		// TODO is enumerating all points or finding a closest one is better/faster?
		const float size_x = minmax[0] - minmax[3];
		const float size_y = minmax[1] - minmax[4];
		const float size_z = minmax[2] - minmax[5];
		const float plane_dist_guard_sqr = (size_x * size_x + size_y * size_y + size_z * size_z) * .25f;

		// Check whether this bbox is completely behind the surface
		if (bbox_plane_dist*bbox_plane_dist > plane_dist_guard_sqr)
			retval = false;
	}

	//APROF_SCOPE_END(canSurfaceLightAffectAABB);

	return retval;
}

static void addLightIndexToLeaf( const mleaf_t *leaf, int index ) {
	const int min_x = floorf(leaf->minmaxs[0] / LIGHT_GRID_CELL_SIZE);
	const int min_y = floorf(leaf->minmaxs[1] / LIGHT_GRID_CELL_SIZE);
	const int min_z = floorf(leaf->minmaxs[2] / LIGHT_GRID_CELL_SIZE);

	const int max_x = ceilf(leaf->minmaxs[3] / LIGHT_GRID_CELL_SIZE);
	const int max_y = ceilf(leaf->minmaxs[4] / LIGHT_GRID_CELL_SIZE);
	const int max_z = ceilf(leaf->minmaxs[5] / LIGHT_GRID_CELL_SIZE);

	if (debug_dump_lights.enabled) {
		gEngine.Con_Reportf("  adding leaf %d min=(%d, %d, %d), max=(%d, %d, %d) total=%d\n",
			leaf->cluster,
			min_x, min_y, min_z,
			max_x, max_y, max_z,
			(max_x - min_x) * (max_y - min_y) * (max_z - min_z)
		);
	}

	for (int x = min_x; x < max_x; ++x)
	for (int y = min_y; y < max_y; ++y)
	for (int z = min_z; z < max_z; ++z) {
		const int cell[3] = {
			x - g_lights.map.grid_min_cell[0],
			y - g_lights.map.grid_min_cell[1],
			z - g_lights.map.grid_min_cell[2]
		};

		const int cell_index = RT_LightCellIndex( cell );
		if (cell_index < 0)
			continue;

		if (bitArrayCheckOrSet(&g_lights_.visited_cells, cell_index)) {
			if (!addLightToCell(cell_index, index)) {
				ERROR_THROTTLED(10, "Cluster %d,%d,%d(%d) ran out of light slots",
					cell[0], cell[1],  cell[2], cell_index);
			}
		}
	}
}

static void addPointLightToAllClusters( int index ) {
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );

	// FIXME there's certainly a better way to do this: just enumerate
	// all clusters, not all leafs

	bitArrayClear(&g_lights_.visited_cells);
	for (int i = 1; i <= world->numleafs; ++i) {
		const mleaf_t *const leaf = world->leafs + i;
		addLightIndexToLeaf( leaf, index );
	}
}

static void addPointLightToClusters( int index ) {
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );

	if (!world->visdata) {
		addPointLightToAllClusters( index );
		return;
	}

	vk_point_light_t *const light = g_lights_.point_lights + index;
	const mleaf_t* leaf = gEngine.Mod_PointInLeaf(light->origin, world->nodes);
	const vk_light_leaf_set_t *const leafs = (vk_light_leaf_set_t*)&g_lights_bsp.accum.count;

	leafAccumPrepare();
	leafAccumAddPotentiallyVisibleFromLeaf( world, leaf, false);
	leafAccumFinalize();

	bitArrayClear(&g_lights_.visited_cells);
	for (int i = 0; i < leafs->num; ++i) {
		const mleaf_t *const leaf = world->leafs + leafs->leafs[i];
		addLightIndexToLeaf( leaf, index );
	}
}

static int addPointLight( const vec3_t origin, const vec3_t color, float radius, int lightstyle, float hack_attenuation ) {
	const int index = g_lights_.num_point_lights;
	vk_point_light_t *const plight = g_lights_.point_lights + index;

	if (g_lights_.num_point_lights >= MAX_POINT_LIGHTS) {
		ERROR_THROTTLED(10, "Too many lights, MAX_POINT_LIGHTS=%d", MAX_POINT_LIGHTS);
		return -1;
	}

	if (debug_dump_lights.enabled) {
		gEngine.Con_Printf("point light %d: origin=(%f %f %f) R=%f color=(%f %f %f)\n", index,
			origin[0], origin[1], origin[2], radius,
			color[0], color[1], color[2]);
	}

	*plight = (vk_point_light_t){0};
	VectorCopy(origin, plight->origin);
	plight->radius = radius;

	VectorScale(color, hack_attenuation, plight->base_color);
	VectorCopy(plight->base_color, plight->color);
	plight->lightstyle = lightstyle;

	// Omnidirectional light
	plight->stopdot = plight->stopdot2 = -1.f;
	VectorSet(plight->dir, 0, 0, 0);

	addPointLightToClusters( index );
	g_lights_.num_point_lights++;
	return index;
}

static int addSpotLight( const vk_light_entity_t *le, float radius, int lightstyle, float hack_attenuation, qboolean all_clusters ) {
	const int index = g_lights_.num_point_lights;
	vk_point_light_t *const plight = g_lights_.point_lights + index;

	if (g_lights_.num_point_lights >= MAX_POINT_LIGHTS) {
		ERROR_THROTTLED(10, "Too many lights, MAX_POINT_LIGHTS=%d", MAX_POINT_LIGHTS);
		return -1;
	}

	if (debug_dump_lights.enabled) {
		gEngine.Con_Printf("%s light %d: origin=(%f %f %f) color=(%f %f %f) dir=(%f %f %f)\n",
			le->type == LightTypeEnvironment ? "environment" : "spot",
			index,
			le->origin[0], le->origin[1], le->origin[2],
			le->color[0], le->color[1], le->color[2],
			le->dir[0], le->dir[1], le->dir[2]);
	}

	*plight = (vk_point_light_t){0};
	VectorCopy(le->origin, plight->origin);
	plight->radius = radius;

	VectorScale(le->color, hack_attenuation, plight->base_color);
	VectorCopy(plight->base_color, plight->color);
	plight->lightstyle = lightstyle;

	VectorCopy(le->dir, plight->dir);
	plight->stopdot = le->stopdot;
	plight->stopdot2 = le->stopdot2;

	if (le->type == LightTypeEnvironment)
		plight->flags = LightFlag_Environment;

	if (all_clusters)
		addPointLightToAllClusters( index );
	else
		addPointLightToClusters( index );

	g_lights_.num_point_lights++;
	return index;
}

void RT_LightAddFlashlight(const struct cl_entity_s *ent, qboolean local_player ) {
	// parameters
	const float hack_attenuation = 0.1;
	float radius = 1.0;
	// TODO: better tune it
	const float _cone = 10.0;
	const float _cone2 = 30.0;
	const vec3_t light_color = {255, 255, 192};
	float light_intensity = 300;

	vec3_t color;
	vec3_t origin;
	vec3_t angles;
	vk_light_entity_t le = { .type = LightTypeSpot };

	float thirdperson_offset = 25;
	vec3_t forward, view_ofs;
	vec3_t vecSrc, vecEnd;
	pmtrace_t *trace;
	if( local_player )
	{
		// local player case
		// position
		if (gEngine.EngineGetParm(PARM_THIRDPERSON, 0)) { // thirdperson
			AngleVectors( g_camera.viewangles, forward, NULL, NULL );
			view_ofs[0] = view_ofs[1] = 0.0f;
			if( ent->curstate.usehull == 1 ) {
				view_ofs[2] = 12.0f; // VEC_DUCK_VIEW;
			} else {
				view_ofs[2] = 28.0f; // DEFAULT_VIEWHEIGHT
			}
			VectorAdd( ent->origin, view_ofs, vecSrc );
			VectorMA( vecSrc, thirdperson_offset, forward, vecEnd );
			trace = gEngine.EV_VisTraceLine( vecSrc, vecEnd, PM_STUDIO_BOX );
			VectorCopy( trace->endpos, origin );
			VectorCopy( forward, le.dir);
		} else { // firstperson
			// based on https://github.com/SNMetamorph/PrimeXT/blob/0869b1abbddd13c1229769d8cd71941610be0bf3/client/flashlight.cpp#L35
			origin[0] = g_camera.vieworg[0] + (g_camera.vright[0] * (-4.0f)) + (g_camera.vforward[0] * 14.0); // forward-back
			origin[1] = g_camera.vieworg[1] + (g_camera.vright[1] * (-4.0f)) + (g_camera.vforward[1] * 14.0); // left-right
			origin[2] = g_camera.vieworg[2] + (g_camera.vright[2] * (-4.0f)) + (g_camera.vforward[2] * 14.0); // up-down
			origin[2] += 2.0f;
			VectorCopy(g_camera.vforward, le.dir);
		}
	}
	else // non-local player case
	{
		thirdperson_offset = 10;
		radius = 10;
		light_intensity = 60;

		VectorCopy( ent->angles, angles );
		// NOTE: pitch divided by 3.0 twice. So we need apply 3^2 = 9
		angles[PITCH] = ent->curstate.angles[PITCH] * 9.0f;
		angles[YAW] = ent->angles[YAW];
		angles[ROLL] = 0.0f; // roll not used

		AngleVectors( angles, angles, NULL, NULL );
		view_ofs[0] = view_ofs[1] = 0.0f;
		if( ent->curstate.usehull == 1 ) {
			view_ofs[2] = 12.0f; // VEC_DUCK_VIEW;
		} else {
			view_ofs[2] = 28.0f; // DEFAULT_VIEWHEIGHT
		}
		VectorAdd( ent->origin, view_ofs, vecSrc );
		VectorMA( vecSrc, thirdperson_offset, angles, vecEnd );
		trace = gEngine.EV_VisTraceLine( vecSrc, vecEnd, PM_STUDIO_BOX );
		VectorCopy( trace->endpos, origin );
		VectorCopy( angles, le.dir );
	}

	VectorCopy(origin, le.origin);

	// prepare colors by parseEntPropRgbav
	VectorScale(light_color, light_intensity / 255.0f, color);

	// convert colors by weirdGoldsrcLightScaling
	float l1 = Q_max(color[0], Q_max(color[1], color[2]));
	l1 = l1 * l1 / 10;
	VectorScale(color, l1, le.color);

	// convert stopdots by parseStopDot
	le.stopdot = cosf(_cone * M_PI / 180.f);
	le.stopdot2 = cosf(_cone2 * M_PI / 180.f);

	/*
	gEngine.Con_Printf("flashlight: origin=(%f %f %f) color=(%f %f %f) dir=(%f %f %f)\n",
		le.origin[0], le.origin[1], le.origin[2],
		le.color[0], le.color[1], le.color[2],
		le.dir[0], le.dir[1], le.dir[2]);
	*/

	addSpotLight(&le, radius, 0, hack_attenuation, false);
}

static float sphereSolidAngleFromDistDiv2Pi(float r, float d) {
	return 1. - sqrt(d*d - r*r)/d;
}

static void addDlight( const dlight_t *dlight ) {
	const float k_light_radius = 2.f;
	const float k_threshold = 2.f;

	float max_comp;
	vec3_t color;
	int index;
	float scaler;

	max_comp = Q_max(dlight->color.r, Q_max(dlight->color.g, dlight->color.b));
	if (max_comp < k_threshold || dlight->radius <= k_light_radius)
		return;

	scaler = k_threshold / (max_comp * sphereSolidAngleFromDistDiv2Pi(k_light_radius, dlight->radius));

	VectorSet(
		color,
		dlight->color.r * scaler,
		dlight->color.g * scaler,
		dlight->color.b * scaler);

	index = addPointLight(dlight->origin, color, k_light_radius, -1, 1.f);
	if (index < 0)
		return;
}

static void processStaticPointLights( void ) {
	APROF_SCOPE_BEGIN_EARLY(static_lights);
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );
	ASSERT(world);

	g_lights_.num_point_lights = 0;
	for (int i = 0; i < g_map_entities.num_lights; ++i) {
		const vk_light_entity_t *le = g_map_entities.lights + i;
		const float default_radius = 2.f; // FIXME tune
		const float hack_attenuation = .1f; // FIXME tune
		const float hack_attenuation_spot = .1f; // FIXME tune
		const float radius = le->radius > 0.f ? le->radius : default_radius;
		int index;

		switch (le->type) {
			case LightTypePoint:
				index = addPointLight(le->origin, le->color, radius, le->style, hack_attenuation);
				break;

			case LightTypeSpot:
			case LightTypeEnvironment:
				index = addSpotLight(le, radius, le->style, hack_attenuation_spot, i == g_map_entities.single_environment_index);
				break;

			default:
				ASSERT(!"Unexpected light type");
				continue;
		}

		if (index < 0)
			break;
	}
	APROF_SCOPE_END(static_lights);
}

void RT_LightsLoadBegin( const struct model_s *map ) {
	// Load RAD data based on map name
	{
		int name_len = Q_strlen(map->name);

		// Strip ".bsp" suffix
		if (name_len > 4 && 0 == Q_stricmp(map->name + name_len - 4, ".bsp"))
			name_len -= 4;

		memset(g_lights_.map.emissive_textures, 0, sizeof(g_lights_.map.emissive_textures));
		loadRadData( map, "maps/lights.rad" );
		loadRadData( map, "%.*s.rad", name_len, map->name );
	}

	// Clear static lights counts
	{
		g_lights_.num_polygons = g_lights_.num_static.polygons = 0;
		g_lights_.num_point_lights = g_lights_.num_static.point_lights = 0;
		g_lights_.num_polygon_vertices = g_lights_.num_static.polygon_vertices = 0;

		for (int i = 0; i < g_lights.map.grid_cells; ++i) {
			vk_lights_cell_t *const cell = g_lights.cells + i;
			cell->num_point_lights = cell->num_static.point_lights = 0;
			cell->num_polygons = cell->num_static.polygons = 0;
			cell->frame_sequence = g_lights_.frame_sequence;
		}
	}

	processStaticPointLights();
}

void RT_LightsLoadEnd( void ) {
	//debug_dump_lights.enabled = true;

	// Fix static counts
	{
		g_lights_.num_static.polygons = g_lights_.num_polygons;
		g_lights_.num_static.point_lights = g_lights_.num_point_lights;
		g_lights_.num_static.polygon_vertices = g_lights_.num_polygon_vertices;

		for (int i = 0; i < g_lights.map.grid_cells; ++i) {
			vk_lights_cell_t *const cell = g_lights.cells + i;
			cell->num_static.point_lights = cell->num_point_lights;
			cell->num_static.polygons = cell->num_polygons;
		}
	}

	g_lights_.stats.dirty_cells->value = g_lights.map.grid_cells;
}

qboolean RT_GetEmissiveForTexture( vec3_t out, int texture_id ) {
	ASSERT(texture_id >= 0);
	ASSERT(texture_id < MAX_TEXTURES);

	{
		vk_emissive_texture_t *const etex = g_lights_.map.emissive_textures + texture_id;
		if (etex->set) {
			VectorCopy(etex->emissive, out);
			return true;
		} else {
			VectorClear(out);
			return false;
		}
	}
}

static void addPolygonLightIndexToLeaf(const mleaf_t* leaf, int poly_index) {
	const int min_x = floorf(leaf->minmaxs[0] / LIGHT_GRID_CELL_SIZE);
	const int min_y = floorf(leaf->minmaxs[1] / LIGHT_GRID_CELL_SIZE);
	const int min_z = floorf(leaf->minmaxs[2] / LIGHT_GRID_CELL_SIZE);

	const int max_x = floorf(leaf->minmaxs[3] / LIGHT_GRID_CELL_SIZE) + 1;
	const int max_y = floorf(leaf->minmaxs[4] / LIGHT_GRID_CELL_SIZE) + 1;
	const int max_z = floorf(leaf->minmaxs[5] / LIGHT_GRID_CELL_SIZE) + 1;

	const qboolean not_visible = false; //TODO static_map && !canSurfaceLightAffectAABB(world, geom->surf, esurf->emissive, leaf->minmaxs);

	if (debug_dump_lights.enabled) {
		gEngine.Con_Reportf("  adding leaf %d min=(%d, %d, %d), max=(%d, %d, %d) total=%d\n",
			leaf->cluster,
			min_x, min_y, min_z,
			max_x, max_y, max_z,
			(max_x - min_x) * (max_y - min_y) * (max_z - min_z)
		);
	}

	if (not_visible)
		return;

	for (int x = min_x; x < max_x; ++x)
	for (int y = min_y; y < max_y; ++y)
	for (int z = min_z; z < max_z; ++z) {
		const int cell[3] = {
			x - g_lights.map.grid_min_cell[0],
			y - g_lights.map.grid_min_cell[1],
			z - g_lights.map.grid_min_cell[2]
		};

		const int cell_index = RT_LightCellIndex( cell );
		if (cell_index < 0)
			continue;

		if (bitArrayCheckOrSet(&g_lights_.visited_cells, cell_index)) {
			const float minmaxs[6] = {
				x * LIGHT_GRID_CELL_SIZE,
				y * LIGHT_GRID_CELL_SIZE,
				z * LIGHT_GRID_CELL_SIZE,
				(x+1) * LIGHT_GRID_CELL_SIZE,
				(y+1) * LIGHT_GRID_CELL_SIZE,
				(z+1) * LIGHT_GRID_CELL_SIZE,
			};

			/* TODO if (static_map && !canSurfaceLightAffectAABB(world, geom->surf, esurf->emissive, minmaxs)) */
			/* 	continue; */

			if (!addSurfaceLightToCell(cell_index, poly_index)) {
				ERROR_THROTTLED(10, "Cluster %d,%d,%d(%d) ran out of polygon light slots",
					cell[0], cell[1],  cell[2], cell_index);
			}
		}
	}
}

static void addPolygonLightToAllClusters( int poly_index ) {
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );

	// FIXME there's certainly a better way to do this: just enumerate
	// all clusters, not all leafs

	bitArrayClear(&g_lights_.visited_cells);
	for (int i = 1; i <= world->numleafs; ++i) {
		const mleaf_t *const leaf = world->leafs + i;
		addPolygonLightIndexToLeaf( leaf, poly_index );
	}
}

static void addPolygonLeafSetToClusters(const vk_light_leaf_set_t *leafs, int poly_index) {
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );

	// FIXME this shouldn't happen in prod
	if (!leafs)
		return;

	bitArrayClear(&g_lights_.visited_cells);

	// Iterate through each visible/potentially affected leaf to get a range of grid cells
	for (int i = 0; i < leafs->num; ++i) {
		const mleaf_t *const leaf = world->leafs + leafs->leafs[i];
		addPolygonLightIndexToLeaf(leaf, poly_index);
	}
}

int RT_LightAddPolygon(const rt_light_add_polygon_t *addpoly) {
	if (g_lights_.num_polygons == MAX_SURFACE_LIGHTS) {
		gEngine.Con_Printf(S_ERROR "Max number of polygon lights %d reached\n", MAX_SURFACE_LIGHTS);
		return -1;
	}

	ASSERT(addpoly->num_vertices > 2);
	ASSERT(addpoly->num_vertices < 8);
	ASSERT(g_lights_.num_polygon_vertices + addpoly->num_vertices <= COUNTOF(g_lights_.polygon_vertices));

	{
		rt_light_polygon_t *const poly = g_lights_.polygons + g_lights_.num_polygons;
		vec3_t *vertices = g_lights_.polygon_vertices + g_lights_.num_polygon_vertices;
		vec3_t normal;

		poly->vertices.offset = g_lights_.num_polygon_vertices;
		poly->vertices.count = addpoly->num_vertices;

		VectorCopy(addpoly->emissive, poly->emissive);
		VectorSet(poly->center, 0, 0, 0);
		VectorSet(normal, 0, 0, 0);

		for (int i = 0; i < addpoly->num_vertices; ++i) {
			if (addpoly->transform_row)
				Matrix3x4_VectorTransform(*addpoly->transform_row, addpoly->vertices[i], vertices[i]);
			else
				VectorCopy(addpoly->vertices[i], vertices[i]);
			VectorAdd(vertices[i], poly->center, poly->center);

			if (i > 1) {
				vec3_t e[2], lnormal;
				VectorSubtract(vertices[i-0], vertices[0], e[0]);
				VectorSubtract(vertices[i-1], vertices[0], e[1]);
				CrossProduct(e[0], e[1], lnormal);
				VectorAdd(lnormal, normal, normal);
			}
		}

		poly->area = VectorLength(normal);
		VectorM(1.f / poly->area, normal, poly->plane);
		poly->plane[3] = -DotProduct(vertices[0], poly->plane);

		VectorM(1.f / poly->vertices.count, poly->center, poly->center);

		if (!addpoly->dynamic || debug_dump_lights.enabled) {
			gEngine.Con_Reportf("added polygon light index=%d color=(%f, %f, %f) center=(%f, %f, %f) plane=(%f, %f, %f, %f) area=%f num_vertices=%d\n",
				g_lights_.num_polygons,
				poly->emissive[0],
				poly->emissive[1],
				poly->emissive[2],
				poly->center[0],
				poly->center[1],
				poly->center[2],
				poly->plane[0],
				poly->plane[1],
				poly->plane[2],
				poly->plane[3],
				poly->area,
				poly->vertices.count
			);
		}

		const model_t* const world = gEngine.pfnGetModelByIndex( 1 );
		if (world->visdata) {
			const vk_light_leaf_set_t *const leafs = addpoly->dynamic
				? getMapLeafsAffectedByMovingSurface( addpoly->surface, addpoly->transform_row )
				: getMapLeafsAffectedByMapSurface( addpoly->surface );
			addPolygonLeafSetToClusters(leafs, g_lights_.num_polygons);
		} else {
			addPolygonLightToAllClusters( g_lights_.num_polygons );
		}

		g_lights_.num_polygon_vertices += addpoly->num_vertices;
		return g_lights_.num_polygons++;
	}
}

void RT_LightsFrameBegin( void ) {
	g_lights_.num_polygons = g_lights_.num_static.polygons;
	g_lights_.num_point_lights = g_lights_.num_static.point_lights;
	g_lights_.num_polygon_vertices = g_lights_.num_static.polygon_vertices;

	g_lights_.stats.dirty_cells->value = 0;
	g_lights_.stats.dirty_cells_size->value = 0;

	for (int i = 0; i < g_lights.map.grid_cells; ++i) {
		vk_lights_cell_t *const cell = g_lights.cells + i;
		cell->num_polygons = cell->num_static.polygons;
		cell->num_point_lights = cell->num_static.point_lights;
	}
}

static void uploadGridRange( int begin, int end ) {
	const int count = end - begin;
	ASSERT( count > 0 );

	const int size = count * sizeof(struct LightCluster);
	const vk_staging_region_t locked = R_VkStagingLockForBuffer( (vk_staging_buffer_args_t) {
		.buffer = g_lights_.buffer.buffer,
		.offset = sizeof(struct LightsMetadata) + begin * sizeof(struct LightCluster),
		.size = size,
		.alignment = 16, // WHY?
	} );

	ASSERT(locked.ptr);

	struct LightCluster *const grid = locked.ptr;
	memset(grid, 0, size);

	for (int i = 0; i < count; ++i) {
		const vk_lights_cell_t *const src = g_lights.cells + i + begin;
		struct LightCluster *const dst = grid + i;

		dst->num_point_lights = src->num_point_lights;
		dst->num_polygons = src->num_polygons;
		memcpy(dst->point_lights, src->point_lights, sizeof(uint8_t) * src->num_point_lights);
		memcpy(dst->polygons, src->polygons, sizeof(uint8_t) * src->num_polygons);
	}

	R_VkStagingUnlock( locked.handle );

	g_lights_.stats.ranges_uploaded->value++;
}

static void uploadGrid( void ) {
	ASSERT(g_lights.map.grid_cells <= MAX_LIGHT_CLUSTERS);

	g_lights_.stats.ranges_uploaded->value = 0;

	int begin = -1;
	for (int i = 0; i < g_lights.map.grid_cells; ++i) {
		const vk_lights_cell_t *const cell = g_lights.cells + i;

		const qboolean dirty = cell->frame_sequence == g_lights_.frame_sequence;
		if (dirty && begin < 0)
			begin = i;

		if (!dirty && begin >= 0) {
			uploadGridRange(begin, i);
			begin = -1;
		}
	}

	if (begin >= 0)
		uploadGridRange(begin, g_lights.map.grid_cells);
}

static void uploadPolygonLights( struct LightsMetadata *metadata ) {
	ASSERT(g_lights_.num_polygons <= MAX_EMISSIVE_KUSOCHKI);
	metadata->num_polygons = g_lights_.num_polygons;
	for (int i = 0; i < g_lights_.num_polygons; ++i) {
		const rt_light_polygon_t *const src_poly = g_lights_.polygons + i;
		struct PolygonLight *const dst_poly = metadata->polygons + i;

		Vector4Copy(src_poly->plane, dst_poly->plane);
		VectorCopy(src_poly->center, dst_poly->center);
		dst_poly->area = src_poly->area;
		VectorCopy(src_poly->emissive, dst_poly->emissive);

		// TODO DEBUG_ASSERT
		ASSERT(src_poly->vertices.count > 2);
		ASSERT(src_poly->vertices.offset < 0xffffu);
		ASSERT(src_poly->vertices.count < 0xffffu);

		ASSERT(src_poly->vertices.offset + src_poly->vertices.count < COUNTOF(metadata->polygon_vertices));

		dst_poly->vertices_count_offset = (src_poly->vertices.count << 16) | (src_poly->vertices.offset);
	}

	// TODO static assert
	ASSERT(sizeof(metadata->polygon_vertices) >= sizeof(g_lights_.polygon_vertices));
	for (int i = 0; i < g_lights_.num_polygon_vertices; ++i) {
		VectorCopy(g_lights_.polygon_vertices[i], metadata->polygon_vertices[i]);
	}
}

static void uploadPointLights( struct LightsMetadata *metadata ) {
	metadata->num_point_lights = g_lights_.num_point_lights;
	for (int i = 0; i < g_lights_.num_point_lights; ++i) {
		vk_point_light_t *const src = g_lights_.point_lights + i;
		struct PointLight *const dst = metadata->point_lights + i;

		VectorCopy(src->origin, dst->origin_r);
		dst->origin_r[3] = src->radius;

		VectorCopy(src->color, dst->color_stopdot);
		dst->color_stopdot[3] = src->stopdot;

		VectorCopy(src->dir, dst->dir_stopdot2);
		dst->dir_stopdot2[3] = src->stopdot2;

		dst->environment = !!(src->flags & LightFlag_Environment);
	}
}

vk_lights_bindings_t VK_LightsUpload( void ) {
	const vk_staging_region_t locked = R_VkStagingLockForBuffer( (vk_staging_buffer_args_t) {
		.buffer = g_lights_.buffer.buffer,
		.offset = 0,
		.size = sizeof(struct LightsMetadata),
		.alignment = 16, // WHY?
	} );

	ASSERT(locked.ptr);

	struct LightsMetadata *metadata = locked.ptr;
	memset(metadata, 0, sizeof(*metadata));

	VectorCopy(g_lights.map.grid_min_cell, metadata->grid_min_cell);
	VectorCopy(g_lights.map.grid_size, metadata->grid_size);

	uploadPolygonLights( metadata );
	uploadPointLights( metadata );

	R_VkStagingUnlock( locked.handle );

	uploadGrid();

	g_lights_.frame_sequence++;

	return (vk_lights_bindings_t){
		.buffer = g_lights_.buffer.buffer,
		.metadata = {
			.offset = 0,
			.size = sizeof(struct LightsMetadata),
		},
		.grid = {
			.offset = sizeof(struct LightsMetadata),
			.size = sizeof(struct LightCluster) * MAX_LIGHT_CLUSTERS,
		},
	};
}

void RT_LightsFrameEnd( void ) {
	APROF_SCOPE_BEGIN_EARLY(finalize);
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );

	if (g_lights_.num_polygons > UINT8_MAX) {
		ERROR_THROTTLED(10, "Too many emissive surfaces found: %d; some areas will be dark", g_lights_.num_polygons);
		g_lights_.num_polygons = UINT8_MAX;
	}

	for (int i = 0; i < MAX_ELIGHTS; ++i) {
		const dlight_t *dlight = gEngine.GetEntityLight(i);
		if (!dlight)
			continue;
		addDlight(dlight);
	}

	for (int i = 0; i < g_lights_.num_point_lights; ++i) {
		vk_point_light_t *const light = g_lights_.point_lights + i;
		if (light->lightstyle < 0 || light->lightstyle >= MAX_LIGHTSTYLES)
			continue;

		{
			const float scale = g_lightmap.lightstylevalue[light->lightstyle] / 255.f;
			VectorScale(light->base_color, scale, light->color);
		}
	}

	APROF_SCOPE_BEGIN(dlights);
	for (int i = 0; i < MAX_DLIGHTS; ++i) {
		const dlight_t *dlight = gEngine.GetDynamicLight(i);
		if( !dlight || dlight->die < gpGlobals->time || !dlight->radius )
			continue;
		addDlight(dlight);
	}
	APROF_SCOPE_END(dlights);

	if (debug_dump_lights.enabled) {
#if 0
		// Print light grid stats
		gEngine.Con_Reportf("Emissive surfaces found: %d\n", g_lights_.num_polygons);

		{
			#define GROUPSIZE 4
			int histogram[1 + (MAX_VISIBLE_SURFACE_LIGHTS + GROUPSIZE - 1) / GROUPSIZE] = {0};
			for (int i = 0; i < g_lights.map.grid_cells; ++i) {
				const vk_lights_cell_t *cluster = g_lights.cells + i;
				const int hist_index = cluster->num_polygons ? 1 + cluster->num_polygons / GROUPSIZE : 0;
				histogram[hist_index]++;
			}

			gEngine.Con_Reportf("Built %d light clusters. Stats:\n", g_lights.map.grid_cells);
			gEngine.Con_Reportf("  0: %d\n", histogram[0]);
			for (int i = 1; i < ARRAYSIZE(histogram); ++i)
				gEngine.Con_Reportf("  %d-%d: %d\n",
					(i - 1) * GROUPSIZE,
					i * GROUPSIZE - 1,
					histogram[i]);
		}

		{
			int num_clusters_with_lights_in_range = 0;
			for (int i = 0; i < g_lights.map.grid_cells; ++i) {
				const vk_lights_cell_t *cluster = g_lights.cells + i;
				if (cluster->num_polygons > 0) {
					gEngine.Con_Reportf(" cluster %d: polygons=%d\n", i, cluster->num_polygons);
				}

				for (int j = 0; j < cluster->num_polygons; ++j) {
					const int index = cluster->polygons[j];
					if (index >= vk_rtx_light_begin->value && index < vk_rtx_light_end->value) {
						++num_clusters_with_lights_in_range;
					}
				}
			}

			gEngine.Con_Reportf("Clusters with filtered lights: %d\n", num_clusters_with_lights_in_range);
		}
#endif
	}

	g_lights_.stats.dirty_cells_size->value = g_lights_.stats.dirty_cells->value * sizeof(struct LightCluster) / 1024;

	debug_dump_lights.enabled = false;
	APROF_SCOPE_END(finalize);
}
