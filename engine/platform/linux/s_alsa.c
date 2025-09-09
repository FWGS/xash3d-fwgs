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

#define PERIOD_SIZE_DEFAULT 2048

static struct s_alsa_t
{
	snd_pcm_t *pcm_handle;
	snd_pcm_hw_params_t *hw_params;

	struct sndinfo * si;

	int period_size;
	qboolean period_npot;
	qboolean paused;
} s_alsa;

void SND_Pause_f( void )
{
	s_alsa.paused = Q_atoi( Cmd_Argv( 1 ) ) ;

	if( !s_alsa.paused )
	{
		snd_pcm_start( s_alsa.pcm_handle );
		snd_pcm_prepare( s_alsa.pcm_handle );
	}
	else
	{
		snd_pcm_drain( s_alsa.pcm_handle );
		snd_pcm_drop( s_alsa.pcm_handle );
	}
}


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
	int samples;

	Sys_GetParmFromCmdLine( "-alsadev", device );

	Cmd_AddRestrictedCommand("pcm_pause", SND_Pause_f, "set pcm pause (debug)" );

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

	p = PERIOD_SIZE_DEFAULT;
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
			Con_Printf( "ALSA: period %d not supported, using %lu\n", PERIOD_SIZE_DEFAULT, p );
			dir = 0;
		}
	}

	if( 0 ) //( p & (p - 1) ) == 0 ) // power of two
	{
		// normally, set samples to period * 2 to minimize latency
		samples = p * 2;
	}
	else
	{
		// if period is NPOT it cannot be used as dma.samples in Xash3D
		// and need more space to send buffer partially
		samples = 1;
		while( samples < p * 4 )
			samples <<= 1;
		s_alsa.period_npot = true;
	}
	s_alsa.period_size = p;

	p = samples;
	snd_pcm_hw_params_set_buffer_size_near( s_alsa.pcm_handle, s_alsa.hw_params, &p );
	Con_Printf( "ALSA: buffer size %lu\n", p );

	// set params
	if( ( err = snd_pcm_hw_params( s_alsa.pcm_handle, s_alsa.hw_params ) ) < 0 )
	{
		Con_Printf( "ALSA: cannot set params(%s)\n", snd_strerror( err ) );
		snd_pcm_hw_params_free(s_alsa.hw_params);
		snd_pcm_close( s_alsa.pcm_handle );
		return false;
	}

	dma.buffer = Mem_Calloc( sndpool, samples * 2 );  //allocate pcm frame buffer
	dma.samplepos = 0;
	dma.samples = samples;
	dma.format.width = 2;
	dma.initialized = 1;
	dma.backendName = "ALSA";

	snd_pcm_prepare( s_alsa.pcm_handle );
	snd_pcm_writei( s_alsa.pcm_handle, dma.buffer, 2 * s_alsa.period_size );
	snd_pcm_start( s_alsa.pcm_handle );

	return true;
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
	if( s_alsa.paused )
		return;

	if( s_alsa.period_npot )
	{
		int avail = snd_pcm_avail_update( s_alsa.pcm_handle );

		if( avail < 0 )
			snd_pcm_prepare( s_alsa.pcm_handle );

		while( avail >= s_alsa.period_size )
		{
			int size    = dma.samples << 1;
			int pos     = dma.samplepos << 1;
			unsigned long  len = s_alsa.period_size * 4;
			int wrapped = pos + len - size;
			int  w;

			if( wrapped < 0 )
			{
				w = snd_pcm_writei( s_alsa.pcm_handle, dma.buffer + pos, len / 4 );
				if( w < 0 )
				{
					snd_pcm_prepare(s_alsa.pcm_handle);
					return;
				}
				dma.samplepos += len >> 1;
			}
			else
			{
				int remaining = size - pos;
				w = snd_pcm_writei( s_alsa.pcm_handle, dma.buffer + pos, remaining / 4 );
				if( w < 0 )
				{
					snd_pcm_prepare(s_alsa.pcm_handle);
					return;
				}
				w = snd_pcm_writei( s_alsa.pcm_handle, dma.buffer, wrapped / 4 );
				if( w < 0 )
				{
					snd_pcm_prepare(s_alsa.pcm_handle);
					return;
				}
				dma.samplepos = wrapped >> 1;
			}

			avail = snd_pcm_avail_update( s_alsa.pcm_handle );
		}
	}
	else // period is 1/2 of samples
	{
		int s, w, frames;
		void *start;

		if( !dma.buffer )
			return;

		s = dma.samplepos * 2;
		start = (void *)&dma.buffer[s];
		frames = s_alsa.period_size / 2;
		// write to card
		if( ( w = snd_pcm_writei( s_alsa.pcm_handle, start, frames ) ) < 0)
		{
			// xrun occured
			snd_pcm_prepare( s_alsa.pcm_handle );
			return;
		}

		dma.samplepos += w * 2;  // mark progress

		if(dma.samplepos >= dma.samples)
		  dma.samplepos = 0;  // wrap buffer
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
	if( !dma.initialized )
		return;

	s_alsa.paused = !active;

	if( !s_alsa.paused )
	{
		snd_pcm_start( s_alsa.pcm_handle );
		snd_pcm_prepare( s_alsa.pcm_handle );
	}
	else
	{
		snd_pcm_drain( s_alsa.pcm_handle );
		snd_pcm_drop( s_alsa.pcm_handle );
	}
}

qboolean VoiceCapture_Init( void )
{
	return false;
}

qboolean VoiceCapture_Activate( qboolean activate )
{
	return false;
}

qboolean VoiceCapture_Lock( qboolean lock )
{
	return false;
}

void VoiceCapture_Shutdown( void )
{

}

#endif
