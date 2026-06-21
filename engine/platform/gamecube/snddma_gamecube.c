/*
snddma_gamecube.c - GameCube sound DMA backend (null/silent fallback)
Provides a safe audio initialization path for GameCube without requiring
full DSP/ARAM setup, preventing engine startup failure due to missing audio.
*/
#include "common.h"
#include "sound.h"
#include "client.h"

#if XASH_GAMECUBE

/*
GameCube audio backend (Phase 1: Silent Fallback)
libogc provides DSP and AI APIs, but full initialization and ARAM management
require careful alignment and memory budgeting. For G05, we provide a null
backend that satisfies the engine's sound subsystem contract without
allocating large DMA buffers or touching the DSP, keeping the 24 MiB budget
safe. Audio output will be silent but stable.

S_UpdateChannels in s_main.c already guards with: if( !snd.buffer ) return;
so setting snd.buffer = NULL and snd.samples = 0 is safe.
*/

qboolean SNDDMA_Init( void )
{
	snd.buffer = NULL;
	snd.samples = 0;
	snd.samplepos = 0;
	snd.initialized = true;
	snd.backend_name = "GameCube (null)";

	snd.format.speed = SOUND_DMA_SPEED;
	snd.format.width = 2;
	snd.format.channels = 2;

	Con_Reportf( "S_Init: GameCube null audio backend initialized (silent fallback)\n" );
	return true;
}

void SNDDMA_Shutdown( void )
{
	snd.initialized = false;
	snd.buffer = NULL;
	snd.samples = 0;
}

void SNDDMA_BeginPainting( void )
{
	// Nothing to lock in null backend
}

void SNDDMA_Submit( void )
{
	// Nothing to submit in null backend
}

#endif /* XASH_GAMECUBE */
