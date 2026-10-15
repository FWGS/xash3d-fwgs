/*
dlfcn_ps4.h - dlfcn-like interface over PS4 kernel module loader
Copyright (C) 2026 Xash3D FWGS contributors

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
#ifndef DLFCN_PS4_H
#define DLFCN_PS4_H

#include <dlfcn.h> // RTLD_* and Dl_info

void *PS4_dlopen( const char *name, int flags );
void *PS4_dlsym( void *handle, const char *symbol );
int PS4_dlclose( void *handle );
char *PS4_dlerror( void );
int PS4_dladdr( const void *addr, Dl_info *info );

// libkernel exports its own dl* functions, don't let them be used by accident
#undef dlopen
#undef dlsym
#undef dlclose
#undef dlerror
#undef dladdr
#define dlopen  PS4_dlopen
#define dlsym   PS4_dlsym
#define dlclose PS4_dlclose
#define dlerror PS4_dlerror
#define dladdr  PS4_dladdr

#endif // DLFCN_PS4_H
