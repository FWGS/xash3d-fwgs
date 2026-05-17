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

#define XRCON_HEADER_SIZE		12
#define XRCON_MAX_FRAME_SIZE	4096
#define XRCON_PRINT_BUFFER_SIZE	4096
#define XRCON_TX_BUFFER_SIZE	(XRCON_MAX_FRAME_SIZE * 4)
#define XRCON_CMND_VERSION		0x00D40000
#define XRCON_CHAN_RECORD_SIZE	58
#define XRCON_MAX_PACKET_SIZE	(XRCON_HEADER_SIZE + 4 + 24 + XRCON_PRINT_BUFFER_SIZE)
#define XRCON_FLUSH_INTERVAL	0.05
#define XRCON_RETRY_DELAY		5.0

static CVAR_DEFINE_AUTO( xrcon_enable, "0", FCVAR_PRIVILEGED, "enable XRCON server" );
static CVAR_DEFINE_AUTO( xrcon_address, "127.0.0.1:27000", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "XRCON server bind address and port" );

typedef enum
{
	XRCON_STATE_IDLE,
	XRCON_STATE_LISTENING,
	XRCON_STATE_CONNECTED,
	XRCON_STATE_DISCONNECTING
} xrcon_state_t;

typedef struct
{
	netadr_t bindadr;
	int listen_socket;
	int client_socket;
	xrcon_state_t state;

	uint32_t tx_pos;
	uint32_t rx_pos;
	uint8_t tx_buffer[XRCON_TX_BUFFER_SIZE];
	uint8_t rx_buffer[XRCON_MAX_FRAME_SIZE + 64];
	
	char print_buffer[XRCON_PRINT_BUFFER_SIZE];
	uint32_t print_pos;
	double print_flush_time;
	double retry_timeout;
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
	}
}

static void XRcon_DisconnectClient( void )
{
    if( xrcon.state == XRCON_STATE_CONNECTED )
        Con_Printf( S_NOTE "%s: client disconnected\n", __func__ );

	XRcon_CloseClientSocket();
	xrcon.print_pos = 0;
	xrcon.tx_pos = 0;
	xrcon.rx_pos = 0;
	xrcon.print_buffer[0] = '\0';
}

static qboolean XRcon_SendPacket( const char *type, const void *body, size_t body_len )
{
	uint16_t total_len = XRCON_HEADER_SIZE + body_len;
	uint8_t frame[XRCON_MAX_PACKET_SIZE];
	sizebuf_t sb;

	if( total_len > sizeof( frame ))
	{
		Con_Printf( S_WARN "%s: packet too large (%u > %zu)\n", __func__, total_len, sizeof( frame ));
		return false;
	}

	MSG_Init( &sb, "XRcon_SendPacket", frame, sizeof( frame ));
	MSG_WriteBytes( &sb, type, 4 );
	MSG_WriteDword( &sb, htonl( XRCON_CMND_VERSION ));
	MSG_WriteWord( &sb, htons( total_len ));
	MSG_WriteWord( &sb, 0 );

	if( body && body_len > 0 )
		MSG_WriteBytes( &sb, body, body_len );

	size_t frame_size = MSG_GetRealBytesWritten( &sb );
	int sent = send( xrcon.client_socket, (const char *)frame, frame_size, 0 );
	if( NET_IsSocketError( sent ))
	{
		int err = WSAGetLastError();
		if( err != WSAEWOULDBLOCK && err != WSAEALREADY )
		{
			Con_Printf( S_ERROR "%s: send error %s\n", __func__, NET_ErrorString( ));
			XRcon_DisconnectClient();
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
			Con_Printf( S_ERROR "%s: transmit buffer overflow\n", __func__ );
			XRcon_DisconnectClient();
			return false;
		}

		memcpy( xrcon.tx_buffer + xrcon.tx_pos, frame + sent, unsent );
		xrcon.tx_pos += unsent;
	}

	return true;
}

