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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "build.h"

#if XASH_APPLE
	#include <sys/syslimits.h>
	#define OS_LIB_PREFIX "lib"
	#define OS_LIB_EXT    "dylib"
	#define OPEN_COMMAND  "open"
#elif XASH_POSIX
	#include <unistd.h>
	#define OS_LIB_PREFIX "lib"
	#define OS_LIB_EXT    "so"
	#define OPEN_COMMAND  "xdg-open"
#elif XASH_WIN32
	#define HSPRITE WINAPI_HSPRITE
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#define VC_EXTRALEAN

	#include <windows.h>
	#undef HSPRITE
	#define open          _open
	#define read          _read
	#define alloca        _alloca
	#define OS_LIB_PREFIX ""
	#define OS_LIB_EXT    "dll"
	#define OPEN_COMMAND  "open"
#endif

#if !defined( _MSC_VER )
	#define __cdecl
	#define __stdcall
#endif

#if !XASH_WIN32
	typedef void* HINSTANCE;
	typedef struct tagPOINT	{ int x, y; } POINT; // one nasty function in cdll_int.h needs it
#endif // !XASH_WIN32

#ifndef XASH_LOW_MEMORY
	#define XASH_LOW_MEMORY 0
#endif

#endif // PORT_H
