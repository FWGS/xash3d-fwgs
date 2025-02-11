/*
sys_win.c - win32 system utils
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

#include "platform/platform.h"
#include "menu_int.h"
#include "server.h"
#include <shellapi.h>

HANDLE g_waitable_timer;

#if XASH_TIMER == TIMER_WIN32
double Platform_DoubleTime( void )
{
	static LARGE_INTEGER	g_PerformanceFrequency;
	static LARGE_INTEGER	g_ClockStart;
	LARGE_INTEGER		CurrentTime;

	if( !g_PerformanceFrequency.QuadPart )
	{
		QueryPerformanceFrequency( &g_PerformanceFrequency );
		QueryPerformanceCounter( &g_ClockStart );
	}
	QueryPerformanceCounter( &CurrentTime );

	return (double)( CurrentTime.QuadPart - g_ClockStart.QuadPart ) / (double)( g_PerformanceFrequency.QuadPart );
}
#endif // XASH_TIMER == TIMER_WIN32

void Win32_Init( void )
{
	HMODULE hModule = LoadLibrary( "kernel32.dll" );

	if( hModule )
	{
		HANDLE ( __stdcall *pfnCreateWaitableTimerExW)( LPSECURITY_ATTRIBUTES lpTimerAttributes, LPCWSTR lpTimerName, DWORD dwFlags, DWORD dwDesiredAccess );

		if(( pfnCreateWaitableTimerExW = GetProcAddress( hModule, "CreateWaitableTimerExW" )))
		{
			// CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
			g_waitable_timer = pfnCreateWaitableTimerExW( NULL, NULL, 0x1 | 0x2, 0 );
		}

		FreeLibrary( "kernel32.dll" );
	}

	if( !g_waitable_timer )
		g_waitable_timer = CreateWaitableTimer( NULL, TRUE, NULL );
}

void Win32_Shutdown( void )
{
	if( g_waitable_timer )
		CloseHandle( g_waitable_timer );
}

qboolean Platform_DebuggerPresent( void )
{
	return IsDebuggerPresent();
}

void Platform_ShellExecute( const char *path, const char *parms )
{
	if( !Q_strcmp( path, GENERIC_UPDATE_PAGE ) || !Q_strcmp( path, PLATFORM_UPDATE_PAGE ))
		path = DEFAULT_UPDATE_PAGE;

	ShellExecute( NULL, "open", path, parms, NULL, SW_SHOW );
}

#if XASH_MESSAGEBOX == MSGBOX_WIN32
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	MessageBox( parentMainWindow ? host.hWnd : NULL, message, title, MB_OK|MB_SETFOREGROUND|MB_ICONSTOP );
}
#endif // XASH_MESSAGEBOX == MSGBOX_WIN32

