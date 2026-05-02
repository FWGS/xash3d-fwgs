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
#include "net_ws.h"
#include "net_ws_private.h"

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

// Protocol constants
#define SBRK_FRAME_HEADER			"SBRK"
#define SBRK_FRAME_HEADER_SIZE		(sizeof(SBRK_FRAME_HEADER) - 1)
#define SBRK_FRAME_LENGTH_SIZE		2
#define SBRK_RESPONSE_HEADER		"sb_connect\n"
#define SBRK_RESPONSE_HEADER_SIZE	(sizeof(SBRK_RESPONSE_HEADER) - 1)
#define SBRK_MAX_FRAME_SIZE			4096
#define SBRK_CONNECT_TIMEOUT		10.0
#define SBRK_CONNECT_RETRY_DELAY	5.0
#define SBRK_TICKET_SIZE_MAX 		2048

static CVAR_DEFINE_AUTO( cl_steam_broker_addr, "127.0.0.1:27420", FCVAR_ARCHIVE, "address of steam broker instance" );

typedef enum
{
	SBRK_STATE_IDLE,
	SBRK_STATE_CONNECTING,
	SBRK_STATE_CONNECTED,
	SBRK_STATE_GAMESHUTDOWN
} sbrk_state_t;

typedef struct
{
	netadr_t adr;
	int socket;
	sbrk_state_t state;
	int challenge;
	netadr_t serveradr;
	double connection_timeout;
	double idle_cycle_timeout;
	uint8_t rx_buffer[SBRK_MAX_FRAME_SIZE + 64];
	uint8_t tx_buffer[SBRK_MAX_FRAME_SIZE + 64];
	uint32_t rx_buffer_pos;
	uint32_t tx_buffer_pos;
} steam_broker_t;

static steam_broker_t broker;

static void SteamBroker_SetState( sbrk_state_t new_state )
{
	if( broker.state != new_state )
	{
		// we also may logging transitions if needed
		broker.state = new_state;
	}
}

static qboolean SteamBroker_UpdateBrokerAddress( void )
{
	if( NET_NetadrType( &broker.adr ) == NA_UNDEFINED )
	{
		if( !NET_StringToAdr( cl_steam_broker_addr.string, &broker.adr ))
			return false;
	}
	return true;
}

static void SteamBroker_CloseSocket( void )
{
	if( NET_IsSocketValid( broker.socket ))
	{
		closesocket( broker.socket );
		broker.socket = INVALID_SOCKET;
	}
	broker.rx_buffer_pos = 0;
	broker.tx_buffer_pos = 0;
}

static void SteamBroker_Disconnect( void )
{
	SteamBroker_CloseSocket();
	SteamBroker_SetState( SBRK_STATE_IDLE );
}

static qboolean SteamBroker_ConnectImpl( void )
{
	int addr_family;
	struct sockaddr_storage addr = { 0 };

	if( NET_NetadrType( &broker.adr ) == NA_IP )
	{
		addr_family = AF_INET;
	}
	else if( NET_NetadrType( &broker.adr ) == NA_IP6 )
	{
		addr_family = AF_INET6;
	}
	else
	{
		Con_Printf( S_ERROR "%s: unsupported broker address type for %s\n", __func__, cl_steam_broker_addr.string );
		return false;
	}

	broker.socket = socket( addr_family, SOCK_STREAM, IPPROTO_TCP );
	if( !NET_IsSocketValid( broker.socket ))
	{
		Con_Printf( S_ERROR "%s: failed to create socket\n", __func__ );
		return false;
	}

	if( !NET_MakeSocketNonBlocking( broker.socket ))
	{
		Con_Printf( S_ERROR "%s: failed to set non-blocking mode, error %s\n", __func__, NET_ErrorString( ));
		SteamBroker_CloseSocket();
		return false;
	}

	NET_NetadrToSockadr( &broker.adr, &addr );

	int result = connect( broker.socket, (struct sockaddr *)&addr, NET_SockAddrLen( &addr ));
	if( NET_IsSocketError( result ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY && err != WSAEINPROGRESS )
		{
			Con_Printf( S_ERROR "%s: failed to connect to broker at %s with error %s\n", __func__, cl_steam_broker_addr.string, NET_ErrorString( ));
			SteamBroker_CloseSocket();
			return false;
		}
	}

	broker.connection_timeout = Platform_DoubleTime() + SBRK_CONNECT_TIMEOUT;
	SteamBroker_SetState( SBRK_STATE_CONNECTING );
	return true;
}

