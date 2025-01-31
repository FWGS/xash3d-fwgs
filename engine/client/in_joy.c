/*
joyinput.c - joystick common input code

Copyright (C) 2016 a1batross

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
#include "keydefs.h"
#include "client.h"
#include "platform/platform.h"

#ifndef SHRT_MAX
#define SHRT_MAX 0x7FFF
#endif

#define MAX_AXES JOY_AXIS_NULL

// index - axis num come from event
// value - inner axis
static engineAxis_t joyaxesmap[MAX_AXES] =
{
	JOY_AXIS_SIDE,  // left stick, x
	JOY_AXIS_FWD,   // left stick, y
	JOY_AXIS_PITCH, // right stick, y
	JOY_AXIS_YAW,   // right stick, x
	JOY_AXIS_RT,    // right trigger
	JOY_AXIS_LT     // left trigger
};

static struct joy_axis_s
{
	short val;
	short prevval;
} joyaxis[MAX_AXES] = { 0 };
static qboolean joy_initialized;

static CVAR_DEFINE_AUTO( joy_pitch,   "100.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "joystick pitch sensitivity" );
static CVAR_DEFINE_AUTO( joy_yaw,     "100.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "joystick yaw sensitivity" );
static CVAR_DEFINE_AUTO( joy_side,    "1.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "joystick side sensitivity. Values from -1.0 to 1.0" );
static CVAR_DEFINE_AUTO( joy_forward, "1.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "joystick forward sensitivity. Values from -1.0 to 1.0" );
static CVAR_DEFINE_AUTO( joy_lt_threshold, "16384", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "left trigger threshold. Value from 0 to 32767");
static CVAR_DEFINE_AUTO( joy_rt_threshold, "16384", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "right trigger threshold. Value from 0 to 32767" );
static CVAR_DEFINE_AUTO( joy_side_key_threshold, "24576", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "side axis key event emit threshold. Value from 0 to 32767" );
static CVAR_DEFINE_AUTO( joy_forward_key_threshold, "24576", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "forward axis key event emit threshold. Value from 0 to 32767");
static CVAR_DEFINE_AUTO( joy_side_deadzone, DEFAULT_JOY_DEADZONE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "side axis deadzone. Value from 0 to 32767" );
static CVAR_DEFINE_AUTO( joy_forward_deadzone, DEFAULT_JOY_DEADZONE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "forward axis deadzone. Value from 0 to 32767");
static CVAR_DEFINE_AUTO( joy_pitch_deadzone, DEFAULT_JOY_DEADZONE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "pitch axis deadzone. Value from 0 to 32767");
static CVAR_DEFINE_AUTO( joy_yaw_deadzone, DEFAULT_JOY_DEADZONE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "yaw axis deadzone. Value from 0 to 32767" );
static CVAR_DEFINE_AUTO( joy_axis_binding, "sfpyrl", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "axis hardware id to engine inner axis binding, "
	"s - side, f - forward, y - yaw, p - pitch, r - left trigger, l - right trigger" );
CVAR_DEFINE_AUTO( joy_enable, "1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enable joystick" );
static CVAR_DEFINE_AUTO( joy_have_gyro, "0", FCVAR_READ_ONLY, "tells whether current active gamepad has gyroscope or not" );
static CVAR_DEFINE_AUTO( joy_calibrated, "0", FCVAR_READ_ONLY, "tells whether current active gamepad gyroscope has been calibrated or not" );

/*
============
Joy_IsActive
============
*/
qboolean Joy_IsActive( void )
{
	return joy_enable.value;
}

/*
===========
Joy_SetCapabilities
===========
*/
void Joy_SetCapabilities( qboolean have_gyro )
{
	Cvar_FullSet( joy_have_gyro.name, have_gyro ? "1" : "0", joy_have_gyro.flags );
}

/*
===========
Joy_SetCalibrationState
===========
*/
void Joy_SetCalibrationState( joy_calibration_state_t state )
{
	if( (int)joy_calibrated.value == state )
		return;

	Cvar_FullSet( joy_calibrated.name, va( "%d", state ), joy_calibrated.flags );
}

