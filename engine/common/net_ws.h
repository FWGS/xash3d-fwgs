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

typedef enum
{
	NET_EAI_NONAME = 0,
	NET_EAI_OK     = 1,
	NET_EAI_AGAIN  = 2
} net_gai_state_t;

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

extern convar_t net_showpackets;
extern convar_t net_clockwindow;
extern convar_t net_send_debug;
extern convar_t net_recv_debug;

void NET_Init( void );
void NET_Shutdown( void );
void NET_Sleep( int msec );
qboolean NET_IsActive( void );
qboolean NET_IsConfigured( void );
void NET_Config( qboolean net_enable, qboolean changeport );
const char *NET_AdrToString( const netadr_t a ) RETURNS_NONNULL;
const char *NET_BaseAdrToString( const netadr_t a ) RETURNS_NONNULL;
qboolean NET_IsReservedAdr( netadr_t a );
qboolean NET_StringToAdr( const char *string, netadr_t *adr );
qboolean NET_StringToFilterAdr( const char *s, netadr_t *adr, uint *prefixlen );
net_gai_state_t NET_StringToAdrNB( const char *string, netadr_t *adr, qboolean v6only );
int NET_CompareAdrSort( const void *_a, const void *_b );
qboolean NET_CompareAdr( const netadr_t a, const netadr_t b );
qboolean NET_CompareBaseAdr( const netadr_t a, const netadr_t b );
qboolean NET_CompareAdrByMask( const netadr_t a, const netadr_t b, uint prefixlen );
qboolean NET_GetPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length );
void NET_SendPacket( netsrc_t sock, size_t length, const void *data, netadr_t to );
void NET_SendPacketEx( netsrc_t sock, size_t length, const void *data, netadr_t to, size_t splitsize );
void NET_IP6BytesToNetadr( netadr_t *adr, const uint8_t *ip6 );
void NET_NetadrToIP6Bytes( uint8_t *ip6, const netadr_t *adr );

static inline qboolean NET_IsLocalAddress( netadr_t adr )
{
	return NET_NetadrType( &adr ) == NA_LOOPBACK;
}

void NET_GetLocalAddress( netadr_t *ip4, netadr_t *ip6 );

#if !XASH_DEDICATED
int CL_GetSplitSize( void );
#endif

void HTTP_AddCustomServer( const char *url );
void HTTP_AddDownload( const char *path, int size, qboolean process, resource_t *res );
void HTTP_ClearCustomServers( void );
void HTTP_Shutdown( void );
void HTTP_ResetProcessState( void );
void HTTP_Init( void );
void HTTP_Run( void );

#endif//NET_WS_H
