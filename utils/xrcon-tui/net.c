/*
net.c - XRCON TCP client for xrcon-tui
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "crtlib.h"
#include "text.h"
#include "net.h"

#define NET_CONNECT_TIMEOUT_MS  5000
#define NET_GREETING_TIMEOUT_MS 2000
#define NET_RECV_BURST          8 // reads per wakeup, keeps input responsive under console spam

static void Net_Message( net_t *net, int kind, const char *fmt, ... ) FORMAT_CHECK( 3 );

static void Net_Message( net_t *net, int kind, const char *fmt, ... )
{
	char msg[512];
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, ap );
	va_end( ap );

	if( net->on_message )
		net->on_message( net->user, kind, msg );
}

static void Net_SetError( net_t *net, int code )
{
	Q_strncpy( net->last_error, Sys_ErrorString( code ), sizeof( net->last_error ));
}

static void Net_CloseSocket( net_t *net )
{
	if( net->sock != SYS_INVALID_SOCKET )
	{
		Sys_SocketClose( net->sock );
		net->sock = SYS_INVALID_SOCKET;
	}
}

static void Net_FreeAddrs( net_t *net )
{
	if( net->addrs )
		freeaddrinfo( net->addrs );

	net->addrs = NULL;
	net->addr_next = NULL;
}

void Net_Init( net_t *net, net_print_fn on_print, net_message_fn on_message, void *user )
{
	memset( net, 0, sizeof( *net ));
	net->sock = SYS_INVALID_SOCKET;
	net->state = NET_OFFLINE;
	net->connect_timeout_ms = NET_CONNECT_TIMEOUT_MS;
	net->on_print = on_print;
	net->on_message = on_message;
	net->user = user;
}

void Net_Disconnect( net_t *net, const char *reason )
{
	if( reason && net->state != NET_OFFLINE )
	{
		Q_strncpy( net->last_error, reason, sizeof( net->last_error ));
		Net_Message( net, NET_MSG_ERROR, "disconnected from %s: %s", net->addr, reason );
	}

	Net_CloseSocket( net );
	Net_FreeAddrs( net );
	net->state = NET_OFFLINE;
	net->rx_len = 0;
	net->tx_len = 0;
	net->server_name[0] = '\0';
}

void Net_Shutdown( net_t *net )
{
	Net_Disconnect( net, NULL );
}

qboolean Net_IsUp( const net_t *net )
{
	return net->state == NET_CONNECTED || net->state == NET_ONLINE;
}

qboolean Net_WantsWrite( const net_t *net )
{
	if( net->state == NET_CONNECTING )
		return true;

	return Net_IsUp( net ) && net->tx_len > 0;
}

const char *Net_StateString( const net_t *net )
{
	switch( net->state )
	{
	case NET_OFFLINE: return "disconnected";
	case NET_CONNECTING: return "connecting";
	case NET_CONNECTED: return "waiting";
	case NET_ONLINE: return "connected";
	}

	return "unknown";
}

static void Net_FormatAddr( const struct addrinfo *ai, char *out, size_t size )
{
	char host[64], serv[16];

	if( getnameinfo( ai->ai_addr, (socklen_t)ai->ai_addrlen, host, sizeof( host ), serv, sizeof( serv ), NI_NUMERICHOST | NI_NUMERICSERV ) != 0 )
	{
		Q_strncpy( out, "?", size );
		return;
	}

	if( ai->ai_family == AF_INET6 )
		Q_snprintf( out, size, "[%s]:%s", host, serv );
	else
		Q_snprintf( out, size, "%s:%s", host, serv );
}

static void Net_Flush( net_t *net )
{
	size_t sent = 0;

	while( sent < net->tx_len )
	{
		int n = Sys_SocketSend( net->sock, net->tx + sent, net->tx_len - sent );

		if( n == SYS_SOCK_AGAIN || n == 0 )
			break;

		if( n < 0 )
		{
			Net_Disconnect( net, Sys_ErrorString( Sys_SocketError()));
			return;
		}

		sent += n;
	}

	memmove( net->tx, net->tx + sent, net->tx_len - sent );
	net->tx_len -= sent;
}

static void Net_OnConnected( net_t *net, int64_t now )
{
	Net_FreeAddrs( net );
	net->state = NET_CONNECTED;
	net->deadline = now + NET_GREETING_TIMEOUT_MS;
	net->greet_warned = false;
	net->last_error[0] = '\0';
	Net_Message( net, NET_MSG_INFO, "connected to %s", net->addr );
	Net_Flush( net );
}

// a host name may resolve to several addresses, e.g. localhost to ::1 and 127.0.0.1,
// while the engine binds only one of them, so walk the list until something answers
static qboolean Net_TryNext( net_t *net, int64_t now )
{
	while( net->addr_next )
	{
		const struct addrinfo *ai = net->addr_next;

		net->addr_next = ai->ai_next;
		Net_FormatAddr( ai, net->addr, sizeof( net->addr ));

		net->sock = Sys_SocketOpen( ai->ai_family );
		if( net->sock == SYS_INVALID_SOCKET )
		{
			Net_SetError( net, Sys_SocketError());
			continue;
		}

		int rc = Sys_SocketConnect( net->sock, ai->ai_addr, ai->ai_addrlen );
		if( rc == SYS_SOCK_ERROR )
		{
			Net_SetError( net, Sys_SocketError());
			Net_CloseSocket( net );
			continue;
		}

		net->state = NET_CONNECTING;
		net->deadline = now + net->connect_timeout_ms;

		if( rc == 0 )
			Net_OnConnected( net, now );

		return true;
	}

	Net_FreeAddrs( net );
	net->state = NET_OFFLINE;

	if( !net->quiet )
		Net_Message( net, NET_MSG_ERROR, "can't connect to %s: %s", net->addr, net->last_error );

	return false;
}

qboolean Net_Connect( net_t *net, const char *host, int port, qboolean quiet, int64_t now )
{
	struct addrinfo hints;
	char port_str[16];

	Net_Disconnect( net, NULL );
	net->quiet = quiet;
	net->last_error[0] = '\0';
	Q_strncpy( net->host, host, sizeof( net->host ));
	net->port = port;
	Q_snprintf( net->addr, sizeof( net->addr ), "%s:%d", host, port );

	memset( &hints, 0, sizeof( hints ));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	Q_snprintf( port_str, sizeof( port_str ), "%d", port );

	int rc = getaddrinfo( host, port_str, &hints, &net->addrs );
	if( rc != 0 )
	{
		net->addrs = NULL;
		Q_strncpy( net->last_error, Sys_ResolveErrorString( rc ), sizeof( net->last_error ));

		if( !quiet )
			Net_Message( net, NET_MSG_ERROR, "can't resolve %s: %s", host, net->last_error );

		return false;
	}

	net->addr_next = net->addrs;
	return Net_TryNext( net, now );
}

void Net_Frame( net_t *net, int64_t now )
{
	if( net->state == NET_CONNECTING && now >= net->deadline )
	{
		Q_strncpy( net->last_error, "connection timed out", sizeof( net->last_error ));
		Net_CloseSocket( net );
		Net_TryNext( net, now );
	}
	else if( net->state == NET_CONNECTED && !net->greet_warned && now >= net->deadline )
	{
		net->greet_warned = true;
		Net_Message( net, NET_MSG_INFO, "waiting for the server to accept the connection, it serves one XRCON client at a time" );
	}
}

int Net_Timeout( const net_t *net, int64_t now, int timeout_ms )
{
	if( net->state == NET_CONNECTING || ( net->state == NET_CONNECTED && !net->greet_warned ))
	{
		int64_t left = net->deadline - now;

		if( left < 0 )
			left = 0;

		if( left < timeout_ms )
			timeout_ms = (int)left;
	}

	return timeout_ms;
}

static void Net_HandlePRNT( net_t *net, const uint8_t *body, size_t len )
{
	if( len < XRCON_PRNT_HEADER_SIZE )
		return;

	const char *text = (const char *)body + XRCON_PRNT_HEADER_SIZE;
	len -= XRCON_PRNT_HEADER_SIZE;

	while( len > 0 && text[len - 1] == '\0' )
		len--;

	if( len > 0 && net->on_print )
		net->on_print( net->user, text, len );
}

static void Net_HandleADON( net_t *net, const uint8_t *body, size_t len )
{
	if( len < 4 )
		return;

	uint16_t be_len;

	memcpy( &be_len, body + 2, sizeof( be_len ));

	size_t name_len = BigShort( be_len );
	if( name_len > len - 4 )
		name_len = len - 4;

	Text_Sanitize( net->server_name, sizeof( net->server_name ), (const char *)body + 4, name_len );
}

static void Net_HandleFrame( net_t *net, uint32_t type, const uint8_t *body, size_t len )
{
	// the engine greets with AINF, ADON and CHAN right after accept()
	if( net->state == NET_CONNECTED )
	{
		net->state = NET_ONLINE;

		if( net->greet_warned )
			Net_Message( net, NET_MSG_INFO, "the server accepted the connection" );
	}

	if( type == XRCON_TYPE_PRNT )
		Net_HandlePRNT( net, body, len );
	else if( type == XRCON_TYPE_ADON )
		Net_HandleADON( net, body, len );
}

static void Net_Parse( net_t *net )
{
	size_t pos = 0;

	while( net->rx_len - pos >= XRCON_HEADER_SIZE )
	{
		const uint8_t *frame = net->rx + pos;
		xrcon_header_t hdr;

		XRcon_ReadHeader( frame, &hdr );

		if( hdr.length < XRCON_HEADER_SIZE )
		{
			Net_Disconnect( net, "malformed frame" );
			return;
		}

		if( net->rx_len - pos < hdr.length )
			break;

		Net_HandleFrame( net, hdr.type, frame + XRCON_HEADER_SIZE, hdr.length - XRCON_HEADER_SIZE );
		pos += hdr.length;
	}

	memmove( net->rx, net->rx + pos, net->rx_len - pos );
	net->rx_len -= pos;
}

void Net_OnReadable( net_t *net )
{
	for( int i = 0; i < NET_RECV_BURST && Net_IsUp( net ); i++ )
	{
		if( net->rx_len >= sizeof( net->rx ))
		{
			Net_Disconnect( net, "receive buffer overflow" );
			return;
		}

		int n = Sys_SocketRecv( net->sock, net->rx + net->rx_len, sizeof( net->rx ) - net->rx_len );

		if( n == SYS_SOCK_AGAIN )
			return;

		if( n == 0 )
		{
			Net_Disconnect( net, "server closed the connection" );
			return;
		}

		if( n < 0 )
		{
			Net_Disconnect( net, Sys_ErrorString( Sys_SocketError()));
			return;
		}

		net->rx_len += n;
		Net_Parse( net );
	}
}

void Net_OnWritable( net_t *net, int64_t now )
{
	if( net->state == NET_CONNECTING )
	{
		int err = Sys_SocketConnectResult( net->sock );

		if( err != 0 )
		{
			Net_SetError( net, err );
			Net_CloseSocket( net );
			Net_TryNext( net, now );
			return;
		}

		Net_OnConnected( net, now );
		return;
	}

	if( Net_IsUp( net ))
		Net_Flush( net );
}

qboolean Net_Send( net_t *net, const char *cmd, size_t len )
{
	if( !Net_IsUp( net ) || len > NET_MAX_COMMAND )
		return false;

	size_t total = XRCON_HEADER_SIZE + len + 1;
	if( net->tx_len + total > sizeof( net->tx ))
		return false;

	uint8_t *p = net->tx + net->tx_len;

	XRcon_WriteHeader( p, XRCON_TYPE_CMND, (uint16_t)total );
	memcpy( p + XRCON_HEADER_SIZE, cmd, len );
	p[XRCON_HEADER_SIZE + len] = '\0';
	net->tx_len += total;

	Net_Flush( net );
	return true;
}
