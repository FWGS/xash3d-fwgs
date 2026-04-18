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
	short rawval; // value before deadzone zeroing, for debug display
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
static CVAR_DEFINE_AUTO( joy_gyro_pitch, "1.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "gyroscope sensitivity for looking up and down" );
static CVAR_DEFINE_AUTO( joy_gyro_yaw, "1.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "gyroscope sensitivity for turning left and right" );
static CVAR_DEFINE_AUTO( joy_gyro_roll, "0.0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "gyroscope sensitivity when tilting the device sideways" );
static CVAR_DEFINE_AUTO( joy_gyro_pitch_deadzone, "0.5", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "gyroscope pitch axis deadzone (deg/s)" );
static CVAR_DEFINE_AUTO( joy_gyro_yaw_deadzone, "0.5", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "gyroscope yaw axis deadzone (deg/s)" );
static CVAR_DEFINE_AUTO( joy_gyro_roll_deadzone, "0.5", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "gyroscope roll axis deadzone (deg/s)" );
static CVAR_DEFINE_AUTO( joy_debug, "0", 0, "visualize gamepad axes and buttons" );

// stores the latest instantaneous gyroscope rotation rates (in rad/s) from platform input
static vec3_t joy_gyro_speed;
// last non-zero gyro speed, retained for debug display after joy_gyro_speed is cleared
static vec3_t joy_gyro_speed_display;

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
		threshold = joy_forward_key_threshold.value;
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

	joyaxis[engineAxis].rawval = value;

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
	if( engineAxis >= MAX_AXES )
		return;

	engineAxis = joyaxesmap[engineAxis];

	if( engineAxis >= MAX_AXES )
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
	VectorCopy( data, joy_gyro_speed );
	VectorCopy( data, joy_gyro_speed_display );
}

/*
=============
Joy_FinalizeMove

Append movement from axis. Called everyframe
=============
*/
void Joy_FinalizeMove( float *fw, float *side, float *dpitch, float *dyaw )
{
	if( !Joy_IsActive( ))
		return;

	if( FBitSet( joy_axis_binding.flags, FCVAR_CHANGED ) )
	{
		const char *bind = joy_axis_binding.string;
		size_t i;

		for( i = 0; bind[i] && i < MAX_AXES; i++ )
		{
			switch( bind[i] )
			{
			case 's': joyaxesmap[i] = JOY_AXIS_SIDE; break;
			case 'f': joyaxesmap[i] = JOY_AXIS_FWD; break;
			case 'y': joyaxesmap[i] = JOY_AXIS_YAW; break;
			case 'p': joyaxesmap[i] = JOY_AXIS_PITCH; break;
			case 'r': joyaxesmap[i] = JOY_AXIS_RT; break;
			case 'l': joyaxesmap[i] = JOY_AXIS_LT; break;
			default : joyaxesmap[i] = MAX_AXES; break;
			}
		}

		ClearBits( joy_axis_binding.flags, FCVAR_CHANGED );
	}

	*fw     -= joy_forward.value * (float)joyaxis[JOY_AXIS_FWD ].val/(float)SHRT_MAX;  // must be form -1.0 to 1.0
	*side   += joy_side.value    * (float)joyaxis[JOY_AXIS_SIDE].val/(float)SHRT_MAX;
	*dpitch += joy_pitch.value * (float)joyaxis[JOY_AXIS_PITCH].val/(float)SHRT_MAX * host.realframetime;
	*dyaw   -= joy_yaw.value   * (float)joyaxis[JOY_AXIS_YAW  ].val/(float)SHRT_MAX * host.realframetime;

	if( joy_have_gyro.value && (int)joy_calibrated.value == JOY_CALIBRATED )
	{
		float pitch_speed = joy_gyro_speed[0] * ( 180.0f / M_PI );
		float yaw_speed   = joy_gyro_speed[1] * ( 180.0f / M_PI );
		float roll_speed  = joy_gyro_speed[2] * ( 180.0f / M_PI );

		if( fabs( pitch_speed ) < joy_gyro_pitch_deadzone.value )
			pitch_speed = 0.0f;
		if( fabs( yaw_speed ) < joy_gyro_yaw_deadzone.value )
			yaw_speed = 0.0f;
		if( fabs( roll_speed ) < joy_gyro_roll_deadzone.value )
			roll_speed = 0.0f;

		*dpitch -= joy_gyro_pitch.value * pitch_speed * host.realframetime;
		*dyaw   += joy_gyro_yaw.value   * yaw_speed   * host.realframetime;
		*dyaw   += joy_gyro_roll.value  * roll_speed  * host.realframetime;
	}

	VectorClear( joy_gyro_speed );
}

/*
=============
Joy_DrawDebug

Draw gamepad axes and buttons for debugging (joy_debug 1)
=============
*/
void Joy_DrawDebug( void )
{
	static const int buttons[] =
	{
		K_A_BUTTON, K_B_BUTTON, K_X_BUTTON, K_Y_BUTTON, 0,
		K_L1_BUTTON, K_R1_BUTTON, 0,
		K_L2_BUTTON, K_R2_BUTTON, 0,
		K_BACK_BUTTON, K_MODE_BUTTON, K_START_BUTTON, 0,
		K_LSTICK, K_RSTICK, 0,

		// simulated buttons
		K_DPAD_UP, K_DPAD_DOWN, K_DPAD_LEFT, K_DPAD_RIGHT, 0,
		K_LTRIGGER, K_RTRIGGER, 0,

		// additional buttons
		K_C_BUTTON, K_Z_BUTTON, K_MISC_BUTTON, 0,
		K_PADDLE1_BUTTON, K_PADDLE2_BUTTON, K_PADDLE3_BUTTON, K_PADDLE4_BUTTON, 0
	};
	static const char *const axisnames[MAX_AXES] = { "SIDE", "FWD", "PITCH", "YAW", "RT", "LT" };

	// deadzone for sticks, threshold for triggers
	static const convar_t *const axis_threshold[MAX_AXES] =
	{
		&joy_side_deadzone,
		&joy_forward_deadzone,
		&joy_pitch_deadzone,
		&joy_yaw_deadzone,
		&joy_rt_threshold,
		&joy_lt_threshold,
	};

	cl_font_t *font = Con_GetCurFont();

	if( !joy_debug.value || !Joy_IsActive( ))
		return;

	float x = 8;
	float y = 100;
	const float bar_w = 100;
	const float halfbar_w = bar_w * 0.5f;
	const float bar_h = font->charHeight - 2;
	const float center = x + halfbar_w;

	const rgba_t bar_backcolor = { 40, 40, 40, 180 };
	const rgba_t bar_fillcolor = { 100, 200, 100, 200 };

	// draw axes as labeled bars
	for( int i = 0; i < MAX_AXES; i++ )
	{

		qboolean is_trigger = ( i >= JOY_AXIS_RT );
		float fval = (float)joyaxis[i].val / SHRT_MAX;
		float fprev = (float)joyaxis[i].prevval / SHRT_MAX;
		float fthreshold = axis_threshold[i]->value / SHRT_MAX;

		CL_DrawString( x, y, axisnames[i], g_color_table[7], font, 0 );
		y += font->charHeight;

		// background
		ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, bar_w, bar_h, bar_backcolor[0], bar_backcolor[1], bar_backcolor[2], bar_backcolor[3] );

		if( is_trigger )
		{
			// fill bar from the left for triggers
			float filled = fval * bar_w;

			if( filled > 0 )
				ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, filled, bar_h, bar_fillcolor[0], bar_fillcolor[1], bar_fillcolor[2], bar_fillcolor[3] );

			// fill threshold with red, and yellow edge marker
			float thresh_x = x + fthreshold * bar_w;
			ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, thresh_x - x, bar_h, 180, 40, 40, 120 );
			ref.dllFuncs.FillRGBA( kRenderTransTexture, thresh_x, y, 1, bar_h, 255, 200, 0, 220 );

			// prevval marker
			float prev_x = x + fprev * bar_w;
			ref.dllFuncs.FillRGBA( kRenderTransTexture, prev_x, y, 1, bar_h, 200, 200, 200, 180 );
		}
		else
		{
			// for sticks bar is filled from the center
			float filled = fval * halfbar_w;
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center + Q_min( 0, filled ), y, fabs( filled ), bar_h, bar_fillcolor[0], bar_fillcolor[1], bar_fillcolor[2], bar_fillcolor[3] );

			// fill deadzone with red, and yellow edge marker
			float thresh_x = fthreshold * halfbar_w;
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center - thresh_x, y, thresh_x * 2, bar_h, 180, 40, 40, 120 );
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center + thresh_x, y, 1, bar_h, 255, 200, 0, 220 );
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center - thresh_x, y, 1, bar_h, 255, 200, 0, 220 );

			// mark the center
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center, y, 1, bar_h, 180, 180, 180, 220 );

			// now draw the actual value
			float raw_x = center + ((float)joyaxis[i].rawval / SHRT_MAX * halfbar_w );
			ref.dllFuncs.FillRGBA( kRenderTransTexture, raw_x, y, 1, bar_h, 255, 255, 100, 200 );

			// prevval marker
			float prev_x = center + fprev * halfbar_w;
			ref.dllFuncs.FillRGBA( kRenderTransTexture, prev_x, y, 1, bar_h, 200, 200, 200, 180 );
		}

		y += bar_h + 2;
	}

	// draw gyro if available
	if( joy_have_gyro.value )
	{
		static const char *gyronames[3] = { "GYRO P", "GYRO Y", "GYRO R" };
		static const convar_t *const gyro_deadzone[3] =
		{
			&joy_gyro_pitch_deadzone,
			&joy_gyro_yaw_deadzone,
			&joy_gyro_roll_deadzone,
		};

		for( int i = 0; i < 3; i++ )
		{
			// clamp to +-5 rad/s for display
			float fval = joy_gyro_speed_display[i] / 5.0f;

			fval = bound( -1.0f, fval, 1.0f );

			CL_DrawString( x, y, gyronames[i], g_color_table[7], font, 0 );
			y += font->charHeight;

			// background
			ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, bar_w, bar_h, bar_backcolor[0], bar_backcolor[1], bar_backcolor[2], bar_backcolor[3] );

			// for gyro bar is filled from the center
			float filled = fval * halfbar_w;
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center + Q_min( 0, filled ), y, fabs( filled ), bar_h, bar_fillcolor[0], bar_fillcolor[1], bar_fillcolor[2], bar_fillcolor[3] );

			// deadzone in deg/s; display scale is +-5 rad/s
			float fthreshold = gyro_deadzone[i]->value * ( M_PI / 180.0f ) / 5.0f;
			fthreshold = bound( 0.0f, fthreshold, 1.0f );

			if( fthreshold > 0.0f )
			{
				float thresh_x = fthreshold * halfbar_w;
				ref.dllFuncs.FillRGBA( kRenderTransTexture, center - thresh_x, y, thresh_x * 2, bar_h, 180, 40, 40, 120 );
				ref.dllFuncs.FillRGBA( kRenderTransTexture, center + thresh_x, y, 1, bar_h, 255, 200, 0, 220 );
				ref.dllFuncs.FillRGBA( kRenderTransTexture, center - thresh_x, y, 1, bar_h, 255, 200, 0, 220 );
			}

			// mark the center
			ref.dllFuncs.FillRGBA( kRenderTransTexture, center, y, 1, bar_h, 180, 180, 180, 220 );

			// gyro doesn't have prevval like axes
			y += bar_h + 2;
		}
	}

	// draw buttons in a row
	y += 4;
	x = 8;
	for( int i = 0; i < ARRAYSIZE( buttons ); i++ )
	{		
		if( !buttons[i] )
		{
			x = 8;
			y += font->charHeight;
			continue;
		}

		qboolean pressed = Key_IsDown( buttons[i] );
		int btn_w = 0;
		rgba_t color;
		string name;

		Q_strncpy( name, Key_KeynumToString( buttons[i] ), sizeof( name ));

		char *p = Q_strstr( name, "_BUTTON" );
		if( p )
			*p = 0;

		MakeRGBA( color, pressed ? 0 : 255, pressed ? 255 : 0, 0, 255 );
		CL_DrawString( x, y, name, color, font, 0 );
		CL_DrawStringLen( font, name, &btn_w, NULL, 0 );
		x += btn_w + 6;
	}
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

	Cvar_RegisterVariable( &joy_gyro_pitch );
	Cvar_RegisterVariable( &joy_gyro_yaw );
	Cvar_RegisterVariable( &joy_gyro_roll );

	Cvar_RegisterVariable( &joy_gyro_pitch_deadzone );
	Cvar_RegisterVariable( &joy_gyro_yaw_deadzone );
	Cvar_RegisterVariable( &joy_gyro_roll_deadzone );

	Cvar_RegisterVariable( &joy_debug );

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
