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
#include "sound_api.h"

// sound engine rate defines
#define SOUND_11k       11025 // 11khz sample rate
#define SOUND_22k       22050 // 22khz sample rate
#define SOUND_44k       44100 // 44khz sample rate

#define SOUND_DMA_SPEED SOUND_44k // hardware playback rate

#define PAINTBUFFER_SIZE 1024

#define S_RAW_SOUND_IDLE_SEC         10 // time interval for idling raw sound before it's freed
#define S_RAW_SOUND_BACKGROUNDTRACK  -2
#define S_RAW_SOUND_SOUNDTRACK       -1
#define S_RAW_SAMPLES_PRECISION_BITS 14

#define CVOXWORDMAX    64

#define CLIP16( x ) bound( SHRT_MIN + 8, x, SHRT_MAX - 8 )

//====================================================================

#define MAX_DYNAMIC_CHANNELS (60 + NUM_AMBIENTS)
#define MAX_CHANNELS         (256 + MAX_DYNAMIC_CHANNELS) // Scourge Of Armagon has too many static sounds on hip2m4.bsp
#define MAX_RAW_CHANNELS     48
#define MAX_RAW_SAMPLES      16384
#define SND_CLIP_DISTANCE    1000.0f

extern int idsp_room;
extern snd_globals_t snd;

extern convar_t s_musicvolume;
extern convar_t s_lerping;
extern convar_t s_test;  // cvar to test new effects
extern convar_t s_samplecount;
extern convar_t s_warn_late_precache;
extern convar_t snd_mute_losefocus;

wavdata_t *S_LoadSound( sfx_t *sfx );
float S_GetMasterVolume( void );
float S_GetMusicVolume( void );

//
// s_main.c
//
int S_RetrieveAudioSamples( const wavdata_t *source, const void **output_buffer, int start_position, int num_samples, qboolean enable_looping );
void S_FreeChannel( channel_t *ch );

//
// s_mix.c
//
void S_ClearBuffers( int num_samples );
void S_PaintChannels( int endtime );

// s_load.c
static inline qboolean S_TestSoundChar( const char *pch, char c )
{
	if( COM_StringEmptyOrNULL( pch ))
		return false;

	return pch[0] == c || pch[1] == c;
}

static inline const char *S_SkipSoundChar( const char *pch )
{
	return pch[0] == '!' ? pch + 1 : pch;
}

sfx_t *S_FindName( const char *name, qboolean *pfInCache );
sound_t S_RegisterSound( const char *name );
void S_FreeSound( sfx_t *sfx );
void S_InitSounds( void );

// s_dsp.c
void SX_Init( void );
void SX_Free( void );
void SX_RoomFX( portable_samplepair_t *paint, int num_samples );
void SX_ClearState( void );

qboolean S_Init( void );
qboolean S_InitSoundAPI( void );
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
void S_RawEntSamples( int entnum, uint samples, uint rate, word width, word channels, const byte *data, int snd_vol, float attn );
void S_StopSound( int entnum, int channel, const char *soundname );
void S_UpdateFrame( struct ref_viewpass_s *rvp );
void S_StopAllSounds( qboolean ambient );
void S_FreeSounds( void );

//
// s_mouth.c
//
void SND_ForceInitMouth( int entnum );
void SND_ForceCloseMouth( int entnum );

void SND_MoveMouth8( mouth_t *mouth, int pos, const wavdata_t *sc, int count, qboolean use_loop );
void SND_MoveMouth16( mouth_t *mouth, int pos, const wavdata_t *sc, int count, qboolean use_loop );
void SND_MoveMouthRaw( mouth_t *mouth, const portable_samplepair_t *buf, int count );

static inline void SND_InitMouth( int entnum, int entchannel )
{
	if(( entchannel == CHAN_VOICE || entchannel == CHAN_STREAM ) && entnum > 0 )
	{
		SND_ForceInitMouth( entnum );
	}
}

static inline void SND_CloseMouth( const channel_t *ch )
{
	if( ch->entchannel == CHAN_VOICE || ch->entchannel == CHAN_STREAM )
	{
		SND_ForceCloseMouth( ch->entnum );
	}
}

//
// s_stream.c
//
void S_StreamBackgroundTrack( void );
void S_PrintBackgroundTrackState( void );
void S_MusicFade( float fade_percent );

//
// s_vox.c
//
void VOX_Init( void );
void VOX_Shutdown( void );
void VOX_SetChanVol( channel_t *ch );
void VOX_LoadSound( channel_t *pchan, const char *psz );
float VOX_ModifyPitch( channel_t *ch, float pitch );
void VOX_LoadWord( channel_t *pchan );
void VOX_FreeWord( channel_t *pchan );

#endif//SOUND_H
