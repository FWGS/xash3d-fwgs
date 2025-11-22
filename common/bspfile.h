/*
bspfile.h - BSP format included q1, hl1 support
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef BSPFILE_H
#define BSPFILE_H

#include <stdint.h>

/*
==============================================================================

BRUSH MODELS

.bsp contain level static geometry with including PVS and lightning info
==============================================================================
*/

// header
#define Q1BSP_VERSION 29 // quake1 regular version (beta is 28)
#define HLBSP_VERSION 30 // half-life regular version
#define QBSP2_VERSION (( 'B' << 0 ) | ('S' << 8 ) | ( 'P' << 16 ) | ( '2' << 24 ))

#define IDEXTRAHEADER (( 'X' << 0 ) | ('A' << 8 ) | ( 'S' << 16 ) | ( 'H' << 24 ))
#define EXTRA_VERSION 4 // ver. 1 was occupied by old versions of XashXT
                        // ver. 2 was occupied by old vesrions of P2:savior
                        // ver. 3 was occupied by experimental versions of P2:savior change fmt

#define DELUXEMAP_VERSION 1
#define IDDELUXEMAPHEADER (( 'Q' << 0 ) | ('L' << 8 ) | ( 'I' << 16 ) | ( 'T' << 24 ))

// worldcraft predefined angles
#define ANGLE_UP   -1
#define ANGLE_DOWN -2

// bmodel limits
#define MAX_MAP_HULLS 4	 // MAX_HULLS

#define SURF_PLANEBACK      BIT( 1 ) // plane should be negated
#define SURF_DRAWSKY        BIT( 2 ) // sky surface
#define SURF_DRAWTURB_QUADS BIT( 3 ) // all subidivided polygons are quads
#define SURF_DRAWTURB       BIT( 4 ) // warp surface
#define SURF_DRAWTILED      BIT( 5 ) // face without lighmap
#define SURF_CONVEYOR       BIT( 6 ) // scrolled texture (was SURF_DRAWBACKGROUND)
#define SURF_UNDERWATER     BIT( 7 ) // caustics
#define SURF_TRANSPARENT    BIT( 8 ) // it's a transparent texture (was SURF_DONTWARP)

// lightstyle management
#define LM_STYLES 4 // MAXLIGHTMAPS
#define LS_NORMAL 0x00
#define LS_UNUSED 0xFE
#define LS_NONE   0xFF

#define MAX_MAP_CLIPNODES_HLBSP 32767
#define MAX_MAP_CLIPNODES_BSP2  524288

// these limis not using by modelloader but only for displaying 'mapstats' correctly
#define MAX_MAP_MODELS       2048                   // embedded models
#define MAX_MAP_ENTSTRING    0x200000               // 2 Mb should be enough
#define MAX_MAP_PLANES       131072                 // can be increased without problems
#define MAX_MAP_NODES        262144                 // can be increased without problems
#define MAX_MAP_CLIPNODES    MAX_MAP_CLIPNODES_BSP2 // can be increased without problems
#define MAX_MAP_LEAFS        131072                 // CRITICAL STUFF to run ad_sepulcher!!!
#define MAX_MAP_VERTS        524288                 // can be increased without problems
#define MAX_MAP_FACES        262144                 // can be increased without problems
#define MAX_MAP_MARKSURFACES 524288                 // can be increased without problems

#define MAX_MAP_ENTITIES   8192           // network limit
#define MAX_MAP_TEXINFO    MAX_MAP_FACES  // in theory each face may have personal texinfo
#define MAX_MAP_EDGES      0x100000       // can be increased but not needed
#define MAX_MAP_SURFEDGES  0x200000       // can be increased but not needed
#define MAX_MAP_TEXTURES   2048           // can be increased but not needed
#define MAX_MAP_MIPTEX     0x2000000      // 32 Mb internal textures data
#define MAX_MAP_LIGHTING   0x2000000      // 32 Mb lightmap raw data (can contain deluxemaps)
#define MAX_MAP_VISIBILITY 0x1000000      // 16 Mb visdata
#define MAX_MAP_FACEINFO   8192           // can be increased but not needed

// quake lump ordering
enum
{
	LUMP_ENTITIES     = 0,
	LUMP_PLANES       = 1,
	LUMP_TEXTURES     = 2,  // internal textures
	LUMP_VERTEXES     = 3,
	LUMP_VISIBILITY   = 4,
	LUMP_NODES        = 5,
	LUMP_TEXINFO      = 6,
	LUMP_FACES        = 7,
	LUMP_LIGHTING     = 8,
	LUMP_CLIPNODES    = 9,
	LUMP_LEAFS        = 10,
	LUMP_MARKSURFACES = 11,
	LUMP_EDGES        = 12,
	LUMP_SURFEDGES    = 13,
	LUMP_MODELS       = 14, // internal submodels
	HEADER_LUMPS      = 15,
};

// extra lump ordering
enum
{
	LUMP_LIGHTVECS     = 0,  // deluxemap data
	LUMP_FACEINFO      = 1,  // landscape and lightmap resolution info
	LUMP_CUBEMAPS      = 2,  // cubemap description
	LUMP_VERTNORMALS   = 3,  // phong shaded vertex normals
	LUMP_LEAF_LIGHTING = 4,  // store vertex lighting for statics
	LUMP_WORLDLIGHTS   = 5,  // list of all the virtual and real lights (used to relight models in-game)
	LUMP_COLLISION     = 6,  // physics engine collision hull dump (userdata)
	LUMP_AINODEGRAPH   = 7,  // node graph that stored into the bsp (userdata)
	LUMP_SHADOWMAP     = 8,  // contains shadow map for direct light
	LUMP_VERTEX_LIGHT  = 9,  // store vertex lighting for statics
	LUMP_UNUSED0       = 10, // one lump reserved for me
	LUMP_UNUSED1       = 11, // one lump reserved for me
	EXTRA_LUMPS        = 12, // count of the extra lumps
};

