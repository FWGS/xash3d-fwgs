/*
atlas.c - generic 2D atlas packer (strip-based)
Copyright (C) 1997 id Software
Copyright (C) Xash3D and Xash3D FWGS developers

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "atlas.h"

qboolean Atlas_AllocBlock( atlas_t *atlas, int w, int h, int *x, int *y )
{
	int i, j;
	int best, best2;
	int size = atlas->size;

	best = size;

	for( i = 0; i <= size - w; )
	{
		best2 = 0;

		for( j = 0; j < w; j++ )
		{
			if( atlas->allocated[i + j] >= best )
				break;
			if( atlas->allocated[i + j] > best2 )
				best2 = atlas->allocated[i + j];
		}

		if( j == w )
		{
			*x = i;
			*y = best = best2;
			if( best == 0 )
				break;
			i++;
		}
		else
		{
			i += j + 1;
		}
	}

	if( best + h > size )
		return false;

	for( i = 0; i < w; i++ )
		atlas->allocated[*x + i] = best + h;

	if( best + h > atlas->max_height )
		atlas->max_height = best + h;

	return true;
}
