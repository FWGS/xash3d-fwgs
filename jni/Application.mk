
# Uncomment this if you're using STL in your project
# See CPLUSPLUS-SUPPORT.html in the NDK documentation for more information
 APP_STL := stlport_static 

CFLAGS_OPT :=  -O1 -fno-omit-frame-pointer -ggdb
CFLAGS_OPT_ARM := -mthumb -mfloat-abi=soft -msoft-float -mcpu=cortex-a9
CFLAGS_OPT_X86 := -msse3

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

APP_ABI := armeabi-v7a
APP_MODULES := xash menu client server NanoGL
ifeq ($(XASH_SDL),1)
APP_MODULES += SDL2
endif
