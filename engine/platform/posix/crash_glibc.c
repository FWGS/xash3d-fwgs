/*
crash_glibc.c - advanced crashhandler based on glibc's execinfo API
Copyright (C) 2016 Mittorn
Copyright (C) 2025 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// on Glibc (which potentially might not be only Linux) systems we
// have backtrace() and backtrace_symbols() calls, which replace for us
// platform-specific code
#if HAVE_EXECINFO
#include <execinfo.h>
#include <signal.h>
#include "common.h"
#include "input.h"
#include "crash.h"

int Sys_CrashDetailsExecinfo( int logfd, char *message, int len, size_t max_len )
{
	void *addrs[16];
	int size = backtrace( addrs, sizeof( addrs ) / sizeof( addrs[0] ));
	char **syms = backtrace_symbols( addrs, size );

	for( int i = 0; i < size && syms; i++ )
	{
		size_t symlen = Q_strlen( syms[i] );
		char ch = '\n';

		write( logfd, syms[i], symlen );
		write( logfd, &ch, 1 );

		write( STDERR_FILENO, syms[i], symlen );
		write( STDERR_FILENO, &ch, 1 );

		len += Q_snprintf( message + len, max_len - len, "%2d: %s\n", i, syms[i] );
	}

	return len;
}
#endif // HAVE_EXECINFO
