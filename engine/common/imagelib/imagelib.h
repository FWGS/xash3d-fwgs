/*
imagelib.h - engine image lib
Copyright (C) 2008 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef IMAGELIB_H
#define IMAGELIB_H

#include "common.h"

// skyorder_q2[6] = { 2, 3, 1, 0, 4, 5, }; // Quake, Half-Life skybox ordering
// skyorder_ms[6] = { 4, 5, 1, 0, 2, 3  }; // Microsoft DDS ordering (reverse)

// cubemap hints
typedef enum
{
	CB_HINT_NO = 0,

	// dds cubemap hints ( Microsoft sides order )
	CB_HINT_POSX,
	CB_HINT_NEGX,
	CB_HINT_POSZ,
	CB_HINT_NEGZ,
	CB_HINT_POSY,
	CB_HINT_NEGY,
	CB_FACECOUNT,
} side_hint_t;

typedef enum
{
	IL_HINT_NO = 0,
	IL_HINT_Q1,	// palette choosing
	IL_HINT_HL,
} image_hint_t;

typedef struct loadformat_s
{
	const char *formatstring;
	const char *ext;
	qboolean (*loadfunc)( const char *name, const byte *buffer, size_t filesize );
	image_hint_t hint;
} loadpixformat_t;

typedef struct saveformat_s
{
	const char *formatstring;
	const char *ext;
	qboolean (*savefunc)( const char *name, rgbdata_t *pix );
} savepixformat_t;

typedef struct imglib_s
{
	const loadpixformat_t	*loadformats;
	const savepixformat_t	*saveformats;

	// current 2d image state
	word			width;
	word			height;
	word			depth;
	byte			num_mips;		// mipmap count
	word			encode;		// custom encode type
	uint			type;		// main type switcher
	uint			flags;		// additional image flags
	size_t			size;		// image rgba size (for bounds checking)
	uint			ptr;		// safe image pointer
	int			bpp;		// PFDesc[type].bpp
	byte			*rgba;		// image pointer (see image_type for details)

	// current cubemap state
	int			source_width;	// locked cubemap dims (all wrong sides will be automatically resampled)
	int			source_height;
	uint			source_type;	// shared image type for all mipmaps or cubemap sides
	int			num_sides;	// how much sides is loaded 
	byte			*cubemap;		// cubemap pack

	// indexed images state
	uint			*d_currentpal;	// installed version of internal palette
	int			d_rendermode;	// palette rendermode
	byte			*palette;		// palette pointer

	// global parms
	rgba_t			fogParams;	// some water textures has info about underwater fog

	image_hint_t		hint;		// hint for some loaders
	byte			*tempbuffer;	// for convert operations
	int			cmd_flags;	// global imglib flags
	int			force_flags;	// override cmd_flags
	qboolean			custom_palette;	// custom palette was installed
} imglib_t;

/*
========================================================================

.BMP image format

========================================================================
*/
#pragma pack( 1 )
typedef struct
{
	char	id[2];		// bmfh.bfType
	dword	fileSize;		// bmfh.bfSize
	dword	reserved0;	// bmfh.bfReserved1 + bmfh.bfReserved2
	dword	bitmapDataOffset;	// bmfh.bfOffBits
	dword	bitmapHeaderSize;	// bmih.biSize
	int	width;		// bmih.biWidth
	int	height;		// bmih.biHeight
	word	planes;		// bmih.biPlanes
	word	bitsPerPixel;	// bmih.biBitCount
	dword	compression;	// bmih.biCompression
	dword	bitmapDataSize;	// bmih.biSizeImage
	dword	hRes;		// bmih.biXPelsPerMeter
	dword	vRes;		// bmih.biYPelsPerMeter
	dword	colors;		// bmih.biClrUsed
	dword	importantColors;	// bmih.biClrImportant
} bmp_t;
#pragma pack( )