static void XRcon_SendChan( void )
{
	uint8_t body[2 + XRCON_CHAN_RECORD_SIZE];
	sizebuf_t sb;
	char chan_name[34];

	MSG_Init( &sb, "XRcon_SendChan", body, sizeof( body ));

	MSG_WriteWord( &sb, htons( 1 ));

	MSG_WriteDword( &sb, 0 ); // id
	MSG_WriteDword( &sb, 0 ); // unknown1
	MSG_WriteDword( &sb, 0 ); // unknown2
	MSG_WriteDword( &sb, htonl( 5 )); // verbosity_default
	MSG_WriteDword( &sb, htonl( 5 )); // verbosity_current

	// RGBA = white
	MSG_WriteByte( &sb, 255 );
	MSG_WriteByte( &sb, 255 );
	MSG_WriteByte( &sb, 255 );
	MSG_WriteByte( &sb, 255 );

	Q_strncpy( chan_name, "Console", sizeof( chan_name ));
	MSG_WriteBytes( &sb, chan_name, sizeof( chan_name ));

	XRcon_SendPacket( "CHAN", body, MSG_GetRealBytesWritten( &sb ));
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

static qboolean XRcon_ProcessFrame( void )
{
	if( xrcon.rx_pos < XRCON_HEADER_SIZE )
		return false;

	sizebuf_t sb;
	char type[5];
	uint16_t total_len, handle;
	uint32_t version;

	MSG_Init( &sb, "XRcon_ProcessFrame", xrcon.rx_buffer, xrcon.rx_pos );
	MSG_ReadBytes( &sb, type, 4 );
	type[4] = '\0';
	version = ntohl( MSG_ReadDword( &sb ));
	total_len = ntohs( MSG_ReadWord( &sb ));
	handle = ntohs( MSG_ReadWord( &sb )); // how to make sure that compiler does not prune this?

	if( version != XRCON_CMND_VERSION )
		Con_Printf( S_WARN "%s: unexpected version 0x%08X\n", __func__, version );

	if( total_len < XRCON_HEADER_SIZE || xrcon.rx_pos < total_len )
		return false;

	uint16_t body_len = total_len - XRCON_HEADER_SIZE;

	if( Q_strcmp( type, "CMND" ) == 0 && body_len > 0 )
	{
		char cmd[XRCON_MAX_FRAME_SIZE];
		size_t cmd_len = ( body_len < sizeof( cmd ) - 1 ) ? body_len : sizeof( cmd ) - 1;

		MSG_ReadBytes( &sb, cmd, cmd_len );
		cmd[cmd_len] = '\0';

		// strip embedded NUL bytes from command string
		{
			uint16_t wr = 0;
			for( uint16_t rd = 0; rd < cmd_len; rd++ )
			{
				if( cmd[rd] != '\0' )
					cmd[wr++] = cmd[rd];
			}
			cmd[wr] = '\0';
		}

		if( cmd[0] != '\0' )
		{
			Con_Printf( S_NOTE "XRcon command: %s\n", cmd );
			Cbuf_AddText( cmd );
			Cbuf_AddText( "\n" );
		}
	}

	// remove processed frame from buffer
	uint32_t frame_size = XRCON_HEADER_SIZE + body_len;
	if( frame_size > xrcon.rx_pos )
		frame_size = xrcon.rx_pos;

	memmove( xrcon.rx_buffer, xrcon.rx_buffer + frame_size, xrcon.rx_pos - frame_size );
	xrcon.rx_pos -= frame_size;

	return true;
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
			XRcon_DisconnectClient();
		return;
	}

	if( received == 0 )
	{
		XRcon_DisconnectClient();
		return;
	}

	xrcon.rx_pos += received;

	while( XRcon_ProcessFrame( ));
}

static void XRcon_FlushPrintBuffer( void )
{
	if( xrcon.print_pos == 0 )
		return;

	uint8_t body[4 + 24 + XRCON_PRINT_BUFFER_SIZE];
	sizebuf_t sb;

	MSG_Init( &sb, "XRcon_FlushPrintBuffer", body, sizeof( body ));
	MSG_WriteDword( &sb, 0 ); // channel_id = 0 (Console)

	// padding 24 bytes
	for( int pad = 0; pad < 24; pad++ )
		MSG_WriteByte( &sb, 0 );

	MSG_WriteBytes( &sb, xrcon.print_buffer, xrcon.print_pos );
	XRcon_SendPacket( "PRNT", body, MSG_GetRealBytesWritten( &sb ));

	xrcon.print_pos = 0;
	xrcon.print_buffer[0] = '\0';
}

