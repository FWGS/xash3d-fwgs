/*
keys.c - console key events
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
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "platform/platform.h"

typedef struct
{
	qboolean		down;
	qboolean		gamedown;
	int		repeats;	// if > 1, it is autorepeating
	const char	*binding;
} enginekey_t;

typedef struct keyname_s
{
	const char	*name;	// key name
	int		keynum;	// key number
	const char	*binding;	// default bind
} keyname_t;

static enginekey_t	keys[256];

static const keyname_t keynames[] =
{
{"TAB",		K_TAB,		""		},
{"ENTER",		K_ENTER,		""		},
{"ESCAPE",	K_ESCAPE, 	"cancelselect"		}, // hardcoded
{"SPACE",		K_SPACE,		"+jump"		},
{"BACKSPACE",	K_BACKSPACE,	""		},
{"UPARROW",	K_UPARROW,	"+forward"	},
{"DOWNARROW",	K_DOWNARROW,	"+back"		},
{"LEFTARROW",	K_LEFTARROW,	"+left"		},
{"RIGHTARROW",	K_RIGHTARROW,	"+right"		},
{"ALT",		K_ALT,		"+strafe"		},
{"CTRL",		K_CTRL,		"+attack"		},
{"SHIFT",		K_SHIFT,		"+speed"		},
{"CAPSLOCK",	K_CAPSLOCK,	""		},
{"SCROLLOCK",	K_SCROLLLOCK,	""		},
{"F1",		K_F1,		"cmd help"	},
{"F2",		K_F2,		"menu_savegame"	},
{"F3",		K_F3,		"menu_loadgame"	},
{"F4",		K_F4,		"menu_controls"	},
{"F5",		K_F5,		"menu_creategame"	},
{"F6",		K_F6,		"savequick"	},
{"F7",		K_F7,		"loadquick"	},
{"F8",		K_F8,		"stop"		},
{"F9",		K_F9,		""		},
{"F10",		K_F10,		"menu_main"	},
{"F11",		K_F11,		""		},
{"F12",		K_F12,		"snapshot"	},
{"INS",		K_INS,		""		},
{"DEL",		K_DEL,		"+lookdown"	},
{"PGDN",		K_PGDN,		"+lookup"		},
{"PGUP",		K_PGUP,		""		},
{"HOME",		K_HOME,		""		},
{"END",		K_END,		"centerview"	},

// mouse buttouns
{"MOUSE1",	K_MOUSE1,		"+attack"		},
{"MOUSE2",	K_MOUSE2,		"+attack2"	},
{"MOUSE3",	K_MOUSE3,		""		},
{"MOUSE4",	K_MOUSE4,		""		},
{"MOUSE5",	K_MOUSE5,		""		},
{"MWHEELUP",	K_MWHEELUP,	""		},
{"MWHEELDOWN",	K_MWHEELDOWN,	""		},

// digital keyboard
{"KP_HOME",	K_KP_HOME,	""		},
{"KP_UPARROW",	K_KP_UPARROW,	"+forward"	},
{"KP_PGUP",	K_KP_PGUP,	""		},
{"KP_LEFTARROW",	K_KP_LEFTARROW,	"+left"		},
{"KP_5",		K_KP_5,		""		},
{"KP_RIGHTARROW",	K_KP_RIGHTARROW,	"+right"		},
{"KP_END",	K_KP_END,		"centerview"	},
{"KP_DOWNARROW",	K_KP_DOWNARROW,	"+back"		},
{"KP_PGDN",	K_KP_PGDN,	"+lookup" 	},
{"KP_ENTER",	K_KP_ENTER,	""		},
{"KP_INS",	K_KP_INS,		""		},
{"KP_DEL",	K_KP_DEL,		"+lookdown"	},
{"KP_SLASH",	K_KP_SLASH,	""		},
{"KP_MINUS",	K_KP_MINUS,	""		},
{"KP_PLUS",	K_KP_PLUS,	""		},
{"PAUSE",		K_PAUSE,		"pause"		},

// Gamepad
// A/B X/Y names match the Xbox controller layout
{"A_BUTTON", K_A_BUTTON, "+jump"},
{"B_BUTTON", K_B_BUTTON, "+use"},
{"X_BUTTON", K_X_BUTTON, "+reload"},
{"Y_BUTTON", K_Y_BUTTON, "impulse 100"}, // Flashlight
{"BACK",   K_BACK_BUTTON, "pause"}, // Menu
{"MODE",   K_MODE_BUTTON, ""},
{"START",  K_START_BUTTON, "cancelselect"},
{"STICK1", K_LSTICK, "+speed"},
{"STICK2", K_RSTICK, "+duck"},
{"L1_BUTTON",  K_L1_BUTTON, "+duck"},
{"R1_BUTTON",  K_R1_BUTTON, "+attack"},
{"DPAD_UP",	K_DPAD_UP,	"impulse 201"}, // Spray
{"DPAD_DOWN",	K_DPAD_DOWN,	"lastinv"},
{"DPAD_LEFT",	K_DPAD_LEFT,	"invprev"},
{"DPAD_RIGHT",	K_DPAD_RIGHT,	"invnext"},
{"L2_BUTTON", K_L2_BUTTON, "+speed"},
{"R2_BUTTON", K_R2_BUTTON, "+attack2"},
{"LTRIGGER" , K_JOY1 , "+speed"}, // L2 in SDL2
{"RTRIGGER" , K_JOY2 , "+attack2"}, // R2 in SDL2
{"JOY3" , K_JOY3 , ""},
{"JOY4" , K_JOY4 , ""},
{"C_BUTTON", K_C_BUTTON, ""},
{"Z_BUTTON", K_Z_BUTTON, ""},
{"MISC_BUTTON", K_MISC_BUTTON, ""},
{"PADDLE1", K_PADDLE1_BUTTON, ""},
{"PADDLE2", K_PADDLE2_BUTTON, ""},
{"PADDLE3", K_PADDLE3_BUTTON, ""},
{"PADDLE4", K_PADDLE4_BUTTON, ""},
{"TOUCHPAD", K_TOUCHPAD, ""},
{"AUX26", K_AUX26, ""}, // generic
{"AUX27", K_AUX27, ""},
{"AUX28", K_AUX28, ""},
{"AUX29", K_AUX29, ""},
{"AUX30", K_AUX30, ""},
{"AUX31", K_AUX31, ""},
{"AUX32", K_AUX32, ""},

// raw semicolon seperates commands
{"SEMICOLON",	';',		""		},
};

static void OSK_EnableTextInput( qboolean enable, qboolean force );
static qboolean OSK_KeyEvent( int key, int down );
static CVAR_DEFINE_AUTO( osk_enable, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "enable built-in on-screen keyboard" );
static CVAR_DEFINE_AUTO( key_rotate, "0", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "rotate arrow keys (0-3)" );

/*
===================
Key_IsDown
===================
*/
int GAME_EXPORT Key_IsDown( int keynum )
{
	if( keynum == -1 )
		return false;
	return keys[keynum].down;
}

