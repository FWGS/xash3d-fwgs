/*
lib_custom_win.c - win32 custom dlls loader
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

#if XASH_LIB == LIB_WIN32 && XASH_X86
#include "lib_win.h"

#define NUMBER_OF_DIRECTORY_ENTRIES	16
#ifndef IMAGE_SIZEOF_BASE_RELOCATION
#define IMAGE_SIZEOF_BASE_RELOCATION	( sizeof( IMAGE_BASE_RELOCATION ))
#endif

typedef struct
{
	PIMAGE_NT_HEADERS	headers;
	byte		*codeBase;
	void		**modules;
	int		numModules;
	int		initialized;
} MEMORYMODULE, *PMEMORYMODULE;

// Protection flags for memory pages (Executable, Readable, Writeable)
static int ProtectionFlags[2][2][2] =
{
{
{ PAGE_NOACCESS, PAGE_WRITECOPY },		// not executable
{ PAGE_READONLY, PAGE_READWRITE },
},
{
{ PAGE_EXECUTE, PAGE_EXECUTE_WRITECOPY },	// executable
{ PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE },
},
};

typedef BOOL (WINAPI *DllEntryProc)( HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved );

#define GET_HEADER_DICTIONARY( module, idx )	&(module)->headers->OptionalHeader.DataDirectory[idx]

static void CopySections( const byte *data, PIMAGE_NT_HEADERS old_headers, PMEMORYMODULE module )
{
	PIMAGE_SECTION_HEADER	section = IMAGE_FIRST_SECTION( module->headers );
	byte			*codeBase = module->codeBase;
	int			i, size;
	byte			*dest;

	for( i = 0; i < module->headers->FileHeader.NumberOfSections; i++, section++ )
	{
		if( section->SizeOfRawData == 0 )
		{
			// section doesn't contain data in the dll itself, but may define
			// uninitialized data
			size = old_headers->OptionalHeader.SectionAlignment;

			if( size > 0 )
			{
				dest = (byte *)VirtualAlloc((byte *)CALCULATE_ADDRESS(codeBase, section->VirtualAddress), size, MEM_COMMIT, PAGE_READWRITE );
				section->Misc.PhysicalAddress = (DWORD)dest;
				memset( dest, 0, size );
			}
			// section is empty
			continue;
		}

		// commit memory block and copy data from dll
		dest = (byte *)VirtualAlloc((byte *)CALCULATE_ADDRESS(codeBase, section->VirtualAddress), section->SizeOfRawData, MEM_COMMIT, PAGE_READWRITE );
		memcpy( dest, (byte *)CALCULATE_ADDRESS(data, section->PointerToRawData), section->SizeOfRawData );
		section->Misc.PhysicalAddress = (DWORD)dest;
	}
}

static void FreeSections( PIMAGE_NT_HEADERS old_headers, PMEMORYMODULE module )
{
	PIMAGE_SECTION_HEADER	section = IMAGE_FIRST_SECTION(module->headers);
	byte			*codeBase = module->codeBase;
	int			i, size;

	for( i = 0; i < module->headers->FileHeader.NumberOfSections; i++, section++ )
	{
		if( section->SizeOfRawData == 0 )
		{
			size = old_headers->OptionalHeader.SectionAlignment;
			if( size > 0 )
			{
				VirtualFree((byte *)CALCULATE_ADDRESS( codeBase, section->VirtualAddress ), size, MEM_DECOMMIT );
				section->Misc.PhysicalAddress = 0;
			}
			continue;
		}

		VirtualFree((byte *)CALCULATE_ADDRESS( codeBase, section->VirtualAddress ), section->SizeOfRawData, MEM_DECOMMIT );
		section->Misc.PhysicalAddress = 0;
	}
}

static void FinalizeSections( MEMORYMODULE *module )
{
	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION( module->headers );
	int	i;

	// loop through all sections and change access flags
	for( i = 0; i < module->headers->FileHeader.NumberOfSections; i++, section++ )
	{
		DWORD	protect, oldProtect, size;
		int	executable = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
		int	readable = (section->Characteristics & IMAGE_SCN_MEM_READ) != 0;
		int	writeable = (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;

		if( section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE )
		{
			// section is not needed any more and can safely be freed
			VirtualFree((LPVOID)section->Misc.PhysicalAddress, section->SizeOfRawData, MEM_DECOMMIT);
			continue;
		}

		// determine protection flags based on characteristics
		protect = ProtectionFlags[executable][readable][writeable];
		if( section->Characteristics & IMAGE_SCN_MEM_NOT_CACHED )
			protect |= PAGE_NOCACHE;

		// determine size of region
		size = section->SizeOfRawData;

		if( size == 0 )
		{
			if( section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA )
				size = module->headers->OptionalHeader.SizeOfInitializedData;
			else if( section->Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA )
				size = module->headers->OptionalHeader.SizeOfUninitializedData;
		}

		if( size > 0 )
		{
			// change memory access flags
			if( !VirtualProtect((LPVOID)section->Misc.PhysicalAddress, size, protect, &oldProtect ))
				Sys_Error( "error protecting memory page\n" );
		}
	}
}

static void PerformBaseRelocation( MEMORYMODULE *module, DWORD delta )
{
	PIMAGE_DATA_DIRECTORY	directory = GET_HEADER_DICTIONARY( module, IMAGE_DIRECTORY_ENTRY_BASERELOC );
	byte			*codeBase = module->codeBase;
	DWORD			i;

	if( directory->Size > 0 )
	{
		PIMAGE_BASE_RELOCATION relocation = (PIMAGE_BASE_RELOCATION)CALCULATE_ADDRESS( codeBase, directory->VirtualAddress );
		for( ; relocation->VirtualAddress > 0; )
		{
			byte	*dest = (byte *)CALCULATE_ADDRESS( codeBase, relocation->VirtualAddress );
			word	*relInfo = (word *)((byte *)relocation + IMAGE_SIZEOF_BASE_RELOCATION );

			for( i = 0; i<((relocation->SizeOfBlock-IMAGE_SIZEOF_BASE_RELOCATION) / 2); i++, relInfo++ )
			{
				DWORD	*patchAddrHL;
				int	type, offset;

				// the upper 4 bits define the type of relocation
				type = *relInfo >> 12;
				// the lower 12 bits define the offset
				offset = *relInfo & 0xfff;

				switch( type )
				{
				case IMAGE_REL_BASED_ABSOLUTE:
					// skip relocation
					break;
				case IMAGE_REL_BASED_HIGHLOW:
					// change complete 32 bit address
					patchAddrHL = (DWORD *)CALCULATE_ADDRESS( dest, offset );
					*patchAddrHL += delta;
					break;
				default:
					Con_Reportf( S_ERROR "PerformBaseRelocation: unknown relocation: %d\n", type );
					break;
				}
			}

			// advance to next relocation block
			relocation = (PIMAGE_BASE_RELOCATION)CALCULATE_ADDRESS( relocation, relocation->SizeOfBlock );
		}
	}
}

FARPROC MemoryGetProcAddress( void *module, const char *name )
{
	PIMAGE_DATA_DIRECTORY	directory = GET_HEADER_DICTIONARY((MEMORYMODULE *)module, IMAGE_DIRECTORY_ENTRY_EXPORT );
	byte			*codeBase = ((PMEMORYMODULE)module)->codeBase;
	PIMAGE_EXPORT_DIRECTORY	exports;
	int			idx = -1;
	DWORD			i, *nameRef;
	WORD			*ordinal;

	if( directory->Size == 0 )
	{
		// no export table found
		return NULL;
	}

	exports = (PIMAGE_EXPORT_DIRECTORY)CALCULATE_ADDRESS( codeBase, directory->VirtualAddress );

	if( exports->NumberOfNames == 0 || exports->NumberOfFunctions == 0 )
	{
		// DLL doesn't export anything
		return NULL;
	}

	// search function name in list of exported names
	nameRef = (DWORD *)CALCULATE_ADDRESS( codeBase, exports->AddressOfNames );
	ordinal = (WORD *)CALCULATE_ADDRESS( codeBase, exports->AddressOfNameOrdinals );

	for( i = 0; i < exports->NumberOfNames; i++, nameRef++, ordinal++ )
	{
		// GetProcAddress case insensative ?????
		if( !Q_stricmp( name, (const char *)CALCULATE_ADDRESS( codeBase, *nameRef )))
		{
			idx = *ordinal;
			break;
		}
	}

	if( idx == -1 )
	{
		// exported symbol not found
		return NULL;
	}

	if((DWORD)idx > exports->NumberOfFunctions )
	{
		// name <-> ordinal number don't match
		return NULL;
	}

	// addressOfFunctions contains the RVAs to the "real" functions
	return (FARPROC)CALCULATE_ADDRESS( codeBase, *(DWORD *)CALCULATE_ADDRESS( codeBase, exports->AddressOfFunctions + (idx * 4)));
}

static int BuildImportTable( MEMORYMODULE *module )
{
	PIMAGE_DATA_DIRECTORY	directory = GET_HEADER_DICTIONARY( module, IMAGE_DIRECTORY_ENTRY_IMPORT );
	byte			*codeBase = module->codeBase;
	int			result = 1;

	if( directory->Size > 0 )
	{
		PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)CALCULATE_ADDRESS( codeBase, directory->VirtualAddress );

		for( ; !IsBadReadPtr( importDesc, sizeof( IMAGE_IMPORT_DESCRIPTOR )) && importDesc->Name; importDesc++ )
		{
			DWORD	*thunkRef, *funcRef;
			LPCSTR	libname;
			void	*handle;

			libname = (LPCSTR)CALCULATE_ADDRESS( codeBase, importDesc->Name );
			handle = COM_LoadLibrary( libname, false, true );

			if( handle == NULL )
			{
				Con_Printf( S_ERROR "couldn't load library %s\n", libname );
				result = 0;
				break;
			}

			module->modules = (void *)Mem_Realloc( host.mempool, module->modules, (module->numModules + 1) * (sizeof( void* )));
			module->modules[module->numModules++] = handle;

			if( importDesc->OriginalFirstThunk )
			{
				thunkRef = (DWORD *)CALCULATE_ADDRESS( codeBase, importDesc->OriginalFirstThunk );
				funcRef = (DWORD *)CALCULATE_ADDRESS( codeBase, importDesc->FirstThunk );
			}
			else
			{
				// no hint table
				thunkRef = (DWORD *)CALCULATE_ADDRESS( codeBase, importDesc->FirstThunk );
				funcRef = (DWORD *)CALCULATE_ADDRESS( codeBase, importDesc->FirstThunk );
			}

			for( ; *thunkRef; thunkRef++, funcRef++ )
			{
				LPCSTR	funcName;

				if( IMAGE_SNAP_BY_ORDINAL( *thunkRef ))
				{
					funcName = (LPCSTR)IMAGE_ORDINAL( *thunkRef );
					*funcRef = (DWORD)COM_GetProcAddress( handle, funcName );
				}
				else
				{
					PIMAGE_IMPORT_BY_NAME thunkData = (PIMAGE_IMPORT_BY_NAME)CALCULATE_ADDRESS( codeBase, *thunkRef );
					funcName = (LPCSTR)&thunkData->Name;
					*funcRef = (DWORD)COM_GetProcAddress( handle, funcName );
				}

				if( *funcRef == 0 )
				{
					Con_Printf( S_ERROR "%s unable to find address: %s\n", libname, funcName );
					result = 0;
					break;
				}
			}
			if( !result ) break;
		}
	}
	return result;
}

void MemoryFreeLibrary( void *hInstance )
{
	MEMORYMODULE	*module = (MEMORYMODULE *)hInstance;

	if( module != NULL )
	{
		int	i;

		if( module->initialized != 0 )
		{
			// notify library about detaching from process
			DllEntryProc DllEntry = (DllEntryProc)CALCULATE_ADDRESS( module->codeBase, module->headers->OptionalHeader.AddressOfEntryPoint );
			(*DllEntry)((HINSTANCE)module->codeBase, DLL_PROCESS_DETACH, 0 );
			module->initialized = 0;
		}

		if( module->modules != NULL )
		{
			// free previously opened libraries
			for( i = 0; i < module->numModules; i++ )
			{
				if( module->modules[i] != NULL )
					COM_FreeLibrary( module->modules[i] );
			}
			Mem_Free( module->modules ); // Mem_Realloc end
		}

		FreeSections( module->headers, module );

		if( module->codeBase != NULL )
		{
			// release memory of library
			VirtualFree( module->codeBase, 0, MEM_RELEASE );
		}

		HeapFree( GetProcessHeap(), 0, module );
	}
}

void *MemoryLoadLibrary( const char *name )
{
	MEMORYMODULE	*result = NULL;
	PIMAGE_DOS_HEADER	dos_header;
	PIMAGE_NT_HEADERS	old_header;
	byte		*code, *headers;
	DWORD		locationDelta;
	DllEntryProc	DllEntry;
	string		errorstring;
	qboolean		successfull;
	void		*data = NULL;

	data = FS_LoadFile( name, NULL, false );

	if( !data )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "couldn't load %s", name );
		goto library_error;
	}

	dos_header = (PIMAGE_DOS_HEADER)data;
	if( dos_header->e_magic != IMAGE_DOS_SIGNATURE )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s it's not a valid executable file", name );
		goto library_error;
	}

	old_header = (PIMAGE_NT_HEADERS)&((const byte *)(data))[dos_header->e_lfanew];
	if( old_header->Signature != IMAGE_NT_SIGNATURE )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s missing PE header", name );
		goto library_error;
	}

	// reserve memory for image of library
	code = (byte *)VirtualAlloc((LPVOID)(old_header->OptionalHeader.ImageBase), old_header->OptionalHeader.SizeOfImage, MEM_RESERVE, PAGE_READWRITE );

	if( code == NULL )
	{
		// try to allocate memory at arbitrary position
		code = (byte *)VirtualAlloc( NULL, old_header->OptionalHeader.SizeOfImage, MEM_RESERVE, PAGE_READWRITE );
	}

	if( code == NULL )
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s can't reserve memory", name );
		goto library_error;
	}

	result = (MEMORYMODULE *)HeapAlloc( GetProcessHeap(), 0, sizeof( MEMORYMODULE ));
	result->codeBase = code;
	result->numModules = 0;
	result->modules = NULL;
	result->initialized = 0;

	// XXX: is it correct to commit the complete memory region at once?
	// calling DllEntry raises an exception if we don't...
	VirtualAlloc( code, old_header->OptionalHeader.SizeOfImage, MEM_COMMIT, PAGE_READWRITE );

	// commit memory for headers
	headers = (byte *)VirtualAlloc( code, old_header->OptionalHeader.SizeOfHeaders, MEM_COMMIT, PAGE_READWRITE );

	// copy PE header to code
	memcpy( headers, dos_header, dos_header->e_lfanew + old_header->OptionalHeader.SizeOfHeaders );
	result->headers = (PIMAGE_NT_HEADERS)&((const byte *)(headers))[dos_header->e_lfanew];

	// update position
	result->headers->OptionalHeader.ImageBase = (DWORD)code;

	// copy sections from DLL file block to new memory location
	CopySections( data, old_header, result );

	// adjust base address of imported data
	locationDelta = (DWORD)(code - old_header->OptionalHeader.ImageBase);
	if( locationDelta != 0 ) PerformBaseRelocation( result, locationDelta );

	// load required dlls and adjust function table of imports
	if( !BuildImportTable( result ))
	{
		Q_snprintf( errorstring, sizeof( errorstring ), "%s failed to build import table", name );
		goto library_error;
	}

	// mark memory pages depending on section headers and release
	// sections that are marked as "discardable"
	FinalizeSections( result );

	// get entry point of loaded library
	if( result->headers->OptionalHeader.AddressOfEntryPoint != 0 )
	{
		DllEntry = (DllEntryProc)CALCULATE_ADDRESS( code, result->headers->OptionalHeader.AddressOfEntryPoint );
		if( DllEntry == 0 )
		{
			Q_snprintf( errorstring, sizeof( errorstring ), "%s has no entry point", name );
			goto library_error;
		}

		// notify library about attaching to process
		successfull = (*DllEntry)((HINSTANCE)code, DLL_PROCESS_ATTACH, 0 );
		if( !successfull )
		{
			Q_snprintf( errorstring, sizeof( errorstring ), "can't attach library %s", name );
			goto library_error;
		}
		result->initialized = 1;
	}

	Mem_Free( data ); // release memory
	return (void *)result;
library_error:
	// cleanup
	if( data ) Mem_Free( data );
	MemoryFreeLibrary( result );
	Con_Printf( S_ERROR "LoadLibrary: %s\n", errorstring );

	return NULL;
}

#endif // XASH_LIB == LIB_WIN32 && XASH_X86
