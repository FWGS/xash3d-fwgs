/*
in_gyro.c - System gyroscope input code
Copyright (C) 2026 Xash3D FWGS contributors

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
#include "input.h"
#include "client.h"

static CVAR_DEFINE_AUTO( gyro_enable, "0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enables aiming with built-in device gyroscope" );
static CVAR_DEFINE_AUTO( gyro_available, "0", FCVAR_READ_ONLY, "tells whether system gyroscope hardware is available or not" );
static CVAR_DEFINE_AUTO( gyro_pitch, "1.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "built-in gyroscope sensitivity for looking up and down" );
static CVAR_DEFINE_AUTO( gyro_yaw, "1.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "built-in gyroscope sensitivity for turning left and right" );
static CVAR_DEFINE_AUTO( gyro_roll, "0.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "built-in gyroscope sensitivity when tilting the device sideways" );
static CVAR_DEFINE_AUTO( gyro_pitch_deadzone, "0.5", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "built-in gyroscope pitch axis deadzone (deg/s)" );
static CVAR_DEFINE_AUTO( gyro_yaw_deadzone, "0.5", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "built-in gyroscope yaw axis deadzone (deg/s)" );
static CVAR_DEFINE_AUTO( gyro_roll_deadzone, "0.5", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "built-in gyroscope roll axis deadzone (deg/s)" );

// stores the latest instantaneous rotation rates from built-in gyroscope
static vec3_t gyro_speed;

/*
==============
IN_GyroInit

==============
*/
void IN_GyroInit( void )
{
	Cvar_RegisterVariable( &gyro_enable );
	Cvar_RegisterVariable( &gyro_available );
	Cvar_RegisterVariable( &gyro_pitch );
	Cvar_RegisterVariable( &gyro_yaw );
	Cvar_RegisterVariable( &gyro_roll );
	Cvar_RegisterVariable( &gyro_pitch_deadzone );
	Cvar_RegisterVariable( &gyro_yaw_deadzone );
	Cvar_RegisterVariable( &gyro_roll_deadzone );
}

/*
==============
IN_GyroCheckAvailability

One-time late check called after startup and configs
==============
*/
void IN_GyroCheckAvailability( void )
{
#if XASH_SDL
	if( gyro_available.value )
		return;

	if( SDLash_GyroIsAvailable() )
	{
		Cvar_FullSet( "gyro_available", "1", FCVAR_READ_ONLY );
	}
#endif
}

/*
=============
IN_GyroEvent

System gyroscope events from platform
=============
*/
void IN_GyroEvent( vec3_t data )
{
	VectorCopy( data, gyro_speed );
}

/*
=============
IN_GyroFinalizeMove

Apply gyro movement to view angles
=============
*/
void IN_GyroFinalizeMove( float *fw, float *side, float *dpitch, float *dyaw )
{
	platform_orientation_t orient;
	float orient_scale = 1.0f;

	if( !gyro_enable.value || !gyro_available.value )
		return;

	orient = Platform_GetDisplayOrientation();
	if( orient == ORIENTATION_LANDSCAPE_FLIPPED )
		orient_scale = -1.0f;

	// In Landscape mode axes are swapped relative to natural (Portrait) orientation
	// Y axis rotation becomes Pitch (up/down)
	// X axis rotation becomes Yaw (left/right)
	float pitch_speed = -orient_scale * gyro_speed[1] * ( 180.0f / M_PI );
	float yaw_speed   = orient_scale * gyro_speed[0] * ( 180.0f / M_PI );
	float roll_speed  = orient_scale * gyro_speed[2] * ( 180.0f / M_PI );

	if( fabs( pitch_speed ) < gyro_pitch_deadzone.value )
		pitch_speed = 0.0f;
	if( fabs( yaw_speed ) < gyro_yaw_deadzone.value )
		yaw_speed = 0.0f;
	if( fabs( roll_speed ) < gyro_roll_deadzone.value )
		roll_speed = 0.0f;

	*dpitch -= gyro_pitch.value * pitch_speed * host.realframetime;
	*dyaw   += gyro_yaw.value   * yaw_speed   * host.realframetime;
	*dyaw   += gyro_roll.value  * roll_speed  * host.realframetime;

	VectorClear( gyro_speed );
}
