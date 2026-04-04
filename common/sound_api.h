/*
sound_api.h - Xash3D extension for client sound interface
Copyright (C) 2026 Xash3D FWGS Developers

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#ifndef SOUND_API_H
#define SOUND_API_H

#include "xash3d_types.h"

#define CL_SOUND_INTERFACE_VERSION	1

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
