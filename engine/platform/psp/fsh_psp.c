/*
fsh_psp.c - PSP filesystem helper
Copyright (C) 2022 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <pspiofilemgr.h>
#include "common.h"
#include "fsh_psp.h"

#define FSH_MAX_PATH		64
#define FSH_EMPTY_STRING	"*empty*"

typedef struct fsh_path_s
{
	char		path[FSH_MAX_PATH];
	int		fsize;
	uint		hashvalue;
	struct fsh_path_s	*nexthash;
}fsh_path_t;

typedef struct fsh_handle_s
{
	qboolean	ready;
	int		count;
	char		folderpath[PATH_MAX];
	int		folderpath_size;
	uint		empty_hash;
	int		pathlist_size;
	int		hashlist_size;
	fsh_path_t	*pathlist;
	fsh_path_t	**hashlist;
	struct fsh_handle_s	*next;
}fsh_handle_t;

static fsh_handle_t	*fsh_poolchain = NULL;

/*
================
FSH_AddHash
================
*/
_inline void FSH_AddHash( fsh_path_t **hashlist, uint hash, fsh_path_t *fptr )
{
	fptr->hashvalue = hash;
	fptr->nexthash = hashlist[hash];
	hashlist[hash] = fptr;
}

/*
================
FSH_RemoveHash
================
*/
_inline void FSH_RemoveHash( fsh_path_t **hashlist, uint hash, fsh_path_t *fptr )
{
	fsh_path_t	**fptr_prev;

	for( fptr_prev = &hashlist[hash]; *fptr_prev != NULL; fptr_prev = &( *fptr_prev )->nexthash )
	{
		if( *fptr_prev == fptr )
		{
			*fptr_prev = fptr->nexthash;
			break;
		}
	}
}

/*
================
FSH_AddFilePath
================
*/
int FSH_AddFilePathWs( fsh_handle_t *handle, const char *path, int size )
{
	uint		hash;
	fsh_path_t	*fptr;
	const char	*strip_path;

	if( !handle )
		return -2;

	if( Q_strnicmp( path, handle->folderpath, handle->folderpath_size ))
		return -2;

	strip_path = path + handle->folderpath_size + 1; // + '/'

	hash = COM_HashKey( strip_path, handle->hashlist_size );

	if( handle->ready && handle->count > 0 )
	{
		// see if already added
		for( fptr = handle->hashlist[hash]; fptr != NULL; fptr = fptr->nexthash )
		{
			if( !Q_stricmp( fptr->path, strip_path ))
				return fptr - handle->pathlist; // index
		}

		// find empty
		for( fptr = handle->hashlist[handle->empty_hash]; fptr != NULL; fptr = fptr->nexthash )
		{
			if( !Q_stricmp( fptr->path, FSH_EMPTY_STRING ))
			{
				Q_strncpy( fptr->path, strip_path, FSH_MAX_PATH - 1 );

				// file size
				fptr->fsize = size;

				// remove empty from hash table
				FSH_RemoveHash( handle->hashlist, handle->empty_hash, fptr );

				// add to hash table
				FSH_AddHash( handle->hashlist, hash, fptr );

				return fptr - handle->pathlist; // index
			}
		}
	}

	// create new
	if( handle->count + 1 >= handle->pathlist_size )
		return -1;

	fptr = &handle->pathlist[handle->count];
	Q_strncpy( fptr->path, strip_path, FSH_MAX_PATH - 1 );

	// file size
	fptr->fsize = size;

	// add to hash table
	FSH_AddHash( handle->hashlist, hash, fptr );

	handle->count++;

	return fptr - handle->pathlist; // index
}

/*
================
FSH_RemoveFilePath
================
*/
int FSH_RemoveFilePath( fsh_handle_t *handle, const char *path )
{
	uint		hash;
	fsh_path_t	*fptr, **fptr_prev;
	const char	*strip_path;

	if( !handle )
		return -2;

	if( Q_strnicmp( path, handle->folderpath, handle->folderpath_size ))
		return -2;

	strip_path = path + handle->folderpath_size + 1; // + '/'

	hash = COM_HashKey( strip_path, handle->hashlist_size );

	for( fptr = handle->hashlist[hash]; fptr != NULL; fptr = fptr->nexthash )
	{
		if( !Q_stricmp( fptr->path, strip_path ))
		{
			memset( fptr->path, 0, FSH_MAX_PATH );
			Q_strncpy( fptr->path, FSH_EMPTY_STRING, FSH_MAX_PATH - 1 );
			fptr->fsize = -3; // undefined

			// remove from hash table
			FSH_RemoveHash( handle->hashlist, hash, fptr );

			// add empty to hash table
			FSH_AddHash( handle->hashlist, handle->empty_hash, fptr );

			return fptr - handle->pathlist; // index
		}
	}

	return -1;
}

