/*
sbrk.c - swap memory allocation
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
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#define _GNU_SOURCE
#include <dlfcn.h>
#include "platform/swap/swap.h"
#include "string.h"

#ifndef XASH_DEFAULT_SWAP_PATH
#define XASH_DEFAULT_SWAP_PATH  "/tmp/xash3d-swap"
#endif
#define PAGE_SIZE 4096
static struct sbrk_state_s
{
	void *top;
	int fd;
	size_t size;
	size_t prealloc;
	pid_t pid;
} s;

static void SWAP_Initialize(void)
{
	char *path;
	char *prealloc = getenv("SWAP_SIZE");
	int fd;

	if( s.top )
		return;

	path = getenv("SWAP_PATH");
	if( !path )
		path = XASH_DEFAULT_SWAP_PATH;
	fd = open( path, O_CREAT|O_RDWR, 0600 );

	if( prealloc ) s.prealloc = atoi(prealloc);
	else s.prealloc = 128*1024*1024;
	s.prealloc &= ~(PAGE_SIZE - 1);

	s.fd = fd;
	ftruncate( fd, s.prealloc );
	s.top = mmap( 0, s.prealloc, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0 );

	// space will be freed on exit
	//unlink(path);
}

void *SWAP_Sbrk(size_t size)
{
	char buf[64];
	SWAP_Initialize();

	if( size == 0 )
		return s.top;
	else if( size > 0 )
	{
		void *res;

		//write(1, buf, snprintf(buf, 32, "allocating %d\n", size) );
		res = s.top;
		s.size += size;
		s.top = res + size;
		if( s.size + size > s.prealloc )
			return (void*)-1;

		memset( res, 0, size );
		return res;

	}
	else
	{
		void *res = s.top;

		if( -size > s.size )
			res = (void*)-1;
		else
		{
			s.top += size;
			s.size += size;
			//write(1, buf, snprintf(buf, 32, "freed %d\n", -size) );
		}

		return res;
	}
}


