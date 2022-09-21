/*
p5ram_psp.c - PSP P5 memory allocator
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
#include <pspkernel.h>
#include <psppower.h>
#include <pspsuspend.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p5ram_psp.h"

#define P5RAM_ALIGN( x, a )	((( x ) + (( typeof( x ))( a ) - 1 )) & ~( typeof( x )( a ) - 1 ))

#define P5RAM_CHECK( addr )	((( unsigned int )addr >= ( unsigned int )p5ram_addr) && (( unsigned int )addr < ( unsigned int )p5ram_addr + p5ram_size))

#define P5RAM_FLAG_INIT		0x01
#define P5RAM_FLAG_SUSPENDED	0x02

typedef struct __attribute__(( aligned( 64 ))) p5ram_info_s
{
	SceUID			uid;
	size_t			size;
	struct p5ram_info_s	*next;
}p5ram_info_t;

static unsigned char	p5ram_flags = 0x00;
static p5ram_info_t	*p5ram_poolchain = NULL;
static void 		*p5ram_addr;
static unsigned int	p5ram_size;

int P5Ram_Init( void )
{
	int		result;

	if( p5ram_flags & P5RAM_FLAG_INIT )
		return -1;

	result = sceKernelVolatileMemLock( 0, &p5ram_addr, &p5ram_size );
	if( result == 0 )
		p5ram_flags |= P5RAM_FLAG_INIT;

	return result;
}

void *P5Ram_Alloc( size_t size, int clear )
{
	SceUID		uid;
	void		*ptr;

	if(!( p5ram_flags & P5RAM_FLAG_INIT ))
		return NULL;

	uid = sceKernelAllocPartitionMemory( 5, "USER P5", PSP_SMEM_Low, size + sizeof( p5ram_info_t ), NULL );
	if( uid < 0 ) return NULL;

	ptr = sceKernelGetBlockHeadAddr( uid );
	(( p5ram_info_t* )ptr )->uid  = uid;
	(( p5ram_info_t* )ptr )->size = size;
	(( p5ram_info_t* )ptr )->next = p5ram_poolchain;
	if( clear ) memset(( unsigned char* )ptr + sizeof( p5ram_info_t ), 0, size );

	p5ram_poolchain = ptr;

	return ( void* )(( unsigned char* )ptr + sizeof( p5ram_info_t ));
}

void P5Ram_Free( void *ptr )
{
	p5ram_info_t *info, *next_ptr;

	if( !( p5ram_flags & P5RAM_FLAG_INIT ) || ptr == NULL ) return;

	info = ( p5ram_info_t* )((unsigned char*)ptr - sizeof( p5ram_info_t ));

	if( info == p5ram_poolchain )
	{
		p5ram_poolchain = p5ram_poolchain->next;
	}
	else
	{
		next_ptr = p5ram_poolchain;
		while( next_ptr )
		{
			if( next_ptr->next == info )
			{
				next_ptr->next = info->next;
				break;
			}
			next_ptr = next_ptr->next;
		}
	}

	sceKernelFreePartitionMemory( info->uid );
}

void P5Ram_FreeAll( void )
{
	p5ram_info_t *next_ptr;

	while( p5ram_poolchain )
	{
		next_ptr = p5ram_poolchain->next;
		sceKernelFreePartitionMemory( p5ram_poolchain->uid );
		p5ram_poolchain = next_ptr;
	}
}

void P5Ram_Shutdown( void )
{
	if( !( p5ram_flags & P5RAM_FLAG_INIT ))
		return;

	P5Ram_FreeAll();
	sceKernelVolatileMemUnlock( 0 );

	p5ram_flags &= ~P5RAM_FLAG_INIT;
}

void P5Ram_PowerCallback( int count, int arg, void *common )
{
	if( !( p5ram_flags & ( P5RAM_FLAG_INIT | P5RAM_FLAG_SUSPENDED )))
		return;

	if ( arg & PSP_POWER_CB_POWER_SWITCH || arg & PSP_POWER_CB_SUSPENDING )
	{
		P5Ram_Shutdown();
		p5ram_flags |= P5RAM_FLAG_SUSPENDED;
	}
	else if ( arg & PSP_POWER_CB_RESUMING )
	{
	}
	else if ( arg & PSP_POWER_CB_RESUME_COMPLETE )
	{
		P5Ram_Init();
		p5ram_flags &= ~P5RAM_FLAG_SUSPENDED;
	}
}

#ifdef P5RAM_DEBUG
void P5Ram_Print( void )
{
	p5ram_info_t *next_ptr;

	printf("+++++++++++++++++++++++++++++\n");
	next_ptr = p5ram_poolchain;
	while( next_ptr )
	{
		printf("P5 ALLOC [ 0x%08X 0x%08X 0x%08X 0x%08X ]\n", next_ptr, next_ptr->uid, next_ptr->size, next_ptr->next );
		next_ptr = next_ptr->next;
	}
	printf("+++++++++++++++++++++++++++++\n");
}
#endif
