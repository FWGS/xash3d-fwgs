#include "crtlib.h"
#include "xash3d_mathlib.h"

static int Test_Atoi( void )
{
	struct {
		const char *str;
		int result;
	} test_data[] = {
		{ "", 0 },
		{ "   123", 123 },
		{ "-123", -123 },
		{ "0xa1ba", 0xa1ba },
		{ "-0xa1ba", -0xa1ba },
		{ "0XA1BA", 0xa1ba },
		{ "-0XA1BA", -0xa1ba },
		{ "'a'", (byte)'a' },
		{ "-'c'", - ((byte)'c') },
	};
	int i;

	for( i = 0; i < sizeof( test_data ) / sizeof( test_data[0] ); i++ )
	{
		if( Q_atoi( test_data[i].str ) != test_data[i].result )
			return i + 1;
	}

	return 0;
}

static int Test_Atof( void )
{
	struct {
		const char *str;
		float result;
	} test_data[] = {
		{ "", 0 },
		{ "   123.123", 123.123 },
		{ "-123.13   ", -123.13 },
		{ "0xa1ba", 0xa1ba },
		{ "-0xa1ba", -0xa1ba },
		{ "0XA1BA", 0xa1ba },
		{ "-0XA1BA", -0xa1ba },
		{ "'a'", (byte)'a' },
		{ "-'c'", - ((byte)'c') }
	};
	int i;

	for( i = 0; i < sizeof( test_data ) / sizeof( test_data[0] ); i++ )
	{
		float result = Q_atof( test_data[i].str );

		if( !Q_equal( result, test_data[i].result ))
			return i + 1;
	}

	return 0;
}

static int Test_Atov( void )
{
	struct {
		const char *str;
		int N;
		float result[3];
	} test_data[] = {
		{ "1.0 1.2 3", 3, { 1.0f, 1.2f, 3.0f }},
		{ "1.234 1.32", 2, { 1.234f, 1.32f }},
	};
	int i;

	for( i = 0; i < sizeof( test_data ) / sizeof( test_data[0] ); i++ )
	{
		float result[3];
		int j;

		memset( result, 0, sizeof( result ));
		Q_atov( result, test_data[i].str, test_data[i].N );

		for( j = 0; j < 3; j++ ) // check that Q_atov didn't parsed more than requested
		{
			if( !Q_equal( result[j], test_data[i].result[j] ))
				return i * 4 + j + 1;
		}
	}

	return 0;
}

int main( void )
{
	int ret = Test_Atoi();

	if( ret > 0 )
		return ret;

	ret = Test_Atof();

	if( ret > 0 )
		return ret + 32;

	ret = Test_Atov();

	if( ret > 0 )
		return ret + 64;

	return 0;
}
