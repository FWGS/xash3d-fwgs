/*
lib_ps4.c - PlayStation 4 dynamic library loading
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <orbis/libkernel.h>

// application package mount point
#define PS4_APP_DIR "/app0"

/*
PS4 modules (.prx) are loaded by the kernel, there is no dlopen in libc.
Handles are kernel module ids, offset by 1 so 0 is never a valid handle.

Modules have no symbol names (the loader works with hashes), but the engine needs the names
of game functions to write savegames. Build scripts put them next to every game library:
<library>.prx.sym has a line "<offset in hex> <name>" for every function of the library,
the offset is counted from the start of the module, like in crash logs
*/
typedef struct
{
	uintptr_t offset;
	char *name;
} ps4_symbol_t;

typedef struct
{
	int32_t handle;
	char *path;
	ps4_symbol_t *symbols; // sorted by offset
	size_t count;
} ps4_symtab_t;

#define PS4_MAX_SYMTABS 16

static ps4_symtab_t ps4_symtabs[PS4_MAX_SYMTABS];

static int PS4_CompareSymbols( const void *a, const void *b )
{
	const ps4_symbol_t *sa = a, *sb = b;

	return ( sa->offset > sb->offset ) - ( sa->offset < sb->offset );
}

static void PS4_LibraryError( const char *fmt, ... ) FORMAT_CHECK( 1 );
static void PS4_LibraryError( const char *fmt, ... )
{
	char buf[512];
	va_list va;

	va_start( va, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	PS4_Log( "library: %s\n", buf );
	COM_PushLibraryError( buf );
}

static qboolean PS4_FileExists( const char *path )
{
	struct stat st;

	return stat( path, &st ) == 0 && S_ISREG( st.st_mode );
}

// modules shipped in the package root: engine modules and, when packaged
// together with the engine, game libraries (only their file name counts)
qboolean PS4_PackageLibraryExists( const char *name )
{
	char path[512];

	if( !name || !name[0] )
		return false;

	Q_snprintf( path, sizeof( path ), "%s/%s", PS4_APP_DIR, COM_FileWithoutPath( name ));
	return PS4_FileExists( path );
}

static void PS4_LoadSymbols( int32_t handle, const char *path )
{
	char symfile[560];
	size_t size;
	char *data, *p;
	ps4_symtab_t *tab = NULL;

	for( size_t i = 0; i < PS4_MAX_SYMTABS && !tab; i++ )
	{
		if( !ps4_symtabs[i].path )
			tab = &ps4_symtabs[i];
	}

	if( !tab )
		return;

	Q_snprintf( symfile, sizeof( symfile ), "%s.sym", path );

	FILE *f = fopen( symfile, "rb" );
	if( !f )
		return;

	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );

	data = malloc( size + 1 );
	if( !data || fread( data, 1, size, f ) != size )
	{
		free( data );
		fclose( f );
		return;
	}

	fclose( f );
	data[size] = 0;

	size_t lines = 0;
	for( p = data; *p; p++ )
	{
		if( *p == '\n' )
			lines++;
	}

	tab->symbols = calloc( lines + 1, sizeof( *tab->symbols ));
	if( !tab->symbols )
	{
		free( data );
		return;
	}

	for( p = data; *p; )
	{
		char *end;
		uintptr_t offset = strtoull( p, &end, 16 );
		char *name = end;
		char *eol = strchr( p, '\n' );

		if( eol )
			*eol = 0;

		while( *name == ' ' )
			name++;

		if( end != p && *name )
		{
			tab->symbols[tab->count].offset = offset;
			tab->symbols[tab->count].name = strdup( name );
			tab->count++;
		}

		if( !eol )
			break;

		p = eol + 1;
	}

	free( data );
	qsort( tab->symbols, tab->count, sizeof( *tab->symbols ), PS4_CompareSymbols );
	tab->handle = handle;
	tab->path = strdup( path );

	PS4_Log( "%s: %d symbols\n", symfile, (int)tab->count );
}

static void PS4_FreeSymbols( int32_t handle )
{
	for( size_t i = 0; i < PS4_MAX_SYMTABS; i++ )
	{
		ps4_symtab_t *tab = &ps4_symtabs[i];

		if( !tab->path || tab->handle != handle )
			continue;

		for( size_t j = 0; j < tab->count; j++ )
			free( tab->symbols[j].name );

		free( tab->symbols );
		free( tab->path );
		memset( tab, 0, sizeof( *tab ));
	}
}

