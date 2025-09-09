/*
snd_ogg_opus.c - loading and streaming of Ogg audio format with Opus codec
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

#include <opusfile.h>
#include <string.h>
#include "soundlib.h"
#include "crtlib.h"
#include "ogg_filestream.h"

typedef struct opus_streaming_ctx_s
{
	file_t      *file;
	OggOpusFile *of;
} opus_streaming_ctx_t;

static int OpusCallback_Read( void *datasource, byte *ptr, int nbytes )
{
	return OggFilestream_Read( ptr, 1, nbytes, datasource );
}

static opus_int64 OpusCallback_Tell( void *datasource )
{
	return OggFilestream_Tell( datasource );
}

static int FS_ReadOggOpus( void *datasource, byte *ptr, int nbytes )
{
	opus_streaming_ctx_t *ctx = (opus_streaming_ctx_t *)datasource;
	return g_fsapi.Read( ctx->file, ptr, nbytes );
}

static int FS_SeekOggOpus( void *datasource, int64_t offset, int whence )
{
	opus_streaming_ctx_t *ctx = (opus_streaming_ctx_t *)datasource;
	return g_fsapi.Seek( ctx->file, offset, whence );
}

static opus_int64 FS_TellOggOpus( void *datasource )
{
	opus_streaming_ctx_t *ctx = (opus_streaming_ctx_t *)datasource;
	return g_fsapi.Tell( ctx->file );
}

static const OpusFileCallbacks op_callbacks_membuf = {
	OpusCallback_Read,
	OggFilestream_Seek,
	OpusCallback_Tell,
	NULL
};

static const OpusFileCallbacks op_callbacks_fs = {
	FS_ReadOggOpus,
	FS_SeekOggOpus,
	FS_TellOggOpus,
	NULL
};

/*
=================================================================

	Ogg Opus decompression & streaming

=================================================================
*/
static void Sound_ScanOpusComments( const OggOpusFile *of )
{
	const char     *value;
	const OpusTags *tags = op_tags( of, -1 );
	if( tags )
	{
		if(( value = opus_tags_query( tags, "LOOPSTART", 0 )))
		{
			sound.loopstart = Q_atoi( value );
			SetBits( sound.flags, SOUND_LOOPED );
		}
		else if(( value = opus_tags_query( tags, "LOOP_START", 0 )))
		{
			sound.loopstart = Q_atoi( value );
			SetBits( sound.flags, SOUND_LOOPED );
		}
	}
}

static const char *Opus_GetErrorDesc( int errorCode )
{
	switch( errorCode )
	{
	case OP_FALSE:
		return "request failed";
	case OP_EOF:
		return "end of file reached";
	case OP_HOLE:
		return "there was a hole in the stream";
	case OP_EREAD:
		return "read error occurred";
	case OP_EFAULT:
		return "fault issue occurred";
	case OP_EIMPL:
		return "feature not implemented";
	case OP_EINVAL:
		return "invalid argument";
	case OP_ENOTFORMAT:
		return "not a valid file format";
	case OP_EBADHEADER:
		return "bad header";
	case OP_EVERSION:
		return "version mismatch";
	case OP_EBADPACKET:
		return "bad packet";
	case OP_EBADLINK:
		return "bad link found";
	case OP_ENOSEEK:
		return "bitstream not seekable";
	case OP_EBADTIMESTAMP:
		return "invalid timestamp";
	default:
		return "unknown error";
	}
}

qboolean Sound_LoadOggOpus( const char *name, const byte *buffer, fs_offset_t filesize )
{
	int ret;
	ogg_filestream_t file;
	OggOpusFile      *of;
	const OpusHead   *opusHead;
	size_t written = 0;

	if( !buffer )
		return false;

	OggFilestream_Init( &file, name, buffer, filesize );
	of = op_open_callbacks( &file, &op_callbacks_membuf, NULL, 0, &ret );
	if( !of )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): %s\n", __func__, file.name, Opus_GetErrorDesc( ret ));
		return false;
	}

	opusHead = op_head( of, -1 );
	if( opusHead->channel_count < 1 || opusHead->channel_count > 2 )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): unsuppored channels count\n", __func__, file.name );
		return false;
	}

	// according to OggOpus specification, sound always encoded at 48kHz sample rate
	// but this isn't a problem, engine can do resampling
	sound.channels = opusHead->channel_count;
	sound.rate = 48000;
	sound.width = 2; // always 16-bit PCM
	sound.type = WF_PCMDATA;
	sound.samples = op_pcm_total( of, -1 );
	sound.size = sound.samples * sound.width * sound.channels;
	sound.wav = (byte *)Mem_Calloc( host.soundpool, sound.size );

	// skip undesired samples before playing sound
	if(( ret = op_pcm_seek( of, opusHead->pre_skip )) < 0 )
	{
		Con_DPrintf( S_ERROR "%s: failed to pre-skip (%s): %s\n", __func__, file.name, Opus_GetErrorDesc( ret ));
		return false;
	}

	SetBits( sound.flags, SOUND_RESAMPLE );
	Sound_ScanOpusComments( of );

	while(( ret = op_read( of, (opus_int16 *)( sound.wav + written ), ( sound.size - written ) / sound.width, NULL )) != 0 )
	{
		if( ret < 0 )
		{
			Con_DPrintf( S_ERROR "%s: failed to read (%s): %s\n", __func__, file.name, Opus_GetErrorDesc( ret ));
			return false;
		}
		written += ret * sound.width * sound.channels;
	}

	op_free( of );
	return true;
}

