/*
sys_ps4.c - PlayStation 4 system utils
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
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/SystemService.h>
#include <orbis/Net.h>
#include "platform/ps4/dlfcn_ps4.h"
#include <SDL.h>

// application package mount point
#define PS4_APP_DIR      "/app0"

// default writable data directory
#define PS4_DATA_DIR     "/data/xash"

// optional command line, one or more arguments separated by whitespace
#define PS4_CMDLINE_FILE "xash3d.cmdline"

// everything printed by engine goes here, written without stdio buffering
// so it survives crashes
#define PS4_LOG_FILE     "stdout.txt"

#define PS4_MAX_ARGV 64

static char ps4_basedir[256];
static char *ps4_argv[PS4_MAX_ARGV];
static char ps4_logpath[300];
static qboolean ps4_watchdog_enabled;
static qboolean ps4_log_sync; // -logsync, -gnmdebug: every write goes to disk, survives a console freeze
static volatile uint64_t ps4_last_log_time;

static void PS4_InstallCrashHandler( void );
static SDL_AssertState SDLCALL PS4_SDLAssertion( const SDL_AssertData *data, void *userdata );
static void SDLCALL PS4_SDLLog( void *userdata, int category, SDL_LogPriority priority, const char *message );

// platform/ps4/compat/ps4_cwd.c
const char *PS4_GetStatLayout( void );

// platform/ps4/compat/ps4_stdio.c
extern void ( *ps4_stdout_hook )( const char *text, size_t len );

/*
==================
PS4_LogWrite

Appends raw text to log file, also receives stdout and stderr, see ps4_stdio.c
==================
*/
static void PS4_LogWrite( const char *text, size_t len )
{
	int fd;

	if( !ps4_logpath[0] )
		return;

	fd = open( ps4_logpath, O_WRONLY | O_CREAT | O_APPEND, 0666 );
	if( fd < 0 )
		return;

	if( write( fd, text, len ) < 0 )
	{
		// nothing we can do
	}

	if( ps4_log_sync )
		sceKernelFsync( fd );

	close( fd );
}

/*
==================
PS4_Log

Appends message to log file, opening it each time, so nothing is lost on crash
==================
*/
void PS4_Log( const char *fmt, ... )
{
	char buf[2048];
	va_list va;
	int len;

	if( !ps4_logpath[0] )
		return;

	ps4_last_log_time = sceKernelGetProcessTime( );

	va_start( va, fmt );
	len = vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	if( len <= 0 )
		return;

	if( len >= (int)sizeof( buf ))
		len = sizeof( buf ) - 1;

	PS4_LogWrite( buf, len );
}

/*
=============================================================================

	DYNAMIC LIBRARY LOADING

PS4 modules (.prx) are loaded by the kernel, no dlopen in libc
Handles are kernel module ids, offset by 1 so 0 is never a valid handle

=============================================================================
*/
static char ps4_dlerror_buf[512];
static qboolean ps4_dlerror_set;

static void PS4_SetDlError( const char *fmt, ... ) FORMAT_CHECK( 1 );
static void PS4_SetDlError( const char *fmt, ... )
{
	va_list va;

	va_start( va, fmt );
	vsnprintf( ps4_dlerror_buf, sizeof( ps4_dlerror_buf ), fmt, va );
	va_end( va );

	ps4_dlerror_set = true;
}

static qboolean PS4_FileExists( const char *path )
{
	struct stat st;

	return stat( path, &st ) == 0 && S_ISREG( st.st_mode );
}

static qboolean PS4_DirExists( const char *path )
{
	struct stat st;

	return stat( path, &st ) == 0 && S_ISDIR( st.st_mode );
}

// modules shipped in the package root: engine modules and, when packaged
// together with the engine, game libraries (only their file name counts)
qboolean PS4_PackageLibraryExists( const char *name )
{
	char path[512];

	if( !name || !name[0] )
		return false;

	Q_snprintf( path, sizeof( path ), "%s/%s", PS4_APP_DIR, COM_FileWithoutPath( name ));
	return PS4_FileExists( path );
}

