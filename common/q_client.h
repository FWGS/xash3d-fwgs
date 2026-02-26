/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// client.h -- primary header for client

#ifndef QUAKE_CLIENT_H
#define QUAKE_CLIENT_H

// Definitions originally taken from Quake, modified for binary compatibility

typedef struct usercmd_s
{
	int16_t   lerp_msec; // added in HL
	int8_t    msec;      // added in QW
	uint8_t   pad1;
	vec3_t    viewangles;

	// intended velocities
	float     forwardmove;
	float     sidemove;
	float     upmove;
	uint8_t   lightlevel;
	uint8_t   pad2;
	uint16_t  buttons; // added in QW
	uint8_t   impulse;

	// added in HL
	uint8_t   weaponselect;
	uint8_t   pad3[2];

	// unused HL impact stuff, left for modders
	// TODO: document how to use them in delta encoding
	int32_t   reserved[4];
} usercmd_t;

STATIC_CHECK_SIZEOF( usercmd_t, 52, 52 );

typedef struct dlight_s
{
	vec3_t   origin;
	float    radius;
	struct
	{
		uint8_t r, g, b;
	} color;
	float    die;      // stop lighting after this time
	float    decay;    // drop this each second
	float    minlight; // don't add when contributing less
	int      key;      // so entities can reuse same entry
	qboolean dark;     // subtracts light instead of adding
} dlight_t;

STATIC_CHECK_SIZEOF( dlight_t, 40, 40 );

//
// cl_input
//
typedef struct
{
	int		down[2];		// key nums holding it down
	int		state;			// low bit is down state
} kbutton_t;

STATIC_CHECK_SIZEOF( kbutton_t, 12, 12 );

#endif // QUAKE_CLIENT_H
