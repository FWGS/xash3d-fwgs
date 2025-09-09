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

#include "miniz.h"
#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "img_png.h"

#if defined(XASH_NO_NETWORK)
	#include "platform/stub/net_stub.h"
#elif XASH_NSWITCH
	// our ntohl is here
	#include <arpa/inet.h>
#elif !XASH_WIN32
	#include <netinet/in.h>
#endif

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
	int		ret;
	short		p, a, b, c, pa, pb, pc;
	byte		*buf_p, *pixbuf, *raw, *prior, *idat_buf = NULL, *uncompressed_buffer = NULL;
	byte		*pallete = NULL, *trns = NULL;
	uint	 	chunk_len, trns_len = 0, plte_len = 0, crc32, crc32_check, oldsize = 0, newsize = 0, rowsize;
	uint		uncompressed_size, pixel_size, pixel_count, i, y, filter_type, chunk_sign, r_alpha, g_alpha, b_alpha;
	qboolean 	has_iend_chunk = false;
	z_stream 	stream = {0};
	png_t		png_hdr;

	if( filesize < sizeof( png_hdr ) )
		return false;

	buf_p = (byte *)buffer;

	// get png header
	memcpy( &png_hdr, buffer, sizeof( png_t ) );

	// check png signature
	if( memcmp( png_hdr.sign, png_sign, sizeof( png_sign ) ) )
	{
		Con_DPrintf( S_ERROR "%s: Invalid PNG signature (%s)\n", __func__, name );
		return false;
	}

	// convert IHDR chunk length to little endian
	png_hdr.ihdr_len = ntohl( png_hdr.ihdr_len );

	// check IHDR chunk length (valid value - 13)
	if( png_hdr.ihdr_len != sizeof( png_ihdr_t ) )
	{
		Con_DPrintf( S_ERROR "%s: Invalid IHDR chunk size %u (%s)\n", __func__, png_hdr.ihdr_len, name );
		return false;
	}

	// check IHDR chunk signature
	if( memcmp( png_hdr.ihdr_sign, ihdr_sign, sizeof( ihdr_sign ) ) )
	{
		Con_DPrintf( S_ERROR "%s: IHDR chunk corrupted (%s)\n", __func__, name );
		return false;
	}

	// convert image width and height to little endian
	image.height = png_hdr.ihdr_chunk.height = ntohl( png_hdr.ihdr_chunk.height );
	image.width  = png_hdr.ihdr_chunk.width  = ntohl( png_hdr.ihdr_chunk.width );

	if( png_hdr.ihdr_chunk.height == 0 || png_hdr.ihdr_chunk.width == 0 )
	{
		Con_DPrintf( S_ERROR "%s: Invalid image size %ux%u (%s)\n", __func__, png_hdr.ihdr_chunk.width, png_hdr.ihdr_chunk.height, name );
		return false;
	}

	if( !Image_ValidSize( name ))
		return false;

	if( png_hdr.ihdr_chunk.bitdepth != 8 )
	{
		Con_DPrintf( S_WARN "%s: Only 8-bit images is supported (%s)\n", __func__, name );
		return false;
	}

	if( !( png_hdr.ihdr_chunk.colortype == PNG_CT_RGB
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_RGBA
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_GREY
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_ALPHA
	    || png_hdr.ihdr_chunk.colortype == PNG_CT_PALLETE ) )
	{
		Con_DPrintf( S_WARN "%s: Unknown color type %u (%s)\n", __func__, png_hdr.ihdr_chunk.colortype, name );
		return false;
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
	CRC32_ProcessBuffer( &crc32_check, buf_p + sizeof( png_hdr.sign ) + sizeof( png_hdr.ihdr_len ), png_hdr.ihdr_len + sizeof( png_hdr.ihdr_sign ) );
	crc32_check = CRC32_Final( crc32_check );

	// check IHDR chunk CRC
	if( ntohl( png_hdr.ihdr_crc32 ) != crc32_check )
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
		memcpy( &chunk_len, buf_p, sizeof( chunk_len ) );

		// convert chunk length to little endian
		chunk_len = ntohl( chunk_len );

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
		if( !memcmp( buf_p, trns_sign, sizeof( trns_sign ) ) )
		{
			trns = buf_p + sizeof( trns_sign );
			trns_len = chunk_len;
		}
		// find pallete for indexed image
		else if( !memcmp( buf_p, plte_sign, sizeof( plte_sign ) ) )
		{
			pallete = buf_p + sizeof( plte_sign );
			plte_len = chunk_len / 3;
		}
		// get all IDAT chunks data
		else if( !memcmp( buf_p, idat_sign, sizeof( idat_sign ) ) )
		{
			newsize = oldsize + chunk_len;
			idat_buf = (byte *)Mem_Realloc( host.imagepool, idat_buf, newsize );
			memcpy( idat_buf + oldsize, buf_p + sizeof( idat_sign ), chunk_len );
			oldsize = newsize;
		}
		else if( !memcmp( buf_p, iend_sign, sizeof( iend_sign ) ) )
			has_iend_chunk = true;

		// calculate chunk CRC
		CRC32_Init( &crc32_check );
		CRC32_ProcessBuffer( &crc32_check, buf_p, chunk_len + sizeof( idat_sign ) );
		crc32_check = CRC32_Final( crc32_check );

		// move pointer
		buf_p += sizeof( chunk_sign );
		buf_p += chunk_len;

		// get real chunk CRC
		memcpy( &crc32, buf_p, sizeof( crc32 ) );

		// check chunk CRC
		if( ntohl( crc32 ) != crc32_check )
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
	pixel_count = image.height * image.width;
	image.size = pixel_count * 4;

	if( png_hdr.ihdr_chunk.colortype & PNG_CT_RGB )
		image.flags |= IMAGE_HAS_COLOR;

	if( trns || ( png_hdr.ihdr_chunk.colortype & PNG_CT_ALPHA ) )
		image.flags |= IMAGE_HAS_ALPHA;

	image.depth = 1;

	rowsize = pixel_size * image.width;

	uncompressed_size = image.height * ( rowsize + 1 ); // +1 for filter
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

	ret = inflate( &stream, Z_NO_FLUSH );
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
				pixbuf[i] = raw[i] + ( ( pixbuf[i - pixel_size] + prior[i] ) >> 1 );
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

		for( y = 0; y < pixel_count; y++, raw += pixel_size )
		{
			*pixbuf++ = raw[0];
			*pixbuf++ = raw[0];
			*pixbuf++ = raw[0];

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
	int		 ret;
	uint		 y, outsize, pixel_size, filtered_size, idat_len;
	uint		 ihdr_len, crc32, rowsize, big_idat_len;
	byte		*in, *buffer, *out, *filtered_buffer, *rowend;
	z_stream 	 stream = {0};
	png_t		 png_hdr;
	png_footer_t	 png_ftr;

	if( FS_FileExists( name, false ) && !Image_CheckFlag( IL_ALLOW_OVERWRITE ))
		return false; // already existed

	// bogus parameter check
	if( !pix->buffer )
		return false;

	// get image description
	switch( pix->type )
	{
	case PF_BGR_24:
	case PF_RGB_24: pixel_size = 3; break;
	case PF_BGRA_32:
	case PF_RGBA_32: pixel_size = 4; break;
	default:
		return false;
	}

	rowsize = pix->width * pixel_size;

	// get filtered image size
	filtered_size = ( rowsize + 1 ) * pix->height;

	out = filtered_buffer = Mem_Malloc( host.imagepool, filtered_size );

	// apply adaptive filter to image
	switch( pix->type )
	{
	case PF_RGB_24:
	case PF_RGBA_32:
		for( y = 0; y < pix->height; y++ )
		{
			in = pix->buffer + y * pix->width * pixel_size;
			*out++ = PNG_F_NONE;
			rowend = in + rowsize;
			for( ; in < rowend; in += pixel_size )
			{
				*out++ = in[0];
				*out++ = in[1];
				*out++ = in[2];
				if( pix->flags & IMAGE_HAS_ALPHA )
					*out++ = in[3];
			}
		}
		break;
	case PF_BGR_24:
	case PF_BGRA_32:
		for( y = 0; y < pix->height; y++ )
		{
			in = pix->buffer + y * pix->width * pixel_size;
			*out++ = PNG_F_NONE;
			rowend = in + rowsize;
			for( ; in < rowend; in += pixel_size )
			{
				*out++ = in[2];
				*out++ = in[1];
				*out++ = in[0];
				if( pix->flags & IMAGE_HAS_ALPHA )
					*out++ = in[3];
			}
		}
		break;
	}



	// get IHDR chunk length
	ihdr_len = sizeof( png_ihdr_t );

	// predict IDAT chunk length
	idat_len = deflateBound( NULL, filtered_size );

	// calculate PNG filesize
	outsize = sizeof( png_t );
	outsize += sizeof( idat_len );
	outsize += sizeof( idat_sign );
	outsize += idat_len;
	outsize += sizeof( png_footer_t );

	// write PNG header
	memcpy( png_hdr.sign, png_sign, sizeof( png_sign ) );

	// write IHDR chunk length
	png_hdr.ihdr_len = htonl( ihdr_len );

	// write IHDR chunk signature
	memcpy( png_hdr.ihdr_sign, ihdr_sign, sizeof( ihdr_sign ) );

	// write image width
	png_hdr.ihdr_chunk.width = htonl( pix->width );

	// write image height
	png_hdr.ihdr_chunk.height = htonl( pix->height );

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
	CRC32_ProcessBuffer( &crc32, &png_hdr.ihdr_sign, ihdr_len + sizeof( ihdr_sign ) );
	crc32 = CRC32_Final( crc32 );

	// write IHDR chunk CRC
	png_hdr.ihdr_crc32 = htonl( crc32 );

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

	ret = deflate( &stream, Z_FINISH );
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

	memcpy( out, &png_hdr, sizeof( png_t ) );

	out += sizeof( png_t );

	// convert IDAT chunk length to big endian
	big_idat_len = htonl( idat_len );

	// write IDAT chunk length
	memcpy( out, &big_idat_len, sizeof( idat_len ) );

	out += sizeof( idat_len );

	// write IDAT chunk signature
	memcpy( out, idat_sign, sizeof( idat_sign ) );

	// calculate IDAT chunk CRC
	CRC32_Init( &crc32 );
	CRC32_ProcessBuffer( &crc32, out, idat_len + sizeof( idat_sign ) );
	crc32 = CRC32_Final( crc32 );

	out += sizeof( idat_sign );
	out += idat_len;

	// write IDAT chunk CRC
	png_ftr.idat_crc32 = htonl( crc32 );

	// write IEND chunk length
	png_ftr.iend_len = 0;

	// write IEND chunk signature
	memcpy( png_ftr.iend_sign, iend_sign, sizeof( iend_sign ) );

	// write IEND chunk CRC
	png_ftr.iend_crc32 = htonl( iend_crc32 );

	// write PNG footer to buffer
	memcpy( out, &png_ftr, sizeof( png_ftr ) );

	FS_WriteFile( name, buffer, outsize );

	Mem_Free( buffer );
	return true;
}
