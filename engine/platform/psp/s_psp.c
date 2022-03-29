/*
s_psp.c - sound hardware output
Copyright (C) 2020 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/


#ifndef XASH_DEDICATED
#include "common.h"
#if XASH_SOUND == SOUND_PSP

#include <pspaudiolib.h>
#include <pspkernel.h>
#include <psputility.h>

#include "sound.h"

#define SAMPLE_16BIT_SHIFT		1
#define SECONDARY_BUFFER_SIZE		0x10000

#define SND_CHANELS  2
#define SND_FREQ     44100
#define SND_BUF_SIZE 16384

typedef struct
{
	short left;
	short right;
}sample_t;

static sample_t 				snd_buffer[SND_BUF_SIZE] __attribute__( ( aligned( 64 ) ) );
static volatile unsigned int	snd_sampleread;

/*
=======================================================================
Global variables. Must be visible to window-procedure function
so it can unlock and free the data block after it has been played.
=======================================================================
*/

dma_t			dma;

void S_Activate( qboolean active )
{
	
}

/* From PSP Quake */
static void PSP_SoundCallback( void* buffer, unsigned int samplesToWrite, void* userData )
{
	// Where are we writing to?
	sample_t* const dest = ( sample_t* )buffer;
	
	// Where are we reading from?
	const sample_t* const src = &snd_buffer[snd_sampleread];
	
	// How many samples to read?
	const unsigned int samplesToRead = samplesToWrite; // 44100
	
	// Going to wrap past the end of the input buffer?
	const unsigned int samplesBeforeEndOfInput = SND_BUF_SIZE - snd_sampleread;
	if ( samplesToRead > samplesBeforeEndOfInput )
	{
		// Yes, so write the first chunk from the end of the input buffer.
		memcpy( dest, src, samplesBeforeEndOfInput * sizeof( sample_t ) );
	
		// Write the second chunk from the start of the input buffer.
		const unsigned int samplesToReadFromBeginning = samplesToRead - samplesBeforeEndOfInput;
				
		memcpy( &dest[samplesBeforeEndOfInput], src, samplesToReadFromBeginning * sizeof( sample_t ) );
	}
	else
	{
		// No wrapping, just copy.
		memcpy( &dest[0], src, samplesToRead * sizeof( sample_t ) );
	}
	
	// Update the read offset.
	snd_sampleread = ( snd_sampleread + samplesToRead ) % SND_BUF_SIZE;
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
	dma.format.speed    = SND_FREQ;
	dma.format.channels = SND_CHANELS;
	dma.format.width    = 2;
	dma.samples         = SND_BUF_SIZE * SND_CHANELS;
	dma.buffer          = ( unsigned char * )snd_buffer;
	dma.samplepos       = 0;

	// Initialise the audio system. This initialises it for the CD audio module
	// too.
	pspAudioInit();

	// Set the channel callback.
	// Sound effects use channel 0, CD audio uses channel 1.
	pspAudioSetChannelCallback( 0, PSP_SoundCallback, 0 );
	
	Con_Printf( "Using PSP audio driver: %d Hz\n", dma.format.speed );

	//SNDDMA_Activate( true );
	
	dma.initialized = true;
#if 0 /* libmp3 */	
	// load modules 
	int status = sceUtilityLoadModule( PSP_MODULE_AV_AVCODEC );
	if ( status < 0 )
	{
		Con_Printf( "Error - PSP_MODULE_AV_AVCODEC returned 0x%08X\n", status );
	}else printf("PSP_MODULE_AV_AVCODEC - OK\n");
	
	status = sceUtilityLoadModule( PSP_MODULE_AV_MP3 );
	if ( status < 0 )
	{
		Con_Printf( "Error - PSP_MODULE_AV_MP3 returned 0x%08X\n", status );
	}else printf("PSP_MODULE_AV_MP3 - OK\n");
	
	// init mp3 resources
	status = sceMp3InitResource();
	if ( status < 0 )
	{
		Con_Printf("Error - sceMp3InitResource returned 0x%08X\n", status);
	}else printf("sceMp3InitResource - OK\n");
#endif
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
	dma.samplepos = snd_sampleread * SND_CHANELS;
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

	return (buffers * fullsamples + samplepos / 2);
}

/*
==============
SNDDMA_BeginPainting

Makes sure dma.buffer is valid
===============
*/
void SNDDMA_BeginPainting( void )
{
	
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
	
	// Clear the mixing buffer so we don't get any noise during cleanup.
	memset(snd_buffer, 0, sizeof(snd_buffer));

	// Clear the channel callback.
	pspAudioSetChannelCallback(0, 0, 0);

	// Stop the audio system?
	pspAudioEndPre();

	// Insert a false delay so the thread can be cleaned up.
	sceKernelDelayThread(50 * 1000);

	// Shut down the audio system.
	pspAudioEnd();
	
	dma.initialized = false;
}

#endif
#endif
