/*
xrcon.h - XRCON TCP client for xrcon-mcp
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

#ifndef XRCON_MCP_XRCON_H
#define XRCON_MCP_XRCON_H

#include <stddef.h>
#include <stdint.h>

#include "xrcon_protocol.h"
#include "log_buf.h"

typedef enum
{
	XRCON_DISCONNECTED,
	XRCON_CONNECTING,
	XRCON_CONNECTED
} xrcon_state_t;

typedef enum
{
	XRCON_PARSER_HEADER,
	XRCON_PARSER_PAYLOAD
} xrcon_parser_state_t;

typedef struct
{
	uint32_t type; // host-endian FourCC matching XRCON_TYPE_*
	uint32_t version;
	uint16_t length;
	uint16_t handle;
} xrcon_header_t;

typedef void (*xrcon_log_changed_fn)( void *user );

typedef struct
{
	char host[128];
	int port;
	int sock;

	xrcon_state_t state;

	log_buf_t *log;
	xrcon_log_changed_fn on_log_changed;
	void *user;

	uint8_t rx[XRCON_RX_BUFFER_SIZE];
	size_t rx_len;

	uint8_t tx[XRCON_TX_BUFFER_SIZE];
	size_t tx_len;

	xrcon_parser_state_t parser;
	xrcon_header_t hdr;

	char server_name[64];
	char last_error[128];
} xrcon_t;

void XRcon_Init( xrcon_t *x, log_buf_t *log, xrcon_log_changed_fn cb, void *user );
void XRcon_Free( xrcon_t *x );

int XRcon_Socket( const xrcon_t *x );
int XRcon_IsConnected( const xrcon_t *x );
int XRcon_IsConnecting( const xrcon_t *x );
int XRcon_WantsRead( const xrcon_t *x );
int XRcon_WantsWrite( const xrcon_t *x );

int XRcon_RequestConnect( xrcon_t *x, const char *host, int port );
void XRcon_RequestDisconnect( xrcon_t *x, const char *reason );

void XRcon_OnReadable( xrcon_t *x );
void XRcon_OnWritable( xrcon_t *x );

int XRcon_SendCmnd( xrcon_t *x, const char *cmd, size_t cmd_len );

const char *XRcon_StateString( const xrcon_t *x );

#endif // XRCON_MCP_XRCON_H
