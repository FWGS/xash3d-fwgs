/*
net.h - XRCON TCP client for xrcon-tui
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

#ifndef XRCON_TUI_NET_H
#define XRCON_TUI_NET_H

#include <stddef.h>
#include <stdint.h>

#include "sys.h"
#include "xrcon_protocol.h"

#define NET_RX_SIZE     65536
#define NET_TX_SIZE     65536
#define NET_MAX_COMMAND ( XRCON_MAX_FRAME_SIZE - XRCON_HEADER_SIZE - 1 )

typedef enum
{
	NET_OFFLINE,
	NET_CONNECTING,
	NET_CONNECTED, // TCP is up but the engine hasn't accepted us yet, it serves one client at a time
	NET_ONLINE
} net_state_t;

enum
{
	NET_MSG_INFO,
	NET_MSG_ERROR
};

typedef void ( *net_print_fn )( void *user, const char *text, size_t len );
typedef void ( *net_message_fn )( void *user, int kind, const char *msg );

typedef struct
{
	net_state_t state;
	sys_socket_t sock;

	// automatic retries don't report failures, the status bar shows last_error instead
	qboolean quiet;
	qboolean greet_warned;

	char host[256];
	int port;
	int connect_timeout_ms; // per address

	struct addrinfo *addrs;
	struct addrinfo *addr_next;
	char addr[96]; // numeric address of the current attempt
	int64_t deadline; // connect timeout, or greeting timeout once connected

	char server_name[64];
	char last_error[128];

	net_print_fn on_print;
	net_message_fn on_message;
	void *user;

	size_t rx_len;
	size_t tx_len;
	uint8_t rx[NET_RX_SIZE];
	uint8_t tx[NET_TX_SIZE];
} net_t;

void Net_Init( net_t *net, net_print_fn on_print, net_message_fn on_message, void *user );
void Net_Shutdown( net_t *net );

qboolean Net_Connect( net_t *net, const char *host, int port, qboolean quiet, int64_t now );
void Net_Disconnect( net_t *net, const char *reason );
qboolean Net_IsUp( const net_t *net );
qboolean Net_WantsWrite( const net_t *net );
const char *Net_StateString( const net_t *net );

void Net_Frame( net_t *net, int64_t now );
int Net_Timeout( const net_t *net, int64_t now, int timeout_ms );
void Net_OnReadable( net_t *net );
void Net_OnWritable( net_t *net, int64_t now );

qboolean Net_Send( net_t *net, const char *cmd, size_t len );

#endif // XRCON_TUI_NET_H
