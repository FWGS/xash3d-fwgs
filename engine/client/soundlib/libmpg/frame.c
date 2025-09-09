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
#include <math.h>

static void *aligned_pointer( void *base, uint alignment )
{
	// work in unsigned integer realm, explicitly.
	// tricking the compiler into integer operations like % by invoking base-NULL is dangerous:
	// it results into ptrdiff_t, which gets negative on big addresses. Big screw up, that.
	// i try to do it "properly" here: Casting only to size_t and no artihmethic with void*.

	size_t	baseval = (size_t)(char *)base;
	size_t	aoff = baseval % alignment;

	if( aoff )
		return (char *)base + alignment - aoff;
	return base;
}

static void frame_default_parm( mpg123_parm_t *mp )
{
	mp->outscale = 1.0;
	mp->flags = 0;
	mp->flags |= MPG123_GAPLESS;
	mp->flags |= MPG123_AUTO_RESAMPLE;
	mp->down_sample = 0;
	mp->rva = 0;
	mp->halfspeed = 0;
	mp->doublespeed = 0;
	mp->verbose = 0;
	mp->timeout = 0;
	mp->resync_limit = 1024;
	mp->index_size = INDEX_SIZE;
	mp->preframes = 4;	// that's good  for layer 3 ISO compliance bitstream.
	mpg123_fmt_all( mp );

	// default of keeping some 4K buffers at hand, should cover the "usual" use case
	// (using 16K pipe buffers as role model).
	mp->feedpool = 5;
	mp->feedbuffer = 4096;
}

// reset everythign except dynamic memory.
static void frame_fixed_reset( mpg123_handle_t *fr )
{
	open_bad( fr );
	fr->to_decode = FALSE;
	fr->to_ignore = FALSE;
	fr->metaflags = 0;
	fr->outblock = 0;	// this will be set before decoding!
	fr->num = -1;
	fr->input_offset = -1;
	fr->playnum = -1;
	fr->state_flags = FRAME_ACCURATE;
	fr->silent_resync = 0;
	fr->audio_start = 0;
	fr->clip = 0;
	fr->oldhead = 0;
	fr->firsthead = 0;
	fr->vbr = MPG123_CBR;
	fr->abr_rate = 0;
	fr->track_frames = 0;
	fr->track_samples = -1;
	fr->framesize=0;
	fr->mean_frames = 0;
	fr->mean_framesize = 0;
	fr->freesize = 0;
	fr->lastscale = -1;
	fr->rva.level[0] = -1;
	fr->rva.level[1] = -1;
	fr->rva.gain[0] = 0;
	fr->rva.gain[1] = 0;
	fr->rva.peak[0] = 0;
	fr->rva.peak[1] = 0;
	fr->fsizeold = 0;
	fr->firstframe = 0;
	fr->ignoreframe = fr->firstframe - fr->p.preframes;
	fr->header_change = 0;
	fr->lastframe = -1;
	fr->fresh = 1;
	fr->new_format = 0;
	frame_gapless_init( fr, -1, 0, 0 );
	fr->lastoff = 0;
	fr->firstoff = 0;
	fr->bo = 1;	// the usual bo
	fr->halfphase = 0;	// here or indeed only on first-time init?
	fr->error_protection = 0;
	fr->freeformat_framesize = -1;
}

int frame_index_setup( mpg123_handle_t *fr )
{
	int	ret = MPG123_ERR;

	if( fr->p.index_size >= 0 )
	{
		// simple fixed index.
		fr->index.grow_size = 0;
		ret = fi_resize( &fr->index, (size_t)fr->p.index_size );
	}
	else
	{
		// a growing index. we give it a start, though.
		fr->index.grow_size = (size_t)(-fr->p.index_size );

		if( fr->index.size < fr->index.grow_size )
			ret = fi_resize( &fr->index, fr->index.grow_size );
		else ret = MPG123_OK; // we have minimal size already... and since growing is OK...
	}

	return ret;
}

