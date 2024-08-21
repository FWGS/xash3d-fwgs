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

#ifndef ALIAS_H
#define ALIAS_H

#include "build.h"
#include STDINT_H
#include "synctype.h"

/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/

#define IDALIASHEADER	(('O'<<24)+('P'<<16)+('D'<<8)+'I')	// little-endian "IDPO"

#define ALIAS_VERSION	6

// client-side model flags
#define ALIAS_ROCKET		0x0001	// leave a trail
#define ALIAS_GRENADE		0x0002	// leave a trail
#define ALIAS_GIB			0x0004	// leave a trail
#define ALIAS_ROTATE		0x0008	// rotate (bonus items)
#define ALIAS_TRACER		0x0010	// green split trail
#define ALIAS_ZOMGIB		0x0020	// small blood trail
#define ALIAS_TRACER2		0x0040	// orange split trail + rotate
#define ALIAS_TRACER3		0x0080	// purple trail

typedef enum
{
	ALIAS_SINGLE = 0,
	ALIAS_GROUP
} aliasframetype_t;

typedef enum
{
	ALIAS_SKIN_SINGLE = 0,
	ALIAS_SKIN_GROUP
} aliasskintype_t;

typedef struct
{
	int32_t		ident;
	int32_t		version;
	vec3_t		scale;
	vec3_t		scale_origin;
	float		boundingradius;
	vec3_t		eyeposition;
	int32_t		numskins;
	int32_t		skinwidth;
	int32_t		skinheight;
	int32_t		numverts;
	int32_t		numtris;
	int32_t		numframes;
	uint32_t	synctype; // was synctype_t
	int32_t		flags;
	float		size;
} daliashdr_t;

STATIC_CHECK_SIZEOF( daliashdr_t, 84, 84 );

typedef struct
{
	int32_t		onseam;
	int32_t		s;
	int32_t		t;
} stvert_t;

STATIC_CHECK_SIZEOF( stvert_t, 12, 12 );

typedef struct dtriangle_s
{
	int32_t		facesfront;
	int32_t		vertindex[3];
} dtriangle_t;

STATIC_CHECK_SIZEOF( dtriangle_t, 16, 16 );

#define DT_FACES_FRONT	0x0010
#define ALIAS_ONSEAM	0x0020

typedef struct
{
	trivertex_t	bboxmin;	// lightnormal isn't used
	trivertex_t	bboxmax;	// lightnormal isn't used
	char		name[16];	// frame name from grabbing
} daliasframe_t;

STATIC_CHECK_SIZEOF( daliasframe_t, 24, 24 );

typedef struct
{
	int32_t		numframes;
	trivertex_t	bboxmin;	// lightnormal isn't used
	trivertex_t	bboxmax;	// lightnormal isn't used
} daliasgroup_t;

STATIC_CHECK_SIZEOF( daliasgroup_t, 12, 12 );

typedef struct
{
	int32_t		numskins;
} daliasskingroup_t;

STATIC_CHECK_SIZEOF( daliasskingroup_t, 4, 4 );

typedef struct
{
	float		interval;
} daliasinterval_t;

STATIC_CHECK_SIZEOF( daliasinterval_t, 4, 4 );

typedef struct
{
	float		interval;
} daliasskininterval_t;

STATIC_CHECK_SIZEOF( daliasskininterval_t, 4, 4 );

typedef struct
{
	uint32_t	type; // was aliasframetype_t
} daliasframetype_t;

STATIC_CHECK_SIZEOF( daliasframetype_t, 4, 4 );

typedef struct
{
	uint32_t	type; // was aliasskintype_t
} daliasskintype_t;

STATIC_CHECK_SIZEOF( daliasskintype_t, 4, 4 );

#endif//ALIAS_H
