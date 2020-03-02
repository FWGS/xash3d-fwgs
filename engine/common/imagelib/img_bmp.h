/*
img_bmp.h - bmp format reference
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
#ifndef IMG_BMP_H
#define IMG_BMP_H
/*
========================================================================

.BMP image format

========================================================================
*/

#define BI_SIZE 40 // size of bitmap info header.
#if !defined(BI_RGB)
#define BI_RGB 0 // uncompressed RGB bitmap(defined in wingdi.h)
#endif

#pragma pack( push, 1 )
typedef struct
{
	char	id[2];		// bmfh.bfType
	dword	fileSize;		// bmfh.bfSize
	dword	reserved0;	// bmfh.bfReserved1 + bmfh.bfReserved2
	dword	bitmapDataOffset;	// bmfh.bfOffBits
	dword	bitmapHeaderSize;	// bmih.biSize
	int		width;		// bmih.biWidth
	int		height;		// bmih.biHeight
	word	planes;		// bmih.biPlanes
	word	bitsPerPixel;	// bmih.biBitCount
	dword	compression;	// bmih.biCompression
	dword	bitmapDataSize;	// bmih.biSizeImage
	dword	hRes;		// bmih.biXPelsPerMeter
	dword	vRes;		// bmih.biYPelsPerMeter
	dword	colors;		// bmih.biClrUsed
	dword	importantColors;	// bmih.biClrImportant
} bmp_t;
#pragma pack( pop )
#endif // IMG_BMP_H

