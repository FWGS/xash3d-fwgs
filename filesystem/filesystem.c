/*
filesystem.c - game filesystem based on DP fs
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

#define _GNU_SOURCE 1

#include "build.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#if XASH_WIN32
#include <direct.h>
#include <io.h>
#include "utflib.h"
#elif XASH_DOS4GW
#include <direct.h>
#else
#include <dirent.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#if HAVE_MEMFD_CREATE
#include <sys/mman.h>
#endif
#include "port.h"
#include "defaults.h"
#include "const.h"
#include "crtlib.h"
#include "crclib.h"
#include "filesystem.h"
#include "filesystem_internal.h"
#include "xash3d_mathlib.h"
#include "common/com_strings.h"
#include "common/protocol.h"
#include "library_suffix.h"

#define FILE_COPY_SIZE		(1024 * 1024)
#define SAVE_AGED_COUNT 2 // the default count of quick and auto saves

#if !defined( O_BINARY )
	#define O_BINARY 0
#endif // !defined( O_BINARY )

#if !defined( O_TEXT )
	#define O_TEXT 0
#endif // !defined( O_TEXT )

#if !XASH_PSVITA && !XASH_NSWITCH
	#define HAVE_DUP
#endif // !XASH_PSVITA && !XASH_NSWITCH

fs_globals_t FI;
poolhandle_t  fs_mempool;
char          fs_rootdir[MAX_SYSPATH];
searchpath_t *fs_writepath;

static searchpath_t *fs_searchpaths = NULL;	// chain
static char fs_basedir[MAX_SYSPATH];	// base game directory
static char fs_gamedir[MAX_SYSPATH];	// game current directory
static char fs_rodir[MAX_SYSPATH];
static string fs_language;
static qboolean fs_ext_path = false;	// attempt to read\write from ./ or ../ pathes

typedef struct fs_archive_s
{
	const char *ext;
	int type;
	FS_ADDARCHIVE_FULLPATH pfnAddArchive_Fullpath;
	qboolean load_wads; // load wads from this archive
	qboolean real_archive;
} fs_archive_t;

// add archives in specific order PAK -> PK3 -> WAD
// so raw WADs takes precedence over WADs included into PAKs and PK3s
static const fs_archive_t g_archives[] =
{
	{
		.ext = "pak",
		.type = SEARCHPATH_PAK,
		.pfnAddArchive_Fullpath = FS_AddPak_Fullpath,
		.load_wads = true,
		.real_archive = true,
	}, {
		.ext = "pk3",
		.type = SEARCHPATH_ZIP,
		.pfnAddArchive_Fullpath = FS_AddZip_Fullpath,
		.load_wads = true,
		.real_archive = true,
	}, {
		.ext = "pk3dir",
		.type = SEARCHPATH_PK3DIR,
		.pfnAddArchive_Fullpath = FS_AddDir_Fullpath,
		.load_wads = true,
		.real_archive = false,
	}, {
		.ext = "wad",
		.type = SEARCHPATH_WAD,
		.pfnAddArchive_Fullpath = FS_AddWad_Fullpath,
		.load_wads = false,
		.real_archive = true,
	},
};

// special fs_archive_t for plain directories
static const fs_archive_t g_directory_archive =
{
	.type = SEARCHPATH_PLAIN,
	.pfnAddArchive_Fullpath = FS_AddDir_Fullpath,
};

#if XASH_ANDROID
static const fs_archive_t g_android_archive =
{
	.type = SEARCHPATH_ANDROID_ASSETS,
	.pfnAddArchive_Fullpath = FS_AddAndroidAssets_Fullpath
};
#endif

#ifdef XASH_REDUCE_FD
static file_t *fs_last_readfile;
static zip_t *fs_last_zip;

static void FS_EnsureOpenFile( file_t *file )
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

static void FS_BackupFileName( file_t *file, const char *path, uint options )
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
static void FS_EnsureOpenFile( file_t *file ) {}
static void FS_BackupFileName( file_t *file, const char *path, uint options ) {}
#endif

static void FS_InitMemory( void );
static void FS_Purge( file_t* file );

void _Mem_Free( void *data, const char *filename, int fileline )
{
	g_engfuncs._Mem_Free( data, filename, fileline );
}

void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return g_engfuncs._Mem_Alloc( poolptr, size, clear, filename, fileline );
}

/*
=============================================================================

FILEMATCH COMMON SYSTEM

=============================================================================
*/
void stringlistinit( stringlist_t *list )
{
	memset( list, 0, sizeof( *list ));
}

void stringlistfreecontents( stringlist_t *list )
{
	int	i;

	for( i = 0; i < list->numstrings; i++ )
	{
		if( list->strings[i] )
			Mem_Free( list->strings[i] );
		list->strings[i] = NULL;
	}

	if( list->strings )
		Mem_Free( list->strings );

	list->numstrings = 0;
	list->maxstrings = 0;
	list->strings = NULL;
}

void stringlistappend( stringlist_t *list, const char *text )
{
	size_t	textlen;

	if( !Q_strcmp( text, "." ) || !Q_strcmp( text, ".." ))
		return; // ignore the virtual directories

	if( list->numstrings >= list->maxstrings )
	{
		list->maxstrings += 4096;
		list->strings = Mem_Realloc( fs_mempool, list->strings, list->maxstrings * sizeof( *list->strings ));
	}

	textlen = Q_strlen( text ) + 1;
	list->strings[list->numstrings] = Mem_Calloc( fs_mempool, textlen );
	memcpy( list->strings[list->numstrings], text, textlen );
	list->numstrings++;
}

void stringlistsort( stringlist_t *list )
{
	char	*temp;
	int	i, j;

	// this is a selection sort (finds the best entry for each slot)
	for( i = 0; i < list->numstrings - 1; i++ )
	{
		for( j = i + 1; j < list->numstrings; j++ )
		{
			if( Q_strcmp( list->strings[i], list->strings[j] ) > 0 )
			{
				temp = list->strings[i];
				list->strings[i] = list->strings[j];
				list->strings[j] = temp;
			}
		}
	}
}

#if XASH_DOS4GW
// convert names to lowercase because dos doesn't care, but pattern matching code often does
static void listlowercase( stringlist_t *list )
{
	char	*c;
	int	i;

	for( i = 0; i < list->numstrings; i++ )
	{
		for( c = list->strings[i]; *c; c++ )
			*c = Q_tolower( *c );
	}
}
#endif

void listdirectory( stringlist_t *list, const char *path, qboolean dirs_only )
{
#if XASH_WIN32
	char pattern[4096];
	struct _finddata_t n_file;
	intptr_t hFile;

	Q_snprintf( pattern, sizeof( pattern ), "%s/*", path );

	// ask for the directory listing handle
	hFile = _findfirst( pattern, &n_file );
	if( hFile == -1 ) return;

	// start a new chain with the the first name
	stringlistappend( list, n_file.name );
	// iterate through the directory
	while( _findnext( hFile, &n_file ) == 0 )
	{
		if( dirs_only && !FBitSet( n_file.attrib, _A_SUBDIR ))
			continue;

		stringlistappend( list, n_file.name );
	}
	_findclose( hFile );
#else
	DIR *dir;
	struct dirent *entry;

	dir = opendir( path );

	if( !dir )
		return;

	// iterate through the directory
	while(( entry = readdir( dir )))
	{
#if HAVE_DIRENT_D_TYPE
		if( dirs_only && entry->d_type != DT_DIR && entry->d_type != DT_LNK && entry->d_type != DT_UNKNOWN )
			continue;
#endif

		stringlistappend( list, entry->d_name );
	}

	closedir( dir );
#endif

#if XASH_DOS4GW
	// convert names to lowercase because 8.3 always in CAPS
	listlowercase( list );
#endif
}

/*
=============================================================================

OTHER PRIVATE FUNCTIONS

=============================================================================
*/

#if XASH_WIN32
/*
====================
FS_PathToWideChar

Converts input UTF-8 string to wide char string.
====================
*/
static const wchar_t *FS_PathToWideChar( const char *path )
{
	static wchar_t pathBuffer[MAX_PATH];
	MultiByteToWideChar( CP_UTF8, 0, path, -1, pathBuffer, MAX_PATH );
	return pathBuffer;
}
#endif

/*
============
FS_CreatePath

Only used for FS_Open.
============
*/
void FS_CreatePath( char *path )
{
	char	*ofs, save;

	for( ofs = path + 1; *ofs; ofs++ )
	{
		if( *ofs == '/' || *ofs == '\\' )
		{
			// create the directory
			save = *ofs;
			*ofs = 0;
#if XASH_WIN32
			_mkdir( path ); // use _wmkdir maybe?
#else // !XASH_WIN32
			mkdir( path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH );
#endif // !XASH_WIN32
			*ofs = save;
		}
	}
}

static searchpath_t *FS_AddArchive_Fullpath( const fs_archive_t *archive, const char *file, int flags )
{
	searchpath_t *search;

	if( !archive )
	{
		int i;
		const char *ext = COM_FileExtension( file );

		for( i = 0; i < sizeof( g_archives ) / sizeof( g_archives[0] ); i++ )
		{
			if( !Q_stricmp( g_archives[i].ext, ext ))
			{
				archive = &g_archives[i];
				break;
			}
		}

		if( !archive )
		{
			Con_Printf( S_ERROR "%s: unknown archive format %s, not mounted\n", __func__, file );
			return NULL;
		}
	}

	for( search = fs_searchpaths; search; search = search->next )
	{
		if( search->type == archive->type && !Q_stricmp( search->filename, file ))
			return search; // already loaded
	}

	search = archive->pfnAddArchive_Fullpath( file, flags );

	if( !search )
		return NULL;

	search->next = fs_searchpaths;
	fs_searchpaths = search;

	// time to add in search list all the wads from this archive
	if( archive->load_wads && !FBitSet( flags, FS_SKIP_ARCHIVED_WADS ))
	{
		stringlist_t list;
		int i;

		stringlistinit( &list );
		search->pfnSearch( search, &list, "*.wad", true );
		stringlistsort( &list ); // keep always sorted

		for( i = 0; i < list.numstrings; i++ )
		{
			searchpath_t *wad;
			char fullpath[MAX_SYSPATH];

			Q_snprintf( fullpath, sizeof( fullpath ), "%s/%s", file, list.strings[i] );
			if(( wad = FS_AddWad_Fullpath( fullpath, flags | FS_LOAD_PACKED_WAD )))
			{
				wad->next = fs_searchpaths;
				fs_searchpaths = wad;
			}
		}

		stringlistfreecontents( &list );
	}

	return search;
}

/*
================
FS_AddArchive_Fullpath
================
*/
searchpath_t *FS_MountArchive_Fullpath( const char *file, int flags )
{
	return FS_AddArchive_Fullpath( NULL, file, flags );
}

/*
================
FS_AddGameDirectory

Sets fs_writepath, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
void FS_AddGameDirectory( const char *dir, uint flags )
{
	stringlist_t list;
	searchpath_t *search;
	int i, j;

	stringlistinit( &list );
	listdirectory( &list, dir, false );
	stringlistsort( &list );

	for( j = 0; j < sizeof( g_archives ) / sizeof( g_archives[0] ); j++ )
	{
		char fullpath[MAX_SYSPATH];
		int i;

		for( i = 0; i < list.numstrings; i++ )
		{
			if( Q_stricmp( COM_FileExtension( list.strings[i] ), g_archives[j].ext ))
				continue;

			Q_snprintf( fullpath, sizeof( fullpath ), "%s%s", dir, list.strings[i] );
			FS_AddArchive_Fullpath( &g_archives[j], fullpath, flags );
		}
	}

	stringlistfreecontents( &list );

#if XASH_ANDROID
	FS_AddArchive_Fullpath( &g_android_archive, dir, flags );
#endif

	// add the directory to the search path
	// (unpacked files have the priority over packed files)
	search = FS_AddArchive_Fullpath( &g_directory_archive, dir, flags );
	if( !FBitSet( flags, FS_NOWRITE_PATH ))
		fs_writepath = search;
}

/*
================
FS_ClearSearchPath
================
*/
void FS_ClearSearchPath( void )
{
	searchpath_t *cur, **prev;
	int i;

	prev = &fs_searchpaths;

	while( true )
	{
		cur = *prev;

		if( !cur )
			break;

		// never delete static paths
		if( FBitSet( cur->flags, FS_STATIC_PATH ))
		{
			prev = &cur->next;
			continue;
		}

		*prev = cur->next;
		cur->pfnClose( cur );
		Mem_Free( cur );
	}

	for( i = 0; i < FI.numgames; i++ )
	{
		if( FI.games[i] )
			FI.games[i]->added = false;
	}
}

