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
#define MAX_INIT_MSG		0x20000	// max length of possible message
#else
#define MAX_INIT_MSG 0x8000
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
void NET_Config( qboolean net_enable );
qboolean NET_IsLocalAddress( netadr_t adr );
char *NET_AdrToString( const netadr_t a );
char *NET_BaseAdrToString( const netadr_t a );
qboolean NET_IsReservedAdr( netadr_t a );
qboolean NET_CompareClassBAdr( netadr_t a, netadr_t b );
qboolean NET_StringToAdr( const char *string, netadr_t *adr );
int NET_StringToAdrNB( const char *string, netadr_t *adr );
qboolean NET_CompareAdr( const netadr_t a, const netadr_t b );
qboolean NET_CompareBaseAdr( const netadr_t a, const netadr_t b );
qboolean NET_GetPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length );
qboolean NET_BufferToBufferCompress( byte *dest, uint *destLen, byte *source, uint sourceLen );
qboolean NET_BufferToBufferDecompress( byte *dest, uint *destLen, byte *source, uint sourceLen );
void NET_SendPacket( netsrc_t sock, size_t length, const void *data, netadr_t to );
void NET_SendPacketEx( netsrc_t sock, size_t length, const void *data, netadr_t to, size_t splitsize );
void NET_ClearLagData( qboolean bClient, qboolean bServer );

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
