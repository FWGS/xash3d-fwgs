/*
snddma_gamecube.c - GameCube sound DMA backend
Uses libogc ASND streaming at 48 kHz with a silent null fallback.
The engine mixer writes native PowerPC big-endian stereo 16-bit samples.
*/
#include "common.h"
#include "sound.h"
#include "client.h"

#if XASH_GAMECUBE

#include <asndlib.h>
#include <ogc/cache.h>

#define GC_AUDIO_CHUNK_SAMPLES	1024
#define GC_AUDIO_CHUNK_BYTES	(GC_AUDIO_CHUNK_SAMPLES * 2 * sizeof( int16_t ))
#define GC_AUDIO_DEFAULT_SAMPLES	8192
#define GC_AUDIO_VOICE		0

static qboolean gc_audio_real;
static qboolean gc_audio_unpaused;
static qboolean gc_voice_started;
static volatile int gc_audio_painting;
static volatile int gc_audio_play_chunk;
static volatile unsigned int gc_audio_chunks_submitted;
static volatile unsigned int gc_audio_nonzero_chunks;
static volatile int gc_audio_last_peak;
static volatile int gc_audio_max_peak;
static qboolean gc_audio_reported_nonzero;
static unsigned int gc_audio_submit_polls;
static int16_t gc_audio_chunk[2][GC_AUDIO_CHUNK_SAMPLES * 2] __attribute__((aligned( 32 )));

static qboolean GCube_NullAudioInit( void );

static int GC_AudioPeak( const int16_t *samples, int sample_count )
{
	int peak = 0;

	for( int i = 0; i < sample_count; i++ )
	{
		int value = samples[i];
		if( value < 0 )
			value = -value;
		if( value > peak )
			peak = value;
	}

	return peak;
}

static void GC_AudioCopyChunk( int16_t *dest, int bytes )
{
	byte *out = (byte *)dest;
	byte *ring;
	int size;
	int pos;
	int wrapped;

	if( !dest || bytes <= 0 )
		return;

	if( !snd.buffer || gc_audio_painting )
	{
		memset( dest, 0, bytes );
		gc_audio_last_peak = 0;
		gc_audio_chunks_submitted++;
		return;
	}

	ring = (byte *)snd.buffer;
	size = snd.samples << 1;
	pos = snd.samplepos << 1;
	wrapped = pos + bytes - size;

	if( wrapped < 0 )
	{
		memcpy( out, ring + pos, bytes );
		snd.samplepos += bytes >> 1;
	}
	else
	{
		int remaining = size - pos;

		memcpy( out, ring + pos, remaining );
		memcpy( out + remaining, ring, wrapped );
		snd.samplepos = wrapped >> 1;
	}

	if( snd.samplepos >= snd.samples )
		snd.samplepos = 0;

	gc_audio_last_peak = GC_AudioPeak( dest, bytes / (int)sizeof( int16_t ));
	if( gc_audio_last_peak > gc_audio_max_peak )
		gc_audio_max_peak = gc_audio_last_peak;
	gc_audio_chunks_submitted++;
	if( gc_audio_last_peak > 0 )
		gc_audio_nonzero_chunks++;
}

static void GC_AudioVoiceCallback( s32 voice )
{
	int chunk;

	(void)voice;

	if( !gc_audio_real || !snd.initialized )
		return;

	chunk = gc_audio_play_chunk ^ 1;
	GC_AudioCopyChunk( gc_audio_chunk[chunk], GC_AUDIO_CHUNK_BYTES );
	DCFlushRange( gc_audio_chunk[chunk], GC_AUDIO_CHUNK_BYTES );
	ASND_AddVoice( GC_AUDIO_VOICE, gc_audio_chunk[chunk], GC_AUDIO_CHUNK_BYTES );
	gc_audio_play_chunk = chunk;
}

static qboolean GCube_StartVoice( void )
{
	s32 status;

	if( gc_voice_started )
		return true;

	GC_AudioCopyChunk( gc_audio_chunk[0], GC_AUDIO_CHUNK_BYTES );
	DCFlushRange( gc_audio_chunk[0], GC_AUDIO_CHUNK_BYTES );

	status = ASND_SetVoice( GC_AUDIO_VOICE, VOICE_STEREO_16BIT_BE, SOUND_DMA_SPEED, 0,
		gc_audio_chunk[0], GC_AUDIO_CHUNK_BYTES, 255, 255, GC_AudioVoiceCallback );
	if( status == SND_INVALID )
		return false;

	gc_voice_started = true;
	gc_audio_play_chunk = 0;
	return true;
}

