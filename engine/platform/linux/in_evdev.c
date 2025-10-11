/*
in_evdev.c - linux evdev interface support
Copyright (C) 2015-2018 mittorn

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
#if XASH_USE_EVDEV


#include "common.h"
#include "input.h"
#include "client.h"
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <dirent.h>
#define MAX_EVDEV_DEVICES 5

struct evdev_s
{
	int initialized, devices;
	int fds[MAX_EVDEV_DEVICES];
	string paths[MAX_EVDEV_DEVICES];
	qboolean grab;
	double grabtime;
	float x, y;
	qboolean chars;
	qboolean shift;
} evdev;

static CVAR_DEFINE_AUTO( evdev_keydebug, "0", 0, "print key events to console" );

static int KeycodeFromEvdev(int keycode, int value)
{
	switch (keycode) {

	case KEY_0:          return '0';
	case KEY_1:          return '1';
	case KEY_2:          return '2';
	case KEY_3:          return '3';
	case KEY_4:          return '4';
	case KEY_5:          return '5';
	case KEY_6:          return '6';
	case KEY_7:          return '7';
	case KEY_8:          return '8';
	case KEY_9:          return '9';
	case KEY_BACKSPACE:  return K_BACKSPACE;
	case KEY_ENTER:      return K_ENTER;
	case KEY_ESC:        return K_ESCAPE;
	case KEY_KP0:        return K_KP_INS;
	case KEY_KP1:        return K_KP_END;
	case KEY_KP2:        return K_KP_DOWNARROW;
	case KEY_KP3:        return K_KP_PGDN;
	case KEY_KP4:        return K_KP_LEFTARROW;
	case KEY_KP5:        return K_KP_5;
	case KEY_KP6:        return K_KP_RIGHTARROW;
	case KEY_KP7:        return K_KP_HOME;
	case KEY_KP8:        return K_KP_UPARROW;
	case KEY_KP9:        return K_KP_PGUP;
	case KEY_KPDOT:      return K_KP_DEL;
	case KEY_KPENTER:    return K_KP_ENTER;
	case KEY_Q: return 'q';
	case KEY_W: return 'w';
	case KEY_E: return 'e';
	case KEY_R: return 'r';
	case KEY_T: return 't';
	case KEY_Y: return 'y';
	case KEY_U: return 'u';
	case KEY_I: return 'i';
	case KEY_O: return 'o';
	case KEY_P: return 'p';
	case KEY_A: return 'a';
	case KEY_S: return 's';
	case KEY_D: return 'd';
	case KEY_F: return 'f';
	case KEY_G: return 'g';
	case KEY_H: return 'h';
	case KEY_J: return 'j';
	case KEY_K: return 'k';
	case KEY_L: return 'l';
	case KEY_Z: return 'z';
	case KEY_X: return 'x';
	case KEY_C: return 'c';
	case KEY_V: return 'v';
	case KEY_B: return 'b';
	case KEY_N: return 'n';
	case KEY_M: return 'm';
	case KEY_LEFTBRACE: return '[';
	case KEY_RIGHTBRACE: return ']';
	case KEY_MINUS: return '-';
	case KEY_EQUAL: return '=';
	case KEY_TAB: return K_TAB;
	case KEY_SEMICOLON: return ';';
	case KEY_APOSTROPHE: return '\'';
	case KEY_GRAVE: return '`';
	case KEY_BACKSLASH: return '\\';
	case KEY_COMMA: return ',';
	case KEY_DOT: return '.';
	case KEY_SLASH: return '/';
	case KEY_SPACE: return K_SPACE;
	case KEY_KPASTERISK: return '*';
	case KEY_RIGHTCTRL:
	case KEY_LEFTCTRL:
		return K_CTRL;
	case KEY_RIGHTSHIFT:
	case KEY_LEFTSHIFT:
		return K_SHIFT;
	case KEY_LEFT: return K_LEFTARROW;
	case KEY_RIGHT: return K_RIGHTARROW;
	case KEY_UP: return K_UPARROW;
	case KEY_DOWN: return K_DOWNARROW;
	case BTN_LEFT: return K_MOUSE1;
	case BTN_RIGHT: return K_MOUSE2;
	case BTN_MIDDLE: return K_MOUSE3;
	case KEY_POWER:	return K_ESCAPE;
	case KEY_VOLUMEDOWN: return K_PGDN;
	case KEY_VOLUMEUP: return K_PGUP;
	case KEY_PLAYPAUSE: return K_ENTER;
	default:
		break;
	}

	return 0;
}
static void Evdev_CheckPermissions( void )
{
#if XASH_ANDROID
	system( "su 0 chmod 664 /dev/input/event*" );
#endif
}

void Evdev_Setup( void )
{
	if( evdev.initialized )
		return;
#if XASH_ANDROID
	system( "su 0 supolicy --live \"allow appdomain input_device dir { ioctl read getattr search open }\" \"allow appdomain input_device chr_file { ioctl read write getattr lock append open }\"" );
	system( "su 0 setenforce permissive" );
#endif
	evdev.initialized = true;
}

#define EV_HASBIT( x, y ) ( x[y / 32] & (1 << y % 32) )

void Evdev_Autodetect_f( void )
{
	int i;
	DIR *dir;
	struct dirent *entry;

	Evdev_Setup();

	Evdev_CheckPermissions();

	if( !( dir = opendir( "/dev/input" ) ) )
	    return;

	while( ( entry = readdir( dir ) ) )
	{
		int fd;
		string path;
		uint types[EV_MAX] = {0};
		uint codes[( KEY_MAX - 1 ) / 32 + 1] = {0};
		qboolean hasbtnmouse;

		if( evdev.devices >= MAX_EVDEV_DEVICES )
			continue;

		Q_snprintf( path, sizeof( path ), "/dev/input/%s", entry->d_name );

		for( i = 0; i < evdev.devices; i++ )
		{
			if( !Q_strncmp( evdev.paths[i], path, sizeof( evdev.paths[i] )))
				goto next;
		}

		if( Q_strncmp( entry->d_name, "event", 5 ) )
			continue;

		fd = open( path, O_RDONLY | O_NONBLOCK );

		if( fd == -1 )
			continue;

		ioctl( fd, EVIOCGBIT( 0, EV_MAX ), types );

		if( !EV_HASBIT( types, EV_KEY ) )
			goto close;

		ioctl( fd, EVIOCGBIT( EV_KEY, KEY_MAX ), codes );

		if( EV_HASBIT( codes, KEY_LEFTCTRL ) && EV_HASBIT( codes, KEY_SPACE ) )
			goto open;

		if( !EV_HASBIT( codes, BTN_MOUSE ) )
			goto close;

		if( EV_HASBIT( types, EV_REL ) )
		{
			memset( codes, 0, sizeof( codes ) );
			ioctl( fd, EVIOCGBIT( EV_REL, KEY_MAX ), codes );

			if( !EV_HASBIT( codes, REL_X ) )
				goto close;

			if( !EV_HASBIT( codes, REL_Y ) )
				goto close;

			if( !EV_HASBIT( codes, REL_WHEEL ) )
				goto close;

			goto open;
		}
		goto close;
open:
		Q_strncpy( evdev.paths[evdev.devices], path, sizeof( evdev.paths[0] ));
		evdev.fds[evdev.devices++] = fd;
		Con_Printf( "Opened device %s\n", path );
#if XASH_INPUT == INPUT_EVDEV
		if( Sys_CheckParm( "-grab" ) )
			ioctl( evdev.fds[i], EVIOCGRAB, (void*) 1 );
#endif
		goto next;
close:
		close( fd );
next:
		continue;
	}
	closedir( dir );

}

/*
===========
Evdev_OpenDevice

For shitty systems that cannot provide relative mouse axes
===========
*/
void Evdev_OpenDevice ( const char *path )
{
	int ret, i;

	if ( evdev.devices >= MAX_EVDEV_DEVICES )
	{
		Con_Printf( "Only %d devices supported!\n", MAX_EVDEV_DEVICES );
		return;
	}

	Evdev_Setup();

	Evdev_CheckPermissions(); // use root to grant access to evdev

	for( i = 0; i < evdev.devices; i++ )
	{
		if( !Q_strncmp( evdev.paths[i], path, sizeof( evdev.paths[i] )))
		{
			Con_Printf( "device %s already open!\n", path );
			return;
		}
	}

	ret = open( path, O_RDONLY | O_NONBLOCK );
	if( ret < 0 )
	{
		Con_Reportf( S_ERROR "Could not open input device %s: %s\n", path, strerror( errno ) );
		return;
	}
	Con_Printf( "Input device #%d: %s opened sucessfully\n", evdev.devices, path );
	evdev.fds[evdev.devices] = ret;
	Q_strncpy( evdev.paths[evdev.devices++], path, sizeof( evdev.paths[0] ));

#if XASH_INPUT == INPUT_EVDEV
		if( Sys_CheckParm( "-grab" ) )
			ioctl( evdev.fds[i], EVIOCGRAB, (void*) 1 );
#endif
}

