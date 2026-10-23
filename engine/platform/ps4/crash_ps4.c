/*
crash_ps4.c - PlayStation 4 crash handler and watchdog
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

#include "platform/platform.h"
#include "platform/ps4/crash_ps4.h"
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <orbis/libkernel.h>

/*
=============================================================================

	CRASH HANDLER

Logs faulting address as module + offset, so it can be resolved with
llvm-addr2line on unstripped ELF from build directory

=============================================================================
*/

// signal numbers of the PS4 kernel (FreeBSD numbering)
#define PS4_SIGILL     4
#define PS4_SIGTRAP    5
#define PS4_SIGABRT    6
#define PS4_SIGFPE     8
#define PS4_SIGBUS     10
#define PS4_SIGSEGV    11
#define PS4_SIGSYS     12
#define PS4_SA_SIGINFO 0x40

#define PS4_MAX_MODULES 128

static pthread_t ps4_engine_thread;
static volatile qboolean ps4_engine_thread_valid;

volatile uint64_t ps4_last_log_time;
qboolean ps4_watchdog_enabled;

static OrbisKernelModuleInfo ps4_modules[PS4_MAX_MODULES];
static size_t ps4_num_modules;

static void PS4_CollectModules( void )
{
	OrbisKernelModule handles[PS4_MAX_MODULES];
	size_t count = 0;

	ps4_num_modules = 0;

	if( sceKernelGetModuleList( handles, PS4_MAX_MODULES, &count ) < 0 )
		return;

	for( size_t i = 0; i < count && i < PS4_MAX_MODULES; i++ )
	{
		OrbisKernelModuleInfo *info = &ps4_modules[ps4_num_modules];

		memset( info, 0, sizeof( *info ));
		info->size = sizeof( *info );

		if( sceKernelGetModuleInfo( handles[i], info ) >= 0 )
			ps4_num_modules++;
	}
}

// returns module name and offset from its first segment if address belongs to code of any module
static qboolean PS4_AddrToModule( uintptr_t addr, const char **name, uintptr_t *offset )
{
	for( size_t i = 0; i < ps4_num_modules; i++ )
	{
		const OrbisKernelModuleInfo *info = &ps4_modules[i];

		for( uint32_t j = 0; j < info->segmentCount && j < 4; j++ )
		{
			uintptr_t start = (uintptr_t)info->segmentInfo[j].address;

			if( addr >= start && addr < start + info->segmentInfo[j].size )
			{
				*name = info->name;
				*offset = addr - (uintptr_t)info->segmentInfo[0].address;
				return true;
			}
		}
	}

	return false;
}

MAYBE_UNUSED static void PS4_LogAddr( const char *prefix, uintptr_t addr )
{
	const char *name;
	uintptr_t offset;

	if( PS4_AddrToModule( addr, &name, &offset ))
		PS4_Log( "%s 0x%016lx %s+0x%lx\n", prefix, (unsigned long)addr, name, (unsigned long)offset );
	else
		PS4_Log( "%s 0x%016lx\n", prefix, (unsigned long)addr );
}

static void PS4_DumpContext( void *context )
{
	const uint64_t *ctx = (const uint64_t *)context;
	uintptr_t here = (uintptr_t)&ctx;
	const uintptr_t *stack;
	int found = 0;

	PS4_CollectModules( );

	PS4_Log( "thread %p%s\n", (void *)pthread_self( ),
		ps4_engine_thread_valid && pthread_equal( pthread_self( ), ps4_engine_thread ) ? " (engine)" : "" );

	// PS4 ucontext: FreeBSD amd64 mcontext starts at word 8
	if( context )
	{
		static const char *names[] =
		{
			"rdi", "rsi", "rdx", "rcx", "r8", "r9", "rax", "rbx", "rbp",
			"r10", "r11", "r12", "r13", "r14", "r15", "trapno", "addr", "flags", "err",
			"rip", "cs", "rflags", "rsp",
		};

		for( int i = 0; i < (int)ARRAYSIZE( names ); i++ )
		{
			uintptr_t v = ctx[9 + i];
			const char *name;
			uintptr_t offset;

			if( PS4_AddrToModule( v, &name, &offset ))
				PS4_Log( "%-6s 0x%016lx %s+0x%lx\n", names[i], (unsigned long)v, name, (unsigned long)offset );
			else
				PS4_Log( "%-6s 0x%016lx\n", names[i], (unsigned long)v );
		}
	}

	// scan interrupted thread stack for return addresses
	// rsp is ctx[31] on PS4, use it only if it's near our own stack
	stack = (const uintptr_t *)( here & ~(uintptr_t)7 );
	if( context )
	{
		uintptr_t rsp = ctx[31]; // see register names above

		if( rsp > here - 64 * 1024 * 1024 && rsp < here + 64 * 1024 * 1024 && !( rsp & 7 ))
		{
			PS4_Log( "rsp 0x%016lx\n", (unsigned long)rsp );
			stack = (const uintptr_t *)rsp;
		}
	}
	for( int i = 0; i < 4096 && found < 40; i++ )
	{
		const char *name;
		uintptr_t offset;

		if( PS4_AddrToModule( stack[i], &name, &offset ))
		{
			PS4_Log( "  stack[%4d] %s+0x%lx\n", i, name, (unsigned long)offset );
			found++;
		}
	}

	PS4_Log( "loaded modules:\n" );
	for( size_t i = 0; i < ps4_num_modules; i++ )
	{
		PS4_Log( "  %s at %p, size 0x%x\n", ps4_modules[i].name,
			ps4_modules[i].segmentInfo[0].address, ps4_modules[i].segmentInfo[0].size );
	}
}

