/*
version.c - clang-format version probe
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
#include "crtlib.h"
#include "xcf.h"

/*
============
ParseVersion

Parse the major version number out of a `clang-format --version`
============
*/
qboolean ParseVersion( const char *s, int *major )
{
	const char *p;

	if( COM_StringEmptyOrNULL( s ) || !major )
		return false;

	// anchor on "version "
	p = Q_strstr( s, "version " );
	if( !p )
		return false;
	p += sizeof( "version " ) - 1;

	while( *p == ' ' || *p == '\t' )
		p++;
	if( *p < '0' || *p > '9' )
		return false;

	*major = Q_atoi( p );
	return true;
}

/*
============
ClangFormatVersion

============
*/
qboolean ClangFormatVersion( const char *exe, int *major )
{
	char *argv[] = { (char *)exe, (char *)"--version", NULL };
	xcf_buf_t out = { 0 };
	qboolean ok = false;

	if( !exe || !major )
		return false;

	if( RunCapture( argv, &out, true ) == 0 && BufPutChar( &out, 0 ))
		ok = ParseVersion( out.data, major );

	BufFree( &out );
	return ok;
}
