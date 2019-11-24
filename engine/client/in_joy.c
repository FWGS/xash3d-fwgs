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

typedef enum engineAxis_e
{
	JOY_AXIS_SIDE = 0,
	JOY_AXIS_FWD,
	JOY_AXIS_PITCH,
	JOY_AXIS_YAW,
	JOY_AXIS_RT,
	JOY_AXIS_LT,
	JOY_AXIS_NULL
} engineAxis_t;

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
static byte currentbinding; // add posibility to remap keys, to place it in joykeys[]
convar_t *joy_enable;
static convar_t *joy_pitch;
static convar_t *joy_yaw;
static convar_t *joy_forward;
static convar_t *joy_side;
static convar_t *joy_found;
static convar_t *joy_index;
static convar_t *joy_lt_threshold;
static convar_t *joy_rt_threshold;
static convar_t *joy_side_deadzone;
static convar_t *joy_forward_deadzone;
static convar_t *joy_side_key_threshold;
static convar_t *joy_forward_key_threshold;
static convar_t *joy_pitch_deadzone;
static convar_t *joy_yaw_deadzone;
static convar_t *joy_axis_binding;

/*
============
Joy_IsActive
============
*/
qboolean Joy_IsActive( void )
{
	return joy_found->value && joy_enable->value;
}

/*
============
Joy_HatMotionEvent

DPad events
============
*/
void Joy_HatMotionEvent( byte hat, byte value )
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

	if( !joy_found->value )
		return;

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
		trigThreshold = joy_rt_threshold->value;
		break;
	case JOY_AXIS_LT:
		trigButton = K_JOY1;
		trigThreshold = joy_lt_threshold->value;
		break;
	default:
		Con_Reportf( S_ERROR  "Joy_ProcessTrigger: invalid axis = %i", engineAxis );
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
		threshold = joy_side_key_threshold->value;
		negative = JOY_HAT_LEFT;
		positive = JOY_HAT_RIGHT;
		break;
	case JOY_AXIS_FWD:
		threshold = joy_side_key_threshold->value;
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
	case JOY_AXIS_FWD:   deadzone = joy_forward_deadzone->value; break;
	case JOY_AXIS_SIDE:  deadzone = joy_side_deadzone->value; break;
	case JOY_AXIS_PITCH: deadzone = joy_pitch_deadzone->value; break;
	case JOY_AXIS_YAW:   deadzone = joy_yaw_deadzone->value; break;
	default:
		Con_Reportf( S_ERROR  "Joy_ProcessStick: invalid axis = %i", engineAxis );
		break;
	}

	if( value < deadzone && value > -deadzone )
		value = 0; // caught new event in deadzone, fill it with zero(no motion)

	// update axis values
	joyaxis[engineAxis].prevval = joyaxis[engineAxis].val;
	joyaxis[engineAxis].val = value;

	// fwd/side axis simulate hat movement
	if( ( engineAxis == JOY_AXIS_SIDE || engineAxis == JOY_AXIS_FWD ) &&
		( CL_IsInMenu() || CL_IsInConsole() ) )
	{
		int val = 0;

		val |= Joy_GetHatValueForAxis( JOY_AXIS_SIDE );
		val |= Joy_GetHatValueForAxis( JOY_AXIS_FWD );

		Joy_HatMotionEvent( 0, val );
	}
}