/*
========================================================================

.TGA image format	(Truevision Targa)

========================================================================
*/
#pragma pack( 1 )
typedef struct tga_s
{
	byte	id_length;
	byte	colormap_type;
	byte	image_type;
	word	colormap_index;
	word	colormap_length;
	byte	colormap_size;
	word	x_origin;
	word	y_origin;
	word	width;
	word	height;
	byte	pixel_size;
	byte	attributes;
} tga_t;
#pragma pack( )

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
	uint	dwSize;
	uint	dwFlags;
	uint	dwFourCC;
	uint	dwRGBBitCount;
	uint	dwRBitMask;
	uint	dwGBitMask;
	uint	dwBBitMask;
	uint	dwABitMask;
} dds_pixf_t;

//  DDCAPS2
typedef struct dds_caps_s
{
	uint	dwCaps1;
	uint	dwCaps2;
	uint	dwCaps3;			// currently unused
	uint	dwCaps4;			// currently unused
} dds_caps_t;

typedef struct dds_s
{
	uint		dwIdent;		// must matched with DDSHEADER
	uint		dwSize;
	uint		dwFlags;		// determines what fields are valid
	uint		dwHeight;
	uint		dwWidth;
	uint		dwLinearSize;	// Formless late-allocated optimized surface size
	uint		dwDepth;		// depth if a volume texture
	uint		dwMipMapCount;	// number of mip-map levels requested
	uint		dwAlphaBitDepth;	// depth of alpha buffer requested
	uint		dwReserved1[10];	// reserved for future expansions
	dds_pixf_t	dsPixelFormat;
	dds_caps_t	dsCaps;
	uint		dwTextureStage;
} dds_t;

// imagelib definitions
#define IMAGE_MAXWIDTH	8192
#define IMAGE_MAXHEIGHT	8192
#define LUMP_MAXWIDTH	1024	// WorldCraft limits
#define LUMP_MAXHEIGHT	1024

enum
{
	LUMP_NORMAL = 0,		// no alpha
	LUMP_MASKED,		// 1-bit alpha channel masked texture
	LUMP_GRADIENT,		// gradient image (decals)
	LUMP_EXTENDED,		// bmp images have extened palette with alpha-channel
	LUMP_HALFLIFE,		// get predefined half-life palette
	LUMP_QUAKE1		// get predefined quake palette
};

enum
{
	PAL_INVALID = -1,
	PAL_CUSTOM = 0,
	PAL_QUAKE1,
	PAL_HALFLIFE
};

extern imglib_t image;

byte *Image_ResampleInternal( const void *indata, int in_w, int in_h, int out_w, int out_h, int intype, qboolean *done );
byte *Image_FlipInternal( const byte *in, word *srcwidth, word *srcheight, int type, int flags );
rgbdata_t *Image_Load(const char *filename, const byte *buffer, size_t buffsize );
qboolean Image_Copy8bitRGBA( const byte *in, byte *out, int pixels );
qboolean Image_AddIndexedImageToPack( const byte *in, int width, int height );
qboolean Image_AddRGBAImageToPack( uint imageSize, const void* data );
void Image_Save( const char *filename, rgbdata_t *pix );
void Image_GetPaletteLMP( const byte *pal, int rendermode );
void Image_GetPaletteBMP( const byte *pal );
int Image_ComparePalette( const byte *pal );
void Image_FreeImage( rgbdata_t *pack );
void Image_CopyPalette24bit( void );
void Image_CopyPalette32bit( void );
void Image_SetPixelFormat( void );
void Image_GetPaletteQ1( void );
void Image_GetPaletteHL( void );

//
// formats load
//
qboolean Image_LoadMIP( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadMDL( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadSPR( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadTGA( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadBMP( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadDDS( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadFNT( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadLMP( const char *name, const byte *buffer, size_t filesize );
qboolean Image_LoadPAL( const char *name, const byte *buffer, size_t filesize );

//
// formats save
//
qboolean Image_SaveTGA( const char *name, rgbdata_t *pix );
qboolean Image_SaveBMP( const char *name, rgbdata_t *pix );

//
// img_quant.c
//
rgbdata_t *Image_Quantize( rgbdata_t *pic );

//
// img_utils.c
//
void Image_Reset( void );
rgbdata_t *ImagePack( void );
byte *Image_Copy( size_t size );
void Image_CopyParms( rgbdata_t *src );
qboolean Image_ValidSize( const char *name );
qboolean Image_LumpValidSize( const char *name );
qboolean Image_CheckFlag( int bit );

#endif//IMAGELIB_H