/*
mpg123.c - compact version of famous library mpg123
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

static int	initialized = 0;

int mpg123_init( void )
{
	if(( sizeof( short ) != 2 ) || ( sizeof( long ) < 4 ))
		return MPG123_BAD_TYPES;

	if( initialized )
		return MPG123_OK; // no need to initialize twice

	init_layer3();
	prepare_decode_tables();
	initialized = 1;

#ifdef IEEE_FLOAT
	// this is rather pointless but it eases my mind to check that we did
	// not enable the special rounding on a VAX or something.
	if( REAL_TO_SHORT_ACCURATE( 12345.67f ) != 12346 )
	{
		return MPG123_ERR;
	}
#endif
	return MPG123_OK;
}

void mpg123_exit( void )
{
	// nothing yet, but something later perhaps
}

// create a new handle with specified decoder, decoder can be "", "auto" or NULL for auto-detection
mpg123_handle_t *mpg123_new( int *error )
{
	return mpg123_parnew( NULL, error );
}

// ...the full routine with optional initial parameters to override defaults.
mpg123_handle_t *mpg123_parnew( mpg123_parm_t *mp, int *error )
{
	mpg123_handle_t	*fr = NULL;
	int		err = MPG123_OK;

	if( initialized )
		fr = (mpg123_handle_t *)malloc( sizeof( mpg123_handle_t ));
	else err = MPG123_NOT_INITIALIZED;

	if( fr != NULL )
	{
		frame_init_par( fr, mp );
		init_synth( fr );
	}

	if( fr != NULL )
	{
		fr->decoder_change = 1;
	}
	else if( err == MPG123_OK )
	{
		err = MPG123_OUT_OF_MEM;
	}

	if( error != NULL )
		*error = err;

	return fr;
}

int mpg123_par( mpg123_parm_t *mp, enum mpg123_parms key, long val )
{
	int	ret = MPG123_OK;

	if( mp == NULL )
		return MPG123_BAD_PARS;

	switch( key )
	{
	case MPG123_VERBOSE:
		mp->verbose = val;
		break;
	case MPG123_FLAGS:
		if( ret == MPG123_OK )
			mp->flags = val;
		break;
	case MPG123_ADD_FLAGS:
		mp->flags |= val;
		break;
	case MPG123_REMOVE_FLAGS:
		mp->flags &= ~val;
		break;
	case MPG123_FORCE_RATE: // should this trigger something
		if( val > 0 )
			ret = MPG123_BAD_RATE;
		break;
	case MPG123_DOWN_SAMPLE:
		if( val != 0 )
			ret = MPG123_BAD_RATE;
		break;
	case MPG123_RVA:
		if( val < 0 || val > MPG123_RVA_MAX )
			ret = MPG123_BAD_RVA;
		else mp->rva = (int)val;
		break;
	case MPG123_DOWNSPEED:
		mp->halfspeed = val < 0 ? 0 : val;
		break;
	case MPG123_UPSPEED:
		mp->doublespeed = val < 0 ? 0 : val;
		break;
	case MPG123_OUTSCALE:
		// choose the value that is non-zero, if any.
		// downscaling integers to 1.0.
		mp->outscale = (double)val / SHORT_SCALE;
		break;
	case MPG123_TIMEOUT:
		if( val > 0 ) ret = MPG123_NO_TIMEOUT;
		break;
	case MPG123_RESYNC_LIMIT:
		mp->resync_limit = val;
		break;
	case MPG123_INDEX_SIZE:
		mp->index_size = val;
		break;
	case MPG123_PREFRAMES:
		if( val >= 0 ) mp->preframes = val;
		else ret = MPG123_BAD_VALUE;
		break;
	case MPG123_FEEDPOOL:
		if( val >= 0 ) mp->feedpool = val;
		else ret = MPG123_BAD_VALUE;
		break;
	case MPG123_FEEDBUFFER:
		if( val > 0 ) mp->feedbuffer = val;
		else ret = MPG123_BAD_VALUE;
		break;
	default:
		ret = MPG123_BAD_PARAM;
	}

	return ret;
}

int mpg123_param( mpg123_handle_t *mh, enum mpg123_parms key, long val )
{
	int	r;

	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	r = mpg123_par(&mh->p, key, val );
	if( r != MPG123_OK )
	{
		mh->err = r;
		return MPG123_ERR;
	}
	else
	{
		// special treatment for some settings.
		if( key == MPG123_INDEX_SIZE )
		{ 
			// apply frame index size and grow property on the fly.
			r = frame_index_setup( mh );
			if( r != MPG123_OK )
				mh->err = MPG123_INDEX_FAIL;
		}

		// feeder pool size is applied right away, reader will react to that.
		if( key == MPG123_FEEDPOOL || key == MPG123_FEEDBUFFER )
			bc_poolsize( &mh->rdat.buffer, mh->p.feedpool, mh->p.feedbuffer );

		return r;
	}
}

int mpg123_close( mpg123_handle_t *mh )
{
	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	// mh->rd is never NULL!
	if( mh->rd->close != NULL )
		mh->rd->close( mh );

	if( mh->new_format )
	{
		invalidate_format( &mh->af );
		mh->new_format = 0;
	}

	// always reset the frame buffers on close, so we cannot forget it in funky opening routines (wrappers, even).
	frame_reset( mh );

	return MPG123_OK;
}

void mpg123_delete( mpg123_handle_t *mh )
{
	if( mh != NULL )
	{
		mpg123_close( mh );
		frame_exit( mh );	// free buffers in frame
		free( mh );	// free struct; cast?
	}
}

int mpg123_open_handle( mpg123_handle_t *mh, void *iohandle )
{
	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	mpg123_close( mh );

	if( mh->rdat.r_read_handle == NULL )
	{
		mh->err = MPG123_BAD_CUSTOM_IO;
		return MPG123_ERR;
	}

	return open_stream_handle( mh, iohandle );
}

int mpg123_open_feed( mpg123_handle_t *mh )
{
	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	mpg123_close( mh );

	return open_feed( mh );
}

int mpg123_replace_reader_handle( mpg123_handle_t *mh, mpg_ssize_t (*fread)( void*, void*, size_t), mpg_off_t (*lseek)(void*, mpg_off_t, int), void(*fclose)(void*))
{
	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	mpg123_close( mh );
	mh->rdat.r_read_handle = fread;
	mh->rdat.r_lseek_handle = lseek;
	mh->rdat.cleanup_handle = fclose;

	return MPG123_OK;
}

// update decoding engine for
// a) a new choice of decoder
// b) a changed native format of the MPEG stream
// ... calls are only valid after parsing some MPEG frame!
int decode_update( mpg123_handle_t *mh )
{
	long	native_rate;
	int	b;

	if( mh->num < 0 )
	{
		mh->err = MPG123_BAD_DECODER_SETUP;
		return MPG123_ERR;
	}

	mh->state_flags |= FRAME_FRESH_DECODER;
	native_rate = frame_freq( mh );

	b = frame_output_format( mh ); // select the new output format based on given constraints.
	if( b < 0 ) return MPG123_ERR;
	if( b == 1 ) mh->new_format = 1; // store for later...

	if( mh->af.rate == native_rate )
		mh->down_sample = 0;
	else if( mh->af.rate == native_rate >> 1 )
		mh->down_sample = 1;
	else if( mh->af.rate == native_rate >> 2 )
		mh->down_sample = 2;
	else mh->down_sample = 3; // flexible (fixed) rate

	switch( mh->down_sample )
	{
	case 0:
	case 1:
	case 2:
		mh->down_sample_sblimit = SBLIMIT >> ( mh->down_sample );
		// with downsampling I get less samples per frame
		mh->outblock = outblock_bytes( mh, ( mh->spf >> mh->down_sample ));
		break;
	}

	if(!( mh->p.flags & MPG123_FORCE_MONO ))
	{
		if( mh->af.channels == 1 )
			mh->single = SINGLE_MIX;
		else mh->single = SINGLE_STEREO;
	}
	else mh->single = ( mh->p.flags & MPG123_FORCE_MONO ) - 1;

	if( set_synth_functions( mh ) != 0 )
		return -1;

	// the needed size of output buffer may have changed.
	if( frame_outbuffer( mh ) != MPG123_OK )
		return -1;

	do_rva( mh );

	return 0;
}

size_t mpg123_safe_buffer( void )
{
	// real is the largest possible output
	return sizeof( float ) * 2 * 1152;
}

size_t mpg123_outblock( mpg123_handle_t *mh )
{
	// try to be helpful and never return zero output block size.
	if( mh != NULL && mh->outblock > 0 )
		return mh->outblock;
	return mpg123_safe_buffer();
}

// read in the next frame we actually want for decoding.
// this includes skipping/ignoring frames, in additon to skipping junk in the parser.
static int get_next_frame( mpg123_handle_t *mh )
{
	int	change = mh->decoder_change;

	// ensure we got proper decoder for ignoring frames.
	// header can be changed from seeking around. But be careful: Only after at
	// least one frame got read, decoder update makes sense.
	if( mh->header_change > 1 && mh->num >= 0 )
	{
		change = 1;
		mh->header_change = 0;

		if( decode_update( mh ) < 0 )
			return MPG123_ERR;
	}

	do
	{
		int	b;

		// decode & discard some frame(s) before beginning.
		if( mh->to_ignore && mh->num < mh->firstframe && mh->num >= mh->ignoreframe )
		{
			// decoder structure must be current! decode_update has been called before...
			(mh->do_layer)( mh );
			mh->buffer.fill = 0;
			mh->to_ignore = mh->to_decode = FALSE;
		}

		// read new frame data; possibly breaking out here for MPG123_NEED_MORE.
		mh->to_decode = FALSE;
		b = read_frame( mh ); // that sets to_decode only if a full frame was read.

		if( b == MPG123_NEED_MORE )
		{
			return MPG123_NEED_MORE; // need another call with data
		}
		else if( b <= 0 )
		{
			// more sophisticated error control?
			if( b == 0 || ( mh->rdat.filelen >= 0 && mh->rdat.filepos == mh->rdat.filelen ))
			{
				// we simply reached the end.
				mh->track_frames = mh->num + 1;

				return MPG123_DONE;
			}

			return MPG123_ERR; // some real error.
		}

		// now, there should be new data to decode ... and also possibly new stream properties
		if( mh->header_change > 1 )
		{
			change = 1;
			mh->header_change = 0;

			// need to update decoder structure right away since frame might need to
			// be decoded on next loop iteration for properly ignoring its output.
			if( decode_update( mh ) < 0 )
				return MPG123_ERR;
		}

		// now some accounting: Look at the numbers and decide if we want this frame.
		mh->playnum++;

		// plain skipping without decoding, only when frame is not ignored on next cycle.
		if( mh->num < mh->firstframe || ( mh->p.doublespeed && ( mh->playnum % mh->p.doublespeed )))
		{
			if(!( mh->to_ignore && mh->num < mh->firstframe && mh->num >= mh->ignoreframe ))
				frame_skip( mh );
		}
		else
		{
			// or, we are finally done and have a new frame.
			break;
		}
	} while( 1 );

	// if we reach this point, we got a new frame ready to be decoded.
	// all other situations resulted in returns from the loop.
	if( change )
	{
		mh->decoder_change = 0;

		if( mh->fresh )
		{
			int b = 0;
			// prepare offsets for gapless decoding.
			frame_gapless_realinit( mh );
			frame_set_frameseek( mh, mh->num );
			mh->fresh = 0;

			// could this possibly happen? With a real big gapless offset...
			if( mh->num < mh->firstframe ) b = get_next_frame( mh );
			if( b < 0 ) return b; // Could be error, need for more, new format...
		}
	}

	return MPG123_OK;
}

static int init_track( mpg123_handle_t *mh )
{
	if( track_need_init( mh ))
	{
		// fresh track, need first frame for basic info.
		int b = get_next_frame( mh );
		if( b < 0 ) return b;
	}

	return 0;
}

// from internal sample number to external.
static mpg_off_t sample_adjust( mpg123_handle_t *mh, mpg_off_t x )
{
	mpg_off_t	s;

	if( mh->p.flags & MPG123_GAPLESS )
	{
		// it's a bit tricky to do this computation for the padding samples.
		// they are not there on the outside.
		if( x > mh->end_os )
		{
			if( x < mh->fullend_os )
				s = mh->end_os - mh->begin_os;
			else s = x - (mh->fullend_os - mh->end_os + mh->begin_os);
		}
		else s = x - mh->begin_os;
	}
	else
	{
		s = x;
	}

	return s;
}

// from external samples to internal
static mpg_off_t sample_unadjust( mpg123_handle_t *mh, mpg_off_t x )
{
	mpg_off_t	s;

	if( mh->p.flags & MPG123_GAPLESS )
	{
		s = x + mh->begin_os;
		// there is a hole; we don't create sample positions in there.
		// jump from the end of the gapless track directly to after the padding.
		if( s >= mh->end_os ) s += mh->fullend_os - mh->end_os;
	}
	else
	{
		s = x;
	}

	return s;
}

// take the buffer after a frame decode (strictly: it is the data from frame fr->num!) and cut samples out.
// fr->buffer.fill may then be smaller than before...
static void frame_buffercheck( mpg123_handle_t *fr )
{
	// when we have no accurate position, gapless code does not make sense.
	if( !( fr->state_flags & FRAME_ACCURATE ))
		return;

	// get a grip on dirty streams that start with a gapless header.
	// simply accept all data from frames that are too much,
	// they are supposedly attached to the stream after the fact.
	if( fr->gapless_frames > 0 && fr->num >= fr->gapless_frames )
		return;

	// important: We first cut samples from the end, then cut from beginning (including left-shift of the buffer).
	// this order works also for the case where firstframe == lastframe.

	// the last interesting (planned) frame: Only use some leading samples.
	// note a difference from the below: The last frame and offset are unchanges by seeks.
	// the lastoff keeps being valid.
	if( fr->lastframe > -1 && fr->num >= fr->lastframe )
	{
		// there can be more than one frame of padding at the end, so we ignore the whole frame if we are beyond lastframe.
		mpg_off_t byteoff = ( fr->num == fr->lastframe ) ? samples_to_bytes( fr, fr->lastoff ) : 0;

		if((mpg_off_t)fr->buffer.fill > byteoff )
			fr->buffer.fill = byteoff;
	}

	// the first interesting frame: Skip some leading samples.
	if( fr->firstoff && fr->num == fr->firstframe )
	{
		mpg_off_t	byteoff = samples_to_bytes( fr, fr->firstoff );
		if((mpg_off_t)fr->buffer.fill > byteoff )
		{
			fr->buffer.fill -= byteoff;

			if( fr->own_buffer ) fr->buffer.p = fr->buffer.data + byteoff;
			else memmove( fr->buffer.data, fr->buffer.data + byteoff, fr->buffer.fill );
		}
		else fr->buffer.fill = 0;

		// we can only reach this frame again by seeking. And on seeking, firstoff will be recomputed.
		// so it is safe to null it here (and it makes the if() decision abort earlier).
		fr->firstoff = 0;
	}
}

// not part of the api. This just decodes the frame and fills missing bits with zeroes.
// there can be frames that are broken and thus make do_layer() fail.
static void decode_the_frame( mpg123_handle_t *fr )
{
	size_t	needed_bytes = decoder_synth_bytes( fr, frame_expect_outsamples( fr ));
	fr->clip += (fr->do_layer)(fr);

	// there could be less data than promised.
	// also, then debugging, we look out for coding errors that could result in _more_ data than expected.
	if( fr->buffer.fill < needed_bytes )
	{
		// one could do a loop with individual samples instead... but zero is zero
		// actually, that is wrong: zero is mostly a series of null bytes,
		// but we have funny 8bit formats that have a different opinion on zero...
		// unsigned 16 or 32 bit formats are handled later.
		memset( fr->buffer.data + fr->buffer.fill, 0, needed_bytes - fr->buffer.fill );

		fr->buffer.fill = needed_bytes;
	}

	postprocess_buffer( fr );
}

int mpg123_read( mpg123_handle_t *mh, byte *out, size_t size, size_t *done )
{
	return mpg123_decode( mh, NULL, 0, out, size, done );
}

int mpg123_feed( mpg123_handle_t *mh, const byte *in, size_t size )
{
	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	if( size > 0 )
	{
		if( in != NULL )
		{
			if( feed_more( mh, in, size ) != 0 )
			{
				return MPG123_ERR;
			}
			else
			{
				// the need for more data might have triggered an error.
				// this one is outdated now with the new data.
				if( mh->err == MPG123_ERR_READER )
					mh->err = MPG123_OK;
				return MPG123_OK;
			}
		}
		else
		{
			mh->err = MPG123_NULL_BUFFER;
			return MPG123_ERR;
		}
	}

	return MPG123_OK;
}

int mpg123_decode( mpg123_handle_t *mh, const byte *inmemory, size_t inmemsize, byte *outmemory, size_t outmemsize, size_t *done )
{
	int	ret = MPG123_OK;
	size_t	mdone = 0;

	if( done != NULL ) *done = 0;
	if( mh == NULL ) return MPG123_BAD_HANDLE;

	if( inmemsize > 0 && mpg123_feed( mh, inmemory, inmemsize ) != MPG123_OK )
	{
		ret = MPG123_ERR;
		goto decodeend;
	}

	if( outmemory == NULL )
		outmemsize = 0; // not just give error, give chance to get a status message.

	while( ret == MPG123_OK )
	{
		// decode a frame that has been read before.
		// this only happens when buffer is empty!
		if( mh->to_decode )
		{
			if( mh->new_format )
			{
				mh->new_format = 0;
				ret = MPG123_NEW_FORMAT;
				goto decodeend;
			}

			if( mh->buffer.size - mh->buffer.fill < mh->outblock )
			{
				ret = MPG123_NO_SPACE;
				goto decodeend;
			}

			decode_the_frame( mh );
			mh->to_decode = mh->to_ignore = FALSE;
			mh->buffer.p = mh->buffer.data;
			frame_buffercheck( mh );
		}

		if( mh->buffer.fill )
		{
			int	a = mh->buffer.fill > (outmemsize - mdone) ? outmemsize - mdone : mh->buffer.fill;

			// copy (part of) the decoded data to the caller's buffer.
			// get what is needed - or just what is there
			memcpy( outmemory, mh->buffer.p, a );

			// less data in frame buffer, less needed, output pointer increase, more data given...
			mh->buffer.fill -= a;
			outmemory  += a;
			mdone += a;
			mh->buffer.p += a;

			if(!( outmemsize > mdone ))
				goto decodeend;
		}
		else
		{
			// if we didn't have data, get a new frame.
			int b = get_next_frame( mh );
			if (b < 0 )
			{
				ret = b;
				goto decodeend;
			}
		}
	}
decodeend:
	if( done != NULL )
		*done = mdone;

	return ret;
}

int mpg123_getformat( mpg123_handle_t *mh, int *rate, int *channels, int *encoding )
{
	int	b;

	if( mh == NULL )
		return MPG123_BAD_HANDLE;
	b = init_track( mh );
	if( b < 0 ) return b;

	if( rate != NULL ) *rate = mh->af.rate;
	if( channels != NULL ) *channels = mh->af.channels;
	if( encoding != NULL ) *encoding = mh->af.encoding;
	mh->new_format = 0;

	return MPG123_OK;
}

int mpg123_scan( mpg123_handle_t *mh )
{
	mpg_off_t	track_frames = 0;
	mpg_off_t	track_samples = 0;
	mpg_off_t	oldpos;
	int	b;

	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	if(!( mh->rdat.flags & READER_SEEKABLE ))
	{
		mh->err = MPG123_NO_SEEK;
		return MPG123_ERR;
	}

	// scan through the _whole_ file, since the current position is no count but computed assuming constant samples per frame.
	// also, we can just keep the current buffer and seek settings. Just operate on input frames here.

	b = init_track( mh ); // mh->num >= 0 !!

	if( b < 0 )
	{
		if( b == MPG123_DONE )
			return MPG123_OK;
		return MPG123_ERR; // must be error here, NEED_MORE is not for seekable streams.
	}

	oldpos = mpg123_tell( mh );
	b = mh->rd->seek_frame( mh, 0 );

	if( b < 0 || mh->num != 0 )
		return MPG123_ERR;

	// one frame must be there now.
	track_frames = 1;
	track_samples = mh->spf; // internal samples.

	// do not increment mh->track_frames in the loop as tha would confuse Frankenstein detection.
	while( read_frame( mh ) == 1 )
	{
		track_samples += mh->spf;
		track_frames++;
	}

	mh->track_frames = track_frames;
	mh->track_samples = track_samples;

	// also, think about usefulness of that extra value track_samples ...
	// it could be used for consistency checking.
	if( mh->p.flags & MPG123_GAPLESS )
		frame_gapless_update( mh, mh->track_samples );

	return mpg123_seek( mh, oldpos, SEEK_SET ) >= 0 ? MPG123_OK : MPG123_ERR;
}

// now, where are we? We need to know the last decoded frame... and what's left of it in buffer.
// the current frame number can mean the last decoded frame or the to-be-decoded frame.
// if mh->to_decode, then mh->num frames have been decoded, the frame mh->num now coming next.
// if not, we have the possibility of mh->num+1 frames being decoded or nothing at all.
// then, there is firstframe...when we didn't reach it yet, then the next data will come from there.
// mh->num starts with -1
mpg_off_t mpg123_tell( mpg123_handle_t *mh )
{
	mpg_off_t	pos = 0;

	if( mh == NULL )
		return MPG123_ERR;

	if( track_need_init( mh ))
		return 0;

	// now we have all the info at hand.
	if(( mh->num < mh->firstframe ) || ( mh->num == mh->firstframe && mh->to_decode ))
	{
		// we are at the beginning, expect output from firstframe on.
		pos = frame_outs( mh, mh->firstframe );
		pos += mh->firstoff;
	}
	else if( mh->to_decode )
	{
		// we start fresh with this frame. Buffer should be empty, but we make sure to count it in.
		pos = frame_outs(mh, mh->num) - bytes_to_samples( mh, mh->buffer.fill );
	}
	else
	{
		// we serve what we have in buffer and then the beginning of next frame...
		pos = frame_outs(mh, mh->num+1) - bytes_to_samples( mh, mh->buffer.fill );
	}

	// substract padding and delay from the beginning. */
	pos = sample_adjust( mh, pos );

	// negative sample offsets are not right, less than nothing is still nothing.
	return pos > 0 ? pos : 0;
}

