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

#define IPAINTBUFFER	0
#define IROOMBUFFER		1
#define ISTREAMBUFFER	2

#define FILTERTYPE_NONE	0
#define FILTERTYPE_LINEAR	1
#define FILTERTYPE_CUBIC	2

#define CCHANVOLUMES	2

#define SND_SCALE_BITS	7
#define SND_SCALE_SHIFT	(8 - SND_SCALE_BITS)
#define SND_SCALE_LEVELS	(1 << SND_SCALE_BITS)

portable_samplepair_t	*g_curpaintbuffer;
portable_samplepair_t	streambuffer[(PAINTBUFFER_SIZE+1)];
portable_samplepair_t	paintbuffer[(PAINTBUFFER_SIZE+1)];
portable_samplepair_t	roombuffer[(PAINTBUFFER_SIZE+1)];
portable_samplepair_t	facingbuffer[(PAINTBUFFER_SIZE+1)];
portable_samplepair_t	temppaintbuffer[(PAINTBUFFER_SIZE+1)];
paintbuffer_t		paintbuffers[CPAINTBUFFERS];

int			snd_scaletable[SND_SCALE_LEVELS][256];

void S_InitScaletable( void )
{
	int	i, j;

	for( i = 0; i < SND_SCALE_LEVELS; i++ )
	{
		for( j = 0; j < 256; j++ )
			snd_scaletable[i][j] = ((signed char)j) * i * (1<<SND_SCALE_SHIFT);
	}
}

/*
===================
S_TransferPaintBuffer

===================
*/
void S_TransferPaintBuffer( int endtime )
{
	int	*snd_p, snd_linear_count;
	int	lpos, lpaintedtime;
	int	i, val, sampleMask;
	short	*snd_out;
	dword	*pbuf;

	pbuf = (dword *)dma.buffer;
	snd_p = (int *)PAINTBUFFER;
	lpaintedtime = paintedtime;
	sampleMask = ((dma.samples >> 1) - 1);

	while( lpaintedtime < endtime )
	{
		// handle recirculating buffer issues
		lpos = lpaintedtime & sampleMask;

		snd_out = (short *)pbuf + (lpos << 1);

		snd_linear_count = (dma.samples>>1) - lpos;
		if( lpaintedtime + snd_linear_count > endtime )
			snd_linear_count = endtime - lpaintedtime;

		snd_linear_count <<= 1;

		// write a linear blast of samples
		for( i = 0; i < snd_linear_count; i += 2 )
		{
			val = (snd_p[i+0] * 256) >> 8;

			if( val > 0x7fff ) snd_out[i+0] = 0x7fff;
			else if( val < (short)0x8000 )
				snd_out[i+0] = (short)0x8000;
			else snd_out[i+0] = val;

			val = (snd_p[i+1] * 256) >> 8;
			if( val > 0x7fff ) snd_out[i+1] = 0x7fff;
			else if( val < (short)0x8000 )
				snd_out[i+1] = (short)0x8000;
			else snd_out[i+1] = val;
		}

		snd_p += snd_linear_count;
		lpaintedtime += (snd_linear_count >> 1);
	}
}

//===============================================================================
// Mix buffer (paintbuffer) management routines
//===============================================================================
// Activate a paintbuffer.  All active paintbuffers are mixed in parallel within 
// MIX_MixChannelsToPaintbuffer, according to flags
_inline void MIX_ActivatePaintbuffer( int ipaintbuffer )
{
	Assert( ipaintbuffer < CPAINTBUFFERS );
	paintbuffers[ipaintbuffer].factive = true;
}

// don't mix into this paintbuffer
_inline void MIX_DeactivatePaintbuffer( int ipaintbuffer )
{
	Assert( ipaintbuffer < CPAINTBUFFERS );
	paintbuffers[ipaintbuffer].factive = false;
}

_inline void MIX_SetCurrentPaintbuffer( int ipaintbuffer )
{
	Assert( ipaintbuffer < CPAINTBUFFERS );
	g_curpaintbuffer = paintbuffers[ipaintbuffer].pbuf;
	Assert( g_curpaintbuffer != NULL );
}

_inline int MIX_GetCurrentPaintbufferIndex( void )
{
	int	i;

	for( i = 0; i < CPAINTBUFFERS; i++ )
	{
		if( g_curpaintbuffer == paintbuffers[i].pbuf )
			return i;
	}
	return 0;
}

_inline paintbuffer_t *MIX_GetCurrentPaintbufferPtr( void )
{
	int	ipaint = MIX_GetCurrentPaintbufferIndex();
	
	Assert( ipaint < CPAINTBUFFERS );
	return &paintbuffers[ipaint];
}

// Don't mix into any paintbuffers
_inline void MIX_DeactivateAllPaintbuffers( void )
{
	int	i;

	for( i = 0; i < CPAINTBUFFERS; i++ )
		paintbuffers[i].factive = false;
}

