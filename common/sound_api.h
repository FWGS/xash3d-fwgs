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

// Forward decls (full def in engine/client/sound.h)
struct channel_s;
struct rawchan_s;
struct portable_samplepair_s;

// Engine-owned state: client reads only. Filled by engine, passed to pfnS_Init.
typedef struct snd_interface_state_s
{
	const struct listener_s *listener;
	const struct channel_s  *channels;        // &channels[0], contiguous
	int                      total_channels;
	struct rawchan_s *const *raw_channels;   // rawchan_t*[]
	int                      max_raw_channels;
} snd_interface_state_t;

// API from engine to client (client calls these)
typedef struct sound_api_s
{
	qboolean      (*CL_GetEntitySpatialization)( struct channel_s *ch );
	struct sfx_s* (*S_GetSfxByHandle)( sound_t handle );
} sound_api_t;

// Callbacks from client to engine (engine calls these when custom sound is active)
typedef struct sound_interface_s
{
	int version;

	qboolean (*pfnS_Init)( snd_interface_state_t *state );
	void     (*pfnS_Shutdown)( void );
	void     (*pfnS_UpdateSound)( void );
	/* Full paint: endtime (sample pairs), dma buffer, paintedtime in/out. Client does mix + transfer to dma.buffer. */
	void     (*pfnS_PaintChannels)( int endtime, struct dma_api_s *dma, int *paintedtime );
	void     (*pfnS_UpdateChannel)( int ch_idx, const struct channel_s *ch, sound_t handle );  // ch=NULL -> channel freed
	void     (*pfnS_UpdateRawChannel)( int raw_idx, struct rawchan_s *ch );  // ch=NULL -> channel freed
	void     (*pfnS_Spatialize)( struct channel_s *ch );
	void     (*pfnS_FreeSound)( struct sfx_s *sfx, sound_t handle );
} sound_interface_t;

#endif // SOUND_API_H
