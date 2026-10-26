/*
kbm_ps4.c - PlayStation 4 USB keyboard and mouse
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

/*
Keyboard keys come as USB HID usage codes (the same numbers as scancodes of SDL),
the layout is US: characters of the text input are made here, the system keyboard
(in_ps4.c) is opened for gamepad only. The mouse reports relative movement, the
cursor of menus is kept here.
*/

#include <stdbool.h>
#include "common.h"
#include "platform/platform.h"
#include "input.h"
#include "keydefs.h"
#include "client.h"
#include "vgui_draw.h"
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <orbis/Keyboard.h>

// libSceMouse, headers of the toolchain have no prototypes for it
int32_t sceMouseInit( void );
int32_t sceMouseOpen( int32_t userId, int32_t type, int32_t index, void *param );
int32_t sceMouseRead( int32_t handle, void *data, int32_t num );
int32_t sceMouseClose( int32_t handle );

typedef struct
{
	uint64_t timestamp;
	bool     connected;
	uint32_t buttons;
	int32_t  x;
	int32_t  y;
	int32_t  wheel;
	int32_t  tilt;
	uint8_t  reserved[8];
} ps4_mousedata_t;

#define PS4_MOUSE_BUTTONS    5 // primary, secondary, middle and two side ones, bits 0..4
#define PS4_MOUSE_SAMPLES    16
#define PS4_KEYBOARD_SAMPLES 8
#define PS4_KBD_PROBES       8
#define PS4_RETRY_INTERVAL   2.0
#define PS4_KEY_REPEAT_DELAY 0.4
#define PS4_KEY_REPEAT_RATE  0.04
#define PS4_HID_KEYS         128

// state of modifiers in OrbisKeyboardData::mods
#define PS4_MOD_CTRL  ( ORBIS_KEYBOARD_MOD_LEFT_CTRL  | ORBIS_KEYBOARD_MOD_RIGHT_CTRL )
#define PS4_MOD_SHIFT ( ORBIS_KEYBOARD_MOD_LEFT_SHIFT | ORBIS_KEYBOARD_MOD_RIGHT_SHIFT )
#define PS4_MOD_ALT   ( ORBIS_KEYBOARD_MOD_LEFT_ALT   | ORBIS_KEYBOARD_MOD_RIGHT_ALT )
#define PS4_MOD_META  ( ORBIS_KEYBOARD_MOD_LEFT_META  | ORBIS_KEYBOARD_MOD_RIGHT_META )

static struct
{
	qboolean initialized;
	double   retry_time;

	// keyboard
	int32_t  kbd;
	struct { int32_t handle, user, index; } probes[PS4_KBD_PROBES]; // other ways to open the keyboard, if the main one is silent
	int      num_probes;
	int      read_error;            // the last error of reading, to log it once
	qboolean kbd_active;            // the keys were used, so the system keyboard stays closed
	byte     pressed[PS4_HID_KEYS]; // keys that were down at the previous update
	uint32_t mods;
	uint32_t locks;
	qboolean stream_works, state_works;
	int      repeat_key;            // HID code, 0 - none
	double   repeat_time;

	// mouse
	int32_t  mice[PS4_KBD_PROBES];  // the same mouse can be opened in several ways, see PS4_KbmInit
	int      num_mice;
	int      mouse_active;          // index in mice[] of the one that gives data, -1 - none yet
	qboolean mouse_connected;
	uint32_t mouse_buttons;
	float    move_x, move_y;        // movement for the game, taken by Platform_MouseMove
	int      cursor_x, cursor_y;    // cursor of the menu
	qboolean grab;
} ps4kbm = { .kbd = -1, .mouse_active = -1 };

