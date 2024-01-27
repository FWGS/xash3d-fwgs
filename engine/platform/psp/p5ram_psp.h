/*
p5ram_psp.h - PSP P5 memory allocator header
Copyright (C) 2022 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef P5RAM_PSP_H
#define P5RAM_PSP_H

//#define P5RAM_DEBUG

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

int P5Ram_Init( void );
void *P5Ram_Alloc( size_t size, int clear );
void P5Ram_Free( void *ptr );
void P5Ram_FreeAll( void );
void P5Ram_Shutdown( void );
void P5Ram_PowerCallback( int count, int arg, void *common );
#ifdef P5RAM_DEBUG
void P5Ram_Print( void );
#endif

#ifdef __cplusplus
}
#endif

#endif // P5RAM_PSP_H
