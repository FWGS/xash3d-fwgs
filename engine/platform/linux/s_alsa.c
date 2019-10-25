/*
s_alsa.c - alsa sound hardware output
Copyright (C) 2019 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#if XASH_SOUND == SOUND_ALSA
#include <alsa/asoundlib.h>
#include "sound.h"

#define BUFFER_SAMPLES 4096
#define SUBMISSION_CHUNK BUFFER_SAMPLES / 2

static struct s_alsa_t
{
	snd_pcm_t *pcm_handle;
	snd_pcm_hw_params_t *hw_params;

	struct sndinfo * si;

	int period_size;
} s_alsa;

/*
==================
SNDDMA_Init

Initialize ALSA pcm device, and bind it to sndinfo.
==================
*/
qboolean SNDDMA_Init( void )
{
	int err, dir = 0;
	unsigned int r;
	snd_pcm_uframes_t p = 0;
	string device = "default";

	Sys_GetParmFromCmdLine( "-alsadev", device );

	if( ( err = snd_pcm_open( &s_alsa.pcm_handle, device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK ) ) < 0)
	{
		Con_Printf( "ALSA: cannot open device %s(%s)\n", device, snd_strerror( err ) );
		return false;
	}

	if( ( err = snd_pcm_hw_params_malloc( &s_alsa.hw_params )) < 0 )
	{
		Con_Printf( "ALSA: cannot allocate hw params(%s)\n", snd_strerror( err ) );
		snd_pcm_close(s_alsa.pcm_handle);
		return false;
	}

	if( ( err = snd_pcm_hw_params_any( s_alsa.pcm_handle, s_alsa.hw_params ) ) < 0 )
	{
		Con_Printf( "ALSA: cannot init hw params(%s)\n", snd_strerror( err ) );
		snd_pcm_hw_params_free(	s_alsa.hw_params );
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}

	if( ( err = snd_pcm_hw_params_set_access( s_alsa.pcm_handle, s_alsa.hw_params, SND_PCM_ACCESS_RW_INTERLEAVED ) ) < 0 )
	{
		Con_Printf( "ALSA: cannot set access(%s)\n", snd_strerror( err ) );
		snd_pcm_hw_params_free( s_alsa.hw_params );
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}

	if( ( err = snd_pcm_hw_params_set_format( s_alsa.pcm_handle, s_alsa.hw_params, SND_PCM_FORMAT_S16 ) ) < 0 )
	{
		Con_Printf("ALSA: 16 bit not supported\n");
		snd_pcm_hw_params_free( s_alsa.hw_params );
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}

	dma.format.speed = SOUND_DMA_SPEED;
	r = dma.format.speed;

	if( ( err = snd_pcm_hw_params_set_rate_near( s_alsa.pcm_handle, s_alsa.hw_params, &r, &dir ) ) < 0 )
	{
		Con_Printf( "ALSA: cannot set rate %d(%s)\n", r, snd_strerror( err ) );
		snd_pcm_hw_params_free(s_alsa.hw_params);
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}
	else
	{
		// rate succeeded, but is perhaps slightly different
		if( dir != 0 )
		{
			Con_Printf( "ALSA: rate %d not supported, using %d\n", SOUND_DMA_SPEED, r );
			dma.format.speed = r;
			dir = 0;
		}
	}

	dma.format.channels = 2;

	if( ( err = snd_pcm_hw_params_set_channels(s_alsa.pcm_handle, s_alsa.hw_params, dma.format.channels ) ) < 0 )
	{
		Con_Printf( "ALSA: cannot set channels %d(%s)\n", 2, snd_strerror( err ) );
		snd_pcm_hw_params_free( s_alsa.hw_params );
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}

	p = BUFFER_SAMPLES / dma.format.channels;
	if( ( err = snd_pcm_hw_params_set_period_size_near( s_alsa.pcm_handle, s_alsa.hw_params, &p, &dir ) ) < 0 )
	{
		Con_Printf("ALSA: cannot set period size (%s)\n", snd_strerror(err));
		snd_pcm_hw_params_free(s_alsa.hw_params);
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}
	else
	{
		// period succeeded, but is perhaps slightly different
		if( dir != 0 )
		{
			snd_pcm_hw_params_get_period_size(s_alsa.hw_params, &p, NULL);
			Con_Printf( "ALSA: period %d not supported, using %lu\n", ( BUFFER_SAMPLES / dma.format.channels ), p );
			dir = 0;
		}
	}

	// set params
	if( ( err = snd_pcm_hw_params( s_alsa.pcm_handle, s_alsa.hw_params ) ) < 0 )
	{
		Con_Printf( "ALSA: cannot set params(%s)\n", snd_strerror( err ) );
		snd_pcm_hw_params_free(s_alsa.hw_params);
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}

	// request period size again
	snd_pcm_hw_params_get_period_size( s_alsa.hw_params, &p, NULL );
	s_alsa.period_size = p;
	Con_Printf( "ALSA: period size %lu\n", p );

	dma.buffer = Z_Malloc( BUFFER_SAMPLES * 2 );  //allocate pcm frame buffer

	dma.samplepos = 0;

	dma.samples = BUFFER_SAMPLES;
	dma.format.width = 2;
	dma.initialized = 1;
	snd_pcm_prepare( s_alsa.pcm_handle );

	return true;
}