/*
===================
Key_StringToKeynum

Returns a key number to be used to index keys[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.

0x11 will be interpreted as raw hex, which will allow new controlers

to be configured even if they don't have defined names.
===================
*/
static int Key_StringToKeynum( const char *str )
{
	int i;

	if( !str || !str[0] )
		return -1;

	if( !str[1] )
		return str[0];

	// check for hex code
	if( str[0] == '0' && str[1] == 'x' && Q_strlen( str ) == 4 )
		return COM_Nibble( str[2] ) << 4 | COM_Nibble( str[3] );

	// scan for a text match
	for( i = 0; i < ARRAYSIZE( keynames ); i++ )
	{
		if( !Q_stricmp( str, keynames[i].name ))
			return keynames[i].keynum;
	}

	return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, a K_* name, or a 0x11 hex string) for the
given keynum.
===================
*/
const char *Key_KeynumToString( int keynum )
{
	static char	tinystr[5];
	int		i, j;

	if( keynum == -1 )
		return "<KEY NOT FOUND>";

	if( keynum < 0 || keynum > 255 )
		return "<OUT OF RANGE>";

	// check for printable ascii (don't use quote)
	if( keynum > 32 && keynum < 127 && keynum != '"' && keynum != ';' && keynum != K_SCROLLLOCK )
	{
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}

	// check for a key string
	for( i = 0; i < ARRAYSIZE( keynames ); i++ )
	{
		if( keynum == keynames[i].keynum )
			return keynames[i].name;
	}

	// make a hex string
	i = keynum >> 4;
	j = keynum & 15;

	tinystr[0] = '0';
	tinystr[1] = 'x';
	tinystr[2] = i > 9 ? i - 10 + 'a' : i + '0';
	tinystr[3] = j > 9 ? j - 10 + 'a' : j + '0';
	tinystr[4] = 0;

	return tinystr;
}

