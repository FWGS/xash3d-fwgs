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

#include "platform_sdl3.h"

void Platform_Vibrate( float time, char flags )
{
	Platform_Vibrate2( time, -1, -1, flags );
}

void Platform_Vibrate2( float time, int val1, int val2, uint flags )
{
	// TODO
}

int Platform_JoyInit( void )
{
	// TODO
	return 0;
}

void Platform_JoyShutdown( void )
{
	// TODO
}

void Platform_CalibrateGamepadGyro( void )
{
	// TODO
}
