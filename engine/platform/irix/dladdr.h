/*
dladdr.h - dladdr prototypes for SGI IRIX
Copyright (C) 2022 Xav101
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/* See engine/platform/irix/dladdr.c for the requirement for this implementation */

#ifndef DLADDR_IRIX_H
#define DLADDR_IRIX_H

#include <rld_interface.h>
#ifndef _RLD_INTERFACE_DLFCN_H_DLADDR
#define _RLD_INTERFACE_DLFCN_H_DLADDR
typedef struct Dl_info {
	const char * dli_fname;
	void       * dli_fbase;
	const char * dli_saddr;
	int          dli_version;
	int          dli_reserved1;
	long         dli_reserved[4];
} Dl_info;
#endif
#define _RLD_DLADDR 14

int dladdr(void *address, Dl_info* dl);

#endif
