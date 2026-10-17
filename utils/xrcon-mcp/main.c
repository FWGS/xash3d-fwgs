/*
main.c - xrcon-mcp entry point and event loop
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
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include "log_buf.h"
#include "xrcon.h"
#include "mcp.h"

#define STDIN_LINE_CAP ( 1024 * 1024 )

typedef struct
{
	size_t log_cap;
} args_t;

static int ParseArgs( int argc, char **argv, args_t *out )
{
	out->log_cap = 262144;

	int c;
	while(( c = getopt( argc, argv, "b:h" )) != -1 )
	{
		switch( c )
		{
		case 'b':
		{
			long v = atol( optarg );
			if( v > 4096 )
				out->log_cap = (size_t)v;
			break;
		}
		case 'h':
			fprintf( stderr,
				"Usage: %s [-b BYTES]\n"
				"\n"
				"MCP (Model Context Protocol) server bridging stdio to an Xash3D engine's\n"
				"XRCON server. Speaks JSON-RPC 2.0 over stdin/stdout.\n"
				"\n"
				"Use the xrcon_connect tool to attach to an engine.\n"
				"\n"
				"  -b BYTES  Log ring buffer size in bytes (default: 262144)\n",
				argv[0] );
			exit( 0 );
		default:
			return -1;
		}
	}
	return 0;
}

static int64_t MonoMs( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void OnXrconLogChanged( void *user )
{
	mcp_t *m = (mcp_t *)user;
	MCP_MarkLogDirty( m );
}

typedef struct
{
	char *buf;
	size_t len;
	size_t cap;
	int eof;
} stdin_buf_t;

static int StdinAccum( stdin_buf_t *sb )
{
	if( !sb->buf )
	{
		sb->cap = 4096;
		sb->buf = (char *)malloc( sb->cap );
		if( !sb->buf )
			return -1;
	}

	while( 1 )
	{
		if( sb->len >= sb->cap )
		{
			size_t ncap = sb->cap * 2;
			if( ncap > STDIN_LINE_CAP )
			{
				fprintf( stderr, "[xrcon-mcp] stdin line exceeded %d bytes, discarding\n", STDIN_LINE_CAP );
				sb->len = 0;
				return 0;
			}
			char *nb = (char *)realloc( sb->buf, ncap );
			if( !nb )
				return -1;
			sb->buf = nb;
			sb->cap = ncap;
		}

		ssize_t n = read( 0, sb->buf + sb->len, sb->cap - sb->len );
		if( n > 0 )
		{
			sb->len += (size_t)n;
			continue;
		}
		if( n == 0 )
		{
			sb->eof = 1;
			return 0;
		}
		if( errno == EAGAIN )
			return 0;
		if( errno == EINTR )
			continue;

		fprintf( stderr, "[xrcon-mcp] read(stdin): %s\n", strerror( errno ));
		sb->eof = 1;
		return -1;
	}
}

static void StdinDrainLines( stdin_buf_t *sb, mcp_t *mcp, int64_t now_ms )
{
	size_t start = 0;
	for( size_t i = 0; i < sb->len; i++ )
	{
		if( sb->buf[i] == '\n' )
		{
			size_t llen = i - start;
			MCP_HandleLine( mcp, sb->buf + start, llen, now_ms );
			start = i + 1;
		}
	}
	if( start > 0 )
	{
		memmove( sb->buf, sb->buf + start, sb->len - start );
		sb->len -= start;
	}
}

static volatile sig_atomic_t g_stop = 0;
static void OnSignal( int s ) { (void)s; g_stop = 1; }

int main( int argc, char **argv )
{
	args_t args;
	if( ParseArgs( argc, argv, &args ) < 0 )
		return 1;

	signal( SIGINT,  OnSignal );
	signal( SIGTERM, OnSignal );
	signal( SIGPIPE, SIG_IGN );

	int sflags = fcntl( 0, F_GETFL, 0 );
	if( sflags >= 0 )
		fcntl( 0, F_SETFL, sflags | O_NONBLOCK );

	log_buf_t log;
	if( LogBuf_Init( &log, args.log_cap ) < 0 )
	{
		fprintf( stderr, "[xrcon-mcp] failed to allocate log buffer\n" );
		return 1;
	}

	xrcon_t xrcon;
	mcp_t mcp;
	MCP_Init( &mcp, &xrcon, &log );
	XRcon_Init( &xrcon, &log, OnXrconLogChanged, &mcp );

	stdin_buf_t sb = { 0 };

	fprintf( stderr, "[xrcon-mcp] starting; idle until xrcon_connect tool is called\n" );

	while( !g_stop )
	{
		int64_t now = MonoMs();

		struct pollfd pfds[2];
		int nfds = 0;

		pfds[nfds].fd = 0;
		pfds[nfds].events = sb.eof ? 0 : POLLIN;
		nfds++;

		int xfd = XRcon_Socket( &xrcon );
		int xidx = -1;
		if( xfd >= 0 )
		{
			pfds[nfds].fd = xfd;
			pfds[nfds].events = 0;
			if( XRcon_WantsRead( &xrcon ))
				pfds[nfds].events |= POLLIN;
			if( XRcon_WantsWrite( &xrcon ))
				pfds[nfds].events |= POLLOUT;
			xidx = nfds;
			nfds++;
		}

		int timeout_ms = 1000;
		if( mcp.log_dirty && mcp.log_subscribed )
			timeout_ms = 0;

		timeout_ms = MCP_PendingPollTimeout( &mcp, now, timeout_ms );

		int rc = poll( pfds, nfds, timeout_ms );
		if( rc < 0 )
		{
			if( errno == EINTR )
				continue;
			fprintf( stderr, "[xrcon-mcp] poll: %s\n", strerror( errno ));
			break;
		}

		now = MonoMs();

		if( pfds[0].revents & ( POLLIN | POLLHUP | POLLERR ))
		{
			StdinAccum( &sb );
			StdinDrainLines( &sb, &mcp, now );
			if( sb.eof )
			{
				fprintf( stderr, "[xrcon-mcp] stdin closed; exiting\n" );
				break;
			}
		}

		if( xidx >= 0 )
		{
			short re = pfds[xidx].revents;
			if( re & POLLOUT )
				XRcon_OnWritable( &xrcon );
			if( re & ( POLLIN | POLLHUP | POLLERR ))
				XRcon_OnReadable( &xrcon );
		}

		MCP_TickPending( &mcp, now );
		MCP_FlushPendingNotifications( &mcp );
	}

	XRcon_Free( &xrcon );
	LogBuf_Free( &log );
	free( sb.buf );
	return 0;
}
