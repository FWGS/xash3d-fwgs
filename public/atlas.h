/*
atlas.h - generic 2D atlas packer (strip-based)
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
#ifndef ATLAS_H
#define ATLAS_H

#include <string.h>
#include "xash3d_types.h"

// maximum atlas dimension
#define ATLAS_MAX_SIZE 1024

typedef struct atlas_s
{
	int allocated[ATLAS_MAX_SIZE];
	int size;
	int max_height;
} atlas_t;

static inline void Atlas_Init( atlas_t *atlas, int size )
{
	memset( atlas->allocated, 0, sizeof( atlas->allocated ));
	atlas->size = size;
	atlas->max_height = 0;
}

qboolean Atlas_AllocBlock( atlas_t *atlas, int w, int h, int *x, int *y );

#endif // ATLAS_H
