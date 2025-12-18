/*
host_sdl3.c - SDL3 host
Copyright (C) 2025 Xash3D FWGS contributors

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

void Platform_RunEvents( void )
{
	SDL_Event ev;

	while( host.status != HOST_CRASHED && !host.shutdown_issued && SDL_PollEvent( &ev ))
	{
		Con_Printf( "SDL3 EVENT: 0x%x\n", ev.type );
	}
}

void Platform_PreCreateMove( void )
{
	// TODO
}
