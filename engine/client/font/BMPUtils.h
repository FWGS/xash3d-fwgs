/*
BMPUtils.h -- single-header BMP library
Copyright (C) 2018 a1batross

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
#pragma once
#ifndef FONT_BMPUTILS_H
#define FONT_BMPUTILS_H

#include "xash3d_types.h"

#define BI_FILE_HEADER_SIZE 14
#define BI_SIZE 40
typedef unsigned short bmp_word_t;

#pragma pack( push, 1 )
struct bmp_t
{
	char id[2];
	uint fileSize;
	uint reserved0;
	uint bitmapDataOffset;
	uint bitmapHeaderSize;
	uint width;
	int  height;
	bmp_word_t planes;
	bmp_word_t bitsPerPixel;
	uint compression;
	uint bitmapDataSize;
	uint hRes;
	uint vRes;
	uint colors;
	uint importantColors;
};

struct rgbquad_t
{
	byte b;
	byte g;
	byte r;
	byte reserved;
};
#pragma pack( pop )

class CBMP
{
public:
	static CBMP *LoadFile( const char *filename );

	CBMP( uint w, uint h )
	{
		bmp_t bhdr;
		const size_t cbPalBytes = 0;
		const uint pixel_size = 4;

		bhdr.id[0] = 'B';
		bhdr.id[1] = 'M';
		bhdr.width = ( w + 3 ) & ~3;
		bhdr.height = h;
		bhdr.bitmapHeaderSize = BI_SIZE;
		bhdr.bitmapDataOffset = sizeof( bmp_t ) + cbPalBytes;
		bhdr.bitmapDataSize = bhdr.width * bhdr.height * pixel_size;
		bhdr.fileSize = bhdr.bitmapDataOffset + bhdr.bitmapDataSize;
		bhdr.reserved0 = 0;
		bhdr.planes = 1;
		bhdr.bitsPerPixel = pixel_size * 8;
		bhdr.compression = 0;
		bhdr.hRes = bhdr.vRes = 0;
		bhdr.colors = 0;
		bhdr.importantColors = 0;

		fileAllocated = false;
		data = new byte[bhdr.fileSize];
		memcpy( data, &bhdr, sizeof( bhdr ));
		memset( data + bhdr.bitmapDataOffset, 0, bhdr.bitmapDataSize );
	}

	CBMP( const bmp_t *header, uint img_sz )
	{
		data = new byte[header->bitmapDataOffset + img_sz];
		fileAllocated = false;
		memcpy( data, header, header->bitmapDataOffset );
		bmp_t *hdr = GetBitmapHdr();
		hdr->bitmapDataSize = img_sz;
		hdr->fileSize = hdr->bitmapDataOffset + hdr->bitmapDataSize;
	}

	~CBMP()
	{
		if( data )
		{
			if( fileAllocated )
				Mem_Free( data );
			else
				delete[] data;
		}
	}

	void Increase( uint w, uint h )
	{
		bmp_t *hdr = GetBitmapHdr();
		bmp_t bhdr;
		const int pixel_size = 4;

		memcpy( &bhdr, hdr, sizeof( bhdr ));
		bhdr.width  = ( w + 3 ) & ~3;
		bhdr.height = h;
		bhdr.bitmapDataSize = bhdr.width * bhdr.height * pixel_size;
		bhdr.fileSize = bhdr.bitmapDataOffset + bhdr.bitmapDataSize;

		byte *newData = new byte[bhdr.fileSize];
		memcpy( newData, &bhdr, sizeof( bhdr ));
		memset( newData + bhdr.bitmapDataOffset, 0, bhdr.bitmapDataSize );

		byte *src = GetTextureData();
		byte *dst = newData + bhdr.bitmapDataOffset;
		for( int y = 0; y < hdr->height; y++ )
		{
			byte *ydst = &dst[( y + ( bhdr.height - hdr->height )) * bhdr.width * 4];
			byte *ysrc = &src[y * hdr->width * 4];
			memcpy( ydst, ysrc, 4 * hdr->width );
		}

		delete[] data;
		data = newData;
	}

	static inline void SwapBmpHdrToLE( bmp_t *hdr )
	{
		LittleLongSW( hdr->fileSize );
		LittleLongSW( hdr->reserved0 );
		LittleLongSW( hdr->bitmapDataOffset );
		LittleLongSW( hdr->bitmapHeaderSize );
		LittleLongSW( hdr->width );
		LittleLongSW( hdr->height );
		LittleShortSW( hdr->planes );
		LittleShortSW( hdr->bitsPerPixel );
		LittleLongSW( hdr->compression );
		LittleLongSW( hdr->bitmapDataSize );
		LittleLongSW( hdr->hRes );
		LittleLongSW( hdr->vRes );
		LittleLongSW( hdr->colors );
		LittleLongSW( hdr->importantColors );
	}

	inline void SwapHdrToLE()
	{
		SwapBmpHdrToLE( (bmp_t *)data );
	}

	// returns engine texture handle (int)
	int Upload( const char *name, int flags = 0 );

	inline byte *GetBitmap() { return data; }
	inline bmp_t *GetBitmapHdr() { return (bmp_t *)data; }
	inline byte *GetTextureData() { return data + GetBitmapHdr()->bitmapDataOffset; }
	inline uint GetTextureDataSize() { return GetBitmapHdr()->bitmapDataSize; }
	inline rgbquad_t *GetPaletteData()
	{
		return (rgbquad_t *)( data + BI_FILE_HEADER_SIZE + GetBitmapHdr()->bitmapHeaderSize );
	}

private:
	CBMP( bmp_t *bmpData ) : fileAllocated( true ), data( (byte *)bmpData ) {}

	bool fileAllocated;
	byte *data;
};

#endif // FONT_BMPUTILS_H
