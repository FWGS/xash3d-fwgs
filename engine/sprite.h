/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#ifndef SPRITE_H
#define SPRITE_H

#include "build.h"
#include STDINT_H
#include "synctype.h"

/*
==============================================================================

SPRITE MODELS

.spr extended version (Half-Life compatible sprites with some Xash3D extensions)
==============================================================================
*/

#define IDSPRITEHEADER	(('P'<<24)+('S'<<16)+('D'<<8)+'I')	// little-endian "IDSP"

#define SPRITE_VERSION_Q1	1				// Quake sprites
#define SPRITE_VERSION_HL	2				// Half-Life sprites
#define SPRITE_VERSION_32	32				// Captain Obvious mode on

typedef enum
{
	FRAME_SINGLE = 0,
	FRAME_GROUP,
	FRAME_ANGLED			// Xash3D ext
} frametype_t;

typedef enum
{
	SPR_NORMAL = 0,
	SPR_ADDITIVE,
	SPR_INDEXALPHA,
	SPR_ALPHTEST,
} drawtype_t;

typedef enum
{
	SPR_FWD_PARALLEL_UPRIGHT = 0,
	SPR_FACING_UPRIGHT,
	SPR_FWD_PARALLEL,
	SPR_ORIENTED,
	SPR_FWD_PARALLEL_ORIENTED,
} angletype_t;

typedef enum
{
	SPR_CULL_FRONT = 0,			// oriented sprite will be draw with one face
	SPR_CULL_NONE,			// oriented sprite will be draw back face too
} facetype_t;

// generic helper
typedef struct
{
	int32_t		ident;		// LittleLong 'ISPR'
	int32_t		version;		// current version 2
} dsprite_t;

STATIC_CHECK_SIZEOF( dsprite_t, 8, 8 );

typedef struct
{
	int32_t		ident;		// LittleLong 'ISPR'
	int32_t		version;		// current version 2
	int32_t		type;		// camera align
	float		boundingradius;	// quick face culling
	int32_t		bounds[2];	// mins\maxs
	int32_t		numframes;	// including groups
	float		beamlength;	// ???
	uint32_t	synctype;		// animation synctype, was synctype_t
} dsprite_q1_t;

STATIC_CHECK_SIZEOF( dsprite_q1_t, 36, 36 );

typedef struct
{
	int32_t		ident;		// LittleLong 'ISPR'
	int32_t		version;		// current version 2
	uint32_t	type;		// camera align, was angletype_t
	uint32_t	texFormat;	// rendering mode, was drawtype_t
	int32_t		boundingradius;	// quick face culling
	int32_t		bounds[2];	// mins\maxs
	int32_t		numframes;	// including groups
	uint32_t	facetype;		// cullface (Xash3D ext), was facetype_t
	uint32_t	synctype;		// animation synctype, was synctype_t
} dsprite_hl_t;

STATIC_CHECK_SIZEOF( dsprite_hl_t, 40, 40 );

typedef struct
{
	int32_t		origin[2];
	int32_t		width;
	int32_t		height;
} dspriteframe_t;

STATIC_CHECK_SIZEOF( dspriteframe_t, 16, 16 );

typedef struct
{
	int32_t		numframes;
} dspritegroup_t;

STATIC_CHECK_SIZEOF( dspritegroup_t, 4, 4 );

typedef struct
{
	float		interval;
} dspriteinterval_t;

STATIC_CHECK_SIZEOF( dspriteinterval_t, 4, 4 );

typedef struct
{
	uint32_t	type; // was frametype_t
} dframetype_t;

STATIC_CHECK_SIZEOF( dframetype_t, 4, 4 );

#endif//SPRITE_H
