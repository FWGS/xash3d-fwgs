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
#define _GNU_SOURCE

#include "common.h"

/*
================
Sys_Crash

Crash handler, called from system
================
*/
#if XASH_CRASHHANDLER == CRASHHANDLER_DBGHELP || XASH_CRASHHANDLER == CRASHHANDLER_WIN32
#if XASH_CRASHHANDLER == CRASHHANDLER_DBGHELP
#pragma comment( lib, "dbghelp" )
#pragma comment( lib, "psapi" )
#include <winnt.h>
#include <dbghelp.h>
#include <psapi.h>

#ifndef XASH_SDL
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;
#endif

int ModuleName( HANDLE process, char *name, void *address, int len )
{
	DWORD_PTR   baseAddress = 0;
	static HMODULE     *moduleArray;
	static unsigned int moduleCount;
	LPBYTE      moduleArrayBytes;
	DWORD       bytesRequired;
	int i;

	if(len < 3)
		return 0;

	if ( !moduleArray && EnumProcessModules( process, NULL, 0, &bytesRequired ) )
	{
		if ( bytesRequired )
		{
			moduleArrayBytes = (LPBYTE)LocalAlloc( LPTR, bytesRequired );

			if ( moduleArrayBytes )
			{
				if( EnumProcessModules( process, (HMODULE *)moduleArrayBytes, bytesRequired, &bytesRequired ) )
				{
					moduleCount = bytesRequired / sizeof( HMODULE );
					moduleArray = (HMODULE *)moduleArrayBytes;
				}
			}
		}
	}

	for( i = 0; i<moduleCount; i++ )
	{
		MODULEINFO info;
		GetModuleInformation( process, moduleArray[i], &info, sizeof(MODULEINFO) );

		if( ( address > info.lpBaseOfDll ) &&
				( (DWORD64)address < (DWORD64)info.lpBaseOfDll + (DWORD64)info.SizeOfImage ) )
			return GetModuleBaseName( process, moduleArray[i], name, len );
	}
	return Q_snprintf(name, len, "???");
}
static void stack_trace( PEXCEPTION_POINTERS pInfo )
{
	char message[1024];
	int len = 0;
	size_t i;
	HANDLE process = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();
	IMAGEHLP_LINE64 line;
	DWORD dline = 0;
	DWORD options;
	CONTEXT context;
	STACKFRAME64 stackframe;
	DWORD image;

	memcpy( &context, pInfo->ContextRecord, sizeof(CONTEXT) );
	options = SymGetOptions();
	options |= SYMOPT_DEBUG;
	options |= SYMOPT_LOAD_LINES;
	SymSetOptions( options );

	SymInitialize( process, NULL, TRUE );



	ZeroMemory( &stackframe, sizeof(STACKFRAME64) );

#ifdef _M_IX86
	image = IMAGE_FILE_MACHINE_I386;
	stackframe.AddrPC.Offset = context.Eip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Ebp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Esp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#elif _M_X64
	image = IMAGE_FILE_MACHINE_AMD64;
	stackframe.AddrPC.Offset = context.Rip;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.Rsp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.Rsp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#elif _M_IA64
	image = IMAGE_FILE_MACHINE_IA64;
	stackframe.AddrPC.Offset = context.StIIP;
	stackframe.AddrPC.Mode = AddrModeFlat;
	stackframe.AddrFrame.Offset = context.IntSp;
	stackframe.AddrFrame.Mode = AddrModeFlat;
	stackframe.AddrBStore.Offset = context.RsBSP;
	stackframe.AddrBStore.Mode = AddrModeFlat;
	stackframe.AddrStack.Offset = context.IntSp;
	stackframe.AddrStack.Mode = AddrModeFlat;
#endif
	len += Q_snprintf( message + len, 1024 - len, "Sys_Crash: address %p, code %p\n", pInfo->ExceptionRecord->ExceptionAddress, (void*)pInfo->ExceptionRecord->ExceptionCode );
	if( SymGetLineFromAddr64( process, (DWORD64)pInfo->ExceptionRecord->ExceptionAddress, &dline, &line ) )
	{
		len += Q_snprintf(message + len, 1024 - len,"Exception: %s:%d:%d\n", (char*)line.FileName, (int)line.LineNumber, (int)dline);
	}
	if( SymGetLineFromAddr64( process, stackframe.AddrPC.Offset, &dline, &line ) )
	{
		len += Q_snprintf(message + len, 1024 - len,"PC: %s:%d:%d\n", (char*)line.FileName, (int)line.LineNumber, (int)dline);
	}
	if( SymGetLineFromAddr64( process, stackframe.AddrFrame.Offset, &dline, &line ) )
	{
		len += Q_snprintf(message + len, 1024 - len,"Frame: %s:%d:%d\n", (char*)line.FileName, (int)line.LineNumber, (int)dline);
	}
	for( i = 0; i < 25; i++ )
	{
		char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		PSYMBOL_INFO symbol = (PSYMBOL_INFO)buffer;
		BOOL result = StackWalk64(
			image, process, thread,
			&stackframe, &context, NULL,
			SymFunctionTableAccess64, SymGetModuleBase64, NULL);

		DWORD64 displacement = 0;
		if( !result )
			break;


		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = MAX_SYM_NAME;

		len += Q_snprintf( message + len, 1024 - len, "% 2d %p", i, (void*)stackframe.AddrPC.Offset );
		if( SymFromAddr( process, stackframe.AddrPC.Offset, &displacement, symbol ) )
		{
			len += Q_snprintf( message + len, 1024 - len, " %s ", symbol->Name );
		}
		if( SymGetLineFromAddr64( process, stackframe.AddrPC.Offset, &dline, &line ) )
		{
			len += Q_snprintf(message + len, 1024 - len,"(%s:%d:%d) ", (char*)line.FileName, (int)line.LineNumber, (int)dline);
		}
		len += Q_snprintf( message + len, 1024 - len, "(");
		len += ModuleName( process, message + len, (void*)stackframe.AddrPC.Offset, 1024 - len );
		len += Q_snprintf( message + len, 1024 - len, ")\n");
	}
#ifdef XASH_SDL
	if( host.type != HOST_DEDICATED ) // let system to restart server automaticly
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR,"Sys_Crash", message, host.hWnd );
#endif
	Sys_PrintLog(message);

	SymCleanup(process);
}
#endif //DBGHELP
LPTOP_LEVEL_EXCEPTION_FILTER       oldFilter;
long _stdcall Sys_Crash( PEXCEPTION_POINTERS pInfo )
{
	// save config
	if( host.status != HOST_CRASHED )
	{
		// check to avoid recursive call
		host.crashed = true;

#if XASH_CRASHHANDLER == CRASHHANDLER_DBGHELP
		stack_trace( pInfo );
#else
		Sys_Warn( "Sys_Crash: call %p at address %p", pInfo->ExceptionRecord->ExceptionAddress, pInfo->ExceptionRecord->ExceptionCode );
#endif

		if( host.type == HOST_NORMAL )
			CL_Crashed(); // tell client about crash
		else host.status = HOST_CRASHED;

		if( host_developer.value <= 0 )
		{
			// no reason to call debugger in release build - just exit
			Sys_Quit();
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		// all other states keep unchanged to let debugger find bug
		Sys_DestroyConsole();
	}

	if( oldFilter )
		return oldFilter( pInfo );
	return EXCEPTION_CONTINUE_EXECUTION;
}

void Sys_SetupCrashHandler( void )
{
	SetErrorMode( SEM_FAILCRITICALERRORS );	// no abort/retry/fail errors
	oldFilter = SetUnhandledExceptionFilter( Sys_Crash );
	host.hInst = GetModuleHandle( NULL );
}

void Sys_RestoreCrashHandler( void )
{
	// restore filter
	if( oldFilter ) SetUnhandledExceptionFilter( oldFilter );
}

#elif XASH_CRASHHANDLER == CRASHHANDLER_UCONTEXT
// Posix signal handler

#include "library.h"

#if XASH_FREEBSD || XASH_NETBSD || XASH_ANDROID || XASH_LINUX
#define HAVE_UCONTEXT_H 1
#endif

#ifdef HAVE_UCONTEXT_H
#include <ucontext.h>
#endif
#include <signal.h>
#include <sys/mman.h>

int printframe( char *buf, int len, int i, void *addr )
{
	Dl_info dlinfo;
	if( len <= 0 )
		return 0; // overflow

	if( dladdr( addr, &dlinfo ))
	{
		if( dlinfo.dli_sname )
			return Q_snprintf( buf, len, "%2d: %p <%s+%lu> (%s)\n", i, addr, dlinfo.dli_sname,
					(unsigned long)addr - (unsigned long)dlinfo.dli_saddr, dlinfo.dli_fname ); // print symbol, module and address
		else
			return Q_snprintf( buf, len, "%2d: %p (%s)\n", i, addr, dlinfo.dli_fname ); // print module and address
	}
	else
		return Q_snprintf( buf, len, "%2d: %p\n", i, addr ); // print only address
}

struct sigaction oldFilter;

#define STACK_BACKTRACE_STR_LEN 17
#define STACK_BACKTRACE_STR "Stack backtrace:\n"
#define STACK_DUMP_STR_LEN 12
#define STACK_DUMP_STR "Stack dump:\n"
#define ALIGN( x, y ) (((int) (x) + ((y)-1)) & ~((y)-1))

static void Sys_Crash( int signal, siginfo_t *si, void *context)
{
	void *pc, **bp, **sp; // this must be set for every OS!
	char message[8192];
	int len, logfd, i = 0;
	size_t pagesize;

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
		pc = (void*)ucontext->uc_mcontext.__gregs[REG_RIP];
		bp = (void**)ucontext->uc_mcontext.__gregs[REG_RBP];
		sp = (void**)ucontext->uc_mcontext.__gregs[REG_RSP];
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
		pc = (void*)ucontext->uc_mcontext.__gregs[REG_EIP];
		bp = (void**)ucontext->uc_mcontext.__gregs[REG_EBP];
		sp = (void**)ucontext->uc_mcontext.__gregs[REG_ESP];
	#elif XASH_OPENBSD
		pc = (void*)ucontext->sc_eip;
		bp = (void**)ucontext->sc_ebp;
		sp = (void**)ucontext->sc_esp;
	#else
		pc = (void*)ucontext->uc_mcontext.gregs[REG_EIP];
		bp = (void**)ucontext->uc_mcontext.gregs[REG_EBP];
		sp = (void**)ucontext->uc_mcontext.gregs[REG_ESP];
	#endif
#elif XASH_ARM64
	pc = (void*)ucontext->uc_mcontext.pc;
	bp = (void*)ucontext->uc_mcontext.regs[29];
	sp = (void*)ucontext->uc_mcontext.sp;
#elif XASH_ARM
	pc = (void*)ucontext->uc_mcontext.arm_pc;
	bp = (void*)ucontext->uc_mcontext.arm_fp;
	sp = (void*)ucontext->uc_mcontext.arm_sp;
#else
	#error "Unknown arch!!!"
#endif

	// safe actions first, stack and memory may be corrupted
	len = Q_snprintf( message, sizeof( message ), "Ver: %s %s (build %i-%s, %s-%s)\n",
		XASH_ENGINE_NAME, XASH_VERSION, Q_buildnum(), Q_buildcommit(), Q_buildos(), Q_buildarch() );

#if !XASH_BSD
	len += Q_snprintf( message + len, sizeof( message ) - len, "Crash: signal %d errno %d with code %d at %p %p\n", signal, si->si_errno, si->si_code, si->si_addr, si->si_ptr );
#else
	len += Q_snprintf( message + len, sizeof( message ) - len, "Crash: signal %d errno %d with code %d at %p\n", signal, si->si_errno, si->si_code, si->si_addr );
#endif

	write( 2, message, len );

	// flush buffers before writing directly to descriptors
	fflush( stdout );
	fflush( stderr );

	// now get log fd and write trace directly to log
	logfd = Sys_LogFileNo();
	write( logfd, message, len );

	// try to print backtrace
	write( 2, STACK_BACKTRACE_STR, STACK_BACKTRACE_STR_LEN );
	write( logfd, STACK_BACKTRACE_STR, STACK_BACKTRACE_STR_LEN );
	strncpy( message + len, STACK_BACKTRACE_STR, sizeof( message ) - len );
	len += STACK_BACKTRACE_STR_LEN;

	pagesize = sysconf( _SC_PAGESIZE );

	do
	{
		int line = printframe( message + len, sizeof( message ) - len, ++i, pc);
		write( 2, message + len, line );
		write( logfd, message + len, line );
		len += line;
		//if( !dladdr(bp,0) ) break; // only when bp is in module
		if( ( mprotect( (char *)ALIGN( bp, pagesize ), pagesize, PROT_READ | PROT_WRITE | PROT_EXEC ) == -1) &&
			( mprotect( (char *)ALIGN( bp, pagesize ), pagesize, PROT_READ | PROT_EXEC ) == -1) &&
			( mprotect( (char *)ALIGN( bp, pagesize ), pagesize, PROT_READ | PROT_WRITE ) == -1) &&
			( mprotect( (char *)ALIGN( bp, pagesize ), pagesize, PROT_READ ) == -1) )
			break;
		if( ( mprotect( (char *)ALIGN( bp[0], pagesize ), pagesize, PROT_READ | PROT_WRITE | PROT_EXEC ) == -1) &&
			( mprotect( (char *)ALIGN( bp[0], pagesize ), pagesize, PROT_READ | PROT_EXEC ) == -1) &&
			( mprotect( (char *)ALIGN( bp[0], pagesize ), pagesize, PROT_READ | PROT_WRITE ) == -1) &&
			( mprotect( (char *)ALIGN( bp[0], pagesize ), pagesize, PROT_READ ) == -1) )
			break;
		pc = bp[1];
		bp = (void**)bp[0];
	}
	while( bp && i < 128 );

	// try to print stack
	write( 2, STACK_DUMP_STR, STACK_DUMP_STR_LEN );
	write( logfd, STACK_DUMP_STR, STACK_DUMP_STR_LEN );
	strncpy( message + len, STACK_DUMP_STR, sizeof( message ) - len );
	len += STACK_DUMP_STR_LEN;

	if( ( mprotect((char *)ALIGN( sp, pagesize ), pagesize, PROT_READ | PROT_WRITE | PROT_EXEC ) != -1) ||
		( mprotect((char *)ALIGN( sp, pagesize ), pagesize, PROT_READ | PROT_EXEC ) != -1) ||
		( mprotect((char *)ALIGN( sp, pagesize ), pagesize, PROT_READ | PROT_WRITE ) != -1) ||
		( mprotect((char *)ALIGN( sp, pagesize ), pagesize, PROT_READ ) != -1) )
	{
		for( i = 0; i < 32; i++ )
		{
			int line = printframe( message + len, sizeof( message ) - len, i, sp[i] );
			write( 2, message + len, line );
			write( logfd, message + len, line );
			len += line;
		}
	}

	// put MessageBox as Sys_Error
	Msg( "%s\n", message );
#ifdef XASH_SDL
	SDL_SetWindowGrab( host.hWnd, SDL_FALSE );
#endif
	MSGBOX( message );

	// log saved, now we can try to save configs and close log correctly, it may crash
	if( host.type == HOST_NORMAL )
		CL_Crashed();
	host.status = HOST_CRASHED;
	host.crashed = true;

	Sys_Quit();
}

void Sys_SetupCrashHandler( void )
{
	struct sigaction act;
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

#elif XASH_CRASHHANDLER == CRASHHANDLER_NULL

void Sys_SetupCrashHandler( void )
{
	// stub
}

void Sys_RestoreCrashHandler( void )
{
	// stub
}
#endif
