/*
snd_ogg.c - loading and streaming of Ogg format with Vorbis/Opus codec
Copyright (C) 2024 SNMetamorph

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "soundlib.h"
#include "crtlib.h"
#include "xash3d_mathlib.h"
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <opusfile.h>
#include <string.h>
#include <stdint.h>

typedef struct ogg_filestream_s
{
	const char *name;
	const byte *buffer;
	size_t filesize;
	size_t position;
} ogg_filestream_t;

typedef struct vorbis_streaming_ctx_s
{
	file_t *file;
	OggVorbis_File vf;
} vorbis_streaming_ctx_t;

static void OggFilestream_Init( ogg_filestream_t *filestream, const char *name, const byte *buffer, size_t filesize )
{
	filestream->name = name;
	filestream->buffer = buffer;
	filestream->filesize = filesize;
	filestream->position = 0;
}

static size_t OggFilestream_Read( void *ptr, size_t blockSize, size_t nmemb, void *datasource )
{
	ogg_filestream_t *filestream = (ogg_filestream_t*)datasource;
	size_t remain = filestream->filesize - filestream->position;
	size_t dataSize = blockSize * nmemb;

	// reads as many blocks as fits in remaining memory
	if( dataSize > remain )
		dataSize = remain - remain % blockSize; 

	memcpy( ptr, filestream->buffer + filestream->position, dataSize );
	filestream->position += dataSize;
	return dataSize / blockSize;
}

static int OggFilestream_Seek( void *datasource, int64_t offset, int whence )
{
	int64_t position;
	ogg_filestream_t *filestream = (ogg_filestream_t*)datasource;

	if( whence == SEEK_SET )
		position = offset;
	else if( whence == SEEK_CUR )
		position = offset + filestream->position;
	else if( whence == SEEK_END )
		position = offset + filestream->filesize;
	else
		return -1;

	if( position < 0 || position > filestream->filesize )
		return -1;

	filestream->position = position;
	return 0;
}

static long OggFilestream_Tell( void *datasource )
{
	ogg_filestream_t *filestream = (ogg_filestream_t*)datasource;
	return filestream->position;
}

static int OpusCallback_Read( void *datasource, byte *ptr, int nbytes )
{
	return OggFilestream_Read( ptr, 1, nbytes, datasource );
}

static opus_int64 OpusCallback_Tell( void *datasource )
{
	return OggFilestream_Tell( datasource );
}

static size_t FS_ReadOggVorbis( void *ptr, size_t blockSize, size_t nmemb, void *datasource )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t*)datasource;
	return g_fsapi.Read( ctx->file, ptr, blockSize * nmemb );
}

static int FS_SeekOggVorbis( void *datasource, int64_t offset, int whence )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t*)datasource;
	return g_fsapi.Seek( ctx->file, offset, whence );
}

static long FS_TellOggVorbis( void *datasource )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t*)datasource;
	return g_fsapi.Tell( ctx->file );
}

static const ov_callbacks ov_callbacks_membuf = {
	OggFilestream_Read,
	OggFilestream_Seek,
	NULL,
	OggFilestream_Tell
};

static const ov_callbacks ov_callbacks_fs = {
	FS_ReadOggVorbis,
	FS_SeekOggVorbis,
	NULL,
	FS_TellOggVorbis
};

static const OpusFileCallbacks op_callbacks_membuf = {
	OpusCallback_Read,
	OggFilestream_Seek,
	OpusCallback_Tell,
	NULL
};

/*
=================================================================

	Ogg Vorbis decompression & streaming

=================================================================
*/
qboolean Sound_LoadOggVorbis( const char *name, const byte *buffer, fs_offset_t filesize )
{
	long ret;
	int section;
	size_t written = 0;
	ogg_filestream_t file;
	OggVorbis_File vorbisFile;
	
	if( !buffer )
		return false;

	OggFilestream_Init( &file, name, buffer, filesize );
	if( ov_open_callbacks( &file, &vorbisFile, NULL, 0, ov_callbacks_membuf ) < 0 ) {
		Con_DPrintf( S_ERROR "%s: failed to load (%s): file reading error\n", __func__, file.name );
		return false;
	}

	vorbis_info *info = ov_info( &vorbisFile, -1 );
	if( info->channels < 1 || info->channels > 2 ) {
		Con_DPrintf( S_ERROR "%s: failed to load (%s): unsuppored channels count\n", __func__, file.name );
		return false;
	}

	sound.channels = info->channels;
	sound.rate = info->rate;
	sound.width = 2; // always 16-bit PCM
	sound.type = WF_PCMDATA;
	sound.samples = ov_pcm_total( &vorbisFile, -1 );
	sound.size = sound.samples * sound.width * sound.channels;
	sound.wav = (byte *)Mem_Calloc( host.soundpool, sound.size );

	while(( ret = ov_read( &vorbisFile, (char*)sound.wav + written, sound.size - written, 0, sound.width, 1, &section )) != 0 )
	{
		if( ret < 0 ) {
			Con_DPrintf( S_ERROR "%s: failed to load (%s): compressed data decoding error\n", __func__, file.name );
			return false;
		}
		written += ret;
	}

	ov_clear( &vorbisFile );
	return true;
}

