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

