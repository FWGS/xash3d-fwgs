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
#if !XASH_DEDICATED
	#if XASH_SDL
		// we are building using libSDL
		#ifndef XASH_VIDEO
			#define XASH_VIDEO VIDEO_SDL
		#endif // XASH_VIDEO

		#ifndef XASH_INPUT
			#define XASH_INPUT INPUT_SDL
		#endif // XASH_INPUT

		#ifndef XASH_SOUND
			#define XASH_SOUND SOUND_SDL
		#endif // XASH_SOUND

		#if XASH_SDL == 2
			#ifndef XASH_TIMER
				#define XASH_TIMER TIMER_SDL
			#endif // XASH_TIMER

			#ifndef XASH_MESSAGEBOX
				#if !XASH_NSWITCH // SDL2 messageboxes not available
					#define XASH_MESSAGEBOX MSGBOX_SDL
				#endif
			#endif // XASH_MESSAGEBOX
		#endif
	#elif XASH_LINUX
		// we are building for Linux without SDL2, can draw only to framebuffer yet
		#ifndef XASH_VIDEO
			#define XASH_VIDEO VIDEO_FBDEV
		#endif // XASH_VIDEO

		#ifndef XASH_INPUT
			#define XASH_INPUT INPUT_EVDEV
		#endif // XASH_INPUT

		#ifndef XASH_SOUND
			#define XASH_SOUND SOUND_ALSA
		#endif // XASH_SOUND

		#define XASH_USE_EVDEV 1
	#elif XASH_DOS4GW
		#ifndef XASH_VIDEO
			#define XASH_VIDEO VIDEO_DOS
		#endif
		#ifndef XASH_TIMER
			#define XASH_TIMER TIMER_DOS
		#endif

		// usually only 10-20 fds availiable
		#define XASH_REDUCE_FD
	#endif
#endif // XASH_DEDICATED

//
// select messagebox implementation
//
#ifndef XASH_MESSAGEBOX
	#if XASH_WIN32
		#define XASH_MESSAGEBOX MSGBOX_WIN32
	#elif XASH_NSWITCH
		#define XASH_MESSAGEBOX MSGBOX_NSWITCH
	#else // !XASH_WIN32
		#define XASH_MESSAGEBOX MSGBOX_STDERR
	#endif // !XASH_WIN32
#endif // XASH_MESSAGEBOX

//
// no timer - no xash
//
#ifndef XASH_TIMER
	#if XASH_WIN32
		#define XASH_TIMER TIMER_WIN32
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

#ifdef XASH_STATIC_LIBS
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
#if XASH_NSWITCH
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

#if XASH_IOS
	// this means that libraries are provided with engine, but not in game data
	// You need add library loading code to library.c when adding new platform
	#define XASH_INTERNAL_GAMELIBS
#endif // XASH_IOS

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
	#define DEFAULT_FULLSCREEN "1" // must be a string
#endif // DEFAULT_FULLSCREEN

#ifndef DEFAULT_MAX_EDICTS
	#define DEFAULT_MAX_EDICTS 1200 // was 900 before HL25
#endif // DEFAULT_MAX_EDICTS

#endif // DEFAULTS_H
