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

extern byte *sndpool;

#include "xash3d_mathlib.h"

// sound engine rate defines
#define SOUND_DMA_SPEED		44100	// hardware playback rate
#define SOUND_11k			11025	// 11khz sample rate
#define SOUND_16k			16000	// 16khz sample rate
#define SOUND_22k			22050	// 22khz sample rate
#define SOUND_32k			32000	// 32khz sample rate
#define SOUND_44k			44100	// 44khz sample rate
#define DMA_MSEC_PER_SAMPLE		((float)(1000.0 / SOUND_DMA_SPEED))

// fixed point stuff for real-time resampling
#define FIX_BITS			28
#define FIX_SCALE			(1 << FIX_BITS)
#define FIX_MASK			((1 << FIX_BITS)-1)
#define FIX_FLOAT(a)		((int)((a) * FIX_SCALE))
#define FIX(a)			(((int)(a)) << FIX_BITS)
#define FIX_INTPART(a)		(((int)(a)) >> FIX_BITS)
#define FIX_FRACTION(a,b)		(FIX(a)/(b))
#define FIX_FRACPART(a)		((a) & FIX_MASK)

// NOTE: clipped sound at 32760 to avoid overload
#define CLIP( x )			(( x ) > 32760 ? 32760 : (( x ) < -32760 ? -32760 : ( x )))

#define PAINTBUFFER_SIZE		1024	// 44k: was 512
#define PAINTBUFFER			(g_curpaintbuffer)
#define CPAINTBUFFERS		3

// sound mixing buffer
#define CPAINTFILTERMEM		3
#define CPAINTFILTERS		4	// maximum number of consecutive upsample passes per paintbuffer

#define S_RAW_SOUND_IDLE_SEC		10	// time interval for idling raw sound before it's freed
#define S_RAW_SOUND_BACKGROUNDTRACK	-2
#define S_RAW_SOUND_SOUNDTRACK	-1
#define S_RAW_SAMPLES_PRECISION_BITS	14

#define CIN_FRAMETIME		(1.0f / 30.0f)

typedef struct
{
	int			left;
	int			right;
} portable_samplepair_t;

typedef struct
{
	qboolean			factive;	// if true, mix to this paintbuffer using flags
	portable_samplepair_t	*pbuf;	// front stereo mix buffer, for 2 or 4 channel mixing
	int			ifilter;	// current filter memory buffer to use for upsampling pass
	portable_samplepair_t	fltmem[CPAINTFILTERS][CPAINTFILTERMEM];
} paintbuffer_t;

typedef struct sfx_s
{
	char		name[MAX_QPATH];
	wavdata_t		*cache;

	int		servercount;
	uint		hashValue;
	struct sfx_s	*hashNext;
} sfx_t;

extern portable_samplepair_t	paintbuffer[];
extern portable_samplepair_t	roombuffer[];
extern portable_samplepair_t	temppaintbuffer[];
extern portable_samplepair_t	*g_curpaintbuffer;
extern paintbuffer_t	paintbuffers[];

// structure used for fading in and out client sound volume.
typedef struct
{
	float		initial_percent;
	float		percent;  	// how far to adjust client's volume down by.
	float		starttime;	// GetHostTime() when we started adjusting volume
	float		fadeouttime;	// # of seconds to get to faded out state
	float		holdtime;		// # of seconds to hold
	float		fadeintime;	// # of seconds to restore
} soundfade_t;

typedef struct
{
	float		percent;
} musicfade_t;

typedef struct snd_format_s
{
	unsigned int	speed;
	unsigned int	width;
	unsigned int	channels;
} snd_format_t;

typedef struct
{
	snd_format_t	format;
	int		samples;		// mono samples in buffer
	int		samplepos;	// in mono samples
	byte		*buffer;
	qboolean		initialized;	// sound engine is active
} dma_t;

#include "vox.h"

typedef struct
{
	double		sample;

	wavdata_t		*pData;
	double 		forcedEndSample;
	qboolean		finished;
} mixer_t;

typedef struct rawchan_s
{
	int			entnum;
	int			master_vol;
	int			leftvol;		// 0-255 left volume
	int			rightvol;		// 0-255 right volume
	float			dist_mult;	// distance multiplier (attenuation/clipK)
	vec3_t			origin;		// only use if fixed_origin is set
	float			radius;		// radius of this sound effect
	volatile uint		s_rawend;
	wavdata_t			sound_info;	// advance play position
	float			oldtime;		// catch time jumps
	size_t			max_samples;	// buffer length
	portable_samplepair_t	rawsamples[1];	// variable sized
} rawchan_t;

