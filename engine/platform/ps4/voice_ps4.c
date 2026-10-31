/*
voice_ps4.c - PlayStation 4 voice capture
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
The microphone (headset or camera) is read through libSceAudioIn. It gives 16 kHz mono,
a thread reads it in blocks and resamples into the engine's voice input buffer.
*/

#include "common.h"
#include "platform/platform.h"
#include "voice.h"
#include <math.h>
#include <pthread.h>
#include <orbis/UserService.h>

// libSceAudioIn, OpenOrbis headers have no prototypes for it
#define AUDIO_IN_TYPE_VOICE_CHAT 0
#define AUDIO_IN_TYPE_GENERAL    1
#define AUDIO_IN_PARAM_S16_MONO  0
#define AUDIO_IN_FREQ            16000
#define AUDIO_IN_GRAIN           256 // samples per sceAudioInInput

int sceAudioInOpen( int32_t userId, uint32_t type, uint32_t index, uint32_t len, uint32_t freq, uint32_t param );
int sceAudioInInput( int32_t handle, void *dest );
int sceAudioInClose( int32_t handle );

static struct
{
	int handle;
	pthread_t thread;
	pthread_mutex_t lock;
	qboolean running;  // thread is alive
	qboolean active;   // samples go to the engine
	double pos;        // resampler position between input samples
	int16_t last;      // previous input sample
} ps4voice = { -1 };

static void PS4_VoiceAppend( const int16_t *in, int count )
{
	const double step = (double)AUDIO_IN_FREQ / (double)voice.samplerate;
	int16_t *out = (int16_t *)( voice.input_buffer + voice.input_buffer_pos );
	int space = ( sizeof( voice.input_buffer ) - voice.input_buffer_pos ) / sizeof( int16_t );
	int written = 0;

	// linear interpolation, position -1 is the last sample of the previous block
	while( ps4voice.pos < count && written < space )
	{
		int i = (int)floor( ps4voice.pos );
		double frac = ps4voice.pos - i;
		int a = i < 0 ? ps4voice.last : in[i];
		int b = i + 1 < count ? in[i + 1] : in[count - 1];

		out[written++] = (int16_t)( a + ( b - a ) * frac );
		ps4voice.pos += step;
	}

	// engine can't keep up, the rest of the block is skipped
	if( ps4voice.pos < count )
		ps4voice.pos = count;

	ps4voice.pos -= count;
	ps4voice.last = in[count - 1];
	voice.input_buffer_pos += written * sizeof( int16_t );
}

static void *PS4_VoiceThread( void *arg )
{
	int16_t block[AUDIO_IN_GRAIN];

	while( ps4voice.running )
	{
		int res = sceAudioInInput( ps4voice.handle, block );

		if( res < 0 )
		{
			char msg[64];

			Q_snprintf( msg, sizeof( msg ), S_ERROR "sceAudioInInput failed: 0x%08x\n", res );
			Sys_PrintLog( msg );
			break;
		}

		pthread_mutex_lock( &ps4voice.lock );
		if( ps4voice.active )
			PS4_VoiceAppend( block, AUDIO_IN_GRAIN );
		pthread_mutex_unlock( &ps4voice.lock );
	}

	return NULL;
}

qboolean VoiceCapture_Init( void )
{
	int32_t user = -1;
	int res;

	VoiceCapture_Shutdown();

	if(( res = sceUserServiceGetInitialUser( &user )) < 0 )
	{
		Con_Printf( S_ERROR "%s: no user: 0x%08x\n", __func__, res );
		return false;
	}

	// voice chat type follows the headset of the user, general is any microphone
	ps4voice.handle = sceAudioInOpen( user, AUDIO_IN_TYPE_VOICE_CHAT, 0, AUDIO_IN_GRAIN, AUDIO_IN_FREQ, AUDIO_IN_PARAM_S16_MONO );
	if( ps4voice.handle < 0 )
	{
		Con_Reportf( "%s: voice chat input: 0x%08x, trying general\n", __func__, ps4voice.handle );
		ps4voice.handle = sceAudioInOpen( user, AUDIO_IN_TYPE_GENERAL, 0, AUDIO_IN_GRAIN, AUDIO_IN_FREQ, AUDIO_IN_PARAM_S16_MONO );
	}

	if( ps4voice.handle < 0 )
	{
		Con_Printf( "%s: no microphone: 0x%08x\n", __func__, ps4voice.handle );
		ps4voice.handle = -1;
		return false;
	}

	pthread_mutex_init( &ps4voice.lock, NULL );
	ps4voice.active = false;
	ps4voice.running = true;
	ps4voice.pos = 0.0;
	ps4voice.last = 0;

	if( pthread_create( &ps4voice.thread, NULL, PS4_VoiceThread, NULL ) != 0 )
	{
		Con_Printf( S_ERROR "%s: can't start capture thread\n", __func__ );
		ps4voice.running = false;
		sceAudioInClose( ps4voice.handle );
		ps4voice.handle = -1;
		pthread_mutex_destroy( &ps4voice.lock );
		return false;
	}

	Con_Printf( S_NOTE "%s: microphone opened, %d Hz resampled to %d Hz\n", __func__, AUDIO_IN_FREQ, voice.samplerate );
	return true;
}

qboolean VoiceCapture_Activate( qboolean activate )
{
	if( ps4voice.handle < 0 )
		return false;

	pthread_mutex_lock( &ps4voice.lock );
	ps4voice.active = activate;
	ps4voice.pos = 0.0;
	pthread_mutex_unlock( &ps4voice.lock );
	return true;
}

qboolean VoiceCapture_Lock( qboolean lock )
{
	if( ps4voice.handle < 0 )
		return false;

	if( lock )
		pthread_mutex_lock( &ps4voice.lock );
	else
		pthread_mutex_unlock( &ps4voice.lock );
	return true;
}

void VoiceCapture_Shutdown( void )
{
	if( ps4voice.handle < 0 )
		return;

	// the thread is blocked in sceAudioInInput at most for one block (16 ms)
	ps4voice.running = false;
	pthread_join( ps4voice.thread, NULL );
	sceAudioInClose( ps4voice.handle );
	ps4voice.handle = -1;
	pthread_mutex_destroy( &ps4voice.lock );
}
