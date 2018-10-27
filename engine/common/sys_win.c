/*
sys_win.c - platform dependent code
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

#include "common.h"
#include "mathlib.h"

qboolean	error_on_exit = false;	// arg for exit();

/*
================
Sys_DoubleTime
================
*/
double Sys_DoubleTime( void )
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

/*
================
Sys_GetClipboardData

create buffer, that contain clipboard
================
*/
char *Sys_GetClipboardData( void )
{
	static char	*data = NULL;
	char		*cliptext;

	if( data )
	{
		// release previous cbd
		Z_Free( data );
		data = NULL;
	}

	if( OpenClipboard( NULL ) != 0 )
	{
		HANDLE	hClipboardData;

		if(( hClipboardData = GetClipboardData( CF_TEXT )) != 0 )
		{
			if(( cliptext = GlobalLock( hClipboardData )) != 0 ) 
			{
				data = Z_Malloc( GlobalSize( hClipboardData ) + 1 );
				Q_strcpy( data, cliptext );
				GlobalUnlock( hClipboardData );
			}
		}
		CloseClipboard();
	}

	return data;
}

/*
================
Sys_SetClipboardData

write screenshot into clipboard
================
*/
void Sys_SetClipboardData( const byte *buffer, size_t size )
{
	EmptyClipboard();

	if( OpenClipboard( NULL ) != 0 )
	{
		HGLOBAL hResult = GlobalAlloc( GMEM_MOVEABLE, size ); 
		byte *bufferCopy = (byte *)GlobalLock( hResult ); 

		memcpy( bufferCopy, buffer, size ); 
		GlobalUnlock( hResult ); 

		if( SetClipboardData( CF_DIB, hResult ) == NULL )
		{
			Con_Printf( S_ERROR "unable to write screenshot\n" );
			GlobalFree( hResult );
		}
		CloseClipboard();
	}
}

/*
================
Sys_Sleep

freeze application for some time
================
*/
void Sys_Sleep( int msec )
{
	msec = bound( 0, msec, 1000 );
	Sleep( msec );
}

/*
================
Sys_GetCurrentUser

returns username for current profile
================
*/
char *Sys_GetCurrentUser( void )
{
	static string	sys_user_name;
	dword		size = sizeof( sys_user_name );

	if( !sys_user_name[0] )
	{
		HINSTANCE	advapi32_dll = LoadLibrary( "advapi32.dll" );
		BOOL (_stdcall *pGetUserNameA)( LPSTR lpBuffer, LPDWORD nSize ) = NULL;
		if( advapi32_dll ) pGetUserNameA = (void *)GetProcAddress( advapi32_dll, "GetUserNameA" );
		if( pGetUserNameA) pGetUserNameA( sys_user_name, &size );
		if( advapi32_dll ) FreeLibrary( advapi32_dll ); // no need anymore...
		if( !sys_user_name[0] ) Q_strcpy( sys_user_name, "player" );
	}

	return sys_user_name;
}

