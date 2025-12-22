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

int S_AdjustLoopedSamplePosition( const wavdata_t *source, int current_sample, qboolean enable_looping )
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
