/*
index.c - compact version of famous library mpg123
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

#include "mpg123.h"
#include "index.h"

// the next expected frame offset, one step ahead.
static mpg_off_t fi_next( frame_index_t *fi )
{
	return (mpg_off_t)fi->fill*fi->step;
}

// shrink down the used index to the half.
// be careful with size = 1 ... there's no shrinking possible there.
static void fi_shrink( frame_index_t *fi )
{
	if( fi->fill < 2 )
	{
		return; // won't shrink below 1.
	}
	else
	{
		size_t	c;

		// double the step, half the fill. Should work as well for fill%2 = 1
		fi->step *= 2;
		fi->fill /= 2;

		// move the data down.
		for( c = 0; c < fi->fill; ++c )
			fi->data[c] = fi->data[2*c];
	}

	fi->next = fi_next( fi );
}

void fi_init( frame_index_t *fi )
{
	fi->data = NULL;
	fi->step = 1;
	fi->fill = 0;
	fi->size = 0;
	fi->grow_size = 0;
	fi->next = fi_next( fi );
}

void fi_exit( frame_index_t *fi )
{
	if( fi->size && fi->data != NULL )
		free( fi->data );

	fi_init( fi ); // be prepared for further fun, still.
}

int fi_resize( frame_index_t *fi, size_t newsize )
{
	mpg_off_t	*newdata = NULL;

	if( newsize == fi->size )
		return 0;

	if( newsize > 0 && newsize < fi->size )
	{
		// when we reduce buffer size a bit, shrink stuff.
		while( fi->fill > newsize )
			fi_shrink( fi );
	}

	newdata = realloc( fi->data, newsize * sizeof( mpg_off_t ));
	if( newsize == 0 || newdata != NULL )
	{
		fi->data = newdata;
		fi->size = newsize;

		if( fi->fill > fi->size )
			fi->fill = fi->size;

		fi->next = fi_next( fi );

		return 0;
	}
	else
	{
		return -1;
	}
}

void fi_add( frame_index_t *fi, mpg_off_t pos )
{
	if( fi->fill == fi->size )
	{
		mpg_off_t	framenum = fi->fill*fi->step;

		// index is full, we need to shrink... or grow.
		// store the current frame number to check later if we still want it.

		// if we want not / cannot grow, we shrink.
		if( !( fi->grow_size && fi_resize( fi, fi->size+fi->grow_size ) == 0 ))
			fi_shrink( fi );

		// now check if we still want to add this frame (could be that not, because of changed step).
		if( fi->next != framenum )
			return;
	}

	// when we are here, we want that frame.
	if( fi->fill < fi->size ) // safeguard for size = 1, or just generally
	{
		fi->data[fi->fill] = pos;
		fi->fill++;
		fi->next = fi_next( fi );
	}
}

int fi_set( frame_index_t *fi, mpg_off_t *offsets, mpg_off_t step, size_t fill )
{
	if( fi_resize( fi, fill ) == -1 )
		return -1;

	fi->step = step;

	if( offsets != NULL )
	{
		memcpy( fi->data, offsets, fill * sizeof( mpg_off_t ));
		fi->fill = fill;
	}
	else
	{
		// allocation only, no entries in index yet
		fi->fill = 0;
	}

	fi->next = fi_next( fi );

	return 0;
}

void fi_reset( frame_index_t *fi )
{
	fi->fill = 0;
	fi->step = 1;
	fi->next = fi_next( fi );
}