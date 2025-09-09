/*
snd_ogg_vorbis.c - loading and streaming of Ogg audio format with Vorbis codec
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

#include <string.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include "soundlib.h"
#include "crtlib.h"
#include "xash3d_mathlib.h"
#include "ogg_filestream.h"

typedef struct vorbis_streaming_ctx_s
{
	file_t *file;
	OggVorbis_File vf;
} vorbis_streaming_ctx_t;

static size_t FS_ReadOggVorbis( void *ptr, size_t blockSize, size_t nmemb, void *datasource )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t *)datasource;
	return g_fsapi.Read( ctx->file, ptr, blockSize * nmemb );
}

static int FS_SeekOggVorbis( void *datasource, int64_t offset, int whence )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t *)datasource;
	return g_fsapi.Seek( ctx->file, offset, whence );
}

static long FS_TellOggVorbis( void *datasource )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t *)datasource;
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

/*
=================================================================

	Ogg Vorbis decompression & streaming

=================================================================
*/
static const char *Vorbis_GetErrorDesc( int errorCode )
{
	switch( errorCode )
	{
	case OV_EOF:
		return "end of file";
	case OV_HOLE:
		return "compressed data sync lost";
	case OV_EBADHEADER:
		return "invalid header";
	case OV_EINVAL:
		return "invalid argument";
	case OV_ENOTVORBIS:
		return "not a Vorbis data";
	case OV_EBADLINK:
		return "link corrupted";
	case OV_EFAULT:
		return "internal error";
	case OV_EIMPL:
		return "not implemented";
	case OV_EBADPACKET:
		return "invalid packet";
	case OV_EVERSION:
		return "version mismatch";
	case OV_ENOSEEK:
		return "bitstream not seekable";
	case OV_ENOTAUDIO:
		return "not an audio data";
	case OV_EREAD:
		return "read error";
	default:
		return "unknown error";
	}
}

static void Sound_ScanVorbisComments( OggVorbis_File *vf )
{
	const char     *value;
	vorbis_comment *vc = ov_comment( vf, -1 );
	if( vc )
	{
		if(( value = vorbis_comment_query( vc, "LOOPSTART", 0 )))
		{
			sound.loopstart = Q_atoi( value );
			SetBits( sound.flags, SOUND_LOOPED );
		}
		else if(( value = vorbis_comment_query( vc, "LOOP_START", 0 )))
		{
			sound.loopstart = Q_atoi( value );
			SetBits( sound.flags, SOUND_LOOPED );
		}
	}
}

qboolean Sound_LoadOggVorbis( const char *name, const byte *buffer, fs_offset_t filesize )
{
	long   ret;
	int    section;
	size_t written = 0;
	vorbis_info      *info;
	ogg_filestream_t file;
	OggVorbis_File   vorbisFile;

	if( !buffer )
		return false;

	OggFilestream_Init( &file, name, buffer, filesize );
	if(( ret = ov_open_callbacks( &file, &vorbisFile, NULL, 0, ov_callbacks_membuf )) < 0 )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): %s\n", __func__, file.name, Vorbis_GetErrorDesc( ret ));
		return false;
	}

	info = ov_info( &vorbisFile, -1 );
	if( info->channels < 1 || info->channels > 2 )
	{
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

	SetBits( sound.flags, SOUND_RESAMPLE );
	Sound_ScanVorbisComments( &vorbisFile );

	while(( ret = ov_read( &vorbisFile, (char *)sound.wav + written, sound.size - written, 0, sound.width, 1, &section )) != 0 )
	{
		if( ret < 0 )
		{
			Con_DPrintf( S_ERROR "%s: failed to load (%s): %s\n", __func__, file.name, Vorbis_GetErrorDesc( ret ));
			return false;
		}
		written += ret;
	}

	ov_clear( &vorbisFile );
	return true;
}

stream_t *Stream_OpenOggVorbis( const char *filename )
{
	int ret;
	stream_t    *stream;
	vorbis_info *info;
	vorbis_streaming_ctx_t *ctx;

	ctx = (vorbis_streaming_ctx_t *)Mem_Calloc( host.soundpool, sizeof( vorbis_streaming_ctx_t ));
	ctx->file = FS_Open( filename, "rb", false );
	if( !ctx->file )
	{
		Mem_Free( ctx );
		return NULL;
	}

	stream = (stream_t *)Mem_Calloc( host.soundpool, sizeof( stream_t ));
	stream->file = ctx->file;
	stream->pos = 0;

	if(( ret = ov_open_callbacks( ctx, &ctx->vf, NULL, 0, ov_callbacks_fs )) < 0 )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): %s\n", __func__, filename, Vorbis_GetErrorDesc( ret ));
		FS_Close( ctx->file );
		Mem_Free( stream );
		Mem_Free( ctx );
		return NULL;
	}

	info = ov_info( &ctx->vf, -1 );
	if( info->channels < 1 || info->channels > 2 )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): unsuppored channels count\n", __func__, filename );
		FS_Close( ctx->file );
		Mem_Free( stream );
		Mem_Free( ctx );
		return NULL;
	}

	stream->buffsize = 0; // how many samples left from previous frame
	stream->channels = info->channels;
	stream->rate = info->rate;
	stream->width = 2; // always 16 bit
	stream->ptr = ctx;
	stream->type = WF_VORBISDATA;

	return stream;
}

int Stream_ReadOggVorbis( stream_t *stream, int needBytes, void *buffer )
{
	int section;
	int bytesWritten = 0;
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t *)stream->ptr;

	while( 1 )
	{
		byte *data;
		int  outsize;

		if( !stream->buffsize )
		{
			stream->pos = ov_read( &ctx->vf, (char *)stream->temp, OUTBUF_SIZE, 0, stream->width, 1, &section );
			if( stream->pos == 0 )
			{
				break; // end of file
			}
			else if( stream->pos < 0 )
			{
				Con_DPrintf( S_ERROR "%s: error during read: %s\n", __func__, Vorbis_GetErrorDesc( stream->pos ));
			}
		}

		// check remaining size
		if( bytesWritten + stream->pos > needBytes )
			outsize = ( needBytes - bytesWritten );
		else
			outsize = stream->pos;

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
	int ret;
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t *)stream->ptr;
	if(( ret = ov_raw_seek_lap( &ctx->vf, newpos )) == 0 )
	{
		stream->buffsize = 0; // flush any previous data
		return true;
	}
	Con_DPrintf( S_ERROR "%s: error during seek: %s\n", __func__, Vorbis_GetErrorDesc( ret ));
	return false; // failed to seek
}

int Stream_GetPosOggVorbis( stream_t *stream )
{
	vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t *)stream->ptr;
	return ov_raw_tell( &ctx->vf );
}

void Stream_FreeOggVorbis( stream_t *stream )
{
	if( stream->ptr )
	{
		vorbis_streaming_ctx_t *ctx = (vorbis_streaming_ctx_t *)stream->ptr;
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
