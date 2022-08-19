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

#include "protocol.h" // MAX_CLIENTS
#include "sound.h"

extern convar_t voice_scale;

typedef struct OpusDecoder OpusDecoder;
typedef struct OpusEncoder OpusEncoder;

#define VOICE_LOCALPLAYER_INDEX (-2)

typedef struct voice_status_s
{
	qboolean talking_ack;
	double talking_timeout;
} voice_status_t;

typedef struct voice_state_s
{
	qboolean initialized;
	qboolean is_recording;
	double start_time;

	voice_status_t local;
	voice_status_t players_status[MAX_CLIENTS];

	// opus stuff
	OpusEncoder *encoder;
	OpusDecoder *decoder;

	// audio info
	uint channels;
	uint width;
	uint samplerate;
	uint frame_size;

	// buffers
	byte input_buffer[MAX_RAW_SAMPLES];
	byte output_buffer[MAX_RAW_SAMPLES];
	byte decompress_buffer[MAX_RAW_SAMPLES];
	fs_offset_t input_buffer_pos;

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
qboolean Voice_Init( const char *pszCodecName, int quality );
void Voice_DeInit( void );
void Voice_Idle( double frametime );
qboolean Voice_IsRecording( void );
void Voice_RecordStop( void );
void Voice_RecordStart( void );
void Voice_Disconnect( void );
void Voice_AddIncomingData( int ent, const byte *data, uint size, uint frames );
void Voice_StatusAck( voice_status_t *status, int playerIndex );

#endif // VOICE_H
