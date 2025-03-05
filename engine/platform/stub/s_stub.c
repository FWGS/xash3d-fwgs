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


#ifndef XASH_DEDICATED
#include "common.h"
#if XASH_SOUND == SOUND_NULL

#include "sound.h"

#define SAMPLE_16BIT_SHIFT		1
#define SECONDARY_BUFFER_SIZE		0x10000

/*
=======================================================================
Global variables. Must be visible to window-procedure function
so it can unlock and free the data block after it has been played.
=======================================================================
*/

//dma_t			dma;

void S_Activate( qboolean active )
{
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
	Msg( "Audio is not enabled\n" );
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
	dma.initialized = false;

	if (dma.buffer) {
		 Z_Free(dma.buffer);
		 dma.buffer = NULL;
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
#endif
