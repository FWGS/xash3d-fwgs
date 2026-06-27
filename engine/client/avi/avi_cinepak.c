/*
avi_cinepak.c - lightweight Cinepak decoder for retail HL1 AVIs
Ported from FFmpeg libavcodec/cinepak.c (LGPL 2.1+)
Copyright (C) 2003 The FFmpeg project
Copyright (C) 2026 Xash3D GameCube port contributors
*/
#include "avi_cinepak.h"

#define CPK_MAX_STRIPS 32
#define CPK_CLIP( v ) bound( 0, (v), 255 )

#define CPK_RL16( p ) ((uint16_t)((p)[0] | ((uint16_t)(p)[1] << 8)))
#define CPK_RL24( p ) ((uint32_t)((p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16)))
#define CPK_RL32( p ) ((uint32_t)((p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24)))

typedef byte cpk_codebook[12];

typedef struct cpk_strip_s
{
	uint16_t	id;
	uint16_t	x1, y1;
	uint16_t	x2, y2;
	cpk_codebook	v4_codebook[256];
	cpk_codebook	v1_codebook[256];
} cpk_strip_t;

typedef struct cpk_decode_s
{
	const byte	*data;
	int		size;
	int		width;
	int		height;
	int		stride;
	int		palette_video;
	int		sega_film_skip_bytes;
	byte		*rgb;
	cpk_strip_t	strips[CPK_MAX_STRIPS];
} cpk_decode_t;

static void Cinepak_DecodeCodebook( cpk_codebook *codebook, int chunk_id, int size, const byte *data )
{
	const byte *eod = data + size;
	uint32_t flag = 0, mask = 0;
	int n = ( chunk_id & 0x04 ) ? 4 : 6;
	byte *p = codebook[0];
	int i;

	for( i = 0; i < 256; i++ )
	{
		if(( chunk_id & 0x01 ) && !( mask >>= 1 ))
		{
			if(( data + 4 ) > eod )
				break;
			flag = CPK_RL32( data );
			data += 4;
			mask = 0x80000000;
		}

		if(!( chunk_id & 0x01 ) || ( flag & mask ))
		{
			int k, kk;

			if(( data + n ) > eod )
				break;

			for( k = 0; k < 4; k++ )
			{
				int r = *data++;
				for( kk = 0; kk < 3; kk++ )
					*p++ = r;
			}

			if( n == 6 )
			{
				int r, g, b, u, v;
				u = *(int8_t *)data++;
				v = *(int8_t *)data++;
				p -= 12;
				for( k = 0; k < 4; k++ )
				{
					r = *p++ + v * 2;
					g = *p++ - ( u / 2 ) - v;
					b = *p + u * 2;
					p -= 2;
					*p++ = CPK_CLIP( r );
					*p++ = CPK_CLIP( g );
					*p++ = CPK_CLIP( b );
				}
			}
		}
		else
		{
			p += 12;
		}
	}
}