static qboolean SteamBroker_SendFrame( const char *payload, size_t payload_size )
{
	if( payload_size > SBRK_MAX_FRAME_SIZE )
	{
		Con_Printf( S_WARN "%s: payload too large (%zu > %u)\n", __func__, payload_size, SBRK_MAX_FRAME_SIZE );
		return false;
	}

	uint8_t frame[SBRK_MAX_FRAME_SIZE + 6];
	sizebuf_t sb;

	MSG_Init( &sb, "SteamBroker_SendFrame", frame, sizeof( frame ));

	MSG_WriteBytes( &sb, SBRK_FRAME_HEADER, SBRK_FRAME_HEADER_SIZE );
	MSG_WriteShort( &sb, payload_size );
	MSG_WriteBytes( &sb, payload, payload_size );

	size_t frame_size = MSG_GetRealBytesWritten( &sb );
	int sent = send( broker.socket, (const char *)frame, frame_size, 0 );
	if( NET_IsSocketError( sent ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY )
		{
			Con_Printf( S_ERROR "%s: send error %s\n", __func__, NET_ErrorString( ));
			SteamBroker_Disconnect( );
			return false;
		}
		sent = 0;
	}

	// bufferize unsent data for deferred sending
	size_t unsent = frame_size - sent;
	if( unsent > 0 )
	{
		size_t available = sizeof( broker.tx_buffer ) - broker.tx_buffer_pos;
		if( available < unsent )
		{
			Con_Printf( S_ERROR "%s: transmit buffer overflow (%zu > %zu)\n", __func__, unsent, available );
			SteamBroker_Disconnect( );
			return false;
		}

		memcpy( broker.tx_buffer + broker.tx_buffer_pos, frame + sent, unsent );
		broker.tx_buffer_pos += unsent;
	}

	return true;
}

static qboolean SteamBroker_ProcessFrame( void )
{
	if( broker.rx_buffer_pos < SBRK_FRAME_HEADER_SIZE + SBRK_FRAME_LENGTH_SIZE )
		return false;

	sizebuf_t sb;
	MSG_Init( &sb, "SteamBroker_ProcessFrame", broker.rx_buffer, broker.rx_buffer_pos );

	// verify frame header
	char header[SBRK_FRAME_HEADER_SIZE];
	if( !MSG_ReadBytes( &sb, header, SBRK_FRAME_HEADER_SIZE ))
		return false;

	if( memcmp( header, SBRK_FRAME_HEADER, SBRK_FRAME_HEADER_SIZE ) != 0 )
	{
		Con_Printf( S_ERROR "%s: invalid frame header\n", __func__ );
		SteamBroker_Disconnect( );
		return false;
	}

	uint16_t payload_size = MSG_ReadShort( &sb );
	uint32_t frame_size = SBRK_FRAME_HEADER_SIZE + SBRK_FRAME_LENGTH_SIZE + payload_size;

	if( MSG_GetNumBytesLeft( &sb ) < payload_size )
		return false; // need more data

	char response_header[SBRK_RESPONSE_HEADER_SIZE];
	if( MSG_ReadBytes( &sb, response_header, SBRK_RESPONSE_HEADER_SIZE ))
	{
		if( memcmp( response_header, SBRK_RESPONSE_HEADER, SBRK_RESPONSE_HEADER_SIZE ) == 0 )
		{
			int32_t challenge = MSG_ReadLong( &sb );
			if( broker.challenge != challenge )
			{
				Con_Printf( S_ERROR "%s: challenge mismatch\n", __func__ );
			}
			else
			{
				uint64_t steam_id;
				MSG_ReadBytes( &sb, &steam_id, sizeof( steam_id ));
				uint32_t ticket_size = MSG_ReadDword( &sb );
				uint8_t ticket_data[SBRK_TICKET_SIZE_MAX];

				if( ticket_size > SBRK_TICKET_SIZE_MAX )
				{
					Con_Printf( S_ERROR "%s: ticket size exceeds limit (%u)\n", __func__, ticket_size );
				}
				else if( MSG_ReadBytes( &sb, ticket_data, ticket_size ))
				{
					Con_Printf( "%s: SteamID: %"PRIu64", ticket: [%d, %d, %d, %d...]\n", __func__, steam_id, ticket_data[0], ticket_data[1], ticket_data[2], ticket_data[3] );

					memcpy( cls.steamid, &steam_id, sizeof( cls.steamid ));
					CL_SendGoldSrcConnectPacket( broker.serveradr, broker.challenge, ticket_data, ticket_size );
					cls.broker_wait = false;
				}
				else
				{
					Con_Printf( S_ERROR "%s: failed to read ticket data\n", __func__ );
				}
			}
		}
	}
	
	// remove processed frame from buffer
	memmove( broker.rx_buffer, broker.rx_buffer + frame_size, broker.rx_buffer_pos - frame_size );
	broker.rx_buffer_pos -= frame_size;

	return true;
}