/*
============
Joy_HatMotionEvent

DPad events
============
*/
static void Joy_HatMotionEvent( int value )
{
	struct
	{
		int mask;
		int key;
	} keys[] =
	{
		{ JOY_HAT_UP, K_UPARROW },
		{ JOY_HAT_DOWN, K_DOWNARROW },
		{ JOY_HAT_LEFT, K_LEFTARROW },
		{ JOY_HAT_RIGHT, K_RIGHTARROW },
	};
	int i;

	for( i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( value & keys[i].mask )
		{
			if( !Key_IsDown( keys[i].key ))
				Key_Event( keys[i].key, true );
		}
		else
		{
			if( Key_IsDown( keys[i].key ))
				Key_Event( keys[i].key, false );
		}
	}
}

/*
=============
Joy_ProcessTrigger
=============
*/
static void Joy_ProcessTrigger( const engineAxis_t engineAxis, short value )
{
	int trigButton = 0, trigThreshold = 0;

	switch( engineAxis )
	{
	case JOY_AXIS_RT:
		trigButton = K_JOY2;
		trigThreshold = joy_rt_threshold.value;
		break;
	case JOY_AXIS_LT:
		trigButton = K_JOY1;
		trigThreshold = joy_lt_threshold.value;
		break;
	default:
		Con_Reportf( S_ERROR "%s: invalid axis = %i\n", __func__, engineAxis );
		break;
	}

	// update axis values
	joyaxis[engineAxis].prevval = joyaxis[engineAxis].val;
	joyaxis[engineAxis].val = value;

	if( joyaxis[engineAxis].val > trigThreshold &&
		joyaxis[engineAxis].prevval <= trigThreshold ) // ignore random press
	{
		Key_Event( trigButton, true );
	}
	else if( joyaxis[engineAxis].val < trigThreshold &&
			 joyaxis[engineAxis].prevval >= trigThreshold ) // we're unpressing (inverted)
	{
		Key_Event( trigButton, false );
	}
}

static int Joy_GetHatValueForAxis( const engineAxis_t engineAxis )
{
	int threshold, negative, positive;

	switch( engineAxis )
	{
	case JOY_AXIS_SIDE:
		threshold = joy_side_key_threshold.value;
		negative = JOY_HAT_LEFT;
		positive = JOY_HAT_RIGHT;
		break;
	case JOY_AXIS_FWD:
		threshold = joy_side_key_threshold.value;
		negative = JOY_HAT_UP;
		positive = JOY_HAT_DOWN;
		break;
	default:
		ASSERT( false ); // only fwd/side axes can emit key events
		return 0;
	}

	// similar code in Joy_ProcessTrigger
	if( joyaxis[engineAxis].val > threshold &&
		joyaxis[engineAxis].prevval <= threshold ) // ignore random press
	{
		return positive;
	}
	if( joyaxis[engineAxis].val < -threshold &&
		joyaxis[engineAxis].prevval >= -threshold ) // we're unpressing (inverted)
	{
		return negative;
	}
	return 0;
}

/*
=============
Joy_ProcessStick
=============
*/
static void Joy_ProcessStick( const engineAxis_t engineAxis, short value )
{
	int deadzone = 0;

	switch( engineAxis )
	{
	case JOY_AXIS_FWD:   deadzone = joy_forward_deadzone.value; break;
	case JOY_AXIS_SIDE:  deadzone = joy_side_deadzone.value; break;
	case JOY_AXIS_PITCH: deadzone = joy_pitch_deadzone.value; break;
	case JOY_AXIS_YAW:   deadzone = joy_yaw_deadzone.value; break;
	default:
		Con_Reportf( S_ERROR "%s: invalid axis = %i\n", __func__, engineAxis );
		break;
	}

	if( value < deadzone && value > -deadzone )
		value = 0; // caught new event in deadzone, fill it with zero(no motion)

	// update axis values
	joyaxis[engineAxis].prevval = joyaxis[engineAxis].val;
	joyaxis[engineAxis].val = value;

	// fwd/side axis simulate hat movement
	if( ( engineAxis == JOY_AXIS_SIDE || engineAxis == JOY_AXIS_FWD ) &&
		( cls.key_dest == key_menu || cls.key_dest == key_console ))
	{
		int val = 0;

		val |= Joy_GetHatValueForAxis( JOY_AXIS_SIDE );
		val |= Joy_GetHatValueForAxis( JOY_AXIS_FWD );

		Joy_HatMotionEvent( val );
	}
}

