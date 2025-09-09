/*
frame.h - compact version of famous library mpg123
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

#ifndef FRAME_H
#define FRAME_H

#define MAXFRAMESIZE	3456	// max = 1728
#define NUM_CHANNELS	2

typedef struct
{
	short		bits;
	short		d;
} al_table_t;

// the output buffer, used to be pcm_sample, pcm_point and audiobufsize
typedef struct outbuffer_s
{
	byte		*data;		// main data pointer, aligned
	byte		*p;		// read pointer
	size_t		fill;		// fill from read pointer
	size_t		size;
	byte		*rdata;		// unaligned base pointer
} outbuffer_t;

typedef struct audioformat_s
{
	int		encoding;		// final encoding, after post-processing.
	int		encsize;		// size of one sample in bytes, plain int should be fine here...
	int		dec_enc;  	// encoding of decoder synth.
	int		dec_encsize;	// size of one decoder sample.
	int		channels;
	int		rate;
} audioformat_t;

typedef struct mpg123_parm_s
{
	int		verbose;		// verbose level
	long		flags;		// combination of above
	int		down_sample;
	int		rva;		// (which) rva to do: 0: nothing, 1: radio/mix/track 2: album/audiophile
	long		halfspeed;
	long		doublespeed;
	long		timeout;

	char		audio_caps[NUM_CHANNELS][MPG123_RATES+1][MPG123_ENCODINGS];
	double		outscale;
	long		resync_limit;
	long		index_size;	// Long, because: negative values have a meaning.
	long		preframes;
	long		feedpool;
	long		feedbuffer;
} mpg123_parm_t;

// generic init, does not include dynamic buffers
void frame_init( mpg123_handle_t *fr );
void frame_init_par( mpg123_handle_t *fr, mpg123_parm_t *mp );
int frame_outbuffer( mpg123_handle_t *fr );
int frame_output_format( mpg123_handle_t *fr );
int frame_buffers( mpg123_handle_t *fr );
int frame_reset( mpg123_handle_t *fr );
int frame_buffers_reset( mpg123_handle_t *fr );
void frame_exit( mpg123_handle_t *fr );
int frame_index_setup( mpg123_handle_t *fr );
mpg_off_t frame_expect_outsamples( mpg123_handle_t *fr );
mpg_off_t frame_offset( mpg123_handle_t *fr, mpg_off_t outs );
void frame_gapless_init( mpg123_handle_t *fr, mpg_off_t framecount, mpg_off_t bskip, mpg_off_t eskip );
void frame_gapless_realinit( mpg123_handle_t *fr );
void frame_gapless_update( mpg123_handle_t *fr, mpg_off_t total_samples );
mpg_off_t frame_index_find( mpg123_handle_t *fr, mpg_off_t want_frame, mpg_off_t* get_frame );
mpg_off_t frame_outs( mpg123_handle_t *fr, mpg_off_t num );
void frame_set_seek( mpg123_handle_t *fr, mpg_off_t sp );
void frame_set_frameseek( mpg123_handle_t *fr, mpg_off_t fe );
int frame_fill_toc( mpg123_handle_t *fr, byte *in );
void frame_skip( mpg123_handle_t *fr );
void do_rva( mpg123_handle_t *fr );

#endif//FRAME_H
