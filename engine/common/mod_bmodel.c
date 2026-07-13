/*
mod_bmodel.c - loading & handling world and brushmodels
Copyright (C) 2016 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "common.h"
#include "mod_local.h"
#include "sprite.h"
#include "xash3d_mathlib.h"
#include "alias.h"
#include "studio.h"
#include "wadfile.h"
#include "world.h"
#include "enginefeatures.h"
#include "client.h"
#include "server.h"			// LUMP_ error codes

#if XASH_GAMECUBE
#include <stdlib.h>
#include "gamecube/mem_gamecube.h"

static void Mod_GCFreeBspPin( void **ptr );

typedef struct gc_bsp_deferred_s
{
	void  *markfaces;
	void  *leafs;
	void  *nodes;
	void  *clipnodes;
	byte  *lightdata;
	size_t lightdatasize;
} gc_bsp_deferred_t;

static gc_bsp_deferred_t gc_bsp_deferred;
static void *gc_marksurfaces_malloc_block;
static void *gc_leafs_malloc_block;
static void *gc_surfaces_malloc_block;
static void *gc_texinfo_malloc_block;
static void *gc_nodes_malloc_block;
static qboolean gc_retain_bsp_source_buffer;
static byte *gc_bsp_scratch_base;
static size_t gc_bsp_scratch_size;

typedef struct gc_bsp_busy_range_s
{
	size_t start;
	size_t end;
} gc_bsp_busy_range_t;

static qboolean Mod_GCPointerInBuffer( const void *ptr, const byte *base, size_t size );
static void *Mod_GCAllocBspScratch( byte *base, size_t size, const gc_bsp_busy_range_t *busy, size_t busy_count, size_t alloc_size, size_t align );

static void Mod_GCFreeBspPin( void **ptr )
{
	if( !ptr || !*ptr )
		return;

	if( Mod_GCPointerInBuffer( *ptr, gc_bsp_scratch_base, gc_bsp_scratch_size ))
	{
		*ptr = NULL;
		return;
	}

	free( *ptr );
	*ptr = NULL;
}
#endif
#include "swaplib.h"
#include "ref_common.h"
#if defined( HAVE_OPENMP )
#include <omp.h>
#endif // HAVE_OPENMP

#define MIPTEX_CUSTOM_PALETTE_SIZE_BYTES ( sizeof( int16_t ) + 768 )

typedef struct leaflist_s
{
	int      count;
	int      maxcount;
	qboolean overflowed;
	int      *list;
	vec3_t   mins, maxs;
	int      topnode; // for overflows where each leaf can't be stored individually
} leaflist_t;

typedef struct
{
	// generic lumps
	dmodel_t   *submodels;
	size_t     numsubmodels;

	dvertex_t  *vertexes;
	size_t     numvertexes;

	dplane_t   *planes;
	size_t     numplanes;

	union
	{
		dnode_t   *nodes;
		dnode32_t *nodes32;
	};
	size_t numnodes;

	union
	{
		dleaf_t   *leafs;
		dleaf32_t *leafs32;
	};
	size_t numleafs;

	union
	{
		dclipnode_t   *clipnodes;
		dclipnode32_t *clipnodes32;
	};
	size_t numclipnodes;

	dtexinfo_t *texinfo;
	size_t     numtexinfo;

	union
	{
		dmarkface_t   *markfaces;
		dmarkface32_t *markfaces32;
	};
	size_t nummarkfaces;

	dsurfedge_t *surfedges;
	size_t      numsurfedges;

	union
	{
		dedge_t   *edges;
		dedge32_t *edges32;
	};
	size_t numedges;

	union
	{
		dface_t   *surfaces;
		dface32_t *surfaces32;
	};
	size_t numsurfaces;

	dfaceinfo_t *faceinfo;
	size_t      numfaceinfo;

	// array lumps
	byte   *visdata;
	size_t visdatasize;

	byte   *lightdata;
	size_t lightdatasize;

	byte   *deluxdata;
	size_t deluxdatasize;

	byte   *shadowdata;
	size_t shadowdatasize;

	byte   *entdata;
	size_t entdatasize;

	byte   *rgblightdata;
	size_t rgblightdatasize;

	// lumps that required personal handler
	dmiptexlump_t *textures;
	size_t        texdatasize;

	// intermediate arrays (pointers will lost after loading, but keep the data)
	color24       *deluxedata_out; // deluxemap data pointer
	byte          *shadowdata_out; // occlusion data pointer
	dclipnode32_t *clipnodes_out;  // temporary 32-bit array to hold clipnodes

	// misc stuff
	int       lightmap_samples; // samples per lightmap (1 or 3)
	int       version;          // model version
	qboolean  isworld;
	qboolean  isbsp30ext;

} dbspmodel_t;

#if XASH_GAMECUBE
static void Mod_GCFreeGcmapPreSurfaceLumps( dbspmodel_t *bmod )
{
	if( !GC_MapLoadMemoryOpt() || !bmod->isworld || !gc_bsp_scratch_base )
		return;

	Mod_GCFreeBspPin( (void **)&bmod->entdata );
	Mod_GCFreeBspPin( (void **)&bmod->planes );
	Mod_GCFreeBspPin( (void **)&bmod->vertexes );
	Mod_GCFreeBspPin( (void **)&bmod->submodels );
	Mod_GCFreeBspPin( (void **)&bmod->surfedges );
	if( bmod->version == QBSP2_VERSION )
		Mod_GCFreeBspPin( (void **)&bmod->edges32 );
	else
		Mod_GCFreeBspPin( (void **)&bmod->edges );
	Mod_GCFreeBspPin( (void **)&bmod->texinfo );
	if( bmod->visdata )
		Mod_GCFreeBspPin( (void **)&bmod->visdata );
	Con_Reportf( "Xash3D GameCube: freed pre-surface BSP scratch lumps\n" );
}
#endif

#if XASH_GAMECUBE
static void Mod_GCInvalidateScratchOverlap( dbspmodel_t *bmod, byte *mod_base, size_t bufferlen, byte *scratch, size_t scratch_size );
#endif

typedef struct
{
	const char *lumpname;
	size_t     entrysize;
	size_t     maxcount;
	size_t     count;
} mlumpstat_t;

typedef struct
{
	char name[64]; // just for debug

	// count errors and warnings
	int numerrors;
	int numwarnings;
} loadstat_t;

#define CHECK_OVERFLOW  BIT( 0 ) // if some of lumps will be overflowed this non fatal for us. But some lumps are critical. mark them

typedef enum
{
	LOADLUMP_STANDARD, // load lump from standard BSP header
	LOADLUMP_BSP30EXT, // ... from BSP30ext header
	LOADLUMP_BSPX,     // ... from BSPX data
} loadlump_source_t;

#define LUMP_SAVESTATS   BIT( 0 )
#define LUMP_BSHIFT_SWAP BIT( 1 )
#define LUMP_SILENT      BIT( 2 )
#define LUMP_BSP30EXT    BIT( 3 ) // extra marker for Mod_LoadLump
#define LUMP_BSPX        BIT( 4 )

typedef struct
{
	const int    lumpnumber;

	// BSPX
	const char   lumpname[24];

	const int    flags;
	const size_t mincount;
	const size_t maxcount;
	const int    entrysize;
	const int    entrysize32; // extended size (0 if not available)
	const char   *loadname;
	const size_t dataofs;  // offsetof into dbspmodel_t for data pointer
	const size_t countofs; // offsetof into dbspmodel_t for count/size
#if XASH_BIG_ENDIAN // do not waste memory on little endian
	const swap_struct_def_t *swap;
	const size_t swaplen;
	const swap_struct_def_t *swap32;
	const size_t swaplen32;
#endif
} mlumpinfo_t;

// all these macros are ugly af
#if XASH_BIG_ENDIAN
	#define LUMP_SWAP( x )       .swap = x, .swaplen = ARRAYSIZE( x )
	#define LUMP_SWAP32( x, y )  .swap = x, .swaplen = ARRAYSIZE( x ), .swap32 = y, .swaplen32 = ARRAYSIZE( y )
#else
	#define LUMP_SWAP( x )
	#define LUMP_SWAP32( x, y )
#endif

le_struct_begin( dlump_swap )
	le_struct_field( dlump_t, fileofs )
	le_struct_field( dlump_t, filelen )
le_struct_end();

le_struct_begin( dheader_swap )
	le_struct_field( dheader_t, version )
	le_struct_array_child( dheader_t, lumps, dlump_swap, HEADER_LUMPS )
le_struct_end();

le_struct_begin( dextrahdr_swap )
	le_struct_field( dextrahdr_t, id )
	le_struct_field( dextrahdr_t, version )
	le_struct_array_child( dextrahdr_t, lumps, dlump_swap, EXTRA_LUMPS )
le_struct_end();

le_struct_begin( dbspx_hdr_swap )
	le_struct_field( dbspx_hdr_t, id )
	le_struct_field( dbspx_hdr_t, numlumps )
	// flexible array member omitted
le_struct_end();

le_struct_begin( dbspx_lump_swap )
	le_struct_field( dbspx_lump_t, fileofs )
	le_struct_field( dbspx_lump_t, filelen )
le_struct_end();

le_struct_begin( dplane_swap )
	le_struct_array( dplane_t, normal, 3 )
	le_struct_field( dplane_t, dist )
	le_struct_field( dplane_t, type )
le_struct_end();

le_struct_begin( dvertex_swap )
	le_struct_array( dvertex_t, point, 3 )
le_struct_end();

le_struct_begin( dnode_swap )
	le_struct_field( dnode_t, planenum )
	le_struct_array( dnode_t, children, 2 )
	le_struct_array( dnode_t, mins, 3 )
	le_struct_array( dnode_t, maxs, 3 )
	le_struct_field( dnode_t, firstface )
	le_struct_field( dnode_t, numfaces )
le_struct_end();

le_struct_begin( dnode32_swap )
	le_struct_field( dnode32_t, planenum )
	le_struct_array( dnode32_t, children, 2 )
	le_struct_array( dnode32_t, mins, 3 )
	le_struct_array( dnode32_t, maxs, 3 )
	le_struct_field( dnode32_t, firstface )
	le_struct_field( dnode32_t, numfaces )
le_struct_end();

le_struct_begin( dtexinfo_swap )
	le_struct_array( dtexinfo_t, vecs[0], 4 )
	le_struct_array( dtexinfo_t, vecs[1], 4 )
	le_struct_field( dtexinfo_t, miptex )
	le_struct_field( dtexinfo_t, flags )
	le_struct_field( dtexinfo_t, faceinfo )
le_struct_end();

le_struct_begin( dface_swap )
	le_struct_field( dface_t, planenum )
	le_struct_field( dface_t, side )
	le_struct_field( dface_t, firstedge )
	le_struct_field( dface_t, numedges )
	le_struct_field( dface_t, texinfo )
	le_struct_field( dface_t, lightofs )
le_struct_end();

le_struct_begin( dface32_swap )
	le_struct_field( dface32_t, planenum )
	le_struct_field( dface32_t, side )
	le_struct_field( dface32_t, firstedge )
	le_struct_field( dface32_t, numedges )
	le_struct_field( dface32_t, texinfo )
	le_struct_field( dface32_t, lightofs )
le_struct_end();

le_struct_begin( dclipnode_swap )
	le_struct_field( dclipnode_t, planenum )
	le_struct_array( dclipnode_t, children, 2 )
le_struct_end();

le_struct_begin( dclipnode32_swap )
	le_struct_field( dclipnode32_t, planenum )
	le_struct_array( dclipnode32_t, children, 2 )
le_struct_end();

le_struct_begin( dleaf_swap )
	le_struct_field( dleaf_t, contents )
	le_struct_field( dleaf_t, visofs )
	le_struct_array( dleaf_t, mins, 3 )
	le_struct_array( dleaf_t, maxs, 3 )
	le_struct_field( dleaf_t, firstmarksurface )
	le_struct_field( dleaf_t, nummarksurfaces )
le_struct_end();

le_struct_begin( dleaf32_swap )
	le_struct_field( dleaf32_t, contents )
	le_struct_field( dleaf32_t, visofs )
	le_struct_array( dleaf32_t, mins, 3 )
	le_struct_array( dleaf32_t, maxs, 3 )
	le_struct_field( dleaf32_t, firstmarksurface )
	le_struct_field( dleaf32_t, nummarksurfaces )
le_struct_end();

le_struct_begin( dedge_swap )
	le_struct_array( dedge_t, v, 2 )
le_struct_end();

le_struct_begin( dedge32_swap )
	le_struct_array( dedge32_t, v, 2 )
le_struct_end();

le_struct_begin( dmodel_swap )
	le_struct_array( dmodel_t, mins, 3 )
	le_struct_array( dmodel_t, maxs, 3 )
	le_struct_array( dmodel_t, origin, 3 )
	le_struct_array( dmodel_t, headnode, MAX_MAP_HULLS )
	le_struct_field( dmodel_t, visleafs )
	le_struct_field( dmodel_t, firstface )
	le_struct_field( dmodel_t, numfaces )
le_struct_end();

le_struct_begin( dfaceinfo_swap )
	le_struct_field( dfaceinfo_t, texture_step )
	le_struct_field( dfaceinfo_t, max_extent )
	le_struct_field( dfaceinfo_t, groupid )
le_struct_end();

le_struct_begin( mip_swap )
	le_struct_field( mip_t, width )
	le_struct_field( mip_t, height )
	le_struct_array( mip_t, offsets, 4 )
le_struct_end();

world_static_t     world;
static loadstat_t  loadstat;
static model_t     *worldmodel;
static byte        g_visdata[(MAX_MAP_LEAFS+7)/8];	// intermediate buffer
static const mlumpinfo_t srclumps[HEADER_LUMPS] =
{
	{
		.lumpnumber  = LUMP_ENTITIES,
		.mincount    = 32,
		.maxcount    = MAX_MAP_ENTSTRING,
		.entrysize   = sizeof( byte ),
		.loadname    = "entities",
		.dataofs     = offsetof( dbspmodel_t, entdata ),
		.countofs    = offsetof( dbspmodel_t, entdatasize ),
	},
	{
		.lumpnumber  = LUMP_PLANES,
		.mincount    = 1,
		.maxcount    = MAX_MAP_PLANES,
		.entrysize   = sizeof( dplane_t ),
		.loadname    = "planes",
		.dataofs     = offsetof( dbspmodel_t, planes ),
		.countofs    = offsetof( dbspmodel_t, numplanes ),
		LUMP_SWAP( dplane_swap )
	},
	{
		.lumpnumber  = LUMP_TEXTURES,
		.mincount    = 1,
		.maxcount    = MAX_MAP_MIPTEX,
		.entrysize   = sizeof( byte ),
		.loadname    = "textures",
		.dataofs     = offsetof( dbspmodel_t, textures ),
		.countofs    = offsetof( dbspmodel_t, texdatasize ),
	},
	{
		.lumpnumber  = LUMP_VERTEXES,
		.maxcount    = MAX_MAP_VERTS,
		.entrysize   = sizeof( dvertex_t ),
		.loadname    = "vertexes",
		.dataofs     = offsetof( dbspmodel_t, vertexes ),
		.countofs    = offsetof( dbspmodel_t, numvertexes ),
		LUMP_SWAP( dvertex_swap )
	},
	{
		.lumpnumber  = LUMP_VISIBILITY,
		.maxcount    = MAX_MAP_VISIBILITY,
		.entrysize   = sizeof( byte ),
		.loadname    = "visibility",
		.dataofs     = offsetof( dbspmodel_t, visdata ),
		.countofs    = offsetof( dbspmodel_t, visdatasize ),
	},
	{
		.lumpnumber  = LUMP_NODES,
		.mincount    = 1,
		.maxcount    = MAX_MAP_NODES,
		.entrysize   = sizeof( dnode_t ),
		.entrysize32 = sizeof( dnode32_t ),
		.loadname    = "nodes",
		.flags       = CHECK_OVERFLOW,
		.dataofs     = offsetof( dbspmodel_t, nodes ),
		.countofs    = offsetof( dbspmodel_t, numnodes ),
		LUMP_SWAP32( dnode_swap, dnode32_swap )
	},
	{
		.lumpnumber  = LUMP_TEXINFO,
		.mincount    = 0,
		.maxcount    = MAX_MAP_TEXINFO,
		.entrysize   = sizeof( dtexinfo_t ),
		.loadname    = "texinfo",
		.flags       = CHECK_OVERFLOW,
		.dataofs     = offsetof( dbspmodel_t, texinfo ),
		.countofs    = offsetof( dbspmodel_t, numtexinfo ),
		LUMP_SWAP( dtexinfo_swap )
	},
	{
		.lumpnumber  = LUMP_FACES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_FACES,
		.entrysize   = sizeof( dface_t ),
		.entrysize32 = sizeof( dface32_t ),
		.loadname    = "faces",
		.flags       = CHECK_OVERFLOW,
		.dataofs     = offsetof( dbspmodel_t, surfaces ),
		.countofs    = offsetof( dbspmodel_t, numsurfaces ),
		LUMP_SWAP32( dface_swap, dface32_swap )
	},
	{
		.lumpnumber  = LUMP_LIGHTING,
		.mincount    = 0,
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "lightmaps",
		.flags       = 0,
		.dataofs     = offsetof( dbspmodel_t, lightdata ),
		.countofs    = offsetof( dbspmodel_t, lightdatasize ),
	},
	{
		.lumpnumber  = LUMP_CLIPNODES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_CLIPNODES,
		.entrysize   = sizeof( dclipnode_t ),
		.entrysize32 = sizeof( dclipnode32_t ),
		.loadname    = "clipnodes",
		.flags       = 0,
		.dataofs     = offsetof( dbspmodel_t, clipnodes ),
		.countofs    = offsetof( dbspmodel_t, numclipnodes ),
		LUMP_SWAP32( dclipnode_swap, dclipnode32_swap )
	},
	{
		.lumpnumber  = LUMP_LEAFS,
		.mincount    = 1,
		.maxcount    = MAX_MAP_LEAFS,
		.entrysize   = sizeof( dleaf_t ),
		.entrysize32 = sizeof( dleaf32_t ),
		.loadname    = "leafs",
		.flags       = CHECK_OVERFLOW,
		.dataofs     = offsetof( dbspmodel_t, leafs ),
		.countofs    = offsetof( dbspmodel_t, numleafs ),
		LUMP_SWAP32( dleaf_swap, dleaf32_swap )
	},
	{
		.lumpnumber  = LUMP_MARKSURFACES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_MARKSURFACES,
		.entrysize   = sizeof( dmarkface_t ),
		.entrysize32 = sizeof( dmarkface32_t ),
		.loadname    = "markfaces",
		.flags       = 0,
		.dataofs     = offsetof( dbspmodel_t, markfaces ),
		.countofs    = offsetof( dbspmodel_t, nummarkfaces ),
	},
	{
		.lumpnumber  = LUMP_EDGES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_EDGES,
		.entrysize   = sizeof( dedge_t ),
		.entrysize32 = sizeof( dedge32_t ),
		.loadname    = "edges",
		.flags       = 0,
		.dataofs     = offsetof( dbspmodel_t, edges ),
		.countofs    = offsetof( dbspmodel_t, numedges ),
		LUMP_SWAP32( dedge_swap, dedge32_swap )
	},
	{
		.lumpnumber  = LUMP_SURFEDGES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_SURFEDGES,
		.entrysize   = sizeof( dsurfedge_t ),
		.loadname    = "surfedges",
		.flags       = 0,
		.dataofs     = offsetof( dbspmodel_t, surfedges ),
		.countofs    = offsetof( dbspmodel_t, numsurfedges ),
	},
	{
		.lumpnumber  = LUMP_MODELS,
		.mincount    = 1,
		.maxcount    = MAX_MAP_MODELS,
		.entrysize   = sizeof( dmodel_t ),
		.loadname    = "models",
		.flags       = CHECK_OVERFLOW,
		.dataofs     = offsetof( dbspmodel_t, submodels ),
		.countofs    = offsetof( dbspmodel_t, numsubmodels ),
		LUMP_SWAP( dmodel_swap )
	},
};

static const mlumpinfo_t extlumps[EXTRA_LUMPS] =
{
	{
		.lumpnumber  = LUMP_LIGHTVECS,
		.mincount    = 0,
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "deluxmaps",
		.dataofs     = offsetof( dbspmodel_t, deluxdata ),
		.countofs    = offsetof( dbspmodel_t, deluxdatasize ),
	},
	{
		.lumpnumber  = LUMP_FACEINFO,
		.mincount    = 0,
		.maxcount    = MAX_MAP_FACEINFO,
		.entrysize   = sizeof( dfaceinfo_t ),
		.loadname    = "faceinfos",
		.flags       = CHECK_OVERFLOW,
		.dataofs     = offsetof( dbspmodel_t, faceinfo ),
		.countofs    = offsetof( dbspmodel_t, numfaceinfo ),
		LUMP_SWAP( dfaceinfo_swap )
	},
	{
		.lumpnumber  = LUMP_SHADOWMAP,
		.mincount    = 0,
		.maxcount    = MAX_MAP_LIGHTING / 3,
		.entrysize   = sizeof( byte ),
		.loadname    = "shadowmap",
		.dataofs     = offsetof( dbspmodel_t, shadowdata ),
		.countofs    = offsetof( dbspmodel_t, shadowdatasize ),
	},
};

static const mlumpinfo_t bspxlumps[] =
{
	{
		.lumpname    = "RGBLIGHTING",
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "rgblighting",
		.dataofs     = offsetof( dbspmodel_t, rgblightdata ),
		.countofs    = offsetof( dbspmodel_t, rgblightdatasize ),
	},
	{
		.lumpname    = "LIGHTINGDIR",
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "lightingdir",
		.dataofs     = offsetof( dbspmodel_t, deluxdata ),
		.countofs    = offsetof( dbspmodel_t, deluxdatasize ),
	},
};

static mlumpstat_t worldstats[HEADER_LUMPS + EXTRA_LUMPS + ARRAYSIZE( bspxlumps )];

#define BOX_CLIPNODES_INITIALIZER \
	{ \
		.planenum = 0, \
		.children = { CONTENTS_EMPTY, 1 }, \
	}, \
	{ \
		.planenum = 1, \
		.children = { 2, CONTENTS_EMPTY }, \
	}, \
	{ \
		.planenum = 2, \
		.children = { CONTENTS_EMPTY, 3 }, \
	}, \
	{ \
		.planenum = 3, \
		.children = { 4, CONTENTS_EMPTY }, \
	}, \
	{ \
		.planenum = 4, \
		.children = { CONTENTS_EMPTY, 5 }, \
	}, \
	{ \
		.planenum = 5, \
		.children = { CONTENTS_SOLID, CONTENTS_EMPTY }, \
	}, \

const mclipnode16_t box_clipnodes16[6] = { BOX_CLIPNODES_INITIALIZER };
const mclipnode32_t box_clipnodes32[6] = { BOX_CLIPNODES_INITIALIZER };

/*
===============================================================================

			Static helper functions

===============================================================================
*/

static const byte *Mod_GetMipTexForTexture( dbspmodel_t *bmod, int i, mip_t *out )
{
	if( i < 0 || i >= bmod->textures->nummiptex )
		return NULL;

	if( bmod->textures->dataofs[i] == -1 )
		return NULL;

	const byte *raw = (byte *)bmod->textures + bmod->textures->dataofs[i];
	memcpy( out, raw, sizeof( *out ));
	return raw;
}

// Returns index of WAD that texture was found in, or -1 if not found.
static int Mod_LoadTextureFromWadList( wadentry_t *list, int count, const char *name, rgbdata_t **pic, char *texpath, size_t texpathlen )
{
	if( !list || COM_StringEmptyOrNULL( name ))
		return -1;

	// check wads in reverse order
	for( int i = count - 1; i >= 0; i-- )
	{
		searchpath_t *sp = NULL;

		while(( sp = g_fsapi.GetArchiveByName( list[i].name, sp )))
		{
			char file[MAX_VA_STRING];

			Q_snprintf( file, sizeof( file ), "%s.mip", name );
			int pack_ind = g_fsapi.FindFileInArchive( sp, file, NULL, 0 );

			if( pack_ind < 0 )
				continue;

			if( texpath != NULL )
				Q_snprintf( texpath, texpathlen, "%s/%s.mip", list[i].name, name );

			if( pic == NULL )
				return i; // dedicated server don't want to load the textures (why?)

			fs_offset_t len;
			byte *buf = g_fsapi.LoadFileFromArchive( sp, file, pack_ind, &len, false );
			if( !buf )
			{
				*pic = NULL;
				return i; // corrupted file, don't ignore it
			}

			// tell imagelib to directly load this texture to save time
			Q_snprintf( file, sizeof( file ), "#%s/%s.mip", list[i].name, name );
			*pic = FS_LoadImage( file, buf, len );
			Mem_Free( buf );
			return i; // if file is corrupted, it's fine, we want to tell the user about it
		}
	}

	return -1;
}

static fs_offset_t Mod_CalculateMipTexSize( const mip_t *mt, qboolean palette )
{
	if( !mt )
		return 0;

	return sizeof( *mt ) + (( mt->width * mt->height * 85 ) >> 6 ) +
		( palette ? MIPTEX_CUSTOM_PALETTE_SIZE_BYTES : 0 );
}

