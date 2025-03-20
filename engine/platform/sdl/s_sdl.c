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
#include "platform/platform.h"
#if XASH_SOUND == SOUND_SDL

#include "sound.h"
#include "voice.h"

#if XASH_SDL == 3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <stdlib.h>

#define SAMPLE_16BIT_SHIFT 1
#define SECONDARY_BUFFER_SIZE 0x10000

#if ! SDL_VERSION_ATLEAST( 2, 0, 0 )
#define SDL_GetCurrentAudioDriver() "legacysdl"
#define SDL_OpenAudioDevice( a, b, c, d, e ) SDL_OpenAudio( ( c ), ( d ) )
#define SDL_CloseAudioDevice( a ) SDL_CloseAudio()
#define SDL_PauseAudioDevice( a, b ) SDL_PauseAudio( ( b ) )
#define SDL_LockAudioDevice( x ) SDL_LockAudio()
#define SDL_UnlockAudioDevice( x ) SDL_UnlockAudio()
#define SDLash_IsAudioError( x ) (( x ) != 0)
#elif SDL_VERSION_ATLEAST( 3, 2, 0 )
#define SDLash_IsAudioError( x ) (!(x))
#else
#define SDLash_IsAudioError( x ) (( x ) == 0)
#endif

#if SDL_MAJOR_VERSION >= 3
// SDL3 moved to booleans, no more weird code with != 0 or < 0
#define SDL_SUCCESS(expr) (expr)
#else
#define SDL_SUCCESS(expr) ((expr) == 0)
#endif

/*
=======================================================================
Global variables. Must be visible to window-procedure function
so it can unlock and free the data block after it has been played.
=======================================================================
*/
#if SDL_MAJOR_VERSION >= 3
static SDL_AudioStream *sdl_dev = NULL;
static SDL_AudioStream *in_dev = NULL;
#else
static SDL_AudioDeviceID sdl_dev = 0;
static SDL_AudioDeviceID in_dev = 0;
#endif
static SDL_AudioFormat sdl_format;
static char sdl_backend_name[32];

#if SDL_MAJOR_VERSION >= 3
static void SDL_SoundCallback( void *userdata, SDL_AudioStream *stream, int len, int totalAmount )
#else
static void SDL_SoundCallback( void *userdata, Uint8 *stream, int len )
#endif
{
	const int size = dma.samples << 1;
	int pos;
	int wrapped;

#if ! SDL_VERSION_ATLEAST( 2, 0, 0 )
	if( !dma.buffer )
	{
#if !SDL_VERSION_ATLEAST(3, 2, 0)
		memset( stream, 0, len );
#endif
		return;
	}
#endif

	pos = dma.samplepos << 1;
	if( pos >= size )
		pos = dma.samplepos = 0;

	wrapped = pos + len - size;

	if( wrapped < 0 )
	{
#if SDL_VERSION_ATLEAST(3, 2, 0)
		SDL_PutAudioStreamData(stream, dma.buffer + pos, len);
#else
		memcpy( stream, dma.buffer + pos, len );
#endif
		dma.samplepos += len >> 1;
	}
	else
	{
		int remaining = size - pos;

#if SDL_VERSION_ATLEAST(3, 2, 0)
		SDL_PutAudioStreamData(stream, dma.buffer + pos, remaining);
		SDL_PutAudioStreamData(stream, dma.buffer, wrapped);
#else
		memcpy( stream, dma.buffer + pos, remaining );
		memcpy( stream + remaining, dma.buffer, wrapped );
#endif
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
	SDL_AudioSpec desired;
#if !SDL_VERSION_ATLEAST(3, 2, 0)
	SDL_AudioSpec obtained;
#endif
	int samplecount;
#if XASH_WIN32
	const char *driver = NULL;
#endif

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
#if SDL_VERSION_ATLEAST( 3, 2, 0 )
	// Er2: Should be this removed?
	SDL_setenv_unsafe( "PULSE_PROP_application.name", GI->title, 1 );
	SDL_setenv_unsafe( "PULSE_PROP_media.role", "game", 1 );
#else
	SDL_setenv( "PULSE_PROP_application.name", GI->title, 1 );
	SDL_setenv( "PULSE_PROP_media.role", "game", 1 );
#endif

	// reinitialize SDL with our driver just in case
	if( SDL_WasInit( SDL_INIT_AUDIO ))
		SDL_QuitSubSystem( SDL_INIT_AUDIO );

	if( !SDL_SUCCESS(SDL_InitSubSystem( SDL_INIT_AUDIO )) )
	{
		Con_Reportf( S_ERROR "Audio: SDL: %s \n", SDL_GetError( ) );
		return false;
	}

	memset( &desired, 0, sizeof( desired ) );
	desired.freq     = SOUND_DMA_SPEED;
	desired.channels = 2;
#if SDL_VERSION_ATLEAST( 3, 2, 0 )
	desired.format   = SDL_AUDIO_S16LE;
#else
	desired.format   = AUDIO_S16LSB;
	desired.samples  = 1024;
	desired.callback = SDL_SoundCallback;
#endif

#if SDL_VERSION_ATLEAST( 3, 2, 0 )
	sdl_dev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, SDL_SoundCallback, NULL);
#else
	sdl_dev = SDL_OpenAudioDevice( NULL, 0, &desired, &obtained, 0 );
#endif

	if( SDLash_IsAudioError( sdl_dev ))
	{
		Con_Printf( "Couldn't open SDL audio: %s\n", SDL_GetError( ) );
		return false;
	}

#if !SDL_VERSION_ATLEAST( 3, 2, 0 )
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
#endif

	samplecount = s_samplecount.value;
	if( !samplecount )
		samplecount = 0x8000;

#if SDL_VERSION_ATLEAST( 3, 2, 0 ) // SDL audio streams have spec, not desired, that's why we just copy values from them
	dma.format.speed    = desired.freq;
	dma.format.channels = desired.channels;
	dma.samples         = samplecount * desired.channels;

	sdl_format = desired.format;
#else
	dma.format.speed    = obtained.freq;
	dma.format.channels = obtained.channels;
	dma.samples         = samplecount * obtained.channels;

	sdl_format = obtained.format;
#endif
	dma.format.width    = 2;
	dma.buffer          = Mem_Calloc( sndpool, dma.samples * 2 );
	dma.samplepos       = 0;

	Con_Printf( "Using SDL audio driver: %s @ %d Hz\n", SDL_GetCurrentAudioDriver( ), dma.format.speed );
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
#if !SDL_VERSION_ATLEAST(3, 2, 0)
	SDL_LockAudioDevice( sdl_dev );
#endif
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
#if !SDL_VERSION_ATLEAST(3, 2, 0)
	SDL_UnlockAudioDevice( sdl_dev );
#endif
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
#if SDL_VERSION_ATLEAST(3, 2, 0)
		SDL_DestroyAudioStream(sdl_dev);
#else
		SDL_CloseAudioDevice( sdl_dev );
#endif
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

#if SDL_VERSION_ATLEAST(3, 2, 0)
	if (active)
		SDL_ResumeAudioStreamDevice(sdl_dev);
	else
		SDL_PauseAudioStreamDevice(sdl_dev);
#else
	SDL_PauseAudioDevice( sdl_dev, !active );
#endif
}

