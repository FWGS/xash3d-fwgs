#include "xash3d_mathlib.h"

int main( void )
{
	if( Q_bsr( 0 ) != 32 )
		return 1;

	if( Q_bsr( 1 ) != 0 )
		return 1;

	if( Q_bsr( 2 ) != 1 )
		return 1;

	if( Q_bsr( 3 ) != 1 )
		return 1;

	if( Q_bsr( 4 ) != 2 )
		return 1;

	if( Q_bsr( 16 ) != 4 )
		return 1;

	if( Q_bsr( 17 ) != 4 )
		return 1;

	if( Q_bsr( 0xFFFFFFFFU ) != 31 )
		return 1;

	return 0;
}
