/*
build.h - compile-time build information

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
#ifndef BUILD_H
#define BUILD_H

// All XASH_* macros set by this header are guaranteed to have positive value otherwise not defined

// Any new define must be undefined at first
// You can generate #undef list below with this oneliner:
//   $ cat build.h | sed 's/\t//g' | grep '^#define XASH' | awk '{ print $2 }' | sort | uniq | awk '{ print "#undef " $1 }'
//
// So in various buildscripts you can grep for ^#undef XASH and select only second word
// or in another oneliner:
//   $ cat build.h | grep '^#undef XASH' | awk '{ print $2 }'

#undef XASH_64BIT
#undef XASH_AMD64
#undef XASH_ANDROID
#undef XASH_APPLE
#undef XASH_ARM
#undef XASH_ARM_HARDFP
#undef XASH_ARM_SOFTFP
#undef XASH_ARMv4
#undef XASH_ARMv5
#undef XASH_ARMv6
#undef XASH_ARMv7
#undef XASH_ARMv8
#undef XASH_BIG_ENDIAN
#undef XASH_BSD
#undef XASH_DOS4GW
#undef XASH_E2K
#undef XASH_EMSCRIPTEN
#undef XASH_FREEBSD
#undef XASH_HAIKU
#undef XASH_IOS
#undef XASH_JS
#undef XASH_LINUX
#undef XASH_LITTLE_ENDIAN
#undef XASH_MINGW
#undef XASH_MIPS
#undef XASH_MOBILE_PLATFORM
#undef XASH_MSVC
#undef XASH_NETBSD
#undef XASH_OPENBSD
#undef XASH_POSIX
#undef XASH_RISCV
#undef XASH_RISCV_DOUBLEFP
#undef XASH_RISCV_SINGLEFP
#undef XASH_RISCV_SOFTFP
#undef XASH_WIN32
#undef XASH_WIN64
#undef XASH_X86

//================================================================
//
//           OPERATING SYSTEM DEFINES
//
//================================================================
#if defined(_WIN32)
	#define XASH_WIN32 1
	#if defined(__MINGW32__)
		#define XASH_MINGW 1
	#elif defined(_MSC_VER)
		#define XASH_MSVC 1
	#endif

	#if defined(_WIN64)
		#define XASH_WIN64 1
	#endif
#elif defined(__linux__)
	#define XASH_LINUX 1
	#if defined(__ANDROID__)
		#define XASH_ANDROID 1
	#endif // defined(__ANDROID__)
	#define XASH_POSIX 1
#elif defined(__APPLE__)
	#include <TargetConditionals.h>
	#define XASH_APPLE 1
	#if TARGET_OS_IOS
		#define XASH_IOS 1
	#endif // TARGET_OS_IOS
	#define XASH_POSIX 1
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
	#define XASH_BSD 1
	#if defined(__FreeBSD__)
		#define XASH_FREEBSD 1
	#elif defined(__NetBSD__)
		#define XASH_NETBSD 1
	#elif defined(__OpenBSD__)
		#define XASH_OPENBSD 1
	#endif
	#define XASH_POSIX 1
#elif defined __EMSCRIPTEN__
	#define XASH_EMSCRIPTEN 1
#elif defined __WATCOMC__ && defined __DOS__
	#define XASH_DOS4GW 1
	#define XASH_LITTLE_ENDIAN 1
#elif defined __HAIKU__
	#define XASH_HAIKU 1
	#define XASH_POSIX 1
#else
#error "Place your operating system name here! If this is a mistake, try to fix conditions above and report a bug"
#endif

#if defined XASH_ANDROID || defined XASH_IOS
	#define XASH_MOBILE_PLATFORM 1
#endif

//================================================================
//
//           ENDIANNESS DEFINES
//
//================================================================

#if defined(XASH_FORCE_LITTLE_ENDIAN) && defined(XASH_FORCE_BIG_ENDIAN)
	#error "Both XASH_FORCE_LITTLE_ENDIAN and XASH_FORCE_BIG_ENDIAN are defined"
#elif defined(XASH_FORCE_LITTLE_ENDIAN)
	#define XASH_LITTLE_ENDIAN 1
#elif defined(XASH_FORCE_BIG_ENDIAN)
	#define XASH_BIG_ENDIAN 1
#endif

#if !defined(XASH_LITTLE_ENDIAN) && !defined(XASH_BIG_ENDIAN)
	#if defined XASH_MSVC || __LITTLE_ENDIAN__
		//!!! Probably all WinNT installations runs in little endian
		#define XASH_LITTLE_ENDIAN 1
	#elif __BIG_ENDIAN__
		#define XASH_BIG_ENDIAN 1
	#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && defined(__ORDER_LITTLE_ENDIAN__) // some compilers define this
		#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			#define XASH_BIG_ENDIAN 1
		#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
			#define XASH_LITTLE_ENDIAN 1
		#else
			#error "Unknown endianness!"
		#endif
	#else
		#include <sys/param.h>
		#if __BYTE_ORDER == __BIG_ENDIAN
			#define XASH_BIG_ENDIAN 1
		#elif __BYTE_ORDER == __LITTLE_ENDIAN
			#define XASH_LITTLE_ENDIAN 1
		#else
			#error "Unknown endianness!"
		#endif
	#endif // !XASH_WIN32
#endif

//================================================================
//
//           CPU ARCHITECTURE DEFINES
//
//================================================================
#if defined(__x86_64__) || defined(_M_X64)
	#define XASH_64BIT 1
	#define XASH_AMD64 1
#elif defined(__i386__) || defined(_X86_) || defined(_M_IX86)
	#define XASH_X86 1
#elif defined __aarch64__ || defined _M_ARM64
	#define XASH_64BIT 1
	#define XASH_ARM 8
#elif defined __arm__ || defined _M_ARM
	#if __ARM_ARCH == 8 || __ARM_ARCH_8__
		#define XASH_ARM 8
	#elif __ARM_ARCH == 7 || __ARM_ARCH_7__ || defined _M_ARM // msvc can only armv7 in 32 bit
		#define XASH_ARM 7
	#elif __ARM_ARCH == 6 || __ARM_ARCH_6__ || __ARM_ARCH_6J__
		#define XASH_ARM 6
	#elif __ARM_ARCH == 5 || __ARM_ARCH_5__
		#define XASH_ARM 5
	#elif __ARM_ARCH == 4 || __ARM_ARCH_4__
		#define XASH_ARM 4
	#else
		#error "Unknown ARM"
	#endif

	#if defined _M_ARM
		#error "No WinMobile port yet! Need to determine which ARM float ABI msvc uses if applicable"
	#endif

	#if defined __SOFTFP__ || __ARM_PCS_VFP == 0
		#define XASH_ARM_SOFTFP 1
	#else // __SOFTFP__
		#define XASH_ARM_HARDFP 1
	#endif // __SOFTFP__
#elif defined __mips__
	#define XASH_MIPS 1
#elif defined __EMSCRIPTEN__
	#define XASH_JS 1
#elif defined __e2k__
	#define XASH_64BIT 1
	#define XASH_E2K 1
#elif defined __riscv
	#define XASH_RISCV 1
	#if __riscv_xlen == 64
		#define XASH_64BIT 1
	#elif __riscv_xlen == 32
		// ...
	#else
		#error "Unknown RISC-V ABI"
	#endif

	#if defined __riscv_float_abi_soft
		#define XASH_RISCV_SOFTFP 1
	#elif defined __riscv_float_abi_single
		#define XASH_RISCV_SINGLEFP 1
	#elif defined __riscv_float_abi_double
		#define XASH_RISCV_DOUBLEFP 1
	#else
		#error "Unknown RISC-V float ABI"
	#endif
#else
	#error "Place your architecture name here! If this is a mistake, try to fix conditions above and report a bug"
#endif

#if defined(XASH_WAF_DETECTED_64BIT) && !defined(XASH_64BIT)
	#define XASH_64BIT 1
#endif

#if XASH_ARM == 8
	#define XASH_ARMv8 1
#elif XASH_ARM == 7
	#define XASH_ARMv7 1
#elif XASH_ARM == 6
	#define XASH_ARMv6 1
#elif XASH_ARM == 5
	#define XASH_ARMv5 1
#elif XASH_ARM == 4
	#define XASH_ARMv4 1
#endif

#endif // BUILD_H