// set upsampling filter indexes back to 0
_inline void MIX_ResetPaintbufferFilterCounters( void )
{
	int	i;

	for( i = 0; i < CPAINTBUFFERS; i++ )
		paintbuffers[i].ifilter = FILTERTYPE_NONE;
}

_inline void MIX_ResetPaintbufferFilterCounter( int ipaintbuffer )
{
	Assert( ipaintbuffer < CPAINTBUFFERS );
	paintbuffers[ipaintbuffer].ifilter = 0;
}

// return pointer to front paintbuffer pbuf, given index
_inline portable_samplepair_t *MIX_GetPFrontFromIPaint( int ipaintbuffer )
{
	Assert( ipaintbuffer < CPAINTBUFFERS );
	return paintbuffers[ipaintbuffer].pbuf;
}

_inline paintbuffer_t *MIX_GetPPaintFromIPaint( int ipaint )
{	
	Assert( ipaint < CPAINTBUFFERS );
	return &paintbuffers[ipaint];
}

void MIX_FreeAllPaintbuffers( void )
{
	// clear paintbuffer structs
	memset( paintbuffers, 0, CPAINTBUFFERS * sizeof( paintbuffer_t ));
}

// Initialize paintbuffers array, set current paint buffer to main output buffer IPAINTBUFFER
void MIX_InitAllPaintbuffers( void )
{
	// clear paintbuffer structs
	memset( paintbuffers, 0, CPAINTBUFFERS * sizeof( paintbuffer_t ));

	paintbuffers[IPAINTBUFFER].pbuf = paintbuffer;
	paintbuffers[IROOMBUFFER].pbuf = roombuffer;
	paintbuffers[ISTREAMBUFFER].pbuf = streambuffer;
		
	MIX_SetCurrentPaintbuffer( IPAINTBUFFER );
}

/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/
void S_PaintMonoFrom8( portable_samplepair_t *pbuf, int *volume, byte *pData, int outCount )
{
	int	*lscale, *rscale;
	int 	i, data;
		
	lscale = snd_scaletable[volume[0] >> SND_SCALE_SHIFT];
	rscale = snd_scaletable[volume[1] >> SND_SCALE_SHIFT];

	for( i = 0; i < outCount; i++ )
	{
		data = pData[i];
		pbuf[i].left += lscale[data];
		pbuf[i].right += rscale[data];
	}
}

void S_PaintStereoFrom8( portable_samplepair_t *pbuf, int *volume, byte *pData, int outCount )
{
	int	*lscale, *rscale;
	uint	left, right;
	word	*data;
	int	i;

	lscale = snd_scaletable[volume[0] >> SND_SCALE_SHIFT];
	rscale = snd_scaletable[volume[1] >> SND_SCALE_SHIFT];
	data = (word *)pData;

	for( i = 0; i < outCount; i++, data++ )
	{
		left = (byte)((*data & 0x00FF));
		right = (byte)((*data & 0xFF00) >> 8);
		pbuf[i].left += lscale[left];
		pbuf[i].right += rscale[right];
	}
}

void S_PaintMonoFrom16( portable_samplepair_t *pbuf, int *volume, short *pData, int outCount )
{
	int	left, right;
	int	i, data;

	for( i = 0; i < outCount; i++ )
	{
		data = pData[i];
		left = ( data * volume[0]) >> 8;
		right = (data * volume[1]) >> 8;
		pbuf[i].left += left;
		pbuf[i].right += right;
	}
}

void S_PaintStereoFrom16( portable_samplepair_t *pbuf, int *volume, short *pData, int outCount )
{
	uint	*data;
	int	left, right;
	int	i;

	data = (uint *)pData;
		
	for( i = 0; i < outCount; i++, data++ )
	{
		left = (signed short)((*data & 0x0000FFFF));
		right = (signed short)((*data & 0xFFFF0000) >> 16);

		left =  (left * volume[0]) >> 8;
		right = (right * volume[1]) >> 8;

		pbuf[i].left += left;
		pbuf[i].right += right;
	}
}

void S_Mix8MonoTimeCompress( portable_samplepair_t *pbuf, int *volume, byte *pData, int inputOffset, uint rateScale, int outCount, int timecompress )
{
}

void S_Mix8Mono( portable_samplepair_t *pbuf, int *volume, byte *pData, int inputOffset, uint rateScale, int outCount, int timecompress )
{
	int	i, sampleIndex = 0;
	uint	sampleFrac = inputOffset;
	int	*lscale, *rscale;

	if( timecompress != 0 )
	{
		S_Mix8MonoTimeCompress( pbuf, volume, pData, inputOffset, rateScale, outCount, timecompress );
//		return;
	}

	// Not using pitch shift?
	if( rateScale == FIX( 1 ))
	{
		S_PaintMonoFrom8( pbuf, volume, pData, outCount );
		return;
	}

	lscale = snd_scaletable[volume[0] >> SND_SCALE_SHIFT];
	rscale = snd_scaletable[volume[1] >> SND_SCALE_SHIFT];

	for( i = 0; i < outCount; i++ )
	{
		pbuf[i].left += lscale[pData[sampleIndex]];
		pbuf[i].right += rscale[pData[sampleIndex]];
		sampleFrac += rateScale;
		sampleIndex += FIX_INTPART( sampleFrac );
		sampleFrac = FIX_FRACPART( sampleFrac );
	}
}

