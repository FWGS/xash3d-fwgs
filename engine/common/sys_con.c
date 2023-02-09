/*
sys_con.c - stdout and log
Copyright (C) 2007 Uncle Mike

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
#if XASH_WIN32
#define STDOUT_FILENO 1
#include <io.h>
#elif XASH_ANDROID
#include <android/log.h>
#endif
#include <string.h>
#include <errno.h>
#if XASH_IRIX
#include <sys/time.h>
#endif

// do not waste precious CPU cycles on mobiles or low memory devices
#if !XASH_WIN32 && !XASH_MOBILE_PLATFORM && !XASH_LOW_MEMORY
#define XASH_COLORIZE_CONSOLE true
// use with caution, running engine in Qt Creator may cause a freeze in read() call
// I was never encountered this bug anywhere else, so still enable by default
// #define XASH_USE_SELECT 1
#else
#define XASH_COLORIZE_CONSOLE false
#endif

#if XASH_USE_SELECT
// non-blocking console input
#include <sys/select.h>
#endif

typedef struct {
	char		title[64];
	qboolean		log_active;
	char		log_path[MAX_SYSPATH];
	FILE		*logfile;
	int 		logfileno;
} LogData;

static LogData s_ld;

char *Sys_Input( void )
{
#if XASH_USE_SELECT
	{
		fd_set rfds;
		static char line[1024];
		static int len;
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO( &rfds );
		FD_SET( 0, &rfds); // stdin
		while( select( 1, &rfds, NULL, NULL, &tv ) > 0 )
		{
			if( read( 0, &line[len], 1 ) != 1 )
				break;
			if( line[len] == '\n' || len > 1022 )
			{
				line[ ++len ] = 0;
				len = 0;
				return line;
			}
			len++;
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}
	}
#endif
#if XASH_WIN32
	return Wcon_Input();
#endif
	return NULL;
}

void Sys_DestroyConsole( void )
{
	// last text message into console or log
	Con_Reportf( "Sys_DestroyConsole: Exiting!\n" );
#if XASH_WIN32
	Wcon_DestroyConsole();
#endif
}

/*
===============================================================================

SYSTEM LOG

===============================================================================
*/
int Sys_LogFileNo( void )
{
	return s_ld.logfileno;
}

static void Sys_FlushStdout( void )
{
	// never printing anything to stdout on mobiles
#if !XASH_MOBILE_PLATFORM
	fflush( stdout );
#endif
}

static void Sys_FlushLogfile( void )
{
	if( s_ld.logfile )
		fflush( s_ld.logfile );
}

void Sys_InitLog( void )
{
	const char	*mode;

	if( Sys_CheckParm( "-log" ) && host.allow_console != 0 )
	{
		s_ld.log_active = true;
		Q_strncpy( s_ld.log_path, "engine.log", sizeof( s_ld.log_path ));
	}

	if( host.change_game && host.type != HOST_DEDICATED )
		mode = "a";
	else mode = "w";

	// create log if needed
	if( s_ld.log_active )
	{
		s_ld.logfile = fopen( s_ld.log_path, mode );

		if ( !s_ld.logfile )
		{
			Con_Reportf( S_ERROR  "Sys_InitLog: can't create log file %s: %s\n", s_ld.log_path, strerror( errno ) );
			return;
		}

		s_ld.logfileno = fileno( s_ld.logfile );

		fprintf( s_ld.logfile, "=================================================================================\n" );
		fprintf( s_ld.logfile, "\t%s (build %i) started at %s\n", s_ld.title, Q_buildnum(), Q_timestamp( TIME_FULL ) );
		fprintf( s_ld.logfile, "=================================================================================\n" );
	}
}

void Sys_CloseLog( void )
{
	char	event_name[64];

	// continue logged
	switch( host.status )
	{
	case HOST_CRASHED:
		Q_strncpy( event_name, "crashed", sizeof( event_name ));
		break;
	case HOST_ERR_FATAL:
		Q_strncpy( event_name, "stopped with error", sizeof( event_name ));
		break;
	default:
		if( !host.change_game ) Q_strncpy( event_name, "stopped", sizeof( event_name ));
		else Q_strncpy( event_name, host.finalmsg, sizeof( event_name ));
		break;
	}

	Sys_FlushStdout(); // flush to stdout to ensure all data was written

	if( s_ld.logfile )
	{
		fprintf( s_ld.logfile, "\n");
		fprintf( s_ld.logfile, "=================================================================================");
		if( host.change_game ) fprintf( s_ld.logfile, "\n\t%s (build %i) %s\n", s_ld.title, Q_buildnum(), event_name );
		else fprintf( s_ld.logfile, "\n\t%s (build %i) %s at %s\n", s_ld.title, Q_buildnum(), event_name, Q_timestamp( TIME_FULL ));
		fprintf( s_ld.logfile, "=================================================================================\n");

		fclose( s_ld.logfile );
		s_ld.logfile = NULL;
	}
}

