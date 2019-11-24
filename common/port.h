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

#include "build.h"

#if !XASH_WIN32
	#include <dlfcn.h>
	#include <unistd.h>

	#define PATH_SPLITTER "/"

	#if XASH_APPLE
		#include <sys/syslimits.h>
		#include "TargetConditionals.h"
		#define OS_LIB_EXT "dylib"
		#define OPEN_COMMAND "open"
	#else
		#define OS_LIB_EXT "so"
		#define OPEN_COMMAND "xdg-open"
	#endif

	#define OS_LIB_PREFIX "lib"

	#if XASH_ANDROID
		//#if defined(LOAD_HARDFP)
		//	#define POSTFIX "_hardfp"
		//#else
			#define POSTFIX
		//#endif
	#else
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
	#define PATH_SPLITTER "\\"
	#ifdef __MINGW32__
		#define _inline static inline
		#define FORCEINLINE inline __attribute__((always_inline))
	#else
		#define FORCEINLINE __forceinline
	#endif

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
		#define WIN32_LEAN_AND_MEAN
		#include <winsock2.h>
		#include <windows.h>
	#undef HSPRITE

	#define OS_LIB_PREFIX ""
	#define OS_LIB_EXT "dll"
	#define VGUI_SUPPORT_DLL "../vgui_support." OS_LIB_EXT
	#ifdef XASH_64BIT
		// windows NameForFunction not implemented yet
		#define XASH_ALLOW_SAVERESTORE_OFFSETS
	#endif
#endif //WIN32
#ifndef XASH_LOW_MEMORY
#define XASH_LOW_MEMORY 0
#endif

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if defined XASH_SDL && !defined REF_DLL
#include <SDL.h>
#endif

#endif // PORT_H
