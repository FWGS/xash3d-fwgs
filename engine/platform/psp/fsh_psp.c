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

#define FSH_MAX_PATH	63

#define FSH_STATE_USED	0x01

typedef struct fsh_path_s
{
	byte			state;
	char			path[FSH_MAX_PATH];
}fsh_path_t;

typedef struct fsh_handle_s
{
	qboolean	ready;
	int		count;
	char		folderpath[FSH_MAX_PATH];
	int		folderpath_size;
	int		pathlist_size;
	fsh_path_t	*pathlist;
}fsh_handle_t;

int FSH_AddFilePath( fsh_handle_t *handle, char *path )
{
	int	index;

	if( !handle )
		return -2;

	if( Q_strnicmp( path, handle->folderpath, handle->folderpath_size ))
		return -2;

	if( handle->ready && handle->count > 0 )
	{
		// see if already added
		for( index = 0; index < handle->count; index++ )
		{
			if( !Q_stricmp( handle->pathlist[index].path, path ))
				return index;
		}

		// find empty
		for( index = 0; index < handle->count; index++ )
		{
			if(!( handle->pathlist[index].state & FSH_STATE_USED ))
			{
				Q_strncpy( handle->pathlist[index].path, path, FSH_MAX_PATH - 1 );
				handle->pathlist[index].state |= FSH_STATE_USED;

				return index;
			}
		}
	}

	// create new
	if( handle->count + 1 >= handle->pathlist_size )
		return -1;

	Q_strncpy( handle->pathlist[handle->count].path, path, FSH_MAX_PATH - 1 );
	handle->pathlist[handle->count].state |= FSH_STATE_USED;
	handle->count++;

	return handle->count;
}

int FSH_RemoveFilePath( fsh_handle_t *handle, char *path )
{
	int	index;

	if( !handle )
		return -2;

	if( Q_strnicmp( path, handle->folderpath, handle->folderpath_size ))
		return -2;

	for( index = 0; index < handle->count; index++ )
	{
		if( !Q_stricmp( handle->pathlist[index].path, path ))
		{
			memset( handle->pathlist[index].path, 0, FSH_MAX_PATH) ;
			handle->pathlist[index].state &= ~FSH_STATE_USED;

			return index;
		}
	}

	return -1;
}

int FSH_Find( fsh_handle_t *handle, char *path )
{
	int	index;

	if( !handle )
		return -2;

	if( !handle->ready || !handle->count || Q_strnicmp( path, handle->folderpath, handle->folderpath_size ))
		return -2;

	for( index = 0; index < handle->count; index++ )
	{
		if( !Q_stricmp(handle->pathlist[index].path, path ))
			return index;
	}

	return -1;
}

static int FSH_ScanDir( fsh_handle_t *handle, char *path )
{
	SceUID		dir;
	SceIoDirent	entry;
	char		temp[FSH_MAX_PATH];
	int		result;

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
			result = FSH_AddFilePath( handle, temp );
		else continue;
		if( result < 0 ) break;
	}
	sceIoDclose( dir );

	return result;
}

fsh_handle_t *FSH_Init( char *path, int maxfiles )
{
	fsh_handle_t	*handle;

	handle = P5Ram_Alloc( sizeof( fsh_handle_t ), 1);
	if( !handle )
		return NULL;

	handle->pathlist_size = maxfiles;
	handle->pathlist = P5Ram_Alloc( handle->pathlist_size * sizeof( fsh_path_t ), 1 );
	if( !handle->pathlist )
	{
		FSH_Shutdown( handle );
		return NULL;
	}

	if( !Q_strnicmp( path, "./", 2 ))
		path += 2;

	Q_strncpy( handle->folderpath, path, FSH_MAX_PATH - 1 );
	handle->folderpath_size = Q_strlen( handle->folderpath );

	if( FSH_ScanDir( handle, handle->folderpath ) < 0 )
	{
		FSH_Shutdown( handle );
		return NULL;
	}

	handle->ready = true;

	return handle;
}

void FSH_Shutdown( fsh_handle_t *handle )
{
	if( !handle )
		return;

	if( handle->pathlist )
		P5Ram_Free( handle->pathlist );

	P5Ram_Free( handle );
}