/*
==============
PS4_HIDToKey

Returns engine keynum of the key. Characters of the text input are made
in PS4_HIDToChar
==============
*/
static int PS4_HIDToKey( int hid )
{
	if( hid >= 4 && hid <= 29 )
		return 'a' + hid - 4;
	if( hid >= 30 && hid <= 38 )
		return '1' + hid - 30;
	if( hid >= 58 && hid <= 69 )
		return K_F1 + hid - 58;

	switch( hid )
	{
	case 39: return '0';
	case 40: return K_ENTER;
	case 41: return K_ESCAPE;
	case 42: return K_BACKSPACE;
	case 43: return K_TAB;
	case 44: return K_SPACE;
	case 45: return '-';
	case 46: return '=';
	case 47: return '[';
	case 48: return ']';
	case 49: return '\\';
	case 51: return ';';
	case 52: return '\'';
	case 53: return '`';
	case 54: return ',';
	case 55: return '.';
	case 56: return '/';
	case 57: return K_CAPSLOCK;
	case 70: return 0; // print screen
	case 71: return K_SCROLLLOCK;
	case 72: return K_PAUSE;
	case 73: return K_INS;
	case 74: return K_HOME;
	case 75: return K_PGUP;
	case 76: return K_DEL;
	case 77: return K_END;
	case 78: return K_PGDN;
	case 79: return K_RIGHTARROW;
	case 80: return K_LEFTARROW;
	case 81: return K_DOWNARROW;
	case 82: return K_UPARROW;
	case 83: return K_KP_NUMLOCK;
	case 84: return K_KP_SLASH;
	case 85: return '*';
	case 86: return K_KP_MINUS;
	case 87: return K_KP_PLUS;
	case 88: return K_KP_ENTER;
	case 89: return K_KP_END;
	case 90: return K_KP_DOWNARROW;
	case 91: return K_KP_PGDN;
	case 92: return K_KP_LEFTARROW;
	case 93: return K_KP_5;
	case 94: return K_KP_RIGHTARROW;
	case 95: return K_KP_HOME;
	case 96: return K_KP_UPARROW;
	case 97: return K_KP_PGUP;
	case 98: return K_KP_INS;
	case 99: return K_KP_DEL;
	case 101: return K_WIN; // application (menu) key
	}

	return 0;
}

/*
==============
PS4_HIDToChar

US layout, returns 0 for keys without a character. The keypad is
digits only with num lock, like the keyboards do
==============
*/
static int PS4_HIDToChar( int hid, qboolean shift, qboolean caps, qboolean numlock )
{
	static const char digits_shifted[] = ")!@#$%^&*(";
	int ch;

	if( hid >= 4 && hid <= 29 )
	{
		ch = 'a' + hid - 4;
		return ( shift != caps ) ? ch - 'a' + 'A' : ch;
	}

	if( hid >= 30 && hid <= 39 )
	{
		int digit = hid == 39 ? 0 : hid - 29;

		return shift ? digits_shifted[digit] : '0' + digit;
	}

	if( hid >= 89 && hid <= 98 && numlock )
		return hid == 98 ? '0' : '1' + hid - 89;

	switch( hid )
	{
	case 44: return ' ';
	case 45: return shift ? '_' : '-';
	case 46: return shift ? '+' : '=';
	case 47: return shift ? '{' : '[';
	case 48: return shift ? '}' : ']';
	case 49: return shift ? '|' : '\\';
	case 51: return shift ? ':' : ';';
	case 52: return shift ? '"' : '\'';
	case 53: return shift ? '~' : '`';
	case 54: return shift ? '<' : ',';
	case 55: return shift ? '>' : '.';
	case 56: return shift ? '?' : '/';
	case 84: return '/';
	case 85: return '*';
	case 86: return '-';
	case 87: return '+';
	case 99: return numlock ? '.' : 0;
	}

	return 0;
}

/*
==============
PS4_KeyDown

A key goes down (or repeats): the engine takes the character when it edits text and
the key otherwise, like it is with SDL: console key is always a key
==============
*/
static void PS4_KeyDown( int hid, uint32_t mods, uint32_t locks )
{
	const qboolean shift = FBitSet( mods, PS4_MOD_SHIFT ) ? true : false;
	const qboolean ctrl = FBitSet( mods, PS4_MOD_CTRL ) ? true : false;
	const qboolean caps = FBitSet( locks, ORBIS_KEYBOARD_CAPS_LOCK ) ? true : false;
	const qboolean numlock = FBitSet( locks, ORBIS_KEYBOARD_NUM_LOCK ) ? true : false;
	const int key = PS4_HIDToKey( hid );

	if( host.textmode )
	{
		// this is how the engine understands ctrl+c, ctrl+v and other hotkeys
		if( cls.key_dest != key_game && ctrl )
		{
			if( hid >= 4 && hid <= 29 )
				CL_CharEvent( hid - 3 );
			return;
		}

		const int ch = PS4_HIDToChar( hid, shift, caps, numlock );
		const qboolean console_key = hid == 53 && cls.key_dest == key_console;

		if( ch && !console_key )
		{
			const char text[2] = { (char)ch, 0 };

			VGui_ReportTextInput( text );
			CL_CharEvent( ch );
			return;
		}
	}

	// keypad is digits with num lock
	if( numlock && hid >= 89 && hid <= 98 )
		Key_Event( hid == 98 ? '0' : '1' + hid - 89, true );
	else if( key )
		Key_Event( key, true );
}

