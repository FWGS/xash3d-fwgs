 /*
filesystem.c - game filesystem based on DP fs
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "library.h"

fs_api_t g_fsapi;
fs_globals_t *FI;

static HINSTANCE fs_hInstance;

static void FS_Rescan_f( void )
{
	FS_Rescan();
}

static void FS_ClearPaths_f( void )
{
	FS_ClearSearchPath();
}

static void FS_Path_f_( void )
{
	FS_Path_f();
}

static fs_interface_t fs_memfuncs =
{
	Con_Printf,
	Con_DPrintf,
	Con_Reportf,
	Sys_Error,

	_Mem_AllocPool,
	_Mem_FreePool,
	_Mem_Alloc,
	_Mem_Realloc,
	_Mem_Free,
};

static void FS_UnloadProgs( void )
{
	COM_FreeLibrary( fs_hInstance );
	fs_hInstance = 0;
}

qboolean FS_LoadProgs( const char *name )
{
	FSAPI GetFSAPI;

	fs_hInstance = COM_LoadLibrary( name, false, true );

	if( !fs_hInstance )
	{
		Host_Error( "FS_LoadProgs: can't load filesystem library %s: %s\n", name, COM_GetLibraryError() );
		return false;
	}

	if( !( GetFSAPI = (FSAPI)COM_GetProcAddress( fs_hInstance, GET_FS_API )))
	{
		FS_UnloadProgs();
		Host_Error( "FS_LoadProgs: can't find GetFSAPI entry point in %s\n", name );
		return false;
	}

	if( !GetFSAPI( FS_API_VERSION, &g_fsapi, &FI, &fs_memfuncs ))
	{
		FS_UnloadProgs();
		Host_Error( "FS_LoadProgs: can't initialize filesystem API: wrong version\n" );
		return false;
	}

	Con_DPrintf( "FS_LoadProgs: filesystem_stdio successfully loaded\n" );

	return true;
}

/*
================
FS_Init
================
*/
void FS_Init( void )
{
	qboolean		hasBaseDir = false;
	qboolean		hasGameDir = false;
	qboolean		caseinsensitive = true;
	int		i;
	string gamedir;

	Cmd_AddRestrictedCommand( "fs_rescan", FS_Rescan_f, "rescan filesystem search pathes" );
	Cmd_AddRestrictedCommand( "fs_path", FS_Path_f_, "show filesystem search pathes" );
	Cmd_AddRestrictedCommand( "fs_clearpaths", FS_ClearPaths_f, "clear filesystem search pathes" );

#if !XASH_WIN32
	if( Sys_CheckParm( "-casesensitive" ) )
		caseinsensitive = false;
#endif

	if( !Sys_GetParmFromCmdLine( "-game", gamedir ))
		Q_strncpy( gamedir, SI.basedirName, sizeof( gamedir )); // gamedir == basedir

	if( !FS_InitStdio( caseinsensitive, host.rootdir, SI.basedirName, gamedir, host.rodir ))
	{
		Host_Error( "Can't init filesystem_stdio!\n" );
		return;
	}

	if( !Sys_GetParmFromCmdLine( "-dll", SI.gamedll ))
		SI.gamedll[0] = 0;

	if( !Sys_GetParmFromCmdLine( "-clientlib", SI.clientlib ))
		SI.clientlib[0] = 0;
}

/*
================
FS_Shutdown
================
*/
void FS_Shutdown( void )
{
	int	i;

	FS_ShutdownStdio();

	memset( &SI, 0, sizeof( sysinfo_t ));

	FS_UnloadProgs();
}




