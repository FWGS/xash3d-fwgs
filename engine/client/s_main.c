/*
s_main.c - sound engine
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

#include "common.h"
#include "sound.h"
#include "client.h"
#include "con_nprint.h"
#include "pm_local.h"
#include "platform/platform.h"

// structure used for fading in and out client sound volume.
struct
{
	int    start_percent;
	int    percent;
	double start_time;
	int    out_seconds;
	int    hold_time;
	int    in_seconds;
} soundfade;

dma_t        dma;
poolhandle_t sndpool;
sound_t      ambient_sfx[NUM_AMBIENTS];
qboolean     snd_ambient = false;
qboolean     snd_fade_sequence = false;
listener_t   s_listener;

channel_t    channels[MAX_CHANNELS];
rawchan_t    *raw_channels[MAX_RAW_CHANNELS];
int          total_channels;

int          soundtime;	// sample PAIRS
int          paintedtime; 	// sample PAIRS

static CVAR_DEFINE( s_volume, "volume", "0.7", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "sound volume" );
CVAR_DEFINE( s_musicvolume, "MP3Volume", "1.0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "background music volume" );
static CVAR_DEFINE( s_mixahead, "_snd_mixahead", "0.12", FCVAR_FILTERABLE, "how much sound to mix ahead of time" );
static CVAR_DEFINE_AUTO( s_show, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "show playing sounds" );
CVAR_DEFINE_AUTO( s_lerping, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "apply interpolation to sound output" );
static CVAR_DEFINE( s_ambient_level, "ambient_level", "0.3", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "volume of environment noises (water and wind)" );
static CVAR_DEFINE( s_ambient_fade, "ambient_fade", "1000", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "rate of volume fading when client is moving" );
static CVAR_DEFINE_AUTO( s_combine_sounds, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "combine channels with same sounds" );
CVAR_DEFINE_AUTO( snd_mute_losefocus, "1", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "silence the audio when game window loses focus" );
CVAR_DEFINE_AUTO( s_test, "0", 0, "engine developer cvar for quick testing new features" );
CVAR_DEFINE_AUTO( s_samplecount, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "sample count (0 for default value)" );
CVAR_DEFINE_AUTO( s_warn_late_precache, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "warn about late precached sounds on client-side" );

/*
=============================================================================

		SOUNDS PROCESSING

=============================================================================
*/
static int S_AdjustLoopedSamplePosition( const wavdata_t *source, int current_sample, qboolean enable_looping )
{
	// check if looping is enabled and we've exceeeded the sample boundary
	if( enable_looping && FBitSet( source->flags, SOUND_LOOPED ) && current_sample >= source->samples )
	{
		// adjust position relative to loop start
		current_sample -= source->loop_start;

		// apply modulo to wrap within loop bounds
		int loop_range = source->samples - source->loop_start;
		if( loop_range > 0 )
			current_sample = source->loop_start + ( current_sample % loop_range );
	}

	return current_sample;
}

int S_RetrieveAudioSamples( const wavdata_t *source, const void **output_buffer, int start_position, int num_samples, qboolean enable_looping )
{
	// handle looping
	start_position = S_AdjustLoopedSamplePosition( source, start_position, enable_looping );

	// calculate how many samples are available from current position
	int available_samples = Q_max( 0, source->samples - start_position );

	// limit requested samples to available samples
	if( num_samples > available_samples )
		num_samples = available_samples;

	// if none, exit early
	if( num_samples <= 0 )
		return 0;

	// calculate the size of each sample frame
	int frame_size = Q_max( 1, source->width * source->channels );

	// convert sample position to byte offset
	start_position *= frame_size;

	*output_buffer = source->buffer + start_position;

	return num_samples;
}

/*
=================
S_GetMasterVolume
=================
*/
float S_GetMasterVolume( void )
{
	float	scale = 1.0f;

	if( host.status == HOST_NOFOCUS && snd_mute_losefocus.value != 0.0f )
	{
		// we return zero volume to keep sounds running
		return 0.0f;
	}

	// apparently goldsrc doesn't care about soundfade
	// despite this, let's keep support for it
	if( cls.key_dest != key_menu && soundfade.percent != 0 )
	{
		scale = soundfade.percent / 100.f;
		scale = bound( 0.0f, scale, 1.0f );
		scale = 1.0f - scale;
	}

	return s_volume.value * scale;
}

/*
=================
S_SoundFade
=================
*/
void S_SoundFade( int fade_percent, int hold_time, int fade_out_seconds, int fade_in_seconds )
{
	soundfade.start_percent = fade_percent;
	soundfade.hold_time     = hold_time;
	soundfade.out_seconds   = fade_out_seconds;
	soundfade.in_seconds    = fade_in_seconds;

	soundfade.start_time = host.realtime;
}

/*
=================
S_IsClient
=================
*/
static qboolean S_IsClient( int entnum )
{
	return entnum == s_listener.entnum;
}


// free channel so that it may be allocated by the
// next request to play a sound.  If sound is a
// word in a sentence, release the sentence.
// Works for static, dynamic, sentence and stream sounds
/*
=================
S_FreeChannel
=================
*/
void S_FreeChannel( channel_t *ch )
{
	ch->sfx = NULL;
	ch->name[0] = '\0';
	ch->use_loop = false;
	ch->is_sentence = false;
	ch->forced_end = ch->sample = 0.0;
	ch->data = NULL;
	ch->finished = false;

	SND_CloseMouth( ch );
}

/*
=================
S_UpdateSoundFade
=================
*/
static void S_UpdateSoundFade( void )
{
	if( host.realtime < soundfade.in_seconds + soundfade.out_seconds + soundfade.hold_time + soundfade.start_time )
	{
		float f = host.realtime - soundfade.start_time;

		if( soundfade.out_seconds && ( f < soundfade.out_seconds ))
		{
			soundfade.percent = soundfade.start_percent * ( f / soundfade.out_seconds );
		}
		else if ( f >= soundfade.hold_time + soundfade.out_seconds )
		{
			f = f - ( soundfade.hold_time + soundfade.out_seconds );
			soundfade.percent = soundfade.start_percent * ( 1.0f - f / (float)soundfade.out_seconds );
		}
		else
		{
			soundfade.percent = soundfade.start_percent;
		}

		if( snd_fade_sequence )
		{
			S_MusicFade( soundfade.percent );

			if( soundfade.percent >= 100 )
			{
				S_StopAllSounds( false );
				S_StopBackgroundTrack();
				snd_fade_sequence = false;
			}
		}
	}
	else
	{
		soundfade.percent = 0;
	}
}

/*
=================
SND_FStreamIsPlaying

Select a channel from the dynamic channel allocation area.  For the given entity,
override any other sound playing on the same channel (see code comments below for
exceptions).
=================
*/
static qboolean SND_FStreamIsPlaying( sfx_t *sfx )
{
	for( int i = NUM_AMBIENTS; i < MAX_DYNAMIC_CHANNELS; i++ )
	{
		if( channels[i].sfx == sfx )
			return true;
	}
	return false;
}

