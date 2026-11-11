/* Strict IPv4 configuration checks, independent of libogc for host testing. */
#ifndef GC_NET_CONFIG_H
#define GC_NET_CONFIG_H
#include <stdint.h>

static inline int GC_NetParseIPv4( const char *text, uint32_t *value )
{
	uint32_t addr = 0;
	int i;
	if( !text || !value ) return 0;
	for( i = 0; i < 4; ++i )
	{
		unsigned octet = 0, digits = 0;
		const char *start = text;
		while( *text >= '0' && *text <= '9' )
		{
			if( ++digits > 3 ) return 0;
			octet = octet * 10 + (unsigned)( *text++ - '0' );
		}
		if( !digits || octet > 255 || ( digits > 1 && *start == '0' )) return 0;
		addr = ( addr << 8 ) | octet;
		if( i < 3 ) { if( *text++ != '.' ) return 0; }
		else if( *text ) return 0;
	}
	*value = addr;
	return 1;
}

static inline int GC_NetValidateStatic( const char *ip, const char *mask, const char *gateway )
{
	uint32_t a, m, g, inverse, host;
	if( !GC_NetParseIPv4( ip, &a ) || !GC_NetParseIPv4( mask, &m )
		|| !GC_NetParseIPv4( gateway, &g )) return 0;
	inverse = ~m;
	/* Contiguous subnet mask, /1 through /30, usable unicast host address. */
	if( !m || inverse < 3 || ( inverse & ( inverse + 1 ))) return 0;
	host = a & inverse;
	if( !host || host == inverse || ( a >> 24 ) == 0
		|| ( a >> 24 ) == 127 || ( a >> 24 ) >= 224 ) return 0;
	/* libogc2 requires a gateway address even on an isolated LAN. */
	if( !g || ( ( g & m ) != ( a & m ) || !( g & inverse )
		|| ( g & inverse ) == inverse || g == a )) return 0;
	return 1;
}
#endif
