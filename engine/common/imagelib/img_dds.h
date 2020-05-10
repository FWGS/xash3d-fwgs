/*
img_dds.h - dds format reference
Copyright (C) 2015 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef IMG_DDS_H
#define IMG_DDS_H
/*
========================================================================

.DDS image format

========================================================================
*/
#define DDSHEADER	((' '<<24)+('S'<<16)+('D'<<8)+'D') // little-endian "DDS "

// various four-cc types
#define TYPE_DXT1	(('1'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT1"
#define TYPE_DXT2	(('2'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT2"
#define TYPE_DXT3	(('3'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT3"
#define TYPE_DXT4	(('4'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT4"
#define TYPE_DXT5	(('5'<<24)+('T'<<16)+('X'<<8)+'D') // little-endian "DXT5"
#define TYPE_ATI1	(('1'<<24)+('I'<<16)+('T'<<8)+'A') // little-endian "ATI1"
#define TYPE_ATI2	(('2'<<24)+('I'<<16)+('T'<<8)+'A') // little-endian "ATI2"
#define TYPE_RXGB	(('B'<<24)+('G'<<16)+('X'<<8)+'R') // little-endian "RXGB" doom3 normalmaps
#define TYPE_$	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'$') // little-endian "$"
#define TYPE_o	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'o') // little-endian "o"
#define TYPE_p	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'p') // little-endian "p"
#define TYPE_q	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'q') // little-endian "q"
#define TYPE_r	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'r') // little-endian "r"
#define TYPE_s	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'s') // little-endian "s"
#define TYPE_t	(('\0'<<24)+('\0'<<16)+('\0'<<8)+'t') // little-endian "t"

// dwFlags1
#define DDS_CAPS				0x00000001L
#define DDS_HEIGHT				0x00000002L
#define DDS_WIDTH				0x00000004L
#define DDS_PITCH				0x00000008L
#define DDS_PIXELFORMAT			0x00001000L
#define DDS_MIPMAPCOUNT			0x00020000L
#define DDS_LINEARSIZE			0x00080000L
#define DDS_DEPTH				0x00800000L

// dwFlags2
#define DDS_ALPHAPIXELS			0x00000001L
#define DDS_ALPHA				0x00000002L
#define DDS_FOURCC				0x00000004L
#define DDS_RGB				0x00000040L
#define DDS_RGBA				0x00000041L	// (DDS_RGB|DDS_ALPHAPIXELS)
#define DDS_LUMINANCE			0x00020000L
#define DDS_DUDV				0x00080000L

// dwCaps1
#define DDS_COMPLEX				0x00000008L
#define DDS_TEXTURE				0x00001000L
#define DDS_MIPMAP				0x00400000L

// dwCaps2
#define DDS_CUBEMAP				0x00000200L
#define DDS_CUBEMAP_POSITIVEX			0x00000400L
#define DDS_CUBEMAP_NEGATIVEX			0x00000800L
#define DDS_CUBEMAP_POSITIVEY			0x00001000L
#define DDS_CUBEMAP_NEGATIVEY			0x00002000L
#define DDS_CUBEMAP_POSITIVEZ			0x00004000L
#define DDS_CUBEMAP_NEGATIVEZ			0x00008000L
#define DDS_CUBEMAP_ALL_SIDES			0x0000FC00L
#define DDS_VOLUME				0x00200000L

typedef struct dds_pf_s
{
	uint32_t    dwSize;
	uint32_t    dwFlags;
	uint32_t    dwFourCC;
	uint32_t    dwRGBBitCount;
	uint32_t    dwRBitMask;
	uint32_t    dwGBitMask;
	uint32_t    dwBBitMask;
	uint32_t    dwABitMask;
} dds_pixf_t;

//  DDCAPS2
typedef struct dds_caps_s
{
	uint32_t    dwCaps1;
	uint32_t    dwCaps2;
	uint32_t    dwCaps3;			// currently unused
	uint32_t    dwCaps4;			// currently unused
} dds_caps_t;

typedef struct dds_s
{
	uint32_t    dwIdent;		// must matched with DDSHEADER
	uint32_t    dwSize;
	uint32_t    dwFlags;		// determines what fields are valid
	uint32_t    dwHeight;
	uint32_t    dwWidth;
	uint32_t    dwLinearSize;	// Formless late-allocated optimized surface size
	uint32_t    dwDepth;		// depth if a volume texture
	uint32_t    dwMipMapCount;	// number of mip-map levels requested
	uint32_t    dwAlphaBitDepth;	// depth of alpha buffer requested
	uint32_t    dwReserved1[10];	// reserved for future expansions
	dds_pixf_t  dsPixelFormat;
	dds_caps_t  dsCaps;
	uint32_t    dwTextureStage;
} dds_t;
#endif // IMG_DDS_H

