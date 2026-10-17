/*
in_ps4.c - PlayStation 4 native gamepad input
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
OpenOrbis SDL2 joystick driver doesn't report sticks returning to center
and drops one of the triggers, so DualShock 4 is read directly through libScePad
*/

#include "platform/platform.h"
#include "input.h"
#include "keydefs.h"
#include "client.h"
#include "vgui_draw.h"
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/CommonDialog.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>

#ifndef ORBIS_PAD_PORT_TYPE_STANDARD
#define ORBIS_PAD_PORT_TYPE_STANDARD 0
#endif

static const struct
{
	uint32_t mask;
	int key;
} ps4_buttons[] =
{
{ ORBIS_PAD_BUTTON_CROSS,     K_A_BUTTON },
{ ORBIS_PAD_BUTTON_CIRCLE,    K_B_BUTTON },
{ ORBIS_PAD_BUTTON_SQUARE,    K_X_BUTTON },
{ ORBIS_PAD_BUTTON_TRIANGLE,  K_Y_BUTTON },
{ ORBIS_PAD_BUTTON_L1,        K_L1_BUTTON },
{ ORBIS_PAD_BUTTON_R1,        K_R1_BUTTON },
{ ORBIS_PAD_BUTTON_L3,        K_LSTICK },
{ ORBIS_PAD_BUTTON_R3,        K_RSTICK },
{ ORBIS_PAD_BUTTON_OPTIONS,   K_START_BUTTON },
{ ORBIS_PAD_BUTTON_TOUCH_PAD, K_BACK_BUTTON },
{ ORBIS_PAD_BUTTON_UP,        K_DPAD_UP },
{ ORBIS_PAD_BUTTON_DOWN,      K_DPAD_DOWN },
{ ORBIS_PAD_BUTTON_LEFT,      K_DPAD_LEFT },
{ ORBIS_PAD_BUTTON_RIGHT,     K_DPAD_RIGHT },
};

static struct
{
	int32_t  handle;
	uint32_t buttons;
	short    axes[MAX_AXES];
	float    vibrate_until;
	qboolean vibrating;
} ps4pad = { -1 };

/*
==============================================================================

System keyboard

The chat line opens the system keyboard dialog together with itself, in the console
and after cancelling Cross opens it. Menu fields ask
for it with pfnSetTextInputBuffer and pfnEnableTextInput: text input is enabled
by the menu as soon as a field gets focus, the keyboard must open only when the
user activates the field.
The dialog starts with the text being edited and replaces it.
The dialog works with UTF-16, our wchar_t is 32 bits, so buffers are uint16_t.

==============================================================================
*/
#define IME_MAX_TEXT 255

static struct
{
	qboolean wanted;       // engine has text input enabled
	qboolean open;         // dialog is running
	qboolean initialized;  // module loaded, common dialog initialized
	qboolean seen_running; // dialog reported running at least once
	qboolean requested;    // the menu asked for the dialog, opened on next input update
	qboolean chat_opened;  // chat line was just opened, the dialog opens with it
	char request_text[IME_MAX_TEXT * 4 + 1];
	uint16_t text[IME_MAX_TEXT + 1];
} ps4ime;

static CVAR_DEFINE_AUTO( ime_use_console_language, "1", FCVAR_ARCHIVE, "system keyboard has the language of the console first and English after it, 0 - English only" );

// SceImeLanguage
#define IME_LANGUAGE_ENGLISH_US     0x00000004ULL

#define IME_OPTION_LANGUAGES_FORCED 0x00000008

static void PS4_SendAxis( engineAxis_t axis, short value );
static void PS4_IME_FromUtf8( uint16_t *out, size_t size, const char *in );

static qboolean PS4_IME_Init( void )
{
	int ret;

	if( ps4ime.initialized )
		return true;

	if(( ret = sceSysmoduleLoadModule( ORBIS_SYSMODULE_IME_DIALOG )) < 0 )
	{
		Con_Printf( S_ERROR "IME dialog module: 0x%08x\n", ret );
		return false;
	}

	// fails if somebody initialized it already, that's fine
	sceCommonDialogInitialize();
	ps4ime.initialized = true;
	return true;
}

