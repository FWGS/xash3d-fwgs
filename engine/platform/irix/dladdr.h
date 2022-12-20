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
