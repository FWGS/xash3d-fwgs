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
typedef struct OpusEncoder OpusEncoder;
typedef struct OpusDecoder OpusDecoder;

#define VOICE_LOOPBACK_INDEX (-2)
#define VOICE_LOCALCLIENT_INDEX (-1)

#define VOICE_PCM_CHANNELS 1 // always mono
#define VOICE_MAX_DATA_SIZE 8192
#define VOICE_MAX_GS_DATA_SIZE 4096

// never change these parameters when using opuscustom
#define VOICE_OPUS_CUSTOM_SAMPLERATE 44100
// must follow opus custom requirements
// also be divisible with MAX_RAW_SAMPLES
#define VOICE_OPUS_CUSTOM_FRAME_SIZE 1024
#define VOICE_OPUS_CUSTOM_CODEC "opus_custom_44k_512"

// a1ba: do not change, we don't have any re-encoding support now
#define VOICE_DEFAULT_CODEC VOICE_OPUS_CUSTOM_CODEC

// GoldSrc voice configuration
#define GS_MAX_DECOMPRESSED_SAMPLES 32768
#define GS_DEFAULT_SAMPLE_RATE 24000
#define GS_DEFAULT_FRAME_SIZE 480

// VPC (Voice Packet Control) types
enum gs_vpc_type {
    GS_VPC_VDATA_SILENCE  = 0,
    GS_VPC_VDATA_MILES 	  = 1,
	GS_VPC_VDATA_SPEEX    = 2,
	GS_VPC_VDATA_RAW      = 3,
	GS_VPC_VDATA_SILK     = 4,
    GS_VPC_VDATA_OPUS_PLC = 6,
    GS_VPC_SETSAMPLERATE  = 11,
    GS_VPC_UNKNOWN        = 10
};

typedef struct voice_status_s
{
	qboolean talking_ack;
	double talking_timeout;
} voice_status_t;

typedef struct voice_autogain_s
{
	int   block_size;
	float current_gain;
	float next_gain;
	float gain_multiplier;
} voice_autogain_t;

typedef struct voice_state_s
{
	string codec;
	int quality;
	qboolean goldsrc;

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

	OpusEncoder *gs_encoder;
	OpusDecoder *gs_decoders[MAX_CLIENTS];

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

	voice_autogain_t autogain;
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

qboolean Voice_IsGoldSrcMode( const char *codec );
qboolean Voice_IsOpusCustomMode( const char *codec );
int Voice_GetBitrateForQuality( int quality, qboolean goldsrc );

#endif // VOICE_H