static void PS4_IME_Open( const char *text )
{
	static const uint16_t title[] = { 'X', 'a', 's', 'h', '3', 'D', 0 };
	OrbisImeDialogSetting param;
	int32_t user = -1;
	int ret;

	if( !PS4_IME_Init( ))
		return;

	sceUserServiceGetInitialUser( &user );
	PS4_IME_FromUtf8( ps4ime.text, IME_MAX_TEXT + 1, text );
	memset( &param, 0, sizeof( param ));
	param.userId = user;
	param.type = ORBIS_TYPE_DEFAULT;
	// without a list the keyboard has the language of the console first and English
	// after it, with the list it has only the listed languages
	if( !ime_use_console_language.value )
	{
		param.supportedLanguages = IME_LANGUAGE_ENGLISH_US;
		param.option = IME_OPTION_LANGUAGES_FORCED;
	}
	param.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
	param.maxTextLength = IME_MAX_TEXT;
	param.inputTextBuffer = (wchar_t *)ps4ime.text;
	param.posx = 1920.0f / 2.0f;
	param.posy = 1080.0f / 2.0f;
	param.horizontalAlignment = ORBIS_H_CENTER;
	param.verticalAlignment = ORBIS_V_CENTER;
	param.title = (const wchar_t *)title;

	if(( ret = sceImeDialogInit( &param, NULL )) < 0 )
	{
		Con_Printf( S_ERROR "sceImeDialogInit failed: 0x%08x\n", ret );
		return;
	}

	ps4ime.open = true;
	ps4ime.seen_running = false;

	// nothing reaches the game while the dialog takes the gamepad: held buttons
	// are released now, buttons still held when it closes don't press again
	for( size_t i = 0; i < ARRAYSIZE( ps4_buttons ); i++ )
	{
		if( FBitSet( ps4pad.buttons, ps4_buttons[i].mask ))
			Key_Event( ps4_buttons[i].key, false );
	}
	PS4_SendAxis( JOY_AXIS_SIDE, 0 );
	PS4_SendAxis( JOY_AXIS_FWD, 0 );
	PS4_SendAxis( JOY_AXIS_YAW, 0 );
	PS4_SendAxis( JOY_AXIS_PITCH, 0 );
	PS4_SendAxis( JOY_AXIS_LT, 0 );
	PS4_SendAxis( JOY_AXIS_RT, 0 );
}

static void PS4_IME_FromUtf8( uint16_t *out, size_t size, const char *in )
{
	const byte *p = (const byte *)in;
	size_t len = 0;

	while( p && *p && len + 2 < size )
	{
		uint32_t c = *p++;
		int extra = 0;

		if( c >= 0xf0 ) { c &= 0x07; extra = 3; }
		else if( c >= 0xe0 ) { c &= 0x0f; extra = 2; }
		else if( c >= 0xc0 ) { c &= 0x1f; extra = 1; }
		else if( c >= 0x80 ) continue; // stray continuation byte

		while( extra-- > 0 && ( *p & 0xc0 ) == 0x80 )
			c = ( c << 6 ) | ( *p++ & 0x3f );

		if( c >= 0x10000 )
		{
			c -= 0x10000;
			out[len++] = 0xd800 + ( c >> 10 );
			out[len++] = 0xdc00 + ( c & 0x3ff );
		}
		else out[len++] = c;
	}

	out[len] = 0;
}

static size_t PS4_IME_ToUtf8( char *out, size_t size, const uint16_t *in )
{
	size_t len = 0;

	while( *in && len + 5 < size )
	{
		uint32_t c = *in++;

		if( c >= 0xd800 && c < 0xdc00 && *in >= 0xdc00 && *in < 0xe000 )
			c = 0x10000 + (( c - 0xd800 ) << 10 ) + ( *in++ - 0xdc00 );

		if( c < 0x80 )
			out[len++] = c;
		else if( c < 0x800 )
		{
			out[len++] = 0xc0 | ( c >> 6 );
			out[len++] = 0x80 | ( c & 0x3f );
		}
		else if( c < 0x10000 )
		{
			out[len++] = 0xe0 | ( c >> 12 );
			out[len++] = 0x80 | (( c >> 6 ) & 0x3f );
			out[len++] = 0x80 | ( c & 0x3f );
		}
		else
		{
			out[len++] = 0xf0 | ( c >> 18 );
			out[len++] = 0x80 | (( c >> 12 ) & 0x3f );
			out[len++] = 0x80 | (( c >> 6 ) & 0x3f );
			out[len++] = 0x80 | ( c & 0x3f );
		}
	}

	out[len] = 0;
	return len;
}