void S_Mix8Stereo( portable_samplepair_t *pbuf, int *volume, byte *pData, int inputOffset, uint rateScale, int outCount )
{
	int	i, sampleIndex = 0;
	uint	sampleFrac = inputOffset;
	int	*lscale, *rscale;

	// Not using pitch shift?
	if( rateScale == FIX( 1 ))
	{
		S_PaintStereoFrom8( pbuf, volume, pData, outCount );
		return;
	}

	lscale = snd_scaletable[volume[0] >> SND_SCALE_SHIFT];
	rscale = snd_scaletable[volume[1] >> SND_SCALE_SHIFT];

	for( i = 0; i < outCount; i++ )
	{
		pbuf[i].left += lscale[pData[sampleIndex+0]];
		pbuf[i].right += rscale[pData[sampleIndex+1]];
		sampleFrac += rateScale;
		sampleIndex += FIX_INTPART( sampleFrac )<<1;
		sampleFrac = FIX_FRACPART( sampleFrac );
	}
}

void S_Mix16Mono( portable_samplepair_t *pbuf, int *volume, short *pData, int inputOffset, uint rateScale, int outCount )
{
	int	i, sampleIndex = 0;
	uint	sampleFrac = inputOffset;

	// Not using pitch shift?
	if( rateScale == FIX( 1 ))
	{
		S_PaintMonoFrom16( pbuf, volume, pData, outCount );
		return;
	}

	for( i = 0; i < outCount; i++ )
	{
		pbuf[i].left += (volume[0] * (int)( pData[sampleIndex] ))>>8;
		pbuf[i].right += (volume[1] * (int)( pData[sampleIndex] ))>>8;
		sampleFrac += rateScale;
		sampleIndex += FIX_INTPART( sampleFrac );
		sampleFrac = FIX_FRACPART( sampleFrac );
	}
}

void S_Mix16Stereo( portable_samplepair_t *pbuf, int *volume, short *pData, int inputOffset, uint rateScale, int outCount )
{
	int	i, sampleIndex = 0;
	uint	sampleFrac = inputOffset;

	// Not using pitch shift?
	if( rateScale == FIX( 1 ))
	{
		S_PaintStereoFrom16( pbuf, volume, pData, outCount );
		return;
	}

	for( i = 0; i < outCount; i++ )
	{
		pbuf[i].left += (volume[0] * (int)( pData[sampleIndex+0] ))>>8;
		pbuf[i].right += (volume[1] * (int)( pData[sampleIndex+1] ))>>8;
		sampleFrac += rateScale;
		sampleIndex += FIX_INTPART(sampleFrac)<<1;
		sampleFrac = FIX_FRACPART(sampleFrac);
	}
}

void S_MixChannel( channel_t *pChannel, void *pData, int outputOffset, int inputOffset, uint fracRate, int outCount, int timecompress )
{
	int			pvol[CCHANVOLUMES];
	paintbuffer_t		*ppaint = MIX_GetCurrentPaintbufferPtr();
	wavdata_t			*pSource = pChannel->sfx->cache;
	portable_samplepair_t	*pbuf;

	Assert( pSource != NULL );

	pvol[0] = bound( 0, pChannel->leftvol, 255 );
	pvol[1] = bound( 0, pChannel->rightvol, 255 );
	pbuf = ppaint->pbuf + outputOffset;

	if( pSource->channels == 1 )
	{
		if( pSource->width == 1 )
			S_Mix8Mono( pbuf, pvol, pData, inputOffset, fracRate, outCount, timecompress );
		else S_Mix16Mono( pbuf, pvol, (short *)pData, inputOffset, fracRate, outCount );
	}
	else
	{
		if( pSource->width == 1 )
			S_Mix8Stereo( pbuf, pvol, pData, inputOffset, fracRate, outCount );
		else S_Mix16Stereo( pbuf, pvol, (short *)pData, inputOffset, fracRate, outCount );
	}
}