static int do_the_seek( mpg123_handle_t *mh )
{
	mpg_off_t	fnum = SEEKFRAME( mh );
	int	b;

	mh->buffer.fill = 0;

	// If we are inside the ignoreframe - firstframe window,
	// we may get away without actual seeking.
	if( mh->num < mh->firstframe )
	{
		mh->to_decode = FALSE; // In any case, don't decode the current frame, perhaps ignore instead.
		if( mh->num > fnum )
			return MPG123_OK;
	}

	// if we are already there, we are fine either for decoding or for ignoring.
	if( mh->num == fnum && ( mh->to_decode || fnum < mh->firstframe ))
		return MPG123_OK;

	// we have the frame before... just go ahead as normal.
	if( mh->num == fnum - 1 )
	{
		mh->to_decode = FALSE;
		return MPG123_OK;
	}

	// OK, real seeking follows... clear buffers and go for it.
	frame_buffers_reset( mh );

	b = mh->rd->seek_frame( mh, fnum );
	if( mh->header_change > 1 )
	{
		if( decode_update( mh ) < 0 )
			return MPG123_ERR;
		mh->header_change = 0;
	}

	if( b < 0 ) return b;

	// Only mh->to_ignore is TRUE.
	if( mh->num < mh->firstframe )
		mh->to_decode = FALSE;
	mh->playnum = mh->num;

	return 0;
}