static void PS4_IME_KeyPress( int key )
{
	Key_Event( key, true );
	Key_Event( key, false );
}

// typed text replaces the edited one: Ctrl+C (char 3) clears console, chat and
// menu fields, then the text goes in like text input events of SDL; console and
// chat send it right away
static void PS4_IME_Deliver( void )
{
	char text[IME_MAX_TEXT * 4 + 1];
	const qboolean menu = cls.key_dest == key_menu;
	const char *p;

	PS4_IME_ToUtf8( text, sizeof( text ), ps4ime.text );

	CL_CharEvent( 'c' - 'a' + 1 );

	VGui_ReportTextInput( text );

	for( p = text; *p; p++ )
	{
		int ch = (byte)*p;

		if( !cls.accept_utf8 )
			ch = Con_UtfProcessCharForce( ch );

		if( ch )
			CL_CharEvent( ch );
	}

	if( !menu && text[0] )
		PS4_IME_KeyPress( K_ENTER );
}

static void PS4_IME_Update( void )
{
	OrbisDialogResult result;
	OrbisDialogStatus status = sceImeDialogGetStatus();

	// dialog may report "none" for a while after it's created and a stale state of
	// the previous one, it's done only when it finishes after running
	if( status == ORBIS_DIALOG_STATUS_RUNNING )
		ps4ime.seen_running = true;
	if( status != ORBIS_DIALOG_STATUS_STOPPED || !ps4ime.seen_running )
		return;

	memset( &result, 0, sizeof( result ));
	sceImeDialogGetResult( &result );
	sceImeDialogTerm();
	ps4ime.open = false;

	Con_Reportf( "IME dialog closed, result %d\n", result.endstatus );
	if( result.endstatus == ORBIS_DIALOG_OK )
		PS4_IME_Deliver();
}

void Platform_EnableTextInput( qboolean enable, int x, int y, int w, int h )
{
	const char *text = Key_GetTextInputBuffer();
	const qboolean was_wanted = ps4ime.wanted;

	ps4ime.wanted = enable;

	// called from the key event of the menu, the dialog is opened outside of it
	if( !enable )
	{
		ps4ime.requested = false;
		ps4ime.chat_opened = false;
	}
	else if( !was_wanted && cls.key_dest == key_message )
	{
		// the chat line has nothing to look at without the keyboard, it opens with it,
		// the console doesn't: it's open during loading and the keyboard would cover it
		ps4ime.chat_opened = true;
	}
	else if( text && !ps4ime.open )
	{
		ps4ime.requested = true;
		Q_strncpy( ps4ime.request_text, text, sizeof( ps4ime.request_text ));
	}
}

static short PS4_StickToAxis( uint8_t value )
{
	// 0..255 to -32768..32767, 128 is center
	int v = ((int)value - 128 ) * 256;

	return (short)bound( -32768, v, 32767 );
}

static short PS4_TriggerToAxis( uint8_t value )
{
	return (short)( value * 32767 / 255 );
}

static void PS4_SendAxis( engineAxis_t axis, short value )
{
	if( ps4pad.axes[axis] == value )
		return;

	ps4pad.axes[axis] = value;
	Joy_AxisMotionEvent( axis, value );
}

static void PS4_StopVibration( void )
{
	OrbisPadVibeParam vibe = { 0 };

	if( ps4pad.handle >= 0 && ps4pad.vibrating )
		scePadSetVibration( ps4pad.handle, &vibe );

	ps4pad.vibrating = false;
}

