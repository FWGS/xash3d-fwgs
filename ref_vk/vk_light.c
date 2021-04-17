#include "vk_light.h"
#include "vk_textures.h"

#include "mod_local.h"
#include "xash3d_mathlib.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

vk_potentially_visible_lights_t g_lights = {0};

vk_emissive_texture_table_t g_emissive_texture_table[MAX_TEXTURES];

static struct {
	const char *name;
	int r, g, b, intensity;
} hack_valve_rad_table[] = {
	"+0~GENERIC65", 255, 255, 255, 750,
	"+0~GENERIC85", 110, 140, 235, 20000,
	"+0~GENERIC86", 255, 230, 125, 10000,
	"+0~GENERIC86B", 60, 220, 170, 20000,
	"+0~GENERIC86R", 128, 0, 0, 60000,
	"GENERIC87A", 100, 255, 100, 1000,
	"GENERIC88A", 255, 100, 100, 1000,
	"GENERIC89A", 40, 40, 130, 1000,
	"GENERIC90A", 200, 255, 200, 1000,
	"GENERIC105", 255, 100, 100, 1000,
	"GENERIC106", 120, 120, 100, 1000,
	"GENERIC107", 180, 50, 180, 1000,
	"GEN_VEND1", 50, 180, 50, 1000,
	"EMERGLIGHT", 255, 200, 100, 50000,
	"+0~FIFTS_LGHT01", 160, 170, 220, 4000,
	"+0~FIFTIES_LGT2", 160, 170, 220, 5000,
	"+0~FIFTS_LGHT4", 160, 170, 220, 4000,
	"+0~LIGHT1", 40, 60, 150, 3000,
	"+0~LIGHT3A", 180, 180, 230, 10000,
	"+0~LIGHT4A", 200, 190, 130, 11000,
	"+0~LIGHT5A", 80, 150, 200, 10000,
	"+0~LIGHT6A", 150, 5, 5, 25000,
	"+0~TNNL_LGT1", 240, 230, 100, 10000,
	"+0~TNNL_LGT2", 190, 255, 255, 12000,
	"+0~TNNL_LGT3", 150, 150, 210, 17000,
	"+0~TNNL_LGT4", 170, 90, 40, 10000,
	"+0LAB1_W6D", 165, 230, 255, 4000,
	"+0LAB1_W6", 150, 160, 210, 8800,
	"+0LAB1_W7", 245, 240, 210, 4000,
	"SKKYLITE", 165, 230, 255, 1000,
	"+0~DRKMTLS1", 205, 0, 0, 6000,
	"+0~DRKMTLGT1", 200, 200, 180, 6000,
	"+0~DRKMTLS2", 150, 120, 20, 30000,
	"+0~DRKMTLS2C", 255, 200, 100, 50000,
	"+0DRKMTL_SCRN", 60, 80, 255, 10000,
	"~LAB_CRT9A", 225, 150, 150, 100,
	"~LAB_CRT9B", 100, 100, 255, 100,
	"~LAB_CRT9C", 100, 200, 150, 100,
	"~LIGHT3A", 190, 20, 20, 3000,
	"~LIGHT3B", 155, 155, 235, 2000,
	"~LIGHT3C", 220, 210, 150, 2500,
	"~LIGHT3E", 90, 190, 140, 6000,
	"C1A3C_MAP", 100, 100, 255, 100,
	"FIFTIES_MON1B", 100, 100, 180, 30,
	"+0~LAB_CRT8", 50, 50, 255, 100,
	"ELEV2_CIEL", 255, 200, 100, 800,
	"YELLOW", 255, 200, 100, 2000,
	"RED", 255, 0, 0, 1000,
	"+0~GENERIC65", 255, 255, 255, 14000,
	"+0~LIGHT3A", 255, 255, 255, 25000,
	"~LIGHT3B", 84, 118, 198, 14000,
	"+0~DRKMTLS2C", 255, 200, 100, 10,
	"~LIGHT3A", 190, 20, 20, 14000,
	"~LIGHT3C", 198, 215, 74, 14000,
	"+0~LIGHT4A", 231, 223, 82, 20000,
	"+0~FIFTS_LGHT06", 255, 255, 255, 8000,
	"+0~FIFTIES_LGT2", 255, 255, 255, 20000,
	"~SPOTYELLOW", 189, 231, 253, 20000,
	"~SPOTBLUE", 7, 163, 245, 18000,
	"+0~DRKMTLS1", 255, 10, 10, 14000,
	"CRYS_2TOP", 171, 254, 168, 14000,
	"+0~GENERIC85", 11000, 16000, 22000, 255,
	"DRKMTL_SCRN3", 1, 111, 220, 500,
	"+0~LIGHT3A", 255, 255, 255, 25000,
	"~LIGHT3B", 84, 118, 198, 14000,
	"+0~DRKMTLS2C", 255, 200, 100, 10,
	"~LIGHT3A", 190, 20, 20, 14000,
	"~LIGHT3C", 198, 215, 74, 14000,
	"+0~LIGHT4A", 231, 223, 82, 20000,
	"+0~FIFTS_LGHT06", 255, 255, 255, 8000,
	"+0~FIFTIES_LGT2", 255, 255, 255, 20000,
	"~SPOTYELLOW", 189, 231, 253, 20000,
	"+0~DRKMTLS1", 255, 10, 10, 14000,
	"LITEPANEL1", 190, 170, 120, 2500,
	"+0BUTTONLITE", 255, 255, 255, 25,
	"+ABUTTONLITE", 255, 255, 255, 25,
	"+0~FIFTS_LGHT3", 160, 170, 220, 4000,
	"~LIGHT5F", 200, 190, 140, 2500,
	"+A~FIFTIES_LGT2", 160, 170, 220, 4000,
	"~LIGHT3F", 200, 190, 140, 2500,
	"~SPOTBLUE", 7, 163, 245, 18000,
	"+0~FIFTS_LGHT5", 255, 255, 255, 10000,
	"+0~LAB1_CMP2", 255, 255, 255, 20,
	"LAB1_COMP3D", 255, 255, 255, 20,
	"~LAB1_COMP7", 255, 255, 255, 20,
	"+0~GENERIC65", 255, 255, 255, 750,
	"+0~LAB1_CMP2", 255, 255, 255, 20,
	"LAB1_COMP3D", 255, 255, 255, 20,
	"~LAB1_COMP7", 255, 255, 255, 20,
};