/*
=================
SND_GetChannelTimeLeft

TODO: this function needs to be removed after whole sound subsystem rewrite
=================
*/
static int SND_GetChannelTimeLeft( const channel_t *ch )
{
	int remaining;

	if( ch->finished || !ch->sfx || !ch->sfx->cache )
		return 0;

	if( ch->is_sentence ) // sentences are special, count all remaining words
	{
		int i;

		if( ch->sentence_finished )
			return 0;

		// current word
		remaining = ch->forced_end - ch->sample;

		// here we count all remaining words, stopping if no sfx or sound file is available
		// see VOX_LoadWord
		for( i = ch->word_index + 1; i < ARRAYSIZE( ch->words ); i++ )
		{
			wavdata_t *sc;
			int end;

			// don't continue with broken sentences
			if( !ch->words[i].sfx )
				break;

			if( !( sc = S_LoadSound( ch->words[i].sfx )))
				break;

			end = ch->words[i].end;

			if( end )
				remaining += sc->samples * 0.01f * end;
			else remaining += sc->samples;
		}
	}
	else
	{
		int samples = ch->sfx->cache->samples;
		int curpos = S_AdjustLoopedSamplePosition( ch->sfx->cache, ch->sample, ch->use_loop );

		remaining = bound( 0, samples - curpos, samples );
	}

	return remaining;
}

/*
=================
SND_PickDynamicChannel

Select a channel from the dynamic channel allocation area.  For the given entity,
override any other sound playing on the same channel (see code comments below for
exceptions).
=================
*/
channel_t *SND_PickDynamicChannel( int entnum, int channel, sfx_t *sfx, qboolean *ignore )
{
	int	ch_idx;
	int	first_to_die;
	int	life_left;
	int	timeleft;

	// check for replacement sound, or find the best one to replace
	first_to_die = -1;
	life_left = 0x7fffffff;
	if( ignore ) *ignore = false;

	if( channel == CHAN_STREAM && SND_FStreamIsPlaying( sfx ))
	{
		if( ignore )
			*ignore = true;
		return NULL;
	}

	for( ch_idx = NUM_AMBIENTS; ch_idx < MAX_DYNAMIC_CHANNELS; ch_idx++ )
	{
		channel_t	*ch = &channels[ch_idx];

		// Never override a streaming sound that is currently playing or
		// voice over IP data that is playing or any sound on CHAN_VOICE( acting )
		if( ch->sfx && ( ch->entchannel == CHAN_STREAM ))
			continue;

		if( channel != CHAN_AUTO && ch->entnum == entnum && ( ch->entchannel == channel || channel == -1 ))
		{
			// always override sound from same entity
			first_to_die = ch_idx;
			break;
		}

		// don't let monster sounds override player sounds
		if( ch->sfx && S_IsClient( ch->entnum ) && !S_IsClient( entnum ))
			continue;

		// try to pick the sound with the least amount of data left to play
		timeleft = SND_GetChannelTimeLeft( ch );

		if( timeleft < life_left )
		{
			life_left = timeleft;
			first_to_die = ch_idx;
		}
	}

	if( first_to_die == -1 )
		return NULL;

	if( channels[first_to_die].sfx )
	{
		// don't restart looping sounds for the same entity
		wavdata_t	*sc = channels[first_to_die].sfx->cache;

		if( sc && FBitSet( sc->flags, SOUND_LOOPED ))
		{
			channel_t	*ch = &channels[first_to_die];

			if( ch->entnum == entnum && ch->entchannel == channel && ch->sfx == sfx )
			{
				if( ignore ) *ignore = true;
				// same looping sound, same ent, same channel, don't restart the sound
				return NULL;
			}
		}

		// be sure and release previous channel if sentence.
		S_FreeChannel( &( channels[first_to_die] ));
	}

	return &channels[first_to_die];
}

/*
=====================
SND_PickStaticChannel

Pick an empty channel from the static sound area, or allocate a new
channel.  Only fails if we're at max_channels (128!!!) or if
we're trying to allocate a channel for a stream sound that is
already playing.
=====================
*/
channel_t *SND_PickStaticChannel( const vec3_t pos, sfx_t *sfx )
{
	channel_t	*ch = NULL;
	int	i;

	// check for replacement sound, or find the best one to replace
 	for( i = MAX_DYNAMIC_CHANNELS; i < total_channels; i++ )
 	{
		if( channels[i].sfx == NULL )
			break;

		if( VectorCompare( pos, channels[i].origin ) && channels[i].sfx == sfx )
			break;
	}

	if( i < total_channels )
	{
		// reuse an empty static sound channel
		ch = &channels[i];
	}
	else
	{
		// no empty slots, alloc a new static sound channel
		if( total_channels == MAX_CHANNELS )
		{
			Con_DPrintf( S_ERROR "%s: no free channels\n", __func__ );
			return NULL;
		}

		// get a channel for the static sound
		ch = &channels[total_channels];
		total_channels++;
	}
	return ch;
}

static qboolean S_MaybeAlterChannel( channel_t *ch, int entnum, int entchannel, int flags, const sfx_t *sfx, int pitch, int vol )
{
	if( !ch->sfx )
		return false;

	if( ch->entnum != entnum )
		return false;

	// if no sfx passed, check if it's a sentence
	if( !sfx )
	{
		if( !ch->is_sentence )
			return false;
	}
	else
	{
		if( ch->sfx != sfx )
			return false;
	}

	if( ch->entchannel != entchannel )
		return false;

	if( FBitSet( flags, SND_STOP ))
	{
		S_FreeChannel( ch );
		return true;
	}

	if( FBitSet( flags, SND_CHANGE_PITCH ))
		ch->basePitch = pitch;

	if( FBitSet( flags, SND_CHANGE_VOL ))
		ch->master_vol = vol;

	return true;
}

/*
=================
S_AlterChannel

search through all channels for a channel that matches this
soundsource, entchannel and sfx, and perform alteration on channel
as indicated by 'flags' parameter. If shut down request and
sfx contains a sentence name, shut off the sentence.
returns TRUE if sound was altered,
returns FALSE if sound was not found (sound is not playing)
=================
*/
static int S_AlterChannel( int entnum, int channel, const sfx_t *sfx, int vol, int pitch, int flags )
{
	channel_t	*ch;
	int	i;

	if( S_TestSoundChar( sfx->name, '!' ))
	{
		// This is a sentence name.
		// For sentences: assume that the entity is only playing one sentence
		// at a time, so we can just shut off
		// any channel that has ch->is_sentence >= 0 and matches the entnum.

		for( i = NUM_AMBIENTS, ch = channels + NUM_AMBIENTS; i < total_channels; i++, ch++ )
		{
			if( S_MaybeAlterChannel( ch, entnum, channel, flags, NULL, pitch, vol ))
				return true;
		}

		// channel not found
		return false;

	}

	// regular sound or streaming sound
	for( i = NUM_AMBIENTS, ch = channels + NUM_AMBIENTS; i < total_channels; i++, ch++ )
	{
		if( S_MaybeAlterChannel( ch, entnum, channel, flags, sfx, pitch, vol ))
			return true;
	}

	return false;
}