void Evdev_OpenDevice_f( void )
{
	if( Cmd_Argc() < 2 )
		Con_Printf( S_USAGE "evdev_opendevice <path>\n" );

	Evdev_OpenDevice( Cmd_Argv( 1 ) );
}

/*
===========
Evdev_CloseDevice_f
===========
*/
void Evdev_CloseDevice_f ( void )
{
	uint i;
	const char *arg;

	if( Cmd_Argc() < 2 )
		return;

	arg = Cmd_Argv( 1 );

	if( Q_isdigit( arg ) )
		i = Q_atoi( arg );
	else for( i = 0; i < evdev.devices; i++ )
		if( !Q_strncmp( evdev.paths[i], arg, sizeof( evdev.paths[i] )))
			break;

	if( i >= evdev.devices )
	{
		Con_Printf( "Device %s is not open\n", arg );
		return;
	}

	close( evdev.fds[i] );
	evdev.devices--;
	Con_Printf( "Device %s closed successfully\n", evdev.paths[i] );

	for( ; i < evdev.devices; i++ )
	{
		Q_strncpy( evdev.paths[i], evdev.paths[i+1], sizeof( evdev.paths[i] ));
		evdev.fds[i] = evdev.fds[i+1];
	}
}

void IN_EvdevFrame ( void )
{
	int dx = 0, dy = 0, i;

	for( i = 0; i < evdev.devices; i++ )
	{
		struct input_event ev;

		while ( read( evdev.fds[i], &ev, 16) == 16 )
		{
			if ( ev.type == EV_REL )
			{
				switch ( ev.code )
				{
				case REL_X:
					dx += ev.value;
					break;

				case REL_Y:
					dy += ev.value;
					break;

				case REL_WHEEL:
					IN_MWheelEvent( ev.value );
					break;
				}
			}
			else if ( ( ev.type == EV_KEY ) && (cls.key_dest == key_game || XASH_INPUT == INPUT_EVDEV ) )
			{
				int key = KeycodeFromEvdev( ev.code, ev.value );

				if( evdev_keydebug.value )
					Con_Printf( "key %d %d %d\n", ev.code, key, ev.value );

				Key_Event( key , ev.value );

				if( evdev.chars && ev.value )
				{
					if( key >= 32 && key < 127 )
					{
						if( evdev.shift )
						{
							key = Key_ToUpper( key );
						}
						CL_CharEvent( key );
					}
				}
				if( key == K_SHIFT )
					evdev.shift = ev.value;
			}
		}

		if( evdev.grab && evdev.grabtime <= host.realtime )
		{
			ioctl( evdev.fds[i], EVIOCGRAB, (void*) 1 );
			Key_ClearStates();
		}

		if( m_ignore.value )
			continue;

		evdev.x += -dx * m_yaw.value;
		evdev.y += dy * m_pitch.value;
	}
	if( evdev.grabtime <= host.realtime )
		evdev.grab = false;
}

