#include "unittest.h"
#include <math.h>

TEST_FIRST(helloworld)
{
}

TEST3(hw2, helloworld, "Hello, World")
{
}

TEST(nonsense, hw2)
{
	if( !( sin( 0 ) != 0.0 ))
		_self->status = 1;
}

TEST(sense, nonsense)
{
	if( !( sin( 0 ) == 0.0 ) )
		_self->status = 1;
}

IMPLEMENT_MAIN(sense, "self-testing")