/*
===================
Key_SetBinding
===================
*/
void GAME_EXPORT Key_SetBinding( int keynum, const char *binding )
{
	if( keynum == -1 ) return;

	// free old bindings
	if( keys[keynum].binding )
	{
		Mem_Free((char *)keys[keynum].binding );
		keys[keynum].binding = NULL;
	}

	// allocate memory for new binding
	keys[keynum].binding = copystring( binding );
}


/*
===================
Key_GetBinding
===================
*/
const char *Key_GetBinding( int keynum )
{
	if( keynum == -1 ) return NULL;
	return keys[keynum].binding;
}

/*
===================
Key_GetKey
===================
*/
static int Key_GetKey( const char *pBinding )
{
	int		 i, len;
	const char	*p;

	if( !pBinding ) return -1;

	len = Q_strlen( pBinding );

	for( i = 0; i < 256; i++ )
	{
		if( !keys[i].binding )
			continue;

		p = keys[i].binding;

		if( *p == '+' )
			p++;

		if( !Q_strnicmp( p, pBinding, len ) )
			return i;
	}

	return -1;
}

/*
=============
Key_LookupBinding

=============
*/
const char *Key_LookupBinding( const char *pBinding )
{
	return Key_KeynumToString( Key_GetKey( pBinding ));
}

/*
===================
Key_Unbind_f
===================
*/
static void Key_Unbind_f( void )
{
	int	b;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "unbind <key> : remove commands from a key\n" );
		return;
	}

	b = Key_StringToKeynum( Cmd_Argv( 1 ));

	if( b == -1 )
	{
		Con_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ));
		return;
	}

	if( b == K_ESCAPE )
	{
		Con_Printf( "Can't unbind ESCAPE key\n" );
		return;
	}

	Key_SetBinding( b, "" );
}

/*
===================
Key_Unbindall_f
===================
*/
static void Key_Unbindall_f( void )
{
	int	i;

	for( i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( keys[i].binding )
			Key_SetBinding( i, "" );
	}

	// set some defaults
	Key_SetBinding( K_ESCAPE, "cancelselect" );
	Key_SetBinding( K_START_BUTTON, "cancelselect" );
}

/*
===================
Key_Reset_f
===================
*/
static void Key_Reset_f( void )
{
	int	i;

	// clear all keys first
	for( i = 0; i < ARRAYSIZE( keys ); i++ )
	{
		if( keys[i].binding )
			Key_SetBinding( i, "" );
	}

	// apply default values
	for( i = 0; i < ARRAYSIZE( keynames ); i++ )
		Key_SetBinding( keynames[i].keynum, keynames[i].binding );
}

