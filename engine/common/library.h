/*
library.h - custom dlls loader
Copyright (C) 2008 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef LIBRARY_H
#define LIBRARY_H

#define MAX_LIBRARY_EXPORTS		4096

typedef struct dll_user_s
{
	void	*hInstance;		// instance handle
	qboolean	custom_loader;		// a bit who indicated loader type
	qboolean	encrypted;		// dll is crypted (some client.dll in HL, CS etc)
	char	dllName[32];		// for debug messages
	char fullPath[2048];
	string shortPath;	// actual dll paths

	// ordinals stuff, valid only on Win32
	word	*ordinals;
	dword	*funcs;
	char	*names[MAX_LIBRARY_EXPORTS];	// max 4096 exports supported
	int	num_ordinals;		// actual exports count
	uintptr_t	funcBase;			// base offset
} dll_user_t;

dll_user_t *FS_FindLibrary( const char *dllname, qboolean directpath );
void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath );
void *COM_GetProcAddress( void *hInstance, const char *name );
const char *COM_NameForFunction( void *hInstance, void *function );
void *COM_FunctionFromName_SR( void *hInstance, const char *pName ); // Save/Restore version
void *COM_FunctionFromName( void *hInstance, const char *pName );
void COM_FreeLibrary( void *hInstance );
const char *COM_GetLibraryError( void );
qboolean COM_CheckLibraryDirectDependency( const char *name, const char *depname, qboolean directpath );

// TODO: Move to internal?
void COM_ResetLibraryError( void );
void COM_PushLibraryError( const char *error );
const char *COM_OffsetNameForFunction( void *function );

typedef enum
{
	LIBRARY_CLIENT,
	LIBRARY_SERVER,
	LIBRARY_GAMEUI
} ECommonLibraryType;

void COM_GetCommonLibraryPath( ECommonLibraryType eLibType, char *out, size_t size );

typedef enum
{
	MANGLE_UNKNOWN = 0,

	/* binary offset, when NameForFunction isn't implemented */
	MANGLE_OFFSET,

	/* Itanium C++ ABI mangling, native for most operating systems */
	MANGLE_ITANIUM,

	/* MSVC "decoration" */
	MANGLE_MSVC,

	/* Valve's silly mangle for crossplatform saves */
	MANGLE_VALVE,
} EFunctionMangleType;

// converts to MANGLE_VALVE if possible
const char *COM_GetPlatformNeutralName( const char *in_name );

// converts to native mangling, result must be freed
char **COM_ConvertToLocalPlatform( EFunctionMangleType to, const char *from, size_t *numfuncs );

// used by lib_win.c
char *COM_GetMSVCName( const char *in_name );


#endif//LIBRARY_H
