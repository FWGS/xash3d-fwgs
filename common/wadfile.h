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

#ifndef WADFILE_H
#define WADFILE_H

/*
========================================================================
.WAD archive format	(WhereAllData - WAD)

List of compressed files, that can be identify only by TYP_*

<format>
header:	dwadinfo_t[dwadinfo_t]
file_1:	byte[dwadinfo_t[num]->disksize]
file_2:	byte[dwadinfo_t[num]->disksize]
file_3:	byte[dwadinfo_t[num]->disksize]
...
file_n:	byte[dwadinfo_t[num]->disksize]
infotable	dlumpinfo_t[dwadinfo_t->numlumps]
========================================================================
*/

#define IDWAD2HEADER	(('2'<<24)+('D'<<16)+('A'<<8)+'W')	// little-endian "WAD2" quake wads
#define IDWAD3HEADER	(('3'<<24)+('D'<<16)+('A'<<8)+'W')	// little-endian "WAD3" half-life wads
#define WAD3_NAMELEN	16

// dlumpinfo_t->attribs
#define ATTR_NONE		0	// allow to read-write
#define ATTR_READONLY	BIT( 0 )	// don't overwrite this lump in anyway
#define ATTR_COMPRESSED	BIT( 1 )	// not used for now, just reserved
#define ATTR_HIDDEN		BIT( 2 )	// not used for now, just reserved
#define ATTR_SYSTEM		BIT( 3 )	// not used for now, just reserved

// dlumpinfo_t->type
#define TYP_ANY		-1	// any type can be accepted
#define TYP_NONE		0	// unknown lump type
#define TYP_LABEL		1	// legacy from Doom1. Empty lump - label (like P_START, P_END etc)
#define TYP_PALETTE		64	// quake or half-life palette (768 bytes)
#define TYP_DDSTEX		65	// contain DDS texture
#define TYP_GFXPIC		66	// menu or hud image (not contain mip-levels)
#define TYP_MIPTEX		67	// quake1 and half-life in-game textures with four miplevels
#define TYP_SCRIPT		68	// contain script files
#define TYP_COLORMAP2	69	// old stuff. build palette from LBM file (not used)
#define TYP_QFONT		70	// half-life font (qfont_t)

/*
========================================================================

.LMP image format	(Half-Life gfx.wad lumps)

========================================================================
*/
typedef struct lmp_s
{
	unsigned int	width;
	unsigned int	height;
} lmp_t;

/*
========================================================================

.MIP image format	(half-Life textures)

========================================================================
*/
typedef struct mip_s
{
	char		name[16];
	unsigned int	width;
	unsigned int	height;
	unsigned int	offsets[4];	// four mip maps stored
} mip_t;

/*
========================================================================

.WAD header format	(half-Life textures)

========================================================================
*/
typedef struct
{
	int		ident;		// should be WAD3
	int		numlumps;		// num files
	int		infotableofs;	// LUT offset
} dwadinfo_t;

/*
========================================================================

.WAD struct	(half-Life textures)

========================================================================
*/
typedef struct
{
	int		filepos;		// file offset in WAD
	int		disksize;		// compressed or uncompressed
	int		size;		// uncompressed
	signed char	type;		// TYP_*
	signed char	attribs;		// file attribs
	signed char	pad0;
	signed char	pad1;
	char		name[WAD3_NAMELEN];	// must be null terminated
} dlumpinfo_t;

#endif//WADFILE_H
