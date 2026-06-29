/*
img_tga_gc.c - GameCube native TGA decode for retail Valve assets
Copyright (C) 2026 Xash3D GameCube port contributors

Decodes packed TGAs from WAD/pack at load time. Large HUD and menu images are
downscaled to the active gc_quality tier; no offline pre-bake step.
*/
#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "img_tga.h"
#include "img_tga_gc.h"

typedef struct gc_tga_decode_s
{
	const char	*name;
	const tga_t	*header;
	byte		*buf_p;
	const byte	*buf_end;
	const rgba_t	*palette;
	byte		*rgba;
	int		out_width;
	int		out_height;
	int		src_width;
	int		src_height;
	qboolean	downsample;
	qboolean	compressed;
	byte		*row_ptr;
	int		row_inc;
	uint		reflectivity[3];
} gc_tga_decode_t;

static qboolean GC_TGA_NeedByte( gc_tga_decode_t *dec, int count )
{
	return dec->buf_p + count <= dec->buf_end;
}

static qboolean GC_TGA_ReadPixel( gc_tga_decode_t *dec, byte *red, byte *green, byte *blue, byte *alpha )
{
	byte index;

	if( !GC_TGA_NeedByte( dec, 1 ))
		return false;

	switch( dec->header->image_type )
	{
	case 1:
	case 9:
		index = *dec->buf_p++;
		if( index >= dec->header->colormap_length )
			return true;
		*red = dec->palette[index][0];
		*green = dec->palette[index][1];
		*blue = dec->palette[index][2];
		*alpha = dec->palette[index][3];
		if( *alpha != 255 )
			image.flags |= IMAGE_HAS_ALPHA;
		break;
	case 2:
	case 10:
		if( !GC_TGA_NeedByte( dec, dec->header->pixel_size == 32 ? 4 : 3 ))
			return false;
		*blue = *dec->buf_p++;
		*green = *dec->buf_p++;
		*red = *dec->buf_p++;
		*alpha = 255;
		if( dec->header->pixel_size == 32 )
		{
			*alpha = *dec->buf_p++;
			if( *alpha != 255 )
				image.flags |= IMAGE_HAS_ALPHA;
		}
		break;
	case 3:
	case 11:
		*red = *green = *blue = *dec->buf_p++;
		*alpha = 255;
		if( dec->header->pixel_size == 16 )
		{
			if( !GC_TGA_NeedByte( dec, 1 ))
				return false;
			*alpha = *dec->buf_p++;
			if( *alpha != 255 )
				image.flags |= IMAGE_HAS_ALPHA;
		}
		break;
	default:
		return false;
	}

	return true;
}

static void GC_TGA_WritePixel( gc_tga_decode_t *dec, int col, int row, byte red, byte green, byte blue, byte alpha )
{
	if( dec->downsample )
	{
		Image_GCWriteRgbaSample( dec->rgba, dec->out_width, dec->out_height,
			dec->src_width, dec->src_height, col, row, red, green, blue, alpha );
		return;
	}

	*dec->row_ptr++ = red;
	*dec->row_ptr++ = green;
	*dec->row_ptr++ = blue;
	*dec->row_ptr++ = alpha;
}

static void GC_TGA_AdvanceRow( gc_tga_decode_t *dec, int *col, int *row )
{
	if( ++*col == dec->src_width )
	{
		*col = 0;
		( *row )++;
		if( !dec->downsample )
			dec->row_ptr += dec->row_inc;
	}
}

static void GC_TGA_AccumulatePixel( gc_tga_decode_t *dec, byte red, byte green, byte blue )
{
	if( red != green || green != blue )
		image.flags |= IMAGE_HAS_COLOR;

	dec->reflectivity[0] += red;
	dec->reflectivity[1] += green;
	dec->reflectivity[2] += blue;
}

static qboolean GC_TGA_SetupOutput( gc_tga_decode_t *dec )
{
	int decode_width = dec->src_width;
	int decode_height = dec->src_height;

	dec->downsample = Image_GCClampDecodeSize( dec->name, &decode_width, &decode_height );
	dec->out_width = decode_width;
	dec->out_height = decode_height;

	image.width = dec->out_width;
	image.height = dec->out_height;
	image.size = (size_t)dec->out_width * (size_t)dec->out_height * 4;

	Con_Reportf( "Xash3D GameCube: ImageLib TGA %s src=%dx%d decode=%dx%d alloc=%s\n",
		dec->name, dec->src_width, dec->src_height, dec->out_width, dec->out_height,
		Q_memprint( image.size ));

	dec->rgba = image.rgba = Mem_Malloc( host.imagepool, image.size );
	if( !dec->rgba )
		return false;

	if( dec->downsample )
		memset( dec->rgba, 0, image.size );

	if( dec->downsample )
	{
		dec->row_ptr = NULL;
		dec->row_inc = 0;
	}
	else if( !Image_CheckFlag( IL_DONTFLIP_TGA ) && ( dec->header->attributes & 0x20 ))
	{
		dec->row_ptr = dec->rgba;
		dec->row_inc = 0;
	}
	else
	{
		dec->row_ptr = dec->rgba + ( dec->src_height - 1 ) * dec->src_width * 4;
		dec->row_inc = -dec->src_width * 4 * 2;
	}

	return true;
}

static qboolean GC_TGA_DecodePixels( gc_tga_decode_t *dec )
{
	int row, col;
	byte red = 0, green = 0, blue = 0, alpha = 255;

	for( row = col = 0; row < dec->src_height; )
	{
		int packet = 0x10000;
		int read_count = 0x10000;

		if( dec->compressed )
		{
			if( !GC_TGA_NeedByte( dec, 1 ))
				return false;
			packet = *dec->buf_p++;
			if( packet & 0x80 )
				read_count = 1;
			packet = 1 + ( packet & 0x7f );
		}

		while( packet-- > 0 && row < dec->src_height )
		{
			if( read_count-- > 0 )
			{
				if( !GC_TGA_ReadPixel( dec, &red, &green, &blue, &alpha ))
					return false;
			}

			GC_TGA_AccumulatePixel( dec, red, green, blue );
			GC_TGA_WritePixel( dec, col, row, red, green, blue, alpha );
			GC_TGA_AdvanceRow( dec, &col, &row );
		}
	}

	return true;
}

qboolean Image_LoadTGA_GameCube( const char *name, const tga_t *header, byte *buf_p, const byte *buf_end, const rgba_t palette[256] )
{
	gc_tga_decode_t dec;

	if( !name || !header || !buf_p || !buf_end || buf_p > buf_end )
		return false;

	memset( &dec, 0, sizeof( dec ));
	dec.name = name;
	dec.header = header;
	dec.buf_p = buf_p;
	dec.buf_end = buf_end;
	dec.palette = palette;
	dec.src_width = header->width;
	dec.src_height = header->height;
	dec.compressed = ( header->image_type == 9 || header->image_type == 10 || header->image_type == 11 );

	if( !GC_TGA_SetupOutput( &dec ))
		return false;

	if( !GC_TGA_DecodePixels( &dec ))
		return false;

	VectorDivide( dec.reflectivity, dec.out_width * dec.out_height, image.fogParams );
	image.depth = 1;
	return true;
}
