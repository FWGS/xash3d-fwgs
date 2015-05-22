
# Uncomment this if you're using STL in your project
# See CPLUSPLUS-SUPPORT.html in the NDK documentation for more information
 APP_STL := stlport_static 



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

APP_ABI := armeabi-v7a x86
APP_MODULES := SDL2 xash menu client server NanoGL
