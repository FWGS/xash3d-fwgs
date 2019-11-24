/*
sys_win.c - posix system utils
Copyright (C) 2019 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <unistd.h> // fork
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "platform/platform.h"
#include "menu_int.h"

static qboolean Sys_FindExecutable( const char *baseName, char *buf, size_t size )
{
	char *envPath;
	char *part;
	size_t length;
	size_t baseNameLength;
	size_t needTrailingSlash;

	if( !baseName || !baseName[0] )
		return false;

	envPath = getenv( "PATH" );
	if( !envPath )
		return false;

	baseNameLength = Q_strlen( baseName );
	while( *envPath )
	{
		part = Q_strchr( envPath, ':' );
		if( part )
			length = part - envPath;
		else
			length = Q_strlen( envPath );

		if( length > 0 )
		{
			needTrailingSlash = ( envPath[length - 1] == '/' ) ? 0 : 1;
			if( length + baseNameLength + needTrailingSlash < size )
			{
				Q_strncpy( buf, envPath, length + 1 );
				if( needTrailingSlash )
					Q_strcpy( buf + length, "/" );
				Q_strcpy( buf + length + needTrailingSlash, baseName );
				buf[length + needTrailingSlash + baseNameLength] = '\0';
				if( access( buf, X_OK ) == 0 )
					return true;
			}
		}

		envPath += length;
		if( *envPath == ':' )
			envPath++;
	}
	return false;
}

#if !XASH_ANDROID
void Platform_ShellExecute( const char *path, const char *parms )
{
	char xdgOpen[128];

	if( !Q_strcmp( path, GENERIC_UPDATE_PAGE ) || !Q_strcmp( path, PLATFORM_UPDATE_PAGE ))
		path = DEFAULT_UPDATE_PAGE;

	if( Sys_FindExecutable( OPEN_COMMAND, xdgOpen, sizeof( xdgOpen ) ) )
	{
		const char *argv[] = { xdgOpen, path, NULL };
		pid_t id = fork( );
		if( id == 0 )
		{
			execv( xdgOpen, (char **)argv );
			fprintf( stderr, "error opening %s %s", xdgOpen, path );
			_exit( 1 );
		}
	}
	else
	{
		Con_Reportf( S_WARN "Could not find "OPEN_COMMAND" utility\n" );
	}
}
#endif // XASH_ANDROID

#if XASH_MESSAGEBOX == MSGBOX_STDERR
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	fprintf( stderr,
		 "======================================\n"
		 "%s: %s\n"
		 "======================================\n", title, message );
}
#endif
