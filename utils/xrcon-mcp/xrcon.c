/*
xrcon.c - XRCON TCP client for xrcon-mcp
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
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "crtlib.h"
#include "xrcon.h"

static void XRcon_SetError( xrcon_t *x, const char *fmt, ... ) FORMAT_CHECK( 2 );
static void XRcon_SetError( xrcon_t *x, const char *fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	Q_vsnprintf( x->last_error, sizeof( x->last_error ), fmt, ap );
	va_end( ap );
	fprintf( stderr, "[xrcon] %s\n", x->last_error );
}

static void XRcon_CloseSocket( xrcon_t *x )
{
	if( x->sock >= 0 )
	{
		close( x->sock );
		x->sock = -1;
	}
}

static void XRcon_Reset( xrcon_t *x )
{
	XRcon_CloseSocket( x );
	x->rx_len = 0;
	x->tx_len = 0;
	x->parser = XRCON_PARSER_HEADER;
	memset( &x->hdr, 0, sizeof( x->hdr ));
	x->server_name[0] = '\0';
}

void XRcon_Init( xrcon_t *x, log_buf_t *log, xrcon_log_changed_fn cb, void *user )
{
	memset( x, 0, sizeof( *x ));
	x->sock = -1;
	x->state = XRCON_DISCONNECTED;
	x->log = log;
	x->on_log_changed = cb;
	x->user = user;
	x->parser = XRCON_PARSER_HEADER;
}

void XRcon_Free( xrcon_t *x )
{
	XRcon_CloseSocket( x );
}

int XRcon_Socket( const xrcon_t *x ) { return x->sock; }
int XRcon_IsConnected( const xrcon_t *x ) { return x->state == XRCON_CONNECTED; }
int XRcon_IsConnecting( const xrcon_t *x ) { return x->state == XRCON_CONNECTING; }

int XRcon_WantsRead( const xrcon_t *x )
{
	return x->state == XRCON_CONNECTED;
}

int XRcon_WantsWrite( const xrcon_t *x )
{
	if( x->state == XRCON_CONNECTING )
		return 1;
	if( x->state == XRCON_CONNECTED && x->tx_len > 0 )
		return 1;
	return 0;
}

const char *XRcon_StateString( const xrcon_t *x )
{
	switch( x->state )
	{
	case XRCON_DISCONNECTED: return "disconnected";
	case XRCON_CONNECTING:   return "connecting";
	case XRCON_CONNECTED:    return "connected";
	}
	return "unknown";
}

static int XRcon_SetNonblock( int fd )
{
	int flags = fcntl( fd, F_GETFL, 0 );
	if( flags < 0 )
		return -1;
	return fcntl( fd, F_SETFL, flags | O_NONBLOCK );
}

int XRcon_RequestConnect( xrcon_t *x, const char *host, int port )
{
	if( x->state != XRCON_DISCONNECTED )
	{
		XRcon_SetError( x, "already %s", XRcon_StateString( x ));
		return -1;
	}

	struct addrinfo hints;
	struct addrinfo *res = NULL;
	char port_str[16];

	memset( &hints, 0, sizeof( hints ));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICSERV;

	Q_snprintf( port_str, sizeof( port_str ), "%d", port );

	int rc = getaddrinfo( host, port_str, &hints, &res );
	if( rc != 0 )
	{
		XRcon_SetError( x, "getaddrinfo(%s:%d): %s", host, port, gai_strerror( rc ));
		return -1;
	}

	Q_strncpy( x->host, host, sizeof( x->host ));
	x->port = port;

	int fd = -1;
	for( struct addrinfo *it = res; it; it = it->ai_next )
	{
		fd = socket( it->ai_family, it->ai_socktype, it->ai_protocol );
		if( fd < 0 )
			continue;

		if( XRcon_SetNonblock( fd ) < 0 )
		{
			close( fd );
			fd = -1;
			continue;
		}

		int one = 1;
		setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof( one ));

		int cr = connect( fd, it->ai_addr, it->ai_addrlen );
		if( cr == 0 )
		{
			x->sock = fd;
			x->state = XRCON_CONNECTED;
			fprintf( stderr, "[xrcon] connected to %s:%d\n", host, port );
			freeaddrinfo( res );
			return 0;
		}
		if( errno == EINPROGRESS )
		{
			x->sock = fd;
			x->state = XRCON_CONNECTING;
			freeaddrinfo( res );
			return 0;
		}

		close( fd );
		fd = -1;
	}

	freeaddrinfo( res );
	XRcon_SetError( x, "connect failed: %s", strerror( errno ));
	return -1;
}

static void XRcon_OnConnectComplete( xrcon_t *x )
{
	int err = 0;
	socklen_t elen = sizeof( err );
	if( getsockopt( x->sock, SOL_SOCKET, SO_ERROR, &err, &elen ) < 0 || err != 0 )
	{
		XRcon_SetError( x, "connect: %s", strerror( err ? err : errno ));
		XRcon_Reset( x );
		x->state = XRCON_DISCONNECTED;
		return;
	}

	x->state = XRCON_CONNECTED;
	fprintf( stderr, "[xrcon] connected to %s:%d\n", x->host, x->port );
}

static void XRcon_Disconnect( xrcon_t *x, const char *why )
{
	if( x->state == XRCON_CONNECTED || x->state == XRCON_CONNECTING )
	{
		fprintf( stderr, "[xrcon] disconnected: %s\n", why ? why : "unknown" );
		char msg[160];
		Q_snprintf( msg, sizeof( msg ), "[xrcon-mcp] disconnected: %s\n", why ? why : "unknown" );
		LogBuf_AppendStr( x->log, msg );
		if( x->on_log_changed )
			x->on_log_changed( x->user );
	}
	XRcon_Reset( x );
	x->state = XRCON_DISCONNECTED;
}

void XRcon_RequestDisconnect( xrcon_t *x, const char *reason )
{
	XRcon_Disconnect( x, reason ? reason : "requested" );
}

static void XRcon_HandlePRNT( xrcon_t *x, const uint8_t *payload, size_t len )
{
	const size_t prnt_hdr = 28; // 4 channel_id + 20 padding + 4 RGBA
	if( len < prnt_hdr )
		return;

	const uint8_t *text = payload + prnt_hdr;
	size_t text_len = len - prnt_hdr;

	while( text_len > 0 && text[text_len - 1] == '\0' )
		text_len--;

	if( text_len == 0 )
		return;

	LogBuf_Append( x->log, (const char *)text, text_len );
	if( text[text_len - 1] != '\n' )
		LogBuf_Append( x->log, "\n", 1 );

	if( x->on_log_changed )
		x->on_log_changed( x->user );
}

static void XRcon_HandleADON( xrcon_t *x, const uint8_t *payload, size_t len )
{
	if( len < 4 )
		return;

	uint16_t name_len = ((uint16_t)payload[2] << 8) | payload[3];
	if( 4 + name_len > len )
		return;

	size_t copy = name_len;
	if( copy >= sizeof( x->server_name ))
		copy = sizeof( x->server_name ) - 1;
	memcpy( x->server_name, payload + 4, copy );
	x->server_name[copy] = '\0';
}

static int XRcon_ParseRx( xrcon_t *x )
{
	while( 1 )
	{
		if( x->parser == XRCON_PARSER_HEADER )
		{
			if( x->rx_len < XRCON_HEADER_SIZE )
				return 0;

			uint32_t version_be;
			uint16_t length_be, handle_be;
			memcpy( &x->hdr.type, x->rx + 0, sizeof( x->hdr.type ));
			memcpy( &version_be, x->rx + 4, sizeof( version_be ));
			memcpy( &length_be, x->rx + 8, sizeof( length_be ));
			memcpy( &handle_be, x->rx + 10, sizeof( handle_be ));
			x->hdr.version = ntohl( version_be );
			x->hdr.length = ntohs( length_be );
			x->hdr.handle = ntohs( handle_be );

			if( x->hdr.length < XRCON_HEADER_SIZE || x->hdr.length > XRCON_RX_BUFFER_SIZE )
			{
				XRcon_SetError( x, "bad frame length %u", x->hdr.length );
				return -1;
			}

			memmove( x->rx, x->rx + XRCON_HEADER_SIZE, x->rx_len - XRCON_HEADER_SIZE );
			x->rx_len -= XRCON_HEADER_SIZE;
			x->parser = XRCON_PARSER_PAYLOAD;
		}

		size_t payload_len = x->hdr.length - XRCON_HEADER_SIZE;
		if( x->rx_len < payload_len )
			return 0;

		if( x->hdr.type == XRCON_TYPE_PRNT )
			XRcon_HandlePRNT( x, x->rx, payload_len );
		else if( x->hdr.type == XRCON_TYPE_ADON )
			XRcon_HandleADON( x, x->rx, payload_len );
		// AINF and CHAN ignored

		memmove( x->rx, x->rx + payload_len, x->rx_len - payload_len );
		x->rx_len -= payload_len;
		x->parser = XRCON_PARSER_HEADER;
	}
}

void XRcon_OnReadable( xrcon_t *x )
{
	if( x->state != XRCON_CONNECTED )
		return;

	while( 1 )
	{
		if( x->rx_len >= sizeof( x->rx ))
		{
			XRcon_Disconnect( x, "rx buffer full" );
			return;
		}

		ssize_t n = recv( x->sock, x->rx + x->rx_len, sizeof( x->rx ) - x->rx_len, 0 );
		if( n > 0 )
		{
			x->rx_len += (size_t)n;
			if( XRcon_ParseRx( x ) < 0 )
			{
				XRcon_Disconnect( x, "frame parse error" );
				return;
			}
			continue;
		}
		if( n == 0 )
		{
			XRcon_Disconnect( x, "peer closed" );
			return;
		}
		if( errno == EAGAIN )
			return;
		if( errno == EINTR )
			continue;

		XRcon_Disconnect( x, strerror( errno ));
		return;
	}
}

void XRcon_OnWritable( xrcon_t *x )
{
	if( x->state == XRCON_CONNECTING )
	{
		XRcon_OnConnectComplete( x );
		if( x->state != XRCON_CONNECTED )
			return;
	}

	while( x->tx_len > 0 )
	{
		ssize_t n = send( x->sock, x->tx, x->tx_len, 0 );
		if( n > 0 )
		{
			memmove( x->tx, x->tx + n, x->tx_len - n );
			x->tx_len -= (size_t)n;
			continue;
		}
		if( n < 0 && ( errno == EAGAIN ))
			return;
		if( n < 0 && errno == EINTR )
			continue;

		XRcon_Disconnect( x, strerror( errno ));
		return;
	}
}

int XRcon_SendCmnd( xrcon_t *x, const char *cmd, size_t cmd_len )
{
	if( x->state != XRCON_CONNECTED )
		return -1;

	if( cmd_len > XRCON_MAX_FRAME_SIZE - XRCON_HEADER_SIZE - 1 )
		cmd_len = XRCON_MAX_FRAME_SIZE - XRCON_HEADER_SIZE - 1;

	size_t total = XRCON_HEADER_SIZE + cmd_len + 1;
	if( x->tx_len + total > sizeof( x->tx ))
	{
		XRcon_SetError( x, "tx buffer overflow" );
		return -1;
	}

	const uint32_t type = XRCON_TYPE_CMND;
	const uint32_t version = htonl( XRCON_CMND_VERSION );
	const uint16_t length = htons( (uint16_t)total );
	const uint16_t handle = 0;

	uint8_t *p = x->tx + x->tx_len;
	memcpy( p + 0, &type, sizeof( type ));
	memcpy( p + 4, &version, sizeof( version ));
	memcpy( p + 8, &length, sizeof( length ));
	memcpy( p + 10, &handle, sizeof( handle ));
	memcpy( p + XRCON_HEADER_SIZE, cmd, cmd_len );
	p[XRCON_HEADER_SIZE + cmd_len] = '\0';
	x->tx_len += total;

	XRcon_OnWritable( x );
	return (int)cmd_len;
}