/*
==============
SNDDMA_GetDMAPos

return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int SNDDMA_GetDMAPos( void )
{
	return dma.samplepos;
}

/*
==============
SNDDMA_GetSoundtime

update global soundtime
===============
*/
int SNDDMA_GetSoundtime( void )
{
	static int buffers, oldsamplepos;
	int samplepos, fullsamples;

	fullsamples = dma.samples / 2;

	// it is possible to miscount buffers
	// if it has wrapped twice between
	// calls to S_Update.  Oh well.
	samplepos = SNDDMA_GetDMAPos( );

	if( samplepos < oldsamplepos )
	{
		buffers++; // buffer wrapped

		if( paintedtime > 0x40000000 )
		{
			// time to chop things off to avoid 32 bit limits
			buffers     = 0;
			paintedtime = fullsamples;
			S_StopAllSounds( true );
		}
	}

	oldsamplepos = samplepos;

	return ( buffers * fullsamples + samplepos / 2 );
}


/*
==============
SNDDMA_Shutdown

Closes the ALSA pcm device and frees the dma buffer.
===============
*/
void SNDDMA_Shutdown(void)
{
	Con_Printf( "Shutting down audio.\n" );
	dma.initialized = false;

	if( dma.buffer )
	{
	  snd_pcm_drop( s_alsa.pcm_handle );
	  snd_pcm_close( s_alsa.pcm_handle );
	  Mem_Free( dma.buffer );
	  dma.buffer = NULL;
	}
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit( void )
{
	int avail = snd_pcm_avail_update( s_alsa.pcm_handle );

	while( avail >= s_alsa.period_size )
	{
		int size    = dma.samples << 1;
		int pos     = dma.samplepos << 1;
		unsigned long  len = s_alsa.period_size;
		int wrapped = pos + len - size;

		if( wrapped < 0 )
		{
			snd_pcm_writei( s_alsa.pcm_handle, dma.buffer + pos, len / 4 );
			dma.samplepos += len >> 1;
		}
		else
		{
			int remaining = size - pos;
			snd_pcm_writei( s_alsa.pcm_handle, dma.buffer + pos, remaining / 4 );
			snd_pcm_writei( s_alsa.pcm_handle, dma.buffer, wrapped / 4 );
			dma.samplepos = wrapped >> 1;
		}

		avail = snd_pcm_avail_update( s_alsa.pcm_handle );
	}
}

/*
==============
SNDDMA_BeginPainting

Callback provided by the engine in case we need it.  We don't.
==============
*/
void SNDDMA_BeginPainting(void)
{
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
	snd_pcm_pause( s_alsa.pcm_handle, active );
}

#endif
