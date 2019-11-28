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

// returns days since Apr 1 2015
int Q_buildnum( void )
{
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
	b -= 41728; // Apr 1 2015

	return b;
}

/*
=============
Q_buildnum_compat

Returns a Xash3D build number. This is left for compability with original Xash3D.
IMPORTANT: this value must be changed ONLY after updating to newer Xash3D
IMPORTANT: this value must be acquired through "build" cvar.
=============
*/
int Q_buildnum_compat( void )
{
	// do not touch this! Only author of Xash3D can increase buildnumbers!
	return 4529;
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

#if XASH_MINGW
	osname = "win32-mingw";
#elif XASH_WIN32
	osname = "win32";
#elif XASH_ANDROID
	osname = "android";
#elif XASH_LINUX
	osname = "linux";
#elif XASH_APPLE
	osname = "apple";
#elif XASH_FREEBSD
	osname = "freebsd";
#elif XASH_NETBSD
	osname = "netbsd";
#elif XASH_OPENBSD
	osname = "openbsd";
#elif XASH_EMSCRIPTEN
	osname = "emscripten";
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

#if XASH_AMD64
	archname = "amd64";
#elif XASH_X86
	archname = "i386";
#elif XASH_ARM64
	archname = "arm64";
#elif XASH_ARM
	archname = "armv"
	#if XASH_ARM == 7
		"7"
	#elif XASH_ARM == 6
		"6"
	#elif XASH_ARM == 5
		"5"
	#elif XASH_ARM == 4
		"4"
	#endif

	#if XASH_ARM_HARDFP
		"hf";
	#else
		"l";
	#endif
#elif XASH_MIPS && XASH_BIG_ENDIAN
	archname = "mips";
#elif XASH_MIPS && XASH_LITTLE_ENDIAN
	archname = "mipsel";
#elif XASH_JS
	archname = "javascript";
#elif XASH_E2K
	archname = "e2k";
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
Q_buildcommit will identify this build as "notset"
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

