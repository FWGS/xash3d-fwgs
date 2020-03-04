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
#include "xash3d_types.h"
#include "crtlib.h"
#include "utils.h"

/*
============
IsFileExists
============
*/
qboolean IsFileExists( const char *filename )
{
	struct stat	st;
	int		ret;

	ret = stat( filename, &st );

	if( ret == -1 )
		return false;

	return true;
}

/*
============
GetFileSize     
============
*/
off_t GetFileSize( FILE *fp )
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
byte *LoadFile( const char *filename )
{
	FILE	*fp;
	byte	*buf;
	off_t	 size;

	fp = fopen( filename, "rb" );

	if( !fp )
		return NULL;
 
	size = GetFileSize( fp );

	buf = malloc( size );

	if( !buf )
		return NULL;

	fread( buf, size, 1, fp );
	fclose( fp );

	return buf;
}