static void *PS4_LoadModule( const char *name )
{
	char path[512];
	int32_t handle;

	if( name[0] == '/' )
	{
		Q_strncpy( path, name, sizeof( path ));
	}
	else
	{
		// engine modules (filesystem, menu, renderers) live in the package root, game
		// libraries too when they are packaged; libraries in the game directory are
		// found by the engine before it gets here, so they override packaged ones
		const char *base = COM_FileWithoutPath( name );

		Q_snprintf( path, sizeof( path ), "%s/%s", PS4_APP_DIR, base );

		if( !PS4_FileExists( path ))
		{
			char basedir[256];

			PS4_GetBasePath( basedir, sizeof( basedir ));
			Q_snprintf( path, sizeof( path ), "%s/%s", basedir, name );
		}
	}

	if( !PS4_FileExists( path ))
	{
		PS4_LibraryError( "%s: file not found", path );
		return NULL;
	}

	handle = (int32_t)sceKernelLoadStartModule( path, 0, NULL, 0, NULL, NULL );

	if( handle < 0 )
	{
		PS4_LibraryError( "%s: sceKernelLoadStartModule failed: 0x%08x", path, (uint32_t)handle );
		return NULL;
	}

	PS4_Log( "loaded module %s: 0x%x\n", path, handle );

	// run global constructors, our startup code exports this, see 3rdparty/ps4compat/ps4_crtlib.c
	{
		void ( *init )( void ) = NULL;

		int ( *shared )( void ) = NULL;

		if( sceKernelDlsym( handle, "__ps4_module_init", (void **)&init ) >= 0 && init )
			init( );
		else
			PS4_Log( "warning: %s has no __ps4_module_init, global constructors may not run\n", path );

		// see 3rdparty/ps4compat/ps4_malloc.c
		if( sceKernelDlsym( handle, "__ps4_heap_shared", (void **)&shared ) >= 0 && shared )
			PS4_Log( "%s: %s heap\n", path, shared( ) ? "shared" : "own" );
	}

	PS4_LoadSymbols( handle, path );

	return (void *)(intptr_t)( handle + 1 );
}

// returns the name of a function in a module loaded by us, NULL if there is no .sym file
static const char *PS4_NameForAddress( const void *addr )
{
	for( size_t i = 0; i < PS4_MAX_SYMTABS; i++ )
	{
		const ps4_symtab_t *tab = &ps4_symtabs[i];
		OrbisKernelModuleInfo module = { .size = sizeof( module ) };

		if( !tab->path || sceKernelGetModuleInfo( tab->handle, &module ) < 0 )
			continue;

		const uintptr_t base = (uintptr_t)module.segmentInfo[0].address;
		const uintptr_t offset = (uintptr_t)addr - base;
		const ps4_symbol_t key = { .offset = offset };

		if( (uintptr_t)addr < base )
			continue;

		const ps4_symbol_t *sym = bsearch( &key, tab->symbols, tab->count, sizeof( *tab->symbols ), PS4_CompareSymbols );
		if( sym )
			return sym->name;
	}

	return NULL;
}

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	dll_user_t *hInst;
	void *handle;

	COM_ResetLibraryError();

	if(( hInst = FS_FindLibrary( dllname, directpath )) == NULL )
	{
		char buf[MAX_VA_STRING];

		// not in the game directory, try modules from the package
		if(( handle = PS4_LoadModule( dllname )) != NULL )
			return handle;

		Q_snprintf( buf, sizeof( buf ), "Failed to find library %s", dllname );
		COM_PushLibraryError( buf );
		return NULL;
	}

	if( hInst->custom_loader )
	{
		char buf[MAX_VA_STRING];

		Q_snprintf( buf, sizeof( buf ), "Custom library loader is not available. Extract library %s and fix gameinfo.txt!", hInst->fullPath );
		COM_PushLibraryError( buf );
		Mem_Free( hInst );
		return NULL;
	}

	handle = PS4_LoadModule( hInst->fullPath );
	Mem_Free( hInst );

	return handle;
}

void COM_FreeLibrary( void *hInstance )
{
	int32_t res = 0;

	if( !hInstance )
		return;

	PS4_FreeSymbols(( int32_t )( (intptr_t)hInstance - 1 ));
	sceKernelStopUnloadModule( (OrbisKernelModule)( (intptr_t)hInstance - 1 ), 0, NULL, 0, NULL, &res );
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
	void *addr = NULL;
	int32_t ret;

	if( !hInstance )
		return NULL;

	ret = sceKernelDlsym( (int32_t)( (intptr_t)hInstance - 1 ), name, &addr );

	if( ret < 0 || !addr )
		return NULL;

	return addr;
}

void *COM_GetProcAddressFromDependency( void *hInstance, const char *depname, const char *name )
{
	// the loader looks through dependencies of the module itself
	return COM_GetProcAddress( hInstance, name );
}

void *COM_FunctionFromName( void *hInstance, const char *pName )
{
	return COM_GetProcAddress( hInstance, pName );
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	const char *name = PS4_NameForAddress( function );

	if( name )
		return COM_GetPlatformNeutralName( name );

	return NULL;
}