static qboolean GCube_RealAudioInit( void )
{
	int samplecount;

	ASND_Init();
	/* ASND_Init leaves playback paused; unpause on first mix submit. */

	samplecount = (int)s_samplecount.value;
	if( samplecount <= 0 )
		samplecount = GC_AUDIO_DEFAULT_SAMPLES;

	snd.format.speed = SOUND_DMA_SPEED;
	snd.format.width = 2;
	snd.format.channels = 2;
	snd.samples = samplecount * snd.format.channels;
	snd.buffer = Mem_Calloc( sndpool, snd.samples * snd.format.width );
	if( !snd.buffer )
	{
		ASND_End();
		Con_Reportf( S_ERROR "GameCube audio: ring buffer allocation failed\n" );
		return false;
	}

	snd.samplepos = 0;
	gc_voice_started = false;
	gc_audio_real = true;
	gc_audio_unpaused = false;
	gc_audio_chunks_submitted = 0;
	gc_audio_nonzero_chunks = 0;
	gc_audio_last_peak = 0;
	gc_audio_max_peak = 0;
	gc_audio_reported_nonzero = false;
	gc_audio_submit_polls = 0;
	snd.initialized = true;
	snd.backend_name = "GameCube (ASND 48kHz)";
	Con_Reportf( "Xash3D GameCube: audio backend ready (%d samples, %d Hz, voice deferred)\n",
		samplecount, SOUND_DMA_SPEED );
	return true;
}

static qboolean GCube_NullAudioInit( void )
{
	snd.buffer = NULL;
	snd.samples = 0;
	snd.samplepos = 0;
	snd.initialized = true;
	snd.backend_name = "GameCube (null)";
	gc_audio_real = false;
	gc_audio_unpaused = false;
	gc_voice_started = false;
	gc_audio_chunks_submitted = 0;
	gc_audio_nonzero_chunks = 0;
	gc_audio_last_peak = 0;
	gc_audio_max_peak = 0;
	gc_audio_reported_nonzero = false;
	gc_audio_submit_polls = 0;

	snd.format.speed = SOUND_DMA_SPEED;
	snd.format.width = 2;
	snd.format.channels = 2;

	Con_Reportf( "Xash3D GameCube: null audio backend active (silent fallback)\n" );
	return true;
}

qboolean SNDDMA_Init( void )
{
	if( Sys_CheckParm( "-gcnullaudio" ))
		return GCube_NullAudioInit();

	if( GCube_RealAudioInit( ))
		return true;

	Con_Reportf( S_WARN "GameCube audio: init failed, using null fallback\n" );
	return GCube_NullAudioInit();
}

void SNDDMA_Shutdown( void )
{
	if( gc_audio_real )
	{
		if( gc_voice_started )
			ASND_StopVoice( GC_AUDIO_VOICE );
		Con_Reportf(
			"Xash3D GameCube: audio shutdown chunks=%u nonzero=%u last_peak=%d max_peak=%d\n",
			gc_audio_chunks_submitted, gc_audio_nonzero_chunks, gc_audio_last_peak, gc_audio_max_peak );
		ASND_End();
	}

	snd.initialized = false;
	gc_audio_real = false;
	gc_voice_started = false;

	if( snd.buffer )
	{
		Mem_Free( snd.buffer );
		snd.buffer = NULL;
	}

	snd.samples = 0;
	snd.samplepos = 0;
}

void SNDDMA_BeginPainting( void )
{
	gc_audio_painting = 1;
}

void SNDDMA_Submit( void )
{
	gc_audio_painting = 0;

	if( gc_audio_real && !gc_audio_unpaused &&
		( cls.state == ca_active || cls.state == ca_cinematic ))
	{
		if( !gc_voice_started && !GCube_StartVoice( ))
		{
			Con_Reportf( S_WARN "GameCube audio: voice start failed, falling back to silent mix\n" );
			gc_audio_real = false;
			return;
		}

		ASND_Pause( 0 );
		gc_audio_unpaused = true;
		Con_Reportf( "Xash3D GameCube: audio voice started state=%d\n", cls.state );
	}

	if( gc_audio_real && gc_voice_started )
	{
		gc_audio_submit_polls++;
		if( !gc_audio_reported_nonzero && gc_audio_nonzero_chunks > 0 )
		{
			Con_Reportf(
				"Xash3D GameCube: audio submitted nonzero PCM chunks=%u peak=%d\n",
				gc_audio_nonzero_chunks, gc_audio_max_peak );
			gc_audio_reported_nonzero = true;
		}
		else if( !gc_audio_reported_nonzero && gc_audio_submit_polls == 300 )
		{
			Con_Reportf(
				"Xash3D GameCube: audio still silent chunks=%u peak=%d\n",
				gc_audio_chunks_submitted, gc_audio_last_peak );
		}
	}
}

void SNDDMA_Activate( qboolean active )
{
	if( !gc_audio_real )
		return;

	ASND_Pause( active ? 0 : 1 );
	gc_audio_unpaused = active ? true : false;
}

#endif /* XASH_GAMECUBE */
