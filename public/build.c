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

#include <stdio.h>
#include "crtlib.h"
#include "buildenums.h"

static const char mond[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

int Q_buildnum_iso( const char *date )
{
	int y, m, d, b, i;

	if( sscanf( date, "%d-%d-%d", &y, &m, &d ) != 3 || y <= 1900 || m <= 0 || d <= 0 )
		return -1;

	// fixup day and month
	m--;
	d--;

	for( i = 0; i < m; i++ )
		d += mond[i];

	y -= 1900;
	b = d + (int)((y - 1) * 365.25f );

	if((( y % 4 ) == 0 ) && m > 1 )
		b += 1;
	b -= 41728; // Apr 1 2015

	return b;
}

/*
===============
Q_buildnum

returns days since Apr 1 2015
===============
*/
int Q_buildnum( void )
{
	static int b = 0;

	if( b ) return b;

	b = Q_buildnum_iso( g_buildcommit_date );

	return b;
}

/*
=============
Q_buildnum_compat

Returns a Xash3D build number. This is left for compability with original Xash3D.
IMPORTANT: this value must be changed ONLY after updating to newer Xash3D base
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
Q_GetPlatformStringByID

Returns name of operating system by ID. Without any spaces.

TODO: add platform-dependent ABI variants, for example, different libc on Linux
============
*/
const char *Q_PlatformStringByID( const int platform )
{
	switch( platform )
	{
	case PLATFORM_WIN32:
		return "win32";
	case PLATFORM_ANDROID:
		return "android";
	case PLATFORM_LINUX:
		return "linux";
	case PLATFORM_APPLE:
		return "apple";
	case PLATFORM_FREEBSD:
		return "freebsd";
	case PLATFORM_NETBSD:
		return "netbsd";
	case PLATFORM_OPENBSD:
		return "openbsd";
	case PLATFORM_EMSCRIPTEN:
		return "emscripten";
	case PLATFORM_DOS4GW:
		return "DOS4GW";
	case PLATFORM_HAIKU:
		return "haiku";
	case PLATFORM_SERENITY:
		return "serenity";
	case PLATFORM_IRIX:
		return "irix";
	case PLATFORM_NSWITCH:
		return "nswitch";
	case PLATFORM_PSVITA:
		return "psvita";
	case PLATFORM_WASI:
		return "wasi";
	case PLATFORM_SUNOS:
		return "sunos";
	case PLATFORM_HURD:
		return "hurd";
	}

	assert( 0 );
	return "unknown";
}

/*
============
Q_buildos

Shortcut for Q_buildos_
============
*/
const char *Q_buildos( void )
{
	return Q_PlatformStringByID( XASH_PLATFORM );
}


/*
============
Q_ArchitectureStringByID

Returns name of the architecture by it's ID. Without any spaces.
============
*/
const char *Q_ArchitectureStringByID( const int arch, const uint abi, const int endianness, const qboolean is64 )
{
	// I don't want to change this function prototype
	// and don't want to use static buffer either
	// so encode all possible variants... :)
	switch( arch )
	{
	case ARCHITECTURE_AMD64:
		return "amd64";
	case ARCHITECTURE_X86:
		return "i386";
	case ARCHITECTURE_E2K:
		return "e2k";
	case ARCHITECTURE_JS:
		return "javascript";
	case ARCHITECTURE_PPC:
		return endianness == ENDIANNESS_LITTLE ?
			( is64 ? "ppc64el" : "ppcel" ):
			( is64 ? "ppc64" : "ppc" );
	case ARCHITECTURE_MIPS:
		return endianness == ENDIANNESS_LITTLE ?
			( is64 ? "mips64el" : "mipsel" ):
			( is64 ? "mips64" : "mips" );
	case ARCHITECTURE_ARM:
		// no support for big endian ARM here
		if( endianness == ENDIANNESS_LITTLE )
		{
			const uint ver = ( abi >> ARCH_ARM_VER_SHIFT ) & ARCH_ARM_VER_MASK;
			const qboolean hardfp = FBitSet( abi, ARCH_ARM_HARDFP );

			if( is64 )
				return "arm64"; // keep as arm64, it's not aarch64!

			switch( ver )
			{
			case 8:
				return hardfp ? "armv8_32hf" : "armv8_32l";
			case 7:
				return hardfp ? "armv7hf" : "armv7l";
			case 6:
				return "armv6l";
			case 5:
				return "armv5l";
			case 4:
				return "armv4l";
			}
		}
		break;
	case ARCHITECTURE_RISCV:
		switch( abi )
		{
		case ARCH_RISCV_FP_SOFT:
			return is64 ? "riscv64" : "riscv32";
		case ARCH_RISCV_FP_SINGLE:
			return is64 ? "riscv64f" : "riscv32f";
		case ARCH_RISCV_FP_DOUBLE:
			return is64 ? "riscv64d" : "riscv32d";
		}
		break;
	case ARCHITECTURE_WASM:
		return is64 ? "wasm64" : "wasm32";
	}

	assert( 0 );
	return is64 ?
		( endianness == ENDIANNESS_LITTLE ? "unknown64el" : "unknownel" ) :
		( endianness == ENDIANNESS_LITTLE ? "unknown64be" : "unknownbe" );
}

/*
============
Q_buildarch

Returns current name of the architecture. Without any spaces.
============
*/
const char *Q_buildarch( void )
{
	return Q_ArchitectureStringByID(
		XASH_ARCHITECTURE,
		XASH_ARCHITECTURE_ABI,
		XASH_ENDIANNESS,
#if XASH_64BIT
		true
#else
		false
#endif
	);
}

