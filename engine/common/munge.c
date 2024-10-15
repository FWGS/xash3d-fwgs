/*
munge.c - protocol mangling for GoldSrc
Copyright (C) ReHLDS developers
Copyright (C) 2023 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "common.h"

static const byte mungify_table[] =
{
	0x7A, 0x64, 0x05, 0xF1, 0x1B, 0x9B, 0xA0, 0xB5,
	0xCA, 0xED, 0x61, 0x0D, 0x4A, 0xDF, 0x8E, 0xC7,
};

static const byte mungify_table2[] =
{
	0x05, 0x61, 0x7A, 0xED,	0x1B, 0xCA, 0x0D, 0x9B,
	0x4A, 0xF1, 0x64, 0xC7,	0xB5, 0x8E, 0xDF, 0xA0,
};

static const byte mungify_table3[] =
{
	0x20, 0x07, 0x13, 0x61,	0x03, 0x45, 0x17, 0x72,
	0x0A, 0x2D, 0x48, 0x0C,	0x4A, 0x12, 0xA9, 0xB5,
};

static uint COM_SwapLong( uint c )
{
	return ((( c >> 0  ) & 0xFF) << 24 ) +
		   ((( c >> 8  ) & 0xFF) << 16 ) +
		   ((( c >> 16 ) & 0xFF) << 8  ) +
		   ((( c >> 24 ) & 0xFF) << 0  );
}

static void COM_GenericMunge( byte *data, const size_t len, const int seq, const byte *table, const qboolean reverse )
{
	const size_t mungelen = len / 4;
	int i;

	for( i = 0; i < mungelen; i++ )
	{
		uint32_t c;
		void *pc = &data[i * 4];
		byte *p;
		int j;

		memcpy( &c, pc, sizeof( c ));
		c ^= seq;
		if( !reverse )
			c = COM_SwapLong( c );

		p = (byte *)&c;
		for( j = 0; j < 4; j++ )
			*p++ ^= (0xa5 | (j << j) | j | table[(i + j) & 0x0f]);

		if( reverse )
			c = COM_SwapLong( c );
		c ^= ~seq;
		memcpy( pc, &c, sizeof( c ));
	}
}

// Anti-proxy/aimbot obfuscation code
// COM_UnMunge should reversably fixup the data
void COM_Munge( byte *data, size_t len, int seq )
{
	COM_GenericMunge( data, len, seq, mungify_table, false );
}

void COM_UnMunge( byte *data, size_t len, int seq )
{
	COM_GenericMunge( data, len, seq, mungify_table, true );
}

void COM_Munge2( byte *data, size_t len, int seq )
{
	COM_GenericMunge( data, len, seq, mungify_table2, false );
}

void COM_UnMunge2( byte *data, size_t len, int seq )
{
	COM_GenericMunge( data, len, seq, mungify_table2, true );
}

void COM_Munge3( byte *data, size_t len, int seq )
{
	COM_GenericMunge( data, len, seq, mungify_table3, false );
}

void COM_UnMunge3( byte *data, size_t len, int seq )
{
	COM_GenericMunge( data, len, seq, mungify_table3, true );
}

#if XASH_ENGINE_TESTS
#include "tests.h"

void Test_RunMunge( void )
{
	const char *msg = "0123456789qwertyuiopasdfghjklzxcvbnmaa";
	const char *expected[][3] =
	{
	{
		"\x33\x2a\x61\x30\x2d\x6e\x35\x74\x2d\x79\x79\x78\x73\x34\x32\x25\x30\x2f\x39\x35\x26\x3c\x33\x61\x31\x22\x78\x67\x29\x68\x6a\x6c\x7d\x7e\x72\x36\x61\x61",
		"\x69\x2a\x31\x30\x2d\x36\x25\x74\x77\x61\x79\x38\x6b\x34\x62\x25\x30\x7f\x39\x35\x76\x34\x33\x61\x39\x2a\x78\x67\x23\x68\x7a\x6c\x7d\x66\x72\x76\x61\x61",
		"\x69\x6a\x71\x30\x6f\x7e\x25\x74\x3f\x69\x69\x38\x63\x2c\x62\x25\x28\x77\x29\x75\x7c\x2c\x73\x21\x23\x62\x38\x27\x6b\x28\x2a\x6c\x3d\x3e\x72\x36\x61\x61",
	}, {
		"\x32\x2a\x61\x31\x2c\x6e\x35\x75\x2c\x79\x79\x79\x72\x34\x32\x24\x31\x2f\x39\x34\x27\x3c\x33\x60\x30\x22\x78\x66\x28\x68\x6a\x6d\x7c\x7e\x72\x37\x61\x61",
		"\x68\x2a\x31\x31\x2c\x36\x25\x75\x76\x61\x79\x39\x6a\x34\x62\x24\x31\x7f\x39\x34\x77\x34\x33\x60\x38\x2a\x78\x66\x22\x68\x7a\x6d\x7c\x66\x72\x77\x61\x61",
		"\x68\x6a\x71\x31\x6e\x7e\x25\x75\x3e\x69\x69\x39\x62\x2c\x62\x24\x29\x77\x29\x74\x7d\x2c\x73\x20\x22\x62\x38\x26\x6a\x28\x2a\x6d\x3c\x3e\x72\x37\x61\x61",
	}
	};
	string buf;
	size_t msglen = Q_strlen( msg ) + 1;
	int i;

	Q_strncpy( buf, msg, msglen );

	for( i = 0; i < 0xFF; i++ )
	{
		COM_Munge( buf, msglen, i );
		if( i < sizeof( expected ) / sizeof( expected[0] ))
		{
			//for( int j = 0; j < msglen; j++ )
			//	printf( "\\x%02x", buf[j] );
			//printf( "\n" );
			TASSERT( !memcmp( buf, expected[i][0], msglen ));
		}
		COM_UnMunge( buf, msglen, i );

		TASSERT( !Q_strcmp( buf, msg ));

		COM_Munge2( buf, msglen, i );
		if( i < sizeof( expected ) / sizeof( expected[0] ))
		{
			TASSERT( !memcmp( buf, expected[i][1], msglen ));
		}
		COM_UnMunge2( buf, msglen, i );

		TASSERT( !Q_strcmp( buf, msg ));

		COM_Munge3( buf, msglen, i );
		if( i < sizeof( expected ) / sizeof( expected[0] ))
		{
			TASSERT( !memcmp( buf, expected[i][2], msglen ));
		}
		COM_UnMunge3( buf, msglen, i );

		TASSERT( !Q_strcmp( buf, msg ));
	}
}
#endif