/*
=================
Sys_GetMachineKey
=================
*/
const char *Sys_GetMachineKey( int *nLength )
{
	HINSTANCE		rpcrt4_dll = LoadLibrary( "rpcrt4.dll" );
	RPC_STATUS	(_stdcall *pUuidCreateSequential)( UUID __RPC_FAR *Uuid ) = NULL;
	static byte	key[32];
	byte		mac[8];
	UUID		uuid;
	int		i;

	if( rpcrt4_dll ) pUuidCreateSequential = (void *)GetProcAddress( rpcrt4_dll, "UuidCreateSequential" );
	if( pUuidCreateSequential ) pUuidCreateSequential( &uuid );	// ask OS to create UUID
	if( rpcrt4_dll ) FreeLibrary( rpcrt4_dll ); // no need anymore...

	for( i = 2; i < 8; i++ ) // bytes 2 through 7 inclusive are MAC address
		mac[i-2] = uuid.Data4[i];

	Q_snprintf( key, sizeof( key ), "%02X-%02X-%02X-%02X-%02X-%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

	if( nLength ) *nLength = Q_strlen( key );
	return key;
}

/*
=================
Sys_ShellExecute
=================
*/
void Sys_ShellExecute( const char *path, const char *parms, qboolean exit )
{
	HINSTANCE	shell32_dll = LoadLibrary( "shell32.dll" );
	HINSTANCE (_stdcall *pShellExecuteA)( HWND hwnd, LPCSTR lpOp, LPCSTR lpFile, LPCSTR lpParam, LPCSTR lpDir, INT nShowCmd ) = NULL;
	if( shell32_dll ) pShellExecuteA = (void *)GetProcAddress( shell32_dll, "ShellExecuteA" );
	if( pShellExecuteA ) pShellExecuteA( NULL, "open", path, parms, NULL, SW_SHOW );
	if( shell32_dll ) FreeLibrary( shell32_dll ); // no need anymore...

	if( exit ) Sys_Quit();
}

/*
==================
Sys_ParseCommandLine

==================
*/
void Sys_ParseCommandLine( LPSTR lpCmdLine, qboolean uncensored )
{
	const char	*blank = "censored";
	static char	commandline[MAX_SYSPATH];
	int		i;

	host.argc = 1;
	host.argv[0] = "exe";

	Q_strncpy( commandline, lpCmdLine, Q_strlen( lpCmdLine ) + 1 );
	lpCmdLine = commandline; // to prevent modify original commandline

	while( *lpCmdLine && ( host.argc < MAX_NUM_ARGVS ))
	{
		while( *lpCmdLine && *lpCmdLine <= ' ' )
			lpCmdLine++;
		if( !*lpCmdLine ) break;

		if( *lpCmdLine == '\"' )
		{
			// quoted string
			lpCmdLine++;
			host.argv[host.argc] = lpCmdLine;
			host.argc++;
			while( *lpCmdLine && ( *lpCmdLine != '\"' ))
				lpCmdLine++;
		}
		else
		{
			// unquoted word
			host.argv[host.argc] = lpCmdLine;
			host.argc++;
			while( *lpCmdLine && *lpCmdLine > ' ')
				lpCmdLine++;
		}

		if( *lpCmdLine )
		{
			*lpCmdLine = 0;
			lpCmdLine++;
		}
	}

	if( uncensored || !host.change_game )
		return;

	for( i = 0; i < host.argc; i++ )
	{
		// we wan't return to first game
		if( !Q_stricmp( "-game", host.argv[i] )) host.argv[i] = (char *)blank;
		// probably it's timewaster, because engine rejected second change
		if( !Q_stricmp( "+game", host.argv[i] )) host.argv[i] = (char *)blank;
		// you sure what is map exists in new game?
		if( !Q_stricmp( "+map", host.argv[i] )) host.argv[i] = (char *)blank;
		// just stupid action
		if( !Q_stricmp( "+load", host.argv[i] )) host.argv[i] = (char *)blank;
		// changelevel beetwen games? wow it's great idea!
		if( !Q_stricmp( "+changelevel", host.argv[i] )) host.argv[i] = (char *)blank;
	}
}

/*
==================
Sys_MergeCommandLine

==================
*/
void Sys_MergeCommandLine( LPSTR lpCmdLine )
{
	static char	commandline[MAX_SYSPATH];

	if( !host.change_game ) return;

	Q_strncpy( commandline, lpCmdLine, Q_strlen( lpCmdLine ) + 1 );
	lpCmdLine = commandline; // to prevent modify original commandline

	while( *lpCmdLine && ( host.argc < MAX_NUM_ARGVS ))
	{
		while( *lpCmdLine && *lpCmdLine <= ' ' )
			lpCmdLine++;
		if( !*lpCmdLine ) break;

		if( *lpCmdLine == '\"' )
		{
			// quoted string
			lpCmdLine++;
			host.argv[host.argc] = lpCmdLine;
			host.argc++;
			while( *lpCmdLine && ( *lpCmdLine != '\"' ))
				lpCmdLine++;
		}
		else
		{
			// unquoted word
			host.argv[host.argc] = lpCmdLine;
			host.argc++;
			while( *lpCmdLine && *lpCmdLine > ' ')
				lpCmdLine++;
		}

		if( *lpCmdLine )
		{
			*lpCmdLine = 0;
			lpCmdLine++;
		}
	}
}

/*
================
Sys_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int Sys_CheckParm( const char *parm )
{
	int	i;

	for( i = 1; i < host.argc; i++ )
	{
		if( !host.argv[i] )
			continue;

		if( !Q_stricmp( parm, host.argv[i] ))
			return i;
	}
	return 0;
}

/*
================
Sys_GetParmFromCmdLine

Returns the argument for specified parm
================
*/
qboolean _Sys_GetParmFromCmdLine( char *parm, char *out, size_t size )
{
	int	argc = Sys_CheckParm( parm );

	if( !argc || !out || !host.argv[argc + 1] )
		return false;

	Q_strncpy( out, host.argv[argc+1], size );

	return true;
}

void Sys_SendKeyEvents( void )
{
	MSG	msg;

	while( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ))
	{
		if( !GetMessage( &msg, NULL, 0, 0 ))
			Sys_Quit ();

      		TranslateMessage( &msg );
      		DispatchMessage( &msg );
	}
}

