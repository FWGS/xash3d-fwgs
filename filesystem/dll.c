/*
dll.c - filesystem_stdio DLL entry, engine interface stubs, exported API table
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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "port.h"
#include "crtlib.h"
#include "filesystem.h"
#include "filesystem_internal.h"
#include "common/com_strings.h"

static poolhandle_t Mem_AllocPoolStub( const char *name, unsigned int flags, const char *filename, int fileline )
{
	return (poolhandle_t)0xDEADC0DE;
}

static void Mem_FreePoolStub( poolhandle_t *poolptr, const char *filename, int fileline )
{
	// stub
}

static void *Mem_AllocStub( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	void *ptr = malloc( size );
	if( clear ) memset( ptr, 0, size );
	return ptr;
}

static void *Mem_ReallocStub( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return realloc( memptr, size );
}

static void Mem_FreeStub( void *data, const char *filename, int fileline )
{
	free( data );
}

static void Con_PrintfStub( const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	vprintf( fmt, ap );
	va_end( ap );
}

static void Sys_ErrorStub( const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	vfprintf( stderr, fmt, ap );
	va_end( ap );

	exit( 1 );
}

static qboolean FS_GetRootDirectory( char *path, size_t size )
{
	size_t dirlen = Q_strlen( fs_rootdir );

	if( dirlen >= size ) // check for possible overflow
		return false;

	Q_strncpy( path, fs_rootdir, size );
	return true;
}

static void *Sys_GetNativeObjectStub( const char *object )
{
	return NULL;
}

fs_interface_t g_engfuncs =
{
	Con_PrintfStub,
	Con_PrintfStub,
	Con_PrintfStub,
	Sys_ErrorStub,
	Mem_AllocPoolStub,
	Mem_FreePoolStub,
	Mem_AllocStub,
	Mem_ReallocStub,
	Mem_FreeStub,
	Sys_GetNativeObjectStub,
};

static qboolean FS_InitInterface( int version, const fs_interface_t *engfuncs )
{
	// to be extended in future interface revisions
	if( version != FS_API_VERSION )
	{
		Con_Printf( S_ERROR "filesystem optional interface version mismatch: expected %d, got %d\n",
			FS_API_VERSION, version );
		return false;
	}

	if( engfuncs->_Con_Printf )
		g_engfuncs._Con_Printf = engfuncs->_Con_Printf;

	if( engfuncs->_Con_DPrintf )
		g_engfuncs._Con_DPrintf = engfuncs->_Con_DPrintf;

	if( engfuncs->_Con_Reportf )
		g_engfuncs._Con_Reportf = engfuncs->_Con_Reportf;

	if( engfuncs->_Sys_Error )
		g_engfuncs._Sys_Error = engfuncs->_Sys_Error;

	if( engfuncs->_Mem_AllocPool && engfuncs->_Mem_FreePool )
	{
		g_engfuncs._Mem_AllocPool = engfuncs->_Mem_AllocPool;
		g_engfuncs._Mem_FreePool = engfuncs->_Mem_FreePool;

		Con_Reportf( "filesystem_stdio: custom pool allocation functions found\n" );
	}

	if( engfuncs->_Mem_Alloc && engfuncs->_Mem_Realloc && engfuncs->_Mem_Free )
	{
		g_engfuncs._Mem_Alloc = engfuncs->_Mem_Alloc;
		g_engfuncs._Mem_Realloc = engfuncs->_Mem_Realloc;
		g_engfuncs._Mem_Free = engfuncs->_Mem_Free;

		Con_Reportf( "filesystem_stdio: custom memory allocation functions found\n" );
	}

	if( engfuncs->_Sys_GetNativeObject )
	{
		g_engfuncs._Sys_GetNativeObject = engfuncs->_Sys_GetNativeObject;
		Con_Reportf( "filesystem_stdio: custom platform-specific functions found\n" );
	}

	return true;
}

const fs_api_t g_api =
{
	FS_InitStdio,
	FS_ShutdownStdio,

	// search path utils
	FS_Rescan,
	FS_ClearSearchPath,
	FS_AllowDirectPaths,
	FS_AddGameDirectory,
	FS_AddGameHierarchy,
	FS_Search,
	FS_SetCurrentDirectory,
	FS_FindLibrary,
	FS_Path_f,

	// gameinfo utils
	FS_Gamedir,
	FS_LoadGameInfo,

	// file ops
	FS_Open,
	FS_Write,
	FS_Read,
	FS_Seek,
	FS_Tell,
	FS_Eof,
	FS_Flush,
	FS_Close,
	FS_Gets,
	FS_UnGetc,
	FS_Getc,
	FS_VPrintf,
	FS_Printf,
	FS_Print,
	FS_FileLength,
	FS_FileCopy,

	// file buffer ops
	FS_LoadFile,
	FS_LoadDirectFile,
	FS_WriteFile,

	// file hashing
	CRC32_File,
	MD5_HashFile,

	// filesystem ops
	FS_FileExists,
	FS_FileTime,
	FS_FileSize,
	FS_Rename,
	FS_Delete,
	FS_SysFileExists,
	FS_GetDiskPath,

	NULL,
	(void *)FS_MountArchive_Fullpath,

	FS_GetFullDiskPath,
	FS_LoadFileMalloc,

	FS_IsArchiveExtensionSupported,
	FS_GetArchiveByName,
	FS_FindFileInArchive,
	FS_OpenFileFromArchive,
	FS_LoadFileFromArchive,

	FS_GetRootDirectory,

	FS_MakeGameInfo,
};

int EXPORT GetFSAPI( int version, fs_api_t *api, fs_globals_t **globals, fs_interface_t *engfuncs );
int EXPORT GetFSAPI( int version, fs_api_t *api, fs_globals_t **globals, fs_interface_t *engfuncs )
{
	if( engfuncs && !FS_InitInterface( version, engfuncs ))
		return 0;

	*api = g_api;
	*globals = &FI;

	return FS_API_VERSION;
}
