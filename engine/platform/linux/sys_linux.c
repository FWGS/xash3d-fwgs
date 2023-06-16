/*
sys_linux.c - Linux system utils
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dlfcn.h>
#include "platform/platform.h"

static void *g_hsystemd;
static int (*g_pfn_sd_notify)( int unset_environment, const char *state );

qboolean Sys_DebuggerPresent( void )
{
	char buf[4096];
	ssize_t num_read;
	int status_fd;

	status_fd = open( "/proc/self/status", O_RDONLY );
	if ( status_fd == -1 )
		return 0;

	num_read = read( status_fd, buf, sizeof( buf ) );
	close( status_fd );

	if ( num_read > 0 )
	{
		static const char TracerPid[] = "TracerPid:";
		const byte *tracer_pid;

		buf[num_read] = 0;
		tracer_pid    = (const byte*)Q_strstr( buf, TracerPid );
		if( !tracer_pid )
			return false;
		//printf( "%s\n", tracer_pid );
		while( *tracer_pid < '0' || *tracer_pid > '9'  )
			if( *tracer_pid++ == '\n' )
				return false;
		//printf( "%s\n", tracer_pid );
		return !!Q_atoi( (const char*)tracer_pid );
	}

	return false;
}

void Platform_SetStatus( const char *status )
{
	string notify_str;

	if( !g_pfn_sd_notify )
		return;

	// TODO: report STOPPING=1
	Q_snprintf( notify_str, sizeof( notify_str ),
		"READY=1\n"
		"WATCHDOG=1\n"
		"STATUS=%s\n", status );

	// Quote: In order to support both service managers that implement this
	// scheme and those which do not, it is generally recommended to ignore
	// the return value of this call
	// a1ba: ok, you asked for no error handling :)
	g_pfn_sd_notify( false, notify_str );
}

void Linux_Init( void )
{
	// sd_notify only for dedicated targets, don't waste time on full client
	if( !Host_IsDedicated( ))
		return;

	if(( g_hsystemd = dlopen( "libsystemd.so.0", RTLD_LAZY )) == NULL )
		return;

	if(( g_pfn_sd_notify = dlsym( g_hsystemd, "sd_notify" )) == NULL )
	{
		dlclose( g_hsystemd );
		g_hsystemd = NULL;
	}

	Con_Reportf( "%s: sd_notify found, will report status to systemd\n", __func__ );
}

void Linux_Shutdown( void )
{
	g_pfn_sd_notify = NULL;

	if( g_hsystemd )
	{
		dlclose( g_hsystemd );
		g_hsystemd = NULL;
	}
}