static qboolean Mod_CalcMipTexUsesCustomPalette( model_t *mod, dbspmodel_t *bmod, int textureIndex )
{
	mip_t mipTex;

	if( !Mod_GetMipTexForTexture( bmod, textureIndex, &mipTex ) || mipTex.offsets[0] <= 0 )
		return false;

	// Calculate the size assuming we are not using a custom palette.
	fs_offset_t size = Mod_CalculateMipTexSize( &mipTex, false );
	fs_offset_t remainingBytes;

	// Compute next data offset to determine allocated miptex space
	for( int nextTextureIndex = textureIndex + 1; nextTextureIndex < mod->numtextures; nextTextureIndex++ )
	{
		int nextOffset = bmod->textures->dataofs[nextTextureIndex];

		if( nextOffset != -1 )
		{
			remainingBytes = nextOffset - ( bmod->textures->dataofs[textureIndex] + size );
			return remainingBytes >= MIPTEX_CUSTOM_PALETTE_SIZE_BYTES;
		}
	}

	// There was no other miptex after this one.
	// See if there is enough space between the end and our offset.
	remainingBytes = bmod->texdatasize - ( bmod->textures->dataofs[textureIndex] + size );
	return remainingBytes >= MIPTEX_CUSTOM_PALETTE_SIZE_BYTES;
}

static qboolean Mod_NameImpliesTextureIsAnimated( texture_t *tex )
{
	if( !tex )
		return false;

	// Not an animated texture name
	if( tex->name[0] != '-' && tex->name[0] != '+' )
		return false;

	// Name implies texture is animated - check second character is valid.
	if( !( tex->name[1] >= '0' && tex->name[1] <= '9' ) &&
		!( tex->name[1] >= 'a' && tex->name[1] <= 'j' ))
	{
		Con_Printf( S_ERROR "%s: animating texture \"%s\" has invalid name\n", __func__, tex->name );
		return false;
	}

	return true;
}

static void Mod_CreateDefaultTexture( model_t *mod, texture_t **texture )
{
	// Pointer must be valid, and value pointed to must be null.
	if( !texture || *texture != NULL )
		return;

	texture_t *tex;
	*texture = tex = Mem_Calloc( mod->mempool, sizeof( *tex ));
	Q_strncpy( tex->name, REF_DEFAULT_TEXTURE, sizeof( tex->name ));

#if !XASH_DEDICATED
	if( !Host_IsDedicated( ))
	{
		tex->gl_texturenum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );
		tex->width = 16;
		tex->height = 16;
	}
#endif // XASH_DEDICATED
}

/*
===============================================================================

			MAP PROCESSING

===============================================================================
*/
/*
=================
Mod_LoadLump

generic loader
=================
*/
static void Mod_LoadLump( const void *in, const mlumpinfo_t *info, mlumpstat_t *stat, int flags, loadlump_source_t source, const void *bspx_data, dbspmodel_t *bmod )
{
	int     version = ((const dheader_t *)in)->version;
	dlump_t l = { 0 };

	switch( source )
	{
	case LOADLUMP_STANDARD:
	{
		const dheader_t *header = in;
		int lumpnumber = info->lumpnumber;

		if( FBitSet( flags, LUMP_BSHIFT_SWAP ))
		{
			if( lumpnumber == LUMP_ENTITIES )
				lumpnumber = LUMP_PLANES;
			else if( lumpnumber == LUMP_PLANES )
				lumpnumber = LUMP_ENTITIES;
		}
		l = header->lumps[lumpnumber];
		break;
	}
	case LOADLUMP_BSP30EXT:
	{
		const dextrahdr_t *header = (const dextrahdr_t *)((const byte *)in + sizeof( dheader_t ));
		if( header->id != IDEXTRAHEADER || header->version != EXTRA_VERSION )
			return;
		l = header->lumps[info->lumpnumber];
		break;
	}
	case LOADLUMP_BSPX:
	{
		if( !bspx_data )
			return;

		const dbspx_hdr_t *header = bspx_data;

		if( header->id != IDBSPXHEADER )
			return;

		int i;
		for( i = 0; i < header->numlumps; i++ )
		{
			if( !Q_strcmp( info->lumpname, header->lumps[i].lumpname ))
			{
				l.fileofs = header->lumps[i].fileofs;
				l.filelen = header->lumps[i].filelen;
				break;
			}
		}

		if( i == header->numlumps )
			return;
	}
	}

	// lump is unused by engine for some reasons ?
	if( !l.fileofs || info->entrysize <= 0 || info->maxcount <= 0 )
		return;

	size_t real_entrysize = info->entrysize; // default

	// analyze real entrysize
	switch( version )
	{
	case HLBSP_VERSION:
		if( FBitSet( flags, LUMP_BSP30EXT ) && info->lumpnumber == LUMP_CLIPNODES )
		{
			// if this map is bsp30ext, try to guess extended clipnodes
			if((( l.filelen % info->entrysize ) || ( l.filelen / info->entrysize32 ) >= MAX_MAP_CLIPNODES_HLBSP ))
			{
				real_entrysize = info->entrysize32;
			}
		}
		break;
	case QBSP2_VERSION:
		if( info->entrysize32 > 0 )
		{
			// always use alternate entrysize for BSP2
			real_entrysize = info->entrysize32;
		}
		break;
	default:
		break;
	}

	// bmodels not required the visibility
	if( info->lumpnumber == LUMP_VISIBILITY && !world.loading && bmod )
		SetBits( flags, LUMP_SILENT ); // shut up warning

	// fill the stats for world
	if( FBitSet( flags, LUMP_SAVESTATS ))
	{
		stat->lumpname = info->loadname;
		stat->entrysize = real_entrysize;
		stat->maxcount = info->maxcount;
		if( real_entrysize != 0 )
			stat->count = l.filelen / real_entrysize;
	}

	// lump is not present
	if( l.filelen <= 0 )
	{
		// don't warn about extra lumps - it's optional
		if( source == LOADLUMP_STANDARD )
		{
			// some data array that may be optional
			if( real_entrysize == sizeof( byte ))
			{
				if( !FBitSet( flags, LUMP_SILENT ))
				{
					Con_DPrintf( S_WARN "map ^2%s^7 has no %s\n", loadstat.name, info->loadname );
					loadstat.numwarnings++;
				}
			}
			else if( info->mincount > 0 )
			{
				// it has the mincount and the lump is completely missed!
				if( !FBitSet( flags, LUMP_SILENT ))
					Con_DPrintf( S_ERROR "map ^2%s^7 has no %s\n", loadstat.name, info->loadname );
				loadstat.numerrors++;
			}
		}
		return;
	}

	if( l.filelen % real_entrysize )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
		{
			Con_DPrintf( S_ERROR "Mod_Load%c%s: Lump size %d was not a multiple of %zu bytes\n", toupper( info->loadname[0] ), &info->loadname[1], l.filelen, real_entrysize );
		}
		loadstat.numerrors++;
		return;
	}

	size_t numelems = l.filelen / real_entrysize;

	if( numelems < info->mincount )
	{
		// it has the mincount and it's smaller than this limit
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "map ^2%s^7 has no %s\n", loadstat.name, info->loadname );
		loadstat.numerrors++;
		return;
	}

	if( numelems > info->maxcount )
	{
		// it has the maxcount and it's overflowed
		if( FBitSet( info->flags, CHECK_OVERFLOW ))
		{
			if( !FBitSet( flags, LUMP_SILENT ))
				Con_DPrintf( S_ERROR "map ^2%s^7 has too many %s\n", loadstat.name, info->loadname );
			loadstat.numerrors++;
			return;
		}
		else if( !FBitSet( flags, LUMP_SILENT ))
		{
			// just throw warning
			Con_DPrintf( S_WARN "map ^2%s^7 has too many %s\n", loadstat.name, info->loadname );
			loadstat.numwarnings++;
		}
	}

	// if no bmod is passed, we are only testing if BSP lumps are not corrupted
	if( !bmod )
		return;

	byte *data = (byte *)in + l.fileofs;

	// all checks are passed, store pointers
	*(byte **)((byte *)bmod + info->dataofs) = data;
	*(size_t *)((byte *)bmod + info->countofs) = numelems;

	// finally, process the data
#if XASH_BIG_ENDIAN
	const swap_struct_def_t *swap = real_entrysize == info->entrysize32 ? info->swap32 : info->swap;
	size_t swaplen = real_entrysize == info->entrysize32 ? info->swaplen32 : info->swaplen;

	if( swap )
	{
		for( size_t j = 0; j < numelems; j++ )
			swap_struct_( swap, swaplen, data + j * real_entrysize );
	}
	// some lumps don't need a swapdef, as all needed data in the lump info
	else if( real_entrysize > 1 )
	{
		for( size_t j = 0; j < numelems; j++ )
			swap_field_( data + j * real_entrysize, real_entrysize );
	}
#endif
}

/*
================
Mod_ArrayUsage
================
*/
static int Mod_ArrayUsage( const char *szItem, int items, int maxitems, int itemsize )
{
	float percentage = maxitems ? (items * 100.0f / maxitems) : 0.0f;
	string s1, s2;

	Q_snprintf( s1, sizeof( s1 ), "%i / %i", items, maxitems );
	Q_snprintf( s2, sizeof( s2 ), "%s / %s", Q_memprint( items * itemsize ), Q_memprint( maxitems * itemsize ));

	Con_Printf( "%-8s\t%-15s\t%-15s\t(%4.1f%%)\t%s^7\n",
		szItem, s1, s2, percentage,
		percentage > 99.99f ? S_RED    "SIZE OVERFLOW!!!" :
		percentage > 95.0f  ? S_YELLOW "SIZE DANGER!" :
		percentage > 80.0f  ? S_GREEN  "VERY FULL!" :
		"" );

	return items * itemsize;
}

/*
================
Mod_GlobUsage
================
*/
static int Mod_GlobUsage( const char *szItem, int itemstorage, int maxstorage )
{
	float percentage = maxstorage ? (itemstorage * 100.0f / maxstorage) : 0.0f;
	string s1;

	Q_snprintf( s1, sizeof( s1 ), "%s / %s", Q_memprint( itemstorage ), Q_memprint( maxstorage ));

	Con_Printf( "%-8s\t%-17s\t%-15s\t(%4.1f%%)\t%s^7\n",
		szItem, "[variable]", s1, percentage,
		percentage > 99.99f ? S_RED    "SIZE OVERFLOW!!!" :
		percentage > 95.0f  ? S_YELLOW "SIZE DANGER!" :
		percentage > 80.0f  ? S_GREEN  "VERY FULL!" :
		"" );

	return itemstorage;
}

/*
=============
Mod_PrintWorldStats_f

Dumps info about world
=============
*/
void Mod_PrintWorldStats_f( void )
{
	model_t	*w = worldmodel;

	if( !w || !w->numsubmodels )
	{
		Con_Printf( "No map loaded\n" );
		return;
	}

	Con_Printf( "\n" );
	Con_Printf( "Lump name\tObjects / MaxObjs\tMemory / MaxMem\tFullness\n" );
	Con_Printf( "=========\t=================\t===============\t========\n" );

	int totalmemory = 0;
	for( int i = 0; i < ARRAYSIZE( worldstats ); i++ )
	{
		mlumpstat_t *stat = &worldstats[i];

		if( !stat->lumpname || !stat->maxcount || !stat->count )
			continue; // unused or lump is empty

		if( stat->entrysize == sizeof( byte ))
			totalmemory += Mod_GlobUsage( stat->lumpname, stat->count, stat->maxcount );
		else
			totalmemory += Mod_ArrayUsage( stat->lumpname, stat->count, stat->maxcount, stat->entrysize );
	}

	Con_Printf( "=== Total BSP file data space used: %s ===\n", Q_memprint( totalmemory ));
	Con_Printf( "World size ( %g %g %g ) units\n", world.size[0], world.size[1], world.size[2] );
	Con_Printf( "Supports transparency world water: %s\n", FBitSet( world.flags, FWORLD_WATERALPHA ) ? "Yes" : "No" );
	Con_Printf( "Lighting: %s\n", FBitSet( w->flags, MODEL_COLORED_LIGHTING ) ? "colored" : "monochrome" );
	Con_Printf( "World total leafs: %d\n", worldmodel->numleafs + 1 );
	Con_Printf( "original name: ^1%s\n", worldmodel->name );
	Con_Printf( "internal name: ^2%s\n", world.message ? world.message : "none" );
	Con_Printf( "map compiler: ^3%s\n", world.compiler ? world.compiler : "unknown" );
	Con_Printf( "map editor: ^2%s\n", world.generator ? world.generator : "unknown" );
}

/*
===============================================================================

			COMMON ROUTINES

===============================================================================
*/
/*
===================
Mod_DecompressPVS

===================
*/
static void Mod_DecompressPVS( byte *const out, const byte *in, size_t visbytes )
{
	byte *dst = out;

	if( !in ) // no visinfo, make all visible
	{
		memset( out, 0xFF, visbytes );
		return;
	}

	while( dst < out + visbytes )
	{
		if( *in ) // uncompressed
		{
			*dst++ = *in++;
		}
		else // zero repeated `c` times
		{
			size_t c = in[1];
			if( c > out + visbytes - dst )
				c = out + visbytes - dst;

			memset( dst, 0, c );
			in += 2;
			dst += c;
		}
	}
}

static size_t Mod_CompressPVS( byte *const out, const byte *in, size_t inbytes )
{
	byte *dst = out;

	for( size_t i = 0; i < inbytes; i++ )
	{
		size_t j = i + 1, rep = 1;

		*dst++ = in[i];

		// only compress zeros
		if( in[i] )
			continue;

		for( ; j < inbytes && rep != 255; j++, rep++ )
		{
			if( in[j] )
				break;
		}

		*dst++ = rep;
		i = j - 1;
	}

	return dst - out;
}

/*
==================
Mod_PointInLeaf

==================
*/
mleaf_t *Mod_PointInLeaf( const vec3_t p, mnode_t *node, model_t *mod )
{
	Assert( node != NULL );

	while( 1 )
	{
		if( node->contents < 0 )
			return (mleaf_t *)node;
		node = node_child( node, PlaneDiff( p, node->plane ) <= 0, mod );
	}

	// never reached
	return NULL;
}

/*
==================
Mod_GetPVSForPoint

Returns PVS data for a given point
NOTE: can return NULL
==================
*/
byte *Mod_GetPVSForPoint( const vec3_t p )
{
	ASSERT( worldmodel != NULL );

	mleaf_t	*leaf = Mod_PointInLeaf( p, worldmodel->nodes, worldmodel );

	if( leaf && leaf->cluster >= 0 )
	{
		Mod_DecompressPVS( g_visdata, leaf->compressed_vis, world.visbytes );
		return g_visdata;
	}

	return NULL;
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

==================
*/
static void Mod_FatPVS_RecursiveBSPNode( const vec3_t org, float radius, byte *visbuffer, int visbytes, mnode_t *node, qboolean phs )
{
	while( node->contents >= 0 )
	{
		float d = PlaneDiff( org, node->plane );

		if( d > radius )
			node = node_child( node, 0, worldmodel );
		else if( d < -radius )
			node = node_child( node, 1, worldmodel );
		else
		{
			// go down both sides
			Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, visbytes, node_child( node, 0, worldmodel ), phs );
			node = node_child( node, 1, worldmodel );
		}
	}

	// if this leaf is in a cluster, accumulate the vis bits
	if(((mleaf_t *)node)->cluster >= 0 )
	{
		if( phs )
		{
			int i = ((mleaf_t *)node)->cluster + 1;
			Mod_DecompressPVS( g_visdata, &world.compressed_phs[world.phsofs[i]], world.visbytes );
		}
		else
		{
			Mod_DecompressPVS( g_visdata, ((mleaf_t *)node)->compressed_vis, world.visbytes );
		}

		Q_memor( visbuffer, g_visdata, visbytes );
	}
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

Calculates a PVS that is the inclusive or of all leafs
within radius pixels of the given point.
==================
*/
int Mod_FatPVS( const vec3_t org, float radius, byte *visbuffer, int visbytes, qboolean merge, qboolean fullvis, qboolean phs )
{
	ASSERT( worldmodel != NULL );

	mleaf_t	*leaf = Mod_PointInLeaf( org, worldmodel->nodes, worldmodel );
	int	bytes = Q_min( world.visbytes, visbytes );

	// enable full visibility for some reasons
	if( fullvis || !worldmodel->visdata || !leaf || leaf->cluster < 0 )
	{
		memset( visbuffer, 0xFF, bytes );
		return bytes;
	}

	// requested PHS but we don't have PHS for some reason
	// enable full visibility
	if( phs && !( world.compressed_phs && world.phsofs ))
	{
		memset( visbuffer, 0xFF, bytes );
		return bytes;
	}

	if( !merge ) memset( visbuffer, 0x00, bytes );

	Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, bytes, worldmodel->nodes, phs );

	return bytes;
}

/*
======================================================================

LEAF LISTING

======================================================================
*/
static void Mod_BoxLeafnums_r( leaflist_t *ll, mnode_t *node )
{
	while( 1 )
	{
		if( node->contents == CONTENTS_SOLID )
			return;

		if( node->contents < 0 )
		{
			mleaf_t	*leaf = (mleaf_t *)node;

			// it's a leaf!
			if( ll->count >= ll->maxcount )
			{
				ll->overflowed = true;
				return;
			}

			ll->list[ll->count++] = leaf->cluster;
			return;
		}

		int sides = BOX_ON_PLANE_SIDE( ll->mins, ll->maxs, node->plane );

		if( sides == 1 )
			node = node_child( node, 0, worldmodel );
		else if( sides == 2 )
			node = node_child( node, 1, worldmodel );
		else
		{
			// go down both
			if( ll->topnode == -1 )
				ll->topnode = node - worldmodel->nodes;
			Mod_BoxLeafnums_r( ll, node_child( node, 0, worldmodel ));
			node = node_child( node, 1, worldmodel );
		}
	}
}

/*
==================
Mod_BoxLeafnums
==================
*/
static int Mod_BoxLeafnums( const vec3_t mins, const vec3_t maxs, int *list, int listsize, int *topnode )
{
	if( !worldmodel ) return 0;

	leaflist_t	ll;
	VectorCopy( mins, ll.mins );
	VectorCopy( maxs, ll.maxs );

	ll.maxcount = listsize;
	ll.overflowed = false;
	ll.topnode = -1;
	ll.list = list;
	ll.count = 0;

	Mod_BoxLeafnums_r( &ll, worldmodel->nodes );

	if( topnode ) *topnode = ll.topnode;
	return ll.count;
}

/*
=============
Mod_BoxVisible

Returns true if any leaf in boxspace
is potentially visible
=============
*/
qboolean Mod_BoxVisible( const vec3_t mins, const vec3_t maxs, const byte *visbits )
{
	if( !visbits || !mins || !maxs )
		return true;

	int	leafList[MAX_BOX_LEAFS];
	int	count = Mod_BoxLeafnums( mins, maxs, leafList, MAX_BOX_LEAFS, NULL );

	for( int i = 0; i < count; i++ )
	{
		if( CHECKVISBIT( visbits, leafList[i] ))
			return true;
	}
	return false;
}

/*
=================
Mod_FindModelOrigin

routine to detect bmodels with origin-brush
=================
*/
static void Mod_FindModelOrigin( const char *entities, const char *modelname, vec3_t origin )
{
	if( !entities || COM_StringEmptyOrNULL( modelname ))
		return;

	if( !origin || !VectorIsNull( origin ))
		return;

	char	*pfile = (char *)entities;
	string	keyname;
	char	token[2048];
	qboolean	model_found;
	qboolean	origin_found;
#if XASH_GAMECUBE
	static qboolean parse_warned;
#endif

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( token[0] != '{' )
#if XASH_GAMECUBE
		{
			if( !parse_warned )
			{
				Con_Reportf( S_WARN "%s: found %s when expecting {; origin scan skipped\n", __func__, token );
				parse_warned = true;
			}
			return;
		}
#else
			Host_Error( "%s: found %s when expecting {\n", __func__, token );
#endif

		model_found = origin_found = false;
		VectorClear( origin );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
#if XASH_GAMECUBE
			{
				if( !parse_warned )
				{
					Con_Reportf( S_WARN "%s: EOF without closing brace; origin scan skipped\n", __func__ );
					parse_warned = true;
				}
				return;
			}
#else
				Host_Error( "%s: EOF without closing brace\n", __func__ );
#endif
			if( token[0] == '}' ) break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
#if XASH_GAMECUBE
			{
				if( !parse_warned )
				{
					Con_Reportf( S_WARN "%s: EOF without closing brace; origin scan skipped\n", __func__ );
					parse_warned = true;
				}
				return;
			}
#else
				Host_Error( "%s: EOF without closing brace\n", __func__ );
#endif

			if( token[0] == '}' )
#if XASH_GAMECUBE
			{
				if( !parse_warned )
				{
					Con_Reportf( S_WARN "%s: closing brace without data; origin scan skipped\n", __func__ );
					parse_warned = true;
				}
				return;
			}
#else
				Host_Error( "%s: closing brace without data\n", __func__ );
#endif

			if( !Q_stricmp( keyname, "model" ) && !Q_stricmp( modelname, token ))
				model_found = true;

			if( !Q_stricmp( keyname, "origin" ))
			{
				Q_atov( origin, token, 3 );
				origin_found = true;
			}
		}

		if( model_found ) break;
	}
}

/*
==================
Mod_CheckWaterAlphaSupport

converted maps potential may don't
support water transparency
==================
*/
static qboolean Mod_CheckWaterAlphaSupport( model_t *mod, dbspmodel_t *bmod )
{
	if( bmod->visdatasize <= 0 )
		return true;

	// check all liquid leafs to see if they can see into empty leafs, if any
	// can we can assume this map supports r_wateralpha
	mleaf_t *leaf = mod->leafs;
	for( int i = 0; i < mod->numleafs; i++, leaf++ )
	{
		if(( leaf->contents == CONTENTS_WATER || leaf->contents == CONTENTS_SLIME ) && leaf->cluster >= 0 )
		{
			Mod_DecompressPVS( g_visdata, leaf->compressed_vis, world.visbytes );

			for( int j = 0; j < mod->numleafs; j++ )
			{
				if( CHECKVISBIT( g_visdata, mod->leafs[j].cluster ) && mod->leafs[j].contents == CONTENTS_EMPTY )
					return true;
			}
		}
	}

	return false;
}

/*
==================
Mod_SampleSizeForFace

return the current lightmap resolution per face
==================
*/
int Mod_SampleSizeForFace( const msurface_t *surf )
{
	if( !surf || !surf->texinfo )
		return LM_SAMPLE_SIZE;

	// world luxels has more priority
	if( FBitSet( surf->texinfo->flags, TEX_WORLD_LUXELS ))
		return 1;

	if( FBitSet( surf->texinfo->flags, TEX_EXTRA_LIGHTMAP ))
		return LM_SAMPLE_EXTRASIZE;

	if( surf->texinfo->faceinfo )
		return surf->texinfo->faceinfo->texture_step;

	return LM_SAMPLE_SIZE;
}

/*
==================
Mod_GetFaceContents

determine face contents by name
==================
*/
static int Mod_GetFaceContents( const char *name )
{
	if( !Q_strnicmp( name, "SKY", 3 ))
		return CONTENTS_SKY;

	if( name[0] == '!' || name[0] == '*' )
	{
		if( !Q_strnicmp( name + 1, "lava", 4 ))
			return CONTENTS_LAVA;
		else if( !Q_strnicmp( name + 1, "slime", 5 ))
			return CONTENTS_SLIME;
		return CONTENTS_WATER; // otherwise it's water
	}

	if( !Q_strnicmp( name, "water", 5 ))
		return CONTENTS_WATER;

	return CONTENTS_SOLID;
}

/*
==================
Mod_GetFaceContents

determine face contents by name
==================
*/
static mvertex_t *Mod_GetVertexByNumber( model_t *mod, int surfedge, const dbspmodel_t *bmod )
{
	int	lindex = mod->surfedges[surfedge];

	if( bmod->version == QBSP2_VERSION )
	{
		if( lindex > 0 )
		{
			medge32_t *edge = &mod->edges32[lindex];
			return &mod->vertexes[edge->v[0]];
		}
		else
		{
			medge32_t *edge = &mod->edges32[-lindex];
			return &mod->vertexes[edge->v[1]];
		}
	}
	else
	{
		if( lindex > 0 )
		{
			medge16_t *edge = &mod->edges16[lindex];
			return &mod->vertexes[edge->v[0]];
		}
		else
		{
			medge16_t *edge = &mod->edges16[-lindex];
			return &mod->vertexes[edge->v[1]];
		}
	}
}

/*
==================
Mod_MakeNormalAxial

remove jitter from near-axial normals
==================
*/
static void Mod_MakeNormalAxial( vec3_t normal )
{
	int	type;

	for( type = 0; type < 3; type++ )
	{
		if( fabs( normal[type] ) > 0.9999f )
			break;
	}

	// make positive and pure axial
	for( int i = 0; i < 3 && type != 3; i++ )
	{
		if( i == type )
			normal[i] = 1.0f;
		else normal[i] = 0.0f;
	}
}

