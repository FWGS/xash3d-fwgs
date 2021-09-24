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
		string key, value;

		pos = gEngine.COM_ParseFile(pos, key);
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

static struct {
	vk_light_leaf_t leaves[MAX_MAP_LEAFS];
} g_lights_bsp;

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

// FIXME copied from mod_bmodel.c
// TODO would it be possible to not decompress each time, but instead get a list of all leaves?
static byte		g_visdata[(MAX_MAP_LEAFS+7)/8];	// intermediate buffer
byte *Mod_DecompressPVS( const byte *in, int visbytes )
{
	byte	*out;
	int	c;

	out = g_visdata;

	if( !in )
	{
		// no vis info, so make all visible
		while( visbytes )
		{
			*out++ = 0xff;
			visbytes--;
		}
		return g_visdata;
	}

	do
	{
		if( *in )
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;

		while( c )
		{
			*out++ = 0;
			c--;
		}
	} while( out - g_visdata < visbytes );

	return g_visdata;
}

#define PR(...) gEngine.Con_Reportf(__VA_ARGS__)

static void DumpLeaves( void ) {
	model_t	*map = gEngine.pfnGetModelByIndex( 1 );
	const world_static_t *world = gEngine.GetWorld();
	ASSERT(map);

	PR("visbytes=%d leafs: %d:\n", world->visbytes, map->numleafs);
	for (int i = 0; i < map->numleafs; ++i) {
		const mleaf_t* leaf = map->leafs + i;
		PR("  %d: contents=%d numsurfaces=%d cluster=%d\n",
			i, leaf->contents, leaf->nummarksurfaces, leaf->cluster);

		// TODO: mark which surfaces belong to which leaves
		// TODO: figure out whether this relationship is stable (surface belongs to only one leaf)

		// print out PVS
		{
			int pvs_count = 0;
			const byte *visdata = Mod_DecompressPVS(leaf->compressed_vis, world->visbytes);
			if (!visdata) continue;
			PR("    PVS:");
			for (int j = 0; j < map->numleafs; ++j) {
				if (CHECKVISBIT(visdata, map->leafs[j].cluster /* FIXME cluster (j+1) or j??!?!*/)) {
					pvs_count++;
					PR(" %d", j);
				}
			}
			PR(" TOTAL: %d\n", pvs_count);
		}
	}
}

typedef struct {
	model_t	*map;
	const world_static_t *world;
	FILE *f;
} traversal_context_t;

static void visitLeaf(const mleaf_t *leaf, const mnode_t *parent, const traversal_context_t *ctx) {
	const int parent_index = parent - ctx->map->nodes;
	int pvs_count = 0;
	const byte *visdata = Mod_DecompressPVS(leaf->compressed_vis, ctx->world->visbytes);
	int num_emissive = 0;

	// ??? empty leaf?
	if (leaf->cluster < 0) // || leaf->nummarksurfaces == 0)
		return;

	fprintf(ctx->f, "\"N%d\" -> \"L%d\"\n", parent_index, leaf->cluster);
	for (int i = 0; i < leaf->nummarksurfaces; ++i) {
		const msurface_t *surf = leaf->firstmarksurface[i];
		const int surf_index = surf - ctx->map->surfaces;
		const int texture_num = surf->texinfo->texture->gl_texturenum;
		const qboolean emissive = texture_num >= 0 && g_lights.map.emissive_textures[texture_num].set;

		if (emissive) num_emissive++;

		fprintf(ctx->f, "L%d -> S%d [color=\"#%s\"; dir=\"none\"];\n",
			leaf->cluster, surf_index, emissive ? "ff0000ff" : "00000040");
	}

	if (!visdata)
		return;

	for (int j = 0; j < ctx->map->numleafs; ++j) {
		if (CHECKVISBIT(visdata, ctx->map->leafs[j].cluster)) {
			pvs_count++;
		}
	}

	fprintf(ctx->f, "\"L%d\" [label=\"Leaf cluster %d\\npvs_count: %d\\nummarksurfaces: %d\\n num_emissive: %d\"; style=filled; fillcolor=\"%s\"; ];\n",
		leaf->cluster, leaf->cluster, pvs_count, leaf->nummarksurfaces, num_emissive,
		num_emissive > 0 ? "red" : "transparent"
		);
}

static void visitNode(const mnode_t *node, const mnode_t *parent, const traversal_context_t *ctx) {
	if (node->contents < 0) {
		visitLeaf((const mleaf_t*)node, parent, ctx);
	} else {
		const int parent_index = parent ? parent - ctx->map->nodes : -1;
		const int node_index = node - ctx->map->nodes;
		fprintf(ctx->f, "\"N%d\" -> \"N%d\"\n", parent_index, node_index);
		fprintf(ctx->f, "\"N%d\" [label=\"numsurfaces: %d\\nfirstsurface: %d\"];\n",
			node_index, node->numsurfaces, node->firstsurface);
		visitNode(node->children[0], node, ctx);
		visitNode(node->children[1], node, ctx);
	}
}