typedef struct channel_s
{
	char		name[16];		// keept sentence name
	sfx_t		*sfx;		// sfx number

	int		leftvol;		// 0-255 left volume
	int		rightvol;		// 0-255 right volume

	int		entnum;		// entity soundsource
	int		entchannel;	// sound channel (CHAN_STREAM, CHAN_VOICE, etc.)
	vec3_t		origin;		// only use if fixed_origin is set
	float		dist_mult;	// distance multiplier (attenuation/clipK)
	int		master_vol;	// 0-255 master volume
	qboolean		isSentence;	// bit who indicated sentence
	int		basePitch;	// base pitch percent (100% is normal pitch playback)
	float		pitch;		// real-time pitch after any modulation or shift by dynamic data
	qboolean		use_loop;		// don't loop default and local sounds
	qboolean		staticsound;	// use origin instead of fetching entnum's origin
	qboolean		localsound;	// it's a local menu sound (not looped, not paused)
	mixer_t		pMixer;

	// sentence mixer
	int		wordIndex;
	mixer_t		*currentWord;	// NULL if sentence is finished
	voxword_t		words[CVOXWORDMAX];
} channel_t;

typedef struct
{
	vec3_t		origin;		// simorg + view_ofs
	vec3_t		velocity;
	vec3_t		forward;
	vec3_t		right;
	vec3_t		up;

	int		entnum;
	int		waterlevel;
	float		frametime;	// used for sound fade
	qboolean		active;
	qboolean		inmenu;		// listener in-menu ?
	qboolean		paused;
	qboolean		streaming;	// playing AVI-file
	qboolean		stream_paused;	// pause only background track
} listener_t;

typedef struct
{
	string		current;		// a currently playing track
	string		loopName;		// may be empty
	stream_t		*stream;
	int		source;		// may be game, menu, etc
} bg_track_t;

//====================================================================

#define MAX_DYNAMIC_CHANNELS	(60 + NUM_AMBIENTS)
#define MAX_CHANNELS	(256 + MAX_DYNAMIC_CHANNELS)	// Scourge Of Armagon has too many static sounds on hip2m4.bsp
#define MAX_RAW_CHANNELS	16
#define MAX_RAW_SAMPLES	8192

extern sound_t	ambient_sfx[NUM_AMBIENTS];
extern qboolean	snd_ambient;
extern channel_t	channels[MAX_CHANNELS];
extern rawchan_t	*raw_channels[MAX_RAW_CHANNELS];
extern int	total_channels;
extern int	paintedtime;
extern int	soundtime;
extern listener_t	s_listener;
extern int	idsp_room;
extern dma_t	dma;

extern convar_t	*s_volume;
extern convar_t	*s_musicvolume;
extern convar_t	*s_show;
extern convar_t	*s_mixahead;
extern convar_t	*s_lerping;
extern convar_t	*dsp_off;
extern convar_t	*s_test;		// cvar to testify new effects
extern convar_t *s_samplecount;
extern convar_t *snd_mute_losefocus;

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
void DSP_Process( int idsp, portable_samplepair_t *pbfront, int sampleCount );
float DSP_GetGain( int idsp );
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
void S_RawSamples( uint samples, uint rate, word width, word channels, const byte *data, int entnum );
void S_StopSound( int entnum, int channel, const char *soundname );
void S_UpdateFrame( struct ref_viewpass_s *rvp );
uint S_GetRawSamplesLength( int entnum );
void S_ClearRawChannel( int entnum );
void S_StopAllSounds( qboolean ambient );
void S_FreeSounds( void );

//
// s_mouth.c
//
void SND_InitMouth( int entnum, int entchannel );
void SND_MoveMouth8( channel_t *ch, wavdata_t *pSource, int count );
void SND_MoveMouth16( channel_t *ch, wavdata_t *pSource, int count );
void SND_CloseMouth( channel_t *ch );

//
// s_stream.c
//
void S_StreamSoundTrack( void );
void S_StreamBackgroundTrack( void );
qboolean S_StreamGetCurrentState( char *currentTrack, char *loopTrack, int *position );
void S_PrintBackgroundTrackState( void );
void S_FadeMusicVolume( float fadePercent );

//
// s_utils.c
//
int S_ZeroCrossingAfter( wavdata_t *pWaveData, int sample );
int S_ZeroCrossingBefore( wavdata_t *pWaveData, int sample );
int S_GetOutputData( wavdata_t *pSource, void **pData, int samplePosition, int sampleCount, qboolean use_loop );
void S_SetSampleStart( channel_t *pChan, wavdata_t *pSource, int newPosition );
void S_SetSampleEnd( channel_t *pChan, wavdata_t *pSource, int newEndPosition );

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
