/*
filesystem.c - game filesystem based on DP fs (core globals and shared helpers)
Copyright (C) 2003-2006 Mathieu Olivier
Copyright (C) 2000-2007 DarkPlaces contributors
Copyright (C) 2007 Uncle Mike
Copyright (C) 2015-2023 Xash3D FWGS contributors

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
#include <fcntl.h>
#include "port.h"
#include "crtlib.h"
#include "filesystem.h"
#include "filesystem_internal.h"

#if !defined( O_BINARY )
	#define O_BINARY 0
#endif

fs_globals_t FI;
poolhandle_t  fs_mempool;
char          fs_rootdir[MAX_SYSPATH];
searchpath_t *fs_writepath;

#ifdef XASH_REDUCE_FD
static file_t *fs_last_readfile;
static zip_t *fs_last_zip;

void FS_EnsureOpenFile( file_t *file )
{
	if( fs_last_readfile == file )
		return;

	if( file && !file->backup_path )
		return;

	if( fs_last_readfile && (fs_last_readfile->handle != -1) )
	{
		fs_last_readfile->backup_position = lseek(  fs_last_readfile->handle, 0, SEEK_CUR );
		close( fs_last_readfile->handle );
		fs_last_readfile->handle = -1;
	}
	fs_last_readfile = file;
	if( file && (file->handle == -1) )
	{
		file->handle = open( file->backup_path, file->backup_options );
		lseek( file->handle, file->backup_position, SEEK_SET );
	}
}

void FS_BackupFileName( file_t *file, const char *path, uint options )
{
	if( path == NULL )
	{
		if( file->backup_path )
			Mem_Free( (void*)file->backup_path );
		if( file == fs_last_readfile )
			FS_EnsureOpenFile( NULL );
	}
	else if( options == O_RDONLY || options == (O_RDONLY|O_BINARY) )
	{
		file->backup_path = copystring( path );
		file->backup_options = options;
	}
}
#else
void FS_EnsureOpenFile( file_t *file ) {}
void FS_BackupFileName( file_t *file, const char *path, uint options ) {}
#endif

void _Mem_Free( void *data, const char *filename, int fileline )
{
	g_engfuncs._Mem_Free( data, filename, fileline );
}

void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return g_engfuncs._Mem_Alloc( poolptr, size, clear, filename, fileline );
}

void FS_InitMemory( void )
{
	fs_mempool = Mem_AllocPool( "FileSystem Pool" );
	fs_searchpaths = NULL;
}
