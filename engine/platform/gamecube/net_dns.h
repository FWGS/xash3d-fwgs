/* Minimal DNS (RFC 1035) A-record query builder / response parser.
 * Pure C, no libogc dependency, so it can be unit-tested on the host.
 * libbba/libogc2 ship no resolver, so internet play (master servers,
 * hostname connects) needs this. */
#ifndef GC_NET_DNS_H
#define GC_NET_DNS_H
#include <stdint.h>
#include <stddef.h>

enum { GC_DNS_MAX_PACKET = 512 };

/* Returns query length, or 0 if name is invalid / buffer too small. */
static inline size_t GC_DnsBuildQuery( uint8_t *buf, size_t size, uint16_t id, const char *name )
{
	size_t pos = 12, label_start;
	size_t len = 0;

	if( !buf || !name || !name[0] || size < 18 ) return 0;
	buf[0] = id >> 8; buf[1] = id & 255;
	buf[2] = 0x01; buf[3] = 0x00;	/* RD */
	buf[4] = 0; buf[5] = 1;		/* QDCOUNT */
	for( pos = 6; pos < 12; pos++ ) buf[pos] = 0;
	pos = 12;
	while( *name )
	{
		label_start = pos++;
		len = 0;
		while( *name && *name != '.' )
		{
			if( pos + 1 >= size ) return 0;
			buf[pos++] = (uint8_t)*name++;
			len++;
		}
		if( !len || len > 63 ) return 0;
		buf[label_start] = (uint8_t)len;
		if( *name == '.' ) name++;
	}
	if( pos + 5 > size || pos - 12 > 254 ) return 0;
	buf[pos++] = 0;
	buf[pos++] = 0; buf[pos++] = 1;	/* QTYPE A */
	buf[pos++] = 0; buf[pos++] = 1;	/* QCLASS IN */
	return pos;
}

/* Skips an encoded name starting at pos. Returns new pos or 0 on error. */
static inline size_t GC_DnsSkipName( const uint8_t *buf, size_t len, size_t pos )
{
	while( pos < len )
	{
		uint8_t l = buf[pos];
		if( l == 0 ) return pos + 1;
		if(( l & 0xC0 ) == 0xC0 ) return pos + 2 <= len ? pos + 2 : 0;
		if( l & 0xC0 ) return 0;
		pos += 1 + l;
	}
	return 0;
}

/* Parses a response. Returns 1 and writes the first A record (network
 * byte order bytes) to ip[4]; returns 0 on mismatch / error / no answer. */
static inline int GC_DnsParseResponse( const uint8_t *buf, size_t len, uint16_t id, uint8_t ip[4] )
{
	unsigned qd, an, i;
	size_t pos;

	if( !buf || !ip || len < 12 ) return 0;
	if((( buf[0] << 8 ) | buf[1] ) != id ) return 0;
	if( !( buf[2] & 0x80 )) return 0;		/* not a response */
	if(( buf[3] & 0x0F ) != 0 ) return 0;		/* RCODE */
	qd = ( buf[4] << 8 ) | buf[5];
	an = ( buf[6] << 8 ) | buf[7];
	pos = 12;
	for( i = 0; i < qd; i++ )
	{
		pos = GC_DnsSkipName( buf, len, pos );
		if( !pos || pos + 4 > len ) return 0;
		pos += 4;
	}
	for( i = 0; i < an; i++ )
	{
		unsigned type, cls, rdlen;
		pos = GC_DnsSkipName( buf, len, pos );
		if( !pos || pos + 10 > len ) return 0;
		type = ( buf[pos] << 8 ) | buf[pos + 1];
		cls = ( buf[pos + 2] << 8 ) | buf[pos + 3];
		rdlen = ( buf[pos + 8] << 8 ) | buf[pos + 9];
		pos += 10;
		if( pos + rdlen > len ) return 0;
		if( type == 1 && cls == 1 && rdlen == 4 )
		{
			ip[0] = buf[pos]; ip[1] = buf[pos + 1]; ip[2] = buf[pos + 2]; ip[3] = buf[pos + 3];
			return 1;
		}
		pos += rdlen;	/* CNAME etc: keep scanning for the A record */
	}
	return 0;
}
#endif