void PS4_InputUpdate( void )
{
	OrbisPadData data;

	if( ps4pad.handle < 0 )
		return;

	if( ps4pad.vibrating && host.realtime > ps4pad.vibrate_until )
		PS4_StopVibration( );

	memset( &data, 0, sizeof( data ));

	if( scePadReadState( ps4pad.handle, &data ) < 0 || !data.connected )
		return;

	if( ps4ime.requested )
	{
		ps4ime.requested = false;
		PS4_IME_Open( ps4ime.request_text );
	}

	if( ps4ime.chat_opened )
	{
		ps4ime.chat_opened = false;
		if( cls.key_dest == key_message && !ps4ime.open )
			PS4_IME_Open( Con_GetEditText( ));
	}

	// the system keyboard has the gamepad, state is only tracked
	if( ps4ime.open )
	{
		PS4_IME_Update();
		ps4pad.buttons = data.buttons;
		return;
	}

	uint32_t changed = data.buttons ^ ps4pad.buttons;

	// Cross opens the system keyboard in the console and chat again, after it was cancelled
	if( FBitSet( changed & data.buttons, ORBIS_PAD_BUTTON_CROSS ) && ps4ime.wanted && host.textmode
		&& ( cls.key_dest == key_console || cls.key_dest == key_message ))
	{
		PS4_IME_Open( Con_GetEditText( ));
		ps4pad.buttons = data.buttons;
		return;
	}

	for( size_t i = 0; changed && i < ARRAYSIZE( ps4_buttons ); i++ )
	{
		if( FBitSet( changed, ps4_buttons[i].mask ))
			Key_Event( ps4_buttons[i].key, FBitSet( data.buttons, ps4_buttons[i].mask ) ? true : false );
	}

	ps4pad.buttons = data.buttons;

	// same layout as SDL GameController code uses
	PS4_SendAxis( JOY_AXIS_SIDE, PS4_StickToAxis( data.leftStick.x ));
	PS4_SendAxis( JOY_AXIS_FWD, PS4_StickToAxis( data.leftStick.y ));
	PS4_SendAxis( JOY_AXIS_YAW, PS4_StickToAxis( data.rightStick.x ));
	PS4_SendAxis( JOY_AXIS_PITCH, PS4_StickToAxis( data.rightStick.y ));
	PS4_SendAxis( JOY_AXIS_LT, PS4_TriggerToAxis( data.analogButtons.l2 ));
	PS4_SendAxis( JOY_AXIS_RT, PS4_TriggerToAxis( data.analogButtons.r2 ));
}

int Platform_JoyInit( void )
{
	int32_t user = -1;
	int ret;

	if( ps4pad.handle >= 0 )
		return 1;

	Con_Reportf( "Joystick: PS4 libScePad\n" );

	if( !Cvar_FindVar( ime_use_console_language.name ))
		Cvar_RegisterVariable( &ime_use_console_language );

	if(( ret = scePadInit( )) < 0 )
	{
		Con_Printf( S_ERROR "scePadInit failed: 0x%08x\n", ret );
		return 0;
	}

	if(( ret = sceUserServiceGetInitialUser( &user )) < 0 )
	{
		Con_Printf( S_ERROR "sceUserServiceGetInitialUser failed: 0x%08x\n", ret );
		return 0;
	}

	ps4pad.handle = scePadOpen( user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL );

	// might be already opened by somebody else
	if( ps4pad.handle < 0 )
		ps4pad.handle = scePadGetHandle( user, ORBIS_PAD_PORT_TYPE_STANDARD, 0 );

	if( ps4pad.handle < 0 )
	{
		Con_Printf( S_ERROR "scePadOpen failed: 0x%08x\n", ps4pad.handle );
		return 0;
	}

	ps4pad.buttons = 0;
	memset( ps4pad.axes, 0, sizeof( ps4pad.axes ));

	return 1;
}

void Platform_JoyShutdown( void )
{
	PS4_StopVibration( );

	if( ps4pad.handle >= 0 )
		scePadClose( ps4pad.handle );

	ps4pad.handle = -1;
}

void Platform_CalibrateGamepadGyro( void )
{
}

void Platform_Vibrate2( float time, int val1, int val2, uint flags )
{
	OrbisPadVibeParam vibe;

	if( ps4pad.handle < 0 )
		return;

	if( val1 < 0 )
		val1 = 0xFFFF;
	if( val2 < 0 )
		val2 = 0xFFFF;

	vibe.lgMotor = (uint8_t)( bound( 0, val1, 0xFFFF ) >> 8 );
	vibe.smMotor = (uint8_t)( bound( 0, val2, 0xFFFF ) >> 8 );

	if( scePadSetVibration( ps4pad.handle, &vibe ) >= 0 )
	{
		ps4pad.vibrating = true;
		ps4pad.vibrate_until = host.realtime + time;
	}
}

void Platform_Vibrate( float time, char flags )
{
	Platform_Vibrate2( time, -1, -1, flags );
}
