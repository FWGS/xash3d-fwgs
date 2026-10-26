/*
con_posix.c - reading from stdin
Copyright (C) 2024 Flying With Gauss

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"

#if !XASH_MOBILE_PLATFORM && !XASH_LOW_MEMORY
// use with caution, running engine in Qt Creator may cause a freeze in read() call
// I have never encountered this bug anywhere else, so still enable by default
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>

#define CON_HISTORY 64

enum
{
	ESC_NONE = 0,
	ESC_START,
	ESC_CSI,
	ESC_CSI_PARAMS, // only first parameter is used
	ESC_SS3,
};

static struct
{
	qboolean raw; // stdin and stdout are a terminal, we draw the input line ourselves
	qboolean output_midline; // engine didn't finish printing a line, input line is hidden
	qboolean dirty;
	struct termios oldterm;

	char line[MAX_STRING];
	int len;
	int cursor;

	char history[CON_HISTORY][MAX_STRING];
	char backup[MAX_STRING];
	int history_next;
	int history_line;

	int esc_state;
	int esc_param;
} s_con;

static void Posix_ConWrite( const char *buf, size_t len )
{
	if( write( STDOUT_FILENO, buf, len ) < 0 )
	{
		// not critical for us
	}
}

static void Posix_ConRedraw( void )
{
	char buf[MAX_STRING + 16];
	int columns = 0;

	s_con.dirty = false;

	if( s_con.output_midline )
		return;

	// every UTF-8 sequence is assumed to take one column
	for( int i = s_con.cursor; i < s_con.len; i++ )
	{
		if(((byte)s_con.line[i] & 0xC0 ) != 0x80 )
			columns++;
	}

	int len = Q_snprintf( buf, sizeof( buf ), "\r\033[K] %s", s_con.line );

	if( columns > 0 )
		len += Q_snprintf( buf + len, sizeof( buf ) - len, "\033[%dD", columns );

	Posix_ConWrite( buf, len );
}

static int Posix_ConPrevChar( int pos )
{
	if( pos > 0 )
		pos--;

	while( pos > 0 && ((byte)s_con.line[pos] & 0xC0 ) == 0x80 )
		pos--;

	return pos;
}

static int Posix_ConNextChar( int pos )
{
	if( pos < s_con.len )
		pos++;

	while( pos < s_con.len && ((byte)s_con.line[pos] & 0xC0 ) == 0x80 )
		pos++;

	return pos;
}

static void Posix_ConSetCursor( int pos )
{
	s_con.cursor = pos;
	s_con.dirty = true;
}

static void Posix_ConSetLine( const char *text )
{
	s_con.len = Q_strncpy( s_con.line, text, sizeof( s_con.line ));
	Posix_ConSetCursor( s_con.len );
}

static void Posix_ConInsert( char ch )
{
	if( s_con.len >= sizeof( s_con.line ) - 1 )
		return;

	memmove( s_con.line + s_con.cursor + 1, s_con.line + s_con.cursor, s_con.len - s_con.cursor + 1 );
	s_con.line[s_con.cursor] = ch;
	s_con.len++;
	Posix_ConSetCursor( s_con.cursor + 1 );
}

static void Posix_ConDelete( int from, int to )
{
	if( from >= to )
		return;

	memmove( s_con.line + from, s_con.line + to, s_con.len - to + 1 );
	s_con.len -= to - from;
	Posix_ConSetCursor( from );
}

static void Posix_ConHistorySave( void )
{
	if( s_con.history_line == s_con.history_next )
		Q_strncpy( s_con.backup, s_con.line, sizeof( s_con.backup ));
	else
		Q_strncpy( s_con.history[s_con.history_line % CON_HISTORY], s_con.line, sizeof( s_con.history[0] ));
}

static void Posix_ConHistoryUp( void )
{
	if( s_con.history_line <= Q_max( 0, s_con.history_next - CON_HISTORY ))
		return;

	Posix_ConHistorySave();
	s_con.history_line--;
	Posix_ConSetLine( s_con.history[s_con.history_line % CON_HISTORY] );
}

static void Posix_ConHistoryDown( void )
{
	if( s_con.history_line >= s_con.history_next )
		return;

	Posix_ConHistorySave();
	s_con.history_line++;

	if( s_con.history_line == s_con.history_next )
		Posix_ConSetLine( s_con.backup );
	else
		Posix_ConSetLine( s_con.history[s_con.history_line % CON_HISTORY] );
}

static void Posix_ConHistoryAppend( void )
{
	// don't store repeating commands
	if( s_con.history_next == 0 || Q_strcmp( s_con.history[( s_con.history_next - 1 ) % CON_HISTORY], s_con.line ))
	{
		Q_strncpy( s_con.history[s_con.history_next % CON_HISTORY], s_con.line, sizeof( s_con.history[0] ));
		s_con.history_next++;
	}

	s_con.history_line = s_con.history_next;
}

static void Posix_ConComplete( void )
{
	Cmd_AutoComplete( s_con.line );
	s_con.len = Q_strlen( s_con.line );
	Posix_ConSetCursor( s_con.len );
}

static char *Posix_ConNewline( void )
{
	static char cmd[MAX_STRING + 1];

	// leave the command on the screen, just like terminal does
	Posix_ConRedraw();
	Posix_ConWrite( "\n", 1 );
	s_con.dirty = true;

	if( s_con.len == 0 )
		return NULL;

	Posix_ConHistoryAppend();
	Q_snprintf( cmd, sizeof( cmd ), "%s\n", s_con.line );
	Posix_ConSetLine( "" );

	return cmd;
}

static void Posix_ConKey( int key )
{
	switch( key )
	{
	case 'A':
		Posix_ConHistoryUp();
		break;
	case 'B':
		Posix_ConHistoryDown();
		break;
	case 'C':
		Posix_ConSetCursor( Posix_ConNextChar( s_con.cursor ));
		break;
	case 'D':
		Posix_ConSetCursor( Posix_ConPrevChar( s_con.cursor ));
		break;
	case 'H':
		Posix_ConSetCursor( 0 );
		break;
	case 'F':
		Posix_ConSetCursor( s_con.len );
		break;
	case '~':
		switch( s_con.esc_param )
		{
		case 1:
		case 7:
			Posix_ConSetCursor( 0 );
			break;
		case 3:
			Posix_ConDelete( s_con.cursor, Posix_ConNextChar( s_con.cursor ));
			break;
		case 4:
		case 8:
			Posix_ConSetCursor( s_con.len );
			break;
		}
		break;
	}
}

static void Posix_ConEscape( char ch )
{
	switch( s_con.esc_state )
	{
	case ESC_START:
		if( ch == '[' )
			s_con.esc_state = ESC_CSI;
		else if( ch == 'O' )
			s_con.esc_state = ESC_SS3;
		else
			s_con.esc_state = ESC_NONE;
		return;
	case ESC_CSI:
	case ESC_CSI_PARAMS:
		if( ch == ';' )
		{
			s_con.esc_state = ESC_CSI_PARAMS;
			return;
		}

		if( ch >= '0' && ch <= '9' )
		{
			if( s_con.esc_state == ESC_CSI && s_con.esc_param < 1000 )
				s_con.esc_param = s_con.esc_param * 10 + ch - '0';
			return;
		}
		break;
	}

	s_con.esc_state = ESC_NONE;
	Posix_ConKey( ch );
}

void Posix_ShutdownConsole( void )
{
	if( !s_con.raw )
		return;

	if( !s_con.output_midline )
		Posix_ConWrite( "\r\033[K", 4 );

	tcsetattr( STDIN_FILENO, TCSANOW, &s_con.oldterm );
	s_con.raw = false;
}

static void Posix_ConSignal( int sig )
{
	Posix_ShutdownConsole();
	signal( sig, SIG_DFL );
	raise( sig );
}

void Posix_InitConsole( void )
{
	if( !Host_IsDedicated( ) || !isatty( STDIN_FILENO ) || !isatty( STDOUT_FILENO ))
		return;

	// changing terminal from background job stops the process with SIGTTOU
	if( tcgetpgrp( STDIN_FILENO ) != getpgrp( ))
		return;

	if( tcgetattr( STDIN_FILENO, &s_con.oldterm ) < 0 )
		return;

	struct termios term = s_con.oldterm;
	term.c_lflag &= ~( ICANON | ECHO );
	term.c_cc[VMIN] = 0;
	term.c_cc[VTIME] = 0;

	if( tcsetattr( STDIN_FILENO, TCSANOW, &term ) < 0 )
		return;

	s_con.raw = true;
	Posix_ConRedraw();

	// SDL quits properly on Ctrl-C, default handler kills us before shutdown can give the terminal back
	struct sigaction old, act = { 0 };
	act.sa_handler = Posix_ConSignal;

	if( sigaction( SIGINT, NULL, &old ) == 0 && old.sa_handler == SIG_DFL )
		sigaction( SIGINT, &act, NULL );
}

void Posix_ConsolePrintBegin( void )
{
	if( s_con.raw && !s_con.output_midline )
		Posix_ConWrite( "\r\033[K", 4 );
}

void Posix_ConsolePrintEnd( const char *msg )
{
	if( !s_con.raw )
		return;

	size_t len = Q_strlen( msg );

	if( len > 0 )
		s_con.output_midline = msg[len - 1] != '\n';

	Posix_ConRedraw();
}

static qboolean Posix_ReadChar( char *ch )
{
	fd_set rfds;
	struct timeval tv = { 0 };

	FD_ZERO( &rfds );
	FD_SET( STDIN_FILENO, &rfds );

	if( select( STDIN_FILENO + 1, &rfds, NULL, NULL, &tv ) <= 0 )
		return false;

	return read( STDIN_FILENO, ch, 1 ) == 1;
}

static char *Posix_InputLine( void )
{
	static char line[1024];
	static int len;

	while( Posix_ReadChar( &line[len] ))
	{
		if( line[len] == '\n' || len > ( sizeof( line ) - 2 ))
		{
			line[++len] = 0;
			len = 0;
			return line;
		}
		len++;
	}

	return NULL;
}

char *Posix_Input( void )
{
	char ch;

	if( !Host_IsDedicated( ))
		return NULL;

	if( !s_con.raw )
		return Posix_InputLine();

	while( Posix_ReadChar( &ch ))
	{
		if( s_con.esc_state != ESC_NONE )
		{
			Posix_ConEscape( ch );
			continue;
		}

		switch( ch )
		{
		case '\033':
			s_con.esc_state = ESC_START;
			s_con.esc_param = 0;
			break;
		case '\r':
		case '\n':
		{
			char *cmd = Posix_ConNewline();

			if( cmd )
				return cmd;
			break;
		}
		case '\b':
		case 0x7f:
			Posix_ConDelete( Posix_ConPrevChar( s_con.cursor ), s_con.cursor );
			break;
		case '\t':
			Posix_ConComplete();
			break;
		case 'a' - 'a' + 1:
			Posix_ConSetCursor( 0 );
			break;
		case 'e' - 'a' + 1:
			Posix_ConSetCursor( s_con.len );
			break;
		default:
			if((byte)ch >= ' ' )
				Posix_ConInsert( ch );
			break;
		}
	}

	if( s_con.dirty )
		Posix_ConRedraw();

	return NULL;
}
#endif // !XASH_MOBILE_PLATFORM && !XASH_LOW_MEMORY
