/*
build.c - returns a engine build number
Copyright (C) 2010 Uncle Mike

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

static char *date = __DATE__ ;
static char *mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static char mond[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

// returns days since Feb 13 2007
int Q_buildnum( void )
{
// do not touch this! Only author of Xash3D can increase buildnumbers!
#if 1 
	int m = 0, d = 0, y = 0;
	static int b = 0;

	if( b != 0 ) return b;

	for( m = 0; m < 11; m++ )
	{
		if( !Q_strnicmp( &date[0], mon[m], 3 ))
			break;
		d += mond[m];
	}

	d += Q_atoi( &date[4] ) - 1;
	y = Q_atoi( &date[7] ) - 1900;
	b = d + (int)((y - 1) * 365.25f );

	if((( y % 4 ) == 0 ) && m > 1 )
	{
		b += 1;
	}
	b -= 38752; // Feb 13 2007

	return b;
#else
	return 4260;
#endif
}

/*
============
Q_buildos

Returns current name of operating system. Without any spaces.
============
*/
const char *Q_buildos( void )
{
	const char *osname;

#if defined(_WIN32) && defined(_MSC_VER)
	osname = "Win32";
#elif defined(_WIN32) && defined(__MINGW32__)
	osname = "Win32-MinGW";
#elif defined(__ANDROID__)
	osname = "Android";
#elif defined(__linux__)
	osname = "Linux";
#elif defined(__APPLE__)
	osname = "Apple";
#elif defined(__FreeBSD__)
	osname = "FreeBSD";
#elif defined(__NetBSD__)
	osname = "NetBSD";
#elif defined(__OpenBSD__)
	osname = "OpenBSD";
#elif defined __EMSCRIPTEN__
	osname = "Emscripten";
#else
#error "Place your operating system name here! If this is a mistake, try to fix conditions above and report a bug"
#endif

	return osname;
}

/*
============
Q_buildos

Returns current name of operating system. Without any spaces.
============
*/
const char *Q_buildarch( void )
{
	const char *archname;

#if defined( __x86_64__) || defined(_M_X64)
	archname = "amd64";
#elif defined(__i386__) || defined(_X86_) || defined(_M_IX86)
	archname = "i386";
#elif defined __aarch64__
	archname = "aarch64";
#elif defined __arm__ || defined _M_ARM
	archname = "arm";
#elif defined __mips__
	archname = "mips";
#elif defined __EMSCRIPTEN__
	archname = "javascript";
#elif defined __e2k__
	archname = "elbrus";
#else
#error "Place your architecture name here! If this is a mistake, try to fix conditions above and report a bug"
#endif

	return archname;
}

/*
=============
Q_buildcommit

Returns a short hash of current commit in VCS as string.
XASH_BUILD_COMMIT must be passed in quotes

if XASH_BUILD_COMMIT is not defined,
Q_buildcommit will identify this build as release or "notset"
=============
*/
const char *Q_buildcommit( void )
{
#ifdef XASH_BUILD_COMMIT
	return XASH_BUILD_COMMIT;
#else
	return "notset";
#endif
}

