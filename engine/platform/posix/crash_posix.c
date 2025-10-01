/*
crash_posix.c - advanced crashhandler
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

#include "common.h"

// while this is mostly POSIX-compatible functions
// the contents of ucontext_t is platform-dependent
// before adding new OS here, check platform.h
#define _XOPEN_SOURCE 1 // required for ucontext
#if XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX || XASH_APPLE
#ifndef XASH_OPENBSD
	#include <ucontext.h>
#endif
#include <signal.h>
#include <sys/mman.h>
#include "library.h"
#include "input.h"
#include "crash.h"

static qboolean have_libbacktrace = false;

static void Sys_Crash( int signal, siginfo_t *si, void *context )
{
	char message[8192];
	int len, logfd, i = 0;
	qboolean detailed_message = false;

	// safe actions first, stack and memory may be corrupted
	len = Q_snprintf( message, sizeof( message ), "Ver: " XASH_ENGINE_NAME " " XASH_VERSION " (build %i-%s-%s, %s-%s)\n",
		Q_buildnum(), g_buildcommit, g_buildbranch, Q_buildos(), Q_buildarch() );

#if !XASH_FREEBSD && !XASH_NETBSD && !XASH_OPENBSD && !XASH_APPLE
	len += Q_snprintf( message + len, sizeof( message ) - len, "Crash: signal %d errno %d with code %d at %p %p\n", signal, si->si_errno, si->si_code, si->si_addr, si->si_ptr );
#else
	len += Q_snprintf( message + len, sizeof( message ) - len, "Crash: signal %d errno %d with code %d at %p\n", signal, si->si_errno, si->si_code, si->si_addr );
#endif

	write( STDERR_FILENO, message, len );

	// now get log fd and write trace directly to log
	logfd = Sys_LogFileNo();
	write( logfd, message, len );

#if HAVE_LIBBACKTRACE
	if( have_libbacktrace && !detailed_message )
	{
		len = Sys_CrashDetailsLibbacktrace( logfd, message, len, sizeof( message ));
		detailed_message = true;
	}
#endif // HAVE_LIBBACKTRACE

#if HAVE_EXECINFO
	if( !detailed_message )
	{
		len = Sys_CrashDetailsExecinfo( logfd, message, len, sizeof( message ));
		detailed_message = true;
	}
#endif // HAVE_EXECINFO

#if !XASH_DEDICATED
	IN_SetMouseGrab( false );
#endif
	host.status = HOST_CRASHED;

	// put MessageBox as Sys_Error
	Platform_MessageBox( "Xash Error", message, false );

	// log saved, now we can try to save configs and close log correctly, it may crash
	if( host.type == HOST_NORMAL )
		CL_Crashed();

	Sys_Quit( "crashed" );
}

static struct sigaction old_segv_act;
static struct sigaction old_abrt_act;
static struct sigaction old_bus_act;
static struct sigaction old_ill_act;

void Sys_SetupCrashHandler( const char *argv0 )
{
	struct sigaction act =
	{
		.sa_sigaction = Sys_Crash,
		.sa_flags = SA_SIGINFO | SA_ONSTACK,
	};

#if HAVE_LIBBACKTRACE
	have_libbacktrace = Sys_SetupLibbacktrace( argv0 );
#endif // HAVE_LIBBACKTRACE

	sigaction( SIGSEGV, &act, &old_segv_act );
	sigaction( SIGABRT, &act, &old_abrt_act );
	sigaction( SIGBUS,  &act, &old_bus_act );
	sigaction( SIGILL,  &act, &old_ill_act );

}

void Sys_RestoreCrashHandler( void )
{
	sigaction( SIGSEGV, &old_segv_act, NULL );
	sigaction( SIGABRT, &old_abrt_act, NULL );
	sigaction( SIGBUS,  &old_bus_act, NULL );
	sigaction( SIGILL,  &old_ill_act, NULL );
}

#endif // XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX
