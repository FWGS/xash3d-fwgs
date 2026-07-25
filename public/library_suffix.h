/*
library_suffix.h - main library-suffix API header

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/
#pragma once
#ifndef LIBRARY_SUFFIX_H
#define LIBRARY_SUFFIX_H

#include "build.h"
#include "buildenums.h"

#if XASH_APPLE
	#define OS_LIB_EXT "dylib"
#elif XASH_WIN32
	#define OS_LIB_EXT "dll"
#elif XASH_PSP
	#define OS_LIB_EXT "prx"
#elif XASH_POSIX
	#define OS_LIB_EXT "so"
#else
	#error
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
	#define RETURNS_NONNULL __attribute__(( returns_nonnull ))
#else
	#define RETURNS_NONNULL
#endif

// returns cpu or os name by it's predefined id
const char *Q_PlatformStringByID( int platform ) RETURNS_NONNULL;
const char *Q_ArchitectureStringByID( int arch, unsigned int abi, int endianness, int is64 ) RETURNS_NONNULL;

// returns current build cpu and os
const char *Q_buildos( void ) RETURNS_NONNULL;
const char *Q_buildarch( void ) RETURNS_NONNULL;

// generate library filename based on system properties
// returns a string in format of
// <prefix><name>_<os>_<arch>.<ext>
// where
// - prefix: widely accepted DLL prefix. Currently only adds "lib" on Android
// - name:   DLL name, without Intel suffix (like _i?86). You can strip it with COM_StripIntelSuffix
// - os:     Q_buildos return value, omitted for win32, linux and osx
// - arch:   Q_buildarch return value, omitted for 32-bit x86 on win32, linux and osx
// - ext:    widely accepted DLL extension
// return value: number of bytes written, excluding null terminator, or -1 on overflow
int COM_GenerateCommonLibraryName( const char *name, char *out, size_t size );

// version of COM_GenerateCommonLibraryName that accepts custom architectures and systems
int COM_GenerateLibraryName( char *out, size_t size,
	const char *prefix, const char *name,
	int platform,
	int arch, unsigned int abi, int endianness, int is64,
	const char *ext );

// checks if string matches following regex '_i[3-6]86$' and strips the suffix
// hl_i386 -> hl
void COM_StripIntelSuffix( char *out );

#ifdef __cplusplus
}
#endif

#endif // LIBRARY_SUFFIX_H
