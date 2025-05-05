/*
s_sdl1.c - sound hardware output
Copyright (C) 2009 Uncle Mike
Copyright (C) 2025 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "platform.h"
#if XASH_SOUND == SOUND_SDL

#include "sound.h"
#include "voice.h"

#include <SDL.h>
#include <stdlib.h>

#define SAMPLE_16BIT_SHIFT 1
#define SECONDARY_BUFFER_SIZE 0x10000

#define SDL_GetCurrentAudioDriver() "legacysdl"
#define SDL_OpenAudioDevice( a, b, c, d, e ) SDL_OpenAudio( ( c ), ( d ) )
#define SDL_CloseAudioDevice( a ) SDL_CloseAudio()
#define SDL_PauseAudioDevice( a, b ) SDL_PauseAudio( ( b ) )
#define SDL_LockAudioDevice( x ) SDL_LockAudio()
#define SDL_UnlockAudioDevice( x ) SDL_UnlockAudio()
#define SDLash_IsAudioError( x ) (( x ) != 0)

/*
=======================================================================
Global variables. Must be visible to window-procedure function
so it can unlock and free the data block after it has been played.
=======================================================================
*/
static int sdl_dev;
static char sdl_backend_name[32];

static void SDL_SoundCallback( void *userdata, Uint8 *stream, int len )
{
	const int size = dma.samples << 1;
	int pos;
	int wrapped;

	if( !dma.buffer )
	{
		memset( stream, 0, len );
		return;
	}

	pos = dma.samplepos << 1;
	if( pos >= size )
		pos = dma.samplepos = 0;

	wrapped = pos + len - size;

	if( wrapped < 0 )
	{
		memcpy( stream, dma.buffer + pos, len );
		dma.samplepos += len >> 1;
	}
	else
	{
		int remaining = size - pos;

		memcpy( stream, dma.buffer + pos, remaining );
		memcpy( stream + remaining, dma.buffer, wrapped );
		dma.samplepos = wrapped >> 1;
	}

	if( dma.samplepos >= size )
		dma.samplepos = 0;
}

/*
==================
SNDDMA_Init

Try to find a sound device to mix for.
Returns false if nothing is found.
==================
*/
qboolean SNDDMA_Init( void )
{
	SDL_AudioSpec desired, obtained;
	int samplecount;
	const char *driver = NULL;

	// reinitialize SDL with our driver just in case
	if( SDL_WasInit( SDL_INIT_AUDIO ))
		SDL_QuitSubSystem( SDL_INIT_AUDIO );

	if( SDL_InitSubSystem( SDL_INIT_AUDIO ))
	{
		Con_Reportf( S_ERROR "Audio: SDL: %s \n", SDL_GetError( ) );
		return false;
	}

	memset( &desired, 0, sizeof( desired ) );
	desired.freq     = SOUND_DMA_SPEED;
	desired.format   = AUDIO_S16LSB;
	desired.samples  = 1024;
	desired.channels = 2;
	desired.callback = SDL_SoundCallback;

	sdl_dev = SDL_OpenAudioDevice( NULL, 0, &desired, &obtained, 0 );

	if( SDLash_IsAudioError( sdl_dev ))
	{
		Con_Printf( "Couldn't open SDL audio: %s\n", SDL_GetError( ) );
		return false;
	}

	if( obtained.format != AUDIO_S16LSB )
	{
		Con_Printf( "SDL audio format %d unsupported.\n", obtained.format );
		goto fail;
	}

	if( obtained.channels != 1 && obtained.channels != 2 )
	{
		Con_Printf( "SDL audio channels %d unsupported.\n", obtained.channels );
		goto fail;
	}

	dma.format.speed    = obtained.freq;
	dma.format.channels = obtained.channels;
	dma.format.width    = 2;
	samplecount = s_samplecount.value;
	if( !samplecount )
		samplecount = 0x8000;
	dma.samples         = samplecount * obtained.channels;
	dma.buffer          = Mem_Calloc( sndpool, dma.samples * 2 );
	dma.samplepos       = 0;

	Con_Printf( "Using SDL audio driver: %s @ %d Hz\n", SDL_GetCurrentAudioDriver( ), obtained.freq );
	Q_snprintf( sdl_backend_name, sizeof( sdl_backend_name ), "SDL" );
	dma.initialized = true;
	dma.backendName = sdl_backend_name;

	SNDDMA_Activate( true );

	return true;

fail:
	SNDDMA_Shutdown( );
	return false;
}


/*
==============
SNDDMA_BeginPainting

Makes sure dma.buffer is valid
===============
*/
void SNDDMA_BeginPainting( void )
{
	SDL_LockAudioDevice( sdl_dev );
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
Also unlocks the dsound buffer
===============
*/
void SNDDMA_Submit( void )
{
	SDL_UnlockAudioDevice( sdl_dev );
}

/*
==============
SNDDMA_Shutdown

Reset the sound device for exiting
===============
*/
void SNDDMA_Shutdown( void )
{
	Con_Printf( "Shutting down audio.\n" );
	dma.initialized = false;

	if( sdl_dev )
	{
		SNDDMA_Activate( false );

		SDL_CloseAudioDevice( sdl_dev );
	}

	if( SDL_WasInit( SDL_INIT_AUDIO ))
		SDL_QuitSubSystem( SDL_INIT_AUDIO );

	if( dma.buffer )
	{
		Mem_Free( dma.buffer );
		dma.buffer = NULL;
	}
}

/*
===========
SNDDMA_Activate
Called when the main window gains or loses focus.
The window have been destroyed and recreated
between a deactivate and an activate.
===========
*/
void SNDDMA_Activate( qboolean active )
{
	if( !dma.initialized )
		return;

	SDL_PauseAudioDevice( sdl_dev, !active );
}

/*
===========
VoiceCapture_Init
===========
*/
qboolean VoiceCapture_Init( void )
{
	return false;
}

/*
===========
VoiceCapture_Activate
===========
*/
qboolean VoiceCapture_Activate( qboolean activate )
{
	return false;
}

/*
===========
VoiceCapture_Lock
===========
*/
qboolean VoiceCapture_Lock( qboolean lock )
{
	return false;
}

/*
==========
VoiceCapture_Shutdown
==========
*/
void VoiceCapture_Shutdown( void )
{
}

#endif // XASH_SOUND == SOUND_SDL
