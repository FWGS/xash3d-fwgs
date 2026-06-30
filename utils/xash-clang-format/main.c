/*
main.c - xash-clang-format wrapper entry point
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "crtlib.h"
#include "xcf.h"

#ifndef XCF_CLANG_FORMAT_DEFAULT
#define XCF_CLANG_FORMAT_DEFAULT "clang-format"
#endif

// TODO: move to custom config keys
#define XCF_MIN_CLANG_FORMAT_MAJOR 17

static const char *g_clang_format = NULL;

/*
============
ResolveClangFormat
============
*/
static const char *ResolveClangFormat( void )
{
	const char *e = getenv( "XASH_CLANG_FORMAT_BIN" );

	if( !COM_StringEmptyOrNULL( e ))
		return e;

	return XCF_CLANG_FORMAT_DEFAULT;
}

/*
============
IsFileArg

Attempt to distinguish files from other args.
But to keep parsing args simple, it will parse value as --key value as
filename. :(
============
*/
static qboolean IsFileArg( const char *a )
{
	if( COM_StringEmptyOrNULL( a ))
		return false;
	if( a[0] == '-' )
		return false;
	return true;
}

/*
============
ReadTheFile

============
*/
static qboolean ReadTheFile( const char *path, xcf_buf_t *out )
{
	FILE *f = fopen( path, "rb" );

	if( !f )
		return false;

	char buf[8192];
	size_t n;

	while(( n = fread( buf, 1, sizeof( buf ), f )) > 0 )
	{
		if( !BufPutMem( out, buf, n ))
		{
			fclose( f );
			return false;
		}
	}

	fclose( f );
	return true;
}

/*
============
WriteTheFile

============
*/
static qboolean WriteTheFile( const char *path, const char *data, size_t len )
{
	FILE *f = fopen( path, "wb" );

	if( !f )
		return false;
	if( fwrite( data, 1, len, f ) != len )
	{
		fclose( f );
		return false;
	}
	if( fclose( f ) != 0 )
		return false;
	return true;
}

/*
============
FindConfig
============
*/
static char *FindConfig( const char *startdir )
{
	char dir[MAX_SYSPATH];

	if( startdir )
	{
		Q_strncpy( dir, startdir, sizeof( dir ));
	}
	else if( !getcwd( dir, sizeof( dir )))
	{
		return NULL;
	}

	for( ;; )
	{
		char path[MAX_SYSPATH];
		struct stat st;
		char *slash;

		Q_snprintf( path, sizeof( path ), "%s/.clang-format", dir );

		if( stat( path, &st ) == 0 && S_ISREG( st.st_mode ))
		{
			size_t len = Q_strlen( path ) + 1;
			char *r = malloc( len );

			if( r )
				Q_strncpy( r, path, len );
			return r;
		}

		slash = Q_strrchr( dir, '/' );
		if( !slash || slash == dir )
			break;
		*slash = 0;
	}

	return NULL;
}

/*
============
LoadConfig
============
*/
static qboolean LoadConfig( const char *path, xcf_config_t *cfg )
{
	xcf_buf_t buf = { 0 };
	qboolean ok = false;

	if( path && ReadTheFile( path, &buf ) && BufPutChar( &buf, 0 ))
		ok = ParseConfigText( buf.data, cfg );
	else
		ParseConfigText( "", cfg );

	BufFree( &buf );
	return ok;
}

/*
============
Emit

Transform pass on buffer and output to file
============
*/
static qboolean Emit( const xcf_config_t *cfg, const xcf_buf_t *raw, FILE *dst, const char *write_path )
{
	xcf_buf_t out = { 0 };
	const char *data;
	size_t len;
	if( cfg->collapse_parens && Transform( raw->data, raw->len, &out ))
	{
		data = out.data;
		len = out.len;
	}
	else
	{
		data = raw->data;
		len = raw->len;
	}

	qboolean ok = true;
	if( write_path )
	{
		if( !WriteTheFile( write_path, data, len ))
		{
			fprintf( stderr, "xash-clang-format: cannot write %s: %s\n", write_path, strerror( errno ));
			ok = false;
		}
	}
	else if( len )
	{
		if( fwrite( data, 1, len, dst ) != len )
			ok = false;
	}

	BufFree( &out );
	return ok;
}

/*
============
PipeMode

Process stdout (when clang-format called without -i)
============
*/
static int PipeMode( int argc, char **argv, const xcf_config_t *cfg )
{
	char **fwd = malloc( sizeof( char * ) * ( argc + 1 ));
	if( !fwd )
		return 1;

	fwd[0] = (char *)g_clang_format;
	for( int i = 1; i < argc; i++ )
		fwd[i] = argv[i];
	fwd[argc] = NULL;

	xcf_buf_t raw = { 0 };
	int rc = RunCapture( fwd, &raw, false );
	free( fwd );

	if( rc != 0 )
	{
		// pass through whatever clang-format produced
		if( raw.len )
			fwrite( raw.data, 1, raw.len, stdout );
		BufFree( &raw );
		return rc < 0 ? 1 : rc;
	}

	rc = Emit( cfg, &raw, stdout, NULL ) ? 0 : 1;
	BufFree( &raw );
	return rc;
}