void frame_init_par( mpg123_handle_t *fr, mpg123_parm_t *mp )
{
	fr->own_buffer = TRUE;
	fr->buffer.data = NULL;
	fr->buffer.rdata = NULL;
	fr->buffer.fill = 0;
	fr->buffer.size = 0;
	fr->rawbuffs = NULL;
	fr->rawbuffss = 0;
	fr->rawdecwin = NULL;
	fr->rawdecwins = 0;
	fr->layerscratch = NULL;
	fr->xing_toc = NULL;

	// unnecessary: fr->buffer.size = fr->buffer.fill = 0;
	// frame_outbuffer is missing...
	// frame_buffers is missing... that one needs cpu opt setting!
	// after these... frame_reset is needed before starting full decode
	invalidate_format( &fr->af );
	fr->rdat.r_read = NULL;
	fr->rdat.r_lseek = NULL;
	fr->rdat.iohandle = NULL;
	fr->rdat.r_read_handle = NULL;
	fr->rdat.r_lseek_handle = NULL;
	fr->rdat.cleanup_handle = NULL;
	fr->wrapperdata = NULL;
	fr->wrapperclean = NULL;
	fr->decoder_change = 1;
	fr->err = MPG123_OK;

	if( mp == NULL ) frame_default_parm( &fr->p );
	else memcpy( &fr->p, mp, sizeof( mpg123_parm_t ));

	bc_prepare( &fr->rdat.buffer, fr->p.feedpool, fr->p.feedbuffer );

	fr->down_sample = 0;	// initialize to silence harmless errors when debugging.
	frame_fixed_reset( fr );	// reset only the fixed data, dynamic buffers are not there yet!
	fr->synth = NULL;
	fr->synth_mono = NULL;
	fr->make_decode_tables = NULL;

	fi_init( &fr->index );
	frame_index_setup( fr );	// apply the size setting.
}

static void frame_decode_buffers_reset( mpg123_handle_t *fr )
{
	if( fr->rawbuffs ) /* memset(NULL, 0, 0) not desired */
		memset( fr->rawbuffs, 0, fr->rawbuffss );
}

int frame_buffers( mpg123_handle_t *fr )
{
	int	buffssize = 4352;

	buffssize += 15; // for 16-byte alignment

	if(fr->rawbuffs != NULL && fr->rawbuffss != buffssize)
	{
		free(fr->rawbuffs);
		fr->rawbuffs = NULL;
	}

	if( fr->rawbuffs == NULL )
		fr->rawbuffs = (byte *)malloc( buffssize );

	if( fr->rawbuffs == NULL )
		return -1;

	fr->rawbuffss = buffssize;
	fr->short_buffs[0][0] = aligned_pointer( fr->rawbuffs, 16 );
	fr->short_buffs[0][1] = fr->short_buffs[0][0] + 0x110;
	fr->short_buffs[1][0] = fr->short_buffs[0][1] + 0x110;
	fr->short_buffs[1][1] = fr->short_buffs[1][0] + 0x110;
	fr->float_buffs[0][0] = aligned_pointer( fr->rawbuffs, 16 );
	fr->float_buffs[0][1] = fr->float_buffs[0][0] + 0x110;
	fr->float_buffs[1][0] = fr->float_buffs[0][1] + 0x110;
	fr->float_buffs[1][1] = fr->float_buffs[1][0] + 0x110;

	// now the different decwins... all of the same size, actually
	// the MMX ones want 32byte alignment, which I'll try to ensure manually
	{
		int	decwin_size = (512 + 32) * sizeof( float );

		// hm, that's basically realloc() ...
		if( fr->rawdecwin != NULL && fr->rawdecwins != decwin_size )
		{
			free( fr->rawdecwin );
			fr->rawdecwin = NULL;
		}

		if( fr->rawdecwin == NULL )
			fr->rawdecwin = (byte *)malloc( decwin_size );

		if( fr->rawdecwin == NULL )
			return -1;

		fr->rawdecwins = decwin_size;
		fr->decwin = (float *)fr->rawdecwin;
	}

	// layer scratch buffers are of compile-time fixed size, so allocate only once.
	if( fr->layerscratch == NULL )
	{
		// allocate specific layer3 buffers
		size_t	scratchsize = 0;
		float	*scratcher;

		scratchsize += sizeof( float ) * 2 * SBLIMIT * SSLIMIT; // hybrid_in
		scratchsize += sizeof( float ) * 2 * SSLIMIT * SBLIMIT; // hybrid_out

		fr->layerscratch = malloc( scratchsize + 63 );
		if(fr->layerscratch == NULL) return -1;

		// get aligned part of the memory, then divide it up.
		scratcher = aligned_pointer( fr->layerscratch, 64 );

		// those funky pointer casts silence compilers...
		// One might change the code at hand to really just use 1D arrays,
		// but in practice, that would not make a (positive) difference.
		fr->layer3.hybrid_in = (float(*)[SBLIMIT][SSLIMIT])scratcher;
		scratcher += 2 * SBLIMIT * SSLIMIT;
		fr->layer3.hybrid_out = (float(*)[SSLIMIT][SBLIMIT])scratcher;
		scratcher += 2 * SSLIMIT * SBLIMIT;

		// note: These buffers don't need resetting here.
	}

	// only reset the buffers we created just now.
	frame_decode_buffers_reset( fr );

	return 0;
}

