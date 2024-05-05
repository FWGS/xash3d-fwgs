/*
vox.h - sentences vox private header
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef VOX_H
#define VOX_H

#define CVOXWORDMAX    64
#define SENTENCE_INDEX -99999 // unique sentence index

typedef struct voxword_s
{
	int    volume;       // increase percent, ie: 125 = 125% increase
	int    pitch;        // pitch shift up percent
	int    start;        // offset start of wave percent
	int    end;          // offset end of wave percent
	int    cbtrim;       // end of wave after being trimmed to 'end'
	int    fKeepCached;  // 1 if this word was already in cache before sentence referenced it
	int    samplefrac;   // if pitch shifting, this is position into wav * 256
	int    timecompress; // % of wave to skip during playback (causes no pitch shift)
	sfx_t *sfx;          // name and cache pointer
} voxword_t;

struct channel_s;
void VOX_LoadWord( struct channel_s *pchan );
void VOX_FreeWord( struct channel_s *pchan );

#endif
