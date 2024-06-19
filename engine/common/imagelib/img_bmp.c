/*
img_bmp.c - bmp format load & save
Copyright (C) 2007 Uncle Mike

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
#include "img_bmp.h"

/*
=============
Image_LoadBMP
=============
*/
qboolean Image_LoadBMP( const char *name, const byte *buffer, fs_offset_t filesize )
{
	byte	*buf_p, *pixbuf;
	rgba_t	palette[256] = { 0 };
	int	i, columns, column, rows, row, bpp = 1;
	int	cbPalBytes = 0, padSize = 0, bps = 0;
	uint	reflectivity[3] = { 0, 0, 0 };
	qboolean	load_qfont = false;
	bmp_t	bhdr;
	fs_offset_t estimatedSize;

	if( filesize < sizeof( bhdr ))
	{
		Con_Reportf( S_ERROR "%s: %s have incorrect file size %li should be greater than %zu (header)\n", __func__, name, (long)filesize, sizeof( bhdr ));
		return false;
	}

	buf_p = (byte *)buffer;
	memcpy( &bhdr, buf_p, sizeof( bmp_t ));
	buf_p += BI_FILE_HEADER_SIZE + bhdr.bitmapHeaderSize;

	// bogus file header check
	if( bhdr.reserved0 != 0 ) return false;
	if( bhdr.planes != 1 ) return false;

	if( memcmp( bhdr.id, "BM", 2 ))
	{
		Con_DPrintf( S_ERROR "%s: only Windows-style BMP files supported (%s)\n", __func__, name );
		return false;
	}

	if(!( bhdr.bitmapHeaderSize == 40 || bhdr.bitmapHeaderSize == 108 || bhdr.bitmapHeaderSize == 124 ))
	{
		Con_DPrintf( S_ERROR "%s: %s have non-standard header size %i\n", __func__, name, bhdr.bitmapHeaderSize );
		return false;
	}

	// bogus info header check
	if( bhdr.fileSize != filesize )
	{
		// Sweet Half-Life issues. splash.bmp have bogus filesize
		Con_Reportf( S_WARN "%s: %s have incorrect file size %li should be %i\n", __func__, name, (long)filesize, bhdr.fileSize );
	}

	// bogus compression?  Only non-compressed supported.
	if( bhdr.compression != BI_RGB )
	{
		if( bhdr.bitsPerPixel != 32 || bhdr.compression != BI_BITFIELDS )
		{ 
			Con_DPrintf( S_ERROR "%s: only uncompressed BMP files supported (%s)\n", __func__, name );
			return false;
		}
	}

	image.width = columns = bhdr.width;
	image.height = rows = abs( bhdr.height );

	if( !Image_ValidSize( name ))
		return false;

	// special case for loading qfont (menu font)
	if( !Q_strncmp( name, "#XASH_SYSTEMFONT_001", 20 ))
	{
		// NOTE: same as system font we can use 4-bit bmps only
		// step1: move main layer into alpha-channel (give grayscale from RED channel)
		// step2: fill main layer with 255 255 255 color (white)
		// step3: ????
		// step4: PROFIT!!! (economy up to 150 kb for menu.dll final size)
		image.flags |= IMAGE_HAS_ALPHA;
		load_qfont = true;
	}

	if( bhdr.bitsPerPixel <= 8 )
	{
		// figure out how many entries are actually in the table
		if( bhdr.colors == 0 )
		{
			bhdr.colors = 256;
			cbPalBytes = ( 1 << bhdr.bitsPerPixel ) * sizeof( rgba_t );
		}
		else cbPalBytes = bhdr.colors * sizeof( rgba_t );
	}

	estimatedSize = ( buf_p - buffer ) + cbPalBytes;
	if( filesize < estimatedSize )
	{
		Con_Reportf( S_ERROR "%s: %s have incorrect file size %li should be greater than %li (palette)\n", __func__, name, (long)filesize, (long)estimatedSize );
		return false;
	}

	memcpy( palette, buf_p, cbPalBytes );

	// setup gradient alpha for player decal
	if( !Q_strncmp( name, "#logo", 5 ))
	{
		for( i = 0; i < bhdr.colors; i++ )
			palette[i][3] = i;
		image.flags |= IMAGE_HAS_ALPHA;
	}

	if( Image_CheckFlag( IL_OVERVIEW ) && bhdr.bitsPerPixel == 8 )
	{
		// convert green background into alpha-layer, make opacity for all other entries
		for( i = 0; i < bhdr.colors; i++ )
		{
			if( palette[i][0] == 0 && palette[i][1] == 255 && palette[i][2] == 0 )
			{
				palette[i][0] = palette[i][1] = palette[i][2] = palette[i][3] = 0;
				image.flags |= IMAGE_HAS_ALPHA;
			}
			else palette[i][3] = 255;
		}
	}

	if( Image_CheckFlag( IL_KEEP_8BIT ) && bhdr.bitsPerPixel == 8 )
	{
		pixbuf = image.palette = Mem_Malloc( host.imagepool, 1024 );

		// bmp have a reversed palette colors
		for( i = 0; i < bhdr.colors; i++ )
		{
			*pixbuf++ = palette[i][2];
			*pixbuf++ = palette[i][1];
			*pixbuf++ = palette[i][0];
			*pixbuf++ = palette[i][3];
		}
		image.type = PF_INDEXED_32; // 32 bit palette
	}
	else
	{
		image.palette = NULL;
		image.type = PF_RGBA_32;
		bpp = 4;
	}

	buf_p += cbPalBytes;
	bps = image.width * (bhdr.bitsPerPixel >> 3);

	switch( bhdr.bitsPerPixel )
	{
	case 1:
		padSize = (( 32 - ( bhdr.width % 32 )) / 8 ) % 4;
		break;
	case 4:
		padSize = (( 8 - ( bhdr.width % 8 )) / 2 ) % 4;
		break;
	case 16:
		padSize = ( 4 - ( image.width * 2 % 4 )) % 4;
		break;
	case 8:
	case 24:
		padSize = ( 4 - ( bps % 4 )) % 4;
		break;
	}

	estimatedSize = ( buf_p - buffer ) + image.width * image.height * ( bhdr.bitsPerPixel >> 3 );
	if( filesize < estimatedSize )
	{
		if( image.palette )
		{
			Mem_Free( image.palette );
			image.palette = NULL;
		}

		Con_Reportf( S_ERROR "%s: %s have incorrect file size %li should be greater than %li (pixels)\n", __func__, name, (long)filesize, (long)estimatedSize );
		return false;
	}

	image.depth = 1;
	image.size = image.width * image.height * bpp;
	image.rgba = Mem_Malloc( host.imagepool, image.size );

	for( row = rows - 1; row >= 0; row-- )
	{
		pixbuf = image.rgba + (row * columns * bpp);

		for( column = 0; column < columns; column++ )
		{
			byte	red, green, blue, alpha;
			word	shortPixel;
			int	c, k, palIndex;

			switch( bhdr.bitsPerPixel )
			{
			case 1:
				alpha = *buf_p++;
				column--;	// ingnore main iterations
				for( c = 0, k = 128; c < 8; c++, k >>= 1 )
				{
					red = green = blue = (!!(alpha & k) == 1 ? 0xFF : 0x00);
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 0x00;
					if( ++column == columns )
						break;
				}
				break;
			case 4:
				alpha = *buf_p++;
				palIndex = alpha >> 4;
				if( load_qfont )
				{
					*pixbuf++ = red = 255;
					*pixbuf++ = green = 255;
					*pixbuf++ = blue = 255;
					*pixbuf++ = palette[palIndex][2];
				}
				else
				{
					*pixbuf++ = red = palette[palIndex][2];
					*pixbuf++ = green = palette[palIndex][1];
					*pixbuf++ = blue = palette[palIndex][0];
					*pixbuf++ = palette[palIndex][3];
				}
				if( ++column == columns ) break;
				palIndex = alpha & 0x0F;
				if( load_qfont )
				{
					*pixbuf++ = red = 255;
					*pixbuf++ = green = 255;
					*pixbuf++ = blue = 255;
					*pixbuf++ = palette[palIndex][2];
				}
				else
				{
					*pixbuf++ = red = palette[palIndex][2];
					*pixbuf++ = green = palette[palIndex][1];
					*pixbuf++ = blue = palette[palIndex][0];
					*pixbuf++ = palette[palIndex][3];
				}
				break;
			case 8:
				palIndex = *buf_p++;
				red = palette[palIndex][2];
				green = palette[palIndex][1];
				blue = palette[palIndex][0];
				alpha = palette[palIndex][3];

				if( Image_CheckFlag( IL_KEEP_8BIT ))
				{
					*pixbuf++ = palIndex;
				}
				else
				{
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alpha;
				}
				break;
			case 16:
				shortPixel = *(word *)buf_p, buf_p += 2;
				*pixbuf++ = blue = (shortPixel & ( 31 << 10 )) >> 7;
				*pixbuf++ = green = (shortPixel & ( 31 << 5 )) >> 2;
				*pixbuf++ = red = (shortPixel & ( 31 )) << 3;
				*pixbuf++ = 0xff;
				break;
			case 24:
				blue = *buf_p++;
				green = *buf_p++;
				red = *buf_p++;
				*pixbuf++ = red;
				*pixbuf++ = green;
				*pixbuf++ = blue;
				*pixbuf++ = 0xFF;
				break;
			case 32:
				blue = *buf_p++;
				green = *buf_p++;
				red = *buf_p++;
				alpha = *buf_p++;
				*pixbuf++ = red;
				*pixbuf++ = green;
				*pixbuf++ = blue;
				*pixbuf++ = alpha;
				if( alpha != 255 ) image.flags |= IMAGE_HAS_ALPHA;
				break;
			default:
				Mem_Free( image.palette );
				Mem_Free( image.rgba );
				return false;
			}

			if( red != green || green != blue )
				image.flags |= IMAGE_HAS_COLOR;

			reflectivity[0] += red;
			reflectivity[1] += green;
			reflectivity[2] += blue;
		}
		buf_p += padSize;	// actual only for 4-bit bmps
	}

	VectorDivide( reflectivity, ( image.width * image.height ), image.fogParams );
	if( image.palette )
		Image_GetPaletteBMP( image.palette );

	return true;
}