int frame_buffers_reset( mpg123_handle_t *fr )
{
	fr->buffer.fill = 0; // hm, reset buffer fill... did we do a flush?
	fr->bsnum = 0;

	// wondering: could it be actually _wanted_ to retain buffer contents over different files? (special gapless / cut stuff)
	fr->bsbuf = fr->bsspace[1];
	fr->bsbufold = fr->bsbuf;
	fr->bitreservoir = 0;
	frame_decode_buffers_reset( fr );
	memset( fr->bsspace, 0, 2 * ( MAXFRAMESIZE + 512 ));
	memset( fr->ssave, 0, 34 );
	fr->hybrid_blc[0] = fr->hybrid_blc[1] = 0;
	memset( fr->hybrid_block, 0, sizeof( float ) * 2 * 2 * SBLIMIT * SSLIMIT );

	return 0;
}

void frame_init( mpg123_handle_t *fr )
{
	frame_init_par( fr, NULL );
}

int frame_outbuffer( mpg123_handle_t *fr )
{
	size_t	size = fr->outblock;

	if( !fr->own_buffer )
	{
		if( fr->buffer.size < size )
		{
			fr->err = MPG123_BAD_BUFFER;
			return MPG123_ERR;
		}
	}

	if( fr->buffer.rdata != NULL && fr->buffer.size != size )
	{
		free( fr->buffer.rdata );
		fr->buffer.rdata = NULL;
	}

	fr->buffer.size = size;
	fr->buffer.data = NULL;

	// be generous: use 16 byte alignment
	if( fr->buffer.rdata == NULL )
		fr->buffer.rdata = (byte *)malloc( fr->buffer.size + 15 );

	if( fr->buffer.rdata == NULL )
	{
		fr->err = MPG123_OUT_OF_MEM;
		return MPG123_ERR;
	}

	fr->buffer.data = aligned_pointer( fr->buffer.rdata, 16 );
	fr->own_buffer = TRUE;
	fr->buffer.fill = 0;

	return MPG123_OK;
}

static void frame_free_toc( mpg123_handle_t *fr )
{
	if( fr->xing_toc != NULL )
	{
		free( fr->xing_toc );
		fr->xing_toc = NULL;
	}
}

// Just copy the Xing TOC over...
int frame_fill_toc( mpg123_handle_t *fr, byte *in )
{
	if( fr->xing_toc == NULL )
		fr->xing_toc = malloc( 100 );

	if( fr->xing_toc != NULL )
	{
		memcpy( fr->xing_toc, in, 100 );
		return TRUE;
	}

	return FALSE;
}

// prepare the handle for a new track.
// reset variables, buffers...
int frame_reset( mpg123_handle_t *fr )
{
	frame_buffers_reset( fr );
	frame_fixed_reset( fr );
	frame_free_toc( fr );
	fi_reset( &fr->index );

	return 0;
}

