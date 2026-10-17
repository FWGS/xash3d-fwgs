/*
mcp.c - MCP JSON-RPC dispatcher for xrcon-mcp
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
#include <string.h>
#include <unistd.h>

#include "crtlib.h"
#include "mcp.h"
#include "json.h"

#define MCP_PROTOCOL_VERSION "2025-06-18"
#define MCP_SERVER_NAME      "xash-xrcon-mcp"
#define MCP_SERVER_VERSION   "0.1.0"

#define MCP_TOOL_CONNECT     "xrcon_connect"
#define MCP_TOOL_DISCONNECT  "xrcon_disconnect"
#define MCP_TOOL_EXEC        "xrcon_exec"
#define MCP_TOOL_EXEC_SYNC   "xrcon_exec_sync"
#define MCP_TOOL_APROPOS     "xrcon_apropos"
#define MCP_TOOL_WAIT        "xrcon_wait"

#define MCP_SYNC_DEFAULT_WAIT_MS   2000
#define MCP_SYNC_DEFAULT_QUIET_MS  300
#define MCP_SYNC_DEFAULT_MAX_BYTES 16384
#define MCP_SYNC_MAX_WAIT_MS       30000

#define MCP_WAIT_DEFAULT_WAIT_MS   5000

#define MCP_CONNECT_DEFAULT_HOST    "127.0.0.1"
#define MCP_CONNECT_DEFAULT_PORT    27000
#define MCP_CONNECT_DEFAULT_WAIT_MS 3000
#define MCP_CONNECT_MAX_WAIT_MS     30000

void MCP_Init( mcp_t *m, xrcon_t *xrcon, log_buf_t *log )
{
	memset( m, 0, sizeof( *m ));
	m->xrcon = xrcon;
	m->log = log;
}

void MCP_MarkLogDirty( mcp_t *m )
{
	m->log_dirty = 1;
}

static void MCP_WriteLine( json_w_t *w )
{
	if( w->oom || !w->buf )
		return;
	fwrite( w->buf, 1, w->len, stdout );
	fputc( '\n', stdout );
	fflush( stdout );
}

// The input line buffer is reused after MCP_HandleLine returns, so a deferred
// response must own its id literal. Snapshot it as a ready-to-splice JSON fragment.
static void MCP_CaptureId( char *out, size_t cap, const json_doc_t *doc, int id_idx )
{
	if( id_idx < 0 )
	{
		Q_strncpy( out, "null", cap );
		return;
	}

	const json_tok_t *t = &doc->toks[id_idx];
	size_t tlen = (size_t)( t->end - t->start );

	if( t->type == JSON_PRIMITIVE )
	{
		if( tlen >= cap )
			tlen = cap - 1;
		memcpy( out, doc->json + t->start, tlen );
		out[tlen] = '\0';
		return;
	}
	if( t->type == JSON_STRING )
	{
		json_w_t w;
		JSON_WInit( &w );
		JSON_WStrEscapedN( &w, doc->json + t->start, tlen );
		if( !w.oom && w.len < cap )
		{
			memcpy( out, w.buf, w.len );
			out[w.len] = '\0';
		}
		else
		{
			Q_strncpy( out, "null", cap );
		}
		JSON_WFree( &w );
		return;
	}
	Q_strncpy( out, "null", cap );
}

static void MCP_EmitId( json_w_t *w, const json_doc_t *doc, int id_idx )
{
	if( id_idx < 0 )
	{
		JSON_WNull( w );
		return;
	}

	const json_tok_t *t = &doc->toks[id_idx];
	if( t->type == JSON_PRIMITIVE )
	{
		JSON_WStrN( w, doc->json + t->start, (size_t)( t->end - t->start ));
		return;
	}
	if( t->type == JSON_STRING )
	{
		JSON_WStrEscapedN( w, doc->json + t->start, (size_t)( t->end - t->start ));
		return;
	}
	JSON_WNull( w );
}

static void MCP_SendError( const json_doc_t *doc, int id_idx, int code, const char *message )
{
	json_w_t w;
	JSON_WInit( &w );
	JSON_WStr( &w, "{\"jsonrpc\":\"2.0\",\"id\":" );
	MCP_EmitId( &w, doc, id_idx );
	JSON_WStr( &w, ",\"error\":{\"code\":" );
	JSON_WInt64( &w, code );
	JSON_WStr( &w, ",\"message\":" );
	JSON_WStrEscaped( &w, message );
	JSON_WStr( &w, "}}" );
	MCP_WriteLine( &w );
	JSON_WFree( &w );
}

static void MCP_StartResult( json_w_t *w, const json_doc_t *doc, int id_idx )
{
	JSON_WInit( w );
	JSON_WStr( w, "{\"jsonrpc\":\"2.0\",\"id\":" );
	MCP_EmitId( w, doc, id_idx );
	JSON_WStr( w, ",\"result\":" );
}

static void MCP_FinishAndSend( json_w_t *w )
{
	JSON_WPutc( w, '}' );
	MCP_WriteLine( w );
	JSON_WFree( w );
}

static void MCP_DoInitialize( mcp_t *m, const json_doc_t *doc, int id_idx )
{
	char version[64];
	Q_snprintf( version, sizeof( version ), "%s+build.%d", MCP_SERVER_VERSION, Q_buildnum( ));

	json_w_t w;
	MCP_StartResult( &w, doc, id_idx );

	JSON_WStr( &w, "{\"protocolVersion\":\"" MCP_PROTOCOL_VERSION "\"," );
	JSON_WStr( &w, "\"serverInfo\":{\"name\":\"" MCP_SERVER_NAME "\",\"version\":" );
	JSON_WStrEscaped( &w, version );
	JSON_WStr( &w, "}," );
	JSON_WStr( &w, "\"capabilities\":{" );
	JSON_WStr( &w, "\"tools\":{\"listChanged\":false}," );
	JSON_WStr( &w, "\"resources\":{\"subscribe\":true,\"listChanged\":false}" );
	JSON_WStr( &w, "}}" );

	MCP_FinishAndSend( &w );
	m->initialized = 1;
}

static void MCP_DoPing( const json_doc_t *doc, int id_idx )
{
	json_w_t w;
	MCP_StartResult( &w, doc, id_idx );
	JSON_WStr( &w, "{}" );
	MCP_FinishAndSend( &w );
}

static void MCP_DoToolsList( const json_doc_t *doc, int id_idx )
{
	json_w_t w;
	MCP_StartResult( &w, doc, id_idx );

	JSON_WStr( &w, "{\"tools\":[" );

	JSON_WStr( &w, "{\"name\":\"" MCP_TOOL_CONNECT "\"," );
	JSON_WStr( &w, "\"description\":\"Open the XRCON TCP connection to an Xash3D engine. Must be called before any other xrcon_* tool. The engine permits only one XRCON client at a time, so disconnect with " MCP_TOOL_DISCONNECT " when you are done to let other sessions in. Waits up to wait_ms for the connect to complete and returns the resulting state.\"," );
	JSON_WStr( &w, "\"inputSchema\":{" );
	JSON_WStr( &w, "\"type\":\"object\"," );
	JSON_WStr( &w, "\"properties\":{" );
	JSON_WStr( &w, "\"host\":{\"type\":\"string\",\"description\":\"XRCON host or IP (default 127.0.0.1).\"}," );
	JSON_WStr( &w, "\"port\":{\"type\":\"integer\",\"description\":\"XRCON TCP port (default 27000).\"}," );
	JSON_WStr( &w, "\"wait_ms\":{\"type\":\"integer\",\"description\":\"Hard cap on wait for connect to complete (default 3000, max 30000).\"}" );
	JSON_WStr( &w, "}}}," );

	JSON_WStr( &w, "{\"name\":\"" MCP_TOOL_DISCONNECT "\"," );
	JSON_WStr( &w, "\"description\":\"Close the XRCON TCP connection. Call this when finished so other sessions can connect; the engine only allows one XRCON client at a time. No-op if already disconnected.\"," );
	JSON_WStr( &w, "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}," );

	JSON_WStr( &w, "{\"name\":\"" MCP_TOOL_EXEC "\"," );
	JSON_WStr( &w, "\"description\":\"Execute a console command on the connected Xash3D engine via XRCON (fire-and-forget). Output appears asynchronously in resource " MCP_URI_LOG ". For commands where you want the immediate response in the tool result, prefer " MCP_TOOL_EXEC_SYNC ".\"," );
	JSON_WStr( &w, "\"inputSchema\":{" );
	JSON_WStr( &w, "\"type\":\"object\"," );
	JSON_WStr( &w, "\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"Console command to execute (e.g. status, echo hi, map crossfire).\"}}," );
	JSON_WStr( &w, "\"required\":[\"command\"]" );
	JSON_WStr( &w, "}}," );

	JSON_WStr( &w, "{\"name\":\"" MCP_TOOL_EXEC_SYNC "\"," );
	JSON_WStr( &w, "\"description\":\"Execute a console command and wait for the engine to respond, returning the log delta produced during the wait window. Returns when the log has been quiet for quiet_ms after producing output, or when wait_ms elapses. Prefer this for interactive commands like 'status', 'bind w', cvar queries; use " MCP_TOOL_EXEC " for commands that restart the engine (game change, quit) since the connection drops.\"," );
	JSON_WStr( &w, "\"inputSchema\":{" );
	JSON_WStr( &w, "\"type\":\"object\"," );
	JSON_WStr( &w, "\"properties\":{" );
	JSON_WStr( &w, "\"command\":{\"type\":\"string\",\"description\":\"Console command to execute.\"}," );
	JSON_WStr( &w, "\"wait_ms\":{\"type\":\"integer\",\"description\":\"Hard cap on wait time in milliseconds (default 2000, max 30000). Raised to min_ms when min_ms is larger.\"}," );
	JSON_WStr( &w, "\"quiet_ms\":{\"type\":\"integer\",\"description\":\"Return early once the log has been quiet this long after producing output (default 300).\"}," );
	JSON_WStr( &w, "\"min_ms\":{\"type\":\"integer\",\"description\":\"Never return before this many milliseconds have passed (default 0). Set it for commands that emit output in bursts separated by long gaps, such as map, where the quiet window would otherwise return a half-finished transcript.\"}," );
	JSON_WStr( &w, "\"max_bytes\":{\"type\":\"integer\",\"description\":\"Cap on returned log delta bytes; tail is preserved (default 16384).\"}" );
	JSON_WStr( &w, "}," );
	JSON_WStr( &w, "\"required\":[\"command\"]" );
	JSON_WStr( &w, "}}," );

	JSON_WStr( &w, "{\"name\":\"" MCP_TOOL_APROPOS "\"," );
	JSON_WStr( &w, "\"description\":\"Search Xash3D console cvars and commands by substring against both name and short description. Use BEFORE guessing a cvar/command name (e.g. before assuming 'mp_gamemode' exists, run apropos 'deathmatch' or 'gamemode' to see what the loaded mod actually exposes). Returns the engine's raw apropos output.\"," );
	JSON_WStr( &w, "\"inputSchema\":{" );
	JSON_WStr( &w, "\"type\":\"object\"," );
	JSON_WStr( &w, "\"properties\":{" );
	JSON_WStr( &w, "\"pattern\":{\"type\":\"string\",\"description\":\"Substring to match against cvar/command names and their help text.\"}," );
	JSON_WStr( &w, "\"wait_ms\":{\"type\":\"integer\",\"description\":\"Hard cap on wait time in milliseconds (default 2000, max 30000).\"}," );
	JSON_WStr( &w, "\"max_bytes\":{\"type\":\"integer\",\"description\":\"Cap on returned bytes; tail is preserved (default 16384).\"}" );
	JSON_WStr( &w, "}," );
	JSON_WStr( &w, "\"required\":[\"pattern\"]" );
	JSON_WStr( &w, "}}," );

	JSON_WStr( &w, "{\"name\":\"" MCP_TOOL_WAIT "\"," );
	JSON_WStr( &w, "\"description\":\"Wait without sending anything and return the log produced during the wait. Pair it with " MCP_TOOL_EXEC " for commands whose output arrives long after the command returns, most importantly 'map', which loads for many seconds and prints in bursts. Also the way to watch for output the engine emits on its own, such as a client connecting or a script firing.\"," );
	JSON_WStr( &w, "\"inputSchema\":{" );
	JSON_WStr( &w, "\"type\":\"object\"," );
	JSON_WStr( &w, "\"properties\":{" );
	JSON_WStr( &w, "\"wait_ms\":{\"type\":\"integer\",\"description\":\"How long to wait in milliseconds (default 5000, max 30000).\"}," );
	JSON_WStr( &w, "\"quiet_ms\":{\"type\":\"integer\",\"description\":\"Return early once the log has been quiet this long after producing output. Defaults to wait_ms, which never triggers, so by default the full window is waited out.\"}," );
	JSON_WStr( &w, "\"min_ms\":{\"type\":\"integer\",\"description\":\"Never return before this many milliseconds have passed (default 0).\"}," );
	JSON_WStr( &w, "\"max_bytes\":{\"type\":\"integer\",\"description\":\"Cap on returned log delta bytes; tail is preserved (default 16384).\"}" );
	JSON_WStr( &w, "}}}" );

	JSON_WStr( &w, "]}" );

	MCP_FinishAndSend( &w );
}

static void MCP_ToolError( const json_doc_t *doc, int id_idx, const char *msg )
{
	json_w_t w;
	MCP_StartResult( &w, doc, id_idx );
	JSON_WStr( &w, "{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":" );
	JSON_WStrEscaped( &w, msg );
	JSON_WStr( &w, "}]}" );
	MCP_FinishAndSend( &w );
}

static void MCP_ToolText( const json_doc_t *doc, int id_idx, const char *msg )
{
	json_w_t w;
	MCP_StartResult( &w, doc, id_idx );
	JSON_WStr( &w, "{\"content\":[{\"type\":\"text\",\"text\":" );
	JSON_WStrEscaped( &w, msg );
	JSON_WStr( &w, "}]}" );
	MCP_FinishAndSend( &w );
}

enum
{
	MCP_KIND_CONNECT,
	MCP_KIND_DISCONNECT,
	MCP_KIND_EXEC,
	MCP_KIND_SYNC,
	MCP_KIND_APROPOS,
	MCP_KIND_WAIT,
};

static int MCP_OptInt( const json_doc_t *doc, int args_idx, const char *key, int defval, int minval, int maxval )
{
	if( args_idx < 0 )
		return defval;
	int t_idx = JSON_ObjFind( doc, args_idx, key );
	if( t_idx < 0 )
		return defval;
	int64_t v;
	if( JSON_TokenAsInt64( doc, t_idx, &v ) < 0 )
		return defval;
	if( v < (int64_t)minval ) v = minval;
	if( v > (int64_t)maxval ) v = maxval;
	return (int)v;
}

static void MCP_DoToolsCall( mcp_t *m, const json_doc_t *doc, int id_idx, int params_idx, int64_t now_ms )
{
	if( params_idx < 0 )
	{
		MCP_SendError( doc, id_idx, -32602, "missing params" );
		return;
	}

	int name_idx = JSON_ObjFind( doc, params_idx, "name" );
	if( name_idx < 0 )
	{
		MCP_SendError( doc, id_idx, -32602, "unknown tool" );
		return;
	}

	int kind;
	if( JSON_StringEq( doc, name_idx, MCP_TOOL_CONNECT ))
		kind = MCP_KIND_CONNECT;
	else if( JSON_StringEq( doc, name_idx, MCP_TOOL_DISCONNECT ))
		kind = MCP_KIND_DISCONNECT;
	else if( JSON_StringEq( doc, name_idx, MCP_TOOL_EXEC ))
		kind = MCP_KIND_EXEC;
	else if( JSON_StringEq( doc, name_idx, MCP_TOOL_EXEC_SYNC ))
		kind = MCP_KIND_SYNC;
	else if( JSON_StringEq( doc, name_idx, MCP_TOOL_APROPOS ))
		kind = MCP_KIND_APROPOS;
	else if( JSON_StringEq( doc, name_idx, MCP_TOOL_WAIT ))
		kind = MCP_KIND_WAIT;
	else
	{
		MCP_SendError( doc, id_idx, -32602, "unknown tool" );
		return;
	}

	int args_idx = JSON_ObjFind( doc, params_idx, "arguments" );

	if( kind == MCP_KIND_DISCONNECT )
	{
		if( m->pending_connect_active )
		{
			MCP_ToolError( doc, id_idx, "connect in progress; wait for it to finish" );
			return;
		}
		if( !XRcon_IsConnected( m->xrcon ) && !XRcon_IsConnecting( m->xrcon ))
		{
			MCP_ToolText( doc, id_idx, "already disconnected" );
			return;
		}
		XRcon_RequestDisconnect( m->xrcon, "tool" );
		MCP_ToolText( doc, id_idx, "disconnected" );
		return;
	}

	if( kind == MCP_KIND_CONNECT )
	{
		if( m->pending_connect_active )
		{
			MCP_ToolError( doc, id_idx, "another connect is in progress" );
			return;
		}
		if( XRcon_IsConnected( m->xrcon ) || XRcon_IsConnecting( m->xrcon ))
		{
			MCP_ToolError( doc, id_idx, "already connected; call xrcon_disconnect first" );
			return;
		}

		char host[128];
		Q_strncpy( host, MCP_CONNECT_DEFAULT_HOST, sizeof( host ));
		if( args_idx >= 0 )
		{
			int h_idx = JSON_ObjFind( doc, args_idx, "host" );
			if( h_idx >= 0 && JSON_StringDup( doc, h_idx, host, sizeof( host )) < 0 )
			{
				MCP_ToolError( doc, id_idx, "invalid host string" );
				return;
			}
		}

		int port = MCP_OptInt( doc, args_idx, "port", MCP_CONNECT_DEFAULT_PORT, 1, 65535 );
		int wait_ms = MCP_OptInt( doc, args_idx, "wait_ms", MCP_CONNECT_DEFAULT_WAIT_MS, 1, MCP_CONNECT_MAX_WAIT_MS );

		if( XRcon_RequestConnect( m->xrcon, host, port ) < 0 )
		{
			MCP_ToolError( doc, id_idx, m->xrcon->last_error );
			return;
		}

		if( XRcon_IsConnected( m->xrcon ))
		{
			char reply[192];
			Q_snprintf( reply, sizeof( reply ), "connected to %s:%d", host, port );
			MCP_ToolText( doc, id_idx, reply );
			return;
		}

		m->pending_connect_active = 1;
		MCP_CaptureId( m->pending_connect_id, sizeof( m->pending_connect_id ), doc, id_idx );
		m->pending_connect_deadline_ms = now_ms + wait_ms;
		return;
	}

	char cmd[XRCON_MAX_FRAME_SIZE];
	int cmdlen = 0;

	cmd[0] = '\0';

	if( kind == MCP_KIND_WAIT )
	{
		// nothing to send, we only observe the log
	}
	else if( kind == MCP_KIND_APROPOS )
	{
		int pat_idx = args_idx >= 0 ? JSON_ObjFind( doc, args_idx, "pattern" ) : -1;
		if( pat_idx < 0 )
		{
			MCP_ToolError( doc, id_idx, "missing arguments.pattern" );
			return;
		}

		char pat[XRCON_MAX_FRAME_SIZE];
		int patlen = JSON_StringDup( doc, pat_idx, pat, sizeof( pat ));
		if( patlen < 0 )
		{
			MCP_ToolError( doc, id_idx, "invalid pattern string" );
			return;
		}

		// Refuse console separators so a pattern can't piggyback a second command.
		for( int i = 0; i < patlen; i++ )
		{
			if( pat[i] == ';' || pat[i] == '\n' || pat[i] == '\r' )
			{
				MCP_ToolError( doc, id_idx, "pattern contains forbidden character" );
				return;
			}
		}

		cmdlen = Q_snprintf( cmd, sizeof( cmd ), "apropos %s", pat );
		if( cmdlen < 0 )
		{
			MCP_ToolError( doc, id_idx, "apropos pattern too long" );
			return;
		}
	}
	else
	{
		int cmd_idx = args_idx >= 0 ? JSON_ObjFind( doc, args_idx, "command" ) : -1;
		if( cmd_idx < 0 )
		{
			MCP_ToolError( doc, id_idx, "missing arguments.command" );
			return;
		}

		cmdlen = JSON_StringDup( doc, cmd_idx, cmd, sizeof( cmd ));
		if( cmdlen < 0 )
		{
			MCP_ToolError( doc, id_idx, "invalid command string" );
			return;
		}
	}

	if( !XRcon_IsConnected( m->xrcon ))
	{
		MCP_ToolError( doc, id_idx, "not connected to XRCON server; command dropped" );
		return;
	}

	int is_sync = ( kind != MCP_KIND_EXEC );

	if( is_sync && m->pending_active )
	{
		MCP_ToolError( doc, id_idx, "another synchronous call is already in flight" );
		return;
	}

	int wait_default = kind == MCP_KIND_WAIT ? MCP_WAIT_DEFAULT_WAIT_MS : MCP_SYNC_DEFAULT_WAIT_MS;
	int wait_ms = is_sync ? MCP_OptInt( doc, args_idx, "wait_ms", wait_default, 1, MCP_SYNC_MAX_WAIT_MS ) : 0;

	// a bare wait wants the whole window, and a quiet window as long as the wait can never end it early
	int quiet_default = kind == MCP_KIND_WAIT ? wait_ms : MCP_SYNC_DEFAULT_QUIET_MS;
	int quiet_ms = is_sync ? MCP_OptInt( doc, args_idx, "quiet_ms", quiet_default, 0, MCP_SYNC_MAX_WAIT_MS ) : 0;

	int min_ms = is_sync ? MCP_OptInt( doc, args_idx, "min_ms", 0, 0, MCP_SYNC_MAX_WAIT_MS ) : 0;
	int max_b = is_sync ? MCP_OptInt( doc, args_idx, "max_bytes", MCP_SYNC_DEFAULT_MAX_BYTES, 1, (int)m->log->cap ) : 0;

	if( min_ms > wait_ms )
		wait_ms = min_ms;

	uint64_t before = m->log->total_appended;

	if( kind != MCP_KIND_WAIT )
	{
		int sent = XRcon_SendCmnd( m->xrcon, cmd, (size_t)cmdlen );
		if( sent < 0 )
		{
			MCP_ToolError( doc, id_idx, "send failed" );
			return;
		}

		if( !is_sync )
		{
			char reply[160];
			Q_snprintf( reply, sizeof( reply ),
				"queued %d bytes; watch resource " MCP_URI_LOG " for output", sent );
			MCP_ToolText( doc, id_idx, reply );
			return;
		}
	}

	m->pending_active = 1;
	MCP_CaptureId( m->pending_id, sizeof( m->pending_id ), doc, id_idx );
	m->pending_start_appended = before;
	m->pending_deadline_ms = now_ms + wait_ms;
	m->pending_min_deadline_ms = now_ms + min_ms;
	m->pending_last_change_ms = now_ms;
	m->pending_quiet_ms = quiet_ms;
	m->pending_max_bytes = max_b;
}

static void MCP_FireSyncResponse( mcp_t *m, int timed_out )
{
	log_buf_t *log = m->log;
	uint64_t since = log->total_appended - m->pending_start_appended;
	int truncated = 0;
	size_t want;

	if( since > (uint64_t)log->len )
	{
		want = log->len;
		truncated = 1;
	}
	else
	{
		want = (size_t)since;
	}

	if( m->pending_max_bytes > 0 && (size_t)m->pending_max_bytes < want )
	{
		want = (size_t)m->pending_max_bytes;
		truncated = 1;
	}

	size_t offset = log->len - want;

	json_w_t w;
	JSON_WInit( &w );
	JSON_WStr( &w, "{\"jsonrpc\":\"2.0\",\"id\":" );
	JSON_WStr( &w, m->pending_id );
	JSON_WStr( &w, ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":" );
	JSON_WStrEscapedN( &w, log->data + offset, want );
	JSON_WStr( &w, "}],\"_meta\":{\"bytes\":" );
	JSON_WInt64( &w, (int64_t)want );
	JSON_WStr( &w, ",\"timed_out\":" );
	JSON_WBool( &w, timed_out );
	JSON_WStr( &w, ",\"truncated\":" );
	JSON_WBool( &w, truncated );
	JSON_WStr( &w, "}}}" );
	MCP_WriteLine( &w );
	JSON_WFree( &w );

	m->pending_active = 0;
}

static void MCP_DoResourcesList( const json_doc_t *doc, int id_idx )
{
	json_w_t w;
	MCP_StartResult( &w, doc, id_idx );

	JSON_WStr( &w, "{\"resources\":[" );
	JSON_WStr( &w, "{\"uri\":\"" MCP_URI_LOG "\",\"name\":\"XRCON console log\",\"description\":\"Live console output from the connected engine. Subscribe to receive notifications when new lines arrive.\",\"mimeType\":\"text/plain\"}," );
	JSON_WStr( &w, "{\"uri\":\"" MCP_URI_INFO "\",\"name\":\"XRCON connection info\",\"description\":\"JSON snapshot of XRCON connection state and server identity.\",\"mimeType\":\"application/json\"}" );
	JSON_WStr( &w, "]}" );

	MCP_FinishAndSend( &w );
}

static void MCP_WriteResource( json_w_t *w, const char *uri, const char *mime, const char *text, size_t text_len )
{
	JSON_WStr( w, "{\"contents\":[{\"uri\":\"" );
	JSON_WStr( w, uri );
	JSON_WStr( w, "\",\"mimeType\":\"" );
	JSON_WStr( w, mime );
	JSON_WStr( w, "\",\"text\":" );
	JSON_WStrEscapedN( w, text, text_len );
	JSON_WStr( w, "}]}" );
}

static void MCP_DoResourcesRead( mcp_t *m, const json_doc_t *doc, int id_idx, int params_idx )
{
	if( params_idx < 0 )
	{
		MCP_SendError( doc, id_idx, -32602, "missing params" );
		return;
	}

	int uri_idx = JSON_ObjFind( doc, params_idx, "uri" );
	if( uri_idx < 0 )
	{
		MCP_SendError( doc, id_idx, -32602, "missing params.uri" );
		return;
	}

	if( JSON_StringEq( doc, uri_idx, MCP_URI_LOG ))
	{
		json_w_t w;
		MCP_StartResult( &w, doc, id_idx );
		MCP_WriteResource( &w, MCP_URI_LOG, "text/plain",
			m->log->data, m->log->len );
		MCP_FinishAndSend( &w );
		return;
	}

	if( JSON_StringEq( doc, uri_idx, MCP_URI_INFO ))
	{
		char info[1024];
		int n = Q_snprintf( info, sizeof( info ),
			"{\"host\":\"%s\",\"port\":%d,\"state\":\"%s\",\"server_name\":\"%s\","
			"\"log_bytes_appended\":%llu,\"log_bytes_evicted\":%llu,\"last_error\":\"%s\"}",
			m->xrcon->host, m->xrcon->port,
			XRcon_StateString( m->xrcon ),
			m->xrcon->server_name,
			(unsigned long long)m->log->total_appended,
			(unsigned long long)m->log->total_evicted,
			m->xrcon->last_error );

		if( n < 0 )
			n = 0;

		json_w_t w;
		MCP_StartResult( &w, doc, id_idx );
		MCP_WriteResource( &w, MCP_URI_INFO, "application/json", info, (size_t)n );
		MCP_FinishAndSend( &w );
		return;
	}

	MCP_SendError( doc, id_idx, -32602, "unknown resource uri" );
}

static void MCP_DoResourcesSubscribe( mcp_t *m, const json_doc_t *doc, int id_idx, int params_idx, int subscribe )
{
	if( params_idx < 0 )
	{
		MCP_SendError( doc, id_idx, -32602, "missing params" );
		return;
	}

	int uri_idx = JSON_ObjFind( doc, params_idx, "uri" );
	if( uri_idx < 0 )
	{
		MCP_SendError( doc, id_idx, -32602, "missing params.uri" );
		return;
	}

	if( JSON_StringEq( doc, uri_idx, MCP_URI_LOG ))
		m->log_subscribed = subscribe;
	else if( JSON_StringEq( doc, uri_idx, MCP_URI_INFO ))
		m->info_subscribed = subscribe;
	else
	{
		MCP_SendError( doc, id_idx, -32602, "unknown resource uri" );
		return;
	}

	json_w_t w;
	MCP_StartResult( &w, doc, id_idx );
	JSON_WStr( &w, "{}" );
	MCP_FinishAndSend( &w );
}

void MCP_HandleLine( mcp_t *m, const char *line, size_t len, int64_t now_ms )
{
	while( len > 0 && ( line[len - 1] == '\n' || line[len - 1] == '\r' ))
		len--;
	if( len == 0 )
		return;

	json_tok_t toks[JSON_MAX_TOKS];
	int n = JSON_Parse( line, (int)len, toks, JSON_MAX_TOKS );

	json_doc_t doc;
	doc.json = line;
	doc.len = (int)len;
	doc.toks = toks;
	doc.ntoks = n;

	if( n <= 0 || toks[0].type != JSON_OBJECT )
	{
		fprintf( stderr, "[mcp] parse error on input line\n" );
		// We can't safely echo an id we couldn't parse, so send a parse error with id=null.
		json_doc_t empty = { line, 0, NULL, 0 };
		MCP_SendError( &empty, -1, -32700, "parse error" );
		return;
	}

	int method_idx = JSON_ObjFind( &doc, 0, "method" );
	int id_idx = JSON_ObjFind( &doc, 0, "id" );
	int params_idx = JSON_ObjFind( &doc, 0, "params" );

	if( method_idx < 0 )
	{
		MCP_SendError( &doc, id_idx, -32600, "missing method" );
		return;
	}

	// Notifications: no response.
	if( id_idx < 0 )
	{
		if( JSON_StringEq( &doc, method_idx, "notifications/initialized" ))
		{
			m->initialized = 1;
			return;
		}
		// Silently ignore unknown notifications.
		return;
	}

	if( JSON_StringEq( &doc, method_idx, "initialize" ))
	{
		MCP_DoInitialize( m, &doc, id_idx );
		return;
	}
	if( JSON_StringEq( &doc, method_idx, "ping" ))
	{
		MCP_DoPing( &doc, id_idx );
		return;
	}
	if( JSON_StringEq( &doc, method_idx, "tools/list" ))
	{
		MCP_DoToolsList( &doc, id_idx );
		return;
	}
	if( JSON_StringEq( &doc, method_idx, "tools/call" ))
	{
		MCP_DoToolsCall( m, &doc, id_idx, params_idx, now_ms );
		return;
	}
	if( JSON_StringEq( &doc, method_idx, "resources/list" ))
	{
		MCP_DoResourcesList( &doc, id_idx );
		return;
	}
	if( JSON_StringEq( &doc, method_idx, "resources/read" ))
	{
		MCP_DoResourcesRead( m, &doc, id_idx, params_idx );
		return;
	}
	if( JSON_StringEq( &doc, method_idx, "resources/subscribe" ))
	{
		MCP_DoResourcesSubscribe( m, &doc, id_idx, params_idx, 1 );
		return;
	}
	if( JSON_StringEq( &doc, method_idx, "resources/unsubscribe" ))
	{
		MCP_DoResourcesSubscribe( m, &doc, id_idx, params_idx, 0 );
		return;
	}

	MCP_SendError( &doc, id_idx, -32601, "method not found" );
}

static void MCP_FireConnectResponse( mcp_t *m, int connected, const char *detail )
{
	json_w_t w;
	JSON_WInit( &w );
	JSON_WStr( &w, "{\"jsonrpc\":\"2.0\",\"id\":" );
	JSON_WStr( &w, m->pending_connect_id );
	JSON_WStr( &w, ",\"result\":{" );
	if( !connected )
		JSON_WStr( &w, "\"isError\":true," );
	JSON_WStr( &w, "\"content\":[{\"type\":\"text\",\"text\":" );
	JSON_WStrEscaped( &w, detail );
	JSON_WStr( &w, "}]}}" );
	MCP_WriteLine( &w );
	JSON_WFree( &w );

	m->pending_connect_active = 0;
}

void MCP_TickPending( mcp_t *m, int64_t now_ms )
{
	if( m->pending_connect_active )
	{
		if( XRcon_IsConnected( m->xrcon ))
		{
			char reply[192];
			Q_snprintf( reply, sizeof( reply ), "connected to %s:%d",
				m->xrcon->host, m->xrcon->port );
			MCP_FireConnectResponse( m, 1, reply );
		}
		else if( !XRcon_IsConnecting( m->xrcon ))
		{
			MCP_FireConnectResponse( m, 0, m->xrcon->last_error );
		}
		else if( now_ms >= m->pending_connect_deadline_ms )
		{
			XRcon_RequestDisconnect( m->xrcon, "connect timeout" );
			MCP_FireConnectResponse( m, 0, "connect timeout" );
		}
	}

	if( !m->pending_active )
		return;

	if( m->log_dirty )
		m->pending_last_change_ms = now_ms;

	int output_seen = m->log->total_appended > m->pending_start_appended;
	int timed_out = now_ms >= m->pending_deadline_ms;
	int quiet_hit = output_seen && now_ms - m->pending_last_change_ms >= m->pending_quiet_ms;

	if( now_ms < m->pending_min_deadline_ms )
		quiet_hit = 0;

	if( timed_out || quiet_hit )
		MCP_FireSyncResponse( m, timed_out );
}

int MCP_PendingPollTimeout( const mcp_t *m, int64_t now_ms, int cur_timeout_ms )
{
	int64_t soonest = (int64_t)cur_timeout_ms;

	if( m->pending_connect_active )
	{
		int64_t until = m->pending_connect_deadline_ms - now_ms;
		if( until < 0 ) until = 0;
		if( until < soonest ) soonest = until;
	}

	if( m->pending_active )
	{
		int64_t until_deadline = m->pending_deadline_ms - now_ms;
		if( until_deadline < 0 ) until_deadline = 0;
		if( until_deadline < soonest ) soonest = until_deadline;

		if( m->log->total_appended > m->pending_start_appended )
		{
			int64_t until_quiet = m->pending_last_change_ms + m->pending_quiet_ms - now_ms;
			int64_t until_min = m->pending_min_deadline_ms - now_ms;
			if( until_quiet < until_min ) until_quiet = until_min;
			if( until_quiet < 0 ) until_quiet = 0;
			if( until_quiet < soonest ) soonest = until_quiet;
		}
	}

	if( soonest < (int64_t)cur_timeout_ms )
		return (int)soonest;
	return cur_timeout_ms;
}

void MCP_FlushPendingNotifications( mcp_t *m )
{
	if( !m->log_dirty || !m->log_subscribed )
	{
		m->log_dirty = 0;
		return;
	}

	json_w_t w;
	JSON_WInit( &w );
	JSON_WStr( &w, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/resources/updated\",\"params\":{\"uri\":\"" MCP_URI_LOG "\"}}" );
	MCP_WriteLine( &w );
	JSON_WFree( &w );

	m->log_dirty = 0;
}
