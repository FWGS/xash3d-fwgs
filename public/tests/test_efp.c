#include <stdlib.h>
#include "crtlib.h"
#include <stdio.h>

static int Test_ExtractFilePath( void )
{
	char dst[64];
	const char *strings[] =
	{
		"dir/file", "dir",
		"bark\\meow", "bark",
		"nopath", "",
		"knee/deep/in/paths", "knee/deep/in",
		// yes, it removes the behavior/ even if it might be technically a directory
		"keep/the/original/func/behavior/", "keep/the/original/func",
		"backslashes\\are\\annoying\\af", "backslashes\\are\\annoying",
		"", ""
	};
	size_t i;

	for( i = 0; i < sizeof( strings ) / sizeof( strings[0] ); i += 2 )
	{
		COM_ExtractFilePath( strings[i], dst );
		if( Q_strcmp( dst, strings[i+1] ))
		{
			printf( "%s %s %s\n", strings[i], strings[i+1], dst );
			return (i >> 1) + 1;
		}
	}

	return 0;
}

int main( void )
{
	if( Test_ExtractFilePath( ))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
