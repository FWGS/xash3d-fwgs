/*
s_mix.c - portable code to mix sounds
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

static portable_samplepair_t roombuffer[(PAINTBUFFER_SIZE+1)], paintbuffer[(PAINTBUFFER_SIZE+1)];

#define S_MakeMixMono( x ) \
	static void S_MixMono ## x( portable_samplepair_t *pbuf, const int *volume, const void *buf, int num_samples ) \
	{ \
		const int##x##_t *data = buf; \
		for( int i = 0; i < num_samples; i++ ) \
		{ \
			pbuf[i].left  += ( data[i] * volume[0] ) >> ( x - 8 ); \
			pbuf[i].right += ( data[i] * volume[1] ) >> ( x - 8 ); \
		} \
	} \

#define S_MakeMixStereo( x ) \
	static void S_MixStereo ## x( portable_samplepair_t *pbuf, const int *volume, const void *buf, int num_samples ) \
	{ \
		const int##x##_t *data = buf; \
		for( int i = 0; i < num_samples; i++ ) \
		{ \
			pbuf[i].left  += ( data[i * 2 + 0] * volume[0] ) >> ( x - 8 ); \
			pbuf[i].right += ( data[i * 2 + 1] * volume[1] ) >> ( x - 8 ); \
		} \
	} \

#define S_MakeMixMonoPitch( x ) \
	static void S_MixMonoPitch ## x( portable_samplepair_t *pbuf, const int *volume, const void *buf, double offset_frac, double rate_scale, int num_samples ) \
	{ \
		const int##x##_t *data = buf; \
		uint sample_idx = 0; \
		for( int i = 0; i < num_samples; i++ ) \
		{ \
			pbuf[i].left  += ( data[sample_idx] * volume[0] ) >> ( x - 8 ); \
			pbuf[i].right += ( data[sample_idx] * volume[1] ) >> ( x - 8 ); \
			offset_frac += rate_scale; \
			sample_idx += (uint)offset_frac; \
			offset_frac -= (uint)offset_frac; \
		} \
	} \

#define S_MakeMixStereoPitch( x ) \
	static void S_MixStereoPitch ## x( portable_samplepair_t *pbuf, const int *volume, const void *buf, double offset_frac, double rate_scale, int num_samples ) \
	{ \
		const int##x##_t *data = buf; \
		uint sample_idx = 0; \
		for( int i = 0; i < num_samples; i++ ) \
		{ \
			pbuf[i].left  += ( data[sample_idx+0] * volume[0] ) >> ( x - 8 ); \
			pbuf[i].right += ( data[sample_idx+1] * volume[1] ) >> ( x - 8 ); \
			offset_frac += rate_scale; \
			sample_idx += (uint)offset_frac << 1; \
			offset_frac -= (uint)offset_frac; \
		} \
	} \

S_MakeMixMono( 8 )
S_MakeMixMono( 16 )
S_MakeMixStereo( 8 )
S_MakeMixStereo( 16 )
S_MakeMixMonoPitch( 8 )
S_MakeMixMonoPitch( 16 )
S_MakeMixStereoPitch( 8 )
S_MakeMixStereoPitch( 16 )

static void S_MixAudio( portable_samplepair_t *pbuf, const int *pvol, const void *buf, int channels, int width, double offset_frac, double rate_scale, int num_samples )
{
	if( Q_equal( rate_scale, 1.0 ))
	{
		if( channels == 1 )
		{
			if( width == 1 )
				S_MixMono8( pbuf, pvol, buf, num_samples );
			else
				S_MixMono16( pbuf, pvol, buf, num_samples );
		}
		else
		{
			if( width == 1 )
				S_MixStereo8( pbuf, pvol, buf, num_samples );
			else
				S_MixStereo16( pbuf, pvol, buf, num_samples );
		}
	}
	else
	{
		if( channels == 1 )
		{
			if( width == 1 )
				S_MixMonoPitch8( pbuf, pvol, buf, offset_frac, rate_scale, num_samples );
			else
				S_MixMonoPitch16( pbuf, pvol, buf, offset_frac, rate_scale, num_samples );
		}
		else
		{
			if( width == 1 )
				S_MixStereoPitch8( pbuf, pvol, buf, offset_frac, rate_scale, num_samples );
			else
				S_MixStereoPitch16( pbuf, pvol, buf, offset_frac, rate_scale, num_samples );
		}
	}
}

static int S_AdjustNumSamples( channel_t *chan, int num_samples, double rate, double timecompress_rate )
{
	if( chan->finished )
		return 0;

	// if channel is set to end at specific sample,
	// detect if it's the last mixing pass and truncate
	if( chan->forced_end )
	{
		// calculate the last sample position
		double end_sample = chan->sample + rate * num_samples * timecompress_rate;

		if( end_sample >= chan->forced_end )
		{
			chan->finished = true;
			return floor(( chan->forced_end - chan->sample ) / ( rate * timecompress_rate ));
		}
	}

	return num_samples;
}

static int S_MixChannelToBuffer( portable_samplepair_t *pbuf, channel_t *chan, int num_samples, int out_rate, double pitch, int offset, int timecompress )
{
	const int initial_offset = offset;
	const int pvol[2] =
	{
		bound( 0, chan->leftvol, 255 ),
		bound( 0, chan->rightvol, 255 ),
	};
	double rate = pitch * chan->sfx->cache->rate / (double)out_rate;

	// timecompress at 100% is skipping the entire sfx, so mark as finished and exit
	if( timecompress >= 100 )
	{
		chan->finished = true;
		return 0;
	}

	double timecompress_rate = 1 / ( 1 - timecompress / 100.0 );

	num_samples = S_AdjustNumSamples( chan, num_samples, rate, timecompress_rate );
	if( num_samples == 0 )
		return 0;

	while( num_samples > 0 )
	{
		// calculate the last sample position
		double end_sample = chan->sample + rate * num_samples * timecompress_rate;

		// and get total amount of samples we want
		int	request_num_samples = (int)(ceil( end_sample ) - floor( chan->sample ));

		// get sample pointer and also amount of samples available
		const void *audio = NULL;
		int available = S_RetrieveAudioSamples( chan->sfx->cache, &audio, chan->sample, request_num_samples, chan->use_loop );

		// no samples available, exit
		if( !available )
			break;

		double sample_frac = chan->sample - floor( chan->sample );

		// this is how much data we output
		int out_count = num_samples;
		if( request_num_samples > available ) // but we can't write more than we have
			out_count = (int)ceil(( available - sample_frac ) / ( rate ));

		const wavdata_t *wav = chan->sfx->cache;
		S_MixAudio( pbuf + offset, pvol, audio, wav->channels, wav->width, sample_frac, rate, out_count );

		chan->sample += out_count * rate * timecompress_rate;
		offset += out_count;
		num_samples -= out_count;
	}

	// samples couldn't be retrieved, mark as finished
	if( num_samples > 0 )
		chan->finished = true;

	// total amount of samples mixed
	return offset - initial_offset;
}

static int VOX_MixChannelToBuffer( portable_samplepair_t *pbuf, channel_t *chan, int num_samples, int out_rate, double pitch )
{
	int	offset = 0;

	if( chan->sentence_finished )
		return 0;

	while( num_samples > 0 && !chan->sentence_finished )
	{
		int	outputCount = S_MixChannelToBuffer( pbuf, chan, num_samples, out_rate, pitch, offset, chan->words[chan->word_index].timecompress );

		offset += outputCount;
		num_samples -= outputCount;

		// if we finished load a next word
		if( chan->finished )
		{
			VOX_FreeWord( chan );
			chan->word_index++;
			VOX_LoadWord( chan );

			if( !chan->sentence_finished )
				chan->sfx = chan->words[chan->word_index].sfx;
		}
	}

	return offset;
}

static int S_MixNormalChannels( portable_samplepair_t *dst, int end, int rate )
{
	const qboolean local = Host_IsLocalGame();
	const qboolean ingame = CL_IsInGame();
	const int num_samples = ( end - paintedtime ) / ( SOUND_DMA_SPEED / rate );

	// FWGS feature: make everybody sound like chipmunks when we're going fast
	const float pitch_mult = ( sys_timescale.value + 1 ) / 2;

	int num_mixed_channels = 0;

	if( num_samples <= 0 )
		return num_mixed_channels;

	if( cl.background && cls.key_dest == key_console )
		return num_mixed_channels; // no sounds in console with background map

	for( int i = 0; i < total_channels; i++ )
	{
		channel_t *ch = &channels[i];

		if( !ch->sfx )
			continue;

		if( !cl.background )
		{
			if( cls.key_dest == key_console && ch->localsound )
			{
				// play, playvol
			}
			else if(( cls.key_dest == key_menu || cl.paused ) && !ch->localsound && local )
			{
				// play only local sounds, keep pause for other
				continue;
			}
			else if( cls.key_dest != key_menu && !ingame && !ch->staticsound )
			{
				// play only ambient sounds, keep pause for other
				continue;
			}
		}

		wavdata_t *sc = S_LoadSound( ch->sfx );

		if( !sc )
		{
			S_FreeChannel( ch );
			continue;
		}

		// if the sound is unaudible, skip it
		// if it's also not looping, free it
		if( ch->leftvol < 8 && ch->rightvol < 8 )
		{
			if( !FBitSet( sc->flags, SOUND_LOOPED ) || !ch->use_loop )
				S_FreeChannel( ch );

			continue;
		}

		if( rate != sc->rate )
			continue;

		if( ch->entchannel == CHAN_VOICE || ch->entchannel == CHAN_STREAM )
		{
			cl_entity_t *ent = CL_GetEntityByIndex( ch->entnum );

			if( ent != NULL )
			{
				if( sc->width == 1 )
					SND_MoveMouth8( &ent->mouth, ch->sample, sc, num_samples, ch->use_loop );
				else
					SND_MoveMouth16( &ent->mouth, ch->sample, sc, num_samples, ch->use_loop );
			}
		}

		double pitch = VOX_ModifyPitch( ch, ch->basePitch * 0.01 ) * pitch_mult;

		num_mixed_channels++;

		if( ch->is_sentence )
		{
			VOX_MixChannelToBuffer( dst, ch, num_samples, rate, pitch );

			if( ch->sentence_finished )
				S_FreeChannel( ch );
		}
		else
		{
			S_MixChannelToBuffer( dst, ch, num_samples, rate, pitch, 0, 0 );

			if( ch->finished )
				S_FreeChannel( ch );
		}
	}

	return num_mixed_channels;
}

static int S_AverageSample( int a, int b )
{
	return ( a >> 1 ) + ( b >> 1 ) + ((( a & 1 ) + ( b & 1 )) >> 1 );
}

static void S_UpsampleBuffer( portable_samplepair_t *dst, size_t num_samples )
{
	if( s_lerping.value )
	{
		// copy even positions and average odd
		for( size_t i = num_samples - 1; i > 0; i-- )
		{
			dst[i * 2] = dst[i];

			dst[i * 2 + 1].left = S_AverageSample( dst[i].left, dst[i - 1].left );
			dst[i * 2 + 1].right = S_AverageSample( dst[i].right, dst[i - 1].right );
		}
	}
	else
	{
		// copy into even and odd positions
		for( size_t i = num_samples - 1; i > 0; i-- )
		{
			dst[i * 2] = dst[i];
			dst[i * 2 + 1] = dst[i];
		}
	}

	dst[1] = dst[0];
}

static int S_MixNormalChannelsToRoombuffer( int end, int count )
{
	// for room buffer we only support CD rates like 11k, 22k, and 44k
	// TODO: 48k output would require support from platform-specific backends first
	// until there is no real usecase, let's keep it simple
	int num_mixed_channels = S_MixNormalChannels( roombuffer, end, SOUND_11k );

	if( dma.format.speed >= SOUND_22k )
	{
		if( num_mixed_channels > 0 )
			S_UpsampleBuffer( roombuffer, count / ( SOUND_22k / SOUND_11k ));

		num_mixed_channels += S_MixNormalChannels( roombuffer, end, SOUND_22k );
	}

	if( dma.format.speed >= SOUND_44k )
	{
		if( num_mixed_channels > 0 )
			S_UpsampleBuffer( roombuffer, count / ( SOUND_44k / SOUND_22k ));

		num_mixed_channels += S_MixNormalChannels( roombuffer, end, SOUND_44k );
	}

	return num_mixed_channels;
}

static int S_MixRawChannels( int end )
{
	int num_room_channels = 0;

	if( cl.paused )
		return 0;

	// paint in the raw channels
	for( size_t i = 0; i < ARRAYSIZE( raw_channels ); i++ )
	{
		// copy from the streaming sound source
		rawchan_t *ch = raw_channels[i];

		if( !ch )
			continue;

		// not audible
		if( !ch->leftvol && !ch->rightvol )
			continue;

		qboolean is_voice = CL_IsPlayerIndex( ch->entnum )
			|| ch->entnum == VOICE_LOOPBACK_INDEX
			|| ch->entnum == VOICE_LOCALCLIENT_INDEX;

		portable_samplepair_t *pbuf;
		if( is_voice || ch->entnum == S_RAW_SOUND_BACKGROUNDTRACK )
		{
			// for streams we don't have fancy things like volume controls
			// or DSP processing or upsampling, so paint it directly into result buffer
			pbuf = paintbuffer;
		}
		else
		{
			pbuf = roombuffer;
			num_room_channels++;
		}

		uint stop = (end < ch->s_rawend) ? end : ch->s_rawend;
		const uint mask = ch->max_samples - 1;

		for( size_t i = 0, j = paintedtime; j < stop; i++, j++ )
		{
			pbuf[i].left  += ( ch->rawsamples[j & mask].left * ch->leftvol ) >> 8;
			pbuf[i].right += ( ch->rawsamples[j & mask].right * ch->rightvol ) >> 8;
		}

		if( ch->entnum > 0 )
		{
			cl_entity_t *ent = CL_GetEntityByIndex( ch->entnum );
			int pos = paintedtime & ( ch->max_samples - 1 );
			int count = bound( 0, ch->max_samples - pos, stop - paintedtime );

			if( ent )
				SND_MoveMouthRaw( &ent->mouth, &ch->rawsamples[pos], count );
		}
	}

	return num_room_channels;
}

static void S_MixBufferWithGain( portable_samplepair_t *dst, const portable_samplepair_t *src, size_t count, int gain )
{
	if( gain == 256 )
	{
		for( size_t i = 0; i < count; i++ )
		{
			dst[i].left += src[i].left;
			dst[i].right += src[i].right;
		}
	}
	else
	{
		for( size_t i = 0; i < count; i++ )
		{
			dst[i].left += ( src[i].left * gain ) >> 8;
			dst[i].right += ( src[i].right * gain ) >> 8;
		}
	}
}

static void S_WriteLinearBlastStereo16( short *snd_out, const int *snd_p, size_t count )
{
	for( size_t i = 0; i < count; i += 2 )
	{
		snd_out[i+0] = CLIP16( snd_p[i+0] );
		snd_out[i+1] = CLIP16( snd_p[i+1] );
	}
}

static void S_TransferPaintBuffer( const portable_samplepair_t *src, int endtime )
{
	const int *snd_p = (const int *)src;
	const int sampleMask = ((dma.samples >> 1) - 1);
	int lpaintedtime = paintedtime;

	SNDDMA_BeginPainting ();

	while( lpaintedtime < endtime )
	{
		// handle recirculating buffer issues
		int lpos = lpaintedtime & sampleMask;

		short *snd_out = (short *)dma.buffer + (lpos << 1);

		int snd_linear_count = (dma.samples>>1) - lpos;
		if( lpaintedtime + snd_linear_count > endtime )
			snd_linear_count = endtime - lpaintedtime;

		snd_linear_count <<= 1;

		// write a linear blast of samples
		S_WriteLinearBlastStereo16( snd_out, snd_p, snd_linear_count );

		snd_p += snd_linear_count;
		lpaintedtime += (snd_linear_count >> 1);
	}

	SNDDMA_Submit();
}

void S_ClearBuffers( int num_samples )
{
	const size_t num_bytes = ( num_samples + 1 ) * sizeof( portable_samplepair_t );

	memset( roombuffer, 0, num_bytes );
	memset( paintbuffer, 0, num_bytes );
}

void S_PaintChannels( int endtime )
{
	int gain = S_GetMasterVolume() * 256;

	while( paintedtime < endtime )
	{
		// if paintbuffer is smaller than DMA buffer
		int end = endtime;
		if( end - paintedtime > PAINTBUFFER_SIZE )
			end = paintedtime + PAINTBUFFER_SIZE;

		const int num_samples = end - paintedtime;

		S_ClearBuffers( num_samples );

		int room_channels = S_MixNormalChannelsToRoombuffer( end, num_samples );

		room_channels += S_MixRawChannels( end );

		// now process DSP and mix result into paintbuffer
		if( room_channels > 0 )
		{
			if( cls.key_dest != key_menu )
				SX_RoomFX( roombuffer, num_samples );

			S_MixBufferWithGain( paintbuffer, roombuffer, num_samples, gain );
		}

		// transfer out according to DMA format
		S_TransferPaintBuffer( paintbuffer, end );
		paintedtime = end;
	}
}