//=======================================================================
//			DLL'S MANAGER SYSTEM
//=======================================================================
qboolean Sys_LoadLibrary( dll_info_t *dll )
{
	const dllfunc_t	*func;
	string		errorstring;

	// check errors
	if( !dll ) return false;	// invalid desc
	if( dll->link ) return true;	// already loaded

	if( !dll->name || !*dll->name )
		return false; // nothing to load

	Con_Reportf( "Sys_LoadLibrary: Loading %s", dll->name );

	if( dll->fcts ) 
	{
		// lookup export table
		for( func = dll->fcts; func && func->name != NULL; func++ )
			*func->func = NULL;
	}

	if( !dll->link ) dll->link = LoadLibrary ( dll->name ); // environment pathes

	// no DLL found
	if( !dll->link ) 
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "Sys_LoadLibrary: couldn't load %s\n", dll->name );
		goto error;
	}

	// Get the function adresses
	for( func = dll->fcts; func && func->name != NULL; func++ )
	{
		if( !( *func->func = Sys_GetProcAddress( dll, func->name )))
		{
			Q_snprintf( errorstring, sizeof( errorstring ), "Sys_LoadLibrary: %s missing or invalid function (%s)\n", dll->name, func->name );
			goto error;
		}
	}
          Con_Reportf( " - ok\n" );

	return true;
error:
	Con_Reportf( " - failed\n" );
	Sys_FreeLibrary( dll ); // trying to free 
	if( dll->crash ) Sys_Error( errorstring );
	else Con_DPrintf( "%s%s", S_ERROR, errorstring );			

	return false;
}

void* Sys_GetProcAddress( dll_info_t *dll, const char* name )
{
	if( !dll || !dll->link ) // invalid desc
		return NULL;

	return (void *)GetProcAddress( dll->link, name );
}

qboolean Sys_FreeLibrary( dll_info_t *dll )
{
	// invalid desc or alredy freed
	if( !dll || !dll->link )
		return false;

	if( host.status == HOST_CRASHED )
	{
		// we need to hold down all modules, while MSVC can find error
		Con_Reportf( "Sys_FreeLibrary: hold %s for debugging\n", dll->name );
		return false;
	}
	else Con_Reportf( "Sys_FreeLibrary: Unloading %s\n", dll->name );

	FreeLibrary( dll->link );
	dll->link = NULL;

	return true;
}

/*
================
Sys_WaitForQuit

wait for 'Esc' key will be hit
================
*/
void Sys_WaitForQuit( void )
{
	MSG	msg;

	Con_RegisterHotkeys();		

	msg.message = 0;

	// wait for the user to quit
	while( msg.message != WM_QUIT )
	{
		if( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ))
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		} 
		else Sys_Sleep( 20 );
	}
}

