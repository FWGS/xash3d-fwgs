/*
ps4_stdio.c - thread safe standard output for PS4
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

/*
Static musl libc from OpenOrbis toolchain doesn't know about threads created
through system pthread library, so it never locks FILE objects. When engine
and SDL threads print at the same time, stdout buffer pointers get corrupted
and the next printf crashes. Redirecting stdout with freopen() or dup2()
isn't allowed for applications either.

This file is linked into every PS4 image and the linker redirects standard
output functions here with --wrap=<function> (see scripts/waifulib/ps4.py).
Text written to stdout and stderr is formatted on the stack and passed to
ps4_stdout_hook, or written to the descriptor directly, never touching
shared FILE state. Other streams go to the original functions.
*/

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define PS4_STDIO_BUF 4096

// set by the engine to duplicate the output into its log file
void ( *ps4_stdout_hook )( const char *text, size_t len );

int __real_vfprintf( FILE *stream, const char *fmt, va_list args );
int __real_fputs( const char *s, FILE *stream );
int __real_fputc( int c, FILE *stream );
int __real_putc( int c, FILE *stream );
size_t __real_fwrite( const void *ptr, size_t size, size_t nmemb, FILE *stream );

static int PS4_IsStd( FILE *stream )
{
	return stream == stdout || stream == stderr;
}

static void PS4_StdWrite( FILE *stream, const char *text, size_t len )
{
	if( !len )
		return;

	if( ps4_stdout_hook )
		ps4_stdout_hook( text, len );
	else
		write( stream == stderr ? STDERR_FILENO : STDOUT_FILENO, text, len );
}

static int PS4_StdPrintf( FILE *stream, const char *fmt, va_list args )
{
	char buf[PS4_STDIO_BUF];
	int len = vsnprintf( buf, sizeof( buf ), fmt, args );

	if( len < 0 )
		return len;

	PS4_StdWrite( stream, buf, (size_t)len < sizeof( buf ) ? (size_t)len : sizeof( buf ) - 1 );
	return len;
}

int __wrap_vfprintf( FILE *stream, const char *fmt, va_list args )
{
	if( PS4_IsStd( stream ))
		return PS4_StdPrintf( stream, fmt, args );

	return __real_vfprintf( stream, fmt, args );
}

int __wrap_fprintf( FILE *stream, const char *fmt, ... )
{
	va_list args;
	int ret;

	va_start( args, fmt );
	ret = __wrap_vfprintf( stream, fmt, args );
	va_end( args );

	return ret;
}

int __wrap_vprintf( const char *fmt, va_list args )
{
	return PS4_StdPrintf( stdout, fmt, args );
}

int __wrap_printf( const char *fmt, ... )
{
	va_list args;
	int ret;

	va_start( args, fmt );
	ret = PS4_StdPrintf( stdout, fmt, args );
	va_end( args );

	return ret;
}

int __wrap_fputs( const char *s, FILE *stream )
{
	if( PS4_IsStd( stream ))
	{
		PS4_StdWrite( stream, s, strlen( s ));
		return 0;
	}

	return __real_fputs( s, stream );
}

int __wrap_puts( const char *s )
{
	PS4_StdWrite( stdout, s, strlen( s ));
	PS4_StdWrite( stdout, "\n", 1 );
	return 0;
}

int __wrap_fputc( int c, FILE *stream )
{
	if( PS4_IsStd( stream ))
	{
		char ch = (char)c;
		PS4_StdWrite( stream, &ch, 1 );
		return (unsigned char)c;
	}

	return __real_fputc( c, stream );
}

int __wrap_putc( int c, FILE *stream )
{
	if( PS4_IsStd( stream ))
		return __wrap_fputc( c, stream );

	return __real_putc( c, stream );
}

int __wrap_putchar( int c )
{
	return __wrap_fputc( c, stdout );
}

size_t __wrap_fwrite( const void *ptr, size_t size, size_t nmemb, FILE *stream )
{
	if( PS4_IsStd( stream ))
	{
		PS4_StdWrite( stream, ptr, size * nmemb );
		return nmemb;
	}

	return __real_fwrite( ptr, size, nmemb, stream );
}