static void traverseBSP( void ) {
	const traversal_context_t ctx = {
		.map = gEngine.pfnGetModelByIndex( 1 ),
		.world = gEngine.GetWorld(),
		.f = fopen("bsp.dot", "w"),
	};

	fprintf(ctx.f, "digraph bsp { node [shape=box];\n");
	visitNode(ctx.map->nodes, NULL, &ctx);
	fprintf(ctx.f,
		"subgraph surfaces {rank = max; style= filled; color = lightgray;\n");
	for (int i = 0; i < ctx.map->numsurfaces; i++) {
		const msurface_t *surf = ctx.map->surfaces + i;
		const int texture_num = surf->texinfo->texture->gl_texturenum;
		fprintf(ctx.f, "S%d [rank=\"max\"; label=\"S%d\\ntexture: %s\\nnumedges: %d\\ntexture_num=%d\"; style=filled; fillcolor=\"%s\";];\n",
			i, i,
			surf->texinfo && surf->texinfo->texture ? surf->texinfo->texture->name : "NULL",
			surf->numedges, texture_num,
			(texture_num >= 0 && g_lights.map.emissive_textures[texture_num].set) ? "red" : "transparent" );
	}
	fprintf(ctx.f, "}\n}\n");
	fclose(ctx.f);
	//exit(0);
}

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
	vk_lights_cell_t *cluster = g_lights.cells + cell_index;

	if (light_cell[0] < 0 || light_cell[1] < 0 || light_cell[2] < 0
		|| (light_cell[0] >= g_lights.map.grid_size[0])
		|| (light_cell[1] >= g_lights.map.grid_size[1])
		|| (light_cell[2] >= g_lights.map.grid_size[2]))
		return;

	if (cluster->num_emissive_surfaces == MAX_VISIBLE_SURFACE_LIGHTS) {
		gEngine.Con_Printf(S_ERROR "Cluster %d,%d,%d(%d) ran out of emissive surfaces slots\n",
			light_cell[0], light_cell[1],  light_cell[2], cell_index
			);
		return;
	}

	cluster->emissive_surfaces[cluster->num_emissive_surfaces] = emissive_surface_index;
	++cluster->num_emissive_surfaces;
}

const vk_emissive_surface_t *VK_LightsAddEmissiveSurface( const struct vk_render_geometry_s *geom, const matrix3x4 *transform_row ) {
	const int texture_num = geom->texture; // Animated texture
	if (!geom->surf)
		return NULL; // TODO break? no surface means that model is not brush

	if (geom->material != kXVkMaterialSky && geom->material != kXVkMaterialEmissive && !g_lights.map.emissive_textures[texture_num].set)
		return NULL;

	// FIXME how does one get an mleaf_t from msurface_t ????!
	{
		vec3_t origin;
		Matrix3x4_VectorTransform(*transform_row, geom->surf->info->origin, origin);
		lbspAddLightByOrigin( LightTypeSurface, origin );
	}

	if (g_lights.num_emissive_surfaces < 256) {
		// Insert into emissive surfaces
		vk_emissive_surface_t *esurf = g_lights.emissive_surfaces + g_lights.num_emissive_surfaces;
		esurf->kusok_index = geom->kusok_index;
		if (geom->material != kXVkMaterialSky && geom->material != kXVkMaterialEmissive) {
			VectorCopy(g_lights.map.emissive_textures[texture_num].emissive, esurf->emissive);
		} else {
			// TODO per-map sky emissive
			VectorSet(esurf->emissive, 1000.f, 1000.f, 1000.f);
		}
		Matrix3x4_Copy(esurf->transform, *transform_row);

		// Insert into light grid cell
		{
			vec3_t light_cell;
			float effective_radius;
			const float intensity_threshold = 1.f / 255.f; // TODO better estimate
			const float intensity = Q_max(Q_max(esurf->emissive[0], esurf->emissive[1]), esurf->emissive[2]);
			ASSERT(geom->surf->info);
			// FIXME using just origin is incorrect
			{
				vec3_t light_cell_f;
				vec3_t origin;
				Matrix3x4_VectorTransform(*transform_row, geom->surf->info->origin, origin);
				VectorDivide(origin, LIGHT_GRID_CELL_SIZE, light_cell_f);
				light_cell[0] = floorf(light_cell_f[0]);
				light_cell[1] = floorf(light_cell_f[1]);
				light_cell[2] = floorf(light_cell_f[2]);
			}
			VectorSubtract(light_cell, g_lights.map.grid_min_cell, light_cell);

			ASSERT(light_cell[0] >= 0);
			ASSERT(light_cell[1] >= 0);
			ASSERT(light_cell[2] >= 0);
			ASSERT(light_cell[0] < g_lights.map.grid_size[0]);
			ASSERT(light_cell[1] < g_lights.map.grid_size[1]);
			ASSERT(light_cell[2] < g_lights.map.grid_size[2]);

			//		3.3	Add it to those cells
			effective_radius = sqrtf(intensity / intensity_threshold);
			{
				const int irad = ceilf(effective_radius / LIGHT_GRID_CELL_SIZE);
				//gEngine.Con_Reportf("Emissive surface %d: max intensity: %f; eff rad: %f; cell rad: %d\n", i, intensity, effective_radius, irad);
				for (int x = -irad; x <= irad; ++x)
					for (int y = -irad; y <= irad; ++y)
						for (int z = -irad; z <= irad; ++z) {
							const int cell[3] = { light_cell[0] + x, light_cell[1] + y, light_cell[2] + z};
							// TODO culling, ...
							// 		3.1 Compute light size and intensity (?)
							//		3.2 Compute which cells it might affect
							//			- light orientation
							//			- light intensity
							//			- PVS
							addSurfaceLightToCell(cell, g_lights.num_emissive_surfaces);
						}
			}
		}

		++g_lights.num_emissive_surfaces;
		return esurf;
	}

	++g_lights.num_emissive_surfaces;
	return NULL;
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
		qboolean have_surf = false;
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

		if (have_surf)
			exit(0);
	}

#if 0
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

void VK_LightsShutdown( void ) {
}
