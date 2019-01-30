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
#ifdef XASH_USE_EVDEV

#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <dirent.h>
#include "common.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"

#define MAX_EVDEV_DEVICES 5

struct evdev_s
{
	int initialized, devices;
	int fds[MAX_EVDEV_DEVICES];
	string paths[MAX_EVDEV_DEVICES];
	qboolean grab;
	float grabtime;
	float x, y;
} evdev;

int KeycodeFromEvdev(int keycode, int value);

static void Evdev_CheckPermissions()
{
#ifdef __ANDROID__
	system( "su 0 chmod 664 /dev/input/event*" );
#endif
}

void Evdev_Setup( void )
{
	if( evdev.initialized )
		return;
#ifdef __ANDROID__
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

		Q_snprintf( path, MAX_STRING, "/dev/input/%s", entry->d_name );

		for( i = 0; i < evdev.devices; i++ )
			if( !Q_strncmp( evdev.paths[i], path, MAX_STRING ) )
				goto next;

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
			Q_memset( codes, 0, sizeof( codes ) );
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
		Q_strncpy( evdev.paths[evdev.devices], path, MAX_STRING );
		evdev.fds[evdev.devices++] = fd;
		Msg( "Opened device %s\n", path );
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
		Msg( "Only %d devices supported!\n", MAX_EVDEV_DEVICES );
		return;
	}

	Evdev_Setup();

	Evdev_CheckPermissions(); // use root to grant access to evdev

	for( i = 0; i < evdev.devices; i++ )
	{
		if( !Q_strncmp( evdev.paths[i], path, MAX_STRING ) )
		{
			Msg( "device %s already open!\n", path );
			return;
		}
	}

	ret = open( path, O_RDONLY | O_NONBLOCK );
	if( ret < 0 )
	{
		Con_Reportf( S_ERROR  "Could not open input device %s: %s\n", path, strerror( errno ) );
		return;
	}
	Msg( "Input device #%d: %s opened sucessfully\n", evdev.devices, path );
	evdev.fds[evdev.devices] = ret;
	Q_strncpy( evdev.paths[evdev.devices++], path, MAX_STRING );
}

void Evdev_OpenDevice_f( void )
{
	if( Cmd_Argc() < 2 )
		Msg( S_USAGE "evdev_opendevice <path>\n" );

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
	char *arg;

	if( Cmd_Argc() < 2 )
		return;

	arg = Cmd_Argv( 1 );

	if( Q_isdigit( arg ) )
		i = Q_atoi( arg );
	else for( i = 0; i < evdev.devices; i++ )
		if( !Q_strncmp( evdev.paths[i], arg, MAX_STRING ) )
			break;

	if( i >= evdev.devices )
	{
		Msg( "Device %s is not open\n", arg );
		return;
	}

	close( evdev.fds[i] );
	evdev.devices--;
	Msg( "Device %s closed successfully\n", evdev.paths[i] );

	for( ; i < evdev.devices; i++ )
	{
		Q_strncpy( evdev.paths[i], evdev.paths[i+1], MAX_STRING );
		evdev.fds[i] = evdev.fds[i+1];
	}
}

void IN_EvdevFrame ()
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
					case REL_X: dx += ev.value;
					break;

					case REL_Y: dy += ev.value;
					break;

					case REL_WHEEL:
					if( ev.value > 0)
					{
						Key_Event( K_MWHEELDOWN, 1 );
						Key_Event( K_MWHEELDOWN, 0 );
					}
					else
					{
						Key_Event( K_MWHEELUP, 1 );
						Key_Event( K_MWHEELUP, 0 );
					}
					break;
				}
			}
			else if ( ( ev.type == EV_KEY ) && cls.key_dest == key_game )
			{
				switch( ev.code )
				{
				case BTN_LEFT:
					Key_Event( K_MOUSE1, ev.value );
					break;
				case BTN_MIDDLE:
					Key_Event( K_MOUSE3, ev.value );
					break;
				case BTN_RIGHT:
					Key_Event( K_MOUSE2, ev.value );
					break;
				default:
					Key_Event ( KeycodeFromEvdev( ev.code, ev.value ) , ev.value);
				}
			}
		}

		if( evdev.grab && evdev.grabtime <= host.realtime )
		{
			ioctl( evdev.fds[i], EVIOCGRAB, (void*) 1 );
			Key_ClearStates();
		}

		if( m_ignore->integer )
			continue;
		
		evdev.x += -dx * m_yaw->value;
		evdev.y += dy * m_pitch->value;
	}
	if( evdev.grabtime <= host.realtime )
		evdev.grab = false;
}

void Evdev_SetGrab( qboolean grab )
{
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
}

void IN_EvdevMove( float *yaw, float *pitch )
{
	*yaw += evdev.x;
	*pitch += evdev.y;
	evdev.x = evdev.y = 0.0f;
}

void Evdev_Init( void )
{
	Cmd_AddCommand ("evdev_open", Evdev_OpenDevice_f, "Open event device");
	Cmd_AddCommand ("evdev_close", Evdev_CloseDevice_f, "Close event device");
	Cmd_AddCommand ("evdev_autodetect", Evdev_Autodetect_f, "Automaticly open mouses and keyboards");
}

void Evdev_Shutdown( void )
{
	Cmd_RemoveCommand( "evdev_open" );
	Cmd_RemoveCommand( "evdev_close" );
	Cmd_RemoveCommand( "evdev_autodetect" );
}

#endif // XASH_USE_EVDEV