/*
=============
Joy_AxisMotionEvent

Axis events
=============
*/
void Joy_AxisMotionEvent( engineAxis_t engineAxis, short value )
{
	if( engineAxis >= JOY_AXIS_NULL )
		return;

	if( value == joyaxis[engineAxis].val )
		return; // it is not an update

	if( engineAxis >= JOY_AXIS_RT )
		Joy_ProcessTrigger( engineAxis, value );
	else
		Joy_ProcessStick( engineAxis, value );
}

/*
=============
Joy_GyroEvent

Gyroscope events
=============
*/
void Joy_GyroEvent( vec3_t data )
{

}

/*
=============
Joy_FinalizeMove

Append movement from axis. Called everyframe
=============
*/
void Joy_FinalizeMove( float *fw, float *side, float *dpitch, float *dyaw )
{
	if( !Joy_IsActive() )
		return;

	if( FBitSet( joy_axis_binding.flags, FCVAR_CHANGED ) )
	{
		const char *bind = joy_axis_binding.string;
		size_t i;

		for( i = 0; bind[i]; i++ )
		{
			switch( bind[i] )
			{
			case 's': joyaxesmap[i] = JOY_AXIS_SIDE; break;
			case 'f': joyaxesmap[i] = JOY_AXIS_FWD; break;
			case 'y': joyaxesmap[i] = JOY_AXIS_YAW; break;
			case 'p': joyaxesmap[i] = JOY_AXIS_PITCH; break;
			case 'r': joyaxesmap[i] = JOY_AXIS_RT; break;
			case 'l': joyaxesmap[i] = JOY_AXIS_LT; break;
			default : joyaxesmap[i] = JOY_AXIS_NULL; break;
			}
		}

		ClearBits( joy_axis_binding.flags, FCVAR_CHANGED );
	}

	*fw     -= joy_forward.value * (float)joyaxis[JOY_AXIS_FWD ].val/(float)SHRT_MAX;  // must be form -1.0 to 1.0
	*side   += joy_side.value    * (float)joyaxis[JOY_AXIS_SIDE].val/(float)SHRT_MAX;
	*dpitch -= joy_pitch.value * (float)joyaxis[JOY_AXIS_PITCH].val/(float)SHRT_MAX * host.realframetime;
	*dyaw   += joy_yaw.value   * (float)joyaxis[JOY_AXIS_YAW  ].val/(float)SHRT_MAX * host.realframetime;
}

static void Joy_CalibrateGyro_f( void )
{
	if( !joy_have_gyro.value )
	{
		Con_Printf( "Current active gamepad doesn't have gyroscope\n" );
		return;
	}

	Platform_CalibrateGamepadGyro();
}

/*
=============
Joy_Init

Main init procedure
=============
*/
void Joy_Init( void )
{
	Cmd_AddRestrictedCommand( "joy_calibrate_gyro", Joy_CalibrateGyro_f, "calibrate gamepad gyroscope. You must to put gamepad on stationary surface" );

	Cvar_RegisterVariable( &joy_pitch );
	Cvar_RegisterVariable( &joy_yaw );
	Cvar_RegisterVariable( &joy_side );
	Cvar_RegisterVariable( &joy_forward );

	Cvar_RegisterVariable( &joy_lt_threshold );
	Cvar_RegisterVariable( &joy_rt_threshold );

	// emit a key event at 75% axis move
	Cvar_RegisterVariable( &joy_side_key_threshold );
	Cvar_RegisterVariable( &joy_forward_key_threshold );

	// by default, we rely on deadzone detection come from system, but some glitchy devices report false deadzones
	Cvar_RegisterVariable( &joy_side_deadzone );
	Cvar_RegisterVariable( &joy_forward_deadzone );
	Cvar_RegisterVariable( &joy_pitch_deadzone );
	Cvar_RegisterVariable( &joy_yaw_deadzone );

	Cvar_RegisterVariable( &joy_axis_binding );

	Cvar_RegisterVariable( &joy_have_gyro );
	Cvar_RegisterVariable( &joy_calibrated );
	Cvar_RegisterVariable( &joy_enable );

	// renamed from -nojoy to -noenginejoy to not conflict with
	// client.dll's joystick support
	if( Sys_CheckParm( "-noenginejoy" ))
	{
		Cvar_FullSet( "joy_enable", "0", FCVAR_READ_ONLY );
		return;
	}

	Platform_JoyInit();

	joy_initialized = true;
}

/*
===========
Joy_Shutdown

Shutdown joystick code
===========
*/
void Joy_Shutdown( void )
{
	Platform_JoyShutdown();
}
