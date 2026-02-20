/*
sys_win.c - platform dependent code (which haven't moved to platform dir yet)
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
#include "xash3d_mathlib.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <errno.h>

#if _MSC_VER
#include <intrin.h>
#endif

#if XASH_SDL == 2
#include <SDL.h>
#elif XASH_SDL == 3
#include <SDL3/SDL.h>
#endif

#if XASH_POSIX
#include <unistd.h>
#include <signal.h>

#if !XASH_ANDROID
#include <pwd.h>
#endif
#endif

#if XASH_WIN32
#include <process.h>
#endif

#if XASH_NSWITCH
#include <switch.h>
#endif

#if XASH_PSVITA
#include <vitasdk.h>
#endif

#include "menu_int.h" // _UPDATE_PAGE macro

#include "library.h"
#include "whereami.h"

int error_on_exit = 0;	// arg for exit();

/*
================
Sys_DoubleTime
================
*/
double GAME_EXPORT Sys_DoubleTime( void )
{
	return Platform_DoubleTime();
}

/*
===============
Sys_FloatTime
===============
*/
float GAME_EXPORT Sys_FloatTime( void )
{
	return (float)Platform_DoubleTime();
}

/*
================
Sys_DebugBreak
================
*/
void Sys_DebugBreak( void )
{
	qboolean was_grabbed = false;

	if( !Sys_DebuggerPresent( ))
		return;

	if( host.hWnd ) // so annoying
	{
		was_grabbed = Platform_GetMouseGrab();
		Platform_SetMouseGrab( false );
	}

#if XASH_WIN32 // both MSVC and MinGW support it
	__debugbreak();
#else // !_MSC_VER
	INLINE_RAISE( SIGINT );
	INLINE_NANOSLEEP1(); // sometimes signal comes with delay, let it interrupt nanosleep
#endif // !_MSC_VER

	if( was_grabbed )
		Platform_SetMouseGrab( true );
}

#if !XASH_DEDICATED
/*
================
Sys_GetClipboardData

create buffer, that contain clipboard
================
*/
char *Sys_GetClipboardData( void )
{
	static char data[1024];

	data[0] = '\0';

	Platform_GetClipboardText( data, sizeof( data ));

	return data;
}
#endif // XASH_DEDICATED

/*
================
Sys_GetCurrentUser

returns username for current profile
================
*/
const char *Sys_GetCurrentUser( void )
{
	// TODO: move to platform
#if XASH_WIN32
	static wchar_t sw_userName[MAX_STRING];
	DWORD size = ARRAYSIZE( sw_userName );

	if( GetUserNameW( sw_userName, &size ) && sw_userName[0] != 0 )
	{
		static char s_userName[MAX_STRING * 4];

		// set length to -1, so it will null terminate
		WideCharToMultiByte( CP_UTF8, 0, sw_userName, -1, s_userName, sizeof( s_userName ), NULL, NULL );
		return s_userName;
	}
#elif XASH_PSVITA
	static string username;
	sceAppUtilSystemParamGetString( SCE_SYSTEM_PARAM_ID_USERNAME, username, sizeof( username ) - 1 );
	if( !COM_StringEmpty( username ))
		return username;
#elif XASH_POSIX && !XASH_ANDROID && !XASH_NSWITCH
	static string username;
	struct passwd *pw = getpwuid( geteuid( ));

	// POSIX standard says pw _might_ point to static area, so let's make a copy
	if( pw && !COM_StringEmptyOrNULL( pw->pw_name ))
	{
		Q_strncpy( username, pw->pw_name, sizeof( username ));
		return username;
	}
#endif
	return "Player";
}