static void SteamBroker_HandleDataTx( void )
{
	if( broker.tx_buffer_pos == 0 )
		return;

	int sent = send( broker.socket, (const char *)broker.tx_buffer, broker.tx_buffer_pos, 0 );
	if( NET_IsSocketError( sent ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY )
		{
			Con_Printf( S_ERROR "%s: send error %s\n", __func__, NET_ErrorString( ));
			SteamBroker_Disconnect( );
		}
		return;
	}

	if( sent > 0 )
	{
		// remove sent data from buffer
		memmove( broker.tx_buffer, broker.tx_buffer + sent, broker.tx_buffer_pos - sent );
		broker.tx_buffer_pos -= sent;
	}
}

static void SteamBroker_HandleDataRx( void )
{
	int available = sizeof( broker.rx_buffer ) - broker.rx_buffer_pos;
	if( available <= 0 )
	{
		Con_Printf( S_ERROR "%s: receive buffer overflow\n", __func__ );
		SteamBroker_Disconnect( );
		return;
	}

	int received = recv( broker.socket, (char *)broker.rx_buffer + broker.rx_buffer_pos, available, 0 );
	if( NET_IsSocketError( received ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY )
		{
			Con_Printf( S_ERROR "%s: recv error %s\n", __func__, NET_ErrorString( ));
			SteamBroker_Disconnect( );
		}
		return;
	}

	if( received == 0 )
	{
		Con_Printf( S_NOTE "%s: connection closed by broker\n", __func__ );
		SteamBroker_Disconnect( );
		return;
	}

	broker.rx_buffer_pos += received;

	while( SteamBroker_ProcessFrame( ));
}

static void SteamBroker_UpdateIdle( void )
{
	if( broker.idle_cycle_timeout < Platform_DoubleTime( ))
	{
		if( SteamBroker_UpdateBrokerAddress( ))
		{
			SteamBroker_ConnectImpl( );
		}
		else
		{
			Con_Printf( S_ERROR "%s: failed to resolve broker address \"%s\"\n", __func__, cl_steam_broker_addr.string );
		}
		broker.idle_cycle_timeout = Platform_DoubleTime() + SBRK_CONNECT_RETRY_DELAY;
	}
}

void SteamBroker_AnnounceGameStart( const char *gamedir )
{
	if( Q_stricmp( cl_ticket_generator.string, "steam" ) != 0 )
		return;

	if( broker.state != SBRK_STATE_CONNECTED )
		return;

	// sb_gamedir <gamedir>
	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_gamedir %s", gamedir );

	if( len > 0 )
		SteamBroker_SendFrame( buf, len );
}

void SteamBroker_AnnounceGameShutdown( void )
{
	if( Q_stricmp( cl_ticket_generator.string, "steam" ) != 0 )
		return;

	if( broker.state != SBRK_STATE_CONNECTED )
		return;

	SteamBroker_SendFrame( "sb_terminate", sizeof( "sb_terminate" ) - 1 );
}

static void SteamBroker_UpdateConnecting( void )
{
	if( Platform_DoubleTime() > broker.connection_timeout )
	{
		Con_Printf( S_WARN "%s: connection to %s timed out\n", __func__, cl_steam_broker_addr.string );
		SteamBroker_Disconnect();
		return;
	}

	fd_set writefds;
	FD_ZERO( &writefds );
	FD_SET( broker.socket, &writefds );

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

#if XASH_WIN32
	int select_result = select( 0, NULL, &writefds, NULL, &tv );
#else
	int select_result = select( broker.socket + 1, NULL, &writefds, NULL, &tv );
#endif
	if( select_result == SOCKET_ERROR )
	{
		Con_Printf( S_ERROR "%s: select() failed\n", __func__ );
		SteamBroker_Disconnect();
		return;
	}

	if( FD_ISSET( broker.socket, &writefds ))
	{
		// socket is writable - connection established or failed
		int err = 0;
		socklen_t err_len = sizeof( err );
		if( NET_IsSocketError( getsockopt( broker.socket, SOL_SOCKET, SO_ERROR, (char *)&err, &err_len )))
		{
			Con_Printf( S_ERROR "%s: getsockopt() failed\n", __func__ );
			SteamBroker_Disconnect();
			return;
		}
		else if( err != 0 )
		{
			Con_Printf( S_ERROR "%s: connection failed with error %d\n", __func__, err );
			SteamBroker_Disconnect();
			return;
		}
		else
		{
			broker.connection_timeout = 0;
			Con_Printf( S_NOTE "%s: connected to broker at %s\n", __func__, cl_steam_broker_addr.string );
			SteamBroker_SetState( SBRK_STATE_CONNECTED );
			SteamBroker_AnnounceGameStart( GI->gamefolder );
		}
	}
}

static void SteamBroker_UpdateConnected( void )
{
	SteamBroker_HandleDataTx( );
	SteamBroker_HandleDataRx( );
}

qboolean SteamBroker_InitiateGameConnection( netadr_t serveradr, int challenge )
{
	// only ipv4 supported
	if( NET_NetadrType( &serveradr ) != NA_IP )
		return false;

	if( broker.state != SBRK_STATE_CONNECTED )
	{
		Con_Printf( S_WARN "%s: broker not connected\n", __func__ );
		return false;
	}

	broker.challenge = challenge;
	broker.serveradr = serveradr;

	// sb_connect <ip:port> <server_steamid> <secure> <challenge>
	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_connect %s %"PRIu64" %d %d", NET_AdrToString( serveradr ), cls.server_steamid, cls.vac2_secure ? 1 : 0, challenge );

	if( !SteamBroker_SendFrame( buf, len ))
		return false;

	return true;
}

void SteamBroker_TerminateGameConnection( void )
{
	if( broker.state != SBRK_STATE_CONNECTED )
		return;

	if( Q_stricmp( cl_ticket_generator.string, "steam" ) != 0 )
		return;

	// sb_disconnect <ip:port> <challenge>
	char buf[512];
	int len = Q_snprintf( buf, sizeof( buf ), "sb_disconnect %s %d", NET_AdrToString( cls.serveradr ), broker.challenge );

	SteamBroker_SendFrame( buf, len );
}

void SteamBroker_Frame( void )
{
	if( FBitSet( cl_steam_broker_addr.flags | cl_ticket_generator.flags, FCVAR_CHANGED ))
	{
		ClearBits( cl_ticket_generator.flags, FCVAR_CHANGED );
		ClearBits( cl_steam_broker_addr.flags, FCVAR_CHANGED );

		if( broker.state != SBRK_STATE_IDLE )
		{
			SteamBroker_Disconnect();
		}

		// reinitialize address
		NET_NetadrSetType( &broker.adr, NA_UNDEFINED );
	}

	if( Q_stricmp( cl_ticket_generator.string, "steam" ) != 0 )
		return;

	// update state machine
	switch( broker.state )
	{
	case SBRK_STATE_IDLE:
		SteamBroker_UpdateIdle( );
		break;
	case SBRK_STATE_CONNECTING:
		SteamBroker_UpdateConnecting( );
		break;
	case SBRK_STATE_CONNECTED:
		SteamBroker_UpdateConnected( );
		break;
	case SBRK_STATE_GAMESHUTDOWN:
		// do nothing, just wait for game shutdown
		break;
	}
}

void SteamBroker_Init( void )
{
	broker.state = SBRK_STATE_IDLE;
	broker.socket = INVALID_SOCKET;
	broker.rx_buffer_pos = 0;
	broker.tx_buffer_pos = 0;
	Cvar_RegisterVariable( &cl_steam_broker_addr );
	NET_NetadrSetType( &broker.adr, NA_UNDEFINED );
}

void SteamBroker_Shutdown( void )
{
	if( Q_stricmp( cl_ticket_generator.string, "steam" ) != 0 )
		return;

	SteamBroker_AnnounceGameShutdown( );
	SteamBroker_SetState( SBRK_STATE_GAMESHUTDOWN );
}
