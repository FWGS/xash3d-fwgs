/*
port.h -- Portability Layer for Windows types
Copyright (C) 2015 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#pragma once
#ifndef PORT_H
#define PORT_H

#if defined(__LP64__) || defined(__LLP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__) ) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
  #define XASH_64BIT
#endif

#ifdef XASH_64BIT
#define ARCH_SUFFIX "64"
#else
#define ARCH_SUFFIX
#endif

#if defined(__ANDROID__) || TARGET_OS_IOS
#define XASH_MOBILE_PLATFORM
#endif

#ifdef _WIN32
#define PATH_SPLITTER "\\"
#else
#define PATH_SPLITTER "/"
#endif

#if !defined(_WIN32)
	#include <limits.h>
	#include <dlfcn.h>
	#include <stdlib.h>
	#include <unistd.h>
	#include <string.h>

	#if defined(__APPLE__)
		#include <sys/syslimits.h>
		#ifdef TARGET_OS_IPHONE
			#define OS_LIB_EXT ""
			#define NO_LIB_EXT
		#else
			#define OS_LIB_EXT "dylib"
		#endif
		#define OPEN_COMMAND "open"
		#include "TargetConditionals.h"
	#else
		#define OS_LIB_EXT "so"
		#define OPEN_COMMAND "xdg-open"
	#endif

	#define OS_LIB_PREFIX "lib"

	#if defined(__ANDROID__)
		//#if defined(LOAD_HARDFP)
		//	#define POSTFIX "_hardfp"
		//#else
			#define POSTFIX
		//#endif

		// don't change these names
		#define MENUDLL   "libmenu"   POSTFIX "." OS_LIB_EXT
		#define CLIENTDLL "libclient" POSTFIX "." OS_LIB_EXT
		#define SERVERDLL "libserver" POSTFIX "." OS_LIB_EXT
		#define GAMEPATH "/sdcard/xash"
	#elif defined(TARGET_OS_IPHONE)
		#define MENUDLL   "libmenu"
		#define CLIENTDLL "client"
		#define SERVERDLL "server"
	#else
	        #define MENUDLL "libmenu" ARCH_SUFFIX "." OS_LIB_EXT
	        #define CLIENTDLL "client" ARCH_SUFFIX "." OS_LIB_EXT
	#endif

	#define VGUI_SUPPORT_DLL "libvgui_support." OS_LIB_EXT

	// Windows-specific
	#define __cdecl
	#define __stdcall

	#define _inline	static inline
	#define FORCEINLINE inline __attribute__((always_inline))
	#define O_BINARY 0 // O_BINARY is Windows extension
	#define O_TEXT 0 // O_TEXT is Windows extension

	// Windows functions to Linux equivalent
	#define _mkdir( x )					mkdir( x, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH )
	#define LoadLibrary( x )			dlopen( x, RTLD_NOW )
	#define GetProcAddress( x, y )		dlsym( x, y )
	#define SetCurrentDirectory( x )	(!chdir( x ))
	#define FreeLibrary( x )			dlclose( x )
	//#define MAKEWORD( a, b )			((short int)(((unsigned char)(a))|(((short int)((unsigned char)(b)))<<8)))
	#define max( a, b )                 (((a) > (b)) ? (a) : (b))
	#define min( a, b )                 (((a) < (b)) ? (a) : (b))
	#define tell( a )					lseek(a, 0, SEEK_CUR)

	typedef unsigned char	BYTE;
	typedef short int	    WORD;
	typedef unsigned int    DWORD;
	typedef int	    LONG;
	typedef unsigned int   ULONG;
	typedef int			WPARAM;
	typedef unsigned int    LPARAM;

	typedef void* HANDLE;
	typedef void* HMODULE;
	typedef void* HINSTANCE;

	typedef char* LPSTR;

	typedef struct tagPOINT
	{
		int x, y;
	} POINT;
#else // WIN32
	#ifdef __MINGW32__
		#define _inline static inline
		#define FORCEINLINE inline __attribute__((always_inline))
	#else
		#define FORCEINLINE __forceinline
	#endif

	#define strcasecmp _stricmp
	#define strncasecmp _strnicmp
	#define open _open
	#define read _read
	#define alloca _alloca

	// shut-up compiler warnings
	#pragma warning(disable : 4244)	// MIPS
	#pragma warning(disable : 4018)	// signed/unsigned mismatch
	#pragma warning(disable : 4305)	// truncation from const double to float
	#pragma warning(disable : 4115)	// named type definition in parentheses
	#pragma warning(disable : 4100)	// unreferenced formal parameter
	#pragma warning(disable : 4127)	// conditional expression is constant
	#pragma warning(disable : 4057)	// differs in indirection to slightly different base types
	#pragma warning(disable : 4201)	// nonstandard extension used
	#pragma warning(disable : 4706)	// assignment within conditional expression
	#pragma warning(disable : 4054)	// type cast' : from function pointer
	#pragma warning(disable : 4310)	// cast truncates constant value

	#define HSPRITE WINAPI_HSPRITE
		#include <windows.h>
	#undef HSPRITE

	#define OS_LIB_EXT "dll"
	#define MENUDLL "menu"ARCH_SUFFIX"." OS_LIB_EXT
	#define CLIENTDLL "client"ARCH_SUFFIX"." OS_LIB_EXT
	#define VGUI_SUPPORT_DLL "../vgui_support." OS_LIB_EXT
#ifdef XASH_64BIT
// windows NameForFunction not implemented yet
#define XASH_ALLOW_SAVERESTORE_OFFSETS
#endif
#endif //WIN32

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

#ifndef USHRT_MAX
#define USHRT_MAX 65535
#endif

#if defined XASH_SDL && !defined REF_DLL
#include <SDL.h>
#endif

#endif // PORT_H