mpg_off_t mpg123_seek( mpg123_handle_t *mh, mpg_off_t sampleoff, int whence )
{
	mpg_off_t	pos;
	int	b;

	pos = mpg123_tell( mh ); // adjusted samples

	// pos < 0 also can mean that simply a former seek failed at the lower levels.
	// in that case, we only allow absolute seeks.
	if( pos < 0 && whence != SEEK_SET )
	{
		// unless we got the obvious error of NULL handle,
		// this is a special seek failure.
		if( mh != NULL )
			mh->err = MPG123_NO_RELSEEK;
		return MPG123_ERR;
	}

	if(( b = init_track( mh )) < 0 )
		return b;

	switch( whence )
	{
	case SEEK_CUR: pos += sampleoff; break;
	case SEEK_SET: pos = sampleoff; break;
	case SEEK_END:
		// when we do not know the end already, we can try to find it.
		if( mh->track_frames < 1 && ( mh->rdat.flags & READER_SEEKABLE ))
			mpg123_scan( mh );
		if( mh->track_frames > 0 )
			pos = sample_adjust( mh, frame_outs( mh, mh->track_frames )) - sampleoff;
		else if( mh->end_os > 0 )
			pos = sample_adjust( mh, mh->end_os ) - sampleoff;
		else
		{
			mh->err = MPG123_NO_SEEK_FROM_END;
			return MPG123_ERR;
		}
		break;
	default:
		mh->err = MPG123_BAD_WHENCE;
		return MPG123_ERR;
	}

	if( pos < 0 ) pos = 0;
	// pos now holds the wanted sample offset in adjusted samples
	frame_set_seek( mh, sample_unadjust( mh, pos ));
	pos = do_the_seek( mh );
	if( pos < 0 ) return pos;

	return mpg123_tell( mh );
}

