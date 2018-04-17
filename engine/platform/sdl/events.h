/*
events.h - SDL event system handlers
Copyright (C) 2015-2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#pragma once
#ifndef KEYWRAPPER_H
#define KEYWRAPPER_H

#ifdef XASH_SDL

void SDLash_RunEvents( void );
void SDLash_EnableTextInput( int enable, qboolean force );
int SDLash_JoyInit( int numjoy ); // pass -1 to init every joystick

#endif // XASH_SDL
#endif // KEYWRAPPER_H
