/*
lib_win.c - win32 dynamic library loading
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

#include "common.h"

#if XASH_LIB == LIB_WIN32
#include "lib_win.h"

static const wchar_t *FS_PathToWideChar( const char *path )
{
	static wchar_t pathBuffer[MAX_PATH];
	MultiByteToWideChar( CP_UTF8, 0, path, -1, pathBuffer, MAX_PATH );
	return pathBuffer;
}


static DWORD GetOffsetByRVA( DWORD rva, PIMAGE_NT_HEADERS nt_header )
{
	int i = 0;
	PIMAGE_SECTION_HEADER sect_header = IMAGE_FIRST_SECTION( nt_header );

	if (!rva)
		return rva;

	for( i = 0; i < nt_header->FileHeader.NumberOfSections; i++, sect_header++)
	{
		if( rva >= sect_header->VirtualAddress && rva < sect_header->VirtualAddress + sect_header->Misc.VirtualSize )
			break;
	}
	return (rva - sect_header->VirtualAddress + sect_header->PointerToRawData);
}

/*
---------------------------------------------------------------

		Name for function stuff

---------------------------------------------------------------
*/
static void FsGetString( file_t *f, char *str )
{
	char	ch;

	while(( ch = FS_Getc( f )) != EOF )
	{
		*str++ = ch;
		if( !ch ) break;
	}
}

static void FreeNameFuncGlobals( dll_user_t *hInst )
{
	int	i;

	if( !hInst ) return;

	if( hInst->ordinals ) Mem_Free( hInst->ordinals );
	if( hInst->funcs ) Mem_Free( hInst->funcs );

	for( i = 0; i < hInst->num_ordinals; i++ )
	{
		if( hInst->names[i] )
			Mem_Free( hInst->names[i] );
	}

	hInst->num_ordinals = 0;
	hInst->ordinals = NULL;
	hInst->funcs = NULL;
}

