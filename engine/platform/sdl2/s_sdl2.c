/*
s_backend.c - sound hardware output
Copyright (C) 2009 Uncle Mike

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

#define SDLash_IsAudioError( x ) (( x ) == 0)

/*
=======================================================================
Global variables. Must be visible to window-procedure function
so it can unlock and free the data block after it has been played.
=======================================================================
*/
static int sdl_dev;
static SDL_AudioDeviceID in_dev = 0;
static SDL_AudioFormat sdl_format;
static char sdl_backend_name[32];

static void SDL_SoundCallback( void *userdata, Uint8 *stream, int len )
{
	const int size = dma.samples << 1;
	int pos;
	int wrapped;

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

	// Modders often tend to use proprietary crappy solutions
	// like FMOD to play music, sometimes even with versions outdated by a few decades!
	//
	// As these bullshit sound engines prefer to use DirectSound, we ask SDL2 to do
	// the same. Why you might ask? If SDL2 uses another audio API, like WASAPI on
	// more modern versions of Windows, it breaks the logic inside Windows, and the whole
	// application could hang in WaitFor{Single,Multiple}Object function, either called by
	// SDL2 if FMOD was shut down first, or deep in dsound.dll->fmod.dll if SDL2 audio
	// was shut down first.
	//
	// I honestly don't know who is the real culprit here: FMOD, HL modders, Windows, SDL2
	// or us.
	//
	// Also, fun note, GoldSrc seems doesn't use SDL2 for sound stuff at all, as nothing
	// reference SDL audio functions there. It's probably has DirectSound backend, that's
	// why modders never stumble upon this bug.
#if XASH_WIN32
	driver = "directsound";

	if( SDL_getenv( "SDL_AUDIODRIVER" ))
		driver = NULL; // let SDL2 and user decide

	SDL_SetHint( SDL_HINT_AUDIODRIVER, driver );
#endif // XASH_WIN32

	// even if we don't have PA
	// we still can safely set env variables
	SDL_setenv( "PULSE_PROP_application.name", GI->title, 1 );
	SDL_setenv( "PULSE_PROP_media.role", "game", 1 );

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

	sdl_format = obtained.format;

	Con_Printf( "Using SDL audio driver: %s @ %d Hz\n", SDL_GetCurrentAudioDriver( ), obtained.freq );
	Q_snprintf( sdl_backend_name, sizeof( sdl_backend_name ), "SDL (%s)", SDL_GetCurrentAudioDriver( ));
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

#if !XASH_EMSCRIPTEN
		SDL_CloseAudioDevice( sdl_dev );
#endif
	}

#if !XASH_EMSCRIPTEN
	if( SDL_WasInit( SDL_INIT_AUDIO ))
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
#endif

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
SDL_SoundInputCallback
===========
*/
static void SDL_SoundInputCallback( void *userdata, Uint8 *stream, int len )
{
	int size = Q_min( len, sizeof( voice.input_buffer ) - voice.input_buffer_pos );

	// engine can't keep up, skip audio
	if( !size )
		return;

	memcpy( voice.input_buffer + voice.input_buffer_pos, stream, size );
	voice.input_buffer_pos += size;
}

/*
===========
VoiceCapture_Init
===========
*/
qboolean VoiceCapture_Init( void )
{
	SDL_AudioSpec wanted, spec;

	if( !SDLash_IsAudioError( in_dev ))
	{
		VoiceCapture_Shutdown();
	}

	SDL_zero( wanted );
	wanted.freq = voice.samplerate;
	wanted.format = AUDIO_S16LSB;
	wanted.channels = VOICE_PCM_CHANNELS;
	wanted.samples = voice.frame_size;
	wanted.callback = SDL_SoundInputCallback;

	in_dev = SDL_OpenAudioDevice( NULL, SDL_TRUE, &wanted, &spec, 0 );

	if( SDLash_IsAudioError( in_dev ))
	{
		Con_Printf( "%s: error creating capture device (%s)\n", __func__, SDL_GetError() );
		return false;
	}
		
	Con_Printf( S_NOTE "%s: capture device creation success (%i: %s)\n", __func__, in_dev, SDL_GetAudioDeviceName( in_dev, SDL_TRUE ) );
	return true;
}

/*
===========
VoiceCapture_Activate
===========
*/
qboolean VoiceCapture_Activate( qboolean activate )
{
	if( SDLash_IsAudioError( in_dev ))
		return false;

	SDL_PauseAudioDevice( in_dev, activate ? SDL_FALSE : SDL_TRUE );
	return true;
}

/*
===========
VoiceCapture_Lock
===========
*/
qboolean VoiceCapture_Lock( qboolean lock )
{
	if( SDLash_IsAudioError( in_dev ))
		return false;

	if( lock ) SDL_LockAudioDevice( in_dev );
	else SDL_UnlockAudioDevice( in_dev );

	return true;
}

/*
==========
VoiceCapture_Shutdown
==========
*/
void VoiceCapture_Shutdown( void )
{
	if( SDLash_IsAudioError( in_dev ))
		return;

	SDL_CloseAudioDevice( in_dev );
	in_dev = 0;
}

#endif // XASH_SOUND == SOUND_SDL