typedef struct {
	const char *name;
	int r, g, b, intensity;
} vk_light_texture_rad_data;

static void loadRadData( const model_t *map, const char *filename ) {
	fs_offset_t size;
	const byte *data, *buffer = gEngine.COM_LoadFile( filename, &size, false);

	memset(g_emissive_texture_table, 0, sizeof(g_emissive_texture_table));

	if (!buffer) {
		gEngine.Con_Printf(S_ERROR "Couldn't load rad data from file %s, the map will be completely black\n", filename);
		return;
	}

	data = buffer;
	for (;;) {
		string name;
		float r, g, b, scale;

		int num = sscanf(data, "%s %f %f %f %f", name, &r, &g, &b, &scale);
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
			gEngine.Con_Printf( "skipping rad entry %s\n", num ? name : "" );
			num = 0;
		}

		if (num != 0) {
			gEngine.Con_Printf("rad entry: %s %f %f %f\n", name, r, g, b);

			{
				const char *tex_name_without_prefix = Q_strchr(name, '/');
				if (!tex_name_without_prefix)
					tex_name_without_prefix = name;
				else
					tex_name_without_prefix += 1;

				// TODO we also have textures in format Q_sprintf(name, "halflife.wad/%s.mip", hack_valve_rad_table[i].name);
				string texname;
				Q_sprintf(texname, "#%s:%s.mip", map->name, tex_name_without_prefix);

				const int tex_id = VK_FindTexture(texname);
				if (tex_id) {
					ASSERT(tex_id < MAX_TEXTURES);

					g_emissive_texture_table[tex_id].emissive[0] = r;
					g_emissive_texture_table[tex_id].emissive[1] = g;
					g_emissive_texture_table[tex_id].emissive[2] = b;
					g_emissive_texture_table[tex_id].set = true;
				}
			}
		}

		data = Q_strchr(data, '\n');
		if (!data)
			break;
		while (!isalnum(*data)) ++data;
	}

	Mem_Free(buffer);
}