#if XASH_COLORIZE_CONSOLE == true
static void Sys_WriteEscapeSequenceForColorcode( int fd, int c )
{
	static const char *q3ToAnsi[ 8 ] =
	{
		"\033[30m", // COLOR_BLACK
		"\033[31m", // COLOR_RED
		"\033[32m", // COLOR_GREEN
		"\033[33m", // COLOR_YELLOW
		"\033[34m", // COLOR_BLUE
		"\033[36m", // COLOR_CYAN
		"\033[35m", // COLOR_MAGENTA
		"\033[0m", // COLOR_WHITE
	};
	const char *esc = q3ToAnsi[c];

	if( c == 7 )
		write( fd, esc, 4 );
	else write( fd, esc, 5 );
}
#else
static void Sys_WriteEscapeSequenceForColorcode( int fd, int c ) {}
#endif

static void Sys_PrintLogfile( const int fd, const char *logtime, const char *msg, const qboolean colorize )
{
	const char *p = msg;

	write( fd, logtime, Q_strlen( logtime ) );

	while( p && *p )
	{
		p = Q_strchr( msg, '^' );

		if( p == NULL )
		{
			write( fd, msg, Q_strlen( msg ));
			break;
		}
		else if( IsColorString( p ))
		{
			if( p != msg )
				write( fd, msg, p - msg );
			msg = p + 2;

			if( colorize )
				Sys_WriteEscapeSequenceForColorcode( fd, ColorIndex( p[1] ));
		}
		else
		{
			write( fd, msg, p - msg + 1 );
			msg = p + 1;
		}
	}

	// flush the color
	if( colorize )
		Sys_WriteEscapeSequenceForColorcode( fd, 7 );
}

static void Sys_PrintStdout( const char *logtime, const char *msg )
{
#if XASH_MOBILE_PLATFORM
	static char buf[MAX_PRINT_MSG];

	// strip color codes
	COM_StripColors( msg, buf );

	// platform-specific output
#if XASH_ANDROID && !XASH_DEDICATED
	__android_log_write( ANDROID_LOG_DEBUG, "Xash", buf );
#endif // XASH_ANDROID && !XASH_DEDICATED

#if TARGET_OS_IOS
	void IOS_Log( const char * );
	IOS_Log( buf );
#endif // TARGET_OS_IOS

#if XASH_NSWITCH && NSWITCH_DEBUG
	// just spew it to stderr normally in debug mode
	fprintf( stderr, "%s %s", logtime, buf );
#endif // XASH_NSWITCH && NSWITCH_DEBUG

#elif !XASH_WIN32 // Wcon does the job
	Sys_PrintLogfile( STDOUT_FILENO, logtime, msg, XASH_COLORIZE_CONSOLE );
	Sys_FlushStdout();
#endif
}

void Sys_PrintLog( const char *pMsg )
{
	time_t crt_time;
	const struct tm	*crt_tm;
	char logtime[32] = "";
	static char lastchar;

	time( &crt_time );
	crt_tm = localtime( &crt_time );

	if( !lastchar || lastchar == '\n')
		strftime( logtime, sizeof( logtime ), "[%H:%M:%S] ", crt_tm ); //short time

	// spew to stdout
	Sys_PrintStdout( logtime, pMsg );

	if( !s_ld.logfile )
	{
		// save last char to detect when line was not ended
		lastchar = pMsg[Q_strlen( pMsg ) - 1];
		return;
	}

	if( !lastchar || lastchar == '\n')
		strftime( logtime, sizeof( logtime ), "[%Y:%m:%d|%H:%M:%S] ", crt_tm ); //full time
	
	// save last char to detect when line was not ended
	lastchar = pMsg[Q_strlen( pMsg ) - 1];

	Sys_PrintLogfile( s_ld.logfileno, logtime, pMsg, false );
	Sys_FlushLogfile();
}

/*
=============================================================================

CONSOLE PRINT

=============================================================================
*/
/*
=============
Con_Printf

=============
*/
void GAME_EXPORT Con_Printf( const char *szFmt, ... )
{
	static char	buffer[MAX_PRINT_MSG];
	va_list		args;

	if( !host.allow_console )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	Sys_Print( buffer );
}

/*
=============
Con_DPrintf

=============
*/
void GAME_EXPORT Con_DPrintf( const char *szFmt, ... )
{
	static char	buffer[MAX_PRINT_MSG];
	va_list		args;

	if( host_developer.value < DEV_NORMAL )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	if( buffer[0] == '0' && buffer[1] == '\n' && buffer[2] == '\0' )
		return; // hlrally spam

	Sys_Print( buffer );
}

/*
=============
Con_Reportf

=============
*/
void Con_Reportf( const char *szFmt, ... )
{
	static char	buffer[MAX_PRINT_MSG];
	va_list		args;

	if( host_developer.value < DEV_EXTENDED )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	Sys_Print( buffer );
}


#if XASH_MESSAGEBOX == MSGBOX_STDERR
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	fprintf( stderr,
		 "======================================\n"
		 "%s: %s\n"
		 "======================================\n", title, message );
}
#endif

