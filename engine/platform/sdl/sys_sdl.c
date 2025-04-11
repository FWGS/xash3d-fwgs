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

#if XASH_SDL == 3
// Officially recommended method of using SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include "platform/platform.h"
#include "events.h"

#if SDL_MAJOR_VERSION >= 3
// SDL3 moved to booleans, no more weird code with != 0 or < 0
#define SDL_SUCCESS(expr) (expr)
#else
#define SDL_SUCCESS(expr) ((expr) == 0)
#endif

#if XASH_TIMER == TIMER_SDL
double Platform_DoubleTime( void )
{
	static Uint64 g_PerformanceFrequency;
	static Uint64 g_ClockStart;
	Uint64 CurrentTime;

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

static const char *SDLash_CategoryToString( int category )
{
	switch( category )
	{
	case SDL_LOG_CATEGORY_APPLICATION: return "App";
	case SDL_LOG_CATEGORY_ERROR: return "Error";
	case SDL_LOG_CATEGORY_ASSERT: return "Assert";
	case SDL_LOG_CATEGORY_SYSTEM: return "System";
	case SDL_LOG_CATEGORY_AUDIO: return "Audio";
	case SDL_LOG_CATEGORY_VIDEO: return "Video";
	case SDL_LOG_CATEGORY_RENDER: return "Render";
	case SDL_LOG_CATEGORY_INPUT: return "Input";
	case SDL_LOG_CATEGORY_TEST: return "Test";
	default: return "Unknown";
	}
}

static void SDLCALL SDLash_LogOutputFunction( void *userdata, int category, SDL_LogPriority priority, const char *message )
{
	switch( priority )
	{
	case SDL_LOG_PRIORITY_CRITICAL:
	case SDL_LOG_PRIORITY_ERROR:
		Con_Printf( S_ERROR S_BLUE "SDL" S_DEFAULT ": [%s] %s\n", SDLash_CategoryToString( category ), message );
		break;
	case SDL_LOG_PRIORITY_WARN:
		Con_DPrintf( S_WARN S_BLUE "SDL" S_DEFAULT ": [%s] %s\n", SDLash_CategoryToString( category ), message );
		break;
	case SDL_LOG_PRIORITY_INFO:
		Con_Reportf( S_NOTE S_BLUE "SDL" S_DEFAULT ": [%s] %s\n", SDLash_CategoryToString( category ), message );
		break;
	default:
		Con_Reportf( S_BLUE "SDL" S_DEFAULT ": [%s] %s\n", SDLash_CategoryToString( category ), message );
		break;
	}
}

void SDLash_Init( const char *basedir )
{
#if XASH_APPLE
	char *path = SDL_GetBasePath();
	if( path != NULL )
	{
		char buf[MAX_VA_STRING];

		Q_snprintf( buf, sizeof( buf ), "%s%s/extras.pk3", path, basedir );
		setenv( "XASH3D_EXTRAS_PAK1", buf, true );
	}
#endif

#if SDL_MAJOR_VERSION >= 3 // Function renames
	SDL_SetLogOutputFunction( SDLash_LogOutputFunction, NULL );

	if( host_developer.value >= 2 )
		SDL_SetLogPriorities( SDL_LOG_PRIORITY_VERBOSE );
	else if( host_developer.value >= 1 )
		SDL_SetLogPriorities( SDL_LOG_PRIORITY_WARN );
	else
		SDL_SetLogPriorities( SDL_LOG_PRIORITY_ERROR );
#else
	SDL_LogSetOutputFunction( SDLash_LogOutputFunction, NULL );

	if( host_developer.value >= 2 )
		SDL_LogSetAllPriority( SDL_LOG_PRIORITY_VERBOSE );
	else if( host_developer.value >= 1 )
		SDL_LogSetAllPriority( SDL_LOG_PRIORITY_WARN );
	else
		SDL_LogSetAllPriority( SDL_LOG_PRIORITY_ERROR );
#endif

#ifndef SDL_INIT_EVENTS
#define SDL_INIT_EVENTS 0
#endif
#ifndef SDL_INIT_TIMER // No longer needed since SDL3
#define SDL_INIT_TIMER 0
#endif
	if( !SDL_SUCCESS(SDL_Init( SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS )) )
	{
		Sys_Warn( "SDL_Init failed: %s", SDL_GetError() );
		host.type = HOST_DEDICATED;
	}

#if SDL_MAJOR_VERSION == 2
	SDL_SetHint( SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0" );
	SDL_StopTextInput();
#endif
#if SDL_MAJOR_VERSION >= 2
	SDL_SetHint( SDL_HINT_MOUSE_TOUCH_EVENTS, "0" );
	SDL_SetHint( SDL_HINT_TOUCH_MOUSE_EVENTS, "0" );
#endif // XASH_SDL == 2

	SDLash_InitCursors();
}

void SDLash_Shutdown( void )
{
	SDLash_FreeCursors();

	SDL_Quit();
}
