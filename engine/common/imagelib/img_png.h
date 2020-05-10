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

enum png_colortype
{
	PNG_CT_GREY,
	PNG_CT_PALLETE = BIT(0),
	PNG_CT_RGB = BIT(1),
	PNG_CT_ALPHA = BIT(2),
	PNG_CT_RGBA = (PNG_CT_RGB|PNG_CT_ALPHA)
};

enum png_filter
{
	PNG_F_NONE,
	PNG_F_SUB,
	PNG_F_UP,
	PNG_F_AVERAGE,
	PNG_F_PAETH
};

#pragma pack( push, 1 )
typedef struct png_ihdr_s
{
	uint32_t    width;
	uint32_t    height;
	uint8_t     bitdepth;
	uint8_t     colortype;
	uint8_t     compression;
	uint8_t     filter;
	uint8_t     interlace;
} png_ihdr_t;

typedef struct png_s
{
	uint8_t     sign[8];
	uint32_t    ihdr_len;
	uint8_t     ihdr_sign[4];
	png_ihdr_t  ihdr_chunk;
	uint32_t    ihdr_crc32;
} png_t;

typedef struct png_footer_s
{
	uint32_t    idat_crc32;
	uint32_t    iend_len;
	uint8_t     iend_sign[4];
	uint32_t    iend_crc32;
} png_footer_t;
#pragma pack( pop )
#endif // IMG_PNG_H

