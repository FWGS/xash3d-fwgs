/*
mcp.h - MCP JSON-RPC dispatcher for xrcon-mcp
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

#ifndef XRCON_MCP_MCP_H
#define XRCON_MCP_MCP_H

#include <stddef.h>
#include <stdint.h>

#include "log_buf.h"
#include "xrcon.h"

#define MCP_URI_LOG  "xrcon://log"
#define MCP_URI_INFO "xrcon://info"

typedef struct
{
	xrcon_t *xrcon;
	log_buf_t *log;
	int log_subscribed;
	int info_subscribed;
	int initialized;
	int log_dirty; // set when log changed; main loop drains it via MCP_FlushPendingNotifications

	// Single-flight xrcon_exec_sync deferred response.
	int pending_active;
	char pending_id[256]; // captured raw JSON id literal (number/string/null)
	uint64_t pending_start_appended; // log->total_appended at the moment command was queued
	int64_t pending_deadline_ms;
	int64_t pending_min_deadline_ms; // quiet window can't end the wait before this, output arrives in bursts
	int64_t pending_last_change_ms; // only meaningful once log->total_appended exceeds pending_start_appended
	int pending_quiet_ms;
	int pending_max_bytes;

	// Deferred xrcon_connect response, waiting for the in-flight TCP connect to settle.
	int pending_connect_active;
	char pending_connect_id[256];
	int64_t pending_connect_deadline_ms;
} mcp_t;

void MCP_Init( mcp_t *m, xrcon_t *xrcon, log_buf_t *log );
void MCP_HandleLine( mcp_t *m, const char *line, size_t len, int64_t now_ms );
void MCP_MarkLogDirty( mcp_t *m );
void MCP_FlushPendingNotifications( mcp_t *m );
void MCP_TickPending( mcp_t *m, int64_t now_ms );
int MCP_PendingPollTimeout( const mcp_t *m, int64_t now_ms, int cur_timeout_ms );

#endif // XRCON_MCP_MCP_H