/*
=================
S_SpatializeChannel
=================
*/
static void S_SpatializeChannel( int *left_vol, int *right_vol, int master_vol, float dot, float dist )
{
	float scale;

	// add in distance effect
	scale = ( 1.0f - dist ) * ( 1.0f + dot );
	float rvol = round( master_vol * scale );

	scale = ( 1.0f - dist ) * ( 1.0f - dot );
	float lvol = round( master_vol * scale );

	*right_vol = (int)( bound( 0.0f, rvol, 255.0f ));
	*left_vol = (int)( bound( 0.0f, lvol, 255.0f ));
}

/*
=================
SND_Spatialize
=================
*/
static void SND_Spatialize( channel_t *ch )
{
	// anything coming from the view entity will allways be full volume
	if( S_IsClient( ch->entnum ))
	{
		ch->leftvol = ch->master_vol;
		ch->rightvol = ch->master_vol;

		// if playing a word, set volume
		VOX_SetChanVol( ch );
		return;
	}

	if( !ch->staticsound )
	{
		if( !CL_GetEntitySpatialization( ch ))
		{
			// origin is null and entity not exist on client
			ch->leftvol = ch->rightvol = 0;
			return;
		}
	}

	// source_vec is vector from listener to sound source
	// player sounds come from 1' in front of player
	vec3_t source_vec;
	VectorSubtract( ch->origin, s_listener.origin, source_vec );

	// normalize source_vec and get distance from listener to source
	float dist = VectorNormalizeLength( source_vec );
	float dot = DotProduct( s_listener.right, source_vec );

	if( !FBitSet( host.bugcomp, BUGCOMP_SPATIALIZE_SOUND_WITH_ATTN_NONE ))
	{
		// don't pan sounds with no attenuation
		if( ch->dist_mult <= 0.0f ) dot = 0.0f;
	}

	// fill out channel volumes for single location
	S_SpatializeChannel( &ch->leftvol, &ch->rightvol, ch->master_vol, dot, dist * ch->dist_mult );

	// if playing a word, set volume
	VOX_SetChanVol( ch );
}

/*
====================
S_StartSound

Start a sound effect for the given entity on the given channel (ie; voice, weapon etc).
Try to grab a channel out of the 8 dynamic spots available.
Currently used for looping sounds, streaming sounds, sentences, and regular entity sounds.
NOTE: volume is 0.0 - 1.0 and attenuation is 0.0 - 1.0 when passed in.
Pitch changes playback pitch of wave by % above or below 100.  Ignored if pitch == 100

NOTE: it's not a good idea to play looping sounds through StartDynamicSound, because
if the looping sound starts out of range, or is bumped from the buffer by another sound
it will never be restarted.  Use StartStaticSound (pass CHAN_STATIC to EMIT_SOUND or
SV_StartSound.
====================
*/
void S_StartSound( const vec3_t pos, int ent, int chan, sound_t handle, float fvol, float attn, int pitch, int flags )
{
	wavdata_t	*pSource;
	sfx_t	*sfx = NULL;
	channel_t	*target_chan, *check;
	int	vol, ch_idx;
	qboolean	bIgnore = false;

	if( !dma.initialized ) return;
	sfx = S_GetSfxByHandle( handle );
	if( !sfx ) return;

	vol = bound( 0, fvol * 255, 255 );
	if( pitch <= 1 ) pitch = PITCH_NORM; // Invasion issues

	if( flags & ( SND_STOP|SND_CHANGE_VOL|SND_CHANGE_PITCH ))
	{
		if( S_AlterChannel( ent, chan, sfx, vol, pitch, flags ))
			return;

		if( flags & SND_STOP ) return;
		// fall through - if we're not trying to stop the sound,
		// and we didn't find it (it's not playing), go ahead and start it up
	}

	if( !pos ) pos = refState.vieworg;

	if( chan == CHAN_STREAM )
		SetBits( flags, SND_STOP_LOOPING );

	// pick a channel to play on
	if( chan == CHAN_STATIC ) target_chan = SND_PickStaticChannel( pos, sfx );
	else target_chan = SND_PickDynamicChannel( ent, chan, sfx, &bIgnore );

	if( !target_chan )
	{
		if( !bIgnore )
			Con_DPrintf( S_ERROR "dropped sound \"" DEFAULT_SOUNDPATH "%s\"\n", sfx->name );
		return;
	}

	// spatialize
	memset( target_chan, 0, sizeof( *target_chan ));

	VectorCopy( pos, target_chan->origin );
	target_chan->staticsound = ( ent == 0 ) ? true : false;
	target_chan->use_loop = (flags & SND_STOP_LOOPING) ? false : true;
	target_chan->localsound = (flags & SND_LOCALSOUND) ? true : false;
	target_chan->dist_mult = (attn / SND_CLIP_DISTANCE);
	target_chan->master_vol = vol;
	target_chan->entnum = ent;
	target_chan->entchannel = chan;
	target_chan->basePitch = pitch;
	target_chan->is_sentence = false;
	target_chan->sfx = sfx;

	pSource = NULL;

	if( S_TestSoundChar( sfx->name, '!' ))
	{
		// this is a sentence
		// link all words and load the first word
		// NOTE: sentence names stored in the cache lookup are
		// prepended with a '!'.  Sentence names stored in the
		// sentence file do not have a leading '!'.
		VOX_LoadSound( target_chan, S_SkipSoundChar( sfx->name ));
		Q_strncpy( target_chan->name, sfx->name, sizeof( target_chan->name ));
		sfx = target_chan->sfx;
		if( sfx ) pSource = sfx->cache;
	}
	else
	{
		// regular or streamed sound fx
		pSource = S_LoadSound( sfx );
		target_chan->name[0] = '\0';
	}

	if( !pSource )
	{
		S_FreeChannel( target_chan );
		return;
	}

	SND_Spatialize( target_chan );

	// If a client can't hear a sound when they FIRST receive the StartSound message,
	// the client will never be able to hear that sound. This is so that out of
	// range sounds don't fill the playback buffer. For streaming sounds, we bypass this optimization.
	if( !target_chan->leftvol && !target_chan->rightvol )
	{
		// looping sounds don't use this optimization because they should stick around until they're killed.
		if( !sfx->cache || !FBitSet( sfx->cache->flags, SOUND_LOOPED ))
		{
			// if this is a streaming sound, play the whole thing.
			if( chan != CHAN_STREAM )
			{
				S_FreeChannel( target_chan );
				return; // not audible at all
			}
		}
	}

	// Init client entity mouth movement vars
	SND_InitMouth( ent, chan );
}

