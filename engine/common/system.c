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
#include "xash3d_mathlib.h"
#include "platform/platform.h"
#include <stdlib.h>
#include <errno.h>

#ifdef XASH_SDL
#include <SDL.h>
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

#include "menu_int.h" // _UPDATE_PAGE macro

#include "library.h"
#include "whereami.h"

qboolean	error_on_exit = false;	// arg for exit();

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
================
Sys_DebugBreak
================
*/
void Sys_DebugBreak( void )
{
#if XASH_LINUX || ( XASH_WIN32 && !XASH_64BIT )
#if _MSC_VER
	if( Sys_DebuggerPresent() )
		_asm { int 3 }
#elif XASH_X86
	if( Sys_DebuggerPresent() )
		asm volatile( "int $3;" );
#else
	if( Sys_DebuggerPresent() )
		raise( SIGINT );
#endif
#endif
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
Sys_Sleep

freeze application for some time
================
*/
void Sys_Sleep( int msec )
{
	if( !msec )
		return;

	msec = Q_min( msec, 1000 );
	Platform_Sleep( msec );
}

/*
================
Sys_GetCurrentUser

returns username for current profile
================
*/
const char *Sys_GetCurrentUser( void )
{
#if XASH_WIN32
	static string	s_userName;
	unsigned long size = sizeof( s_userName );

	if( GetUserName( s_userName, &size ))
		return s_userName;
#elif XASH_POSIX && !XASH_ANDROID && !XASH_NSWITCH
	uid_t uid = geteuid();
	struct passwd *pw = getpwuid( uid );

	if( pw )
		return pw->pw_name;
#endif
	return "Player";
}

/*
==================
Sys_ParseCommandLine

==================
*/
void Sys_ParseCommandLine( int argc, char** argv )
{
	const char	*blank = "censored";
	int		i;

	host.argc = argc;
	host.argv = argv;

	if( !host.change_game ) return;

	for( i = 0; i < host.argc; i++ )
	{
		// we don't want to return to first game
			 if( !Q_stricmp( "-game", host.argv[i] )) host.argv[i] = (char *)blank;
		// probably it's timewaster, because engine rejected second change
		else if( !Q_stricmp( "+game", host.argv[i] )) host.argv[i] = (char *)blank;
		// you sure that map exists in new game?
		else if( !Q_stricmp( "+map", host.argv[i] )) host.argv[i] = (char *)blank;
		// just stupid action
		else if( !Q_stricmp( "+load", host.argv[i] )) host.argv[i] = (char *)blank;
		// changelevel beetwen games? wow it's great idea!
		else if( !Q_stricmp( "+changelevel", host.argv[i] )) host.argv[i] = (char *)blank;
	}
}

/*
==================
Sys_MergeCommandLine

==================
*/
void Sys_MergeCommandLine( void )
{
	const char	*blank = "censored";
	int		i;

	if( !host.change_game ) return;

	for( i = 0; i < host.argc; i++ )
	{
		// second call
		if( Host_IsDedicated() && !Q_strnicmp( "+menu_", host.argv[i], 6 ))
			host.argv[i] = (char *)blank;
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

void Sys_SendKeyEvents( void )
{
#if XASH_WIN32
	MSG	msg;

	while( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ))
	{
		if( !GetMessage( &msg, NULL, 0, 0 ))
			Sys_Quit ();

		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}
#endif
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

	if( !dll->link )
		dll->link = COM_LoadLibrary( dll->name, false, true ); // environment pathes

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
	if( dll->crash ) Sys_Error( "%s", errorstring );
	else Con_Reportf( S_ERROR  "%s", errorstring );

	return false;
}

void* Sys_GetProcAddress( dll_info_t *dll, const char* name )
{
	if( !dll || !dll->link ) // invalid desc
		return NULL;

	return (void *)COM_GetProcAddress( dll->link, name );
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

	COM_FreeLibrary( dll->link );
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
		else Sys_Sleep( 20 );
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

	Msg( "Sys_Warn: %s\n", text );

	if( !Host_IsDedicated() ) // dedicated server should not hang on messagebox
		MSGBOX(text);
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
	if( host.change_game ) Sys_Sleep( 200 );

	error_on_exit = true;
	host.status = HOST_ERR_FATAL;
	va_start( argptr, error );
	Q_vsnprintf( text, MAX_PRINT_MSG, error, argptr );
	va_end( argptr );

	Sys_DebugBreak();

	SV_SysError( text );

	if( !Host_IsDedicated() )
	{
#if XASH_SDL == 2
		if( host.hWnd ) SDL_HideWindow( host.hWnd );
#endif
#if XASH_WIN32
		Wcon_ShowConsole( false );
#endif
		MSGBOX( text );
		Sys_Print( text );
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

	Sys_Quit();
}

#if XASH_EMSCRIPTEN
/* strange glitchy bug on emscripten
_exit->_Exit->asm._exit->_exit
As we do not need atexit(), just throw hidden exception
*/
#include <emscripten.h>
#define exit my_exit
void my_exit(int ret)
{
	emscripten_cancel_main_loop();
	printf("exit(%d)\n", ret);
	EM_ASM(if(showElement)showElement('reload', true);throw 'SimulateInfiniteLoop');
}
#endif

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

	Rcon_Print( pMsg );
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
qboolean Sys_NewInstance( const char *gamedir )
{
#if XASH_NSWITCH
	char newargs[4096];
	const char *exe = host.argv[0]; // arg 0 is always the full NRO path

	// TODO: carry over the old args (assuming you can even pass any)
	Q_snprintf( newargs, sizeof( newargs ), "%s -game %s", exe, gamedir );
	// just restart the entire thing
	printf( "envSetNextLoad exe: `%s`\n", exe );
	printf( "envSetNextLoad argv:\n`%s`\n", newargs );
	Host_Shutdown( );
	envSetNextLoad( exe, newargs );
	exit( 0 );
#else
	int i = 0;
	qboolean replacedArg = false;
	size_t exelen;
	char *exe, **newargs;

	// don't use engine allocation utils here
	// they will be freed after Host_Shutdown
	newargs = calloc( host.argc + 4, sizeof( *newargs ));
	while( i < host.argc )
	{
		newargs[i] = strdup( host.argv[i] );

		// replace existing -game argument
		if( !Q_stricmp( newargs[i], "-game" ))
		{
			newargs[i + 1] = strdup( gamedir );
			replacedArg = true;
			i += 2;
		}
		else i++;
	}

	if( !replacedArg )
	{
		newargs[i++] = strdup( "-game" );
		newargs[i++] = strdup( gamedir );
	}

	newargs[i++] = strdup( "-changegame" );
	newargs[i] = NULL;

	exelen = wai_getExecutablePath( NULL, 0, NULL );
	exe = malloc( exelen + 1 );
	wai_getExecutablePath( exe, exelen, NULL );
	exe[exelen] = 0;

	Host_Shutdown();

	execv( exe, newargs );

	// if execv returned, it's probably an error
	printf( "execv failed: %s", strerror( errno ));

	for( ; i >= 0; i-- )
		free( newargs[i] );
	free( newargs );
	free( exe );
#endif

	return false;
}
