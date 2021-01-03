/*
soundlib.h - engine sound lib
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef SOUNDLIB_H
#define SOUNDLIB_H

#include "common.h"

#define FRAME_SIZE		32768	// must match with mp3 frame size
#define OUTBUF_SIZE		8192	// don't change!

typedef struct loadwavfmt_s
{
	const char *formatstring;
	const char *ext;
	qboolean (*loadfunc)( const char *name, const byte *buffer, fs_offset_t filesize );
} loadwavfmt_t;

typedef struct streamfmt_s
{
	const char *formatstring;
	const char *ext;

	stream_t *(*openfunc)( const char *filename );
	int (*readfunc)( stream_t *stream, int bytes, void *buffer );
	int (*setposfunc)( stream_t *stream, int newpos );
	int (*getposfunc)( stream_t *stream );
	void (*freefunc)( stream_t *stream );
} streamfmt_t;

typedef struct sndlib_s
{
	const loadwavfmt_t	*loadformats;
	const streamfmt_t	*streamformat;	// music stream

	// current sound state
	int		type;		// sound type
	int		rate;		// num samples per second (e.g. 11025 - 11 khz)
	int		width;		// resolution - bum bits divided by 8 (8 bit is 1, 16 bit is 2)
	int		channels;		// num channels (1 - mono, 2 - stereo)
	int		loopstart;	// start looping from
	uint		samples;		// total samplecount in sound
	uint		flags;		// additional sound flags
	size_t		size;		// sound unpacked size (for bounds checking)
	byte		*wav;		// sound pointer (see sound_type for details)

	byte		*tempbuffer;	// for convert operations
	int		cmd_flags;
} sndlib_t;

struct stream_s
{
	const streamfmt_t	*format;	// streamformat to operate

	// stream info
	file_t		*file;	// stream file
	int		width;	// resolution - num bits divided by 8 (8 bit is 1, 16 bit is 2)
	int		rate;	// stream rate
	int		channels;	// stream channels
	int		type;	// wavtype
	size_t		size;	// total stream size

	// current stream state
	void		*ptr;	// internal decoder state
	char		temp[OUTBUF_SIZE]; // mpeg decoder stuff
	size_t		pos;	// actual track position (or actual buffer remains)
	int		buffsize;	// cached buffer size
};

/*
========================================================================

.WAV sound format

========================================================================
*/

#define RIFFHEADER		(('F'<<24)+('F'<<16)+('I'<<8)+'R') // little-endian "RIFF"
#define WAVEHEADER		(('E'<<24)+('V'<<16)+('A'<<8)+'W') // little-endian "WAVE"
#define FORMHEADER		((' '<<24)+('t'<<16)+('m'<<8)+'f') // little-endian "fmt "
#define DATAHEADER		(('a'<<24)+('t'<<16)+('a'<<8)+'d') // little-endian "data"

typedef struct
{
	int	riff_id;		// 'RIFF'
	int	rLen;
	int	wave_id;		// 'WAVE'
	int	fmt_id;		// 'fmt '
	int	pcm_header_len;	// varies...
	short	wFormatTag;
	short	nChannels;	// 1,2 for stereo data is (l,r) pairs
	int	nSamplesPerSec;
	int	nAvgBytesPerSec;
	short	nBlockAlign;
	short	nBitsPerSample;
} wavehdr_t;

typedef struct
{
	int	data_id;		// 'data' or 'fact'
	int	dLen;
} chunkhdr_t;

extern sndlib_t sound;
//
// formats load
//
qboolean Sound_LoadWAV( const char *name, const byte *buffer, fs_offset_t filesize );
qboolean Sound_LoadMPG( const char *name, const byte *buffer, fs_offset_t filesize );

//
// stream operate
//
stream_t *Stream_OpenWAV( const char *filename );
int Stream_ReadWAV( stream_t *stream, int bytes, void *buffer );
int Stream_SetPosWAV( stream_t *stream, int newpos );
int Stream_GetPosWAV( stream_t *stream );
void Stream_FreeWAV( stream_t *stream );
stream_t *Stream_OpenMPG( const char *filename );
int Stream_ReadMPG( stream_t *stream, int bytes, void *buffer );
int Stream_SetPosMPG( stream_t *stream, int newpos );
int Stream_GetPosMPG( stream_t *stream );
void Stream_FreeMPG( stream_t *stream );

#endif//SOUNDLIB_H
