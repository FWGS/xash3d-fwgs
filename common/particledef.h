/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#ifndef PARTICLEDEF_H
#define PARTICLEDEF_H

typedef enum
{
	pt_static,
	pt_grav,
	pt_slowgrav,
	pt_fire,
	pt_explode,
	pt_explode2,
	pt_blob,
	pt_blob2,
	pt_vox_slowgrav,
	pt_vox_grav,
	pt_clientcustom	// Must have callback function specified
} ptype_t;

typedef struct particle_s
{
	vec3_t		org;
	short		color;
	short		packedColor;
	struct particle_s	*next;
	vec3_t		vel;
	float		ramp;
	float		die;
	ptype_t		type;
	void		(*deathfunc)( struct particle_s *particle );

	// for pt_clientcusttom, we'll call this function each frame
	void		(*callback)( struct particle_s *particle, float frametime );

	// For deathfunc, etc.
	unsigned char	context;
} particle_t;

#endif//PARTICLEDEF_H