int S_MixDataToDevice( channel_t *pChannel, int sampleCount, int outRate, int outOffset, int timeCompress )
{
	// save this to compute total output
	int	startingOffset = outOffset;
	float	inputRate = ( pChannel->pitch * pChannel->sfx->cache->rate );
	float	rate = inputRate / outRate;
		
	// shouldn't be playing this if finished, but return if we are
	if( pChannel->pMixer.finished )
		return 0;

	// If we are terminating this wave prematurely, then make sure we detect the limit
	if( pChannel->pMixer.forcedEndSample )
	{
		// how many total input samples will we need?
		int	samplesRequired = (int)(sampleCount * rate);

		// will this hit the end?
		if( pChannel->pMixer.sample + samplesRequired >= pChannel->pMixer.forcedEndSample )
		{
			// yes, mark finished and truncate the sample request
			pChannel->pMixer.finished = true;
			sampleCount = (int)((pChannel->pMixer.forcedEndSample - pChannel->pMixer.sample) / rate );
		}
	}

	while( sampleCount > 0 )
	{
		int	availableSamples, outSampleCount;
		wavdata_t	*pSource = pChannel->sfx->cache;
		qboolean	use_loop = pChannel->use_loop;
		void	*pData = NULL;
		double	sampleFrac;
		int	i, j;

		// compute number of input samples required
		double	end = pChannel->pMixer.sample + rate * sampleCount;
		int	inputSampleCount = (int)(ceil( end ) - floor( pChannel->pMixer.sample ));

		availableSamples = S_GetOutputData( pSource, &pData, pChannel->pMixer.sample, inputSampleCount, use_loop );

		// none available, bail out
		if( !availableSamples ) break;

		sampleFrac = pChannel->pMixer.sample - floor( pChannel->pMixer.sample );

		if( availableSamples < inputSampleCount )
		{
			// how many samples are there given the number of input samples and the rate.
			outSampleCount = (int)ceil(( availableSamples - sampleFrac ) / rate );
		}
		else
		{
			outSampleCount = sampleCount;
		}

		// Verify that we won't get a buffer overrun.
		Assert( floor( sampleFrac + rate * ( outSampleCount - 1 )) <= availableSamples );

		// save current paintbuffer
		j = MIX_GetCurrentPaintbufferIndex();

		for( i = 0; i < CPAINTBUFFERS; i++ )
		{
			if( !paintbuffers[i].factive )
				continue;

			// mix chan into all active paintbuffers
			MIX_SetCurrentPaintbuffer( i );

			S_MixChannel( pChannel, pData, outOffset, FIX_FLOAT( sampleFrac ), FIX_FLOAT( rate ), outSampleCount, timeCompress );
		}

		MIX_SetCurrentPaintbuffer( j );

		pChannel->pMixer.sample += outSampleCount * rate;
		outOffset += outSampleCount;
		sampleCount -= outSampleCount;
	}

	// Did we run out of samples? if so, mark finished
	if( sampleCount > 0 )
	{
		pChannel->pMixer.finished = true;
	}

	// total number of samples mixed !!! at the output clock rate !!!
	return outOffset - startingOffset;
}

qboolean S_ShouldContinueMixing( channel_t *ch )
{
	if( ch->isSentence )
	{
		if( ch->currentWord )
			return true;
		return false;
	}

	return !ch->pMixer.finished;
}