static void PS4_KeyUp( int hid, uint32_t locks )
{
	const qboolean numlock = FBitSet( locks, ORBIS_KEYBOARD_NUM_LOCK ) ? true : false;
	const int key = PS4_HIDToKey( hid );

	if( numlock && hid >= 89 && hid <= 98 )
		Key_Event( hid == 98 ? '0' : '1' + hid - 89, false );
	else if( key )
		Key_Event( key, false );
}

static void PS4_ModEvent( uint32_t mods, uint32_t old, uint32_t mask, int key )
{
	const qboolean now = FBitSet( mods, mask ) ? true : false;

	if( now != ( FBitSet( old, mask ) ? true : false ))
		Key_Event( key, now );
}

// the stream version of reading, headers of the toolchain have it without a prototype
int32_t ps4_sceKeyboardRead( int32_t handle, OrbisKeyboardData *data, int32_t num ) __asm__( "sceKeyboardRead" );

static qboolean PS4_KeyboardDataIsEmpty( const OrbisKeyboardData *data )
{
	// no keys is one element of the list, a zero
	return data->mods == 0 && data->keycodes[0] == 0 && data->keycodes[1] == 0;
}

/*
==============
PS4_KeyboardApply

Sends events for the difference between the previous and the new state of the keyboard
==============
*/
static void PS4_KeyboardApply( OrbisKeyboardData *data )
{
	byte now[PS4_HID_KEYS] = { 0 };

	if( data->nkeys > ARRAYSIZE( data->keycodes ))
		data->nkeys = ARRAYSIZE( data->keycodes );

	for( uint32_t i = 0; i < data->nkeys; i++ )
	{
		const uint16_t hid = data->keycodes[i];

		// 0 is nothing, 1 is rollover error, modifiers have their own bits
		if( hid >= 4 && hid < PS4_HID_KEYS )
			now[hid] = 1;
	}

	// something of the keyboard is used, the system keyboard is not needed
	// (nkeys is 1 for no keys, the list ends with a zero)
	for( int hid = 4; hid < PS4_HID_KEYS; hid++ )
	{
		if( now[hid] )
			ps4kbm.kbd_active = true;
	}

	PS4_ModEvent( data->mods, ps4kbm.mods, PS4_MOD_CTRL, K_CTRL );
	PS4_ModEvent( data->mods, ps4kbm.mods, PS4_MOD_SHIFT, K_SHIFT );
	PS4_ModEvent( data->mods, ps4kbm.mods, PS4_MOD_ALT, K_ALT );
	PS4_ModEvent( data->mods, ps4kbm.mods, PS4_MOD_META, K_WIN );

	for( int hid = 4; hid < PS4_HID_KEYS; hid++ )
	{
		if( now[hid] && !ps4kbm.pressed[hid] )
		{
			PS4_KeyDown( hid, data->mods, data->locks );

			// the keyboard doesn't repeat by itself, only the last key does
			ps4kbm.repeat_key = hid;
			ps4kbm.repeat_time = host.realtime + PS4_KEY_REPEAT_DELAY;
		}
		else if( !now[hid] && ps4kbm.pressed[hid] )
		{
			PS4_KeyUp( hid, data->locks );

			if( ps4kbm.repeat_key == hid )
				ps4kbm.repeat_key = 0;
		}
	}

	memcpy( ps4kbm.pressed, now, sizeof( now ));
	ps4kbm.mods = data->mods;
	ps4kbm.locks = data->locks;
}

