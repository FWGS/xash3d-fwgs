/*
buildenums.h - platforms/architectures enumeration values

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
#ifndef BUILDENUMS_H
#define BUILDENUMS_H

#include "build.h"

// This header defines the enumeration values that can be passed to Q_build*
// functions and get current value through XASH_PLATFORM, XASH_ARCHITECTURE and
// XASH_ARCHITECTURE_ABI defines

//================================================================
//
//           OPERATING SYSTEM DEFINES
//
//================================================================
#define PLATFORM_WIN32      1
#define PLATFORM_LINUX      2
#define PLATFORM_FREEBSD    3
#define PLATFORM_ANDROID    4
#define PLATFORM_APPLE      5
#define PLATFORM_NETBSD     6
#define PLATFORM_OPENBSD    7
// reserved
#define PLATFORM_DOS4GW     9
#define PLATFORM_HAIKU      10
#define PLATFORM_SERENITY   11
#define PLATFORM_IRIX       12
#define PLATFORM_NSWITCH    13
#define PLATFORM_PSVITA     14
#define PLATFORM_WASI       15
#define PLATFORM_SUNOS      16
#define PLATFORM_HURD       17
#define PLATFORM_PSP        18
#define PLATFORM_GAMECUBE   19

#if XASH_WIN32
	#define XASH_PLATFORM PLATFORM_WIN32
#elif XASH_ANDROID
	#define XASH_PLATFORM PLATFORM_ANDROID
#elif XASH_LINUX
	#define XASH_PLATFORM PLATFORM_LINUX
#elif XASH_APPLE
	#define XASH_PLATFORM PLATFORM_APPLE
#elif XASH_FREEBSD
	#define XASH_PLATFORM PLATFORM_FREEBSD
#elif XASH_NETBSD
	#define XASH_PLATFORM PLATFORM_NETBSD
#elif XASH_OPENBSD
	#define XASH_PLATFORM PLATFORM_OPENBSD
#elif XASH_DOS4GW
	#define XASH_PLATFORM PLATFORM_DOS4GW
#elif XASH_HAIKU
	#define XASH_PLATFORM PLATFORM_HAIKU
#elif XASH_SERENITY
	#define XASH_PLATFORM PLATFORM_SERENITY
#elif XASH_IRIX
	#define XASH_PLATFORM PLATFORM_IRIX
#elif XASH_NSWITCH
	#define XASH_PLATFORM PLATFORM_NSWITCH
#elif XASH_PSVITA
	#define XASH_PLATFORM PLATFORM_PSVITA
#elif XASH_WASI
	#define XASH_PLATFORM PLATFORM_WASI
#elif XASH_SUNOS
	#define XASH_PLATFORM PLATFORM_SUNOS
#elif XASH_HURD
	#define XASH_PLATFORM PLATFORM_HURD
#elif XASH_PSP
	#define XASH_PLATFORM PLATFORM_PSP
#elif XASH_GAMECUBE
	#define XASH_PLATFORM PLATFORM_GAMECUBE
#else
	#error
#endif

//================================================================
//
//           CPU ARCHITECTURE DEFINES
//
//================================================================
#define ARCHITECTURE_X86     1
#define ARCHITECTURE_AMD64   2
#define ARCHITECTURE_ARM     3
#define ARCHITECTURE_MIPS    4
// reserved
#define ARCHITECTURE_E2K     7
#define ARCHITECTURE_RISCV   8
#define ARCHITECTURE_PPC     9
#define ARCHITECTURE_WASM    10
#define ARCHITECTURE_SPARC   11

#if XASH_AMD64
	#define XASH_ARCHITECTURE ARCHITECTURE_AMD64
#elif XASH_X86
	#define XASH_ARCHITECTURE ARCHITECTURE_X86
#elif XASH_ARM
	#define XASH_ARCHITECTURE ARCHITECTURE_ARM
#elif XASH_MIPS
	#define XASH_ARCHITECTURE ARCHITECTURE_MIPS
#elif XASH_E2K
	#define XASH_ARCHITECTURE ARCHITECTURE_E2K
#elif XASH_RISCV
	#define XASH_ARCHITECTURE ARCHITECTURE_RISCV
#elif XASH_PPC
	#define XASH_ARCHITECTURE ARCHITECTURE_PPC
#elif XASH_WASM
	#define XASH_ARCHITECTURE ARCHITECTURE_WASM
#elif XASH_SPARC
	#define XASH_ARCHITECTURE ARCHITECTURE_SPARC
#else
	#error
#endif

//================================================================
//
//           ENDIANNESS DEFINES
//
//================================================================
#define ENDIANNESS_LITTLE  1
#define ENDIANNESS_BIG     2

#if XASH_LITTLE_ENDIAN
	#define XASH_ENDIANNESS ENDIANNESS_LITTLE
#elif XASH_BIG_ENDIAN
	#define XASH_ENDIANNESS ENDIANNESS_BIG
#else
	#error
#endif

//================================================================
//
//           APPLICATION BINARY INTERFACE
//
//================================================================
#define BIT( n )                    ( 1U << ( n ))
#define FBitSet( bit_vector, bits ) (( bit_vector ) & ( bits ))

#define ARCH_ARM_VER_MASK   ( BIT( 5 ) - 1 )
#define ARCH_ARM_VER_SHIFT  0
#define ARCH_ARM_HARDFP     BIT( 5 )

#define ARCH_RISCV_FP_SOFT   0
#define ARCH_RISCV_FP_SINGLE 1
#define ARCH_RISCV_FP_DOUBLE 2

#if XASH_ARCHITECTURE == ARCHITECTURE_ARM
	#if XASH_ARM_HARDFP
		#define XASH_ARCHITECTURE_ABI ( ARCH_ARM_HARDFP | XASH_ARM )
	#else
		#define XASH_ARCHITECTURE_ABI ( XASH_ARM )
	#endif
#elif XASH_ARCHITECTURE == ARCHITECTURE_RISCV
	#if XASH_RISCV_SOFTFP
		#define XASH_ARCHITECTURE_ABI ARCH_RISCV_FP_SOFT
	#elif XASH_RISCV_SINGLEFP
		#define XASH_ARCHITECTURE_ABI ARCH_RISCV_FP_SINGLE
	#elif XASH_RISCV_DOUBLEFP
		#define XASH_ARCHITECTURE_ABI ARCH_RISCV_FP_DOUBLE
	#else
		#error
	#endif
#else
	#define XASH_ARCHITECTURE_ABI 0 // unused
#endif


#endif // BUILDENUMS_H
