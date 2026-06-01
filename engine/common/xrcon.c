/*
xrcon.c - implementation of XRCON remote console access server
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

#include "common.h"
#include "net_ws.h"
#include "net_ws_private.h"
#include "net_buffer.h"

#define XRCON_TYPE_CMND          HostFourCC( 'C', 'M', 'N', 'D' )
#define XRCON_TYPE_CHAN          HostFourCC( 'C', 'H', 'A', 'N' )
#define XRCON_TYPE_AINF          HostFourCC( 'A', 'I', 'N', 'F' )
#define XRCON_TYPE_ADON          HostFourCC( 'A', 'D', 'O', 'N' )
#define XRCON_TYPE_PRNT          HostFourCC( 'P', 'R', 'N', 'T' )

#define XRCON_CMND_VERSION       0x000000D4
#define XRCON_CMND_VERSION_CS2RC 0x00D40000 // CS2RemoteConsole <= 1.3.0 byte-slot bug

// type (u32) + version (u32) + length (u16) + handle (u16)
#define XRCON_HEADER_SIZE        ( sizeof( uint32_t ) * 2 + sizeof( uint16_t ) * 2 )
// hardcoded size for channel name, including null terminator
#define XRCON_CHAN_NAME_SIZE     34
// unknowns[19] (u32) + padding (u8)
#define XRCON_AINF_PACKET_SIZE   ( sizeof( uint32_t ) * 19 + sizeof( uint8_t ) )
// channel_id (u32) + padding (u32[5]) + RGBA (u32)
#define XRCON_PRNT_HEADER_SIZE   ( sizeof( uint32_t ) * 7 )

#define XRCON_MAX_FRAME_SIZE     4096 // an arbitrary number, should be enough for everything
#define XRCON_PRINT_BUFFER_SIZE  4096
#define XRCON_MAX_PACKET_SIZE    ( XRCON_HEADER_SIZE + XRCON_PRNT_HEADER_SIZE + XRCON_PRINT_BUFFER_SIZE )

#define XRCON_TX_BUFFER_SIZE     ( XRCON_MAX_FRAME_SIZE * 4 )
#define XRCON_RX_BUFFER_SIZE     ( XRCON_MAX_FRAME_SIZE + 64 )

static CVAR_DEFINE_AUTO( xrcon_enable, "0", FCVAR_PRIVILEGED, "enable remote console access server" );
static CVAR_DEFINE_AUTO( xrcon_address, "127.0.0.1:27000", FCVAR_PRIVILEGED, "XRCON server bind address and port" );
static CVAR_DEFINE_AUTO( xrcon_flush_interval, "0.05", FCVAR_PRIVILEGED, "seconds between flushes of pending console output to the XRCON client" );
static CVAR_DEFINE_AUTO( xrcon_retry_delay, "5.0", FCVAR_PRIVILEGED, "seconds to wait before retrying to bind the XRCON listen socket after a failure" );

typedef enum
{
	XRCON_STATE_IDLE,
	XRCON_STATE_LISTENING,
	XRCON_STATE_CONNECTED
} xrcon_state_t;

typedef enum
{
	XRCON_PARSER_WAIT_HEADER,
	XRCON_PARSER_WAIT_PAYLOAD
} xrcon_parser_state_t;

typedef struct
{
	uint32_t type;
	uint32_t version;
	uint16_t length;
	uint16_t handle;
} xrcon_frame_header_t;

typedef struct
{
	netadr_t bindadr;
	SOCKET listen_socket;
	SOCKET client_socket;
	xrcon_state_t state;

	uint32_t tx_pos;
	uint32_t rx_pos;
	uint8_t tx_buffer[XRCON_TX_BUFFER_SIZE];
	uint8_t rx_buffer[XRCON_RX_BUFFER_SIZE];
	
	char print_buffer[XRCON_PRINT_BUFFER_SIZE];
	uint32_t print_pos;
	double print_flush_time;
	double retry_timeout;

	struct
	{
		xrcon_parser_state_t state;
		xrcon_frame_header_t frame;
	} parser;
} xrcon_t;

static xrcon_t xrcon;

static void XRcon_SetState( xrcon_state_t new_state )
{
	xrcon.state = new_state;
}

static void XRcon_CloseListenSocket( void )
{
	if( NET_IsSocketValid( xrcon.listen_socket ))
	{
		closesocket( xrcon.listen_socket );
		xrcon.listen_socket = INVALID_SOCKET;
	}
}

static void XRcon_CloseClientSocket( void )
{
	if( NET_IsSocketValid( xrcon.client_socket ))
	{
		closesocket( xrcon.client_socket );
		xrcon.client_socket = INVALID_SOCKET;

		return true;
	}

	return false;
}

static void XRcon_DisconnectClient( void )
{
	xrcon.print_pos = 0;
	xrcon.tx_pos = 0;
	xrcon.rx_pos = 0;
	xrcon.print_buffer[0] = '\0';
	xrcon.parser.state = XRCON_PARSER_WAIT_HEADER;

	qboolean was_active = XRcon_CloseClientSocket();
	XRcon_SetState( XRCON_STATE_LISTENING );

	if( was_active )
		Con_Printf( S_NOTE "%s: client disconnected\n", __func__ );
}

static qboolean XRcon_SendPacket( uint32_t type, const void *body, size_t body_len )
{
	size_t total_len = XRCON_HEADER_SIZE + body_len;
	uint8_t frame[XRCON_MAX_PACKET_SIZE];

	if( total_len > sizeof( frame ))
	{
		Con_Printf( S_WARN "%s: packet too large (%zu > %zu)\n", __func__, total_len, sizeof( frame ));
		return false;
	}

	sizebuf_t sb;
	MSG_Init( &sb, __func__, frame, sizeof( frame ));
	MSG_WriteBytes( &sb, &type, 4 );
	MSG_WriteDword( &sb, htonl( XRCON_CMND_VERSION ));
	MSG_WriteWord( &sb, htons((short)total_len ));
	MSG_WriteWord( &sb, 0 ); // handle

	if( body && body_len > 0 )
		MSG_WriteBytes( &sb, body, body_len );

	size_t frame_size = MSG_GetRealBytesWritten( &sb );
	int sent = send( xrcon.client_socket, (const char *)frame, frame_size, 0 );
	if( NET_IsSocketError( sent ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY )
		{
			const char *errstr = NET_ErrorString();
			XRcon_DisconnectClient();
			Con_Printf( S_ERROR "%s: send error %s\n", __func__, errstr );
			return false;
		}
		sent = 0;
	}

	size_t unsent = frame_size - sent;
	if( unsent > 0 )
	{
		size_t available = sizeof( xrcon.tx_buffer ) - xrcon.tx_pos;
		if( available < unsent )
		{
			XRcon_DisconnectClient();
			Con_Printf( S_ERROR "%s: transmit buffer overflow\n", __func__ );
			return false;
		}

		memcpy( xrcon.tx_buffer + xrcon.tx_pos, frame + sent, unsent );
		xrcon.tx_pos += unsent;
	}

	return true;
}

static void XRcon_SendCHAN( void )
{
	sizebuf_t sb;
	uint8_t body[256];

	MSG_Init( &sb, __func__, body, sizeof( body ));
	MSG_WriteWord( &sb, htons( 1 )); // channels count
	MSG_WriteDword( &sb, 0 ); // id
	MSG_WriteDword( &sb, 0 ); // unknown1
	MSG_WriteDword( &sb, 0 ); // unknown2
	MSG_WriteDword( &sb, htonl( 5 )); // verbosity_default
	MSG_WriteDword( &sb, htonl( 5 )); // verbosity_current

	// RGBA = white
	MSG_WriteDword( &sb, 0xFFFFFFFF );

	const char *channel_name = "Console";
	MSG_WriteString( &sb, channel_name );

	size_t name_padding = XRCON_CHAN_NAME_SIZE - Q_strlen( channel_name ) - 1;
	for( size_t i = 0; i < name_padding; i++ )
		MSG_WriteByte( &sb, 0 );

	XRcon_SendPacket( XRCON_TYPE_CHAN, body, MSG_GetRealBytesWritten( &sb ));
}

static void XRcon_SendAINF( void )
{
	uint8_t body[XRCON_AINF_PACKET_SIZE] = { 0 };
	XRcon_SendPacket( XRCON_TYPE_AINF, body, sizeof( body ));
}

static void XRcon_SendADON( const char *name )
{
	sizebuf_t sb;
	uint8_t body[256];
	size_t len = Q_strlen( name );

	MSG_Init( &sb, __func__, body, sizeof( body ));
	MSG_WriteWord( &sb, htons( 0 ));
	MSG_WriteWord( &sb, htons( len ));
	MSG_WriteBytes( &sb, name, len );
	XRcon_SendPacket( XRCON_TYPE_ADON, body, MSG_GetRealBytesWritten( &sb ));
}

static void XRcon_HandleCMND( const char *command )
{
	Con_Printf( S_NOTE "XRcon command: %s\n", command );
	Cbuf_AddText( command );
	Cbuf_AddText( "\n" );
}

static qboolean XRcon_UpdateBindAddress( void )
{
	if( NET_NetadrType( &xrcon.bindadr ) == NA_UNDEFINED )
	{
		if( !NET_StringToAdr( xrcon_address.string, &xrcon.bindadr ))
			return false;
	}
	return true;
}

static void XRcon_StartListening( void )
{
	int addr_family;
	struct sockaddr_storage addr = { 0 };

	if( !XRcon_UpdateBindAddress( ))
	{
		Con_Printf( S_ERROR "%s: invalid address \"%s\"\n", __func__, xrcon_address.string );
		return;
	}

	if( NET_NetadrType( &xrcon.bindadr ) == NA_IP )
		addr_family = AF_INET;
	else if( NET_NetadrType( &xrcon.bindadr ) == NA_IP6 )
		addr_family = AF_INET6;
	else
	{
		Con_Printf( S_ERROR "%s: unsupported address type for %s\n", __func__, xrcon_address.string );
		return;
	}

	xrcon.listen_socket = socket( addr_family, SOCK_STREAM, IPPROTO_TCP );
	if( !NET_IsSocketValid( xrcon.listen_socket ))
	{
		Con_Printf( S_ERROR "%s: failed to create listen socket\n", __func__ );
		return;
	}

	if( !NET_MakeSocketNonBlocking( xrcon.listen_socket ))
	{
		Con_Printf( S_ERROR "%s: failed to set non-blocking mode, error %s\n", __func__, NET_ErrorString( ));
		XRcon_CloseListenSocket();
		return;
	}

	NET_MakeSocketReuseAddr( xrcon.listen_socket );
	NET_NetadrToSockadr( &xrcon.bindadr, &addr );

	if( bind( xrcon.listen_socket, (struct sockaddr *)&addr, NET_SockAddrLen( &addr )) == SOCKET_ERROR )
	{
		Con_Printf( S_ERROR "%s: bind to %s failed, error %s\n", __func__, xrcon_address.string, NET_ErrorString( ));
		XRcon_CloseListenSocket();
		return;
	}

	if( listen( xrcon.listen_socket, 1 ) == SOCKET_ERROR )
	{
		Con_Printf( S_ERROR "%s: listen failed, error %s\n", __func__, NET_ErrorString( ));
		XRcon_CloseListenSocket();
		return;
	}

	Con_Printf( S_NOTE "%s: started listening on %s\n", __func__, xrcon_address.string );
	XRcon_SetState( XRCON_STATE_LISTENING );
}

static void XRcon_ProcessRxData( void )
{
	while( true )
	{
		if( xrcon.parser.state == XRCON_PARSER_WAIT_HEADER )
		{
			if( xrcon.rx_pos < XRCON_HEADER_SIZE )
				return;

			sizebuf_t sb;
			uint32_t type = 0;

			MSG_Init( &sb, __func__, xrcon.rx_buffer, xrcon.rx_pos );
			MSG_ReadBytes( &sb, &type, sizeof( type ), 4 );
			uint32_t version = ntohl( MSG_ReadDword( &sb ));
			uint16_t total_len = ntohs( MSG_ReadWord( &sb ));
			uint16_t handle = ntohs( MSG_ReadWord( &sb ));

			if( total_len <= XRCON_HEADER_SIZE || total_len > sizeof( xrcon.rx_buffer ))
			{
				Con_Printf( S_WARN "%s: invalid frame size %u, disconnecting\n", __func__, total_len );
				XRcon_DisconnectClient();
				return;
			}

			if( version != XRCON_CMND_VERSION && version != XRCON_CMND_VERSION_CS2RC )
				Con_Printf( S_WARN "%s: unexpected version 0x%08X\n", __func__, version );

			xrcon.parser.frame.type = type;
			xrcon.parser.frame.version = version;
			xrcon.parser.frame.length = total_len;
			xrcon.parser.frame.handle = handle;
			xrcon.parser.state = XRCON_PARSER_WAIT_PAYLOAD;

			size_t bytes_read = MSG_GetNumBytesRead( &sb );
			memmove( xrcon.rx_buffer, xrcon.rx_buffer + bytes_read, xrcon.rx_pos - bytes_read );
			xrcon.rx_pos -= bytes_read;
		}
		else if( xrcon.parser.state == XRCON_PARSER_WAIT_PAYLOAD )
		{
			size_t payload_length = xrcon.parser.frame.length - XRCON_HEADER_SIZE;
			if( xrcon.rx_pos < payload_length )
				return;

			if( xrcon.parser.frame.type == XRCON_TYPE_CMND )
			{
				char cmd[XRCON_MAX_FRAME_SIZE];
				size_t cmd_len = Q_min( payload_length, sizeof( cmd ) - 1 );
				memcpy( cmd, xrcon.rx_buffer, cmd_len );
				cmd[cmd_len] = '\0';
				XRcon_HandleCMND( cmd );
			}
			else
			{
				Con_Printf( S_WARN "%s: unknown message type 0x%08x\n", __func__, xrcon.parser.frame.type );
			}

			xrcon.parser.state = XRCON_PARSER_WAIT_HEADER;
			memmove( xrcon.rx_buffer, xrcon.rx_buffer + payload_length, xrcon.rx_pos - payload_length );
			xrcon.rx_pos -= payload_length;
		}
	}
}

static void XRcon_HandleDataTx( void )
{
	if( xrcon.tx_pos == 0 )
		return;

	int sent = send( xrcon.client_socket, (const char *)xrcon.tx_buffer, xrcon.tx_pos, 0 );
	if( NET_IsSocketError( sent ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY )
			XRcon_DisconnectClient();

		return;
	}

	if( sent > 0 )
	{
		memmove( xrcon.tx_buffer, xrcon.tx_buffer + sent, xrcon.tx_pos - sent );
		xrcon.tx_pos -= sent;
	}
}

static void XRcon_HandleDataRx( void )
{
	int available = sizeof( xrcon.rx_buffer ) - xrcon.rx_pos;
	if( available <= 0 )
	{
		Con_Printf( S_ERROR "%s: receive buffer overflow\n", __func__ );
		XRcon_DisconnectClient();
		return;
	}

	int received = recv( xrcon.client_socket, (char *)xrcon.rx_buffer + xrcon.rx_pos, available, 0 );
	if( NET_IsSocketError( received ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY )
		{ 
			XRcon_DisconnectClient();
		}
		return;
	}

	if( received == 0 )
	{
		XRcon_DisconnectClient();
		return;
	}

	xrcon.rx_pos += received;
	XRcon_ProcessRxData();
}

static void XRcon_FlushPrintBuffer( void )
{
	if( xrcon.print_pos == 0 )
		return;

	sizebuf_t sb;
	uint8_t body[XRCON_PRNT_HEADER_SIZE + XRCON_PRINT_BUFFER_SIZE];
	
	MSG_Init( &sb, __func__, body, sizeof( body ));
	MSG_WriteDword( &sb, 0 ); // channel_id = 0 (Console)

	// padding 20 bytes
	for( int i = 0; i < 5; i++ )
		MSG_WriteDword( &sb, 0 );

	// RGBA = white
	MSG_WriteDword( &sb, 0xFFFFFFFF );

	MSG_WriteBytes( &sb, xrcon.print_buffer, xrcon.print_pos );
	MSG_WriteByte( &sb, 0 );
	XRcon_SendPacket( XRCON_TYPE_PRNT, body, MSG_GetRealBytesWritten( &sb ));

	xrcon.print_pos = 0;
	xrcon.print_buffer[0] = '\0';
}

static void XRcon_UpdateListening( void )
{
	fd_set readfds;
	struct timeval tv = { 0 };

	FD_ZERO( &readfds );
	FD_SET( xrcon.listen_socket, &readfds );

#if XASH_WIN32
	int result = select( 0, &readfds, NULL, NULL, &tv );
#else
	int result = select( xrcon.listen_socket + 1, &readfds, NULL, NULL, &tv );
#endif
	if( NET_IsSocketError( result ))
	{
		Con_Printf( S_ERROR "%s: select() failed\n", __func__ );
		return;
	}

	if( !FD_ISSET( xrcon.listen_socket, &readfds ))
		return;

	struct sockaddr_storage client_addr;
	socklen_t addr_len = sizeof( client_addr );
	SOCKET client = accept( xrcon.listen_socket, (struct sockaddr *)&client_addr, &addr_len );

	if( !NET_IsSocketValid( client ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEINPROGRESS )
			Con_Printf( S_ERROR "%s: accept() failed, error %s\n", __func__, NET_ErrorString( ));

		return;
	}

	if( !NET_MakeSocketNonBlocking( client ))
	{
		Con_Printf( S_ERROR "%s: failed to set client non-blocking\n", __func__ );
		closesocket( client );
		return;
	}

	xrcon.client_socket = client;
	xrcon.print_pos = 0;
	xrcon.print_buffer[0] = '\0';
	xrcon.tx_pos = 0;
	xrcon.rx_pos = 0;
	xrcon.parser.state = XRCON_PARSER_WAIT_HEADER;
	xrcon.print_flush_time = Platform_DoubleTime() + xrcon_flush_interval.value;

	netadr_t adr;
	NET_SockadrToNetadr( &client_addr, &adr );
	Con_Printf( S_NOTE "%s: connected client %s\n", __func__, NET_AdrToString( adr ));

	XRcon_SendAINF();
	XRcon_SendADON( "HLDS" );
	XRcon_SendCHAN();
	XRcon_SetState( XRCON_STATE_CONNECTED );
}

static void XRcon_UpdateConnected( void )
{
	if( !NET_IsSocketValid( xrcon.client_socket ))
	{
		XRcon_DisconnectClient();
		return;
	}

	XRcon_HandleDataTx();
	XRcon_HandleDataRx();

	if( xrcon.print_pos > 0 && Platform_DoubleTime() >= xrcon.print_flush_time )
	{
		XRcon_FlushPrintBuffer();
		xrcon.print_flush_time = Platform_DoubleTime() + xrcon_flush_interval.value;
	}
}

static void XRcon_UpdateIdle( void )
{
	if( xrcon.retry_timeout > Platform_DoubleTime( ))
		return;

	XRcon_StartListening();
	xrcon.retry_timeout = Platform_DoubleTime() + xrcon_retry_delay.value;
}

void XRcon_Print( const char *msg )
{
	if( xrcon.state != XRCON_STATE_CONNECTED )
		return;

	if( !msg )
		return;

	while( *msg )
	{
		const char *p = Q_strchrnul( msg, '\n' );
		size_t length = p - msg;

		if( xrcon.print_pos + length < sizeof( xrcon.print_buffer ) - 1 )
		{
			memcpy( xrcon.print_buffer + xrcon.print_pos, msg, length );
			xrcon.print_pos += length;
		}

		if( *p == '\0' )
			return;

		if( xrcon.print_pos > 0 )
		{
			XRcon_FlushPrintBuffer();
			xrcon.print_flush_time = Platform_DoubleTime() + xrcon_flush_interval.value;
		}
		msg = p + 1;
	}
}

static void XRcon_Terminate( void )
{
	XRcon_DisconnectClient();
	XRcon_CloseListenSocket();
	xrcon.retry_timeout = 0;
	NET_NetadrSetType( &xrcon.bindadr, NA_UNDEFINED );
}

void XRcon_Frame( void )
{
	if( !xrcon_enable.value )
	{
		if( xrcon.state != XRCON_STATE_IDLE )
		{
			XRcon_Terminate();
			XRcon_SetState( XRCON_STATE_IDLE );
		}
		return;
	}

	if( FBitSet( xrcon_address.flags, FCVAR_CHANGED ))
	{
		ClearBits( xrcon_address.flags, FCVAR_CHANGED );
		XRcon_Terminate();
		XRcon_SetState( XRCON_STATE_IDLE );
	}

	switch( xrcon.state )
	{
	case XRCON_STATE_IDLE:
		XRcon_UpdateIdle();
		break;
	case XRCON_STATE_LISTENING:
		XRcon_UpdateListening();
		break;
	case XRCON_STATE_CONNECTED:
		XRcon_UpdateConnected();
		break;
	}
}

void XRcon_Init( void )
{
	xrcon.state = XRCON_STATE_IDLE;
	xrcon.listen_socket = INVALID_SOCKET;
	xrcon.client_socket = INVALID_SOCKET;
	xrcon.rx_pos = 0;
	xrcon.tx_pos = 0;
	xrcon.parser.state = XRCON_PARSER_WAIT_HEADER;
	xrcon.print_pos = 0;
	xrcon.print_buffer[0] = '\0';
	xrcon.retry_timeout = 0;

	Cvar_RegisterVariable( &xrcon_enable );
	Cvar_RegisterVariable( &xrcon_address );
	Cvar_RegisterVariable( &xrcon_flush_interval );
	Cvar_RegisterVariable( &xrcon_retry_delay );
	NET_NetadrSetType( &xrcon.bindadr, NA_UNDEFINED );
}

void XRcon_Shutdown( void )
{
	XRcon_DisconnectClient();
	XRcon_CloseListenSocket();
	XRcon_SetState( XRCON_STATE_IDLE );
}

qboolean XRcon_IsActive( void )
{
	return xrcon.state == XRCON_STATE_CONNECTED;
}
