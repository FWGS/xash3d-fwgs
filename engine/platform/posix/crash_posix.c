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

#include "common.h"

// while this is mostly POSIX-compatible functions
// the contents of ucontext_t is platform-dependent
// before adding new OS here, check platform.h
#if XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX
#ifndef XASH_OPENBSD
	#include <ucontext.h>
#endif
#include <signal.h>
#include <sys/mman.h>
#include "library.h"

#define STACK_BACKTRACE_STR     "Stack backtrace:\n"
#define STACK_DUMP_STR          "Stack dump:\n"

#define STACK_BACKTRACE_STR_LEN ( sizeof( STACK_BACKTRACE_STR ) - 1 )
#define STACK_DUMP_STR_LEN      ( sizeof( STACK_DUMP_STR ) - 1 )
#define ALIGN( x, y ) (((uintptr_t) ( x ) + (( y ) - 1 )) & ~(( y ) - 1 ))

static struct sigaction oldFilter;

static int Sys_PrintFrame( char *buf, int len, int i, void *addr )
{
	Dl_info dlinfo;
	if( len <= 0 )
		return 0; // overflow

		if( dladdr( addr, &dlinfo ))
		{
			if( dlinfo.dli_sname )
				return Q_snprintf( buf, len, "%2d: %p <%s+%lu> (%s)\n", i, addr, dlinfo.dli_sname,
								   (unsigned long)addr - (unsigned long)dlinfo.dli_saddr, dlinfo.dli_fname ); // print symbol, module and address

				return Q_snprintf( buf, len, "%2d: %p (%s)\n", i, addr, dlinfo.dli_fname ); // print module and address
		}

		return Q_snprintf( buf, len, "%2d: %p\n", i, addr ); // print only address
}

