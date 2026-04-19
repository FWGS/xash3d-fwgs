#include "atlas.h"

static int Test_BasicAlloc( void )
{
	atlas_t atlas;
	int x, y;

	Atlas_Init( &atlas, 128 );

	if( !Atlas_AllocBlock( &atlas, 16, 16, &x, &y ))
		return 1;

	if( x != 0 || y != 0 )
		return 2;

	if( atlas.max_height != 16 )
		return 3;

	if( !Atlas_AllocBlock( &atlas, 16, 16, &x, &y ))
		return 4;

	if( x != 16 || y != 0 )
		return 5;

	return 0;
}

static int Test_Full( void )
{
	atlas_t atlas;
	int x, y;

	Atlas_Init( &atlas, 64 );

	if( !Atlas_AllocBlock( &atlas, 64, 64, &x, &y ))
		return 1;

	if( Atlas_AllocBlock( &atlas, 1, 1, &x, &y ))
		return 2;

	return 0;
}

static int Test_Packing( void )
{
	atlas_t atlas;
	int x, y;

	Atlas_Init( &atlas, 64 );

	if( !Atlas_AllocBlock( &atlas, 32, 16, &x, &y ))
		return 1;

	if( !Atlas_AllocBlock( &atlas, 32, 16, &x, &y ))
		return 2;

	if( x != 32 || y != 0 )
		return 3;

	if( !Atlas_AllocBlock( &atlas, 64, 16, &x, &y ))
		return 4;

	if( y != 16 )
		return 5;

	return 0;
}

static int Test_Reset( void )
{
	atlas_t atlas;
	int x, y;

	Atlas_Init( &atlas, 64 );

	if( !Atlas_AllocBlock( &atlas, 64, 64, &x, &y ))
		return 1;

	Atlas_Init( &atlas, 64 );

	if( !Atlas_AllocBlock( &atlas, 64, 64, &x, &y ))
		return 2;

	if( x != 0 || y != 0 )
		return 3;

	return 0;
}

static int Test_TooLarge( void )
{
	atlas_t atlas;
	int x, y;

	Atlas_Init( &atlas, 64 );

	if( Atlas_AllocBlock( &atlas, 65, 1, &x, &y ))
		return 1;

	if( Atlas_AllocBlock( &atlas, 1, 65, &x, &y ))
		return 2;

	return 0;
}

int main( void )
{
	int ret;

	ret = Test_BasicAlloc();
	if( ret > 0 )
		return ret;

	ret = Test_Full();
	if( ret > 0 )
		return ret + 16;

	ret = Test_Packing();
	if( ret > 0 )
		return ret + 32;

	ret = Test_Reset();
	if( ret > 0 )
		return ret + 48;

	ret = Test_TooLarge();
	if( ret > 0 )
		return ret + 64;

	return 0;
}
