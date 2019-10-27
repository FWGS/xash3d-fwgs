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

// must match definition in alias.h
#ifndef SYNCTYPE_T
#define SYNCTYPE_T
typedef enum
{
	ST_SYNC = 0,
	ST_RAND
} synctype_t;
#endif

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
	int		ident;		// LittleLong 'ISPR'
	int		version;		// current version 2
} dsprite_t;

typedef struct
{
	int		ident;		// LittleLong 'ISPR'
	int		version;		// current version 2
	int		type;		// camera align
	float		boundingradius;	// quick face culling
	int		bounds[2];	// mins\maxs
	int		numframes;	// including groups
	float		beamlength;	// ???
	synctype_t	synctype;		// animation synctype
} dsprite_q1_t;

typedef struct
{
	int		ident;		// LittleLong 'ISPR'
	int		version;		// current version 2
	angletype_t	type;		// camera align
	drawtype_t	texFormat;	// rendering mode
	int		boundingradius;	// quick face culling
	int		bounds[2];	// mins\maxs
	int		numframes;	// including groups
	facetype_t	facetype;		// cullface (Xash3D ext)
	synctype_t	synctype;		// animation synctype
} dsprite_hl_t;

typedef struct
{
	int		origin[2];
	int		width;
	int		height;
} dspriteframe_t;

typedef struct
{
	int		numframes;
} dspritegroup_t;

typedef struct
{
	float		interval;
} dspriteinterval_t;

typedef struct
{
	frametype_t	type;
} dframetype_t;

#endif//SPRITE_H