static const char *mpg123_error[] =
{
	"No error... (code 0)",
	"Unable to set up output format! (code 1)",
	"Invalid channel number specified. (code 2)",
	"Invalid sample rate specified. (code 3)",
	"Unable to allocate memory for 16 to 8 converter table! (code 4)",
	"Bad parameter id! (code 5)",
	"Bad buffer given -- invalid pointer or too small size. (code 6)",
	"Out of memory -- some malloc() failed. (code 7)",
	"You didn't initialize the library! (code 8)",
	"Invalid decoder choice. (code 9)",
	"Invalid mpg123 handle. (code 10)",
	"Unable to initialize frame buffers (out of memory?)! (code 11)",
	"Invalid RVA mode. (code 12)",
	"This build doesn't support gapless decoding. (code 13)",
	"Not enough buffer space. (code 14)",
	"Incompatible numeric data types. (code 15)",
	"Bad equalizer band. (code 16)",
	"Null pointer given where valid storage address needed. (code 17)",
	"Error reading the stream. (code 18)",
	"Cannot seek from end (end is not known). (code 19)",
	"Invalid 'whence' for seek function. (code 20)",
	"Build does not support stream timeouts. (code 21)",
	"File access error. (code 22)",
	"Seek not supported by stream. (code 23)",
	"No stream opened. (code 24)",
	"Bad parameter handle. (code 25)",
	"Invalid parameter addresses for index retrieval. (code 26)",
	"Lost track in the bytestream and did not attempt resync. (code 27)",
	"Failed to find valid MPEG data within limit on resync. (code 28)",
	"No 8bit encoding possible. (code 29)",
	"Stack alignment is not good. (code 30)",
	"You gave me a NULL buffer? (code 31)",
	"File position is screwed up, please do an absolute seek (code 32)",
	"Inappropriate NULL-pointer provided.",
	"Bad key value given.",
	"There is no frame index (disabled in this build).",
	"Frame index operation failed.",
	"Decoder setup failed (invalid combination of settings?)",
	"Feature not in this build.",
	"Some bad value has been provided.",
	"Low-level seeking has failed (call to lseek(), usually).",
	"Custom I/O obviously not prepared.",
	"Overflow in LFS (large file support) conversion.",
	"Overflow in integer conversion.",
};

const char *mpg123_plain_strerror( int errcode )
{
	if( errcode >= 0 && errcode < sizeof( mpg123_error ) / sizeof( char* ))
		return mpg123_error[errcode];

	switch( errcode )
	{
	case MPG123_ERR:
		return "A generic mpg123 error.";
	case MPG123_DONE:
		return "Message: I am done with this track.";
	case MPG123_NEED_MORE:
		return "Message: Feed me more input data!";
	case MPG123_NEW_FORMAT:
		return "Message: Prepare for a changed audio format (query the new one)!";
	default:
		return "I have no idea - an unknown error code!";
	}
}

const char *get_error( mpg123_handle_t *mh )
{
	if( !mh ) return mpg123_plain_strerror( MPG123_BAD_HANDLE );
	return mpg123_plain_strerror( mh->err );
}
