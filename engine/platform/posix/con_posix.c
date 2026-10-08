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

#if XASH_POSIX
#include <unistd.h>
#endif

#if XASH_POSIX && defined( _POSIX_VERSION ) && !XASH_MOBILE_PLATFORM && !XASH_LOW_MEMORY
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND_HISTORY	64

typedef struct
{
	qboolean	raw;
	qboolean	inited;
	struct termios	term;
	int		historyLine;
	int		historyNext;
	int		cursorPosition;
	int		textLen;
	int		escParam;
	int		escState;
	string		text;
	string		savedText;
	string		lineBuffer[COMMAND_HISTORY];
} posix_con_t;

static posix_con_t	s_pc;
static char		s_input[MAX_STRING + 2];

static void Posix_Write( const char *buf, size_t size )
{
	if( write( STDOUT_FILENO, buf, size ) < 0 )
		return;
}

static void Posix_Print( const char *msg )
{
	Posix_Write( msg, Q_strlen( msg ));
}

static void Posix_Redraw( void )
{
	char	seq[16];
	int	back;

	Posix_Print( "\r\033[K" );
	Posix_Write( s_pc.text, s_pc.textLen );

	back = s_pc.textLen - s_pc.cursorPosition;
	if( back > 0 )
	{
		Q_snprintf( seq, sizeof( seq ), "\033[%dD", back );
		Posix_Print( seq );
	}
}

// called after engine prints to restore the input line
void Posix_Con_Redraw( void )
{
	if( !s_pc.raw || s_pc.textLen == 0 )
		return;

	Posix_Redraw();
}

static void Posix_Store( const char *cmd )
{
	int prev = Q_max( 0, s_pc.historyLine - 1 );

	if( !cmd[0] )
		return;

	// skip repeating commands
	if( !Q_strcmp( cmd, s_pc.lineBuffer[prev % COMMAND_HISTORY] ))
		return;

	Q_strncpy( s_pc.lineBuffer[s_pc.historyNext % COMMAND_HISTORY], cmd, sizeof( s_pc.lineBuffer[0] ));
	s_pc.historyLine = ++s_pc.historyNext;
}

static void Posix_HistoryUp( void )
{
	if( s_pc.historyNext == 0 )
		return;

	if( s_pc.historyLine == s_pc.historyNext )
		Q_strncpy( s_pc.savedText, s_pc.text, sizeof( s_pc.savedText ));
	else
		Q_strncpy( s_pc.lineBuffer[s_pc.historyLine % COMMAND_HISTORY], s_pc.text, sizeof( s_pc.text ));

	if(( s_pc.historyNext - s_pc.historyLine ) < COMMAND_HISTORY )
		s_pc.historyLine = Q_max( 0, s_pc.historyLine - 1 );

	Q_strncpy( s_pc.text, s_pc.lineBuffer[s_pc.historyLine % COMMAND_HISTORY], sizeof( s_pc.text ));
	s_pc.textLen = Q_strlen( s_pc.text );
	s_pc.cursorPosition = s_pc.textLen;
	Posix_Redraw();
}

static void Posix_HistoryDown( void )
{
	if( s_pc.historyLine == s_pc.historyNext )
		return;

	Q_strncpy( s_pc.lineBuffer[s_pc.historyLine % COMMAND_HISTORY], s_pc.text, sizeof( s_pc.text ));

	s_pc.historyLine = Q_min( s_pc.historyNext, s_pc.historyLine + 1 );

	if( s_pc.historyLine == s_pc.historyNext )
		Q_strncpy( s_pc.text, s_pc.savedText, sizeof( s_pc.text ));
	else
		Q_strncpy( s_pc.text, s_pc.lineBuffer[s_pc.historyLine % COMMAND_HISTORY], sizeof( s_pc.text ));

	s_pc.textLen = Q_strlen( s_pc.text );
	s_pc.cursorPosition = s_pc.textLen;
	Posix_Redraw();
}