/*
================
FSH_RenameFilePath
================
*/
int FSH_RenameFilePath( fsh_handle_t *handle, const char *oldname, const char *newname )
{
	uint		hash;
	fsh_path_t	*fptr, **fptr_prev;
	const char	*strip_path;

	if( !handle )
		return -2;

	if( Q_strnicmp( oldname, handle->folderpath, handle->folderpath_size ))
		return -2;

	strip_path = oldname + handle->folderpath_size + 1; // + '/'

	hash = COM_HashKey( strip_path, handle->hashlist_size );

	for( fptr = handle->hashlist[hash]; fptr != NULL; fptr = fptr->nexthash )
	{
		if( !Q_stricmp( fptr->path, strip_path ))
		{
			strip_path = newname + handle->folderpath_size + 1; // + '/'

			memset( fptr->path, 0, FSH_MAX_PATH );
			Q_strncpy( fptr->path, strip_path, FSH_MAX_PATH - 1 );

			// remove old from hash table
			FSH_RemoveHash( handle->hashlist, hash, fptr );

			// add new to hash table
			hash = COM_HashKey( strip_path, handle->hashlist_size );
			FSH_AddHash( handle->hashlist, hash, fptr );

			return fptr - handle->pathlist; // index;
		}
	}

	return -1;
}

/*
================
FSH_FindSize
================
*/
int FSH_FindSize( fsh_handle_t *handle, const char *path )
{
	uint		hash;
	fsh_path_t	*fptr;
	const char	*strip_path;

	if( !handle )
		return -2;

	if( !handle->ready || !handle->count || Q_strnicmp( path, handle->folderpath, handle->folderpath_size ))
		return -2;

	strip_path = path + handle->folderpath_size + 1; // + '/'

	hash = COM_HashKey( strip_path, handle->hashlist_size );

	for( fptr = handle->hashlist[hash]; fptr != NULL; fptr = fptr->nexthash )
	{
		if( !Q_stricmp( fptr->path, strip_path ))
			return fptr->fsize;
	}

	return -1;
}

/*
================
FSH_Find
================
*/
int FSH_Find( fsh_handle_t *handle, const char *path )
{
	uint		hash;
	fsh_path_t	*fptr;
	const char	*strip_path;

	if( !handle )
		return -2;

	if( !handle->ready || !handle->count || Q_strnicmp( path, handle->folderpath, handle->folderpath_size ))
		return -2;

	strip_path = path + handle->folderpath_size + 1; // + '/'

	hash = COM_HashKey( strip_path, handle->hashlist_size );

	for( fptr = handle->hashlist[hash]; fptr != NULL; fptr = fptr->nexthash )
	{
		if( !Q_stricmp( fptr->path, strip_path ))
			return fptr - handle->pathlist; // index
	}

	return -1;
}

/*
================
FSH_ScanDir
================
*/
static int FSH_ScanDir( fsh_handle_t *handle, const char *path )
{
	SceUID		dir;
	SceIoDirent	entry;
	char		temp[FSH_MAX_PATH];
	int		result;
	int		fsize;

	if(( dir = sceIoDopen( path )) < 0 )
		return -1;

	result = 0;

	// iterate through the directory
	while( 1 )
	{
		// zero the dirent, to avoid possible problems with sceIoDread
		memset( &entry, 0, sizeof( SceIoDirent ));
		if( !sceIoDread( dir, &entry ))
			break;

		// ignore the virtual directories
		if( !Q_stricmp( entry.d_name, "." ) || !Q_stricmp( entry.d_name, ".." ))
			continue;

		sprintf( temp, "%s/%s", path, entry.d_name );

		if(FIO_S_ISDIR( entry.d_stat.st_mode ))
			result = FSH_ScanDir( handle, temp );
		else if(FIO_S_ISREG( entry.d_stat.st_mode ))
		{
			if( entry.d_stat.st_size <= __INT_MAX__ )
				fsize = ( int )entry.d_stat.st_size;
			else fsize = -3; // undefined

			result = FSH_AddFilePathWs( handle, temp, fsize );
		}
		else continue;
		if( result < 0 ) break;
	}
	sceIoDclose( dir );

	return result;
}

/*
================
FSH_Create
================
*/
fsh_handle_t *FSH_Create( const char *path, int maxfiles )
{
	fsh_handle_t	*handle;

	handle = P5Ram_Alloc( sizeof( fsh_handle_t ), 1 );
	if( !handle )
		return NULL;

	handle->pathlist_size = maxfiles;
	handle->pathlist = P5Ram_Alloc( handle->pathlist_size * sizeof( fsh_path_t ), 1 );
	if( !handle->pathlist )
	{
		P5Ram_Free( handle );
		return NULL;
	}

	handle->hashlist_size = maxfiles >> 2;
	handle->hashlist = P5Ram_Alloc( handle->hashlist_size * sizeof( fsh_path_t* ), 1 );
	if( !handle->hashlist )
	{
		P5Ram_Free( handle );
		return NULL;
	}

	handle->empty_hash = COM_HashKey( FSH_EMPTY_STRING, handle->hashlist_size );

	if( !Q_strnicmp( path, "./", 2 ))
		path += 2;

	Q_strncpy( handle->folderpath, path, FSH_MAX_PATH - 1 );
	handle->folderpath_size = Q_strlen( handle->folderpath );

	if( FSH_ScanDir( handle, handle->folderpath ) < 0 )
	{
		P5Ram_Free( handle );
		return NULL;
	}

	handle->next = fsh_poolchain;
	fsh_poolchain = handle;

	handle->ready = true;

	return handle;
}

/*
================
FSH_Shutdown
================
*/
void FSH_Free( fsh_handle_t *handle )
{
	if( !handle )
		return;

	if( handle->pathlist )
		P5Ram_Free( handle->pathlist );

	if( handle->hashlist )
		P5Ram_Free( handle->hashlist );

	P5Ram_Free( handle );
}
