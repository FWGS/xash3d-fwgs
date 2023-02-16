#if 0
#include "vk_common.h"
#include "xash3d_types.h"
#include "protocol.h"
#include "const.h"
#include "bspfile.h"
#include "mod_local.h"

#include "vk_light.h"

#define PR(...) gEngine.Con_Reportf(__VA_ARGS__)

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

void traverseBSP( void ) {
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
#endif
