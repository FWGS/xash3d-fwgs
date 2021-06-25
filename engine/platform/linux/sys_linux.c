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
#include "platform/platform.h"

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
