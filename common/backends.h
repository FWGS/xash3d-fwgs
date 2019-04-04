/*
backends.h - backend macro definations
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


#ifndef BACKENDS_H
#define BACKENDS_H

// video backends (XASH_VIDEO)
#define VIDEO_NULL 0
#define VIDEO_SDL 1
#define VIDEO_ANDROID 2

// audio backends (XASH_SOUND)
#define SOUND_NULL 0
#define SOUND_SDL 1
#define SOUND_OPENSLES 2

// crash handler (XASH_CRASHHANDLER)
#define CRASHHANDLER_NULL 0
#define CRASHHANDLER_UCONTEXT 1
#define CRASHHANDLER_DBGHELP 2
#define CRASHHANDLER_WIN32 3

// input (XASH_INPUT)
#define INPUT_NULL 0
#define INPUT_SDL 1
#define INPUT_ANDROID 2

// timer (XASH_TIMER)
#define TIMER_NULL 0 // not used
#define TIMER_SDL 1
#define TIMER_LINUX 2
#define TIMER_WIN32 3

#endif /* BACKENDS_H */