/*
====================
FS_CheckNastyPath
Return true if the path should be rejected due to one of the following:
1: path elements that are non-portable
2: path elements that would allow access to files outside the game directory,
	or are just not a good idea for a mod to be using.
====================
*/
static int FS_CheckNastyPath( const char *path )
{
	// all: never allow an empty path, as for gamedir it would access the parent directory and a non-gamedir path it is just useless
	if( COM_StringEmptyOrNULL( path )) return 2;

	if( fs_ext_path ) return 0;     // allow any path

	// Mac: don't allow Mac-only filenames - : is a directory separator
	// instead of /, but we rely on / working already, so there's no reason to
	// support a Mac-only path
	// Amiga and Windows: : tries to go to root of drive
	if( Q_strchr( path, ':' )) return 1; // non-portable attempt to go to root of drive

	// Amiga: // is parent directory
	if( Q_strstr( path, "//")) return 1; // non-portable attempt to go to parent directory

	// all: don't allow going to parent directory (../ or /../)
	if( Q_strstr( path, "..")) return 2; // attempt to go outside the game directory

	// Windows and UNIXes: don't allow absolute paths
	if( path[0] == '/') return 2; // attempt to go outside the game directory

#if 0
	// all: forbid trailing slash on gamedir
	if( isgamedir && path[Q_strlen(path)-1] == '/' ) return 2;
#endif

	// all: forbid leading dot on any filename for any reason
	if( Q_strstr(path, "/.")) return 2; // attempt to go outside the game directory

	// after all these checks we're pretty sure it's a / separated filename
	// and won't do much if any harm
	return false;
}

/*
================
FS_WriteGameInfo

assume GameInfo is valid
================
*/
static qboolean FS_WriteGameInfo( const char *filepath, const gameinfo_t *GameInfo )
{
	file_t	*f = FS_Open( filepath, "w", false ); // we in binary-mode
	int	i, write_ambients = false;

	if( !f )
		return false;

	FS_Printf( f, "// generated by " XASH_ENGINE_NAME " " XASH_VERSION "-%s (%s-%s)\n\n\n", g_buildcommit, Q_buildos(), Q_buildarch() );

	if( !COM_StringEmpty( GameInfo->basedir ))
		FS_Printf( f, "basedir\t\t\"%s\"\n", GameInfo->basedir );

	// DEPRECATED: gamedir key isn't supported by FWGS fork
	// but write it anyway to keep compability with original Xash3D
	if( !COM_StringEmpty( GameInfo->gamefolder ))
		FS_Printf( f, "gamedir\t\t\"%s\"\n", GameInfo->gamefolder );

	if( !COM_StringEmpty( GameInfo->falldir ))
		FS_Printf( f, "fallback_dir\t\"%s\"\n", GameInfo->falldir );

	if( !COM_StringEmpty( GameInfo->title ))
		FS_Printf( f, "title\t\t\"%s\"\n", GameInfo->title );

	if( !COM_StringEmpty( GameInfo->startmap ))
		FS_Printf( f, "startmap\t\t\"%s\"\n", GameInfo->startmap );

	if( !COM_StringEmpty( GameInfo->trainmap ))
		FS_Printf( f, "trainmap\t\t\"%s\"\n", GameInfo->trainmap );

	if( GameInfo->version != 0.0f )
		FS_Printf( f, "version\t\t%g\n", GameInfo->version );

	if( GameInfo->size != 0 )
		FS_Printf( f, "size\t\t%zu\n", GameInfo->size );

	if( !COM_StringEmpty( GameInfo->game_url ))
		FS_Printf( f, "url_info\t\t\"%s\"\n", GameInfo->game_url );

	if( !COM_StringEmpty( GameInfo->update_url ))
		FS_Printf( f, "url_update\t\t\"%s\"\n", GameInfo->update_url );

	if( !COM_StringEmpty( GameInfo->type ))
		FS_Printf( f, "type\t\t\"%s\"\n", GameInfo->type );

	if( !COM_StringEmpty( GameInfo->date ))
		FS_Printf( f, "date\t\t\"%s\"\n", GameInfo->date );

	if( !COM_StringEmpty( GameInfo->dll_path ))
		FS_Printf( f, "dllpath\t\t\"%s\"\n", GameInfo->dll_path );

	if( !COM_StringEmpty( GameInfo->game_dll ))
		FS_Printf( f, "gamedll\t\t\"%s\"\n", GameInfo->game_dll );

	if( !COM_StringEmpty( GameInfo->game_dll_linux ))
		FS_Printf( f, "gamedll_linux\t\t\"%s\"\n", GameInfo->game_dll_linux );

	if( !COM_StringEmpty( GameInfo->game_dll_osx ))
		FS_Printf( f, "gamedll_osx\t\t\"%s\"\n", GameInfo->game_dll_osx );

	if( !COM_StringEmpty( GameInfo->iconpath ))
		FS_Printf( f, "icon\t\t\"%s\"\n", GameInfo->iconpath );

	switch( GameInfo->gamemode )
	{
	case 1: FS_Print( f, "gamemode\t\t\"singleplayer_only\"\n" ); break;
	case 2: FS_Print( f, "gamemode\t\t\"multiplayer_only\"\n" ); break;
	}

	if( !COM_StringEmpty( GameInfo->sp_entity ))
		FS_Printf( f, "sp_entity\t\t\"%s\"\n", GameInfo->sp_entity );
	if( !COM_StringEmpty( GameInfo->mp_entity ))
		FS_Printf( f, "mp_entity\t\t\"%s\"\n", GameInfo->mp_entity );
	if( !COM_StringEmpty( GameInfo->mp_filter ))
		FS_Printf( f, "mp_filter\t\t\"%s\"\n", GameInfo->mp_filter );

	if( GameInfo->secure )
		FS_Printf( f, "secure\t\t\"%i\"\n", GameInfo->secure );

	if( GameInfo->nomodels )
		FS_Printf( f, "nomodels\t\t\"%i\"\n", GameInfo->nomodels );

	if( GameInfo->max_edicts > 0 )
		FS_Printf( f, "max_edicts\t%i\n", GameInfo->max_edicts );
	if( GameInfo->max_tents > 0 )
		FS_Printf( f, "max_tempents\t%i\n", GameInfo->max_tents );
	if( GameInfo->max_beams > 0 )
		FS_Printf( f, "max_beams\t\t%i\n", GameInfo->max_beams );
	if( GameInfo->max_particles > 0 )
		FS_Printf( f, "max_particles\t%i\n", GameInfo->max_particles );

	for( i = 0; i < NUM_AMBIENTS; i++ )
	{
		if( *GameInfo->ambientsound[i] )
		{
			if( !write_ambients )
			{
				FS_Print( f, "\n" );
				write_ambients = true;
			}
			FS_Printf( f, "ambient%i\t\t%s\n", i, GameInfo->ambientsound[i] );
		}
	}

	if( GameInfo->noskills )
		FS_Printf( f, "noskills\t\t\"%i\"\n", GameInfo->noskills );

	if( GameInfo->quicksave_aged_count != SAVE_AGED_COUNT )
		FS_Printf( f, "quicksave_aged_count\t\t%d\n", GameInfo->quicksave_aged_count );

	if( GameInfo->autosave_aged_count != SAVE_AGED_COUNT )
		FS_Printf( f, "autosave_aged_count\t\t%d\n", GameInfo->autosave_aged_count );

	// HL25 compatibility
	if( GameInfo->animated_title )
		FS_Printf( f, "animated_title\t\t%i\n", GameInfo->animated_title );

	if( GameInfo->hd_background )
		FS_Printf( f, "hd_background\t\t%i\n", GameInfo->hd_background );

	// always expose our extensions :)
	FS_Printf( f, "internal_vgui_support\t\t%i\n", GameInfo->internal_vgui_support );
	FS_Printf( f, "render_picbutton_text\t\t%i\n", GameInfo->render_picbutton_text );

	if( !COM_StringEmpty( GameInfo->demomap ))
		FS_Printf( f, "demomap\t\t\"%s\"\n", GameInfo->demomap );

	FS_Close( f );	// all done

	return true;
}

static void FS_MakeGameInfo( void )
{
	if( FS_WriteGameInfo( "gameinfo.txt", FI.GameInfo ))
		Con_Printf( "Successfully generated %s/gameinfo.txt\n", FI.GameInfo->gamefolder );
	else
		Con_Printf( S_ERROR "Can't open %s/gameinfo.txt for write\n", FI.GameInfo->gamefolder );
}

static void FS_InitGameInfo( gameinfo_t *GameInfo, const char *gamedir, qboolean quake, time_t mtime )
{
	memset( GameInfo, 0, sizeof( *GameInfo ));

	// filesystem info
	GameInfo->mtime = mtime;
	Q_strncpy( GameInfo->gamefolder, gamedir, sizeof( GameInfo->gamefolder ));
	Q_strncpy( GameInfo->sp_entity, "info_player_start", sizeof( GameInfo->sp_entity ));
	Q_strncpy( GameInfo->mp_entity, "info_player_deathmatch", sizeof( GameInfo->mp_entity ));
	Q_strncpy( GameInfo->iconpath, "game.ico", sizeof( GameInfo->iconpath ));

	if( quake )
	{
		Q_strncpy( GameInfo->basedir, "id1", sizeof( GameInfo->basedir ));
		Q_strncpy( GameInfo->title, gamedir, sizeof( GameInfo->title ));
		Q_strncpy( GameInfo->startmap, "start", sizeof( GameInfo->startmap ));
		Q_strncpy( GameInfo->dll_path, "bin", sizeof( GameInfo->dll_path ));
		Q_strncpy( GameInfo->game_dll, "bin/progs.dll", sizeof( GameInfo->game_dll ));
		Q_strncpy( GameInfo->game_dll_linux, "bin/progs.so", sizeof( GameInfo->game_dll_linux ));
		Q_strncpy( GameInfo->game_dll_osx, "bin/progs.dylib", sizeof( GameInfo->game_dll_osx ));
	}
	else
	{
		Q_strncpy( GameInfo->basedir, fs_basedir, sizeof( GameInfo->basedir ));
		Q_strncpy( GameInfo->title, gamedir, sizeof( GameInfo->title ));
		Q_strncpy( GameInfo->startmap, "c0a0", sizeof( GameInfo->startmap ));
		Q_strncpy( GameInfo->dll_path, "cl_dlls", sizeof( GameInfo->dll_path ));
		Q_strncpy( GameInfo->game_dll, "dlls/hl.dll", sizeof( GameInfo->game_dll ));
		Q_strncpy( GameInfo->game_dll_linux, "dlls/hl.so", sizeof( GameInfo->game_dll_linux ));
		Q_strncpy( GameInfo->game_dll_osx, "dlls/hl.dylib", sizeof( GameInfo->game_dll_osx ));
	}

	GameInfo->max_edicts     = DEFAULT_MAX_EDICTS; // default value if not specified
	GameInfo->max_tents      = 500;
	GameInfo->max_beams      = 128;
	GameInfo->max_particles  = 4096;
	GameInfo->version        = 1.0f;

	GameInfo->quicksave_aged_count = SAVE_AGED_COUNT;
	GameInfo->autosave_aged_count  = SAVE_AGED_COUNT;
}

