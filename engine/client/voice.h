#ifndef VOICE_H
#define VOICE_H

#include <opus.h>

#include "common.h"
#include "client.h"
#include "sound.h"
#include "soundlib/soundlib.h"
#include "library.h"

extern convar_t voice_scale;

typedef struct voice_state_s
{
	qboolean initialized;
	qboolean is_recording;
	float start_time;
	qboolean talking_ack;
	float talking_timeout;

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
} voice_state_t;

extern voice_state_t voice;

void CL_AddVoiceToDatagram( void );

void Voice_RegisterCvars( void );
qboolean Voice_Init( const char *pszCodecName, int quality );
void Voice_DeInit( void );
uint Voice_GetCompressedData( byte *out, uint maxsize, uint *frames );
void Voice_Idle( float frametime );
qboolean Voice_IsRecording( void );
void Voice_RecordStop( void );
void Voice_RecordStart( void );
void Voice_AddIncomingData( int ent, byte *data, uint size, uint frames );
qboolean Voice_GetLoopback( void );
void Voice_LocalPlayerTalkingAck( void );
void Voice_StartChannel( uint samples, byte *data, int entnum );

#endif // VOICE_H