static void PS4_KeyboardUpdate( void )
{
	OrbisKeyboardData samples[PS4_KEYBOARD_SAMPLES];
	OrbisKeyboardData data;
	int count, ret;

	// nothing from the main handle so far, look at the others
	if( !ps4kbm.stream_works && !ps4kbm.state_works )
	{
		for( int i = 0; i < ps4kbm.num_probes; i++ )
		{
			OrbisKeyboardData probe;

			memset( &probe, 0, sizeof( probe ));
			if( sceKeyboardReadState( ps4kbm.probes[i].handle, &probe ) < 0 || PS4_KeyboardDataIsEmpty( &probe ))
				continue;

			Con_Reportf( "PS4 keyboard: handle 0x%08x (user 0x%x, index %d) gives data, using it\n",
				ps4kbm.probes[i].handle, ps4kbm.probes[i].user, ps4kbm.probes[i].index );
			if( ps4kbm.kbd >= 0 )
				sceKeyboardClose( ps4kbm.kbd );

			for( int j = 0; j < ps4kbm.num_probes; j++ )
			{
				if( j != i )
					sceKeyboardClose( ps4kbm.probes[j].handle );
			}

			ps4kbm.kbd = ps4kbm.probes[i].handle;
			ps4kbm.num_probes = 0;
			break;
		}
	}

	if( ps4kbm.kbd < 0 )
		return;

	// stream of changes, it's what the mouse has
	memset( samples, 0, sizeof( samples ));
	count = ps4_sceKeyboardRead( ps4kbm.kbd, samples, ARRAYSIZE( samples ));

	// and the snapshot, ReadState is what the example of the toolchain uses
	memset( &data, 0, sizeof( data ));
	ret = sceKeyboardReadState( ps4kbm.kbd, &data );

	if( count > (int)ARRAYSIZE( samples ))
		count = ARRAYSIZE( samples );

	if( count > 0 )
	{
		ps4kbm.stream_works = true;

		for( int i = 0; i < count; i++ )
			PS4_KeyboardApply( &samples[i] );
	}
	else if( !ps4kbm.stream_works && ret >= 0 )
	{
		// the snapshot is trusted once it had anything in it
		if( !PS4_KeyboardDataIsEmpty( &data ))
			ps4kbm.state_works = true;

		if( ps4kbm.state_works )
			PS4_KeyboardApply( &data );
	}

	// repeat in the console, chat and menus, in the game the keys are held
	if( ps4kbm.repeat_key && ps4kbm.pressed[ps4kbm.repeat_key] && cls.key_dest != key_game
		&& host.realtime >= ps4kbm.repeat_time )
	{
		PS4_KeyDown( ps4kbm.repeat_key, ps4kbm.mods, ps4kbm.locks );
		ps4kbm.repeat_time = host.realtime + PS4_KEY_REPEAT_RATE;
	}
}

static void PS4_MouseSetConnected( qboolean connected )
{
	if( connected == ps4kbm.mouse_connected )
		return;

	ps4kbm.mouse_connected = connected;
	Con_Reportf( "PS4 mouse: %s\n", connected ? "connected" : "disconnected" );
}

// reads one handle of the mouse, returns true if the mouse is connected and gave data
static qboolean PS4_MouseRead( int32_t handle, qboolean *lost )
{
	ps4_mousedata_t data[PS4_MOUSE_SAMPLES];
	qboolean got = false;
	int count;

	memset( data, 0, sizeof( data ));
	*lost = false;
	count = sceMouseRead( handle, data, ARRAYSIZE( data ));
	if( count < 0 )
	{
		*lost = true;
		return false;
	}

	for( int i = 0; i < count; i++ )
	{
		const ps4_mousedata_t *m = &data[i];

		if( !m->connected )
		{
			*lost = true;
			continue;
		}

		got = true;
		*lost = false;
		PS4_MouseSetConnected( true );

		ps4kbm.move_x += m->x;
		ps4kbm.move_y += m->y;

		if( !ps4kbm.grab )
		{
			ps4kbm.cursor_x = bound( 0, ps4kbm.cursor_x + m->x, refState.width - 1 );
			ps4kbm.cursor_y = bound( 0, ps4kbm.cursor_y + m->y, refState.height - 1 );
		}

		const uint32_t changed = m->buttons ^ ps4kbm.mouse_buttons;

		for( int b = 0; b < PS4_MOUSE_BUTTONS; b++ )
		{
			if( FBitSet( changed, BIT( b )))
				IN_MouseEvent( b, FBitSet( m->buttons, BIT( b )) ? 1 : 0 );
		}

		ps4kbm.mouse_buttons = m->buttons;

		if( m->wheel )
			IN_MWheelEvent( m->wheel > 0 ? 1 : -1 );
	}

	return got;
}

