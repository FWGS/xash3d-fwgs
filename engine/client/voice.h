/*
voice.h - voice chat implementation
Copyright (C) 2022 Velaron
Copyright (C) 2022 SNMetamorph

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef VOICE_H
#define VOICE_H

#include "common.h"
#include "protocol.h" // MAX_CLIENTS
#include "sound.h"

typedef struct OpusCustomEncoder OpusCustomEncoder;
typedef struct OpusCustomDecoder OpusCustomDecoder;
typedef struct OpusCustomMode OpusCustomMode;

#define VOICE_LOOPBACK_INDEX (-2)
#define VOICE_LOCALCLIENT_INDEX (-1)

#define VOICE_PCM_CHANNELS 1 // always mono

// never change these parameters when using opuscustom
#define VOICE_OPUS_CUSTOM_SAMPLERATE SOUND_44k
// must follow opus custom requirements
// also be divisible with MAX_RAW_SAMPLES
#define VOICE_OPUS_CUSTOM_FRAME_SIZE 1024
#define VOICE_OPUS_CUSTOM_CODEC "opus_custom_44k_512"

// a1ba: do not change, we don't have any re-encoding support now
#define VOICE_DEFAULT_CODEC VOICE_OPUS_CUSTOM_CODEC

typedef struct voice_status_s
{
	qboolean talking_ack;
	double talking_timeout;
} voice_status_t;

typedef struct voice_state_s
{
	string codec;
	int quality;

	qboolean initialized;
	qboolean is_recording;
	qboolean device_opened;
	double start_time;

	voice_status_t local;
	voice_status_t players_status[MAX_CLIENTS];

	// opus stuff
	OpusCustomMode    *custom_mode;
	OpusCustomEncoder *encoder;
	OpusCustomDecoder *decoders[MAX_CLIENTS];

	// audio info
	uint width;
	uint samplerate;
	uint frame_size; // in samples

	// buffers
	byte input_buffer[MAX_RAW_SAMPLES];
	byte compress_buffer[MAX_RAW_SAMPLES];
	byte decompress_buffer[MAX_RAW_SAMPLES];
	fs_offset_t input_buffer_pos; // in bytes

	// input from file
	wavdata_t *input_file;
	fs_offset_t input_file_pos; // in bytes

	// automatic gain control
	struct {
		int block_size;
		float current_gain;
		float next_gain;
		float gain_multiplier;
	} autogain;
} voice_state_t;

extern voice_state_t voice;

void CL_AddVoiceToDatagram( void );

void Voice_RegisterCvars( void );
qboolean Voice_Init( const char *pszCodecName, int quality, qboolean preinit );
void Voice_Idle( double frametime );
qboolean Voice_IsRecording( void );
void Voice_RecordStop( void );
void Voice_RecordStart( void );
void Voice_Disconnect( void );
void Voice_AddIncomingData( int ent, const byte *data, uint size, uint frames );
void Voice_StatusAck( voice_status_t *status, int playerIndex );

#endif // VOICE_H