stream_t *Stream_OpenOggVorbis( const char *filename )
{
	stream_t *stream;
	vorbis_streaming_ctx_t *ctx;

	ctx = (vorbis_streaming_ctx_t*)Mem_Calloc( host.soundpool, sizeof( vorbis_streaming_ctx_t ));
	ctx->file = FS_Open( filename, "rb", false );
	if (!ctx->file) {
		Mem_Free( ctx );
		return NULL;
	}

	stream = (stream_t*)Mem_Calloc( host.soundpool, sizeof( stream_t ));
	stream->file = ctx->file;
	stream->pos = 0;

	if( ov_open_callbacks( ctx, &ctx->vf, NULL, 0, ov_callbacks_fs ) < 0 )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): file openning error\n", __func__, filename );
		FS_Close( ctx->file );
		Mem_Free( stream );
		Mem_Free( ctx );
		return NULL;
	}

	vorbis_info *info = ov_info( &ctx->vf, -1 );
	if( info->channels < 1 || info->channels > 2 ) {
		Con_DPrintf( S_ERROR "%s: failed to load (%s): unsuppored channels count\n", __func__, filename );
		FS_Close( ctx->file );
		Mem_Free( stream );
		Mem_Free( ctx );
		return NULL;
	}

	stream->buffsize = 0; // how many samples left from previous frame
	stream->channels = info->channels;
	stream->rate = info->rate;
	stream->width = 2;	// always 16 bit
	stream->ptr = ctx;
	stream->type = WF_VORBISDATA;

	return stream;
}

int Stream_ReadOggVorbis( stream_t *stream, int needBytes, void *buffer )
{
	int section;
	int	bytesWritten = 0;
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t*)stream->ptr;
	
	while( 1 )
	{
		byte *data;
		int	outsize;

		if( !stream->buffsize )
		{
			if(( stream->pos = ov_read( &ctx->vf, (char*)stream->temp, OUTBUF_SIZE, 0, stream->width, 1, &section )) <= 0 )
				break; // there was EoF or error
		}

		// check remaining size
		if( bytesWritten + stream->pos > needBytes )
			outsize = ( needBytes - bytesWritten );
		else outsize = stream->pos;

		// copy raw sample to output buffer
		data = (byte *)buffer + bytesWritten;
		memcpy( data, &stream->temp[stream->buffsize], outsize );
		bytesWritten += outsize;
		stream->pos -= outsize;
		stream->buffsize += outsize;

		// continue from this sample on a next call
		if( bytesWritten >= needBytes )
			return bytesWritten;

		stream->buffsize = 0; // no bytes remaining
	}

	return 0;
}

int Stream_SetPosOggVorbis( stream_t *stream, int newpos )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t*)stream->ptr;
	if( ov_raw_seek_lap( &ctx->vf, newpos ) == 0 ) {
		stream->buffsize = 0; // flush any previous data
		return true;
	}
	return false; // failed to seek
}

int Stream_GetPosOggVorbis( stream_t *stream )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t*)stream->ptr;
	return ov_raw_tell( &ctx->vf );
}

void Stream_FreeOggVorbis( stream_t *stream )
{
	if( stream->ptr )
	{
		vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t*)stream->ptr;
		ov_clear( &ctx->vf );
		Mem_Free( stream->ptr );
		stream->ptr = NULL;
	}

	if( stream->file )
	{
		FS_Close( stream->file );
		stream->file = NULL;
	}

	Mem_Free( stream );
}

/*
=================================================================

	Ogg Opus decompression

=================================================================
*/
qboolean Sound_LoadOggOpus( const char *name, const byte *buffer, fs_offset_t filesize )
{
	long ret;
	ogg_filestream_t file;
	OggOpusFile *of;
	const OpusHead *opusHead;
	size_t written = 0;

	if( !buffer )
		return false;

	OggFilestream_Init( &file, name, buffer, filesize );
	of = op_open_callbacks( &file, &op_callbacks_membuf, NULL, 0, NULL );
	if( !of ) {
		Con_DPrintf( S_ERROR "%s: failed to load (%s): file reading error\n", __func__, file.name );
		return false;
	}

	opusHead = op_head( of, -1 );
	if( opusHead->channel_count < 1 || opusHead->channel_count > 2 ) {
		Con_DPrintf( S_ERROR "%s: failed to load (%s): unsuppored channels count\n", __func__, file.name );
		return false;
	}

	// according to OggOpus specification, sound always encoded at 48kHz sample rate
	// but this isn't a problem, engine can do resampling 
	sound.channels = opusHead->channel_count;
	sound.rate = 48000;
	sound.width = 2; // always 16-bit PCM
	sound.type = WF_PCMDATA;
	sound.flags = SOUND_RESAMPLE;
	sound.samples = op_pcm_total( of, -1 );
	sound.size = sound.samples * sound.width * sound.channels;
	sound.wav = (byte *)Mem_Calloc( host.soundpool, sound.size );

	// skip undesired samples before playing sound
	if( op_pcm_seek( of, opusHead->pre_skip ) < 0 ) {
		Con_DPrintf( S_ERROR "%s: failed to load (%s): pre-skip error\n", __func__, file.name );
		return false;
	}

	while(( ret = op_read( of, (opus_int16*)(sound.wav + written), (sound.size - written) / sound.width, NULL )) != 0 )
	{
		if( ret < 0 ) {
			Con_DPrintf( S_ERROR "%s: failed to load (%s): compressed data decoding error\n", __func__, file.name );
			return false;
		}
		written += ret * sound.width * sound.channels;
	}

	op_free( of );
	return true;
}