/*
===================
Key_Bind_f
===================
*/
static void Key_Bind_f( void )
{
	char	cmd[1024];
	int	i, c, b;

	c = Cmd_Argc();

	if( c < 2 )
	{
		Con_Printf( S_USAGE "bind <key> [command] : attach a command to a key\n" );
		return;
	}

	b = Key_StringToKeynum( Cmd_Argv( 1 ));

	if( b == -1 )
	{
		Con_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ));
		return;
	}

	if( c == 2 )
	{
		if( keys[b].binding )
			Con_Printf( "\"%s\" = \"%s\"\n", Cmd_Argv( 1 ), keys[b].binding );
		else Con_Printf( "\"%s\" is not bound\n", Cmd_Argv( 1 ));
		return;
	}

	// copy the rest of the command line
	cmd[0] = 0; // start out with a null string

	for( i = 2; i < c; i++ )
	{
		Q_strncat( cmd, Cmd_Argv( i ), sizeof( cmd ));
		if( i != ( c - 1 )) Q_strncat( cmd, " ", sizeof( cmd ));
	}

	Key_SetBinding( b, cmd );
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings( file_t *f )
{
	int	i;
	string newCommand;

	if( !f ) return;

	FS_Printf( f, "unbindall\n" );

	for( i = 0; i < 256; i++ )
	{
		if( !COM_CheckString( keys[i].binding ))
			continue;

		Cmd_Escape( newCommand, keys[i].binding, sizeof( newCommand ));
		FS_Printf( f, "bind %s \"%s\"\n", Key_KeynumToString( i ), newCommand );
	}
}

/*
============
Key_Bindlist_f

============
*/
static void Key_Bindlist_f( void )
{
	int	i;

	for( i = 0; i < 256; i++ )
	{
		if( !COM_CheckString( keys[i].binding ))
			continue;

		Con_Printf( "%s \"%s\"\n", Key_KeynumToString( i ), keys[i].binding );
	}
}

/*
==============================================================================

			LINE TYPING INTO THE CONSOLE

==============================================================================
*/
/*
===================
Key_Init
===================
*/
void Key_Init( void )
{
	int i;

	// register our functions
	Cmd_AddRestrictedCommand( "bind", Key_Bind_f, "binds a command to the specified key in bindmap" );
	Cmd_AddRestrictedCommand( "unbind", Key_Unbind_f, "removes a command on the specified key in bindmap" );
	Cmd_AddRestrictedCommand( "unbindall", Key_Unbindall_f, "removes all commands from all keys in bindmap" );
	Cmd_AddRestrictedCommand( "resetkeys", Key_Reset_f, "reset all keys to their default values" );
	Cmd_AddCommand( "bindlist", Key_Bindlist_f, "display current key bindings" );
	Cmd_AddCommand( "makehelp", Key_EnumCmds_f, "write help.txt that contains all console cvars and cmds" );

	// setup default binding. "unbindall" from config.cfg will be reset it
	for( i = 0; i < ARRAYSIZE( keynames ); i++ )
		Key_SetBinding( keynames[i].keynum, keynames[i].binding );

	Cvar_RegisterVariable( &osk_enable );
	Cvar_RegisterVariable( &key_rotate );

}

/*
===================
Key_AddKeyCommands
===================
*/
static void Key_AddKeyCommands( int key, const char *kb, qboolean down )
{
	char	button[1024];
	char	*buttonPtr;
	char	cmd[1024];
	int	i;

	if( !kb ) return;
	buttonPtr = button;

	for( i = 0; ; i++ )
	{
		if( kb[i] == ';' || !kb[i] )
		{
			*buttonPtr = '\0';
			if( button[0] == '+' )
			{
				// button commands add keynum as a parm
				if( down ) Q_snprintf( cmd, sizeof( cmd ), "%s %i\n", button, key );
				else Q_snprintf( cmd, sizeof( cmd ), "-%s %i\n", button + 1, key );
				Cbuf_AddText( cmd );
			}
			else if( down )
			{
				// down-only command
				Cbuf_AddText( button );
				Cbuf_AddText( "\n" );
			}

			buttonPtr = button;
			while((((byte)kb[i]) <= ' ' || kb[i] == ';' ) && kb[i] != 0 )
				i++;
		}

		*buttonPtr++ = kb[i];
		if( !kb[i] ) break;
	}
}