void *PS4_dlopen( const char *name, int flags )
{
	char path[512];
	int32_t handle;

	(void)flags;

	if( !name )
	{
		PS4_SetDlError( "dlopen(NULL) is not supported" );
		return NULL;
	}

	if( name[0] == '/' )
	{
		Q_strncpy( path, name, sizeof( path ));
	}
	else
	{
		// engine modules (filesystem, menu, renderers) live in the package root, game
		// libraries too when they are packaged; libraries in the game directory are
		// found by the engine before it gets here, so they override packaged ones
		const char *base = COM_FileWithoutPath( name );

		Q_snprintf( path, sizeof( path ), "%s/%s", PS4_APP_DIR, base );

		if( !PS4_FileExists( path ))
			Q_snprintf( path, sizeof( path ), "%s/%s", ps4_basedir, name );
	}

	if( !PS4_FileExists( path ))
	{
		PS4_SetDlError( "%s: file not found", path );
		PS4_Log( "dlopen: %s\n", ps4_dlerror_buf );
		return NULL;
	}

	handle = (int32_t)sceKernelLoadStartModule( path, 0, NULL, 0, NULL, NULL );

	if( handle < 0 )
	{
		PS4_SetDlError( "%s: sceKernelLoadStartModule failed: 0x%08x", path, (uint32_t)handle );
		PS4_Log( "dlopen: %s\n", ps4_dlerror_buf );
		return NULL;
	}

	PS4_Log( "loaded module %s: 0x%x\n", path, handle );

	// run global constructors, our startup code exports this, see compat/ps4_crtlib.c
	{
		void ( *init )( void ) = NULL;

		int ( *shared )( void ) = NULL;

		if( sceKernelDlsym( handle, "__ps4_module_init", (void **)&init ) >= 0 && init )
			init( );
		else
			PS4_Log( "warning: %s has no __ps4_module_init, global constructors may not run\n", path );

		// see compat/ps4_malloc.c
		if( sceKernelDlsym( handle, "__ps4_heap_shared", (void **)&shared ) >= 0 && shared )
			PS4_Log( "%s: %s heap\n", path, shared( ) ? "shared" : "own" );
	}

	return (void *)(intptr_t)( handle + 1 );
}

void *PS4_dlsym( void *handle, const char *symbol )
{
	void *addr = NULL;
	int32_t ret;

	if( !handle )
	{
		PS4_SetDlError( "dlsym(%s): invalid handle", symbol );
		return NULL;
	}

	ret = sceKernelDlsym( (int32_t)( (intptr_t)handle - 1 ), symbol, &addr );

	if( ret < 0 || !addr )
	{
		PS4_SetDlError( "dlsym(%s): symbol not found: 0x%08x", symbol, (uint32_t)ret );
		return NULL;
	}

	return addr;
}

int PS4_dlclose( void *handle )
{
	int32_t res = 0;

	if( !handle )
		return -1;

	return sceKernelStopUnloadModule( (OrbisKernelModule)( (intptr_t)handle - 1 ), 0, NULL, 0, NULL, &res ) < 0 ? -1 : 0;
}

char *PS4_dlerror( void )
{
	if( !ps4_dlerror_set )
		return NULL;

	ps4_dlerror_set = false;
	return ps4_dlerror_buf;
}

int PS4_dladdr( const void *addr, Dl_info *info )
{
	// no symbol information available, savegames use offsets instead
	(void)addr;
	(void)info;
	return 0;
}

/*
=============================================================================

	PATHS AND COMMAND LINE

=============================================================================
*/
static void PS4_MakeDir( const char *path )
{
	if( mkdir( path, 0777 ) < 0 && errno != EEXIST )
		PS4_Log( "mkdir %s failed: %s\n", path, strerror( errno ));
}

/*
==================
PS4_FindBaseDir

Prefer /data/xash, fall back to USB storage
==================
*/
static void PS4_FindBaseDir( void )
{
	const char *env = getenv( "XASH3D_BASEDIR" );
	char path[256];

	if( env && env[0] )
	{
		Q_strncpy( ps4_basedir, env, sizeof( ps4_basedir ));
		return;
	}

	// game data already in internal storage
	Q_snprintf( path, sizeof( path ), "%s/%s", PS4_DATA_DIR, XASH_GAMEDIR );
	if( PS4_DirExists( path ))
	{
		Q_strncpy( ps4_basedir, PS4_DATA_DIR, sizeof( ps4_basedir ));
		return;
	}

	for( int i = 0; i < 8; i++ )
	{
		Q_snprintf( path, sizeof( path ), "/mnt/usb%d/xash/%s", i, XASH_GAMEDIR );
		if( PS4_DirExists( path ))
		{
			Q_snprintf( ps4_basedir, sizeof( ps4_basedir ), "/mnt/usb%d/xash", i );
			return;
		}
	}

	// nothing found, use internal storage anyway so user sees where to put data
	Q_strncpy( ps4_basedir, PS4_DATA_DIR, sizeof( ps4_basedir ));
}

qboolean PS4_GetBasePath( char *buf, const size_t buflen )
{
	if( !ps4_basedir[0] )
		PS4_FindBaseDir();

	Q_strncpy( buf, ps4_basedir, buflen );
	return true;
}

