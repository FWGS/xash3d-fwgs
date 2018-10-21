/*
vid_sdl.c - SDL input component
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef XASH_DEDICATED
#include <SDL.h>

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "events.h"
#include "sound.h"
#include "vid_common.h"
#include "gl_local.h"

static SDL_Joystick *joy;
static SDL_GameController *gamecontroller;

/*
=============
Platform_GetMousePos

=============
*/
void Platform_GetMousePos( int *x, int *y )
{
	SDL_GetMouseState( x, y );
}

/*
=============
Platform_SetMousePos

============
*/
void Platform_SetMousePos( int x, int y )
{
	SDL_WarpMouseInWindow( host.hWnd, x, y );
}

/*
=============
Platform_Vibrate

=============
*/
void Platform_Vibrate( float time, char flags )
{
	// stub
}

/*
=============
SDLash_EnableTextInput

=============
*/
void Platform_EnableTextInput( qboolean enable )
{
	enable ? SDL_StartTextInput() : SDL_StopTextInput();
}

/*
=============
SDLash_JoyInit_Old

=============
*/
static int SDLash_JoyInit_Old( int numjoy )
{
	int num;
	int i;

	MsgDev( D_INFO, "Joystick: SDL\n" );

	if( SDL_WasInit( SDL_INIT_JOYSTICK ) != SDL_INIT_JOYSTICK &&
		SDL_InitSubSystem( SDL_INIT_JOYSTICK ) )
	{
		MsgDev( D_INFO, "Failed to initialize SDL Joysitck: %s\n", SDL_GetError() );
		return 0;
	}

	if( joy )
	{
		SDL_JoystickClose( joy );
	}

	num = SDL_NumJoysticks();

	if( num > 0 )
		MsgDev( D_INFO, "%i joysticks found:\n", num );
	else
	{
		MsgDev( D_INFO, "No joystick found.\n" );
		return 0;
	}

	for( i = 0; i < num; i++ )
		MsgDev( D_INFO, "%i\t: %s\n", i, SDL_JoystickNameForIndex( i ) );

	MsgDev( D_INFO, "Pass +set joy_index N to command line, where N is number, to select active joystick\n" );

	joy = SDL_JoystickOpen( numjoy );

	if( !joy )
	{
		MsgDev( D_INFO, "Failed to select joystick: %s\n", SDL_GetError( ) );
		return 0;
	}

	MsgDev( D_INFO, "Selected joystick: %s\n"
		"\tAxes: %i\n"
		"\tHats: %i\n"
		"\tButtons: %i\n"
		"\tBalls: %i\n",
		SDL_JoystickName( joy ), SDL_JoystickNumAxes( joy ), SDL_JoystickNumHats( joy ),
		SDL_JoystickNumButtons( joy ), SDL_JoystickNumBalls( joy ) );

	SDL_GameControllerEventState( SDL_DISABLE );
	SDL_JoystickEventState( SDL_ENABLE );

	return num;
}

/*
=============
SDLash_JoyInit_New

=============
*/
static int SDLash_JoyInit_New( int numjoy )
{
	int temp, num;
	int i;

	MsgDev( D_INFO, "Joystick: SDL GameController API\n" );

	if( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) != SDL_INIT_GAMECONTROLLER &&
		SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ) )
	{
		MsgDev( D_INFO, "Failed to initialize SDL GameController API: %s\n", SDL_GetError() );
		return 0;
	}

	// chance to add mappings from file
	SDL_GameControllerAddMappingsFromFile( "controllermappings.txt" );

	if( gamecontroller )
	{
		SDL_GameControllerClose( gamecontroller );
	}

	temp = SDL_NumJoysticks();
	num = 0;

	for( i = 0; i < temp; i++ )
	{
		if( SDL_IsGameController( i ))
			num++;
	}

	if( num > 0 )
		MsgDev( D_INFO, "%i joysticks found:\n", num );
	else
	{
		MsgDev( D_INFO, "No joystick found.\n" );
		return 0;
	}

	for( i = 0; i < num; i++ )
		MsgDev( D_INFO, "%i\t: %s\n", i, SDL_GameControllerNameForIndex( i ) );

	MsgDev( D_INFO, "Pass +set joy_index N to command line, where N is number, to select active joystick\n" );

	gamecontroller = SDL_GameControllerOpen( numjoy );

	if( !gamecontroller )
	{
		MsgDev( D_INFO, "Failed to select joystick: %s\n", SDL_GetError( ) );
		return 0;
	}
// was added in SDL2-2.0.6, allow build with earlier versions just in case
#if SDL_MAJOR_VERSION > 2 || SDL_MINOR_VERSION > 0 || SDL_PATCHLEVEL >= 6
	MsgDev( D_INFO, "Selected joystick: %s (%i:%i:%i)\n",
		SDL_GameControllerName( gamecontroller ),
		SDL_GameControllerGetVendor( gamecontroller ),
		SDL_GameControllerGetProduct( gamecontroller ),
		SDL_GameControllerGetProductVersion( gamecontroller ));
#endif
	SDL_GameControllerEventState( SDL_ENABLE );
	SDL_JoystickEventState( SDL_DISABLE );

	return num;
}

/*
=============
Platform_JoyInit

=============
*/
int Platform_JoyInit( int numjoy )
{
	// SDL_Joystick is now an old API
	// SDL_GameController is preferred
	if( Sys_CheckParm( "-sdl_joy_old_api" ) )
		return SDLash_JoyInit_Old(numjoy);

	return SDLash_JoyInit_New(numjoy);
}

#endif // XASH_DEDICATED