/*
====================
S_RestoreSound

Restore a sound effect for the given entity on the given channel
====================
*/
void S_RestoreSound( const vec3_t pos, int ent, int chan, sound_t handle, float fvol, float attn, int pitch, int flags, double sample, double end, int wordIndex )
{
	wavdata_t	*pSource;
	sfx_t	*sfx = NULL;
	channel_t	*target_chan;
	qboolean	bIgnore = false;
	int	vol;

	if( !dma.initialized ) return;
	sfx = S_GetSfxByHandle( handle );
	if( !sfx ) return;

	vol = bound( 0, fvol * 255, 255 );
	if( pitch <= 1 ) pitch = PITCH_NORM; // Invasion issues

	// pick a channel to play on
	if( chan == CHAN_STATIC ) target_chan = SND_PickStaticChannel( pos, sfx );
	else target_chan = SND_PickDynamicChannel( ent, chan, sfx, &bIgnore );

	if( !target_chan )
	{
		if( !bIgnore )
			Con_DPrintf( S_ERROR "dropped sound \"" DEFAULT_SOUNDPATH "%s\"\n", sfx->name );
		return;
	}

	// spatialize
	memset( target_chan, 0, sizeof( *target_chan ));

	VectorCopy( pos, target_chan->origin );
	target_chan->staticsound = ( ent == 0 ) ? true : false;
	target_chan->use_loop = (flags & SND_STOP_LOOPING) ? false : true;
	target_chan->localsound = (flags & SND_LOCALSOUND) ? true : false;
	target_chan->dist_mult = (attn / SND_CLIP_DISTANCE);
	target_chan->master_vol = vol;
	target_chan->entnum = ent;
	target_chan->entchannel = chan;
	target_chan->basePitch = pitch;
	target_chan->is_sentence = false;
	target_chan->sfx = sfx;

	pSource = NULL;

	if( S_TestSoundChar( sfx->name, '!' ))
	{
		// this is a sentence
		// link all words and load the first word
		// NOTE: sentence names stored in the cache lookup are
		// prepended with a '!'.  Sentence names stored in the
		// sentence file do not have a leading '!'.
		VOX_LoadSound( target_chan, S_SkipSoundChar( sfx->name ));
		Q_strncpy( target_chan->name, sfx->name, sizeof( target_chan->name ));

		// not a first word in sentence!
		if( wordIndex != 0 )
		{
			VOX_FreeWord( target_chan );		// release first loaded word
			target_chan->word_index = wordIndex;	// restore current word
			VOX_LoadWord( target_chan );

			if( !target_chan->sentence_finished )
			{
				target_chan->sfx = target_chan->words[target_chan->word_index].sfx;
				sfx = target_chan->sfx;
				pSource = sfx->cache;
			}
		}
		else
		{
			sfx = target_chan->sfx;
			if( sfx ) pSource = sfx->cache;
		}
	}
	else
	{
		// regular or streamed sound fx
		pSource = S_LoadSound( sfx );
		target_chan->name[0] = '\0';
	}

	if( !pSource )
	{
		S_FreeChannel( target_chan );
		return;
	}

	SND_Spatialize( target_chan );

	// NOTE: first spatialization may be failed because listener position is invalid at this time
	// so we should keep all sounds an actual and waiting for player spawn.

	// apply the sample offests
	target_chan->sample = sample;
	target_chan->forced_end = end;

	// Init client entity mouth movement vars
	SND_InitMouth( ent, chan );
}

/*
=================
S_AmbientSound

Start playback of a sound, loaded into the static portion of the channel array.
Currently, this should be used for looping ambient sounds, looping sounds
that should not be interrupted until complete, non-creature sentences,
and one-shot ambient streaming sounds.  Can also play 'regular' sounds one-shot,
in case designers want to trigger regular game sounds.
Pitch changes playback pitch of wave by % above or below 100.  Ignored if pitch == 100

NOTE: volume is 0.0 - 1.0 and attenuation is 0.0 - 1.0 when passed in.
=================
*/
void S_AmbientSound( const vec3_t pos, int ent, sound_t handle, float fvol, float attn, int pitch, int flags )
{
	channel_t	*ch;
	wavdata_t	*pSource = NULL;
	sfx_t	*sfx = NULL;
	int	vol, fvox = 0;

	if( !dma.initialized ) return;
	sfx = S_GetSfxByHandle( handle );
	if( !sfx ) return;

	vol = bound( 0, fvol * 255, 255 );
	if( pitch <= 1 ) pitch = PITCH_NORM; // Invasion issues

	if( flags & (SND_STOP|SND_CHANGE_VOL|SND_CHANGE_PITCH))
	{
		if( S_AlterChannel( ent, CHAN_STATIC, sfx, vol, pitch, flags ))
			return;
		if( flags & SND_STOP ) return;
	}

	// pick a channel to play on from the static area
	ch = SND_PickStaticChannel( pos, sfx );
	if( !ch ) return;

	VectorCopy( pos, ch->origin );
	ch->entnum = ent;

	CL_GetEntitySpatialization( ch );

	if( S_TestSoundChar( sfx->name, '!' ))
	{
		// this is a sentence. link words to play in sequence.
		// NOTE: sentence names stored in the cache lookup are
		// prepended with a '!'.  Sentence names stored in the
		// sentence file do not have a leading '!'.

		// link all words and load the first word
		VOX_LoadSound( ch, S_SkipSoundChar( sfx->name ));
		Q_strncpy( ch->name, sfx->name, sizeof( ch->name ));
		sfx = ch->sfx;
		if( sfx ) pSource = sfx->cache;
		fvox = 1;
	}
	else
	{
		// load regular or stream sound
		pSource = S_LoadSound( sfx );
		ch->sfx = sfx;
		ch->is_sentence = false;
		ch->name[0] = '\0';
	}

	if( !pSource )
	{
		S_FreeChannel( ch );
		return;
	}

	pitch *= (sys_timescale.value + 1) / 2;

	// never update positions if source entity is 0
	ch->staticsound = ( ent == 0 ) ? true : false;
	ch->use_loop = (flags & SND_STOP_LOOPING) ? false : true;
	ch->localsound = (flags & SND_LOCALSOUND) ? true : false;
	ch->master_vol = vol;
	ch->dist_mult = (attn / SND_CLIP_DISTANCE);
	ch->entchannel = CHAN_STATIC;
	ch->basePitch = pitch;

	SND_Spatialize( ch );
}

/*
==================
S_StartLocalSound
==================
*/
void S_StartLocalSound( const char *name, float volume, qboolean reliable )
{
	sound_t	sfxHandle;
	int	flags = (SND_LOCALSOUND|SND_STOP_LOOPING);
	int	channel = CHAN_AUTO;

	if( reliable ) channel = CHAN_STATIC;

	if( !dma.initialized ) return;
	sfxHandle = S_RegisterSound( name );
	S_StartSound( NULL, s_listener.entnum, channel, sfxHandle, volume, ATTN_NONE, PITCH_NORM, flags );
}

