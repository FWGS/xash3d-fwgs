/*
sys_sdl.c - SDL2 system utils
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

#include <SDL.h>
#include "platform/platform.h"

#if XASH_TIMER == TIMER_SDL
double Platform_DoubleTime( void )
{
	static longtime_t g_PerformanceFrequency;
	static longtime_t g_ClockStart;
	longtime_t CurrentTime;

	if( !g_PerformanceFrequency )
	{
		g_PerformanceFrequency = SDL_GetPerformanceFrequency();
		g_ClockStart = SDL_GetPerformanceCounter();
	}
	CurrentTime = SDL_GetPerformanceCounter();
	return (double)( CurrentTime - g_ClockStart ) / (double)( g_PerformanceFrequency );
}

void Platform_Sleep( int msec )
{
	SDL_Delay( msec );
}
#endif // XASH_TIMER == TIMER_SDL

void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, title, message, parentMainWindow ? host.hWnd : NULL );
}
