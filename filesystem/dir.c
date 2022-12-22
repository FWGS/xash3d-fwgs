/*
dir.c - caseinsensitive directory operations
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

enum
{
	DIRENTRY_EMPTY_DIRECTORY = 0, // don't care if it's not directory or it's empty
	DIRENTRY_NOT_SCANNED = -1,
	DIRENTRY_CASEINSENSITIVE = -2, // directory is already caseinsensitive, just copy whatever is left
};

typedef struct dir_s
{
	string name;
	int numentries;
	struct dir_s *entries; // sorted
} dir_t;

static int FS_SortDir( const void *_a, const void *_b )
{
	const dir_t *a = _a;
	const dir_t *b = _b;
	return Q_stricmp( a->name, b->name );
}

static void FS_FreeDirEntries( dir_t *dir )
{
	if( dir->entries )
	{
		int i;
		for( i = 0; i < dir->numentries; i++ )
			FS_FreeDirEntries( &dir->entries[i] );
		dir->entries = NULL;
	}

	dir->numentries = DIRENTRY_NOT_SCANNED;
}

static void FS_InitDirEntries( dir_t *dir, const stringlist_t *list )
{
	int i;

	if( !list->numstrings )
	{
		dir->numentries = DIRENTRY_EMPTY_DIRECTORY;
		dir->entries = NULL;
		return;
	}

	dir->numentries = list->numstrings;
	dir->entries = Mem_Malloc( fs_mempool, sizeof( dir_t ) * dir->numentries );

	for( i = 0; i < list->numstrings; i++ )
	{
		dir_t *entry = &dir->entries[i];
		Q_strncpy( entry->name, list->strings[i], sizeof( entry->name ));
		entry->numentries = DIRENTRY_NOT_SCANNED;
		entry->entries = NULL;
	}

	qsort( dir->entries, dir->numentries, sizeof( dir->entries[0] ), FS_SortDir );
}

static void FS_PopulateDirEntries( dir_t *dir, const char *path )
{
#if XASH_WIN32 // Windows is always case insensitive
	dir->numentries = DIRENTRY_CASEINSENSITIVE;
	dir->entries = NULL;
#else
	stringlist_t list;

	if( !FS_SysFolderExists( path ))
	{
		dir->numentries = DIRENTRY_EMPTY_DIRECTORY;
		dir->entries = NULL;
		return;
	}

	stringlistinit( &list );
	listdirectory( &list, path, false );
	FS_InitDirEntries( dir, &list );
	stringlistfreecontents( &list );
#endif
}

static int FS_FindDirEntry( dir_t *dir, const char *name )
{
	int left, right;

	if( dir->numentries < 0 )
		return -1;

	// look for the file (binary search)
	left = 0;
	right = dir->numentries - 1;
	while( left <= right )
	{
		int   middle = (left + right) / 2;
		int	diff;

		diff = Q_stricmp( dir->entries[middle].name, name );

		// found it
		if( !diff )
			return middle;

		// if we're too far in the list
		if( diff > 0 )
			right = middle - 1;
		else left = middle + 1;
	}
	return -1;
}

static void FS_MergeDirEntries( dir_t *dir, const stringlist_t *list )
{
	int i;
	dir_t temp;

	FS_InitDirEntries( &temp, list );

	// copy all entries that has the same name and has subentries
	for( i = 0; i < dir->numentries; i++ )
	{
		int j;

		// don't care about directories without subentries
		if( dir->entries == NULL )
			continue;

		// try to find this directory in new tree
		j = FS_FindDirEntry( &temp, dir->entries[i].name );

		// not found, free memory
		if( j < 0 )
		{
			FS_FreeDirEntries( &dir->entries[i] );
			continue;
		}

		// found directory, move all entries
		temp.entries[j].numentries = dir->entries[i].numentries;
		temp.entries[j].entries = dir->entries[i].entries;
	}

	// now we can free old tree and replace it with temporary
	Mem_Free( dir->entries );
	dir->numentries = temp.numentries;
	dir->entries = temp.entries;
}

static int FS_MaybeUpdateDirEntries( dir_t *dir, const char *path, const char *entryname )
{
	stringlist_t list;
	qboolean update = false;
	int idx;

	stringlistinit( &list );
	listdirectory( &list, path, false );

	// find the reason to update entries list
	if( list.numstrings != dir->numentries )
	{
		// small optimization to not search string in the list
		// and directly go updating entries
		update = true;
	}
	else
	{
		for( idx = 0; idx < list.numstrings; idx++ )
		{
			if( !Q_stricmp( list.strings[idx], entryname ))
			{
				update = true;
				break;
			}
		}
	}

	if( !update )
	{
		stringlistfreecontents( &list );
		return -1;
	}

	FS_MergeDirEntries( dir, &list );
	stringlistfreecontents( &list );
	return FS_FindDirEntry( dir, entryname );
}

#if 1
qboolean FS_FixFileCase( dir_t *dir, const char *path, char *dst, size_t len, qboolean createpath )
{
	const char *prev = path;
	const char *next = Q_strchrnul( prev, PATH_SEPARATOR );
	size_t i = Q_strlen( dst ); // dst is expected to have searchpath filename

	while( true )
	{
		char entryname[MAX_SYSPATH];
		int ret;

		// this subdirectory is case insensitive, just slam everything that's left
		if( dir->numentries == DIRENTRY_CASEINSENSITIVE )
		{
			i += Q_strncpy( &dst[i], prev, len - i );
			if( i >= len )
			{
				Con_Printf( "%s: overflow while searching %s (caseinsensitive entry)\n", __FUNCTION__, path );
				return false;
			}
			break;
		}

		// populate cache if needed
		if( dir->numentries == DIRENTRY_NOT_SCANNED )
			FS_PopulateDirEntries( dir, dst );

		// get our entry name
		Q_strncpy( entryname, prev, next - prev + 1 );
		ret = FS_FindDirEntry( dir, entryname );

		// didn't found, but does it exists in FS?
		if( ret < 0 )
		{
			ret = FS_MaybeUpdateDirEntries( dir, dst, entryname );

			if( ret < 0 )
			{
				// if we're creating files or folders, we don't care if path doesn't exist
				// so copy everything that's left and exit without an error
				if( createpath )
				{
					i += Q_strncpy( &dst[i], prev, len - i );
					if( i >= len )
					{
						Con_Printf( "%s: overflow while searching %s (create path)\n", __FUNCTION__, path );
						return false;
					}

					return true;
				}
				return false;
			}
		}

		dir = &dir->entries[ret];
		ret = Q_strncpy( &dst[i], dir->name, len - i );

		// file not found, rescan...
		if( !FS_SysFileOrFolderExists( dst ))
		{
			// strip failed part
			dst[i] = 0;

			ret = FS_MaybeUpdateDirEntries( dir, dst, entryname );

			// file not found, exit... =/
			if( ret < 0 )
			{
				// if we're creating files or folders, we don't care if path doesn't exist
				// so copy everything that's left and exit without an error
				if( createpath )
				{
					i += Q_strncpy( &dst[i], prev, len - i );
					if( i >= len )
					{
						Con_Printf( "%s: overflow while searching %s (create path 2)\n", __FUNCTION__, path );
						return false;
					}

					return true;
				}
				return false;
			}

			dir = &dir->entries[ret];
			ret = Q_strncpy( &dst[i], dir->name, len - i );
		}

		i += ret;
		if( i >= len ) // overflow!
		{
			Con_Printf( "%s: overflow while searching %s (appending fixed file name)\n", __FUNCTION__, path );
			return false;
		}

		// end of string, found file, return
		if( next[0] == '\0' )
			break;

		// move pointer one character forward, find next path split character
		prev = next + 1;
		next = Q_strchrnul( prev, PATH_SEPARATOR );
		i += Q_strncpy( &dst[i], PATH_SEPARATOR_STR, len - i );
		if( i >= len ) // overflow!
		{
			Con_Printf( "%s: overflow while searching %s (path separator)\n", __FUNCTION__, path );
			return false;
		}
	}

	return true;
}
#else
qboolean FS_FixFileCase( dir_t *dir, const char *path, char *dst, size_t len, qboolean createpath )
{
	const char *prev = path;
	const char *next = Q_strchrnul( prev, PATH_SEPARATOR );
	size_t i = Q_strlen( dst ); // dst is expected to have searchpath filename

	while( true )
	{
		stringlist_t list;
		char entryname[MAX_SYSPATH];
		int idx;

		// get our entry name
		Q_strncpy( entryname, prev, next - prev + 1 );

		stringlistinit( &list );
		listdirectory( &list, dst, false );

		for( idx = 0; idx < list.numstrings; idx++ )
		{
			if( !Q_stricmp( list.strings[idx], entryname ))
				break;
		}

		if( idx != list.numstrings )
		{
			i += Q_strncpy( &dst[i], list.strings[idx], len - i );
			if( i >= len ) // overflow!
			{
				Con_Printf( "%s: overflow while searching %s (appending fixed file name)\n", __FUNCTION__, path );
				return false;
			}
		}
		else
		{
			stringlistfreecontents( &list );
			return false;
		}

		stringlistfreecontents( &list );

		// end of string, found file, return
		if( next[0] == '\0' )
			break;

		// move pointer one character forward, find next path split character
		prev = next + 1;
		next = Q_strchrnul( prev, PATH_SEPARATOR );
		i += Q_strncpy( &dst[i], PATH_SEPARATOR_STR, len - i );
		if( i >= len ) // overflow!
		{
			Con_Printf( "%s: overflow while searching %s (path separator)\n", __FUNCTION__, path );
			return false;
		}
	}

	return true;
}
#endif

static void FS_Close_DIR( searchpath_t *search )
{
	FS_FreeDirEntries( search->dir );
	Mem_Free( search->dir );
}

static void FS_PrintInfo_DIR( searchpath_t *search, char *dst, size_t size )
{
	Q_strncpy( dst, search->filename, size );
}

static int FS_FindFile_DIR( searchpath_t *search, const char *path, char *fixedname, size_t len )
{
	char netpath[MAX_SYSPATH];

	Q_strncpy( netpath, search->filename, sizeof( netpath ));
	if( !FS_FixFileCase( search->dir, path, netpath, sizeof( netpath ), false ))
		return -1;

	if( FS_SysFileExists( netpath, !FBitSet( search->flags, FS_CUSTOM_PATH )))
	{
		// return fixed case file name only local for that searchpath
		if( fixedname )
			Q_strncpy( fixedname, netpath + Q_strlen( search->filename ), len );
		return 0;
	}

	return -1;
}

static void FS_Search_DIR( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive )
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

static int FS_FileTime_DIR( searchpath_t *search, const char *filename )
{
	int time;
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", search->filename, filename );
	return FS_SysFileTime( path );
}

static file_t *FS_OpenFile_DIR( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", search->filename, filename );
	return FS_SysOpen( path, mode );
}

void FS_InitDirectorySearchpath( searchpath_t *search, const char *path, int flags )
{
	memset( search, 0, sizeof( searchpath_t ));

	Q_strncpy( search->filename, path, sizeof( search->filename ));
	search->type = SEARCHPATH_PLAIN;
	search->flags = flags;
	search->pfnPrintInfo = FS_PrintInfo_DIR;
	search->pfnClose = FS_Close_DIR;
	search->pfnOpenFile = FS_OpenFile_DIR;
	search->pfnFileTime = FS_FileTime_DIR;
	search->pfnFindFile = FS_FindFile_DIR;
	search->pfnSearch = FS_Search_DIR;

	// create cache root
	search->dir = Mem_Malloc( fs_mempool, sizeof( dir_t ));
	search->dir->name[0] = 0; // root has no filename, unused
	FS_PopulateDirEntries( search->dir, path );
}

searchpath_t *FS_AddDir_Fullpath( const char *path, qboolean *already_loaded, int flags )
{
	searchpath_t *search;

	for( search = fs_searchpaths; search; search = search->next )
	{
		if( search->type == SEARCHPATH_PLAIN && !Q_stricmp( search->filename, path ))
		{
			if( already_loaded )
				*already_loaded = true;
			return search;
		}
	}

	if( already_loaded )
		*already_loaded = false;

	search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ));
	FS_InitDirectorySearchpath( search, path, flags );

	search->next = fs_searchpaths;
	fs_searchpaths = search;

	Con_Printf( "Adding directory: %s\n", path );

	return search;
}