/*
==================
S_GetCurrentStaticSounds

grab all static sounds playing at current channel
==================
*/
int S_GetCurrentStaticSounds( soundlist_t *pout, int size )
{
	int	sounds_left = size;
	int	i;

	if( !dma.initialized )
		return 0;

	for( i = MAX_DYNAMIC_CHANNELS; i < total_channels && sounds_left; i++ )
	{
		if( channels[i].entchannel == CHAN_STATIC && channels[i].sfx && channels[i].sfx->name[0] )
		{
			if( channels[i].is_sentence && channels[i].name[0] )
				Q_strncpy( pout->name, channels[i].name, sizeof( pout->name ));
			else Q_strncpy( pout->name, channels[i].sfx->name, sizeof( pout->name ));
			pout->entnum = channels[i].entnum;
			VectorCopy( channels[i].origin, pout->origin );
			pout->volume = (float)channels[i].master_vol / 255.0f;
			pout->attenuation = channels[i].dist_mult * SND_CLIP_DISTANCE;
			pout->looping = ( channels[i].use_loop && FBitSet( channels[i].sfx->cache->flags, SOUND_LOOPED ));
			pout->pitch = channels[i].basePitch;
			pout->channel = channels[i].entchannel;
			pout->wordIndex = channels[i].word_index;
			pout->samplePos = channels[i].sample;
			pout->forcedEnd = channels[i].forced_end;

			sounds_left--;
			pout++;
		}
	}

	return ( size - sounds_left );
}

/*
==================
S_GetCurrentStaticSounds

grab all static sounds playing at current channel
==================
*/
int S_GetCurrentDynamicSounds( soundlist_t *pout, int size )
{
	int	sounds_left = size;
	int	i, looped;

	if( !dma.initialized )
		return 0;

	for( i = 0; i < MAX_CHANNELS && sounds_left; i++ )
	{
		if( !channels[i].sfx || !channels[i].sfx->name[0] || !Q_stricmp( channels[i].sfx->name, "*default" ))
			continue;	// don't serialize default sounds

		looped = ( channels[i].use_loop && FBitSet( channels[i].sfx->cache->flags, SOUND_LOOPED ));

		if( channels[i].entchannel == CHAN_STATIC && looped && !Host_IsQuakeCompatible())
			continue;	// never serialize static looped sounds. It will be restoring in game code

		if( channels[i].is_sentence && channels[i].name[0] )
			Q_strncpy( pout->name, channels[i].name, sizeof( pout->name ));
		else Q_strncpy( pout->name, channels[i].sfx->name, sizeof( pout->name ));
		pout->entnum = (channels[i].entnum < 0) ? 0 : channels[i].entnum;
		VectorCopy( channels[i].origin, pout->origin );
		pout->volume = (float)channels[i].master_vol / 255.0f;
		pout->attenuation = channels[i].dist_mult * SND_CLIP_DISTANCE;
		pout->pitch = channels[i].basePitch;
		pout->channel = channels[i].entchannel;
		pout->wordIndex = channels[i].word_index;
		pout->samplePos = channels[i].sample;
		pout->forcedEnd = channels[i].forced_end;
		pout->looping = looped;

		sounds_left--;
		pout++;
	}

	return ( size - sounds_left );
}

/*
===================
S_InitAmbientChannels
===================
*/
static void S_InitAmbientChannels( void )
{
	int	ambient_channel;
	channel_t	*chan;

	for( ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++ )
	{
		chan = &channels[ambient_channel];

		chan->staticsound = true;
		chan->use_loop = true;
		chan->entchannel = CHAN_STATIC;
		chan->dist_mult = (ATTN_NONE / SND_CLIP_DISTANCE);
		chan->basePitch = PITCH_NORM;
	}
}

/*
===================
S_UpdateAmbientSounds
===================
*/
static void S_UpdateAmbientSounds( void )
{
	int ambient_channel;

	if( !snd_ambient )
		return;

	// calc ambient sound levels
	if( !cl.worldmodel )
		return;

	mleaf_t	*leaf = Mod_PointInLeaf( s_listener.origin, cl.worldmodel->nodes, cl.worldmodel );

	if( !leaf || !s_ambient_level.value )
	{
		for( ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++ )
			channels[ambient_channel].sfx = NULL;

		return;
	}

	for( ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++ )
	{
		channel_t *chan = &channels[ambient_channel];
		chan->sfx = S_GetSfxByHandle( ambient_sfx[ambient_channel] );

		// ambient is unused
		if( !chan->sfx )
		{
			chan->rightvol = 0;
			chan->leftvol = 0;
			continue;
		}

		float vol = s_ambient_level.value * leaf->ambient_sound_level[ambient_channel];
		if( vol < 0.0f )
			vol = 0.0f;

		// don't adjust volume too fast
		if( chan->master_vol < vol )
		{
			chan->master_vol += round( cl_clientframetime() * s_ambient_fade.value );
			if( chan->master_vol > vol )
				chan->master_vol = vol;
		}
		else if( chan->master_vol > vol )
		{
			chan->master_vol -= round( cl_clientframetime() * s_ambient_fade.value );
			if( chan->master_vol < vol )
				chan->master_vol = vol;
		}

		chan->leftvol = chan->rightvol = chan->master_vol;
	}
}

/*
=============================================================================

		SOUND STREAM RAW SAMPLES

=============================================================================
*/
/*
===================
S_FindRawChannel
===================
*/
rawchan_t *S_FindRawChannel( int entnum, qboolean create )
{
	rawchan_t *ch;

	if( !sndpool )
		return NULL; // sound is not active

	if( !entnum )
		return NULL; // world is unused

	// check for replacement sound, or find the best one to replace
	int best_time = 0x7fffffff;
	int best = -1;
	int free = -1;

	for( int i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		ch = raw_channels[i];

		if( free < 0 && !ch )
		{
			free = i;
		}
		else if( ch )
		{
			int	time;

			// exact match
			if( ch->entnum == entnum )
				return ch;

			time = ch->s_rawend - paintedtime;
			if( time < best_time )
			{
				best = i;
				best_time = time;
			}
		}
	}

	if( !create )
		return NULL;

	if( free >= 0 )
		best = free;

	if( best < 0 )
		return NULL; // no free slots

	if( !raw_channels[best] )
	{
		size_t raw_samples = MAX_RAW_SAMPLES;
		raw_channels[best] = Mem_Calloc( sndpool, sizeof( *ch ) + sizeof( portable_samplepair_t ) * raw_samples );
		raw_channels[best]->max_samples = raw_samples;
	}

	ch = raw_channels[best];
	ch->entnum = entnum;
	ch->s_rawend = 0;

	return ch;
}

/*
===================
S_RawSamplesStereo
===================
*/
uint S_RawSamplesStereo( portable_samplepair_t *rawsamples, uint rawend, uint max_samples, uint samples, uint rate, word width, word channels, const byte *data )
{
	uint	fracstep, samplefrac;
	uint	src, dst;

	if( rawend < paintedtime )
		rawend = paintedtime;

	fracstep = ((double) rate / (double)SOUND_DMA_SPEED) * (double)(1 << S_RAW_SAMPLES_PRECISION_BITS);
	samplefrac = 0;

	if( width == 2 )
	{
		const short *in = (const short *)data;

		if( channels == 2 )
		{
			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = in[src*2+0];
				rawsamples[dst].right = in[src*2+1];
			}
		}
		else
		{
			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = in[src];
				rawsamples[dst].right = in[src];
			}
		}
	}
	else
	{
		if( channels == 2 )
		{
			const char *in = (const char *)data;

			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = in[src*2+0] << 8;
				rawsamples[dst].right = in[src*2+1] << 8;
			}
		}
		else
		{
			for( src = 0; src < samples; samplefrac += fracstep, src = ( samplefrac >> S_RAW_SAMPLES_PRECISION_BITS ))
			{
				dst = rawend++ & ( max_samples - 1 );
				rawsamples[dst].left = ( data[src] - 128 ) << 8;
				rawsamples[dst].right = ( data[src] - 128 ) << 8;
			}
		}
	}

	return rawend;
}

