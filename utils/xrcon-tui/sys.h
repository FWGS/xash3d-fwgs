/*
sys.h - platform layer for xrcon-tui: terminal, keyboard, sockets and waiting
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

#ifndef XRCON_TUI_SYS_H
#define XRCON_TUI_SYS_H

#include <stddef.h>
#include <stdint.h>

#include "xash3d_types.h"

#if XASH_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sys_socket_t;
#define SYS_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
typedef int sys_socket_t;
#define SYS_INVALID_SOCKET ( -1 )
#endif

// Sys_Wait result bits
#define SYS_EV_INPUT      BIT( 0 ) // Sys_ReadKeys or Sys_ReadStdin has something
#define SYS_EV_RESIZE     BIT( 1 )
#define SYS_EV_QUIT       BIT( 2 )
#define SYS_EV_SOCK_READ  BIT( 3 )
#define SYS_EV_SOCK_WRITE BIT( 4 )

// socket call results besides byte counts
#define SYS_SOCK_AGAIN ( -1 )
#define SYS_SOCK_ERROR ( -2 )

enum
{
	TK_NONE = 0,
	TK_CHAR, // ch holds a code point, or a lowercase letter with TK_MOD_CTRL
	TK_ENTER,
	TK_BACKSPACE,
	TK_DELETE,
	TK_ESCAPE,
	TK_UP,
	TK_DOWN,
	TK_LEFT,
	TK_RIGHT,
	TK_HOME,
	TK_END,
	TK_PGUP,
	TK_PGDN,
	TK_WHEEL_UP,
	TK_WHEEL_DOWN,
	TK_F1, // F2..F12 follow
	TK_F12 = TK_F1 + 11,
};

#define TK_MOD_SHIFT BIT( 0 )
#define TK_MOD_ALT   BIT( 1 )
#define TK_MOD_CTRL  BIT( 2 )
#define TK_MOD_PASTE BIT( 3 ) // character comes from a bracketed paste

typedef struct
{
	int key;
	int mods;
	uint32_t ch;
} sys_key_t;

qboolean Sys_Init( qboolean mouse );
void Sys_Shutdown( void );
void Sys_GetSize( int *cols, int *rows );
void Sys_Write( const char *data, size_t len );
int64_t Sys_Milliseconds( void );

// MCP mode, stdin and stdout are pipes carrying JSON-RPC lines instead of a terminal
qboolean Sys_InitStdio( void );

// blocks until input, a socket event, a signal or the timeout, returns SYS_EV_* bits
int Sys_Wait( sys_socket_t sock, qboolean want_write, int timeout_ms );
int Sys_ReadKeys( sys_key_t *keys, int max );

// returns the number of bytes read, 0 if nothing is pending, -1 at the end of input
int Sys_ReadStdin( char *buf, size_t size );

// non-blocking TCP socket, ready to be passed to Sys_Wait
sys_socket_t Sys_SocketOpen( int family );
void Sys_SocketClose( sys_socket_t sock );
int Sys_SocketConnect( sys_socket_t sock, const struct sockaddr *addr, size_t addr_len );
int Sys_SocketConnectResult( sys_socket_t sock );
int Sys_SocketSend( sys_socket_t sock, const void *data, size_t len );
int Sys_SocketRecv( sys_socket_t sock, void *data, size_t len );
int Sys_SocketError( void );
const char *Sys_ErrorString( int code );
const char *Sys_ResolveErrorString( int code );

#endif // XRCON_TUI_SYS_H
