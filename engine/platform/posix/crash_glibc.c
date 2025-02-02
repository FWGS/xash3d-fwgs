/*
crashhandler.c - advanced crashhandler
Copyright (C) 2016 Mittorn

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
#include "common.h"
#include <execinfo.h>
#include <signal.h>

void Sys_Crash( int signal, siginfo_t *si, void *context )
{
	char message[8192];
	int len, logfd, i = 0;
	int size;
	void *addrs[16];
	char **syms;

	// safe actions first, stack and memory may be corrupted
	len = Q_snprintf( message, sizeof( message ), "Ver: " XASH_ENGINE_NAME " " XASH_VERSION " (build %i-%s, %s-%s)\n",
					  Q_buildnum(), g_buildcommit, Q_buildos(), Q_buildarch() );

#if !XASH_FREEBSD && !XASH_NETBSD && !XASH_OPENBSD && !XASH_APPLE // they don't have si_ptr
	len += Q_snprintf( message + len, sizeof( message ) - len, "Crash: signal %d errno %d with code %d at %p %p\n", signal, si->si_errno, si->si_code, si->si_addr, si->si_ptr );
#else
	len += Q_snprintf( message + len, sizeof( message ) - len, "Crash: signal %d errno %d with code %d at %p\n", signal, si->si_errno, si->si_code, si->si_addr );
#endif

	write( STDERR_FILENO, message, len );

	// flush buffers before writing directly to descriptors
	fflush( stdout );
	fflush( stderr );

	// now get log fd and write trace directly to log
	logfd = Sys_LogFileNo();
	write( logfd, message, len );

	size = backtrace( addrs, sizeof( addrs ) / sizeof( addrs[0] ));
	syms = backtrace_symbols( addrs, size );

	for( i = 0; i < size && syms; i++ )
	{
		size_t symlen = Q_strlen( syms[i] );
		char ch = '\n';

		write( logfd, syms[i], symlen );
		write( logfd, &ch, 1 );

		len += Q_snprintf( message + len, sizeof( message ) - len, "%2d: %s\n", i, syms[i] );
	}

	// put MessageBox as Sys_Error
	Msg( "%s\n", message );
#ifdef XASH_SDL
	SDL_SetWindowGrab( host.hWnd, SDL_FALSE );
#endif
	host.crashed = true;
	Platform_MessageBox( "Xash Error", message, false );

	// log saved, now we can try to save configs and close log correctly, it may crash
	if( host.type == HOST_NORMAL )
		CL_Crashed();
	host.status = HOST_CRASHED;

	Sys_Quit( "crashed" );
}

#endif // HAVE_EXECINFO
