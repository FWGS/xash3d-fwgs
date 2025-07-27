/*
utils.c - Useful helper functions
Copyright (C) 2020 Andrey Akhmichin

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "xash3d_types.h"
#include "port.h"
#include "crtlib.h"
#include "settings.h"
#include "utils.h"

/*
============
MakeDirectory
============
*/
qboolean MakeDirectory( const char *path )
{
	if( -1 == _mkdir( path ))
	{
		if( errno == EEXIST )
		{
			// TODO: when filesystem library will be ready
			// use FS_SysFolderExists here or replace this whole function
			// with FS_CreatePath
#if XASH_WIN32
		        DWORD   dwFlags = GetFileAttributes( path );
		        return ( dwFlags != -1 ) && ( dwFlags & FILE_ATTRIBUTE_DIRECTORY );
#else
		        struct stat buf;

		        if( !stat( path, &buf ))
				return S_ISDIR( buf.st_mode );
#endif
		}
		return false;
	}

	return true;
}

/*
============
MakeFullPath
============
*/
qboolean MakeFullPath( const char *path )
{
	char *p = (char *)path, tmp;

	if( *p == '/' )
		p++;

	for( ; *p; )
	{
		p = Q_strpbrk( p, "/\\" );

		if( p )
		{
			tmp = *p;
			*p = '\0';
		}

		if( !MakeDirectory( path ))
		{
			LogPrintf( "ERROR: Couldn't create directory %s.", path );
			return false;
		}

		if( !p )
			break;

		*p++ = tmp;
	}

	return true;
}

/*
============
ExtractFileName
============
*/
void ExtractFileName( char *name, size_t size )
{
	char	tmp[MAX_SYSPATH];

	if( !( name && *name ) || size <= 0 )
		return;

	name[size - 1] = '\0';

	if( Q_strpbrk( name, "/\\" ))
	{
		Q_strncpy( tmp, COM_FileWithoutPath( name ), sizeof( tmp ));
		Q_strncpy( name, tmp, size );
	}
}

/*
============
GetFileSize
============
*/
off_t GetSizeOfFile( FILE *fp )
{
	struct stat	st;
	int		fd;

	fd = fileno( fp );
	fstat( fd, &st );

	return st.st_size;
}

/*
============
LoadFile
============
*/
byte *LoadFile( const char *filename, off_t *size )
{
	FILE	*fp;
	byte	*buf;

	fp = fopen( filename, "rb" );

	if( !fp )
		return NULL;

	*size = GetSizeOfFile( fp );

	buf = malloc( *size );

	if( !buf )
		return NULL;

	fread( buf, *size, 1, fp );
	fclose( fp );

	return buf;
}

/*
============
LogPutS
============
*/
void LogPutS( const char *str )
{
	if( ( globalsettings & SETTINGS_NOLOGS ))
		return;

	if( Q_strncmp( str, "ERROR:", sizeof( "ERROR:" ) - 1 ))
		puts( str );
	else fprintf( stderr, "%s\n", str );
}

/*
============
LogPrintf
============
*/
void LogPrintf( const char *szFmt, ... )
{
	va_list args;
	static char buffer[2048];

	if( ( globalsettings & SETTINGS_NOLOGS ))
		return;

	va_start( args, szFmt );
        Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	if( Q_strncmp( buffer, "ERROR:", sizeof( "ERROR:" ) - 1 ))
		puts( buffer );
	else fprintf( stderr, "%s\n", buffer );
}