/*
===========
SDL_SoundInputCallback
===========
*/
#if SDL_MAJOR_VERSION >= 3
static void SDL_SoundInputCallback( void *userdata, SDL_AudioStream *stream, int len, int totalAmount )
#else
static void SDL_SoundInputCallback( void *userdata, Uint8 *stream, int len )
#endif
{
	int size = Q_min( len, sizeof( voice.input_buffer ) - voice.input_buffer_pos );

	// engine can't keep up, skip audio
	if( !size )
		return;

#if SDL_VERSION_ATLEAST(3, 2, 0)
	SDL_GetAudioStreamData(stream, voice.input_buffer + voice.input_buffer_pos, size);
#else
	memcpy( voice.input_buffer + voice.input_buffer_pos, stream, size );
#endif
	voice.input_buffer_pos += size;
}

/*
===========
VoiceCapture_Init
===========
*/
qboolean VoiceCapture_Init( void )
{
	SDL_AudioSpec wanted;
#if !SDL_VERSION_ATLEAST(3, 2, 0)
	SDL_AudioSpec spec;
#endif

	if( !SDLash_IsAudioError( in_dev ))
	{
		VoiceCapture_Shutdown();
	}

	SDL_zero( wanted );
	wanted.freq = voice.samplerate;
	wanted.channels = VOICE_PCM_CHANNELS;
#if SDL_VERSION_ATLEAST( 3, 2, 0 )
	wanted.format = SDL_AUDIO_S16LE;
#else
	wanted.format = AUDIO_S16LSB;
	wanted.samples = voice.frame_size;
	wanted.callback = SDL_SoundInputCallback;
#endif

#if SDL_VERSION_ATLEAST( 3, 2, 0 )
	sdl_dev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &wanted, SDL_SoundInputCallback, NULL);
#else
	in_dev = SDL_OpenAudioDevice( NULL, SDL_TRUE, &wanted, &spec, 0 );
#endif


	if( SDLash_IsAudioError( in_dev ))
	{
		Con_Printf( "%s: error creating capture device (%s)\n", __func__, SDL_GetError() );
		return false;
	}

#if SDL_VERSION_ATLEAST(3, 2, 0)
	Con_Printf( S_NOTE "%s: capture device creation success (%p: %s)\n", __func__, in_dev, SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(in_dev)) );
#else
	Con_Printf( S_NOTE "%s: capture device creation success (%i: %s)\n", __func__, in_dev, SDL_GetAudioDeviceName( in_dev, SDL_TRUE ) );
#endif
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

#if SDL_VERSION_ATLEAST(3, 2, 0)
	if (activate)
		SDL_ResumeAudioStreamDevice(in_dev);
	else
		SDL_PauseAudioStreamDevice(in_dev);
#else
	SDL_PauseAudioDevice( in_dev, activate ? SDL_FALSE : SDL_TRUE );
#endif
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

#if !SDL_VERSION_ATLEAST( 3, 2, 0 ) // SDL3 have internal locks for streams
	if( lock ) SDL_LockAudioDevice( in_dev );
	else SDL_UnlockAudioDevice( in_dev );
#endif

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

#if SDL_VERSION_ATLEAST( 3, 2, 0 )
	SDL_DestroyAudioStream(in_dev);
#else
	SDL_CloseAudioDevice( in_dev );
#endif
}

#endif // XASH_SOUND == SOUND_SDL