static void Posix_EventCharacter( char ch )
{
	if( s_pc.textLen >= (int)sizeof( s_pc.text ) - 1 )
		return;

	memmove( s_pc.text + s_pc.cursorPosition + 1, s_pc.text + s_pc.cursorPosition, s_pc.textLen - s_pc.cursorPosition );
	s_pc.text[s_pc.cursorPosition] = ch;
	s_pc.cursorPosition++;
	s_pc.textLen++;
	s_pc.text[s_pc.textLen] = '\0';
	s_pc.historyLine = s_pc.historyNext;
	Posix_Redraw();
}

static void Posix_EventBackspace( void )
{
	if( s_pc.cursorPosition < 1 )
		return;

	memmove( s_pc.text + s_pc.cursorPosition - 1, s_pc.text + s_pc.cursorPosition, s_pc.textLen - s_pc.cursorPosition );
	s_pc.cursorPosition--;
	s_pc.textLen--;
	s_pc.text[s_pc.textLen] = '\0';
	s_pc.historyLine = s_pc.historyNext;
	Posix_Redraw();
}

static void Posix_EventDelete( void )
{
	if( s_pc.cursorPosition >= s_pc.textLen )
		return;

	memmove( s_pc.text + s_pc.cursorPosition, s_pc.text + s_pc.cursorPosition + 1, s_pc.textLen - s_pc.cursorPosition );
	s_pc.textLen--;
	s_pc.text[s_pc.textLen] = '\0';
	s_pc.historyLine = s_pc.historyNext;
	Posix_Redraw();
}

static void Posix_EventLeftArrow( void )
{
	if( s_pc.cursorPosition == 0 )
		return;

	s_pc.cursorPosition--;
	Posix_Print( "\b" );
}

static void Posix_EventRightArrow( void )
{
	if( s_pc.cursorPosition == s_pc.textLen )
		return;

	Posix_Write( s_pc.text + s_pc.cursorPosition, 1 );
	s_pc.cursorPosition++;
}

static void Posix_EventHome( void )
{
	while( s_pc.cursorPosition > 0 )
	{
		s_pc.cursorPosition--;
		Posix_Print( "\b" );
	}
}

static void Posix_EventEnd( void )
{
	if( s_pc.textLen > s_pc.cursorPosition )
	{
		Posix_Write( s_pc.text + s_pc.cursorPosition, s_pc.textLen - s_pc.cursorPosition );
		s_pc.cursorPosition = s_pc.textLen;
	}
}

static void Posix_EventTab( void )
{
	s_pc.text[s_pc.textLen] = '\0';
	Cmd_AutoComplete( s_pc.text );
	s_pc.textLen = Q_strlen( s_pc.text );
	s_pc.cursorPosition = s_pc.textLen;
	s_pc.historyLine = s_pc.historyNext;
	Posix_Redraw();
}

static char *Posix_EventNewline( void )
{
	int len = s_pc.textLen;

	Posix_Print( "\n" );

	s_pc.text[s_pc.textLen] = '\0';
	s_pc.cursorPosition = 0;
	s_pc.textLen = 0;

	if( len == 0 )
		return NULL;

	Posix_Store( s_pc.text );

	Q_strncpy( s_input, s_pc.text, sizeof( s_input ) - 1 );
	s_input[len] = '\n';
	s_input[len + 1] = '\0';
	s_pc.text[0] = '\0';

	return s_input;
}

static void Posix_EventArrow( char ch )
{
	switch( ch )
	{
	case 'A': Posix_HistoryUp(); break;
	case 'B': Posix_HistoryDown(); break;
	case 'C': Posix_EventRightArrow(); break;
	case 'D': Posix_EventLeftArrow(); break;
	case 'H': Posix_EventHome(); break;
	case 'F': Posix_EventEnd(); break;
	}
}