/*
==================
Sys_ParseCommandLine

==================
*/
void Sys_ParseCommandLine( int argc, const char **argv )
{
	const char	*blank = "censored";
	int		i;

	host.argc = argc;
	host.argv = argv;

	if( !host.change_game ) return;

	for( i = 0; i < host.argc; i++ )
	{
		// we don't want to return to first game
			 if( !Q_stricmp( "-game", host.argv[i] )) host.argv[i] = blank;
		// probably it's timewaster, because engine rejected second change
		else if( !Q_stricmp( "+game", host.argv[i] )) host.argv[i] = blank;
		// you sure that map exists in new game?
		else if( !Q_stricmp( "+map", host.argv[i] )) host.argv[i] = blank;
		// just stupid action
		else if( !Q_stricmp( "+load", host.argv[i] )) host.argv[i] = blank;
		// changelevel beetwen games? wow it's great idea!
		else if( !Q_stricmp( "+changelevel", host.argv[i] )) host.argv[i] = blank;
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
qboolean _Sys_GetParmFromCmdLine( const char *parm, char *out, size_t size )
{
	int	argc = Sys_CheckParm( parm );

	if( !argc || !out || !host.argv[argc + 1] )
		return false;

	Q_strncpy( out, host.argv[argc+1], size );

	return true;
}

qboolean Sys_GetIntFromCmdLine( const char* argName, int *out )
{
	int argIndex = Sys_CheckParm( argName );

	if( argIndex < 1 || argIndex + 1 >= host.argc || !host.argv[argIndex + 1] )
	{
		*out = 0;
		return false;
	}

	*out = Q_atoi( host.argv[argIndex + 1] );
	return true;
}

//=======================================================================
//			DLL'S MANAGER SYSTEM
//=======================================================================
qboolean Sys_LoadLibrary( dll_info_t *dll )
{
	size_t i;
	string		errorstring;

	// check errors
	if( !dll ) return false;	// invalid desc
	if( dll->link ) return true;	// already loaded

	if( !dll->name || !*dll->name )
		return false; // nothing to load

	Con_Reportf( "%s: Loading %s", __func__, dll->name );

	if( dll->fcts ) // lookup export table
		ClearExports( dll->fcts, dll->num_fcts );

	if( !dll->link )
		dll->link = COM_LoadLibrary( dll->name, false, true ); // environment pathes

	// no DLL found
	if( !dll->link )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "Sys_LoadLibrary: couldn't load %s\n", dll->name );
		goto error;
	}

	// Get the function adresses
	for( i = 0; i < dll->num_fcts; i++ )
	{
		const dllfunc_t *func = &dll->fcts[i];
		if( !( *func->func = COM_GetProcAddress( dll->link, func->name )))
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
	if( dll->crash ) Sys_Error( "%s", errorstring );
	else Con_Reportf( S_ERROR "%s", errorstring );

	return false;
}

qboolean Sys_FreeLibrary( dll_info_t *dll )
{
	// invalid desc or alredy freed
	if( !dll || !dll->link )
		return false;

	if( host.status == HOST_CRASHED )
	{
		// we need to hold down all modules, while MSVC can find error
		Con_Reportf( "%s: hold %s for debugging\n", __func__, dll->name );
		return false;
	}
	else Con_Reportf( "%s: Unloading %s\n", __func__, dll->name );

	COM_FreeLibrary( dll->link );
	dll->link = NULL;

	ClearExports( dll->fcts, dll->num_fcts );

	return true;
}

/*
================
Sys_WaitForQuit

wait for 'Esc' key will be hit
================
*/
static void Sys_WaitForQuit( void )
{
#if XASH_WIN32
	MSG	msg;
	msg.message = 0;

	// wait for the user to quit
	while( msg.message != WM_QUIT )
	{
		if( PeekMessage( &msg, 0, 0, 0, PM_REMOVE ))
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		else Platform_Sleep( 20 );
	}
#endif
}

/*
================
Sys_Warn

Just messagebox
================
*/
void Sys_Warn( const char *format, ... )
{
	va_list	argptr;
	char	text[MAX_PRINT_MSG];

	va_start( argptr, format );
	Q_vsnprintf( text, MAX_PRINT_MSG, format, argptr );
	va_end( argptr );

	Sys_DebugBreak();

	Con_Printf( "%s: %s\n", __func__, text );

	if( !Host_IsDedicated() ) // dedicated server should not hang on messagebox
		Platform_MessageBox( "Xash Warning", text, true );
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
	char	text[MAX_PRINT_MSG];

	// enable cursor before debugger call
	if( !Host_IsDedicated( ))
		Platform_SetCursorType( dc_arrow );

	if( host.status == HOST_ERR_FATAL )
		return; // don't multiple executes

	// make sure that console received last message
	if( host.change_game ) Platform_Sleep( 200 );

	error_on_exit = 1;
	host.status = HOST_ERR_FATAL;
	va_start( argptr, error );
	Q_vsnprintf( text, MAX_PRINT_MSG, error, argptr );
	va_end( argptr );

	Sys_DebugBreak();

	SV_SysError( text );

	if( !Host_IsDedicated() )
	{
#if XASH_SDL >= 2
		if( host.hWnd ) SDL_HideWindow( host.hWnd );
#endif
#if XASH_WIN32
		Wcon_ShowConsole( false );
#endif
		Sys_Print( text );
		Platform_MessageBox( "Xash Error", text, true );
	}
	else
	{
#if XASH_WIN32
		Wcon_ShowConsole( true );
		Wcon_DisableInput();	// disable input line for dedicated server
#endif
		Sys_Print( text );	// print error message
		Sys_WaitForQuit();
	}

	Sys_Quit( "caught an error" );
}

/*
================
Sys_Quit
================
*/
void Sys_Quit( const char *reason )
{
	Host_ShutdownWithReason( reason );
	Host_ExitInMain();
}

/*
================
Sys_Print

print into window console
================
*/
void Sys_Print( const char *pMsg )
{
#if !XASH_DEDICATED
	if( !Host_IsDedicated() )
	{
		Con_Print( pMsg );
	}
#endif

#if XASH_WIN32
	{
		const char	*msg;
		static char	buffer[MAX_PRINT_MSG];
		static char	logbuf[MAX_PRINT_MSG];
		char		*b = buffer;
		char		*c = logbuf;
		int		i = 0;

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
			else
			{
				if( msg[i] == '\1' || msg[i] == '\2' || msg[i] == '\3' )
					i++;
				*b = *c = msg[i];
				b++, c++;
			}
			i++;
		}

		*b = *c = 0; // terminator

		Wcon_WinPrint( buffer );
	}
#endif

	Sys_PrintLog( pMsg );

	Rcon_Print( &host.rd, pMsg );
}

