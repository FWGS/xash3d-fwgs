/*
lib_posix.c - dynamic library code for POSIX systems
Copyright (C) 2018 Flying With Gauss

This program is free software: you can redistribute it and/sor modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform/platform.h"
#if XASH_LIB == LIB_POSIX

#if XASH_NSWITCH
	#define SOLDER_LIBDL_COMPAT
	#include <solder.h>
#elif XASH_PSVITA
	#define VRTLD_LIBDL_COMPAT
	#include <vrtld.h>
#else
	#include <dlfcn.h>
#endif

#ifdef XASH_IRIX
#include "platform/irix/dladdr.h"
#endif
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"
#include "platform/android/lib_android.h"

#ifdef XASH_NO_LIBDL
void *dlsym( void *handle, const char *symbol )
{
	Con_DPrintf( "%s( %p, \"%s\" ): stub\n", __func__, handle, symbol );
	return NULL;
}

void *dlopen( const char *name, int flag )
{
	Con_DPrintf( "%s( \"%s\", %d ): stub\n", __func__, name, flag );
	return NULL;
}

int dlclose( void *handle )
{
	Con_DPrintf( "%s( %p ): stub\n", __func__, handle );
	return 0;
}

char *dlerror( void )
{
	return "Loading ELF libraries not supported in this build!\n";
}

int dladdr( const void *addr, Dl_info *info )
{
	return 0;
}
#endif // XASH_NO_LIBDL

#if !XASH_APPLE && !XASH_ANDROID && !XASH_PSVITA && !XASH_NSWITCH
/*
=============================================================================

	DIRECT DEPENDENCY CHECK

We only parse enough of the binary to walk its list of direct dependencies.
The parser is byte order, machine and word size agnostic, so a 64-bit engine can inspect a 32-bit game library and vice versa.

=============================================================================
*/
#define ELFCLASS32  1
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_STRTAB   5
#define DT_STRSZ    10

typedef struct
{
	const byte *data;
	uint64_t    size;
	qboolean    be;   // file is big endian
	qboolean    is64; // file is ELFCLASS64
} elf_file_t;

static qboolean ELF_ReadField( const elf_file_t *elf, uint64_t offset, size_t size, uint64_t *out )
{
	if( size > sizeof( *out ) || offset > elf->size || elf->size - offset < size )
		return false;

	const byte *p = elf->data + offset;
	uint64_t val = 0;

	if( elf->be )
	{
		for( size_t i = 0; i < size; i++ )
			val = ( val << 8 ) | p[i];
	}
	else
	{
		for( size_t i = size; i-- > 0; )
			val = ( val << 8 ) | p[i];
	}

	*out = val;

	return true;
}

// reads a field that's a dword in ELF32 and a qword in ELF64
static qboolean ELF_ReadWord( const elf_file_t *elf, uint64_t offset, uint64_t *out )
{
	return ELF_ReadField( elf, offset, elf->is64 ? 8 : 4, out );
}

static qboolean ELF_ReadPhdr( const elf_file_t *elf, uint64_t off, uint64_t *type, uint64_t *offset, uint64_t *vaddr, uint64_t *filesz )
{
	const uint ptrsz = elf->is64 ? 8 : 4;

	if( !ELF_ReadField( elf, off, 4, type ))
		return false;

	return ELF_ReadWord( elf, off + ptrsz, offset )
		&& ELF_ReadWord( elf, off + ptrsz * 2, vaddr )
		&& ELF_ReadWord( elf, off + ptrsz * 4, filesz );
}

// DT_ entries refer to virtual addresses, but we're looking at a file on disk
static qboolean ELF_VirtualToFileOffset( const elf_file_t *elf, uint64_t phoff, uint64_t phentsize, uint64_t phnum, uint64_t vaddr, uint64_t *out )
{
	for( uint64_t i = 0; i < phnum; i++ )
	{
		uint64_t type, offset, base, filesz;

		if( !ELF_ReadPhdr( elf, phoff + i * phentsize, &type, &offset, &base, &filesz ))
			return false;

		if( type != PT_LOAD || vaddr < base || vaddr - base >= filesz )
			continue;

		*out = offset + ( vaddr - base );

		return true;
	}

	return false;
}

