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

#include "soundlib.h"
#include "crtlib.h"
#include "ogg_filestream.h"
#include <opusfile.h>
#include <string.h>
#include <stdint.h>

static int OpusCallback_Read( void *datasource, byte *ptr, int nbytes )
{
	return OggFilestream_Read( ptr, 1, nbytes, datasource );
}

static opus_int64 OpusCallback_Tell( void *datasource )
{
	return OggFilestream_Tell( datasource );
}

static const OpusFileCallbacks op_callbacks_membuf = {
	OpusCallback_Read,
	OggFilestream_Seek,
	OpusCallback_Tell,
	NULL
};

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
