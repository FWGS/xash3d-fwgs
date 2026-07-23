/*
sensor_sdl3.c - SDL3 sensor handling
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
#include "platform_sdl3.h"

static SDL_SensorID g_system_gyro_id = -1;
static SDL_Sensor   *g_system_gyro;

/*
==============
SDLash_InitSensors

==============
*/
void SDLash_InitSensors( void )
{
	if( SDL_InitSubSystem( SDL_INIT_SENSOR ) == 0 )
	{
		int num_sensors;
		SDL_SensorID *sensors = SDL_GetSensors( &num_sensors );

		for( int i = 0; i < num_sensors; i++ )
		{
			if( SDL_GetSensorTypeForID( i ) == SDL_SENSOR_GYRO )
			{
				g_system_gyro = SDL_OpenSensor( i );
				if( g_system_gyro )
				{
					g_system_gyro_id = sensors[i];
					Con_Printf( "SDL: Opened built-in gyroscope: %s\n", SDL_GetSensorName( g_system_gyro ));
					break;
				}
			}
		}
		SDL_free( sensors );
	}
	else
	{
		Con_Reportf( S_ERROR "Failed to init SDL Sensor subsystem: %s\n", SDL_GetError());
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
		SDL_CloseSensor( g_system_gyro );
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
	return( g_system_gyro != NULL );
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
