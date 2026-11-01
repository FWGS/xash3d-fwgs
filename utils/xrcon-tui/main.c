/*
main.c - xrcon-tui entry point and event loop
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

#include "crtlib.h"
#include "xash3d_mathlib.h"
#if XASH_WIN32
#include "getopt.h"
#else
#include <unistd.h>
#endif

#include "sys.h"
#include "net.h"
#include "ui.h"
#include "log_buf.h"
#include "mcp.h"

#define DEFAULT_HOST         "127.0.0.1"
#define DEFAULT_PORT         27000
#define DEFAULT_RECONNECT_MS 3000
#define DEFAULT_SCROLLBACK   20000
#define MAX_EXEC             32
#define FRAME_INTERVAL_MS    16
#define MCP_LOG_SIZE         262144

typedef struct
{
	char host[256];
	int port;
	int reconnect_ms;
	qboolean mouse;
	const char *exec[MAX_EXEC];
	int exec_count;
	char target[300];
	ui_config_t ui;
	qboolean mcp;
} args_t;

static net_t net;
static ui_t ui;

static void PrintUsage( FILE *f, const char *argv0 )
{
	fprintf( f,
		"Usage: %s [-nm] [-p port] [-r ms] [-e command]... [-b lines] [host[:port]]\n"
		"       %s -M\n"
		"\n"
		"Terminal client for the Xash3D FWGS remote console (XRCON).\n"
		"Enable it in the engine with \"xrcon_enable 1\", it listens on xrcon_address,\n"
		"127.0.0.1:27000 by default, and serves one client at a time.\n"
		"\n"
		"  host[:port]  server address, IPv6 goes in brackets: [::1]:27000 (default: %s:%d)\n"
		"  -p PORT      server port when the address doesn't have one\n"
		"  -r MS        reconnect delay in milliseconds, 0 disables reconnecting (default: %d)\n"
		"  -e CMD       send CMD every time the connection is up, can be repeated\n"
		"  -b LINES     scrollback size in lines (default: %d)\n"
		"  -n           no colors, also implied by the NO_COLOR environment variable\n"
		"  -m           don't capture the mouse, keeps the terminal's own text selection\n"
		"  -M           run as an MCP server speaking JSON-RPC on stdin and stdout instead\n"
		"               of the TUI, the connection is opened by the xrcon_connect tool\n"
		"  -v           print version and exit\n"
		"  -h           print this help and exit\n"
		"\n"
		"Press F1 inside for key bindings.\n",
		argv0, argv0, DEFAULT_HOST, DEFAULT_PORT, DEFAULT_RECONNECT_MS, DEFAULT_SCROLLBACK );
}

static qboolean ParseNumber( const char *s, long min, long max, long *out )
{
	char *end;
	long v = strtol( s, &end, 10 );

	if( end == s || *end != '\0' || v < min || v > max )
		return false;

	*out = v;
	return true;
}

static qboolean ParseAddress( const char *s, char *host, size_t size, int *port )
{
	const char *port_str = NULL;
	size_t len;
	long v;

	if( s[0] == '[' )
	{
		const char *end = Q_strchr( s, ']' );

		if( !end || ( end[1] != '\0' && end[1] != ':' ))
			return false;

		s++;
		len = end - s;

		if( end[1] == ':' )
			port_str = end + 2;
	}
	else
	{
		const char *colon = Q_strchr( s, ':' );

		len = Q_strlen( s );

		// more than one colon is a bare IPv6 address without a port
		if( colon && !Q_strchr( colon + 1, ':' ))
		{
			len = colon - s;
			port_str = colon + 1;
		}
	}

	if( len == 0 || len >= size )
		return false;

	memcpy( host, s, len );
	host[len] = '\0';

	if( port_str )
	{
		if( !ParseNumber( port_str, 1, 65535, &v ))
			return false;

		*port = (int)v;
	}

	return true;
}

static qboolean ArgError( const char *argv0, int opt, const char *value )
{
	fprintf( stderr, "%s: invalid -%c value '%s'\n", argv0, opt, value );
	return false;
}

static qboolean ParseArgs( int argc, char **argv, args_t *args )
{
	const char *no_color = getenv( "NO_COLOR" );
	int port = DEFAULT_PORT;
	long v;
	int c;

	memset( args, 0, sizeof( *args ));
	Q_strncpy( args->host, DEFAULT_HOST, sizeof( args->host ));
	args->reconnect_ms = DEFAULT_RECONNECT_MS;
	args->mouse = true;
	args->ui.scrollback = DEFAULT_SCROLLBACK;
	args->ui.colors = !no_color || !no_color[0];

	while(( c = getopt( argc, argv, "b:e:hmMnp:r:v" )) != -1 )
	{
		switch( c )
		{
		case 'b':
			if( !ParseNumber( optarg, 100, 1000000, &v ))
				return ArgError( argv[0], c, optarg );
			args->ui.scrollback = (size_t)v;
			break;
		case 'e':
			if( args->exec_count >= MAX_EXEC )
			{
				fprintf( stderr, "%s: too many -e commands, %d at most\n", argv[0], MAX_EXEC );
				return false;
			}
			if( Q_strlen( optarg ) > NET_MAX_COMMAND )
				return ArgError( argv[0], c, optarg );
			args->exec[args->exec_count++] = optarg;
			break;
		case 'h':
			PrintUsage( stdout, argv[0] );
			exit( 0 );
		case 'm':
			args->mouse = false;
			break;
		case 'M':
			args->mcp = true;
			break;
		case 'n':
			args->ui.colors = false;
			break;
		case 'p':
			if( !ParseNumber( optarg, 1, 65535, &v ))
				return ArgError( argv[0], c, optarg );
			port = (int)v;
			break;
		case 'r':
			if( !ParseNumber( optarg, 0, 3600000, &v ))
				return ArgError( argv[0], c, optarg );
			args->reconnect_ms = (int)v;
			break;
		case 'v':
			printf( "xrcon-tui build %d, commit %s, branch %s\n", Q_buildnum(), g_buildcommit, g_buildbranch );
			exit( 0 );
		default:
			PrintUsage( stderr, argv[0] );
			return false;
		}
	}

	if( argc - optind > 1 )
	{
		PrintUsage( stderr, argv[0] );
		return false;
	}

	if( optind < argc && !ParseAddress( argv[optind], args->host, sizeof( args->host ), &port ))
	{
		fprintf( stderr, "%s: invalid address '%s'\n", argv[0], argv[optind] );
		return false;
	}

	args->port = port;

	if( Q_strchr( args->host, ':' ))
		Q_snprintf( args->target, sizeof( args->target ), "[%s]:%d", args->host, port );
	else
		Q_snprintf( args->target, sizeof( args->target ), "%s:%d", args->host, port );

	args->ui.target = args->target;
	return true;
}

static void OnPrint( void *user, const char *text, size_t len )
{
	UI_AddText((ui_t *)user, SB_LINE_SERVER, text, len );
}

static void OnMessage( void *user, int kind, const char *msg )
{
	UI_Message((ui_t *)user, kind == NET_MSG_ERROR ? SB_LINE_ERROR : SB_LINE_INFO, "%s", msg );
}

static void ProcessInput( void )
{
	sys_key_t keys[64];
	int count;

	while(( count = Sys_ReadKeys( keys, ARRAYSIZE( keys ))) > 0 )
	{
		for( int i = 0; i < count; i++ )
			UI_HandleKey( &ui, &keys[i] );
	}
}

static int WaitTimeout( int64_t now, int64_t last_draw, int64_t next_connect )
{
	int timeout = 1000;

	if( ui.dirty )
		timeout = (int)Q_max( last_draw + FRAME_INTERVAL_MS - now, 0 );

	if( next_connect != 0 && next_connect - now < timeout )
		timeout = (int)Q_max( next_connect - now, 0 );

	return Net_Timeout( &net, now, timeout );
}

static int RunMCP( void )
{
	log_buf_t log;
	mcp_t mcp;
	char buf[4096];
	qboolean eof = false;

	if( LogBuf_Init( &log, MCP_LOG_SIZE ) < 0 )
	{
		fprintf( stderr, "[xrcon-mcp] failed to allocate log buffer\n" );
		return 1;
	}

	MCP_Init( &mcp, &net, &log );
	Net_Init( &net, MCP_OnPrint, MCP_OnMessage, &mcp );

	if( !Sys_InitStdio())
	{
		LogBuf_Free( &log );
		return 1;
	}

	fprintf( stderr, "[xrcon-mcp] starting; idle until xrcon_connect tool is called\n" );

	while( !eof )
	{
		int64_t now = Sys_Milliseconds();
		int timeout = Net_Timeout( &net, now, MCP_PendingPollTimeout( &mcp, now, 1000 ));
		int events = Sys_Wait( net.sock, Net_WantsWrite( &net ), timeout );

		now = Sys_Milliseconds();

		if( FBitSet( events, SYS_EV_QUIT ))
			break;

		if( FBitSet( events, SYS_EV_SOCK_WRITE ))
			Net_OnWritable( &net, now );

		if( FBitSet( events, SYS_EV_SOCK_READ ))
			Net_OnReadable( &net );

		if( FBitSet( events, SYS_EV_INPUT ))
		{
			int n;

			while(( n = Sys_ReadStdin( buf, sizeof( buf ))) > 0 )
				MCP_Feed( &mcp, buf, n, now );

			eof = n < 0;
		}

		Net_Frame( &net, now );
		MCP_TickPending( &mcp, now );
		MCP_FlushPendingNotifications( &mcp );
	}

	if( eof )
		fprintf( stderr, "[xrcon-mcp] stdin closed; exiting\n" );

	Net_Shutdown( &net );
	Sys_Shutdown();
	MCP_Free( &mcp );
	LogBuf_Free( &log );
	return 0;
}

static int RunTUI( const args_t *args )
{
	if( !UI_Init( &ui, &net, &args->ui ))
	{
		UI_Shutdown( &ui );
		return 1;
	}

	Net_Init( &net, OnPrint, OnMessage, &ui );

	if( !Sys_Init( args->mouse ))
	{
		UI_Shutdown( &ui );
		return 1;
	}

	UI_Resize( &ui );
	UI_Message( &ui, SB_LINE_INFO, "xrcon-tui build %d, press F1 for key bindings", Q_buildnum());

	int64_t next_connect = Sys_Milliseconds();
	int64_t last_draw = 0;
	qboolean quiet = false;
	qboolean was_up = false;

	while( !ui.quit )
	{
		int64_t now = Sys_Milliseconds();

		if( ui.want_reconnect )
		{
			ui.want_reconnect = false;
			Net_Disconnect( &net, NULL );
			next_connect = now;
			quiet = false;
		}

		if( net.state == NET_OFFLINE && next_connect != 0 && now >= next_connect )
		{
			next_connect = 0;
			Net_Connect( &net, args->host, args->port, quiet, now );

			// report only the first failure in a row, the status bar shows the rest
			quiet = true;
		}

		Net_Frame( &net, now );

		qboolean up = Net_IsUp( &net );
		if( up && !was_up )
		{
			for( int i = 0; i < args->exec_count; i++ )
				Net_Send( &net, args->exec[i], Q_strlen( args->exec[i] ));

			quiet = false;
		}
		was_up = up;

		if( net.state == NET_OFFLINE && next_connect == 0 && args->reconnect_ms > 0 )
			next_connect = now + args->reconnect_ms;

		ui.reconnecting = net.state == NET_OFFLINE && next_connect != 0;
		UI_Tick( &ui );

		if( ui.dirty && now - last_draw >= FRAME_INTERVAL_MS )
		{
			UI_Draw( &ui );
			last_draw = now;
		}

		int events = Sys_Wait( net.sock, Net_WantsWrite( &net ), WaitTimeout( now, last_draw, next_connect ));

		now = Sys_Milliseconds();

		if( FBitSet( events, SYS_EV_QUIT ))
			break;

		if( FBitSet( events, SYS_EV_RESIZE ))
			UI_Resize( &ui );

		if( FBitSet( events, SYS_EV_SOCK_WRITE ))
			Net_OnWritable( &net, now );

		if( FBitSet( events, SYS_EV_SOCK_READ ))
			Net_OnReadable( &net );

		if( FBitSet( events, SYS_EV_INPUT ))
			ProcessInput();
	}

	Net_Shutdown( &net );
	Sys_Shutdown();
	UI_Shutdown( &ui );
	return 0;
}

int main( int argc, char **argv )
{
	args_t args;

	if( !ParseArgs( argc, argv, &args ))
		return 1;

	if( args.mcp )
		return RunMCP();

	return RunTUI( &args );
}
