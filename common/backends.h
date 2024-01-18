/*
backends.h - backend macro definitions
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
#define VIDEO_NULL    0
#define VIDEO_SDL     1
#define VIDEO_FBDEV   3
#define VIDEO_DOS     4
#define VIDEO_PSP     5

// audio backends (XASH_SOUND)
#define SOUND_NULL     0
#define SOUND_SDL      1
#define SOUND_ALSA     3
#define SOUND_PSP      4

// input (XASH_INPUT)
#define INPUT_NULL    0
#define INPUT_SDL     1
#define INPUT_EVDEV   3
#define INPUT_PSP     4

// timer (XASH_TIMER)
#define TIMER_NULL  0 // not used
#define TIMER_SDL   1
#define TIMER_POSIX 2
#define TIMER_WIN32 3
#define TIMER_DOS   4
#define TIMER_PSP   5

// messageboxes (XASH_MESSAGEBOX)
#define MSGBOX_STDERR  0
#define MSGBOX_SDL     1
#define MSGBOX_WIN32   3
#define MSGBOX_NSWITCH 4
#define MSGBOX_PSP     5

// library loading (XASH_LIB)
#define LIB_NULL   0
#define LIB_POSIX  1
#define LIB_WIN32  2
#define LIB_STATIC 3
#define LIB_PSP    4

#endif /* BACKENDS_H */
