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

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "events.h"

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
// LeftX, LeftY, RightX, RightY, TriggerRight, TriggerLeft
static const engineAxis_t g_axis_mapping[] =
{
	JOY_AXIS_SIDE, // SDL_CONTROLLER_AXIS_LEFTX,
	JOY_AXIS_FWD, // SDL_CONTROLLER_AXIS_LEFTY,
	JOY_AXIS_PITCH, // SDL_CONTROLLER_AXIS_RIGHTX,
	JOY_AXIS_YAW, // SDL_CONTROLLER_AXIS_RIGHTY,
	JOY_AXIS_LT, // SDL_CONTROLLER_AXIS_TRIGGERLEFT,
	JOY_AXIS_RT, // SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
};

static SDL_JoystickID g_current_gamepad_id = -1; // used to send rumble to
static SDL_GameController *g_current_gamepad;
static SDL_GameController **g_gamepads;
static size_t g_num_gamepads;

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
	g_current_gamepad_id = id;
	g_current_gamepad = SDL_GameControllerFromInstanceID( id );
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
}

static void SDLash_GameControllerRemoved( SDL_JoystickID id )
{
	size_t i;

	if( id == g_current_gamepad_id )
	{
		g_current_gamepad_id = -1;
		g_current_gamepad = NULL;
	}

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
			SDL_GameControllerClose( gc );
			g_gamepads[i] = NULL;
		}
	}
}

/*
=============
SDLash_JoyInit

=============
*/
static int SDLash_JoyInit( void )
{
	int count, numJoysticks, i;

	Con_Reportf( "Joystick: SDL GameController API\n" );
	if( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) != SDL_INIT_GAMECONTROLLER &&
		SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ))
	{
		Con_Reportf( "Failed to initialize SDL GameController API: %s\n", SDL_GetError() );
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
	}
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
	return SDLash_JoyInit();
}

/*
=============
Platform_JoyShutdown

=============
*/
void Platform_JoyShutdown( void )
{
	size_t i;

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

	g_current_gamepad = NULL;
	g_current_gamepad_id = -1;

	SDL_QuitSubSystem( SDL_INIT_GAMECONTROLLER );
}
#else // SDL_VERSION_ATLEAST( 2, 0, 0 )
void Platform_Vibrate( float time, char flags )
{

}

int Platform_JoyInit( void )
{
	return 0;
}

void Platform_JoyShutdown( void )
{

}
#endif
