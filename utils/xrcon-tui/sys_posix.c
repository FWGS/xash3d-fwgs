/*
sys_posix.c - POSIX terminal, keyboard and socket layer for xrcon-tui
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
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>

#include "crtlib.h"
#include "utflib.h"
#include "xash3d_mathlib.h"
#include "text.h"
#include "sys.h"

#define TERM_INPUT_SIZE     4096
#define TERM_ESC_TIMEOUT_MS 50 // how long a lone ESC waits for the rest of a sequence
#define TERM_SEQ_MAX        32 // longer escape sequences are garbage
#define TERM_PASTE_END      "\x1b[201~"

// alternate screen, no autowrap, bracketed paste
#define TERM_ENTER     "\x1b[?1049h\x1b[?7l\x1b[?2004h"
#define TERM_MOUSE_ON  "\x1b[?1000h\x1b[?1006h"
#define TERM_LEAVE     "\x1b[?1000l\x1b[?1006l\x1b[?2004l\x1b[?7h\x1b[0m\x1b[?25h\x1b[?1049l"

static struct
{
	qboolean active;
	qboolean mouse;
	struct termios saved;
	int wake[2]; // self-pipe, signal handlers write to it to interrupt select()

	volatile sig_atomic_t resized;
	volatile sig_atomic_t quit;

	uint8_t in[TERM_INPUT_SIZE];
	size_t in_len;
	int64_t esc_since; // when an incomplete sequence was first seen, 0 if none
	qboolean paste;
	qboolean paste_cr;

	qboolean stdio; // MCP mode, stdin is raw bytes
	qboolean eof;
} term = { .wake = { -1, -1 } };

static void Term_WriteRaw( const char *s, size_t len )
{
	while( len > 0 )
	{
		ssize_t n = write( STDOUT_FILENO, s, len );

		if( n < 0 )
		{
			if( errno == EINTR )
				continue;

			return;
		}

		s += n;
		len -= n;
	}
}

static qboolean Term_EnterRaw( void )
{
	struct termios raw = term.saved;

	raw.c_iflag &= ~( IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON );
	raw.c_oflag &= ~OPOST;
	raw.c_lflag &= ~( ECHO | ECHONL | ICANON | ISIG | IEXTEN );
	raw.c_cflag &= ~( CSIZE | PARENB );
	raw.c_cflag |= CS8;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if( tcsetattr( STDIN_FILENO, TCSAFLUSH, &raw ) < 0 )
		return false;

	Term_WriteRaw( TERM_ENTER, sizeof( TERM_ENTER ) - 1 );

	if( term.mouse )
		Term_WriteRaw( TERM_MOUSE_ON, sizeof( TERM_MOUSE_ON ) - 1 );

	return true;
}

// only async-signal-safe calls here, it also runs from the crash handler
static void Term_LeaveRaw( void )
{
	Term_WriteRaw( TERM_LEAVE, sizeof( TERM_LEAVE ) - 1 );
	tcsetattr( STDIN_FILENO, TCSAFLUSH, &term.saved );
}

static void Term_Wake( void )
{
	char c = 0;

	if( term.wake[1] >= 0 )
	{
		if( write( term.wake[1], &c, 1 ) < 0 )
		{
			// pipe is full, select() wakes up anyway
		}
	}
}

static void Term_Signal( int sig )
{
	int saved = errno;

	if( sig == SIGWINCH )
		term.resized = 1;
	else
		term.quit = 1;

	Term_Wake();
	errno = saved;
}

static void Term_Crash( int sig )
{
	if( term.active )
		Term_LeaveRaw();

	signal( sig, SIG_DFL );
	raise( sig );
}

static void Term_SetSignal( int sig, void ( *handler )( int ))
{
	struct sigaction sa;

	memset( &sa, 0, sizeof( sa ));
	sa.sa_handler = handler;
	sigemptyset( &sa.sa_mask );
	sigaction( sig, &sa, NULL );
}

static qboolean Term_OpenWakePipe( void )
{
	if( pipe( term.wake ) < 0 )
		return false;

	for( int i = 0; i < 2; i++ )
	{
		fcntl( term.wake[i], F_SETFL, fcntl( term.wake[i], F_GETFL, 0 ) | O_NONBLOCK );
		fcntl( term.wake[i], F_SETFD, FD_CLOEXEC );
	}

	return true;
}

qboolean Sys_Init( qboolean mouse )
{
	if( !isatty( STDIN_FILENO ) || !isatty( STDOUT_FILENO ))
	{
		fprintf( stderr, "xrcon-tui: stdin and stdout must be a terminal\n" );
		return false;
	}

	if( tcgetattr( STDIN_FILENO, &term.saved ) < 0 )
	{
		fprintf( stderr, "xrcon-tui: tcgetattr: %s\n", strerror( errno ));
		return false;
	}

	if( !Term_OpenWakePipe())
	{
		fprintf( stderr, "xrcon-tui: pipe: %s\n", strerror( errno ));
		return false;
	}

	signal( SIGPIPE, SIG_IGN );
	Term_SetSignal( SIGWINCH, Term_Signal );
	Term_SetSignal( SIGINT, Term_Signal );
	Term_SetSignal( SIGTERM, Term_Signal );
	Term_SetSignal( SIGHUP, Term_Signal );
	Term_SetSignal( SIGSEGV, Term_Crash );
	Term_SetSignal( SIGBUS, Term_Crash );
	Term_SetSignal( SIGFPE, Term_Crash );
	Term_SetSignal( SIGILL, Term_Crash );
	Term_SetSignal( SIGABRT, Term_Crash );

	term.mouse = mouse;

	if( !Term_EnterRaw())
	{
		fprintf( stderr, "xrcon-tui: tcsetattr: %s\n", strerror( errno ));
		return false;
	}

	term.active = true;
	atexit( Sys_Shutdown );
	return true;
}

qboolean Sys_InitStdio( void )
{
	if( !Term_OpenWakePipe())
	{
		fprintf( stderr, "xrcon-tui: pipe: %s\n", strerror( errno ));
		return false;
	}

	signal( SIGPIPE, SIG_IGN );
	Term_SetSignal( SIGINT, Term_Signal );
	Term_SetSignal( SIGTERM, Term_Signal );
	Term_SetSignal( SIGHUP, Term_Signal );
	term.stdio = true;
	return true;
}

void Sys_Shutdown( void )
{
	if( !term.active )
		return;

	term.active = false;
	Term_LeaveRaw();
}

void Sys_GetSize( int *cols, int *rows )
{
	struct winsize ws;

	if( ioctl( STDOUT_FILENO, TIOCGWINSZ, &ws ) == 0 && ws.ws_col > 0 && ws.ws_row > 0 )
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return;
	}

	*cols = 80;
	*rows = 24;
}

void Sys_Write( const char *data, size_t len )
{
	Term_WriteRaw( data, len );
}

int64_t Sys_Milliseconds( void )
{
	struct timespec ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int Term_Mods( int param )
{
	int bits = param > 1 ? param - 1 : 0;
	int mods = 0;

	if( FBitSet( bits, BIT( 0 )))
		SetBits( mods, TK_MOD_SHIFT );

	if( FBitSet( bits, BIT( 1 ) | BIT( 3 )))
		SetBits( mods, TK_MOD_ALT );

	if( FBitSet( bits, BIT( 2 )))
		SetBits( mods, TK_MOD_CTRL );

	return mods;
}

static void Term_MouseButton( int button, sys_key_t *key )
{
	// only the vertical wheel, buttons 64 and 65
	if( !FBitSet( button, 64 ) || FBitSet( button, 2 ))
		return;

	key->key = FBitSet( button, 1 ) ? TK_WHEEL_DOWN : TK_WHEEL_UP;

	if( FBitSet( button, 4 ))
		SetBits( key->mods, TK_MOD_SHIFT );

	if( FBitSet( button, 8 ))
		SetBits( key->mods, TK_MOD_ALT );

	if( FBitSet( button, 16 ))
		SetBits( key->mods, TK_MOD_CTRL );
}

static int Term_TildeKey( int code )
{
	if( code >= 11 && code <= 15 )
		return TK_F1 + code - 11;

	if( code >= 17 && code <= 21 )
		return TK_F1 + 5 + code - 17;

	if( code == 23 || code == 24 )
		return TK_F1 + 10 + code - 23;

	switch( code )
	{
	case 1:
	case 7:
		return TK_HOME;
	case 3:
		return TK_DELETE;
	case 4:
	case 8:
		return TK_END;
	case 5:
		return TK_PGUP;
	case 6:
		return TK_PGDN;
	}

	return TK_NONE;
}

static int Term_LetterKey( uint8_t c )
{
	switch( c )
	{
	case 'A': return TK_UP;
	case 'B': return TK_DOWN;
	case 'C': return TK_RIGHT;
	case 'D': return TK_LEFT;
	case 'H': return TK_HOME;
	case 'F': return TK_END;
	case 'P': return TK_F1;
	case 'Q': return TK_F1 + 1;
	case 'R': return TK_F1 + 2;
	case 'S': return TK_F1 + 3;
	}

	return TK_NONE;
}

static void Term_DecodeCSI( uint8_t priv, const int *params, int count, uint8_t final, sys_key_t *key )
{
	if( priv == '<' )
	{
		if( final == 'M' )
			Term_MouseButton( params[0], key );
		return;
	}

	if( priv != 0 )
		return;

	key->mods = Term_Mods( count > 1 ? params[1] : 0 );

	if( final == '~' )
	{
		if( params[0] == 200 )
			term.paste = true;
		else
			key->key = Term_TildeKey( params[0] );
	}
	else
	{
		key->key = Term_LetterKey( final );
	}
}

// returns the number of bytes consumed, 0 if more are needed
static size_t Term_ParseCSI( const uint8_t *s, size_t len, sys_key_t *key )
{
	int params[4] = { 0 };
	int count = 1;
	uint8_t priv = 0;
	size_t i = 2;

	if( len <= i )
		return 0;

	if( s[i] >= 0x3c && s[i] <= 0x3f )
		priv = s[i++];

	for( ; i < len && i < TERM_SEQ_MAX; i++ )
	{
		uint8_t c = s[i];

		if( c >= '0' && c <= '9' )
		{
			if( params[count - 1] < 100000 )
				params[count - 1] = params[count - 1] * 10 + c - '0';
		}
		else if( c == ';' || c == ':' )
		{
			if( count < (int)ARRAYSIZE( params ))
				count++;
		}
		else if( c >= 0x40 && c <= 0x7e )
		{
			Term_DecodeCSI( priv, params, count, c, key );
			return i + 1;
		}
		else if( c < 0x20 || c > 0x2f )
		{
			// not a CSI byte, drop what we have
			return i;
		}
	}

	return i >= TERM_SEQ_MAX ? i : 0;
}

static size_t Term_ParseSS3( const uint8_t *s, size_t len, sys_key_t *key )
{
	if( len < 3 )
		return 0;

	if( s[2] == 'M' )
		key->key = TK_ENTER;
	else
		key->key = Term_LetterKey( s[2] );

	return 3;
}

static size_t Term_ParseText( const uint8_t *s, size_t len, qboolean flush, sys_key_t *key )
{
	size_t need = Q_UTF8SequenceLength( s[0] );

	if( len < need && !flush )
	{
		qboolean valid = true;

		for( size_t i = 1; i < len; i++ )
		{
			if(( s[i] & 0xc0 ) != 0x80 )
				valid = false;
		}

		if( valid )
			return 0;
	}

	key->key = TK_CHAR;
	return Q_DecodeUTF8Char((const char *)s, len, &key->ch );
}

static size_t Term_ParsePlain( const uint8_t *s, size_t len, qboolean flush, sys_key_t *key )
{
	uint8_t c = s[0];

	switch( c )
	{
	case '\r':
	case '\n':
		key->key = TK_ENTER;
		return 1;
	case 0x08:
	case 0x7f:
		key->key = TK_BACKSPACE;
		return 1;
	case 0x1b:
		key->key = TK_ESCAPE;
		return 1;
	}

	if( c < 0x20 )
	{
		if( c >= 1 && c <= 26 )
		{
			key->key = TK_CHAR;
			key->ch = 'a' + c - 1;
			key->mods = TK_MOD_CTRL;
		}

		return 1;
	}

	return Term_ParseText( s, len, flush, key );
}

static size_t Term_ParsePaste( const uint8_t *s, size_t len, qboolean flush, sys_key_t *key )
{
	const size_t end_len = sizeof( TERM_PASTE_END ) - 1;
	uint8_t c = s[0];

	if( c == 0x1b )
	{
		size_t n = Q_min( len, end_len );

		if( memcmp( s, TERM_PASTE_END, n ))
			return 1; // stray escape inside the pasted text

		if( n < end_len )
			return flush ? n : 0;

		term.paste = false;
		return n;
	}

	if( c == '\r' || c == '\n' )
	{
		qboolean crlf = c == '\n' && term.paste_cr;

		term.paste_cr = c == '\r';

		if( !crlf )
		{
			key->key = TK_CHAR;
			key->ch = '\n';
			key->mods = TK_MOD_PASTE;
		}

		return 1;
	}

	term.paste_cr = false;

	if( c == '\t' )
	{
		key->key = TK_CHAR;
		key->ch = ' ';
		key->mods = TK_MOD_PASTE;
		return 1;
	}

	if( c < 0x20 )
		return 1;

	size_t n = Term_ParseText( s, len, flush, key );
	key->mods = TK_MOD_PASTE;
	return n;
}

static size_t Term_ParseKey( const uint8_t *s, size_t len, qboolean flush, sys_key_t *key )
{
	size_t n;

	memset( key, 0, sizeof( *key ));

	if( term.paste )
		return Term_ParsePaste( s, len, flush, key );

	if( s[0] != 0x1b )
		return Term_ParsePlain( s, len, flush, key );

	if( len == 1 )
		n = 0;
	else if( s[1] == '[' )
		n = Term_ParseCSI( s, len, key );
	else if( s[1] == 'O' )
		n = Term_ParseSS3( s, len, key );
	else if( s[1] == 0x1b )
		n = 1;
	else
	{
		// ESC prefix means Alt
		n = Term_ParsePlain( s + 1, len - 1, flush, key );

		if( n != 0 )
		{
			SetBits( key->mods, TK_MOD_ALT );
			n++;
		}
	}

	if( n != 0 )
	{
		if( n == 1 )
			key->key = TK_ESCAPE;
		return n;
	}

	if( !flush )
		return 0;

	// the rest of the sequence never came, it was a plain Escape
	memset( key, 0, sizeof( *key ));
	key->key = TK_ESCAPE;
	return 1;
}

int Sys_ReadKeys( sys_key_t *keys, int max )
{
	int64_t now = Sys_Milliseconds();
	qboolean flush = term.esc_since != 0 && now - term.esc_since >= TERM_ESC_TIMEOUT_MS;
	qboolean incomplete = false;
	size_t pos = 0;
	int count = 0;

	while( pos < term.in_len && count < max )
	{
		sys_key_t key;
		size_t n = Term_ParseKey( term.in + pos, term.in_len - pos, flush, &key );

		if( n == 0 )
		{
			incomplete = true;
			break;
		}

		pos += n;

		if( key.key != TK_NONE )
			keys[count++] = key;
	}

	memmove( term.in, term.in + pos, term.in_len - pos );
	term.in_len -= pos;

	if( !incomplete )
		term.esc_since = 0;
	else if( term.esc_since == 0 || flush )
		term.esc_since = now;

	return count;
}

int Sys_ReadStdin( char *buf, size_t size )
{
	if( term.in_len == 0 )
		return term.eof ? -1 : 0;

	size_t n = Q_min( size, term.in_len );

	memcpy( buf, term.in, n );
	memmove( term.in, term.in + n, term.in_len - n );
	term.in_len -= n;
	return (int)n;
}

int Sys_Wait( sys_socket_t sock, qboolean want_write, int timeout_ms )
{
	fd_set rfds, wfds;
	struct timeval tv;
	int maxfd = Q_max( STDIN_FILENO, term.wake[0] );
	int events = 0;

	if( term.esc_since != 0 )
	{
		int left = (int)( term.esc_since + TERM_ESC_TIMEOUT_MS - Sys_Milliseconds());

		if( left <= 0 )
			return SYS_EV_INPUT;

		if( timeout_ms < 0 || left < timeout_ms )
			timeout_ms = left;
	}

	FD_ZERO( &rfds );
	FD_ZERO( &wfds );
	FD_SET( term.wake[0], &rfds );

	if( !term.eof )
		FD_SET( STDIN_FILENO, &rfds );

	if( sock != SYS_INVALID_SOCKET )
	{
		FD_SET( sock, &rfds );

		if( want_write )
			FD_SET( sock, &wfds );

		maxfd = Q_max( maxfd, sock );
	}

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = ( timeout_ms % 1000 ) * 1000;

	if( select( maxfd + 1, &rfds, &wfds, NULL, timeout_ms < 0 ? NULL : &tv ) < 0 )
	{
		FD_ZERO( &rfds );
		FD_ZERO( &wfds );
	}

	if( FD_ISSET( term.wake[0], &rfds ))
	{
		char drain[64];

		while( read( term.wake[0], drain, sizeof( drain )) > 0 )
			continue;
	}

	if( term.resized )
	{
		term.resized = 0;
		SetBits( events, SYS_EV_RESIZE );
	}

	if( term.quit )
		SetBits( events, SYS_EV_QUIT );

	if( FD_ISSET( STDIN_FILENO, &rfds ) && term.in_len < sizeof( term.in ))
	{
		ssize_t n = read( STDIN_FILENO, term.in + term.in_len, sizeof( term.in ) - term.in_len );

		if( n > 0 )
		{
			term.in_len += n;
			SetBits( events, SYS_EV_INPUT );
		}
		else if( n == 0 || ( errno != EINTR && errno != EAGAIN ))
		{
			// the terminal is gone, a pipe is drained by Sys_ReadStdin first
			if( term.stdio )
			{
				term.eof = true;
				SetBits( events, SYS_EV_INPUT );
			}
			else
			{
				SetBits( events, SYS_EV_QUIT );
			}
		}
	}

	if( term.esc_since != 0 && Sys_Milliseconds() - term.esc_since >= TERM_ESC_TIMEOUT_MS )
		SetBits( events, SYS_EV_INPUT );

	if( sock != SYS_INVALID_SOCKET )
	{
		if( FD_ISSET( sock, &rfds ))
			SetBits( events, SYS_EV_SOCK_READ );

		if( FD_ISSET( sock, &wfds ))
			SetBits( events, SYS_EV_SOCK_WRITE );
	}

	return events;
}

static qboolean Sys_WouldBlock( int err )
{
#if EAGAIN != EWOULDBLOCK
	if( err == EWOULDBLOCK )
		return true;
#endif // EAGAIN != EWOULDBLOCK

	return err == EAGAIN;
}

sys_socket_t Sys_SocketOpen( int family )
{
	int one = 1;
	int sock = socket( family, SOCK_STREAM, IPPROTO_TCP );

	if( sock < 0 )
		return SYS_INVALID_SOCKET;

	int flags = fcntl( sock, F_GETFL, 0 );
	if( flags < 0 || fcntl( sock, F_SETFL, flags | O_NONBLOCK ) < 0 || sock >= FD_SETSIZE )
	{
		close( sock );
		return SYS_INVALID_SOCKET;
	}

	fcntl( sock, F_SETFD, FD_CLOEXEC );
	setsockopt( sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof( one ));
	return sock;
}

void Sys_SocketClose( sys_socket_t sock )
{
	close( sock );
}

int Sys_SocketConnect( sys_socket_t sock, const struct sockaddr *addr, size_t addr_len )
{
	if( connect( sock, addr, (socklen_t)addr_len ) == 0 )
		return 0;

	if( errno == EINPROGRESS || errno == EINTR )
		return SYS_SOCK_AGAIN;

	return SYS_SOCK_ERROR;
}

int Sys_SocketConnectResult( sys_socket_t sock )
{
	int err = 0;
	socklen_t len = sizeof( err );

	if( getsockopt( sock, SOL_SOCKET, SO_ERROR, &err, &len ) < 0 )
		return errno;

	return err;
}

int Sys_SocketSend( sys_socket_t sock, const void *data, size_t len )
{
	while( true )
	{
		ssize_t n = send( sock, data, len, 0 );

		if( n >= 0 )
			return (int)n;

		if( errno == EINTR )
			continue;

		return Sys_WouldBlock( errno ) ? SYS_SOCK_AGAIN : SYS_SOCK_ERROR;
	}
}

int Sys_SocketRecv( sys_socket_t sock, void *data, size_t len )
{
	while( true )
	{
		ssize_t n = recv( sock, data, len, 0 );

		if( n >= 0 )
			return (int)n;

		if( errno == EINTR )
			continue;

		return Sys_WouldBlock( errno ) ? SYS_SOCK_AGAIN : SYS_SOCK_ERROR;
	}
}

int Sys_SocketError( void )
{
	return errno;
}

const char *Sys_ErrorString( int code )
{
	return strerror( code );
}

const char *Sys_ResolveErrorString( int code )
{
#ifdef EAI_SYSTEM
	if( code == EAI_SYSTEM )
		return strerror( errno );
#endif // EAI_SYSTEM

	return gai_strerror( code );
}
