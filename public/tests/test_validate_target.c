#include <stdlib.h>
#include "build.h"
#include "buildenums.h"
#include "crtlib.h"

#ifndef VALIDATE_TARGET
#error
#endif

int main( void )
{
	string buf;

	if( Q_snprintf( buf, sizeof( buf ), "%s-%s", Q_buildos( ), Q_buildarch( )) < 0 )
		return 1;

	if( Q_strcmp( buf, VALIDATE_TARGET ) != 0 )
		return 2;

	return EXIT_SUCCESS;
}