// texture flags
#define TEX_SPECIAL        BIT( 0 ) // sky or slime, no lightmap or 256 subdivision
#define TEX_WORLD_LUXELS   BIT( 1 ) // alternative lightmap matrix will be used (luxels per world units instead of luxels per texels)
#define TEX_AXIAL_LUXELS   BIT( 2 ) // force world luxels to axial positive scales
#define TEX_EXTRA_LIGHTMAP BIT( 3 ) // bsp31 legacy - using 8 texels per luxel instead of 16 texels per luxel
#define TEX_SCROLL         BIT( 6 ) // Doom special FX

#define IsLiquidContents( cnt )	( cnt == CONTENTS_WATER || cnt == CONTENTS_SLIME || cnt == CONTENTS_LAVA )

// ambient sound types
enum
{
	AMBIENT_WATER = 0, // waterfall
	AMBIENT_SKY,       // wind
	AMBIENT_SLIME,     // never used in quake
	AMBIENT_LAVA,      // never used in quake
	NUM_AMBIENTS,      // automatic ambient sounds
};

//
// BSP File Structures
//
typedef struct
{
	int32_t fileofs;
	int32_t filelen;
} dlump_t;

typedef struct
{
	int32_t version;
	dlump_t lumps[HEADER_LUMPS];
} dheader_t;

typedef struct
{
	int32_t id; // must be little endian XASH
	int32_t version;
	dlump_t lumps[EXTRA_LUMPS];
} dextrahdr_t;

typedef struct
{
	vec3_t  mins;
	vec3_t  maxs;
	vec3_t  origin;                  // for sounds or lights
	int32_t headnode[MAX_MAP_HULLS];
	int32_t visleafs;                // not including the solid leaf 0
	int32_t firstface;
	int32_t numfaces;
} dmodel_t;

typedef struct
{
	int32_t nummiptex;
	int32_t dataofs[4]; // [nummiptex]
} dmiptexlump_t;

typedef struct
{
	vec3_t  point;
} dvertex_t;

typedef struct
{
	vec3_t  normal;
	float   dist;
	int32_t type; // PLANE_X - PLANE_ANYZ ?
} dplane_t;

typedef struct
{
	int32_t  planenum;
	int16_t  children[2]; // negative numbers are -(leafs + 1), not nodes
	int16_t  mins[3];     // for sphere culling
	int16_t  maxs[3];
	uint16_t firstface;
	uint16_t numfaces;    // counting both sides
} dnode_t;

typedef struct
{
	int32_t planenum;
	int32_t children[2]; // negative numbers are -(leafs+1), not nodes
	float   mins[3];     // for sphere culling
	float   maxs[3];
	int32_t firstface;
	int32_t numfaces;    // counting both sides
} dnode32_t;

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
typedef struct
{
	int32_t  contents;
	int32_t  visofs;                      // -1 = no visibility info

	int16_t  mins[3];                     // for frustum culling
	int16_t  maxs[3];
	uint16_t firstmarksurface;
	uint16_t nummarksurfaces;

	// automatic ambient sounds
	uint8_t  ambient_level[NUM_AMBIENTS]; // ambient sound level (0 - 255)
} dleaf_t;

typedef struct
{
	int32_t contents;
	int32_t visofs;                       // -1 = no visibility info

	float   mins[3];                      // for frustum culling
	float   maxs[3];

	int32_t firstmarksurface;
	int32_t nummarksurfaces;

	uint8_t ambient_level[NUM_AMBIENTS];
} dleaf32_t;

typedef struct
{
	int32_t planenum;
	int16_t children[2]; // negative numbers are contents
} dclipnode_t;

typedef struct
{
	int32_t planenum;
	int32_t children[2]; // negative numbers are contents
} dclipnode32_t;

typedef struct
{
	float   vecs[2][4]; // texmatrix [s/t][xyz offset]
	int32_t miptex;
	int16_t flags;
	int16_t faceinfo;   // -1 no face info otherwise dfaceinfo_t
} dtexinfo_t;

typedef struct
{
	char     landname[16]; // name of decsription in mapname_land.txt
	uint16_t texture_step; // default is 16, pixels\luxels ratio
	uint16_t max_extent;   // default is 16, subdivision step ((texture_step * max_extent) - texture_step)
	int16_t  groupid;      // to determine equal landscapes from various groups, -1 - no group
} dfaceinfo_t;

typedef uint16_t dmarkface_t;   // leaf marksurfaces indexes
typedef int32_t  dmarkface32_t; // leaf marksurfaces indexes

typedef int32_t dsurfedge_t; // map surfedges

// NOTE: that edge 0 is never used, because negative edge nums
// are used for counterclockwise use of the edge in a face
typedef struct
{
	uint16_t v[2]; // vertex numbers
} dedge_t;

typedef struct
{
	int32_t  v[2]; // vertex numbers
} dedge32_t;

typedef struct
{
	uint16_t planenum;
	int16_t  side;

	int32_t  firstedge;         // we must support > 64k edges
	int16_t  numedges;
	int16_t  texinfo;

	// lighting info
	uint8_t  styles[LM_STYLES];
	int32_t  lightofs;          // start of [numstyles*surfsize] samples
} dface_t;

typedef struct
{
	int32_t planenum;
	int32_t side;

	int32_t firstedge;          // we must support > 64k edges
	int32_t numedges;
	int32_t texinfo;

	// lighting info
	uint8_t styles[LM_STYLES];
	int32_t lightofs;           // start of [numstyles*surfsize] samples
} dface32_t;

#endif // BSPFILE_H