/*
===================
S_RawEntSamples
===================
*/
void S_RawEntSamples( int entnum, uint samples, uint rate, word width, word channels, const byte *data, int snd_vol )
{
	rawchan_t	*ch;

	if( snd_vol < 0 )
		snd_vol = 0;

	if( !( ch = S_FindRawChannel( entnum, true )))
		return;

	ch->master_vol = snd_vol;
	ch->dist_mult = (ATTN_NONE / SND_CLIP_DISTANCE);
	ch->s_rawend = S_RawSamplesStereo( ch->rawsamples, ch->s_rawend, ch->max_samples, samples, rate, width, channels, data );
	ch->leftvol = ch->rightvol = snd_vol;
}

/*
===================
S_FreeIdleRawChannels

Free raw channel that have been idling for too long.
===================
*/
static void S_FreeIdleRawChannels( void )
{
	int	i;

	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		rawchan_t	*ch = raw_channels[i];

		if( !ch )
			continue;

		if( ch->s_rawend >= paintedtime )
			continue;

		if( ch->entnum > 0 )
		{
			SND_ForceCloseMouth( ch->entnum );

			if( CL_IsPlayerIndex( ch->entnum ))
				Voice_StopChannel( ch->entnum );
		}

		if(( paintedtime - ch->s_rawend ) / SOUND_DMA_SPEED >= S_RAW_SOUND_IDLE_SEC )
		{
			raw_channels[i] = NULL;
			Mem_Free( ch );
		}
	}
}

/*
===================
S_ClearRawChannels
===================
*/
static void S_ClearRawChannels( void )
{
	int	i;

	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		rawchan_t	*ch = raw_channels[i];

		if( !ch ) continue;
		ch->s_rawend = 0;
		ch->oldtime = -1;
	}
}

/*
===================
S_SpatializeRawChannels
===================
*/
static void S_SpatializeRawChannels( void )
{
	for( int i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		rawchan_t *ch = raw_channels[i];

		if( !ch )
			continue;

		if( ch->s_rawend < paintedtime )
		{
			ch->leftvol = ch->rightvol = 0;
			continue;
		}

		// spatialization
		if( !S_IsClient( ch->entnum ) && ch->dist_mult && ch->entnum >= 0 && ch->entnum < GI->max_edicts )
		{
			if( !CL_GetMovieSpatialization( ch ))
			{
				// origin is null and entity not exist on client
				ch->leftvol = ch->rightvol = 0;
			}
			else
			{
				vec3_t	source_vec;

				VectorSubtract( ch->origin, s_listener.origin, source_vec );

				// normalize source_vec and get distance from listener to source
				float dist = VectorNormalizeLength( source_vec );
				float dot = DotProduct( s_listener.right, source_vec );

				// don't pan sounds with no attenuation
				if( ch->dist_mult <= 0.0f ) dot = 0.0f;

				// fill out channel volumes for single location
				S_SpatializeChannel( &ch->leftvol, &ch->rightvol, ch->master_vol, dot, dist * ch->dist_mult );
			}
		}
		else
		{
			ch->leftvol = ch->rightvol = ch->master_vol;
		}
	}
}

/*
===================
S_FreeRawChannels
===================
*/
static void S_FreeRawChannels( void )
{
	int	i;

	// free raw samples
	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		if( raw_channels[i] )
			Mem_Free( raw_channels[i] );
	}

	memset( raw_channels, 0, sizeof( raw_channels ));
}

//=============================================================================

/*
==================
S_ClearBuffer
==================
*/
static void S_ClearBuffer( void )
{
	S_ClearRawChannels();

	SNDDMA_BeginPainting ();
	if( dma.buffer )
		memset( dma.buffer, 0, dma.samples * 2 );
	SNDDMA_Submit ();

	S_ClearBuffers( PAINTBUFFER_SIZE );
}

/*
==================
S_StopSound

stop all sounds for entity on a channel.
==================
*/
void GAME_EXPORT S_StopSound( int entnum, int channel, const char *soundname )
{
	sfx_t	*sfx;

	if( !dma.initialized ) return;
	sfx = S_FindName( soundname, NULL );
	S_AlterChannel( entnum, channel, sfx, 0, 0, SND_STOP );
}

/*
==================
S_StopAllSounds
==================
*/
void S_StopAllSounds( qboolean ambient )
{
	int	i;

	if( !dma.initialized ) return;
	total_channels = MAX_DYNAMIC_CHANNELS;	// no statics

	for( i = 0; i < MAX_CHANNELS; i++ )
	{
		if( !channels[i].sfx ) continue;
		S_FreeChannel( &channels[i] );
	}

	SX_ClearState();

	// clear all the channels
	memset( channels, 0, sizeof( channels ));

	// restart the ambient sounds
	if( ambient ) S_InitAmbientChannels ();

	S_ClearBuffer ();

	// clear any remaining soundfade
	memset( &soundfade, 0, sizeof( soundfade ));
}

/*
==============
S_GetSoundtime

update global soundtime

(was part of platform code)
===============
*/
static int S_GetSoundtime( void )
{
	static int buffers, oldsamplepos;
	int samplepos, fullsamples;

	fullsamples = dma.samples / 2;

	// it is possible to miscount buffers
	// if it has wrapped twice between
	// calls to S_Update.  Oh well.
	samplepos = dma.samplepos;

	if( samplepos < oldsamplepos )
	{
		buffers++; // buffer wrapped

		if( paintedtime > 0x40000000 )
		{
			// time to chop things off to avoid 32 bit limits
			buffers     = 0;
			paintedtime = fullsamples;
			S_StopAllSounds( true );
		}
	}

	oldsamplepos = samplepos;

	return ( buffers * fullsamples + samplepos / 2 );
}

//=============================================================================
static void S_UpdateChannels( void )
{
	uint	endtime;
	int	samps;

	SNDDMA_BeginPainting();

	if( !dma.buffer ) return;

	// updates DMA time
	soundtime = S_GetSoundtime();

	// soundtime - total samples that have been played out to hardware at dmaspeed
	// paintedtime - total samples that have been mixed at speed
	// endtime - target for samples in mixahead buffer at speed
	endtime = soundtime + s_mixahead.value * SOUND_DMA_SPEED;
	samps = dma.samples >> 1;

	if((int)(endtime - soundtime) > samps )
		endtime = soundtime + samps;

	if(( endtime - paintedtime ) & 0x3 )
	{
		// the difference between endtime and painted time should align on
		// boundaries of 4 samples. this is important when upsampling from 11khz -> 44khz.
		endtime -= ( endtime - paintedtime ) & 0x3;
	}

	S_PaintChannels( endtime );

	SNDDMA_Submit();
}

