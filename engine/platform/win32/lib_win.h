/*
lib_win.h - common win32 dll definitions
Copyright (C) 2022 Flying With Gauss

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "library.h"
#include <winnt.h>
#include <psapi.h>
#include STDINT_H

#define CALCULATE_ADDRESS( base, offset ) ((uint8_t *)( base ) + (uintptr_t)( offset ))

FARPROC MemoryGetProcAddress( void *module, const char *name );
void MemoryFreeLibrary( void *hInstance );
void *MemoryLoadLibrary( const char *name );
