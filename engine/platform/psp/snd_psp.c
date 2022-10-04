/*
snd_psp.c - psp sound hardware output
Copyright (C) 2022 Sergey Galushko

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
#if XASH_SOUND == SOUND_PSP
#include <malloc.h>

#include <pspaudio.h>
#include <pspkernel.h>
#include <pspdmac.h>

#include "sound.h"

#define	PSP_NUM_AUDIO_SAMPLES	1024 // must be multiple of 64
#define PSP_OUTPUT_CHANNELS	2
#define PSP_OUTPUT_BUFFER_SIZE	(( PSP_NUM_AUDIO_SAMPLES ) * ( PSP_OUTPUT_CHANNELS ))

static struct
{
	SceUID		threadUID;
	SceUID		semaUID;
	int		channel;
	volatile int	volL;
	volatile int	volR;
	volatile int	running;
} snd_psp = { -1, -1, -1, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, 1 };

static short snd_psp_buff[2][PSP_OUTPUT_BUFFER_SIZE]  __attribute__(( aligned( 64 )));

/*
==================
SNDDMA_MainThread

Copy samples
==================
*/
static int SNDDMA_MainThread( SceSize args, void *argp )
{
	int index = 0;

	while( snd_psp.running )
	{
		sceKernelWaitSema( snd_psp.semaUID, 1, NULL );

		int len     = PSP_OUTPUT_BUFFER_SIZE;
		int size    = dma.samples;
		int pos     = dma.samplepos;
		int wrapped = pos + len - size;

		if( wrapped < 0 )
		{
			sceDmacMemcpy( snd_psp_buff[index], dma.buffer + ( pos * 2 ), len * 2 );
			dma.samplepos += len;
		}
		else
		{
			int remaining = size - pos;
			sceDmacMemcpy( snd_psp_buff[index], dma.buffer + ( pos * 2 ), remaining * 2 );
			if( wrapped > 0 )
				sceDmacMemcpy( snd_psp_buff[index] + ( remaining * 2 ), dma.buffer, wrapped * 2 );
			dma.samplepos = wrapped;
		}

		sceKernelSignalSema( snd_psp.semaUID, 1 );

		sceAudioOutputPannedBlocking( snd_psp.channel, snd_psp.volL, snd_psp.volR, snd_psp_buff[index] );
		index = !index;
	}

	sceKernelExitThread( 0 );
	return 0;
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
	int samplecount;

	dma.format.speed    = SOUND_DMA_SPEED;
	dma.format.channels = PSP_OUTPUT_CHANNELS;
	dma.format.width    = 2;

	// must be multiple of 64
	samplecount = s_samplecount->value;
	if( !samplecount )
		samplecount = 0x4000;

	dma.samples         = samplecount * PSP_OUTPUT_CHANNELS;
	dma.samplepos       = 0;
	dma.buffer          = memalign( 64, dma.samples * 2 ); // 16 bit
	if( !dma.buffer )
		return false;

	// clearing buffers
	memset( dma.buffer, 0, dma.samples * 2 );
	memset( snd_psp_buff, 0, sizeof( snd_psp_buff ));

	// allocate and initialize a hardware output channel
	snd_psp.channel = sceAudioChReserve( PSP_AUDIO_NEXT_CHANNEL, 
		PSP_NUM_AUDIO_SAMPLES, PSP_AUDIO_FORMAT_STEREO );
	if( snd_psp.channel < 0 )
	{
		SNDDMA_Shutdown();
		return false;
	}

	// create semaphore
	snd_psp.semaUID = sceKernelCreateSema( "sound_sema", 0, 1, 255, NULL );
	if( snd_psp.semaUID <= 0 )
	{
		SNDDMA_Shutdown();
		return false;
	}

	// create audio thread
	snd_psp.threadUID = sceKernelCreateThread( "sound_thread", SNDDMA_MainThread, 0x12, 0x8000, 0, 0 );
	if( snd_psp.threadUID < 0 )
	{
		SNDDMA_Shutdown();
		return false;
	}

	// start audio thread
	if( sceKernelStartThread( snd_psp.threadUID, 0, 0 ) < 0 )
	{
		SNDDMA_Shutdown();
		return false;
	}

	Con_Printf( "Using PSP audio driver: %d Hz\n", dma.format.speed );

	dma.initialized = true;

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
	static int	buffers, oldsamplepos;
	int		samplepos, fullsamples;

	fullsamples = dma.samples / 2;

	// it is possible to miscount buffers
	// if it has wrapped twice between
	// calls to S_Update.  Oh well.
	samplepos = SNDDMA_GetDMAPos();

	if( samplepos < oldsamplepos )
	{
		buffers++; // buffer wrapped

		if( paintedtime > 0x40000000 )
		{
			// time to chop things off to avoid 32 bit limits
			buffers = 0;
			paintedtime = fullsamples;
			S_StopAllSounds( true );
		}
	}

	oldsamplepos = samplepos;

	return ( buffers * fullsamples + samplepos / 2 );
}

/*
==============
SNDDMA_BeginPainting

Makes sure dma.buffer is valid
===============
*/
void SNDDMA_BeginPainting( void )
{
	if( snd_psp.semaUID > 0 )
		sceKernelWaitSema( snd_psp.semaUID, 1, NULL );
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
	if( snd_psp.semaUID > 0 )
		sceKernelSignalSema( snd_psp.semaUID, 1 );
}

/*
==============
SNDDMA_Shutdown

Reset the sound device for exiting
===============
*/
void SNDDMA_Shutdown( void )
{
	Con_Printf("Shutting down audio.\n");

	snd_psp.running = 0;

	if( snd_psp.threadUID >= 0 )
	{
		sceKernelWaitThreadEnd( snd_psp.threadUID, NULL );
		sceKernelDeleteThread( snd_psp.threadUID );
		snd_psp.threadUID = -1;
	}

	if( snd_psp.semaUID > 0 )
	{
		sceKernelDeleteSema( snd_psp.semaUID );
		snd_psp.semaUID = -1;
	}

	if( snd_psp.channel >= 0 )
	{
		sceAudioChRelease( snd_psp.channel );
		snd_psp.channel = -1;
	}

	if( dma.buffer )
	{
		free( dma.buffer );
		dma.buffer = NULL;
	}

	dma.initialized = false;
}

#endif // XASH_SOUND == SOUND_PSP