long _stdcall Sys_Crash( PEXCEPTION_POINTERS pInfo )
{
	// save config
	if( host.status != HOST_CRASHED )
	{
		// check to avoid recursive call
		error_on_exit = true;
		host.crashed = true;

		if( host.type == HOST_NORMAL )
			CL_Crashed(); // tell client about crash
		else host.status = HOST_CRASHED;

		Con_Printf( "unhandled exception: %p at address %p\n", pInfo->ExceptionRecord->ExceptionAddress, pInfo->ExceptionRecord->ExceptionCode );

		if( !host_developer.value )
		{
			// for non-development mode
			Sys_Quit();
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		// all other states keep unchanged to let debugger find bug
		Con_DestroyConsole();
	}

	if( host.oldFilter )
		return host.oldFilter( pInfo );
	return EXCEPTION_CONTINUE_EXECUTION;
}

/*
================
Sys_Error

NOTE: we must prepare engine to shutdown
before call this
================
*/
void Sys_Error( const char *error, ... )
{
	va_list	argptr;
	char	text[MAX_SYSPATH];
         
	if( host.status == HOST_ERR_FATAL )
		return; // don't multiple executes

	// make sure what console received last message
	if( host.change_game ) Sys_Sleep( 200 );

	error_on_exit = true;
	host.status = HOST_ERR_FATAL;	
	va_start( argptr, error );
	Q_vsprintf( text, error, argptr );
	va_end( argptr );

	SV_SysError( text );

	if( host.type == HOST_NORMAL )
	{
		if( host.hWnd ) ShowWindow( host.hWnd, SW_HIDE );
	}

	if( host_developer.value )
	{
		Con_ShowConsole( true );
		Con_DisableInput();	// disable input line for dedicated server
		Sys_Print( text );	// print error message
		Sys_WaitForQuit();
	}
	else
	{
		Con_ShowConsole( false );
		MSGBOX( text );
	}

	Sys_Quit();
}

/*
================
Sys_Quit
================
*/
void Sys_Quit( void )
{
	Host_Shutdown();
	exit( error_on_exit );
}

/*
================
Sys_Print

print into window console
================
*/
void Sys_Print( const char *pMsg )
{
	const char	*msg;
	static char	buffer[MAX_PRINT_MSG];
	static char	logbuf[MAX_PRINT_MSG];
	char		*b = buffer;
	char		*c = logbuf;	
	int		i = 0;

	if( host.type == HOST_NORMAL )
		Con_Print( pMsg );

	// if the message is REALLY long, use just the last portion of it
	if( Q_strlen( pMsg ) > sizeof( buffer ) - 1 )
		msg = pMsg + Q_strlen( pMsg ) - sizeof( buffer ) + 1;
	else msg = pMsg;

	// copy into an intermediate buffer
	while( msg[i] && (( b - buffer ) < sizeof( buffer ) - 1 ))
	{
		if( msg[i] == '\n' && msg[i+1] == '\r' )
		{
			b[0] = '\r';
			b[1] = c[0] = '\n';
			b += 2, c++;
			i++;
		}
		else if( msg[i] == '\r' )
		{
			b[0] = c[0] = '\r';
			b[1] = '\n';
			b += 2, c++;
		}
		else if( msg[i] == '\n' )
		{
			b[0] = '\r';
			b[1] = c[0] = '\n';
			b += 2, c++;
		}
		else if( msg[i] == '\35' || msg[i] == '\36' || msg[i] == '\37' )
		{
			i++; // skip console pseudo graph
		}
		else if( IsColorString( &msg[i] ))
		{
			i++; // skip color prefix
		}
		else
		{
			if( msg[i] == '\1' || msg[i] == '\2' )
				i++;
			*b = *c = msg[i];
			b++, c++;
		}
		i++;
	}

	*b = *c = 0; // terminator

	Sys_PrintLog( logbuf );
	Con_WinPrint( buffer );
}