qboolean Image_SaveBMP( const char *name, rgbdata_t *pix )
{
	file_t		*pfile = NULL;
	size_t		total_size, cur_size;
	rgba_t		rgrgbPalette[256];
	dword		cbBmpBits;
	byte		*clipbuf = NULL;
	byte		*pb, *pbBmpBits;
	dword		cbPalBytes;
	dword		biTrueWidth;
	int		pixel_size;
	int		i, x, y;
	bmp_t	hdr;

	if( FS_FileExists( name, false ) && !Image_CheckFlag( IL_ALLOW_OVERWRITE ) )
		return false; // already existed

	// bogus parameter check
	if( !pix->buffer )
		return false;

	// get image description
	switch( pix->type )
	{
	case PF_INDEXED_24:
	case PF_INDEXED_32:
		pixel_size = 1;
		break;
	case PF_RGB_24:
		pixel_size = 3;
		break;
	case PF_RGBA_32:
		pixel_size = 4;
		break;
	default:
		return false;
	}

	pfile = FS_Open( name, "wb", false );
	if( !pfile ) return false;

	// NOTE: align transparency column will sucessfully removed
	// after create sprite or lump image, it's just standard requiriments
	biTrueWidth = ((pix->width + 3) & ~3);
	cbBmpBits = biTrueWidth * pix->height * pixel_size;
	cbPalBytes = ( pixel_size == 1 ) ? 256 * sizeof( rgba_t ) : 0;

	// Bogus file header check
	hdr.id[0] = 'B';
	hdr.id[1] = 'M';
	hdr.fileSize =  sizeof( hdr ) + cbBmpBits + cbPalBytes;
	hdr.reserved0 = 0;
	hdr.bitmapDataOffset = sizeof( hdr ) + cbPalBytes;
	hdr.bitmapHeaderSize = BI_SIZE;
	hdr.width = biTrueWidth;
	hdr.height = pix->height;
	hdr.planes = 1;
	hdr.bitsPerPixel = pixel_size * 8;
	hdr.compression = BI_RGB;
	hdr.bitmapDataSize = cbBmpBits;
	hdr.hRes = 0;
	hdr.vRes = 0;
	hdr.colors = ( pixel_size == 1 ) ? 256 : 0;
	hdr.importantColors = 0;

	FS_Write( pfile, &hdr, sizeof( bmp_t ));

	pbBmpBits = Mem_Malloc( host.imagepool, cbBmpBits );

	if( pixel_size == 1 )
	{
		pb = pix->palette;

		// copy over used entries
		for( i = 0; i < (int)hdr.colors; i++ )
		{
			rgrgbPalette[i][2] = *pb++;
			rgrgbPalette[i][1] = *pb++;
			rgrgbPalette[i][0] = *pb++;

			// bmp feature - can store 32-bit palette if present
			// some viewers e.g. fimg.exe can show alpha-chanell for it
			if( pix->type == PF_INDEXED_32 )
				rgrgbPalette[i][3] = *pb++;
			else rgrgbPalette[i][3] = 0;
		}

		// write palette
		FS_Write( pfile, rgrgbPalette, cbPalBytes );
	}

	pb = pix->buffer;

	for( y = 0; y < hdr.height; y++ )
	{
		i = (hdr.height - 1 - y ) * (hdr.width);

		for( x = 0; x < pix->width; x++ )
		{
			if( pixel_size == 1 )
			{
				// 8-bit
				pbBmpBits[i] = pb[x];
			}
			else
			{
				// 24 bit
				pbBmpBits[i*pixel_size+0] = pb[x*pixel_size+2];
				pbBmpBits[i*pixel_size+1] = pb[x*pixel_size+1];
				pbBmpBits[i*pixel_size+2] = pb[x*pixel_size+0];
			}

			if( pixel_size == 4 ) // write alpha channel
				pbBmpBits[i*pixel_size+3] = pb[x*pixel_size+3];
			i++;
		}

		pb += pix->width * pixel_size;
	}

	// write bitmap bits (remainder of file)
	FS_Write( pfile, pbBmpBits, cbBmpBits );
	FS_Close( pfile );

	Mem_Free( pbBmpBits );

	return true;
}