qboolean Platform_CheckLibraryDirectDependency( const byte *data, size_t size, const char *depname )
{
	// "\177ELF" is deliberately octal: a hex escape would swallow the E as a digit
	if( size < 64 || memcmp( data, "\177ELF", 4 ))
		return false;

	// we only know these two classes and two encodings
	if(( data[4] != ELFCLASS32 && data[4] != ELFCLASS64 )
		|| ( data[5] != ELFDATA2LSB && data[5] != ELFDATA2MSB ))
		return false;

	const elf_file_t elf =
	{
		.data = data,
		.size = size,
		.be   = data[5] == ELFDATA2MSB,
		.is64 = data[4] == ELFCLASS64,
	};
	uint64_t phoff, phentsize, phnum;

	if( !ELF_ReadWord( &elf, elf.is64 ? 32 : 28, &phoff )
		|| !ELF_ReadField( &elf, elf.is64 ? 54 : 42, 2, &phentsize )
		|| !ELF_ReadField( &elf, elf.is64 ? 56 : 44, 2, &phnum ))
		return false;

	// refuse to walk a table with entries smaller than the header we know about
	if( !phnum || phentsize < ( elf.is64 ? 56 : 32 ))
		return false;

	uint64_t dynoff = 0, dynsize = 0;
	qboolean found_dynamic = false;

	for( uint64_t i = 0; i < phnum; i++ )
	{
		uint64_t type, offset, vaddr, filesz;

		if( !ELF_ReadPhdr( &elf, phoff + i * phentsize, &type, &offset, &vaddr, &filesz ))
			return false;

		if( type != PT_DYNAMIC )
			continue;

		dynoff = offset;
		dynsize = filesz;
		found_dynamic = true;
		break;
	}

	if( !found_dynamic || dynoff >= size )
		return false; // statically linked, nothing to look at

	if( size - dynoff < dynsize )
		dynsize = size - dynoff; // truncated file, never read past the end

	const uint64_t dynvaloff = elf.is64 ? 8 : 4;
	const uint64_t dynentsize = dynvaloff * 2;
	const uint64_t dynend = dynoff + dynsize;

	// first pass, find the dynamic string table
	uint64_t strtab = 0, strsz = 0, stroff;
	qboolean found_strtab = false;

	for( uint64_t off = dynoff; off + dynentsize <= dynend; off += dynentsize )
	{
		uint64_t tag, val;

		if( !ELF_ReadWord( &elf, off, &tag ) || !ELF_ReadWord( &elf, off + dynvaloff, &val ))
			return false;

		if( tag == DT_NULL )
			break;

		if( tag == DT_STRTAB )
		{
			strtab = val;
			found_strtab = true;
		}
		else if( tag == DT_STRSZ )
			strsz = val;
	}

	if( !found_strtab || !strsz )
		return false;

	if( !ELF_VirtualToFileOffset( &elf, phoff, phentsize, phnum, strtab, &stroff ) || stroff >= size )
		return false;

	if( size - stroff < strsz )
		strsz = size - stroff;

	// second pass, walk the dependencies
	for( uint64_t off = dynoff; off + dynentsize <= dynend; off += dynentsize )
	{
		uint64_t tag, val;

		if( !ELF_ReadWord( &elf, off, &tag ) || !ELF_ReadWord( &elf, off + dynvaloff, &val ))
			return false;

		if( tag == DT_NULL )
			break;

		if( tag != DT_NEEDED || val >= strsz )
			continue;

		const char *dep = (const char *)( data + stroff + val );

		// the string table is supposed to be NUL terminated, but don't trust the file
		if( !memchr( dep, '\0', (size_t)( strsz - val )))
			continue;

		// DT_NEEDED normally holds a bare SONAME, but it can be a path if the
		// library was linked without -soname
		if( !Q_strcmp( dep, depname ) || !Q_strcmp( COM_FileWithoutPath( dep ), depname ))
			return true;
	}

	return false;
}
#endif // !XASH_APPLE && !XASH_ANDROID && !XASH_PSVITA && !XASH_NSWITCH

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	COM_ResetLibraryError();

	// platforms where gameinfo mechanism is impossible
#ifdef Platform_POSIX_LoadLibrary
	return Platform_POSIX_LoadLibrary( dllname );
#endif

	// platforms where gameinfo mechanism is working goes here
	// and use FS_FindLibrary
	dll_user_t *hInst = FS_FindLibrary( dllname, directpath );
	char buf[MAX_VA_STRING];

	if( !hInst )
	{
		// try to find by linker(LD_LIBRARY_PATH, DYLD_LIBRARY_PATH, LD_32_LIBRARY_PATH and so on...)
		void *pHandle = dlopen( dllname, RTLD_NOW );
		if( pHandle )
			return pHandle;

		Q_snprintf( buf, sizeof( buf ), "Failed to find library %s", dllname );
		COM_PushLibraryError( buf );
		COM_PushLibraryError( dlerror() );
		return NULL;
	}

	if( hInst->custom_loader )
	{
		Q_snprintf( buf, sizeof( buf ), "Custom library loader is not available. Extract library %s and fix gameinfo.txt!", hInst->fullPath );
		COM_PushLibraryError( buf );
		Mem_Free( hInst );
		return NULL;
	}

	if( !( hInst->hInstance = dlopen( hInst->fullPath, RTLD_NOW ) ) )
	{
		COM_PushLibraryError( dlerror() );
		Mem_Free( hInst );
		return NULL;
	}

	void *pHandle = hInst->hInstance;

	Mem_Free( hInst );

	return pHandle;
}

void COM_FreeLibrary( void *hInstance )
{
#ifdef Platform_POSIX_FreeLibrary
	Platform_POSIX_FreeLibrary( hInstance );
#else
	dlclose( hInstance );
#endif
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
#if Platform_POSIX_GetProcAddress
	return Platform_POSIX_GetProcAddress( hInstance, name );
#else
	return dlsym( hInstance, name );
#endif
}

void *COM_GetProcAddressFromDependency( void *hInstance, const char *depname, const char *name )
{
	// dlsym walks the dependency tree of the handle, no need to look the dependency up by name
	return dlsym( hInstance, name );
}

void *COM_FunctionFromName( void *hInstance, const char *pName )
{
	return COM_GetProcAddress( hInstance, pName );
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	// NOTE: dladdr() is a glibc extension
	Dl_info info = {0};
	int ret = dladdr( (void*)function, &info );
	if( ret && info.dli_sname )
		return COM_GetPlatformNeutralName( info.dli_sname );

#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	return COM_OffsetNameForFunction( function );
#else
	return NULL;
#endif
}

#endif // _WIN32
