/*
em_lib.h - dynamic library code for iOS
Copyright (C) 2017-2018 mittorn

This program is free software: you can redistribute it and/sor modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#ifdef __EMSCRIPTEN__
#ifndef EM_LIB_H
#define EM_LIB_H

void *EMSCRIPTEN_LoadLibrary( const char *dllname );

#endif // EM_LIB_H
#endif // __EMSCRIPTEN__
