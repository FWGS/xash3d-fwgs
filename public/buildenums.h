/*
build.h - compile-time build information
Copyright (C) 2023 Alibek Omarov

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
#define PLATFORM_EMSCRIPTEN 8
#define PLATFORM_DOS4GW     9
#define PLATFORM_HAIKU      10
#define PLATFORM_SERENITY   11
#define PLATFORM_IRIX       12
#define PLATFORM_NSWITCH    13
#define PLATFORM_PSVITA     14
#define PLATFORM_WASI       15
#define PLATFORM_SUNOS      16
#define PLATFORM_HURD       17

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
#elif XASH_EMSCRIPTEN
	#define XASH_PLATFORM PLATFORM_EMSCRIPTEN
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
#define ARCHITECTURE_JS      6
#define ARCHITECTURE_E2K     7
#define ARCHITECTURE_RISCV   8
#define ARCHITECTURE_PPC     9
#define ARCHITECTURE_WASM    10

#if XASH_AMD64
	#define XASH_ARCHITECTURE ARCHITECTURE_AMD64
#elif XASH_X86
	#define XASH_ARCHITECTURE ARCHITECTURE_X86
#elif XASH_ARM
	#define XASH_ARCHITECTURE ARCHITECTURE_ARM
#elif XASH_MIPS
	#define XASH_ARCHITECTURE ARCHITECTURE_MIPS
#elif XASH_JS
	#define XASH_ARCHITECTURE ARCHITECTURE_JS
#elif XASH_E2K
	#define XASH_ARCHITECTURE ARCHITECTURE_E2K
#elif XASH_RISCV
	#define XASH_ARCHITECTURE ARCHITECTURE_RISCV
#elif XASH_PPC
	#define XASH_ARCHITECTURE ARCHITECTURE_PPC
#elif XASH_WASM
	#define XASH_ARCHITECTURE ARCHITECTURE_WASM
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
#define BIT( n )		( 1U << ( n ))

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
