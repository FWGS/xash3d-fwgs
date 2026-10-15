/*
img_png.c - png format load & save
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

#include "zlib_wrapper.h"
#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "img_png.h"

static const char png_sign[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
static const char ihdr_sign[] = {'I', 'H', 'D', 'R'};
static const char trns_sign[] = {'t', 'R', 'N', 'S'};
static const char plte_sign[] = {'P', 'L', 'T', 'E'};
static const char idat_sign[] = {'I', 'D', 'A', 'T'};
static const char iend_sign[] = {'I', 'E', 'N', 'D'};
static const int  iend_crc32 = 0xAE426082;

/*
=============
Image_LoadPNG
=============
*/
qboolean Image_LoadPNG( const char *name, const byte *buffer, fs_offset_t filesize )
{
	short		p, a, b, c, pa, pb, pc;
	byte		*pixbuf, *raw, *prior, *idat_buf = NULL, *uncompressed_buffer = NULL;
	byte		*pallete = NULL, *trns = NULL;
	uint	 	chunk_len, trns_len = 0, plte_len = 0, crc32, crc32_check, oldsize = 0, newsize = 0;
	uint		pixel_size, i, y, filter_type, chunk_sign, r_alpha, g_alpha, b_alpha, grey_scale;
	qboolean 	has_iend_chunk = false;
	z_stream 	stream = {0};
	png_t		png_hdr;

	if( filesize < sizeof( png_hdr ))
		return false;

	byte *buf_p = (byte *)buffer;

	// get png header
	memcpy( &png_hdr, buffer, sizeof( png_t ));

	// check png signature
	if( memcmp( png_hdr.sign, png_sign, sizeof( png_sign )))
	{
		Con_DPrintf( S_ERROR "%s: Invalid PNG signature (%s)\n", __func__, name );
		return false;
	}

	// convert IHDR chunk length to little endian
	png_hdr.ihdr_len = BigLong( png_hdr.ihdr_len );

	// check IHDR chunk length (valid value - 13)
	if( png_hdr.ihdr_len != sizeof( png_ihdr_t ))
	{
		Con_DPrintf( S_ERROR "%s: Invalid IHDR chunk size %u (%s)\n", __func__, png_hdr.ihdr_len, name );
		return false;
	}

	// check IHDR chunk signature
	if( memcmp( png_hdr.ihdr_sign, ihdr_sign, sizeof( ihdr_sign )))
	{
		Con_DPrintf( S_ERROR "%s: IHDR chunk corrupted (%s)\n", __func__, name );
		return false;
	}

	// convert image width and height to little endian
	image.height = png_hdr.ihdr_chunk.height = BigLong( png_hdr.ihdr_chunk.height );
	image.width  = png_hdr.ihdr_chunk.width  = BigLong( png_hdr.ihdr_chunk.width );

	if( png_hdr.ihdr_chunk.height == 0 || png_hdr.ihdr_chunk.width == 0 )
	{
		Con_DPrintf( S_ERROR "%s: Invalid image size %ux%u (%s)\n", __func__, png_hdr.ihdr_chunk.width, png_hdr.ihdr_chunk.height, name );
		return false;
	}

	if( !Image_ValidSize( name ))
		return false;

	if( !( png_hdr.ihdr_chunk.colortype == PNG_CT_RGB
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_RGBA
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_GREY
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_ALPHA
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_PALLETE ))
	{
		Con_DPrintf( S_WARN "%s: Unknown color type %u (%s)\n", __func__, png_hdr.ihdr_chunk.colortype, name );
		return false;
	}

	if( png_hdr.ihdr_chunk.bitdepth != 8 )
	{
		// 1, 2 and 4 bits per sample are only allowed for greyscale and indexed images
		qboolean sub_byte = png_hdr.ihdr_chunk.bitdepth == 1 || png_hdr.ihdr_chunk.bitdepth == 2 || png_hdr.ihdr_chunk.bitdepth == 4;

		if( !sub_byte || !( png_hdr.ihdr_chunk.colortype == PNG_CT_GREY || png_hdr.ihdr_chunk.colortype == PNG_CT_PALLETE ))
		{
			Con_DPrintf( S_WARN "%s: Unsupported bit depth %u for color type %u (%s)\n", __func__, png_hdr.ihdr_chunk.bitdepth, png_hdr.ihdr_chunk.colortype, name );
			return false;
		}
	}

	if( png_hdr.ihdr_chunk.compression > 0 )
	{
		Con_DPrintf( S_ERROR "%s: Unknown compression method %u (%s)\n", __func__, png_hdr.ihdr_chunk.compression, name );
		return false;
	}

	if( png_hdr.ihdr_chunk.filter > 0 )
	{
		Con_DPrintf( S_ERROR "%s: Unknown filter type %u (%s)\n", __func__, png_hdr.ihdr_chunk.filter, name );
		return false;
	}

	if( png_hdr.ihdr_chunk.interlace == 1 )
	{
		Con_DPrintf( S_WARN "%s: Adam7 Interlacing not supported (%s)\n", __func__, name );
		return false;
	}

	if( png_hdr.ihdr_chunk.interlace > 0 )
	{
		Con_DPrintf( S_ERROR "%s: Unknown interlacing type %u (%s)\n", __func__, png_hdr.ihdr_chunk.interlace, name );
		return false;
	}

	// calculate IHDR chunk CRC
	CRC32_Init( &crc32_check );
	CRC32_ProcessBuffer( &crc32_check, buf_p + sizeof( png_hdr.sign ) + sizeof( png_hdr.ihdr_len ), png_hdr.ihdr_len + sizeof( png_hdr.ihdr_sign ));
	crc32_check = CRC32_Final( crc32_check );

	// check IHDR chunk CRC
	if( BigLong( png_hdr.ihdr_crc32 ) != crc32_check )
	{
		Con_DPrintf( S_ERROR "%s: IHDR chunk has wrong CRC32 sum (%s)\n", __func__, name );
		return false;
	}

	// move pointer
	buf_p += sizeof( png_hdr );

	// find all critical chunks
	while( !has_iend_chunk && ( buf_p - buffer ) < filesize )
	{
		// get chunk length
		memcpy( &chunk_len, buf_p, sizeof( chunk_len ));

		// convert chunk length to little endian
		chunk_len = BigLong( chunk_len );

		if( chunk_len > INT_MAX )
		{
			Con_DPrintf( S_ERROR "%s: Found chunk with wrong size (%s)\n", __func__, name );
			if( idat_buf ) Mem_Free( idat_buf );
			return false;
		}

		if( chunk_len > filesize - ( buf_p - buffer ))
		{
			Con_DPrintf( S_ERROR "%s: Found chunk with size past file size (%s)\n", __func__, name );
			if( idat_buf ) Mem_Free( idat_buf );
			return false;
		}

		// move pointer
		buf_p += sizeof( chunk_len );

		// find transparency
		if( !memcmp( buf_p, trns_sign, sizeof( trns_sign )))
		{
			trns = buf_p + sizeof( trns_sign );
			trns_len = chunk_len;
		}
		// find pallete for indexed image
		else if( !memcmp( buf_p, plte_sign, sizeof( plte_sign )))
		{
			pallete = buf_p + sizeof( plte_sign );
			plte_len = chunk_len / 3;
		}
		// get all IDAT chunks data
		else if( !memcmp( buf_p, idat_sign, sizeof( idat_sign )))
		{
			newsize = oldsize + chunk_len;
			idat_buf = (byte *)Mem_Realloc( host.imagepool, idat_buf, newsize );
			memcpy( idat_buf + oldsize, buf_p + sizeof( idat_sign ), chunk_len );
			oldsize = newsize;
		}
		else if( !memcmp( buf_p, iend_sign, sizeof( iend_sign )))
			has_iend_chunk = true;

		// calculate chunk CRC
		CRC32_Init( &crc32_check );
		CRC32_ProcessBuffer( &crc32_check, buf_p, chunk_len + sizeof( idat_sign ));
		crc32_check = CRC32_Final( crc32_check );

		// move pointer
		buf_p += sizeof( chunk_sign );
		buf_p += chunk_len;

		// get real chunk CRC
		memcpy( &crc32, buf_p, sizeof( crc32 ));

		// check chunk CRC
		if( BigLong( crc32 ) != crc32_check )
		{
			Con_DPrintf( S_ERROR "%s: Found chunk with wrong CRC32 sum (%s)\n", __func__, name );
			if( idat_buf ) Mem_Free( idat_buf );
			return false;
		}

		// move pointer
		buf_p += sizeof( crc32 );
	}

	if( oldsize == 0 )
	{
		Con_DPrintf( S_ERROR "%s: Couldn't find IDAT chunks (%s)\n", __func__, name );
		return false;
	}

	if( png_hdr.ihdr_chunk.colortype == PNG_CT_PALLETE && !pallete )
	{
		Con_DPrintf( S_ERROR "%s: PLTE chunk not found (%s)\n", __func__, name );
		Mem_Free( idat_buf );
		return false;
	}

	if( !has_iend_chunk )
	{
		Con_DPrintf( S_ERROR "%s: IEND chunk not found (%s)\n", __func__, name );
		Mem_Free( idat_buf );
		return false;
	}

	if( chunk_len != 0 )
	{
		Con_DPrintf( S_ERROR "%s: IEND chunk has wrong size %u (%s)\n", __func__, chunk_len, name );
		Mem_Free( idat_buf );
		return false;
	}

	switch( png_hdr.ihdr_chunk.colortype )
	{
	case PNG_CT_GREY:
	case PNG_CT_PALLETE:
		pixel_size = 1;
		break;
	case PNG_CT_ALPHA:
		pixel_size = 2;
		break;
	case PNG_CT_RGB:
		pixel_size = 3;
		break;
	case PNG_CT_RGBA:
		pixel_size = 4;
		break;
	default:
		pixel_size = 0; // make compiler happy
		ASSERT( false );
		break;
	}

	image.type = PF_RGBA_32; // always exctracted to 32-bit buffer
	uint pixel_count = image.height * image.width;
	image.size = pixel_count * 4;

	if( png_hdr.ihdr_chunk.colortype & PNG_CT_RGB )
		image.flags |= IMAGE_HAS_COLOR;

	if( trns || ( png_hdr.ihdr_chunk.colortype & PNG_CT_ALPHA ))
		image.flags |= IMAGE_HAS_ALPHA;

	image.depth = 1;

	// sub-byte rows are padded to a whole byte, filters then work on bytes like 8-bit greyscale
	uint rowsize = ( pixel_size * image.width * png_hdr.ihdr_chunk.bitdepth + 7 ) / 8;

	uint uncompressed_size = image.height * ( rowsize + 1 ); // +1 for filter
	uncompressed_buffer = Mem_Malloc( host.imagepool, uncompressed_size );

	stream.next_in = idat_buf;
	stream.total_in = stream.avail_in = newsize;
	stream.next_out = uncompressed_buffer;
	stream.total_out = stream.avail_out = uncompressed_size;

	// uncompress image
	if( inflateInit2( &stream, MAX_WBITS ) != Z_OK )
	{
		Con_DPrintf( S_ERROR "%s: IDAT chunk decompression failed (%s)\n", __func__, name );
		Mem_Free( uncompressed_buffer );
		Mem_Free( idat_buf );
		return false;
	}

	int ret = inflate( &stream, Z_NO_FLUSH );
	inflateEnd( &stream );

	Mem_Free( idat_buf );

	if( ret != Z_OK && ret != Z_STREAM_END )
	{
		Con_DPrintf( S_ERROR "%s: IDAT chunk decompression failed (%s)\n", __func__, name );
		Mem_Free( uncompressed_buffer );
		return false;
	}

	prior = pixbuf = image.rgba = Mem_Malloc( host.imagepool, image.size );

	i = 0;

	raw = uncompressed_buffer;

	if( png_hdr.ihdr_chunk.colortype != PNG_CT_RGBA )
		prior = pixbuf = raw;

	filter_type = *raw++;

	// decode adaptive filter
	switch( filter_type )
	{
	case PNG_F_NONE:
	case PNG_F_UP:
		for( ; i < rowsize; i++ )
			pixbuf[i] = raw[i];
		break;
	case PNG_F_SUB:
	case PNG_F_PAETH:
		for( ; i < pixel_size; i++ )
			pixbuf[i] = raw[i];

		for( ; i < rowsize; i++ )
			pixbuf[i] = raw[i] + pixbuf[i - pixel_size];
		break;
	case PNG_F_AVERAGE:
		for( ; i < pixel_size; i++ )
			pixbuf[i] = raw[i];

		for( ; i < rowsize; i++ )
			pixbuf[i] = raw[i] + ( pixbuf[i - pixel_size] >> 1 );
		break;
	default:
		Con_DPrintf( S_ERROR "%s: Found unknown filter type (%s)\n", __func__, name );
		Mem_Free( uncompressed_buffer );
		Mem_Free( image.rgba );
		return false;
	}

	for( y = 1; y < image.height; y++ )
	{
		i = 0;

		pixbuf += rowsize;
		raw += rowsize;

		filter_type = *raw++;

		switch( filter_type )
		{
		case PNG_F_NONE:
			for( ; i < rowsize; i++ )
				pixbuf[i] = raw[i];
			break;
		case PNG_F_SUB:
			for( ; i < pixel_size; i++ )
				pixbuf[i] = raw[i];

			for( ; i < rowsize; i++ )
				pixbuf[i] = raw[i] + pixbuf[i - pixel_size];
			break;
		case PNG_F_UP:
			for( ; i < rowsize; i++ )
				pixbuf[i] = raw[i] + prior[i];
			break;
		case PNG_F_AVERAGE:
			for( ; i < pixel_size; i++ )
				pixbuf[i] = raw[i] + ( prior[i] >> 1 );

			for( ; i < rowsize; i++ )
				pixbuf[i] = raw[i] + (( pixbuf[i - pixel_size] + prior[i] ) >> 1 );
			break;
		case PNG_F_PAETH:
			for( ; i < pixel_size; i++ )
				pixbuf[i] = raw[i] + prior[i];

			for( ; i < rowsize; i++ )
			{
				a = pixbuf[i - pixel_size];
				b = prior[i];
				c = prior[i - pixel_size];
				p = a + b - c;
				pa = abs( p - a );
				pb = abs( p - b );
				pc = abs( p - c );

				pixbuf[i] = raw[i];

				if( pc < pa && pc < pb )
					pixbuf[i] += c;
				else if( pb < pa )
					pixbuf[i] += b;
				else
					pixbuf[i] += a;
			}
			break;
		default:
			Con_DPrintf( S_ERROR "%s: Found unknown filter type (%s)\n", __func__, name );
			Mem_Free( uncompressed_buffer );
			Mem_Free( image.rgba );
			return false;
		}

		prior = pixbuf;
	}

	// unpack sub-byte samples so every pixel takes a whole byte, first pixel is in the high bits
	if( png_hdr.ihdr_chunk.bitdepth < 8 )
	{
		const uint bitdepth = png_hdr.ihdr_chunk.bitdepth;
		const uint mask = ( 1 << bitdepth ) - 1;
		byte *unpacked = Mem_Malloc( host.imagepool, pixel_count );

		for( y = 0; y < image.height; y++ )
		{
			const byte *row = uncompressed_buffer + y * rowsize;

			for( i = 0; i < image.width; i++ )
			{
				uint bit = i * bitdepth;
				unpacked[y * image.width + i] = ( row[bit >> 3] >> ( 8 - bitdepth - ( bit & 7 ))) & mask;
			}
		}

		Mem_Free( uncompressed_buffer );
		uncompressed_buffer = unpacked;
	}

	pixbuf = image.rgba;
	raw = uncompressed_buffer;

	switch( png_hdr.ihdr_chunk.colortype )
	{
	case PNG_CT_RGB:
		if( trns )
		{
			r_alpha = trns[0] << 8 | trns[1];
			g_alpha = trns[2] << 8 | trns[3];
			b_alpha = trns[4] << 8 | trns[5];
		}

		for( y = 0; y < pixel_count; y++, raw += pixel_size )
		{
			*pixbuf++ = raw[0];
			*pixbuf++ = raw[1];
			*pixbuf++ = raw[2];

			if( trns && r_alpha == raw[0]
			    && g_alpha == raw[1]
			    && b_alpha == raw[2] )
				*pixbuf++ = 0;
			else
				*pixbuf++ = 0xFF;
		}
		break;
	case PNG_CT_GREY:
		if( trns )
			r_alpha = trns[0] << 8 | trns[1];

		// stretch sub-byte samples to the full range, transparency is checked against the original sample
		grey_scale = 255 / (( 1 << png_hdr.ihdr_chunk.bitdepth ) - 1 );

		for( y = 0; y < pixel_count; y++, raw += pixel_size )
		{
			*pixbuf++ = raw[0] * grey_scale;
			*pixbuf++ = raw[0] * grey_scale;
			*pixbuf++ = raw[0] * grey_scale;

			if( trns && r_alpha == raw[0] )
				*pixbuf++ = 0;
			else
				*pixbuf++ = 0xFF;
		}
		break;
	case PNG_CT_ALPHA:
		for( y = 0; y < pixel_count; y++, raw += pixel_size )
		{
			*pixbuf++ = raw[0];
			*pixbuf++ = raw[0];
			*pixbuf++ = raw[0];
			*pixbuf++ = raw[1];
		}
		break;
	case PNG_CT_PALLETE:
		for( y = 0; y < pixel_count; y++, raw += pixel_size )
		{
			if( raw[0] < plte_len )
			{
				*pixbuf++ = pallete[3 * raw[0] + 0];
				*pixbuf++ = pallete[3 * raw[0] + 1];
				*pixbuf++ = pallete[3 * raw[0] + 2];

				if( trns && raw[0] < trns_len )
					*pixbuf++ = trns[raw[0]];
				else
					*pixbuf++ = 0xFF;
			}
			else
			{
				*pixbuf++ = 0;
				*pixbuf++ = 0;
				*pixbuf++ = 0;
				*pixbuf++ = 0xFF;
			}
		}
		break;
	default:
		break;
	}

	Mem_Free( uncompressed_buffer );

	return true;
}

