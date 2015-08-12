
# Uncomment this if you're using STL in your project
# See CPLUSPLUS-SUPPORT.html in the NDK documentation for more information
APP_STL := stlport_static 

XASH_SDL ?= 1

ifeq ($(XASH_SDL),1)
APP_PLATFORM := android-12
else
APP_PLATFORM := android-8
endif


CFLAGS_OPT :=  -O3 -fomit-frame-pointer -ggdb -funsafe-math-optimizations -ftree-vectorize -fgraphite-identity -floop-interchange -funsafe-loop-optimizations -finline-limit=1024
CFLAGS_OPT_ARM := -mthumb -mfpu=neon -mcpu=cortex-a9 -pipe -mvectorize-with-neon-quad -DVECTORIZE_SINCOS
CFLAGS_OPT_ARMv5 := -msoft-float -marm -pipe
CFLAGS_OPT_X86 := -mtune=atom -march=atom -mssse3 -mfpmath=sse -funroll-loops -pipe -DVECTORIZE_SINCOS

APPLICATIONMK_PATH = $(call my-dir)

ifeq ($(EMILE),1)
SDL_PATH    := $(APPLICATIONMK_PATH)/src/SDL-mirror
else
SDL_PATH    := $(APPLICATIONMK_PATH)/src/SDL2
endif


TOUCHCONTROLS_PATH := $(APPLICATIONMK_PATH)/src/MobileTouchControls


NANOGL_PATH := $(APPLICATIONMK_PATH)/src/NanoGL/nanogl

XASH3D_PATH := $(APPLICATIONMK_PATH)/src/Xash3D/xash3d

XASHXT_PATH := $(APPLICATIONMK_PATH)/src/XashXT/XashXT

HLSDK_PATH  := $(APPLICATIONMK_PATH)/src/HLSDK/halflife/

APP_ABI := x86 armeabi-v7a-hard
# Use armeabi-v7a to disable hardfloat, armeabi to build armv5 xash3d
# Change CFLAGS_OPT_ARMv5 to "-mfloat-abi=softfp -mfpu=vfp" and fix setup.mk of ndk to build armv6
# Mods are built with both ABI support
# ARMv6 and ARMv5 xash3d builds use softfp only and compatible only with softfp mods
# Build both armeabi-v7a-hard and armeabi-v7a supported only for mods, not for engine

APP_MODULES := xash menu client server NanoGL
ifeq ($(XASH_SDL),1)
APP_MODULES += SDL2
endif
