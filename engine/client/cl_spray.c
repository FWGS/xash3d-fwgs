/*
cl_spray.c - spray conversion for GoldSrc protocol
Copyright (C) 2025 Xash3D FWGS contributors

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
#include "client.h"
#include "custom.h"
#include "wadfile.h"
#include "filesystem.h"
#include "imagelib.h"
#include "img_bmp.h"
#include "cl_spray.h"

typedef struct logo_color_t
{
	const char *name;
	byte r, g, b;
} logo_color_t;

static const logo_color_t g_logo_colors[] = {
	{ "orange", 255, 120, 24 },
	{ "blue", 24, 120, 255 },
	{ "green", 24, 200, 24 },
	{ "red", 255, 32, 32 },
	{ "white", 255, 255, 255 },
	{ "black", 0, 0, 0 },
	{ "yellow", 255, 255, 32 },
	{ "purple", 180, 32, 255 },
	{ "cyan", 32, 255, 255 },
	{ "pink", 255, 64, 180 },
};
#define LOGO_COLOR_DEFAULT ( &g_logo_colors[0] )

// find color by colorname
static const logo_color_t *LogoColor_FindByName( const char *name )
{
	int i;
	if( !name || !*name )
		return LOGO_COLOR_DEFAULT;
	for( i = 0; i < sizeof( g_logo_colors ) / sizeof( g_logo_colors[0] ); ++i )
		if( !Q_stricmp( name, g_logo_colors[i].name ))
			return &g_logo_colors[i];
	return LOGO_COLOR_DEFAULT;
}

// adjusts spray dimensions
static void CL_AdjustSprayDimensions( int *width, int *height )
{
	float aspect = (float)( *width ) / (float)( *height );
	int   h, w;
	for( h = (( *height ) / 16 ) * 16; h >= 16; h -= 16 )
	{
		w = ((int)( h * aspect ) / 16 ) * 16;

		if( w < 16 || w > *width )
			continue;

		if( w * h < SPRAY_MAX_SURFACE )
		{
			*width = w;
			*height = h;
			return;
		}
	}
	// fallback: minimal size
	*width = 16;
	*height = 16;
}

// generates mipmaps for indexed data
static void CL_GenerateMipmaps( const byte *source, int width, int height, byte *mip1, byte *mip2, byte *mip3 )
{
	const int sizes[3][2] = {
		{ width / 2, height / 2 },
		{ width / 4, height / 4 },
		{ width / 8, height / 8 }
	};
	byte      *mips[3] = { mip1, mip2, mip3 };
	int m, mw, mh, step, y, x;

	for( m = 0; m < 3; ++m )
	{
		if( !mips[m] )
			continue;
		mw = sizes[m][0];
		mh = sizes[m][1];
		step = 1 << ( m + 1 );
		for( y = 0; y < mh; ++y )
		{
			for( x = 0; x < mw; ++x )
			{
				mips[m][y * mw + x] = source[( y * step ) * width + ( x * step )];
			}
		}
	}
}

// loads and prepares the image
static rgbdata_t *CL_LoadAndPrepareImage( const char *inputFile, int *width, int *height, rgbdata_t **scaled_image_out )
{
	rgbdata_t *image = FS_LoadImage( inputFile, NULL, 0 );
	rgbdata_t *scaled_image = NULL;
	qboolean  resampled = false;
	byte      *resampled_buf = NULL;
	byte      *pix = NULL;
	int i, bpp = 0; // bytes per pixel

	if( !image )
		return NULL;

	bpp = ( image->type == PF_RGBA_32 ) ? 4 : 3;

	if( bpp )
	{
		for( i = 0; i < image->width * image->height; i++ )
		{
			pix = &image->buffer[i * bpp];
			if( bpp == 4 )
			{
				if( pix[3] <= 128 ) // transparent
				{
					pix[0] = 0;
					pix[1] = 0;
					pix[2] = 255;
					pix[3] = 255; // or 0, doesn't matter
				}
			}
			else // bpp == 3
			{
				if( pix[0] == 0 && pix[1] == 0 && pix[2] == 255 )
					pix[2] = 254;
			}
		}
	}

	*width = image->width;
	*height = image->height;
	CL_AdjustSprayDimensions( width, height );
	if( *width < 16 || *height < 16 || *width % 16 != 0 || *height % 16 != 0 )
	{
		Con_DPrintf( S_ERROR "Invalid dimensions after adjustment: %dx%d\n", *width, *height );
		FS_FreeImage( image );
		return NULL;
	}
	if( *width != image->width || *height != image->height )
	{
		resampled_buf = Image_ResampleInternal( image->buffer, image->width, image->height, *width, *height, image->type, &resampled );
		if( !resampled_buf )
		{
			Con_DPrintf( S_ERROR "Failed to create scaled image\n" );
			FS_FreeImage( image );
			return NULL;
		}
		scaled_image = Mem_Malloc( host.imagepool, sizeof( rgbdata_t ));
		scaled_image->buffer = Mem_Malloc( host.imagepool, *width * *height * 4 );
		scaled_image->width = *width;
		scaled_image->height = *height;
		scaled_image->type = image->type;
		scaled_image->size = *width * *height * 4;
		memcpy( scaled_image->buffer, resampled_buf, *width * *height * 4 );
		Con_DPrintf( S_GREEN "Created scaled image %dx%d\n", *width, *height );
	}
	*scaled_image_out = scaled_image;
	return image;
}

// write wad3 file
static qboolean CL_WriteSprayWAD3( const byte *indexed, int width, int height, const byte *palette, int lump_type )
{
	int          mip0_size = width * height;
	int          mip1_size = mip0_size / 4;
	int          mip2_size = mip0_size / 16;
	int          mip3_size = mip0_size / 64;
	byte         *mip1_data = (byte *)Mem_Malloc( cls.mempool, mip1_size );
	byte         *mip2_data = (byte *)Mem_Malloc( cls.mempool, mip2_size );
	byte         *mip3_data = (byte *)Mem_Malloc( cls.mempool, mip3_size );
	qboolean     result = false;
	file_t       *f;
	wad3header_t header;
	mip_t        miptex;
	long         palette_offset;
	short        palette_size;
	long         infotableofs;
	wad3lump_t   lump;
	int          infotableofs32;
	fs_offset_t  pad;
	byte         zero = 0;
	int          i;

	if( !mip1_data || !mip2_data || !mip3_data )
		goto cleanup;

	CL_GenerateMipmaps( indexed, width, height, mip1_data, mip2_data, mip3_data );

	memset( &miptex, 0, sizeof( mip_t ));
	strncpy( miptex.name, SPRAY_NAME, sizeof( miptex.name ) - 1 );
	miptex.width = width;
	miptex.height = height;
	miptex.offsets[0] = sizeof( mip_t );
	miptex.offsets[1] = miptex.offsets[0] + mip0_size;
	miptex.offsets[2] = miptex.offsets[1] + mip1_size;
	miptex.offsets[3] = miptex.offsets[2] + mip2_size;

	f = FS_Open( SPRAY_FILENAME, "wb", false );
	if( !f )
		goto cleanup;

	memset( &header, 0, sizeof( header ));
	memcpy( header.identification, "WAD3", 4 );
	header.numlumps = 1;

	palette_offset = miptex.offsets[3] + mip3_size;
	palette_size = SPRAY_PALETTE_SIZE;

	FS_Write( f, &header, sizeof( header ));
	FS_Write( f, &miptex, sizeof( mip_t ));
	FS_Write( f, indexed, mip0_size );
	FS_Write( f, mip1_data, mip1_size );
	FS_Write( f, mip2_data, mip2_size );
	FS_Write( f, mip3_data, mip3_size );
	FS_Write( f, &palette_size, sizeof( short ));
	FS_Write( f, palette, SPRAY_PALETTE_BYTES );

	// padding up to a multiple of 4
	pad = (( FS_Tell( f ) + 3 ) & ~3 ) - FS_Tell( f );
	for( i = 0; i < pad; ++i )
	{
		FS_Write( f, &zero, 1 );
	}

	infotableofs = FS_Tell( f );
	memset( &lump, 0, sizeof( lump ));
	lump.filepos = sizeof( wad3header_t );
	lump.disksize = (int)( palette_offset + sizeof( short ) + SPRAY_PALETTE_BYTES );
	lump.size = lump.disksize;
	lump.type = (char)lump_type;
	lump.compression = 0;
	strncpy( lump.name, SPRAY_NAME, sizeof( lump.name ) - 1 );
	FS_Write( f, &lump, sizeof( lump ));

	FS_Seek( f, offsetof( wad3header_t, infotableofs ), SEEK_SET );
	infotableofs32 = (int)infotableofs;
	FS_Write( f, &infotableofs32, sizeof( int ));

	FS_Close( f );
	result = true;

cleanup:
	if( mip1_data )
		Mem_Free( mip1_data );
	if( mip2_data )
		Mem_Free( mip2_data );
	if( mip3_data )
		Mem_Free( mip3_data );
	return result;
}

// convert BMP to spray WAD3
static qboolean CL_ConvertBMPToSprayWAD3( const char *logoname, const char *logocolor )
{
	char      spray_bmp_path[MAX_QPATH];
	rgbdata_t *image;
	int       width, height;
	rgbdata_t *scaled_image = NULL;
	byte      palette[SPRAY_PALETTE_BYTES];
	const logo_color_t *color;
	float     t;
	int       i;
	qboolean  result;

	Q_snprintf( spray_bmp_path, sizeof( spray_bmp_path ), "logos/%s.bmp", ( logoname && *logoname ) ? logoname : "lambda" );

	Image_SetForceFlags( IL_KEEP_8BIT );
	image = FS_LoadImage( spray_bmp_path, NULL, 0 );
	Image_SetForceFlags( 0 );
	if( !image || !image->palette || !( image->type == PF_INDEXED_24 || image->type == PF_INDEXED_32 ))
	{
		Con_DPrintf( S_ERROR "Failed to load indexed BMP: %s\n", spray_bmp_path );
		if( image )
			FS_FreeImage( image );
		return false;
	}

	width = image->width;
	height = image->height;
	CL_AdjustSprayDimensions( &width, &height );
	if( width < 16 || height < 16 || width % 16 != 0 || height % 16 != 0 )
	{
		Con_DPrintf( S_ERROR "Invalid BMP dimensions after adjustment: %dx%d\n", width, height );
		FS_FreeImage( image );
		return false;
	}

	if( width != image->width || height != image->height )
	{
		rgbdata_t *tmp = CL_LoadAndPrepareImage( spray_bmp_path, &width, &height, &scaled_image );
		if( !tmp )
		{
			Con_DPrintf( S_ERROR "Failed to resample BMP for spray\n" );
			FS_FreeImage( image );
			return false;
		}
		if( tmp != image )
			FS_FreeImage( tmp );
		if( scaled_image )
		{
			FS_FreeImage( image );
			image = scaled_image;
		}
	}

	// generate gradient palette
	color = LogoColor_FindByName( logocolor );
	for( i = 0; i < 256; ++i )
	{
		t = i / 255.0f;
		palette[i * 3 + 0] = (byte)( color->r * t );
		palette[i * 3 + 1] = (byte)( color->g * t );
		palette[i * 3 + 2] = (byte)( color->b * t );
	}
	palette[255 * 3 + 0] = color->r;
	palette[255 * 3 + 1] = color->g;
	palette[255 * 3 + 2] = color->b;

	result = CL_WriteSprayWAD3( image->buffer, width, height, palette, TYP_PALETTE );
	FS_FreeImage( image );
	return result;
}

// convert image to WAD3 spray or miptex
qboolean CL_ConvertImageToWAD3( const char *filename )
{
	const char *ext;
	rgbdata_t  *image = NULL, *scaled_image = NULL, *quant = NULL;
	int width = 0, height = 0;
	byte palette[SPRAY_PALETTE_BYTES];
	qboolean   result = false;
	rgbdata_t  *process_image = NULL;
	byte       *indexed;
	int pal_count;
	int i, src_idx;
	byte       r, g, b, a;

	ext = Q_strrchr( filename, '.' );
	if( ext && !Q_stricmp( ext, ".bmp" ))
	{
		const char *logoname = Cvar_VariableString( "cl_logofile" );
		const char *logocolor = Cvar_VariableString( "cl_logocolor" );
		return CL_ConvertBMPToSprayWAD3( logoname, logocolor );
	}

	// load and prepare image
	image = CL_LoadAndPrepareImage( filename, &width, &height, &scaled_image );
	if( !image )
		return false;

	// quantize image and get indexed data + palette
	process_image = scaled_image ? scaled_image : image;
	quant = Image_Quantize( process_image );
	if( !quant || !quant->buffer || !quant->palette )
	{
		Con_DPrintf( S_ERROR "Failed to quantize image\n" );
		if( scaled_image )
		{
			if( scaled_image->buffer )
				Mem_Free( scaled_image->buffer );
			Mem_Free( scaled_image );
		}
		FS_FreeImage( image );
		return false;
	}

	memcpy( palette, quant->palette, SPRAY_PALETTE_BYTES );
	pal_count = SPRAY_PALETTE_SIZE;
	for( ; pal_count < 255; ++pal_count )
	{
		palette[pal_count * 3 + 0] = 0;
		palette[pal_count * 3 + 1] = 0;
		palette[pal_count * 3 + 2] = 0;
	}
	palette[255 * 3 + 0] = 0;
	palette[255 * 3 + 1] = 0;
	palette[255 * 3 + 2] = 255;

	// replace transparent pixels with index 255
	indexed = quant->buffer;
	for( i = 0; i < width * height; ++i )
	{
		src_idx = indexed[i];
		r = quant->palette[src_idx * 3 + 0];
		g = quant->palette[src_idx * 3 + 1];
		b = quant->palette[src_idx * 3 + 2];
		if( process_image->type == PF_RGBA_32 )
		{
			a = process_image->buffer[i * 4 + 3];
			if( a <= 128 )
				indexed[i] = 255;
		}
		else if( r == 0 && g == 0 && b == 255 )
		{
			indexed[i] = 255;
		}
	}

	result = CL_WriteSprayWAD3( indexed, width, height, palette, TYP_MIPTEX );

	if( scaled_image )
	{
		if( scaled_image->buffer )
			Mem_Free( scaled_image->buffer );
		Mem_Free( scaled_image );
	}
	FS_FreeImage( image );
	return result;
}