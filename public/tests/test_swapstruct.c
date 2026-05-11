#include <stddef.h>
#include "swaplib.h"

typedef struct
{
	uint16_t x;
	uint16_t y;
} inner_t;

typedef struct
{
	uint8_t  a;
	uint16_t b;
	uint32_t c;
	inner_t  inner;
} outer_t;

swap_struct_begin( inner_def )
	swap_struct_field( inner_t, x )
	swap_struct_field( inner_t, y )
swap_struct_end();

swap_struct_begin( outer_def )
	swap_struct_field( outer_t, b )
	swap_struct_field( outer_t, c )
	swap_struct_child( outer_t, inner, inner_def )
swap_struct_end();

typedef struct
{
	uint32_t version;
	inner_t  items[4];
} array_outer_t;

swap_struct_begin( array_outer_def )
	swap_struct_field( array_outer_t, version )
	swap_struct_array_child( array_outer_t, items, inner_def, 4 )
swap_struct_end();

typedef struct
{
	uint16_t tag;
	uint32_t values[3];
} int_array_t;

swap_struct_begin( int_array_def )
	swap_struct_field( int_array_t, tag )
	swap_struct_array( int_array_t, values, 3 )
swap_struct_end();

static int Test_SwapField( void )
{
	inner_t data = { .x = 0x0102, .y = 0x0304 };

	swap_struct( inner_def, &data );

	if( data.x != 0x0201 )
		return 1;

	if( data.y != 0x0403 )
		return 2;

	// swap again, should restore original
	swap_struct( inner_def, &data );

	if( data.x != 0x0102 )
		return 3;

	if( data.y != 0x0304 )
		return 4;

	return 0;
}

static int Test_SwapChild( void )
{
	outer_t data =
	{
		.a = 0xAA,
		.b = 0x0102,
		.c = 0x01020304,
		.inner = { .x = 0x0506, .y = 0x0708 },
	};

	swap_struct( outer_def, &data );

	// a is uint8_t, must be untouched
	if( data.a != 0xAA )
		return 1;

	// b must be byte-swapped
	if( data.b != 0x0201 )
		return 2;

	// c must be byte-swapped
	if( data.c != 0x04030201 )
		return 3;

	// child fields must be byte-swapped
	if( data.inner.x != 0x0605 )
		return 4;

	if( data.inner.y != 0x0807 )
		return 5;

	// swap back
	swap_struct( outer_def, &data );

	if( data.b != 0x0102 )
		return 6;

	if( data.c != 0x01020304 )
		return 7;

	if( data.inner.x != 0x0506 )
		return 8;

	if( data.inner.y != 0x0708 )
		return 9;

	return 0;
}

static int Test_SwapArrayChild( void )
{
	array_outer_t data =
	{
		.version = 0x01020304,
		.items =
		{
			{ .x = 0x0102, .y = 0x0304 },
			{ .x = 0x0506, .y = 0x0708 },
			{ .x = 0x090A, .y = 0x0B0C },
			{ .x = 0x0D0E, .y = 0x0F10 },
		},
	};

	swap_struct( array_outer_def, &data );

	if( data.version != 0x04030201 )
		return 1;

	if( data.items[0].x != 0x0201 || data.items[0].y != 0x0403 )
		return 2;

	if( data.items[1].x != 0x0605 || data.items[1].y != 0x0807 )
		return 3;

	if( data.items[2].x != 0x0A09 || data.items[2].y != 0x0C0B )
		return 4;

	if( data.items[3].x != 0x0E0D || data.items[3].y != 0x100F )
		return 5;

	// swap back
	swap_struct( array_outer_def, &data );

	if( data.version != 0x01020304 )
		return 6;

	if( data.items[0].x != 0x0102 || data.items[0].y != 0x0304 )
		return 7;

	if( data.items[3].x != 0x0D0E || data.items[3].y != 0x0F10 )
		return 8;

	return 0;
}

static int Test_SwapArray( void )
{
	int_array_t data =
	{
		.tag = 0x0102,
		.values = { 0x01020304, 0x05060708, 0x090A0B0C },
	};

	swap_struct( int_array_def, &data );

	if( data.tag != 0x0201 )
		return 1;

	if( data.values[0] != 0x04030201 )
		return 2;

	if( data.values[1] != 0x08070605 )
		return 3;

	if( data.values[2] != 0x0C0B0A09 )
		return 4;

	// swap back
	swap_struct( int_array_def, &data );

	if( data.tag != 0x0102 )
		return 5;

	if( data.values[0] != 0x01020304 )
		return 6;

	if( data.values[1] != 0x05060708 )
		return 7;

	if( data.values[2] != 0x090A0B0C )
		return 8;

	return 0;
}

int main( void )
{
	int ret;

	ret = Test_SwapField();

	if( ret > 0 )
		return ret;

	ret = Test_SwapChild();

	if( ret > 0 )
		return ret + 32;

	ret = Test_SwapArrayChild();

	if( ret > 0 )
		return ret + 64;

	ret = Test_SwapArray();

	if( ret > 0 )
		return ret + 96;

	return 0;
}
