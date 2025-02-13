/*
sys_con.c - win32 dedicated and developer console
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "xash3d_mathlib.h"

/*
===============================================================================

WIN32 CONSOLE

===============================================================================
*/

// console defines
#define COMMAND_HISTORY	64	// system console keep more commands than game console

typedef struct
{
	string		title;
	string		previousTitle;
	UINT		previousCodePage;
	UINT		previousOutputCodePage;
	HWND		hWnd;
	HANDLE		hInput;
	HANDLE		hOutput;
	int			inputLine;
	int			browseLine;
	int			cursorPosition;
	int			totalLines;
	int			savedConsoleTextLen;
	int			consoleTextLen;
	string		consoleText;
	string		savedConsoleText;
	string		statusLine;
	string		lineBuffer[COMMAND_HISTORY];
	qboolean	inputEnabled;
	qboolean	consoleVisible;
	qboolean	attached;
} WinConData;

static WinConData	s_wcd;
static WORD g_color_table[8] =
{
FOREGROUND_INTENSITY,									// black
FOREGROUND_RED,											// red
FOREGROUND_GREEN,										// green
FOREGROUND_RED | FOREGROUND_GREEN,						// yellow
FOREGROUND_BLUE | FOREGROUND_INTENSITY,					// blue
FOREGROUND_GREEN | FOREGROUND_BLUE,						// cyan
FOREGROUND_RED | FOREGROUND_BLUE,						// magenta
FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,	// default color (white)
};

static BOOL WINAPI Wcon_HandleConsole(DWORD CtrlType)
{
	return TRUE;
}

static void Wcon_PrintInternal( const char *msg, int length )
{
	char *pTemp;
	DWORD cbWritten;
	const char *pMsgString;
	static char tmpBuf[2048];
	static char szOutput[2048];

	Q_strncpy( szOutput, msg, length ? (length + 1) : ( sizeof( szOutput ) - 1 ));
	if( length )
		szOutput[length + 1] = '\0';
	else
		szOutput[sizeof( szOutput ) - 1] = '\0';

	pTemp = tmpBuf;
	pMsgString = szOutput;
	while( pMsgString && *pMsgString )
	{
		if( IsColorString( pMsgString ))
		{
			if (( pTemp - tmpBuf ) > 0 )
			{
				// dump accumulated text before change color
				*pTemp = 0; // terminate string
				WriteFile( s_wcd.hOutput, tmpBuf, Q_strlen(tmpBuf), &cbWritten, 0 );
				pTemp = tmpBuf;
			}

			// set new color
			SetConsoleTextAttribute( s_wcd.hOutput, g_color_table[ColorIndex(*(pMsgString + 1))] );
			pMsgString += 2; // skip color info
		}
		else if(( pTemp - tmpBuf ) < sizeof( tmpBuf ) - 1 )
		{
			*pTemp++ = *pMsgString++;
		}
		else
		{
			// temp buffer is full, dump it now
			*pTemp = 0; // terminate string
			WriteFile( s_wcd.hOutput, tmpBuf, Q_strlen(tmpBuf), &cbWritten, 0 );
			pTemp = tmpBuf;
		}
	}

	// check for last portion
	if (( pTemp - tmpBuf ) > 0 )
	{
		// dump accumulated text
		*pTemp = 0; // terminate string
		WriteFile( s_wcd.hOutput, tmpBuf, Q_strlen(tmpBuf), &cbWritten, 0 );
		pTemp = tmpBuf;
	}

	// restore white color
	SetConsoleTextAttribute( s_wcd.hOutput, g_color_table[7] );
}

void Wcon_ShowConsole( qboolean show )
{
	if( !s_wcd.hWnd || show == s_wcd.consoleVisible || s_wcd.attached )
		return;

	s_wcd.consoleVisible = show;
	if( show )
		ShowWindow( s_wcd.hWnd, SW_SHOW );
	else
		ShowWindow( s_wcd.hWnd, SW_HIDE );
}

void Wcon_DisableInput( void )
{
	if( host.type != HOST_DEDICATED || !s_wcd.hWnd )
		return;

	s_wcd.inputEnabled = false;
}

