/*
img_tga.h - tga format reference
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
#ifndef IMG_TGA_H
#define IMG_TGA_H
/*
========================================================================

.TGA image format	(Truevision Targa)

========================================================================
*/
#pragma pack( push, 1 )
typedef struct tga_s
{
	uint8_t  id_length;
	uint8_t  colormap_type;
	uint8_t  image_type;
	uint16_t colormap_index;
	uint16_t colormap_length;
	uint8_t  colormap_size;
	uint16_t x_origin;
	uint16_t y_origin;
	uint16_t width;
	uint16_t height;
	uint8_t  pixel_size;
	uint8_t  attributes;
} tga_t;
#pragma pack( pop )
#endif // IMG_TGA_H

