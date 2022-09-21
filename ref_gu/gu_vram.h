/*
 * Helper for use with the PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed as 'free to use and modify as long as credited appropriately'
 *
 * vram.h - Standard C high performance VRAM allocation routines.
 *
 * Copyright (c) 2007 Alexander Berl 'Raphael' <raphael@fx-world.org>
 * http://wordpress.fx-world.org
 */
#ifndef vram_h__
#define vram_h__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int vinit( void );

void* vrelptr( void *ptr );		// make a pointer relative to memory base address (ATTENTION: A NULL rel ptr is not illegal/invalid!)
void* vabsptr( void *ptr );		// make a pointer absolute (default return type of valloc)

void* valloc( size_t size );
void vfree( void* ptr );
size_t vmemavail();
size_t vlargestblock();
int vchkptr( void *ptr );


#ifdef _DEBUG
// Debug printf (to stdout) a trace of the current Memblocks
void __memwalk();
#endif


#ifdef __cplusplus
}
#endif

#endif  // ifdef vram_h__
