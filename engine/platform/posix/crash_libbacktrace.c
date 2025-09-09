/*
crash_libbacktrace.c - advanced crashhandler based on libbacktrace
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

#if HAVE_LIBBACKTRACE
#include <signal.h>
#include "common.h"
#include "backtrace.h"
#include "input.h"
#include "crash.h"

static struct backtrace_state *g_bt_state;
static qboolean enable_libbacktrace;

static void Sys_BacktraceError( void *data, const char *msg, int errnum )
{
	if( errnum < 0 )
	{
		Con_Printf( S_ERROR "no symbol info, no libbacktrace\n" );
		return;
	}

	Con_Printf( S_ERROR "libbacktrace error: %s (%d)\n", msg, errnum );

	enable_libbacktrace = false;
}

struct print_data
{
	char *message;
	size_t message_size;
	int len;
	int idx;
	int logfd;
};

static void Sys_AppendPrint( struct print_data *pd, const char *fmt, ... )
{
	va_list va;
	int len;

	va_start( va, fmt );
	len = Q_vsnprintf( pd->message, pd->message_size, fmt, va );
	va_end( va );

	if( len > 0 )
	{
		char ch = '\n';

		write( pd->logfd, pd->message, len );
		write( pd->logfd, &ch, 1 );

		write( STDERR_FILENO, pd->message, len );
		write( STDERR_FILENO, &ch, 1 );

		pd->message += len;
		pd->len += len;
		pd->message_size -= len;
	}
}

static void Sys_BacktracePrintError( void *data, const char *msg, int errnum )
{
	struct print_data *pd = data;
	Sys_AppendPrint( pd, "%2d: error: %s (%d)\n", pd->idx++, msg, errnum );
}

static void Sys_BacktracePrintSyminfo( void *data, uintptr_t pc, const char *symname, uintptr_t symval, uintptr_t symsize )
{
	struct print_data *pd = data;
	Dl_info dlinfo = { 0 };
	const char *module_name;

	if( dladdr((void *)pc, &dlinfo ))
		module_name = dlinfo.dli_fname;
	else module_name = NULL;

	if( symname )
	{
		if( module_name )
			Sys_AppendPrint( pd, "%2d: <%s+%d> (%s)\n", pd->idx++, symname, pc - symval, module_name );
		else
			Sys_AppendPrint( pd, "%2d: <%s+%d>\n", pd->idx++, symname, pc - symval );
	}
	else
	{
		if( module_name )
			Sys_AppendPrint( pd, "%2d: %p (%s)\n", pd->idx++, pc, module_name );
		else
			Sys_AppendPrint( pd, "%2d: %p\n", pd->idx++, pc );
	}
}

static int Sys_BacktracePrintFull( void *data, uintptr_t pc, const char *filename, int lineno, const char *function )
{
	struct print_data *pd = data;
	Dl_info dlinfo = { 0 };
	const char *module_name;

	if( dladdr((void *)pc, &dlinfo ))
		module_name = dlinfo.dli_fname;
	else module_name = NULL;

	if( filename && lineno >= 0 && function )
	{
		filename = COM_FileWithoutPath( filename );

		if( module_name )
			Sys_AppendPrint( pd, "%2d: %s (%s:%d) (%s)\n", pd->idx++, function, filename, lineno, module_name );
		else
			Sys_AppendPrint( pd, "%2d: %s (%s:%d)\n", pd->idx++, function, filename, lineno );
	}
	else
	{
		backtrace_syminfo( g_bt_state, pc, Sys_BacktracePrintSyminfo, Sys_BacktraceError, data );
	}

	return 0;
}

int Sys_CrashDetailsLibbacktrace( int logfd, char *message, int len, size_t max_len )
{
	struct print_data pd =
	{
		.message = message + len,
		.message_size = sizeof( message ) - len,
		.logfd = logfd,
		.len = len,
	};

	backtrace_full( g_bt_state, 1, Sys_BacktracePrintFull, Sys_BacktracePrintError, &pd );

	return pd.len;
}

qboolean Sys_SetupLibbacktrace( const char *argv0 )
{
	enable_libbacktrace = true;
	g_bt_state = backtrace_create_state( argv0, true, Sys_BacktraceError, NULL );
	return g_bt_state != NULL && enable_libbacktrace;
}

#endif // HAVE_EXECINFO
