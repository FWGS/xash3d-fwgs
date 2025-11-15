#include <stdlib.h>
#include "crtlib.h"

int main( void )
{
	int res;

	if( Q_buildnum_compat() != 4529 )
		return 200;

	if( Q_buildnum_iso( "2015-04-02 21:19:10 +0300" ) != 1 )
		return 202;

	if( Q_buildnum_iso( "2023-04-17 21:19:10 +0300" ) != 2938 )
		return 204;

	return EXIT_SUCCESS;
}
