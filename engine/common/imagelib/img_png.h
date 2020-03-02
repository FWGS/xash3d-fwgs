/*
img_png.h - png format reference
Copyright (C) 2019 Andrey Akhmichin

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef IMG_PNG_H
#define IMG_PNG_H
/*
========================================================================

.PNG image format	(Portable Network Graphics)

========================================================================
*/

enum
{
	PNG_CT_GREY,
	PNG_CT_PALLETE = BIT(0),
	PNG_CT_RGB = BIT(1),
	PNG_CT_ALPHA = BIT(2),
	PNG_CT_RGBA = (PNG_CT_RGB|PNG_CT_ALPHA)
} png_colortype;

enum
{
	PNG_F_NONE,
	PNG_F_SUB,
	PNG_F_UP,
	PNG_F_AVERAGE,
	PNG_F_PAETH
} png_filter;

#pragma pack( push, 1 )
typedef struct png_ihdr_s
{
	uint	width;
	uint	height;
	byte	bitdepth;
	byte	colortype;
	byte	compression;
	byte	filter;
	byte	interlace;	
} png_ihdr_t;

typedef struct png_s
{
	byte		sign[8];
	uint		ihdr_len;
	byte		ihdr_sign[4];
	png_ihdr_t 	ihdr_chunk;
	uint		ihdr_crc32;
} png_t;
#pragma pack( pop )

typedef struct png_footer_s
{
	uint	idat_crc32;
	uint	iend_len;
	byte	iend_sign[4];
	uint	iend_crc32;
} png_footer_t;
#endif // IMG_PNG_H

