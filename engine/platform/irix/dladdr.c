/*
 * From SGI IRIX's 'man dladdr'
 *
 * <dlfcn.h> does not contain a prototype for dladdr or definition of
 * Dl_info. The #include <dlfcn.h> in the SYNOPSIS line is traditional,
 * but contains no dladdr prototype and no IRIX library contains an
 * implementation. Write your own declaration based on the code below.
 *
 * The following code is dependent on internal interfaces that are not
 * part of the IRIX compatibility guarantee; however, there is no future
 * intention to change this interface, so on a practical level, the code
 * below is safe to use on IRIX.
 *
 * 
 *
 * The following code has been reproduced from the manpage.
 */ 

#include "dladdr.h"

int dladdr(void *address, Dl_info* dl)
{
	void *v;
	v = _rld_new_interface(_RLD_DLADDR, address, dl);
	return (int)v;
}
