#include "xash3d_mathlib.h"

static int Test_RoundUpHullSize( void )
{
	struct {
		vec3_t mins, maxs;
		vec3_t result_mins, result_maxs;
	} test_data[] = {
		// zero stays zero
		{ { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
		// exact table values and exact integers aren't bumped to the next entry
		{ { -16, -60, -4 }, { 16, 60, 4 }, { -16, -60, -4 }, { 16, 60, 4 } },
		// fractions grow outwards to the next table entry
		{ { -15.3f, -3.39f, -17 }, { 15.3f, 21.8333f, 19 }, { -16, -4, -18 }, { 16, 24, 24 } },
		// beyond the table the magnitude is rounded outwards
		{ { -771.86f, -424.11f, -200 }, { 354.03f, 179.945f, 200 }, { -772, -425, -200 }, { 355, 180, 200 } },
		// bounds on the inner side of the origin shrink towards it
		{ { 1.12f, 2.73f, 20.5f }, { -1.5f, -20.5f, -2 }, { 0, 2, 18 }, { 0, -18, -2 } },
		{ { 300.7f, 0, 0 }, { -300.7f, 0, 0 }, { 300, 0, 0 }, { -300, 0, 0 } },
	};

	for( int i = 0; i < sizeof( test_data ) / sizeof( test_data[0] ); i++ )
	{
		vec3_t mins, maxs;

		VectorCopy( test_data[i].mins, mins );
		VectorCopy( test_data[i].maxs, maxs );
		RoundUpHullSize( mins, maxs );

		if( !VectorCompare( mins, test_data[i].result_mins ) || !VectorCompare( maxs, test_data[i].result_maxs ))
			return i + 1;
	}

	return 0;
}

int main( void )
{
	return Test_RoundUpHullSize();
}
