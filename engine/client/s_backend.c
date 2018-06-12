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
#include "sound.h"
#include <dsound.h>

#define iDirectSoundCreate( a, b, c )	pDirectSoundCreate( a, b, c )

static HRESULT ( _stdcall *pDirectSoundCreate)(GUID* lpGUID, LPDIRECTSOUND* lpDS, IUnknown* pUnkOuter );

static dllfunc_t dsound_funcs[] =
{
{ "DirectSoundCreate", (void **) &pDirectSoundCreate },
{ NULL, NULL }
};

dll_info_t dsound_dll = { "dsound.dll", dsound_funcs, false };

#define SAMPLE_16BIT_SHIFT		1
#define SECONDARY_BUFFER_SIZE		0x10000

typedef enum
{
	SIS_SUCCESS,
	SIS_FAILURE,
	SIS_NOTAVAIL
} si_state_t;

static qboolean		snd_firsttime = true;
static qboolean		primary_format_set;
static HWND		snd_hwnd;

/* 
=======================================================================
Global variables. Must be visible to window-procedure function 
so it can unlock and free the data block after it has been played.
=======================================================================
*/ 
static DWORD		locksize;
static HPSTR		lpData, lpData2;
static DWORD		gSndBufSize;
static MMTIME		mmstarttime;
static LPDIRECTSOUNDBUFFER	pDSBuf, pDSPBuf;
static LPDIRECTSOUND	pDS;

qboolean SNDDMA_InitDirect( void *hInst );
void SNDDMA_FreeSound( void );

static const char *DSoundError( int error )
{
	switch( error )
	{
	case DSERR_BUFFERLOST:
		return "buffer is lost";
	case DSERR_INVALIDCALL:
		return "invalid call";
	case DSERR_INVALIDPARAM:
		return "invalid param";
	case DSERR_PRIOLEVELNEEDED:
		return "invalid priority level";
	}

	return "unknown error";
}

/*
==================
DS_CreateBuffers
==================
*/
static qboolean DS_CreateBuffers( void *hInst )
{
	WAVEFORMATEX	pformat, format;
	DSBCAPS		dsbcaps;
	DSBUFFERDESC	dsbuf;

	memset( &format, 0, sizeof( format ));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.wBitsPerSample = 16;
	format.nSamplesPerSec = SOUND_DMA_SPEED;
	format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign; 
	format.cbSize = 0;

	if( pDS->lpVtbl->SetCooperativeLevel( pDS, hInst, DSSCL_EXCLUSIVE ) != DS_OK )
	{
		Con_DPrintf( S_ERROR "DirectSound: failed to set EXCLUSIVE coop level\n" );
		SNDDMA_FreeSound();
		return false;
	}

	// get access to the primary buffer, if possible, so we can set the sound hardware format
	memset( &dsbuf, 0, sizeof( dsbuf ));
	dsbuf.dwSize = sizeof( DSBUFFERDESC );
	dsbuf.dwFlags = DSBCAPS_PRIMARYBUFFER;
	dsbuf.dwBufferBytes = 0;
	dsbuf.lpwfxFormat = NULL;

	memset( &dsbcaps, 0, sizeof( dsbcaps ));
	dsbcaps.dwSize = sizeof( dsbcaps );
	primary_format_set = false;

	if( pDS->lpVtbl->CreateSoundBuffer( pDS, &dsbuf, &pDSPBuf, NULL ) == DS_OK )
	{
		pformat = format;

		if( pDSPBuf->lpVtbl->SetFormat( pDSPBuf, &pformat ) != DS_OK )
		{
			if( snd_firsttime )
				Con_DPrintf( S_ERROR "DirectSound: failed to set primary sound format\n" );
		}
		else
		{
			primary_format_set = true;
		}
	}

	// create the secondary buffer we'll actually work with
	memset( &dsbuf, 0, sizeof( dsbuf ));
	dsbuf.dwSize = sizeof( DSBUFFERDESC );
	dsbuf.dwFlags = (DSBCAPS_CTRLFREQUENCY|DSBCAPS_LOCSOFTWARE);
	dsbuf.dwBufferBytes = SECONDARY_BUFFER_SIZE;
	dsbuf.lpwfxFormat = &format;

	memset( &dsbcaps, 0, sizeof( dsbcaps ));
	dsbcaps.dwSize = sizeof( dsbcaps );

	if( pDS->lpVtbl->CreateSoundBuffer( pDS, &dsbuf, &pDSBuf, NULL ) != DS_OK )
	{
		// couldn't get hardware, fallback to software.
		dsbuf.dwFlags = (DSBCAPS_LOCSOFTWARE|DSBCAPS_GETCURRENTPOSITION2);

		if( pDS->lpVtbl->CreateSoundBuffer( pDS, &dsbuf, &pDSBuf, NULL ) != DS_OK )
		{
			Con_DPrintf( S_ERROR "DirectSound: failed to create secondary buffer\n" );
			SNDDMA_FreeSound ();
			return false;
		}
	}

	if( pDSBuf->lpVtbl->GetCaps( pDSBuf, &dsbcaps ) != DS_OK )
	{
		Con_DPrintf( S_ERROR "DirectSound: failed to get capabilities\n" );
		SNDDMA_FreeSound ();
		return false;
	}

	// make sure mixer is active
	if( pDSBuf->lpVtbl->Play( pDSBuf, 0, 0, DSBPLAY_LOOPING ) != DS_OK )
	{
		Con_DPrintf( S_ERROR "DirectSound: failed to create circular buffer\n" );
		SNDDMA_FreeSound ();
		return false;
	}

	// we don't want anyone to access the buffer directly w/o locking it first
	lpData = NULL;
	dma.samplepos = 0;
	snd_hwnd = (HWND)hInst;
	gSndBufSize = dsbcaps.dwBufferBytes;
	dma.samples = gSndBufSize / 2;
	dma.buffer = (byte *)lpData;

	SNDDMA_BeginPainting();
	if( dma.buffer ) memset( dma.buffer, 0, dma.samples * 2 );
	SNDDMA_Submit();

	return true;
}

