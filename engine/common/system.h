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
#include "xash3d_types.h"
#include "const.h"
#include "crtlib.h"
#include "platform/platform.h"

#define MSGBOX( x )	Platform_MessageBox( "Xash Error", (x), false );
#define MSGBOX2( x )	Platform_MessageBox( "Host Error", (x), true );
#define MSGBOX3( x )	Platform_MessageBox( "Host Recursive Error", (x), true );
#define ASSERT( exp )	if(!( exp )) Sys_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )

/*
========================================================================
internal dll's loader

two main types - native dlls and other win32 libraries will be recognized automatically
NOTE: never change this structure because all dll descriptions in xash code
writes into struct by offsets not names
========================================================================
*/

void Sys_Sleep( int msec );
double Sys_DoubleTime( void );
char *Sys_GetClipboardData( void );
const char *Sys_GetCurrentUser( void );
int Sys_CheckParm( const char *parm );
void Sys_Warn( const char *format, ... ) _format( 1 );
void Sys_Error( const char *error, ... ) _format( 1 );
qboolean Sys_LoadLibrary( dll_info_t *dll );
void* Sys_GetProcAddress( dll_info_t *dll, const char* name );
qboolean Sys_FreeLibrary( dll_info_t *dll );
void Sys_ParseCommandLine( int argc, char **argv );
void Sys_MergeCommandLine( void );
void Sys_SetupCrashHandler( void );
void Sys_RestoreCrashHandler( void );
void Sys_DebugBreak( void );
#define Sys_GetParmFromCmdLine( parm, out ) _Sys_GetParmFromCmdLine( parm, out, sizeof( out ))
qboolean _Sys_GetParmFromCmdLine( const char *parm, char *out, size_t size );
qboolean Sys_GetIntFromCmdLine( const char *parm, int *out );
void Sys_SendKeyEvents( void );
void Sys_Print( const char *pMsg );
void Sys_PrintLog( const char *pMsg );
void Sys_InitLog( void );
void Sys_CloseLog( void );
void Sys_Quit( void ) NORETURN;
qboolean Sys_NewInstance( const char *gamedir );

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
#if XASH_WIN32
void Wcon_InitConsoleCommands( void );
void Wcon_ShowConsole( qboolean show );
void Wcon_CreateConsole( void );
void Wcon_DestroyConsole( void );
void Wcon_DisableInput( void );
char *Wcon_Input( void );
void Wcon_WinPrint( const char *pMsg );
void Wcon_SetStatus( const char *pStatus );
#endif

// text messages
#define Msg	Con_Printf

#ifdef __cplusplus
}
#endif
#endif//SYSTEM_H
