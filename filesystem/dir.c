/*
dir.c - caseinsensitive directory operations
Copyright (C) 2022 Alibek Omarov, Velaron
Copyright (C) 2023 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "build.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stddef.h>
#if XASH_POSIX
#include <unistd.h>
#if !XASH_PSVITA
#include <sys/ioctl.h>
#endif
#endif
#if XASH_LINUX
#include <linux/fs.h>
#ifndef FS_CASEFOLD_FL // for compatibility with older distros
#define FS_CASEFOLD_FL 0x40000000
#endif // FS_CASEFOLD_FL
#endif // XASH_LINUX

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

static qboolean Platform_GetDirectoryCaseSensitivity( const char *dir )
{
#if XASH_WIN32 || XASH_PSVITA || XASH_NSWITCH
	return false;
#elif XASH_ANDROID
	// on Android, doing code below causes crash in MediaProviderGoogle.apk!libfuse_jni.so
	// which in turn makes vold (Android's Volume Daemon) to umount /storage/emulated/0
	// and because you can't unmount a filesystem when there is file descriptors open
	// it has no other choice but to terminate and then kill our program
	return true;
#elif XASH_LINUX && defined( FS_IOC_GETFLAGS )
	int flags = 0;
	int fd;

	fd = open( dir, O_RDONLY | O_NONBLOCK );
	if( fd < 0 )
		return true;

	if( ioctl( fd, FS_IOC_GETFLAGS, &flags ) < 0 )
	{
		close( fd );
		return true;
	}

	close( fd );

	return !FBitSet( flags, FS_CASEFOLD_FL );
#else
	return true;
#endif
}

static int FS_SortDirEntries( const void *_a, const void *_b )
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

	dir->numentries = list->numstrings;
	dir->entries = Mem_Malloc( fs_mempool, sizeof( dir_t ) * dir->numentries );

	for( i = 0; i < list->numstrings; i++ )
	{
		dir_t *entry = &dir->entries[i];

		Q_strncpy( entry->name, list->strings[i], sizeof( entry->name ));
		entry->numentries = DIRENTRY_NOT_SCANNED;
		entry->entries = NULL;
	}

	qsort( dir->entries, dir->numentries, sizeof( dir->entries[0] ), FS_SortDirEntries );
}

static void FS_PopulateDirEntries( dir_t *dir, const char *path )
{
	stringlist_t list;

	if( !FS_SysFolderExists( path ))
	{
		dir->numentries = DIRENTRY_EMPTY_DIRECTORY;
		dir->entries = NULL;
		return;
	}

	if( !Platform_GetDirectoryCaseSensitivity( path ))
	{
		dir->numentries = DIRENTRY_CASEINSENSITIVE;
		dir->entries = NULL;
		return;
	}

	stringlistinit( &list );
	listdirectory( &list, path, false );
	if( !list.numstrings )
	{
		dir->numentries = DIRENTRY_EMPTY_DIRECTORY;
		dir->entries = NULL;
	}
	else
	{
		FS_InitDirEntries( dir, &list );
	}
	stringlistfreecontents( &list );
}

static int FS_FindDirEntry( dir_t *dir, const char *name )
{
	int left, right;

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

	// glorified realloc for sorted dir entries
	// make new array and copy old entries with same name and subentries
	// everything else get freed

	FS_InitDirEntries( &temp, list );

	for( i = 0; i < dir->numentries; i++ )
	{
		dir_t *oldentry = &dir->entries[i];
		dir_t *newentry;
		int j;

		// don't care about directories without subentries
		if( oldentry->entries == NULL )
			continue;

		// try to find this directory in new tree
		j = FS_FindDirEntry( &temp, oldentry->name );

		// not found, free memory
		if( j < 0 )
		{
			FS_FreeDirEntries( oldentry );
			continue;
		}

		// found directory, move all entries
		newentry = &temp.entries[j];

		newentry->numentries = oldentry->numentries;
		newentry->entries = oldentry->entries;
	}

	// now we can free old tree and replace it with temporary
	// do not add null check there! If we hit it, it's probably a logic error!
	Mem_Free( dir->entries );
	dir->numentries = temp.numentries;
	dir->entries = temp.entries;
}

static int FS_MaybeUpdateDirEntries( dir_t *dir, const char *path, const char *entryname )
{
	stringlist_t list;
	int ret;

	stringlistinit( &list );
	listdirectory( &list, path, false );

	if( list.numstrings == 0 ) // empty directory
	{
		FS_FreeDirEntries( dir );
		dir->numentries = DIRENTRY_EMPTY_DIRECTORY;
		ret = -1;
	}
	else if( dir->numentries <= DIRENTRY_EMPTY_DIRECTORY ) // not initialized or was empty
	{
		FS_InitDirEntries( dir, &list );
		ret = FS_FindDirEntry( dir, entryname );
	}
	else if( list.numstrings != dir->numentries ) // quick update
	{
		FS_MergeDirEntries( dir, &list );
		ret = FS_FindDirEntry( dir, entryname );
	}
	else
	{
		// do heavy compare if directory now have an entry we need
		int i;

		for( i = 0; i < list.numstrings; i++ )
		{
			if( !Q_stricmp( list.strings[i], entryname ))
				break;
		}

		if( i != list.numstrings )
		{
			FS_MergeDirEntries( dir, &list );
			ret = FS_FindDirEntry( dir, entryname );
		}
		else ret = -1;
	}

	stringlistfreecontents( &list );
	return ret;
}

static inline qboolean FS_AppendToPath( char *dst, size_t *pi, const size_t len, const char *src, const char *path, const char *err )
{
	size_t i = *pi;

	i += Q_strncpy( &dst[i], src, len - i );
	*pi = i;

	if( i >= len )
	{
		Con_Printf( S_ERROR "%s: overflow while appending %s (%s)\n", __func__, path, err );
		return false;
	}
	return true;
}

qboolean FS_FixFileCase( dir_t *dir, const char *path, char *dst, const size_t len, qboolean createpath )
{
	const char *prev;
	const char *next;
	size_t i = 0;

	if( !FS_AppendToPath( dst, &i, len, dir->name, path, "init" ))
		return false;

	// nothing to fix
	if( !COM_CheckStringEmpty( path ))
		return true;

	for( prev = path, next = Q_strchrnul( prev, '/' );
		  ;
		  prev = next + 1, next = Q_strchrnul( prev, '/' ))
	{
		qboolean uptodate = false; // do not run second scan if we're just updated our directory list
		size_t temp;
		char entryname[MAX_SYSPATH];
		int ret;

		if( dir->numentries == DIRENTRY_NOT_SCANNED )
		{
			// read directory first time
			FS_PopulateDirEntries( dir, dst );
			uptodate = true;
		}

		// this subdirectory is case insensitive, just slam everything that's left
		if( dir->numentries == DIRENTRY_CASEINSENSITIVE )
		{
			if( !FS_AppendToPath( dst, &i, len, prev, path, "caseinsensitive entry" ))
				return false;

			// check file existense
			return createpath ? true : FS_SysFileOrFolderExists( dst );
		}

		// get our entry name
		Q_strncpy( entryname, prev, next - prev + 1 );

		// didn't found, but does it exists in FS?
		if(( ret = FS_FindDirEntry( dir, entryname )) < 0 )
		{
			// if we're creating files or folders, we don't care if path doesn't exist
			// so copy everything that's left and exit without an error
			if( uptodate || ( ret = FS_MaybeUpdateDirEntries( dir, dst, entryname )) < 0 )
				return createpath ? FS_AppendToPath( dst, &i, len, prev, path, "create path" ) : false;

			uptodate = true;
		}

		dir = &dir->entries[ret];
		temp = i;
		if( !FS_AppendToPath( dst, &temp, len, dir->name, path, "case fix" ))
			return false;

		if( !uptodate && !FS_SysFileOrFolderExists( dst )) // file not found, rescan...
		{
			dst[i] = 0; // strip failed part

			// if we're creating files or folders, we don't care if path doesn't exist
			// so copy everything that's left and exit without an error
			if(( ret = FS_MaybeUpdateDirEntries( dir, dst, entryname )) < 0 )
				return createpath ? FS_AppendToPath( dst, &i, len, prev, path, "create path rescan" ) : false;

			dir = &dir->entries[ret];
			if( !FS_AppendToPath( dst, &temp, len, dir->name, path, "case fix rescan" ))
				return false;
		}
		i = temp;

		// end of string, found file, return
		if( next[0] == '\0' || ( next[0] == '/' && next[1] == '\0' ))
			break;

		if( !FS_AppendToPath( dst, &i, len, "/", path, "path separator" ))
			return false;
	}

	return true;
}

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

	if( !FS_FixFileCase( search->dir, path, netpath, sizeof( netpath ), false ))
		return -1;

	if( FS_SysFileExists( netpath ))
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

	if( !FS_FixFileCase( search->dir, basepath, netpath, sizeof( netpath ), false ))
	{
		Mem_Free( basepath );
		return;
	}

	stringlistinit( &dirlist );
	listdirectory( &dirlist, netpath, false );

	Q_strncpy( temp, basepath, sizeof( temp ));

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
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", search->filename, filename );
	return FS_SysFileTime( path );
}

static file_t *FS_OpenFile_DIR( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	file_t *f;
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", search->filename, filename );
	f = FS_SysOpen( path, mode );
	if( !f )
		return NULL;

	f->searchpath = search;

	return f;
}

void FS_InitDirectorySearchpath( searchpath_t *search, const char *path, int flags )
{
	memset( search, 0, sizeof( searchpath_t ));

	Q_strncpy( search->filename, path, sizeof( search->filename ) - 1 );
	COM_PathSlashFix( search->filename );

	if( !Q_stricmp( COM_FileExtension( path ), "pk3dir" ))
		search->type = SEARCHPATH_PK3DIR;
	else search->type = SEARCHPATH_PLAIN;
	search->flags = flags;
	search->pfnPrintInfo = FS_PrintInfo_DIR;
	search->pfnClose = FS_Close_DIR;
	search->pfnOpenFile = FS_OpenFile_DIR;
	search->pfnFileTime = FS_FileTime_DIR;
	search->pfnFindFile = FS_FindFile_DIR;
	search->pfnSearch = FS_Search_DIR;

	// create cache root
	search->dir = Mem_Malloc( fs_mempool, sizeof( dir_t ));
	Q_strncpy( search->dir->name, search->filename, sizeof( search->dir->name ));
	FS_PopulateDirEntries( search->dir, path );
}

searchpath_t *FS_AddDir_Fullpath( const char *path, int flags )
{
	searchpath_t *search;

	search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ));
	FS_InitDirectorySearchpath( search, path, flags );
	Con_Printf( "Adding directory: %s\n", path );

	return search;
}