/*
==================
PS4_CopyFileIfChanged

Copies a file shipped in the package to writable storage,
if destination is missing or has different size
==================
*/
static void PS4_CopyFileIfChanged( const char *src, const char *dst )
{
	static char buf[65536];
	struct stat sst, dst_st;
	int in, out;
	ssize_t len;

	if( stat( src, &sst ) < 0 )
		return;

	if( stat( dst, &dst_st ) == 0 && dst_st.st_size == sst.st_size )
		return;

	in = open( src, O_RDONLY );
	if( in < 0 )
		return;

	out = open( dst, O_WRONLY | O_CREAT | O_TRUNC, 0666 );
	if( out < 0 )
	{
		PS4_Log( "can't write %s: %s\n", dst, strerror( errno ));
		close( in );
		return;
	}

	while(( len = read( in, buf, sizeof( buf ))) > 0 )
	{
		if( write( out, buf, len ) != len )
		{
			PS4_Log( "write to %s failed: %s\n", dst, strerror( errno ));
			break;
		}
	}

	close( out );
	close( in );

	PS4_Log( "installed %s\n", dst );
}

static void PS4_SetupDataDir( void )
{
	char path[512];

	PS4_GetBasePath( path, sizeof( path ));
	PS4_MakeDir( path );

	// start new log on each launch
	Q_snprintf( ps4_logpath, sizeof( ps4_logpath ), "%s/%s", ps4_basedir, PS4_LOG_FILE );
	unlink( ps4_logpath );

	PS4_Log( "Xash3D FWGS PS4: base directory %s, stat layout %s\n", ps4_basedir, PS4_GetStatLayout( ));

	// libraries (SDL port in particular) print to stdout directly,
	// applications can't redirect it, so it's captured by ps4_stdio.c
	ps4_stdout_hook = PS4_LogWrite;

	// emulated by platform/ps4/compat/ps4_cwd.c, so relative paths work in engine image too
	if( chdir( ps4_basedir ) < 0 )
		PS4_Log( "chdir %s failed: %s\n", ps4_basedir, strerror( errno ));

	// engine resources are shipped in the package, but should be visible to the game
	Q_snprintf( path, sizeof( path ), "%s/%s", ps4_basedir, XASH_GAMEDIR );
	PS4_MakeDir( path );
	Q_strncat( path, "/extras.pk3", sizeof( path ));
	PS4_CopyFileIfChanged( PS4_APP_DIR "/" XASH_GAMEDIR "/extras.pk3", path );
}

/*
==================
PS4_GetArgv

There is no command line on PS4, read additional arguments from file
==================
*/
int PS4_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	static char cmdline[2048];
	char path[512];
	int argc = 0;
	FILE *f;

	PS4_SetupDataDir();

	PS4_InstallCrashHandler( );

	// before SDL_Init, so nothing can block on stdin or get lost
	SDL_SetAssertionHandler( PS4_SDLAssertion, NULL );
	SDL_LogSetOutputFunction( PS4_SDLLog, NULL );

	ps4_argv[argc++] = ( in_argc > 0 && in_argv[0] ) ? in_argv[0] : (char *)PS4_APP_DIR "/eboot.bin";

	Q_snprintf( path, sizeof( path ), "%s/%s", ps4_basedir, PS4_CMDLINE_FILE );
	f = fopen( path, "rb" );

	if( f )
	{
		size_t len = fread( cmdline, 1, sizeof( cmdline ) - 1, f );
		char *p = cmdline;

		fclose( f );
		cmdline[len] = 0;

		while( *p && argc < PS4_MAX_ARGV - 1 )
		{
			while( *p && isspace( *p ))
				*p++ = 0;

			if( !*p )
				break;

			ps4_argv[argc++] = p;

			while( *p && !isspace( *p ))
				p++;
		}
	}
	else
	{
		// log is always written, so it's possible to see what went wrong; console is
		// available, but without -dev its messages don't cover the top of the screen,
		// "-log -dev 2" in xash3d.cmdline gives verbose log and messages on screen
		ps4_argv[argc++] = (char *)"-log";
		ps4_argv[argc++] = (char *)"-console";
	}

	ps4_argv[argc] = NULL;

	for( int i = 0; i < argc; i++ )
	{
		PS4_Log( "argv[%d] = %s\n", i, ps4_argv[i] );

		if( !Q_strcmp( ps4_argv[i], "-ps4watchdog" ))
			ps4_watchdog_enabled = true;

		if( !Q_strcmp( ps4_argv[i], "-logsync" ) || !Q_strcmp( ps4_argv[i], "-gnmdebug" ))
			ps4_log_sync = true;
	}

	*out_argv = ps4_argv;
	return argc;
}

/*
=============================================================================

	CRASH HANDLER

Logs faulting address as module + offset, so it can be resolved with
llvm-addr2line on unstripped ELF from build directory

=============================================================================
*/

// PS4 kernel uses FreeBSD numbering and layouts, OpenOrbis headers partially don't
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

