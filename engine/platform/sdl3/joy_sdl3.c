/*
joy_sdl3 - SDL3 gamepads
Copyright (C) 2018-2025 Xash3D FWGS contributors

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
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "platform_sdl3.h"

static const int g_button_mapping[] =
{
	K_A_BUTTON, K_B_BUTTON, K_X_BUTTON, K_Y_BUTTON,
	K_BACK_BUTTON, K_MODE_BUTTON, K_START_BUTTON,
	K_LSTICK, K_RSTICK,
	K_L1_BUTTON, K_R1_BUTTON,
	K_DPAD_UP, K_DPAD_DOWN, K_DPAD_LEFT, K_DPAD_RIGHT,
	K_MISC_BUTTON,
	K_PADDLE1_BUTTON, K_PADDLE2_BUTTON, K_PADDLE3_BUTTON, K_PADDLE4_BUTTON,
	K_TOUCHPAD,
};

// Swap axis to follow default axis binding:
static const engineAxis_t g_axis_mapping[] =
{
	JOY_AXIS_SIDE, // SDL_GAMEPAD_AXIS_LEFTX,
	JOY_AXIS_FWD, // SDL_GAMEPAD_AXIS_LEFTY,
	JOY_AXIS_YAW, // SDL_GAMEPAD_AXIS_RIGHTX,
	JOY_AXIS_PITCH, // SDL_GAMEPAD_AXIS_RIGHTY,
	JOY_AXIS_LT, // SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
	JOY_AXIS_RT, // SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

static SDL_JoystickID g_current_gamepad_id; // used to send rumble to, zero is invalid
static SDL_Gamepad *g_current_gamepad;
static SDL_Gamepad **g_gamepads;
static size_t g_num_gamepads;

#define CALIBRATION_TIME 5.0f

static struct
{
	float    time;
	vec3_t   data;
	vec3_t   calibrated_values;
	float    data_rate;
	int      samples;
	qboolean continuous; // true after first successful calibration
} gyrocal;

static void SDLash_RestartCalibration( void )
{
	Joy_SetCalibrationState( JOY_NOT_CALIBRATED );

	memset( &gyrocal, 0, sizeof( gyrocal ));

	gyrocal.time = host.realtime + CALIBRATION_TIME;

	gyrocal.data_rate = SDL_GetGamepadSensorDataRate( g_current_gamepad, SDL_SENSOR_GYRO );
	if( !gyrocal.data_rate )
		gyrocal.data_rate = 10.0f;

	Con_Printf( S_NOTE "Starting gyroscope calibration at %g data rate for %g seconds...\n", gyrocal.data_rate, CALIBRATION_TIME );
}

static void SDLash_FinalizeCalibration( void )
{
	int min_samples = Q_rint( CALIBRATION_TIME * gyrocal.data_rate * 0.5f );

	// we waited for few seconds and got too few samples
	if( gyrocal.samples <= min_samples )
	{
		if( !gyrocal.continuous )
		{
			Joy_SetCalibrationState( JOY_FAILED_TO_CALIBRATE );
			Con_Printf( S_ERROR "Calibration failed, got samples %d < %d\n", gyrocal.samples, min_samples );
			gyrocal.time = 0.0f;
			return;
		}
	}
	else
	{
		VectorScale( gyrocal.data, 1.0f / gyrocal.samples, gyrocal.calibrated_values );
		Joy_SetCalibrationState( JOY_CALIBRATED );
		if( !gyrocal.continuous )
			Con_Printf( "Calibration done. Result: %f %f %f at %d samples\n", gyrocal.calibrated_values[0], gyrocal.calibrated_values[1], gyrocal.calibrated_values[2], gyrocal.samples );
		gyrocal.continuous = true;
	}

	// schedule next calibration window
	VectorClear( gyrocal.data );
	gyrocal.samples = 0;
	gyrocal.time = host.realtime + CALIBRATION_TIME;
}

static void SDLash_AccumulateCalibrationData( const float *data )
{
	// for continuous background calibration only listen for noise
	// by comparing it with calibrated values
	//
	// for first calibration this might be hurtful as device might
	// output offset data
	if( gyrocal.continuous )
	{
		vec3_t calibrated;
		VectorSubtract( data, gyrocal.calibrated_values, calibrated );

		if( VectorLength( calibrated ) > 0.1f )
			return;
	}

	VectorAdd( gyrocal.data, data, gyrocal.data );
	gyrocal.samples++;

	if( !gyrocal.continuous )
		Joy_SetCalibrationState( JOY_CALIBRATING );
}

static void SDLash_GamepadAddMappings( const char *name )
{
	fs_offset_t len = 0;
	byte *p = FS_LoadFile( name, &len, false );

	if( !p )
		return;

	if( len > 0 )
	{
		SDL_IOStream *io = SDL_IOFromConstMem( p, (size_t)len );

		if( io )
			SDL_AddGamepadMappingsFromIO( io, true );
	}

	Mem_Free( p );
}

static void SDLash_SetActiveGamepad( SDL_JoystickID id )
{
	if( g_current_gamepad_id == id )
		return;

	// going to change active controller, disable gyro events in old
	if( g_current_gamepad )
		SDL_SetGamepadSensorEnabled( g_current_gamepad, SDL_SENSOR_GYRO, false );

	g_current_gamepad_id = id;

	if( id == 0 )
	{
		g_current_gamepad = NULL;
		Joy_SetCapabilities( false );
		Joy_SetCalibrationState( JOY_NOT_CALIBRATED );
	}
	else
	{
		qboolean have_gyro;

		g_current_gamepad = SDL_GetGamepadFromID( id );

		have_gyro = SDL_GamepadHasSensor( g_current_gamepad, SDL_SENSOR_GYRO );

		if( have_gyro )
		{
			SDL_SetGamepadSensorEnabled( g_current_gamepad, SDL_SENSOR_GYRO, true );
			SDLash_RestartCalibration();
		}

		Joy_SetCapabilities( have_gyro );
	}
}

static void SDLash_GamepadAdded( SDL_JoystickID id )
{
	SDL_Gamepad *gc = SDL_OpenGamepad( id );
	if( !gc )
	{
		Con_PrintSDLError( "SDL_OpenGamepad" );
		return;
	}

	// this "game controller" only exists on Android within emulator and tries to map
	// keyboard events into game controller events, which as you can expect, doesn't
	// work and I don't understand the intention here. When debugging Xash in Android
	// Studio emulator, just enable hardware input passthrough.
#if XASH_ANDROID
	if( !Q_strcmp( SDL_GetGamepadName( gc ), "qwerty2" ))
	{
		SDL_CloseGamepad( gc );
		return;
	}
#endif // XASH_ANDROID

	SDL_Gamepad **list = Mem_Realloc( host.mempool, g_gamepads, sizeof( *list ) * ( g_num_gamepads + 1 ));
	list[g_num_gamepads++] = gc;

	g_gamepads = list;

	// set as current device if none other set
	if( g_current_gamepad_id == 0 )
		SDLash_SetActiveGamepad( id );

	char *mapping = SDL_GetGamepadMapping( gc );
	Con_Printf( "Detected \"%s\" game controller.\nMapping string: %s\n", SDL_GetGamepadName( gc ), mapping );
	SDL_free( mapping );
}

static void SDLash_GamepadRemoved( SDL_JoystickID id )
{
	if( id == g_current_gamepad_id )
		SDLash_SetActiveGamepad( 0 );

	// now close the device
	for( size_t i = 0; i < g_num_gamepads; i++ )
	{
		SDL_Gamepad *gc = g_gamepads[i];

		if( !gc )
			continue;

		if( SDL_GetGamepadID( gc ) == id )
		{
			Con_Printf( "Game controller \"%s\" was disconnected\n", SDL_GetGamepadName( gc ));

			SDL_CloseGamepad( gc );
			g_gamepads[i] = NULL;
		}
	}
}

static void SDLash_GamepadSensorUpdate( const SDL_GamepadSensorEvent *sensor )
{
	vec3_t data;

	if( sensor->which != g_current_gamepad_id )
		return;

	if( sensor->sensor != SDL_SENSOR_GYRO )
		return;

	if( gyrocal.time != 0.0f )
	{
		if( host.realtime > gyrocal.time )
			SDLash_FinalizeCalibration();
		else
			SDLash_AccumulateCalibrationData( sensor->data );

		// block gyro events only during initial calibration
		if( !gyrocal.continuous )
			return;
	}

	VectorSubtract( sensor->data, gyrocal.calibrated_values, data );
	Joy_GyroEvent( data );
}

void SDLash_HandleGamepadEvent( const SDL_Event *ev )
{
	int x;

	switch( ev->type )
	{
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		SDLash_SetActiveGamepad( ev->gaxis.which );
		x = ev->gaxis.axis;
		if( x >= 0 && x < ARRAYSIZE( g_axis_mapping ))
			Joy_AxisMotionEvent( g_axis_mapping[x], ev->gaxis.value );
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		SDLash_SetActiveGamepad( ev->gbutton.which );
		x = ev->gbutton.button;
		if( x >= 0 && x < ARRAYSIZE( g_button_mapping ))
			Key_Event( g_button_mapping[x], ev->gbutton.down );
		break;
	case SDL_EVENT_GAMEPAD_REMOVED:
		SDLash_GamepadRemoved( ev->gdevice.which );
		break;
	case SDL_EVENT_GAMEPAD_ADDED:
		SDLash_GamepadAdded( ev->gdevice.which );
		break;
	case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
		SDLash_GamepadSensorUpdate( &ev->gsensor );
		break;
	}
}

void Platform_CalibrateGamepadGyro( void )
{
	SDLash_RestartCalibration();
}

void Platform_Vibrate2( float time, int val1, int val2, uint flags )
{
	SDL_Gamepad *gc = g_current_gamepad;

	if( g_current_gamepad_id == 0 || !gc )
		return;

	if( val1 < 0 )
		val1 = COM_RandomLong( 0x7FFF, 0xFFFF );

	if( val2 < 0 )
		val2 = COM_RandomLong( 0x7FFF, 0xFFFF );

	Uint32 ms = (Uint32)ceil( time );
	SDL_RumbleGamepad( gc, val1, val2, ms );
}

/*
=============
Platform_Vibrate

=============
*/
void Platform_Vibrate( float time, char flags )
{
	Platform_Vibrate2( time, -1, -1, flags );
}