static void frame_free_buffers( mpg123_handle_t *fr )
{
	if( fr->rawbuffs != NULL )
		free( fr->rawbuffs );
	fr->rawbuffs = NULL;
	fr->rawbuffss = 0;

	if( fr->rawdecwin != NULL )
		free( fr->rawdecwin );
	fr->rawdecwin = NULL;
	fr->rawdecwins = 0;

	if( fr->layerscratch != NULL )
		free( fr->layerscratch );
}

void frame_exit( mpg123_handle_t *fr )
{
	if( fr->buffer.rdata != NULL )
		free( fr->buffer.rdata );

	fr->buffer.rdata = NULL;
	frame_free_buffers( fr );
	frame_free_toc( fr );
	fi_exit( &fr->index );

	// clean up possible mess from LFS wrapper.
	if( fr->wrapperclean != NULL )
	{
		fr->wrapperclean( fr->wrapperdata );
		fr->wrapperdata = NULL;
	}

	bc_cleanup( &fr->rdat.buffer );
}

static int mpg123_framedata( mpg123_handle_t *mh, ulong *header, byte **bodydata, size_t *bodybytes )
{
	if( mh == NULL )
		return MPG123_BAD_HANDLE;

	if( !mh->to_decode )
		return MPG123_ERR;

	if( header != NULL )
		*header = mh->oldhead;

	if( bodydata != NULL )
		*bodydata  = mh->bsbuf;

	if( bodybytes != NULL )
		*bodybytes = mh->framesize;

	return MPG123_OK;
}

// Fuzzy frame offset searching (guessing).
// When we don't have an accurate position, we may use an inaccurate one.
// Possibilities:
//	- use approximate positions from Xing TOC (not yet parsed)
//	- guess wildly from mean framesize and offset of first frame / beginning of file.
static mpg_off_t frame_fuzzy_find( mpg123_handle_t *fr, mpg_off_t want_frame, mpg_off_t *get_frame )
{
	mpg_off_t	ret = fr->audio_start; // default is to go to the beginning.

	*get_frame = 0;

	// but we try to find something better.
	// Xing VBR TOC works with relative positions, both in terms of audio frames and stream bytes.
	// thus, it only works when whe know the length of things.
	// oh... I assume the offsets are relative to the _total_ file length.
	if( fr->xing_toc != NULL && fr->track_frames > 0 && fr->rdat.filelen > 0 )
	{
		// one could round...
		int toc_entry = (int)((double)want_frame * 100.0 / fr->track_frames );

		// it is an index in the 100-entry table.
		if( toc_entry < 0 ) toc_entry = 0;
		if( toc_entry > 99 ) toc_entry = 99;

		// now estimate back what frame we get.
		*get_frame = (mpg_off_t)((double)toc_entry / 100.0 * fr->track_frames );
		fr->state_flags &= ~FRAME_ACCURATE;
		fr->silent_resync = 1;

		// question: Is the TOC for whole file size (with/without ID3) or the "real" audio data only?
		// ID3v1 info could also matter.
		ret = (mpg_off_t)((double)fr->xing_toc[toc_entry] / 256.0 * fr->rdat.filelen);
	}
	else if( fr->mean_framesize > 0 )
	{
		// just guess with mean framesize (may be exact with CBR files).
		// query filelen here or not?
		fr->state_flags &= ~FRAME_ACCURATE; // fuzzy!
		fr->silent_resync = 1;
		*get_frame = want_frame;
		ret = (mpg_off_t)(fr->audio_start + fr->mean_framesize * want_frame);
	}

	return ret;
}