static qboolean Cinepak_DecodeVectors( cpk_decode_t *s, cpk_strip_t *strip, int chunk_id, int size, const byte *data )
{
	const byte *eod = data + size;
	uint32_t flag = 0, mask = 0;
	int x, y;

	for( y = strip->y1; y < strip->y2; y += 4 )
	{
		byte *ip0, *ip1, *ip2, *ip3;

		ip0 = ip1 = ip2 = ip3 = s->rgb + strip->x1 * 3 + y * s->stride;
		if( s->height - y > 1 )
		{
			ip1 = ip0 + s->stride;
			if( s->height - y > 2 )
			{
				ip2 = ip1 + s->stride;
				if( s->height - y > 3 )
					ip3 = ip2 + s->stride;
			}
		}

		for( x = strip->x1; x < strip->x2; x += 4 )
		{
			if(( chunk_id & 0x01 ) && !( mask >>= 1 ))
			{
				if(( data + 4 ) > eod )
					return false;
				flag = CPK_RL32( data );
				data += 4;
				mask = 0x80000000;
			}

			if(!( chunk_id & 0x01 ) || ( flag & mask ))
			{
				if(!( chunk_id & 0x02 ) && !( mask >>= 1 ))
				{
					if(( data + 4 ) > eod )
						return false;
					flag = CPK_RL32( data );
					data += 4;
					mask = 0x80000000;
				}

				if(( chunk_id & 0x02 ) || !( flag & mask ))
				{
					byte *p;
					if( data >= eod )
						return false;
					p = strip->v1_codebook[*data++];
					memcpy( ip3 + 0, p + 6, 3 ); memcpy( ip3 + 3, p + 6, 3 );
					memcpy( ip2 + 0, p + 6, 3 ); memcpy( ip2 + 3, p + 6, 3 );
					memcpy( ip3 + 6, p + 9, 3 ); memcpy( ip3 + 9, p + 9, 3 );
					memcpy( ip2 + 6, p + 9, 3 ); memcpy( ip2 + 9, p + 9, 3 );
					memcpy( ip1 + 0, p + 0, 3 ); memcpy( ip1 + 3, p + 0, 3 );
					memcpy( ip0 + 0, p + 0, 3 ); memcpy( ip0 + 3, p + 0, 3 );
					memcpy( ip1 + 6, p + 3, 3 ); memcpy( ip1 + 9, p + 3, 3 );
					memcpy( ip0 + 6, p + 3, 3 ); memcpy( ip0 + 9, p + 3, 3 );
				}
				else if( flag & mask )
				{
					byte *cb0, *cb1, *cb2, *cb3;
					if(( data + 4 ) > eod )
						return false;
					cb0 = strip->v4_codebook[*data++];
					cb1 = strip->v4_codebook[*data++];
					cb2 = strip->v4_codebook[*data++];
					cb3 = strip->v4_codebook[*data++];
					memcpy( ip3 + 0, cb2 + 6, 6 );
					memcpy( ip3 + 6, cb3 + 6, 6 );
					memcpy( ip2 + 0, cb2 + 0, 6 );
					memcpy( ip2 + 6, cb3 + 0, 6 );
					memcpy( ip1 + 0, cb0 + 6, 6 );
					memcpy( ip1 + 6, cb1 + 6, 6 );
					memcpy( ip0 + 0, cb0 + 0, 6 );
					memcpy( ip0 + 6, cb1 + 0, 6 );
				}
			}

			ip0 += 12; ip1 += 12;
			ip2 += 12; ip3 += 12;
		}
	}

	return true;
}

static qboolean Cinepak_DecodeStrip( cpk_decode_t *s, cpk_strip_t *strip, const byte *data, int size )
{
	const byte *eod = data + size;
	int chunk_id, chunk_size;

	if( strip->x2 > s->width || strip->y2 > s->height ||
		strip->x1 >= strip->x2 || strip->y1 >= strip->y2 )
		return false;

	while(( data + 4 ) <= eod )
	{
		chunk_id = data[0];
		chunk_size = CPK_RL24( &data[1] ) - 4;
		if( chunk_size < 0 )
			return false;

		data += 4;
		chunk_size = (( data + chunk_size ) > eod ) ? (int)( eod - data ) : chunk_size;

		switch( chunk_id )
		{
		case 0x20: case 0x21: case 0x24: case 0x25:
			Cinepak_DecodeCodebook( strip->v4_codebook, chunk_id, chunk_size, data );
			break;
		case 0x22: case 0x23: case 0x26: case 0x27:
			Cinepak_DecodeCodebook( strip->v1_codebook, chunk_id, chunk_size, data );
			break;
		case 0x30: case 0x31: case 0x32:
			return Cinepak_DecodeVectors( s, strip, chunk_id, chunk_size, data );
		}

		data += chunk_size;
	}

	return false;
}

static qboolean Cinepak_PredecodeCheck( cpk_decode_t *s )
{
	int num_strips = CPK_RL16( &s->data[8] );
	int encoded_buf_size = CPK_RL24( &s->data[1] );

	if( s->size < encoded_buf_size )
		return false;

	if( s->sega_film_skip_bytes == -1 )
	{
		if( encoded_buf_size <= 0 )
			return false;
		if( encoded_buf_size != s->size && ( s->size % encoded_buf_size ) != 0 )
		{
			if( s->size >= 16 && s->data[10] == 0xFE && s->data[11] == 0x00 &&
				s->data[12] == 0x00 && s->data[13] == 0x06 &&
				s->data[14] == 0x00 && s->data[15] == 0x00 )
				s->sega_film_skip_bytes = 6;
			else s->sega_film_skip_bytes = 2;
		}
		else s->sega_film_skip_bytes = 0;
	}

	if( s->size < 10 + s->sega_film_skip_bytes + num_strips * 12 )
		return false;

	if( num_strips > 0 )
	{
		const byte *chunk = s->data + 10 + s->sega_film_skip_bytes;
		int strip_size = CPK_RL24( &chunk[1] );
		if( strip_size < 12 || strip_size > encoded_buf_size )
			return false;
	}

	return true;
}

