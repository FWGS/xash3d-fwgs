/*
s_utils.c - common sound functions
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

//-----------------------------------------------------------------------------
// Purpose: wrap the position wrt looping
// Input  : samplePosition - absolute position
// Output : int - looped position
//-----------------------------------------------------------------------------
int S_ConvertLoopedPosition( wavdata_t *pSource, int samplePosition, qboolean use_loop )
{
	// if the wave is looping and we're past the end of the sample
	// convert to a position within the loop
	// At the end of the loop, we return a short buffer, and subsequent call
	// will loop back and get the rest of the buffer
	if( FBitSet( pSource->flags, SOUND_LOOPED ) && samplePosition >= pSource->samples && use_loop )
	{
		// size of loop
		int	loopSize = pSource->samples - pSource->loopStart;

		// subtract off starting bit of the wave
		samplePosition -= pSource->loopStart;

		if( loopSize )
		{
			// "real" position in memory (mod off extra loops)
			samplePosition = pSource->loopStart + ( samplePosition % loopSize );
		}
		// ERROR? if no loopSize
	}

	return samplePosition;
}

int S_GetOutputData( wavdata_t *pSource, void **pData, int samplePosition, int sampleCount, qboolean use_loop )
{
	int	totalSampleCount;
	int	sampleSize;

	// handle position looping
	samplePosition = S_ConvertLoopedPosition( pSource, samplePosition, use_loop );

	// how many samples are available (linearly not counting looping)
	totalSampleCount = pSource->samples - samplePosition;

	// may be asking for a sample out of range, clip at zero
	if( totalSampleCount < 0 ) totalSampleCount = 0;

	// clip max output samples to max available
	if( sampleCount > totalSampleCount )
		sampleCount = totalSampleCount;

	sampleSize = pSource->width * pSource->channels;

	// this can never be zero -- other functions divide by this.
	// This should never happen, but avoid crashing
	if( sampleSize <= 0 ) sampleSize = 1;

	// byte offset in sample database
	samplePosition *= sampleSize;

	// if we are returning some samples, store the pointer
	if( sampleCount )
	{
		*pData = pSource->buffer + samplePosition;
	}

	return sampleCount;
}