void Evdev_SetGrab( qboolean grab )
{
	// grab only if evdev is secondary input source
#if XASH_INPUT != INPUT_EVDEV
	int i;

	if( grab )
	{
		Key_Event( K_ESCAPE, 0 ); //Do not leave ESC down
		evdev.grabtime = host.realtime + 0.5;
		Key_ClearStates();
	}
	else
	{
		for( i = 0; i < evdev.devices; i++ )
			ioctl( evdev.fds[i], EVIOCGRAB, (void*) 0 );
	}
	evdev.grab = grab;
#endif
}

void IN_EvdevMove( float *yaw, float *pitch )
{
	*yaw += evdev.x;
	*pitch += evdev.y;
	evdev.x = evdev.y = 0.0f;
}

#if XASH_INPUT == INPUT_EVDEV

void Platform_EnableTextInput( qboolean enable )
{
	evdev.chars = enable;
	evdev.shift = false;
}

void Platfrom_MouseMove( float *yaw, float *pitch )
{
	// already catched in IN_EvdevMove
}

#endif

void Evdev_Init( void )
{
	Cmd_AddRestrictedCommand ("evdev_open", Evdev_OpenDevice_f, "Open event device");
	Cmd_AddRestrictedCommand ("evdev_close", Evdev_CloseDevice_f, "Close event device");
	Cmd_AddRestrictedCommand ("evdev_autodetect", Evdev_Autodetect_f, "Automaticly open mouses and keyboards");
#if XASH_INPUT == INPUT_EVDEV
	Evdev_Autodetect_f();
#endif
}

void Evdev_Shutdown( void )
{
	int i;

	Cmd_RemoveCommand( "evdev_open" );
	Cmd_RemoveCommand( "evdev_close" );
	Cmd_RemoveCommand( "evdev_autodetect" );
	Cvar_RegisterVariable( &evdev_keydebug );

	for( i = 0; i < evdev.devices; i++ )
	{
		ioctl( evdev.fds[i], EVIOCGRAB, (void*) 0 );
		close( evdev.fds[i] );
		evdev.fds[i] = -1;
	}
	evdev.devices = 0;
}

#endif // XASH_USE_EVDEV
