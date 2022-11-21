/*
dir.c - directory operations
Copyright (C) 2022 Alibek Omarov, Velaron

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#if XASH_POSIX
#include <unistd.h>
#endif
#include <errno.h>
#include <stddef.h>
#include "port.h"
#include "filesystem_internal.h"
#include "crtlib.h"
#include "xash3d_mathlib.h"
#include "common/com_strings.h"

void FS_Close_DIR( searchpath_t *search ) {}

void FS_PrintInfo_DIR( searchpath_t *search, char *dst, size_t size )
{
	Q_strncpy( dst, search->filename, size );
}

int FS_FindFile_DIR( searchpath_t *search, const char *path )
{
	char netpath[MAX_SYSPATH];

	Q_snprintf( netpath, sizeof( netpath ), "%s%s", search->filename, path );

	if( FS_SysFileExists( netpath, !( search->flags & FS_CUSTOM_PATH ) ) )
		return 0;

	return -1;
}

void FS_Search_DIR( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive )
{
	string netpath, temp;
	stringlist_t dirlist;
	const char *slash, *backslash, *colon, *separator;
	int basepathlength, dirlistindex, resultlistindex;
	char *basepath;

	slash = Q_strrchr( pattern, '/' );
	backslash = Q_strrchr( pattern, '\\' );
	colon = Q_strrchr( pattern, ':' );

	separator = Q_max( slash, backslash );
	separator = Q_max( separator, colon );
	
	basepathlength = separator ? (separator + 1 - pattern) : 0;
	basepath = Mem_Calloc( fs_mempool, basepathlength + 1 );
	if( basepathlength ) memcpy( basepath, pattern, basepathlength );
	basepath[basepathlength] = '\0';

	Q_snprintf( netpath, sizeof( netpath ), "%s%s", search->filename, basepath );

	stringlistinit( &dirlist );
	listdirectory( &dirlist, netpath, caseinsensitive );

	Q_strncpy( temp,  basepath, sizeof( temp ) );

	for( dirlistindex = 0; dirlistindex < dirlist.numstrings; dirlistindex++ )
	{
		Q_strncpy( &temp[basepathlength], dirlist.strings[dirlistindex], sizeof( temp ) - basepathlength );

		if( matchpattern( temp, (char *)pattern, true ) )
		{
			for( resultlistindex = 0; resultlistindex < list->numstrings; resultlistindex++ )
			{
				if( !Q_strcmp( list->strings[resultlistindex], temp ) )
					break;
			}

			if( resultlistindex == list->numstrings )
				stringlistappend( list, temp );
		}
	}

	stringlistfreecontents( &dirlist );

	Mem_Free( basepath );
}

int FS_FileTime_DIR( searchpath_t *search, const char *filename )
{
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", search->filename, filename );
	return FS_SysFileTime( path );
}

file_t *FS_OpenFile_DIR( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", search->filename, filename );
	return FS_SysOpen( path, mode );
}