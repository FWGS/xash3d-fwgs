/*
sound.h - sndlib main header
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

#ifndef SOUND_H
#define SOUND_H

extern poolhandle_t sndpool;

#include "xash3d_mathlib.h"

#define XASH_AUDIO_CD_QUALITY 1 // some platforms might need this

// sound engine rate defines
#if XASH_AUDIO_CD_QUALITY
#define SOUND_11k       11025 // 11khz sample rate
#define SOUND_22k       22050 // 22khz sample rate
#define SOUND_44k       44100 // 44khz sample rate
#else // XASH_AUDIO_CD_QUALITY
#define SOUND_11k       12000 // 11khz sample rate
#define SOUND_22k       24000 // 22khz sample rate
#define SOUND_44k       48000 // 44khz sample rate
#endif // XASH_AUDIO_CD_QUALITY

#define SOUND_DMA_SPEED SOUND_44k // hardware playback rate

// NOTE: clipped sound at 32760 to avoid overload
#define CLIP( x ) (( x ) > 32760 ? 32760 : (( x ) < -32760 ? -32760 : ( x )))

#define PAINTBUFFER_SIZE 1024	// 44k: was 512

#define S_RAW_SOUND_IDLE_SEC         10 // time interval for idling raw sound before it's freed
#define S_RAW_SOUND_BACKGROUNDTRACK  -2
#define S_RAW_SOUND_SOUNDTRACK       -1
#define S_RAW_SAMPLES_PRECISION_BITS 14

typedef struct
{
	int left;
	int right;
} portable_samplepair_t;

typedef struct sfx_s
{
	char          name[MAX_QPATH];
	wavdata_t    *cache;

	int           servercount;
	uint          hashValue;
	struct sfx_s *hashNext;
} sfx_t;

// structure used for fading in and out client sound volume.
typedef struct
{
	float initial_percent;
	float percent;     // how far to adjust client's volume down by.
	float starttime;   // GetHostTime() when we started adjusting volume
	float fadeouttime; // # of seconds to get to faded out state
	float holdtime;    // # of seconds to hold
	float fadeintime;  // # of seconds to restore
} soundfade_t;

typedef struct
{
	float percent;
} musicfade_t;

typedef struct snd_format_s
{
	uint speed;
	byte width;
	byte channels;
} snd_format_t;

typedef struct
{
	snd_format_t format;
	int          samples;     // mono samples in buffer
	int          samplepos;   // in mono samples
	qboolean     initialized; // sound engine is active
	byte        *buffer;
	const char  *backendName;
} dma_t;

#include "vox.h"

typedef struct
{
	double     sample;
	wavdata_t *pData;
	double     forcedEndSample;
	qboolean   finished;
} mixer_t;

typedef struct rawchan_s
{
	int                   entnum;
	int                   master_vol;
	int                   leftvol;       // 0-255 left volume
	int                   rightvol;      // 0-255 right volume
	float                 dist_mult;     // distance multiplier (attenuation/clipK)
	vec3_t                origin;        // only use if fixed_origin is set
	volatile uint         s_rawend;
	float                 oldtime;       // catch time jumps
	size_t                max_samples;   // buffer length
	portable_samplepair_t rawsamples[]; // variable sized
} rawchan_t;

typedef struct channel_s
{
	char     name[16];    // keep sentence name
	sfx_t   *sfx;         // sfx number

	int      leftvol;     // 0-255 left volume
	int      rightvol;    // 0-255 right volume

	int      entnum;      // entity soundsource
	int      entchannel;  // sound channel (CHAN_STREAM, CHAN_VOICE, etc.)
	vec3_t   origin;      // only use if fixed_origin is set
	float    dist_mult;   // distance multiplier (attenuation/clipK)
	int      master_vol;  // 0-255 master volume
	int      basePitch;   // base pitch percent (100% is normal pitch playback)
	float    pitch;       // real-time pitch after any modulation or shift by dynamic data
	qboolean use_loop;    // don't loop default and local sounds
	qboolean staticsound; // use origin instead of fetching entnum's origin
	qboolean localsound;  // it's a local menu sound (not looped, not paused)
	mixer_t  pMixer;

	// sentence mixer
	qboolean  isSentence;  // bit indicating sentence
	int       wordIndex;
	mixer_t  *currentWord; // NULL if sentence is finished
	voxword_t words[CVOXWORDMAX];
} channel_t;

typedef struct
{
	vec3_t   origin;   // simorg + view_ofs
	vec3_t   forward;
	vec3_t   right;
	vec3_t   up;

	int      entnum;
	int      waterlevel;
	float    frametime;     // used for sound fade
	qboolean active;
	qboolean inmenu;        // listener in-menu ?
	qboolean paused;
	qboolean streaming;     // playing AVI-file
	qboolean stream_paused; // pause only background track
} listener_t;

typedef struct
{
	string    current;  // a currently playing track
	string    loopName; // may be empty
	stream_t *stream;
	int       source;   // may be game, menu, etc
} bg_track_t;

typedef int sound_t;

//====================================================================

#define MAX_DYNAMIC_CHANNELS (60 + NUM_AMBIENTS)
#define MAX_CHANNELS         (256 + MAX_DYNAMIC_CHANNELS) // Scourge Of Armagon has too many static sounds on hip2m4.bsp
#define MAX_RAW_CHANNELS     48
#define MAX_RAW_SAMPLES      16384
#define SND_CLIP_DISTANCE    1000.0f

extern sound_t    ambient_sfx[NUM_AMBIENTS];
extern qboolean   snd_ambient;
extern channel_t  channels[MAX_CHANNELS];
extern rawchan_t *raw_channels[MAX_RAW_CHANNELS];
extern int        total_channels;
extern int        paintedtime;
extern int        soundtime;
extern listener_t s_listener;
extern int        idsp_room;
extern dma_t      dma;

extern convar_t s_musicvolume;
extern convar_t s_lerping;
extern convar_t s_test;  // cvar to test new effects
extern convar_t s_samplecount;
extern convar_t s_warn_late_precache;
extern convar_t snd_mute_losefocus;

void S_InitScaletable( void );
wavdata_t *S_LoadSound( sfx_t *sfx );
float S_GetMasterVolume( void );
float S_GetMusicVolume( void );

//
// s_main.c
//
void S_FreeChannel( channel_t *ch );

//
// s_mix.c
//
int S_MixDataToDevice( channel_t *pChannel, int sampleCount, int outputRate, int outputOffset, int timeCompress );
void MIX_ClearAllPaintBuffers( int SampleCount, qboolean clearFilters );
void MIX_InitAllPaintbuffers( void );
void MIX_FreeAllPaintbuffers( void );
void MIX_PaintChannels( int endtime );

// s_load.c
qboolean S_TestSoundChar( const char *pch, char c );
char *S_SkipSoundChar( const char *pch );
sfx_t *S_FindName( const char *name, int *pfInCache );
sound_t S_RegisterSound( const char *name );
void S_FreeSound( sfx_t *sfx );
void S_InitSounds( void );

// s_dsp.c
void SX_Init( void );
void SX_Free( void );
void CheckNewDspPresets( void );
void DSP_Process( portable_samplepair_t *pbfront, int sampleCount );
void DSP_ClearState( void );

qboolean S_Init( void );
void S_Shutdown( void );
void S_SoundList_f( void );
void S_SoundInfo_f( void );

struct ref_viewpass_s;
channel_t *SND_PickDynamicChannel( int entnum, int channel, sfx_t *sfx, qboolean *ignore );
channel_t *SND_PickStaticChannel( const vec3_t pos, sfx_t *sfx );
int S_GetCurrentStaticSounds( soundlist_t *pout, int size );
int S_GetCurrentDynamicSounds( soundlist_t *pout, int size );
sfx_t *S_GetSfxByHandle( sound_t handle );
rawchan_t *S_FindRawChannel( int entnum, qboolean create );
uint S_RawSamplesStereo( portable_samplepair_t *rawsamples, uint rawend, uint max_samples, uint samples, uint rate, word width, word channels, const byte *data );
void S_RawEntSamples( int entnum, uint samples, uint rate, word width, word channels, const byte *data, int snd_vol );
void S_StopSound( int entnum, int channel, const char *soundname );
void S_UpdateFrame( struct ref_viewpass_s *rvp );
void S_StopAllSounds( qboolean ambient );
void S_FreeSounds( void );

//
// s_mouth.c
//
void SND_InitMouth( int entnum, int entchannel );
void SND_ForceInitMouth( int entnum );
void SND_MoveMouth8( channel_t *ch, wavdata_t *pSource, int count );
void SND_MoveMouth16( channel_t *ch, wavdata_t *pSource, int count );
void SND_MoveMouthRaw( rawchan_t *ch, portable_samplepair_t *pData, int count );
void SND_CloseMouth( channel_t *ch );
void SND_ForceCloseMouth( int entnum );

//
// s_stream.c
//
void S_StreamBackgroundTrack( void );
void S_PrintBackgroundTrackState( void );
void S_FadeMusicVolume( float fadePercent );

//
// s_utils.c
//
int S_ZeroCrossingAfter( wavdata_t *pWaveData, int sample );
int S_ZeroCrossingBefore( wavdata_t *pWaveData, int sample );
int S_ConvertLoopedPosition( wavdata_t *pSource, int samplePosition, qboolean use_loop );
int S_GetOutputData( wavdata_t *pSource, void **pData, int samplePosition, int sampleCount, qboolean use_loop );

//
// s_vox.c
//
void VOX_Init( void );
void VOX_Shutdown( void );
void VOX_SetChanVol( channel_t *ch );
void VOX_LoadSound( channel_t *pchan, const char *psz );
float VOX_ModifyPitch( channel_t *ch, float pitch );
int VOX_MixDataToDevice( channel_t *pChannel, int sampleCount, int outputRate, int outputOffset );

#endif//SOUND_H