static void Sys_Crash( int signal, siginfo_t *si, void *context)
{
	void *pc = NULL, **bp = NULL, **sp = NULL; // this must be set for every OS!
	char message[8192];
	int len, logfd, i = 0;

#if XASH_OPENBSD
	struct sigcontext *ucontext = (struct sigcontext*)context;
#else
	ucontext_t *ucontext = (ucontext_t*)context;
#endif

#if XASH_AMD64
#if XASH_FREEBSD
	pc = (void*)ucontext->uc_mcontext.mc_rip;
	bp = (void**)ucontext->uc_mcontext.mc_rbp;
	sp = (void**)ucontext->uc_mcontext.mc_rsp;
#elif XASH_NETBSD
	pc = (void*)ucontext->uc_mcontext.__gregs[_REG_RIP];
	bp = (void**)ucontext->uc_mcontext.__gregs[_REG_RBP];
	sp = (void**)ucontext->uc_mcontext.__gregs[_REG_RSP];
#elif XASH_OPENBSD
	pc = (void*)ucontext->sc_rip;
	bp = (void**)ucontext->sc_rbp;
	sp = (void**)ucontext->sc_rsp;
#else
	pc = (void*)ucontext->uc_mcontext.gregs[REG_RIP];
	bp = (void**)ucontext->uc_mcontext.gregs[REG_RBP];
	sp = (void**)ucontext->uc_mcontext.gregs[REG_RSP];
#endif
#elif XASH_X86
#if XASH_FREEBSD
	pc = (void*)ucontext->uc_mcontext.mc_eip;
	bp = (void**)ucontext->uc_mcontext.mc_ebp;
	sp = (void**)ucontext->uc_mcontext.mc_esp;
#elif XASH_NETBSD
	pc = (void*)ucontext->uc_mcontext.__gregs[_REG_EIP];
	bp = (void**)ucontext->uc_mcontext.__gregs[_REG_EBP];
	sp = (void**)ucontext->uc_mcontext.__gregs[_REG_ESP];
#elif XASH_OPENBSD
	pc = (void*)ucontext->sc_eip;
	bp = (void**)ucontext->sc_ebp;
	sp = (void**)ucontext->sc_esp;
#else
	pc = (void*)ucontext->uc_mcontext.gregs[REG_EIP];
	bp = (void**)ucontext->uc_mcontext.gregs[REG_EBP];
	sp = (void**)ucontext->uc_mcontext.gregs[REG_ESP];
#endif
#elif XASH_ARM && XASH_64BIT
	pc = (void*)ucontext->uc_mcontext.pc;
	bp = (void*)ucontext->uc_mcontext.regs[29];
	sp = (void*)ucontext->uc_mcontext.sp;
#elif XASH_ARM
	pc = (void*)ucontext->uc_mcontext.arm_pc;
	bp = (void*)ucontext->uc_mcontext.arm_fp;
	sp = (void*)ucontext->uc_mcontext.arm_sp;
#endif

	// safe actions first, stack and memory may be corrupted
	len = Q_snprintf( message, sizeof( message ), "Ver: " XASH_ENGINE_NAME " " XASH_VERSION " (build %i-%s, %s-%s)\n",
					  Q_buildnum(), Q_buildcommit(), Q_buildos(), Q_buildarch() );

#if !XASH_FREEBSD && !XASH_NETBSD && !XASH_OPENBSD
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

	if( pc && bp && sp )
	{
		size_t pagesize = sysconf( _SC_PAGESIZE );

		// try to print backtrace
		write( STDERR_FILENO, STACK_BACKTRACE_STR, STACK_BACKTRACE_STR_LEN );
		write( logfd, STACK_BACKTRACE_STR, STACK_BACKTRACE_STR_LEN );
		Q_strncpy( message + len, STACK_BACKTRACE_STR, sizeof( message ) - len );
		len += STACK_BACKTRACE_STR_LEN;

		// false on success, true on failure
#define try_allow_read(pointer, pagesize) \
		(( mprotect( (char *)ALIGN( (pointer), (pagesize) ), (pagesize), PROT_READ | PROT_WRITE | PROT_EXEC ) == -1 ) && \
		( mprotect( (char *)ALIGN( (pointer), (pagesize) ), (pagesize), PROT_READ | PROT_EXEC ) == -1 ) && \
		( mprotect( (char *)ALIGN( (pointer), (pagesize) ), (pagesize), PROT_READ | PROT_WRITE ) == -1 ) && \
		( mprotect( (char *)ALIGN( (pointer), (pagesize) ), (pagesize), PROT_READ ) == -1 ))

		do
		{
			int line = Sys_PrintFrame( message + len, sizeof( message ) - len, ++i, pc);
			write( STDERR_FILENO, message + len, line );
			write( logfd, message + len, line );
			len += line;
			//if( !dladdr(bp,0) ) break; // only when bp is in module
			if( try_allow_read( bp, pagesize ) )
				break;
			if( try_allow_read( bp[0], pagesize ) )
				break;
			pc = bp[1];
			bp = (void**)bp[0];
		}
		while( bp && i < 128 );

		// try to print stack
		write( STDERR_FILENO, STACK_DUMP_STR, STACK_DUMP_STR_LEN );
		write( logfd, STACK_DUMP_STR, STACK_DUMP_STR_LEN );
		Q_strncpy( message + len, STACK_DUMP_STR, sizeof( message ) - len );
		len += STACK_DUMP_STR_LEN;

		if( !try_allow_read( sp, pagesize ) )
		{
			for( i = 0; i < 32; i++ )
			{
				int line = Sys_PrintFrame( message + len, sizeof( message ) - len, i, sp[i] );
				write( STDERR_FILENO, message + len, line );
				write( logfd, message + len, line );
				len += line;
			}
		}

#undef try_allow_read
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


	Sys_Quit();
}

void Sys_SetupCrashHandler( void )
{
	struct sigaction act = { 0 };
	act.sa_sigaction = Sys_Crash;
	act.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigaction( SIGSEGV, &act, &oldFilter );
	sigaction( SIGABRT, &act, &oldFilter );
	sigaction( SIGBUS,  &act, &oldFilter );
	sigaction( SIGILL,  &act, &oldFilter );
}

void Sys_RestoreCrashHandler( void )
{
	sigaction( SIGSEGV, &oldFilter, NULL );
	sigaction( SIGABRT, &oldFilter, NULL );
	sigaction( SIGBUS,  &oldFilter, NULL );
	sigaction( SIGILL,  &oldFilter, NULL );
}

#endif // XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX
