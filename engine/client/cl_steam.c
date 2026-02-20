/*
cl_steam.c - steam(tm) broker implementation
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <inttypes.h>
#include "common.h"
#include "client.h"

// What is a broker?
// From Wikipedia, the free encyclopedia:
// "The broker pattern is an architecture pattern that involves the use of an
// intermediary software entity, called a "broker", to facilitate communication
// between two or more software components. The broker acts as a "middleman"
// between the components, allowing them to communicate without being aware of
// each other's existence.
//
// Due to proprietary nature of Steamworks SDK, it cannot be run on same amount
// of platforms supported by Xash3D FWGS, neither we can link directly due to
// GNU GPLv3 license. However, here comes the broker, by running it (in trusted
// network, preferrably) on a machine that has Steam client installed, the
// engine can communicate with it, acquiring needed information to log-in into
// Steam protected multiplayer servers.
static CVAR_DEFINE_AUTO( cl_steam_broker_addr, "127.0.0.1:27420", FCVAR_ARCHIVE, "address of steam broker instance" );

static struct
{
	netadr_t adr;

	int challenge;
	netadr_t serveradr;
} broker;

qboolean SteamBroker_InitiateGameConnection( netadr_t serveradr, int challenge )
{
	if( NET_NetadrType( &broker.adr ) == NA_UNDEFINED )
	{
		if( !NET_StringToAdr( cl_steam_broker_addr.string, &broker.adr ))
		{
			Con_Printf( "%s: NET_StringToAdr( %s ) failed\n", __func__, cl_steam_broker_addr.string );
			return false;
		}
	}

	// only ipv4 supported
	if( NET_NetadrType( &serveradr ) != NA_IP )
		return false;

	broker.challenge = challenge;
	broker.serveradr = serveradr;

	// sb_connect <ip:port> <server's steam id> <secure> <challenge>
	// the message calls
	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_connect %s %"PRIu64" %s %d", NET_AdrToString( serveradr ), cls.server_steamid, cls.vac2_secure ? "true" : "false", broker.challenge );

	NET_SendPacket( NS_CLIENT, len, buf, broker.adr );

	return true;
}

void SteamBroker_TerminateGameConnection( void )
{
	if( NET_NetadrType( &broker.adr ) == NA_UNDEFINED )
		return;

	if( NET_NetadrType( &cls.serveradr ) != NA_IP )
		return;

	if( cls.legacymode != PROTO_GOLDSRC )
		return;

	// sb_terminate <ip:port> <challenge>
	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_terminate %s %d", NET_AdrToString( cls.serveradr ), broker.challenge );

	NET_SendPacket( NS_CLIENT, len, buf, broker.adr );
	NET_NetadrSetType( &broker.adr, NA_UNDEFINED );
}

void SteamBroker_HandlePacket( netadr_t from, sizebuf_t *msg )
{
	// message format
	// sb_connect\n<4 byte challenge><8 byte steamid><unsigned 4 byte len><len bytes ticket>
	int challenge;
	uint32_t len;
	uint8_t ticket[2048]; // 2048 bytes according to SDK docs

	if( !NET_CompareAdr( from, broker.adr ))
		return;

	challenge = MSG_ReadLong( msg );

	if( broker.challenge != challenge ) // TODO: print error
		return;

	MSG_ReadBytes( msg, cls.steamid, sizeof( cls.steamid ));

	len = MSG_ReadDword( msg );
	if( len > sizeof( ticket )) // TODO: print error, proceed without ticket?
		return;

	MSG_ReadBytes( msg, ticket, len );

	Con_Printf( "%s: SteamID: %"PRIu64", ticket: [%d, %d, %d, %d...]\n", __func__, *(uint64_t *)cls.steamid, ticket[0], ticket[1], ticket[2], ticket[3] );

	CL_SendGoldSrcConnectPacket( broker.serveradr, challenge, ticket, len );

	cls.broker_wait = false;
}

void SteamBroker_Init( void )
{
	Cvar_RegisterVariable( &cl_steam_broker_addr );
	NET_NetadrSetType( &broker.adr, NA_UNDEFINED );
}

void SteamBroker_Shutdown( void )
{
}