static void Wcon_SetInputText( const char *inputText )
{
	if( host.type != HOST_DEDICATED )
		return;

	while( s_wcd.consoleTextLen-- )
	{
		Wcon_PrintInternal( "\b \b", 0 );
	}
	Wcon_PrintInternal( inputText, 0 );
	Q_strncpy( s_wcd.consoleText, inputText, sizeof(s_wcd.consoleText) - 1 );
	s_wcd.consoleTextLen = Q_strlen( inputText );
	s_wcd.cursorPosition = s_wcd.consoleTextLen;
	s_wcd.browseLine = s_wcd.inputLine;
}

static void Wcon_Clear_f( void )
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	SMALL_RECT scrollRect;
	COORD scrollTarget;
	CHAR_INFO fill;

	if( host.type != HOST_DEDICATED )
		return;

	if( !GetConsoleScreenBufferInfo( s_wcd.hOutput, &csbi ))
	{
		return;
	}

	scrollRect.Left = 0;
	scrollRect.Top = 0;
	scrollRect.Right = csbi.dwSize.X;
	scrollRect.Bottom = csbi.dwSize.Y;
	scrollTarget.X = 0;
	scrollTarget.Y = (SHORT)(0 - csbi.dwSize.Y);
	fill.Char.UnicodeChar = TEXT(' ');
	fill.Attributes = csbi.wAttributes;
	ScrollConsoleScreenBuffer( s_wcd.hOutput, &scrollRect, NULL, scrollTarget, &fill );

	csbi.dwCursorPosition.X = 0;
	csbi.dwCursorPosition.Y = 0;
	SetConsoleCursorPosition( s_wcd.hOutput, csbi.dwCursorPosition );

	s_wcd.consoleText[0] = '\0';
	s_wcd.consoleTextLen = 0;
	s_wcd.cursorPosition = 0;
	s_wcd.inputLine = 0;
	s_wcd.browseLine = 0;
	s_wcd.totalLines = 0;
	Wcon_PrintInternal( "\n", 0 );
}

static void Wcon_EventUpArrow()
{
	int nLastCommandInHistory = s_wcd.inputLine + 1;
	if( nLastCommandInHistory > s_wcd.totalLines )
		nLastCommandInHistory = 0;

	if( s_wcd.browseLine == nLastCommandInHistory )
		return;

	if( s_wcd.browseLine == s_wcd.inputLine )
	{
		if( s_wcd.consoleTextLen > 0 )
		{
			Q_strncpy( s_wcd.savedConsoleText, s_wcd.consoleText, s_wcd.consoleTextLen );
		}
		s_wcd.savedConsoleTextLen = s_wcd.consoleTextLen;
	}

	s_wcd.browseLine--;
	if( s_wcd.browseLine < 0 )
	{
		s_wcd.browseLine = s_wcd.totalLines - 1;
	}

	while( s_wcd.consoleTextLen-- )
	{
		Wcon_PrintInternal( "\b \b", 0 );
	}

	Wcon_PrintInternal( s_wcd.lineBuffer[s_wcd.browseLine], 0 );
	Q_strncpy( s_wcd.consoleText, s_wcd.lineBuffer[s_wcd.browseLine], sizeof( s_wcd.consoleText ));
	s_wcd.consoleTextLen = Q_strlen( s_wcd.lineBuffer[s_wcd.browseLine] );
	s_wcd.cursorPosition = s_wcd.consoleTextLen;
}