/*
===================
Key_IsAllowedAutoRepeat

List of keys that allows auto-repeat
===================
*/
static qboolean Key_IsAllowedAutoRepeat( int key )
{
	if( cls.key_dest != key_game )
		return true;

	switch( key )
	{
	case K_BACKSPACE:
	case K_PAUSE:
	case K_PGUP:
	case K_KP_PGUP:
	case K_PGDN:
	case K_KP_PGDN:
		return true;
	default:
		return false;
	}
}

static int Key_Rotate( int key )
{
	if( key_rotate.value == 1.0f ) // CW
	{
		if( key == K_UPARROW )
				key = K_LEFTARROW;
		else if( key == K_LEFTARROW )
				key = K_DOWNARROW;
		else if( key == K_RIGHTARROW )
				key = K_UPARROW;
		else if( key == K_DOWNARROW )
				key = K_RIGHTARROW;
	}

	else if( key_rotate.value == 3.0f ) // CCW
	{
		if( key == K_UPARROW )
				key = K_RIGHTARROW;
		else if( key == K_LEFTARROW )
				key = K_UPARROW;
		else if( key == K_RIGHTARROW )
				key = K_DOWNARROW;
		else if( key == K_DOWNARROW )
				key = K_LEFTARROW;
	}

	else if( key_rotate.value == 2.0f )
	{
		if( key == K_UPARROW )
				key = K_DOWNARROW;
		else if( key == K_LEFTARROW )
				key = K_RIGHTARROW;
		else if( key == K_RIGHTARROW )
				key = K_LEFTARROW;
		else if( key == K_DOWNARROW )
				key = K_UPARROW;
	}

	return key;
}


/*
===================
Key_Event

Called by the system for both key up and key down events
===================
*/
void GAME_EXPORT Key_Event( int key, int down )
{
	const char	*kb;

	key = Key_Rotate( key );

	if( OSK_KeyEvent( key, down ) )
		return;

	// key was pressed before engine was run
	if( !keys[key].down && !down )
		return;

	kb = keys[key].binding;
	keys[key].down = down;

#ifdef HACKS_RELATED_HLMODS
	if(( cls.key_dest == key_game ) && ( cls.state == ca_cinematic ) && ( key != K_ESCAPE || !down ))
	{
		// only escape passed when cinematic is playing
		// HLFX 0.6 bug: crash in vgui3.dll while press +attack during movie playback
		return;
	}
#endif
	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_game && ( down || keys[key].gamedown ))
	{
		if( !clgame.dllFuncs.pfnKey_Event( down, key, keys[key].binding ))
		{
			if( keys[key].repeats == 0 && down )
			{
				keys[key].gamedown = true;
			}

			if( !down )
			{
				keys[key].gamedown = false;
				keys[key].repeats = 0;
			}
			return; // handled in client.dll
		}
	}

	// update auto-repeat status
	if( down )
	{
		keys[key].repeats++;

		if( !Key_IsAllowedAutoRepeat( key ) && keys[key].repeats > 1 )
		{
			// ignore most autorepeats
			return;
		}

		if( key >= 200 && !kb )
			Con_Printf( "%s is unbound.\n", Key_KeynumToString( key ));
	}
	else
	{
		keys[key].gamedown = false;
		keys[key].repeats = 0;
	}

	VGui_KeyEvent( key, down );

	// console key is hardcoded, so the user can never unbind it
	if( key == '`' || key == '~' )
	{
		// we are in typing mode, so don't switch to console
		if( cls.key_dest == key_message || !down )
			return;

		Con_ToggleConsole_f();
		return;
	}

	// escape is always handled special
	if( key == K_ESCAPE && down )
	{
		switch( cls.key_dest )
		{
		case key_game:
			if( r_showtextures.value )
			{
				// close texture atlas
				Cvar_DirectSet( &r_showtextures, "0" );
				return;
			}
			else if( host.mouse_visible && cls.state != ca_cinematic )
			{
				clgame.dllFuncs.pfnKey_Event( down, key, keys[key].binding );
				return; // handled in client.dll
			}
			break;
		default:
			break;
		}
	}

	if( cls.key_dest == key_menu )
	{
		// classic Xash3D menus don't have an extension that tells engine
		// to enable text input
		if( !gameui.use_extended_api )
		{
			// we don't know if menu wants text input or not
			// enable it unconditionally
			Key_EnableTextInput( true, false );

			// pass this key to the menu, if printable
			if( !host.textmode && down && ( key >= 32 ) && ( key <= 'z' ))
			{
				if( Key_IsDown( K_SHIFT ))
					key += 'A' - 'a';

				UI_CharEvent( key );
			}
		}

		UI_KeyEvent( key, down );
		return;
	}

	// key up events only perform actions if the game key binding is
	// a button command (leading + sign).  These will be processed even in
	// console mode and menu mode, to keep the character from continuing
	// an action started before a mode switch.
	if( !down )
	{
		Key_AddKeyCommands( key, kb, down );
		return;
	}

	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_game )
	{
		Key_AddKeyCommands( key, kb, down );
	}
	else if( cls.key_dest == key_console )
	{
		Key_Console( key );
	}
	else if( cls.key_dest == key_message )
	{
		Key_Message( key );
	}
}