static void PS4_MouseUpdate( void )
{
	if( !ps4kbm.num_mice )
		return;

	qboolean lost;

	// the mouse is found: only its handle is read, so the same movement isn't counted twice
	if( ps4kbm.mouse_active >= 0 )
	{
		PS4_MouseRead( ps4kbm.mice[ps4kbm.mouse_active], &lost );
		if( lost )
		{
			for( int b = 0; b < PS4_MOUSE_BUTTONS; b++ )
			{
				if( FBitSet( ps4kbm.mouse_buttons, BIT( b )))
					IN_MouseEvent( b, 0 );
			}
			ps4kbm.mouse_buttons = 0;

			// unplugged, any handle that gives data is taken next time
			ps4kbm.mouse_active = -1;
			PS4_MouseSetConnected( false );
		}
		return;
	}

	// not found yet, the first handle that gives data is used
	for( int i = 0; i < ps4kbm.num_mice; i++ )
	{
		if( PS4_MouseRead( ps4kbm.mice[i], &lost ))
		{
			ps4kbm.mouse_active = i;
			Con_Reportf( "PS4 mouse: handle 0x%08x gives data, using it\n", ps4kbm.mice[i] );
			break;
		}
	}
}

static void PS4_KbmInit( void )
{
	int32_t user = -1;
	int ret;

	ps4kbm.initialized = true;

	if(( ret = sceUserServiceGetInitialUser( &user )) < 0 )
	{
		Con_Printf( S_ERROR "sceUserServiceGetInitialUser failed: 0x%08x\n", ret );
		return;
	}

	ps4kbm.cursor_x = refState.width / 2;
	ps4kbm.cursor_y = refState.height / 2;

	// lists of keyboards and mice are kept by the system, the devices are checked while reading
	// the modules might be loaded and initialized by the system already, that is not a failure
	const int kbd_load = sceSysmoduleLoadModule( ORBIS_SYSMODULE_KEYBOARD );
	const int kbd_init = sceKeyboardInit( );
	ps4kbm.kbd = sceKeyboardOpen( user, 0, 0, NULL );

	// the main way is silent on some firmwares: the same keyboard is opened for the other users and
	// ports, whichever gives data is used
	{
		OrbisUserServiceLoginUserIdList list = { { -1, -1, -1, -1 } };
		int32_t users[2 + ORBIS_USER_SERVICE_MAX_LOGIN_USERS] = { ORBIS_USER_SERVICE_USER_ID_SYSTEM, user };
		int num_users = 2;

		if( sceUserServiceGetLoginUserIdList( &list ) >= 0 )
		{
			for( int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++ )
			{
				if( list.userId[i] != -1 && list.userId[i] != user )
					users[num_users++] = list.userId[i];
			}
		}

		for( int u = 0; u < num_users && ps4kbm.num_probes < PS4_KBD_PROBES; u++ )
		{
			for( int index = 0; index < 2 && ps4kbm.num_probes < PS4_KBD_PROBES; index++ )
			{
				const int32_t handle = sceKeyboardOpen( users[u], 0, index, NULL );

				Con_Reportf( "PS4 keyboard: user 0x%x, index %d: open 0x%08x\n", users[u], index, handle );
				if( handle < 0 || handle == ps4kbm.kbd )
					continue;

				ps4kbm.probes[ps4kbm.num_probes].handle = handle;
				ps4kbm.probes[ps4kbm.num_probes].user = users[u];
				ps4kbm.probes[ps4kbm.num_probes].index = index;
				ps4kbm.num_probes++;
			}
		}
	}

	const int mouse_load = sceSysmoduleLoadModule( ORBIS_SYSMODULE_MOUSE );
	const int mouse_init = sceMouseInit( );

	// as with the keyboard, the mouse isn't always at the first port of the initial user
	// (a receiver of a wireless mouse can take the first place), so the other ones are opened too
	{
		OrbisUserServiceLoginUserIdList list = { { -1, -1, -1, -1 } };
		int32_t users[2 + ORBIS_USER_SERVICE_MAX_LOGIN_USERS] = { user, ORBIS_USER_SERVICE_USER_ID_SYSTEM };
		int num_users = 2;

		if( sceUserServiceGetLoginUserIdList( &list ) >= 0 )
		{
			for( int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++ )
			{
				if( list.userId[i] != -1 && list.userId[i] != user )
					users[num_users++] = list.userId[i];
			}
		}

		for( int u = 0; u < num_users; u++ )
		{
			for( int index = 0; index < 2 && ps4kbm.num_mice < PS4_KBD_PROBES; index++ )
			{
				const int32_t handle = sceMouseOpen( users[u], 0, index, NULL );

				Con_Reportf( "PS4 mouse: user 0x%x, index %d: open 0x%08x\n", users[u], index, handle );
				if( handle >= 0 )
					ps4kbm.mice[ps4kbm.num_mice++] = handle;
			}
		}
	}

	Con_Reportf( "PS4 keyboard: load 0x%08x, init 0x%08x, open 0x%08x\n", kbd_load, kbd_init, ps4kbm.kbd );
	Con_Reportf( "PS4 mouse: load 0x%08x, init 0x%08x, %d handles\n", mouse_load, mouse_init, ps4kbm.num_mice );
}

