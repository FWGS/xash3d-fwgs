/*
s_ps4.c - PlayStation 4 sound output
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
The main audio port of the console plays 48 kHz only, the engine mixes music and raw
samples at SOUND_DMA_SPEED (44.1 kHz), so a thread takes the engine's DMA buffer and
resamples it into the port. sceAudioOutOutput waits until the hardware needs the next block,
that sets the pace.
*/

#include "common.h"
#include "platform/platform.h"
#include "sound.h"
#include <pthread.h>
#include <string.h>

// libSceAudioOut, OpenOrbis headers may not have prototypes for it
#define AUDIO_OUT_USER_SYSTEM       0xff // the main port belongs to the system, not to a user
#define AUDIO_OUT_PORT_MAIN         0
#define AUDIO_OUT_FORMAT_S16_STEREO 1
#define AUDIO_OUT_RATE              48000
#define AUDIO_OUT_GRAIN             512 // frames per sceAudioOutOutput, 256 * n

int sceAudioOutInit( void );
int sceAudioOutOpen( int32_t userId, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param );
int sceAudioOutOutput( int32_t handle, const void *ptr );
int sceAudioOutClose( int32_t handle );

static struct
{
	int handle;
	pthread_t thread;
	pthread_mutex_t lock; // recursive, the mixer expects a lock it can take twice
	qboolean lock_ready;
	qboolean running;     // thread is alive
	qboolean active;      // false while the game is paused by the system, silence goes out
	double pos;           // position in frames of snd.buffer, fractional because of resampling
	int16_t grain[AUDIO_OUT_GRAIN * 2];
} ps4snd = { -1 };

// called with the lock held
static void PS4_SoundFill( int16_t *out )
{
	const int frames = snd.samples >> 1; // stereo frames in the buffer of the engine
	const int16_t *in = (const int16_t *)snd.buffer;
	const double step = (double)SOUND_DMA_SPEED / (double)AUDIO_OUT_RATE;
	double pos = ps4snd.pos;

	for( int i = 0; i < AUDIO_OUT_GRAIN; i++ )
	{
		const int i0 = (int)pos;
		const int i1 = i0 + 1 < frames ? i0 + 1 : 0;
		const float frac = (float)( pos - i0 );

		// linear interpolation between neighbour frames
		out[i * 2] = (int16_t)( in[i0 * 2] + ( in[i1 * 2] - in[i0 * 2] ) * frac );
		out[i * 2 + 1] = (int16_t)( in[i0 * 2 + 1] + ( in[i1 * 2 + 1] - in[i0 * 2 + 1] ) * frac );

		pos += step;
		if( pos >= frames )
			pos -= frames;
	}

	ps4snd.pos = pos;
	// the mixer counts played position in 16-bit samples
	snd.samplepos = (int)pos * 2;
}

static void *PS4_SoundThread( void *arg )
{
	while( ps4snd.running )
	{
		int res;

		pthread_mutex_lock( &ps4snd.lock );
		if( ps4snd.active && snd.buffer )
			PS4_SoundFill( ps4snd.grain );
		else
			memset( ps4snd.grain, 0, sizeof( ps4snd.grain ));
		pthread_mutex_unlock( &ps4snd.lock );

		res = sceAudioOutOutput( ps4snd.handle, ps4snd.grain );
		if( res < 0 )
		{
			Con_Reportf( S_ERROR "sceAudioOutOutput failed: 0x%08x\n", res );
			break;
		}
	}

	return NULL;
}

/*
==================
SNDDMA_Init

Opens the main audio port
==================
*/
qboolean SNDDMA_Init( void )
{
	pthread_mutexattr_t attr;
	int samplecount;

	sceAudioOutInit(); // fails if somebody has done it already, which is fine

	ps4snd.handle = sceAudioOutOpen( AUDIO_OUT_USER_SYSTEM, AUDIO_OUT_PORT_MAIN, 0, AUDIO_OUT_GRAIN, AUDIO_OUT_RATE, AUDIO_OUT_FORMAT_S16_STEREO );
	if( ps4snd.handle < 0 )
	{
		Con_Printf( S_ERROR "Audio: sceAudioOutOpen failed: 0x%08x\n", ps4snd.handle );
		ps4snd.handle = -1;
		return false;
	}

	if( !ps4snd.lock_ready )
	{
		pthread_mutexattr_init( &attr );
		pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
		pthread_mutex_init( &ps4snd.lock, &attr );
		pthread_mutexattr_destroy( &attr );
		ps4snd.lock_ready = true;
	}

	// the engine mixes at its own rate, resampling is up to us
	snd.format.speed    = SOUND_DMA_SPEED;
	snd.format.channels = 2;
	snd.format.width    = 2;
	samplecount = s_samplecount.value;
	if( !samplecount )
		samplecount = 0x8000;
	snd.samples         = samplecount * snd.format.channels;
	snd.buffer          = Mem_Calloc( sndpool, snd.samples * 2 );
	snd.samplepos       = 0;

	ps4snd.pos = 0.0;
	ps4snd.active = true;
	ps4snd.running = true;

	if( pthread_create( &ps4snd.thread, NULL, PS4_SoundThread, NULL ) != 0 )
	{
		Con_Printf( S_ERROR "Audio: can't start the output thread\n" );
		ps4snd.running = false;
		SNDDMA_Shutdown();
		return false;
	}

	Con_Printf( "Using PS4 audio output: %d Hz (mixing at %d Hz)\n", AUDIO_OUT_RATE, SOUND_DMA_SPEED );
	snd.initialized = true;
	snd.backend_name = "PS4 AudioOut";

	return true;
}

/*
==============
SNDDMA_BeginPainting

Makes sure snd.buffer is valid and not read by the output thread
===============
*/
void SNDDMA_BeginPainting( void )
{
	if( ps4snd.lock_ready )
		pthread_mutex_lock( &ps4snd.lock );
}

/*
==============
SNDDMA_Submit

The thread reads the buffer by itself, this only lets it go
===============
*/
void SNDDMA_Submit( void )
{
	if( ps4snd.lock_ready )
		pthread_mutex_unlock( &ps4snd.lock );
}

/*
==============
SNDDMA_Shutdown

Stops the thread and closes the port
===============
*/
void SNDDMA_Shutdown( void )
{
	Con_Printf( "Shutting down audio.\n" );
	snd.initialized = false;

	if( ps4snd.running )
	{
		ps4snd.running = false;
		pthread_join( ps4snd.thread, NULL ); // waits for the block in progress
	}

	if( ps4snd.handle >= 0 )
	{
		sceAudioOutClose( ps4snd.handle );
		ps4snd.handle = -1;
	}

	if( snd.buffer )
	{
		Mem_Free( snd.buffer );
		snd.buffer = NULL;
	}
}

/*
===========
SNDDMA_Activate

Pauses the output when the game is not shown
===========
*/
void SNDDMA_Activate( qboolean active )
{
	if( !ps4snd.lock_ready )
		return;

	pthread_mutex_lock( &ps4snd.lock );
	ps4snd.active = active;
	pthread_mutex_unlock( &ps4snd.lock );
}
