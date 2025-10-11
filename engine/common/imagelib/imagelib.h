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
	const char *ext;
	qboolean (*loadfunc)( const char *name, const byte *buffer, fs_offset_t filesize );
	image_hint_t hint;
} loadpixformat_t;

typedef struct saveformat_s
{
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

	int			hint;		// hint for some loaders
	byte			*tempbuffer;	// for convert operations
	int			cmd_flags;	// global imglib flags
	int			force_flags;	// override cmd_flags
	qboolean			custom_palette;	// custom palette was installed
} imglib_t;

// imagelib definitions
#define IMAGE_MAXWIDTH	8192
#define IMAGE_MAXHEIGHT	8192
#define LUMP_MAXWIDTH	1024	// WorldCraft limits
#define LUMP_MAXHEIGHT	1024
#define PLDECAL_MAXWIDTH  768 // total of ~2mb uncompressed rgba data
#define PLDECAL_MAXHEIGHT 768
#define IMAGE_GRADIENT_DECAL (1<<10) // TYP_PALETTE lump in WAD

enum
{
	LUMP_NORMAL = 0, // no alpha
	LUMP_MASKED,     // 1-bit alpha channel masked texture
	LUMP_GRADIENT,   // gradient image (decals)
	LUMP_EXTENDED,   // bmp images have extened palette with alpha-channel
	LUMP_HALFLIFE,   // get predefined half-life palette
	LUMP_QUAKE1,     // get predefined quake palette
	LUMP_TEXGAMMA,   // apply texgamma on top of palette, for half-life mips
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
size_t Image_ComputeSize( int type, int width, int height, int depth );
void Image_GenerateMipmaps( const byte *source, int width, int height, byte *mip1, byte *mip2, byte *mip3 );

//
// formats load
//
qboolean Image_LoadMIP( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadMDL( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadSPR( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadTGA( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadBMP( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadPNG( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadDDS( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadFNT( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadLMP( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadPAL( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadKTX2( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Image_LoadWAD( const char *name, const byte *buffer, fs_offset_t filesize );

//
// formats save
//
qboolean Image_SaveTGA( const char *name, rgbdata_t *pix );
qboolean Image_SaveBMP( const char *name, rgbdata_t *pix );
qboolean Image_SavePNG( const char *name, rgbdata_t *pix );
qboolean Image_SaveWAD( const char *name, rgbdata_t *pix );

//
// img_quant.c
//
rgbdata_t *Image_Quantize( rgbdata_t *pic );

//
// img_utils.c
//
void Image_Reset( void );
byte *Image_Copy( size_t size );
void Image_CopyParms( rgbdata_t *src );
qboolean Image_ValidSize( const char *name );
qboolean Image_LumpValidSize( const char *name );
qboolean Image_CheckFlag( int bit );

#endif//IMAGELIB_H
