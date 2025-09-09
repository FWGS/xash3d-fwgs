/*
sys_win.c - posix system utils
Copyright (C) 2019 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <unistd.h> // fork
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "platform/platform.h"
#include "menu_int.h"

static qboolean Sys_FindExecutable( const char *baseName, char *buf, size_t size )
{
	char *envPath;
	char *part;
	size_t length;
	size_t baseNameLength;
	size_t needTrailingSlash;

	if( !baseName || !baseName[0] )
		return false;

	envPath = getenv( "PATH" );
	if( !COM_CheckString( envPath ) )
		return false;

	baseNameLength = Q_strlen( baseName );
	while( *envPath )
	{
		part = Q_strchr( envPath, ':' );
		if( part )
			length = part - envPath;
		else
			length = Q_strlen( envPath );

		if( length > 0 )
		{
			needTrailingSlash = ( envPath[length - 1] == '/' ) ? 0 : 1;
			if( length + baseNameLength + needTrailingSlash < size )
			{
				string temp;

				Q_strncpy( temp, envPath, length + 1 );
				Q_snprintf( buf, size, "%s%s%s",
					temp, needTrailingSlash ? "/" : "", baseName );

				if( access( buf, X_OK ) == 0 )
					return true;
			}
		}

		envPath += length;
		if( *envPath == ':' )
			envPath++;
	}
	return false;
}

#if !XASH_ANDROID && !XASH_NSWITCH && !XASH_PSVITA
void Platform_ShellExecute( const char *path, const char *parms )
{
	char xdgOpen[128];

	if( !Q_strcmp( path, GENERIC_UPDATE_PAGE ) || !Q_strcmp( path, PLATFORM_UPDATE_PAGE ))
		path = DEFAULT_UPDATE_PAGE;

	if( Sys_FindExecutable( OPEN_COMMAND, xdgOpen, sizeof( xdgOpen ) ) )
	{
		const char *argv[] = { xdgOpen, path, NULL };
		pid_t id = fork( );
		if( id == 0 )
		{
			execv( xdgOpen, (char **)argv );
			fprintf( stderr, "error opening %s %s", xdgOpen, path );
			_exit( 1 );
		}
	}
	else
	{
		Con_Reportf( S_WARN "Could not find "OPEN_COMMAND" utility\n" );
	}
}
#endif // XASH_ANDROID

void Posix_Daemonize( void )
{
	if( Sys_CheckParm( "-daemonize" ))
	{
#if XASH_POSIX && defined(_POSIX_VERSION) && !defined(XASH_MOBILE_PLATFORM)
		pid_t daemon;

		daemon = fork();

		if( daemon < 0 )
		{
			Host_Error( "fork() failed: %s\n", strerror( errno ) );
		}

		if( daemon > 0 )
		{
			// parent
			Con_Reportf( "Child pid: %i\n", daemon );
			exit( 0 );
		}
		else
		{
			// don't be closed by parent
			if( setsid() < 0 )
			{
				Host_Error( "setsid() failed: %s\n", strerror( errno ) );
			}

			// set permissions
			umask( 0 );

			// engine will still use stdin/stdout,
			// so just redirect them to /dev/null
			close( STDIN_FILENO );
			close( STDOUT_FILENO );
			close( STDERR_FILENO );
			open("/dev/null", O_RDONLY); // becomes stdin
			open("/dev/null", O_RDWR); // stdout
			open("/dev/null", O_RDWR); // stderr

			// fallthrough
		}
#elif defined(XASH_MOBILE_PLATFORM)
		Sys_Error( "Can't run in background on mobile platforms!" );
#else
		Sys_Error( "Daemonize not supported on this platform!" );
#endif
	}

}

static void Posix_SigtermCallback( int signal )
{
	string reason;
	Q_snprintf( reason, sizeof( reason ), "caught signal %d", signal );
	Sys_Quit( reason );
}

void Posix_SetupSigtermHandling( void )
{
#if !XASH_PSVITA
	struct sigaction act = { 0 };
	act.sa_handler = Posix_SigtermCallback;
	act.sa_flags = 0;
	sigaction( SIGTERM, &act, NULL );
#endif
}

#if XASH_TIMER == TIMER_POSIX
double Platform_DoubleTime( void )
{
	struct timespec ts;
#if XASH_IRIX
	clock_gettime( CLOCK_SGI_CYCLE, &ts );
#else
	clock_gettime( CLOCK_MONOTONIC, &ts );
#endif
	return (double) ts.tv_sec + (double) ts.tv_nsec/1000000000.0;
}

void Platform_Sleep( int msec )
{
	usleep( msec * 1000 );
}
#endif // XASH_TIMER == TIMER_POSIX

