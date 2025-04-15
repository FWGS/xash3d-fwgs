#pragma once
#ifndef BUILD_H
#define BUILD_H

/*
All XASH_* macros set by this header are guaranteed to have positive value
otherwise not defined.

Every macro is intended to be the unified interface for buildsystems that lack
platform & CPU detection, and a neat quick way for checks in platform code
For Q_build* macros, refer to buildenums.h

Any new define must be undefined at first
You can generate #undef list below with this oneliner:
  $ sed 's/\t//g' build.h | grep '^#define XASH' | awk '{ print $2 }' | \
		sort | uniq | awk '{ print "#undef " $1 }'

Then you can use another oneliner to query all variables:
  $ grep '^#undef XASH' build.h | awk '{ print $2 }'
*/

#undef XASH_64BIT
#undef XASH_AMD64
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
#undef XASH_DOS4GW
#undef XASH_E2K
#undef XASH_EMSCRIPTEN
#undef XASH_FREEBSD
#undef XASH_HAIKU
#undef XASH_IRIX
#undef XASH_JS
#undef XASH_LINUX
#undef XASH_LINUX_UNKNOWN
#undef XASH_LITTLE_ENDIAN
#undef XASH_MIPS
#undef XASH_NETBSD
#undef XASH_OPENBSD
#undef XASH_POSIX
#undef XASH_RISCV
#undef XASH_RISCV_DOUBLEFP
#undef XASH_RISCV_SINGLEFP
#undef XASH_RISCV_SOFTFP
#undef XASH_SERENITY
#undef XASH_WIN32
#undef XASH_X86

//================================================================
//
//           PLATFORM DETECTION CODE
//
//================================================================
#if defined _WIN32
	#define XASH_WIN32 1
#elif defined __EMSCRIPTEN__
	#define XASH_EMSCRIPTEN 1
#elif defined __WATCOMC__ && defined __DOS__
	#define XASH_DOS4GW 1
#else // POSIX compatible
	#define XASH_POSIX 1
	#if defined __linux__
		#include <features.h>
		// if our system libc has features.h header
		// try to detect it to not confuse other libcs with built with glibc game libraries
		#if !defined __GLIBC__
			#define XASH_LINUX_UNKNOWN 1
		#endif
		#define XASH_LINUX 1
	#elif defined __FreeBSD__
		#define XASH_FREEBSD 1
	#elif defined __NetBSD__
		#define XASH_NETBSD 1
	#elif defined __OpenBSD__
		#define XASH_OPENBSD 1
	#elif defined __HAIKU__
		#define XASH_HAIKU 1
	#elif defined __serenity__
		#define XASH_SERENITY 1
	#elif defined __sgi
		#define XASH_IRIX 1
	#elif defined __APPLE__
		#include <TargetConditionals.h>
		#define XASH_APPLE 1
	#else
		#error
	#endif
#endif

//================================================================
//
//           ENDIANNESS DEFINES
//
//================================================================

#if !defined XASH_ENDIANNESS
	#if defined XASH_WIN32 || __LITTLE_ENDIAN__
		//!!! Probably all WinNT installations runs in little endian
		#define XASH_LITTLE_ENDIAN 1
	#elif __BIG_ENDIAN__
		#define XASH_BIG_ENDIAN 1
	#elif defined __BYTE_ORDER__ && defined __ORDER_BIG_ENDIAN__ && defined __ORDER_LITTLE_ENDIAN__ // some compilers define this
		#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			#define XASH_BIG_ENDIAN 1
		#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
			#define XASH_LITTLE_ENDIAN 1
		#endif
	#else
		#include <sys/param.h>
		#if __BYTE_ORDER == __BIG_ENDIAN
			#define XASH_BIG_ENDIAN 1
		#elif __BYTE_ORDER == __LITTLE_ENDIAN
			#define XASH_LITTLE_ENDIAN 1
		#endif
	#endif // !XASH_WIN32
#endif

//================================================================
//
//           CPU ARCHITECTURE DEFINES
//
//================================================================
#if defined __x86_64__ || defined _M_X64
#ifdef LINUX
	#define XASH_X86 1
#else
	#define XASH_64BIT 1
	#define XASH_AMD64 1
#endif
#elif defined __i386__ || defined _X86_ || defined _M_IX86
	#define XASH_X86 1
#elif defined __aarch64__ || defined _M_ARM64
	#define XASH_64BIT 1
	#define XASH_ARM   8
#elif defined __mips__
	#define XASH_MIPS 1
#elif defined __EMSCRIPTEN__
	#define XASH_JS 1
#elif defined __e2k__
	#define XASH_64BIT 1
	#define XASH_E2K 1
#elif defined _M_ARM // msvc
	#define XASH_ARM 7
	#define XASH_ARM_HARDFP 1
#elif defined __arm__
	#if __ARM_ARCH == 8 || __ARM_ARCH_8__
		#define XASH_ARM 8
	#elif __ARM_ARCH == 7 || __ARM_ARCH_7__
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

	#if defined __SOFTFP__ || __ARM_PCS_VFP == 0
		#define XASH_ARM_SOFTFP 1
	#else // __SOFTFP__
		#define XASH_ARM_HARDFP 1
	#endif // __SOFTFP__
#elif defined __riscv
	#define XASH_RISCV 1

	#if __riscv_xlen == 64
		#define XASH_64BIT 1
	#elif __riscv_xlen != 32
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
