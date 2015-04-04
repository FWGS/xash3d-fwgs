
# Uncomment this if you're using STL in your project
# See CPLUSPLUS-SUPPORT.html in the NDK documentation for more information
# APP_STL := stlport_static 

APPLICATIONMK_PATH = $(call my-dir)

SDL_PATH    := $(APPLICATIONMK_PATH)/src/SDL2

SDL_IMAGE_PATH := $(APPLICATIONMK_PATH)/src/SDL2_image/

NANOGL_PATH := $(APPLICATIONMK_PATH)/src/NanoGL/nanogl

XASH3D_PATH := $(APPLICATIONMK_PATH)/src/Xash3D/xash3d

XASHXT_PATH := $(APPLICATIONMK_PATH)/src/XashXT/XashXT

HLSDK_PATH  := $(APPLICATIONMK_PATH)/src/HLSDK/halflife/

APP_ABI := armeabi-v7a x86
APP_MODULES := SDL2 SDL2_image xash menu client server NanoGL
