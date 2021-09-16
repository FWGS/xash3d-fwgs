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

typedef struct {
	const char *name;
	int r, g, b, intensity;
} vk_light_texture_rad_data;

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

static void parseStaticLightEntities( void ) {
	const model_t* const world = gEngine.pfnGetModelByIndex( 1 );
	char *pos;
	enum {
		Unknown,
		Light, LightSpot,
	} classname = Unknown;
	struct {
		vec3_t origin;
		vec3_t color;
		//float radius;
		int style;
	} light = {0};
	enum {
		HaveOrigin = 1,
		HaveColor = 2,
		//HaveStyle = 4,
		HaveClass = 8,
		HaveAll = HaveOrigin | HaveColor | HaveClass,
	};
	unsigned int have = 0;

	ASSERT(world);

	pos = world->entities;
	for (;;) {
		string key, value;

		pos = gEngine.COM_ParseFile(pos, key);
		if (!pos)
			break;
		if (key[0] == '{') {
			classname = Unknown;
			have = 0;
			continue;
		}
		if (key[0] == '}') {
			// TODO handle entity
			if (have != HaveAll)
				continue;
			if (classname != Light && classname != LightSpot)
				continue;

			// TODO store this
			//VK_RenderAddStaticLight(light.origin, light.color);
			continue;
		}

		pos = gEngine.COM_ParseFile(pos, value);
		if (!pos)
			break;

		if (Q_strcmp(key, "origin") == 0) {
			const int components = sscanf(value, "%f %f %f",
				&light.origin[0],
				&light.origin[1],
				&light.origin[2]);
			if (components == 3)
				have |= HaveOrigin;
		} else
		if (Q_strcmp(key, "_light") == 0) {
			float scale = 1.f / 255.f;
			const int components = sscanf(value, "%f %f %f %f",
				&light.color[0],
				&light.color[1],
				&light.color[2],
				&scale);
			if (components == 1) {
				light.color[2] = light.color[1] = light.color[0] = light.color[0] / 255.f;
				have |= HaveColor;
			} else if (components == 4) {
				scale /= 255.f * 255.f;
				light.color[0] *= scale;
				light.color[1] *= scale;
				light.color[2] *= scale;
				have |= HaveColor;
			} else if (components == 3) {
				light.color[0] *= scale;
				light.color[1] *= scale;
				light.color[2] *= scale;
				have |= HaveColor;
			}
		} else if (Q_strcmp(key, "classname") == 0) {
			if (Q_strcmp(value, "light") == 0)
				classname = Light;
			else if (Q_strcmp(value, "light_spot") == 0)
				classname = LightSpot;
			have |= HaveClass;
		}
	}
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
	loadRadData( map, "rad/lights.rad" );

	{
		// Extract <mapname> from maps/<mapname>.bsp
		char mapname[sizeof(map->name)];
		int name_len;

		const char *name_begin = Q_strrchr(map->name, '/');
		if (name_begin)
			++name_begin;
		else
			name_begin = map->name;

		name_len = Q_strlen(name_begin);

		// Strip ".bsp" suffix
		if (name_len > 4 && 0 == Q_stricmp(name_begin + name_len - 4, ".bsp"))
			name_len -= 4;

		loadRadData( map, "rad/%.*s.rad", name_len, name_begin );
	}
}

void VK_LightsFrameInit( void )
{
	g_lights.num_emissive_surfaces = 0;
	memset(g_lights.cells, 0, sizeof(g_lights.cells));
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
			int cluster_index;
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

void VK_LightsFrameFinalize( void )
{
	if (g_lights.num_emissive_surfaces > UINT8_MAX) {
		gEngine.Con_Printf(S_ERROR "Too many emissive surfaces found: %d; some areas will be dark\n", g_lights.num_emissive_surfaces);
		g_lights.num_emissive_surfaces = UINT8_MAX;
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