static void Wcon_EventDownArrow()
{
	if( s_wcd.browseLine == s_wcd.inputLine )
		return;

	if( ++s_wcd.browseLine > s_wcd.totalLines )
		s_wcd.browseLine = 0;

	while( s_wcd.consoleTextLen-- )
	{
		Wcon_PrintInternal( "\b \b", 0 );
	}

	if( s_wcd.browseLine == s_wcd.inputLine )
	{
		if( s_wcd.savedConsoleTextLen > 0 )
		{
			Q_strncpy( s_wcd.consoleText, s_wcd.savedConsoleText, s_wcd.savedConsoleTextLen );
			Wcon_PrintInternal( s_wcd.consoleText, s_wcd.savedConsoleTextLen );
		}
		s_wcd.consoleTextLen = s_wcd.savedConsoleTextLen;
	}
	else
	{
		Wcon_PrintInternal( s_wcd.lineBuffer[s_wcd.browseLine], 0 );
		Q_strncpy( s_wcd.consoleText, s_wcd.lineBuffer[s_wcd.browseLine], sizeof( s_wcd.consoleText ));
		s_wcd.consoleTextLen = Q_strlen( s_wcd.lineBuffer[s_wcd.browseLine] );
	}
	s_wcd.cursorPosition = s_wcd.consoleTextLen;
}

static void Wcon_EventLeftArrow()
{
	if( s_wcd.cursorPosition == 0 )
		return;

	Wcon_PrintInternal( "\b", 0 );
	s_wcd.cursorPosition--;
}

static void Wcon_EventRightArrow()
{
	if( s_wcd.cursorPosition == s_wcd.consoleTextLen )
		return;

	Wcon_PrintInternal( s_wcd.consoleText + s_wcd.cursorPosition, 1 );
	s_wcd.cursorPosition++;
}

static int Wcon_EventNewline()
{
	int nLen;

	nLen = 0;
	Wcon_PrintInternal( "\n", 0 );
	if( s_wcd.consoleTextLen )
	{
		nLen = s_wcd.consoleTextLen;

		s_wcd.consoleText[s_wcd.consoleTextLen] = '\0';
		s_wcd.consoleTextLen = 0;
		s_wcd.cursorPosition = 0;

		if (( s_wcd.inputLine == 0 ) || ( Q_strcmp( s_wcd.lineBuffer[s_wcd.inputLine - 1], s_wcd.consoleText )))
		{
			Q_strncpy( s_wcd.lineBuffer[s_wcd.inputLine], s_wcd.consoleText, sizeof( s_wcd.consoleText ));
			s_wcd.inputLine++;

			if( s_wcd.inputLine > s_wcd.totalLines )
				s_wcd.totalLines = s_wcd.inputLine;

			if( s_wcd.inputLine >= COMMAND_HISTORY )
				s_wcd.inputLine = 0;
		}
		s_wcd.browseLine = s_wcd.inputLine;
	}
	return nLen;
}

static void Wcon_EventBackspace()
{
	int nCount;

	if( s_wcd.cursorPosition < 1 )
	{
		return;
	}

	s_wcd.consoleTextLen--;
	s_wcd.cursorPosition--;

	Wcon_PrintInternal( "\b", 0 );

	for( nCount = s_wcd.cursorPosition; nCount < s_wcd.consoleTextLen; ++nCount )
	{
		s_wcd.consoleText[nCount] = s_wcd.consoleText[nCount + 1];
		Wcon_PrintInternal( s_wcd.consoleText + nCount, 1 );
	}

	Wcon_PrintInternal( " ", 0 );

	nCount = s_wcd.consoleTextLen;
	while( nCount >= s_wcd.cursorPosition )
	{
		Wcon_PrintInternal( "\b", 0 );
		nCount--;
	}

	s_wcd.browseLine = s_wcd.inputLine;
}

static void Wcon_EventTab()
{
	s_wcd.consoleText[s_wcd.consoleTextLen] = '\0';
	Cmd_AutoComplete( s_wcd.consoleText );
	Wcon_SetInputText( s_wcd.consoleText );
}

static void Wcon_EventCharacter(char c)
{
	int nCount;

	if( s_wcd.consoleTextLen >= ( sizeof( s_wcd.consoleText ) - 2 ))
	{
		return;
	}

	nCount = s_wcd.consoleTextLen;
	while( nCount > s_wcd.cursorPosition )
	{
		s_wcd.consoleText[nCount] = s_wcd.consoleText[nCount - 1];
		nCount--;
	}

	s_wcd.consoleText[s_wcd.cursorPosition] = c;
	Wcon_PrintInternal( s_wcd.consoleText + s_wcd.cursorPosition, s_wcd.consoleTextLen - s_wcd.cursorPosition + 1 );
	s_wcd.consoleTextLen++;
	s_wcd.cursorPosition++;

	nCount = s_wcd.consoleTextLen;
	while( nCount > s_wcd.cursorPosition )
	{
		Wcon_PrintInternal( "\b", 0 );
		nCount--;
	}

	s_wcd.browseLine = s_wcd.inputLine;
}