/*
==================
Sys_CanRestart

Returns true if execv-like syscall is available
==================
*/
qboolean Sys_CanRestart( void )
{
#if XASH_NSWITCH || XASH_PSVITA
	return true;
#else
	int exelen = wai_getExecutablePath( NULL, 0, NULL );

	return exelen > 0;
#endif
}

/*
==================
Sys_ChangeGame

This is a special function

Here we restart engine with new -game parameter
but since engine will be unloaded during this call
it explicitly doesn't use internal allocation or string copy utils
==================
*/
qboolean Sys_NewInstance( const char *gamedir, const char *finalmsg )
{
	qboolean replaced_arg = false;
	int i;

#if XASH_NSWITCH
	char newargs[4096];
	const char *exe = host.argv[0]; // arg 0 is always the full NRO path

	for( i = 0; i < host.argc; i++ )
	{
		Q_strncat( newargs, host.argv[i], sizeof( newargs ));
		Q_strncat( newargs, " ", sizeof( newargs ));

		if( !Q_stricmp( host.argv[i], "-game" ))
		{
			Q_strncat( newargs, gamedir, sizeof( newargs ));
			Q_strncat( newargs, " ", sizeof( newargs ));
			replaced_arg = true;
			i++;
		}
	}

	if( !replaced_arg )
	{
		Q_strncat( newargs, "-game ", sizeof( newargs ));
		Q_strncat( newargs, gamedir, sizeof( newargs ));
	}

	Q_strncat( newargs, " -changegame", sizeof( newargs ));

	// just restart the entire thing
	printf( "envSetNextLoad exe: `%s`\n", exe );
	printf( "envSetNextLoad argv:\n`%s`\n", newargs );

	Host_ShutdownWithReason( finalmsg );
	envSetNextLoad( exe, newargs );
	exit( 0 );
#else
	int exelen;
	char *exe, **newargs;

	// don't use engine allocation utils here
	// they will be freed after Host_Shutdown
	newargs = calloc( host.argc + 4, sizeof( *newargs ));

	for( i = 0; i < host.argc; i++ )
	{
		newargs[i] = strdup( host.argv[i] );

		if( !Q_stricmp( newargs[i], "-game" ))
		{
			newargs[i + 1] = strdup( gamedir );
			replaced_arg = true;
			i++;
		}
	}

	if( !replaced_arg )
	{
		newargs[i++] = strdup( "-game" );
		newargs[i++] = strdup( gamedir );
	}

	newargs[i++] = strdup( "-changegame" );
	newargs[i] = NULL;

	Host_ShutdownWithReason( finalmsg );

#if XASH_PSVITA
	// under normal circumstances it's always going to be the same path
	exe = strdup( "app0:/eboot.bin" );
	sceAppMgrLoadExec( exe, newargs, NULL );
#else
	exelen = wai_getExecutablePath( NULL, 0, NULL );
	if( exelen >= 0 )
	{
		exe = malloc( exelen + 1 );
		wai_getExecutablePath( exe, exelen, NULL );
		exe[exelen] = 0;

		execv( exe, newargs );

		// if execv returned, it's probably an error
		printf( "execv failed: %s", strerror( errno ));
	}
#endif

	for( ; i >= 0; i-- )
		free( newargs[i] );
	free( newargs );
	free( exe );
#endif

	return false;
}


/*
==================
Sys_GetNativeObject

Get platform-specific native object
==================
*/
void *Sys_GetNativeObject( const char *obj )
{
	void *ptr;

	if( COM_StringEmptyOrNULL( obj ))
		return NULL;

	ptr = FS_GetNativeObject( obj );

	if( ptr )
		return ptr;

	// Backend should consider that obj is case-sensitive
#if XASH_ANDROID
	ptr = Android_GetNativeObject( obj );
#endif // XASH_ANDROID

	return ptr;
}
