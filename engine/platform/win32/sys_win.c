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
#include <timeapi.h>
#include <bcrypt.h>

typedef NTSTATUS (NTAPI *pfnNtSetTimerResolution_t)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);
typedef NTSTATUS (NTAPI *pfnNtQueryTimerResolution_t)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

typedef struct {
	HANDLE hEvent;
	HANDLE hTimer;
	HANDLE hThread;
	HANDLE hMutex;
	DWORD iThreadID;
	double flInterval;
} timer_win32_t;

static timer_win32_t g_timer;

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

void Platform_Sleep( int msec )
{
	Sleep( msec );
}
#endif // XASH_TIMER == TIMER_WIN32

qboolean Sys_DebuggerPresent( void )
{
	return IsDebuggerPresent();
}

static void Sys_SetTimerHighResolution( void )
{	
	HMODULE hndl;
	pfnNtSetTimerResolution_t pNtSetTimerResolution;
	pfnNtQueryTimerResolution_t pNtQueryTimerResolution;
	
	hndl = GetModuleHandle( "ntdll.dll" );
	pNtSetTimerResolution = (pfnNtSetTimerResolution_t)GetProcAddress( hndl, "NtSetTimerResolution" );
	pNtQueryTimerResolution = (pfnNtQueryTimerResolution_t)GetProcAddress( hndl, "NtQueryTimerResolution" );

	if( pNtSetTimerResolution && pNtQueryTimerResolution )
	{
		// undocumented NT API functions, for some mystery reason works better than timeBeginPeriod
		ULONG min, max, cur;
		pNtQueryTimerResolution(&min, &max, &cur);
		pNtSetTimerResolution(max, 1, &cur);
	}
	else
	{
		// more conventional method to set timer resolution, using as fallback
		TIMECAPS tc;
		timeGetDevCaps( &tc, sizeof( TIMECAPS ));
		timeBeginPeriod( tc.wPeriodMin );
	}
}

static DWORD WINAPI Sys_TimerThread( LPVOID a )
{
	double oldtime = 0;
	LARGE_INTEGER delay;
	while (true)
	{
		WaitForSingleObject( g_timer.hMutex, INFINITE ); // lock mutex
		double realtime = Platform_DoubleTime();
		double delta = g_timer.flInterval - ( realtime - oldtime );
		oldtime = realtime;

		if (delta > 0)
			delta = 0;

		delay.QuadPart = -1 * 1e7 * ( g_timer.flInterval + delta );
		ReleaseMutex( g_timer.hMutex ); // unlock mutex

		SetWaitableTimer( g_timer.hTimer, &delay, 0, NULL, NULL, 0 );
		WaitForSingleObject( g_timer.hTimer, INFINITE );
		SetEvent( g_timer.hEvent );
	}
}

void Platform_Delay( double time )
{
	WaitForSingleObject( g_timer.hMutex, INFINITE ); // lock mutex
	g_timer.flInterval = time;
	ReleaseMutex( g_timer.hMutex ); // unlock mutex
	WaitForSingleObject( g_timer.hEvent, INFINITE ); // wait when event will be triggered
}

void Platform_TimerInit( void )
{
	Sys_SetTimerHighResolution();
	g_timer.hTimer = CreateWaitableTimer( NULL, TRUE, NULL );
	g_timer.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	g_timer.flInterval = 1.0 / 60.0;

	g_timer.hMutex = CreateMutex(
		NULL,              // default security attributes
		FALSE,             // initially not owned
		NULL);             // unnamed mutex

	g_timer.hThread = CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size
		Sys_TimerThread,		// thread function name
		0,						// argument to thread function
		0,                      // use default creation flags
		&g_timer.iThreadID);
}

void Platform_TimerShutdown( void )
{
}

void Platform_ShellExecute( const char *path, const char *parms )
{
	if( !Q_strcmp( path, GENERIC_UPDATE_PAGE ) || !Q_strcmp( path, PLATFORM_UPDATE_PAGE ))
		path = DEFAULT_UPDATE_PAGE;

	ShellExecute( NULL, "open", path, parms, NULL, SW_SHOW );
}

void Platform_UpdateStatusLine( void )
{
	int clientsCount;
	char szStatus[128];
	static double lastTime;

	if( host.type != HOST_DEDICATED )
		return;

	// update only every 1/2 seconds
	if(( sv.time - lastTime ) < 0.5f )
		return;

	clientsCount = SV_GetConnectedClientsCount( NULL );
	Q_snprintf( szStatus, sizeof( szStatus ) - 1, "%.1f fps %2i/%2i on %16s", 1.f / sv.frametime, clientsCount, svs.maxclients, host.game.levelName );
#ifdef XASH_WIN32
	Wcon_SetStatus( szStatus );
#endif
	lastTime = sv.time;
}

#if XASH_MESSAGEBOX == MSGBOX_WIN32
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	MessageBox( parentMainWindow ? host.hWnd : NULL, message, title, MB_OK|MB_SETFOREGROUND|MB_ICONSTOP );
}
#endif // XASH_MESSAGEBOX == MSGBOX_WIN32

#ifndef XASH_SDL

void Platform_Init( void )
{
	Wcon_CreateConsole(); // system console used by dedicated server or show fatal errors

}
void Platform_Shutdown( void )
{
	Wcon_DestroyConsole();
}
#endif
