/*
swaplib.h - byte-swapping for on-disk structures
Copyright (C) 2026 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef SWAPLIB_H
#define SWAPLIB_H

#include "build.h"
#include "xash3d_types.h"

typedef struct swap_struct_def_s
{
	uint32_t offset;
	int32_t size;
	struct swap_struct_def_s *subdef;
	uint32_t count;
	uint32_t stride;
} swap_struct_def_t;

static inline void swap_two_bytes_( byte *a, byte *b )
{
	byte tmp = *a;
	*a = *b;
	*b = tmp;
}

static inline void swap_field_( byte *p, int32_t size )
{
	switch( size )
	{
	case 2:
		swap_two_bytes_( &p[0], &p[1] );
		break;
	case 4:
		swap_two_bytes_( &p[0], &p[3] );
		swap_two_bytes_( &p[1], &p[2] );
		break;
	case 8:
		swap_two_bytes_( &p[0], &p[7] );
		swap_two_bytes_( &p[1], &p[6] );
		swap_two_bytes_( &p[2], &p[5] );
		swap_two_bytes_( &p[3], &p[4] );
		break;
	default:
		break;
	}
}

static inline void swap_struct_( const swap_struct_def_t *def, size_t len, byte *data )
{
	for( size_t i = 0; i < len; i++ )
	{
		if( def[i].size < 0 )
		{
			uint32_t count = def[i].count ? def[i].count : 1;
			for( uint32_t j = 0; j < count; j++ )
				swap_struct_( def[i].subdef, -def[i].size, &data[def[i].offset + j * def[i].stride] );
			continue;
		}

		if( def[i].count > 0 )
		{
			for( uint32_t j = 0; j < def[i].count; j++ )
				swap_field_( &data[def[i].offset + j * def[i].stride], def[i].size );
			continue;
		}

		swap_field_( &data[def[i].offset], def[i].size );
	}
}

#define swap_struct( swapdef, ptr ) swap_struct_(( swapdef ), sizeof( swapdef ) / sizeof( swapdef[0] ), (byte *)( ptr ))

#define swap_struct_begin( swapdef )              static swap_struct_def_t swapdef[] = {
#define swap_struct_field( record, field )         { .offset = offsetof( record, field ), .size = sizeof(((record *)0)->field) },
#define swap_struct_child( record, field, child )  { .offset = offsetof( record, field ), .size = -(int32_t)(sizeof( child ) / sizeof( child[0] )), .subdef = child },
#define swap_struct_array( record, field, cnt )             { .offset = offsetof( record, field ), .size = sizeof(((record *)0)->field[0]), .count = cnt, .stride = sizeof(((record *)0)->field[0]) },
#define swap_struct_array_child( record, field, child, cnt ) { .offset = offsetof( record, field ), .size = -(int32_t)(sizeof( child ) / sizeof( child[0] )), .subdef = child, .count = cnt, .stride = sizeof(((record *)0)->field[0]) },
#define swap_struct_end()                          }

// this is done in macros so we can completely avoid defining this
// in little endian targets
#ifdef XASH_LITTLE_ENDIAN
	#define be_struct_begin( x )              swap_struct_begin( x )
	#define be_struct_field( x, y )           swap_struct_field( x, y )
	#define be_struct_child( x, y, z )        swap_struct_child( x, y, z )
	#define be_struct_array( x, y, z )        swap_struct_array( x, y, z )
	#define be_struct_array_child( x, y, z, w ) swap_struct_array_child( x, y, z, w )
	#define be_struct_end()                   swap_struct_end()
	#define be_struct_swap( x, y )            swap_struct( x, y )
	#define le_struct_begin( x )
	#define le_struct_field( x, y )
	#define le_struct_child( x, y, z )
	#define le_struct_array( x, y, z )
	#define le_struct_array_child( x, y, z, w )
	#define le_struct_end()
	#define le_struct_swap( x, y )            (void)(y)
#else
	#define le_struct_begin( x )              swap_struct_begin( x )
	#define le_struct_field( x, y )           swap_struct_field( x, y )
	#define le_struct_child( x, y, z )        swap_struct_child( x, y, z )
	#define le_struct_array( x, y, z )        swap_struct_array( x, y, z )
	#define le_struct_array_child( x, y, z, w ) swap_struct_array_child( x, y, z, w )
	#define le_struct_end()                   swap_struct_end()
	#define le_struct_swap( x, y )            swap_struct( x, y )
	#define be_struct_begin( x )
	#define be_struct_field( x, y )
	#define be_struct_child( x, y, z )
	#define be_struct_array( x, y, z )
	#define be_struct_array_child( x, y, z, w )
	#define be_struct_end()
	#define be_struct_swap( x, y )            (void)(y)
#endif


#endif // SWAPLIB_H