/*
==============
PS4_KbmUpdate

Once per frame, from Platform_RunEvents
==============
*/
void PS4_KbmUpdate( void )
{
	if( !ps4kbm.initialized )
		PS4_KbmInit( );

	PS4_KeyboardUpdate( );
	PS4_MouseUpdate( );
}

qboolean PS4_KeyboardActive( void )
{
	return ps4kbm.kbd_active;
}

/*
==============
Platform_GetKeyModifiers

==============
*/
key_modifier_t Platform_GetKeyModifiers( void )
{
	key_modifier_t mods = KeyModifier_None;

	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_LEFT_SHIFT ))
		SetBits( mods, KeyModifier_LeftShift );
	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_RIGHT_SHIFT ))
		SetBits( mods, KeyModifier_RightShift );
	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_LEFT_CTRL ))
		SetBits( mods, KeyModifier_LeftCtrl );
	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_RIGHT_CTRL ))
		SetBits( mods, KeyModifier_RightCtrl );
	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_LEFT_ALT ))
		SetBits( mods, KeyModifier_LeftAlt );
	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_RIGHT_ALT ))
		SetBits( mods, KeyModifier_RightAlt );
	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_LEFT_META ))
		SetBits( mods, KeyModifier_LeftSuper );
	if( FBitSet( ps4kbm.mods, ORBIS_KEYBOARD_MOD_RIGHT_META ))
		SetBits( mods, KeyModifier_RightSuper );
	if( FBitSet( ps4kbm.locks, ORBIS_KEYBOARD_NUM_LOCK ))
		SetBits( mods, KeyModifier_NumLock );
	if( FBitSet( ps4kbm.locks, ORBIS_KEYBOARD_CAPS_LOCK ))
		SetBits( mods, KeyModifier_CapsLock );

	return mods;
}

void PS4_KbmShutdown( void )
{
	if( ps4kbm.kbd >= 0 )
		sceKeyboardClose( ps4kbm.kbd );
	for( int i = 0; i < ps4kbm.num_probes; i++ )
		sceKeyboardClose( ps4kbm.probes[i].handle );
	for( int i = 0; i < ps4kbm.num_mice; i++ )
		sceMouseClose( ps4kbm.mice[i] );

	ps4kbm.kbd = -1;
	ps4kbm.num_probes = 0;
	ps4kbm.num_mice = 0;
	ps4kbm.mouse_active = -1;
	ps4kbm.initialized = false;
}

/*
==============
platform interface of the mouse
==============
*/
void Platform_MouseMove( float *x, float *y )
{
	*x = ps4kbm.move_x;
	*y = ps4kbm.move_y;
	ps4kbm.move_x = ps4kbm.move_y = 0.0f;
}

void GAME_EXPORT Platform_GetMousePos( int *x, int *y )
{
	if( x ) *x = ps4kbm.cursor_x;
	if( y ) *y = ps4kbm.cursor_y;
}

void GAME_EXPORT Platform_SetMousePos( int x, int y )
{
	ps4kbm.cursor_x = bound( 0, x, refState.width - 1 );
	ps4kbm.cursor_y = bound( 0, y, refState.height - 1 );
}

