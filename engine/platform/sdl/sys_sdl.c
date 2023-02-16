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
#include "events.h"

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

#if XASH_MESSAGEBOX == MSGBOX_SDL
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, title, message, parentMainWindow ? host.hWnd : NULL );
}
#endif // XASH_MESSAGEBOX == MSGBOX_SDL
void Posix_Daemonize( void );
void Platform_Init( void )
{
#ifndef SDL_INIT_EVENTS
#define SDL_INIT_EVENTS 0
#endif

	if( SDL_Init( SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS ) )
	{
		Sys_Warn( "SDL_Init failed: %s", SDL_GetError() );
		host.type = HOST_DEDICATED;
	}

#if XASH_SDL == 2
	SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
	SDL_StopTextInput();
#endif // XASH_SDL == 2

#if XASH_NSWITCH
	NSwitch_Init();
#elif XASH_WIN32
	Wcon_CreateConsole(); // system console used by dedicated server or show fatal errors
#elif XASH_POSIX
	Posix_Daemonize();
#endif

	SDLash_InitCursors();
}

void Platform_Shutdown( void )
{
	SDLash_FreeCursors();

#if XASH_NSWITCH
	NSwitch_Shutdown();
#elif XASH_WIN32
	Wcon_DestroyConsole();
#endif
}
