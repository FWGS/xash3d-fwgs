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

static int Test_DecodeUTF8Char( void )
{
	struct
	{
		const char *s;
		size_t len;
		uint32_t uc;
		size_t consumed;
	} test_data[] = {
		{ "a", 1, 'a', 1 },
		{ "\xd0\x96", 2, 0x0416, 2 },
		{ "\xe2\x82\xac", 3, 0x20AC, 3 },
		{ "\xf0\x9f\x98\x80", 4, 0x1F600, 4 },
		{ "\xff", 1, 0xFFFD, 1 }, // invalid lead byte
		{ "\x80", 1, 0xFFFD, 1 }, // lone continuation byte
		{ "\xc0\xaf", 2, 0xFFFD, 1 }, // overlong '/', the lead byte is never valid
		{ "\xe0\x80\xaf", 3, 0xFFFD, 3 }, // overlong '/'
		{ "\xed\xa0\x80", 3, 0xFFFD, 3 }, // surrogate
		{ "\xf4\x90\x80\x80", 4, 0xFFFD, 4 }, // past U+10FFFF
		{ "\xe2\x82", 2, 0xFFFD, 2 }, // truncated
		{ "\xe2\x41", 2, 0xFFFD, 1 }, // cut by ASCII, which is decoded next
	};

	for( size_t i = 0; i < sizeof( test_data ) / sizeof( test_data[0] ); i++ )
	{
		uint32_t uc;
		size_t n = Q_DecodeUTF8Char( test_data[i].s, test_data[i].len, &uc );

		if( uc != test_data[i].uc || n != test_data[i].consumed )
			return i + 1;
	}

	return 0;
}

static int Test_UTF8PrevNextChar( void )
{
	// a, CYRILLIC CAPITAL LETTER ZHE, EURO SIGN, z
	const char s[] = "a\xd0\x96\xe2\x82\xacz";
	const size_t starts[] = { 0, 1, 3, 6 };
	const size_t len = sizeof( s ) - 1;

	for( size_t i = 0; i < sizeof( starts ) / sizeof( starts[0] ); i++ )
	{
		size_t next = i + 1 < sizeof( starts ) / sizeof( starts[0] ) ? starts[i + 1] : len;

		if( Q_UTF8NextChar( s, len, starts[i] ) != next )
			return i + 1;

		if( Q_UTF8PrevChar( s, next ) != starts[i] )
			return i + 8;
	}

	if( Q_UTF8NextChar( s, len, len ) != len || Q_UTF8PrevChar( s, 0 ) != 0 )
		return 16;

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

	ret = Test_DecodeUTF8Char();

	if( ret > 0 )
		return ret + 64;

	ret = Test_UTF8PrevNextChar();

	if( ret > 0 )
		return ret + 96;

	return 0;
}
