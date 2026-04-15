/*
sensor_sdl2.c - SDL2 sensor handling
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

#include <SDL.h>
#include "common.h"
#include "input.h"
#include "platform_sdl2.h"

#if SDL_VERSION_ATLEAST( 2, 0, 14 )
static SDL_SensorID g_system_gyro_id = -1;
static SDL_Sensor *g_system_gyro;

/*
==============
SDLash_InitSensors

==============
*/
void SDLash_InitSensors( void )
{
	if( SDL_InitSubSystem( SDL_INIT_SENSOR ) == 0 )
	{
		int num_sensors = SDL_NumSensors();

		for( int i = 0; i < num_sensors; i++ )
		{
			if( SDL_SensorGetDeviceType( i ) == SDL_SENSOR_GYRO )
			{
				g_system_gyro = SDL_SensorOpen( i );
				if( g_system_gyro )
				{
					g_system_gyro_id = SDL_SensorGetInstanceID( g_system_gyro );
					Con_Printf( "SDL: Opened built-in gyroscope: %s\n", SDL_SensorGetName( g_system_gyro ) );
					break;
				}
			}
		}
	}
	else
	{
		Con_Reportf( S_ERROR "Failed to init SDL Sensor subsystem: %s\n", SDL_GetError() );
	}
}

/*
==============
SDLash_ShutdownSensors

==============
*/
void SDLash_ShutdownSensors( void )
{
	if( g_system_gyro )
	{
		SDL_SensorClose( g_system_gyro );
		g_system_gyro = NULL;
		g_system_gyro_id = -1;
	}

	SDL_QuitSubSystem( SDL_INIT_SENSOR );
}

/*
==============
SDLash_GyroIsAvailable

==============
*/
qboolean SDLash_GyroIsAvailable( void )
{
	return ( g_system_gyro != NULL );
}

/*
==============
SDLash_SensorUpdate

==============
*/
void SDLash_SensorUpdate( SDL_SensorEvent sensor )
{
	if( sensor.which == g_system_gyro_id )
	{
		IN_GyroEvent( sensor.data );
	}
}
#else
void SDLash_InitSensors( void ) { }
void SDLash_ShutdownSensors( void ) { }
qboolean SDLash_GyroIsAvailable( void ) { return false; }
#endif
