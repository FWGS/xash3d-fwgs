/*
synth.c - compact version of famous library mpg123
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "mpg123.h"
#include "sample.h"

#define BACKPEDAL	0x10	// we use autoincrement and thus need this re-adjustment for window/b0.
#define BLOCK	0x40	// one decoding block is 64 samples.

#define WRITE_SHORT_SAMPLE( samples, sum, clip ) \
	if(( sum ) > 32767.0f ) { *(samples) = 0x7fff; (clip)++; } \
	else if(( sum ) < -32768.0f ) { *(samples) = -0x8000; (clip)++; } \
	else { *(samples) = REAL_TO_SHORT( sum ); }

// main synth function, uses the plain dct64
static int synth_1to1( float *bandPtr, int channel, mpg123_handle_t *fr, int final )
{
	static const int	step = 2;
	short		*samples = (short *) (fr->buffer.data + fr->buffer.fill);
	float		*b0, **buf; // (*buf)[0x110];
	int		clip = 0;
	int		bo1;

	if( !channel )
	{
		fr->bo--;
		fr->bo &= 0xf;
		buf = fr->float_buffs[0];
	}
	else
	{
		samples++;
		buf = fr->float_buffs[1];
	}

	if( fr->bo & 0x1 )
	{
		b0 = buf[0];
		bo1 = fr->bo;
		dct64( buf[1] + ((fr->bo + 1) & 0xf ), buf[0] + fr->bo, bandPtr );
	}
	else
	{
		b0 = buf[1];
		bo1 = fr->bo+1;
		dct64( buf[0] + fr->bo, buf[1] + fr->bo + 1, bandPtr );
	}

	{
		float		*window = fr->decwin + 16 - bo1;
		register int	j;

		for( j = (BLOCK / 4); j; j--, b0 += 0x400 / BLOCK - BACKPEDAL, window += 0x800 / BLOCK - BACKPEDAL, samples += step )
		{
			float	sum;

			sum  = REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );
			sum += REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );
			sum += REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );
			sum += REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );
			sum += REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );
			sum += REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );
			sum += REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );
			sum += REAL_MUL_SYNTH( *window++, *b0++ );
			sum -= REAL_MUL_SYNTH( *window++, *b0++ );

			WRITE_SHORT_SAMPLE( samples, sum, clip );
		}

		{
			float	sum;

			sum  = REAL_MUL_SYNTH( window[0x0], b0[0x0] );
			sum += REAL_MUL_SYNTH( window[0x2], b0[0x2] );
			sum += REAL_MUL_SYNTH( window[0x4], b0[0x4] );
			sum += REAL_MUL_SYNTH( window[0x6], b0[0x6] );
			sum += REAL_MUL_SYNTH( window[0x8], b0[0x8] );
			sum += REAL_MUL_SYNTH( window[0xA], b0[0xA] );
			sum += REAL_MUL_SYNTH( window[0xC], b0[0xC] );
			sum += REAL_MUL_SYNTH( window[0xE], b0[0xE] );

			WRITE_SHORT_SAMPLE( samples, sum, clip );
			samples += step;
			b0 -= 0x400 / BLOCK;
			window -= 0x800 / BLOCK;
		}
		window += bo1<<1;

		for( j= (BLOCK / 4) - 1; j; j--, b0 -= 0x400 / BLOCK + BACKPEDAL, window -= 0x800 / BLOCK - BACKPEDAL, samples += step )
		{
			float	sum;

			sum = -REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );
			sum -= REAL_MUL_SYNTH( *(--window), *b0++ );

			WRITE_SHORT_SAMPLE( samples, sum, clip );
		}
	}

	if( final ) fr->buffer.fill += BLOCK * sizeof( short );

	return clip;
}

// the call of left and right plain synth, wrapped.
// this may be replaced by a direct stereo optimized synth.
static int synth_stereo( float *bandPtr_l, float *bandPtr_r, mpg123_handle_t *fr )
{
	int	clip;

	clip = (fr->synth)( bandPtr_l, 0, fr, 0 );
	clip += (fr->synth)( bandPtr_r, 1, fr, 1 );
	return clip;
}

// mono to stereo synth, wrapping over synth_1to1
static int synth_1to1_m2s(float *bandPtr, mpg123_handle_t *fr )
{
	byte	*samples = fr->buffer.data;
	int	i, ret;

	ret = synth_1to1( bandPtr, 0, fr, 1 );
	samples += fr->buffer.fill - BLOCK * sizeof( short );

	for( i = 0; i < (BLOCK / 2); i++ )
	{
		((short *)samples)[1] = ((short *)samples)[0];
		samples += 2 * sizeof( short );
	}

	return ret;
}

// mono synth, wrapping over synth_1to1
static int synth_1to1_mono( float *bandPtr, mpg123_handle_t *fr )
{
	short	samples_tmp[BLOCK];
	short	*tmp1 = samples_tmp;
	byte	*samples = fr->buffer.data;
	int	pnt = fr->buffer.fill;
	int	i, ret;

	// save buffer stuff, trick samples_tmp into there, decode, restore
	fr->buffer.data = (byte *)samples_tmp;
	fr->buffer.fill = 0;

	ret = synth_1to1( bandPtr, 0, fr, 0 );	// decode into samples_tmp
	fr->buffer.data = samples;		// restore original value

	// now append samples from samples_tmp
	samples += pnt;			// just the next mem in frame buffer

	for( i = 0; i < (BLOCK / 2); i++ )
	{
		*((short *)samples) = *tmp1;
		samples += sizeof( short );
		tmp1 += 2;
	}

	fr->buffer.fill = pnt + (BLOCK / 2) * sizeof( short );

	return ret;
}

static const struct synth_s synth_base =
{
{
{ synth_1to1 }	// plain
},
{
{ synth_stereo }	// stereo, by default only wrappers over plain synth
},
{
{ synth_1to1_m2s }	// mono2stereo
},
{
{ synth_1to1_mono }	// mono
}
};

void init_synth( mpg123_handle_t *fr )
{
	fr->synths = synth_base;
}

static int find_synth(func_synth synth,  const func_synth synths[r_limit][f_limit])
{
	enum synth_resample	ri;
	enum synth_format	fi;

	for( ri = 0; ri < r_limit; ++ri )
	{
		for( fi = 0; fi < f_limit; ++fi )
		{
			if( synth == synths[ri][fi] )
				return TRUE;
                    }
	}

	return FALSE;
}

enum optdec
{
	autodec = 0,
	generic,
	nodec
};

// determine what kind of decoder is actually active
// this depends on runtime choices which may cause fallback to i386 or generic code.
static int find_dectype( mpg123_handle_t *fr )
{
	enum optdec	type = nodec;
	func_synth	basic_synth = fr->synth;

	if( find_synth( basic_synth, synth_base.plain ))
		type = generic;

	if( type != nodec )
	{
		return MPG123_OK;
	}
	else
	{
		fr->err = MPG123_BAD_DECODER_SETUP;
		return MPG123_ERR;
	}
}


// set synth functions for current frame
int set_synth_functions( mpg123_handle_t *fr )
{
	enum synth_resample	resample = r_none;
	enum synth_format	basic_format = f_none; // default is always 16bit, or whatever.

	if( fr->af.dec_enc & MPG123_ENC_16 )
		basic_format = f_16;

	// make sure the chosen format is compiled into this lib.
	if( basic_format == f_none )
		return -1;

	// be explicit about downsampling variant.
	switch( fr->down_sample )
	{
	case 0: resample = r_1to1; break;
	}

	if( resample == r_none )
		return -1;

	// finally selecting the synth functions for stereo / mono.
	fr->synth = fr->synths.plain[resample][basic_format];
	fr->synth_stereo = fr->synths.stereo[resample][basic_format];
	fr->synth_mono = fr->af.channels == 2 ? fr->synths.mono2stereo[resample][basic_format] : fr->synths.mono[resample][basic_format];

	if( find_dectype( fr ) != MPG123_OK )
	{
		fr->err = MPG123_BAD_DECODER_SETUP;
		return MPG123_ERR;
	}

	if( frame_buffers( fr ) != 0 )
	{
		fr->err = MPG123_NO_BUFFERS;
		return MPG123_ERR;
	}

	init_layer3_stuff( fr );
	fr->make_decode_tables = make_decode_tables;

	// we allocated the table buffers just now, so (re)create the tables.
	fr->make_decode_tables( fr );

	return 0;
}
