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

// Experimental implementation, backward compatibility is not guaranteed.
#define CL_SOUND_INTERFACE_VERSION	1

#define FL_VOXWORD_IN_CACHE BIT( 0 ) // if set, it was loaded prior and shouldn't be freed

#define FL_CHAN_USE_LOOP          BIT( 0 ) // don't loop default and local sounds
#define FL_CHAN_STATIC_SOUND      BIT( 1 ) // use origin instead of fetching entnum's origin
#define FL_CHAN_LOCAL_SOUND       BIT( 2 ) // it's a local menu sound (not looped, not paused)
#define FL_CHAN_SENTENCE_FINISHED BIT( 4 ) // if set, finished playing sentence
#define FL_CHAN_FINISHED          BIT( 5 ) // if set, finished playing single word

typedef int sound_t;

typedef struct portable_samplepair_s
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

typedef struct voxword_s
{
	sfx_t    *sfx;
	uint16_t volume;       // volume percent
	uint16_t pitch;        // pitch shift percent (keep large for extra chipmunk fun)
	uint8_t  timecompress; // percent of skipped data (speeds up playback without pitch shift)
	uint8_t  start;        // percent at which playback starts
	uint8_t  end;          // percent at which playback ends
	uint8_t  flags;
} voxword_t;

typedef struct channel_s
{
	char      name[16];    // keep sentence name
	sfx_t     *sfx;        // sfx number

	vec3_t    origin;      // only use if fixed_origin is set
	float     dist_mult;   // distance multiplier (attenuation/clipK)

	int       entchannel;  // sound channel (CHAN_STREAM, CHAN_VOICE, etc.)
	uint      flags;
	short     entnum;      // entity soundsource
	short     master_vol;  // 0-255 master volume
	short     leftvol;     // 0-255 left volume
	short     rightvol;    // 0-255 right volume
	short     basePitch;   // base pitch percent (100% is normal pitch playback)
	byte      word_index;

	// HACKHACK: count when this channel became inaudible
	// to not free it when it could be respatialized soon
#define MAX_CHANNEL_INAUDIBLE_TIME 0.1f
	float     inauduble_free_time;

	double    sample;
	double    forced_end;
	wavdata_t *data;
	voxword_t *words; // dynamically allocated, (num_words + 1) entries, null sfx terminates

	uintptr_t engine_reserved[8]; // only for engine developers
	uintptr_t game_reserved[8];   // free space for game developers
} channel_t;

typedef struct rawchan_s
{
	short                 entnum;
	short                 master_vol;
	short                 leftvol;       // 0-255 left volume
	short                 rightvol;      // 0-255 right volume
	float                 dist_mult;     // distance multiplier (attenuation/clipK)
	vec3_t                origin;        // only use if fixed_origin is set
	volatile uint         s_rawend;
	float                 oldtime;       // catch time jumps

	uintptr_t engine_reserved[8]; // only for engine developers
	uintptr_t game_reserved[8];   // free space for game developers

	size_t                max_samples;   // buffer length
	portable_samplepair_t rawsamples[]; // variable sized
} rawchan_t;

typedef struct snd_format_s
{
	uint speed;
	byte width;
	byte channels;
} snd_format_t;

typedef struct snd_globals_s
{
	// dma
	const char   *backend_name;
	byte         *buffer;
	snd_format_t format;
	qboolean     initialized; // sound engine is active
	int          samples; // mono samples in buffer
	int          samplepos; // in mono samples

	int          paintedtime; // total samples that have been mixed at speed
	int          soundtime; // total samples that have been played out to hardware at dma speed

	// listener (client, camera, etc)
	vec3_t       origin;
	vec3_t       forward, right, up;
	int          entnum;
	qboolean     streaming; // playing AVI-file
	qboolean     stream_paused; // pause only background track

	// SoundAPI shared pointers
	channel_t    *const channels;
	int          max_channels;
	int          total_channels;
	rawchan_t    **const raw_channels;
	int          max_raw_channels;
	sound_t      ambient_sfx[NUM_AMBIENTS];
	qboolean     have_ambient_sfx;
} snd_globals_t;

typedef struct voice_audio_info_s
{
	uint width;
	uint samplerate;
	uint frame_size; // in samples
} voice_audio_info_t;

// API from engine to client (client calls these)
typedef struct sound_api_s
{
	qboolean           (*CL_GetEntitySpatialization)( channel_t *ch );
	sfx_t*             (*S_GetSfxByHandle)( sound_t handle );
	// Voice extensions
	void               (*pfnS_RawEntSamples)( int entnum, uint samples, uint rate, word width, word channels, const byte *data, int snd_vol, float attn );
	void               (*pfnSND_ForceInitMouth)( int entnum );
	voice_audio_info_t (*pfnVoice_GetAudioInfo)( void );
} sound_api_t;

// Callbacks from client to engine (engine calls these when custom sound is active)
typedef struct sound_interface_s
{
	int version;

	qboolean (*pfnS_Init)( snd_globals_t *globals );
	void     (*pfnS_Shutdown)( void );
	void     (*pfnS_UpdateSound)( void );
	/* Full paint: endtime (sample pairs), dma buffer, paintedtime in/out. Client does mix + transfer to dma.buffer. */
	void     (*pfnS_PaintChannels)( int endtime );
	void     (*pfnS_UpdateChannel)( int ch_idx, const channel_t *ch, sound_t handle );  // ch=NULL -> channel freed
	void     (*pfnS_UpdateRawChannel)( int raw_idx, rawchan_t *ch );  // ch=NULL -> channel freed
	void     (*pfnS_Spatialize)( channel_t *ch );
	void     (*pfnS_FreeSound)( sfx_t *sfx, sound_t handle );
} sound_interface_t;

#endif // SOUND_API_H
