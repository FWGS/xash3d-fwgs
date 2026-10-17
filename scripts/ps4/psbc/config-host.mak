# Host (Linux x86_64) build configuration for opengnm-psbc, used by build.sh.
# Upstream only has config.orbis.mak and a Makefile written on ARM64 macOS.

DESTDIR=/usr/local
BINDIR=/bin

OPENGNM_INCLUDE?=../opengnm/include

# _GNU_SOURCE: pthread_barrier_t is hidden with _XOPEN_SOURCE=500 in glibc
# BLAKE3_*: portable implementation only, SSE/AVX variants are assembly files
SHARED_FLAGS=\
	-Iinclude/ \
	-Ilibpsbc/ \
	-I$(OPENGNM_INCLUDE) \
	-I../Vulkan-Headers/include \
	-Isrc/ \
	-Isrc/amd \
	-Isrc/amd/common \
	-Isrc/amd/common/nir \
	-Isrc/amd/compiler \
	-Isrc/amd/vulkan \
	-Isrc/amd/vulkan/nir \
	-Isrc/vulkan/runtime \
	-Isrc/vulkan/runtime/bvh \
	-Isrc/vulkan/util \
	-Isrc/compiler \
	-Isrc/compiler/nir \
	-Isrc/compiler/spirv \
	-Isrc/gallium/include \
	-Isrc/mesa \
	-Isrc/mesa/main \
	-Isrc/util \
	-Icmd/psbc \
	-Iinclude/mesa \
	-D_GNU_SOURCE \
	-DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 -DBLAKE3_USE_NEON=0 \
	-DUTIL_ARCH_LITTLE_ENDIAN=1 \
	-DUTIL_ARCH_BIG_ENDIAN=0 \
	-DHAVE_STRUCT_TIMESPEC=1 \
	-DHAVE_PTHREAD=1

CC=clang-18
CXX=clang++-18
LD=clang++-18
PYTHON=python3
AR=ar

CFLAGS=-std=gnu11 -Wall -O2 -g $(SHARED_FLAGS) \
       -Wno-macro-redefined -Wno-typedef-redefinition \
       -Wno-unused-function -Wno-unused-variable \
       -Dalloca=__builtin_alloca \
       -DHAVE_SYSCONF=1 \
       -include strings.h
CXXFLAGS=-std=c++17 -Wall -O2 -g $(SHARED_FLAGS) \
         -Wno-macro-redefined -Wno-typedef-redefinition \
         -Wno-unused-function -Wno-unused-variable \
         -Dalloca=__builtin_alloca \
         -DHAVE_SYSCONF=1 \
         -include strings.h
LDFLAGS=-lm