qboolean LibraryLoadSymbols( dll_user_t *hInst )
{
	file_t		*f;
	string		errorstring;
	IMAGE_DOS_HEADER	dos_header;
	LONG		nt_signature;
	IMAGE_FILE_HEADER   pe_header;
	IMAGE_SECTION_HEADER	section_header;
	qboolean		rdata_found;
	IMAGE_OPTIONAL_HEADER   optional_header;
	long		rdata_delta = 0;
	IMAGE_EXPORT_DIRECTORY  export_directory;
	long		name_offset;
	long		exports_offset;
	long		ordinal_offset;
	long		function_offset;
	string		function_name;
	dword		*p_Names = NULL;
	int		i, index;

	// can only be done for loaded libraries
	if( !hInst ) return false;

	for( i = 0; i < hInst->num_ordinals; i++ )
		hInst->names[i] = NULL;

	f = FS_Open( hInst->shortPath, "rb", false );
	if( !f )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "couldn't load %s", hInst->shortPath );
		goto table_error;
	}

	if( FS_Read( f, &dos_header, sizeof( dos_header )) != sizeof( dos_header ))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s has corrupted EXE header", hInst->shortPath );
		goto table_error;
	}

	if( dos_header.e_magic != IMAGE_DOS_SIGNATURE )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have a valid dll signature", hInst->shortPath );
		goto table_error;
	}

	if( FS_Seek( f, dos_header.e_lfanew, SEEK_SET ) == -1 )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s error seeking for new exe header", hInst->shortPath );
		goto table_error;
	}

	if( FS_Read( f, &nt_signature, sizeof( nt_signature )) != sizeof( nt_signature ))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s has corrupted NT header", hInst->shortPath );
		goto table_error;
	}

	if( nt_signature != IMAGE_NT_SIGNATURE )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have a valid NT signature", hInst->shortPath );
		goto table_error;
	}

	if( FS_Read( f, &pe_header, sizeof( pe_header )) != sizeof( pe_header ))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have a valid PE header", hInst->shortPath );
		goto table_error;
	}

	if( !pe_header.SizeOfOptionalHeader )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have an optional header", hInst->shortPath );
		goto table_error;
	}

	if( FS_Read( f, &optional_header, sizeof( optional_header )) != sizeof( optional_header ))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s optional header probably corrupted", hInst->shortPath );
		goto table_error;
	}

	rdata_found = false;

	for( i = 0; i < pe_header.NumberOfSections; i++ )
	{
		if( FS_Read( f, &section_header, sizeof( section_header )) != sizeof( section_header ))
		{
			Q_snprintf( errorstring, sizeof( errorstring ), "%s error during reading section header", hInst->shortPath );
			goto table_error;
		}

		if((( optional_header.DataDirectory[0].VirtualAddress >= section_header.VirtualAddress ) &&
			(optional_header.DataDirectory[0].VirtualAddress < (section_header.VirtualAddress + section_header.Misc.VirtualSize))))
		{
			rdata_found = true;
			break;
		}
	}

	if( rdata_found )
	{
		rdata_delta = section_header.VirtualAddress - section_header.PointerToRawData;
	}

	exports_offset = optional_header.DataDirectory[0].VirtualAddress - rdata_delta;

	if( FS_Seek( f, exports_offset, SEEK_SET ) == -1 )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have a valid exports section", hInst->shortPath );
		goto table_error;
	}

	if( FS_Read( f, &export_directory, sizeof( export_directory )) != sizeof( export_directory ))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have a valid optional header", hInst->shortPath );
		goto table_error;
	}

	hInst->num_ordinals = export_directory.NumberOfNames;	// also number of ordinals

	if( hInst->num_ordinals > MAX_LIBRARY_EXPORTS )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s too many exports %i", hInst->shortPath, hInst->num_ordinals );
		hInst->num_ordinals = 0;
		goto table_error;
	}

	ordinal_offset = export_directory.AddressOfNameOrdinals - rdata_delta;

	if( FS_Seek( f, ordinal_offset, SEEK_SET ) == -1 )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have a valid ordinals section", hInst->shortPath );
		goto table_error;
	}

	hInst->ordinals = Mem_Malloc( host.mempool, hInst->num_ordinals * sizeof( word ));

	if( FS_Read( f, hInst->ordinals, hInst->num_ordinals * sizeof( word )) != (hInst->num_ordinals * sizeof( word )))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s error during reading ordinals table", hInst->shortPath );
		goto table_error;
	}

	function_offset = export_directory.AddressOfFunctions - rdata_delta;

	if( FS_Seek( f, function_offset, SEEK_SET ) == -1 )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s does not have a valid export address section", hInst->shortPath );
		goto table_error;
	}

	hInst->funcs = Mem_Malloc( host.mempool, hInst->num_ordinals * sizeof( dword ));

	if( FS_Read( f, hInst->funcs, hInst->num_ordinals * sizeof( dword )) != (hInst->num_ordinals * sizeof( dword )))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s error during reading export address section", hInst->shortPath );
		goto table_error;
	}

	name_offset = export_directory.AddressOfNames - rdata_delta;

	if( FS_Seek( f, name_offset, SEEK_SET ) == -1 )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s file does not have a valid names section", hInst->shortPath );
		goto table_error;
	}

	p_Names = Mem_Malloc( host.mempool, hInst->num_ordinals * sizeof( dword ));

	if( FS_Read( f, p_Names, hInst->num_ordinals * sizeof( dword )) != (hInst->num_ordinals * sizeof( dword )))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s error during reading names table", hInst->shortPath );
		goto table_error;
	}

	for( i = 0; i < hInst->num_ordinals; i++ )
	{
		name_offset = p_Names[i] - rdata_delta;

		if( name_offset != 0 )
		{
			if( FS_Seek( f, name_offset, SEEK_SET ) != -1 )
			{
				FsGetString( f, function_name );
				hInst->names[i] = copystring( COM_GetMSVCName( function_name ));
			}
			else break;
		}
	}

	if( i != hInst->num_ordinals )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s error during loading names section", hInst->shortPath );
		goto table_error;
	}
	FS_Close( f );

	for( i = 0; i < hInst->num_ordinals; i++ )
	{
		if( !Q_strcmp( "GiveFnptrsToDll", hInst->names[i] ))	// main entry point for user dlls
		{
			void	*fn_offset;

			index = hInst->ordinals[i];
			fn_offset = COM_GetProcAddress( hInst, "GiveFnptrsToDll" );
			hInst->funcBase = (uintptr_t)(fn_offset) - hInst->funcs[index];
			break;
		}
	}

	if( p_Names ) Mem_Free( p_Names );
	return true;
table_error:
	// cleanup
	if( f ) FS_Close( f );
	if( p_Names ) Mem_Free( p_Names );
	FreeNameFuncGlobals( hInst );
	Con_Printf( S_ERROR "%s: %s\n", __func__, errorstring );

	return false;
}

