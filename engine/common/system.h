/*
system.h - platform dependent code
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef SYSTEM_H
#define SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "port.h"

#include <setjmp.h>
#include <stdio.h>
#include <time.h>

#ifdef XASH_SDL
#include <SDL_messagebox.h>

#define MSGBOX( x )		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Xash Error", x, NULL )
#define MSGBOX2( x )	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Host Error", x, host.hWnd )
#define MSGBOX3( x )	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Host Recursive Error", x, host.hWnd )
#elif defined(__ANDROID__) && !defined(XASH_DEDICATED)
#define MSGBOX( x ) 	Android_MessageBox( "Xash Error", x )
#define MSGBOX2( x )	Android_MessageBox( "Host Error", x )
#define MSGBOX3( x )	Android_MessageBox( "Host Recursive Error", x )
#elif defined _WIN32
#define MSGBOX( x ) 	MessageBox( NULL, x, "Xash Error", MB_OK|MB_SETFOREGROUND|MB_ICONSTOP )
#define MSGBOX2( x )	MessageBox( host.hWnd, x, "Host Error", MB_OK|MB_SETFOREGROUND|MB_ICONSTOP )
#define MSGBOX3( x )	MessageBox( host.hWnd, x, "Host Recursive Error", MB_OK|MB_SETFOREGROUND|MB_ICONSTOP )
#else
#define BORDER1 "======================================\n"
#define MSGBOX( x )		fprintf( stderr, BORDER1 "Xash Error: %s\n" BORDER1, x )
#define MSGBOX2( x )	fprintf( stderr, BORDER1 "Host Error: %s\n" BORDER1, x )
#define MSGBOX3( x )	fprintf( stderr, BORDER1 "Host Recursive Error: %s\n" BORDER1, x )
#endif

#include "xash3d_types.h"

#include "const.h"

#define ASSERT( exp )	if(!( exp )) Sys_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )

/*
========================================================================
internal dll's loader

two main types - native dlls and other win32 libraries will be recognized automatically
NOTE: never change this structure because all dll descriptions in xash code
writes into struct by offsets not names
========================================================================
*/
typedef struct dllfunc_s
{
	const char	*name;
	void		**func;
} dllfunc_t;

typedef struct dll_info_s
{
	const char	*name;	// name of library
	const dllfunc_t	*fcts;	// list of dll exports	
	qboolean		crash;	// crash if dll not found
	void		*link;	// hinstance of loading library
} dll_info_t;

void Sys_Sleep( int msec );
double Sys_DoubleTime( void );
char *Sys_GetClipboardData( void );
char *Sys_GetCurrentUser( void );
int Sys_CheckParm( const char *parm );
void Sys_Warn( const char *format, ... );
void Sys_Error( const char *error, ... );
qboolean Sys_LoadLibrary( dll_info_t *dll );
void* Sys_GetProcAddress( dll_info_t *dll, const char* name );
qboolean Sys_FreeLibrary( dll_info_t *dll );
void Sys_ParseCommandLine( int argc, const char **argv );
void Sys_MergeCommandLine( void );
void Sys_SetupCrashHandler( void );
void Sys_RestoreCrashHandler( void );
void Sys_SetClipboardData( const byte *buffer, size_t size );
#define Sys_GetParmFromCmdLine( parm, out ) _Sys_GetParmFromCmdLine( parm, out, sizeof( out ))
qboolean _Sys_GetParmFromCmdLine( char *parm, char *out, size_t size );
void Sys_ShellExecute( const char *path, const char *parms, qboolean exit );
void Sys_SendKeyEvents( void );
void Sys_Print( const char *pMsg );
void Sys_PrintLog( const char *pMsg );
void Sys_InitLog( void );
void Sys_CloseLog( void );
void Sys_Quit( void );

//
// sys_con.c
//
char *Sys_Input( void );
void Sys_DestroyConsole( void );
void Sys_CloseLog( void );
void Sys_InitLog( void );
void Sys_PrintLog( const char *pMsg );
int Sys_LogFileNo( void );

//
// con_win.c
//
#ifdef _WIN32
void Wcon_ShowConsole( qboolean show );
void Wcon_Print( const char *pMsg );
void Wcon_Init( void );
void Wcon_CreateConsole( void );
void Wcon_DestroyConsole( void );
void Wcon_DisableInput( void );
void Wcon_Clear( void );
char *Wcon_Input( void );
#endif

// text messages
#define Msg	Con_Printf
void MsgDev( int level, const char *pMsg, ... );

#ifdef __cplusplus
}
#endif
#endif//SYSTEM_H
