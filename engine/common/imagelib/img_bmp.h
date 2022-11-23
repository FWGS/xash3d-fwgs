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

#define BI_FILE_HEADER_SIZE 14
#define BI_SIZE 40 // size of bitmap info header.
#if !defined(BI_RGB)
#define BI_RGB		  0	// uncompressed RGB bitmap (defined in wingdi.h)
#define BI_RLE8       1
#define BI_RLE4       2
#define BI_BITFIELDS  3
#define BI_JPEG       4
#define BI_PNG        5
#endif

#pragma pack( push, 1 )
typedef struct
{
	int8_t   id[2];            // bmfh.bfType
	uint32_t fileSize;         // bmfh.bfSize
	uint32_t reserved0;        // bmfh.bfReserved1 + bmfh.bfReserved2
	uint32_t bitmapDataOffset; // bmfh.bfOffBits
	uint32_t bitmapHeaderSize; // bmih.biSize
	int32_t  width;            // bmih.biWidth
	int32_t  height;           // bmih.biHeight
	uint16_t planes;           // bmih.biPlanes
	uint16_t bitsPerPixel;     // bmih.biBitCount
	uint32_t compression;      // bmih.biCompression
	uint32_t bitmapDataSize;   // bmih.biSizeImage
	uint32_t hRes;             // bmih.biXPelsPerMeter
	uint32_t vRes;             // bmih.biYPelsPerMeter
	uint32_t colors;           // bmih.biClrUsed
	uint32_t importantColors;  // bmih.biClrImportant
} bmp_t;
#pragma pack( pop )
#endif // IMG_BMP_H

