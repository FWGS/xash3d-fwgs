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
	#if XASH_APPLE
		#include <sys/syslimits.h>
		#define OS_LIB_EXT "dylib"
		#define OPEN_COMMAND "open"
	#elif XASH_EMSCRIPTEN
		#define OS_LIB_EXT "wasm"
		#define OPEN_COMMAND "???"
	#else
		#define OS_LIB_EXT "so"
		#define OPEN_COMMAND "xdg-open"
	#endif
	#define OS_LIB_PREFIX "lib"
	#define VGUI_SUPPORT_DLL "libvgui_support." OS_LIB_EXT

	// Windows-specific
	#define __cdecl
	#define __stdcall
	#define _inline	static inline

	#if XASH_POSIX
		#include <unistd.h>
		#if XASH_NSWITCH
			#define SOLDER_LIBDL_COMPAT
			#include <solder.h>
		#elif XASH_PSVITA
			#define VRTLD_LIBDL_COMPAT
			#include <vrtld.h>
			#define O_BINARY 0
		#else
			#include <dlfcn.h>
			#define HAVE_DUP
			#define O_BINARY 0
		#endif
		#define O_TEXT 0
		#define _mkdir( x ) mkdir( x, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH )
	#endif

	typedef void* HANDLE;
	typedef void* HINSTANCE;

	typedef struct tagPOINT
	{
		int x, y;
	} POINT;
#else // WIN32
	#define open _open
	#define read _read
	#define alloca _alloca

	#define HSPRITE WINAPI_HSPRITE
		#define WIN32_LEAN_AND_MEAN
		#include <winsock2.h>
		#include <windows.h>
	#undef HSPRITE

	#define OS_LIB_PREFIX ""
	#define OS_LIB_EXT "dll"
	#define VGUI_SUPPORT_DLL "../vgui_support." OS_LIB_EXT
	#define HAVE_DUP
#endif //WIN32

#ifndef XASH_LOW_MEMORY
#define XASH_LOW_MEMORY 0
#endif

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#endif // PORT_H
