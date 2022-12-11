/*
net_ws.h - network shared functions
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef NET_WS_H
#define NET_WS_H

typedef enum
{
	NS_CLIENT,
	NS_SERVER,
	NS_COUNT
} netsrc_t;

// Max length of unreliable message
#define MAX_DATAGRAM		16384

// Max length of a multicast message
#define MAX_MULTICAST		8192	// some mods spamming for rain effect


#if !XASH_LOW_MEMORY
#define MAX_INIT_MSG		0x30000	// max length of possible message
#else
#define MAX_INIT_MSG		0x8000
#endif
// net packets type
#define NET_HEADER_OUTOFBANDPACKET	-1
#define NET_HEADER_SPLITPACKET	-2
#define NET_HEADER_COMPRESSEDPACKET	-3


#include "netadr.h"

extern convar_t	*net_showpackets;
extern convar_t	*net_clockwindow;

void NET_Init( void );
void NET_Shutdown( void );
void NET_Sleep( int msec );
qboolean NET_IsActive( void );
qboolean NET_IsConfigured( void );
void NET_Config( qboolean net_enable, qboolean changeport );
qboolean NET_IsLocalAddress( netadr_t adr );
const char *NET_AdrToString( const netadr_t a );
const char *NET_BaseAdrToString( const netadr_t a );
qboolean NET_IsReservedAdr( netadr_t a );
qboolean NET_CompareClassBAdr( const netadr_t a, const netadr_t b );
qboolean NET_StringToAdr( const char *string, netadr_t *adr );
qboolean NET_StringToFilterAdr( const char *s, netadr_t *adr, uint *prefixlen );
int NET_StringToAdrNB( const char *string, netadr_t *adr );
int NET_CompareAdrSort( const void *_a, const void *_b );
qboolean NET_CompareAdr( const netadr_t a, const netadr_t b );
qboolean NET_CompareBaseAdr( const netadr_t a, const netadr_t b );
qboolean NET_CompareAdrByMask( const netadr_t a, const netadr_t b, uint prefixlen );
qboolean NET_GetPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length );
qboolean NET_BufferToBufferCompress( byte *dest, uint *destLen, byte *source, uint sourceLen );
qboolean NET_BufferToBufferDecompress( byte *dest, uint *destLen, byte *source, uint sourceLen );
void NET_SendPacket( netsrc_t sock, size_t length, const void *data, netadr_t to );
void NET_SendPacketEx( netsrc_t sock, size_t length, const void *data, netadr_t to, size_t splitsize );
void NET_ClearLagData( qboolean bClient, qboolean bServer );
void NET_IP6BytesToNetadr( netadr_t *adr, const uint8_t *ip6 );
void NET_NetadrToIP6Bytes( uint8_t *ip6, const netadr_t *adr );

#if !XASH_DEDICATED
qboolean CL_LegacyMode( void );
int CL_GetSplitSize( void );
#endif

void HTTP_AddCustomServer( const char *url );
void HTTP_AddDownload( const char *path, int size, qboolean process );
void HTTP_ClearCustomServers( void );
void HTTP_Shutdown( void );
void HTTP_Init( void );
void HTTP_Run( void );

#endif//NET_WS_H
