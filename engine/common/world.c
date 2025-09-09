/*
world.c - common worldtrace routines
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
#include "world.h"
#include "pm_defs.h"
#include "mod_local.h"
#include "xash3d_mathlib.h"
#include "studio.h"

/*
==================
World_TransformAABB
==================
*/
void World_TransformAABB( matrix4x4 transform, const vec3_t mins, const vec3_t maxs, vec3_t outmins, vec3_t outmaxs )
{
	vec3_t	p1, p2;
	matrix4x4	itransform;
	int	i;

	if( !outmins || !outmaxs ) return;

	Matrix4x4_Invert_Simple( itransform, transform );
	ClearBounds( outmins, outmaxs );

	// compute a full bounding box
	for( i = 0; i < 8; i++ )
	{
		p1[0] = ( i & 1 ) ? mins[0] : maxs[0];
		p1[1] = ( i & 2 ) ? mins[1] : maxs[1];
		p1[2] = ( i & 4 ) ? mins[2] : maxs[2];

		p2[0] = DotProduct( p1, itransform[0] );
		p2[1] = DotProduct( p1, itransform[1] );
		p2[2] = DotProduct( p1, itransform[2] );

		if( p2[0] < outmins[0] ) outmins[0] = p2[0];
		if( p2[0] > outmaxs[0] ) outmaxs[0] = p2[0];
		if( p2[1] < outmins[1] ) outmins[1] = p2[1];
		if( p2[1] > outmaxs[1] ) outmaxs[1] = p2[1];
		if( p2[2] < outmins[2] ) outmins[2] = p2[2];
		if( p2[2] > outmaxs[2] ) outmaxs[2] = p2[2];
	}

	// sanity check
	for( i = 0; i < 3; i++ )
	{
		if( outmins[i] > outmaxs[i] )
		{
			VectorClear( outmins );
			VectorClear( outmaxs );
			return;
		}
	}
}