// TODO load from .rad file
static void initHackRadTable( void ) {
	memset(g_emissive_texture_table, 0, sizeof(g_emissive_texture_table));

	for (int i = 0; i < ARRAYSIZE(hack_valve_rad_table); ++i) {
		const float scale = hack_valve_rad_table[i].intensity / (255.f * 255.f);
		int tex_id;
		char name[256];
		Q_sprintf(name, "halflife.wad/%s.mip", hack_valve_rad_table[i].name);
		tex_id = VK_FindTexture(name);
		if (!tex_id)
			continue;
		ASSERT(tex_id < MAX_TEXTURES);

		g_emissive_texture_table[tex_id].emissive[0] = hack_valve_rad_table[i].r * scale;
		g_emissive_texture_table[tex_id].emissive[1] = hack_valve_rad_table[i].g * scale;
		g_emissive_texture_table[tex_id].emissive[2] = hack_valve_rad_table[i].b * scale;
		g_emissive_texture_table[tex_id].set = true;
	}
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
		const qboolean emissive = texture_num >= 0 && g_emissive_texture_table[texture_num].set;

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
			(texture_num >= 0 && g_emissive_texture_table[texture_num].set) ? "red" : "transparent" );
	}
	fprintf(ctx.f, "}\n}\n");
	fclose(ctx.f);
	//exit(0);
}

static void buildStaticMapEmissiveSurfaces( void ) {
	const model_t *map = gEngine.pfnGetModelByIndex( 1 );

	// Initialize emissive surface table
	for (int i = map->firstmodelsurface; i < map->firstmodelsurface + map->nummodelsurfaces; ++i) {
		const msurface_t *surface = map->surfaces + i;
		const int texture_num = surface->texinfo->texture->gl_texturenum;

		// TODO animated textures ???
		if (!g_emissive_texture_table[texture_num].set)
			continue;

		if (g_lights.num_emissive_surfaces < 256) {
			vk_emissive_surface_t *esurf = g_lights.emissive_surfaces + g_lights.num_emissive_surfaces;

			esurf->surface_index = i;
			VectorCopy(g_emissive_texture_table[texture_num].emissive, esurf->emissive);
		}

		++g_lights.num_emissive_surfaces;
	}

	gEngine.Con_Reportf("Emissive surfaces found: %d\n", g_lights.num_emissive_surfaces);

	if (g_lights.num_emissive_surfaces > UINT8_MAX + 1) {
		gEngine.Con_Printf(S_ERROR "Too many emissive surfaces found: %d; some areas will be dark\n", g_lights.num_emissive_surfaces);
		g_lights.num_emissive_surfaces = UINT8_MAX + 1;
	}
}

static void addSurfaceLightToCell( const int light_cell[3], int emissive_surface_index ) {
	const uint cluster_index = light_cell[0] + light_cell[1] * g_lights.grid.size[0] + light_cell[2] * g_lights.grid.size[0] * g_lights.grid.size[1];
	vk_light_cluster_t *cluster = g_lights.grid.cells + cluster_index;

	if (light_cell[0] < 0 || light_cell[1] < 0 || light_cell[2] < 0
		|| (light_cell[0] >= g_lights.grid.size[0])
		|| (light_cell[1] >= g_lights.grid.size[1])
		|| (light_cell[2] >= g_lights.grid.size[2]))
		return;

	if (cluster->num_emissive_surfaces == MAX_VISIBLE_SURFACE_LIGHTS) {
		gEngine.Con_Printf(S_ERROR "Cluster %d,%d,%d(%d) ran out of emissive surfaces slots\n",
			light_cell[0], light_cell[1],  light_cell[2], cluster_index
			);
		return;
	}

	cluster->emissive_surfaces[cluster->num_emissive_surfaces] = emissive_surface_index;
	++cluster->num_emissive_surfaces;
}

