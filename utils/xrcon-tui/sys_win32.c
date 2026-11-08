/*
sys_win32.c - Windows console, keyboard and socket layer for xrcon-tui
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
#include <limits.h>
#include <io.h>
#include <fcntl.h>

#include "crtlib.h"
#include "utflib.h"
#include "xash3d_mathlib.h"
#include "sys.h"

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

#define CON_INPUT_RECORDS 128
#define CON_KEY_QUEUE     512
#define CON_STDIN_BUFFER  65536

// alternate screen, no autowrap; keys and mouse arrive as console input records
#define CON_ENTER "\x1b[?1049h\x1b[?7l"
#define CON_LEAVE "\x1b[?7h\x1b[0m\x1b[?25h\x1b[?1049l"

static struct
{
	qboolean active;
	HANDLE hin;
	HANDLE hout;
	DWORD saved_in_mode;
	DWORD saved_out_mode;
	HANDLE wake; // set by the console control handler
	WSAEVENT sock_event;
	int connect_error;
	volatile LONG quit;
	int cols;
	int rows;

	utfstate_t utf16;
	sys_key_t keys[CON_KEY_QUEUE];
	int key_count;

	wchar_t *wbuf;
	size_t wbuf_cap;
	char errbuf[256];

	// MCP mode, anonymous pipes can't be waited on, so a thread reads stdin
	qboolean stdio;
	HANDLE stdin_ready; // set by the reader thread
	HANDLE stdin_space; // set when Sys_ReadStdin frees the buffer
	CRITICAL_SECTION stdin_lock;
	char stdin_buf[CON_STDIN_BUFFER];
	size_t stdin_len;
	qboolean stdin_eof;
} con;

static BOOL WINAPI Con_CtrlHandler( DWORD type )
{
	(void)type;

	InterlockedExchange( &con.quit, 1 );
	SetEvent( con.wake );
	return TRUE;
}

qboolean Sys_Init( qboolean mouse )
{
	WSADATA wsa;
	DWORD in_mode;

	con.hin = GetStdHandle( STD_INPUT_HANDLE );
	con.hout = GetStdHandle( STD_OUTPUT_HANDLE );

	if( !GetConsoleMode( con.hin, &con.saved_in_mode ) || !GetConsoleMode( con.hout, &con.saved_out_mode ))
	{
		fprintf( stderr, "xrcon-tui: must run in a console window\n" );
		return false;
	}

	if( !SetConsoleMode( con.hout, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN )
		&& !SetConsoleMode( con.hout, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING ))
	{
		fprintf( stderr, "xrcon-tui: this console doesn't support VT sequences, Windows 10 or newer is required\n" );
		return false;
	}

	if( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
	{
		SetConsoleMode( con.hout, con.saved_out_mode );
		fprintf( stderr, "xrcon-tui: WSAStartup failed\n" );
		return false;
	}

	// processed input is off so Ctrl-C reaches us as a key, quick edit would eat the mouse
	in_mode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
	if( mouse )
		SetBits( in_mode, ENABLE_MOUSE_INPUT );
	else
		SetBits( in_mode, con.saved_in_mode & ENABLE_QUICK_EDIT_MODE );

	SetConsoleMode( con.hin, in_mode );

	con.wake = CreateEventW( NULL, FALSE, FALSE, NULL );
	con.sock_event = WSACreateEvent();
	SetConsoleCtrlHandler( Con_CtrlHandler, TRUE );
	Sys_GetSize( &con.cols, &con.rows );

	con.active = true;
	Sys_Write( CON_ENTER, sizeof( CON_ENTER ) - 1 );
	atexit( Sys_Shutdown );
	return true;
}

static DWORD WINAPI Con_StdinThread( LPVOID arg )
{
	char buf[4096];
	DWORD n;

	(void)arg;

	while( ReadFile( con.hin, buf, sizeof( buf ), &n, NULL ) && n > 0 )
	{
		size_t done = 0;

		while( done < n )
		{
			EnterCriticalSection( &con.stdin_lock );
			size_t chunk = Q_min( sizeof( con.stdin_buf ) - con.stdin_len, n - done );
			memcpy( con.stdin_buf + con.stdin_len, buf + done, chunk );
			con.stdin_len += chunk;
			LeaveCriticalSection( &con.stdin_lock );

			done += chunk;
			SetEvent( con.stdin_ready );

			if( done < n )
				WaitForSingleObject( con.stdin_space, INFINITE );
		}
	}

	EnterCriticalSection( &con.stdin_lock );
	con.stdin_eof = true;
	LeaveCriticalSection( &con.stdin_lock );
	SetEvent( con.stdin_ready );
	return 0;
}

qboolean Sys_InitStdio( void )
{
	WSADATA wsa;

	if( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
	{
		fprintf( stderr, "xrcon-tui: WSAStartup failed\n" );
		return false;
	}

	// JSON-RPC lines must end with LF, not CRLF
	_setmode( _fileno( stdout ), _O_BINARY );

	con.stdio = true;
	con.hin = GetStdHandle( STD_INPUT_HANDLE );
	con.wake = CreateEventW( NULL, FALSE, FALSE, NULL );
	con.sock_event = WSACreateEvent();
	con.stdin_ready = CreateEventW( NULL, FALSE, FALSE, NULL );
	con.stdin_space = CreateEventW( NULL, FALSE, FALSE, NULL );
	InitializeCriticalSection( &con.stdin_lock );
	SetConsoleCtrlHandler( Con_CtrlHandler, TRUE );

	HANDLE thread = CreateThread( NULL, 0, Con_StdinThread, NULL, 0, NULL );
	if( !thread )
	{
		fprintf( stderr, "xrcon-tui: can't start the stdin reader thread\n" );
		return false;
	}

	CloseHandle( thread );
	return true;
}

void Sys_Shutdown( void )
{
	if( !con.active )
		return;

	con.active = false;
	Sys_Write( CON_LEAVE, sizeof( CON_LEAVE ) - 1 );
	SetConsoleMode( con.hin, con.saved_in_mode );
	SetConsoleMode( con.hout, con.saved_out_mode );
	SetConsoleCtrlHandler( Con_CtrlHandler, FALSE );
	WSACloseEvent( con.sock_event );
	CloseHandle( con.wake );
	WSACleanup();
	free( con.wbuf );
	con.wbuf = NULL;
	con.wbuf_cap = 0;
}

void Sys_GetSize( int *cols, int *rows )
{
	CONSOLE_SCREEN_BUFFER_INFO info;

	if( GetConsoleScreenBufferInfo( con.hout, &info ))
	{
		*cols = info.srWindow.Right - info.srWindow.Left + 1;
		*rows = info.srWindow.Bottom - info.srWindow.Top + 1;
		return;
	}

	*cols = 80;
	*rows = 24;
}

void Sys_Write( const char *data, size_t len )
{
	if( len == 0 || len > INT_MAX )
		return;

	int wlen = MultiByteToWideChar( CP_UTF8, 0, data, (int)len, NULL, 0 );
	if( wlen <= 0 )
		return;

	if((size_t)wlen > con.wbuf_cap )
	{
		wchar_t *wbuf = (wchar_t *)realloc( con.wbuf, wlen * sizeof( *wbuf ));
		if( !wbuf )
			return;

		con.wbuf = wbuf;
		con.wbuf_cap = wlen;
	}

	MultiByteToWideChar( CP_UTF8, 0, data, (int)len, con.wbuf, wlen );

	for( int pos = 0; pos < wlen; )
	{
		DWORD written = 0;

		if( !WriteConsoleW( con.hout, con.wbuf + pos, wlen - pos, &written, NULL ) || written == 0 )
			return;

		pos += written;
	}
}

int64_t Sys_Milliseconds( void )
{
	return (int64_t)GetTickCount64();
}

static void Con_PushKey( int key, int mods, uint32_t ch, int repeat )
{
	for( int i = 0; i < repeat && con.key_count < CON_KEY_QUEUE; i++ )
	{
		sys_key_t *k = &con.keys[con.key_count++];

		k->key = key;
		k->mods = mods;
		k->ch = ch;
	}
}

static int Con_Mods( DWORD state )
{
	int mods = 0;

	if( FBitSet( state, SHIFT_PRESSED ))
		SetBits( mods, TK_MOD_SHIFT );

	if( FBitSet( state, LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED ))
		SetBits( mods, TK_MOD_CTRL );

	if( FBitSet( state, LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED ))
		SetBits( mods, TK_MOD_ALT );

	return mods;
}

static int Con_VirtualKey( WORD vk )
{
	if( vk >= VK_F1 && vk <= VK_F12 )
		return TK_F1 + vk - VK_F1;

	switch( vk )
	{
	case VK_RETURN: return TK_ENTER;
	case VK_BACK: return TK_BACKSPACE;
	case VK_DELETE: return TK_DELETE;
	case VK_ESCAPE: return TK_ESCAPE;
	case VK_UP: return TK_UP;
	case VK_DOWN: return TK_DOWN;
	case VK_LEFT: return TK_LEFT;
	case VK_RIGHT: return TK_RIGHT;
	case VK_HOME: return TK_HOME;
	case VK_END: return TK_END;
	case VK_PRIOR: return TK_PGUP;
	case VK_NEXT: return TK_PGDN;
	}

	return TK_NONE;
}

static void Con_KeyEvent( const KEY_EVENT_RECORD *ev )
{
	if( !ev->bKeyDown )
		return;

	int repeat = Q_max( ev->wRepeatCount, 1 );
	int mods = Con_Mods( ev->dwControlKeyState );
	int key = Con_VirtualKey( ev->wVirtualKeyCode );

	if( key != TK_NONE )
	{
		Con_PushKey( key, mods, 0, repeat );
		return;
	}

	uint32_t uc = Q_DecodeUTF16( &con.utf16, ev->uChar.UnicodeChar );
	if( uc == 0 )
		return;

	if( uc < 0x20 )
	{
		if( uc <= 26 )
			Con_PushKey( TK_CHAR, TK_MOD_CTRL, 'a' + uc - 1, repeat );
		return;
	}

	// AltGr shows up as Ctrl+Alt and has already produced the character
	if( FBitSet( mods, TK_MOD_CTRL ) && FBitSet( mods, TK_MOD_ALT ))
		ClearBits( mods, TK_MOD_CTRL | TK_MOD_ALT );

	ClearBits( mods, TK_MOD_SHIFT );
	Con_PushKey( TK_CHAR, mods, uc, repeat );
}

static void Con_MouseEvent( const MOUSE_EVENT_RECORD *ev )
{
	if( ev->dwEventFlags != MOUSE_WHEELED )
		return;

	int key = (short)HIWORD( ev->dwButtonState ) > 0 ? TK_WHEEL_UP : TK_WHEEL_DOWN;
	Con_PushKey( key, Con_Mods( ev->dwControlKeyState ), 0, 1 );
}

static int Con_ReadInput( void )
{
	DWORD pending = 0;
	int events = 0;
	int cols, rows;

	while( con.key_count < CON_KEY_QUEUE && GetNumberOfConsoleInputEvents( con.hin, &pending ) && pending > 0 )
	{
		INPUT_RECORD recs[CON_INPUT_RECORDS];
		DWORD count = 0;

		if( !ReadConsoleInputW( con.hin, recs, Q_min( pending, ARRAYSIZE( recs )), &count ) || count == 0 )
			break;

		for( DWORD i = 0; i < count; i++ )
		{
			switch( recs[i].EventType )
			{
			case KEY_EVENT:
				Con_KeyEvent( &recs[i].Event.KeyEvent );
				break;
			case MOUSE_EVENT:
				Con_MouseEvent( &recs[i].Event.MouseEvent );
				break;
			case WINDOW_BUFFER_SIZE_EVENT:
				SetBits( events, SYS_EV_RESIZE );
				break;
			}
		}
	}

	if( con.key_count > 0 )
		SetBits( events, SYS_EV_INPUT );

	// not every console sends WINDOW_BUFFER_SIZE_EVENT on window resize
	Sys_GetSize( &cols, &rows );
	if( cols != con.cols || rows != con.rows )
	{
		con.cols = cols;
		con.rows = rows;
		SetBits( events, SYS_EV_RESIZE );
	}

	return events;
}

static qboolean Con_StdinPending( void )
{
	EnterCriticalSection( &con.stdin_lock );
	qboolean pending = con.stdin_len > 0 || con.stdin_eof;
	LeaveCriticalSection( &con.stdin_lock );
	return pending;
}

int Sys_ReadStdin( char *buf, size_t size )
{
	int n;

	EnterCriticalSection( &con.stdin_lock );

	if( con.stdin_len == 0 )
	{
		n = con.stdin_eof ? -1 : 0;
	}
	else
	{
		size_t chunk = Q_min( size, con.stdin_len );

		memcpy( buf, con.stdin_buf, chunk );
		memmove( con.stdin_buf, con.stdin_buf + chunk, con.stdin_len - chunk );
		con.stdin_len -= chunk;
		n = (int)chunk;
	}

	LeaveCriticalSection( &con.stdin_lock );

	if( n > 0 )
		SetEvent( con.stdin_space );

	return n;
}

int Sys_ReadKeys( sys_key_t *keys, int max )
{
	int count = Q_min( con.key_count, max );

	memcpy( keys, con.keys, count * sizeof( *keys ));
	memmove( con.keys, con.keys + count, ( con.key_count - count ) * sizeof( *keys ));
	con.key_count -= count;
	return count;
}

int Sys_Wait( sys_socket_t sock, qboolean want_write, int timeout_ms )
{
	HANDLE handles[3];
	DWORD count = 0;
	int events;

	// FD_WRITE fires on its own after send() reports WSAEWOULDBLOCK
	(void)want_write;

	if( con.key_count > 0 || ( con.stdio && Con_StdinPending()))
		timeout_ms = 0;

	handles[count++] = con.stdio ? con.stdin_ready : con.hin;
	handles[count++] = con.wake;

	if( sock != SYS_INVALID_SOCKET )
		handles[count++] = con.sock_event;

	WaitForMultipleObjects( count, handles, FALSE, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms );

	if( con.stdio )
		events = Con_StdinPending() ? SYS_EV_INPUT : 0;
	else
		events = Con_ReadInput();

	if( con.quit )
		SetBits( events, SYS_EV_QUIT );

	if( sock != SYS_INVALID_SOCKET )
	{
		WSANETWORKEVENTS ne;

		if( WSAEnumNetworkEvents( sock, con.sock_event, &ne ) == 0 )
		{
			if( FBitSet( ne.lNetworkEvents, FD_READ | FD_CLOSE ))
				SetBits( events, SYS_EV_SOCK_READ );

			if( FBitSet( ne.lNetworkEvents, FD_WRITE | FD_CONNECT ))
				SetBits( events, SYS_EV_SOCK_WRITE );

			if( FBitSet( ne.lNetworkEvents, FD_CONNECT ))
				con.connect_error = ne.iErrorCode[FD_CONNECT_BIT];
		}
	}

	return events;
}

sys_socket_t Sys_SocketOpen( int family )
{
	BOOL one = TRUE;
	SOCKET sock = socket( family, SOCK_STREAM, IPPROTO_TCP );

	if( sock == INVALID_SOCKET )
		return SYS_INVALID_SOCKET;

	// also makes the socket non-blocking
	WSAResetEvent( con.sock_event );
	if( WSAEventSelect( sock, con.sock_event, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE ) != 0 )
	{
		closesocket( sock );
		return SYS_INVALID_SOCKET;
	}

	setsockopt( sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof( one ));
	con.connect_error = 0;
	return sock;
}

void Sys_SocketClose( sys_socket_t sock )
{
	closesocket( sock );
	WSAResetEvent( con.sock_event );
}

int Sys_SocketConnect( sys_socket_t sock, const struct sockaddr *addr, size_t addr_len )
{
	if( connect( sock, addr, (int)addr_len ) == 0 )
		return 0;

	return WSAGetLastError() == WSAEWOULDBLOCK ? SYS_SOCK_AGAIN : SYS_SOCK_ERROR;
}

int Sys_SocketConnectResult( sys_socket_t sock )
{
	(void)sock;
	return con.connect_error;
}

int Sys_SocketSend( sys_socket_t sock, const void *data, size_t len )
{
	int n = send( sock, (const char *)data, (int)Q_min( len, INT_MAX ), 0 );

	if( n != SOCKET_ERROR )
		return n;

	return WSAGetLastError() == WSAEWOULDBLOCK ? SYS_SOCK_AGAIN : SYS_SOCK_ERROR;
}

int Sys_SocketRecv( sys_socket_t sock, void *data, size_t len )
{
	int n = recv( sock, (char *)data, (int)Q_min( len, INT_MAX ), 0 );

	if( n != SOCKET_ERROR )
		return n;

	return WSAGetLastError() == WSAEWOULDBLOCK ? SYS_SOCK_AGAIN : SYS_SOCK_ERROR;
}

int Sys_SocketError( void )
{
	return WSAGetLastError();
}

const char *Sys_ErrorString( int code )
{
	wchar_t wbuf[256];
	DWORD len = FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, wbuf, ARRAYSIZE( wbuf ), NULL );

	// messages end with a period and CRLF
	while( len > 0 && ( wbuf[len - 1] == L'\r' || wbuf[len - 1] == L'\n' || wbuf[len - 1] == L'.' || wbuf[len - 1] == L' ' ))
		len--;

	int n = len > 0 ? WideCharToMultiByte( CP_UTF8, 0, wbuf, len, con.errbuf, sizeof( con.errbuf ) - 1, NULL, NULL ) : 0;
	if( n <= 0 )
	{
		Q_snprintf( con.errbuf, sizeof( con.errbuf ), "error %d", code );
		return con.errbuf;
	}

	con.errbuf[n] = '\0';
	return con.errbuf;
}

const char *Sys_ResolveErrorString( int code )
{
	// getaddrinfo reports WSA error codes
	return Sys_ErrorString( code );
}
