/*
proc.c - subprocess utilities
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "xcf.h"

/*
============
RunCapture

Spawn argv as a child, capture stdout into `out`.
============
*/
int RunCapture( char *const argv[], xcf_buf_t *out, qboolean silent_stderr )
{
	int p[2];
	pid_t pid;
	char buf[8192];
	int status;

	if( pipe( p ) < 0 )
		return -1;

	pid = fork();
	if( pid < 0 )
	{
		close( p[0] );
		close( p[1] );
		return -1;
	}

	if( pid == 0 )
	{
		close( p[0] );
		dup2( p[1], STDOUT_FILENO );
		close( p[1] );
		if( silent_stderr )
		{
			int devnull = open( "/dev/null", O_WRONLY );

			if( devnull >= 0 )
			{
				dup2( devnull, STDERR_FILENO );
				close( devnull );
			}
		}
		execvp( argv[0], argv );
		_exit( 127 );
	}

	close( p[1] );

	for( ;; )
	{
		ssize_t r = read( p[0], buf, sizeof( buf ));

		if( r < 0 )
		{
			if( errno == EINTR )
				continue;
			break;
		}
		if( r == 0 )
			break;
		if( !BufPutMem( out, buf, r ))
		{
			close( p[0] );
			kill( pid, SIGKILL );
			waitpid( pid, NULL, 0 );
			return -1;
		}
	}
	close( p[0] );

	if( waitpid( pid, &status, 0 ) < 0 )
		return -1;

	// heh wife excited
	if( WIFEXITED( status ))
		return WEXITSTATUS( status );

	return -1;
}
