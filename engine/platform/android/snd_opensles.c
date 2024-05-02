/*
Copyright (C) 2015 SiPlus, Chasseur de bots

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "common.h"
#include "platform/platform.h"
#if XASH_SOUND == SOUND_OPENSLES
#include <SLES/OpenSLES.h>
#include "pthread.h"
#include "sound.h"

extern dma_t			dma;

static SLObjectItf snddma_android_engine = NULL;
static SLObjectItf snddma_android_outputMix = NULL;
static SLObjectItf snddma_android_player = NULL;
static SLBufferQueueItf snddma_android_bufferQueue;
static SLPlayItf snddma_android_play;

static pthread_mutex_t snddma_android_mutex = PTHREAD_MUTEX_INITIALIZER;

static int snddma_android_size;

static const SLInterfaceID *pSL_IID_ENGINE;
static const SLInterfaceID *pSL_IID_BUFFERQUEUE;
static const SLInterfaceID *pSL_IID_PLAY;
static SLresult SLAPIENTRY (*pslCreateEngine)(
		SLObjectItf             *pEngine,
		SLuint32                numOptions,
		const SLEngineOption    *pEngineOptions,
		SLuint32                numInterfaces,
		const SLInterfaceID     *pInterfaceIds,
		const SLboolean         * pInterfaceRequired
);

void SNDDMA_Activate( qboolean active )
{
	if( !dma.initialized )
		return;

	if( active )
	{
		memset( dma.buffer, 0, snddma_android_size * 2 );
		(*snddma_android_bufferQueue)->Enqueue( snddma_android_bufferQueue, dma.buffer, snddma_android_size );
		(*snddma_android_play)->SetPlayState( snddma_android_play, SL_PLAYSTATE_PLAYING );
	}
	else
	{
		(*snddma_android_play)->SetPlayState( snddma_android_play, SL_PLAYSTATE_STOPPED );
		(*snddma_android_bufferQueue)->Clear( snddma_android_bufferQueue );
	}
}

static void SNDDMA_Android_Callback( SLBufferQueueItf bq, void *context )
{
	uint8_t *buffer2;

	pthread_mutex_lock( &snddma_android_mutex );

	buffer2 = ( uint8_t * )dma.buffer + snddma_android_size;
	(*bq)->Enqueue( bq, buffer2, snddma_android_size );
	memcpy( buffer2, dma.buffer, snddma_android_size );
	memset( dma.buffer, 0, snddma_android_size );
	dma.samplepos += dma.samples;

	pthread_mutex_unlock( &snddma_android_mutex );
}

static const char *SNDDMA_Android_Init( void )
{
	SLresult result;

	SLEngineItf engine;

	int freq;

	SLDataLocator_BufferQueue sourceLocator;
	SLDataFormat_PCM sourceFormat;
	SLDataSource source;

	SLDataLocator_OutputMix sinkLocator;
	SLDataSink sink;

	SLInterfaceID interfaceID;
	SLboolean interfaceRequired;

	int samples;
	void *handle = dlopen( "libOpenSLES.so", RTLD_LAZY );

	if( !handle )
		return "dlopen for libOpenSLES.so";

	pslCreateEngine = dlsym( handle, "slCreateEngine" );

	if( !pslCreateEngine )
		return "resolve slCreateEngine";

	pSL_IID_ENGINE = dlsym( handle, "SL_IID_ENGINE" );

	if( !pSL_IID_ENGINE )
		return "resolve SL_IID_ENGINE";

	pSL_IID_PLAY = dlsym( handle, "SL_IID_PLAY" );

	if( !pSL_IID_PLAY )
		return "resolve SL_IID_PLAY";

	pSL_IID_BUFFERQUEUE = dlsym( handle, "SL_IID_BUFFERQUEUE" );

	if( !pSL_IID_BUFFERQUEUE )
		return "resolve SL_IID_BUFFERQUEUE";


	result = pslCreateEngine( &snddma_android_engine, 0, NULL, 0, NULL, NULL );
	if( result != SL_RESULT_SUCCESS ) return "slCreateEngine";
	result = (*snddma_android_engine)->Realize( snddma_android_engine, SL_BOOLEAN_FALSE );
	if( result != SL_RESULT_SUCCESS ) return "engine->Realize";
	result = (*snddma_android_engine)->GetInterface( snddma_android_engine, *pSL_IID_ENGINE, &engine );
	if( result != SL_RESULT_SUCCESS ) return "engine->GetInterface(ENGINE)";

	result = (*engine)->CreateOutputMix( engine, &snddma_android_outputMix, 0, NULL, NULL );
	if( result != SL_RESULT_SUCCESS ) return "engine->CreateOutputMix";
	result = (*snddma_android_outputMix)->Realize( snddma_android_outputMix, SL_BOOLEAN_FALSE );
	if( result != SL_RESULT_SUCCESS ) return "outputMix->Realize";

	freq = SOUND_DMA_SPEED;
	sourceLocator.locatorType = SL_DATALOCATOR_BUFFERQUEUE;
	sourceLocator.numBuffers = 2;
	sourceFormat.formatType = SL_DATAFORMAT_PCM;
	sourceFormat.numChannels = 2; // always stereo, because engine supports only stereo
	sourceFormat.samplesPerSec = freq * 1000;
	sourceFormat.bitsPerSample = 16; // always 16 bit audio
	sourceFormat.containerSize = sourceFormat.bitsPerSample;
	sourceFormat.channelMask = SL_SPEAKER_FRONT_LEFT|SL_SPEAKER_FRONT_RIGHT;
	sourceFormat.endianness = SL_BYTEORDER_LITTLEENDIAN;
	source.pLocator = &sourceLocator;
	source.pFormat = &sourceFormat;

	sinkLocator.locatorType = SL_DATALOCATOR_OUTPUTMIX;
	sinkLocator.outputMix = snddma_android_outputMix;
	sink.pLocator = &sinkLocator;
	sink.pFormat = NULL;

	interfaceID = *pSL_IID_BUFFERQUEUE;
	interfaceRequired = SL_BOOLEAN_TRUE;

	result = (*engine)->CreateAudioPlayer( engine, &snddma_android_player, &source, &sink, 1, &interfaceID, &interfaceRequired );
	if( result != SL_RESULT_SUCCESS ) return "engine->CreateAudioPlayer";
	result = (*snddma_android_player)->Realize( snddma_android_player, SL_BOOLEAN_FALSE );
	if( result != SL_RESULT_SUCCESS ) return "player->Realize";
	result = (*snddma_android_player)->GetInterface( snddma_android_player, *pSL_IID_BUFFERQUEUE, &snddma_android_bufferQueue );
	if( result != SL_RESULT_SUCCESS ) return "player->GetInterface(BUFFERQUEUE)";
	result = (*snddma_android_player)->GetInterface( snddma_android_player, *pSL_IID_PLAY, &snddma_android_play );
	if( result != SL_RESULT_SUCCESS ) return "player->GetInterface(PLAY)";
	result = (*snddma_android_bufferQueue)->RegisterCallback( snddma_android_bufferQueue, SNDDMA_Android_Callback, NULL );
	if( result != SL_RESULT_SUCCESS ) return "bufferQueue->RegisterCallback";

	samples = s_samplecount.value;
	if( !samples )
		samples = 4096;

	dma.format.channels = sourceFormat.numChannels;
	dma.samples = samples * sourceFormat.numChannels;
	dma.format.speed = freq;
	snddma_android_size = dma.samples * ( sourceFormat.bitsPerSample >> 3 );
	dma.buffer = Z_Malloc( snddma_android_size * 2 );
	dma.samplepos = 0;
	// dma.sampleframes = dma.samples / dma.format.channels;
	dma.format.width = 2;
	if( !dma.buffer ) return "malloc";

	//snddma_android_mutex = trap_Mutex_Create();

	dma.initialized = true;

	SNDDMA_Activate( true );

	return NULL;
}

qboolean SNDDMA_Init( void )
{
	const char *initError;

	Msg( "OpenSL ES audio device initializing...\n" );

	initError = SNDDMA_Android_Init();
	if( initError )
	{
		Msg( S_ERROR "SNDDMA_Init: %s failed.\n", initError );
		SNDDMA_Shutdown();
		return false;
	}

	Msg( "OpenSL ES audio initialized.\n" );
	dma.backendName = "OpenSL ES";
	return true;
}

void SNDDMA_Shutdown( void )
{
	Msg( "Closing OpenSL ES audio device...\n" );

	if( snddma_android_player )
	{
		(*snddma_android_player)->Destroy( snddma_android_player );
		snddma_android_player = NULL;
	}
	if( snddma_android_outputMix )
	{
		(*snddma_android_outputMix)->Destroy( snddma_android_outputMix );
		snddma_android_outputMix = NULL;
	}
	if( snddma_android_engine )
	{
		(*snddma_android_engine)->Destroy( snddma_android_engine );
		snddma_android_engine = NULL;
	}

	if( dma.buffer )
	{
		Z_Free( dma.buffer );
		dma.buffer = NULL;
	}

	//if( snddma_android_mutex )
		//trap_Mutex_Destroy( &snddma_android_mutex );

	Msg( "OpenSL ES audio device shut down.\n" );
}

void SNDDMA_Submit( void )
{
	pthread_mutex_unlock( &snddma_android_mutex );
}

void SNDDMA_BeginPainting( void )
{
	pthread_mutex_lock( &snddma_android_mutex );
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
