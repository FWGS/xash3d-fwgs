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
static CVAR_DEFINE_AUTO( gyro_debug, "0", 0, "visualize built-in device gyroscope" );

// stores the latest instantaneous rotation rates from built-in gyroscope
static vec3_t gyro_speed;
static vec3_t gyro_speed_display;

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
	Cvar_RegisterVariable( &gyro_debug );
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

static void IN_GyroMap( const vec3_t src, vec3_t dst )
{
	float orient_scale = 1.0f;

	platform_orientation_t orient = Platform_GetDisplayOrientation();
	if( orient == ORIENTATION_LANDSCAPE_FLIPPED )
		orient_scale = -1.0f;

	#if XASH_ANDROID || XASH_IOS
		// In Landscape mode axes are swapped relative to natural (Portrait) orientation
		// Y axis rotation becomes Pitch (up/down)
		// X axis rotation becomes Yaw (left/right)
		dst[0] = -orient_scale * src[1] * ( 180.0f / M_PI );
		dst[1] = orient_scale * src[0] * ( 180.0f / M_PI );
		dst[2] = orient_scale * src[2] * ( 180.0f / M_PI );
	#else
		dst[0] = orient_scale * src[0] * ( 180.0f / M_PI );
		dst[1] = orient_scale * src[1] * ( 180.0f / M_PI );
		dst[2] = orient_scale * src[2] * ( 180.0f / M_PI );
	#endif
}

/*
=============
IN_GyroFinalizeMove

Apply gyro movement to view angles
=============
*/
void IN_GyroFinalizeMove( float *fw, float *side, float *dpitch, float *dyaw )
{
	vec3_t mapped_speed;

	if( !gyro_enable.value || !gyro_available.value )
		return;

	IN_GyroMap( gyro_speed, mapped_speed );
	VectorCopy( mapped_speed, gyro_speed_display );

	if( fabs( mapped_speed[0] ) < gyro_pitch_deadzone.value )
		mapped_speed[0] = 0.0f;
	if( fabs( mapped_speed[1] ) < gyro_yaw_deadzone.value )
		mapped_speed[1] = 0.0f;
	if( fabs( mapped_speed[2] ) < gyro_roll_deadzone.value )
		mapped_speed[2] = 0.0f;

	*dpitch -= gyro_pitch.value * mapped_speed[0] * host.realframetime;
	*dyaw   += gyro_yaw.value   * mapped_speed[1] * host.realframetime;
	*dyaw   += gyro_roll.value  * mapped_speed[2] * host.realframetime;

	VectorClear( gyro_speed );
}

void IN_GyroDrawDebug( void )
{
	cl_font_t *font = Con_GetCurFont();

	if( !gyro_debug.value || !gyro_available.value )
		return;

	float x = 8;
	float y = 100;
	const float bar_w = 100;
	const float halfbar_w = bar_w * 0.5f;
	const float bar_h = font->charHeight - 2;
	const float center = x + halfbar_w;

	platform_orientation_t orient = Platform_GetDisplayOrientation();
	const char *orient_name = "UNKNOWN";
	switch( orient )
	{
	case ORIENTATION_LANDSCAPE:
		orient_name = "LANDSCAPE";
		break;
	case ORIENTATION_LANDSCAPE_FLIPPED:
		orient_name = "LANDSCAPE FLIPPED";
		break;
	case ORIENTATION_PORTRAIT:
		orient_name = "PORTRAIT";
		break;
	case ORIENTATION_PORTRAIT_FLIPPED:
		orient_name = "PORTRAIT FLIPPED";
		break;
	default:
		orient_name = "UNKNOWN";
		break;
	}

	char orient_text[32];
	Q_snprintf( orient_text, sizeof( orient_text ), "ORIENT: %s", orient_name );

	const rgba_t bar_backcolor = { 40, 40, 40, 180 };
	const rgba_t bar_fillcolor = { 100, 200, 100, 200 };
	static const char *gyronames[3] = { "GYRO P", "GYRO Y", "GYRO R" };
	static const convar_t *const gyro_deadzone[3] =
	{
		&gyro_pitch_deadzone,
		&gyro_yaw_deadzone,
		&gyro_roll_deadzone,
	};

	for( int i = 0; i < 3; i++ )
	{
		float fval = gyro_speed_display[i] / 180.0f;
		fval = bound( -1.0f, fval, 1.0f );

		CL_DrawString( x, y, gyronames[i], g_color_table[7], font, 0 );
		y += font->charHeight;

		ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, bar_w, bar_h, bar_backcolor[0], bar_backcolor[1], bar_backcolor[2], bar_backcolor[3] );

		float filled = fval * halfbar_w;
		ref.dllFuncs.FillRGBA( kRenderTransTexture, center + Q_min( 0, filled ), y, fabs( filled ), bar_h, bar_fillcolor[0], bar_fillcolor[1], bar_fillcolor[2], bar_fillcolor[3] );

		float fthreshold = gyro_deadzone[i]->value / 180.0f;
		fthreshold = bound( 0.0f, fthreshold, 1.0f );
		if( fthreshold > 0.0f )
		{
			float thresh_x = fthreshold * halfbar_w;
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center - thresh_x, y, thresh_x * 2, bar_h, 180, 40, 40, 120 );
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center + thresh_x, y, 1, bar_h, 255, 200, 0, 220 );
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center - thresh_x, y, 1, bar_h, 255, 200, 0, 220 );
		}

		ref.dllFuncs.FillRGBA( kRenderTransTexture, center, y, 1, bar_h, 180, 180, 180, 220 );
		y += bar_h + 2;
	}

	CL_DrawString( x, y, orient_text, g_color_table[1], font, 0 );
}