/*
=============
Joy_AxisMotionEvent

Axis events
=============
*/
void Joy_AxisMotionEvent( byte axis, short value )
{
	byte engineAxis;

	if( !joy_found->value )
		return;

	if( axis >= MAX_AXES )
	{
		Con_Reportf( "Only 6 axes is supported\n" );
		return;
	}

	engineAxis = joyaxesmap[axis]; // convert to engine inner axis control

	if( engineAxis == JOY_AXIS_NULL )
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
Joy_BallMotionEvent

Trackball events. UNDONE
=============
*/
void Joy_BallMotionEvent( byte ball, short xrel, short yrel )
{
	//if( !joy_found->value )
	//	return;
}

/*
=============
Joy_ButtonEvent

Button events
=============
*/
void Joy_ButtonEvent( byte button, byte down )
{
	if( !joy_found->value )
		return;

	// generic game button code.
	if( button > 32 )
	{
		int origbutton = button;
		button = ( button & 31 ) + K_AUX1;

		Con_Reportf( "Only 32 joybuttons is supported, converting %i button ID to %s\n", origbutton, Key_KeynumToString( button ) );
	}
	else button += K_AUX1;

	Key_Event( button, down );
}

/*
=============
Joy_RemoveEvent

Called when joystick is removed. For future expansion
=============
*/
void Joy_RemoveEvent( void )
{
	if( joy_found->value )
		Cvar_FullSet( "joy_found", "0", FCVAR_READ_ONLY );
}

/*
=============
Joy_RemoveEvent

Called when joystick is removed. For future expansion
=============
*/
void Joy_AddEvent( void )
{
	if( joy_enable->value && !joy_found->value )
		Cvar_FullSet( "joy_found", "1", FCVAR_READ_ONLY );
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

	if( FBitSet( joy_axis_binding->flags, FCVAR_CHANGED ) )
	{
		const char *bind = joy_axis_binding->string;
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

		ClearBits( joy_axis_binding->flags, FCVAR_CHANGED );
	}

	*fw     -= joy_forward->value * (float)joyaxis[JOY_AXIS_FWD ].val/(float)SHRT_MAX;  // must be form -1.0 to 1.0
	*side   += joy_side->value    * (float)joyaxis[JOY_AXIS_SIDE].val/(float)SHRT_MAX;
#if !defined(XASH_SDL)
	*dpitch += joy_pitch->value * (float)joyaxis[JOY_AXIS_PITCH].val/(float)SHRT_MAX * host.realframetime;  // abs axis rotate is frametime related
	*dyaw   -= joy_yaw->value   * (float)joyaxis[JOY_AXIS_YAW  ].val/(float)SHRT_MAX * host.realframetime;
#else
	// HACKHACK: SDL have inverted look axis.
	*dpitch -= joy_pitch->value * (float)joyaxis[JOY_AXIS_PITCH].val/(float)SHRT_MAX * host.realframetime;
	*dyaw   += joy_yaw->value   * (float)joyaxis[JOY_AXIS_YAW  ].val/(float)SHRT_MAX * host.realframetime;
#endif
}

/*
=============
Joy_Init

Main init procedure
=============
*/
void Joy_Init( void )
{
	joy_pitch   = Cvar_Get( "joy_pitch",   "100.0", FCVAR_ARCHIVE, "joystick pitch sensitivity" );
	joy_yaw     = Cvar_Get( "joy_yaw",     "100.0", FCVAR_ARCHIVE, "joystick yaw sensitivity" );
	joy_side    = Cvar_Get( "joy_side",    "1.0", FCVAR_ARCHIVE, "joystick side sensitivity. Values from -1.0 to 1.0" );
	joy_forward = Cvar_Get( "joy_forward", "1.0", FCVAR_ARCHIVE, "joystick forward sensitivity. Values from -1.0 to 1.0" );

	joy_lt_threshold = Cvar_Get( "joy_lt_threshold", "-16384", FCVAR_ARCHIVE, "left trigger threshold. Value from -32768 to 32767");
	joy_rt_threshold = Cvar_Get( "joy_rt_threshold", "-16384", FCVAR_ARCHIVE, "right trigger threshold. Value from -32768 to 32767" );

	// emit a key event at 75% axis move
	joy_side_key_threshold = Cvar_Get( "joy_side_key_threshold", "24576", FCVAR_ARCHIVE, "side axis key event emit threshold. Value from 0 to 32767" );
	joy_forward_key_threshold = Cvar_Get( "joy_forward_key_threshold", "24576", FCVAR_ARCHIVE, "forward axis key event emit threshold. Value from 0 to 32767");

	// by default, we rely on deadzone detection come from system, but some glitchy devices report false deadzones
	joy_side_deadzone = Cvar_Get( "joy_side_deadzone", "0", FCVAR_ARCHIVE, "side axis deadzone. Value from 0 to 32767" );
	joy_forward_deadzone = Cvar_Get( "joy_forward_deadzone", "0", FCVAR_ARCHIVE, "forward axis deadzone. Value from 0 to 32767");
	joy_pitch_deadzone = Cvar_Get( "joy_pitch_deadzone", "0", FCVAR_ARCHIVE, "pitch axis deadzone. Value from 0 to 32767");
	joy_yaw_deadzone = Cvar_Get( "joy_yaw_deadzone", "0", FCVAR_ARCHIVE, "yaw axis deadzone. Value from 0 to 32767" );

	joy_axis_binding = Cvar_Get( "joy_axis_binding", "sfpyrl", FCVAR_ARCHIVE, "axis hardware id to engine inner axis binding, "
		"s - side, f - forward, y - yaw, p - pitch, r - left trigger, l - right trigger" );
	joy_found   = Cvar_Get( "joy_found", "0", FCVAR_READ_ONLY, "is joystick is connected" );
	// we doesn't loaded config.cfg yet, so this cvar is not archive.
	// change by +set joy_index in cmdline
	joy_index   = Cvar_Get( "joy_index", "0", FCVAR_READ_ONLY, "current active joystick" );

	joy_enable = Cvar_Get( "joy_enable", "1", FCVAR_ARCHIVE, "enable joystick" );

	if( Sys_CheckParm( "-nojoy" ))
	{
		Cvar_FullSet( "joy_enable", "0", FCVAR_READ_ONLY );
		return;
	}

	Cvar_FullSet( "joy_found", va( "%d", Platform_JoyInit( joy_index->value )), FCVAR_READ_ONLY );
}

/*
===========
Joy_Shutdown

Shutdown joystick code
===========
*/
void Joy_Shutdown( void )
{
	Cvar_FullSet( "joy_found", 0, FCVAR_READ_ONLY );
}
