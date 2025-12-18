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

#include "platform_sdl3.h"
#include "sound.h"
#include "voice.h"

/*
=======================================================================
Global variables. Must be visible to window-procedure function
so it can unlock and free the data block after it has been played.
=======================================================================
*/
static SDL_AudioStream *out_stream;
static char sdl_backend_name[32];

static void SDLash_OutputCallback( void *userdata, SDL_AudioStream *stream, int additional_amount, int len )
{
	const int size = dma.samples << 1;
	int pos;
	int wrapped;

	(void)userdata;
	(void)additional_amount;

	pos = dma.samplepos << 1;
	if( pos >= size )
		pos = dma.samplepos = 0;

	wrapped = pos + len - size;

	if( wrapped < 0 )
	{
		SDL_PutAudioStreamData( stream, dma.buffer + pos, len );
		dma.samplepos += len >> 1;
	}
	else
	{
		int remaining = size - pos;

		SDL_PutAudioStreamData( stream, dma.buffer + pos, remaining );
		SDL_PutAudioStreamData( stream, dma.buffer, wrapped );
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

	SDL_SetHint( SDL_HINT_AUDIO_DRIVER, driver );
#endif // XASH_WIN32

	if( SDL_WasInit( SDL_INIT_AUDIO ))
		SDL_QuitSubSystem( SDL_INIT_AUDIO );

	if( !SDL_InitSubSystem( SDL_INIT_AUDIO ))
	{
		Con_Reportf( S_ERROR "Audio: SDL: %s\n", SDL_GetError( ));
		return false;
	}

	const SDL_AudioSpec spec = {
		.freq = SOUND_DMA_SPEED,
		.format = SDL_AUDIO_S16,
		.channels = 2,
	};
	out_stream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
		&spec,
		SDLash_OutputCallback,
		NULL
	);

	if( !out_stream )
	{
		Con_PrintSDLError( "SDL_OpenAudioDeviceStream" );
		return false;
	}

	dma.format.speed = SOUND_DMA_SPEED;
	dma.format.channels = 2;
	dma.format.width = 2;
	int samplecount = s_samplecount.value;
	if( !samplecount )
		samplecount = 0x8000;
	dma.samples = samplecount * dma.format.channels;
	dma.buffer = Mem_Calloc( sndpool, dma.samples * dma.format.width );
	dma.samplepos = 0;
	dma.initialized = true;
	Q_snprintf( sdl_backend_name, sizeof( sdl_backend_name ), "SDL3 (%s)", SDL_GetCurrentAudioDriver( ));
	dma.backendName = sdl_backend_name;

	Con_Printf( "Using audio driver: %s @ %d Hz\n", sdl_backend_name, SOUND_DMA_SPEED );

	SNDDMA_Activate( true );

	return true;
}


/*
==============
SNDDMA_BeginPainting

Makes sure dma.buffer is valid
===============
*/
void SNDDMA_BeginPainting( void )
{
	SDL_LockAudioStream( out_stream );
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
	SDL_UnlockAudioStream( out_stream );
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

	if( out_stream )
	{
		SNDDMA_Activate( false );
		SDL_DestroyAudioStream( out_stream );
		out_stream = NULL;
	}

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

	if( active )
		SDL_ResumeAudioStreamDevice( out_stream );
	else
		SDL_PauseAudioStreamDevice( out_stream );
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