stream_t *Stream_OpenOggOpus( const char *filename )
{
	int ret;
	stream_t *stream;
	opus_streaming_ctx_t *ctx;
	const OpusHead       *opusHead;

	ctx = (opus_streaming_ctx_t *)Mem_Calloc( host.soundpool, sizeof( opus_streaming_ctx_t ));
	ctx->file = FS_Open( filename, "rb", false );
	if( !ctx->file )
	{
		Mem_Free( ctx );
		return NULL;
	}

	stream = (stream_t *)Mem_Calloc( host.soundpool, sizeof( stream_t ));
	stream->file = ctx->file;
	stream->pos = 0;

	ctx->of = op_open_callbacks( ctx, &op_callbacks_fs, NULL, 0, &ret );
	if( !ctx->of )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): %s\n", __func__, filename, Opus_GetErrorDesc( ret ));
		FS_Close( ctx->file );
		Mem_Free( stream );
		Mem_Free( ctx );
		return NULL;
	}

	opusHead = op_head( ctx->of, -1 );
	if( opusHead->channel_count < 1 || opusHead->channel_count > 2 )
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): unsuppored channels count\n", __func__, filename );
		op_free( ctx->of );
		FS_Close( ctx->file );
		Mem_Free( stream );
		Mem_Free( ctx );
		return NULL;
	}

	// skip undesired samples before playing sound
	if(( ret = op_pcm_seek( ctx->of, opusHead->pre_skip )) < 0 )
	{
		Con_DPrintf( S_ERROR "%s: failed to pre-skip (%s): %s\n", __func__, filename, Opus_GetErrorDesc( ret ));
		op_free( ctx->of );
		FS_Close( ctx->file );
		Mem_Free( stream );
		Mem_Free( ctx );
		return NULL;
	}

	stream->buffsize = 0; // how many samples left from previous frame
	stream->channels = opusHead->channel_count;
	stream->rate = 48000; // that's fixed at 48kHz for Opus format
	stream->width = 2;    // always 16 bit
	stream->ptr = ctx;
	stream->type = WF_OPUSDATA;

	return stream;
}

int Stream_ReadOggOpus( stream_t *stream, int needBytes, void *buffer )
{
	int bytesWritten = 0;
	opus_streaming_ctx_t *ctx = (opus_streaming_ctx_t *)stream->ptr;

	while( 1 )
	{
		int  ret;
		byte *data;
		int  outsize;

		if( !stream->buffsize )
		{
			ret = op_read( ctx->of, (opus_int16 *)stream->temp, OUTBUF_SIZE / stream->width, NULL );
			if( ret == 0 )
				break; // end of file
			else if( ret < 0 )
				Con_DPrintf( S_ERROR "%s: error during read: %s\n", __func__, Opus_GetErrorDesc( ret ));
			else
				stream->pos = ret * stream->width * stream->channels;
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

int Stream_SetPosOggOpus( stream_t *stream, int newpos )
{
	int ret;
	opus_streaming_ctx_t *ctx = (opus_streaming_ctx_t *)stream->ptr;
	if(( ret = op_raw_seek( ctx->of, newpos )) == 0 )
	{
		stream->buffsize = 0; // flush any previous data
		return true;
	}
	Con_DPrintf( S_ERROR "%s: error during seek: %s\n", __func__, Opus_GetErrorDesc( ret ));
	return false; // failed to seek
}

int Stream_GetPosOggOpus( stream_t *stream )
{
	opus_streaming_ctx_t *ctx = (opus_streaming_ctx_t *)stream->ptr;
	return op_raw_tell( ctx->of );
}

void Stream_FreeOggOpus( stream_t *stream )
{
	if( stream->ptr )
	{
		opus_streaming_ctx_t *ctx = (opus_streaming_ctx_t *)stream->ptr;
		op_free( ctx->of );
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