qboolean Platform_GetMouseGrab( void )
{
	return ps4kbm.grab;
}

void Platform_SetMouseGrab( qboolean enable )
{
	ps4kbm.grab = enable;
	ps4kbm.move_x = ps4kbm.move_y = 0.0f; // what was moved before is not for the game
}

qboolean Platform_MouseConnected( void )
{
	return ps4kbm.mouse_connected;
}

/*
==============
cursor

The engine leaves the cursor to the system (SDL asks the window system to show one),
the console has none, so it's drawn here after everything else. It's an arrow
whatever the type is, only "no cursor" is respected
==============
*/
static const char *ps4_cursor_art[] =
{
	"X           ",
	"XX          ",
	"X.X         ",
	"X..X        ",
	"X...X       ",
	"X....X      ",
	"X.....X     ",
	"X......X    ",
	"X.......X   ",
	"X........X  ",
	"X.....XXXXX ",
	"X..X..X     ",
	"X.X X..X    ",
	"XX  X..X    ",
	"X    X..X   ",
	"     X..X   ",
	"      X..X  ",
	"      X..X  ",
	"       XX   ",
};

#define PS4_CURSOR_ART_W   12
#define PS4_CURSOR_ART_H   ARRAYSIZE( ps4_cursor_art )
#define PS4_CURSOR_TEX_W   16
#define PS4_CURSOR_TEX_H   32

static struct
{
	VGUI_DefaultCursor type;
	int                texture;
} ps4cursor = { .type = dc_arrow };

void Platform_SetCursorType( VGUI_DefaultCursor type )
{
	ps4cursor.type = type;
}

static void PS4_CursorLoad( void )
{
	byte pixels[PS4_CURSOR_TEX_W * PS4_CURSOR_TEX_H * 4] = { 0 };

	for( size_t y = 0; y < PS4_CURSOR_ART_H; y++ )
	{
		for( size_t x = 0; x < PS4_CURSOR_ART_W; x++ )
		{
			const char c = ps4_cursor_art[y][x];
			byte *px = &pixels[( y * PS4_CURSOR_TEX_W + x ) * 4];

			if( c == 'X' )
			{
				px[0] = px[1] = px[2] = 0;
				px[3] = 255;
			}
			else if( c == '.' )
			{
				px[0] = px[1] = px[2] = px[3] = 255;
			}
		}
	}

	rgbdata_t pic = {
		.width = PS4_CURSOR_TEX_W,
		.height = PS4_CURSOR_TEX_H,
		.type = PF_RGBA_32,
		.size = sizeof( pixels ),
		.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA,
		.buffer = pixels,
	};

	ps4cursor.texture = GL_LoadTextureInternal( "*ps4_cursor", &pic, TF_IMAGE|TF_ALLOW_NEAREST );
	Con_Reportf( "PS4 cursor: texture %d\n", ps4cursor.texture );
}

void Platform_DrawCursor( void )
{
	static int last_reason = -1;
	const int reason = !ps4kbm.mouse_connected ? 1 : ps4kbm.grab ? 2 : ps4cursor.type == dc_none ? 3 : 0;

	// why it is not drawn, on every change
	if( reason != last_reason )
	{
		Con_Reportf( "PS4 cursor: %s\n", reason == 1 ? "no mouse yet" : reason == 2 ? "mouse is grabbed"
			: reason == 3 ? "hidden by the engine" : "drawn" );
		last_reason = reason;
	}

	if( reason )
		return;

	if( !ps4cursor.texture )
		PS4_CursorLoad( );

	if( !ps4cursor.texture )
		return;

	// 24 pixels tall on 1080p, the tip of the arrow is the hot spot
	const float scale = Q_max( 1.0f, refState.height / 1080.0f * 1.3f );
	const float w = PS4_CURSOR_ART_W * scale * 1.5f;
	const float h = PS4_CURSOR_ART_H * scale * 1.5f;

	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
	ref.dllFuncs.R_DrawStretchPic( ps4kbm.cursor_x, ps4kbm.cursor_y, w, h,
		0.0f, 0.0f, (float)PS4_CURSOR_ART_W / PS4_CURSOR_TEX_W, (float)PS4_CURSOR_ART_H / PS4_CURSOR_TEX_H,
		ps4cursor.texture );
}