/*
==================
DS_DestroyBuffers
==================
*/
static void DS_DestroyBuffers( void )
{
	if( pDS ) pDS->lpVtbl->SetCooperativeLevel( pDS, snd_hwnd, DSSCL_NORMAL );

	if( pDSBuf )
	{
		pDSBuf->lpVtbl->Stop( pDSBuf );
		pDSBuf->lpVtbl->Release( pDSBuf );
	}

	// only release primary buffer if it's not also the mixing buffer we just released
	if( pDSPBuf && ( pDSBuf != pDSPBuf ))
		pDSPBuf->lpVtbl->Release( pDSPBuf );

	dma.buffer = NULL;
	pDSPBuf = NULL;
	pDSBuf = NULL;
}

/*
==================
SNDDMA_LockSound
==================
*/
void SNDDMA_LockSound( void )
{
	if( pDSBuf != NULL )
		pDSBuf->lpVtbl->Stop( pDSBuf );
}

/*
==================
SNDDMA_LockSound
==================
*/
void SNDDMA_UnlockSound( void )
{
	if( pDSBuf != NULL )
		pDSBuf->lpVtbl->Play( pDSBuf, 0, 0, DSBPLAY_LOOPING );
}

/*
==================
SNDDMA_FreeSound
==================
*/
void SNDDMA_FreeSound( void )
{
	if( pDS )
	{
		DS_DestroyBuffers();
		pDS->lpVtbl->Release( pDS );
		Sys_FreeLibrary( &dsound_dll );
	}

	lpData = NULL;
	pDSPBuf = NULL;
	pDSBuf = NULL;
	pDS = NULL;
}

