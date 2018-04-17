/*
index.h - compact version of famous library mpg123
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef INDEX_H
#define INDEX_H

typedef struct frame_index_s
{
	mpg_off_t	*data;	// actual data, the frame positions
	mpg_off_t	step;	// advancement in frame number per index point
	mpg_off_t	next;	// frame offset supposed to come next into the index
	size_t	size;	// total number of possible entries
	size_t	fill;	// number of used entries
	size_t	grow_size;// if > 0: index allowed to grow on need with these steps, instead of lowering resolution
} frame_index_t;

// the condition for a framenum to be appended to the index. 
#define FI_NEXT( fi, framenum )	((fi).size && framenum == (fi).next)

// initialize stuff, set things to zero and NULL...
void fi_init( frame_index_t *fi );
// deallocate/zero things.
void fi_exit( frame_index_t *fi );
// prepare a given size, preserving current fill, if possible.
int fi_resize( frame_index_t *fi, size_t newsize );
// append a frame position, reducing index density if needed.
void fi_add( frame_index_t *fi, mpg_off_t pos );
// replace the frame index
int fi_set( frame_index_t *fi, mpg_off_t *offsets, mpg_off_t step, size_t fill );
// empty the index (setting fill=0 and step=1), but keep current size.
void fi_reset( frame_index_t *fi );

#endif//INDEX_H