static void buildStaticMapLightsGrid( void ) {
	const model_t	*map = gEngine.pfnGetModelByIndex( 1 );

	// 1. Determine map bounding box (and optimal grid size?)
		// map->mins, maxs
	vec3_t map_size, min_cell, max_cell;
	VectorSubtract(map->maxs, map->mins, map_size);

	VectorDivide(map->mins, LIGHT_GRID_CELL_SIZE, min_cell);
	min_cell[0] = floorf(min_cell[0]);
	min_cell[1] = floorf(min_cell[1]);
	min_cell[2] = floorf(min_cell[2]);
	VectorCopy(min_cell, g_lights.grid.min_cell);

	VectorDivide(map->maxs, LIGHT_GRID_CELL_SIZE, max_cell);
	max_cell[0] = ceilf(max_cell[0]);
	max_cell[1] = ceilf(max_cell[1]);
	max_cell[2] = ceilf(max_cell[2]);

	VectorSubtract(max_cell, min_cell, g_lights.grid.size);
	g_lights.grid.num_cells = g_lights.grid.size[0] * g_lights.grid.size[1] * g_lights.grid.size[2];
	ASSERT(g_lights.grid.num_cells < MAX_LIGHT_CLUSTERS);

	gEngine.Con_Reportf("Map mins:(%f, %f, %f), maxs:(%f, %f, %f), size:(%f, %f, %f), min_cell:(%f, %f, %f) cells:(%d, %d, %d); total: %d\n",
		map->mins[0], map->mins[1], map->mins[2],
		map->maxs[0], map->maxs[1], map->maxs[2],
		map_size[0], map_size[1], map_size[2],
		min_cell[0], min_cell[1], min_cell[2],
		g_lights.grid.size[0],
		g_lights.grid.size[1],
		g_lights.grid.size[2],
		g_lights.grid.num_cells
	);

	// 3. For all light sources
	for (int i = 0; i < g_lights.num_emissive_surfaces; ++i) {
		const vk_emissive_surface_t *emissive = g_lights.emissive_surfaces + i;
		const msurface_t *surface = map->surfaces + emissive->surface_index;
		int cluster_index;
		vk_light_cluster_t *cluster;
		vec3_t light_cell;
		ASSERT(surface->info);

		// FIXME using just origin is incorrect
		{
			vec3_t light_cell_f;
			VectorDivide(surface->info->origin, LIGHT_GRID_CELL_SIZE, light_cell_f);
			light_cell[0] = floorf(light_cell_f[0]);
			light_cell[1] = floorf(light_cell_f[1]);
			light_cell[2] = floorf(light_cell_f[2]);
		}
		VectorSubtract(light_cell, g_lights.grid.min_cell, light_cell);

		ASSERT(light_cell[0] >= 0);
		ASSERT(light_cell[1] >= 0);
		ASSERT(light_cell[2] >= 0);
		ASSERT(light_cell[0] < g_lights.grid.size[0]);
		ASSERT(light_cell[1] < g_lights.grid.size[1]);
		ASSERT(light_cell[2] < g_lights.grid.size[2]);

		//		3.3	Add it to those cells
		// TODO radius
		for (int x = -2; x <= 2; ++x)
			for (int y = -2; y <= 2; ++y)
				for (int z = -2; z <= 2; ++z) {
					const int cell[3] = { light_cell[0] + x, light_cell[1] + y, light_cell[2] + z};
					// TODO culling, ...
					// 		3.1 Compute light size and intensity (?)
					//		3.2 Compute which cells it might affect
					//			- light orientation
					//			- light intensity
					//			- PVS
					addSurfaceLightToCell(cell, i);
				}
	}

	// Print light grid stats
	{
		#define GROUPSIZE 4
		int histogram[1 + (MAX_VISIBLE_SURFACE_LIGHTS + GROUPSIZE - 1) / GROUPSIZE] = {0};
		for (int i = 0; i < g_lights.grid.num_cells; ++i) {
			const vk_light_cluster_t *cluster = g_lights.grid.cells + i;
			const int hist_index = cluster->num_emissive_surfaces ? 1 + cluster->num_emissive_surfaces / GROUPSIZE : 0;
			histogram[hist_index]++;
		}

		gEngine.Con_Reportf("Built %d light clusters. Stats:\n", g_lights.grid.num_cells);
		gEngine.Con_Reportf("  0: %d\n", histogram[0]);
		for (int i = 1; i < ARRAYSIZE(histogram); ++i)
			gEngine.Con_Reportf("  %d-%d: %d\n",
				(i - 1) * GROUPSIZE,
				i * GROUPSIZE - 1,
				histogram[i]);
	}

	{
		for (int i = 0; i < g_lights.grid.num_cells; ++i) {
			const vk_light_cluster_t *cluster = g_lights.grid.cells + i;
			if (cluster->num_emissive_surfaces > 0) {
				gEngine.Con_Reportf(" cluster %d: emissive_surfaces=%d\n", i, cluster->num_emissive_surfaces);
			}
		}
	}
}

void VK_LightsLoadMap( void ) {
	const model_t *map = gEngine.pfnGetModelByIndex( 1 );

	parseStaticLightEntities();

	g_lights.num_emissive_surfaces = 0;
	g_lights.grid.num_cells = 0;
	memset(g_lights.grid.size, 0, sizeof(g_lights.grid.size));

	// FIXME ...
	//initHackRadTable();

	// Load RAD data based on map name
	loadRadData( map, "rad/lights_anomalous_materials.rad" );

	buildStaticMapEmissiveSurfaces();
	buildStaticMapLightsGrid();
}

void VK_LightsShutdown( void ) {
}
