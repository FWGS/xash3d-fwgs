/*
sys_ogc.c - misc wii stubs
Copyright (C) 2026 mintferret

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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <fat.h>
#include <SDL.h>
#include <gccore.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
#include <ogc/usbmouse.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BASE_PATH "/xash3d"

// Let MEM2 handle the heap
u32 MALLOC_MEM2 = 1;

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
}

void OGC_Init( void )
{
	WPAD_Init( );

	// Needs to be enabled to disable it
	KEYBOARD_Init(NULL);
	KEYBOARD_Deinit();

	MOUSE_Init( );
	printf( "%s\n", __func__ );
}

void OGC_Shutdown( void )
{
	printf( "%s\n", __func__ );
}

// Check if there's a xash folder inside each directory, sd goes first
qboolean OGC_GetBasePath( char *buf, const size_t buflen )
{
	static const char *drives[] = { "sd", "usb" };
	for ( size_t i = 0; i < sizeof( drives ) / sizeof( *drives ); i++ )
	{
		Q_snprintf( buf, buflen, "%s:" BASE_PATH, drives[i] );
		DIR* dir = opendir( buf );
		if ( dir )
		{
			closedir( dir );
			return true;
		}
	}
	return false;
}