/*
==================
Mod_LightMatrixFromTexMatrix

compute lightmap matrix based on texture matrix
==================
*/
static void Mod_LightMatrixFromTexMatrix( const mtexinfo_t *tx, float lmvecs[2][4] )
{
	float	lmscale = LM_SAMPLE_SIZE;

	// this is can't be possible but who knews
	if( FBitSet( tx->flags, TEX_EXTRA_LIGHTMAP ))
		lmscale = LM_SAMPLE_EXTRASIZE;

	if( tx->faceinfo )
		lmscale = tx->faceinfo->texture_step;

	// copy texmatrix into lightmap matrix fisrt
	for( int i = 0; i < 2; i++ )
	{
		for( int j = 0; j < 4; j++ )
		{
			lmvecs[i][j] = tx->vecs[i][j];
		}
	}

	if( !FBitSet( tx->flags, TEX_WORLD_LUXELS ))
		return; // just use texmatrix

	VectorNormalize( lmvecs[0] );
	VectorNormalize( lmvecs[1] );

	if( FBitSet( tx->flags, TEX_AXIAL_LUXELS ))
	{
		Mod_MakeNormalAxial( lmvecs[0] );
		Mod_MakeNormalAxial( lmvecs[1] );
	}

	// put the lighting origin at center the of poly
	VectorScale( lmvecs[0], (1.0f / lmscale), lmvecs[0] );
	VectorScale( lmvecs[1], -(1.0f / lmscale), lmvecs[1] );

	lmvecs[0][3] = lmscale * 0.5f;
	lmvecs[1][3] = -lmscale * 0.5f;
}

/*
=================
Mod_CalcSurfaceExtents

Fills in surf->texturemins[] and surf->extents[]
=================
*/
static void Mod_CalcSurfaceExtents( model_t *mod, msurface_t *surf, const dbspmodel_t *bmod )
{
	// this place is VERY critical to precision
	// keep it as float, don't use double, because it causes issues with lightmap
	float		mins[2], maxs[2], val;
	float		lmmins[2], lmmaxs[2];
	int		bmins[2], bmaxs[2];
	mextrasurf_t	*info = surf->info;
	mvertex_t	*v;

	int		sample_size = Mod_SampleSizeForFace( surf );
	mtexinfo_t	*tex = surf->texinfo;

	Mod_LightMatrixFromTexMatrix( tex, info->lmvecs );

	mins[0] = lmmins[0] = mins[1] = lmmins[1] = 999999;
	maxs[0] = lmmaxs[0] = maxs[1] = lmmaxs[1] =-999999;

	for( int i = 0; i < surf->numedges; i++ )
	{
		int e = mod->surfedges[surf->firstedge + i];

		if( e >= mod->numedges || e <= -mod->numedges )
			Host_Error( "%s: bad edge\n", __func__ );

		if( bmod->version == QBSP2_VERSION )
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges32[e].v[0]];
			else v = &mod->vertexes[mod->edges32[-e].v[1]];
		}
		else
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges16[e].v[0]];
			else v = &mod->vertexes[mod->edges16[-e].v[1]];
		}

		for( int j = 0; j < 2; j++ )
		{
			val = DotProductPrecise( v->position, surf->texinfo->vecs[j] ) + surf->texinfo->vecs[j][3];
			mins[j] = Q_min( val, mins[j] );
			maxs[j] = Q_max( val, maxs[j] );
		}

		for( int j = 0; j < 2; j++ )
		{
			val = DotProductPrecise( v->position, info->lmvecs[j] ) + info->lmvecs[j][3];
			lmmins[j] = Q_min( val, lmmins[j] );
			lmmaxs[j] = Q_max( val, lmmaxs[j] );
		}
	}

	for( int i = 0; i < 2; i++ )
	{
		bmins[i] = floor( mins[i] / sample_size );
		bmaxs[i] = ceil( maxs[i] / sample_size );

		surf->texturemins[i] = bmins[i] * sample_size;
		surf->extents[i] = (bmaxs[i] - bmins[i]) * sample_size;

		if( FBitSet( tex->flags, TEX_WORLD_LUXELS ))
		{
			lmmins[i] = floor( lmmins[i] );
			lmmaxs[i] = ceil( lmmaxs[i] );

			info->lightmapmins[i] = lmmins[i];
			info->lightextents[i] = (lmmaxs[i] - lmmins[i]);
		}
		else
		{
			// just copy texturemins
			info->lightmapmins[i] = surf->texturemins[i];
			info->lightextents[i] = surf->extents[i];
		}

#if !XASH_DEDICATED && 0 // REFTODO:
		if( !FBitSet( tex->flags, TEX_SPECIAL ) && ( surf->extents[i] > 16384 ) && ( tr.block_size == BLOCK_SIZE_DEFAULT ))
			Con_Reportf( S_ERROR "Bad surface extents %i\n", surf->extents[i] );
#endif // XASH_DEDICATED
	}
}

/*
=================
Mod_CalcSurfaceBounds

fills in surf->mins and surf->maxs
=================
*/
static void Mod_CalcSurfaceBounds( model_t *mod, msurface_t *surf, const dbspmodel_t *bmod )
{
	mvertex_t	*v;

	ClearBounds( surf->info->mins, surf->info->maxs );

	for( int i = 0; i < surf->numedges; i++ )
	{
		int e = mod->surfedges[surf->firstedge + i];

		if( e >= mod->numedges || e <= -mod->numedges )
			Host_Error( "%s: bad edge\n", __func__ );

		if( bmod->version == QBSP2_VERSION )
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges32[e].v[0]];
			else v = &mod->vertexes[mod->edges32[-e].v[1]];
		}
		else
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges16[e].v[0]];
			else v = &mod->vertexes[mod->edges16[-e].v[1]];
		}
		AddPointToBounds( v->position, surf->info->mins, surf->info->maxs );
	}

	VectorAverage( surf->info->mins, surf->info->maxs, surf->info->origin );
}

/*
=================
Mod_CreateFaceBevels
=================
*/
static void Mod_CreateFaceBevels( model_t *mod, msurface_t *surf, const dbspmodel_t *bmod )
{
	vec3_t		delta, edgevec;
	vec3_t		faceNormal;
	mvertex_t	*v0, *v1;
	int		contents;

	if( surf->texinfo && surf->texinfo->texture )
		contents = Mod_GetFaceContents( surf->texinfo->texture->name );
	else contents = CONTENTS_SOLID;

	int		size = sizeof( mfacebevel_t ) + surf->numedges * sizeof( mplane_t );
	byte		*facebevel = (byte *)Mem_Calloc( mod->mempool, size );
	mfacebevel_t	*fb = (mfacebevel_t *)facebevel;
	facebevel += sizeof( mfacebevel_t );
	fb->edges = (mplane_t *)facebevel;
	fb->numedges = surf->numedges;
	fb->contents = contents;
	surf->info->bevel = fb;

	if( FBitSet( surf->flags, SURF_PLANEBACK ))
		VectorNegate( surf->plane->normal, faceNormal );
	else VectorCopy( surf->plane->normal, faceNormal );

	// compute face origin and plane edges
	for( int i = 0; i < surf->numedges; i++ )
	{
		mplane_t	*dest = &fb->edges[i];

		v0 = Mod_GetVertexByNumber( mod, surf->firstedge + i, bmod );
		v1 = Mod_GetVertexByNumber( mod, surf->firstedge + (i + 1) % surf->numedges, bmod );
		VectorSubtract( v1->position, v0->position, edgevec );
		CrossProduct( faceNormal, edgevec, dest->normal );
		VectorNormalize( dest->normal );
		dest->dist = DotProduct( dest->normal, v0->position );
		dest->type = PlaneTypeForNormal( dest->normal );
		VectorAdd( fb->origin, v0->position, fb->origin );
	}

	VectorScale( fb->origin, 1.0f / surf->numedges, fb->origin );

	// compute face radius
	for( int i = 0; i < surf->numedges; i++ )
	{
		v0 = Mod_GetVertexByNumber( mod, surf->firstedge + i, bmod );
		VectorSubtract( v0->position, fb->origin, delta );
		vec_t radius = DotProduct( delta, delta );
		fb->radius = Q_max( radius, fb->radius );
	}
}

/*
=================
Mod_SetParent
=================
*/
static void Mod_SetParent( model_t *mod, mnode_t *node, mnode_t *parent )
{
	typedef struct
	{
		mnode_t *node;
		mnode_t *parent;
	} mod_parent_frame_t;

	mod_parent_frame_t *stack;
	byte *node_seen = NULL;
	byte *leaf_seen = NULL;
	size_t stack_capacity;
	size_t stack_size = 0;
	size_t duplicate_nodes = 0;
	size_t duplicate_leafs = 0;

	if( !node )
		return;

	stack_capacity = mod ? (size_t)Q_max( mod->numnodes, 1 ) : 1;
	stack = malloc( stack_capacity * sizeof( *stack ));
	if( mod )
	{
		if( mod->numnodes > 0 )
			node_seen = calloc( mod->numnodes, sizeof( *node_seen ));
		if( mod->numleafs > 0 )
			leaf_seen = calloc( mod->numleafs, sizeof( *leaf_seen ));
	}

	if( !stack || ( mod && (( mod->numnodes > 0 && !node_seen ) || ( mod->numleafs > 0 && !leaf_seen ))))
	{
		free( stack );
		free( node_seen );
		free( leaf_seen );
		node->parent = parent;

		if( node->contents < 0 )
			return; // it's leaf

		Mod_SetParent( mod, node_child( node, 0, mod ), node );
		Mod_SetParent( mod, node_child( node, 1, mod ), node );
		return;
	}

	stack[stack_size].node = node;
	stack[stack_size].parent = parent;
	stack_size++;

	while( stack_size > 0 )
	{
		mnode_t *cur;
		mnode_t *cur_parent;
		size_t node_index = 0;
		size_t leaf_index = 0;
		qboolean is_leaf = false;

		stack_size--;
		cur = stack[stack_size].node;
		cur_parent = stack[stack_size].parent;

		if( mod )
		{
			const byte *cur_ptr = (const byte *)cur;
			const byte *nodes_begin = (const byte *)mod->nodes;
			const byte *nodes_end = nodes_begin + mod->numnodes * sizeof( *mod->nodes );
			const byte *leafs_begin = (const byte *)mod->leafs;
			const byte *leafs_end = leafs_begin + mod->numleafs * sizeof( *mod->leafs );

			if( cur_ptr >= nodes_begin && cur_ptr < nodes_end )
			{
				node_index = (size_t)( cur - mod->nodes );

				if( node_seen && node_seen[node_index] )
				{
					duplicate_nodes++;
					continue;
				}

				if( node_seen )
					node_seen[node_index] = 1;
			}
			else if( cur_ptr >= leafs_begin && cur_ptr < leafs_end )
			{
				is_leaf = true;
				leaf_index = (size_t)((mleaf_t *)cur - mod->leafs );

				if( leaf_seen && leaf_seen[leaf_index] )
				{
					duplicate_leafs++;
					continue;
				}

				if( leaf_seen )
					leaf_seen[leaf_index] = 1;

				if( cur->contents >= 0 )
				{
					free( stack );
					free( node_seen );
					free( leaf_seen );
					Host_Error( "%s: leaf %zu has non-leaf contents %d in %s\n",
						__func__, leaf_index, cur->contents, mod->name );
					return;
				}
			}
			else
			{
				free( stack );
				free( node_seen );
				free( leaf_seen );
				Host_Error( "%s: child pointer %p is outside node/leaf ranges for %s\n",
					__func__, (void *)cur, mod->name );
				return;
			}
		}

		cur->parent = cur_parent;

		if( cur->contents < 0 )
			continue; // it's leaf

		if( is_leaf )
		{
			free( stack );
			free( node_seen );
			free( leaf_seen );
			Host_Error( "%s: leaf %zu treated as internal node in %s\n",
				__func__, leaf_index, mod->name );
			return;
		}

		if( stack_size + 2 > stack_capacity )
		{
			free( stack );
			free( node_seen );
			free( leaf_seen );
			Host_Error( "%s: parent walk stack overflow for %s (%zu nodes)\n",
				__func__, mod ? mod->name : "<unknown>", stack_capacity );
			return;
		}

		stack[stack_size].node = node_child( cur, 1, mod );
		stack[stack_size].parent = cur;
		stack_size++;
		stack[stack_size].node = node_child( cur, 0, mod );
		stack[stack_size].parent = cur;
		stack_size++;
	}

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && mod && ( duplicate_nodes || duplicate_leafs ))
	{
		Con_Reportf( "Xash3D GameCube: parent walk dedup nodes=%zu leafs=%zu map=%s\n",
			duplicate_nodes, duplicate_leafs, mod->name );
	}
#endif

	free( stack );
	free( node_seen );
	free( leaf_seen );
}

/*
==================
CountClipNodes_r
==================
*/
static void CountClipNodes16_r( mclipnode16_t *src, hull_t *hull, int nodenum )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == MAX_MAP_CLIPNODES_HLBSP )
		Host_Error( "%s: MAX_MAP_CLIPNODES_HLBSP limit exceeded\n", __func__ );
	hull->lastclipnode++;

	CountClipNodes16_r( src, hull, src[nodenum].children[0] );
	CountClipNodes16_r( src, hull, src[nodenum].children[1] );
}

static void CountClipNodes32_r( mclipnode32_t *src, hull_t *hull, int nodenum )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == MAX_MAP_CLIPNODES_BSP2 )
		Host_Error( "%s: MAX_MAP_CLIPNODES_BSP2 limit exceeded\n", __func__ );
	hull->lastclipnode++;

	CountClipNodes32_r( src, hull, src[nodenum].children[0] );
	CountClipNodes32_r( src, hull, src[nodenum].children[1] );
}

static void CountDClipNodes_r( dclipnode32_t *src, hull_t *hull, int nodenum, const int max_clipnodes )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == max_clipnodes )
		Host_Error( "%s: MAX_MAP_CLIPNODES (%d) limit exceeded\n", __func__, max_clipnodes );
	hull->lastclipnode++;

	CountDClipNodes_r( src, hull, src[nodenum].children[0], max_clipnodes );
	CountDClipNodes_r( src, hull, src[nodenum].children[1], max_clipnodes );
}

/*
==================
RemapClipNodes_r
==================
*/
static int RemapClipNodes_r( dbspmodel_t *bmod, dclipnode32_t *srcnodes, hull_t *hull, int nodenum )
{
	// leaf?
	if( nodenum < 0 )
		return nodenum;

	// emit a clipnode
	if( bmod->version == QBSP2_VERSION )
	{
		if( hull->lastclipnode == MAX_MAP_CLIPNODES_BSP2 )
			Host_Error( "%s: MAX_MAP_CLIPNODES_BSP2 limit exceeded\n", __func__ );
	}
	else
	{
		if( hull->lastclipnode == MAX_MAP_CLIPNODES_HLBSP )
			Host_Error( "%s: MAX_MAP_CLIPNODES_HLBSP limit exceeded\n", __func__ );
	}

	dclipnode32_t *src = srcnodes + nodenum;

	int c = hull->lastclipnode;
	hull->lastclipnode++;

	if( bmod->version == QBSP2_VERSION )
	{
		mclipnode32_t *out = &hull->clipnodes32[c];
		out->planenum = src->planenum;
		for( int i = 0; i < 2; i++ )
			out->children[i] = RemapClipNodes_r( bmod, srcnodes, hull, src->children[i] );
	}
	else
	{
		mclipnode16_t *out = &hull->clipnodes16[c];
		out->planenum = src->planenum;
		for( int i = 0; i < 2; i++ )
			out->children[i] = RemapClipNodes_r( bmod, srcnodes, hull, src->children[i] );
	}

	return c;
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0( model_t *mod, const dbspmodel_t *bmod )
{
	hull_t *hull = &mod->hulls[0];

	hull->firstclipnode = 0;
	hull->lastclipnode = mod->numnodes - 1;
	hull->planes = mod->planes;

	if( bmod->version == QBSP2_VERSION )
	{
		mclipnode32_t *out;
		mnode_t *in = mod->nodes;

		hull->clipnodes32 = out = Mem_Malloc( mod->mempool, mod->numnodes * sizeof( *hull->clipnodes32 ));

		for( int i = 0; i < mod->numnodes; i++, out++, in++ )
		{
			out->planenum = in->plane - mod->planes;

			for( int j = 0; j < 2; j++ )
			{
				mnode_t *child = node_child( in, j, mod );

				if( child->contents < 0 )
					out->children[j] = child->contents;
				else
					out->children[j] = child - mod->nodes;
			}
		}
	}
	else
	{
		mclipnode16_t *out = NULL;
		mnode_t *in = mod->nodes;

#if XASH_GAMECUBE
		if( GC_MapLoadMemoryOpt() && bmod->isworld && gc_retain_bsp_source_buffer
			&& gc_bsp_scratch_base && gc_bsp_scratch_size && mod->numnodes > 0 )
		{
			const size_t hull0_bytes = mod->numnodes * sizeof( *hull->clipnodes16 );
			gc_bsp_busy_range_t busy[10];
			size_t busy_count = 0;

			if( mod->surfaces && Mod_GCPointerInBuffer( mod->surfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->surfaces;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start
					+ bmod->numsurfaces * ( sizeof( msurface_t ) + sizeof( mextrasurf_t ));
				busy_count++;
			}

			if( mod->texinfo && Mod_GCPointerInBuffer( mod->texinfo, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->texinfo;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->numtexinfo * sizeof( mtexinfo_t );
				busy_count++;
			}

			if( mod->marksurfaces && Mod_GCPointerInBuffer( mod->marksurfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->marksurfaces;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->nummarksurfaces * sizeof( *mod->marksurfaces );
				busy_count++;
			}

			if( mod->leafs && Mod_GCPointerInBuffer( mod->leafs, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->leafs;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->numleafs * sizeof( *mod->leafs );
				busy_count++;
			}

			if( mod->nodes && Mod_GCPointerInBuffer( mod->nodes, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->nodes;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->numnodes * sizeof( *mod->nodes );
				busy_count++;
			}

			if( mod->clipnodes16 && Mod_GCPointerInBuffer( mod->clipnodes16, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->clipnodes16;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->numclipnodes * sizeof( *mod->clipnodes16 );
				busy_count++;
			}

			if( Mod_GCPointerInBuffer( bmod->lightdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->lightdatasize )
			{
				const byte *p = (const byte *)bmod->lightdata;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + bmod->lightdatasize;
				busy_count++;
			}

			if( Mod_GCPointerInBuffer( bmod->deluxdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->deluxdatasize )
			{
				const byte *p = (const byte *)bmod->deluxdata;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + bmod->deluxdatasize;
				busy_count++;
			}

			if( Mod_GCPointerInBuffer( bmod->shadowdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->shadowdatasize )
			{
				const byte *p = (const byte *)bmod->shadowdata;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + bmod->shadowdatasize;
				busy_count++;
			}

			if( Mod_GCPointerInBuffer( bmod->rgblightdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->rgblightdatasize )
			{
				const byte *p = (const byte *)bmod->rgblightdata;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + bmod->rgblightdatasize;
				busy_count++;
			}

			for( size_t i = 0; i < busy_count; i++ )
			{
				for( size_t j = i + 1; j < busy_count; j++ )
				{
					if( busy[j].start < busy[i].start )
					{
						gc_bsp_busy_range_t tmp = busy[i];
						busy[i] = busy[j];
						busy[j] = tmp;
					}
				}
			}

			out = (mclipnode16_t *)Mod_GCAllocBspScratch( gc_bsp_scratch_base, gc_bsp_scratch_size, busy, busy_count, hull0_bytes, 32 );
			if( out )
			{
				hull->clipnodes16 = out;
				Con_Reportf( "Xash3D GameCube: world hull0 using BSP scratch %s\n", Q_memprint( hull0_bytes ));
			}
		}
#endif
		if( !out )
			hull->clipnodes16 = out = Mem_Malloc( mod->mempool, mod->numnodes * sizeof( *hull->clipnodes16 ));

		for( int i = 0; i < mod->numnodes; i++, out++, in++ )
		{
			out->planenum = in->plane - mod->planes;

			for( int j = 0; j < 2; j++ )
			{
				mnode_t *child = node_child( in, j, mod );

				if( child->contents < 0 )
					out->children[j] = child->contents;
				else
					out->children[j] = child - mod->nodes;
			}
		}
	}

}

/*
=================
Mod_SetupHull
=================
*/
static void Mod_SetupHull( dbspmodel_t *bmod, model_t *mod, int headnode, int hullnum, model_t *world )
{
	hull_t *hull = &mod->hulls[hullnum];

	switch( hullnum )
	{
	case 1:
		VectorCopy( host.player_mins[0], hull->clip_mins ); // copy human hull
		VectorCopy( host.player_maxs[0], hull->clip_maxs );
		break;
	case 2:
		VectorCopy( host.player_mins[3], hull->clip_mins ); // copy large hull
		VectorCopy( host.player_maxs[3], hull->clip_maxs );
		break;
	case 3:
		VectorCopy( host.player_mins[1], hull->clip_mins ); // copy head hull
		VectorCopy( host.player_maxs[1], hull->clip_maxs );
		break;
	default:
		Host_Error( "%s: bad hull number %i\n", __func__, hullnum );
		break;
	}

	if( VectorIsNull( hull->clip_mins ) && VectorIsNull( hull->clip_maxs ))
		return;	// no hull specified

	// assume no hull
	hull->firstclipnode = hull->lastclipnode = 0;
	hull->planes = NULL; // hull is missed

	if( headnode >= mod->numclipnodes )
		return;	// ZHLT weird empty hulls

	// bsp30ext allows for extended total amount of clipnodes, but the limit is still 16-bit per submodel
	// therefore we need to remap them
	// take a simpler route if we don't need clipnodes remapping
	if( !bmod->isbsp30ext )
	{
		hull->planes = mod->planes;

		// some map "optimizers" (you know who you are!) put -1 here
		// ... and it's purposefully? encode CONTENTS_EMPTY sometimes
		// but might cause out of bounds reads
		hull->firstclipnode = headnode;
		hull->lastclipnode = mod->numclipnodes - 1;

		// only allocate clipnodes array for the base model, only for first hull
		if( mod == world && hullnum == 1 )
		{
			if( bmod->version == QBSP2_VERSION )
			{
				hull->clipnodes32 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes32 ) * mod->numclipnodes );

				for( int i = 0; i < mod->numclipnodes; i++ )
				{
					hull->clipnodes32[i].planenum = bmod->clipnodes_out[i].planenum;
					hull->clipnodes32[i].children[0] = bmod->clipnodes_out[i].children[0];
					hull->clipnodes32[i].children[1] = bmod->clipnodes_out[i].children[1];
				}
			}
			else
			{
				if( bmod->clipnodes_out )
				{
					hull->clipnodes16 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes16 ) * mod->numclipnodes );

					for( int i = 0; i < mod->numclipnodes; i++ )
					{
						hull->clipnodes16[i].planenum = bmod->clipnodes_out[i].planenum;
						hull->clipnodes16[i].children[0] = bmod->clipnodes_out[i].children[0];
						hull->clipnodes16[i].children[1] = bmod->clipnodes_out[i].children[1];
					}
				}
				else
				{
#if XASH_GAMECUBE
					/* Prefer the owned compact array when BSP pin was freed. */
					if( world->clipnodes16 )
						hull->clipnodes16 = world->clipnodes16;
					else
#endif
					hull->clipnodes16 = (mclipnode16_t *)bmod->clipnodes;
				}
			}
		}
		else
		{
			if( bmod->version == QBSP2_VERSION )
				hull->clipnodes32 = world->hulls[1].clipnodes32;
			else
				hull->clipnodes16 = world->hulls[1].clipnodes16;
		}

		return;
	}

	if(( headnode == -1 ) || ( hullnum != 1 && headnode == 0 ))
		return; // hull missed

	// fit array to real count
	if( bmod->version == QBSP2_VERSION )
	{
		CountDClipNodes_r( bmod->clipnodes_out, hull, headnode, MAX_MAP_CLIPNODES_BSP2 );
		hull->clipnodes32 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes32 ) * hull->lastclipnode );
	}
	else
	{
		CountDClipNodes_r( bmod->clipnodes_out, hull, headnode, MAX_MAP_CLIPNODES_HLBSP );
		hull->clipnodes16 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes16 ) * hull->lastclipnode );
	}

	hull->planes = mod->planes; // share planes
	hull->lastclipnode = 0; // restart counting

	RemapClipNodes_r( bmod, bmod->clipnodes_out, hull, headnode ); // remap clipnodes to 16-bit indexes
}

static qboolean Mod_LoadLitfile( model_t *mod, const char *ext, size_t expected_size, color24 **out, size_t *outsize )
{
	char        modelname[64], path[64];

	COM_FileBase( mod->name, modelname, sizeof( modelname ));
	Q_snprintf( path, sizeof( path ), "maps/%s.%s", modelname, ext );

	int iCompare;
	if( !pfnCompareFileTime( path, mod->name, &iCompare ))
		return false;

	if( iCompare < 0 ) // this may happens if level-designer used -onlyents key for hlcsg
		Con_Printf( S_WARN "%s probably is out of date\n", path );

	file_t *f = FS_Open( path, "rb", false );

	if( !f )
	{
		Con_Printf( S_ERROR "couldn't load %s\n", path );
		return false;
	}

	// skip header bytes
	fs_offset_t datasize = FS_FileLength( f ) - 8;
	uint        hdr[2];

	if( datasize != expected_size )
	{
		Con_Printf( S_ERROR "%s has mismatched size (%li should be %zu)\n", path, (long)datasize, expected_size );
		goto cleanup_and_error;
	}

	if( FS_Read( f, hdr, sizeof( hdr )) != sizeof( hdr ))
	{
		Con_Printf( S_ERROR "failed reading header from %s\n", path );
		goto cleanup_and_error;
	}

	if( LittleLong( hdr[0] ) != IDDELUXEMAPHEADER )
	{
		Con_Printf( S_ERROR "%s is corrupted\n", path );
		goto cleanup_and_error;
	}

	if( LittleLong( hdr[1] ) != DELUXEMAP_VERSION )
	{
		Con_Printf( S_ERROR "has %s mismatched version (%u should be %u)\n", path, LittleLong( hdr[1] ), DELUXEMAP_VERSION );
		goto cleanup_and_error;
	}

	*out = Mem_Malloc( mod->mempool, datasize );
	*outsize = datasize;

	FS_Read( f, *out, datasize );
	FS_Close( f );
	return true;

cleanup_and_error:
	FS_Close( f );
	return false;
}

/*
=================
Mod_SetupSubmodels

duplicate the basic information
for embedded submodels
=================
*/
static void Mod_SetupSubmodels( model_t *mod, dbspmodel_t *bmod )
{
	const qboolean colored = FBitSet( mod->flags, MODEL_COLORED_LIGHTING ) ? true : false;
	const qboolean qbsp2 = FBitSet( mod->flags, MODEL_QBSP2 ) ? true : false;
	const char *name = mod->name;
	model_t *world = mod; // submodels might want to share hulls

	mod->numframes = 2;	// regular and alternate animation

	// set up the submodels
	for( int i = 0; i < mod->numsubmodels; i++ )
	{
		dmodel_t *bm = &mod->submodels[i];
#if XASH_GAMECUBE
		if(( i & 7 ) == 0 )
			Con_Reportf( "Xash3D GameCube: bmodel submodel %i/%zu\n", i, mod->numsubmodels );
#endif

		// hull 0 is just shared across all bmodels
		mod->hulls[0].firstclipnode = bm->headnode[0];
		mod->hulls[0].lastclipnode = bm->headnode[0]; // need to be real count

		// counting a real number of clipnodes per each submodel
		if( bmod->version == QBSP2_VERSION )
			CountClipNodes32_r( mod->hulls[0].clipnodes32, &mod->hulls[0], bm->headnode[0] );
		else
			CountClipNodes16_r( mod->hulls[0].clipnodes16, &mod->hulls[0], bm->headnode[0] );

		// but hulls1-3 is build individually for a each given submodel
		for( int j = 1; j < MAX_MAP_HULLS; j++ )
			Mod_SetupHull( bmod, mod, bm->headnode[j], j, world );

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy( bm->mins, mod->mins );
		VectorCopy( bm->maxs, mod->maxs );

		mod->radius = RadiusFromBounds( mod->mins, mod->maxs );
		mod->numleafs = bm->visleafs;
		mod->flags = 0;

		// this bit will be shared between all the submodels include worldmodel
		if( colored ) SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		if( qbsp2 ) SetBits( mod->flags, MODEL_QBSP2 );

		if( i != 0 )
		{
			char temp[MAX_VA_STRING];

			Q_snprintf( temp, sizeof( temp ), "*%i", i );
#if XASH_GAMECUBE
			if( !Sys_CheckParm( "-gcmap" ))
#endif
				Mod_FindModelOrigin( world->entities, temp, bm->origin );

			// mark models that have origin brushes
			if( !VectorIsNull( bm->origin ))
				SetBits( mod->flags, MODEL_HAS_ORIGIN );
#ifdef HACKS_RELATED_HLMODS
			// c2a1 doesn't have origin brush it's just placed at center of the level
			if( i == 11 && !Q_stricmp( name, "maps/c2a1.bsp" ))
				SetBits( mod->flags, MODEL_HAS_ORIGIN );
#endif
		}

		// sets the model flags
		for( int j = 0; i != 0 && j < mod->nummodelsurfaces; j++ )
		{
			msurface_t *surf = mod->surfaces + mod->firstmodelsurface + j;

			if( FBitSet( surf->flags, SURF_CONVEYOR ))
				SetBits( mod->flags, MODEL_CONVEYOR );

			if( FBitSet( surf->flags, SURF_TRANSPARENT ))
				SetBits( mod->flags, MODEL_TRANSPARENT );

			if( FBitSet( surf->flags, SURF_DRAWTURB ))
				SetBits( mod->flags, MODEL_LIQUID );
		}

		if( i < mod->numsubmodels - 1 )
		{
			char	name[8];

			// duplicate the basic information
			Q_snprintf( name, sizeof( name ), "*%i", i + 1 );
			model_t *submod = Mod_FindName( name, true );
			*submod = *mod;
			Q_strncpy( submod->name, name, sizeof( submod->name ));
			submod->mempool = 0;
			mod = submod;
		}
	}

	if( bmod->clipnodes_out != NULL )
		Mem_Free( bmod->clipnodes_out );
}

/*
===============================================================================

			MAP LOADING

===============================================================================
*/
/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels( model_t *mod, dbspmodel_t *bmod )
{
	// allocate extradata for each dmodel_t
	dmodel_t *out = Mem_Malloc( mod->mempool, bmod->numsubmodels * sizeof( *out ));

	mod->numsubmodels = bmod->numsubmodels;
	mod->submodels = out;
	dmodel_t *in = bmod->submodels;

	if( bmod->isworld )
		refState.max_surfaces = 0;
	int oldmaxfaces = refState.max_surfaces;

	for( int i = 0; i < bmod->numsubmodels; i++, in++, out++ )
	{
		for( int j = 0; j < 3; j++ )
		{
			// reset empty bounds to prevent error
			if( in->mins[j] == 999999.0f )
				in->mins[j] = 0.0f;
			if( in->maxs[j] == -999999.0f)
				in->maxs[j] = 0.0f;

			// spread the mins / maxs by a unit
			out->mins[j] = in->mins[j] - 1.0f;
			out->maxs[j] = in->maxs[j] + 1.0f;
			out->origin[j] = in->origin[j];
		}

		for( int j = 0; j < MAX_MAP_HULLS; j++ )
			out->headnode[j] = in->headnode[j];

		out->visleafs = in->visleafs;
		out->firstface = in->firstface;
		out->numfaces = in->numfaces;

		if( i == 0 && bmod->isworld )
			continue; // skip the world to save mem
		oldmaxfaces = Q_max( oldmaxfaces, out->numfaces );
	}

	// these array used to sort translucent faces in bmodels
	if( oldmaxfaces > refState.max_surfaces )
	{
		refState.draw_surfaces = (sortedface_t *)Z_Realloc( refState.draw_surfaces, oldmaxfaces * sizeof( sortedface_t ));
		refState.max_surfaces = oldmaxfaces;
	}
}

static int Mod_LoadEntities_splitstr_handler( char *prev, char *next, void *userdata )
{
	world_static_t *w = userdata;

	*next = '\0';

	if( COM_StringEmpty( prev ))
		return 0;

	COM_FixSlashes( prev );
	const char *wad = COM_FileWithoutPath( prev );

	if( Q_stricmp( COM_FileExtension( wad ), "wad" ))
		return 0;

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() )
		return 0;
#endif

	// make sure that wad does really exists
	if( FS_FileExists( wad, false ))
	{
		int num = w->wadcount++;

		// FIXME: that's right, it goes into host.mempool!
		w->wadlist = Mem_Realloc( host.mempool, w->wadlist, w->wadcount * sizeof( *w->wadlist ));

		Q_strncpy( w->wadlist[num].name, wad, sizeof( w->wadlist[num].name ));
		w->wadlist[num].usage = 0;
	}

	return 0;
}

/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities( model_t *mod, const dbspmodel_t *bmod )
{
	byte   *entpatch = NULL;
	char   token[MAX_TOKEN];
	string keyname;
	char   *entdata = bmod->entdata;
	size_t entdatasize = bmod->entdatasize;

	if( bmod->isworld )
	{
#if XASH_GAMECUBE
		if( !Sys_CheckParm( "-gcmap" ))
#endif
		{
		char        entfilename[MAX_QPATH];
		fs_offset_t	entpatchsize;

		// if world check for entfile too
		Q_strncpy( entfilename, mod->name, sizeof( entfilename ));
		COM_ReplaceExtension( entfilename, ".ent", sizeof( entfilename ));

		// make sure that entity patch is never than bsp
		int ft1 = FS_FileTime( mod->name, false );
		int ft2 = FS_FileTime( entfilename, true );

		if( ft2 != -1 )
		{
			if( ft1 > ft2 )
			{
				Con_Printf( S_WARN "Entity patch is older than bsp. Ignored.\n" );
			}
			else if(( entpatch = FS_LoadFile( entfilename, &entpatchsize, true )) != NULL )
			{
				Con_Printf( "^2Read entity patch:^7 %s\n", entfilename );
				entdatasize = entpatchsize;
				entdata = entpatch;
			}
		}
		}
	}

	// make sure that we really have null terminator
	mod->entities = Mem_Malloc( mod->mempool, entdatasize + 1 );
	memcpy( mod->entities, entdata, entdatasize ); // moving to private model pool
	mod->entities[entdatasize] = 0;

	Mem_Free( entpatch ); // release entpatch if present
	entpatch = NULL;

	if( !bmod->isworld )
		return;

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() )
	{
		Con_Reportf( "Xash3D GameCube: bmodel entities copied %zu bytes\n", entdatasize );
		return;
	}
#endif

	char *pfile = (char *)mod->entities;
	Mem_Free( world.generator );
	world.generator = NULL;

	Mem_Free( world.compiler );
	world.compiler = NULL;

	Mem_Free( world.message );
	world.message = NULL;

	Mem_Free( world.wadlist );
	world.wadlist = NULL;
	world.wadcount = 0;

	world.litwater_minlight = -1;
	world.litwater_scale = -1.0f;

	// parse all the wads for loading textures in right ordering
	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( token[0] != '{' )
			Host_Error( "%s: found %s when expecting {\n", __func__, token );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "%s: EOF without closing brace\n", __func__ );

			if( token[0] == '}' )
				break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "%s: EOF without closing brace\n", __func__ );

			if( token[0] == '}' )
				Host_Error( "%s: closing brace without data\n", __func__ );

			if( !Q_stricmp( keyname, "wad" ))
			{
				Q_splitstr( token, ';', &world, Mod_LoadEntities_splitstr_handler );
			}
			else if( !Q_stricmp( keyname, "message" ))
			{
				Mem_Free( world.message );
				world.message = copystring( token ); // FIXME: owned by host.mempool
			}
			else if( !Q_stricmp( keyname, "compiler" ) || !Q_stricmp( keyname, "_compiler" ))
			{
				Mem_Free( world.compiler );
				world.compiler = copystring( token ); // FIXME: owned by host.mempool
			}
			else if( !Q_stricmp( keyname, "generator" ) || !Q_stricmp( keyname, "_generator" ))
			{
				Mem_Free( world.generator );
				world.generator = copystring( token );
			}
			else if( !Q_stricmp( keyname, "_litwater" ))
			{
				if( Q_atoi( token ) != 0 )
					SetBits( world.flags, FWORLD_HAS_LITWATER );
			}
			else if( !Q_stricmp( keyname, "_litwater_minlight" ))
				world.litwater_minlight = Q_atoi( token );
			else if( !Q_stricmp( keyname, "_litwater_scale" ))
				world.litwater_scale = Q_atof( token );
		}
		return;	// all done
	}
}