// Mix all channels into active paintbuffers until paintbuffer is full or 'endtime' is reached.
// endtime: time in 44khz samples to mix
// rate: ignore samples which are not natively at this rate (for multipass mixing/filtering)
// if rate == SOUND_ALL_RATES then mix all samples this pass
// flags: if SOUND_MIX_DRY, then mix only samples with channel flagged as 'dry'
// outputRate: target mix rate for all samples.  Note, if outputRate = SOUND_DMA_SPEED, then
// this routine will fill the paintbuffer to endtime.  Otherwise, fewer samples are mixed.
// if( endtime - paintedtime ) is not aligned on boundaries of 4, 
// we'll miss data if outputRate < SOUND_DMA_SPEED!
void MIX_MixChannelsToPaintbuffer( int endtime, int rate, int outputRate )
{
	channel_t *ch;
	wavdata_t	*pSource;
	int	i, sampleCount;
	qboolean	bZeroVolume;

	// mix each channel into paintbuffer
	ch = channels;
	
	// validate parameters
	Assert( outputRate <= SOUND_DMA_SPEED );

	// make sure we're not discarding data
	Assert( !(( endtime - paintedtime ) & 0x3 ) || ( outputRate == SOUND_DMA_SPEED ));
											  
	// 44k: try to mix this many samples at outputRate
	sampleCount = ( endtime - paintedtime ) / ( SOUND_DMA_SPEED / outputRate );
	
	if( sampleCount <= 0 ) return;

	for( i = 0; i < total_channels; i++, ch++ )
	{
		if( !ch->sfx ) continue;

		// NOTE: background map is allow both type sounds: menu and game
		if( !cl.background )
		{
			if( cls.key_dest == key_console && ch->localsound )
			{
				// play, playvol
			}
			else if(( s_listener.inmenu || s_listener.paused ) && !ch->localsound )
			{
				// play only local sounds, keep pause for other
				continue;
			}
			else if( !s_listener.inmenu && !s_listener.active && !ch->staticsound )
			{
				// play only ambient sounds, keep pause for other
				continue;
			}
		}
		else if( cls.key_dest == key_console )
			continue;	// silent mode in console

		pSource = S_LoadSound( ch->sfx );

		// Don't mix sound data for sounds with zero volume. If it's a non-looping sound, 
		// just remove the sound when its volume goes to zero.
		bZeroVolume = !ch->leftvol && !ch->rightvol;
 
		if( !bZeroVolume )
		{
			// this values matched with GoldSrc
			if( ch->leftvol < 8 && ch->rightvol < 8 )
				bZeroVolume = true;
		}

		if( !pSource || ( bZeroVolume && pSource->loopStart == -1 ))
		{
			if( !pSource )
			{
				S_FreeChannel( ch );
				continue;
			}
		}
		else if( bZeroVolume )
		{
			continue;
		}

		// multipass mixing - only mix samples of specified sample rate
		switch( rate )
		{
		case SOUND_11k:
		case SOUND_22k:
		case SOUND_44k:
			if( rate != pSource->rate )
				continue;
			break;
		default:	break;
		}

		// get playback pitch
		if( ch->isSentence )
			ch->pitch = VOX_ModifyPitch( ch, ch->basePitch * 0.01f );
		else ch->pitch = ch->basePitch * 0.01f;

		if( CL_GetEntityByIndex( ch->entnum ) && ( ch->entchannel == CHAN_VOICE ))
		{
			if( pSource->width == 1 )
				SND_MoveMouth8( ch, pSource, sampleCount );
			else SND_MoveMouth16( ch, pSource, sampleCount );
		}

		// mix channel to all active paintbuffers.
		// NOTE: must be called once per channel only - consecutive calls retrieve additional data.
		if( ch->isSentence )
			VOX_MixDataToDevice( ch, sampleCount, outputRate, 0 );
		else S_MixDataToDevice( ch, sampleCount, outputRate, 0, 0 );

		if( !S_ShouldContinueMixing( ch ))
		{
			S_FreeChannel( ch );
		}
	}
}

// pass in index -1...count+2, return pointer to source sample in either paintbuffer or delay buffer
_inline portable_samplepair_t *S_GetNextpFilter( int i, portable_samplepair_t *pbuffer, portable_samplepair_t *pfiltermem )
{
	// The delay buffer is assumed to precede the paintbuffer by 6 duplicated samples
	if( i == -1 ) return (&(pfiltermem[0]));
	if( i == 0 ) return (&(pfiltermem[1]));
	if( i == 1 ) return (&(pfiltermem[2]));

	// return from paintbuffer, where samples are doubled.  
	// even samples are to be replaced with interpolated value.
	return (&(pbuffer[(i-2) * 2 + 1]));
}

// pass forward over passed in buffer and cubic interpolate all odd samples
// pbuffer: buffer to filter (in place)
// prevfilter:  filter memory. NOTE: this must match the filtertype ie: filtercubic[] for FILTERTYPE_CUBIC
// if NULL then perform no filtering.
// count: how many samples to upsample. will become count*2 samples in buffer, in place.