static void FS_ParseGenericGameInfo( gameinfo_t *GameInfo, const char *buf, const qboolean isGameInfo )
{
	char *pfile = (char*) buf;
	qboolean found_linux = false, found_osx = false;
	string token;

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		// different names in liblist/gameinfo
		if( !Q_stricmp( token, isGameInfo ? "title" : "game" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->title, sizeof( GameInfo->title ));
		}
		// valid for both
		else if( !Q_stricmp( token, "fallback_dir" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->falldir, sizeof( GameInfo->falldir ));
		}
		// valid for both
		else if( !Q_stricmp( token, "startmap" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->startmap, sizeof( GameInfo->startmap ));
			COM_StripExtension( GameInfo->startmap ); // HQ2:Amen has extension .bsp
		}
		// only trainmap is valid for gameinfo
		else if( !Q_stricmp( token, "trainmap" ) ||
			(!isGameInfo && !Q_stricmp( token, "trainingmap" )))
		{
			pfile = COM_ParseFile( pfile, GameInfo->trainmap, sizeof( GameInfo->trainmap ));
			COM_StripExtension( GameInfo->trainmap ); // HQ2:Amen has extension .bsp
		}
		// valid for both
		else if( !Q_stricmp( token, "url_info" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_url, sizeof( GameInfo->game_url ));
		}
		// different names
		else if( !Q_stricmp( token, isGameInfo ? "url_update" : "url_dl" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->update_url, sizeof( GameInfo->update_url ));
		}
		// valid for both
		else if( !Q_stricmp( token, "gamedll" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_dll, sizeof( GameInfo->game_dll ));
			COM_FixSlashes( GameInfo->game_dll );
		}
		// valid for both
		else if( !Q_stricmp( token, "gamedll_linux" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_dll_linux, sizeof( GameInfo->game_dll_linux ));
			found_linux = true;
		}
		// valid for both
		else if( !Q_stricmp( token, "gamedll_osx" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_dll_osx, sizeof( GameInfo->game_dll_osx ));
			found_osx = true;
		}
		// valid for both
		else if( !Q_stricmp( token, "icon" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->iconpath, sizeof( GameInfo->iconpath ));
			COM_FixSlashes( GameInfo->iconpath );
			COM_DefaultExtension( GameInfo->iconpath, ".ico", sizeof( GameInfo->iconpath ));
		}
		else if( !Q_stricmp( token, "type" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));

			if( isGameInfo )
			{
				Q_strncpy( GameInfo->type, token, sizeof( GameInfo->type ));
			}
			else
			{
				if( !Q_stricmp( token, "singleplayer_only" ))
				{
					// TODO: Remove this ugly hack too.
					// This was made because Half-Life has multiplayer,
					// but for some reason it's marked as singleplayer_only.
					// Old WON version is fine.
					if( !Q_stricmp( GameInfo->gamefolder, "valve") )
						GameInfo->gamemode = GAME_NORMAL;
					else
						GameInfo->gamemode = GAME_SINGLEPLAYER_ONLY;
					Q_strncpy( GameInfo->type, "Single", sizeof( GameInfo->type ));
				}
				else if( !Q_stricmp( token, "multiplayer_only" ))
				{
					GameInfo->gamemode = GAME_MULTIPLAYER_ONLY;
					Q_strncpy( GameInfo->type, "Multiplayer", sizeof( GameInfo->type ));
				}
				else
				{
					// pass type without changes
					if( !isGameInfo )
						GameInfo->gamemode = GAME_NORMAL;
					Q_strncpy( GameInfo->type, token, sizeof( GameInfo->type ));
				}
			}
		}
		// valid for both
		else if( !Q_stricmp( token, "version" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->version = Q_atof( token );
		}
		// valid for both
		else if( !Q_stricmp( token, "size" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->size = Q_atoi( token );
		}
		else if( !Q_stricmp( token, isGameInfo ? "mp_entity" : "mpentity" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->mp_entity, sizeof( GameInfo->mp_entity ));
		}
		else if( !Q_stricmp( token, isGameInfo ? "mp_filter" : "mpfilter" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->mp_filter, sizeof( GameInfo->mp_filter ));
		}
		// valid for both
		else if( !Q_stricmp( token, "secure" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->secure = Q_atoi( token ) ? true : false;
		}
		// valid for both
		else if( !Q_stricmp( token, "nomodels" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->nomodels = Q_atoi( token ) ? true : false;
		}
		else if( !Q_stricmp( token, isGameInfo ? "max_edicts" : "edicts" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->max_edicts = bound( MIN_EDICTS, Q_atoi( token ), MAX_EDICTS );
		}
		// valid for both
		else if( !Q_stricmp( token, "hd_background" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->hd_background = Q_atoi( token ) ? true : false;
		}
		else if( !Q_stricmp( token, "animated_title" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->animated_title = Q_atoi( token ) ? true : false;
		}
		// only for gameinfo
		else if( isGameInfo )
		{
			if( !Q_stricmp( token, "basedir" ))
			{
				string fs_path;
				pfile = COM_ParseFile( pfile, fs_path, sizeof( fs_path ));
				if( Q_stricmp( fs_path, GameInfo->basedir ) || Q_stricmp( fs_path, GameInfo->gamefolder ))
					Q_strncpy( GameInfo->basedir, fs_path, sizeof( GameInfo->basedir ));
			}
			else if( !Q_stricmp( token, "sp_entity" ))
			{
				pfile = COM_ParseFile( pfile, GameInfo->sp_entity, sizeof( GameInfo->sp_entity ));
			}
			else if( isGameInfo && !Q_stricmp( token, "dllpath" ))
			{
				pfile = COM_ParseFile( pfile, GameInfo->dll_path, sizeof( GameInfo->dll_path ));
			}
			else if( isGameInfo && !Q_stricmp( token, "date" ))
			{
				pfile = COM_ParseFile( pfile, GameInfo->date, sizeof( GameInfo->date ));
			}
			else if( !Q_stricmp( token, "max_tempents" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->max_tents = bound( 300, Q_atoi( token ), 2048 );
			}
			else if( !Q_stricmp( token, "max_beams" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->max_beams = bound( 64, Q_atoi( token ), 512 );
			}
			else if( !Q_stricmp( token, "max_particles" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->max_particles = bound( 1024, Q_atoi( token ), 131072 );
			}
			else if( !Q_stricmp( token, "gamemode" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				// TODO: Remove this ugly hack too.
				// This was made because Half-Life has multiplayer,
				// but for some reason it's marked as singleplayer_only.
				// Old WON version is fine.
				if( !Q_stricmp( token, "singleplayer_only" ) && Q_stricmp( GameInfo->gamefolder, "valve") )
					GameInfo->gamemode = GAME_SINGLEPLAYER_ONLY;
				else if( !Q_stricmp( token, "multiplayer_only" ))
					GameInfo->gamemode = GAME_MULTIPLAYER_ONLY;
			}
			else if( !Q_strnicmp( token, "ambient", 7 ))
			{
				int	ambientNum = Q_atoi( token + 7 );

				if( ambientNum < 0 || ambientNum >= NUM_AMBIENTS )
					ambientNum = 0;
				pfile = COM_ParseFile( pfile, GameInfo->ambientsound[ambientNum],
					sizeof( GameInfo->ambientsound[ambientNum] ));
			}
			else if( !Q_stricmp( token, "noskills" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->noskills = Q_atoi( token ) ? true : false;
			}
			else if( !Q_stricmp( token, "render_picbutton_text" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->render_picbutton_text = Q_atoi( token ) ? true : false;
			}
			else if( !Q_stricmp( token, "internal_vgui_support" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->internal_vgui_support = Q_atoi( token ) ? true : false;
			}
			else if( !Q_stricmp( token, "quicksave_aged_count" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->quicksave_aged_count = bound( 2, Q_atoi( token ), 99 );
			}
			else if( !Q_stricmp( token, "autosave_aged_count" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->autosave_aged_count = bound( 2, Q_atoi( token ), 99 );
			}
			else if( !Q_stricmp( token, "demomap" ))
			{
				pfile = COM_ParseFile( pfile, GameInfo->demomap, sizeof( GameInfo->demomap ));
			}
		}
	}

	// demomap only valid for gameinfo.txt but HL1 after 25th anniversary update
	// comes with demo chapter. Set the demomap here.
	if( COM_StringEmpty( GameInfo->demomap ))
	{
		if( !Q_stricmp( GameInfo->title, "Half-Life" )) // original check from GameUI
			Q_strncpy( GameInfo->demomap, "hldemo1", sizeof( GameInfo->demomap ));
	}

	if( !found_linux || !found_osx )
	{
		// just replace extension from dll to so/dylib
		char gamedll[64];
		Q_strncpy( gamedll, GameInfo->game_dll, sizeof( gamedll ));
		COM_StripExtension( gamedll );

		if( !found_linux )
			Q_snprintf( GameInfo->game_dll_linux, sizeof( GameInfo->game_dll_linux ), "%s.so", gamedll );

		if( !found_osx )
			Q_snprintf( GameInfo->game_dll_osx, sizeof( GameInfo->game_dll_osx ), "%s.dylib", gamedll );
	}

	// make sure what gamedir is really exist
	// a1ba: why we are doing this???
	Q_snprintf( token, sizeof( token ), "%s/%s", fs_rootdir, GameInfo->falldir );
	if( !FS_SysFolderExists( token ))
	{
		if( !COM_StringEmpty( fs_rodir ))
		{
			Q_snprintf( token, sizeof( token ), "%s/%s", fs_rodir, GameInfo->falldir );
			if( !FS_SysFolderExists( token ))
				GameInfo->falldir[0] = 0;
		}
		else GameInfo->falldir[0] = 0;
	}
}

/*
================
FS_ParseLiblistGam
================
*/
static qboolean FS_ParseLiblistGam( const char *filename, const char *gamedir, gameinfo_t *GameInfo, time_t mtime )
{
	char *afile = FS_LoadDirectFile( filename, NULL );

	if( !afile )
		return false;

	FS_InitGameInfo( GameInfo, gamedir, false, mtime );
	FS_ParseGenericGameInfo( GameInfo, afile, false );

	Mem_Free( afile );

	return true;
}

/*
================
FS_ConvertGameInfo
================
*/
static qboolean FS_ConvertGameInfo( const char *gamedir, const char *gameinfo_path, const char *liblist_path, gameinfo_t *gi, time_t liblist_mtime )
{
	memset( gi, 0, sizeof( *gi ));

	// liblist.gam to gameinfo.txt conversion is deprecated, only support for RwDir!
	if( FS_ParseLiblistGam( liblist_path, gamedir, gi, liblist_mtime ))
	{
		Con_DPrintf( "Convert %s to %s\n", liblist_path, gameinfo_path );
		return FS_WriteGameInfo( gameinfo_path, gi );
	}

	return false;
}

/*
================
FS_ReadGameInfo
================
*/
static qboolean FS_ReadGameInfo( const char *filename, const char *gamedir, gameinfo_t *GameInfo, time_t mtime )
{
	char *afile = FS_LoadDirectFile( filename, NULL );

	if( !afile )
		return false;

	FS_InitGameInfo( GameInfo, gamedir, false, mtime );
	FS_ParseGenericGameInfo( GameInfo, afile, true );

	Mem_Free( afile );

	return true;
}

/*
================
FS_CheckForQuakeGameDir

Checks if game directory resembles Quake Engine game directory
(some of checks may as well work with Xash gamedirs, it's not a bug)
================
*/
static qboolean FS_CheckForQuakeGameDir( const char *gamedir )
{
	// if directory contain quake.rc or progs.dat it's 100% quake gamedir
	// quake mods probably always archived, so check pak0.pak too
	const char *files[] = { "progs.dat", "quake.rc" };
	char buf[MAX_SYSPATH];
	int i;

	// try to read pak0.pak first, most quake mods are archived
	if( Q_snprintf( buf, sizeof( buf ), "%s/pak0.pak", gamedir ) > 0 )
	{
		if( FS_SysFileExists( buf ))
		{
			if( FS_CheckForQuakePak( buf, files, sizeof( files ) / sizeof( files[0] )))
				return true;
		}
	}

	// search it in the filesystem
	for( i = 0; i < sizeof( files ) / sizeof( files[0] ); i++ )
	{
		if( Q_snprintf( buf, sizeof( buf ), "%s/%s", gamedir, files[i] ) > 0 )
		{
			if( FS_SysFileExists( buf ))
				return true;
		}
	}

	return false;
}

/*
===============
FS_CheckForXashGameDir

Checks if game directory resembles Xash3D game directory
===============
*/
static qboolean FS_CheckForXashGameDir( const char *gamedir )
{
	// if directory contain gameinfo.txt or liblist.gam it's 100% gamedir
	const char *files[] = { "gameinfo.txt", "liblist.gam" };
	int i;

	for( i = 0; i < sizeof( files ) / sizeof( files[0] ); i++ )
	{
		char buf[MAX_SYSPATH];

		if( Q_snprintf( buf, sizeof( buf ), "%s/%s", gamedir, files[i] ) > 0 )
		{
			if( FS_SysFileExists( buf ))
				return true;
		}
	}

	return false;
}

/*
================
FS_ParseGameInfo
================
*/
static qboolean FS_ParseGameInfo( const char *gamedir, gameinfo_t *GameInfo, qboolean rodir )
{
	char liblist_path[MAX_SYSPATH];
	char gameinfo_path[MAX_SYSPATH];
	char gamedir_path[MAX_SYSPATH];
	time_t liblist_mtime = -1;
	time_t gameinfo_mtime = -1;

	if( rodir )
		Q_snprintf( gamedir_path, sizeof( gamedir_path ), "%s/%s", fs_rodir, gamedir );
	else
		Q_snprintf( gamedir_path, sizeof( gamedir_path ), "%s", gamedir );

	if( !FS_CheckForXashGameDir( gamedir_path ))
	{
		// check if we need to generate gameinfo for Quake
		if( FS_CheckForQuakeGameDir( gamedir_path ))
		{
			// just generate stub gameinfo in memory
			FS_InitGameInfo( GameInfo, gamedir, true, -1 );
			GameInfo->rodir = rodir;
			return true;
		}

		// don't add empty or addon directories
		return false;
	}

	Q_snprintf( gameinfo_path, sizeof( gameinfo_path ), "%s/gameinfo.txt", gamedir_path );
	Q_snprintf( liblist_path, sizeof( liblist_path ), "%s/liblist.gam", gamedir_path );

	liblist_mtime = FS_SysFileTime( liblist_path );
	gameinfo_mtime = FS_SysFileTime( gameinfo_path );

	// in this function we never write new files for RoDir compatibility
	// since RoDir is only FWGS feature, do gameinfo.txt conversion only
	// for RwDir for those who worked with original Xash3D
	if( !rodir )
	{
		// !!!only if we have both liblist.gam and gameinfo.txt try to convert liblist.gam to gameinfo.txt if it's newer!!!
		if( liblist_mtime >= 0 && gameinfo_mtime >= 0 && liblist_mtime > gameinfo_mtime )
		{
			if( FS_ConvertGameInfo( gamedir, gameinfo_path, liblist_path, GameInfo, liblist_mtime ))
				return true;
		}
	}

	// can we parse gameinfo.txt?
	if( gameinfo_mtime >= 0 )
	{
		if( FS_ReadGameInfo( gameinfo_path, gamedir, GameInfo, gameinfo_mtime ))
		{
			GameInfo->rodir = rodir;
			return true;
		}
	}

	// can we parse liblist.gam?
	if( liblist_mtime >= 0 )
	{
		if( FS_ParseLiblistGam( liblist_path, gamedir, GameInfo, liblist_mtime ))
		{
			GameInfo->rodir = rodir;
			return true;
		}
	}

	return false;
}

/*
================
FS_AddGameHierarchy
================
*/
void FS_AddGameHierarchy( const char *dir, uint flags )
{
	int i;
	qboolean isGameDir = flags & FS_GAMEDIR_PATH;
	char buf[MAX_VA_STRING];

	if( COM_StringEmptyOrNULL( dir ))
		return;

	Con_Printf( "%s( %s )\n", __func__, dir );

	// add the common game directory

	// recursive gamedirs
	// for example, czeror->czero->cstrike->valve
	for( i = 0; i < FI.numgames; i++ )
	{
		if( !Q_stricmp( FI.games[i]->gamefolder, dir ))
		{
			dir = FI.games[i]->gamefolder; // fixup directory case

			if( FI.games[i]->added ) // already added, refusing
				break;

			// don't add our game directory as base dir to prevent cyclic dependency
			if( Q_stricmp( FI.games[i]->basedir, GI->gamefolder ))
				break;

			// don't add directory which basedir is equal to this gamefolder to prevent
			// endless loop
			if( Q_stricmp( FI.games[i]->basedir, FI.games[i]->gamefolder ))
				break;

			Con_Reportf( "%s: adding recursive basedir %s\n", __func__, FI.games[i]->basedir );
			FI.games[i]->added = true;
			FS_AddGameHierarchy( FI.games[i]->basedir, flags & (~FS_GAMEDIR_PATH));
			break;
		}
	}

	if( !COM_StringEmpty( fs_rodir ))
	{
		// append new flags to rodir, except FS_GAMEDIR_PATH and FS_CUSTOM_PATH
		uint new_flags = FS_NOWRITE_PATH | (flags & (~FS_GAMEDIR_PATH|FS_CUSTOM_PATH));
		if( isGameDir )
			SetBits( new_flags, FS_GAMERODIR_PATH );

		FS_AllowDirectPaths( true );
		Q_snprintf( buf, sizeof( buf ), "%s/%s/", fs_rodir, dir );
		FS_AddGameDirectory( buf, new_flags );
		FS_AllowDirectPaths( false );
	}

	if( isGameDir )
	{
		Q_snprintf( buf, sizeof( buf ), "%s/" DEFAULT_DOWNLOADED_DIRECTORY, dir );
		FS_AddGameDirectory( buf, FS_NOWRITE_PATH|FS_CUSTOM_PATH );
	}
	Q_snprintf( buf, sizeof( buf ), "%s/", dir );
	FS_AddGameDirectory( buf, flags );

	if( FBitSet( flags, FS_MOUNT_HD ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_hd/", dir );
		FS_AddGameDirectory( buf, flags|FS_NOWRITE_PATH|FS_CUSTOM_PATH );
	}

	if( FBitSet( flags, FS_MOUNT_ADDON ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_addon/", dir );
		FS_AddGameDirectory( buf, flags|FS_NOWRITE_PATH|FS_CUSTOM_PATH );
	}

	if( FBitSet( flags, FS_MOUNT_LV ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_lv/", dir );
		FS_AddGameDirectory( buf, flags|FS_NOWRITE_PATH|FS_CUSTOM_PATH );
	}

	if( FBitSet( flags, FS_MOUNT_L10N ) && !COM_StringEmpty( fs_language ) && Q_isalpha( fs_language ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_%s/", dir, fs_language );
		FS_AddGameDirectory( buf, flags|FS_NOWRITE_PATH|FS_CUSTOM_PATH );
	}

	if( isGameDir )
	{
		Q_snprintf( buf, sizeof( buf ), "%s/" DEFAULT_CUSTOM_DIRECTORY, dir );
		FS_AddGameDirectory( buf, FS_NOWRITE_PATH|FS_CUSTOM_PATH );
	}
}

/*
================
FS_Rescan
================
*/
void FS_Rescan( uint32_t flags, const char *language )
{
	const char *str;
	Con_Reportf( "%s( %s )\n", __func__, GI->title );

	FS_ClearSearchPath();

	flags &= FS_MOUNT_HD|FS_MOUNT_LV|FS_MOUNT_ADDON|FS_MOUNT_L10N;

	if( FBitSet( flags, FS_MOUNT_L10N ))
		Q_strncpy( fs_language, language, sizeof( fs_language ));
	else
		fs_language[0] = 0;

	str = getenv( "XASH3D_EXTRAS_PAK1" );
	if( !COM_StringEmptyOrNULL( str ))
		FS_MountArchive_Fullpath( str, FS_NOWRITE_PATH|FS_CUSTOM_PATH );

	str = getenv( "XASH3D_EXTRAS_PAK2" );
	if( !COM_StringEmptyOrNULL( str ))
		FS_MountArchive_Fullpath( str, FS_NOWRITE_PATH|FS_CUSTOM_PATH );

	if( Q_stricmp( GI->basedir, GI->gamefolder ))
		FS_AddGameHierarchy( GI->basedir, flags );
	if( Q_stricmp( GI->basedir, GI->falldir ) && Q_stricmp( GI->gamefolder, GI->falldir ))
		FS_AddGameHierarchy( GI->falldir, flags );

	((gameinfo_t *)GI)->added = true; // getting rid of const here, as this modifier only for the engine
	FS_AddGameHierarchy( GI->gamefolder, FS_GAMEDIR_PATH | flags );
}

/*
===============
FS_Gamedir

Allows engine to know game directory before gameinfo is initialized
===============
*/
static const char *FS_Gamedir( void )
{
	if( GI )
		return GI->gamefolder;

	return fs_gamedir;
}

/*
================
FS_LoadGameInfo

can be passed null arg
================
*/
static void FS_LoadGameInfo( uint32_t flags, const char *language )
{
	int	i;

	// lock uplevel of gamedir for read\write
	FS_AllowDirectPaths( false );

	Con_Reportf( "%s( %s, 0x%x, %s )\n", __func__, fs_gamedir, flags, language );

	// clear any old paths
	FS_ClearSearchPath();

	// validate gamedir
	for( i = 0; i < FI.numgames; i++ )
	{
		if( !Q_stricmp( FI.games[i]->gamefolder, fs_gamedir ))
			break;
	}

	if( i == FI.numgames )
		Sys_Error( "Couldn't find game directory '%s'\n", fs_gamedir );

	FI.GameInfo = FI.games[i];

	if( FI.GameInfo->rodir )
	{
		// ensure we have directory in rwdir
		char buf[MAX_SYSPATH + MAX_QPATH + 2]; // and have plenty of space
		Q_snprintf( buf, sizeof( buf ), "%s/%s/", fs_rootdir, FI.GameInfo->gamefolder );
		FS_CreatePath( buf );
	}

	FS_Rescan( flags, language ); // create new filesystem
}

/*
==================
FS_CheckForCrypt

return true if library is crypted
==================
*/
static qboolean FS_CheckForCrypt( const char *dllname )
{
	file_t	*f;
	int	key;

	// this encryption is specific to DLLs
	if( Q_stricmp( COM_FileExtension( dllname ), "dll" ))
		return false;

	f = FS_Open( dllname, "rb", false );
	if( !f ) return false;

	FS_Seek( f, 64, SEEK_SET );	// skip first 64 bytes
	FS_Read( f, &key, sizeof( key ));
	FS_Close( f );

	return ( key == LittleLong( 0x12345678 )) ? true : false;
}

static int FS_StripIdiotRelativePath( const char *dllname, const char *gamefolder )
{
	string idiot_relpath;
	int len;

	if(( len = Q_snprintf( idiot_relpath, sizeof( idiot_relpath ), "../%s/", gamefolder )) >= 4 )
	{
		if( !Q_strnicmp( dllname, idiot_relpath, len ))
			return len;

		// try backslashes
		idiot_relpath[1] = '\\';
		idiot_relpath[len - 1] = '\\';
		if( !Q_strnicmp( dllname, idiot_relpath, len ))
			return len;
	}

	return 0;
}

/*
==================
FS_FindLibrary

search for library, assume index is valid
==================
*/
static qboolean FS_FindLibrary( const char *dllname, qboolean directpath, fs_dllinfo_t *dllInfo )
{
	string fixedname;
	searchpath_t	*search;
	int index, start = 0, len;

	// check for bad exports
	if( COM_StringEmptyOrNULL( dllname ))
		return false;

	FS_AllowDirectPaths( directpath );

	// HACKHACK remove relative path to game folder
	if( !Q_strnicmp( dllname, "..", 2 ))
	{
		// some modders put relative path to themselves???
		len = FS_StripIdiotRelativePath( dllname, GI->gamefolder );

		if( len == 0 ) // or put relative path to Half-Life game libs
			len = FS_StripIdiotRelativePath( dllname, "valve" );
		start += len;
	}

	Q_strnlwr( &dllname[start], dllInfo->shortPath, sizeof( dllInfo->shortPath )); // always in lower case (why?)
	COM_FixSlashes( dllInfo->shortPath ); // replace all backward slashes
	COM_DefaultExtension( dllInfo->shortPath, "."OS_LIB_EXT, sizeof( dllInfo->shortPath ));	// apply ext if forget

	search = FS_FindFile( dllInfo->shortPath, &index, fixedname, sizeof( fixedname ), false );

	if( search )
	{
		Q_strncpy( dllInfo->shortPath, fixedname, sizeof( dllInfo->shortPath ));
	}
	else if( !directpath )
	{
		FS_AllowDirectPaths( false );

		// trying check also 'bin' folder for indirect paths
		search = FS_FindFile( dllname, &index, fixedname, sizeof( fixedname ), false );
		if( !search )
			return false; // unable to find

		Q_strncpy( dllInfo->shortPath, fixedname, sizeof( dllInfo->shortPath ));
	}

	dllInfo->encrypted = dllInfo->custom_loader = false; // predict state

	if( search && index >= 0 ) // when library is available through VFS
	{
		dllInfo->encrypted = FS_CheckForCrypt( dllInfo->shortPath );

		if( search->type == SEARCHPATH_PLAIN ) // is it on the disk? (intentionally omit pk3dir here)
		{
			// NOTE: gamedll might resolve it's own path using dladdr() and expects absolute path
			// NOTE: the only allowed case when searchpath is set by absolute path is the RoDir
			// rather than figuring out whether path is absolute, just check if it matches
			if( !COM_StringEmpty( fs_rodir ) && !Q_strnicmp( search->filename, fs_rodir, Q_strlen( fs_rodir )))
			{
				Q_snprintf( dllInfo->fullPath, sizeof( dllInfo->fullPath ), "%s%s", search->filename, dllInfo->shortPath );
			}
			else
			{
				Q_snprintf( dllInfo->fullPath, sizeof( dllInfo->fullPath ), "%s/%s%s", fs_rootdir, search->filename, dllInfo->shortPath );
			}
		}
		else
		{
			Q_snprintf( dllInfo->fullPath, sizeof( dllInfo->fullPath ), "%s", dllInfo->shortPath );
			Con_Printf( "%s%s: loading libraries from archives is %s\n",
#if XASH_WIN32 && XASH_X86 // a1ba: custom loader is non-portable (I just don't want to touch it)
				S_WARN, __func__, "non portable and might fail on other platforms"
#else
				S_ERROR, __func__, "unsupported on this platform"
#endif
			);

			dllInfo->custom_loader = true;
		}
	}
	else
	{
		// NOTE: if search is NULL let OS to find the library
		Q_strncpy( dllInfo->fullPath, dllInfo->shortPath, sizeof( dllInfo->fullPath ));
	}

	FS_AllowDirectPaths( false ); // always reset direct paths

	return true;
}

static poolhandle_t Mem_AllocPoolStub( const char *name, const char *filename, int fileline )
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

static void *Sys_GetNativeObjectStub( const char *object )
{
	return NULL;
}

static void FS_ValidateDirectories( const char *path, qboolean *has_base_dir, qboolean *has_game_dir )
{
	stringlist_t dirs;
	int i;

	stringlistinit( &dirs );
	listdirectory( &dirs, path, true );
	stringlistsort( &dirs );

	for( i = 0; i < dirs.numstrings; i++ )
	{
		if( !FS_SysFolderExists( dirs.strings[i] ))
			continue;

		if( !Q_stricmp( fs_basedir, dirs.strings[i] ))
			*has_base_dir = true;

		if( !Q_stricmp( fs_gamedir, dirs.strings[i] ))
			*has_game_dir = true;

		if( *has_base_dir && *has_game_dir )
			break;
	}

	stringlistfreecontents( &dirs );
}

/*
================
FS_Init
================
*/
qboolean FS_InitStdio( qboolean unused_set_to_true, const char *rootdir, const char *basedir, const char *gamedir, const char *rodir )
{
	stringlist_t dirs;
	int i, rodir_num_games;
	char buf[MAX_SYSPATH];

	FS_InitMemory();

#if XASH_ANDROID
	FS_InitAndroid();
#endif

	Q_strncpy( fs_rootdir, rootdir, sizeof( fs_rootdir ));
	Q_strncpy( fs_gamedir, gamedir, sizeof( fs_gamedir ));
	Q_strncpy( fs_basedir, basedir, sizeof( fs_basedir ));
	Q_strncpy( fs_rodir, rodir, sizeof( fs_rodir ));
	fs_language[0] = 0;

	// validate user input
	if( !COM_StringEmpty( fs_rodir ) && !Q_stricmp( fs_rodir, fs_rootdir ))
	{
		Sys_Error( "RoDir and default rootdir can't point to same directory!" );
		return false;
	}

	// look for game directories in RwDir first
	// this whole check only required to fallback to base directory
	// (i.e. default game directory hardcoded in the executable file)
	{
		qboolean has_base_dir = false;
		qboolean has_game_dir = false;

		FS_ValidateDirectories( "./", &has_base_dir, &has_game_dir );

		if( !has_game_dir )
		{
			// look for game directories in RoDir now
			if( !COM_StringEmpty( fs_rodir ))
				FS_ValidateDirectories( fs_rodir, &has_base_dir, &has_game_dir );

			if( !has_game_dir )
			{
				Con_Printf( S_ERROR "game directory \"%s\" not exist\n", fs_gamedir );

				// revert to base game directory
				if( has_base_dir )
					Q_strncpy( fs_gamedir, fs_basedir, sizeof( fs_gamedir ));
			}
		}
	}

	// now start building first level of directory hierarchy
	if( !COM_StringEmpty( fs_rodir ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s/", fs_rodir );
		FS_AddGameDirectory( buf, FS_STATIC_PATH|FS_NOWRITE_PATH );
	}
	FS_AddGameDirectory( "./", FS_STATIC_PATH );

	// but scan rodir for games first
	if( !COM_StringEmpty( fs_rodir ))
	{
		stringlistinit( &dirs );
		listdirectory( &dirs, fs_rodir, true );
		stringlistsort( &dirs );

		for( i = 0; i < dirs.numstrings; i++ )
		{
			Q_snprintf( buf, sizeof( buf ), "%s/%s", fs_rodir, dirs.strings[i] );
			if( !FS_SysFolderExists( buf ))
				continue;

			if( FI.games[FI.numgames] == NULL )
				FI.games[FI.numgames] = Mem_Malloc( fs_mempool, sizeof( *FI.games[FI.numgames] ));

			if( FS_ParseGameInfo( dirs.strings[i], FI.games[FI.numgames], true ))
				FI.numgames++;
		}

		stringlistfreecontents( &dirs );
	}

	rodir_num_games = FI.numgames;

	stringlistinit( &dirs );
	listdirectory( &dirs, "./", true );
	stringlistsort( &dirs );

	for( i = 0; i < dirs.numstrings; i++ )
	{
		int j;

		if( !FS_SysFolderExists( dirs.strings[i] ))
			continue;

		// update gameinfo from rwdir, if it's newer
		// with rodir we should never create new gameinfos anymore,
		// so this only catches possible user changes
		for( j = 0; j < rodir_num_games; j++ )
		{
			if( !Q_stricmp( dirs.strings[i], FI.games[j]->gamefolder ))
			{
				gameinfo_t gi;
				if( FS_ParseGameInfo( dirs.strings[i], &gi, false ))
				{
					if( gi.mtime > FI.games[j]->mtime )
						*FI.games[j] = gi;
				}
				break;
			}
		}

		if( FI.games[FI.numgames] == NULL )
			FI.games[FI.numgames] = Mem_Calloc( fs_mempool, sizeof( *FI.games[FI.numgames] ));

		if( FS_ParseGameInfo( dirs.strings[i], FI.games[FI.numgames], false ))
			FI.numgames++; // added
	}

	stringlistfreecontents( &dirs );

	Con_Reportf( "%s: done\n", __func__ );

	return true;
}

void FS_AllowDirectPaths( qboolean enable )
{
	fs_ext_path = enable;
}

/*
================
FS_Shutdown
================
*/
void FS_ShutdownStdio( void )
{
	int i;

	// release gamedirs
	for( i = 0; i < FI.numgames; i++ )
	{
		if( FI.games[i] )
		{
			Mem_Free( FI.games[i] );
			FI.games[i] = NULL;
		}
	}
	FI.numgames = 0;

	FS_ClearSearchPath(); // release all wad files too
	Mem_FreePool( &fs_mempool );
}

/*
============
FS_Path_f

debug info
============
*/
void FS_Path_f( void )
{
	searchpath_t	*s;

	Con_Printf( "Current search path:\n" );

	for( s = fs_searchpaths; s; s = s->next )
	{
		string info;

		s->pfnPrintInfo( s, info, sizeof(info) );

		Con_Printf( "%s", info );

		if( s->flags & FS_GAMERODIR_PATH ) Con_Printf( " ^2rodir^7" );
		if( s->flags & FS_GAMEDIR_PATH ) Con_Printf( " ^2gamedir^7" );
		if( s->flags & FS_CUSTOM_PATH ) Con_Printf( " ^2custom^7" );
		if( s->flags & FS_NOWRITE_PATH ) Con_Printf( " ^2nowrite^7" );
		if( s->flags & FS_STATIC_PATH ) Con_Printf( " ^2static^7" );

		Con_Printf( "\n" );
	}
}

/*
====================
FS_SysFileTime

Internal function used to determine filetime
====================
*/
int FS_SysFileTime( const char *filename )
{
#if XASH_WIN32
	struct _stat buf;
	if( _wstat( FS_PathToWideChar( filename ), &buf ) < 0 )
#else
	struct stat buf;
	if( stat( filename, &buf ) < 0 )
#endif
		return -1;

	return buf.st_mtime;
}

/*
====================
FS_SysOpen

Internal function used to create a file_t and open the relevant non-packed file on disk
====================
*/
file_t *FS_SysOpen( const char *filepath, const char *mode )
{
	file_t *file;
	int mod, opt, fd = -1;
	qboolean memfile = false;
	uint ind;

	// Parse the mode string
	switch( mode[0] )
	{
	case 'r':	// read
		mod = O_RDONLY;
		opt = 0;
		break;
	case 'w': // write
		mod = O_WRONLY;
		opt = O_CREAT | O_TRUNC;
		break;
	case 'a': // append
		mod = O_WRONLY;
		opt = O_CREAT | O_APPEND;
		break;
	case 'e': // edit
		mod = O_WRONLY;
		opt = O_CREAT;
		break;
	default:
		return NULL;
	}

	for( ind = 1; mode[ind] != '\0'; ind++ )
	{
		switch( mode[ind] )
		{
		case '+':
			mod = O_RDWR;
			break;
		case 'b':
			opt |= O_BINARY;
			break;
		default:
			break;
		}
	}

	// the 'm' flag let's user to create temporary file in memory
	// through so-called "anonymous files"
	if( Q_strchr( mode, 'm' ))
	{
#if HAVE_MEMFD_CREATE
#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 8U
#endif
		fd = memfd_create( filepath, MFD_CLOEXEC | MFD_NOEXEC_SEAL );

		// through fcntl() and MFD_ALLOW_SEALING we could enforce
		// read-write flags but we don't really care about them yet
		if( fd < 0 )
			Con_Printf( S_WARN "%s: can't create anonymous file %s: %s\n", __func__, filepath, strerror( errno ));
		else memfile = true;
#endif
		// if it's unsupported, we can open it on disk
	}

	if( fd < 0 )
	{
#if XASH_WIN32
		fd = _wopen( FS_PathToWideChar( filepath ), mod | opt, 0666 );
#else
		fd = open( filepath, mod|opt, 0666 );
#endif
	}

	if( fd < 0 )
	{
		if( errno != ENOENT )
			Con_Printf( S_ERROR "%s: can't open file %s: %s\n", __func__, filepath, strerror( errno ));

		return NULL;
	}

	file = (file_t *)Mem_Calloc( fs_mempool, sizeof( *file ));
	file->filetime = memfile ? 0 : FS_SysFileTime( filepath );
	file->ungetc = EOF;
	file->handle = fd;

#if !XASH_WIN32
	if( !memfile )
		FS_BackupFileName( file, filepath, mod|opt );
#endif

	file->searchpath = NULL;
	file->real_length = lseek( file->handle, 0, SEEK_END );

	// uncomment do disable write
	//if( opt & O_CREAT )
	//	return NULL;

	// For files opened in append mode, we start at the end of the file
	if( opt & O_APPEND )  file->position = file->real_length;
	else lseek( file->handle, 0, SEEK_SET );

	return file;
}
/*
static int FS_DuplicateHandle( const char *filename, int handle, fs_offset_t pos )
{
#ifdef HAVE_DUP
	return dup( handle );
#else
	int newhandle = open( filename, O_RDONLY|O_BINARY );
	lseek( newhandle, pos, SEEK_SET );
	return newhandle;
#endif
}
*/

file_t *FS_OpenHandle( searchpath_t *searchpath, int handle, fs_offset_t offset, fs_offset_t len )
{
	file_t *file = (file_t *)Mem_Calloc( fs_mempool, sizeof( file_t ));
#ifndef XASH_REDUCE_FD
#ifdef HAVE_DUP
	file->handle = dup( handle );
#else
	file->handle = open( searchpath->filename, O_RDONLY|O_BINARY );
#endif

	if( file->handle < 0 )
	{
		Con_Printf( S_ERROR "%s: couldn't create fd for %s:0x%lx: %s\n", __func__, searchpath->filename, (long)offset, strerror( errno ));
		Mem_Free( file );
		return NULL;
	}

	if( lseek( file->handle, offset, SEEK_SET ) == -1 )
	{
		Mem_Free( file );
		return NULL;
	}

#else
	file->backup_position = offset;
	file->backup_path = copystring( syspath );
	file->backup_options = O_RDONLY|O_BINARY;
	file->handle = -1;
#endif

	file->real_length = len;
	file->offset = offset;
	file->position = 0;
	file->ungetc = EOF;
	file->searchpath = searchpath;

	return file;
}

#if !defined( S_ISREG )
#define S_ISREG( m ) ( FBitSet( m, S_IFMT ) == S_IFREG )
#endif

#if !defined( S_ISDIR )
#define S_ISDIR( m ) ( FBitSet( m, S_IFMT ) == S_IFDIR )
#endif

/*
==================
FS_SysFileExists

Look for a file in the filesystem only
==================
*/
qboolean FS_SysFileExists( const char *path )
{
#if XASH_WIN32
	struct _stat buf;
	if( _wstat( FS_PathToWideChar( path ), &buf ) < 0 )
#else
	struct stat buf;
	if( stat( path, &buf ) < 0 )
#endif
		return false;

	return S_ISREG( buf.st_mode );
}

/*
==================
FS_SysFolderExists

Look for a existing folder
==================
*/
qboolean FS_SysFolderExists( const char *path )
{
#if XASH_WIN32
	struct _stat buf;
	if( _wstat( FS_PathToWideChar( path ), &buf ) < 0 )
#else
	struct stat buf;
	if( stat( path, &buf ) < 0 )
#endif
		return false;

	return S_ISDIR( buf.st_mode );
}

/*
==============
FS_SysFileOrFolderExists

Check if filesystem entry exists at all, don't mind the type
==============
*/
qboolean FS_SysFileOrFolderExists( const char *path )
{
#if XASH_WIN32
	struct _stat buf;
	return _wstat( FS_PathToWideChar( path ), &buf ) >= 0;
#else
	struct stat buf;
	return stat( path, &buf ) >= 0;
#endif
}

/*
==================
FS_SetCurrentDirectory

Sets current directory, path should be in UTF-8 encoding
TODO: make this non-fatal
==================
*/
int FS_SetCurrentDirectory( const char *path )
{
#if XASH_WIN32
	if( !SetCurrentDirectoryW( FS_PathToWideChar( path )))
	{
		const DWORD fm_flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK;
		DWORD errorcode;
		wchar_t wide_buf[1024];
		char buf[1024];

		FormatMessageW( fm_flags, NULL, GetLastError(), 0, wide_buf, sizeof( wide_buf ) / sizeof( wide_buf[0] ), NULL );
		Q_UTF16ToUTF8( buf, sizeof( buf ), wide_buf, sizeof( wide_buf ) / sizeof( wide_buf[0] ));

		Sys_Error( "Changing directory to %s failed: %s\n", path, buf );
		return false;
	}
#elif XASH_POSIX
	if( chdir( path ) < 0 )
	{
		Sys_Error( "Changing directory to %s failed: %s\n", path, strerror( errno ));
		return false;
	}
#else
	// it may be fine for some systems to skip chdir
	Con_Printf( "%s: not implemented, ignoring...\n", __func__ );
	return true;
#endif

	Con_Printf( "%s is working directory now\n", path );
	return true;
}

/*
==================
FS_GetRootDirectory

Returns writable root directory path
==================
*/
qboolean FS_GetRootDirectory( char *path, size_t size )
{
	size_t dirlen = Q_strlen( fs_rootdir );

	if( dirlen >= size ) // check for possible overflow
		return false;

	Q_strncpy( path, fs_rootdir, size );
	return true;
}

/*
====================
FS_FindFile

Look for a file in the packages and in the filesystem

Return the searchpath where the file was found (or NULL)
and the file index in the package if relevant
====================
*/
searchpath_t *FS_FindFile( const char *name, int *index, char *fixedname, size_t len, qboolean gamedironly )
{
	searchpath_t	*search;

	// search through the path, one element at a time
	for( search = fs_searchpaths; search; search = search->next )
	{
		int pack_ind;

		if( gamedironly & !FBitSet( search->flags, FS_GAMEDIRONLY_SEARCH_FLAGS ))
			continue;

		pack_ind = search->pfnFindFile( search, name, fixedname, len );
		if( pack_ind >= 0 )
		{
			if( index )
				*index = pack_ind;
			return search;
		}
	}

	if( fs_ext_path )
	{
		char netpath[MAX_SYSPATH], dirpath[MAX_SYSPATH];

		// HACKHACK: when the code wants to access to root game directory
		// it often uses ../ in conjunction with FS_AllowDirectPath
		// it results in the access above the root game directory
		// FS_Open with "write" flag doesn't have this problem because
		// it looks up relative to fs_writepath
		// the correct solution MIGHT be using fs_writepath instead of fs_rootdir here?
		// but this need to be properly tested so as a temporary solution
		// just strip ../
		if( !Q_strncmp( name, "../", 3 ))
			name += 3;

		Q_snprintf( dirpath, sizeof( dirpath ), "%s/", fs_rootdir );
		Q_snprintf( netpath, sizeof( netpath ), "%s%s", dirpath, name );

		if( FS_SysFileExists( netpath ))
		{
			static searchpath_t fs_directpath;

			// clear old dir cache, if needed
			if( 0 != Q_strcmp( fs_directpath.filename, dirpath ))
			{
				if( fs_directpath.pfnClose )
					fs_directpath.pfnClose( &fs_directpath );
				FS_InitDirectorySearchpath( &fs_directpath, dirpath, 0 );
			}

			// just copy the name, we don't do case sensitivity fix there
			if( fixedname )
				Q_strncpy( fixedname, name, len );

			if( index != NULL )
				*index = 0;

			return &fs_directpath;
		}
	}

	if( index != NULL )
		*index = -1;

	return NULL;
}

/*
===========================
FS_FullPathToRelativePath

Converts full path to the relative path considering current searchpaths
(do not use this function, implemented only for VFileSystem009)
===========================
*/
qboolean FS_FullPathToRelativePath( char *dst, const char *src, size_t size )
{
	searchpath_t *sp;

	for( sp = fs_searchpaths; sp; sp = sp->next )
	{
		size_t splen = Q_strlen( sp->filename );

		if( !Q_strnicmp( sp->filename, src, splen ))
		{
			Q_strncpy( dst, src + splen + 1, size );
			return true;
		}
	}

	Q_strncpy( dst, src, size );
	return false;
}


/*
===========
FS_OpenReadFile

Look for a file in the search paths and open it in read-only mode
===========
*/
file_t *FS_OpenReadFile( const char *filename, const char *mode, qboolean gamedironly )
{
	searchpath_t *search;
	char netpath[MAX_SYSPATH];
	int pack_ind;

	search = FS_FindFile( filename, &pack_ind, netpath, sizeof( netpath ), gamedironly );

	// not found?
	if( search == NULL )
		return NULL;

	return search->pfnOpenFile( search, netpath, mode, pack_ind );
}

/*
=============================================================================

MAIN PUBLIC FUNCTIONS

=============================================================================
*/
/*
====================
FS_Open

Open a file. The syntax is the same as fopen
====================
*/
file_t *FS_Open( const char *filepath, const char *mode, qboolean gamedironly )
{
	if( !fs_searchpaths )
		return NULL;

	// some mappers used leading '/' or '\' in path to models or sounds
	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( FS_CheckNastyPath( filepath ))
		return NULL;

	// if the file is opened in "write", "append", or "read/write" mode
	if( mode[0] == 'w' || mode[0] == 'a'|| mode[0] == 'e' || Q_strchr( mode, '+' ))
	{
		char	real_path[MAX_SYSPATH];

		// open the file on disk directly
		if( !FS_FixFileCase( fs_writepath->dir, filepath, real_path, sizeof( real_path ), true ))
			return NULL;

		FS_CreatePath( real_path ); // Create directories up to the file

		return FS_SysOpen( real_path, mode );
	}

	// else, we look at the various search paths and open the file in read-only mode
	return FS_OpenReadFile( filepath, mode, gamedironly );
}

/*
====================
FS_Close

Close a file
====================
*/
int FS_Close( file_t *file )
{
	if( !file ) return 0;

	FS_BackupFileName( file, NULL, 0 );

	if( file->handle >= 0 )
	{
		if( close( file->handle ))
			return EOF;
	}

	if( file->ztk )
	{
		inflateEnd( &file->ztk->zstream );
		Mem_Free( file->ztk );
	}

	Mem_Free( file );
	return 0;
}

/*
====================
FS_Flush

flushes written data to disk
====================
*/
int FS_Flush( file_t *file )
{
	if( !file ) return 0;

	// purge cached data
	FS_Purge( file );

	// sync
#if XASH_POSIX
	if( fsync( file->handle ) < 0 )
		return EOF;
#else
	if( _commit( file->handle ) < 0 )
		return EOF;
#endif

	return 0;
}

/*
====================
FS_Write

Write "datasize" bytes into a file
====================
*/
fs_offset_t FS_Write( file_t *file, const void *data, size_t datasize )
{
	fs_offset_t	result;

	if( !file ) return 0;

	// if necessary, seek to the exact file position we're supposed to be
	if( file->buff_ind != file->buff_len )
		lseek( file->handle, file->buff_ind - file->buff_len, SEEK_CUR );

	// purge cached data
	FS_Purge( file );

	// write the buffer and update the position
	result = write( file->handle, data, datasize );
	file->position = lseek( file->handle, 0, SEEK_CUR );

	if( file->real_length < file->position )
		file->real_length = file->position;

	if( result < 0 )
		return 0;
	return result;
}

/*
====================
FS_Read

Read up to "buffersize" bytes from a file
====================
*/
fs_offset_t FS_Read( file_t *file, void *buffer, size_t buffersize )
{
	fs_offset_t	done;
	fs_offset_t	nb;
	fs_offset_t	count;

	// nothing to copy
	if( buffersize == 0 ) return 1;

	// Get rid of the ungetc character
	if( file->ungetc != EOF )
	{
		((char*)buffer)[0] = file->ungetc;
		buffersize--;
		file->ungetc = EOF;
		done = 1;
	}
	else done = 0;

	// first, we copy as many bytes as we can from "buff"
	if( file->buff_ind < file->buff_len )
	{
		count = file->buff_len - file->buff_ind;
		count = ( buffersize > count ) ? count : (fs_offset_t)buffersize;

		done += count;
		memcpy( buffer, &file->buff[file->buff_ind], count );
		file->buff_ind += count;

		buffersize -= count;
		if( buffersize == 0 )
			return done;
	}

	// NOTE: at this point, the read buffer is always empty

	FS_EnsureOpenFile( file ); // FIXME: broken XASH_REDUCE_FD in case of compressed files!

	if( FBitSet( file->flags, FILE_DEFLATED ))
	{
		// If the file is compressed, it's more complicated...
		// We cycle through a few operations until we have read enough data
		while( buffersize > 0 )
		{
			ztoolkit_t *ztk = file->ztk;
			int error;

			// NOTE: at this point, the read buffer is always empty

			// If "input" is also empty, we need to refill it
			if( ztk->in_ind == ztk->in_len )
			{
				// If we are at the end of the file
				if( file->position == file->real_length )
					return done;

				count = (fs_offset_t)( ztk->comp_length - ztk->in_position );
				if( count > (fs_offset_t)sizeof( ztk->input ))
					count = (fs_offset_t)sizeof( ztk->input );
				lseek( file->handle, file->offset + (fs_offset_t)ztk->in_position, SEEK_SET );
				if( read( file->handle, ztk->input, count ) != count )
				{
					Con_Printf( "%s: unexpected end of file\n", __func__ );
					break;
				}

				ztk->in_ind = 0;
				ztk->in_len = count;
				ztk->in_position += count;
			}

			ztk->zstream.next_in = &ztk->input[ztk->in_ind];
			ztk->zstream.avail_in = (unsigned int)( ztk->in_len - ztk->in_ind );

			// Now that we are sure we have compressed data available, we need to determine
			// if it's better to inflate it in "file->buff" or directly in "buffer"

			// Inflate the data in "file->buff"
			if( buffersize < sizeof( file->buff ) / 2 )
			{
				ztk->zstream.next_out = file->buff;
				ztk->zstream.avail_out = sizeof( file->buff );
			}
			else
			{
				ztk->zstream.next_out = &((unsigned char*)buffer)[done];
				ztk->zstream.avail_out = (unsigned int)buffersize;
			}

			error = inflate( &ztk->zstream, Z_SYNC_FLUSH );
			if( error != Z_OK && error != Z_STREAM_END )
			{
				Con_Printf( "%s: Can't inflate file (%d)\n", __func__, error );
				break;
			}
			ztk->in_ind = ztk->in_len - ztk->zstream.avail_in;

			if( buffersize < sizeof( file->buff ) / 2 )
			{
				file->buff_len = (fs_offset_t)sizeof( file->buff ) - ztk->zstream.avail_out;
				file->position += file->buff_len;

				// Copy the requested data in "buffer" (as much as we can)
				count = (fs_offset_t)buffersize > file->buff_len ? file->buff_len : (fs_offset_t)buffersize;
				memcpy( &((unsigned char*)buffer)[done], file->buff, count );
				file->buff_ind = count;
			}
			else
			{
				count = (fs_offset_t)( buffersize - ztk->zstream.avail_out );
				file->position += count;

				// Purge cached data
				FS_Purge( file );
			}

			done += count;
			buffersize -= count;
		}

		return done;
	}

	// we must take care to not read after the end of the file
	count = file->real_length - file->position;

	// if we have a lot of data to get, put them directly into "buffer"
	if( buffersize > sizeof( file->buff ) / 2 )
	{
		if( count > (fs_offset_t)buffersize )
			count = (fs_offset_t)buffersize;
		lseek( file->handle, file->offset + file->position, SEEK_SET );
		nb = read( file->handle, &((byte *)buffer)[done], count );

		if( nb > 0 )
		{
			done += nb;
			file->position += nb;
			// purge cached data
			FS_Purge( file );
		}
	}
	else
	{
		if( count > (fs_offset_t)sizeof( file->buff ))
			count = (fs_offset_t)sizeof( file->buff );
		lseek( file->handle, file->offset + file->position, SEEK_SET );
		nb = read( file->handle, file->buff, count );

		if( nb > 0 )
		{
			file->buff_len = nb;
			file->position += nb;

			// copy the requested data in "buffer" (as much as we can)
			count = (fs_offset_t)buffersize > file->buff_len ? file->buff_len : (fs_offset_t)buffersize;
			memcpy( &((byte *)buffer)[done], file->buff, count );
			file->buff_ind = count;
			done += count;
		}
	}

	return done;
}

/*
====================
FS_Print

Print a string into a file
====================
*/
int FS_Print( file_t *file, const char *msg )
{
	return FS_Write( file, msg, Q_strlen( msg ));
}

/*
====================
FS_Printf

Print a string into a file
====================
*/
int FS_Printf( file_t *file, const char *format, ... )
{
	int	result;
	va_list	args;

	va_start( args, format );
	result = FS_VPrintf( file, format, args );
	va_end( args );

	return result;
}

/*
====================
FS_VPrintf

Print a string into a file
====================
*/
int FS_VPrintf( file_t *file, const char *format, va_list ap )
{
	int	len;
	fs_offset_t	buff_size = MAX_SYSPATH;
	char	*tempbuff;

	if( !file ) return 0;

	while( 1 )
	{
		tempbuff = (char *)Mem_Malloc( fs_mempool, buff_size );
		len = Q_vsnprintf( tempbuff, buff_size, format, ap );

		if( len >= 0 && len < buff_size )
			break;

		Mem_Free( tempbuff );
		buff_size *= 2;
	}

	len = write( file->handle, tempbuff, len );
	Mem_Free( tempbuff );

	return len;
}

/*
====================
FS_Getc

Get the next character of a file
====================
*/
int FS_Getc( file_t *file )
{
	char	c;

	if( FS_Read( file, &c, 1 ) != 1 )
		return EOF;

	return c;
}

/*
====================
FS_UnGetc

Put a character back into the read buffer (only supports one character!)
====================
*/
int FS_UnGetc( file_t *file, char c )
{
	// If there's already a character waiting to be read
	if( file->ungetc != EOF )
		return EOF;

	file->ungetc = c;
	return c;
}

/*
====================
FS_Gets

Same as fgets
====================
*/
int FS_Gets( file_t *file, char *string, size_t bufsize )
{
	int	c, end = 0;

	while( 1 )
	{
		c = FS_Getc( file );

		if( c == '\r' || c == '\n' || c < 0 )
			break;

		if( end < bufsize - 1 )
			string[end++] = c;
	}
	string[end] = 0;

	// remove \n following \r
	if( c == '\r' )
	{
		c = FS_Getc( file );

		if( c != '\n' )
			FS_UnGetc( file, c );
	}

	return c;
}

/*
====================
FS_Seek

Move the position index in a file
NOTE: when porting code, check return value!
NOTE: it's not compatible with lseek!
====================
*/
int FS_Seek( file_t *file, fs_offset_t offset, int whence )
{
	// compute the file offset
	switch( whence )
	{
	case SEEK_CUR:
		offset += file->position - file->buff_len + file->buff_ind;
		break;
	case SEEK_SET:
		break;
	case SEEK_END:
		offset += file->real_length;
		break;
	default:
		return -1;
	}

	if( offset < 0 || offset > file->real_length )
		return -1;

	// if we have the data in our read buffer, we don't need to actually seek
	if( file->position - file->buff_len <= offset && offset <= file->position )
	{
		file->buff_ind = offset + file->buff_len - file->position;
		return 0;
	}

	FS_EnsureOpenFile( file );
	// Purge cached data
	FS_Purge( file );

	if( FBitSet( file->flags, FILE_DEFLATED ))
	{
		// Seeking in compressed files is more a hack than anything else,
		// but we need to support it, so here we go.
		ztoolkit_t *ztk = file->ztk;
		unsigned char *buffer;
		fs_offset_t buffersize;

		// If we have to go back in the file, we need to restart from the beginning
		if( offset <= file->position )
		{
			ztk->in_ind = 0;
			ztk->in_len = 0;
			ztk->in_position = 0;
			file->position = 0;
			if( lseek( file->handle, file->offset, SEEK_SET ) == -1 )
				Con_Printf("IMPOSSIBLE: couldn't seek in already opened pk3 file.\n");

			// Reset the Zlib stream
			ztk->zstream.next_in = ztk->input;
			ztk->zstream.avail_in = 0;
			inflateReset( &ztk->zstream );
		}

		// We need a big buffer to force inflating into it directly
		buffersize = 2 * sizeof( file->buff );
		buffer = (unsigned char *)Mem_Malloc( fs_mempool, buffersize );

		// Skip all data until we reach the requested offset
		while( offset > ( file->position - file->buff_len + file->buff_ind ))
		{
			fs_offset_t diff = offset - ( file->position - file->buff_len + file->buff_ind );
			fs_offset_t count;

			count = ( diff > buffersize ) ? buffersize : diff;
			if( FS_Read( file, buffer, count ) != count )
			{
				Mem_Free( buffer );
				return -1;
			}
		}

		Mem_Free( buffer );
		return 0;
	}

	if( lseek( file->handle, file->offset + offset, SEEK_SET ) == -1 )
		return -1;
	file->position = offset;

	return 0;


}

/*
====================
FS_Tell

Give the current position in a file
====================
*/
fs_offset_t FS_Tell( file_t *file )
{
	if( !file ) return 0;
	return file->position - file->buff_len + file->buff_ind;
}

/*
====================
FS_Eof

indicates at reached end of file
====================
*/
qboolean FS_Eof( file_t *file )
{
	if( !file ) return true;
	return (( file->position - file->buff_len + file->buff_ind ) == file->real_length ) ? true : false;
}

/*
====================
FS_Purge

Erases any buffered input or output data
====================
*/
static void FS_Purge( file_t *file )
{
	file->buff_len = 0;
	file->buff_ind = 0;
	file->ungetc = EOF;
}

static void *FS_CustomAlloc( size_t size )
{
	return Mem_Malloc( fs_mempool, size );
}

static void FS_CustomFree( void *data )
{
	Mem_Free( data );
}

static byte *FS_LoadFileFromArchive( searchpath_t *sp, const char *path, int pack_ind, fs_offset_t *filesizeptr, const qboolean sys_malloc )
{
	fs_offset_t	filesize;
	file_t *file;
	byte *buf;
	void *( *pfnAlloc )( size_t ) = sys_malloc ? malloc : FS_CustomAlloc;
	void ( *pfnFree )( void * ) = sys_malloc ? free : FS_CustomFree;

	// custom load file function for compressed files
	if( sp->pfnLoadFile )
		return sp->pfnLoadFile( sp, path, pack_ind, filesizeptr, pfnAlloc, pfnFree );

	file = sp->pfnOpenFile( sp, path, "rb", pack_ind );

	if( !file ) // TODO: indicate errors
		return NULL;

	filesize = file->real_length;
	buf = (byte *)pfnAlloc( filesize + 1 );

	if( unlikely( !buf )) // TODO: indicate errors
	{
		Con_Reportf( "%s: can't alloc %li bytes, no free memory\n", __func__, (long)filesize + 1 );
		FS_Close( file );
		return NULL;
	}

	buf[filesize] = '\0';
	FS_Read( file, buf, filesize );
	FS_Close( file );
	if( filesizeptr ) *filesizeptr = filesize;

	return buf;
}

/*
============
FS_LoadFile

Filename are relative to the xash directory.
Always appends a 0 byte.
============
*/
static byte *FS_LoadFile_( const char *path, fs_offset_t *filesizeptr, const qboolean gamedironly, const qboolean custom_alloc )
{
	searchpath_t *search;
	char netpath[MAX_SYSPATH];
	int pack_ind;

	// some mappers used leading '/' or '\' in path to models or sounds
	if( path[0] == '/' || path[0] == '\\' )
		path++;

	if( path[0] == '/' || path[0] == '\\' )
		path++;

	if( !fs_searchpaths || FS_CheckNastyPath( path ))
		return NULL;

	search = FS_FindFile( path, &pack_ind, netpath, sizeof( netpath ), gamedironly );

	if( !search )
		return NULL;

	return FS_LoadFileFromArchive( search, netpath, pack_ind, filesizeptr, !custom_alloc );
}

byte *FS_LoadFileMalloc( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	return FS_LoadFile_( path, filesizeptr, gamedironly, false );
}

byte *FS_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	return FS_LoadFile_( path, filesizeptr, gamedironly, true );
}

qboolean CRC32_File( dword *crcvalue, const char *filename )
{
	char	buffer[1024];
	int	num_bytes;
	file_t	*f;

	f = FS_Open( filename, "rb", false );
	if( !f ) return false;

	CRC32_Init( crcvalue );

	while( 1 )
	{
		num_bytes = FS_Read( f, buffer, sizeof( buffer ));

		if( num_bytes > 0 )
			CRC32_ProcessBuffer( crcvalue, buffer, num_bytes );

		if( FS_Eof( f )) break;
	}

	FS_Close( f );
	return true;
}

qboolean MD5_HashFile( byte digest[16], const char *pszFileName, uint seed[4] )
{
	file_t		*file;
	MD5Context_t	MD5_Hash = { 0 };

	if(( file = FS_Open( pszFileName, "rb", false )) == NULL )
		return false;

	MD5Init( &MD5_Hash );

	if( seed )
		MD5Update( &MD5_Hash, (const byte *)seed, 16 );

	while( 1 )
	{
		byte buffer[1024];
		int bytes = FS_Read( file, buffer, sizeof( buffer ));

		if( bytes > 0 )
			MD5Update( &MD5_Hash, buffer, bytes );

		if( FS_Eof( file ))
			break;
	}

	FS_Close( file );
	MD5Final( digest, &MD5_Hash );

	return true;
}

/*
============
FS_LoadFile

Filename are relative to the xash directory.
Always appends a 0 byte.
============
*/
byte *FS_LoadDirectFile( const char *path, fs_offset_t *filesizeptr )
{
	file_t		*file;
	byte		*buf = NULL;
	fs_offset_t	filesize = 0;

	file = FS_SysOpen( path, "rb" );

	if( !file )
		return NULL;

	// Try to load
	filesize = file->real_length;
	buf = (byte *)Mem_Malloc( fs_mempool, filesize + 1 );
	buf[filesize] = '\0';
	FS_Read( file, buf, filesize );
	FS_Close( file );

	if( filesizeptr )
		*filesizeptr = filesize;

	return buf;
}


/*
============
FS_WriteFile

The filename will be prefixed by the current game directory
============
*/
qboolean FS_WriteFile( const char *filename, const void *data, fs_offset_t len )
{
	file_t *file;

	file = FS_Open( filename, "wb", false );

	if( !file )
	{
		Con_Reportf( S_ERROR "%s: failed on %s\n", __func__, filename );
		return false;
	}

	FS_Write( file, data, len );
	FS_Close( file );

	return true;
}

/*
=============================================================================

OTHERS PUBLIC FUNCTIONS

=============================================================================
*/
/*
==================
FS_FileExists

Look for a file in the packages and in the filesystem
==================
*/
int GAME_EXPORT FS_FileExists( const char *filename, int gamedironly )
{
	return FS_FindFile( filename, NULL, NULL, 0, gamedironly ) != NULL;
}

/*
==================
FS_GetDiskPath

Build direct path for file in the filesystem
return NULL for file in pack
==================
*/
const char *FS_GetDiskPath( const char *name, qboolean gamedironly )
{
	static char diskpath[MAX_SYSPATH];

	if( FS_GetFullDiskPath( diskpath, sizeof( diskpath ), name, gamedironly ))
		return diskpath;

	return NULL;
}

/*
==================
FS_GetFullDiskPath

Build full path for file on disk
return false for file in pack
==================
*/
qboolean FS_GetFullDiskPath( char *buffer, size_t size, const char *name, qboolean gamedironly )
{
	searchpath_t *search;
	char temp[MAX_SYSPATH];

	search = FS_FindFile( name, NULL, temp, sizeof( temp ), gamedironly );

	if( search && search->type == SEARCHPATH_PLAIN )
	{
		Q_snprintf( buffer, size, "%s%s", search->filename, temp );
		return true;
	}

	return false;
}

/*
==================
FS_FileSize

return size of file in bytes
==================
*/
fs_offset_t FS_FileSize( const char *filename, qboolean gamedironly )
{
	int	length = -1; // in case file was missed
	file_t	*fp;

	fp = FS_Open( filename, "rb", gamedironly );

	if( fp )
	{
		// it exists
		FS_Seek( fp, 0, SEEK_END );
		length = FS_Tell( fp );
		FS_Close( fp );
	}

	return length;
}

/*
==================
FS_FileLength

return size of file in bytes
==================
*/
fs_offset_t FS_FileLength( file_t *f )
{
	if( !f ) return 0;
	return f->real_length;
}

/*
==================
FS_FileTime

return time of creation file in seconds
==================
*/
int FS_FileTime( const char *filename, qboolean gamedironly )
{
	searchpath_t *search;
	char netpath[MAX_SYSPATH];
	int pack_ind;

	search = FS_FindFile( filename, &pack_ind, netpath, sizeof( netpath ), gamedironly );
	if( !search ) return -1; // doesn't exist

	return search->pfnFileTime( search, netpath );
}

/*
==================
FS_Rename

rename specified file from gamefolder
==================
*/
qboolean FS_Rename( const char *oldname, const char *newname )
{
	char oldname2[MAX_SYSPATH], newname2[MAX_SYSPATH], oldpath[MAX_SYSPATH], newpath[MAX_SYSPATH];
	int ret;

	// a1ba: disallow path traversal
	if( FS_CheckNastyPath( oldname ) || FS_CheckNastyPath( newname ))
		return false;

	if( !fs_writepath )
		return false;

	if( COM_StringEmptyOrNULL( oldname ) || COM_StringEmptyOrNULL( newname ))
		return false;

	// no work done
	if( !Q_stricmp( oldname, newname ))
		return true;

	// fix up slashes
	Q_strncpy( oldname2, oldname, sizeof( oldname2 ));
	Q_strncpy( newname2, newname, sizeof( newname2 ));

	COM_FixSlashes( oldname2 );
	COM_FixSlashes( newname2 );

	// file does not exist
	if( !FS_FixFileCase( fs_writepath->dir, oldname2, oldpath, sizeof( oldpath ), false ))
		return false;

	// exit if overflowed
	if( !FS_FixFileCase( fs_writepath->dir, newname2, newpath, sizeof( newpath ), true ))
		return false;

	ret = rename( oldpath, newpath );
	if( ret < 0 )
	{
		Con_Printf( "%s: failed to rename file %s (%s) to %s (%s): %s\n",
			__func__, oldpath, oldname2, newpath, newname2, strerror( errno ));
		return false;
	}

	return true;
}

/*
==================
FS_Delete

delete specified file from gamefolder
==================
*/
qboolean GAME_EXPORT FS_Delete( const char *path )
{
	char path2[MAX_SYSPATH], real_path[MAX_SYSPATH];
	int ret;

	// a1ba: disallow path traversal
	if( FS_CheckNastyPath( path ))
		return false;

	if( !fs_writepath || COM_StringEmptyOrNULL( path ))
		return false;

	Q_strncpy( path2, path, sizeof( path2 ));
	COM_FixSlashes( path2 );

	if( !FS_FixFileCase( fs_writepath->dir, path2, real_path, sizeof( real_path ), true ))
		return true;

	ret = remove( real_path );
	if( ret < 0 && errno != ENOENT )
	{
		Con_Printf( "%s: failed to delete file %s (%s): %s\n", __func__, real_path, path, strerror( errno ));
		return false;
	}

	return true;
}

/*
==================
FS_FileCopy

==================
*/
qboolean FS_FileCopy( file_t *pOutput, file_t *pInput, int fileSize )
{
	char	*buf = Mem_Malloc( fs_mempool, FILE_COPY_SIZE );
	int	size, readSize;
	qboolean	done = true;

	while( fileSize > 0 )
	{
		if( fileSize > FILE_COPY_SIZE )
			size = FILE_COPY_SIZE;
		else size = fileSize;

		if(( readSize = FS_Read( pInput, buf, size )) < size )
		{
			Con_Reportf( S_ERROR "%s: unexpected end of input file (%d < %d)\n", __func__, readSize, size );
			fileSize = 0;
			done = false;
			break;
		}

		FS_Write( pOutput, buf, readSize );
		fileSize -= size;
	}

	Mem_Free( buf );
	return done;
}

/*
===========
FS_Search

Allocate and fill a search structure with information on matching filenames.
===========
*/
search_t *FS_Search( const char *pattern, int caseinsensitive, int gamedironly )
{
	search_t *search = NULL;
	searchpath_t *searchpath;
	int i, numfiles, numchars;
	stringlist_t resultlist;

	if( pattern[0] == '.' || pattern[0] == ':' || pattern[0] == '/' || pattern[0] == '\\' )
		return NULL; // punctuation issues

	stringlistinit( &resultlist );

	// search through the path, one element at a time
	for( searchpath = fs_searchpaths; searchpath; searchpath = searchpath->next )
	{
		if( gamedironly && !FBitSet( searchpath->flags, FS_GAMEDIRONLY_SEARCH_FLAGS ))
			continue;

		searchpath->pfnSearch( searchpath, &resultlist, pattern, caseinsensitive );
	}

	if( resultlist.numstrings )
	{
		stringlistsort( &resultlist );
		numfiles = resultlist.numstrings;
		numchars = 0;

		for( i = 0; i < resultlist.numstrings; i++ )
			numchars += (int)Q_strlen( resultlist.strings[i]) + 1;
		search = Mem_Calloc( fs_mempool, sizeof(search_t) + numchars + numfiles * sizeof( char* ));
		search->filenames = (char **)((char *)search + sizeof( search_t ));
		search->filenamesbuffer = (char *)((char *)search + sizeof( search_t ) + numfiles * sizeof( char* ));
		search->numfilenames = (int)numfiles;
		numfiles = numchars = 0;

		for( i = 0; i < resultlist.numstrings; i++ )
		{
			size_t	textlen;

			search->filenames[numfiles] = search->filenamesbuffer + numchars;
			textlen = Q_strlen(resultlist.strings[i]) + 1;
			memcpy( search->filenames[numfiles], resultlist.strings[i], textlen );
			numfiles++;
			numchars += (int)textlen;
		}
	}

	stringlistfreecontents( &resultlist );

	return search;
}

static qboolean FS_IsArchiveExtensionSupported( const char *ext, uint flags )
{
	int i;

	if( ext == NULL )
		return false;

	for( i = 0; i < sizeof( g_archives ) / sizeof( g_archives[0] ); i++ )
	{
		if( FBitSet( flags, IAES_ONLY_REAL_ARCHIVES ) && !g_archives[i].real_archive )
			continue;

		if( !Q_stricmp( ext, g_archives[i].ext ))
			return true;
	}

	return false;
}

static searchpath_t *FS_GetArchiveByName( const char *name, searchpath_t *prev )
{
	searchpath_t *sp = prev ? prev->next : fs_searchpaths;

	for( ; sp; sp = sp->next )
	{
		if( !Q_stricmp( COM_FileWithoutPath( sp->filename ), name ))
			return sp;
	}

	return NULL;
}

static int FS_FindFileInArchive( searchpath_t *sp, const char *path, char *truepath, size_t len )
{
	return sp->pfnFindFile( sp, path, truepath, len );
}

static file_t *FS_OpenFileFromArchive( searchpath_t *sp, const char *path, const char *mode, int pack_ind )
{
	return sp->pfnOpenFile( sp, path, mode, pack_ind );
}

void FS_InitMemory( void )
{
	fs_mempool = Mem_AllocPool( "FileSystem Pool" );
	fs_searchpaths = NULL;
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