/*
=============
Image_SavePNG
=============
*/
qboolean Image_SavePNG( const char *name, rgbdata_t *pix )
{
	uint		pixel_size;
	uint		crc32;
	byte		*out;
	z_stream 	 stream = {0};
	png_t		 png_hdr;
	png_footer_t	 png_ftr;
	const qboolean be = ImageBigEndian( pix->type );

	if( FS_FileExists( name, false ) && !Image_CheckFlag( IL_ALLOW_OVERWRITE ))
		return false; // already existed

	// bogus parameter check
	if( !pix->buffer )
		return false;

	// get image description
	switch( pix->type )
	{
	case PF_BGR_24:
	case PF_RGB_24:
		pixel_size = 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		pixel_size = 4;
		break;
	default:
		return false;
	}

	uint rowsize = pix->width * pixel_size;

	// get filtered image size
	uint filtered_size = ( rowsize + 1 ) * pix->height;

	byte *filtered_buffer;
	out = filtered_buffer = Mem_Malloc( host.imagepool, filtered_size );

	// apply adaptive filter to image
	for( uint y = 0; y < pix->height; y++ )
	{
		byte *in = pix->buffer + y * pix->width * pixel_size;
		*out++ = PNG_F_NONE;
		byte *rowend = in + rowsize;
		for( ; in < rowend; in += pixel_size )
		{
			*out++ = be ? in[2] : in[0];
			*out++ = in[1];
			*out++ = be ? in[0] : in[2];

			if( pix->flags & IMAGE_HAS_ALPHA )
				*out++ = in[3];
		}
	}

	// get IHDR chunk length
	uint ihdr_len = sizeof( png_ihdr_t );

	// predict IDAT chunk length
	uint idat_len = deflateBound( NULL, filtered_size );

	// calculate PNG filesize
	uint outsize = sizeof( png_t );
	outsize += sizeof( idat_len );
	outsize += sizeof( idat_sign );
	outsize += idat_len;
	outsize += sizeof( png_footer_t );

	// write PNG header
	memcpy( png_hdr.sign, png_sign, sizeof( png_sign ));

	// write IHDR chunk length
	png_hdr.ihdr_len = BigLong( ihdr_len );

	// write IHDR chunk signature
	memcpy( png_hdr.ihdr_sign, ihdr_sign, sizeof( ihdr_sign ));

	// write image width
	png_hdr.ihdr_chunk.width = BigLong( pix->width );

	// write image height
	png_hdr.ihdr_chunk.height = BigLong( pix->height );

	// write image bitdepth
	png_hdr.ihdr_chunk.bitdepth = 8;

	// write image colortype
	png_hdr.ihdr_chunk.colortype = ( pix->flags & IMAGE_HAS_ALPHA ) ? PNG_CT_RGBA : PNG_CT_RGB; // 8 bits of alpha

	// write image comression method
	png_hdr.ihdr_chunk.compression = 0;

	// write image filter type
	png_hdr.ihdr_chunk.filter = 0;

	// write image interlacing
	png_hdr.ihdr_chunk.interlace = 0;

	// get IHDR chunk CRC
	CRC32_Init( &crc32 );
	CRC32_ProcessBuffer( &crc32, &png_hdr.ihdr_sign, ihdr_len + sizeof( ihdr_sign ));
	crc32 = CRC32_Final( crc32 );

	// write IHDR chunk CRC
	png_hdr.ihdr_crc32 = BigLong( crc32 );

	byte *buffer;
	out = buffer = (byte *)Mem_Malloc( host.imagepool, outsize );

	stream.next_in = filtered_buffer;
	stream.avail_in = filtered_size;
	stream.next_out = buffer + sizeof( png_hdr ) + sizeof( idat_len ) + sizeof( idat_sign );
	stream.avail_out = idat_len;

	// compress image
	if( deflateInit( &stream, Z_BEST_COMPRESSION ) != Z_OK )
	{
		Con_DPrintf( S_ERROR "%s: deflateInit failed (%s)\n", __func__, name );
		Mem_Free( filtered_buffer );
		Mem_Free( buffer );
		return false;
	}

	int ret = deflate( &stream, Z_FINISH );
	deflateEnd( &stream );

	Mem_Free( filtered_buffer );

	if( ret != Z_OK && ret != Z_STREAM_END )
	{
		Con_DPrintf( S_ERROR "%s: IDAT chunk compression failed (%s)\n", __func__, name );
		Mem_Free( buffer );
		return false;
	}

	// get final filesize
	outsize -= idat_len;
	idat_len = stream.total_out;
	outsize += idat_len;

	memcpy( out, &png_hdr, sizeof( png_t ));

	out += sizeof( png_t );

	// convert IDAT chunk length to big endian
	uint big_idat_len = BigLong( idat_len );

	// write IDAT chunk length
	memcpy( out, &big_idat_len, sizeof( idat_len ));

	out += sizeof( idat_len );

	// write IDAT chunk signature
	memcpy( out, idat_sign, sizeof( idat_sign ));

	// calculate IDAT chunk CRC
	CRC32_Init( &crc32 );
	CRC32_ProcessBuffer( &crc32, out, idat_len + sizeof( idat_sign ));
	crc32 = CRC32_Final( crc32 );

	out += sizeof( idat_sign );
	out += idat_len;

	// write IDAT chunk CRC
	png_ftr.idat_crc32 = BigLong( crc32 );

	// write IEND chunk length
	png_ftr.iend_len = 0;

	// write IEND chunk signature
	memcpy( png_ftr.iend_sign, iend_sign, sizeof( iend_sign ));

	// write IEND chunk CRC
	png_ftr.iend_crc32 = BigLong( iend_crc32 );

	// write PNG footer to buffer
	memcpy( out, &png_ftr, sizeof( png_ftr ));

	FS_WriteFile( name, buffer, outsize );

	Mem_Free( buffer );
	return true;
}