static const char *GetLastErrorAsString( void )
{
	const DWORD fm_flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK;
	DWORD errorcode;
	wchar_t wide_errormessage[256];
	static string errormessage;

	errorcode = GetLastError();
	if ( !errorcode )
		return "";

	FormatMessageW( fm_flags, NULL, errorcode, 0, wide_errormessage, ARRAYSIZE( wide_errormessage ), NULL );
	Q_UTF16ToUTF8( errormessage, sizeof( errormessage ), wide_errormessage, ARRAYSIZE( wide_errormessage ));

	return errormessage;
}

static PIMAGE_IMPORT_DESCRIPTOR GetImportDescriptor( const char *name, byte *data, PIMAGE_NT_HEADERS *peheader )
{
	PIMAGE_DOS_HEADER dosHeader;
	PIMAGE_NT_HEADERS peHeader;
	PIMAGE_DATA_DIRECTORY importDir;
	PIMAGE_IMPORT_DESCRIPTOR importDesc;

	if ( !data )
	{
		Con_Printf( S_ERROR "%s: couldn't load %s\n", __func__, name );
		return NULL;
	}

	dosHeader = (PIMAGE_DOS_HEADER)data;
	if ( dosHeader->e_magic != IMAGE_DOS_SIGNATURE )
	{
		Con_Printf( S_ERROR "%s: %s is not a valid executable file\n", __func__, name );
		return NULL;
	}

	peHeader = (PIMAGE_NT_HEADERS)( data + dosHeader->e_lfanew );
	if ( peHeader->Signature != IMAGE_NT_SIGNATURE )
	{
		Con_Printf( S_ERROR "%s: %s is missing a PE header\n", __func__, name );
		return NULL;
	}

	importDir = &peHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if( importDir->Size <= 0 )
	{
		Con_Printf( S_ERROR "%s: %s has no dependencies\n", __func__, name );
		return NULL;
	}

	*peheader = peHeader;
	importDesc = (PIMAGE_IMPORT_DESCRIPTOR)CALCULATE_ADDRESS( data, GetOffsetByRVA( importDir->VirtualAddress, peHeader ) );

	return importDesc;
}

static void ListMissingModules( dll_user_t *hInst )
{
	PIMAGE_NT_HEADERS peHeader;
	PIMAGE_IMPORT_DESCRIPTOR importDesc;
	byte *data;
	char	buf[MAX_VA_STRING];

	if( !hInst || !g_fsapi.LoadFile ) return;

	data = g_fsapi.LoadFile( hInst->dllName, NULL, false );
	if( !data ) return;

	importDesc = GetImportDescriptor( hInst->dllName, data, &peHeader );
	if( !importDesc )
	{
		Mem_Free( data );
		return;
	}

	for( ; !IsBadReadPtr( importDesc, sizeof( IMAGE_IMPORT_DESCRIPTOR ) ) && importDesc->Name; importDesc++ )
	{
		HMODULE hMod;
		const char *importName = (const char *)CALCULATE_ADDRESS( data, GetOffsetByRVA( importDesc->Name, peHeader ) );

		hMod = LoadLibraryExW( FS_PathToWideChar( importName ), NULL, LOAD_LIBRARY_AS_DATAFILE );
		if ( !hMod )
		{
			Q_snprintf( buf, sizeof( buf ), "%s not found!", importName );
			COM_PushLibraryError( buf );
		}
		else
			FreeLibrary( hMod );
	}

	Mem_Free( data );
	return;
}

qboolean COM_CheckLibraryDirectDependency( const char *name, const char *depname, qboolean directpath )
{
	PIMAGE_NT_HEADERS peHeader;
	PIMAGE_IMPORT_DESCRIPTOR importDesc;
	byte *data;
	dll_user_t *hInst;
	qboolean ret = FALSE;

	hInst = FS_FindLibrary( name, directpath );
	if ( !hInst ) return FALSE;

	data = FS_LoadFile( name, NULL, false );
	if ( !data )
	{
		COM_FreeLibrary( hInst );
		return FALSE;
	}

	importDesc = GetImportDescriptor( name, data, &peHeader );
	if ( !importDesc )
	{
		COM_FreeLibrary( hInst );
		Mem_Free( data );
		return FALSE;
	}

	for( ; !IsBadReadPtr( importDesc, sizeof( IMAGE_IMPORT_DESCRIPTOR ) ) && importDesc->Name; importDesc++ )
	{
		const char *importName = (const char *)CALCULATE_ADDRESS( data, GetOffsetByRVA( importDesc->Name, peHeader ) );

		if ( !Q_stricmp( importName, depname ) )
		{
			COM_FreeLibrary( hInst );
			Mem_Free( data );
			return TRUE;
		}
	}

	COM_FreeLibrary( hInst );
	Mem_Free( data );
	return FALSE;
}

