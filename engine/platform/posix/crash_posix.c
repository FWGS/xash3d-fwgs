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

#if XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX || XASH_APPLE
#include <signal.h>
#include <sys/mman.h>
#if XASH_ANDROID
#include <sys/stat.h>
#include <fcntl.h>
#include <android/log.h>
#endif
#include "library.h"
#include "input.h"
#include "crash.h"

#if XASH_ANDROID
static char crashlog_path[MAX_OSPATH];
#endif

static qboolean have_libbacktrace = false;
static char crash_message[8192];

static void Sys_Crash( int signal, siginfo_t *si, void *context )
{
	// safe actions first, stack and memory may be corrupted
	int len = Q_snprintf( crash_message, sizeof( crash_message ), "Ver: " XASH_ENGINE_NAME " " XASH_VERSION " (build %i-%s-%s, %s-%s)\n",
		Q_buildnum(), g_buildcommit, g_buildbranch, Q_buildos(), Q_buildarch() );

#if !XASH_FREEBSD && !XASH_NETBSD && !XASH_OPENBSD && !XASH_APPLE
	len += Q_snprintf( crash_message + len, sizeof( crash_message ) - len, "Crash: signal %d errno %d with code %d at %p %p\n", signal, si->si_errno, si->si_code, si->si_addr, si->si_ptr );
#else
	len += Q_snprintf( crash_message + len, sizeof( crash_message ) - len, "Crash: signal %d errno %d with code %d at %p\n", signal, si->si_errno, si->si_code, si->si_addr );
#endif

	write( STDERR_FILENO, crash_message, len );

#if XASH_ANDROID
	__android_log_write( ANDROID_LOG_FATAL, "Xash", crash_message );
#endif

	// now get log fd and write trace directly to log
	int logfd = Sys_LogFileNo();
	if( logfd >= 0 )
		write( logfd, crash_message, len );

#if HAVE_LIBBACKTRACE
	qboolean detailed_message = false;
	if( have_libbacktrace && !detailed_message )
	{
		len = Sys_CrashDetailsLibbacktrace( logfd, crash_message, len, sizeof( crash_message ));
		detailed_message = true;
	}
#endif // HAVE_LIBBACKTRACE

#if XASH_ANDROID
	// also write to a dedicated crash report file the Java side picks up on next launch
	if( crashlog_path[0] )
	{
		int crashfd = open( crashlog_path, O_WRONLY|O_CREAT|O_TRUNC, 0644 );
		if( crashfd >= 0 )
		{
			write( crashfd, crash_message, len );
			close( crashfd );
		}
	}

	// JNI/SDL calls aren't safe from a signal handler on Android
	_exit( 128 + signal );
#else
#if !XASH_DEDICATED
	IN_SetMouseGrab( false );
#endif
	host.status = HOST_CRASHED;

	// put MessageBox as Sys_Error
	Platform_MessageBox( "Xash Error", crash_message, false );

	// log saved, now we can try to save configs and close log correctly, it may crash
	if( host.type == HOST_NORMAL )
		CL_Crashed();

	Sys_Quit( "crashed" );
#endif // XASH_ANDROID
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

#if XASH_ANDROID
	const char *crashdir = getenv( "XASH3D_CRASH_DIR" );

	if( !COM_StringEmptyOrNULL( crashdir ))
		Q_snprintf( crashlog_path, sizeof( crashlog_path ), "%s/crash.log", crashdir );

	// unblock the engine/SDL_main thread just in case
	sigset_t set;
	sigemptyset( &set );
	sigaddset( &set, SIGSEGV );
	sigaddset( &set, SIGABRT );
	sigaddset( &set, SIGBUS );
	sigaddset( &set, SIGILL );
	pthread_sigmask( SIG_UNBLOCK, &set, NULL );
#endif

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
