#include "vk_light.h"
#include "vk_textures.h"
#include "vk_brush.h"

#include "mod_local.h"
#include "xash3d_mathlib.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h> // isalnum...

vk_lights_t g_lights = {0};

static int lookupTextureF( const char *fmt, ...) {
	int tex_id = 0;
	char buffer[1024];
	va_list argptr;
	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	tex_id = VK_FindTexture(buffer);
	gEngine.Con_Reportf("Looked up texture %s -> %d\n", buffer, tex_id);
	return tex_id;
}

static void loadRadData( const model_t *map, const char *fmt, ... ) {
	fs_offset_t size;
	char *data;
	byte *buffer;
	char filename[1024];

	va_list argptr;
	va_start( argptr, fmt );
	vsnprintf( filename, sizeof filename, fmt, argptr );
	va_end( argptr );

	buffer = gEngine.COM_LoadFile( filename, &size, false);

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

				if (!texture_name) {
					texture_name = name;
				} else {
					// name is now just a wad name
					texture_name[0] = '\0';
					wad_name = name;

					texture_name += 1;
				}

				// Try bsp texture first
				tex_id = lookupTextureF("#%s:%s.mip", map->name, texture_name);

				// Try wad texture if bsp is not there
				if (!tex_id) {
					if (!wad_name)
						wad_name = "halflife";
					tex_id = lookupTextureF("%s.wad/%s.mip", wad_name, texture_name);
				}

				if (tex_id) {
					ASSERT(tex_id < MAX_TEXTURES);

					g_lights.map.emissive_textures[tex_id].emissive[0] = r;
					g_lights.map.emissive_textures[tex_id].emissive[1] = g;
					g_lights.map.emissive_textures[tex_id].emissive[2] = b;
					g_lights.map.emissive_textures[tex_id].set = true;
				}
			}
		}

		if (!line_end)
			break;

		data = line_end + 1;
	}

	Mem_Free(buffer);
}

typedef struct {
	vec3_t origin;
	vec3_t color;
	//int style;
	//char pattern[64];
	//int dark;
} vk_light_entity_t;

struct {
	int num_lights;
	vk_light_entity_t lights[64];

	// TODO spot light entities
} g_light_entities;

#define ENT_PROP_LIST(X) \
	X(0, vec3_t, origin, Vec3) \
	X(1, vec3_t, angles, Vec3) \
	X(2, float, pitch, Float) \
	X(3, vec3_t, _light, Rgbav) \
	X(4, class_name_e, classname, Classname) \
	X(5, float, angle, Float) \

typedef enum {
	Unknown = 0,
	Light,
	LightSpot,
	LightEnvironment,
	Ignored,
} class_name_e;

typedef struct {
#define DECLARE_FIELD(num, type, name, kind) type name;
	ENT_PROP_LIST(DECLARE_FIELD)
#undef DECLARE_FIELD
} entity_props_t;

typedef enum {
	None = 0,
#define DECLARE_FIELD(num, type, name, kind) Field_##name = (1<<num),
	ENT_PROP_LIST(DECLARE_FIELD)
#undef DECLARE_FIELD
} fields_read_e;

static unsigned parseEntPropFloat(const string value, float *out, unsigned bit) {
	return (1 == sscanf(value, "%f", out)) ? bit : 0;
}

static unsigned parseEntPropVec3(const string value, vec3_t *out, unsigned bit) {
	return (3 == sscanf(value, "%f %f %f", &(*out)[0], &(*out)[1], &(*out)[2])) ? bit : 0;
}

static unsigned parseEntPropRgbav(const string value, vec3_t *out, unsigned bit) {
	float scale = 1.f / 255.f;
	const int components = sscanf(value, "%f %f %f %f", &(*out)[0], &(*out)[1], &(*out)[2], &scale);
	if (components == 1) {
		(*out)[2] = (*out)[1] = (*out)[0] = (*out)[0] / 255.f;
		return bit;
	} else if (components == 4) {
		scale /= 255.f * 255.f;
		(*out)[0] *= scale;
		(*out)[1] *= scale;
		(*out)[2] *= scale;
		return bit;
	} else if (components == 3) {
		(*out)[0] *= scale;
		(*out)[1] *= scale;
		(*out)[2] *= scale;
		return bit;
	}

	return 0;
}

static unsigned parseEntPropClassname(const string value, class_name_e *out, unsigned bit) {
	if (Q_strcmp(value, "light") == 0) {
		*out = Light;
	} else if (Q_strcmp(value, "light_spot") == 0) {
		*out = LightSpot;
	} else if (Q_strcmp(value, "light_environment") == 0) {
		*out = LightEnvironment;
	} else {
		*out = Ignored;
	}

	return bit;
}

static void parseStaticLightEntities( void ) {
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );
	char *pos;
	unsigned have_fields = 0;
	entity_props_t values;

	ASSERT(world);

	g_light_entities.num_lights = 0;
	VectorSet(g_lights.map.sun_dir, 0, 0, 0);
	VectorSet(g_lights.map.sun_color, 0, 0, 0);

	pos = world->entities;
	//gEngine.Con_Reportf("ENTITIES: %s\n", pos);
	for (;;) {
		char key[1024];
		char value[1024];

		pos = gEngine.COM_ParseFile(pos, key);
		ASSERT(Q_strlen(key) < sizeof(key));
		if (!pos)
			break;
		if (key[0] == '{') {
			have_fields = None;
			values = (entity_props_t){0};
			continue;
		} else if (key[0] == '}') {
			switch (values.classname) {
				case Light:
					{
						const unsigned need_fields = Field_origin | Field__light;
						if ((have_fields & need_fields) != need_fields) {
							gEngine.Con_Printf(S_ERROR "Missing some fields for light entity\n");
							continue;
						}
					}

					if (g_light_entities.num_lights == ARRAYSIZE(g_light_entities.lights)) {
						gEngine.Con_Printf(S_ERROR "Too many lights entities in map\n");
						continue;
					}

					{
						vk_light_entity_t *le = g_light_entities.lights + g_light_entities.num_lights++;
						VectorCopy(values.origin, le->origin);
						VectorCopy(values._light, le->color);
					}
					break;
				case LightSpot:
					// TODO
					break;

				case LightEnvironment:
				{
					float angle = values.angle;
					vec3_t dir = {0};

					const unsigned need_fields = Field__light;
					if ((have_fields & need_fields) != need_fields) {
						gEngine.Con_Printf(S_ERROR "Missing _light prop for light_environment\n");
						continue;
					}

					if (angle == -1) { // UP
						dir[0] = dir[1] = 0;
						dir[2] = 1;
					} else if (angle == -2) { // DOWN
						dir[0] = dir[1] = 0;
						dir[2] = -1;
					} else {
						if (angle == 0) {
							angle = values.angles[1];
						}

						angle *= M_PI / 180.f;

						dir[2] = 0;
						dir[0] = cosf(angle);
						dir[1] = sinf(angle);
					}

					angle = values.pitch ? values.pitch : values.angles[0];

					angle *= M_PI / 180.f;
					dir[2] = sinf(angle);
					dir[0] *= cosf(angle);
					dir[1] *= cosf(angle);

					VectorScale(dir, -1.f, g_lights.map.sun_dir);
					//VectorCopy(dir, g_lights.map.sun_dir);
					VectorCopy(values._light, g_lights.map.sun_color);
					break;
				}

				case Unknown:
				case Ignored:
					// Skip
					break;
			}

			continue;
		}

		pos = gEngine.COM_ParseFile(pos, value);
		ASSERT(Q_strlen(value) < sizeof(value));
		if (!pos)
			break;

#define READ_FIELD(num, type, name, kind) \
		if (Q_strcmp(key, #name) == 0) { \
			const unsigned bit = parseEntProp##kind(value, &values.name, Field_##name); \
			if (bit == 0) { \
				gEngine.Con_Printf( S_ERROR "Error parsing entity property " #name ", invalid value: %s\n", value); \
			} else have_fields |= bit; \
		} else
		ENT_PROP_LIST(READ_FIELD)
		{
			//gEngine.Con_Reportf("Unknown field %s with value %s\n", key, value);
		}
#undef CHECK_FIELD
	}
}

typedef enum { LightTypePoint, LightTypeSurface, LightTypeSpot} LightType;

#define MAX_LEAF_LIGHTS 64
typedef struct {
	int num_lights;
	struct {
		LightType type;
	} light[MAX_LEAF_LIGHTS];
} vk_light_leaf_t;
#define MAX_SURF_ASSOCIATED_LEAFS 16

typedef struct {
	int num;
	int leafs[];
} vk_light_leaf_set_t;

typedef struct {
	vk_light_leaf_set_t *potentially_visible_leafs;
} vk_surface_metadata_t;

static struct {
	vk_light_leaf_t leaves[MAX_MAP_LEAFS];

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
	} accum;
} g_lights_bsp = {0};

