/*
defaults.h - set up default configuration
Copyright (C) 2016 Mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef DEFAULTS_H
#define DEFAULTS_H

#include "backends.h"
#include "build.h"

/*
===================================================================

SETUP BACKENDS DEFINITIONS

===================================================================
*/
//
// when compiling client, we need to pick video, audio and input implementations
//
#if !XASH_DEDICATED // when compiling client, we need to pick video, audio and input implementations
	#if XASH_SDL // we are building with SDL
		#define XASH_VIDEO     VIDEO_SDL
		#define XASH_INPUT     INPUT_SDL
		#define XASH_SOUND     SOUND_SDL
	#elif XASH_LINUX // we are building for Linux without SDL, only framebuffer is supported for now
		#define XASH_VIDEO     VIDEO_FBDEV
		#define XASH_INPUT     INPUT_EVDEV
		#define XASH_SOUND     SOUND_ALSA
		#define XASH_USE_EVDEV 1
	#elif XASH_DOS4GW
		#define XASH_VIDEO     VIDEO_DOS
		#define XASH_REDUCE_FD 1 // usually only 10-20 fds available
	#elif XASH_PSP
		#define XASH_VIDEO     VIDEO_PSP
		#define XASH_INPUT     INPUT_PSP
		#define XASH_SOUND     SOUND_PSP
		#define XASH_REDUCE_FD 1
		#define XASH_NO_TOUCH  1
		#define XASH_NO_ZIP    1
	#endif
#endif // !XASH_DEDICATED

//
// select messagebox implementation
//
#ifndef XASH_MESSAGEBOX
	#if XASH_SDL >= 2 && !XASH_NSWITCH // SDL2 messageboxes are not available on NSW
		#define XASH_MESSAGEBOX MSGBOX_SDL
	#elif XASH_WIN32
		#define XASH_MESSAGEBOX MSGBOX_WIN32
	#elif XASH_NSWITCH
		#define XASH_MESSAGEBOX MSGBOX_NSWITCH
	#elif XASH_PSP
		#define XASH_MESSAGEBOX MSGBOX_PSP
	#else // !XASH_WIN32
		#define XASH_MESSAGEBOX MSGBOX_STDERR
	#endif // !XASH_WIN32
#endif // XASH_MESSAGEBOX

//
// no timer - no xash
//
#ifndef XASH_TIMER
	#if XASH_SDL >= 2
		#define XASH_TIMER TIMER_SDL
	#elif XASH_WIN32
		#define XASH_TIMER TIMER_WIN32
	#elif XASH_DOS4GW
		#define XASH_TIMER TIMER_DOS
	#elif XASH_PSP
		#define XASH_TIMER TIMER_PSP
	#else // !XASH_WIN32
		#define XASH_TIMER TIMER_POSIX
	#endif // !XASH_WIN32
#endif

//
// determine movie playback backend
//
#ifndef XASH_AVI
	#if HAVE_FFMPEG
		#define XASH_AVI AVI_FFMPEG
	#else
		#define XASH_AVI AVI_NULL
	#endif
#endif

#if XASH_PSP
	#define XASH_PSP LIB_PSP
#elif defined( XASH_STATIC_LIBS )
	#define XASH_LIB LIB_STATIC
	#define XASH_INTERNAL_GAMELIBS
	#define XASH_ALLOW_SAVERESTORE_OFFSETS
#elif XASH_WIN32
	#define XASH_LIB LIB_WIN32
#elif XASH_POSIX
	#define XASH_LIB LIB_POSIX
#endif

//
// fallback to NULL
//
#ifndef XASH_VIDEO
	#define XASH_VIDEO VIDEO_NULL
#endif // XASH_VIDEO

#ifndef XASH_SOUND
	#define XASH_SOUND SOUND_NULL
#endif // XASH_SOUND

#ifndef XASH_INPUT
	#define XASH_INPUT INPUT_NULL
#endif // XASH_INPUT

/*
=========================================================================

Default build-depended cvar and constant values

=========================================================================
*/

// Platform overrides
#if XASH_WIN32
	// set up windowed by default on Windows to avoid problems with
	// Xbox Game Bar
	#define DEFAULT_FULLSCREEN   "0"
#elif XASH_NSWITCH
	#define DEFAULT_TOUCH_ENABLE "1"
	#define DEFAULT_M_IGNORE     "1"
	#define DEFAULT_MODE_WIDTH   1280
	#define DEFAULT_MODE_HEIGHT  720
	#define DEFAULT_ALLOWCONSOLE 1
#elif XASH_PSVITA
	#define DEFAULT_TOUCH_ENABLE "1"
	#define DEFAULT_M_IGNORE     "1"
	#define DEFAULT_MODE_WIDTH   960
	#define DEFAULT_MODE_HEIGHT  544
	#define DEFAULT_ALLOWCONSOLE 1
#elif XASH_ANDROID
	#define DEFAULT_TOUCH_ENABLE "1"
#elif XASH_MOBILE_PLATFORM
	#define DEFAULT_TOUCH_ENABLE "1"
	#define DEFAULT_M_IGNORE     "1"
#endif // !XASH_MOBILE_PLATFORM && !XASH_NSWITCH

// Defaults
#ifndef DEFAULT_TOUCH_ENABLE
	#define DEFAULT_TOUCH_ENABLE "0"
#endif // DEFAULT_TOUCH_ENABLE

#ifndef DEFAULT_M_IGNORE
	#define DEFAULT_M_IGNORE "0"
#endif // DEFAULT_M_IGNORE

#ifndef DEFAULT_JOY_DEADZONE
	#define DEFAULT_JOY_DEADZONE "4096"
#endif // DEFAULT_JOY_DEADZONE

#ifndef DEFAULT_DEV
	#define DEFAULT_DEV 0
#endif // DEFAULT_DEV

#ifndef DEFAULT_ALLOWCONSOLE
	#define DEFAULT_ALLOWCONSOLE 0
#endif // DEFAULT_ALLOWCONSOLE

#ifndef DEFAULT_FULLSCREEN
	#define DEFAULT_FULLSCREEN "2" // must be a string
#endif // DEFAULT_FULLSCREEN

#ifndef DEFAULT_MAX_EDICTS
	#define DEFAULT_MAX_EDICTS 1200 // was 900 before HL25
#endif // DEFAULT_MAX_EDICTS


#ifndef DEFAULT_ACCELERATED_RENDERER
	#if XASH_PSP
		#define DEFAULT_ACCELERATED_RENDERER "gu"
	#elif XASH_MOBILE_PLATFORM
		#define DEFAULT_ACCELERATED_RENDERER "gles1"
	#elif XASH_IOS
		#define DEFAULT_ACCELERATED_RENDERER "gles2"
	#else // !XASH_MOBILE_PLATFORM
		#define DEFAULT_ACCELERATED_RENDERER "gl"
	#endif // !XASH_MOBILE_PLATFORM
#endif // DEFAULT_ACCELERATED_RENDERER

#ifndef DEFAULT_SOFTWARE_RENDERER
	#define DEFAULT_SOFTWARE_RENDERER "soft" // mittorn's ref_soft
#endif // DEFAULT_SOFTWARE_RENDERER

#endif // DEFAULTS_H
