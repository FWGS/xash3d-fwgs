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

#include <setjmp.h>
#include <stdio.h>
#include <time.h>
#include <windows.h>

#define MSGBOX( x )		MessageBox( NULL, x, "Xash Error", MB_OK|MB_SETFOREGROUND|MB_ICONSTOP )
#define MSGBOX2( x )	MessageBox( host.hWnd, x, "Host Error", MB_OK|MB_SETFOREGROUND|MB_ICONSTOP )
#define MSGBOX3( x )	MessageBox( host.hWnd, x, "Host Recursive Error", MB_OK|MB_SETFOREGROUND|MB_ICONSTOP )

// basic typedefs
typedef int		sound_t;
typedef float		vec_t;
typedef vec_t		vec2_t[2];
typedef vec_t		vec3_t[3];
typedef vec_t		vec4_t[4];
typedef byte		rgba_t[4];	// unsigned byte colorpack
typedef vec_t		matrix3x4[3][4];
typedef vec_t		matrix4x4[4][4];

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
void Sys_Error( const char *error, ... );
qboolean Sys_LoadLibrary( dll_info_t *dll );
void* Sys_GetProcAddress( dll_info_t *dll, const char* name );
qboolean Sys_FreeLibrary( dll_info_t *dll );
void Sys_ParseCommandLine( LPSTR lpCmdLine, qboolean uncensored );
void Sys_MergeCommandLine( LPSTR lpCmdLine );
long _stdcall Sys_Crash( PEXCEPTION_POINTERS pInfo );
void Sys_SetClipboardData( const byte *buffer, size_t size );
#define Sys_GetParmFromCmdLine( parm, out ) _Sys_GetParmFromCmdLine( parm, out, sizeof( out ))
qboolean _Sys_GetParmFromCmdLine( char *parm, char *out, size_t size );
void Sys_ShellExecute( const char *path, const char *parms, qboolean exit );
const char *Sys_GetMachineKey( int *nLength );
void Sys_SendKeyEvents( void );
void Sys_Print( const char *pMsg );
void Sys_PrintLog( const char *pMsg );
void Sys_InitLog( void );
void Sys_CloseLog( void );
void Sys_Quit( void );

//
// sys_con.c
//
void Con_ShowConsole( qboolean show );
void Con_WinPrint( const char *pMsg );
void Con_InitConsoleCommands( void );
void Con_CreateConsole( void );
void Con_DestroyConsole( void );
void Con_RegisterHotkeys( void );
void Con_DisableInput( void );
char *Con_Input( void );

// text messages
#define Msg	Con_Printf

#ifdef __cplusplus
}
#endif
#endif//SYSTEM_H