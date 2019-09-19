/*
android_lib.h - dynamic library code for Android OS
Copyright (C) 2018 Flying With Gauss

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
#ifndef ANDROID_LIB_H
#define ANDROID_LIB_H

void *ANDROID_LoadLibrary( const char *dllname );
void *ANDROID_GetProcAddress( void *hInstance, const char *name );

#endif // ANDROID_LIB_H