/*
=============
Platform_JoyInit

=============
*/
int Platform_JoyInit( void )
{
	Con_Reportf( "Joystick: SDL Gamepad API\n" );
	if( SDL_WasInit( SDL_INIT_GAMEPAD ) != SDL_INIT_GAMEPAD && !SDL_InitSubSystem( SDL_INIT_GAMEPAD ))
	{
		Con_Reportf( "Failed to initialize SDL Gamepad API: %s\n", SDL_GetError( ));
		return 0;
	}

	SDLash_GamepadAddMappings( "gamecontrollerdb.txt" ); // shipped in extras.pk3
	SDLash_GamepadAddMappings( "controllermappings.txt" );

	int count = 0;
	SDL_JoystickID *gamepads = SDL_GetGamepads( &count );
	SDL_free( gamepads );

	return count;
}

/*
=============
Platform_JoyShutdown

=============
*/
void Platform_JoyShutdown( void )
{
	SDLash_SetActiveGamepad( 0 );

	for( size_t i = 0; i < g_num_gamepads; i++ )
	{
		if( !g_gamepads[i] )
			continue;

		SDL_CloseGamepad( g_gamepads[i] );
		g_gamepads[i] = NULL;
	}

	Mem_Free( g_gamepads );
	g_gamepads = NULL;
	g_num_gamepads = 0;

	SDL_QuitSubSystem( SDL_INIT_GAMEPAD );
}
