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

void Platform_ShellExecute( const char *path, const char *parms )
{
	if( !Q_strcmp( path, GENERIC_UPDATE_PAGE ) || !Q_strcmp( path, PLATFORM_UPDATE_PAGE ))
		path = DEFAULT_UPDATE_PAGE;

	ShellExecute( NULL, "open", path, parms, NULL, SW_SHOW );
}

void Platform_UpdateStatusLine( void )
{
	int clientsCount, botsCountUnused;
	char szStatus[128];
	static double lastTime;

	if( host.type != HOST_DEDICATED )
		return;

	// update only every 1/2 seconds
	if(( sv.time - lastTime ) < 0.5f )
		return;

	SV_GetPlayerCount( &clientsCount, &botsCountUnused );
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