/*
================
Key_EnableTextInput

================
*/
void Key_EnableTextInput( qboolean enable, qboolean force )
{
	if( osk_enable.value )
	{
		OSK_EnableTextInput( enable, force );
		return;
	}
	if( enable && ( !host.textmode || force ))
		Platform_EnableTextInput( true );
	else if( !enable && ( host.textmode || force ))
		Platform_EnableTextInput( false );

	host.textmode = enable;
}

/*
=========
Key_SetKeyDest
=========
*/
void GAME_EXPORT Key_SetKeyDest( int key_dest )
{
	IN_ToggleClientMouse( key_dest, cls.key_dest );

	switch( key_dest )
	{
	case key_game:
		Key_EnableTextInput( false, false );
		cls.key_dest = key_game;
		break;
	case key_menu:
		Key_EnableTextInput( false, false );
		cls.key_dest = key_menu;
		break;
	case key_console:
#if !XASH_NSWITCH && !XASH_PSVITA // if we don't disable this, pops up the keyboard during load
		Key_EnableTextInput( true, false );
#endif
		cls.key_dest = key_console;
		break;
	case key_message:
		Key_EnableTextInput( true, false );
		cls.key_dest = key_message;
		break;
	default:
		Host_Error( "%s: wrong destination (%i)\n", __func__, key_dest );
		break;
	}
}

/*
===================
Key_ClearStates
===================
*/
void GAME_EXPORT Key_ClearStates( void )
{
	int	i;

	// don't clear keys during changelevel
	if( cls.changelevel )
		return;

	for( i = 0; i < 256; i++ )
	{
		if( i >= K_MOUSE1 && i <= K_MOUSE5 )
			IN_MouseEvent( i - K_MOUSE1, false );
		else
			Key_Event( i, false );

		keys[i].down = 0;
		keys[i].repeats = 0;
		keys[i].gamedown = 0;
	}

	if( clgame.hInstance )
		clgame.dllFuncs.IN_ClearStates();
}

/*
===================
CL_CharEvent

Normal keyboard characters, already shifted / capslocked / etc
===================
*/
void CL_CharEvent( int key )
{
	// the console key should never be used as a char
	if( key == '`' || key == '~' ) return;

	if( cls.key_dest == key_console && !Con_Visible( ))
	{
		if((char)key == '`' || (char)key == '?' )
			return; // don't pass '`' when we open the console
	}

	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_console || cls.key_dest == key_message )
	{
		Con_CharEvent( key );
	}
	else if( cls.key_dest == key_menu )
	{
		UI_CharEvent( key );
	}
}