static void PS4_InstallCrashHandler( void )
{
	const int signals[] = { PS4_SIGILL, PS4_SIGTRAP, PS4_SIGABRT, PS4_SIGFPE, PS4_SIGBUS, PS4_SIGSEGV, PS4_SIGSYS };
	struct sigaction sa;

	memset( &sa, 0, sizeof( sa ));
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
PS4_RunOnBigStack

Default main thread stack is too small for the engine
==================
*/
#define PS4_MAIN_STACK_SIZE ( 16 * 1024 * 1024 )

typedef struct
{
	int ( *func )( void *arg );
	void *arg;
	int ret;
} ps4_thread_args_t;

static void *PS4_ThreadEntry( void *arg )
{
	ps4_thread_args_t *args = arg;
	pthread_t watchdog;

	ps4_engine_thread = pthread_self( );
	ps4_engine_thread_valid = true;

	// diagnostic for hangs during startup, enabled with -ps4watchdog in xash3d.cmdline
	if( ps4_watchdog_enabled && pthread_create( &watchdog, NULL, PS4_WatchdogThread, NULL ) == 0 )
		pthread_detach( watchdog );

	args->ret = args->func( args->arg );
	return NULL;
}

int PS4_RunOnBigStack( int ( *func )( void *arg ), void *arg )
{
	ps4_thread_args_t args = { func, arg, 0 };
	pthread_attr_t attr;
	pthread_t thread;
	int ret;

	pthread_attr_init( &attr );
	pthread_attr_setstacksize( &attr, PS4_MAIN_STACK_SIZE );

	ret = pthread_create( &thread, &attr, PS4_ThreadEntry, &args );
	pthread_attr_destroy( &attr );

	if( ret != 0 )
	{
		PS4_Log( "pthread_create failed: %d, running on main thread\n", ret );
		return func( arg );
	}

	PS4_Log( "started engine thread\n" );
	pthread_join( thread, NULL );
	PS4_Log( "engine thread finished: %d\n", args.ret );

	// returning from main runs libc and module teardown, which crashes with CE-34878
	// ask the system to close the application instead
	PS4_Log( "exiting\n" );
	sceSystemServiceLoadExec( "exit", NULL );

	// shouldn't get here
	_exit( args.ret );
	return args.ret;
}

/*
==================
PS4_SDLAssertion

Default SDL handler asks user on stdin and blocks forever, log and continue instead
==================
*/
static SDL_AssertState SDLCALL PS4_SDLAssertion( const SDL_AssertData *data, void *userdata )
{
	PS4_Log( "SDL assertion failed: '%s' at %s:%d (%s), %u times\n",
		data->condition, data->filename, data->linenum, data->function, data->trigger_count );
	return SDL_ASSERTION_ALWAYS_IGNORE;
}

static void SDLCALL PS4_SDLLog( void *userdata, int category, SDL_LogPriority priority, const char *message )
{
	PS4_Log( "SDL: %s\n", message );
}

void PS4_Init( void )
{
	OrbisNetDnsInfo dns;
	int ret;

	PS4_Log( "PS4_Init\n" );

	// libSceNet must be initialized for DNS server lookup used by libc resolver
	if(( ret = sceNetInit( )) < 0 )
		PS4_Log( "sceNetInit failed: 0x%08x\n", ret );

	memset( &dns, 0, sizeof( dns ));
	if(( ret = sceNetGetDnsInfo( &dns, 0 )) < 0 )
		PS4_Log( "sceNetGetDnsInfo failed: 0x%08x\n", ret );
	else
	{
		const uint8_t *a = (const uint8_t *)&dns.primary_dns, *b = (const uint8_t *)&dns.secondary_dns;
		PS4_Log( "DNS servers: %d.%d.%d.%d, %d.%d.%d.%d\n", a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3] );
	}

	// SDL's PS4 backends expect user service to be initialized
	sceUserServiceInitialize( NULL );
}

/*
================
PS4_FramePresented

system keeps showing startup splash (sce_sys/pic0.png) over the game
until application hides it. Frames presented during engine initialization
are black, so splash is hidden only when the main loop has drawn something
================
*/
static int ps4_splash_frames = -1; // -1: still initializing, 0: hidden

void PS4_AllowHideSplash( void )
{
	if( ps4_splash_frames < 0 )
		ps4_splash_frames = 3;
}

void PS4_FramePresented( void )
{
	int ret;

	if( ps4_splash_frames <= 0 || --ps4_splash_frames > 0 )
		return;

	ret = sceSystemServiceHideSplashScreen( );
	PS4_Log( "sceSystemServiceHideSplashScreen: 0x%x\n", ret );
}

void PS4_Shutdown( void )
{
	PS4_Log( "PS4_Shutdown\n" );
}