static void prepareLeafAccum( void ) {
	memset(&g_lights_bsp.accum, 0, sizeof(g_lights_bsp.accum));
}

static qboolean addLeafToAccum( uint16_t leaf_index ) {
	// Check whether this leaf was already added
#define LEAF_ADDED_BIT 0x8000000ul
	if (g_lights_bsp.accum.leafs[leaf_index] & LEAF_ADDED_BIT)
		return false;
#undef LEAF_ADDED_BIT

	g_lights_bsp.accum.leafs[g_lights_bsp.accum.count++] |= leaf_index;
	return true;
}

vk_light_leaf_set_t *getMapLeafsAffectedBySurface( const msurface_t *surf ) {
	const model_t	*const map = gEngine.pfnGetModelByIndex( 1 );
	const int surf_index = surf - map->surfaces;
	vk_surface_metadata_t * const smeta = g_lights_bsp.surfaces + surf_index;
	ASSERT(surf_index >= 0);
	ASSERT(surf_index < g_lights_bsp.num_surfaces);

	// Check if PVL hasn't been collected yet
	if (!smeta->potentially_visible_leafs) {
		int leafs_direct = 0, leafs_pvs = 0;
		prepareLeafAccum();

		// Enumerate all the map leafs and pick ones that have this surface referenced
		gEngine.Con_Reportf("Collecting visible leafs for surface %d:", surf_index);
		for (int i = 0; i < map->numleafs; ++i) {
			const mleaf_t *leaf = map->leafs + i;
			for (int j = 0; j < leaf->nummarksurfaces; ++j) {
				const msurface_t *leaf_surf = leaf->firstmarksurface[j];
				if (leaf_surf != surf)
					continue;

				// FIXME split direct leafs marking from pvs propagation
				leafs_direct++;
				if (addLeafToAccum( i )) {
					gEngine.Con_Reportf(" %d", i);
				} else {
					--leafs_pvs;
				}

				// Get all PVS leafs
				{
					const byte *pvs = leaf->compressed_vis;
					int pvs_leaf_index = 0;
					for (;pvs_leaf_index < map->numleafs; ++pvs) {
						uint8_t bits = pvs[0];

						// PVS is RLE encoded
						if (bits == 0) {
							const int skip = pvs[1];
							pvs_leaf_index += skip;
							++pvs;
							continue;
						}

						for (int k = 0; k < 8; ++k, ++pvs_leaf_index, bits >>= 1) {
							if ((bits&1) == 0)
								continue;

							if (addLeafToAccum( pvs_leaf_index )) {
								leafs_pvs++;
								gEngine.Con_Reportf(" *%d", pvs_leaf_index);
							}
						}
					}
				}
			}
		}
		gEngine.Con_Reportf(" (sum=%d, direct=%d, pvs=%d)\n", g_lights_bsp.accum.count, leafs_direct, leafs_pvs);

		smeta->potentially_visible_leafs = (vk_light_leaf_set_t*)Mem_Malloc(vk_core.pool, sizeof(smeta->potentially_visible_leafs) + sizeof(int) * g_lights_bsp.accum.count);
		smeta->potentially_visible_leafs->num = g_lights_bsp.accum.count;
		memcpy(smeta->potentially_visible_leafs->leafs, g_lights_bsp.accum.leafs, sizeof(int) * smeta->potentially_visible_leafs->num);
	}

	return smeta->potentially_visible_leafs;
}

static void lbspClear( void ) {
	for (int i = 0; i < MAX_MAP_LEAFS; ++i)
		g_lights_bsp.leaves[i].num_lights = 0;
}

static void lbspAddLightByLeaf( LightType type, const mleaf_t *leaf) {
	const int leaf_index = leaf->cluster + 1;
	ASSERT(leaf_index >= 0 && leaf_index < MAX_MAP_LEAFS);

	{
		vk_light_leaf_t *light_leaf = g_lights_bsp.leaves + leaf_index;

		ASSERT(light_leaf->num_lights <= MAX_LEAF_LIGHTS);
		if (light_leaf->num_lights == MAX_LEAF_LIGHTS) {
			gEngine.Con_Printf(S_ERROR "Max number of lights %d exceeded for leaf %d\n", MAX_LEAF_LIGHTS, leaf_index);
			return;
		}

		light_leaf->light[light_leaf->num_lights++].type = type;
	}
}

static void lbspAddLightByOrigin( LightType type, const vec3_t origin) {
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );
	const mleaf_t* leaf = gEngine.Mod_PointInLeaf(origin, world->nodes);
	if (!leaf) {
		gEngine.Con_Printf(S_ERROR "Adding light %d with origin (%f, %f, %f) ended up in no leaf\n",
			type, origin[0], origin[1], origin[2]);
		return;
	}
	lbspAddLightByLeaf( type, leaf);
}

static void prepareSurfacesLeafVisibilityCache( void ) {
	const model_t	*map = gEngine.pfnGetModelByIndex( 1 );
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

extern void traverseBSP( void );

void VK_LightsNewMap( void )
{
	const model_t	*map = gEngine.pfnGetModelByIndex( 1 );

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

	//traverseBSP();
	prepareSurfacesLeafVisibilityCache();

	VK_LightsLoadMapStaticLights();
}

void VK_LightsLoadMapStaticLights( void )
{
	const model_t	*map = gEngine.pfnGetModelByIndex( 1 );

	parseStaticLightEntities();

	// Load RAD data based on map name
	memset(g_lights.map.emissive_textures, 0, sizeof(g_lights.map.emissive_textures));
	loadRadData( map, "maps/lights.rad" );

	{
		int name_len = Q_strlen(map->name);

		// Strip ".bsp" suffix
		if (name_len > 4 && 0 == Q_stricmp(map->name + name_len - 4, ".bsp"))
			name_len -= 4;

		loadRadData( map, "%.*s.rad", name_len, map->name );
	}
}

void VK_LightsFrameInit( void )
{
	g_lights.num_emissive_surfaces = 0;
	memset(g_lights.cells, 0, sizeof(g_lights.cells));

	lbspClear();
}

static void addSurfaceLightToCell( const int light_cell[3], int emissive_surface_index ) {
	const uint cell_index = light_cell[0] + light_cell[1] * g_lights.map.grid_size[0] + light_cell[2] * g_lights.map.grid_size[0] * g_lights.map.grid_size[1];
	vk_lights_cell_t *const cluster = g_lights.cells + cell_index;

	if (light_cell[0] < 0 || light_cell[1] < 0 || light_cell[2] < 0
		|| (light_cell[0] >= g_lights.map.grid_size[0])
		|| (light_cell[1] >= g_lights.map.grid_size[1])
		|| (light_cell[2] >= g_lights.map.grid_size[2]))
		return;

	// Check whether it has been added already
	for (int i = 0; i < cluster->num_emissive_surfaces; ++i )
		if (cluster->emissive_surfaces[i] == emissive_surface_index)
			return;

	if (cluster->num_emissive_surfaces == MAX_VISIBLE_SURFACE_LIGHTS) {
		gEngine.Con_Printf(S_ERROR "Cluster %d,%d,%d(%d) ran out of emissive surfaces slots\n",
			light_cell[0], light_cell[1],  light_cell[2], cell_index
			);
		return;
	}

	cluster->emissive_surfaces[cluster->num_emissive_surfaces++] = emissive_surface_index;
}

const vk_emissive_surface_t *VK_LightsAddEmissiveSurface( const struct vk_render_geometry_s *geom, const matrix3x4 *transform_row, qboolean static_map ) {
	const int texture_num = geom->texture; // Animated texture
	if (!geom->surf)
		return NULL; // TODO break? no surface means that model is not brush

	// FIXME non-static light surfaces are broken temporarily
	if (!static_map)
		return NULL;

	if (geom->material != kXVkMaterialSky && geom->material != kXVkMaterialEmissive && !g_lights.map.emissive_textures[texture_num].set)
		return NULL;

	if (g_lights.num_emissive_surfaces >= 256)
		return NULL;

	{
		const model_t* const world = gEngine.pfnGetModelByIndex( 1 );
		const vk_light_leaf_set_t *const leafs = getMapLeafsAffectedBySurface( geom->surf );
		vk_emissive_surface_t *esurf = g_lights.emissive_surfaces + g_lights.num_emissive_surfaces;

		{
			// Add this light to per-leaf stats
			//gEngine.Con_Reportf("surface %p, leafs %d\n", geom->surf, leafs->num);
			for (int i = 0; i < leafs->num; ++i)
				lbspAddLightByLeaf(LightTypeSurface, world->leafs + leafs->leafs[i]);
		}

		// Insert into emissive surfaces
		esurf->kusok_index = geom->kusok_index;
		if (geom->material != kXVkMaterialSky && geom->material != kXVkMaterialEmissive) {
			VectorCopy(g_lights.map.emissive_textures[texture_num].emissive, esurf->emissive);
		} else {
			// TODO per-map sky emissive
			VectorSet(esurf->emissive, 1000.f, 1000.f, 1000.f);
		}
		Matrix3x4_Copy(esurf->transform, *transform_row);

		// Iterate through each visible/potentially affected leaf to get a range of grid cells
		for (int i = 0; i < leafs->num; ++i) {
			const mleaf_t *const leaf = world->leafs + leafs->leafs[i];

			const int min_x = (int)(leaf->minmaxs[0] / LIGHT_GRID_CELL_SIZE);
			const int min_y = (int)(leaf->minmaxs[1] / LIGHT_GRID_CELL_SIZE);
			const int min_z = (int)(leaf->minmaxs[2] / LIGHT_GRID_CELL_SIZE);

			const int max_x = (int)(leaf->minmaxs[3] / LIGHT_GRID_CELL_SIZE);
			const int max_y = (int)(leaf->minmaxs[4] / LIGHT_GRID_CELL_SIZE);
			const int max_z = (int)(leaf->minmaxs[5] / LIGHT_GRID_CELL_SIZE);

			/* gEngine.Con_Reportf( "minmaxs %f-%f, %f-%f, %f-%f =>" */
			/* 	"cells %d-%d, %d-%d, %d-%d\n", */
			/* 	leaf->minmaxs[0], leaf->minmaxs[3], */
			/* 	leaf->minmaxs[1], leaf->minmaxs[4], */
			/* 	leaf->minmaxs[2], leaf->minmaxs[5], */
			/* 	min_x, max_x, */
			/* 	min_y, max_y, */
			/* 	min_z, max_z); */

			for (int x = min_x; x <= max_x; ++x)
			for (int y = min_y; y <= max_y; ++y)
			for (int z = min_z; z <= max_z; ++z) {
				const int cell[3] = {
					x - g_lights.map.grid_min_cell[0],
					y - g_lights.map.grid_min_cell[1],
					z - g_lights.map.grid_min_cell[2]
				};
				// TODO culling, ...
				//		3.1 Compute light size and intensity (?)
				//			- light orientation
				//			- light intensity
				addSurfaceLightToCell(cell, g_lights.num_emissive_surfaces);
			}
		}

		++g_lights.num_emissive_surfaces;
		return esurf;
	}
}

static qboolean addDlight( const dlight_t *dlight ) {
	vk_point_light_t *light = g_lights.point_lights + g_lights.num_point_lights;

	if( !dlight || dlight->die < gpGlobals->time || !dlight->radius )
		return true;

	lbspAddLightByOrigin( LightTypePoint, dlight->origin );

	if (g_lights.num_point_lights >= MAX_POINT_LIGHTS)
		return false;

	Vector4Set(
		light->color,
		dlight->color.r / 255.f,
		dlight->color.g / 255.f,
		dlight->color.b / 255.f,
		1.f);
	Vector4Set(
		light->origin,
		dlight->origin[0],
		dlight->origin[1],
		dlight->origin[2],
		dlight->radius);

	++g_lights.num_point_lights;

	return true;
}

void VK_LightsFrameFinalize( void )
{
	if (g_lights.num_emissive_surfaces > UINT8_MAX) {
		gEngine.Con_Printf(S_ERROR "Too many emissive surfaces found: %d; some areas will be dark\n", g_lights.num_emissive_surfaces);
		g_lights.num_emissive_surfaces = UINT8_MAX;
	}

	g_lights.num_point_lights = 0;
	for (int i = 0; i < g_light_entities.num_lights; ++i, ++g_lights.num_point_lights) {
		const vk_light_entity_t *entity = g_light_entities.lights + i;
		vk_point_light_t *light = g_lights.point_lights + g_lights.num_point_lights;

		lbspAddLightByOrigin( LightTypePoint, entity->origin );

		if (g_lights.num_point_lights >= MAX_POINT_LIGHTS) {
			gEngine.Con_Printf(S_ERROR "Too many point light entities, MAX_POINT_LIGHTS=%d\n", MAX_POINT_LIGHTS);
			break;
		}

		Vector4Copy(entity->color, light->color);
		Vector4Copy(entity->origin, light->origin);

		// FIXME ???
		light->origin[3] = 50.f;
		light->color[3] = 1.f;
	}

	for (int i = 0; i < MAX_ELIGHTS; ++i) {
		const dlight_t *dlight = gEngine.GetEntityLight(i);
		if (!addDlight(dlight)) {
			gEngine.Con_Printf(S_ERROR "Too many elights, MAX_POINT_LIGHTS=%d\n", MAX_POINT_LIGHTS);
			break;
		}
	}

	for (int i = 0; i < MAX_DLIGHTS; ++i) {
		const dlight_t *dlight = gEngine.GetDynamicLight(i);
		if (!addDlight(dlight)) {
			gEngine.Con_Printf(S_ERROR "Too many dlights, MAX_POINT_LIGHTS=%d\n", MAX_POINT_LIGHTS);
			break;
		}
	}

	{
		static qboolean have_surf = false;
		if (!have_surf) {
			for (int i = 0; i < MAX_MAP_LEAFS; ++i ) {
				const vk_light_leaf_t *lleaf = g_lights_bsp.leaves + i;
				int point = 0, spot = 0, surface = 0;
				if (lleaf->num_lights == 0)
					continue;

				for (int j = 0; j < lleaf->num_lights; ++j) {
					switch (lleaf->light[j].type) {
						case LightTypePoint: ++point; break;
						case LightTypeSpot: ++spot; break;
						case LightTypeSurface: have_surf = true; ++surface; break;
					}
				}

				gEngine.Con_Printf("\tLeaf %d, lights %d: spot=%d point=%d surface=%d\n", i, lleaf->num_lights, spot, point, surface);
			}

#if 1
		// Print light grid stats
		gEngine.Con_Reportf("Emissive surfaces found: %d\n", g_lights.num_emissive_surfaces);

		{
			#define GROUPSIZE 4
			int histogram[1 + (MAX_VISIBLE_SURFACE_LIGHTS + GROUPSIZE - 1) / GROUPSIZE] = {0};
			for (int i = 0; i < g_lights.map.grid_cells; ++i) {
				const vk_lights_cell_t *cluster = g_lights.cells + i;
				const int hist_index = cluster->num_emissive_surfaces ? 1 + cluster->num_emissive_surfaces / GROUPSIZE : 0;
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
			for (int i = 0; i < g_lights.map.grid_cells; ++i) {
				const vk_lights_cell_t *cluster = g_lights.cells + i;
				if (cluster->num_emissive_surfaces > 0) {
					gEngine.Con_Reportf(" cluster %d: emissive_surfaces=%d\n", i, cluster->num_emissive_surfaces);
				}
			}
		}
#endif
		}

		/* if (have_surf) */
		/* 	exit(0); */
	}
}

void VK_LightsShutdown( void ) {
}
