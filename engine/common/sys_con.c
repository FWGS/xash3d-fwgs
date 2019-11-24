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

#if !XASH_WIN32 && !XASH_MOBILE_PLATFORM
#define XASH_COLORIZE_CONSOLE
// use with caution, running engine in Qt Creator may cause a freeze in read() call
// I was never encountered this bug anywhere else, so still enable by default
// #define XASH_USE_SELECT
#endif

#ifdef XASH_USE_SELECT
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
#ifdef XASH_USE_SELECT
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
		if( !s_ld.logfile ) Con_Reportf( S_ERROR  "Sys_InitLog: can't create log file %s\n", s_ld.log_path );

		fprintf( s_ld.logfile, "=================================================================================\n" );
		fprintf( s_ld.logfile, "\t%s (build %i) started at %s\n", s_ld.title, Q_buildnum(), Q_timestamp( TIME_FULL ));
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

void Sys_PrintLog( const char *pMsg )
{
	time_t		crt_time;
	const struct tm	*crt_tm;
	char logtime[32] = "";
	static char lastchar;

	time( &crt_time );
	crt_tm = localtime( &crt_time );
#if XASH_ANDROID && !XASH_DEDICATED
	__android_log_print( ANDROID_LOG_DEBUG, "Xash", "%s", pMsg );
#endif

#if TARGET_OS_IOS
	void IOS_Log(const char*);
	IOS_Log(pMsg);
#endif


	if( !lastchar || lastchar == '\n')
		strftime( logtime, sizeof( logtime ), "[%H:%M:%S] ", crt_tm ); //short time

#ifdef XASH_COLORIZE_CONSOLE
	{
		char colored[4096];
		const char *msg = pMsg;
		int len = 0;
		while( *msg && ( len < 4090 ) )
		{
			static char q3ToAnsi[ 8 ] =
			{
				'0', // COLOR_BLACK
				'1', // COLOR_RED
				'2', // COLOR_GREEN
				'3', // COLOR_YELLOW
				'4', // COLOR_BLUE
				'6', // COLOR_CYAN
				'5', // COLOR_MAGENTA
				0 // COLOR_WHITE
			};

			if( IsColorString( msg ) )
			{
				int color;

				msg++;
				color = q3ToAnsi[ *msg++ % 8 ];
				colored[len++] = '\033';
				colored[len++] = '[';
				if( color )
				{
					colored[len++] = '3';
					colored[len++] = color;
				}
				else
					colored[len++] = '0';
				colored[len++] = 'm';
			}
			else
				colored[len++] = *msg++;
		}
		colored[len] = 0;
		printf( "\033[34m%s\033[0m%s\033[0m", logtime, colored );

	}
#else
#if !XASH_ANDROID || XASH_DEDICATED
	printf( "%s %s", logtime, pMsg );
	fflush( stdout );
#endif
#endif

	// save last char to detect when line was not ended
	lastchar = pMsg[strlen(pMsg)-1];

	if( !s_ld.logfile )
		return;

	if( !lastchar || lastchar == '\n')
		strftime( logtime, sizeof( logtime ), "[%Y:%m:%d|%H:%M:%S]", crt_tm ); //full time

	fprintf( s_ld.logfile, "%s %s", logtime, pMsg );
	fflush( s_ld.logfile );
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
void Con_Printf( const char *szFmt, ... )
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
void Con_DPrintf( const char *szFmt, ... )
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
