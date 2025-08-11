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
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ASSERT( x ) do { (void)( x ); } while( 0 )
#include "stb_image.h"

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