/*
============
Key_ToUpper

A helper function if platform input doesn't support text mode properly
============
*/
int Key_ToUpper( int keynum )
{
	if( keynum == '-' )
		return '_';
	if( keynum == '=' )
		return '+';
	if( keynum == ';' )
		return ':';
	if( keynum == '\'' )
		return '"';

	return Q_toupper( keynum );
}

/* On-screen keyboard:
 *
 * 4 lines with 13 buttons each
 * Left trigger == backspace
 * Right trigger == space
 * Any button press is button press on keyboard
 *
 * Our layout:
 *  0  1  2  3  4  5  6  7  8  9  10 11 12
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |` |1 |2 |3 |4 |5 |6 |7 |8 |9 |0 |- |= | 0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |q |w |e |r |t |y |u |i |o |p |[ |] |\ | 1
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |CL|a |s |d |f |g |h |j |k |l |; |' |BS| 2
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |SH|z |x |c |v |b |n |m |, |. |/ |SP|EN| 3
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 */

#define MAX_OSK_ROWS 13
#define MAX_OSK_LINES 4

enum
{
	OSK_DEFAULT = 0,
	OSK_UPPER, // on caps, shift
	/*
	OSK_RUSSIAN,
	OSK_RUSSIAN_UPPER,
	*/
	OSK_LAST
};

enum
{
	OSK_TAB = 16,
	OSK_SHIFT,
	OSK_BACKSPACE,
	OSK_ENTER,
	OSK_SPECKEY_LAST
};
static const char *osk_keylayout[][4] =
{
	{
			  "`1234567890-=",  // 13
			  "qwertyuiop[]\\", // 13
		"\x10" "asdfghjkl;'" "\x12",   // 11 + caps on a left, enter on a right
		"\x11" "zxcvbnm,./ " "\x13"     // 10 + esc on left + shift on a left/right
	},
	{
			  "~!@#$%^&*()_+",
			  "QWERTYUIOP{}|",
		"\x10" "ASDFGHJKL:\"" "\x12",
		"\x11" "ZXCVBNM<>? "  "\x13"
	}
};

struct osk_s
{
	qboolean enable;
	int curlayout;
	qboolean shift;
	qboolean sending;
	struct {
		signed char x;
		signed char y;
		char val;
	} curbutton;
} osk;

static qboolean OSK_KeyEvent( int key, int down )
{
	if( !osk.enable || !osk_enable.value )
		return false;

	if( osk.sending )
	{
		osk.sending = false;
		return false;
	}

	if( osk.curbutton.val == 0 )
	{
		if( key == K_ENTER )
		{
			osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
			return true;
		}
		return false;
	}


	switch ( key )
	{
	case K_ENTER:
		switch( osk.curbutton.val )
		{
		case OSK_ENTER:
			osk.sending  = true;
			Key_Event( K_ENTER, down );
			//osk_enable = false; // TODO: handle multiline
			break;
		case OSK_SHIFT:
			if( !down )
				break;

			if( osk.curlayout & 1 )
				osk.curlayout--;
			else
				osk.curlayout++;

			osk.shift = true;
			osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
			break;
		case OSK_BACKSPACE:
			Key_Event( K_BACKSPACE, down ); break;
		case OSK_TAB:
			Key_Event( K_TAB, down ); break;
		default:
			{
				int ch;

				if( !down )
				{
					if( osk.shift && osk.curlayout & 1 )
						osk.curlayout--;

					osk.shift = false;
					osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
					break;
				}

				ch = (byte)osk.curbutton.val;

				// do not pass UTF-8 sequence into the engine, convert it here
				if( !cls.accept_utf8 )
					ch = Con_UtfProcessCharForce( ch );

				if( !ch )
					break;

				CL_CharEvent( ch );
				break;
			}
		}
		break;
	case K_UPARROW:
		if( down && --osk.curbutton.y < 0 )
		{
			osk.curbutton.y = MAX_OSK_LINES - 1;
			osk.curbutton.val = 0;
			return true;
		}
		break;
	case K_DOWNARROW:
		if( down && ++osk.curbutton.y >= MAX_OSK_LINES )
		{
			osk.curbutton.y = 0;
			osk.curbutton.val = 0;
			return true;
		}
		break;
	case K_LEFTARROW:
		if( down && --osk.curbutton.x < 0 )
			osk.curbutton.x = MAX_OSK_ROWS - 1;
		break;
	case K_RIGHTARROW:
		if( down && ++osk.curbutton.x >= MAX_OSK_ROWS )
			osk.curbutton.x = 0;
		break;
	default:
		return false;
	}

	osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
	return true;

}