void S_Interpolate2xCubic( portable_samplepair_t *pbuffer, portable_samplepair_t *pfiltermem, int cfltmem, int count )
{

// implement cubic interpolation on 2x upsampled buffer.   Effectively delays buffer contents by 2 samples.
// pbuffer: contains samples at 0, 2, 4, 6...
// temppaintbuffer is temp buffer, same size as paintbuffer, used to store processed values
// count: number of samples to process in buffer ie: how many samples at 0, 2, 4, 6...

// finpos is the fractional, inpos the integer part.
//		finpos = 0.5 for upsampling by 2x
//		inpos is the position of the sample

//		xm1 = x [inpos - 1];
//		x0 = x [inpos + 0];
//		x1 = x [inpos + 1];
//		x2 = x [inpos + 2];
//		a = (3 * (x0-x1) - xm1 + x2) / 2;
//		b = 2*x1 + xm1 - (5*x0 + x2) / 2;
//		c = (x1 - xm1) / 2;
//		y [outpos] = (((a * finpos) + b) * finpos + c) * finpos + x0;

	int i, upCount = count << 1;
	int a, b, c;
	int xm1, x0, x1, x2;
	portable_samplepair_t *psamp0;
	portable_samplepair_t *psamp1;
	portable_samplepair_t *psamp2;
	portable_samplepair_t *psamp3;
	int outpos = 0;

	Assert( upCount <= PAINTBUFFER_SIZE );

	// pfiltermem holds 6 samples from previous buffer pass
	// process 'count' samples
	for( i = 0; i < count; i++)
	{
		// get source sample pointer
		psamp0 = S_GetNextpFilter( i-1, pbuffer, pfiltermem );
		psamp1 = S_GetNextpFilter( i+0, pbuffer, pfiltermem );
		psamp2 = S_GetNextpFilter( i+1, pbuffer, pfiltermem );
		psamp3 = S_GetNextpFilter( i+2, pbuffer, pfiltermem );

		// write out original sample to interpolation buffer
		temppaintbuffer[outpos++] = *psamp1;

		// get all left samples for interpolation window
		xm1 = psamp0->left;
		x0 = psamp1->left;
		x1 = psamp2->left;
		x2 = psamp3->left;
		
		// interpolate
		a = (3 * (x0-x1) - xm1 + x2) / 2;
		b = 2*x1 + xm1 - (5*x0 + x2) / 2;
		c = (x1 - xm1) / 2;
		
		// write out interpolated sample
		temppaintbuffer[outpos].left = a/8 + b/4 + c/2 + x0;
		
		// get all right samples for window
		xm1 = psamp0->right;
		x0 = psamp1->right;
		x1 = psamp2->right;
		x2 = psamp3->right;
		
		// interpolate
		a = (3 * (x0-x1) - xm1 + x2) / 2;
		b = 2*x1 + xm1 - (5*x0 + x2) / 2;
		c = (x1 - xm1) / 2;
		
		// write out interpolated sample, increment output counter
		temppaintbuffer[outpos++].right = a/8 + b/4 + c/2 + x0;
		
		Assert( outpos <= ( sizeof( temppaintbuffer ) / sizeof( temppaintbuffer[0] )));
	}
	
	Assert( cfltmem >= 3 );

	// save last 3 samples from paintbuffer
	pfiltermem[0] = pbuffer[upCount - 5];
	pfiltermem[1] = pbuffer[upCount - 3];
	pfiltermem[2] = pbuffer[upCount - 1];

	// copy temppaintbuffer back into paintbuffer
	for( i = 0; i < upCount; i++ )
		pbuffer[i] = temppaintbuffer[i];
}

// pass forward over passed in buffer and linearly interpolate all odd samples
// pbuffer: buffer to filter (in place)
// prevfilter:  filter memory. NOTE: this must match the filtertype ie: filterlinear[] for FILTERTYPE_LINEAR
// if NULL then perform no filtering.
// count: how many samples to upsample. will become count*2 samples in buffer, in place.
void S_Interpolate2xLinear( portable_samplepair_t *pbuffer, portable_samplepair_t *pfiltermem, int cfltmem, int count )
{
	int	i, upCount = count<<1;

	Assert( upCount <= PAINTBUFFER_SIZE );
	Assert( cfltmem >= 1 );

	// use interpolation value from previous mix
	pbuffer[0].left = (pfiltermem->left + pbuffer[0].left) >> 1;
	pbuffer[0].right = (pfiltermem->right + pbuffer[0].right) >> 1;

	for( i = 2; i < upCount; i += 2 )
	{
		// use linear interpolation for upsampling
		pbuffer[i].left = (pbuffer[i].left + pbuffer[i-1].left) >> 1;
		pbuffer[i].right = (pbuffer[i].right + pbuffer[i-1].right) >> 1;
	}

	// save last value to be played out in buffer
	*pfiltermem = pbuffer[upCount - 1]; 
}

// upsample by 2x, optionally using interpolation
// count: how many samples to upsample. will become count*2 samples in buffer, in place.
// pbuffer: buffer to upsample into (in place)
// pfiltermem:  filter memory. NOTE: this must match the filtertype ie: filterlinear[] for FILTERTYPE_LINEAR
// if NULL then perform no filtering.
// cfltmem: max number of sample pairs filter can use
// filtertype: FILTERTYPE_NONE, _LINEAR, _CUBIC etc.  Must match prevfilter.
void S_MixBufferUpsample2x( int count, portable_samplepair_t *pbuffer, portable_samplepair_t *pfiltermem, int cfltmem, int filtertype )
{
	int	upCount = count<<1;
	int	i, j;	

	// reverse through buffer, duplicating contents for 'count' samples
	for( i = upCount - 1, j = count - 1; j >= 0; i-=2, j-- )
	{	
		pbuffer[i] = pbuffer[j];
		pbuffer[i-1] = pbuffer[j];
	}

	// pass forward through buffer, interpolate all even slots
	switch( filtertype )
	{
	case FILTERTYPE_LINEAR:
		S_Interpolate2xLinear( pbuffer, pfiltermem, cfltmem, count );
		break;
	case FILTERTYPE_CUBIC:
		S_Interpolate2xCubic( pbuffer, pfiltermem, cfltmem, count );
		break;
	default:	// no filter
		break;
	}
}

