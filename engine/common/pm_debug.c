/*
pm_debug.c - player move debugging code
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

#include "common.h"
#include "mathlib.h"
#include "pm_local.h"
#if !XASH_DEDICATED
#include "client.h" // CL_Particle
#endif

// expand debugging BBOX particle hulls by this many units.
#define BOX_GAP	0.0f    

/*
===============
PM_ParticleLine

draw line from particles
================
*/
void PM_ParticleLine( const vec3_t start, const vec3_t end, int pcolor, float life, float zvel )
{
#if !XASH_DEDICATED
	float	len, curdist;
	vec3_t	diff, pos;

	// determine distance
	VectorSubtract( end, start, diff );
	len = VectorNormalizeLength( diff );
	curdist = 0;

	while( curdist <= len )
	{
		VectorMA( start, curdist, diff, pos );
		CL_Particle( pos, pcolor, life, 0, zvel );
		curdist += 2.0f;
	}
#endif // XASH_DEDICATED
}

/*
================
PM_DrawRectangle

================
*/
static void PM_DrawRectangle( const vec3_t tl, const vec3_t bl, const vec3_t tr, const vec3_t br, int pcolor, float life )
{
	PM_ParticleLine( tl, bl, pcolor, life, 0 );
	PM_ParticleLine( bl, br, pcolor, life, 0 );
	PM_ParticleLine( br, tr, pcolor, life, 0 );
	PM_ParticleLine( tr, tl, pcolor, life, 0 );
}

/*
================
PM_DrawBBox

================
*/
void PM_DrawBBox( const vec3_t mins, const vec3_t maxs, const vec3_t origin, int pcolor, float life )
{
#if !XASH_DEDICATED
	vec3_t	p[8], tmp;
	float	gap = BOX_GAP;
	int	i;

	for( i = 0; i < 8; i++ )
	{
		tmp[0] = (i & 1) ? mins[0] - gap : maxs[0] + gap;
		tmp[1] = (i & 2) ? mins[1] - gap : maxs[1] + gap ;
		tmp[2] = (i & 4) ? mins[2] - gap : maxs[2] + gap ;

		VectorAdd( tmp, origin, tmp );
		VectorCopy( tmp, p[i] );
	}

	for( i = 0; i < 6; i++ )
	{
		PM_DrawRectangle( p[boxpnt[i][1]], p[boxpnt[i][0]], p[boxpnt[i][2]], p[boxpnt[i][3]], pcolor, life );
	}
#endif // XASH_DEDICATED
}
