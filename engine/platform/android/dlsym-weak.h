/*
dlsym-weak.h -- custom dlsym() function to override bionic libc bug on Android <5.0
Copyright (C) 2015-2017 Flying With Gauss

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef DLSYM_WEAH_H
#define DLSYM_WEAK_H

// ------------ dlsym-weak.cpp ------------ //
void* dlsym_weak(void* handle, const char* symbol);

#endif