// zero out all paintbuffers
void MIX_ClearAllPaintBuffers( int SampleCount, qboolean clearFilters )
{
	int	count = min( SampleCount, PAINTBUFFER_SIZE );
	int	i;

	// zero out all paintbuffer data (ignore sampleCount)
	for( i = 0; i < CPAINTBUFFERS; i++ )
	{
		if( paintbuffers[i].pbuf != NULL )
			memset( paintbuffers[i].pbuf, 0, (count+1) * sizeof( portable_samplepair_t ));

		if( clearFilters )
		{
			memset( paintbuffers[i].fltmem, 0, sizeof( paintbuffers[i].fltmem ));
		}
	}

	if( clearFilters )
	{
		MIX_ResetPaintbufferFilterCounters();
	}
}

// mixes pbuf1 + pbuf2 into pbuf3, count samples
// fgain is output gain 0-1.0
// NOTE: pbuf3 may equal pbuf1 or pbuf2!
void MIX_MixPaintbuffers( int ibuf1, int ibuf2, int ibuf3, int count, float fgain )
{
	portable_samplepair_t	*pbuf1, *pbuf2, *pbuf3;
	int			i, gain;

	gain = 256 * fgain;
	
	Assert( count <= PAINTBUFFER_SIZE );
	Assert( ibuf1 < CPAINTBUFFERS );
	Assert( ibuf2 < CPAINTBUFFERS );
	Assert( ibuf3 < CPAINTBUFFERS );

	pbuf1 = paintbuffers[ibuf1].pbuf;
	pbuf2 = paintbuffers[ibuf2].pbuf;
	pbuf3 = paintbuffers[ibuf3].pbuf;
	
	// destination buffer stereo - average n chans down to stereo 

	// destination 2ch:
	// pb1 2ch + pb2 2ch		-> pb3 2ch
	// pb1 2ch + pb2 (4ch->2ch)		-> pb3 2ch
	// pb1 (4ch->2ch) + pb2 (4ch->2ch)	-> pb3 2ch

	// mix front channels
	for( i = 0; i < count; i++ )
	{
		pbuf3[i].left = pbuf1[i].left;
		pbuf3[i].right = pbuf1[i].right;
		pbuf3[i].left += (pbuf2[i].left * gain) >> 8;
		pbuf3[i].right += (pbuf2[i].right * gain) >> 8;
	}
}

void MIX_CompressPaintbuffer( int ipaint, int count )
{
	portable_samplepair_t	*pbuf;
	paintbuffer_t		*ppaint;
	int			i;

	ppaint = MIX_GetPPaintFromIPaint( ipaint );
	pbuf = ppaint->pbuf;
	
	for( i = 0; i < count; i++, pbuf++ )
	{
		pbuf->left = CLIP( pbuf->left );
		pbuf->right = CLIP( pbuf->right );
	}
}

void S_MixUpsample( int sampleCount, int filtertype )
{
	paintbuffer_t	*ppaint = MIX_GetCurrentPaintbufferPtr();
	int		ifilter = ppaint->ifilter;

	Assert( ifilter < CPAINTFILTERS );

	S_MixBufferUpsample2x( sampleCount, ppaint->pbuf, &(ppaint->fltmem[ifilter][0]), CPAINTFILTERMEM, filtertype );

	// make sure on next upsample pass for this paintbuffer, new filter memory is used
	ppaint->ifilter++;
}

void MIX_MixStreamBuffer( int end )
{
	portable_samplepair_t	*pbuf;
	rawchan_t			*ch;

	pbuf = MIX_GetPFrontFromIPaint( ISTREAMBUFFER );
	ch = S_FindRawChannel( S_RAW_SOUND_BACKGROUNDTRACK, false );

	// clear the paint buffer
	if( s_listener.paused || !ch || ch->s_rawend < paintedtime )
	{
		memset( pbuf, 0, (end - paintedtime) * sizeof( portable_samplepair_t ));
	}
	else
	{	
		int	i, stop;

		// copy from the streaming sound source
		stop = (end < ch->s_rawend) ? end : ch->s_rawend;

		for( i = paintedtime; i < stop; i++ )
		{
			pbuf[i-paintedtime].left = ( ch->rawsamples[i & ( ch->max_samples - 1 )].left * ch->leftvol ) >> 8;
			pbuf[i-paintedtime].right = ( ch->rawsamples[i & ( ch->max_samples - 1 )].right * ch->rightvol ) >> 8;
		}			

		for( ; i < end; i++ )
			pbuf[i-paintedtime].left = pbuf[i-paintedtime].right = 0;
	}
}

