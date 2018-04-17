/*
frame.c - compact version of famous library mpg123
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

enum mpg123_channelcount
{
	MPG123_MONO   = 1,
	MPG123_STEREO = 2
};

// only the standard rates
static const long my_rates[MPG123_RATES] = 
{
	 8000, 11025, 12000, 
	16000, 22050, 24000,
	32000, 44100, 48000,
};

static const int my_encodings[MPG123_ENCODINGS] =
{
	MPG123_ENC_SIGNED_16,
	MPG123_ENC_UNSIGNED_16,
};

// the list of actually possible encodings.
static const int good_encodings[] =
{
	MPG123_ENC_SIGNED_16,
	MPG123_ENC_UNSIGNED_16,
};

// check if encoding is a valid one in this build.
static int good_enc( const int enc )
{
	size_t	i;

	for( i = 0; i < sizeof( good_encodings ) / sizeof( int ); ++i )
	{
		if( enc == good_encodings[i] )
			return TRUE;
	}

	return FALSE;
}

void mpg123_rates( const long **list, size_t *number )
{
	if( number != NULL ) *number = sizeof( my_rates ) / sizeof( long );
	if( list != NULL ) *list = my_rates;
}

// now that's a bit tricky... One build of the library knows only a subset of the encodings.
void mpg123_encodings( const int **list, size_t *number )
{
	if( number != NULL ) *number = sizeof( good_encodings ) / sizeof( int );
	if( list != NULL ) *list = good_encodings;
}

int mpg123_encsize( int encoding )
{
	return sizeof( short );
}

static int rate2num( long r )
{
	int	i;

	for( i = 0; i < MPG123_RATES; i++ )
	{
		if( my_rates[i] == r )
			return i;
	}

	return -1;
}

static int enc2num( int encoding )
{
	int	i;

	for( i = 0; i < MPG123_ENCODINGS; ++i )
	{
		if( my_encodings[i] == encoding )
			return i;
	}

	return -1;
}

static int cap_fit( mpg123_handle_t *fr, audioformat_t *nf, int f0, int f2)
{
	int	i;
	int	c  = nf->channels - 1;
	int	rn = rate2num( nf->rate );

	if( rn >= 0 )
	{
		for( i = f0; i <f2; i++ )
		{
			if( fr->p.audio_caps[c][rn][i] )
			{
				nf->encoding = my_encodings[i];
				return 1;
			}
		}
	}

	return 0;
}

static int freq_fit( mpg123_handle_t *fr, audioformat_t *nf, int f0, int f2 )
{
	nf->rate = frame_freq( fr ) >> fr->p.down_sample;

	if( cap_fit( fr, nf, f0, f2 ))
		return 1;

	if( fr->p.flags & MPG123_AUTO_RESAMPLE )
	{
		nf->rate >>= 1;
		if( cap_fit( fr, nf, f0, f2 ))
			return 1;

		nf->rate >>= 1;
		if( cap_fit( fr, nf, f0, f2 ))
			return 1;
	}

	return 0;
}

// match constraints against supported audio formats, store possible setup in frame
// return: -1: error; 0: no format change; 1: format change
int frame_output_format( mpg123_handle_t *fr )
{
	int		f0 = 0;
	int		f2 = MPG123_ENCODINGS;
	mpg123_parm_t	*p = &fr->p;
	audioformat_t	nf;

	// initialize new format, encoding comes later
	nf.channels = fr->stereo;

	// force stereo is stronger
	if( p->flags & MPG123_FORCE_MONO )
		nf.channels = 1;

	if( p->flags & MPG123_FORCE_STEREO )
		nf.channels = 2;

	if( freq_fit( fr, &nf, f0, 2 ))
		goto end; // try rates with 16bit

	if( freq_fit( fr, &nf, f0 <=2 ? 2 : f0, f2 ))
		goto end; // ... 8bit

	// try again with different stereoness
	if( nf.channels == 2 && !( p->flags & MPG123_FORCE_STEREO ))
		nf.channels = 1;
	else if( nf.channels == 1 && !( p->flags & MPG123_FORCE_MONO ))
		nf.channels = 2;

	if( freq_fit( fr, &nf, f0, 2 ))
		goto end; // try rates with 16bit
	if( freq_fit( fr, &nf,  f0 <= 2 ? 2 : f0, f2 ))
		goto end; // ... 8bit

	fr->err = MPG123_BAD_OUTFORMAT;
	return -1;
end:	
	// here is the _good_ end.
	// we had a successful match, now see if there's a change
	if( nf.rate == fr->af.rate && nf.channels == fr->af.channels && nf.encoding == fr->af.encoding )
	{
		return 0; // the same format as before
	}
	else
	{	// a new format
		fr->af.rate = nf.rate;
		fr->af.channels = nf.channels;
		fr->af.encoding = nf.encoding;

		// cache the size of one sample in bytes, for ease of use.
		fr->af.encsize = mpg123_encsize( fr->af.encoding );
		if( fr->af.encsize < 1 )
		{
			fr->err = MPG123_BAD_OUTFORMAT;
			return -1;
		}

		// set up the decoder synth format. Might differ.
		// without high-precision synths, 16 bit signed is the basis for
		// everything higher than 8 bit.
		if( fr->af.encsize > 2 )
		{
			fr->af.dec_enc = MPG123_ENC_SIGNED_16;
		}
		else
		{
			switch( fr->af.encoding )
			{
			case MPG123_ENC_UNSIGNED_16:
				fr->af.dec_enc = MPG123_ENC_SIGNED_16;
				break;
			default:
				fr->af.dec_enc = fr->af.encoding;
				break;
			}
		}

		fr->af.dec_encsize = mpg123_encsize( fr->af.dec_enc );

		return 1;
	}
}

static int mpg123_fmt_none( mpg123_parm_t *mp )
{
	if( mp == NULL )
		return MPG123_BAD_PARS;

	memset( mp->audio_caps, 0, sizeof( mp->audio_caps ));
	return MPG123_OK;
}

int mpg123_fmt_all( mpg123_parm_t *mp )
{
	size_t	rate, ch, enc;

	if( mp == NULL )
		return MPG123_BAD_PARS;

	for( ch = 0; ch < NUM_CHANNELS; ++ch )
	{
		for( rate = 0; rate < MPG123_RATES+1; ++rate )
		{
			for( enc = 0; enc < MPG123_ENCODINGS; ++enc )
				mp->audio_caps[ch][rate][enc] = good_enc( my_encodings[enc] );
		}
	}

	return MPG123_OK;
}

static int mpg123_fmt( mpg123_parm_t *mp, long rate, int channels, int encodings )
{
	int	ie, ic, ratei;
	int	ch[2] = { 0, 1 };

	if( mp == NULL )
		return MPG123_BAD_PARS;

	if(!( channels & ( MPG123_MONO|MPG123_STEREO )))
		return MPG123_BAD_CHANNEL;

	if(!( channels & MPG123_STEREO ))
		ch[1] = 0;
	else if(!( channels & MPG123_MONO ))
		ch[0] = 1;

	ratei = rate2num( rate );
	if( ratei < 0 ) return MPG123_BAD_RATE;

	// now match the encodings
	for( ic = 0; ic < 2; ++ic )
	{
		for( ie = 0; ie < MPG123_ENCODINGS; ++ie )
		{
			if( good_enc( my_encodings[ie] ) && (( my_encodings[ie] & encodings ) == my_encodings[ie] ))
				mp->audio_caps[ch[ic]][ratei][ie] = 1;
		}

		if( ch[0] == ch[1] )
			break; // no need to do it again
	}

	return MPG123_OK;
}

static int mpg123_fmt_support( mpg123_parm_t *mp, long rate, int encoding )
{
	int	ratei, enci;
	int	ch = 0;

	ratei = rate2num( rate );
	enci = enc2num( encoding );

	if( mp == NULL || ratei < 0 || enci < 0 )
		return 0;

	if( mp->audio_caps[0][ratei][enci] )
		ch |= MPG123_MONO;

	if( mp->audio_caps[1][ratei][enci] )
		ch |= MPG123_STEREO;

	return ch;
}

int mpg123_format_none( mpg123_handle_t *mh )
{
	int	r;

	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	r = mpg123_fmt_none( &mh->p );

	if( r != MPG123_OK )
	{
		mh->err = r;
		return MPG123_ERR;
	}

	return r;
}

int mpg123_format_all( mpg123_handle_t *mh )
{
	int	r;

	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	r = mpg123_fmt_all( &mh->p );

	if( r != MPG123_OK )
	{
		mh->err = r;
		return MPG123_ERR;
	}

	return r;
}

int mpg123_format( mpg123_handle_t *mh, long rate, int channels, int encodings )
{
	int	r;

	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	r = mpg123_fmt( &mh->p, rate, channels, encodings );

	if( r != MPG123_OK )
	{
		mh->err = r;
		return MPG123_ERR;
	}

	return r;
}

int mpg123_format_support( mpg123_handle_t *mh, long rate, int encoding )
{
	if( mh == NULL )
		return 0;

	return mpg123_fmt_support( &mh->p, rate, encoding );
}

// call this one to ensure that any valid format will be something different than this.
void invalidate_format( audioformat_t *af )
{
	af->encoding = 0;
	af->channels = 0;
	af->rate = 0;
}

// number of bytes the decoder produces.
mpg_off_t decoder_synth_bytes( mpg123_handle_t *fr, mpg_off_t s )
{
	return s * fr->af.dec_encsize * fr->af.channels;
}

// samples/bytes for output buffer after post-processing.
// take into account: channels, bytes per sample -- NOT resampling!
mpg_off_t samples_to_bytes( mpg123_handle_t *fr, mpg_off_t s )
{
	return s * fr->af.encsize * fr->af.channels;
}

mpg_off_t bytes_to_samples( mpg123_handle_t *fr, mpg_off_t b )
{
	return b / fr->af.encsize / fr->af.channels;
}

// number of bytes needed for decoding _and_ post-processing.
mpg_off_t outblock_bytes( mpg123_handle_t *fr, mpg_off_t s )
{
	int encsize = (fr->af.encsize > fr->af.dec_encsize ? fr->af.encsize : fr->af.dec_encsize);
	return s * encsize * fr->af.channels;
}

static void conv_s16_to_u16( outbuffer_t *buf )
{
	int16_t	*ssamples = (int16_t *)buf->data;
	uint16_t	*usamples = (uint16_t *)buf->data;
	size_t	count = buf->fill / sizeof( int16_t );
	size_t	i;

	for( i = 0; i < count; ++i )
	{
		long tmp = (long)ssamples[i] + 32768;
		usamples[i] = (uint16_t)tmp;
	}
}

void postprocess_buffer( mpg123_handle_t *fr )
{
	switch( fr->af.dec_enc )
	{
	case MPG123_ENC_SIGNED_16:
		switch( fr->af.encoding )
		{
		case MPG123_ENC_UNSIGNED_16:
			conv_s16_to_u16(&fr->buffer);
			break;
		}
		break;
	}
}