/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes( model_t *mod, const dbspmodel_t *bmod )
{
	mplane_t	*out;

	dplane_t	*in = bmod->planes;
	mod->planes = out = Mem_Malloc( mod->mempool, bmod->numplanes * sizeof( *out ));
	mod->numplanes = bmod->numplanes;

	for( int i = 0; i < bmod->numplanes; i++, in++, out++ )
	{
		out->signbits = 0;
		for( int j = 0; j < 3; j++ )
		{
			out->normal[j] = in->normal[j];

			if( out->normal[j] < 0.0f )
				SetBits( out->signbits, BIT( j ));
		}

		if( VectorLength( out->normal ) < 0.5f )
			Con_Printf( S_ERROR "bad normal for plane #%i\n", i );

		out->dist = in->dist;
		out->type = in->type;
	}
}

/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes( model_t *mod, dbspmodel_t *bmod )
{
	mvertex_t	*out;

	dvertex_t	*in = bmod->vertexes;
	out = mod->vertexes = Mem_Malloc( mod->mempool, bmod->numvertexes * sizeof( mvertex_t ));
	mod->numvertexes = bmod->numvertexes;

	if( bmod->isworld ) ClearBounds( world.mins, world.maxs );

	for( int i = 0; i < bmod->numvertexes; i++, in++, out++ )
	{
		if( bmod->isworld )
			AddPointToBounds( in->point, world.mins, world.maxs );
		VectorCopy( in->point, out->position );
	}

	if( !bmod->isworld ) return;

	VectorSubtract( world.maxs, world.mins, world.size );

	for( int i = 0; i < 3; i++ )
	{
		// spread the mins / maxs by a pixel
		world.mins[i] -= 1.0f;
		world.maxs[i] += 1.0f;
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges( model_t *mod, dbspmodel_t *bmod )
{
	mod->numedges = bmod->numedges;

	if( bmod->version == QBSP2_VERSION )
	{
		dedge32_t *in = bmod->edges32;
		medge32_t *out;
		mod->edges32 = out = Mem_Malloc( mod->mempool, bmod->numedges * sizeof( *out ));

		for( int i = 0; i < bmod->numedges; i++, in++, out++ )
		{
			out->v[0] = in->v[0];
			out->v[1] = in->v[1];
		}
	}
	else
	{
		dedge_t	*in = bmod->edges;
		medge16_t *out;
		mod->edges16 = out = Mem_Malloc( mod->mempool, bmod->numedges * sizeof( *out ));

		for( int i = 0; i < bmod->numedges; i++, in++, out++ )
		{
			out->v[0] = (word)in->v[0];
			out->v[1] = (word)in->v[1];
		}
	}
}

/*
=================
Mod_LoadSurfEdges
=================
*/
static void Mod_LoadSurfEdges( model_t *mod, dbspmodel_t *bmod )
{
	mod->surfedges = Mem_Malloc( mod->mempool, bmod->numsurfedges * sizeof( dsurfedge_t ));
	memcpy( mod->surfedges, bmod->surfedges, bmod->numsurfedges * sizeof( dsurfedge_t ));
	mod->numsurfedges = bmod->numsurfedges;
}

/*
=================
Mod_LoadMarkSurfaces
=================
*/
static void Mod_LoadMarkSurfaces( model_t *mod, dbspmodel_t *bmod )
{
	msurface_t	**out = NULL;

#if XASH_GAMECUBE
	{
		const size_t mark_bytes = bmod->nummarkfaces * sizeof( *out );
		gc_bsp_busy_range_t busy[4];
		size_t busy_count = 0;

		if( GC_MapLoadMemoryOpt() && gc_retain_bsp_source_buffer && gc_bsp_scratch_base && gc_bsp_scratch_size )
		{
			if( mod->surfaces && Mod_GCPointerInBuffer( mod->surfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->surfaces;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start
					+ bmod->numsurfaces * ( sizeof( msurface_t ) + sizeof( mextrasurf_t ));
				busy_count++;
			}

			if( mod->texinfo && Mod_GCPointerInBuffer( mod->texinfo, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->texinfo;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->numtexinfo * sizeof( mtexinfo_t );
				busy_count++;
			}

			if( bmod->version == QBSP2_VERSION )
			{
				if( bmod->markfaces32 && Mod_GCPointerInBuffer( bmod->markfaces32, gc_bsp_scratch_base, gc_bsp_scratch_size ))
				{
					const byte *p = (const byte *)bmod->markfaces32;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + bmod->nummarkfaces * sizeof( dmarkface32_t );
					busy_count++;
				}
			}
			else if( bmod->markfaces && Mod_GCPointerInBuffer( bmod->markfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)bmod->markfaces;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + bmod->nummarkfaces * sizeof( dmarkface_t );
				busy_count++;
			}

			for( size_t i = 0; i < busy_count; i++ )
			{
				for( size_t j = i + 1; j < busy_count; j++ )
				{
					if( busy[j].start < busy[i].start )
					{
						gc_bsp_busy_range_t tmp = busy[i];
						busy[i] = busy[j];
						busy[j] = tmp;
					}
				}
			}

			out = (msurface_t **)Mod_GCAllocBspScratch( gc_bsp_scratch_base, gc_bsp_scratch_size, busy, busy_count, mark_bytes, 32 );
			if( out )
			{
				Mod_GCInvalidateScratchOverlap( bmod, gc_bsp_scratch_base, gc_bsp_scratch_size, (byte *)out, mark_bytes );
				Con_Reportf( "Xash3D GameCube: world marksurfaces using BSP scratch %s\n", Q_memprint( mark_bytes ));
			}
		}

		if( !out )
		{
		out = (msurface_t **)malloc( mark_bytes );
		if( out )
		{
			gc_marksurfaces_malloc_block = out;
			Con_Reportf( "Xash3D GameCube: world marksurfaces via malloc %s\n", Q_memprint( mark_bytes ));
		}
		else
		{
			out = Mem_Malloc( mod->mempool, mark_bytes );
		}
		}
		mod->marksurfaces = out;
	}
#else
	mod->marksurfaces = out = Mem_Malloc( mod->mempool, bmod->nummarkfaces * sizeof( *out ));
#endif
	mod->nummarksurfaces = bmod->nummarkfaces;

	if( bmod->version == QBSP2_VERSION )
	{
		const dmarkface32_t *in = bmod->markfaces32;

		for( int i = 0; i < bmod->nummarkfaces; i++ )
		{
			if( in[i] < 0 || in[i] >= mod->numsurfaces )
				Host_Error( "%s: bad surface number %i at %i (max %i) in '%s'\n", __func__, in[i], i, mod->numsurfaces, mod->name );
			out[i] = mod->surfaces + in[i];
		}
	}
	else
	{
		const dmarkface_t *in = bmod->markfaces;

		for( int i = 0; i < bmod->nummarkfaces; i++ )
		{
			// NOTE: some of the buggy compilers have written a broken BSP file
			// with marksurface pointing at negative surface, for example darkf6.bsp
			// and darkf26.bsp in darkfuture mod. GoldSrc straight up writes
			// invalid pointer to a surface. Try to fix up these cases...
			if( mod->numsurfaces <= INT16_MAX && (int16_t)in[i] < 0 )
			{
				Con_Printf( S_WARN "%s: fixing up bad surface number %i at %i (max %i) in '%s'\n", __func__, in[i], i, mod->numsurfaces, mod->name );
				out[i] = mod->surfaces;
				continue;
			}

			if( in[i] < 0 || in[i] >= mod->numsurfaces )
				Host_Error( "%s: bad surface number %i at %i (max %i) in '%s'\n", __func__, in[i], i, mod->numsurfaces, mod->name );
			out[i] = mod->surfaces + in[i];
		}
	}

#if XASH_GAMECUBE
	if( bmod->version == QBSP2_VERSION )
		Mod_GCFreeBspPin( (void **)&bmod->markfaces32 );
	else
		Mod_GCFreeBspPin( (void **)&bmod->markfaces );
#endif
}

static qboolean Mod_LooksLikeWaterTexture( const char *name )
{
	if(( name[0] == '*' && Q_stricmp( name, REF_DEFAULT_TEXTURE )) || name[0] == '!' )
		return true;

	if( !Host_IsQuakeCompatible( ))
	{
		if( !Q_strncmp( name, "water", 5 ) || !Q_strnicmp( name, "laser", 5 ))
			return true;
	}

	return false;
}

static void Mod_TextureReplacementReport( const char *modelname, const char *texname, const char *type, int gl_texturenum, const char *foundpath )
{
	if( host_allow_materials.value != 2.0f )
		return;

	if( gl_texturenum > 0 ) // found and loaded successfully
		Con_Printf( "Looking for %s:%s%s tex replacement..." S_GREEN "OK (%s)\n", modelname, texname, type, foundpath );
	else if( gl_texturenum < 0 ) // not found
		Con_Printf( "Looking for %s:%s%s tex replacement..." S_YELLOW "MISS (%s)\n", modelname, texname, type, foundpath );
	else // found but not loaded
		Con_Printf( "Looking for %s:%s%s tex replacement..." S_RED "FAIL (%s)\n", modelname, texname, type, foundpath );
}

static qboolean Mod_SearchForTextureReplacement( char *out, size_t size, const char *modelname, const char *texname, const char *type )
{
	const char *subdirs[] = { modelname, "common" };

	for( int i = 0; i < ARRAYSIZE( subdirs ); i++ )
	{
		if( Q_snprintf( out, size, "materials/%s/%s%s.tga", subdirs[i], texname, type ) < 0 )
			continue; // truncated name

		if( g_fsapi.FileExists( out, false ))
			return true; // found, load it
	}

	Mod_TextureReplacementReport( modelname, texname, type, -1, "not found" );

	return false;
}

static void Mod_InitSkyClouds( model_t *mod, const mip_t *mt, texture_t *tx, qboolean custom_palette )
{
#if !XASH_DEDICATED
	rgbdata_t	r_temp, *r_sky;
	uint	*trans, *rgba;
	uint	transpix;
	int	r, g, b;
	int	p;
	string	texname;
	int solidskyTexture = 0, alphaskyTexture = 0;

	if( !ref.initialized )
		return;

#if XASH_GAMECUBE
	/*
	 * The GX renderer does not currently consume the legacy Quake sky-cloud
	 * pair, and splitting it costs enough transient image memory to block
	 * early HL1 maps such as c1a0 before active rendering.
	 */
	Con_DPrintf( "Xash3D GameCube: skipping legacy sky cloud split for %s\n", tx->name );
	return;
#endif

	if( Mod_AllowMaterials( ))
	{
		rgbdata_t *pic;

		if( Mod_SearchForTextureReplacement( texname, sizeof( texname ), mod->name, mt->name, "_solid" ))
		{
			pic = FS_LoadImage( texname, NULL, 0 );
			if( pic )
			{
				// need to do rename texture to properly cleanup these textures on reload
				solidskyTexture = GL_LoadTextureInternal( "solid_sky", pic, TF_NOMIPMAP );
				Mod_TextureReplacementReport( mod->name, mt->name, "_solid", solidskyTexture, texname );
				FS_FreeImage( pic );
			}
		}

		if( Mod_SearchForTextureReplacement( texname, sizeof( texname ), mod->name, mt->name, "_alpha" ))
		{
			pic = FS_LoadImage( texname, NULL, 0 );
			if( pic )
			{
				alphaskyTexture = GL_LoadTextureInternal( "alpha_sky", pic, TF_NOMIPMAP );
				Mod_TextureReplacementReport( mod->name, mt->name, "_alpha", alphaskyTexture, texname );
				FS_FreeImage( pic );
			}
		}

		if( !solidskyTexture || !alphaskyTexture )
		{
			ref.dllFuncs.GL_FreeTexture( solidskyTexture );
			ref.dllFuncs.GL_FreeTexture( alphaskyTexture );
		}
		else goto done; // replacements found, notify the renderer and exit
	}

	Q_snprintf( texname, sizeof( texname ), "%s%s.mip", ( mt->offsets[0] > 0 ) ? "#" : "", tx->name );

	if( mt->offsets[0] > 0 )
	{
		size_t size = sizeof( mip_t ) + (( mt->width * mt->height * 85 ) >> 6 );

		if( custom_palette )
			size += sizeof( short ) + 768;

		Image_SetForceFlags( IL_HOST_ENDIAN );
		r_sky = FS_LoadImage( texname, (byte *)mt, size );
	}
	else
	{
		// okay loading it from wad
		r_sky = FS_LoadImage( texname, NULL, 0 );
	}

	if( !r_sky || !r_sky->palette || r_sky->type != PF_INDEXED_32 || r_sky->height == 0 )
	{
		Con_Printf( S_ERROR "%s: unable to load sky texture %s\n", __func__, tx->name );

		if( r_sky )
			FS_FreeImage( r_sky );

		return;
	}

	// make an average value for the back to avoid
	// a fringe on the top level
	trans = Mem_Malloc( host.mempool, r_sky->height * r_sky->height * sizeof( *trans ));
	r = g = b = 0;

	for( int i = 0; i < r_sky->width >> 1; i++ )
	{
		for( int j = 0; j < r_sky->height; j++ )
		{
			p = r_sky->buffer[i * r_sky->width + j + r_sky->height];
			rgba = (uint *)r_sky->palette + p;
			trans[(i * r_sky->height) + j] = *rgba;
			r += ((byte *)rgba)[0];
			g += ((byte *)rgba)[1];
			b += ((byte *)rgba)[2];
		}
	}

	((byte *)&transpix)[0] = r / ( r_sky->height * r_sky->height );
	((byte *)&transpix)[1] = g / ( r_sky->height * r_sky->height );
	((byte *)&transpix)[2] = b / ( r_sky->height * r_sky->height );
	((byte *)&transpix)[3] = 0;

	// build a temporary image
	r_temp = *r_sky;
	r_temp.width = r_sky->width >> 1;
	r_temp.height = r_sky->height;
	r_temp.type = PF_RGBA_32;
	r_temp.flags = IMAGE_HAS_COLOR;
	r_temp.size = r_temp.width * r_temp.height * 4;
	r_temp.buffer = (byte *)trans;
	r_temp.palette = NULL;

	// load it in
	solidskyTexture = GL_LoadTextureInternal( "solid_sky", &r_temp, TF_NOMIPMAP | TF_ALLOW_NEAREST );

	for( int i = 0; i < r_sky->width >> 1; i++ )
	{
		for( int j = 0; j < r_sky->height; j++ )
		{
			p = r_sky->buffer[i * r_sky->width + j];

			if( p == 0 )
			{
				trans[(i * r_sky->height) + j] = transpix;
			}
			else
			{
				rgba = (uint *)r_sky->palette + p;
				trans[(i * r_sky->height) + j] = *rgba;
			}
		}
	}

	r_temp.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA;

	// load it in
	alphaskyTexture = GL_LoadTextureInternal( "alpha_sky", &r_temp, TF_NOMIPMAP | TF_ALLOW_NEAREST );

	// clean up
	FS_FreeImage( r_sky );
	Mem_Free( trans );

	if( !solidskyTexture || !alphaskyTexture )
	{
		ref.dllFuncs.GL_FreeTexture( solidskyTexture );
		ref.dllFuncs.GL_FreeTexture( alphaskyTexture );
		return;
	}

done:
	// notify the renderer
	ref.dllFuncs.R_SetSkyCloudsTextures( solidskyTexture, alphaskyTexture );

	if( solidskyTexture && alphaskyTexture )
		SetBits( world.flags, FWORLD_SKYSPHERE );
#endif // !XASH_DEDICATED
}

static void Mod_LoadTextureData( model_t *mod, dbspmodel_t *bmod, int textureIndex )
{
	uint32_t txFlags = 0;
	char texpath[MAX_VA_STRING];
	char safemtname[16]; // only for external textures
	qboolean load_external = false;

	// don't load texture data on dedicated server, as there is no renderer.
	// but count the wadusage for automatic precache
	texture_t *texture = mod->textures[textureIndex];
	mip_t mipTex;
	const byte *mipRaw = Mod_GetMipTexForTexture( bmod, textureIndex, &mipTex );
	const qboolean usesCustomPalette = Mod_CalcMipTexUsesCustomPalette( mod, bmod, textureIndex );
	const qboolean iswater = Mod_LooksLikeWaterTexture( mipTex.name );
	const uint texture_force_flags = r_allow_wad3_luma.value ? IL_ALLOW_WAD3_LUMA : 0;

	// check for multi-layered sky texture (quake1 specific)
	if( bmod->isworld && Q_strncmp( mipTex.name, "sky", 3 ) == 0 && ( mipTex.width / mipTex.height ) == 2 )
	{
#if XASH_GAMECUBE
		/* Cloud split needs a large transient RGBA buffer and the GX path
		 * never consumed solid/alpha layers. Fall through and load the sky
		 * mip as a single scrolling layer for textured sky fills. */
		Con_Reportf( "Xash3D GameCube: loading sky as single layer for %s\n", mipTex.name );
#else
		Mod_InitSkyClouds( mod, &mipTex, texture, usesCustomPalette ); // load quake sky
		return;
#endif
	}

	// FIXME: for ENGINE_IMPROVED_LINETRACE we need to load textures on server too
	// but there is no facility for this yet
	if( FBitSet( host.features, ENGINE_IMPROVED_LINETRACE ) && mipTex.name[0] == '{' )
		SetBits( txFlags, TF_KEEP_SOURCE ); // Paranoia2 texture alpha-tracing

	// check if this is water to keep the source texture and expand it to RGBA (so ripple effect works)
	if( iswater )
		SetBits( txFlags, TF_KEEP_SOURCE | TF_EXPAND_SOURCE );

	// Texture loading order:
	// 1. HQ from disk
	// 2. From WAD
	// 3. Internal from map

	texture->gl_texturenum = 0;
	Q_strncpy( safemtname, mipTex.name, sizeof( safemtname ));
	if( safemtname[0] == '*' )
		safemtname[0] = '!'; // replace unexpected symbol

	if( Mod_AllowMaterials( ))
	{
#if !XASH_DEDICATED
		if( Mod_SearchForTextureReplacement( texpath, sizeof( texpath ), mod->name, safemtname, "" ))
		{
			texture->gl_texturenum = ref.dllFuncs.GL_LoadTexture( texpath, NULL, 0, txFlags );
			load_external = texture->gl_texturenum != 0;
			Mod_TextureReplacementReport( mod->name, safemtname, "", texture->gl_texturenum, texpath );
		}
#endif // !XASH_DEDICATED
	}

	// Try WAD texture (force while r_wadtextures is 1)
	if( !texture->gl_texturenum && (( r_wadtextures.value && world.wadcount > 0 ) || mipTex.offsets[0] <= 0 ))
	{
		rgbdata_t *pic = NULL;
		int wad_index = Mod_LoadTextureFromWadList( world.wadlist, world.wadcount, mipTex.name, Host_IsDedicated() ? NULL : &pic, texpath, sizeof( texpath ));

		if( wad_index >= 0 )
		{
#if !XASH_DEDICATED
			if( !Host_IsDedicated( ) && pic != NULL )
			{
				Image_SetForceFlags( texture_force_flags );
				texture->gl_texturenum = ref.dllFuncs.GL_LoadTextureFromBuffer( texpath, pic, txFlags, false );
				FS_FreeImage( pic );
			}
#endif // !XASH_DEDICATED

			world.wadlist[wad_index].usage++;
		}
	}

#if !XASH_DEDICATED
	if( Host_IsDedicated( ))
		return;

	// WAD failed, so use internal texture (if present)
	if( mipTex.offsets[0] > 0 && texture->gl_texturenum == 0 )
	{
		string texName;
		const size_t size = Mod_CalculateMipTexSize( &mipTex, usesCustomPalette );

		Q_snprintf( texName, sizeof( texName ), "#%s:%s.mip", loadstat.name, mipTex.name );
		Image_SetForceFlags( texture_force_flags | IL_HOST_ENDIAN );
		texture->gl_texturenum = ref.dllFuncs.GL_LoadTexture( texName, mipRaw, size, txFlags );
	}

	// If texture is completely missed:
	if( texture->gl_texturenum == 0 )
	{
		Con_DPrintf( S_ERROR "Unable to find %s.mip\n", mipTex.name );
		texture->gl_texturenum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );
	}

	texture->fb_texturenum = 0;

	// Check for luma texture
	// a1ba: ignore for water because fb_texturenum will be used to store ripple texture
	if( iswater )
		return;

	if( load_external ) // external textures will not have TF_HAS_LUMA flag because it set only from WAD images loader
	{
		if( Mod_SearchForTextureReplacement( texpath, sizeof( texpath ), mod->name, safemtname, "_luma" ))
		{
			texture->fb_texturenum = ref.dllFuncs.GL_LoadTexture( texpath, NULL, 0, TF_MAKELUMA );
			Mod_TextureReplacementReport( mod->name, safemtname, "_luma", texture->fb_texturenum, texpath );
		}
	}

	if( FBitSet( REF_GET_PARM( PARM_TEX_FLAGS, texture->gl_texturenum ), TF_HAS_LUMA ) && !texture->fb_texturenum )
	{
		string texName;

		Q_snprintf( texName, sizeof( texName ), "#%s:%s_luma.mip", loadstat.name, mipTex.name );

		Image_SetForceFlags( texture_force_flags | IL_HOST_ENDIAN );

		if( mipTex.offsets[0] > 0 )
		{
			const size_t size = Mod_CalculateMipTexSize( &mipTex, usesCustomPalette );
			texture->fb_texturenum = ref.dllFuncs.GL_LoadTexture( texName, mipRaw, size, TF_MAKELUMA );
		}
		else
		{
			rgbdata_t *pic = NULL;

			// NOTE: We can't load the _luma texture from the WAD as normal because it
			// doesn't exist there. The original texture is already loaded, but cannot be modified.
			// Instead, load the original texture again and convert it to luma.
			int wad_index = Mod_LoadTextureFromWadList( world.wadlist, world.wadcount, texture->name, &pic, NULL, 0 );

			if( wad_index >= 0 && pic != NULL )
			{
				// OK, loading it from wad or hi-res(??) version
				texture->fb_texturenum = ref.dllFuncs.GL_LoadTextureFromBuffer( texName, pic, TF_MAKELUMA, false );
				FS_FreeImage( pic );
				world.wadlist[wad_index].usage++;
			}
		}
	}
#endif // !XASH_DEDICATED
}

static void Mod_LoadTexture( model_t *mod, dbspmodel_t *bmod, int textureIndex )
{
	if( textureIndex < 0 || textureIndex >= mod->numtextures )
		return;

	mip_t mipTex;
	const byte *mipRaw = Mod_GetMipTexForTexture( bmod, textureIndex, &mipTex );

	if( !mipRaw )
	{
		// No data for this texture.
		// Create default texture (some mods require this).
		Mod_CreateDefaultTexture( mod, &mod->textures[textureIndex] );
		return;
	}

	if( mipTex.name[0] == '\0' )
	{
		Q_snprintf( mipTex.name, sizeof( mipTex.name ), "miptex_%i", textureIndex );
		memcpy((char *)mipRaw, mipTex.name, sizeof( mipTex.name ));
	}

	texture_t *texture = (texture_t *)Mem_Calloc( mod->mempool, sizeof( *texture ));
	mod->textures[textureIndex] = texture;

	// Ensure texture name is lowercase.
	Q_strnlwr( mipTex.name, texture->name, sizeof( texture->name ));

	texture->width = mipTex.width;
	texture->height = mipTex.height;

	Mod_LoadTextureData( mod, bmod, textureIndex );
}

static void Mod_LoadAllTextures( model_t *mod, dbspmodel_t *bmod )
{
	for( int i = 0; i < mod->numtextures; i++ )
		Mod_LoadTexture( mod, bmod, i );
}

static void Mod_SequenceAnimatedTexture( model_t *mod, int baseTextureIndex )
{
	if( baseTextureIndex < 0 || baseTextureIndex >= mod->numtextures )
		return;

	texture_t *baseTexture = mod->textures[baseTextureIndex];

	texture_t *anims[10];
	texture_t *altanims[10];
	int max = 0;
	int altmax = 0;

	if( !Mod_NameImpliesTextureIsAnimated( baseTexture ))
		return;

	// Already sequenced
	if( baseTexture->anim_next )
		return;

	// find the number of frames in the animation
	memset( anims, 0, sizeof( anims ));
	memset( altanims, 0, sizeof( altanims ));

	if( baseTexture->name[1] >= '0' && baseTexture->name[1] <= '9' )
	{
		// This texture is a standard animation frame.
		int frameIndex = (int)baseTexture->name[1] - (int)'0';

		anims[frameIndex] = baseTexture;
		max = frameIndex + 1;
	}
	else
	{
		// This texture is an alternate animation frame.
		int frameIndex = (int)baseTexture->name[1] - (int)'a';

		altanims[frameIndex] = baseTexture;
		altmax = frameIndex + 1;
	}

	// Now search the rest of the textures to find all other frames.
	for( int candidateIndex = baseTextureIndex + 1; candidateIndex < mod->numtextures; candidateIndex++ )
	{
		texture_t *altTexture = mod->textures[candidateIndex];

		if( !Mod_NameImpliesTextureIsAnimated( altTexture ))
			continue;

		// This texture is animated, but is it part of the same group as
		// the original texture we encountered? Check that the rest of
		// the name matches the original (both will be valid for at least
		// string index 2).
		if( Q_strcmp( altTexture->name + 2, baseTexture->name + 2 ) != 0 )
			continue;

		if( altTexture->name[1] >= '0' && altTexture->name[1] <= '9' )
		{
			// This texture is a standard frame.
			int frameIndex = (int)altTexture->name[1] - (int)'0';
			anims[frameIndex] = altTexture;

			if( frameIndex >= max )
				max = frameIndex + 1;
		}
		else
		{
			// This texture is an alternate frame.
			int frameIndex = (int)altTexture->name[1] - (int)'a';
			altanims[frameIndex] = altTexture;

			if( frameIndex >= altmax )
				altmax = frameIndex + 1;
		}
	}

	// Link all standard animated frames together.
	for( int candidateIndex = 0; candidateIndex < max; candidateIndex++ )
	{
		texture_t *tex = anims[candidateIndex];

		if( !tex )
		{
			Con_Printf( S_ERROR "%s: missing frame %i of animated texture \"%s\"\n",
				__func__,
				candidateIndex,
				baseTexture->name );

			baseTexture->anim_total = 0;
			break;
		}

		tex->anim_total = max * ANIM_CYCLE;
		tex->anim_min = candidateIndex * ANIM_CYCLE;
		tex->anim_max = ( candidateIndex + 1 ) * ANIM_CYCLE;
		tex->anim_next = anims[( candidateIndex + 1 ) % max];

		if( altmax > 0 )
			tex->alternate_anims = altanims[0];
	}

	// Link all alternate animated frames together.
	for( int candidateIndex = 0; candidateIndex < altmax; candidateIndex++ )
	{
		texture_t *tex = altanims[candidateIndex];

		if( !tex )
		{
			Con_Printf( S_ERROR "%s: missing alternate frame %i of animated texture \"%s\"\n",
				__func__,
				candidateIndex,
				baseTexture->name );

			baseTexture->anim_total = 0;
			break;
		}

		tex->anim_total = altmax * ANIM_CYCLE;
		tex->anim_min = candidateIndex * ANIM_CYCLE;
		tex->anim_max = ( candidateIndex + 1 ) * ANIM_CYCLE;
		tex->anim_next = altanims[( candidateIndex + 1 ) % altmax];

		if( max > 0 )
			tex->alternate_anims = anims[0];
	}
}

static void Mod_SequenceAllAnimatedTextures( model_t *mod )
{
	for( int i = 0; i < mod->numtextures; i++ )
		Mod_SequenceAnimatedTexture( mod, i );
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures( model_t *mod, dbspmodel_t *bmod )
{
#if !XASH_DEDICATED
	// release old sky layers first
	if( !Host_IsDedicated() && bmod->isworld )
	{
#if !XASH_GAMECUBE
		ref.dllFuncs.GL_FreeTexture( R_GetBuiltinTexture( "alpha_sky" ));
		ref.dllFuncs.GL_FreeTexture( R_GetBuiltinTexture( "solid_sky" ));
#endif
	}
#endif

	dmiptexlump_t *lump = bmod->textures;

#if XASH_BIG_ENDIAN
	if( lump )
	{
		lump->nummiptex = LittleLong( lump->nummiptex );

		for( int i = 0; i < lump->nummiptex; i++ )
		{
			lump->dataofs[i] = LittleLong( lump->dataofs[i] );

			if( lump->dataofs[i] >= 0 )
			{
				mip_t *mt = (mip_t *)((byte *)lump + lump->dataofs[i]);
				le_struct_swap( mip_swap, mt );
			}
		}
	}
#endif

	if( bmod->texdatasize < 1 || !lump || lump->nummiptex < 1 )
	{
		// no textures
		mod->textures = NULL;
		return;
	}

	mod->textures = (texture_t **)Mem_Calloc( mod->mempool, lump->nummiptex * sizeof( texture_t * ));
	mod->numtextures = lump->nummiptex;

	Mod_LoadAllTextures( mod, bmod );
	Mod_SequenceAllAnimatedTextures( mod );

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && bmod->isworld )
	{
		Mod_GCFreeBspPin( (void **)&bmod->textures );
		bmod->texdatasize = 0;
		Con_Reportf( "Xash3D GameCube: released textures lump from BSP scratch\n" );
	}
#endif
}

#if !XASH_DEDICATED
static void Mod_ParseDetailTextures( model_t *mod )
{
	string	token, texname;
	string	detail_texname;
	string	detail_path;
	string filepath;

	Q_strncpy( filepath, mod->name, sizeof( filepath ));
	COM_StripExtension( filepath );
	Q_strncat( filepath, "_detail.txt", sizeof( filepath ));

	byte *afile = FS_LoadFile( filepath, NULL, false );
	if( !afile )
		return;

	char *pfile = (char *)afile;

	// format: 'texturename' 'detailtexture' 'xScale' 'yScale'
	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		texname[0] = '\0';
		detail_texname[0] = '\0';

		// read texname
		if( token[0] == '{' )
		{
			// NOTE: COM_ParseFile handled some symbols seperately
			// this code will be fix it
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			Q_snprintf( texname, sizeof( texname ), "{%s", token );
		}
		else
			Q_strncpy( texname, token, sizeof( texname ));

		// read detailtexture name
		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		Q_strncpy( detail_texname, token, sizeof( detail_texname ));

		// trying the scales or '{'
		pfile = COM_ParseFile( pfile, token, sizeof( token ));

		// read second part of detailtexture name
		if( token[0] == '{' )
		{
			Q_strncat( detail_texname, token, sizeof( detail_texname ));
			pfile = COM_ParseFile( pfile, token, sizeof( token )); // read scales
			Q_strncat( detail_texname, token, sizeof( detail_texname ));
			pfile = COM_ParseFile( pfile, token, sizeof( token )); // parse scales
		}

		Q_snprintf( detail_path, sizeof( detail_path ), "gfx/%s", detail_texname );

		// read scales
		float xScale = Q_atof( token );

		pfile = COM_ParseFile( pfile, token, sizeof( token ));
		float yScale = Q_atof( token );

		if( xScale <= 0.0f || yScale <= 0.0f )
			continue;

		// search for existing texture and uploading detail texture
		for( int i = 0; i < mod->numtextures; i++ )
		{
			texture_t *tex = mod->textures[i];

			if( Q_stricmp( tex->name, texname ))
				continue;

			tex->dt_texturenum = ref.dllFuncs.GL_LoadTexture( detail_path, NULL, 0, TF_FORCE_COLOR|TF_NOFLIP_TGA );

			if( tex->dt_texturenum )
				ref.dllFuncs.R_SetDetailScaleForTexture( tex->gl_texturenum, xScale, yScale );

			break;
		}
	}

	Mem_Free( afile );
}

void Mod_LoadDetailTextures( model_t *mod )
{
	convar_t *r_detailtextures = Cvar_FindVar( "r_detailtextures" );

	if( !r_detailtextures || !r_detailtextures->value )
		return;

	Mod_ParseDetailTextures( mod );
}
#endif // !XASH_DEDICATED

/*
=================
Mod_LoadTexInfo
=================
*/
static void Mod_LoadTexInfo( model_t *mod, dbspmodel_t *bmod )
{
	mfaceinfo_t	*fout, *faceinfo;
	mtexinfo_t	*out = NULL;

	// trying to load faceinfo
	faceinfo = fout = Mem_Calloc( mod->mempool, bmod->numfaceinfo * sizeof( *fout ));
	dfaceinfo_t	*fin = bmod->faceinfo;

	for( int i = 0; i < bmod->numfaceinfo; i++, fin++, fout++ )
	{
		Q_strncpy( fout->landname, fin->landname, sizeof( fout->landname ));
		fout->texture_step = fin->texture_step;
		fout->max_extent = fin->max_extent;
		fout->groupid = fin->groupid;
	}

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && mod->type == mod_brush && bmod->isworld && bmod->numtexinfo > 0 )
	{
		size_t texinfo_bytes = bmod->numtexinfo * sizeof( *out );

		if( bmod->lightdata && bmod->lightdatasize >= texinfo_bytes )
		{
			out = (mtexinfo_t *)bmod->lightdata;
			memset( out, 0, texinfo_bytes );
			bmod->lightdata = NULL;
			bmod->lightdatasize = 0;
			gc_retain_bsp_source_buffer = true;
			Con_Reportf( "Xash3D GameCube: world texinfo using BSP scratch %s\n", Q_memprint( texinfo_bytes ));
		}
		if( !out )
		{
			out = (mtexinfo_t *)malloc( texinfo_bytes );
			if( out )
			{
				memset( out, 0, texinfo_bytes );
				gc_texinfo_malloc_block = out;
				Con_Reportf( "Xash3D GameCube: world texinfo via malloc %s\n", Q_memprint( texinfo_bytes ));
			}
		}
	}
#endif
	if( !out )
		out = Mem_Calloc( mod->mempool, bmod->numtexinfo * sizeof( *out ));
	mod->texinfo = out;
	mod->numtexinfo = bmod->numtexinfo;
	dtexinfo_t	*in = bmod->texinfo;

	for( int i = 0; i < bmod->numtexinfo; i++, in++, out++ )
	{
		for( int j = 0; j < 2; j++ )
			for( int k = 0; k < 4; k++ )
				out->vecs[j][k] = in->vecs[j][k];

		int miptex = in->miptex;
		if( miptex < 0 || miptex >= mod->numtextures )
			miptex = 0; // this is possible?
		out->texture = mod->textures[miptex];
		out->flags = in->flags;

		// make sure what faceinfo is really exist
		if( faceinfo != NULL && in->faceinfo != -1 && in->faceinfo < bmod->numfaceinfo )
			out->faceinfo = &faceinfo[in->faceinfo];
	}

}

/*
=================
Mod_LoadSurfaces
=================
*/
static void Mod_LoadSurfaces( model_t *mod, dbspmodel_t *bmod )
{
	int          test_lightsize = -1;
	int          next_lightofs = -1;
	int          prev_lightofs = -1;
	int          lightofs;
	msurface_t   *out;
	mextrasurf_t *info;
#if XASH_GAMECUBE
	const size_t surf_bytes = bmod->numsurfaces * ( sizeof( msurface_t ) + sizeof( mextrasurf_t ));
	byte         *surf_block = NULL;
	qboolean     use_bsp_surface_scratch = GC_MapLoadMemoryOpt() && mod->type == mod_brush && bmod->isworld;

	/* Surface output is populated while the original BSP face lump is still
	 * being read. Keep it off the BSP scratch buffer until overlap handling is
	 * proven safe on hardware/Dolphin. */
	if( use_bsp_surface_scratch && gc_retain_bsp_source_buffer && gc_bsp_scratch_base && gc_bsp_scratch_size )
	{
		gc_bsp_busy_range_t busy[4];
		size_t busy_count = 0;

		if( bmod->version == QBSP2_VERSION )
		{
			if( Mod_GCPointerInBuffer( bmod->surfaces32, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)bmod->surfaces32;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + bmod->numsurfaces * sizeof( dface32_t );
				busy_count++;
			}
		}
		else if( Mod_GCPointerInBuffer( bmod->surfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
		{
			const byte *p = (const byte *)bmod->surfaces;
			busy[busy_count].start = p - gc_bsp_scratch_base;
			busy[busy_count].end = busy[busy_count].start + bmod->numsurfaces * sizeof( dface_t );
			busy_count++;
		}

		if( Mod_GCPointerInBuffer( bmod->lightdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->lightdatasize )
		{
			const byte *p = (const byte *)bmod->lightdata;
			busy[busy_count].start = p - gc_bsp_scratch_base;
			busy[busy_count].end = busy[busy_count].start + bmod->lightdatasize;
			busy_count++;
		}

		if( Mod_GCPointerInBuffer( bmod->deluxdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->deluxdatasize )
		{
			const byte *p = (const byte *)bmod->deluxdata;
			busy[busy_count].start = p - gc_bsp_scratch_base;
			busy[busy_count].end = busy[busy_count].start + bmod->deluxdatasize;
			busy_count++;
		}

		if( Mod_GCPointerInBuffer( bmod->shadowdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->shadowdatasize )
		{
			const byte *p = (const byte *)bmod->shadowdata;
			busy[busy_count].start = p - gc_bsp_scratch_base;
			busy[busy_count].end = busy[busy_count].start + bmod->shadowdatasize;
			busy_count++;
		}

		for( size_t i = 0; i < busy_count; i++ )
		{
			for( size_t j = i + 1; j < busy_count; j++ )
			{
				if( busy[j].start < busy[i].start )
				{
					gc_bsp_busy_range_t tmp = busy[i];
					busy[i] = busy[j];
					busy[j] = tmp;
				}
			}
		}

		surf_block = (byte *)Mod_GCAllocBspScratch( gc_bsp_scratch_base, gc_bsp_scratch_size, busy, busy_count, surf_bytes, 32 );
		if( surf_block )
		{
			memset( surf_block, 0, surf_bytes );
			Mod_GCInvalidateScratchOverlap( bmod, gc_bsp_scratch_base, gc_bsp_scratch_size, surf_block, surf_bytes );
			Con_Reportf( "Xash3D GameCube: world surfaces using BSP scratch %s\n", Q_memprint( surf_bytes ));
		}
	}

	if( !surf_block )
	{
		surf_block = (byte *)malloc( surf_bytes );
		if( surf_block )
		{
			gc_surfaces_malloc_block = surf_block;
			memset( surf_block, 0, surf_bytes );
			Con_Reportf( "Xash3D GameCube: world surfaces via malloc %s\n", Q_memprint( surf_bytes ));
		}
		else
		{
			surf_block = Mem_Calloc( mod->mempool, surf_bytes );
		}
	}

	mod->surfaces = out = (msurface_t *)surf_block;
	info = (mextrasurf_t *)( surf_block + bmod->numsurfaces * sizeof( msurface_t ));
#else
	mod->surfaces = out = Mem_Calloc( mod->mempool, bmod->numsurfaces * sizeof( msurface_t ));
	info = Mem_Calloc( mod->mempool, bmod->numsurfaces * sizeof( mextrasurf_t ));
#endif
	mod->numsurfaces = bmod->numsurfaces;

	// predict samplecount based on bspversion
	if( bmod->version == Q1BSP_VERSION || bmod->version == QBSP2_VERSION )
		bmod->lightmap_samples = 1;
	else
		bmod->lightmap_samples = 3;

	for( int i = 0; i < bmod->numsurfaces; i++, out++ )
	{
		mextrasurf_t *surf_extra = info + i;

		// setup crosslinks between two parts of msurface_t
		out->info = surf_extra;
		surf_extra->surf = out;

		if( bmod->version == QBSP2_VERSION )
		{
			dface32_t	*in = &bmod->surfaces32[i];

			if(( in->firstedge + in->numedges ) > mod->numsurfedges )
				continue;	// corrupted level?
			out->firstedge = in->firstedge;
			out->numedges = in->numedges;
			if( in->side ) SetBits( out->flags, SURF_PLANEBACK );
			out->plane = mod->planes + in->planenum;
			out->texinfo = mod->texinfo + in->texinfo;

			for( int j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in->styles[j];
			lightofs = in->lightofs;
		}
		else
		{
			dface_t	*in = &bmod->surfaces[i];

			if(( in->firstedge + in->numedges ) > mod->numsurfedges )
			{
				Con_Reportf( S_ERROR "bad surface %i from %zu\n", i, bmod->numsurfaces );
				continue;
			}

			out->firstedge = in->firstedge;
			out->numedges = in->numedges;
			if( in->side ) SetBits( out->flags, SURF_PLANEBACK );
			out->plane = mod->planes + in->planenum;
			out->texinfo = mod->texinfo + in->texinfo;

			for( int j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in->styles[j];
			lightofs = in->lightofs;
		}

		texture_t	*tex = out->texinfo->texture;

		if( !Q_strncmp( tex->name, "sky", 3 ))
			SetBits( out->flags, SURF_DRAWSKY );

		if( Mod_LooksLikeWaterTexture( tex->name ))
			SetBits( out->flags, SURF_DRAWTURB );

		if( !Q_strncmp( tex->name, "scroll", 6 ))
			SetBits( out->flags, SURF_CONVEYOR );

		if( FBitSet( out->texinfo->flags, TEX_SCROLL ))
			SetBits( out->flags, SURF_CONVEYOR );

		// g-cont. added a combined conveyor-transparent
		if( !Q_strncmp( tex->name, "{scroll", 7 ))
			SetBits( out->flags, SURF_CONVEYOR|SURF_TRANSPARENT );

		if( tex->name[0] == '{' )
			SetBits( out->flags, SURF_TRANSPARENT );

		if( FBitSet( out->texinfo->flags, TEX_SPECIAL ))
			SetBits( out->flags, SURF_DRAWTILED );

		Mod_CalcSurfaceBounds( mod, out, bmod );
		Mod_CalcSurfaceExtents( mod, out, bmod );
#if XASH_GAMECUBE
		if( !Sys_CheckParm( "-gcmap" ) && !Sys_CheckParm( "-gcnobevels" ))
#endif
		Mod_CreateFaceBevels( mod, out, bmod );

		// grab the second sample to detect colored lighting
		if( test_lightsize > 0 && lightofs != -1 )
		{
			if( lightofs > prev_lightofs && lightofs < next_lightofs )
				next_lightofs = lightofs;
		}

		// grab the first sample to determine lightmap size
		if( lightofs != -1 && test_lightsize == -1 )
		{
			int sample_size = Mod_SampleSizeForFace( out );
			int smax = (info->lightextents[0] / sample_size) + 1;
			int tmax = (info->lightextents[1] / sample_size) + 1;
			int lightstyles = 0;

			test_lightsize = smax * tmax;
			// count styles to right compute test_lightsize
			for( int j = 0; j < MAXLIGHTMAPS && out->styles[j] != 255; j++ )
				lightstyles++;

			test_lightsize *= lightstyles;
			prev_lightofs = lightofs;
			next_lightofs = 99999999;
		}

#if !XASH_DEDICATED // TODO: Do we need subdivide on server?
		if( FBitSet( out->flags, SURF_DRAWTURB ) && !Host_IsDedicated() )
			ref.dllFuncs.GL_SubdivideSurface( mod, out ); // cut up polygon for warps
#endif
	}
	// now we have enough data to trying determine samplecount per lightmap pixel
	if( test_lightsize > 0 && prev_lightofs != -1 && next_lightofs != -1 && next_lightofs != 99999999 )
	{
		float samples = (float)(next_lightofs - prev_lightofs) / (float)test_lightsize;

		if( samples != (int)samples )
		{
			test_lightsize = (test_lightsize + 3) & ~3; // align datasize and try again
			samples = (float)(next_lightofs - prev_lightofs) / (float)test_lightsize;
		}

		if( samples == 1 || samples == 3 )
		{
			if( bmod->lightmap_samples != (int)samples )
				Con_DPrintf( S_WARN "detected light sample count: %g\n", samples );
			bmod->lightmap_samples = (int)samples;
			bmod->lightmap_samples = Q_max( bmod->lightmap_samples, 1 ); // avoid division by zero
		}
		else Con_DPrintf( S_WARN "lighting invalid samplecount: %g, defaulting to %i\n", samples, bmod->lightmap_samples );
	}

#if XASH_GAMECUBE
	if( bmod->version == QBSP2_VERSION )
		Mod_GCFreeBspPin( (void **)&bmod->surfaces32 );
	else
		Mod_GCFreeBspPin( (void **)&bmod->surfaces );
#endif
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes( model_t *mod, dbspmodel_t *bmod )
{
	mnode_t	*out = NULL;

	#if XASH_GAMECUBE
		if( GC_MapLoadMemoryOpt() && mod->type == mod_brush && bmod->isworld && bmod->numnodes > 0 )
		{
			size_t node_bytes = bmod->numnodes * sizeof( *out );
			gc_bsp_busy_range_t busy[9];
			size_t busy_count = 0;
			const qboolean use_bsp_node_scratch = true;

			/* Nodes are one of the last world lumps still stressing MEM1 on large
			 * retained-staging maps. Keep them on BSP scratch when we can, while
			 * explicitly avoiding deferred lighting lumps that will be needed later. */
			if( use_bsp_node_scratch && gc_retain_bsp_source_buffer
				&& gc_bsp_scratch_base && gc_bsp_scratch_size )
			{
				if( mod->surfaces && Mod_GCPointerInBuffer( mod->surfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
				{
					const byte *p = (const byte *)mod->surfaces;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start
						+ bmod->numsurfaces * ( sizeof( msurface_t ) + sizeof( mextrasurf_t ));
					busy_count++;
				}

				if( mod->texinfo && Mod_GCPointerInBuffer( mod->texinfo, gc_bsp_scratch_base, gc_bsp_scratch_size ))
				{
					const byte *p = (const byte *)mod->texinfo;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + mod->numtexinfo * sizeof( mtexinfo_t );
					busy_count++;
				}

				if( mod->marksurfaces && Mod_GCPointerInBuffer( mod->marksurfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
				{
					const byte *p = (const byte *)mod->marksurfaces;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + mod->nummarksurfaces * sizeof( *mod->marksurfaces );
					busy_count++;
				}

				if( mod->leafs && Mod_GCPointerInBuffer( mod->leafs, gc_bsp_scratch_base, gc_bsp_scratch_size ))
				{
					const byte *p = (const byte *)mod->leafs;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + mod->numleafs * sizeof( *mod->leafs );
					busy_count++;
				}

				if( Mod_GCPointerInBuffer( bmod->lightdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->lightdatasize )
				{
					const byte *p = (const byte *)bmod->lightdata;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + bmod->lightdatasize;
					busy_count++;
				}

				if( Mod_GCPointerInBuffer( bmod->deluxdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->deluxdatasize )
				{
					const byte *p = (const byte *)bmod->deluxdata;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + bmod->deluxdatasize;
					busy_count++;
				}

				if( Mod_GCPointerInBuffer( bmod->shadowdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->shadowdatasize )
				{
					const byte *p = (const byte *)bmod->shadowdata;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + bmod->shadowdatasize;
					busy_count++;
				}

				if( Mod_GCPointerInBuffer( bmod->rgblightdata, gc_bsp_scratch_base, gc_bsp_scratch_size ) && bmod->rgblightdatasize )
				{
					const byte *p = (const byte *)bmod->rgblightdata;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + bmod->rgblightdatasize;
					busy_count++;
				}

				if( bmod->version == QBSP2_VERSION )
				{
					if( bmod->nodes32 && Mod_GCPointerInBuffer( bmod->nodes32, gc_bsp_scratch_base, gc_bsp_scratch_size ))
					{
						const byte *p = (const byte *)bmod->nodes32;
						busy[busy_count].start = p - gc_bsp_scratch_base;
						busy[busy_count].end = busy[busy_count].start + bmod->numnodes * sizeof( dnode32_t );
						busy_count++;
					}
				}
				else if( bmod->nodes && Mod_GCPointerInBuffer( bmod->nodes, gc_bsp_scratch_base, gc_bsp_scratch_size ))
				{
					const byte *p = (const byte *)bmod->nodes;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + bmod->numnodes * sizeof( dnode_t );
					busy_count++;
				}

				for( size_t i = 0; i < busy_count; i++ )
				{
					for( size_t j = i + 1; j < busy_count; j++ )
					{
						if( busy[j].start < busy[i].start )
						{
							gc_bsp_busy_range_t tmp = busy[i];
							busy[i] = busy[j];
							busy[j] = tmp;
						}
					}
				}

				out = (mnode_t *)Mod_GCAllocBspScratch( gc_bsp_scratch_base, gc_bsp_scratch_size, busy, busy_count, node_bytes, 32 );
				if( out )
				{
					memset( out, 0, node_bytes );
					Mod_GCInvalidateScratchOverlap( bmod, gc_bsp_scratch_base, gc_bsp_scratch_size, (byte *)out, node_bytes );
					Con_Reportf( "Xash3D GameCube: world nodes using BSP scratch %s\n", Q_memprint( node_bytes ));
				}
			}

			if( !out )
			{
				out = (mnode_t *)malloc( node_bytes );
				if( out )
				{
					memset( out, 0, node_bytes );
					gc_nodes_malloc_block = out;
					Con_Reportf( "Xash3D GameCube: world nodes via malloc %s\n", Q_memprint( node_bytes ));
				}
			}
		}
	#endif
	if( !out )
		out = (mnode_t *)Mem_Calloc( mod->mempool, bmod->numnodes * sizeof( *out ));
	mod->nodes = out;
	mod->numnodes = bmod->numnodes;

	for( int i = 0; i < mod->numnodes; i++, out++ )
	{
		if( bmod->version == QBSP2_VERSION )
		{
			dnode32_t	*in = &bmod->nodes32[i];

			for( int j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

#if !XASH_64BIT
			if( in->firstface >= BIT( 24 ))
			{
				Host_Error( "%s: face index limit exceeded on node %i\n", __func__, i );
				return;
			}

			if( in->numfaces >= BIT( 24 ))
			{
				Host_Error( "%s: face count limit exceeded on node %i\n", __func__, i );
				return;
			}
#endif

			out->plane = mod->planes + in->planenum;
			out->firstsurface_0 = in->firstface & 0xFFFF;
			out->numsurfaces_0  = in->numfaces  & 0xFFFF;

			out->firstsurface_1 = in->firstface >> 16;
			out->numsurfaces_1  = in->numfaces >> 16;

			for( int j = 0; j < 2; j++ )
			{
				int p = in->children[j];
#if XASH_64BIT
				if( p >= 0 ) out->children_[j] = mod->nodes + p;
				else out->children_[j] = (mnode_t *)(mod->leafs + ( -1 - p ));
#else
				if( j == 0 )
				{
					if( p >= 0 )
					{
						out->child_0_leaf = 0;
						out->child_0_off  = p;
					}
					else
					{
						out->child_0_leaf = 1;
						out->child_0_off = -1 - p;
					}
				}
				else
				{
					if( p >= 0 )
					{
						out->child_1_leaf = 0;
						out->child_1_off  = p;
					}
					else
					{
						out->child_1_leaf = 1;
						out->child_1_off = -1 - p;
					}
				}
#endif
			}
		}
		else
		{
			dnode_t	*in = &bmod->nodes[i];

			for( int j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

			out->plane = mod->planes + in->planenum;
			out->firstsurface_0 = in->firstface;
			out->numsurfaces_0 = in->numfaces;

			for( int j = 0; j < 2; j++ )
			{
				int p = in->children[j];
				if( p >= 0 ) out->children_[j] = mod->nodes + p;
				else out->children_[j] = (mnode_t *)(mod->leafs + ( -1 - p ));
			}
		}
	}

	// sets nodes and leafs
#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && bmod->isworld )
		Con_Reportf( "Xash3D GameCube: world node parent walk begin\n" );
#endif
	Mod_SetParent( mod, mod->nodes, NULL );
#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && bmod->isworld )
		Con_Reportf( "Xash3D GameCube: world node parent walk ready\n" );
#endif

#if XASH_GAMECUBE
	if( bmod->version == QBSP2_VERSION )
		Mod_GCFreeBspPin( (void **)&bmod->nodes32 );
	else
		Mod_GCFreeBspPin( (void **)&bmod->nodes );
#endif
}

#if XASH_GAMECUBE
static byte *Mod_GCPinBspLump( poolhandle_t pool, const byte *src, size_t size )
{
	byte *copy;

	if( !src || size == 0 )
		return NULL;

	(void)pool;
	copy = (byte *)malloc( size );
	if( !copy )
		return NULL;

	memcpy( copy, src, size );
	return copy;
}

static qboolean Mod_GCPointerInBuffer( const void *ptr, const byte *base, size_t size )
{
	const byte *p = (const byte *)ptr;

	if( !ptr || !base || size == 0 )
		return false;

	return p >= base && p < base + size;
}

static size_t Mod_GCAlignDown( size_t value, size_t align )
{
	if( align <= 1 )
		return value;
	return value & ~( align - 1 );
}

static void *Mod_GCAllocBspScratch( byte *base, size_t size, const gc_bsp_busy_range_t *busy, size_t busy_count, size_t alloc_size, size_t align )
{
	size_t best_start = 0;
	size_t best_end = 0;
	size_t cursor;

	if( !base || size == 0 || alloc_size == 0 )
		return NULL;

	alloc_size = ALIGN( alloc_size, align );
	cursor = size;

	for( size_t i = busy_count; i > 0; i-- )
	{
		const gc_bsp_busy_range_t *range = &busy[i - 1];
		size_t gap_start = range->end;
		size_t gap_end = cursor;

		cursor = range->start;

		if( gap_end <= gap_start )
			continue;

		size_t gap_start_aligned = ALIGN( gap_start, align );
		size_t gap_end_aligned = Mod_GCAlignDown( gap_end, align );

		if( gap_end_aligned <= gap_start_aligned )
			continue;

		if( gap_end_aligned - gap_start_aligned < alloc_size )
			continue;

		best_end = gap_end_aligned;
		best_start = best_end - alloc_size;
		break;
	}

	if( best_end <= best_start )
	{
		size_t gap_start = 0;
		size_t gap_end = cursor;
		size_t gap_start_aligned = ALIGN( gap_start, align );
		size_t gap_end_aligned = Mod_GCAlignDown( gap_end, align );

		if( gap_end_aligned > gap_start_aligned && gap_end_aligned - gap_start_aligned >= alloc_size )
		{
			best_end = gap_end_aligned;
			best_start = best_end - alloc_size;
		}
	}

	if( best_end <= best_start )
		return NULL;

	return base + best_start;
}

static void Mod_GCInvalidateScratchOverlap( dbspmodel_t *bmod, byte *mod_base, size_t bufferlen, byte *scratch, size_t scratch_size )
{
	const byte *scratch_end = scratch + scratch_size;

	if( !bmod || !mod_base || bufferlen == 0 || !scratch || scratch_size == 0 )
		return;

	if( bmod->version == QBSP2_VERSION )
	{
		if( Mod_GCPointerInBuffer( bmod->markfaces32, mod_base, bufferlen )
			&& (const byte *)bmod->markfaces32 < scratch_end
			&& (const byte *)bmod->markfaces32 + bmod->nummarkfaces * sizeof( dmarkface32_t ) > scratch )
			bmod->markfaces32 = NULL;

		if( Mod_GCPointerInBuffer( bmod->leafs32, mod_base, bufferlen )
			&& (const byte *)bmod->leafs32 < scratch_end
			&& (const byte *)bmod->leafs32 + bmod->numleafs * sizeof( dleaf32_t ) > scratch )
			bmod->leafs32 = NULL;

		if( Mod_GCPointerInBuffer( bmod->nodes32, mod_base, bufferlen )
			&& (const byte *)bmod->nodes32 < scratch_end
			&& (const byte *)bmod->nodes32 + bmod->numnodes * sizeof( dnode32_t ) > scratch )
			bmod->nodes32 = NULL;

		if( Mod_GCPointerInBuffer( bmod->surfaces32, mod_base, bufferlen )
			&& (const byte *)bmod->surfaces32 < scratch_end
			&& (const byte *)bmod->surfaces32 + bmod->numsurfaces * sizeof( dface32_t ) > scratch )
			bmod->surfaces32 = NULL;
	}
	else
	{
		if( Mod_GCPointerInBuffer( bmod->markfaces, mod_base, bufferlen )
			&& (const byte *)bmod->markfaces < scratch_end
			&& (const byte *)bmod->markfaces + bmod->nummarkfaces * sizeof( dmarkface_t ) > scratch )
			bmod->markfaces = NULL;

		if( Mod_GCPointerInBuffer( bmod->leafs, mod_base, bufferlen )
			&& (const byte *)bmod->leafs < scratch_end
			&& (const byte *)bmod->leafs + bmod->numleafs * sizeof( dleaf_t ) > scratch )
			bmod->leafs = NULL;

		if( Mod_GCPointerInBuffer( bmod->nodes, mod_base, bufferlen )
			&& (const byte *)bmod->nodes < scratch_end
			&& (const byte *)bmod->nodes + bmod->numnodes * sizeof( dnode_t ) > scratch )
			bmod->nodes = NULL;

		if( Mod_GCPointerInBuffer( bmod->surfaces, mod_base, bufferlen )
			&& (const byte *)bmod->surfaces < scratch_end
			&& (const byte *)bmod->surfaces + bmod->numsurfaces * sizeof( dface_t ) > scratch )
			bmod->surfaces = NULL;
	}

	if( Mod_GCPointerInBuffer( bmod->clipnodes, mod_base, bufferlen )
		&& (const byte *)bmod->clipnodes < scratch_end
		&& (const byte *)bmod->clipnodes + bmod->numclipnodes * sizeof( dclipnode_t ) > scratch )
		bmod->clipnodes = NULL;

	if( Mod_GCPointerInBuffer( bmod->clipnodes32, mod_base, bufferlen )
		&& (const byte *)bmod->clipnodes32 < scratch_end
		&& (const byte *)bmod->clipnodes32 + bmod->numclipnodes * sizeof( dclipnode32_t ) > scratch )
		bmod->clipnodes32 = NULL;
}

static void Mod_GCStashDeferredLump( void **slot, const byte *src, size_t size )
{
	if( !slot || !src || size == 0 )
		return;

	*slot = Mod_GCPinBspLump( 0, src, size );
}

static void Mod_GCDropDeferredBspLumpsBeforeSurfaces( void )
{
	Mod_GCFreeBspPin( &gc_bsp_deferred.leafs );
	Mod_GCFreeBspPin( &gc_bsp_deferred.nodes );
	Mod_GCFreeBspPin( &gc_bsp_deferred.clipnodes );
	GC_MemSample( "bsp-deferred-drop" );
}

void Mod_GameCubeFreeMallocSurfaces( model_t *mod )
{
	if( gc_marksurfaces_malloc_block )
	{
		if( mod && mod->marksurfaces == (msurface_t **)gc_marksurfaces_malloc_block )
			mod->marksurfaces = NULL;

		free( gc_marksurfaces_malloc_block );
		gc_marksurfaces_malloc_block = NULL;
	}

	if( gc_leafs_malloc_block )
	{
		if( mod && mod->leafs == (mleaf_t *)gc_leafs_malloc_block )
			mod->leafs = NULL;

		free( gc_leafs_malloc_block );
		gc_leafs_malloc_block = NULL;
	}

	if( gc_surfaces_malloc_block )
	{
		if( mod && mod->surfaces == (msurface_t *)gc_surfaces_malloc_block )
			mod->surfaces = NULL;

		free( gc_surfaces_malloc_block );
		gc_surfaces_malloc_block = NULL;
	}

	if( !gc_texinfo_malloc_block )
		goto free_nodes;

	if( mod && mod->texinfo == (mtexinfo_t *)gc_texinfo_malloc_block )
		mod->texinfo = NULL;

	free( gc_texinfo_malloc_block );
	gc_texinfo_malloc_block = NULL;

free_nodes:
	if( !gc_nodes_malloc_block )
		return;

	if( mod && mod->nodes == (mnode_t *)gc_nodes_malloc_block )
		mod->nodes = NULL;

	free( gc_nodes_malloc_block );
	gc_nodes_malloc_block = NULL;
}

static void Mod_GCRestoreDeferredMarkfaces( dbspmodel_t *bmod )
{
	if( !gc_bsp_deferred.markfaces )
		return;

	if( bmod->version == QBSP2_VERSION )
		bmod->markfaces32 = (dmarkface32_t *)gc_bsp_deferred.markfaces;
	else
		bmod->markfaces = (dmarkface_t *)gc_bsp_deferred.markfaces;
	gc_bsp_deferred.markfaces = NULL;
}

static void Mod_GCRestoreDeferredLeafs( dbspmodel_t *bmod )
{
	if( !gc_bsp_deferred.leafs )
		return;

	if( bmod->version == QBSP2_VERSION )
		bmod->leafs32 = (dleaf32_t *)gc_bsp_deferred.leafs;
	else
		bmod->leafs = (dleaf_t *)gc_bsp_deferred.leafs;
	gc_bsp_deferred.leafs = NULL;
}

static void Mod_GCRestoreDeferredNodes( dbspmodel_t *bmod )
{
	if( !gc_bsp_deferred.nodes )
		return;

	if( bmod->version == QBSP2_VERSION )
		bmod->nodes32 = (dnode32_t *)gc_bsp_deferred.nodes;
	else
		bmod->nodes = (dnode_t *)gc_bsp_deferred.nodes;
	gc_bsp_deferred.nodes = NULL;
}

static void Mod_GCRestoreDeferredClipnodes( dbspmodel_t *bmod )
{
	if( !gc_bsp_deferred.clipnodes )
		return;

	if( bmod->version == QBSP2_VERSION
		|| ( bmod->isbsp30ext && bmod->numclipnodes >= MAX_MAP_CLIPNODES_HLBSP ))
		bmod->clipnodes32 = (dclipnode32_t *)gc_bsp_deferred.clipnodes;
	else
		bmod->clipnodes = (dclipnode_t *)gc_bsp_deferred.clipnodes;
	gc_bsp_deferred.clipnodes = NULL;
}

static void Mod_GCRestoreDeferredLightdata( dbspmodel_t *bmod )
{
	if( !gc_bsp_deferred.lightdata || !gc_bsp_deferred.lightdatasize )
		return;

	bmod->lightdata = gc_bsp_deferred.lightdata;
	bmod->lightdatasize = gc_bsp_deferred.lightdatasize;
	gc_bsp_deferred.lightdata = NULL;
	gc_bsp_deferred.lightdatasize = 0;
}

static const mlumpinfo_t *Mod_GCFindStdLumpInfo( int lumpnum )
{
	for( int i = 0; i < ARRAYSIZE( srclumps ); i++ )
	{
		if( srclumps[i].lumpnumber == lumpnum )
			return &srclumps[i];
	}
	return NULL;
}

static qboolean Mod_GCReloadStdBspLump( model_t *mod, dbspmodel_t *bmod, int lumpnum )
{
	const mlumpinfo_t *info = Mod_GCFindStdLumpInfo( lumpnum );
	file_t *f;
	dheader_t header;
	dlump_t lump;
	byte *lumpbuf;
	size_t real_entrysize;
	size_t numelems;
	char bsppath[MAX_QPATH];

	if( !info )
		return false;

	if( Q_stristr( mod->name, ".bsp" ))
		Q_strncpy( bsppath, mod->name, sizeof( bsppath ));
	else
		Q_snprintf( bsppath, sizeof( bsppath ), "%s.bsp", mod->name );
	f = FS_Open( bsppath, "rb", false );
	if( !f )
	{
		Con_Reportf( S_ERROR "Xash3D GameCube: failed to reopen %s for lump %i\n", bsppath, lumpnum );
		return false;
	}

	if( FS_Read( f, &header, sizeof( header )) != sizeof( header ))
	{
		FS_Close( f );
		return false;
	}

	le_struct_swap( dheader_swap, &header );

	if( header.version != bmod->version )
	{
		FS_Close( f );
		return false;
	}

	lump = header.lumps[lumpnum];
	if( lump.filelen <= 0 )
	{
		FS_Close( f );
		return false;
	}

	real_entrysize = info->entrysize;
	if( bmod->version == QBSP2_VERSION && info->entrysize32 > 0 )
		real_entrysize = info->entrysize32;
	else if( bmod->version == HLBSP_VERSION && bmod->isbsp30ext
		&& info->lumpnumber == LUMP_CLIPNODES
		&& (( lump.filelen % info->entrysize ) || ( lump.filelen / info->entrysize32 ) >= MAX_MAP_CLIPNODES_HLBSP ))
		real_entrysize = info->entrysize32;

	if( lump.filelen % real_entrysize )
	{
		FS_Close( f );
		return false;
	}

	numelems = lump.filelen / real_entrysize;
	lumpbuf = (byte *)malloc( lump.filelen );
	if( !lumpbuf )
	{
		FS_Close( f );
		return false;
	}

	FS_Seek( f, lump.fileofs, SEEK_SET );
	if( FS_Read( f, lumpbuf, lump.filelen ) != lump.filelen )
	{
		free( lumpbuf );
		FS_Close( f );
		return false;
	}
	FS_Close( f );

#if XASH_BIG_ENDIAN
	{
		const swap_struct_def_t *swap = real_entrysize == info->entrysize32 ? info->swap32 : info->swap;
		size_t swaplen = real_entrysize == info->entrysize32 ? info->swaplen32 : info->swaplen;

		if( swap )
		{
			for( size_t j = 0; j < numelems; j++ )
				swap_struct_( swap, swaplen, lumpbuf + j * real_entrysize );
		}
		else if( real_entrysize > 1 )
		{
			for( size_t j = 0; j < numelems; j++ )
				swap_field_( lumpbuf + j * real_entrysize, real_entrysize );
		}
	}
#endif

	*(byte **)((byte *)bmod + info->dataofs) = lumpbuf;
	*(size_t *)((byte *)bmod + info->countofs) = numelems;
	Con_Reportf( "Xash3D GameCube: reloaded BSP lump %s (%s) from disc\n",
		info->loadname, Q_memprint( lump.filelen ));
	return true;
}

static void Mod_GCEnsureBspLump( model_t *mod, dbspmodel_t *bmod, int lumpnum )
{
	const mlumpinfo_t *info = Mod_GCFindStdLumpInfo( lumpnum );
	void *current;

	if( !info )
		return;

	current = *(void **)((byte *)bmod + info->dataofs);
	if( current )
		return;

	Mod_GCReloadStdBspLump( mod, bmod, lumpnum );
}

static void Mod_GCReleaseBspSourceBuffer( model_t *mod, dbspmodel_t *bmod, byte *mod_base, size_t bufferlen );

static void Mod_GCReleaseGcmapPreSurfaceStaging( model_t *mod, dbspmodel_t *bmod, byte *mod_base, size_t bufferlen )
{
	byte *pinned;

	if( !GC_MapLoadMemoryOpt() || !bmod->isworld || !mod_base || bufferlen == 0 )
		return;

	if( !gc_retain_bsp_source_buffer
		&& !( mod->texinfo && Mod_GCPointerInBuffer( mod->texinfo, mod_base, bufferlen )))
		return;

	if( mod->texinfo && Mod_GCPointerInBuffer( mod->texinfo, mod_base, bufferlen ))
	{
		size_t texinfo_bytes = mod->numtexinfo * sizeof( mtexinfo_t );
		qboolean texinfo_heap_owned = false;

		pinned = malloc( texinfo_bytes );
		if( pinned )
		{
			memcpy( pinned, mod->texinfo, texinfo_bytes );
			texinfo_heap_owned = true;
		}
			else
			{
				pinned = Mem_TryMalloc( mod->mempool, texinfo_bytes );
				if( pinned )
					memcpy( pinned, mod->texinfo, texinfo_bytes );
			}

		if( !pinned )
		{
			Con_Reportf( S_WARN "Xash3D GameCube: retaining gcmap BSP staging; texinfo pin skipped (%s)\n",
				Q_memprint( texinfo_bytes ));
			gc_retain_bsp_source_buffer = true;
			return;
		}
		mod->texinfo = (mtexinfo_t *)pinned;
		if( texinfo_heap_owned )
			gc_texinfo_malloc_block = pinned;
	}

	if( bmod->version == QBSP2_VERSION )
	{
		if( Mod_GCPointerInBuffer( bmod->surfaces32, mod_base, bufferlen ))
			bmod->surfaces32 = NULL;
	}
	else if( Mod_GCPointerInBuffer( bmod->surfaces, mod_base, bufferlen ))
	{
		bmod->surfaces = NULL;
	}

	gc_retain_bsp_source_buffer = false;
	Con_Reportf( "Xash3D GameCube: releasing gcmap BSP staging %s before surfaces\n",
		Q_memprint( bufferlen ));
	Mod_GCReleaseBspSourceBuffer( mod, bmod, mod_base, bufferlen );
	gc_bsp_scratch_base = NULL;
	gc_bsp_scratch_size = 0;
}

static void Mod_GCReleaseBspSourceBuffer( model_t *mod, dbspmodel_t *bmod, byte *mod_base, size_t bufferlen )
{
	size_t leaf_esize, node_esize, clip_esize;

	if( !mod_base || bufferlen == 0 )
		return;
	if( gc_retain_bsp_source_buffer )
	{
		Con_Reportf( "Xash3D GameCube: retaining BSP source buffer for gcmap scratch\n" );
		return;
	}

	if( !Mod_GCPointerInBuffer( bmod->leafs, mod_base, bufferlen )
		&& !Mod_GCPointerInBuffer( bmod->markfaces, mod_base, bufferlen )
		&& !Mod_GCPointerInBuffer( bmod->lightdata, mod_base, bufferlen ))
		return;

	memset( &gc_bsp_deferred, 0, sizeof( gc_bsp_deferred ));

	leaf_esize = ( bmod->version == QBSP2_VERSION ) ? sizeof( dleaf32_t ) : sizeof( dleaf_t );
	node_esize = ( bmod->version == QBSP2_VERSION ) ? sizeof( dnode32_t ) : sizeof( dnode_t );
	if( bmod->version == QBSP2_VERSION
		|| ( bmod->isbsp30ext && bmod->numclipnodes >= MAX_MAP_CLIPNODES_HLBSP ))
		clip_esize = sizeof( dclipnode32_t );
	else
		clip_esize = sizeof( dclipnode_t );

	if( Mod_GCPointerInBuffer( bmod->markfaces, mod_base, bufferlen ))
	{
		if( bmod->version == QBSP2_VERSION )
			bmod->markfaces32 = NULL;
		else
			bmod->markfaces = NULL;
	}

	if( Mod_GCPointerInBuffer( bmod->leafs, mod_base, bufferlen ))
	{
		if( bmod->version == QBSP2_VERSION )
			bmod->leafs32 = NULL;
		else
			bmod->leafs = NULL;
	}

	if( Mod_GCPointerInBuffer( bmod->nodes, mod_base, bufferlen ))
	{
		if( bmod->version == QBSP2_VERSION )
			bmod->nodes32 = NULL;
		else
			bmod->nodes = NULL;
	}

	if( Mod_GCPointerInBuffer( bmod->clipnodes, mod_base, bufferlen ))
	{
		if( clip_esize == sizeof( dclipnode32_t ))
			bmod->clipnodes32 = NULL;
		else
			bmod->clipnodes = NULL;
	}

	if( bmod->lightdatasize && Mod_GCPointerInBuffer( bmod->lightdata, mod_base, bufferlen )
		&& !Sys_CheckParm( "-gcnolightmaps" ) && bmod->lightdatasize <= ( 256 * 1024 ))
	{
		Mod_GCStashDeferredLump( (void **)&gc_bsp_deferred.lightdata, bmod->lightdata, bmod->lightdatasize );
		gc_bsp_deferred.lightdatasize = bmod->lightdatasize;
		bmod->lightdata = NULL;
		bmod->lightdatasize = 0;
	}
	else if( Mod_GCPointerInBuffer( bmod->lightdata, mod_base, bufferlen ))
	{
		bmod->lightdata = NULL;
		bmod->lightdatasize = 0;
	}

	if( bmod->deluxdatasize && Mod_GCPointerInBuffer( bmod->deluxdata, mod_base, bufferlen ))
		bmod->deluxdata = NULL, bmod->deluxdatasize = 0;

	if( bmod->shadowdatasize && Mod_GCPointerInBuffer( bmod->shadowdata, mod_base, bufferlen ))
		bmod->shadowdata = NULL, bmod->shadowdatasize = 0;

	if( bmod->rgblightdatasize && Mod_GCPointerInBuffer( bmod->rgblightdata, mod_base, bufferlen ))
		bmod->rgblightdata = NULL, bmod->rgblightdatasize = 0;

	Con_Reportf( "Xash3D GameCube: released BSP source buffer %s before surface load\n",
		Q_memprint( bufferlen ));
	GC_MemSample( "bsp-buffer-release" );
	Mod_ReleaseBrushSourceBuffer( mod_base );
}
#endif

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs( model_t *mod, dbspmodel_t *bmod )
{
	mleaf_t	*out = NULL;
	int	visclusters = 0;

#if XASH_GAMECUBE
	{
		const size_t leaf_bytes = bmod->numleafs * sizeof( *out );
			gc_bsp_busy_range_t busy[5];
			size_t busy_count = 0;
			const qboolean use_bsp_leaf_scratch = true;

			/* Retained-staging maps need scratch-backed leaf storage to stay
			 * within MEM1; the parent walk now guards against overlap fallout. */
			if( use_bsp_leaf_scratch && GC_MapLoadMemoryOpt()
				&& gc_retain_bsp_source_buffer && gc_bsp_scratch_base && gc_bsp_scratch_size )
			{
			if( mod->surfaces && Mod_GCPointerInBuffer( mod->surfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->surfaces;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start
					+ bmod->numsurfaces * ( sizeof( msurface_t ) + sizeof( mextrasurf_t ));
				busy_count++;
			}

			if( mod->texinfo && Mod_GCPointerInBuffer( mod->texinfo, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->texinfo;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->numtexinfo * sizeof( mtexinfo_t );
				busy_count++;
			}

			if( mod->marksurfaces && Mod_GCPointerInBuffer( mod->marksurfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)mod->marksurfaces;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + mod->nummarksurfaces * sizeof( *mod->marksurfaces );
				busy_count++;
			}

			if( bmod->version == QBSP2_VERSION )
			{
				if( bmod->leafs32 && Mod_GCPointerInBuffer( bmod->leafs32, gc_bsp_scratch_base, gc_bsp_scratch_size ))
				{
					const byte *p = (const byte *)bmod->leafs32;
					busy[busy_count].start = p - gc_bsp_scratch_base;
					busy[busy_count].end = busy[busy_count].start + bmod->numleafs * sizeof( dleaf32_t );
					busy_count++;
				}
			}
			else if( bmod->leafs && Mod_GCPointerInBuffer( bmod->leafs, gc_bsp_scratch_base, gc_bsp_scratch_size ))
			{
				const byte *p = (const byte *)bmod->leafs;
				busy[busy_count].start = p - gc_bsp_scratch_base;
				busy[busy_count].end = busy[busy_count].start + bmod->numleafs * sizeof( dleaf_t );
				busy_count++;
			}

			for( size_t i = 0; i < busy_count; i++ )
			{
				for( size_t j = i + 1; j < busy_count; j++ )
				{
					if( busy[j].start < busy[i].start )
					{
						gc_bsp_busy_range_t tmp = busy[i];
						busy[i] = busy[j];
						busy[j] = tmp;
					}
				}
			}

			out = (mleaf_t *)Mod_GCAllocBspScratch( gc_bsp_scratch_base, gc_bsp_scratch_size, busy, busy_count, leaf_bytes, 32 );
			if( out )
			{
				memset( out, 0, leaf_bytes );
				Mod_GCInvalidateScratchOverlap( bmod, gc_bsp_scratch_base, gc_bsp_scratch_size, (byte *)out, leaf_bytes );
				Con_Reportf( "Xash3D GameCube: world leafs using BSP scratch %s\n", Q_memprint( leaf_bytes ));
			}
		}

		if( !out )
		{
			out = (mleaf_t *)malloc( leaf_bytes );
			if( out )
			{
				gc_leafs_malloc_block = out;
				memset( out, 0, leaf_bytes );
				Con_Reportf( "Xash3D GameCube: world leafs via malloc %s\n", Q_memprint( leaf_bytes ));
			}
			else
			{
				out = (mleaf_t *)Mem_Calloc( mod->mempool, leaf_bytes );
			}
		}

		mod->leafs = out;
	}
#else
	mod->leafs = out = (mleaf_t *)Mem_Calloc( mod->mempool, bmod->numleafs * sizeof( *out ));
#endif
	mod->numleafs = bmod->numleafs;

	if( bmod->isworld )
	{
		visclusters = mod->submodels[0].visleafs;
		world.visbytes = (visclusters + 7) >> 3;
		world.fatbytes = (visclusters + 31) >> 3;
		refState.visbytes = world.visbytes;
	}

	for( int i = 0; i < bmod->numleafs; i++, out++ )
	{
		int p;

		if( bmod->version == QBSP2_VERSION )
		{
			dleaf32_t	*in = &bmod->leafs32[i];

			for( int j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

			out->contents = in->contents;
			p = in->visofs;

			for( int j = 0; j < 4; j++ )
				out->ambient_sound_level[j] = in->ambient_level[j];

			out->firstmarksurface = mod->marksurfaces + in->firstmarksurface;
			out->nummarksurfaces = in->nummarksurfaces;
		}
		else
		{
			dleaf_t	*in = &bmod->leafs[i];

			for( int j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

			out->contents = in->contents;
			p = in->visofs;

			for( int j = 0; j < 4; j++ )
				out->ambient_sound_level[j] = in->ambient_level[j];

			out->firstmarksurface = mod->marksurfaces + in->firstmarksurface;
			out->nummarksurfaces = in->nummarksurfaces;
		}

		if( bmod->isworld )
		{
			out->cluster = ( i - 1 ); // solid leaf 0 has no visdata

			if( out->cluster >= visclusters )
				out->cluster = -1;

			// ignore visofs errors on leaf 0 (solid)
			if( p >= 0 && out->cluster >= 0 && mod->visdata )
			{
				if( p < bmod->visdatasize )
					out->compressed_vis = mod->visdata + p;
				else Con_Reportf( S_WARN "Mod_LoadLeafs: invalid visofs for leaf #%i\n", i );
			}
		}
		else out->cluster = -1; // no visclusters on bmodels

		if( p == -1 ) out->compressed_vis = NULL;
		else out->compressed_vis = mod->visdata + p;

		// gl underwater warp
		if( out->contents != CONTENTS_EMPTY )
		{
			for( int j = 0; j < out->nummarksurfaces; j++ )
			{
				// mark underwater surfaces
				SetBits( out->firstmarksurface[j]->flags, SURF_UNDERWATER );
			}
		}
	}

	if( bmod->isworld && mod->leafs[0].contents != CONTENTS_SOLID )
		Host_Error( "%s: Map %s has leaf 0 is not CONTENTS_SOLID\n", __func__, mod->name );

	// do some final things for world
	if( bmod->isworld && Mod_CheckWaterAlphaSupport( mod, bmod ))
		SetBits( world.flags, FWORLD_WATERALPHA );

#if XASH_GAMECUBE
	if( bmod->version == QBSP2_VERSION )
		Mod_GCFreeBspPin( (void **)&bmod->leafs32 );
	else
		Mod_GCFreeBspPin( (void **)&bmod->leafs );
#endif
}

/*
===========
Mod_CalcPHS

To be called while loading world for multiplayer game server
===========
*/
static void Mod_CalcPHS( model_t *mod )
{
	const qboolean vis_stats = host_developer.value >= DEV_EXTENDED;
	const size_t rowbytes = ALIGN( world.visbytes, 4 ); // force align rows by 32-bit boundary
	const size_t count = mod->numleafs + 1; // same as mod->submodels[0].visleafs + 1
	size_t total_compressed_size = 0;
	size_t hcount = 0;
	size_t vcount = 0;
	int i;

	if( !mod->visdata )
		return;

#if defined( HAVE_OPENMP )
	Con_Reportf( "Building PHS in %d threads...\n", omp_get_max_threads( ));
#else
	Con_Reportf( "Building PHS...\n" );
#endif

	byte *uncompressed_pvs = Mem_Calloc( mod->mempool, rowbytes * count * 2 );
	byte *uncompressed_phs = &uncompressed_pvs[rowbytes * count];

	world.phsofs = Mem_Calloc( mod->mempool, sizeof( size_t ) * count );
	world.compressed_phs = NULL;

	double t1 = Platform_DoubleTime();

#pragma omp parallel
	{
		// uncompress pvs first
#pragma omp for schedule( static, 256 ) // there might be thousands of leafs, split by 256
		for( i = 0; i < count; i++ )
			Mod_DecompressPVS( &uncompressed_pvs[rowbytes * i], mod->leafs[i].compressed_vis, world.visbytes );

		// now create phs
#pragma omp for schedule( static, 256 ) reduction( + : vcount, hcount )
		for( i = 0; i < count; i++ )
		{
			const byte *scan = &uncompressed_pvs[rowbytes * i];
			byte *dst = &uncompressed_phs[rowbytes * i]; // rowbytes, not rowwords!

			memcpy( dst, scan, rowbytes );

			for( size_t j = 0; j < rowbytes; j++ )
			{
				uint bitbyte = scan[j];

				if( bitbyte == 0 )
					continue;

				for( size_t k = 0; k < 8; k++ )
				{
					if( !FBitSet( bitbyte, BIT( k )))
						continue;

					// OR this pvs row into the phs
					// +1 because pvs is 1 based
					size_t index = (( j * 8 ) + k + 1 );
					if( index >= count )
						continue;

					Q_memor( dst, &uncompressed_pvs[rowbytes * index], rowbytes );
				}
			}

			if( vis_stats && i != 0 )
			{
				for( size_t j = 0; j < count; j++ )
				{
					if( CHECKVISBIT( scan, j ))
						vcount++;

					if( CHECKVISBIT( dst, j ))
						hcount++;
				}
			}
		}
	}

	// since I can't predict at which spot compressed array
	// should be put, this loop is single threaded
	for( i = 0; i < count; i++ )
	{
		const byte *src = &uncompressed_phs[rowbytes * i];
		byte temp_compressed_row[(MAX_MAP_LEAFS+1)/4]; // compression for this row might be ineffective

		size_t compressed_size = Mod_CompressPVS( temp_compressed_row, src, rowbytes );

		world.compressed_phs = Mem_Realloc( mod->mempool, world.compressed_phs, total_compressed_size + compressed_size );
		memcpy( &world.compressed_phs[total_compressed_size], temp_compressed_row, compressed_size );
		world.phsofs[i]	= total_compressed_size;

		total_compressed_size += compressed_size;
	}

	double t2 = Platform_DoubleTime();

	if( vis_stats )
		Con_Reportf( "Average leaves visible / audible / total: %zu / %zu / %zu\n", vcount / count, hcount / count, count );
	Con_Reportf( "Uncompressed PHS size: %s\n", Q_memprint( rowbytes * count ));
	Con_Reportf( "Compressed PHS size: %s\n", Q_memprint( total_compressed_size + sizeof( *world.phsofs ) * count ));
	Con_Reportf( "PHS building time: %.2f ms\n", ( t2 - t1 ) * 1000.0f );

	// TODO: rewrite this into a unit test
	// NOTE: how to get GoldSrc fat PHS and PVS data
	// start a multiplayer server with some op4_bootcamp (for example)
	// attach to process with GDB:
	// (gdb) p gPAS[0]
	// $0 = (byte *) ...
	// (gdb) p gPAS[gPVSRowBytes * (cl.worldmodel->numleafs + 1)]
	// $1 = (byte *) ...
	// (gdb) dump binary memory op4_bootcamp_gs.phs $0 $1
	// (gdb) p gPVS[0]
	// $2 = (byte *) ...
	// (gdb) p gPVS[gPVSRowBytes * (cl.worldmodel->numleafs + 1)]
	// $3 = (byte *) ...
	// (gdb) dump binary memory op4_bootcamp_gs.pvs $0 $1
	//
	// NOTE: as of writing, uncompressed PVS and PHS data do match! hooray!
	//
	// FS_WriteFile( "op4_bootcamp.pvs", uncompressed_pvs, rowbytes * count );
	// FS_WriteFile( "op4_bootcamp.phs", uncompressed_phs, rowbytes * count );

	// release uncompressed data
	Mem_Free( uncompressed_pvs );

	// TODO: cache the PHS somewhere, it might take a long time on giant maps
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes( model_t *mod, dbspmodel_t *bmod )
{
	dclipnode32_t	*out;

#if XASH_GAMECUBE
	if( bmod->version != QBSP2_VERSION && !bmod->isbsp30ext )
	{
		const size_t clip_sz = bmod->numclipnodes * sizeof( mclipnode16_t );

		Con_Reportf( "Xash3D GameCube: using compact clipnodes count=%zu\n", bmod->numclipnodes );
		mod->numclipnodes = bmod->numclipnodes;

		/* GoldSrc dclipnode_t and runtime mclipnode16_t share the same packed
		 * layout. When retained staging is already keeping the BSP source alive,
		 * alias the lump directly instead of spending another MEM1 copy. */
		if( GC_MapLoadMemoryOpt() && gc_retain_bsp_source_buffer && bmod->clipnodes )
		{
			mod->clipnodes16 = (mclipnode16_t *)bmod->clipnodes;
			Con_Reportf( "Xash3D GameCube: compact clipnodes aliased from BSP source %s\n",
				Q_memprint( clip_sz ));
		}
		else
		{
			mod->clipnodes16 = Mem_Malloc( mod->mempool, clip_sz );
			memcpy( mod->clipnodes16, bmod->clipnodes, clip_sz );
			Mod_GCFreeBspPin( (void **)&bmod->clipnodes );
		}
		return;
	}
#endif

	bmod->clipnodes_out = out = (dclipnode32_t *)Mem_Malloc( mod->mempool, bmod->numclipnodes * sizeof( *out ));

	if(( bmod->version == QBSP2_VERSION ) || ( bmod->version == HLBSP_VERSION && bmod->isbsp30ext && bmod->numclipnodes >= MAX_MAP_CLIPNODES_HLBSP ))
	{
		dclipnode32_t *in = bmod->clipnodes32;

		for( int i = 0; i < bmod->numclipnodes; i++, out++, in++ )
		{
			out->planenum = in->planenum;
			out->children[0] = in->children[0];
			out->children[1] = in->children[1];
		}
	}
	else
	{
		dclipnode_t	*in = bmod->clipnodes;

		for( int i = 0; i < bmod->numclipnodes; i++, out++, in++ )
		{
			out->planenum = in->planenum;

			out->children[0] = (unsigned short)in->children[0];
			out->children[1] = (unsigned short)in->children[1];

			// aguirRe QBSP 'broken' clipnodes
			if( out->children[0] >= bmod->numclipnodes )
				out->children[0] -= 65536;
			if( out->children[1] >= bmod->numclipnodes )
				out->children[1] -= 65536;
		}
	}

	// FIXME: fill mod->clipnodes?
	mod->numclipnodes = bmod->numclipnodes;

#if XASH_GAMECUBE
	if( bmod->version == QBSP2_VERSION )
		Mod_GCFreeBspPin( (void **)&bmod->clipnodes32 );
	else
		Mod_GCFreeBspPin( (void **)&bmod->clipnodes );
#endif
}

/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility( model_t *mod, dbspmodel_t *bmod )
{
	// external bmodels have no visibility
	if( !bmod->visdata || !bmod->visdatasize )
		return;

#if XASH_GAMECUBE
	/* Smoke (-gcmap) still drops visdata to save MEM1. New Game / world-render
	 * keep it (~50 KiB) so the renderer can FatPVS-cull instead of full-vis. */
	if( GC_MapLoadMemoryOpt() && mod->type == mod_brush && bmod->isworld
	    && !Sys_CheckParm( "-gcnewgame" ) && !Sys_CheckParm( "-gcworldrender" ))
	{
		Con_Reportf( "Xash3D GameCube: skipping world visdata for gcmap full-vis fallback (%s)\n",
			Q_memprint( bmod->visdatasize ));
		Mod_GCFreeBspPin( (void **)&bmod->visdata );
		return;
	}

	/* Optional: renderer can fall back to full-vis leaf marking. */
	mod->visdata = Mem_TryMalloc( mod->mempool, bmod->visdatasize );
	if( !mod->visdata )
	{
		Con_Reportf( "Xash3D GameCube: skipping visdata (%s) under memory pressure\n",
			Q_memprint( bmod->visdatasize ));
		Mod_GCFreeBspPin( (void **)&bmod->visdata );
		return;
	}
	Con_Reportf( "Xash3D GameCube: world visdata retained (%s)\n", Q_memprint( bmod->visdatasize ));
#else
	mod->visdata = Mem_Malloc( mod->mempool, bmod->visdatasize );
#endif
	memcpy( mod->visdata, bmod->visdata, bmod->visdatasize );

#if XASH_GAMECUBE
	Mod_GCFreeBspPin( (void **)&bmod->visdata );
#endif
}

/*
=================
Mod_LoadLightVecs
=================
*/
static void Mod_LoadLightVecs( model_t *mod, dbspmodel_t *bmod )
{
	if( bmod->deluxdatasize != bmod->lightdatasize )
	{
		if( bmod->deluxdatasize > 0 )
			Con_Printf( S_ERROR "%s: has mismatched size (%zu should be %zu)\n", __func__, bmod->deluxdatasize, bmod->lightdatasize );
		else
			Mod_LoadLitfile( mod, "dlit", bmod->lightdatasize, &bmod->deluxedata_out, &bmod->deluxdatasize ); // old method
		return;
	}

	bmod->deluxedata_out = Mem_Malloc( mod->mempool, bmod->deluxdatasize );
	memcpy( bmod->deluxedata_out, bmod->deluxdata, bmod->deluxdatasize );
}

/*
=================
Mod_LoadShadowmap
=================
*/
static void Mod_LoadShadowmap( model_t *mod, dbspmodel_t *bmod )
{
	if( bmod->shadowdatasize != ( bmod->lightdatasize / 3 ))
	{
		if( bmod->shadowdatasize > 0 )
			Con_Printf( S_ERROR "%s: has mismatched size (%zu should be %zu)\n", __func__, bmod->shadowdatasize, bmod->lightdatasize / 3 );
		return;
	}

	bmod->shadowdata_out = Mem_Malloc( mod->mempool, bmod->shadowdatasize );
	memcpy( bmod->shadowdata_out, bmod->shadowdata, bmod->shadowdatasize );
}

/*
=================
Mod_LoadLighting
=================
*/
static void Mod_LoadLighting( model_t *mod, dbspmodel_t *bmod )
{
#if XASH_GAMECUBE
	Mod_GCRestoreDeferredLightdata( bmod );
#endif

	if( !bmod->lightdatasize )
		return;

#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnolightmaps" ))
	{
		Con_Reportf( "Xash3D GameCube: lightmaps disabled\n" );
		return;
	}

	if( bmod->lightdatasize > ( 256 * 1024 ))
	{
		Con_Reportf( "Xash3D GameCube: lightmaps skipped size=%s\n", Q_memprint( bmod->lightdatasize ));
		return;
	}
#endif

	switch( bmod->lightmap_samples )
	{
	case 1:
		if( bmod->rgblightdata && bmod->rgblightdatasize > 0 && bmod->rgblightdatasize == bmod->lightdatasize * 3 )
		{
			bmod->lightdatasize = bmod->rgblightdatasize;
			mod->lightdata = Mem_Malloc( mod->mempool, bmod->rgblightdatasize );
			memcpy( mod->lightdata, bmod->rgblightdata, bmod->rgblightdatasize );
			SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		}
		else if( Mod_LoadLitfile( mod, "lit", bmod->lightdatasize * 3, &mod->lightdata, &bmod->lightdatasize ))
		{
			SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		}
		else
		{
			mod->lightdata = (color24 *)Mem_Malloc( mod->mempool, bmod->lightdatasize * sizeof( color24 ));

			// expand the white lighting data
			for( int i = 0; i < bmod->lightdatasize; i++ )
				mod->lightdata[i].r = mod->lightdata[i].g = mod->lightdata[i].b = bmod->lightdata[i];
		}
		break;
	case 3:	// load colored lighting
		mod->lightdata = Mem_Malloc( mod->mempool, bmod->lightdatasize );
		memcpy( mod->lightdata, bmod->lightdata, bmod->lightdatasize );
		SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		break;
	default:
		Host_Error( "%s: bad lightmap sample count %i\n", __func__, bmod->lightmap_samples );
		break;
	}

	Con_Reportf( "lighting: %s\n", FBitSet( mod->flags, MODEL_COLORED_LIGHTING ) ? "colored" : "monochrome" );

	// not supposed to be load ?
	if( FBitSet( host.features, ENGINE_LOAD_DELUXEDATA ))
	{
		Mod_LoadLightVecs( mod, bmod );
		Mod_LoadShadowmap( mod, bmod );

		if( bmod->isworld && bmod->deluxdatasize )
			SetBits( world.flags, FWORLD_HAS_DELUXEMAP );
	}

	// setup lightdata pointers
	if( !mod->lightdata )
		return;

	for( int i = 0; i < mod->numsurfaces; i++ )
	{
		int lightofs;

		if( bmod->version == QBSP2_VERSION )
			lightofs = bmod->surfaces32[i].lightofs;
		else
			lightofs = bmod->surfaces[i].lightofs;

		if( lightofs != -1 )
		{
			int offset = lightofs / bmod->lightmap_samples;

			// NOTE: we divide offset by three because lighting and deluxemap keep their pointers
			// into three-bytes structs and shadowmap just monochrome
			mod->surfaces[i].samples = mod->lightdata + offset;

#if !XASH_GAMECUBE
			// if deluxemap is present setup it too
			if( bmod->deluxedata_out )
				mod->surfaces[i].info->deluxemap = bmod->deluxedata_out + offset;

			// will be used by mods
			if( bmod->shadowdata_out )
				mod->surfaces[i].info->shadowmap = bmod->shadowdata_out + offset;
#endif
		}
	}
}

/*
=================
Mod_LumpLooksLikeEntities

=================
*/
static int Mod_LumpLooksLikeEntities( const char *lump, const size_t lumplen )
{
	// look for "classname" string
	return Q_memmem( lump, lumplen, "\"classname\"", sizeof( "\"classname\"" ) - 1 ) != NULL ? 1 : 0;
}

static void Mod_SwapBSPLumps( byte *mod_base, size_t bufferlen )
{
	dheader_t *header = (dheader_t *)mod_base;

	le_struct_swap( dheader_swap, header );

	// BSP30ext pass
	if( header->version == HLBSP_VERSION && bufferlen > sizeof( *header ) + sizeof( dextrahdr_t ))
	{
		dextrahdr_t *ext = (dextrahdr_t *)( mod_base + sizeof( *header ));

		if( ext->id == LittleLong( IDEXTRAHEADER ))
			le_struct_swap( dextrahdr_swap, ext );
	}
}

/*
=================
CRC32_MapFile

compute CRC for the map lump data
=================
*/
qboolean CRC32_MapFile( dword *crcvalue, const char *filename, qboolean multiplayer )
{
	if( !crcvalue )
		return false;

	// always calc same checksum for singleplayer
	if( multiplayer == false )
	{
		*crcvalue = (('H'<<24)+('S'<<16)+('A'<<8)+'X');
		return true;
	}

	file_t *f = FS_Open( filename, "rb", false );
	if( !f )
		return false;

	byte headbuf[sizeof( dheader_t )];
	int num_bytes = FS_Read( f, headbuf, sizeof( headbuf ));

	if( num_bytes != sizeof( headbuf ))
	{
		FS_Close( f );
		return false;
	}

	dheader_t *header = (dheader_t *)headbuf;
	le_struct_swap( dheader_swap, header );

	switch( header->version )
	{
	case Q1BSP_VERSION:
	case HLBSP_VERSION:
	case QBSP2_VERSION:
		break;
	default:
		FS_Close( f );
		return false;
	}

	CRC32_Init( crcvalue );

	char buffer[1024];
	for( int i = LUMP_PLANES; i < HEADER_LUMPS; i++ )
	{
		int lumplen = header->lumps[i].filelen;
		FS_Seek( f, header->lumps[i].fileofs, SEEK_SET );

		while( lumplen > 0 )
		{
			if( lumplen >= sizeof( buffer ))
				num_bytes = FS_Read( f, buffer, sizeof( buffer ));
			else
				num_bytes = FS_Read( f, buffer, lumplen );

			if( num_bytes > 0 )
			{
				lumplen -= num_bytes;
				CRC32_ProcessBuffer( crcvalue, buffer, num_bytes );
			}

			if( FS_Eof( f ))
				break;
		}
	}

	FS_Close( f );

	return true;
}

/*
=================
Mod_FindEndOfBSPFile

scans all lumps to find the factual end of file
=================
*/
static fs_offset_t Mod_FindEndOfBSPFile( const byte *mod_base, size_t bufferlen )
{
	const dheader_t *header = (const dheader_t *)mod_base;
	const dextrahdr_t *ext_header = (const dextrahdr_t *)( mod_base + sizeof( *header ));
	fs_offset_t max_offset = sizeof( *header );

	// find the maximum offset
	for( int i = 0; i < ARRAYSIZE( header->lumps ); i++ )
	{
		fs_offset_t offset = header->lumps[i].fileofs + header->lumps[i].filelen;

		if( max_offset < offset )
			max_offset = offset;
	}

	// to be able to combine BSPX data with BSP30ext, check the extended header too
	if( header->version == HLBSP_VERSION && ext_header->id == IDEXTRAHEADER && ext_header->version == EXTRA_VERSION )
	{
		for( int i = 0; i < ARRAYSIZE( ext_header->lumps ); i++ )
		{
			fs_offset_t offset = ext_header->lumps[i].fileofs + ext_header->lumps[i].filelen;

			if( max_offset < offset )
				max_offset = offset;
		}
	}

	return max_offset;
}

/*
=================
Mod_FindBSPX

find BSPX header position, returns -1 on error
=================
*/
static fs_offset_t Mod_FindBSPX( byte *mod_base, size_t bufferlen )
{
	fs_offset_t max_offset = Mod_FindEndOfBSPFile( mod_base, bufferlen );

	max_offset = ALIGN( max_offset, 4 ); // force 32-bit boundary

	if( max_offset + sizeof( dbspx_hdr_t ) > bufferlen )
		return -1;

	dbspx_hdr_t *bspx_header = (dbspx_hdr_t *)( mod_base + max_offset );

	if( bspx_header->id != LittleLong( IDBSPXHEADER ))
		return -1;

	bspx_header->id = LittleLong( bspx_header->id );
	bspx_header->numlumps = LittleLong( bspx_header->numlumps );

	for( int i = 0; i < bspx_header->numlumps; i++ )
		le_struct_swap( dbspx_lump_swap, &bspx_header->lumps[i] );

	Con_DPrintf( "Found valid BSPX signature at %lld\n", (long long)max_offset );
	return max_offset;
}

/*
=================
Mod_LoadBmodelLumps

loading and processing bmodel
=================
*/
static qboolean Mod_LoadBmodelLumps( model_t *mod, byte *mod_base, size_t bufferlen, qboolean isworld )
{
	dheader_t   *header = (dheader_t *)mod_base;
	int         *extident = (int *)(mod_base + sizeof( dheader_t ));
	char        wadvalue[2048];
	size_t      len = 0;
	int         stat_index = 0, flags = 0;

	// always reset the intermediate struct
	memset( &loadstat, 0, sizeof( loadstat ));

	Q_strncpy( loadstat.name, mod->name, sizeof( loadstat.name ));
	wadvalue[0] = '\0';

	// byte-swap BSP header and lump directory from little-endian
	Mod_SwapBSPLumps( mod_base, bufferlen );

	switch( header->version )
	{
	case HLBSP_VERSION:
		if( *extident == IDEXTRAHEADER )
		{
			SetBits( flags, LUMP_BSP30EXT );
		}
		// only relevant for half-life maps
		else if( !Mod_LumpLooksLikeEntities( mod_base + header->lumps[LUMP_ENTITIES].fileofs, header->lumps[LUMP_ENTITIES].filelen ) &&
			 Mod_LumpLooksLikeEntities( mod_base + header->lumps[LUMP_PLANES].fileofs, header->lumps[LUMP_PLANES].filelen ))
		{
			// blue-shift swapped lumps
			SetBits( flags, LUMP_BSHIFT_SWAP );
		}
		break;
	case Q1BSP_VERSION:
	case QBSP2_VERSION:
		if( header->version == QBSP2_VERSION )
			SetBits( mod->flags, MODEL_QBSP2 );
		break;
	default:
		Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", mod->name, header->version, HLBSP_VERSION );
		loadstat.numerrors++;
		return false;
	}

	dbspmodel_t *bmod = Mem_Calloc( mod->mempool, sizeof( *bmod ));
#if XASH_GAMECUBE
	qboolean retain_bsp_buffer = false;
	gc_retain_bsp_source_buffer = false;
	gc_bsp_scratch_base = mod_base;
	gc_bsp_scratch_size = bufferlen;
#endif
	bmod->version = header->version;	// share up global
	if( isworld )
	{
		world.flags = 0;	// clear world settings
		SetBits( flags, LUMP_SAVESTATS|LUMP_SILENT );
	}
	bmod->isworld = isworld;
	bmod->isbsp30ext = FBitSet( flags, LUMP_BSP30EXT );
	fs_offset_t bspx_header_offset = Mod_FindBSPX( mod_base, bufferlen );

	// loading base lumps
	for( int i = 0; i < ARRAYSIZE( srclumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[stat_index], flags, LOADLUMP_STANDARD, NULL, bmod );

	// loading extralumps
	for( int i = 0; i < ARRAYSIZE( extlumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSP30EXT, NULL, bmod );

	// loading bspx lumps
	if( bspx_header_offset >= 0 )
	{
		for( int i = 0; i < ARRAYSIZE( bspxlumps ); i++, stat_index++ )
			Mod_LoadLump( mod_base, &bspxlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSPX, mod_base + bspx_header_offset, bmod );
	}

	if( !bmod->isworld ) // a1ba: why world excluded here?
	{
		if( loadstat.numerrors )
		{
			Con_DPrintf( "%s: %i error(s), %i warning(s)\n", __func__, loadstat.numerrors, loadstat.numwarnings );
			Mem_Free( bmod );
			return false; // there were errors, we can't load this map
		}

		if( loadstat.numwarnings )
			Con_DPrintf( "%s: %i warning(s)\n", __func__, loadstat.numwarnings );
	}

	// load into heap
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel entities begin\n" );
#endif
	Mod_LoadEntities( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel entities ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel planes begin\n" );
#endif
	Mod_LoadPlanes( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel planes ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel submodel lump begin\n" );
#endif
	Mod_LoadSubmodels( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel submodel lump ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel vertexes begin\n" );
#endif
	Mod_LoadVertexes( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel vertexes ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel edges begin\n" );
#endif
	Mod_LoadEdges( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel edges ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel surfedges begin\n" );
#endif
	Mod_LoadSurfEdges( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel surfedges ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel textures begin\n" );
#endif
	Mod_LoadTextures( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel textures ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel visibility begin\n" );
#endif
	Mod_LoadVisibility( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel visibility ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel texinfo begin\n" );
#endif
	Mod_LoadTexInfo( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel texinfo ready\n" );
	Mod_GCFreeGcmapPreSurfaceLumps( bmod );
	Mod_GCReleaseGcmapPreSurfaceStaging( mod, bmod, mod_base, bufferlen );
	GC_MemSample( "pre-surfaces" );
	Mod_GCEnsureBspLump( mod, bmod, LUMP_FACES );
	Con_Reportf( "Xash3D GameCube: bmodel surfaces begin\n" );
#endif
	Mod_LoadSurfaces( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel surfaces ready\n" );
	Mod_GCEnsureBspLump( mod, bmod, LUMP_MARKSURFACES );
#endif
	Mod_LoadMarkSurfaces( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel marksurfaces ready\n" );
	Mod_GCEnsureBspLump( mod, bmod, LUMP_LEAFS );
#endif
	Mod_LoadLeafs( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel leafs ready\n" );
	Mod_GCEnsureBspLump( mod, bmod, LUMP_NODES );
#endif
	Mod_LoadNodes( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel nodes ready\n" );
	Mod_GCEnsureBspLump( mod, bmod, LUMP_CLIPNODES );
#endif
	Mod_LoadClipnodes( mod, bmod );
#if XASH_GAMECUBE
	if( bmod->version != QBSP2_VERSION && !bmod->isbsp30ext && bmod->clipnodes_out == NULL )
		retain_bsp_buffer = true;
	if( gc_retain_bsp_source_buffer )
		retain_bsp_buffer = true;
#endif
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel clipnodes ready\n" );
#endif

	// preform some post-initalization
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel hull0 begin\n" );
#endif
	Mod_MakeHull0( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel hull0 ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel submodels begin\n" );
#endif
	Mod_SetupSubmodels( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel submodels ready\n" );
	Con_Reportf( "Xash3D GameCube: bmodel lighting begin\n" );
#endif
	Mod_LoadLighting( mod, bmod );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: bmodel lighting ready\n" );
	Mod_GCFreeBspPin( (void **)&bmod->lightdata );
#endif

	if( isworld )
	{
		world.version = bmod->version;
#if XASH_GAMECUBE
		if( retain_bsp_buffer )
			mod->cache.data = mod_base;
#endif
#if !XASH_DEDICATED
		world.deluxedata = bmod->deluxedata_out;	// deluxemap data pointer
		world.shadowdata = bmod->shadowdata_out;	// occlusion data pointer
#endif // XASH_DEDICATED

		if( SV_Active() && svs.maxclients > 1 )
			Mod_CalcPHS( mod );
	}

	qboolean wadlist_warn = false;
	for( int i = 0; i < world.wadcount; i++ )
	{
		if( !world.wadlist[i].usage )
			continue;

		if( !wadlist_warn )
		{
			int ret = Q_snprintf( &wadvalue[len], sizeof( wadvalue ) - len, "%s; ", world.wadlist[i].name );
			if( ret == -1 )
			{
				Con_DPrintf( S_WARN "Too many wad files for output!\n" );
				wadlist_warn = true;
			}
			len += ret;
		}
	}

	if( !COM_StringEmptyOrNULL( wadvalue ))
	{
		wadvalue[Q_strlen( wadvalue ) - 2] = '\0'; // kill the last semicolon
		Con_Reportf( "Wad files required to run the map: \"%s\"\n", wadvalue );
	}

	Mem_Free( bmod );
	return true;
}

static int Mod_LumpLooksLikeEntitiesFile( file_t *f, const dlump_t *l, int flags, const char *msg )
{
	if( FS_Seek( f, l->fileofs, SEEK_SET ) < 0 )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "map ^2%s^7 %s lump past end of file\n", loadstat.name, msg );
		return -1;
	}

	char *buf = Z_Malloc( l->filelen + 1 );
	if( FS_Read( f, buf, l->filelen ) != l->filelen )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "can't read %s lump of map ^2%s^7", msg, loadstat.name );
		Z_Free( buf );
		return -1;
	}

	int ret = Mod_LumpLooksLikeEntities( buf, l->filelen );

	Z_Free( buf );
	return ret;
}

/*
=================
Mod_TestBmodelLumps

check for possible errors
return real entities lump (for bshift swapped lumps)
=================
*/
qboolean Mod_TestBmodelLumps( file_t *f, const char *name, byte *mod_base, size_t buffersize, qboolean silent, dlump_t *entities )
{
	dheader_t   *header = (dheader_t *)mod_base;
	int         *extident = (int *)( mod_base + sizeof( dheader_t ));
	int         flags = 0, stat_index = 0;

	// always reset the intermediate struct
	memset( &loadstat, 0, sizeof( loadstat_t ));

	// store the name to correct show errors and warnings
	Q_strncpy( loadstat.name, name, sizeof( loadstat.name ));
	if( silent )
		SetBits( flags, LUMP_SILENT );

	if( buffersize < sizeof( *header ))
		return false;

	// byte-swap BSP header and lump directory from little-endian
	Mod_SwapBSPLumps( mod_base, buffersize );

	switch( header->version )
	{
	case HLBSP_VERSION:
		if( buffersize > sizeof( *header ) + sizeof( dextrahdr_t ) && *extident == IDEXTRAHEADER )
		{
			SetBits( flags, LUMP_BSP30EXT );
		}
		else
		{
			// only relevant for half-life maps
			int ret = Mod_LumpLooksLikeEntitiesFile( f, &header->lumps[LUMP_ENTITIES], flags, "entities" );
			if( ret < 0 )
				return false;

			if( !ret )
			{
				ret = Mod_LumpLooksLikeEntitiesFile( f, &header->lumps[LUMP_PLANES], flags, "planes" );
				if( ret < 0 )
					return false;

				if( ret )
					SetBits( flags, LUMP_BSHIFT_SWAP );
			}
		}
		break;
	case Q1BSP_VERSION:
	case QBSP2_VERSION:
		break;
	default:
		// don't early out: let me analyze errors
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", name, header->version, HLBSP_VERSION );
		loadstat.numerrors++;
		break;
	}

	// get entities lump to caller
	*entities = header->lumps[FBitSet( flags, LUMP_BSHIFT_SWAP ) ? LUMP_PLANES : LUMP_ENTITIES];

	// loading base lumps
	for( int i = 0; i < ARRAYSIZE( srclumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[stat_index], flags, LOADLUMP_STANDARD, NULL, NULL );

	// loading extralumps
	for( int i = 0; i < ARRAYSIZE( extlumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSP30EXT, NULL, NULL );

	// FIXME: BSPX testing

	if( !FBitSet( flags, LUMP_SILENT ))
	{
		if( loadstat.numerrors )
			Con_Printf( "%s: %i error(s), %i warning(s)\n", __func__, loadstat.numerrors, loadstat.numwarnings );
		else if( loadstat.numwarnings )
			Con_Printf( "%s: %i warning(s)\n", __func__, loadstat.numwarnings );
	}

	return loadstat.numerrors ? false : true;
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel( model_t *mod, void *buffer, size_t buffersize, qboolean *loaded )
{
	char poolname[MAX_VA_STRING];

	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );

	if( loaded ) *loaded = false;

	mod->mempool = Mem_AllocPool( poolname );
	mod->type = mod_brush;

	// loading all the lumps into heap
	if( !Mod_LoadBmodelLumps( mod, buffer, buffersize, world.loading ))
		return; // there were errors

	if( world.loading ) worldmodel = mod;

	if( loaded ) *loaded = true;	// all done
}

/*
==================
Mod_CheckLump

check lump for existing
==================
*/
int GAME_EXPORT Mod_CheckLump( const char *filename, const int lump, int *lumpsize )
{
	file_t		*f = FS_Open( filename, "rb", false );
	byte		buffer[sizeof( dheader_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );

	if( !f ) return LUMP_LOAD_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_HEADER;
	}

	dheader_t *header = (dheader_t *)buffer;

	if( header->version != HLBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_VERSION;
	}

	dextrahdr_t *extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_LOAD_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen <= 0 )
	{
		FS_Close( f );
		return LUMP_LOAD_NOT_EXIST;
	}

	if( lumpsize )
		*lumpsize = extrahdr->lumps[lump].filelen;

	FS_Close( f );

	return LUMP_LOAD_OK;
}

/*
==================
Mod_ReadLump

reading random lump by user request
==================
*/
int GAME_EXPORT Mod_ReadLump( const char *filename, const int lump, void **lumpdata, int *lumpsize )
{
	file_t		*f = FS_Open( filename, "rb", false );
	byte		buffer[sizeof( dheader_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );

	if( !f ) return LUMP_LOAD_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_HEADER;
	}

	dheader_t *header = (dheader_t *)buffer;

	if( header->version != HLBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_VERSION;
	}

	dextrahdr_t *extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_LOAD_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen <= 0 )
	{
		FS_Close( f );
		return LUMP_LOAD_NOT_EXIST;
	}

	byte *data = malloc( extrahdr->lumps[lump].filelen + 1 );
	int length = extrahdr->lumps[lump].filelen;

	if( !data )
	{
		FS_Close( f );
		return LUMP_LOAD_MEM_FAILED;
	}

	FS_Seek( f, extrahdr->lumps[lump].fileofs, SEEK_SET );

	if( FS_Read( f, data, length ) != length )
	{
		free( data );
		FS_Close( f );
		return LUMP_LOAD_CORRUPTED;
	}

	data[length] = 0; // write term
	FS_Close( f );

	if( lumpsize )
		*lumpsize = length;
	*lumpdata = data;

	return LUMP_LOAD_OK;
}

/*
==================
Mod_SaveLump

writing lump by user request
only empty lumps is allows
==================
*/
int GAME_EXPORT Mod_SaveLump( const char *filename, const int lump, void *lumpdata, int lumpsize )
{
	byte		buffer[sizeof( dheader_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );
	int		dummy = lumpsize;

	if( !lumpdata || lumpsize <= 0 )
		return LUMP_SAVE_NO_DATA;

	// make sure what .bsp is placed into gamedir and not in pak
	if( !FS_GetDiskPath( filename, true ))
		return LUMP_SAVE_COULDNT_OPEN;

	// first we should sure what we allow to rewrite this .bsp
	int result = Mod_CheckLump( filename, lump, &dummy );

	if( result != LUMP_LOAD_NOT_EXIST )
		return result;

	file_t *f = FS_Open( filename, "e+b", true );

	if( !f ) return LUMP_SAVE_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_SAVE_BAD_HEADER;
	}

	dheader_t *header = (dheader_t *)buffer;

	// these checks below are redundant
	if( header->version != HLBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_SAVE_BAD_VERSION;
	}

	dextrahdr_t *extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_SAVE_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_SAVE_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen != 0 )
	{
		FS_Close( f );
		return LUMP_SAVE_ALREADY_EXIST;
	}

	FS_Seek( f, 0, SEEK_END );

	// will be saved later
	extrahdr->lumps[lump].fileofs = FS_Tell( f );
	extrahdr->lumps[lump].filelen = lumpsize;

	if( FS_Write( f, lumpdata, lumpsize ) != lumpsize )
	{
		FS_Close( f );
		return LUMP_SAVE_CORRUPTED;
	}

	// update the header
	FS_Seek( f, sizeof( dheader_t ), SEEK_SET );

	if( FS_Write( f, extrahdr, sizeof( dextrahdr_t )) != sizeof( dextrahdr_t ))
	{
		FS_Close( f );
		return LUMP_SAVE_CORRUPTED;
	}

	FS_Close( f );
	return LUMP_SAVE_OK;
}
