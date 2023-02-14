#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined LIB || !defined FUNC
#error
#endif

typedef int (*FuzzFunc)(const char *Data, size_t Size);

void *handle = NULL;
FuzzFunc f = NULL;

int LLVMFuzzerTestOneInput( const char *Data, size_t Size )
{
	if( !handle )
		handle = dlopen( LIB, RTLD_NOW );

	if( handle )
	{
		if( !f )
			f = dlsym( handle, FUNC );

		if( f )
		{
			return f( Data, Size );
		}
	}

	fprintf( stderr, "Fail: %s\n", dlerror() );

	abort();
	return 0;
}
