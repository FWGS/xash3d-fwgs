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

#define ASSERT( exp )	if(!( exp )) Sys_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )

/*
========================================================================
internal dll's loader

two main types - native dlls and other win32 libraries will be recognized automatically
NOTE: never change this structure because all dll descriptions in xash code
writes into struct by offsets not names
========================================================================
*/
typedef struct dll_info_s
{
	const char      *name;	// name of library
	const dllfunc_t *fcts;	// list of dll exports
	const size_t    num_fcts;
	qboolean        crash;	// crash if dll not found
	void            *link;	// hinstance of loading library
} dll_info_t;

extern int error_on_exit;
double GAME_EXPORT Sys_DoubleTime( void ); // only for binary compatibility, use Platform_DoubleTime instead
float GAME_EXPORT Sys_FloatTime( void ); // only for binary compatibility, use Platform_DoubleTime instead
char *Sys_GetClipboardData( void );
const char *Sys_GetCurrentUser( void );
int Sys_CheckParm( const char *parm );
void Sys_Warn( const char *format, ... ) FORMAT_CHECK( 1 );
void Sys_Error( const char *error, ... ) FORMAT_CHECK( 1 );
qboolean Sys_LoadLibrary( dll_info_t *dll );
qboolean Sys_FreeLibrary( dll_info_t *dll );
void Sys_ParseCommandLine( int argc, const char **argv );
void Sys_DebugBreak( void );
#define Sys_GetParmFromCmdLine( parm, out ) _Sys_GetParmFromCmdLine( parm, out, sizeof( out ))
qboolean _Sys_GetParmFromCmdLine( const char *parm, char *out, size_t size );
qboolean Sys_GetIntFromCmdLine( const char *parm, int *out );
void Sys_Print( const char *pMsg );
void Sys_Quit( const char *reason ) NORETURN;
qboolean Sys_CanRestart( void );
qboolean Sys_NewInstance( const char *gamedir, const char *finalmsg );
void *Sys_GetNativeObject( const char *obj );

//
// sys_con.c
//
char *Sys_Input( void );
void Sys_DestroyConsole( void );
void Sys_CloseLog( const char *finalmsg );
void Sys_InitLog( void );
void Sys_PrintLog( const char *pMsg );
int Sys_LogFileNo( void );

// text messages
#define Msg	Con_Printf

#ifdef __cplusplus
}
#endif
#endif//SYSTEM_H