/*
============
PostProcessFile

Read `path`, run the paren-collapse pass, write result back.
============
*/
static qboolean PostProcessFile( const char *path )
{
	xcf_buf_t in = { 0 };
	xcf_buf_t out = { 0 };
	qboolean ok = false;

	if( ReadTheFile( path, &in ) && Transform( in.data, in.len, &out ))
		ok = WriteTheFile( path, out.data, out.len );

	BufFree( &in );
	BufFree( &out );
	return ok;
}

/*
============
InPlaceMode

Post-process mode, when clang-format is called with -i
============
*/
static int InPlaceMode( int argc, char **argv, const xcf_config_t *cfg )
{
	char **fwd = malloc( sizeof( char * ) * ( argc + 1 ));

	if( !fwd )
		return 1;

	fwd[0] = (char *)g_clang_format;
	for( int i = 1; i < argc; i++ )
		fwd[i] = argv[i];
	fwd[argc] = NULL;

	xcf_buf_t sink = { 0 };
	int rc = RunCapture( fwd, &sink, false );

	BufFree( &sink );
	free( fwd );

	if( rc != 0 )
		return rc < 0 ? 1 : rc;

	if( !cfg->collapse_parens )
		return 0;

	for( int i = 1; i < argc; i++ )
	{
		if( !IsFileArg( argv[i] ))
			continue;
		if( !PostProcessFile( argv[i] ))
		{
			fprintf( stderr, "xash-clang-format: post-process failed for %s\n", argv[i] );
			rc = 1;
		}
	}
	return rc;
}

static void Usage( FILE *dst )
{
	fprintf( dst,
		"xash-clang-format: clang-format wrapper with xash3d-fwgs extensions\n"
		"\n"
		"Usage: xash-clang-format [clang-format options] [<file> ...]\n"
		"       xash-clang-format -i <file> ...\n"
		"\n"
		"Most clang-format flags are forwarded as-is but there are limitations:\n"
		"* --key value must be passed as --key=value\n"
		"\n"
		"Use XASH_CLANG_FORMAT_BIN envvar to override the clang-format binary;\n"
		" setting it also skips the version probe.\n"
		"\n"
		"Wrapper's config is shared with clang-format config, but extended keys\n"
		" start with comment hash.\n"
		"New keys:\n"
		"    XashCollapseParens, accepts 'true' or 'false'\n" );
}

int main( int argc, char **argv )
{
	g_clang_format = ResolveClangFormat();

	qboolean has_inplace = false;
	qboolean has_version = false;
	qboolean has_help = false;

	for( int i = 1; i < argc; i++ )
	{
		if( Q_strcmp( argv[i], "-i" ) == 0 )
			has_inplace = true;
		else if( Q_strcmp( argv[i], "--version" ) == 0 || Q_strcmp( argv[i], "-version" ) == 0 )
			has_version = true;
		else if( Q_strcmp( argv[i], "--help" ) == 0 || Q_strcmp( argv[i], "-h" ) == 0 || Q_strcmp( argv[i], "-help" ) == 0 )
			has_help = true;
	}

	if( has_help )
	{
		Usage( stdout );
		return 0;
	}

	if( has_version )
	{
		// transparent passthrough so tools that scrape `clang-format --version`
		// (e.g. git clang-format auto-detection) keep working
		execlp( g_clang_format, g_clang_format, "--version", (char *)NULL );
		fprintf( stderr, "xash-clang-format: cannot exec %s: %s\n", g_clang_format, strerror( errno ));
		return 127;
	}

	// if caller sets XASH_CLANG_FORMAT_BIN — they own version compatibility
	// so skip the probe (lets ./waf format do the check once for parallel invocation)
	if( COM_StringEmptyOrNULL( getenv( "XASH_CLANG_FORMAT_BIN" )))
	{
		int major = 0;

		if( !ClangFormatVersion( g_clang_format, &major ))
		{
			fprintf( stderr, "xash-clang-format: cannot probe clang-format version (binary: %s)\n", g_clang_format );
			return 2;
		}
		if( major < XCF_MIN_CLANG_FORMAT_MAJOR )
		{
			fprintf( stderr, "xash-clang-format: clang-format %d found, need >= %d\n",
				major, XCF_MIN_CLANG_FORMAT_MAJOR );
			return 2;
		}
	}

	xcf_config_t cfg;
	char *cfgpath = FindConfig( NULL );
	LoadConfig( cfgpath, &cfg );
	free( cfgpath );

	return has_inplace ? InPlaceMode( argc, argv, &cfg ) : PipeMode( argc, argv, &cfg );
}