static qboolean Cinepak_DecodeInternal( cpk_decode_t *s )
{
	const byte *eod = s->data + s->size;
	int i, strip_size, frame_flags, num_strips, y0 = 0;

	if( s->size < 10 )
		return false;

	frame_flags = s->data[0];
	num_strips = CPK_RL16( &s->data[8] );
	s->data += 10 + s->sega_film_skip_bytes;
	num_strips = Q_min( num_strips, CPK_MAX_STRIPS );

	for( i = 0; i < num_strips; i++ )
	{
		if(( s->data + 12 ) > eod )
			return false;

		s->strips[i].id = s->data[0];
		if(!( s->strips[i].y1 = CPK_RL16( &s->data[4] )))
			s->strips[i].y2 = ( s->strips[i].y1 = y0 ) + CPK_RL16( &s->data[8] );
		else s->strips[i].y2 = CPK_RL16( &s->data[8] );
		s->strips[i].x1 = CPK_RL16( &s->data[6] );
		s->strips[i].x2 = CPK_RL16( &s->data[10] );

		strip_size = CPK_RL24( &s->data[1] ) - 12;
		if( strip_size < 0 )
			return false;
		s->data += 12;
		strip_size = (( s->data + strip_size ) > eod ) ? (int)( eod - s->data ) : strip_size;

		if( i > 0 && !( frame_flags & 0x01 ))
		{
			memcpy( s->strips[i].v4_codebook, s->strips[i - 1].v4_codebook, sizeof( s->strips[i].v4_codebook ));
			memcpy( s->strips[i].v1_codebook, s->strips[i - 1].v1_codebook, sizeof( s->strips[i].v1_codebook ));
		}

		if( !Cinepak_DecodeStrip( s, &s->strips[i], s->data, strip_size ))
			return false;

		s->data += strip_size;
		y0 = s->strips[i].y2;
	}

	return true;
}

qboolean Cinepak_Init( cinepak_decoder_t *dec, int width, int height, poolhandle_t mempool )
{
	int aligned_w = ( width + 3 ) & ~3;
	int aligned_h = ( height + 3 ) & ~3;

	if( !dec || aligned_w <= 0 || aligned_h <= 0 )
		return false;

	memset( dec, 0, sizeof( *dec ));
	dec->width = aligned_w;
	dec->height = aligned_h;
	dec->stride = aligned_w * 3;
	dec->palette_video = false;
	dec->sega_film_skip_bytes = -1;
	dec->rgb = Mem_Malloc( mempool, dec->stride * aligned_h );
	return dec->rgb != NULL;
}

void Cinepak_Free( cinepak_decoder_t *dec )
{
	if( !dec )
		return;
	if( dec->rgb )
		Mem_Free( dec->rgb );
	memset( dec, 0, sizeof( *dec ));
}

qboolean Cinepak_Decode( cinepak_decoder_t *dec, const byte *data, size_t size )
{
	cpk_decode_t ctx;

	if( !dec || !dec->rgb || !data || size < 10 )
		return false;

	memset( &ctx, 0, sizeof( ctx ));
	ctx.data = data;
	ctx.size = (int)size;
	ctx.width = dec->width;
	ctx.height = dec->height;
	ctx.stride = dec->stride;
	ctx.rgb = dec->rgb;
	ctx.sega_film_skip_bytes = dec->sega_film_skip_bytes;

	if( !Cinepak_PredecodeCheck( &ctx ))
		return false;

	dec->sega_film_skip_bytes = ctx.sega_film_skip_bytes;
	memset( dec->rgb, 0, dec->stride * dec->height );
	return Cinepak_DecodeInternal( &ctx );
}
