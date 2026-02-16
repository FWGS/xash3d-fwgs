/*
img_jpeg.c - jpeg format load via stb_image
Copyright (C) 2025 FWGS Contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "common.h"

// stb_image header is vendored in public/
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBIW_STATIC
#define STBI_ASSERT( x ) do { (void)( x ); } while( 0 )
#include "stb_image.h"
#include "stb_image_write.h"

// writer context for stb_image_write
typedef struct { file_t *fp;
} stbi_fs_ctx_t;

static void stbi_fs_write( void *context, void *data, int size )
{
	stbi_fs_ctx_t *c = (stbi_fs_ctx_t *)context;
	if( c && c->fp )
		FS_Write( c->fp, data, size );
}

/*
=============
Image_LoadJPG
=============
*/
qboolean Image_LoadJPG( const char *name, const byte *buffer, fs_offset_t filesize )
{
	int     width, height, comp;
	stbi_uc *data;

	if( !buffer || filesize <= 3 )
		return false;

	// quick magic check FF D8 FF
	if( buffer[0] != 0xFF || buffer[1] != 0xD8 || buffer[2] != 0xFF )
		return false;

	// decode into 4 channels to match engine RGBA path
	data = stbi_load_from_memory((const stbi_uc *)buffer, (int)filesize, &width, &height, &comp, STBI_rgb_alpha );
	if( !data )
	{
		Con_DPrintf( S_ERROR "%s: failed to decode JPEG (%s)\n", __func__, name );
		return false;
	}

	image.width = (word)width;
	image.height = (word)height;

	if( !Image_ValidSize( name ))
	{
		stbi_image_free( data );
		return false;
	}

	image.type = PF_RGBA_32;
	image.flags |= IMAGE_HAS_COLOR;
	image.size = (size_t)width * (size_t)height * 4;
	image.rgba = (byte *)Mem_Malloc( host.imagepool, image.size );
	memcpy( image.rgba, data, image.size );

	stbi_image_free( data );
	return true;
}

/*
==============
Image_SaveJPG
==============
*/
qboolean Image_SaveJPG( const char *name, rgbdata_t *pix )
{
	file_t     *pfile = NULL;
	byte       *rgb = NULL;
	const byte *src;
	int i, x, y, pixel_size;
	qboolean   ok = false;
	stbi_fs_ctx_t ctx;

	if( FS_FileExists( name, false ) && !Image_CheckFlag( IL_ALLOW_OVERWRITE ))
		return false; // already existed

	// bogus parameter check
	if( !pix->buffer )
		return false;

	// get image description
	switch( pix->type )
	{
	case PF_RGB_24:
	case PF_BGR_24: pixel_size = 3;
		break;
	case PF_RGBA_32:
	case PF_BGRA_32: pixel_size = 4;
		break;
	case PF_LUMINANCE: pixel_size = 1;
		break;
	default:
		return false;
	}

	// JPEG doesn't support alpha, convert to RGB
	if( pixel_size == 4 || pixel_size == 1 )
	{
		rgb = (byte *)Mem_Malloc( host.imagepool, (size_t)pix->width * pix->height * 3 );
		if( !rgb )
			return false;

		src = pix->buffer;
		for( i = 0; i < pix->width * pix->height; i++ )
		{
			if( pixel_size == 4 )
			{
				if( pix->type == PF_RGBA_32 )
				{
					rgb[i * 3 + 0] = src[i * 4 + 0];
					rgb[i * 3 + 1] = src[i * 4 + 1];
					rgb[i * 3 + 2] = src[i * 4 + 2];
				}
				else // BGRA
				{
					rgb[i * 3 + 0] = src[i * 4 + 2];
					rgb[i * 3 + 1] = src[i * 4 + 1];
					rgb[i * 3 + 2] = src[i * 4 + 0];
				}
			}
			else // LUMINANCE
			{
				byte v = src[i];
				rgb[i * 3 + 0] = v;
				rgb[i * 3 + 1] = v;
				rgb[i * 3 + 2] = v;
			}
		}
		src = rgb;
		pixel_size = 3;
	}
	else if( pix->type == PF_BGR_24 )
	{
		// BGR to RGB conversion
		rgb = (byte *)Mem_Malloc( host.imagepool, (size_t)pix->width * pix->height * 3 );
		if( !rgb )
			return false;

		src = pix->buffer;
		for( i = 0; i < pix->width * pix->height; i++ )
		{
			rgb[i * 3 + 0] = src[i * 3 + 2];
			rgb[i * 3 + 1] = src[i * 3 + 1];
			rgb[i * 3 + 2] = src[i * 3 + 0];
		}
		src = rgb;
	}

	pfile = FS_Open( name, "wb", false );
	if( !pfile )
		goto end;

	ctx.fp = pfile;
	ok = stbi_write_jpg_to_func( stbi_fs_write, &ctx, pix->width, pix->height, 3, src, 90 ) != 0;

	FS_Close( pfile );

end:
	if( rgb )
		Mem_Free( rgb );
	return ok;
}