static void Wcon_UpdateStatusLine()
{
	COORD coord;
	WORD wAttrib;
	DWORD dwWritten;

	coord.X = 0;
	coord.Y = 0;
	wAttrib = g_color_table[5] | FOREGROUND_INTENSITY | BACKGROUND_INTENSITY;

	FillConsoleOutputCharacter( s_wcd.hOutput, ' ', 80, coord, &dwWritten );
	FillConsoleOutputAttribute( s_wcd.hOutput, wAttrib, 80, coord, &dwWritten );
	WriteConsoleOutputCharacter( s_wcd.hOutput, s_wcd.statusLine, Q_strlen(s_wcd.statusLine), coord, &dwWritten );
}

static char *Wcon_KeyEvent( int key, WCHAR character )
{
	int nLen;
	char inputBuffer[1024];

	switch( key )
	{
	case VK_UP:
		Wcon_EventUpArrow();
		return NULL;
	case VK_DOWN:
		Wcon_EventDownArrow();
		return NULL;
	case VK_LEFT:
		Wcon_EventLeftArrow();
		return NULL;
	case VK_RIGHT:
		Wcon_EventRightArrow();
		return NULL;
	}

	switch( character )
	{
		case '\r':	// Enter
			nLen = Wcon_EventNewline();
			if (nLen)
			{
				return s_wcd.consoleText;
			}
			break;
		case '\b':	// Backspace
			Wcon_EventBackspace();
			break;
		case '\t':	// TAB
			Wcon_EventTab();
			break;
		default:
			// TODO implement converting wide chars to UTF-8 and properly handling it
			if (( character >= ' ' ) && ( character <= '~' ))
			{
				Wcon_EventCharacter(character);
			}
			break;
	}

	return NULL;
}

/*
===============================================================================

WIN32 IO

===============================================================================
*/

/*
================
Con_WinPrint

print into window console
================
*/
void Wcon_WinPrint( const char *pMsg )
{
	if( !s_wcd.hWnd )
		return;

	int nLen;
	if( s_wcd.consoleTextLen )
	{
		nLen = s_wcd.consoleTextLen;
		while (nLen--)
		{
			Wcon_PrintInternal( "\b \b", 0 );
		}
	}

	Wcon_PrintInternal( pMsg, 0 );

	if( s_wcd.consoleTextLen )
	{
		Wcon_PrintInternal( s_wcd.consoleText, s_wcd.consoleTextLen );
	}

	if( !s_wcd.attached ) 
		Wcon_UpdateStatusLine();
}

