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
#if XASH_ANDROID
#include <android/log.h>
#endif
#include <string.h>
#include <errno.h>
#if XASH_IRIX
#include <sys/time.h>
#endif
#include "xash3d_mathlib.h"

// do not waste precious CPU cycles on mobiles or low memory devices
#if !XASH_WIN32 && !XASH_MOBILE_PLATFORM && !XASH_LOW_MEMORY && !XASH_EMSCRIPTEN
#define XASH_COLORIZE_CONSOLE 1
#else
#define XASH_COLORIZE_CONSOLE 0
#endif

static struct logdata_s {
	char     title[64];
	qboolean log_active;
	qboolean log_time;
	char     log_path[MAX_SYSPATH];
	FILE     *logfile;
	int      logfileno;
} s_ld;

void Sys_DestroyConsole( void )
{
	// last text message into console or log
	Con_Reportf( "%s: Exiting!\n", __func__ );
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
	const char *mode;

	if( Sys_CheckParm( "-log" ))
	{
		if( !Sys_GetParmFromCmdLine( "-log", s_ld.log_path ) || !isalnum( s_ld.log_path[0] ))
			Q_strncpy( s_ld.log_path, "engine.log", sizeof( s_ld.log_path ));

		COM_DefaultExtension( s_ld.log_path, ".log", sizeof( s_ld.log_path ));
		s_ld.log_active = true;
	}

	s_ld.log_time = Sys_CheckParm( "-logtime" );

	if( host.change_game && host.type != HOST_DEDICATED )
		mode = "a";
	else mode = "w";

	if( Host_IsDedicated( ))
		Q_strncpy( s_ld.title, XASH_DEDICATED_SERVER_NAME " " XASH_VERSION, sizeof( s_ld.title ));
	else Q_strncpy( s_ld.title, XASH_ENGINE_NAME " " XASH_VERSION, sizeof( s_ld.title ));

	// create log if needed
	if( s_ld.log_active )
	{
		s_ld.logfile = fopen( s_ld.log_path, mode );

		if ( !s_ld.logfile )
		{
			Con_Reportf( S_ERROR "%s: can't create log file %s: %s\n", __func__, s_ld.log_path, strerror( errno ));
			return;
		}

		s_ld.logfileno = fileno( s_ld.logfile );

		// fit to 80 columns for easier read on standard terminal
		fputs( "================================================================================\n", s_ld.logfile );
		fprintf( s_ld.logfile, "%s (%i, %s, %s, %s-%s)\n", s_ld.title, Q_buildnum(), g_buildcommit, g_buildbranch, Q_buildos(), Q_buildarch());
		fprintf( s_ld.logfile, "Game started at %s\n", Q_timestamp( TIME_FULL ));
		fputs( "================================================================================\n", s_ld.logfile );
		fflush( s_ld.logfile );
	}
}

void Sys_CloseLog( const char *finalmsg )
{
	Sys_FlushStdout(); // flush to stdout to ensure all data was written

	if( !s_ld.logfile )
		return;

	// continue logged
	if( !finalmsg )
	{
		switch( host.status )
		{
		case HOST_CRASHED:
			finalmsg = "crashed";
			break;
		case HOST_ERR_FATAL:
			finalmsg = "stopped with error";
			break;
		default:
			finalmsg = "stopped";
			break;
		}
	}

	fputc( '\n', s_ld.logfile );
	fputs( "================================================================================\n", s_ld.logfile );
	fprintf( s_ld.logfile, "%s (%i, %s, %s, %s-%s)\n", s_ld.title, Q_buildnum(), g_buildcommit, g_buildbranch, Q_buildos(), Q_buildarch());
	fprintf( s_ld.logfile, "Stopped with reason \"%s\" at %s\n", finalmsg, Q_timestamp( TIME_FULL ));
	fputs( "================================================================================\n", s_ld.logfile );
	fclose( s_ld.logfile );
	s_ld.logfile = NULL;
}

#if XASH_COLORIZE_CONSOLE
static qboolean Sys_WriteEscapeSequenceForColorcode( int fd, int c )
{
	static const char *q3ToAnsi[ 8 ] =
	{
		"\033[1;30m", // COLOR_BLACK
		"\033[1;31m", // COLOR_RED
		"\033[1;32m", // COLOR_GREEN
		"\033[1;33m", // COLOR_YELLOW
		"\033[1;34m", // COLOR_BLUE
		"\033[1;36m", // COLOR_CYAN
		"\033[1;35m", // COLOR_MAGENTA
		"\033[0m", // COLOR_WHITE
	};
	const char *esc = q3ToAnsi[c];

	return write( fd, esc, c == 7 ? 4 : 7 ) < 0 ? false : true;
}
#else
static qboolean Sys_WriteEscapeSequenceForColorcode( int fd, int c )
{
	return true;
}
#endif

