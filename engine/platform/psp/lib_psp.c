/*
lib_psp.c - dynamic library code for Sony PSP system
Copyright (C) 2018 Flying With Gauss
Copyright (C) 2022 Sergey Galushko

This program is free software: you can redistribute it and/sor modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "platform/platform.h"

#if XASH_LIB == LIB_PSP
#include <pspmodulemgr.h>
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"

#define MOD_MAXNAMELEN 256

typedef struct table_s
{
	const char	*name;
	void		*pointer;
} table_t;
#include "generated_library_tables.h"

typedef struct mod_func_s
{
	const char	*name;
	void		*addr;
} mod_func_t;

typedef struct mod_handle_s
{
	SceUID		uid;
	char		name[MOD_MAXNAMELEN];
	uint		segAddr;
	uint		segSize;
	mod_func_t	*func;
	struct mod_handle_s	*next;
} mod_handle_t;

static mod_handle_t	*modList = NULL;
static char	*modErrorPtr = NULL;
static char	modErrorBuf[1024];

#define Module_Error( fmt, ... ) Module_SetError( "%s: " fmt "\n", __FUNCTION__, ##__VA_ARGS__ )

int Module_SetError( const char *fmt, ... )
{
	va_list	args;
	int		result;

	va_start( args, fmt );
	result = Q_vsnprintf( modErrorBuf, sizeof( modErrorBuf ), fmt, args );
	va_end( args );

	modErrorPtr = modErrorBuf;

	return result;
}

char *Module_GetError( void )
{
	char *errPtr = modErrorPtr;
	modErrorPtr = NULL;
	return errPtr;
}

static mod_handle_t *Module_Find( const char *name )
{
	mod_handle_t	*modHandle;

	if( !name )
	{
		Module_Error( "input mismatch" );
		return NULL;
	}

	for( modHandle = modList; modHandle; modHandle = modHandle->next )
	{
		if( !Q_strcmp( modHandle->name, name ))
			return modHandle;
	}

	return NULL;
}

static const char *Module_Name( void *handle )
{
	mod_handle_t	*modHandle;

	if( !handle )
	{
		Module_Error( "input mismatch" );
		return NULL;
	}

	for( modHandle = modList; modHandle; modHandle = modHandle->next )
	{
		if( modHandle == handle )
			return modHandle->name;
	}

	return NULL;
}

static mod_func_t *Module_LoadStatic( const char *name )
{
	table_t	*staticLib;

	if( !name )
	{
		Module_Error( "input mismatch" );
		return NULL;
	}

	for( staticLib = ( table_t* )libs; staticLib->pointer && staticLib->name; staticLib++ )
	{
		if( !Q_strcmp( staticLib->name, name ))
			return staticLib->pointer;
	}

	return NULL;
}

mod_handle_t *Module_Load( const char *name )
{
	mod_handle_t	*modHandle;
	mod_func_t	*modFunc;
	qboolean	skipPrefix;
	char		modStaticName[32];
	void		*modArg[2];
	SceUID		modUid;
	uint		modSegAddr, modSegSize;
	SceKernelModuleInfo	info;
	char 		engine_cwd[PATH_MAX];

	if( !name )
		return NULL;

	modHandle = Module_Find( name );
	if( modHandle )
		return modHandle;

	skipPrefix = false;
	modSegAddr = modSegSize = 0;

	Q_sprintf( engine_cwd, "%s/", host.rootdir );

	modArg[0] = &modFunc;
	modArg[1] = engine_cwd;
	modUid = Platform_LoadModule( name, 0, sizeof( modArg ), modArg );
	if( modUid < 0 )
	{
		COM_FileBase( name, modStaticName );

		if( !Q_strncmp( modStaticName, "lib", 3 ) )
			skipPrefix = true;

		modFunc = Module_LoadStatic( skipPrefix ? &modStaticName[3] : modStaticName );
		if( !modFunc )
		{
			Module_Error( "module %s error ( %#010x )", name, modUid );
			return NULL;
		}

		modUid = -1;

		Con_Reportf( "Module_Load( %s ): ( static ) success!\n", name );
	}
	else
	{
		info.size = sizeof( info );
		if ( sceKernelQueryModuleInfo( modUid, &info ) >= 0 )
		{
			if( info.nsegment > 0 )
			{
				modSegAddr = info.segmentaddr[0];
				modSegSize = info.segmentsize[0];
			}
		}

		Con_Reportf( "Module_Load( %s ): ( dynamic ) success!\n", name );
	}

	modHandle = calloc( 1, sizeof( mod_handle_t ));
	if( !modHandle )
	{
		Module_Error( "out of memory" );
		return NULL;
	}

	Q_strncpy( modHandle->name, name, MOD_MAXNAMELEN );

	modHandle->uid = modUid;
	modHandle->func = modFunc;
	modHandle->segAddr = modSegAddr;
	modHandle->segSize = modSegSize;
	modHandle->next = modList;

	modList = modHandle;

	return modHandle;
}

void *Module_GetAddrByName( mod_handle_t *handle, const char *name )
{
	mod_func_t	*modFunc;

	if( !handle || !name )
	{
		Module_Error( "input mismatch" );
		return NULL;
	}

	if( !Module_Name( handle ))
	{
		Module_SetError( "unknown handle" );
		return NULL;
	}

	if( !handle->func )
	{
		Module_SetError( "call Module_Load() first" );
		return NULL;
	}

	for( modFunc = handle->func; modFunc->addr && modFunc->name; modFunc++ )
	{
		if( !Q_strcmp( modFunc->name, name ))
			return modFunc->addr;
	}

	Module_Error( "func %s not found in %s", name, handle->name );

	return NULL;
}

const char *Module_GetNameByAddr( mod_handle_t *handle, const void *addr )
{
	mod_func_t	*modFunc;

	if( !handle || !addr )
	{
		Module_Error( "input mismatch" );
		return NULL;
	}

	if( !Module_Name( handle ))
	{
		Module_SetError( "unknown handle" );
		return NULL;
	}

	if( !handle->func )
	{
		Module_SetError( "call Module_Load() first" );
		return NULL;
	}

	for( modFunc = handle->func; modFunc->addr && modFunc->name; modFunc++ )
	{
		if( modFunc->addr == addr )
			return modFunc->name;
	}

	Module_Error( "addr %#010x not found in %s", addr, handle->name );

	return NULL;
}

uint Module_GetOffsetByAddr( mod_handle_t *handle, const void *addr )
{
	uint	addrUInt;

	if( !handle || !addr )
	{
		Module_Error( "input mismatch" );
		return 0;
	}

	if( !Module_Name( handle ))
	{
		Module_SetError( "unknown handle" );
		return 0;
	}

	if( !handle->segAddr )
	{
		Module_SetError( "unknown segment" );
		return 0;
	}

	addrUInt = ( uint )addr;
	if( addrUInt < handle->segAddr || addrUInt >= ( handle->segAddr + handle->segSize ))
	{
		Module_SetError( "addr %#010x is out of range", addrUInt );
		return 0;
	}

	return addrUInt - handle->segAddr;
}

void *Module_GetAddrByOffset( mod_handle_t *handle, uint offset )
{
	if( !handle || !offset )
	{
		Module_Error( "input mismatch" );
		return NULL;
	}

	if( !Module_Name( handle ))
	{
		Module_SetError( "unknown handle" );
		return NULL;
	}

	if( !handle->segAddr )
	{
		Module_SetError( "unknown segment" );
		return NULL;
	}

	if( offset >= handle->segSize )
	{
		Module_SetError( "offset %#010x is out of range", offset );
		return NULL;
	}

	return ( void* )( offset + handle->segAddr );
}

int Module_Unload( mod_handle_t *handle )
{
	mod_handle_t	*modHandle;
	int		result, sceCode;

	if( !handle )
	{
		Module_Error( "input mismatch" );
		return -1;
	}

	if( !Module_Name( handle ))
	{
		Module_Error( "unknown handle" );
		return -2;
	}

	if( handle->uid != -1 )
	{
		result = Platform_UnloadModule( handle->uid, &sceCode );
		if( result < 0 )
		{
			if( result == -1 )
				Module_Error( "module %s doesn't want to stop", handle->name );
			else
				Module_Error( "module %s error ( %#010x )", handle->name, sceCode );

			return -3;
		}
	}

	if( handle != modList )
	{
		for( modHandle = modList; modHandle; modHandle = modHandle->next )
		{
			if( modHandle->next == handle )
			{
				modHandle->next = handle->next;
				break;
			}
		}
	}
	else modList = handle->next;

	free( handle );

	return 0;
}

qboolean COM_CheckLibraryDirectDependency( const char *name, const char *depname, qboolean directpath )
{
	// TODO: implement
	return true;
}

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	dll_user_t	*hInst = NULL;
	void		*pHandle = NULL;

	COM_ResetLibraryError();

	// platforms where gameinfo mechanism is working goes here
	// and use FS_FindLibrary
	hInst = FS_FindLibrary( dllname, directpath );
	if( !hInst )
	{
		// try to find by linker(LD_LIBRARY_PATH, DYLD_LIBRARY_PATH, LD_32_LIBRARY_PATH and so on...)
		if( !pHandle )
		{
			pHandle = Module_Load( dllname );
			if( pHandle )
				return pHandle;

			COM_PushLibraryError( va( "Failed to find library %s", dllname ));
			COM_PushLibraryError( Module_GetError() );
			return NULL;
		}
	}

	if( hInst->custom_loader )
	{
		COM_PushLibraryError( va( "Custom library loader is not available. Extract library %s and fix gameinfo.txt!", hInst->fullPath ));
		Mem_Free( hInst );
		return NULL;
	}

	if( !( hInst->hInstance = Module_Load( hInst->fullPath )))
	{
		COM_PushLibraryError( Module_GetError() );
		Mem_Free( hInst );
		return NULL;
	}

	pHandle = hInst->hInstance;

	Mem_Free( hInst );

	return pHandle;
}

void COM_FreeLibrary( void *hInstance )
{
	Module_Unload( hInstance );
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
	return Module_GetAddrByName( hInstance, name );
}

void *COM_FunctionFromName_SR( void *hInstance, const char *pName )
{
	void	*funcAddr;

	if( !memcmp( pName, "ofs:", 4 ))
		funcAddr = Module_GetAddrByOffset( hInstance, Q_atoi( &pName[4] ));
	else
		funcAddr = Module_GetAddrByName( hInstance, pName );

	if( !funcAddr )
		Con_Reportf( S_ERROR "FunctionFromName: Can't get symbol %s: %s\n", pName, Module_GetError() );

	return funcAddr;
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	static char	offsetName[16];
	const char	*funcName;
	uint		addrOffset;

	funcName = Module_GetNameByAddr( hInstance, function );
	if( funcName )
		return funcName;

	addrOffset = Module_GetOffsetByAddr( hInstance, function );
	if( !addrOffset )
		return NULL;

	Q_snprintf( offsetName, sizeof( offsetName ), "ofs:%u", addrOffset );
	Con_Reportf( "COM_NameForFunction: %s\n", offsetName );

	return offsetName;
}
#endif // XASH_LIB == LIB_PSP
