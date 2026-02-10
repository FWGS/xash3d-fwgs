/*
sound_api.h - Xash3D extension for client sound interface
Copyright (C) 2026

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef SOUND_API_H
#define SOUND_API_H

#include "xash3d_types.h"

#define CL_SOUND_INTERFACE_VERSION	1

// sound handle (same as engine's sound_t)
typedef int sound_t;

struct dma_api_s;
struct listener_s;

// API from engine to client (client can call these)
// GetSoundWavdata returns (struct wavdata_s *) — cast in engine; full def in engine/common/common.h
typedef struct sound_api_s
{
	sound_t  (*S_RegisterSound)( const char *name );
	void     (*Host_Error)( const char *error, ... );
	void     (*Cvar_Set)( const char *name, const char *value );
	void     (*Con_Printf)( const char *fmt, ... );
	void     (*Con_Reportf)( const char *fmt, ... );
	void    *(*GetSoundWavdata)( sound_t handle );
	// ONLY ADD NEW FUNCTIONS TO THE END
} sound_api_t;

// Callbacks from client to engine (engine calls these when custom sound is active)
// pfn prefix avoids name clash with S_* declarations in common.h
typedef struct sound_interface_s
{
	int version;

	qboolean (*pfnS_Init)( struct dma_api_s *dma, struct listener_s *listener );
	void     (*pfnS_Shutdown)( void );
	void     (*pfnS_StartSound)( const vec3_t pos, int ent, int chan, sound_t handle, float fvol, float attn, int pitch, int flags );
	void     (*pfnS_StopSound)( int entnum, int channel, const char *soundname );
	void     (*pfnS_StopAllSounds)( qboolean ambient );
	void     (*pfnSND_UpdateSound)( void );
	void     (*pfnS_StartLocalSound)( const char *name, float volume, qboolean reliable );
	void     (*pfnS_AmbientSound)( const vec3_t pos, int ent, sound_t handle, float fvol, float attn, int pitch, int flags );
	void     (*pfnS_RestoreSound)( const vec3_t pos, int ent, int chan, sound_t handle, float fvol, float attn, int pitch, int flags, double sample, double end, int wordIndex );
	void     (*pfnS_RawEntSamplesEx)( int entnum, uint samples, uint rate, unsigned short width, unsigned short channels, const byte *data, int snd_vol, float attn );
	void     (*pfnS_FadeMusicVolume)( float fadePercent );
	void     (*pfnS_ExtraUpdate)( void );
} sound_interface_t;

#endif // SOUND_API_H
