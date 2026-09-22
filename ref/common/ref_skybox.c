/*
ref_skybox.c - skybox cubemap projection shared between renderers

Copyright (C) 2026 Uncle Mike
Copyright (C) 2026 FWGS team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "ref_common.h"

const int r_skyTexOrder[SKYBOX_MAX_SIDES] = { 0, 2, 1, 3, 4, 5 };

// s = [0]/[2], t = [1]/[2]
const int vec_to_st[SKYBOX_MAX_SIDES][3] =
{
{ -2,  3,  1 },
{  2,  3, -1 },
{  1,  3,  2 },
{ -1,  3, -2 },
{ -2, -1,  3 },
{ -2,  1, -3 }
};

/*
==============
R_SkyboxAxisFromDir

Pick the cubemap face a world-space direction points at
==============
*/
int R_SkyboxAxisFromDir( const vec3_t dir )
{
	const float av0 = fabs( dir[0] );
	const float av1 = fabs( dir[1] );
	const float av2 = fabs( dir[2] );

	if( av0 > av1 && av0 > av2 )
		return ( dir[0] < 0 ) ? 1 : 0;
	else if( av1 > av2 && av1 > av0 )
		return ( dir[1] < 0 ) ? 3 : 2;
	else
		return ( dir[2] < 0 ) ? 5 : 4;
}

/*
==============
R_SkyboxProject

Project a world-space direction onto a cubemap face, giving s and t
in the [-1, 1] range. Returns false when the direction is parallel
to the face (degenerate projection).
==============
*/
qboolean R_SkyboxProject( const vec3_t dir, int axis, float *s, float *t )
{
	int j = vec_to_st[axis][2];
	const float dv = ( j > 0 ) ? dir[j - 1] : -dir[-j - 1];

	if( dv == 0.0f )
		return false;

	j = vec_to_st[axis][0];
	*s = ( j < 0 ) ? -dir[-j - 1] / dv : dir[j - 1] / dv;
	j = vec_to_st[axis][1];
	*t = ( j < 0 ) ? -dir[-j - 1] / dv : dir[j - 1] / dv;

	return true;
}
