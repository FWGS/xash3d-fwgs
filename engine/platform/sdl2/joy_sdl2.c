/*
joy_sdl.c - SDL gamepads
Copyright (C) 2018-2025 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <SDL.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "platform_sdl2.h"

static const int g_button_mapping[] =
{
#if XASH_NSWITCH // devkitPro/SDL has inverted Nintendo layout for SDL_GameController
	K_B_BUTTON, K_A_BUTTON, K_Y_BUTTON, K_X_BUTTON,
#else
	K_A_BUTTON, K_B_BUTTON, K_X_BUTTON, K_Y_BUTTON,
#endif
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
	JOY_AXIS_SIDE, // SDL_CONTROLLER_AXIS_LEFTX,
	JOY_AXIS_FWD, // SDL_CONTROLLER_AXIS_LEFTY,
	JOY_AXIS_YAW, // SDL_CONTROLLER_AXIS_RIGHTX,
	JOY_AXIS_PITCH, // SDL_CONTROLLER_AXIS_RIGHTY,
	JOY_AXIS_LT, // SDL_CONTROLLER_AXIS_TRIGGERLEFT,
	JOY_AXIS_RT, // SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
};

static SDL_JoystickID g_current_gamepad_id = -1; // used to send rumble to
static SDL_GameController *g_current_gamepad;
static SDL_GameController **g_gamepads;
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

#if SDL_VERSION_ATLEAST( 2, 0, 16 )
	gyrocal.data_rate = SDL_GameControllerGetSensorDataRate( g_current_gamepad, SDL_SENSOR_GYRO );
#endif
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
		// Con_Reportf( S_WARN "Continuous calibration: too few samples (%d < %d), retrying\n", gyrocal.samples, min_samples );
	}
	else
	{
		VectorScale( gyrocal.data, 1.0f / gyrocal.samples, gyrocal.calibrated_values );
		Joy_SetCalibrationState( JOY_CALIBRATED );
		if( !gyrocal.continuous )
			Con_Printf( "Calibration done. Result: %f %f %f at %d samples\n", gyrocal.calibrated_values[0], gyrocal.calibrated_values[1], gyrocal.calibrated_values[2], gyrocal.samples );
		// else
		//	Con_Reportf( "Continuous calibration done. Result: %f %f %f at %d samples\n", gyrocal.calibrated_values[0], gyrocal.calibrated_values[1], gyrocal.calibrated_values[2], gyrocal.samples );
		gyrocal.continuous = true;
	}

	// schedule next calibration window
	VectorClear( gyrocal.data );
	gyrocal.samples = 0;
	gyrocal.time = host.realtime + CALIBRATION_TIME;
}

static void SDLash_AccumulateCalibrationData( vec3_t data )
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

static void SDLash_GameControllerAddMappings( const char *name )
{
	fs_offset_t len = 0;
	byte *p = FS_LoadFile( name, &len, false );

	if( !p )
		return;

	if( len > 0 && len < INT32_MAX ) // function accepts int, SDL3 fixes this
	{
		SDL_RWops *rwops = SDL_RWFromConstMem( p, (int)len );
		SDL_GameControllerAddMappingsFromRW( rwops, true );
	}

	Mem_Free( p );
}

static void SDLash_SetActiveGameController( SDL_JoystickID id )
{
	if( g_current_gamepad_id == id )
		return;

#if SDL_VERSION_ATLEAST( 2, 0, 14 )
	// going to change active controller, disable gyro events in old
	if( g_current_gamepad )
		SDL_GameControllerSetSensorEnabled( g_current_gamepad, SDL_SENSOR_GYRO, SDL_FALSE );
#endif // SDL_VERSION_ATLEAST( 2, 0, 14 )

	g_current_gamepad_id = id;

	if( id < 0 )
	{
		g_current_gamepad = NULL;
		Joy_SetCapabilities( false );
		Joy_SetCalibrationState( JOY_NOT_CALIBRATED );
	}
	else
	{
		qboolean have_gyro = false;

		g_current_gamepad = SDL_GameControllerFromInstanceID( id );

#if SDL_VERSION_ATLEAST( 2, 0, 14 )
		have_gyro = SDL_GameControllerHasSensor( g_current_gamepad, SDL_SENSOR_GYRO );

		if( have_gyro )
		{
			SDL_GameControllerSetSensorEnabled( g_current_gamepad, SDL_SENSOR_GYRO, SDL_TRUE );
			SDLash_RestartCalibration();
		}
#endif // SDL_VERSION_ATLEAST( 2, 0, 14 )

		Joy_SetCapabilities( have_gyro );
	}
}

static void SDLash_GameControllerAdded( int device_index )
{
	SDL_GameController *gc;
	SDL_GameController **list;

	gc = SDL_GameControllerOpen( device_index );
	if( !gc )
	{
		Con_Printf( S_ERROR "SDL_GameControllerOpen: %s\n", SDL_GetError( ));
		return;
	}

	// this "game controller" only exists on Android within emulator and tries to map
	// keyboard events into game controller events, which as you can expect, doesn't
	// work and I don't understand the intention here. When debugging Xash in Android
	// Studio emulator, just enable hardware input passthrough.
#if XASH_ANDROID
	if( !Q_strcmp( SDL_GameControllerName( gc ), "qwerty2" ))
	{
		SDL_GameControllerClose( gc );
		return;
	}
#endif // XASH_ANDROID

	list = Mem_Realloc( host.mempool, g_gamepads, sizeof( *list ) * ( g_num_gamepads + 1 ));
	list[g_num_gamepads++] = gc;

	g_gamepads = list;

	// set as current device if none other set
	if( g_current_gamepad_id < 0 )
	{
		SDL_Joystick *joy = SDL_GameControllerGetJoystick( gc );

		if( joy )
			SDLash_SetActiveGameController( SDL_JoystickInstanceID( joy ));
	}

	Con_Printf( "Detected \"%s\" game controller.\nMapping string: %s\n", SDL_GameControllerName( gc ), SDL_GameControllerMapping( gc ));
}

static void SDLash_GameControllerRemoved( SDL_JoystickID id )
{
	size_t i;

	if( id == g_current_gamepad_id )
		SDLash_SetActiveGameController( -1 );

	// now close the device
	for( i = 0; i < g_num_gamepads; i++ )
	{
		SDL_GameController *gc = g_gamepads[i];
		SDL_Joystick *joy;

		if( !gc )
			continue;

		joy = SDL_GameControllerGetJoystick( gc );

		if( !joy )
			continue;

		if( SDL_JoystickInstanceID( joy ) == id )
		{
			Con_Printf( "Game controller \"%s\" was disconnected\n", SDL_GameControllerName( gc ));

			SDL_GameControllerClose( gc );
			g_gamepads[i] = NULL;
		}
	}
}

#if SDL_VERSION_ATLEAST( 2, 0, 14 )
static void SDLash_GameControllerSensorUpdate( SDL_ControllerSensorEvent sensor )
{
	vec3_t data;

	if( sensor.which != g_current_gamepad_id )
		return;

	if( sensor.sensor != SDL_SENSOR_GYRO )
		return;

	if( gyrocal.time != 0.0f )
	{
		if( host.realtime > gyrocal.time )
			SDLash_FinalizeCalibration();
		else
			SDLash_AccumulateCalibrationData( sensor.data );

		// block gyro events only during initial calibration
		if( !gyrocal.continuous )
			return;
	}

	VectorSubtract( sensor.data, gyrocal.calibrated_values, data );
	Joy_GyroEvent( data );
}
#endif

void SDLash_HandleGameControllerEvent( SDL_Event *ev )
{
	int x;

	switch( ev->type )
	{
	case SDL_CONTROLLERAXISMOTION:
		SDLash_SetActiveGameController( ev->caxis.which );
		x = ev->caxis.axis;
		if( x >= 0 && x < ARRAYSIZE( g_axis_mapping ))
			Joy_AxisMotionEvent( g_axis_mapping[x], ev->caxis.value );
		break;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		SDLash_SetActiveGameController( ev->cbutton.which );
		x = ev->cbutton.button;
		if( x >= 0 && x < ARRAYSIZE( g_button_mapping ))
			Key_Event( g_button_mapping[x], ev->cbutton.state );
		break;
	case SDL_CONTROLLERDEVICEREMOVED:
		SDLash_GameControllerRemoved( ev->cdevice.which );
		break;
	case SDL_CONTROLLERDEVICEADDED:
		SDLash_GameControllerAdded( ev->cdevice.which );
		break;
#if SDL_VERSION_ATLEAST( 2, 0, 14 )
	case SDL_CONTROLLERSENSORUPDATE:
		SDLash_GameControllerSensorUpdate( ev->csensor );
		break;
#endif
	}
}

void Platform_CalibrateGamepadGyro( void )
{
	SDLash_RestartCalibration();
}

void Platform_Vibrate2( float time, int val1, int val2, uint flags )
{
#if SDL_VERSION_ATLEAST( 2, 0, 9 )
	SDL_GameController *gc = g_current_gamepad;
	Uint32 ms;

	if( g_current_gamepad_id < 0 || !gc )
		return;

	if( val1 < 0 )
		val1 = COM_RandomLong( 0x7FFF, 0xFFFF );

	if( val2 < 0 )
		val2 = COM_RandomLong( 0x7FFF, 0xFFFF );

	ms = (Uint32)ceil( time );
	SDL_GameControllerRumble( gc, val1, val2, ms );
#endif // SDL_VERSION_ATLEAST( 2, 0, 9 )
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
	int count, numJoysticks, i;

	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1" );

	Con_Reportf( "Joystick: SDL GameController API\n" );
	if( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) != SDL_INIT_GAMECONTROLLER &&
		SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ))
	{
		Con_Reportf( "Failed to initialize SDL GameController API: %s\n", SDL_GetError( ));
		return 0;
	}

	SDLash_GameControllerAddMappings( "gamecontrollerdb.txt" ); // shipped in extras.pk3
	SDLash_GameControllerAddMappings( "controllermappings.txt" );

	count = 0;
	numJoysticks = SDL_NumJoysticks();
	for ( i = 0; i < numJoysticks; i++ )
	{
		if( SDL_IsGameController( i ))
			++count;
	}

	return count;
}

/*
=============
Platform_JoyShutdown

=============
*/
void Platform_JoyShutdown( void )
{
	size_t i;

	SDLash_SetActiveGameController( -1 );

	for( i = 0; i < g_num_gamepads; i++ )
	{
		if( !g_gamepads[i] )
			continue;

		SDL_GameControllerClose( g_gamepads[i] );
		g_gamepads[i] = NULL;
	}

	Mem_Free( g_gamepads );
	g_gamepads = NULL;
	g_num_gamepads = 0;

	SDL_QuitSubSystem( SDL_INIT_GAMECONTROLLER );
}
