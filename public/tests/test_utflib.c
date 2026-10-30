#include "utflib.h"

static int Test_CP1251ToUnicode( void )
{
	struct {
		uint32_t cp;
		uint32_t uc;
	} test_data[] = {
		{ 'a', 'a' },
		{ 0x80, 0x0402 },
		{ 0xA8, 0x0401 },
		{ 0xB8, 0x0451 },
		{ 0xB9, 0x2116 },
		{ 0xC0, 0x0410 },
		{ 0xDF, 0x042F },
		{ 0xE0, 0x0430 },
		{ 0xFF, 0x044F },
	};
	int i;

	for( i = 0; i < sizeof( test_data ) / sizeof( test_data[0] ); i++ )
	{
		if( Q_CP1251ToUnicode( test_data[i].cp ) != test_data[i].uc )
			return i + 1;
	}

	// 0x98 isn't defined in CP1251
	for( i = 0; i < 256; i++ )
	{
		if( i != 0x98 && Q_UnicodeToCP1251( Q_CP1251ToUnicode( i )) != i )
			return 16 + ( i & 15 );
	}

	return 0;
}

static int Test_CP1252ToUnicode( void )
{
	struct {
		uint32_t cp;
		uint32_t uc;
	} test_data[] = {
		{ 'a', 'a' },
		{ 0x80, 0x20AC },
		{ 0x8A, 0x0160 },
		{ 0x9F, 0x0178 },
		{ 0xA0, 0x00A0 },
		{ 0xE9, 0x00E9 },
		{ 0xFF, 0x00FF },
	};
	int i;

	for( i = 0; i < sizeof( test_data ) / sizeof( test_data[0] ); i++ )
	{
		if( Q_CP1252ToUnicode( test_data[i].cp ) != test_data[i].uc )
			return i + 1;
	}

	return 0;
}

int main( void )
{
	int ret = Test_CP1251ToUnicode();

	if( ret > 0 )
		return ret;

	ret = Test_CP1252ToUnicode();

	if( ret > 0 )
		return ret + 32;

	return 0;
}