static void XRcon_UpdateListening( void )
{
	fd_set readfds;
	struct timeval tv;
	struct sockaddr_storage client_addr;
	socklen_t addr_len = sizeof( client_addr );

	FD_ZERO( &readfds );
	FD_SET( xrcon.listen_socket, &readfds );

	tv.tv_sec = 0;
	tv.tv_usec = 0;

#if XASH_WIN32
	int result = select( 0, &readfds, NULL, NULL, &tv );
#else
	int result = select( xrcon.listen_socket + 1, &readfds, NULL, NULL, &tv );
#endif
	if( result == SOCKET_ERROR )
	{
		Con_Printf( S_ERROR "%s: select() failed\n", __func__ );
		return;
	}

	if( FD_ISSET( xrcon.listen_socket, &readfds ))
	{
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

		xrcon.client_socket = (int)client;
		xrcon.print_pos = 0;
		xrcon.print_buffer[0] = '\0';
		xrcon.tx_pos = 0;
		xrcon.rx_pos = 0;
		xrcon.print_flush_time = Platform_DoubleTime() + XRCON_FLUSH_INTERVAL;

		netadr_t adr;
		NET_SockadrToNetadr( &client_addr, &adr );
		Con_Printf( S_NOTE "%s: connected client %s\n", __func__, NET_AdrToString( adr ));

		XRcon_SendChan();
		XRcon_SetState( XRCON_STATE_CONNECTED );
	}
}

static void XRcon_UpdateConnected( void )
{
	if( !NET_IsSocketValid( xrcon.client_socket ))
	{
		XRcon_SetState( XRCON_STATE_DISCONNECTING );
		return;
	}

	XRcon_HandleDataTx();
	XRcon_HandleDataRx();

	if( xrcon.print_pos > 0 && Platform_DoubleTime() >= xrcon.print_flush_time )
	{
		XRcon_FlushPrintBuffer();
		xrcon.print_flush_time = Platform_DoubleTime() + XRCON_FLUSH_INTERVAL;
	}
}

static void XRcon_UpdateDisconnecting( void )
{
	XRcon_DisconnectClient();
	XRcon_SetState( XRCON_STATE_LISTENING );
}

static void XRcon_UpdateIdle( void )
{
	if( xrcon.retry_timeout > Platform_DoubleTime( ))
		return;

	XRcon_StartListening();
	xrcon.retry_timeout = Platform_DoubleTime() + XRCON_RETRY_DELAY;
}

void XRcon_Print( const char *msg )
{
	if( xrcon.state != XRCON_STATE_CONNECTED )
		return;

	if( !msg || !*msg )
		return;

	size_t len = Q_strlen( msg );
	for( size_t i = 0; i < len; i++ )
	{
		if( msg[i] == '\n' || msg[i] == '\r' )
		{
			if( xrcon.print_pos > 0 )
			{
				XRcon_FlushPrintBuffer();
				xrcon.print_flush_time = Platform_DoubleTime() + XRCON_FLUSH_INTERVAL;
			}
		}
		else if( msg[i] == '^' && i + 1 < len )
		{
			char color_code = msg[i + 1];
			if( color_code >= '0' && color_code <= '9' )
				i++; // skip color code, client does not supports them at the moment
		}
		else if( msg[i] >= ' ' || msg[i] == '\t' )
		{
			if( xrcon.print_pos < sizeof( xrcon.print_buffer ) - 1 )
			{
				xrcon.print_buffer[xrcon.print_pos++] = msg[i];
				xrcon.print_buffer[xrcon.print_pos] = '\0';
			}
		}
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
	case XRCON_STATE_DISCONNECTING:
		XRcon_UpdateDisconnecting();
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
	xrcon.print_pos = 0;
	xrcon.print_buffer[0] = '\0';
	xrcon.retry_timeout = 0;

	Cvar_RegisterVariable( &xrcon_enable );
	Cvar_RegisterVariable( &xrcon_address );
	NET_NetadrSetType( &xrcon.bindadr, NA_UNDEFINED );
}

void XRcon_Shutdown( void )
{
	XRcon_DisconnectClient();
	XRcon_CloseListenSocket();
	XRcon_SetState( XRCON_STATE_IDLE );
}