static void Posix_EventEscape( char ch )
{
	if( s_pc.escState == 1 )
	{
		if( ch == '[' )
		{
			s_pc.escState = 2;
			s_pc.escParam = 0;
		}
		else if( ch == 'O' )
			s_pc.escState = 3;
		else
			s_pc.escState = 0;

		return;
	}

	if( s_pc.escState == 2 )
	{
		if( ch == ';' )
		{
			s_pc.escParam = 0;
			return;
		}

		if( ch >= '0' && ch <= '9' )
		{
			if( s_pc.escParam < 1000 )
				s_pc.escParam = s_pc.escParam * 10 + ( ch - '0' );
			return;
		}

		s_pc.escState = 0;

		if( ch == '~' )
		{
			switch( s_pc.escParam )
			{
			case 1: case 7: Posix_EventHome(); break;
			case 4: case 8: Posix_EventEnd(); break;
			case 3: Posix_EventDelete(); break;
			}
			return;
		}

		Posix_EventArrow( ch );
		return;
	}

	if( s_pc.escState == 3 )
	{
		s_pc.escState = 0;
		Posix_EventArrow( ch );
	}
}

static void Posix_Shutdown( void )
{
	if( !s_pc.raw )
		return;

	tcsetattr( STDIN_FILENO, TCSANOW, &s_pc.term );
	s_pc.raw = false;
}

static void Posix_Signal( int sig )
{
	Posix_Shutdown();
	signal( sig, SIG_DFL );
	raise( sig );
}

static qboolean Posix_Init( void )
{
	struct termios raw;

	if( s_pc.inited )
		return s_pc.raw;

	s_pc.inited = true;

	if( !isatty( STDIN_FILENO ) || !isatty( STDOUT_FILENO ))
		return false;

	if( tcgetattr( STDIN_FILENO, &s_pc.term ) != 0 )
		return false;

	raw = s_pc.term;
	raw.c_lflag &= ~( ICANON | ECHO );
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;

	if( tcsetattr( STDIN_FILENO, TCSANOW, &raw ) != 0 )
		return false;

	s_pc.raw = true;
	atexit( Posix_Shutdown );

	struct sigaction act = { 0 };
	act.sa_handler = Posix_Signal;
	sigaction( SIGINT, &act, NULL );

	return true;
}

static char *Posix_InputLine( void )
{
	static char line[1024];
	static int len;
	fd_set rfds;
	struct timeval tv = { 0 };

	FD_ZERO( &rfds );
	FD_SET( STDIN_FILENO, &rfds );

	while( select( STDIN_FILENO + 1, &rfds, NULL, NULL, &tv ) > 0 )
	{
		if( read( STDIN_FILENO, &line[len], 1 ) != 1 )
			break;

		if( line[len] == '\n' || len > (int)( sizeof( line ) - 2 ))
		{
			line[++len] = 0;
			len = 0;
			return line;
		}

		len++;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO( &rfds );
		FD_SET( STDIN_FILENO, &rfds );
	}

	return NULL;
}

char *Posix_Input( void )
{
	fd_set rfds;
	struct timeval tv = { 0 };

	if( !Host_IsDedicated( ))
		return NULL;

	if( !Posix_Init( ))
		return Posix_InputLine();

	FD_ZERO( &rfds );
	FD_SET( STDIN_FILENO, &rfds );

	while( select( STDIN_FILENO + 1, &rfds, NULL, NULL, &tv ) > 0 )
	{
		char ch;

		if( read( STDIN_FILENO, &ch, 1 ) != 1 )
			break;

		if( s_pc.escState != 0 )
		{
			Posix_EventEscape( ch );
		}
		else
		{
			switch( ch )
			{
			case 0x1b:
				s_pc.escState = 1;
				s_pc.escParam = 0;
				break;
			case '\r':
			case '\n':
			{
				char *line = Posix_EventNewline();

				if( line )
					return line;
				break;
			}
			case 0x7f:
			case '\b':
				Posix_EventBackspace();
				break;
			case '\t':
				Posix_EventTab();
				break;
			default:
				if((unsigned char)ch >= ' ' )
					Posix_EventCharacter( ch );
				break;
			}
		}

		tv.tv_sec = 0;
		tv.tv_usec = 0;
		FD_ZERO( &rfds );
		FD_SET( STDIN_FILENO, &rfds );
	}

	return NULL;
}
#else // !XASH_POSIX || !_POSIX_VERSION || XASH_MOBILE_PLATFORM || XASH_LOW_MEMORY
void Posix_Con_Redraw( void ) { }
#endif // !XASH_POSIX || !_POSIX_VERSION || XASH_MOBILE_PLATFORM || XASH_LOW_MEMORY