/*
==================
SNDDMA_InitDirect

Direct-Sound support
==================
*/
si_state_t SNDDMA_InitDirect( void *hInst )
{
	DSCAPS	dscaps;
	HRESULT	hresult;

	if( !dsound_dll.link )
	{
		if( !Sys_LoadLibrary( &dsound_dll ))
			return SIS_FAILURE;
	}

	if(( hresult = iDirectSoundCreate( NULL, &pDS, NULL )) != DS_OK )
	{
		if( hresult != DSERR_ALLOCATED )
			return SIS_FAILURE;

		Con_DPrintf( S_ERROR "DirectSound: hardware already in use\n" );
		return SIS_NOTAVAIL;
	}

	dscaps.dwSize = sizeof( dscaps );

	if( pDS->lpVtbl->GetCaps( pDS, &dscaps ) != DS_OK )
		Con_DPrintf( S_ERROR "DirectSound: failed to get capabilities\n" );

	if( FBitSet( dscaps.dwFlags, DSCAPS_EMULDRIVER ))
	{
		Con_DPrintf( S_ERROR "DirectSound: driver not installed\n" );
		SNDDMA_FreeSound();
		return SIS_FAILURE;
	}

	if( !DS_CreateBuffers( hInst ))
		return SIS_FAILURE;

	return SIS_SUCCESS;
}

/*
==================
SNDDMA_Init

Try to find a sound device to mix for.
Returns false if nothing is found.
==================
*/
int SNDDMA_Init( void *hInst )
{
	// already initialized
	if( dma.initialized ) return true;

	memset( &dma, 0, sizeof( dma ));

	// init DirectSound
	if( SNDDMA_InitDirect( hInst ) != SIS_SUCCESS )
		return false;
	dma.initialized = true;
	snd_firsttime = false;

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
	int	s;
	MMTIME	mmtime;
	DWORD	dwWrite;

	if( !dma.initialized )
		return 0;
	
	mmtime.wType = TIME_SAMPLES;
	pDSBuf->lpVtbl->GetCurrentPosition( pDSBuf, &mmtime.u.sample, &dwWrite );
	s = mmtime.u.sample - mmstarttime.u.sample;

	s >>= SAMPLE_16BIT_SHIFT;
	s &= (dma.samples - 1);

	return s;
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
	int	reps;
	DWORD	dwSize2;
	DWORD	*pbuf, *pbuf2;
	HRESULT	hr;
	DWORD	dwStatus;

	if( !pDSBuf ) return;

	// if the buffer was lost or stopped, restore it and/or restart it
	if( pDSBuf->lpVtbl->GetStatus( pDSBuf, &dwStatus ) != DS_OK )
		Con_DPrintf( S_ERROR "BeginPainting: couldn't get sound buffer status\n" );
	
	if( dwStatus & DSBSTATUS_BUFFERLOST )
		pDSBuf->lpVtbl->Restore( pDSBuf );
	
	if( !FBitSet( dwStatus, DSBSTATUS_PLAYING ))
		pDSBuf->lpVtbl->Play( pDSBuf, 0, 0, DSBPLAY_LOOPING );

	// lock the dsound buffer
	dma.buffer = NULL;
	reps = 0;

	while(( hr = pDSBuf->lpVtbl->Lock( pDSBuf, 0, gSndBufSize, &pbuf, &locksize, &pbuf2, &dwSize2, 0 )) != DS_OK )
	{
		if( hr != DSERR_BUFFERLOST )
		{
			Con_DPrintf( S_ERROR "BeginPainting: %s\n", DSoundError( hr ));
			S_Shutdown ();
			return;
		}
		else pDSBuf->lpVtbl->Restore( pDSBuf );
		if( ++reps > 2 ) return;
	}

	dma.buffer = (byte *)pbuf;
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
	if( !dma.buffer ) return;
	// unlock the dsound buffer
	if( pDSBuf ) pDSBuf->lpVtbl->Unlock( pDSBuf, dma.buffer, locksize, NULL, 0 );
}

/*
==============
SNDDMA_Shutdown

Reset the sound device for exiting
===============
*/
void SNDDMA_Shutdown( void )
{
	if( !dma.initialized ) return;
	dma.initialized = false;
	SNDDMA_FreeSound();
}

/*
===========
S_Activate

Called when the main window gains or loses focus.
The window have been destroyed and recreated
between a deactivate and an activate.
===========
*/
void S_Activate( qboolean active, void *hInst )
{
	if( !dma.initialized ) return;
	snd_hwnd = (HWND)hInst;

	if( !pDS || !snd_hwnd )
		return;

	if( active )
		DS_CreateBuffers( snd_hwnd );
	else DS_DestroyBuffers();
}