/*
=================
S_ExtraUpdate

Don't let sound skip if going slow
=================
*/
void S_ExtraUpdate( void )
{
	if( !dma.initialized ) return;
	S_UpdateChannels ();
}

/*
============
S_UpdateFrame

update listener position
============
*/
void S_UpdateFrame( struct ref_viewpass_s *rvp )
{
	if( !FBitSet( rvp->flags, RF_DRAW_WORLD ) || FBitSet( rvp->flags, RF_ONLY_CLIENTDRAW ))
		return;

	VectorCopy( rvp->vieworigin, s_listener.origin );
	AngleVectors( rvp->viewangles, s_listener.forward, s_listener.right, s_listener.up );
	s_listener.entnum = rvp->viewentity; // can be camera entity too
}

/*
============
SND_UpdateSound

Called once each time through the main loop
============
*/
void SND_UpdateSound( void )
{
	int		i, j, total;
	channel_t		*ch, *combine;
	con_nprint_t	info;

	if( !dma.initialized ) return;

	// if the loading plaque is up, clear everything
	// out to make sure we aren't looping a dirty
	// dma buffer while loading
	// update any client side sound fade
	S_UpdateSoundFade();

	// release raw-channels that no longer used more than 10 secs
	S_FreeIdleRawChannels();

	// update general area ambient sound sources
	S_UpdateAmbientSounds();

	combine = NULL;

	// update spatialization for static and dynamic sounds
	for( i = NUM_AMBIENTS, ch = channels + NUM_AMBIENTS; i < total_channels; i++, ch++ )
	{
		if( !ch->sfx ) continue;
		SND_Spatialize( ch ); // respatialize channel

		if( !ch->leftvol && !ch->rightvol )
			continue;

		// try to combine static sounds with a previous channel of the same
		// sound effect so we don't mix five torches every frame
		// g-cont: perfomance option, probably kill stereo effect in most cases
		if( i >= MAX_DYNAMIC_CHANNELS && s_combine_sounds.value )
		{
			// see if it can just use the last one
			if( combine && combine->sfx == ch->sfx )
			{
				combine->leftvol += ch->leftvol;
				combine->rightvol += ch->rightvol;
				ch->leftvol = ch->rightvol = 0;
				continue;
			}

			// search for one
			combine = channels + MAX_DYNAMIC_CHANNELS;

			for( j = MAX_DYNAMIC_CHANNELS; j < i; j++, combine++ )
			{
				if( combine->sfx == ch->sfx )
					break;
			}

			if( j == total_channels )
			{
				combine = NULL;
			}
			else
			{
				if( combine != ch )
				{
					combine->leftvol += ch->leftvol;
					combine->rightvol += ch->rightvol;
					ch->leftvol = ch->rightvol = 0;
				}
				continue;
			}
		}
	}

	S_SpatializeRawChannels();

	// debugging output
	if( s_show.value != 0.0f )
	{
		info.color[0] = 1.0f;
		info.color[1] = 0.6f;
		info.color[2] = 0.0f;
		info.time_to_live = 0.5f;

		for( i = 0, total = 1, ch = channels; i < MAX_CHANNELS; i++, ch++ )
		{
			if( ch->sfx && ( ch->leftvol || ch->rightvol ))
			{
				info.index = total;
				Con_NXPrintf( &info, "chan %i, pos (%.f %.f %.f) ent %i, lv%3i rv%3i %s\n",
				i, ch->origin[0], ch->origin[1], ch->origin[2], ch->entnum, ch->leftvol, ch->rightvol, ch->sfx->name );
				total++;
			}
		}

		VectorSet( info.color, 1.0f, 1.0f, 1.0f );
		info.index = 0;

		Con_NXPrintf( &info, "room_type: %i (%s) ----(%i)---- painted: %i\n", idsp_room, Cvar_VariableString( "dsp_coeff_table" ), total - 1, paintedtime );
	}

	S_StreamBackgroundTrack ();

	// mix some sound
	S_UpdateChannels ();
}

/*
===============================================================================

console functions

===============================================================================
*/
static void S_Play_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "play <soundfile>\n" );
		return;
	}

	S_StartLocalSound( Cmd_Argv( 1 ), VOL_NORM, false );
}

static void S_Play2_f( void )
{
	int	i = 1;

	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "play2 <soundfile>\n" );
		return;
	}

	while( i < Cmd_Argc( ))
	{
		S_StartLocalSound( Cmd_Argv( i ), VOL_NORM, true );
		i++;
	}
}

static void S_PlayVol_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "playvol <soundfile volume>\n" );
		return;
	}

	S_StartLocalSound( Cmd_Argv( 1 ), Q_atof( Cmd_Argv( 2 )), false );
}

static void S_Say( const char *name, qboolean reliable )
{
	char sentence[1024];

	// predefined vox sentence
	if( name[0] == '!' )
	{
		S_StartLocalSound( name, 1.0f, reliable );
		return;
	}

	Q_snprintf( sentence, sizeof( sentence ), "!#%s", name );
	S_StartLocalSound( sentence, 1.0f, reliable );
}

static void S_Say_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "speak <vox sentence>\n" );
		return;
	}

	S_Say( Cmd_Argv( 1 ), false );
}

static void S_SayReliable_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( S_USAGE "spk <vox sentence>\n" );
		return;
	}

	S_Say( Cmd_Argv( 1 ), true );
}

/*
=================
S_Music_f
=================
*/
static void S_Music_f( void )
{
	int	c = Cmd_Argc();

	// run background track
	if( c == 1 )
	{
		// blank name stopped last track
		S_StopBackgroundTrack();
	}
	else if( c == 2 )
	{
		string	intro, main, track;
		const char	*ext[] = { "mp3", "wav" };
		int	i;

		Q_strncpy( track, Cmd_Argv( 1 ), sizeof( track ));
		Q_snprintf( intro, sizeof( intro ), "%s_intro", Cmd_Argv( 1 ));
		Q_snprintf( main, sizeof( main ), "%s_main", Cmd_Argv( 1 ));

		for( i = 0; i < 2; i++ )
		{
			char intro_path[MAX_VA_STRING];
			char main_path[MAX_VA_STRING];
			char track_path[MAX_VA_STRING];

			Q_snprintf( intro_path, sizeof( intro_path ), "media/%s.%s", intro, ext[i] );
			Q_snprintf( main_path, sizeof( main_path ), "media/%s.%s", main, ext[i] );

			if( FS_FileExists( intro_path, false ) && FS_FileExists( main_path, false ))
			{
				// combined track with introduction and main loop theme
				S_StartBackgroundTrack( intro, main, 0, false );
				break;
			}

			Q_snprintf( track_path, sizeof( track_path ), "media/%s.%s", track, ext[i] );

			if( FS_FileExists( track_path, false ))
			{
				// single non-looped theme
				S_StartBackgroundTrack( track, NULL, 0, false );
				break;
			}
		}

	}
	else if( c == 3 )
	{
		S_StartBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 2 ), 0, false );
	}
	else if( c == 4 && Q_atoi( Cmd_Argv( 3 )) != 0 )
	{
		// restore command for singleplayer: all arguments are valid
		S_StartBackgroundTrack( Cmd_Argv( 1 ), Cmd_Argv( 2 ), Q_atoi( Cmd_Argv( 3 )), false );
	}
	else Con_Printf( S_USAGE "music <musicfile> [loopfile]\n" );
}

