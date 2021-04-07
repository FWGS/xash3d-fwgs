#include "vk_light.h"
#include "vk_render.h"
#include "vk_textures.h"

#include "mod_local.h"
#include "xash3d_mathlib.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
			VK_RenderAddStaticLight(light.origin, light.color);
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

vk_potentially_visible_lights_t g_lights = {0};

static void initPVL( void ) {
	const model_t	*map = gEngine.pfnGetModelByIndex( 1 );

	if (g_lights.leaves) {
		Mem_Free(g_lights.leaves);
	}

	g_lights.num_leaves = map->numleafs;
	g_lights.leaves = Mem_Malloc(vk_core.pool, g_lights.num_leaves * sizeof(vk_light_leaf_t));
	g_lights.frame_number = -1;

	g_lights.num_emissive_surfaces = 0;
}

void VK_LightsLoad( void ) {
	parseStaticLightEntities();
	initPVL();

	// FIXME ...
	initHackRadTable();
}

void VK_LightsBakePVL( int frame_number ) {
	const model_t	*map = gEngine.pfnGetModelByIndex( 1 );
	const world_static_t *world = gEngine.GetWorld();

	// "Self" lights that belong to a particular leaf
	// TODO use temp pool
	vk_light_leaf_t *eigenlicht = Mem_Malloc(vk_core.pool, g_lights.num_leaves * sizeof(vk_light_leaf_t));
	int *surface_to_emissive_surface_map = Mem_Malloc(vk_core.pool, map->numsurfaces * sizeof(int));

	// Initialize emissive surface table
	for (int i = 0; i < map->numsurfaces; ++i) {
		const msurface_t *surface = map->surfaces + i;
		const int texture_num = surface->texinfo->texture->gl_texturenum;

		surface_to_emissive_surface_map[i] = -1;

		// TODO animated textures ???
		if (!g_emissive_texture_table[texture_num].set)
			continue;

		if (g_lights.num_emissive_surfaces < 256) {
			vk_emissive_surface_t *esurf = g_lights.emissive_surfaces + g_lights.num_emissive_surfaces;

			esurf->surface_index = i;
			VectorCopy(g_emissive_texture_table[texture_num].emissive, esurf->emissive);

			surface_to_emissive_surface_map[i] = g_lights.num_emissive_surfaces;
		}

		++g_lights.num_emissive_surfaces;
	}

	gEngine.Con_Reportf("Emissive surfaces found: %d\n", g_lights.num_emissive_surfaces);

	if (g_lights.num_emissive_surfaces > UINT8_MAX + 1) {
		gEngine.Con_Printf(S_ERROR "Too many emissive surfaces found: %d; some areas will be dark\n", g_lights.num_emissive_surfaces);
		g_lights.num_emissive_surfaces = UINT8_MAX + 1;
	}

	//DumpLeaves();
	traverseBSP();

	// 1. For each leaf collect all emissive surfaces (dlights TODO)
	//   -> set of all emissive surfaces per leaf:
	//   		0 for most of leaves
	//   		1-7 for a few of them
	//
	// 2. For each leaf collect all visible leaves (use PVS)
	// 		For each potentially visible leaf collect all emissive surfaces
	// 	 -> set of all potentially visible surfaces for each leaf
	//


	// First pass: find lights belonging to each leaf
	// TODO this will include wagonchik geometry, which is not what we want
	// TODO hack: do only the first submodel?
	// Lights construction should happen on render with entity knowledge
	for (int i = 0; i < map->numleafs; ++i) {
		vk_light_leaf_t *eigen = eigenlicht + i;
		const mleaf_t *leaf = map->leafs + i;
		int num_surface_lights = 0;

		eigen->num_dlights = eigen->num_slights = 0;

		if (leaf->cluster < 0)
			continue;

		ASSERT(leaf->contents < 0);
		ASSERT(leaf->parent);
		//ASSERT(leaf->nummarksurfaces == leaf->parent->numsurfaces);

		for (int j = 0; j < leaf->nummarksurfaces; ++j) {
			const msurface_t *surface = leaf->firstmarksurface[j];
			const int surface_index = surface - map->surfaces;
			ASSERT(surface_index >= 0);
			ASSERT(surface_index < map->numsurfaces);

			// TODO entity transformation
			// TODO ^^^ we need to bake it per-entity probably

			{
				const int emissive_index = surface_to_emissive_surface_map[surface_index];
				if (emissive_index < 0)
					continue;

				ASSERT(emissive_index < 256);

				++num_surface_lights;
				if (eigen->num_slights == MAX_VISIBLE_SURFACE_LIGHTS)
					continue;

				eigen->slights[eigen->num_slights++] = emissive_index;
			}
		}

		if (num_surface_lights > 0)
			gEngine.Con_Reportf("Leaf %d surface lights %d\n", i, num_surface_lights);

		if (num_surface_lights > MAX_VISIBLE_SURFACE_LIGHTS)
			gEngine.Con_Printf(S_ERROR "Too many surface lights %d for leaf %d\n", num_surface_lights, i);

		// TODO dlights
	}

	// Second pass: find lights visible from each leaf
	for (int i = 0; i < map->numleafs; ++i) {
		const mleaf_t *leaf = map->leafs + i;
		vk_light_leaf_t *lights = g_lights.leaves + i;
		// TODO we should not decompress PVS, as it might be faster to interate through compressed directly
		const byte *visdata = Mod_DecompressPVS(leaf->compressed_vis, world->visbytes);
		int num_emissive_lights = 0;

		{
			// start with self lights
			vk_light_leaf_t *eigen = eigenlicht + i;
			memcpy(lights, eigen, sizeof(*eigen));
			num_emissive_lights = eigen->num_slights;

			/* PR("Leaf %d: marksurfaces=%d, eigen: slights=%d\n", */
			/* 		i, leaf->nummarksurfaces, eigen->num_slights); */
		}

		if (leaf->cluster < 0)
			continue;

		if (!visdata)
			continue;

		// TODO optimize: it's possible to iterate through all PVS leaves without unpacking and enumerating all leaves (?)
		for (int j = 0; j < map->numleafs; ++j) {
			if (j != i && CHECKVISBIT(visdata, map->leafs[j].cluster)) {
				const vk_light_leaf_t *pvs_eigen = eigenlicht + j;

				// copy pvs lights into this leaf lights
				for (int k = 0; k < pvs_eigen->num_slights; ++k) {
					const uint8_t candidate_light = pvs_eigen->slights[k];

					// dedup surfaces: many leafs can look at the same surface, and so it will be included more than once
					qboolean dup = false;
					// It's N^2, but the numbers are small (<10..20), so it's good i guess
					for (int l = 0; l < lights->num_slights; ++l)
						if (candidate_light == lights->slights[l]) {
							dup = true;
							break;
						}

					if (dup)
						continue;

					++num_emissive_lights;
					if (lights->num_slights == MAX_VISIBLE_SURFACE_LIGHTS)
						continue;

					// TODO cull by:
					// - front/back facing?
					// - distance and intensity
					// - ...

					lights->slights[lights->num_slights++] = candidate_light;
				}
			}
		}

		// TODO we see too many light sources for some map leaves. which and why?
		// TODO if not all emissive surfaces can fit, sort them by importance?

		if (num_emissive_lights > 0) {
			vk_light_leaf_t *eigen = eigenlicht + i;
			gEngine.Con_Reportf("Leaf %d surfaces=%d, emissive=%d, visible emssive=%d%s\n", i, leaf->nummarksurfaces, eigen->num_slights, num_emissive_lights,
				(num_emissive_lights > MAX_VISIBLE_SURFACE_LIGHTS) ? " (TOO MANY!)" : "");
		}
	}

	Mem_Free(surface_to_emissive_surface_map);
	Mem_Free(eigenlicht);
	g_lights.frame_number = frame_number;
}