/*
================
Con_CreateConsole

create win32 console
================
*/
void Wcon_CreateConsole( qboolean con_showalways )
{
	if( host.type == HOST_NORMAL )
	{
		Q_strncpy( s_wcd.title, XASH_ENGINE_NAME " " XASH_VERSION, sizeof( s_wcd.title ));
	}
	else // dedicated console
	{
		Q_strncpy( s_wcd.title, XASH_DEDICATED_SERVER_NAME " " XASH_VERSION, sizeof( s_wcd.title ));
	}

	s_wcd.attached = ( AttachConsole( ATTACH_PARENT_PROCESS ) != 0 );
	if( s_wcd.attached )
	{
		GetConsoleTitle( &s_wcd.previousTitle, sizeof( s_wcd.previousTitle ));
		s_wcd.previousCodePage = GetConsoleCP();
		s_wcd.previousOutputCodePage = GetConsoleOutputCP();
	}
	else
	{
		if( host.type != HOST_DEDICATED && host_developer.value == DEV_NONE )
			return; // don't initialize console in case of regular game startup, it's useless anyway
		else
			AllocConsole();
	}

	SetConsoleTitle( s_wcd.title );
	SetConsoleCP( CP_UTF8 );
	SetConsoleOutputCP( CP_UTF8 );

	s_wcd.hWnd = GetConsoleWindow();
	s_wcd.hInput = GetStdHandle( STD_INPUT_HANDLE );
	s_wcd.hOutput = GetStdHandle( STD_OUTPUT_HANDLE );
	s_wcd.inputEnabled = true;
	
	if( !SetConsoleCtrlHandler( &Wcon_HandleConsole, TRUE ))
	{
		Con_Reportf( S_ERROR "Couldn't attach console handler function\n" );
		return;
	}

	if( !s_wcd.attached )
	{ 
		SetWindowPos( s_wcd.hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOREPOSITION | SWP_SHOWWINDOW );

		// show console if needed
		if( con_showalways )
		{
			// make console visible
			ShowWindow( s_wcd.hWnd, SW_SHOWDEFAULT );
			UpdateWindow( s_wcd.hWnd );
			SetForegroundWindow( s_wcd.hWnd );
			SetFocus( s_wcd.hWnd );
			s_wcd.consoleVisible = true;
		}
		else
		{
			s_wcd.consoleVisible = false;
			ShowWindow( s_wcd.hWnd, SW_HIDE );
		}
	}
}

/*
================
Con_InitConsoleCommands

register console commands (dedicated only)
================
*/
void Wcon_InitConsoleCommands( void )
{
	if( host.type != HOST_DEDICATED || !s_wcd.hWnd )
		return;

	Cmd_AddCommand( "clear", Wcon_Clear_f, "clear console history" );
}

/*
================
Con_DestroyConsole

destroy win32 console
================
*/
void Wcon_DestroyConsole( void )
{
	// last text message into console or log
	Con_Reportf( "%s: Unloading xash.dll\n", __func__ );

	if( !s_wcd.attached )
	{
		if( s_wcd.hWnd )
		{
			ShowWindow( s_wcd.hWnd, SW_HIDE );
			s_wcd.hWnd = 0;
		}
	}
	else
	{
		// reverts title & code page for console window that was before starting Xash3D
		SetConsoleCP( s_wcd.previousCodePage );
		SetConsoleOutputCP( s_wcd.previousOutputCodePage );
		SetConsoleTitle( &s_wcd.previousTitle );
		Con_Printf( "Press Enter to continue...\n" );
	}

	FreeConsole();
}

/*
================
Con_Input

returned input text
================
*/
char *Wcon_Input( void )
{
	DWORD i;
	DWORD eventsCount;
	static INPUT_RECORD events[1024];
	
	if( !s_wcd.inputEnabled || !s_wcd.hWnd )
		return NULL;

	while( true )
	{
		if( !GetNumberOfConsoleInputEvents( s_wcd.hInput, &eventsCount ))
		{
			return NULL;
		}

		if( eventsCount <= 0 )
			break;

		if( !ReadConsoleInputW( s_wcd.hInput, events, ARRAYSIZE( events ), &eventsCount ))
		{
			return NULL;
		}

		if( eventsCount == 0 )
			return NULL;

		for( i = 0; i < eventsCount; i++ )
		{
			INPUT_RECORD *pRec = &events[i];
			if( pRec->EventType != KEY_EVENT )
				continue;

			if( pRec->Event.KeyEvent.bKeyDown )
				return Wcon_KeyEvent( pRec->Event.KeyEvent.wVirtualKeyCode, pRec->Event.KeyEvent.uChar.UnicodeChar );
		}
	}
	return NULL;
}

/*
================
Platform_SetStatus

set server status string in console
================
*/
void Platform_SetStatus( const char *pStatus )
{
	if( s_wcd.attached || !s_wcd.hWnd )
		return;

	Q_strncpy( s_wcd.statusLine, pStatus, sizeof( s_wcd.statusLine ) - 1 );
	s_wcd.statusLine[sizeof( s_wcd.statusLine ) - 2] = '\0';
	Wcon_UpdateStatusLine();
}
