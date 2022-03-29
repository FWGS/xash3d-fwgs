/*
gu_extension.c - PSP Graphic Unit extensions
Copyright (C) 2020 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <pspkernel.h>
#include <pspge.h>
#include <pspgu.h>
#include <stdio.h>
#include "gu_extension.h"

/* from guInternal.h */
typedef struct
{
	unsigned int* start;
	unsigned int* current;
	int parent_context;
} GuDisplayList;
extern GuDisplayList* gu_list;
extern int gu_curr_context;
extern int ge_list_executed[];

static unsigned int gu_list_size;


void extGuStart(int cid, void* list, int size)
{
	gu_list_size = size;
	sceGuStart( cid, list );
}

/* Begin user packet */
void *extGuBeginPacket( unsigned int *maxsize )
{
	unsigned int* current_ptr = gu_list->current;
	if( maxsize != NULL )
	{
		unsigned int size = ( ( unsigned int )gu_list->current ) - ( ( unsigned int )gu_list->start );	
		*maxsize = ( ( unsigned int )gu_list_size ) - ( ( unsigned int )size );
	}
	return current_ptr + 2;
}

/* End user packet */
void extGuEndPacket( void *eaddr )
{
	unsigned int* current_ptr = gu_list->current;
	unsigned int size = ( ( unsigned int ) eaddr ) - ( ( unsigned int )current_ptr );
	
	if( size > 0 )
	{
		size += 3;
		size += ( ( unsigned int )( size >> 31 ) ) >> 30;
		size = ( size >> 2 ) << 2;
		
		unsigned int* new_ptr = ( unsigned int* )( ( ( unsigned int ) current_ptr ) + size + 8 );

		int lo = ( 8 << 24 ) | ( ( ( unsigned int )new_ptr ) & 0xffffff );
		int hi = ( 16 << 24 ) | ( ( ( ( unsigned int )new_ptr ) >> 8 ) & 0xf0000 );

		current_ptr[0] = hi;
		current_ptr[1] = lo;
		
		gu_list->current = new_ptr;
		
		if ( !gu_curr_context )
			sceGeListUpdateStallAddr( ge_list_executed[0], new_ptr );
	}
}