/*
=================
S_StopSound_f
=================
*/
static void S_StopSound_f( void )
{
	S_StopAllSounds( true );
}

/*
=================
S_SoundFade_f
=================
*/
static void S_Fade_f( void )
{
	int hold_time = 5;

	if( Cmd_Argc() == 2 )
	{
		hold_time = Q_atof( Cmd_Argv( 1 )); // was float
		hold_time = bound( 1, hold_time, 60 );
	}

	S_SoundFade( 100, 1, hold_time, 0 );
	snd_fade_sequence = true;
}

/*
=================
S_SoundFade_f
=================
*/
static void S_SoundFade_f( void )
{
	int c = Cmd_Argc();
	int fade_percent;
	int hold_time;
	int fade_out_seconds;
	int fade_in_seconds;

	if( c != 3 && c != 5 )
	{
		Con_Printf( S_USAGE "soundfade: <percent> <hold> [out] [in]\n" );
		return;
	}

	fade_percent = Q_atoi( Cmd_Argv( 1 ));
	fade_percent = bound( 0, fade_percent, 100 );

	hold_time = Q_atoi( Cmd_Argv( 2 ));
	hold_time = Q_min( hold_time, 255 );

	if( c == 5 )
	{
		fade_out_seconds = Q_atoi( Cmd_Argv( 3 ));
		fade_out_seconds = Q_min( fade_out_seconds, 255 );

		fade_in_seconds = Q_atoi( Cmd_Argv( 4 ));
		fade_in_seconds = Q_min( fade_in_seconds, 255 );
	}

	S_SoundFade( fade_percent, hold_time, fade_out_seconds, fade_in_seconds );
	snd_fade_sequence = true;
}

/*
=================
S_SoundInfo_f
=================
*/
void S_SoundInfo_f( void )
{
	Con_Printf( "Audio backend: %s\n", dma.backendName );
	Con_Printf( "%5d channel(s)\n", 2 );
	Con_Printf( "%5d samples\n", dma.samples );
	Con_Printf( "%5d bits/sample\n", 16 );
	Con_Printf( "%5d bytes/sec\n", SOUND_DMA_SPEED );
	Con_Printf( "%5d total_channels\n", total_channels );

	S_PrintBackgroundTrackState ();
}

/*
=================
S_VoiceRecordStart_f
=================
*/
static void S_VoiceRecordStart_f( void )
{
	if( cls.state != ca_active )
		return;

	Voice_RecordStart();
}

/*
=================
S_VoiceRecordStop_f
=================
*/
static void S_VoiceRecordStop_f( void )
{
	if( cls.state != ca_active || !Voice_IsRecording() )
		return;

	CL_AddVoiceToDatagram();
	Voice_RecordStop();
}

/*
================
S_Init
================
*/
qboolean S_Init( void )
{
	Cvar_RegisterVariable( &s_volume );
	Cvar_RegisterVariable( &s_musicvolume );
	Cvar_RegisterVariable( &s_mixahead );
	Cvar_RegisterVariable( &s_show );
	Cvar_RegisterVariable( &s_lerping );
	Cvar_RegisterVariable( &s_ambient_level );
	Cvar_RegisterVariable( &s_ambient_fade );
	Cvar_RegisterVariable( &s_combine_sounds );
	Cvar_RegisterVariable( &snd_mute_losefocus );
	Cvar_RegisterVariable( &s_test );
	Cvar_RegisterVariable( &s_samplecount );
	Cvar_RegisterVariable( &s_warn_late_precache );

	if( Sys_CheckParm( "-nosound" ))
	{
		Con_Printf( "Audio: Disabled\n" );
		return false;
	}

	Cmd_AddCommand( "play", S_Play_f, "playing a specified sound file" );
	Cmd_AddCommand( "play2", S_Play2_f, "playing a group of specified sound files" ); // nehahra stuff
	Cmd_AddCommand( "playvol", S_PlayVol_f, "playing a specified sound file with specified volume" );
	Cmd_AddCommand( "stopsound", S_StopSound_f, "stop all sounds" );
	// HLU SDK have command with the same name
	Cmd_AddCommandWithFlags( "music", S_Music_f, "starting a background track", CMD_OVERRIDABLE );
	Cmd_AddCommand( "soundlist", S_SoundList_f, "display loaded sounds" );
	Cmd_AddCommand( "s_info", S_SoundInfo_f, "print sound system information" );
	Cmd_AddCommand( "s_fade", S_Fade_f, "fade all sounds then stop all" );
	Cmd_AddCommand( "soundfade", S_SoundFade_f, "fade all sounds then stop all (goldsrc compatible)" );
	Cmd_AddCommand( "+voicerecord", S_VoiceRecordStart_f, "start voice recording" );
	Cmd_AddCommand( "-voicerecord", S_VoiceRecordStop_f, "stop voice recording" );
	Cmd_AddCommand( "spk", S_SayReliable_f, "reliable play a specified sententce" );
	Cmd_AddCommand( "speak", S_Say_f, "playing a specified sententce" );

	sndpool = Mem_AllocPool( "Sound Zone" );
	dma.backendName = "None";
	if( !SNDDMA_Init( ))
	{
		Con_Printf( "Audio: sound system can't be initialized\n" );
		Mem_FreePool( &sndpool );
		return false;
	}

	soundtime = 0;
	paintedtime = 0;

	// clear ambient sounds
	memset( ambient_sfx, 0, sizeof( ambient_sfx ));

	SX_Init ();
	S_StopAllSounds ( true );
	S_InitSounds ();
	VOX_Init ();

	return true;
}

// =======================================================================
// Shutdown sound engine
// =======================================================================
void S_Shutdown( void )
{
	if( !dma.initialized ) return;

	Cmd_RemoveCommand( "play" );
	Cmd_RemoveCommand( "playvol" );
	Cmd_RemoveCommand( "stopsound" );
	if( Cmd_Exists( "music" ))
		Cmd_RemoveCommand( "music" );
	Cmd_RemoveCommand( "soundlist" );
	Cmd_RemoveCommand( "s_info" );
	Cmd_RemoveCommand( "s_fade" );
	Cmd_RemoveCommand( "soundfade" );
	Cmd_RemoveCommand( "+voicerecord" );
	Cmd_RemoveCommand( "-voicerecord" );
	Cmd_RemoveCommand( "speak" );
	Cmd_RemoveCommand( "spk" );

	S_StopAllSounds (false);
	S_FreeRawChannels ();
	S_FreeSounds ();
	VOX_Shutdown ();
	SX_Free ();

	SNDDMA_Shutdown ();
	Mem_FreePool( &sndpool );
}
