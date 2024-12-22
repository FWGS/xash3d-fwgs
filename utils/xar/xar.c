/*
xar.c -- Xash ARchives (XAR)
Copyright (C) 2023 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "port.h"
#include "build.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "filesystem.h"
#if XASH_POSIX
#include <sys/stat.h>
#include <dlfcn.h>
#define LoadLibrary( x ) dlopen( x, RTLD_NOW )
#define GetProcAddress( x, y ) dlsym( x, y )
#define FreeLibrary( x ) dlclose( x )
#elif XASH_WIN32
#include <windows.h>
#endif

static void *g_hModule;
static FSAPI g_pfnGetFSAPI;
static fs_api_t g_fs;
static fs_globals_t *g_nullglobals;

static qboolean LoadFilesystem( void )
{
#if 0
	string cwd;

	if( getcwd( cwd, sizeof( cwd )) == NULL )
	{
		printf( "getcwd() failed: %s\n", strerror( errno ));
		return false;
	}
#endif
	g_hModule = LoadLibrary( "filesystem_stdio." OS_LIB_EXT );
	if( !g_hModule )
		return false;

	g_pfnGetFSAPI = (void*)GetProcAddress( g_hModule, GET_FS_API );
	if( !g_pfnGetFSAPI )
		return false;

	if( !g_pfnGetFSAPI( FS_API_VERSION, &g_fs, &g_nullglobals, NULL ))
		return false;

	// g_fs.InitStdio( true, cwd, );

	return true;
}

static void FS_CreatePath( char *path )
{
	char	*ofs, save;

	for( ofs = path + 1; *ofs; ofs++ )
	{
		if( *ofs == '/' || *ofs == '\\' )
		{
			// create the directory
			save = *ofs;
			*ofs = 0;
			_mkdir( path );
			*ofs = save;
		}
	}
}

static void usage( const char *arg0 )
{
	printf( "%s: <action> [option...] <file>\n", arg0 );
	puts( "XAR is a simple frontend to Xash3D FWGS's filesystem_stdio library" );
	puts( "that allows interacting with archive types supported by it" );
	puts( "Options:" );
	puts( "\tx\t\teXtract the archive" );
	puts( "\tt\t\tlisT the archive" );
	// TODO: make an interface for modifying
	// puts( "\tc\t\tCreate the archive" );
	// puts( "\tu\t\tUpdate the archive" );
	// puts( "\tr\t\tRemove from the archive" );

	puts( "Extract and list options:" );
	puts( "\t-wads\tauto-mount WADs inside archives" );
	exit( 1 );
}

int main( int argc, char **argv )
{
	const char *filename;
	search_t *search;
	char action;
	int i, flags = FS_NOWRITE_PATH | FS_SKIP_ARCHIVED_WADS;

	if( argc < 3 )
		usage( argv[0] );

	if( !LoadFilesystem())
	{
		puts( "Can't load filesystem_stdio!" );
		return 2;
	}

	action = argv[1][0];
	if( action != 'x' && action != 't' )
	{
		printf( "Unknown action: %c\n", action );
		usage( argv[0] );
	}

	for( i = 2; i < argc - 1; i++ )
	{
		if( !strcmp( argv[i], "-wads" ))
			ClearBits( flags, FS_SKIP_ARCHIVED_WADS );
		else
		{
			printf( "Unknown option: %s\n", argv[i] );
			usage( argv[0] );
		}
	}

	filename = argv[argc-1];

	if( g_fs.MountArchive_Fullpath( filename, flags ) == NULL )
	{
		printf( "Can't mount %s\n", filename );
		return 3;
	}

	// suboptimal, but that's what is available with current FS API
	search = g_fs.Search( "*", false, false );
	if( !search )
	{
		printf( "Can't find any files in %s\n", filename );
		return 4;
	}

	if( action == 't' )
	{
		puts( "File list:" );
		for( i = 0; i < search->numfilenames; i++ )
		{
			printf( "\t%s\n", search->filenames[i] );
		}
	}
	else if( action == 'x' )
	{
		for( i = 0; i < search->numfilenames; i++ )
		{
			struct stat st;
			char *path = search->filenames[i];
			file_t *from;
			char buffer[4096];
			FILE *to;

			if(( from = g_fs.Open( path, "rb", false )) == NULL )
			{
				printf( "Can't open %s in archive, skipping...\n", path );
				continue;
			}

			FS_CreatePath( path );
			if( stat( path, &st ) == 0 )
			{
				printf( "Will not overwrite existing %s file\n", path );
				g_fs.Close( from );
				continue;
			}

			if(( to = fopen( search->filenames[i], "wb" )) == NULL )
			{
				printf( "fopen() failed: %s\n", strerror( errno ));
				g_fs.Close( from );
				continue;
			}

			printf( "Unpacking %s...\n", path );
			while( !g_fs.Eof( from ))
			{
				size_t len = g_fs.Read( from, buffer, sizeof( buffer ));
				fwrite( buffer, 1, len, to );
			}
			fclose( to );
			g_fs.Close( from );
		}
	}

	free( search );

	return 0;
}