void MIX_MixRawSamplesBuffer( int end )
{
	portable_samplepair_t	*pbuf;
	uint			i, j, stop;

	pbuf = MIX_GetCurrentPaintbufferPtr()->pbuf;

	if( s_listener.paused ) return;

	// paint in the raw channels
	for( i = 0; i < MAX_RAW_CHANNELS; i++ )
	{
		// copy from the streaming sound source
		rawchan_t	*ch = raw_channels[i];

		// background track should be mixing into another buffer
		if( !ch || ch->entnum == S_RAW_SOUND_BACKGROUNDTRACK )
			continue;

		// not audible
		if( !ch->leftvol && !ch->rightvol )
			continue;

		stop = (end < ch->s_rawend) ? end : ch->s_rawend;

		for( j = paintedtime; j < stop; j++ )
		{
			pbuf[j-paintedtime].left += ( ch->rawsamples[j & ( ch->max_samples - 1 )].left * ch->leftvol ) >> 8;
			pbuf[j-paintedtime].right += ( ch->rawsamples[j & ( ch->max_samples - 1 )].right * ch->rightvol ) >> 8;
		}
	}
}

// upsample and mix sounds into final 44khz versions of:
// IROOMBUFFER, IFACINGBUFFER, IFACINGAWAY
// dsp fx are then applied to these buffers by the caller.
// caller also remixes all into final IPAINTBUFFER output.
void MIX_UpsampleAllPaintbuffers( int end, int count )
{
	// process stream buffer
	MIX_MixStreamBuffer( end );

	// 11khz sounds are mixed into 3 buffers based on distance from listener, and facing direction
	// These buffers are facing, facingaway, room
	// These 3 mixed buffers are then each upsampled to 22khz.

	// 22khz sounds are mixed into the 3 buffers based on distance from listener, and facing direction
	// These 3 mixed buffers are then each upsampled to 44khz.

	// 44khz sounds are mixed into the 3 buffers based on distance from listener, and facing direction

	MIX_DeactivateAllPaintbuffers();

	// set paintbuffer upsample filter indices to 0
	MIX_ResetPaintbufferFilterCounters();

	// only mix to roombuffer if dsp fx are on KDB: perf
	MIX_ActivatePaintbuffer( IROOMBUFFER );	// operates on MIX_MixChannelsToPaintbuffer

	// mix 11khz sounds: 
	MIX_MixChannelsToPaintbuffer( end, SOUND_11k, SOUND_11k );

	// upsample all 11khz buffers by 2x
	// only upsample roombuffer if dsp fx are on KDB: perf
	MIX_SetCurrentPaintbuffer( IROOMBUFFER ); // operates on MixUpSample
	S_MixUpsample( count / ( SOUND_DMA_SPEED / SOUND_11k ), s_lerping->value );

	// mix 22khz sounds: 
	MIX_MixChannelsToPaintbuffer( end, SOUND_22k, SOUND_22k );
	
	// upsample all 22khz buffers by 2x
	// only upsample roombuffer if dsp fx are on KDB: perf
	MIX_SetCurrentPaintbuffer( IROOMBUFFER );
	S_MixUpsample( count / ( SOUND_DMA_SPEED / SOUND_22k ), s_lerping->value );

	// mix all 44khz sounds to all active paintbuffers
	MIX_MixChannelsToPaintbuffer( end, SOUND_44k, SOUND_DMA_SPEED );

	// mix raw samples from the video streams
	MIX_SetCurrentPaintbuffer( IROOMBUFFER );
	MIX_MixRawSamplesBuffer( end );

	MIX_DeactivateAllPaintbuffers();
	MIX_SetCurrentPaintbuffer( IPAINTBUFFER );
}

void MIX_PaintChannels( int endtime )
{
	int	end, count;

	CheckNewDspPresets();

	while( paintedtime < endtime )
	{
		// if paintbuffer is smaller than DMA buffer
		end = endtime;
		if( endtime - paintedtime > PAINTBUFFER_SIZE )
			end = paintedtime + PAINTBUFFER_SIZE;

		// number of 44khz samples to mix into paintbuffer, up to paintbuffer size
		count = end - paintedtime;

		// clear the all mix buffers
		MIX_ClearAllPaintBuffers( count, false );

		MIX_UpsampleAllPaintbuffers( end, count );

		// process all sounds with DSP
		DSP_Process( idsp_room, MIX_GetPFrontFromIPaint( IROOMBUFFER ), count );

		// add music or soundtrack from movie (no dsp)
		MIX_MixPaintbuffers( IPAINTBUFFER, IROOMBUFFER, IPAINTBUFFER, count, S_GetMasterVolume() );

		// add music or soundtrack from movie (no dsp)
		MIX_MixPaintbuffers( IPAINTBUFFER, ISTREAMBUFFER, IPAINTBUFFER, count, S_GetMusicVolume() );	

		// clip all values > 16 bit down to 16 bit
		MIX_CompressPaintbuffer( IPAINTBUFFER, count );

		// transfer IPAINTBUFFER paintbuffer out to DMA buffer
		MIX_SetCurrentPaintbuffer( IPAINTBUFFER );

		// transfer out according to DMA format
		S_TransferPaintBuffer( end );
		paintedtime = end;
	}
}
