/*
texture.c - texture writer
Copyright (C) 2020 Andrey Akhmichin

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "const.h"
#include "crtlib.h"
#include "studio.h"
#include "img_bmp.h"
#include "mdldec.h"
#include "texture.h"

/*
============
WriteBMP
============
*/
static void WriteBMP( mstudiotexture_t *texture )
{
	int		 i;
	FILE		*fp;
	const byte	*p;
	byte		*palette, *pic, *buf;
	char		 filename[MAX_SYSPATH], texturename[64];
	rgba_t		 rgba_palette[256];
	bmp_t		 bmp_hdr = {0,};
	size_t		 texture_size, len;

	COM_FileBase( texture->name, texturename );
	len = Q_snprintf( filename, MAX_SYSPATH, "%s%s.bmp", destdir, texturename );

	if( len >= MAX_SYSPATH )
	{
		fprintf( stderr, "ERROR: Destination path is too long. Can't write %s.bmp\n", texturename );
		return;
	}

	fp = fopen( filename, "wb" );

	if( !fp )
	{
		fprintf( stderr, "ERROR: Can't write texture file %s\n", filename );
		return;
	}

	texture_size = texture->height * texture->width;
	pic = (byte *)texture_hdr + texture->index;	
	palette = pic + texture_size;

	bmp_hdr.id[0] = 'B';
	bmp_hdr.id[1] = 'M';
	bmp_hdr.width = texture->width;
	bmp_hdr.height = texture->height;
	bmp_hdr.planes = 1;
	bmp_hdr.bitsPerPixel = 8;
	bmp_hdr.bitmapDataSize = texture_size;
	bmp_hdr.colors = 256;

	bmp_hdr.fileSize =  sizeof( bmp_hdr ) + texture_size + sizeof( rgba_palette );
	bmp_hdr.bitmapDataOffset = sizeof( bmp_hdr ) + sizeof( rgba_palette );
	bmp_hdr.bitmapHeaderSize = BI_SIZE;

	fwrite( &bmp_hdr, sizeof( bmp_hdr ), 1, fp );

	p = palette;

	for( i = 0; i < (int)bmp_hdr.colors; i++ )
	{
		rgba_palette[i][2] = *p++;
		rgba_palette[i][1] = *p++;
		rgba_palette[i][0] = *p++;
		rgba_palette[i][3] = 0;
	}

	fwrite( rgba_palette, sizeof( rgba_palette ), 1, fp );

	buf = malloc( texture_size );

	p = pic;
	p += ( bmp_hdr.height - 1 ) * bmp_hdr.width;

	for( i = 0; i < bmp_hdr.height; i++ )
	{
		memcpy( buf + bmp_hdr.width * i, p, bmp_hdr.width );
		p -= bmp_hdr.width;
	}

	fwrite( buf, texture_size, 1, fp );

	fclose( fp );

	free( buf );

	printf( "Texture: %s\n", filename );
}

/*
============
WriteTextures
============
*/
void WriteTextures( void )
{
	int			 i;
	mstudiotexture_t	*texture;

	for( i = 0; i < texture_hdr->numtextures; i++ )
	{
		texture = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex ) + i;

		WriteBMP( texture );
	}
}