static void Sys_PrintLogfile( const int fd, const char *logtime, size_t logtime_len, const char *msg, const int colorize )
{
	const char *p = msg;

	if( logtime_len != 0 )
	{
		if( write( fd, logtime, logtime_len ) < 0 )
		{
			// not critical for us
		}
	}

	while( p && *p )
	{
		p = Q_strchr( msg, '^' );

		if( p == NULL )
		{
			if( write( fd, msg, Q_strlen( msg )) < 0 )
			{
				// don't call engine Msg, might cause recursion
				fprintf( stderr, "%s: write failed: %s\n", __func__, strerror( errno ));
			}
			break;
		}
		else if( IsColorString( p ))
		{
			if( p != msg )
			{
				if( write( fd, msg, p - msg ) < 0 )
					fprintf( stderr, "%s: write failed: %s\n", __func__, strerror( errno ));
			}
			msg = p + 2;

			if( colorize )
				Sys_WriteEscapeSequenceForColorcode( fd, ColorIndex( p[1] ));
		}
		else
		{
			if( write( fd, msg, p - msg + 1 ) < 0 )
				fprintf( stderr, "%s: write failed: %s\n", __func__, strerror( errno ));
			msg = p + 1;
		}
	}

	// flush the color
	if( colorize )
		Sys_WriteEscapeSequenceForColorcode( fd, 7 );
}

static void Sys_PrintStdout( const char *logtime, size_t logtime_len, const char *msg )
{
#if XASH_MOBILE_PLATFORM
	static char buf[MAX_PRINT_MSG];

	// strip color codes
	COM_StripColors( msg, buf );

	// platform-specific output
#if XASH_ANDROID && !XASH_DEDICATED
	__android_log_write( ANDROID_LOG_INFO, "Xash", buf );
#endif // XASH_ANDROID && !XASH_DEDICATED

#if TARGET_OS_IOS
	void IOS_Log( const char * );
	IOS_Log( buf );
#endif // TARGET_OS_IOS

#if XASH_NSWITCH && NSWITCH_DEBUG
	// just spew it to stderr normally in debug mode
	fprintf( stderr, "%s %s", logtime, buf );
#endif // XASH_NSWITCH && NSWITCH_DEBUG

#if XASH_PSVITA
	// spew to stderr only in developer mode
	if( host_developer.value )
	{
		fprintf( stderr, "%s %s", logtime, buf );
	}
#endif

#elif !XASH_WIN32 // Wcon does the job
	Sys_PrintLogfile( STDOUT_FILENO, logtime, logtime_len, msg, XASH_COLORIZE_CONSOLE );
	Sys_FlushStdout();
#endif
}

void Sys_PrintLog( const char *pMsg )
{
	time_t crt_time;
	const struct tm	*crt_tm;
	char logtime[32] = "";
	static char lastchar;
	qboolean print_time = false;
	size_t len, logtime_len = 0;

	if( !lastchar || lastchar == '\n' )
	{
		if( time( &crt_time ) >= 0 )
		{
			crt_tm = localtime( &crt_time );
			print_time = crt_tm != NULL;
		}
	}

	if( print_time )
	{
		logtime_len = strftime( logtime, sizeof( logtime ), "[%H:%M:%S] ", crt_tm ); // short time
		logtime_len = Q_min( logtime_len, sizeof( logtime ) - 1 ); // just in case
	}

	// spew to stdout
	Sys_PrintStdout( logtime, logtime_len, pMsg );

	len = Q_strlen( pMsg );

	// save last char to detect when line was not ended
	lastchar = len > 0 ? pMsg[len - 1] : 0;

	// spew to engine.log
	if( s_ld.logfile )
	{
		if( s_ld.log_time && print_time )
		{
			logtime_len = strftime( logtime, sizeof( logtime ), "[%Y:%m:%d|%H:%M:%S] ", crt_tm ); //full time
			logtime_len = Q_min( logtime_len, sizeof( logtime ) - 1 ); // just in case
		}
		else
		{
			logtime[0] = '\0';
			logtime_len = 0;
		}

		Sys_PrintLogfile( s_ld.logfileno, logtime, logtime_len, pMsg, false );
		Sys_FlushLogfile();
	}
}

/*
=============================================================================

CONSOLE PRINT

=============================================================================
*/
static void Con_Printfv( qboolean debug, const char *szFmt, va_list args )
{
	static char buffer[MAX_PRINT_MSG];
	qboolean add_newline;

	add_newline = Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args ) < 0;

	if( debug && !Q_strcmp( buffer, "0\n" ))
		return; // hlrally spam

	Sys_Print( buffer );
	if( add_newline )
		Sys_Print( "\n" );
}

/*
=============
Con_Printf

=============
*/
void GAME_EXPORT Con_Printf( const char *szFmt, ... )
{
	va_list args;

	if( !host.allow_console )
		return;

	va_start( args, szFmt );
	Con_Printfv( false, szFmt, args );
	va_end( args );
}

/*
=============
Con_DPrintf

=============
*/
void GAME_EXPORT Con_DPrintf( const char *szFmt, ... )
{
	va_list args;

	if( host_developer.value < DEV_NORMAL )
		return;

	va_start( args, szFmt );
	Con_Printfv( true, szFmt, args );
	va_end( args );
}

/*
=============
Con_Reportf

=============
*/
void Con_Reportf( const char *szFmt, ... )
{
	va_list args;

	if( host_developer.value < DEV_EXTENDED )
		return;

	va_start( args, szFmt );
	Con_Printfv( false, szFmt, args );
	va_end( args );
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

