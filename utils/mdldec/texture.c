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
#include "img_tga.h"
#include "mdldec.h"
#include "utils.h"
#include "settings.h"
#include "texture.h"

/*
============
WriteBMP
============
*/
static void WriteBMP( FILE *fp, mstudiotexture_t *texture )
{
	int		 i;
	const byte	*p;
	byte		*palette, *pic;
	rgba_t		 rgba_palette[256];
	bmp_t		 bmp_hdr = {0,};

	bmp_hdr.id[0] = 'B';
	bmp_hdr.id[1] = 'M';
	bmp_hdr.width = texture->width;
	bmp_hdr.height = texture->height;
	bmp_hdr.planes = 1;
	bmp_hdr.bitsPerPixel = 8;
	bmp_hdr.bitmapDataSize = bmp_hdr.width * bmp_hdr.height;
	bmp_hdr.colors = 256;

	bmp_hdr.fileSize =  sizeof( bmp_hdr ) + bmp_hdr.bitmapDataSize + sizeof( rgba_palette );
	bmp_hdr.bitmapDataOffset = sizeof( bmp_hdr ) + sizeof( rgba_palette );
	bmp_hdr.bitmapHeaderSize = BI_SIZE;

	pic = (byte *)texture_hdr + texture->index;
	palette = pic + bmp_hdr.bitmapDataSize;

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

	p = pic;
	p += ( bmp_hdr.height - 1 ) * bmp_hdr.width;

	for( i = 0; i < bmp_hdr.height; i++ )
	{
		fwrite( p, bmp_hdr.width, 1, fp );
		p -= bmp_hdr.width;
	}
}

/*
============
WriteTGA
============
*/
static void WriteTGA( FILE *fp, mstudiotexture_t *texture )
{
	int              i;
	const byte      *p;
	byte            *palette, *pic;
	rgb_t		 rgb_palette[256];
	tga_t		 tga_hdr = {0,};

	tga_hdr.colormap_type = tga_hdr.image_type = 1;
	tga_hdr.colormap_length = 256;
	tga_hdr.colormap_size = 24;
	tga_hdr.pixel_size = 8;
	tga_hdr.width = texture->width;
	tga_hdr.height = texture->height;

	pic = (byte *)texture_hdr + texture->index;
	palette = pic + tga_hdr.width * tga_hdr.height;

	fwrite( &tga_hdr, sizeof( tga_hdr ), 1, fp );

	p = palette;

	for( i = 0; i < (int)tga_hdr.colormap_length; i++ )
	{
		rgb_palette[i][2] = *p++;
		rgb_palette[i][1] = *p++;
		rgb_palette[i][0] = *p++;
	}

	fwrite( rgb_palette, sizeof( rgb_palette ), 1, fp );

	p = pic;
	p += ( tga_hdr.height - 1 ) * tga_hdr.width;

	for( i = 0; i < tga_hdr.height; i++ )
	{
		fwrite( p, tga_hdr.width, 1, fp );
		p -= tga_hdr.width;
	}
}

/*
============
WriteTextures
============
*/
void WriteTextures( void )
{
	int			 i, len, namelen, emptyplace;
	FILE			*fp;
	mstudiotexture_t	*texture = (mstudiotexture_t *)( (byte *)texture_hdr + texture_hdr->textureindex );
	char			 path[MAX_SYSPATH];

	len = Q_snprintf( path, MAX_SYSPATH, ( globalsettings & SETTINGS_SEPARATETEXTURESFOLDER ) ? "%s" TEXTUREPATH : "%s", destdir );

	if( len == -1 || !MakeDirectory( path ))
	{
		LogPutS( "ERROR: Destination path is too long or write permission denied. Couldn't create directory for textures." );
		return;
	}

	emptyplace = MAX_SYSPATH - len;

	for( i = 0; i < texture_hdr->numtextures; ++i, ++texture )
	{
		namelen = Q_strncpy( &path[len], texture->name, emptyplace );

		if( emptyplace - namelen < 0 )
		{
			LogPrintf( "ERROR: Destination path is too long. Couldn't write %s.", texture->name );
			return;
		}

		fp = fopen( path, "wb" );

		if( !fp )
		{
			LogPrintf( "ERROR: Couldn't write texture file %s.", path );
			return;
		}

		// Many filesystems couldn't write files if "#" is first character in the name.
		if( texture->name[0] == '#' ) texture->name[0] = 's';

		if( !Q_stricmp( COM_FileExtension( texture->name ), "tga" ))
			WriteTGA( fp, texture );
		else
			WriteBMP( fp, texture );

		fclose( fp );

		LogPrintf( "Texture: %s.", path );
	}
}