static void PS4_CrashHandler( int sig, siginfo_t *si, void *context )
{
	static volatile int crashed;
	void *fault_addr = NULL;

	if( crashed++ )
		_exit( 1 );

	// FreeBSD siginfo: si_addr follows six ints
	if( si )
		memcpy( &fault_addr, (const byte *)si + 24, sizeof( fault_addr ));

	PS4_Log( "\n*** CRASH: signal %d, fault address %p ***\n", sig, fault_addr );
	PS4_DumpContext( context );
	_exit( 1 );
}

/*
==================
PS4_Watchdog

If engine prints nothing for a while, it's probably stuck: log where
==================
*/
#define PS4_SIGUSR1         30
#define PS4_WATCHDOG_USEC   ( 10 * 1000 * 1000 )

static void PS4_WatchdogSignal( int sig, siginfo_t *si, void *context )
{
	PS4_Log( "\n*** WATCHDOG: engine thread state ***\n" );
	PS4_DumpContext( context );
	PS4_Log( "*** WATCHDOG: end ***\n" );
}

static void *PS4_WatchdogThread( void *arg )
{
	while( 1 )
	{
		sceKernelUsleep( 1000 * 1000 );

		if( !ps4_engine_thread_valid || !ps4_last_log_time )
			continue;

		if( sceKernelGetProcessTime( ) - ps4_last_log_time > PS4_WATCHDOG_USEC )
		{
			PS4_Log( "watchdog: no output for %d seconds\n", PS4_WATCHDOG_USEC / 1000000 );
			pthread_kill( ps4_engine_thread, PS4_SIGUSR1 );
			break; // only once, it's a diagnostic
		}
	}

	return NULL;
}

void PS4_InstallCrashHandler( void )
{
	const int signals[] = { PS4_SIGILL, PS4_SIGTRAP, PS4_SIGABRT, PS4_SIGFPE, PS4_SIGBUS, PS4_SIGSEGV, PS4_SIGSYS };
	struct sigaction sa = { 0 };

	sa.__sa_handler.__sa_sigaction = (void *)PS4_CrashHandler;
	sa.sa_flags = PS4_SA_SIGINFO;

	for( size_t i = 0; i < ARRAYSIZE( signals ); i++ )
	{
		if( sigaction( signals[i], &sa, NULL ) < 0 )
			PS4_Log( "sigaction %d failed: %s\n", signals[i], strerror( errno ));
	}

	sa.__sa_handler.__sa_sigaction = (void *)PS4_WatchdogSignal;
	if( sigaction( PS4_SIGUSR1, &sa, NULL ) < 0 )
		PS4_Log( "sigaction %d failed: %s\n", PS4_SIGUSR1, strerror( errno ));
}

/*
==================
PS4_StartWatchdog

Remembers the thread the engine runs on, starts the watchdog when it's enabled
==================
*/
void PS4_StartWatchdog( void )
{
	pthread_t watchdog;

	ps4_engine_thread = pthread_self( );
	ps4_engine_thread_valid = true;

	// diagnostic for hangs during startup, enabled with -ps4watchdog in xash3d.cmdline
	if( ps4_watchdog_enabled && pthread_create( &watchdog, NULL, PS4_WatchdogThread, NULL ) == 0 )
		pthread_detach( watchdog );
}