// find the best frame in index just before the wanted one, seek to there
// then step to just before wanted one with read_frame
// do not care tabout the stuff that was in buffer but not played back
// everything that left the decoder is counted as played
// decide if you want low latency reaction and accurate timing info or stable long-time playback with buffer!
mpg_off_t frame_index_find( mpg123_handle_t *fr, mpg_off_t want_frame, mpg_off_t* get_frame )
{
	mpg_off_t	gopos = 0; // default is file start if no index position

	*get_frame = 0;

	// possibly use VBRI index, too? I'd need an example for this...
	if( fr->index.fill )
	{
		size_t	fi; // find in index

		// at index fi there is frame step*fi...
		fi = want_frame / fr->index.step;

		if( fi >= fr->index.fill )
		{
			// if we are beyond the end of frame index...
			// when fuzzy seek is allowed, we have some limited tolerance for the frames we want to read rather then jump over.
			if( fr->p.flags & MPG123_FUZZY && want_frame - (fr->index.fill- 1) * fr->index.step > 10 )
			{
				gopos = frame_fuzzy_find( fr, want_frame, get_frame );
				if( gopos > fr->audio_start )
					return gopos; // only in that case, we have a useful guess.
				// else... just continue, fuzzyness didn't help.
			}

			// use the last available position, slowly advancing from that one.
			fi = fr->index.fill - 1;
		}

		// we have index position, that yields frame and byte offsets.
		*get_frame = fi * fr->index.step;
		gopos = fr->index.data[fi];
		fr->state_flags |= FRAME_ACCURATE; // when using the frame index, we are accurate.
	}
	else
	{
		if( fr->p.flags & MPG123_FUZZY )
			return frame_fuzzy_find( fr, want_frame, get_frame );

		// a bit hackish here... but we need to be fresh when looking for the first header again.
		fr->firsthead = 0;
		fr->oldhead = 0;
	}

	return gopos;
}

static mpg_off_t frame_ins2outs( mpg123_handle_t *fr, mpg_off_t ins )
{
	mpg_off_t	outs = 0;

	switch( fr->down_sample )
	{
	case 0:
		outs = ins >> fr->down_sample;
		break;
	default:	break;
	}

	return outs;
}

mpg_off_t frame_outs( mpg123_handle_t *fr, mpg_off_t num )
{
	mpg_off_t	outs = 0;

	switch( fr->down_sample )
	{
	case 0:
		outs = (fr->spf >> fr->down_sample) * num;
		break;
	default:	break;
	}

	return outs;
}

// compute the number of output samples we expect from this frame.
// this is either simple spf() or a tad more elaborate for ntom.
mpg_off_t frame_expect_outsamples( mpg123_handle_t *fr )
{
	mpg_off_t	outs = 0;

	switch( fr->down_sample )
	{
	case 0:
		outs = fr->spf >> fr->down_sample;
		break;
	default:	break;
	}

	return outs;
}

mpg_off_t frame_offset( mpg123_handle_t *fr, mpg_off_t outs )
{
	mpg_off_t	num = 0;

	switch( fr->down_sample )
	{
	case 0:
		num = outs / (fr->spf >> fr->down_sample);
		break;
	default:	break;
	}

	return num;
}

// input in _input_ samples
void frame_gapless_init( mpg123_handle_t *fr, mpg_off_t framecount, mpg_off_t bskip, mpg_off_t eskip )
{
	fr->gapless_frames = framecount;

	if( fr->gapless_frames > 0 && bskip >= 0 && eskip >= 0 )
	{
		fr->begin_s = bskip + GAPLESS_DELAY;
		fr->end_s = framecount * fr->spf - eskip + GAPLESS_DELAY;
	}
	else fr->begin_s = fr->end_s = 0;

	// these will get proper values later, from above plus resampling info.
	fr->begin_os = 0;
	fr->end_os = 0;
	fr->fullend_os = 0;
}

void frame_gapless_realinit( mpg123_handle_t *fr )
{
	fr->begin_os = frame_ins2outs( fr, fr->begin_s );
	fr->end_os = frame_ins2outs( fr, fr->end_s );

	if( fr->gapless_frames > 0 )
		fr->fullend_os = frame_ins2outs( fr, fr->gapless_frames * fr->spf );
	else fr->fullend_os = 0;
}

// at least note when there is trouble...
void frame_gapless_update( mpg123_handle_t *fr, mpg_off_t total_samples )
{
	mpg_off_t gapless_samples = fr->gapless_frames * fr->spf;

	if( fr->gapless_frames < 1 )
		return;

	if( gapless_samples > total_samples )
	{
		// This invalidates the current position... but what should I do?
		frame_gapless_init( fr, -1, 0, 0 );
		frame_gapless_realinit( fr );
		fr->lastframe = -1;
		fr->lastoff = 0;
	}
}

// compute the needed frame to ignore from, for getting accurate/consistent output for intended firstframe.
static mpg_off_t ignoreframe( mpg123_handle_t *fr )
{
	mpg_off_t	preshift = fr->p.preframes;

	// layer 3 _really_ needs at least one frame before.
	if( fr->lay == 3 && preshift < 1 )
		preshift = 1;

	// layer 1 & 2 really do not need more than 2.
	if(fr->lay != 3 && preshift > 2 )
		preshift = 2;

	return fr->firstframe - preshift;
}

// the frame seek... this is not simply the seek to fe * fr->spf samples in output because we think of _input_ frames here.
// seek to frame offset 1 may be just seek to 200 samples offset in output since the beginning of first frame is delay/padding.
// hm, is that right? OK for the padding stuff, but actually, should the decoder delay be better totally hidden or not?
// with gapless, even the whole frame position could be advanced further than requested (since Homey don't play dat).
void frame_set_frameseek( mpg123_handle_t *fr, mpg_off_t fe )
{
	fr->firstframe = fe;

	if( fr->p.flags & MPG123_GAPLESS && fr->gapless_frames > 0 )
	{
		// take care of the beginning...
		mpg_off_t	beg_f = frame_offset( fr, fr->begin_os );

		if( fe <= beg_f )
		{
			fr->firstframe = beg_f;
			fr->firstoff = fr->begin_os - frame_outs( fr, beg_f );
		}
		else
		{
			fr->firstoff = 0;
		}

		// the end is set once for a track at least, on the frame_set_frameseek called in get_next_frame()
		if( fr->end_os > 0 )
		{
			fr->lastframe = frame_offset( fr, fr->end_os );
			fr->lastoff = fr->end_os - frame_outs( fr, fr->lastframe );
		}
		else
		{
			fr->lastframe = -1;
			fr->lastoff = 0;
		}
	}
	else
	{
		fr->firstoff = fr->lastoff = 0;
		fr->lastframe = -1;
	}

	fr->ignoreframe = ignoreframe( fr );
}

void frame_skip( mpg123_handle_t *fr )
{
	if( fr->lay == 3 )
		set_pointer( fr, 512 );
}

// sample accurate seek prepare for decoder.
// this gets unadjusted output samples and takes resampling into account
void frame_set_seek( mpg123_handle_t *fr, mpg_off_t sp )
{
	fr->firstframe = frame_offset( fr, sp );
	fr->ignoreframe = ignoreframe( fr );
	fr->firstoff = sp - frame_outs( fr, fr->firstframe );
}

static int get_rva( mpg123_handle_t *fr, double *peak, double *gain )
{
	double	p = -1;
	double	g = 0;
	int	ret = 0;

	if( fr->p.rva )
	{
		int	rt = 0;

		// should one assume a zero RVA as no RVA?
		if( fr->p.rva == 2 && fr->rva.level[1] != -1 )
			rt = 1;

		if( fr->rva.level[rt] != -1 )
		{
			p = fr->rva.peak[rt];
			g = fr->rva.gain[rt];
			ret = 1; // success.
		}
	}

	if( peak != NULL ) *peak = p;
	if( gain != NULL ) *gain = g;

	return ret;
}

// adjust the volume, taking both fr->outscale and rva values into account
void do_rva( mpg123_handle_t *fr )
{
	double	peak = 0;
	double	gain = 0;
	double	newscale;
	double	rvafact = 1;

	if( get_rva( fr, &peak, &gain ))
		rvafact = pow( 10, gain / 20 );

	newscale = fr->p.outscale * rvafact;

	// if peak is unknown (== 0) this check won't hurt
	if(( peak * newscale ) > 1.0 )
		newscale = 1.0 / peak;

	// first rva setting is forced with fr->lastscale < 0
	if( newscale != fr->lastscale || fr->decoder_change )
	{
		fr->lastscale = newscale;
		// it may be too early, actually.
		if( fr->make_decode_tables != NULL )
			fr->make_decode_tables( fr ); // the actual work
	}
}
