/*
platform_sdl3.h - SDL3 platform definitions
Copyright (C) 2025 Er2off
Copyright (C) 2025 Alibek Omarov

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
#include "common.h"

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

		return 0.0;
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
#endif

void SDLash_NanoSleep( int nsec )
{
	SDL_DelayNS( nsec );
}

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
	case SDL_LOG_CATEGORY_GPU: return "GPU";
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
	SDL_SetLogOutputFunction( SDLash_LogOutputFunction, NULL );

	if( host_developer.value >= 2 )
		SDL_SetLogPriorities( SDL_LOG_PRIORITY_VERBOSE );
	else if( host_developer.value >= 1 )
		SDL_SetLogPriorities( SDL_LOG_PRIORITY_WARN );
	else
		SDL_SetLogPriorities( SDL_LOG_PRIORITY_ERROR );

	if( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS ))
	{
		Sys_Warn( "SDL_Init failed: %s", SDL_GetError( ));
		host.type = HOST_DEDICATED;
	}

	SDL_SetHint( SDL_HINT_MOUSE_TOUCH_EVENTS, "0" );
	SDL_SetHint( SDL_HINT_TOUCH_MOUSE_EVENTS, "0" );

	SDLash_InitCursors();
}

void SDLash_Shutdown( void )
{
	SDLash_FreeCursors();
	SDL_Quit();
}
