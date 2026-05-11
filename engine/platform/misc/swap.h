/*
sbrk_swap.h - swap memory allocation
Copyright (C) 2019 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
//#include <stdint.h>
void *SWAP_Sbrk( size_t size );
void *SWAP_Malloc( size_t size );
void *SWAP_Calloc( size_t nelem, size_t size );
void SWAP_Free( void *cp );
void *SWAP_Realloc( void *cp, size_t size );
size_t SWAP_MallocUsableSize( void * cp );