/*
================
COM_LoadLibrary

smart dll loader - can loading dlls from pack or wad files
================
*/
void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	dll_user_t *hInst;
	char buf[MAX_VA_STRING];

	COM_ResetLibraryError();

	hInst = FS_FindLibrary( dllname, directpath );
	if( !hInst )
	{
		Q_snprintf( buf, sizeof( buf ), "Failed to find library %s", dllname );
		COM_PushLibraryError( buf );
		return NULL;
	}

	if( hInst->encrypted )
	{
		Q_snprintf( buf, sizeof( buf ), "Library %s is encrypted, cannot load", hInst->shortPath );
		COM_PushLibraryError( buf );
		COM_FreeLibrary( hInst );
		return NULL;
	}

#if XASH_X86
	if( hInst->custom_loader )
	{
		hInst->hInstance = MemoryLoadLibrary( hInst->fullPath );
	}
	else
#endif
	{
		hInst->hInstance = LoadLibraryW( FS_PathToWideChar( hInst->fullPath ));
	}

	if( !hInst->hInstance )
	{
		COM_PushLibraryError( GetLastErrorAsString() );

		if ( GetLastError() == ERROR_MOD_NOT_FOUND )
			ListMissingModules( hInst );

		COM_FreeLibrary( hInst );
		return NULL;
	}

	// if not set - FunctionFromName and NameForFunction will not working
	if( build_ordinals_table )
	{
		if( !LibraryLoadSymbols( hInst ))
		{
			Q_snprintf( buf, sizeof( buf ), "Failed to load library %s", dllname );
			COM_PushLibraryError( buf );
			COM_FreeLibrary( hInst );
			return NULL;
		}
	}

	return hInst;
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
	dll_user_t *hInst = (dll_user_t *)hInstance;

	if( !hInst || !hInst->hInstance )
		return NULL;

#if XASH_X86
	if( hInst->custom_loader )
		return (void *)MemoryGetProcAddress( hInst->hInstance, name );
#endif
	return (void *)GetProcAddress( hInst->hInstance, name );
}

void COM_FreeLibrary( void *hInstance )
{
	dll_user_t *hInst = (dll_user_t *)hInstance;

	if( !hInst || !hInst->hInstance )
		return; // already freed

	if( host.status == HOST_CRASHED )
	{
		// we need to hold down all modules, while MSVC can find error
		Con_Reportf( "%s: hold %s for debugging\n", __func__, hInst->dllName );
		return;
	}
	else Con_Reportf( "%s: Unloading %s\n", __func__, hInst->dllName );

#if XASH_X86
	if( hInst->custom_loader )
	{
		MemoryFreeLibrary( hInst->hInstance );
	}
	else
#endif
	{
		FreeLibrary( hInst->hInstance );
	}

	hInst->hInstance = NULL;

	if( hInst->num_ordinals )
		FreeNameFuncGlobals( hInst );
	Mem_Free( hInst );	// done
}

void *COM_FunctionFromName( void *hInstance, const char *pName )
{
	dll_user_t	*hInst = (dll_user_t *)hInstance;
	int		i, index;

	if( !hInst || !hInst->hInstance )
		return 0;

	for( i = 0; i < hInst->num_ordinals; i++ )
	{
		if( !Q_strcmp( pName, hInst->names[i] ))
		{
			index = hInst->ordinals[i];
			return (void *)( hInst->funcs[index] + hInst->funcBase );
		}
	}

	// couldn't find the function name to return address
	Con_Printf( "Can't find proc: %s\n", pName );

	return 0;
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	dll_user_t	*hInst = (dll_user_t *)hInstance;
	int		i, index;

	if( !hInst || !hInst->hInstance )
		return NULL;

	for( i = 0; i < hInst->num_ordinals; i++ )
	{
		index = hInst->ordinals[i];

		if(( (char*)function - (char*)hInst->funcBase ) == hInst->funcs[index] )
			return hInst->names[i];
	}

	// couldn't find the function address to return name
	Con_Printf( "Can't find address: %08lx\n", function );

	return NULL;
}
#endif // _WIN32
