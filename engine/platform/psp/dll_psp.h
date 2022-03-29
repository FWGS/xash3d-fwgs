#ifndef DLL_PSP_H
#define DLL_PSP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#define RTLD_LAZY 0x0001
#define RTLD_NOW  0x0002

typedef struct dllexport_s
{
	const char *name;
	void *func;
} dllexport_t;

typedef struct Dl_info_s
{
	void *dli_fhandle;
	const char *dli_sname;
	const void *dli_saddr;
} Dl_info;

void *dlsym(void *handle, const char *symbol );
void *dlopen(const char *name, int flag );
int dlclose(void *handle);
char *dlerror( void );
int dladdr( const void *addr, Dl_info *info );

#ifdef __cplusplus
}
#endif

#endif