/*
=============
OSK_EnableTextInput

Enables built-in IME
=============
*/
static void OSK_EnableTextInput( qboolean enable, qboolean force )
{
	qboolean old = osk.enable;

	osk.enable = enable;

	if( osk.enable && (!old || force) )
	{
		osk.curlayout = 0;
		osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
	}
}

#define X_START 0.1347475f
#define Y_START 0.567f
#define X_STEP 0.05625
#define Y_STEP 0.0825

/*
============
Joy_DrawSymbolButton

Draw button with symbol on it
============
*/
static void OSK_DrawSymbolButton( int symb, float x, float y, float width, float height )
{
	cl_font_t *font = Con_GetCurFont();
	byte color[] = { 255, 255, 255, 255 };
	int x1 = x * refState.width,
		y1 = y * refState.height,
		w = width * refState.width,
		h = height * refState.height;

	if( symb == osk.curbutton.val )
		ref.dllFuncs.FillRGBA( kRenderTransTexture, x1, y1, w, h, 255, 160, 0, 100 );

	if( !symb || symb == ' ' || (symb >= OSK_TAB && symb < OSK_SPECKEY_LAST ) )
		return;

	CL_DrawCharacter(
		x1 + width * 0.4 * refState.width,
		y1 + height * 0.4 * refState.height,
		symb, color, font, 0 );
}

/*
=============
Joy_DrawSpecialButton

Draw special button, like shift, enter or esc
=============
*/
static void OSK_DrawSpecialButton( const char *name, float x, float y, float width, float height )
{
	byte color[] = { 0, 255, 0, 255 };

	Con_DrawString(
		x * refState.width + width * 0.4 * refState.width,
		y * refState.height + height * 0.4 * refState.height,
		name,
		color );
}


/*
=============
Joy_DrawOnScreenKeyboard

Draw on screen keyboard, if enabled
=============
*/
void OSK_Draw( void )
{
	const char **curlayout = osk_keylayout[osk.curlayout]; // shortcut :)
	float  x, y;
	int i, j;

	if( !osk.enable || !osk_enable.value || !osk.curbutton.val )
		return;

	// draw keyboard
	ref.dllFuncs.FillRGBA( kRenderTransTexture, X_START * refState.width, Y_START * refState.height,
					  X_STEP * MAX_OSK_ROWS * refState.width,
					  Y_STEP * MAX_OSK_LINES * refState.height, 100, 100, 100, 100 );

	OSK_DrawSpecialButton( "-]",   X_START,               Y_START + Y_STEP * 2, X_STEP, Y_STEP );
	OSK_DrawSpecialButton( "<-",  X_START + X_STEP * 12, Y_START + Y_STEP * 2, X_STEP, Y_STEP );

	OSK_DrawSpecialButton( "sh", X_START,               Y_START + Y_STEP * 3, X_STEP, Y_STEP );
	OSK_DrawSpecialButton( "en", X_START + X_STEP * 12, Y_START + Y_STEP * 3, X_STEP, Y_STEP );

	for( y = Y_START,     j = 0; j < MAX_OSK_LINES; j++, y += Y_STEP )
		for( x = X_START, i = 0; i < MAX_OSK_ROWS;  i++, x += X_STEP )
			OSK_DrawSymbolButton( curlayout[j][i], x, y, X_STEP, Y_STEP );
}