#if XASH_ENGINE_TESTS
#include "tests.h"

// 5x5, 4-bit indexed
static const byte png_pal4[] =
{
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x05,
	0x04, 0x03, 0x00, 0x00, 0x00, 0x7f, 0x41, 0x3b, 0xd6, 0x00, 0x00, 0x00,
	0x30, 0x50, 0x4c, 0x54, 0x45, 0x00, 0xff, 0x00, 0x11, 0xee, 0x05, 0x22,
	0xdd, 0x0a, 0x33, 0xcc, 0x0f, 0x44, 0xbb, 0x14, 0x55, 0xaa, 0x19, 0x66,
	0x99, 0x1e, 0x77, 0x88, 0x23, 0x88, 0x77, 0x28, 0x99, 0x66, 0x2d, 0xaa,
	0x55, 0x32, 0xbb, 0x44, 0x37, 0xcc, 0x33, 0x3c, 0xdd, 0x22, 0x41, 0xee,
	0x11, 0x46, 0xff, 0x00, 0x4b, 0xb5, 0xdc, 0x7d, 0xe3, 0x00, 0x00, 0x00,
	0x08, 0x74, 0x52, 0x4e, 0x53, 0x00, 0x1e, 0x3c, 0x5a, 0x78, 0x96, 0xb4,
	0xd2, 0x3e, 0x7c, 0x31, 0xfb, 0x00, 0x00, 0x00, 0x1c, 0x49, 0x44, 0x41,
	0x54, 0x78, 0xda, 0x63, 0x60, 0xce, 0x3c, 0xc0, 0x18, 0x91, 0x16, 0xc4,
	0x14, 0xea, 0x1a, 0xc0, 0x3c, 0xe7, 0x5e, 0x08, 0x4b, 0x68, 0x5a, 0x00,
	0x00, 0x3a, 0x05, 0x06, 0x0a, 0xae, 0x0c, 0x41, 0xad, 0x00, 0x00, 0x00,
	0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// 7x5, 2-bit greyscale
static const byte png_grey2[] =
{
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x05,
	0x02, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x41, 0xb1, 0xa5, 0x00, 0x00, 0x00,
	0x02, 0x74, 0x52, 0x4e, 0x53, 0x00, 0x01, 0x01, 0x94, 0xfd, 0xae, 0x00,
	0x00, 0x00, 0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xb0, 0xb4,
	0x60, 0xf4, 0xfb, 0xc7, 0xe4, 0xea, 0xc2, 0x3c, 0x5b, 0x8b, 0x25, 0xf4,
	0x3f, 0x00, 0x1c, 0xe3, 0x04, 0x6a, 0x3f, 0x9c, 0x16, 0x8d, 0x00, 0x00,
	0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// 11x5, 1-bit indexed
static const byte png_pal1[] =
{
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x05,
	0x01, 0x03, 0x00, 0x00, 0x00, 0xa9, 0x68, 0x84, 0x15, 0x00, 0x00, 0x00,
	0x06, 0x50, 0x4c, 0x54, 0x45, 0x00, 0xff, 0x00, 0x11, 0xee, 0x05, 0xc2,
	0x70, 0x9d, 0xe9, 0x00, 0x00, 0x00, 0x01, 0x74, 0x52, 0x4e, 0x53, 0x00,
	0x40, 0xe6, 0xd8, 0x66, 0x00, 0x00, 0x00, 0x17, 0x49, 0x44, 0x41, 0x54,
	0x78, 0xda, 0x63, 0x08, 0x75, 0x60, 0x5c, 0xf5, 0x8d, 0x69, 0xf5, 0x02,
	0xe6, 0x06, 0x6d, 0x96, 0xd5, 0xaf, 0x01, 0x28, 0x40, 0x05, 0xcc, 0x40,
	0x3e, 0x9f, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
	0x42, 0x60, 0x82,
};

static void Test_CheckSubBytePNG( const char *name, const byte *data, size_t size, uint width, uint height, uint bitdepth, qboolean indexed )
{
	const uint mask = ( 1 << bitdepth ) - 1;
	rgbdata_t *load = FS_LoadImage( name, data, size );
	uint mismatches = 0;

	TASSERT( load != NULL );
	if( !load )
		return;

	TASSERT( load->width == width );
	TASSERT( load->height == height );
	TASSERT( load->type == PF_RGBA_32 );
	TASSERT( FBitSet( load->flags, IMAGE_HAS_ALPHA ));
	TASSERT( load->size == width * height * 4 );

	if( load->width == width && load->height == height && load->size == width * height * 4 )
	{
		for( uint y = 0; y < height; y++ )
		{
			for( uint x = 0; x < width; x++ )
			{
				uint v = ( 3 * x + 5 * y ) & mask;
				byte expected[4];

				if( indexed )
				{
					expected[0] = v * 17;
					expected[1] = 255 - v * 17;
					expected[2] = v * 5;
					expected[3] = v < ( mask + 1 ) / 2 ? v * 30 : 255;
				}
				else
				{
					expected[0] = expected[1] = expected[2] = v * 255 / mask;
					expected[3] = v == 1 ? 0 : 255;
				}

				if( memcmp( load->buffer + ( y * width + x ) * 4, expected, sizeof( expected )))
					mismatches++;
			}
		}
	}

	TASSERT_EQi( mismatches, 0 );

	FS_FreeImage( load );
}

void Test_RunPNG( void )
{
	Image_Setup();

	Con_Printf( "Checking if we can read 1, 2 and 4-bit PNG images...\n" );
	Test_CheckSubBytePNG( "#test_pal4.png", png_pal4, sizeof( png_pal4 ), 5, 5, 4, true );
	Test_CheckSubBytePNG( "#test_grey2.png", png_grey2, sizeof( png_grey2 ), 7, 5, 2, false );
	Test_CheckSubBytePNG( "#test_pal1.png", png_pal1, sizeof( png_pal1 ), 11, 5, 1, true );
}
#endif // XASH_ENGINE_TESTS
