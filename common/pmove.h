/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef QW_PMOVE_H
#define QW_PMOVE_H

// a1ba: from QW's client/pmove.h
// and modified to keep binary compatibility with
// goldsrc base structs and xash extensions
typedef struct pmplane_s
{
	vec3_t normal;
	float dist;
} pmplane_t;

STATIC_CHECK_SIZEOF( pmplane_t, 16, 16 );

typedef struct pmtrace_s
{
	qboolean  allsolid;        // if true, plane is not valid
	qboolean  startsolid;      // if true, the initial point was in a solid area
	qboolean  inopen, inwater;
	float     fraction;        // time completed, 1.0 = didn't hit anything
	vec3_t    endpos;          // final position
	pmplane_t plane;           // surface normal at impact
	int       ent;             // entity the surface is on
	vec3_t    deltavelocity;
	int       hitgroup;
} pmtrace_t;

STATIC_CHECK_SIZEOF( pmtrace_t, 68, 68 );

typedef struct movevars_s
{
	float    gravity;
	float    stopspeed;
	float    maxspeed;
	float    spectatormaxspeed;
	float    accelerate;
	float    airaccelerate;
	float    wateraccelerate;
	float    friction;
	float    edgefriction; // goldsrc binary compat
	float    waterfriction;
	float    entgravity;

	// goldsrc additions
	float    bounce;
	float    stepsize;
	float    maxvelocity;
	float    zmax;
	float    waveHeight;
	qboolean footsteps;
	char     skyName[32];
	float    rollangle;
	float    rollspeed;
	vec3_t   skycolor;
	vec3_t   skyvec;

	// xash extensions
	int      features;
	int      fog_settings;
	float    wateralpha;
	vec3_t   skydir; // unused
	float    skyangle; // unused
} movevars_t;

STATIC_CHECK_SIZEOF( movevars_t, 160, 160 );

#endif // QW_PMOVE_H
