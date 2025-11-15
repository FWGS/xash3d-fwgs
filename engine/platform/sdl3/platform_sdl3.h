/*
platform_sdl3.h - SDL3 platform definitions
Copyright (C) 2025 Er2off
Copyright (C) 2025 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef PLATFORM_SDL3_H
#define PLATFORM_SDL3_H

#include <SDL3/SDL.h>
#include "platform/platform.h"

//
// in_sdl3.c
//
void SDLash_InitCursors( void );
void SDLash_FreeCursors( void );

#endif // PLATFORM_SDL3_H
