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

#include "build.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#if XASH_WIN32
#include <direct.h>
#include <io.h>
#elif XASH_DOS4GW
#include <direct.h>
#include <errno.h>
#else
#include <dirent.h>
#include <errno.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include "port.h"
#include "const.h"
#include "crtlib.h"
#include "crclib.h"
#include "filesystem.h"
#include "filesystem_internal.h"
#include "xash3d_mathlib.h"
#include "common/com_strings.h"
#include "common/protocol.h"

#define FILE_COPY_SIZE		(1024 * 1024)

fs_globals_t FI;
qboolean      fs_ext_path = false;	// attempt to read\write from ./ or ../ pathes
poolhandle_t  fs_mempool;
searchpath_t *fs_searchpaths = NULL;	// chain
char          fs_rodir[MAX_SYSPATH];
char          fs_rootdir[MAX_SYSPATH];
searchpath_t *fs_writepath;

static char			fs_basedir[MAX_SYSPATH];	// base game directory
static char			fs_gamedir[MAX_SYSPATH];	// game current directory
#if !XASH_WIN32
static qboolean		fs_caseinsensitive = true; // try to search missing files
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

void stringlistappend( stringlist_t *list, char *text )
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

void listdirectory( stringlist_t *list, const char *path )
{
#if XASH_WIN32
	char pattern[4096];
	struct _finddata_t	n_file;
	intptr_t		hFile;
#else
	DIR *dir;
	struct dirent *entry;
#endif

#if XASH_WIN32
	Q_snprintf( pattern, sizeof( pattern ), "%s/*", path );

	// ask for the directory listing handle
	hFile = _findfirst( pattern, &n_file );
	if( hFile == -1 ) return;

	// start a new chain with the the first name
	stringlistappend( list, n_file.name );
	// iterate through the directory
	while( _findnext( hFile, &n_file ) == 0 )
		stringlistappend( list, n_file.name );
	_findclose( hFile );
#else
	if( !( dir = opendir( path ) ) )
		return;

	// iterate through the directory
	while( ( entry = readdir( dir ) ))
		stringlistappend( list, entry->d_name );
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
			_mkdir( path );
			*ofs = save;
		}
	}
}



/*
================
FS_AddArchive_Fullpath
================
*/
static qboolean FS_AddArchive_Fullpath( const char *file, qboolean *already_loaded, int flags )
{
	const char *ext = COM_FileExtension( file );

	if( !Q_stricmp( ext, "pk3" ))
		return FS_AddZip_Fullpath( file, already_loaded, flags );
	else if ( !Q_stricmp( ext, "pak" ))
		return FS_AddPak_Fullpath( file, already_loaded, flags );

	// skip wads, this function only meant to be used for extras
	return false;
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
	char fullpath[MAX_SYSPATH];
	int i;

	stringlistinit( &list );
	listdirectory( &list, dir );
	stringlistsort( &list );

	// add archives in specific order PAK -> PK3 -> WAD
	// so raw WADs takes precedence over WADs included into PAKs and PK3s
	for( i = 0; i < list.numstrings; i++ )
	{
		const char *ext = COM_FileExtension( list.strings[i] );

		if( !Q_stricmp( ext, "pak" ))
		{
			Q_snprintf( fullpath, sizeof( fullpath ), "%s%s", dir, list.strings[i] );
			FS_AddPak_Fullpath( fullpath, NULL, flags );
		}
	}

	for( i = 0; i < list.numstrings; i++ )
	{
		const char *ext = COM_FileExtension( list.strings[i] );

		if( !Q_stricmp( ext, "pk3" ))
		{
			Q_snprintf( fullpath, sizeof( fullpath ), "%s%s", dir, list.strings[i] );
			FS_AddZip_Fullpath( fullpath, NULL, flags );
		}
	}

	for( i = 0; i < list.numstrings; i++ )
	{
		const char *ext = COM_FileExtension( list.strings[i] );

		if( !Q_stricmp( ext, "wad" ))
		{
			FS_AllowDirectPaths( true );
			Q_snprintf( fullpath, sizeof( fullpath ), "%s%s", dir, list.strings[i] );
			FS_AddWad_Fullpath( fullpath, NULL, flags );
			FS_AllowDirectPaths( false );
		}
	}

	stringlistfreecontents( &list );

	// add the directory to the search path
	// (unpacked files have the priority over packed files)
	search = FS_AddDir_Fullpath( dir, NULL, flags );
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
int FS_CheckNastyPath (const char *path, qboolean isgamedir)
{
	// all: never allow an empty path, as for gamedir it would access the parent directory and a non-gamedir path it is just useless
	if( !COM_CheckString( path )) return 2;

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

	// all: forbid trailing slash on gamedir
	if( isgamedir && path[Q_strlen(path)-1] == '/' ) return 2;

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
static void FS_WriteGameInfo( const char *filepath, gameinfo_t *GameInfo )
{
	file_t	*f = FS_Open( filepath, "w", false ); // we in binary-mode
	int	i, write_ambients = false;

	if( !f ) Sys_Error( "FS_WriteGameInfo: can't write %s\n", filepath );	// may be disk-space is out?

	FS_Printf( f, "// generated by " XASH_ENGINE_NAME " " XASH_VERSION "-%s (%s-%s)\n\n\n", Q_buildcommit(), Q_buildos(), Q_buildarch() );

	if( COM_CheckStringEmpty( GameInfo->basedir ) )
		FS_Printf( f, "basedir\t\t\"%s\"\n", GameInfo->basedir );

	// DEPRECATED: gamedir key isn't supported by FWGS fork
	// but write it anyway to keep compability with original Xash3D
	if( COM_CheckStringEmpty( GameInfo->gamefolder ) )
		FS_Printf( f, "gamedir\t\t\"%s\"\n", GameInfo->gamefolder );

	if( COM_CheckStringEmpty( GameInfo->falldir ) )
		FS_Printf( f, "fallback_dir\t\"%s\"\n", GameInfo->falldir );

	if( COM_CheckStringEmpty( GameInfo->title ) )
		FS_Printf( f, "title\t\t\"%s\"\n", GameInfo->title );

	if( COM_CheckStringEmpty( GameInfo->startmap ) )
		FS_Printf( f, "startmap\t\t\"%s\"\n", GameInfo->startmap );

	if( COM_CheckStringEmpty( GameInfo->trainmap ) )
		FS_Printf( f, "trainmap\t\t\"%s\"\n", GameInfo->trainmap );

	if( GameInfo->version != 0.0f )
		FS_Printf( f, "version\t\t%g\n", GameInfo->version );

	if( GameInfo->size != 0 )
		FS_Printf( f, "size\t\t%lu\n", GameInfo->size );

	if( COM_CheckStringEmpty( GameInfo->game_url ) )
		FS_Printf( f, "url_info\t\t\"%s\"\n", GameInfo->game_url );

	if( COM_CheckStringEmpty( GameInfo->update_url ) )
		FS_Printf( f, "url_update\t\t\"%s\"\n", GameInfo->update_url );

	if( COM_CheckStringEmpty( GameInfo->type ) )
		FS_Printf( f, "type\t\t\"%s\"\n", GameInfo->type );

	if( COM_CheckStringEmpty( GameInfo->date ) )
		FS_Printf( f, "date\t\t\"%s\"\n", GameInfo->date );

	if( COM_CheckStringEmpty( GameInfo->dll_path ) )
		FS_Printf( f, "dllpath\t\t\"%s\"\n", GameInfo->dll_path );

	if( COM_CheckStringEmpty( GameInfo->game_dll ) )
		FS_Printf( f, "gamedll\t\t\"%s\"\n", GameInfo->game_dll );

	if( COM_CheckStringEmpty( GameInfo->game_dll_linux ) )
		FS_Printf( f, "gamedll_linux\t\t\"%s\"\n", GameInfo->game_dll_linux );

	if( COM_CheckStringEmpty( GameInfo->game_dll_osx ) )
		FS_Printf( f, "gamedll_osx\t\t\"%s\"\n", GameInfo->game_dll_osx );

	if( COM_CheckStringEmpty( GameInfo->iconpath ))
		FS_Printf( f, "icon\t\t\"%s\"\n", GameInfo->iconpath );

	switch( GameInfo->gamemode )
	{
	case 1: FS_Print( f, "gamemode\t\t\"singleplayer_only\"\n" ); break;
	case 2: FS_Print( f, "gamemode\t\t\"multiplayer_only\"\n" ); break;
	}

	if( COM_CheckStringEmpty( GameInfo->sp_entity ))
		FS_Printf( f, "sp_entity\t\t\"%s\"\n", GameInfo->sp_entity );
	if( COM_CheckStringEmpty( GameInfo->mp_entity ))
		FS_Printf( f, "mp_entity\t\t\"%s\"\n", GameInfo->mp_entity );
	if( COM_CheckStringEmpty( GameInfo->mp_filter ))
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
		FS_Printf( f, "noskills\t\t\"%i\"\n", GameInfo->nomodels );

	// always expose our extensions :)
	FS_Printf( f, "internal_vgui_support\t\t%s\n", GameInfo->internal_vgui_support ? "1" : "0" );
	FS_Printf( f, "render_picbutton_text\t\t%s\n", GameInfo->render_picbutton_text ? "1" : "0" );

	FS_Print( f, "\n\n\n" );
	FS_Close( f );	// all done
}

void FS_InitGameInfo( gameinfo_t *GameInfo, const char *gamedir )
{
	memset( GameInfo, 0, sizeof( *GameInfo ));

	// filesystem info
	Q_strncpy( GameInfo->gamefolder, gamedir, sizeof( GameInfo->gamefolder ));
	Q_strncpy( GameInfo->basedir, "valve", sizeof( GameInfo->basedir ));
	GameInfo->falldir[0] = 0;
	Q_strncpy( GameInfo->startmap, "c0a0", sizeof( GameInfo->startmap ));
	Q_strncpy( GameInfo->trainmap, "t0a0", sizeof( GameInfo->trainmap ));
	Q_strncpy( GameInfo->title, "New Game", sizeof( GameInfo->title ));
	GameInfo->version = 1.0f;

	// .dll pathes
	Q_strncpy( GameInfo->dll_path, "cl_dlls", sizeof( GameInfo->dll_path ));
	Q_strncpy( GameInfo->game_dll, "dlls/hl.dll", sizeof( GameInfo->game_dll ));
	Q_strncpy( GameInfo->game_dll_linux, "dlls/hl.so", sizeof( GameInfo->game_dll_linux ));
	Q_strncpy( GameInfo->game_dll_osx, "dlls/hl.dylib", sizeof( GameInfo->game_dll_osx ));

	// .ico path
	Q_strncpy( GameInfo->iconpath, "game.ico", sizeof( GameInfo->iconpath ));

	Q_strncpy( GameInfo->sp_entity, "info_player_start", sizeof( GameInfo->sp_entity ));
	Q_strncpy( GameInfo->mp_entity, "info_player_deathmatch", sizeof( GameInfo->mp_entity ));

	GameInfo->max_edicts     = 900; // default value if not specified
	GameInfo->max_tents      = 500;
	GameInfo->max_beams      = 128;
	GameInfo->max_particles  = 4096;
}

void FS_ParseGenericGameInfo( gameinfo_t *GameInfo, const char *buf, const qboolean isGameInfo )
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
			COM_DefaultExtension( GameInfo->iconpath, ".ico" );
		}
		else if( !Q_stricmp( token, "type" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));

			if( !isGameInfo && !Q_stricmp( token, "singleplayer_only" ))
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
			else if( !isGameInfo && !Q_stricmp( token, "multiplayer_only" ))
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
		else if( !Q_stricmp( token, "edicts" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->max_edicts = Q_atoi( token );
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
			GameInfo->secure = Q_atoi( token );
		}
		// valid for both
		else if( !Q_stricmp( token, "nomodels" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->nomodels = Q_atoi( token );
		}
		else if( !Q_stricmp( token, isGameInfo ? "max_edicts" : "edicts" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			GameInfo->max_edicts = bound( MIN_EDICTS, Q_atoi( token ), MAX_EDICTS );
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
				GameInfo->max_particles = bound( 4096, Q_atoi( token ), 32768 );
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

				if( ambientNum < 0 || ambientNum > ( NUM_AMBIENTS - 1 ))
					ambientNum = 0;
				pfile = COM_ParseFile( pfile, GameInfo->ambientsound[ambientNum],
					sizeof( GameInfo->ambientsound[ambientNum] ));
			}
			else if( !Q_stricmp( token, "noskills" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->noskills = Q_atoi( token );
			}
			else if( !Q_stricmp( token, "render_picbutton_text" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->render_picbutton_text = Q_atoi( token );
			}
			else if( !Q_stricmp( token, "internal_vgui_support" ))
			{
				pfile = COM_ParseFile( pfile, token, sizeof( token ));
				GameInfo->internal_vgui_support = Q_atoi( token );
			}
		}
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
	Q_snprintf( token, sizeof( token ), "%s/%s", fs_rootdir, GameInfo->falldir );
	if( !FS_SysFolderExists( token ))
		GameInfo->falldir[0] = '\0';
}

/*
================
FS_CreateDefaultGameInfo
================
*/
void FS_CreateDefaultGameInfo( const char *filename )
{
	gameinfo_t	defGI;

	FS_InitGameInfo( &defGI, fs_basedir );

	// make simple gameinfo.txt
	FS_WriteGameInfo( filename, &defGI );
}

/*
================
FS_ParseLiblistGam
================
*/
static qboolean FS_ParseLiblistGam( const char *filename, const char *gamedir, gameinfo_t *GameInfo )
{
	char	*afile;

	if( !GameInfo ) return false;
	afile = (char *)FS_LoadDirectFile( filename, NULL );
	if( !afile ) return false;

	FS_InitGameInfo( GameInfo, gamedir );

	FS_ParseGenericGameInfo( GameInfo, afile, false );

	Mem_Free( afile );

	return true;
}

/*
================
FS_ConvertGameInfo
================
*/
static qboolean FS_ConvertGameInfo( const char *gamedir, const char *gameinfo_path, const char *liblist_path )
{
	gameinfo_t	GameInfo;

	memset( &GameInfo, 0, sizeof( GameInfo ));

	if( FS_ParseLiblistGam( liblist_path, gamedir, &GameInfo ))
	{
		Con_DPrintf( "Convert %s to %s\n", liblist_path, gameinfo_path );
		FS_WriteGameInfo( gameinfo_path, &GameInfo );

		return true;
	}

	return false;
}

/*
================
FS_ReadGameInfo
================
*/
static qboolean FS_ReadGameInfo( const char *filepath, const char *gamedir, gameinfo_t *GameInfo )
{
	char	*afile;

	afile = (char *)FS_LoadFile( filepath, NULL, false );
	if( !afile )
		return false;

	FS_InitGameInfo( GameInfo, gamedir );

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
static qboolean FS_CheckForQuakeGameDir( const char *gamedir, qboolean direct )
{
	// if directory contain config.cfg or progs.dat it's 100% gamedir
	// quake mods probably always archived but can missed config.cfg before first running
	const char *files[] = { "config.cfg", "progs.dat", "pak0.pak" };
	int i;

	for( i = 0; i < sizeof( files ) / sizeof( files[0] ); i++ )
	{
		char	buf[MAX_VA_STRING];

		Q_snprintf( buf, sizeof( buf ), "%s/%s", gamedir, files[i] );
		if( direct ? FS_SysFileExists( buf ) : FS_FileExists( buf, false ))
			return true;
	}

	return false;
}

/*
===============
FS_CheckForXashGameDir

Checks if game directory resembles Xash3D game directory
===============
*/
static qboolean FS_CheckForXashGameDir( const char *gamedir, qboolean direct )
{
	// if directory contain gameinfo.txt or liblist.gam it's 100% gamedir
	const char *files[] = { "gameinfo.txt", "liblist.gam" };
	int i;

	for( i = 0; i < sizeof( files ) / sizeof( files[0] ); i++ )
	{
		char	buf[MAX_SYSPATH];

		Q_snprintf( buf, sizeof( buf ), "%s/%s", gamedir, files[i] );
		if( direct ? FS_SysFileExists( buf ) : FS_FileExists( buf, false ))
			return true;
	}

	return false;

}

/*
================
FS_ParseGameInfo
================
*/
static qboolean FS_ParseGameInfo( const char *gamedir, gameinfo_t *GameInfo )
{
	string		liblist_path, gameinfo_path;
	string		default_gameinfo_path;
	qboolean	haveUpdate = false;

	Q_snprintf( default_gameinfo_path, sizeof( default_gameinfo_path ), "%s/gameinfo.txt", fs_basedir );
	Q_snprintf( gameinfo_path, sizeof( gameinfo_path ), "%s/gameinfo.txt", gamedir );
	Q_snprintf( liblist_path, sizeof( liblist_path ), "%s/liblist.gam", gamedir );

	// here goes some RoDir magic...
	if( COM_CheckStringEmpty( fs_rodir ))
	{
		string	gameinfo_ro, liblist_ro;
		fs_offset_t roLibListTime, roGameInfoTime, rwGameInfoTime;

		FS_AllowDirectPaths( true );

		Q_snprintf( gameinfo_ro, sizeof( gameinfo_ro ), "%s/%s/gameinfo.txt", fs_rodir, gamedir );
		Q_snprintf( liblist_ro, sizeof( liblist_ro ), "%s/%s/liblist.gam", fs_rodir, gamedir );

		roLibListTime = FS_SysFileTime( liblist_ro );
		roGameInfoTime = FS_SysFileTime( gameinfo_ro );
		rwGameInfoTime = FS_SysFileTime( gameinfo_path );

		if( roLibListTime > rwGameInfoTime )
		{
			haveUpdate = FS_ConvertGameInfo( gamedir, gameinfo_path, liblist_ro );
		}
		else if( roGameInfoTime > rwGameInfoTime )
		{
			fs_offset_t len;
			char *afile_ro = (char *)FS_LoadDirectFile( gameinfo_ro, &len );

			if( afile_ro )
			{
				Con_DPrintf( "Copy rodir %s to rwdir %s\n", gameinfo_ro, gameinfo_path );
				haveUpdate = true;
				FS_WriteFile( gameinfo_path, afile_ro, len );
				Mem_Free( afile_ro );
			}
		}

		FS_AllowDirectPaths( false );
	}

	// if user change liblist.gam update the gameinfo.txt
	if( FS_FileTime( liblist_path, false ) > FS_FileTime( gameinfo_path, false ))
		FS_ConvertGameInfo( gamedir, gameinfo_path, liblist_path );

	// force to create gameinfo for specified game if missing
	if(( FS_CheckForQuakeGameDir( gamedir, false ) || !Q_stricmp( fs_gamedir, gamedir )) && !FS_FileExists( gameinfo_path, false ))
	{
		gameinfo_t tmpGameInfo;
		memset( &tmpGameInfo, 0, sizeof( tmpGameInfo ));

		if( FS_ReadGameInfo( default_gameinfo_path, gamedir, &tmpGameInfo ))
		{
			// now we have copy of game info from basedir but needs to change gamedir
			Con_DPrintf( "Convert %s to %s\n", default_gameinfo_path, gameinfo_path );
			Q_strncpy( tmpGameInfo.gamefolder, gamedir, sizeof( tmpGameInfo.gamefolder ));
			FS_WriteGameInfo( gameinfo_path, &tmpGameInfo );
		}
		else FS_CreateDefaultGameInfo( gameinfo_path );
	}

	if( !GameInfo || !FS_FileExists( gameinfo_path, false ))
		return false; // no dest

	return FS_ReadGameInfo( gameinfo_path, gamedir, GameInfo );
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

	GI->added = true;

	if( !COM_CheckString( dir ))
		return;

	// add the common game directory

	// recursive gamedirs
	// for example, czeror->czero->cstrike->valve
	for( i = 0; i < FI.numgames; i++ )
	{
		if( !Q_strnicmp( FI.games[i]->gamefolder, dir, 64 ))
		{
			Con_Reportf( "FS_AddGameHierarchy: adding recursive basedir %s\n", FI.games[i]->basedir );
			if( !FI.games[i]->added && Q_stricmp( FI.games[i]->gamefolder, FI.games[i]->basedir ))
			{
				FI.games[i]->added = true;
				FS_AddGameHierarchy( FI.games[i]->basedir, flags & (~FS_GAMEDIR_PATH) );
			}
			break;
		}
	}

	if( COM_CheckStringEmpty( fs_rodir ) )
	{
		// append new flags to rodir, except FS_GAMEDIR_PATH and FS_CUSTOM_PATH
		uint newFlags = FS_NOWRITE_PATH | (flags & (~FS_GAMEDIR_PATH|FS_CUSTOM_PATH));
		if( isGameDir )
			newFlags |= FS_GAMERODIR_PATH;

		FS_AllowDirectPaths( true );
		Q_snprintf( buf, sizeof( buf ), "%s/%s/", fs_rodir, dir );
		FS_AddGameDirectory( buf, newFlags );
		FS_AllowDirectPaths( false );
	}

	if( isGameDir )
	{
		Q_snprintf( buf, sizeof( buf ), "%s/downloaded/", dir );
		FS_AddGameDirectory( buf, FS_NOWRITE_PATH | FS_CUSTOM_PATH );
	}
	Q_snprintf( buf, sizeof( buf ), "%s/", dir );
	FS_AddGameDirectory( buf, flags );
	if( isGameDir )
	{
		Q_snprintf( buf, sizeof( buf ), "%s/custom/", dir );
		FS_AddGameDirectory( buf, FS_NOWRITE_PATH | FS_CUSTOM_PATH );
	}
}

/*
================
FS_Rescan
================
*/
void FS_Rescan( void )
{
	const char *str;
	const int extrasFlags = FS_NOWRITE_PATH | FS_CUSTOM_PATH;
	Con_Reportf( "FS_Rescan( %s )\n", GI->title );

	FS_ClearSearchPath();

#if XASH_IOS
	{
		char buf[MAX_VA_STRING];

		Q_snprintf( buf, sizeof( buf ), "%sextras.pak", SDL_GetBasePath() );
		FS_AddPak_Fullpath( buf, NULL, extrasFlags );
		Q_snprintf( buf, sizeof( buf ), "%sextras_%s.pak", SDL_GetBasePath(), GI->gamefolder );
		FS_AddPak_Fullpath( buf, NULL, extrasFlags );
	}
#else
	str = getenv( "XASH3D_EXTRAS_PAK1" );
	if( COM_CheckString( str ))
		FS_AddArchive_Fullpath( str, NULL, extrasFlags );

	str = getenv( "XASH3D_EXTRAS_PAK2" );
	if( COM_CheckString( str ))
		FS_AddArchive_Fullpath( str, NULL, extrasFlags );
#endif


	if( Q_stricmp( GI->basedir, GI->gamefolder ))
		FS_AddGameHierarchy( GI->basedir, 0 );
	if( Q_stricmp( GI->basedir, GI->falldir ) && Q_stricmp( GI->gamefolder, GI->falldir ))
		FS_AddGameHierarchy( GI->falldir, 0 );
	FS_AddGameHierarchy( GI->gamefolder, FS_GAMEDIR_PATH );
}

/*
================
FS_LoadGameInfo

can be passed null arg
================
*/
void FS_LoadGameInfo( const char *rootfolder )
{
	int	i;

	// lock uplevel of gamedir for read\write
	fs_ext_path = false;

	if( rootfolder ) Q_strcpy( fs_gamedir, rootfolder );
	Con_Reportf( "FS_LoadGameInfo( %s )\n", fs_gamedir );

	// clear any old pathes
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

	FS_Rescan(); // create new filesystem
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

	return ( key == 0x12345678 ) ? true : false;
}

/*
==================
FS_FindLibrary

search for library, assume index is valid
==================
*/
static qboolean FS_FindLibrary( const char *dllname, qboolean directpath, fs_dllinfo_t *dllInfo )
{
	searchpath_t	*search;
	int index, start = 0, i, len;

	fs_ext_path = directpath;

	// check for bad exports
	if( !COM_CheckString( dllname ))
		return false;

	// HACKHACK remove absoulte path to valve folder
	if( !Q_strnicmp( dllname, "..\\valve\\", 9 ) || !Q_strnicmp( dllname, "../valve/", 9 ))
		start += 9;

	// replace all backward slashes
	len = Q_strlen( dllname );

	for( i = 0; i < len; i++ )
	{
		if( dllname[i+start] == '\\' ) dllInfo->shortPath[i] = '/';
		else dllInfo->shortPath[i] = Q_tolower( dllname[i+start] );
	}
	dllInfo->shortPath[i] = '\0';

	COM_DefaultExtension( dllInfo->shortPath, "."OS_LIB_EXT );	// apply ext if forget

	search = FS_FindFile( dllInfo->shortPath, &index, NULL, 0, false );

	if( !search && !directpath )
	{
		fs_ext_path = false;

		// trying check also 'bin' folder for indirect paths
		Q_strncpy( dllInfo->shortPath, dllname, sizeof( dllInfo->shortPath ));
		search = FS_FindFile( dllInfo->shortPath, &index, NULL, 0, false );
		if( !search ) return false; // unable to find
	}

	dllInfo->encrypted = FS_CheckForCrypt( dllInfo->shortPath );

	if( index >= 0 && !dllInfo->encrypted && search )
	{
		Q_snprintf( dllInfo->fullPath, sizeof( dllInfo->fullPath ),
			"%s%s", search->filename, dllInfo->shortPath );
		dllInfo->custom_loader = false;	// we can loading from disk and use normal debugging
	}
	else
	{
		// NOTE: if search is NULL let the OS found library himself
		Q_strncpy( dllInfo->fullPath, dllInfo->shortPath, sizeof( dllInfo->fullPath ));

		if( search && search->type != SEARCHPATH_PLAIN )
		{
#if XASH_WIN32 && XASH_X86 // a1ba: custom loader is non-portable (I just don't want to touch it)
			Con_Printf( S_WARN "%s: loading libraries from packs is deprecated "
				"and will be removed in the future\n", __FUNCTION__ );
			dllInfo->custom_loader = true;
#else
			Con_Printf( S_WARN "%s: loading libraries from packs is unsupported on "
				"this platform\n", __FUNCTION__ );
			dllInfo->custom_loader = false;
#endif
		}
		else
		{
			dllInfo->custom_loader = false;
		}
	}
	fs_ext_path = false; // always reset direct paths

	return true;
}

poolhandle_t _Mem_AllocPool( const char *name, const char *filename, int fileline )
{
	return (poolhandle_t)0xDEADC0DE;
}

void  _Mem_FreePool( poolhandle_t *poolptr, const char *filename, int fileline )
{
	// stub
}

void* _Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	if( clear ) return calloc( 1, size );
	return malloc( size );
}

void* _Mem_Realloc( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return realloc( memptr, size );
}

void  _Mem_Free( void *data, const char *filename, int fileline )
{
	free( data );
}

void _Con_Printf( const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	vprintf( fmt, ap );
	va_end( ap );
}

void _Sys_Error( const char *fmt, ... )
{
	va_list ap;

	va_start( ap, fmt );
	vfprintf( stderr, fmt, ap );
	va_end( ap );

	exit( 1 );
}


/*
================
FS_Init
================
*/
qboolean FS_InitStdio( qboolean caseinsensitive, const char *rootdir, const char *basedir, const char *gamedir, const char *rodir )
{
	stringlist_t	dirs;
	qboolean		hasBaseDir = false;
	qboolean		hasGameDir = false;
	int		i;
	char		buf[MAX_VA_STRING];

	FS_InitMemory();
#if !XASH_WIN32
	fs_caseinsensitive = caseinsensitive;
#endif

	Q_strncpy( fs_rootdir, rootdir, sizeof( fs_rootdir ));
	Q_strncpy( fs_gamedir, gamedir, sizeof( fs_gamedir ));
	Q_strncpy( fs_basedir, basedir, sizeof( fs_basedir ));
	Q_strncpy( fs_rodir, rodir, sizeof( fs_rodir ));

	// add readonly directories first
	if( COM_CheckStringEmpty( fs_rodir ))
	{
		if( !Q_stricmp( fs_rodir, fs_rootdir ))
		{
			Sys_Error( "RoDir and default rootdir can't point to same directory!" );
			return false;
		}

		stringlistinit( &dirs );
		listdirectory( &dirs, fs_rodir );
		stringlistsort( &dirs );

		for( i = 0; i < dirs.numstrings; i++ )
		{
			char roPath[MAX_SYSPATH];

			Q_snprintf( roPath, sizeof( roPath ), "%s/%s/", fs_rodir, dirs.strings[i] );

			// check if it's a directory
			if( !FS_SysFolderExists( roPath ))
				continue;

			// check if it's gamedir
			if( FS_CheckForXashGameDir( roPath, true ) || FS_CheckForQuakeGameDir( roPath, true ))
			{
				char rwPath[MAX_SYSPATH];

				Q_snprintf( rwPath, sizeof( rwPath ), "%s/%s/", fs_rootdir, dirs.strings[i] );
				FS_CreatePath( rwPath );
			}
		}

		stringlistfreecontents( &dirs );
	}

	// validate directories
	stringlistinit( &dirs );
	listdirectory( &dirs, "./" );
	stringlistsort( &dirs );

	for( i = 0; i < dirs.numstrings; i++ )
	{
		if( !Q_stricmp( fs_basedir, dirs.strings[i] ))
			hasBaseDir = true;

		if( !Q_stricmp( fs_gamedir, dirs.strings[i] ))
			hasGameDir = true;
	}

	if( !hasGameDir )
	{
		Con_Printf( S_ERROR "game directory \"%s\" not exist\n", fs_gamedir );
		if( hasBaseDir ) Q_strncpy( fs_gamedir, fs_basedir, sizeof( fs_gamedir ));
	}

	// build list of game directories here
	if( COM_CheckStringEmpty( fs_rodir ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s/", fs_rodir );
		FS_AddGameDirectory( buf, FS_STATIC_PATH|FS_NOWRITE_PATH );
	}
	FS_AddGameDirectory( "./", FS_STATIC_PATH );

	for( i = 0; i < dirs.numstrings; i++ )
	{
		if( !FS_SysFolderExists( dirs.strings[i] ))
			continue;

		if( FI.games[FI.numgames] == NULL )
			FI.games[FI.numgames] = (gameinfo_t *)Mem_Calloc( fs_mempool, sizeof( gameinfo_t ));

		if( FS_ParseGameInfo( dirs.strings[i], FI.games[FI.numgames] ))
			FI.numgames++; // added
	}

	stringlistfreecontents( &dirs );
	Con_Reportf( "FS_Init: done\n" );

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
		if( FI.games[i] ) Mem_Free( FI.games[i] );

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
	file_t	*file;
	int	mod, opt;
	uint	ind;

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

	file = (file_t *)Mem_Calloc( fs_mempool, sizeof( *file ));
	file->filetime = FS_SysFileTime( filepath );
	file->ungetc = EOF;

#if XASH_WIN32
	file->handle = _wopen( FS_PathToWideChar( filepath ), mod | opt, 0666 );
#else
	file->handle = open( filepath, mod|opt, 0666 );
#endif

#if !XASH_WIN32
	if( file->handle < 0 )
		FS_BackupFileName( file, filepath, mod|opt );
#endif

	if( file->handle < 0 )
	{
		Mem_Free( file );
		return NULL;
	}


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

file_t *FS_OpenHandle( const char *syspath, int handle, fs_offset_t offset, fs_offset_t len )
{
	file_t *file = (file_t *)Mem_Calloc( fs_mempool, sizeof( file_t ));
#ifndef XASH_REDUCE_FD
#ifdef HAVE_DUP
	file->handle = dup( handle );
#else
	file->handle = open( syspath, O_RDONLY|O_BINARY );
#endif

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
==================
*/
int FS_SetCurrentDirectory( const char *path )
{
#if XASH_WIN32
	return SetCurrentDirectoryW( FS_PathToWideChar( path ));
#elif XASH_POSIX
	return !chdir( path );
#else
#error
#endif
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
	// some stupid mappers used leading '/' or '\' in path to models or sounds
	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( FS_CheckNastyPath( filepath, false ))
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
		if( close( file->handle ))
			return EOF;

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
	result = write( file->handle, data, (fs_offset_t)datasize );
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
	fs_offset_t	count, done;
	fs_offset_t	nb;

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

		done += ((fs_offset_t)buffersize > count ) ? count : (fs_offset_t)buffersize;
		memcpy( buffer, &file->buff[file->buff_ind], done );
		file->buff_ind += done;

		buffersize -= done;
		if( buffersize == 0 )
			return done;
	}

	// NOTE: at this point, the read buffer is always empty

	FS_EnsureOpenFile( file );
	// we must take care to not read after the end of the file
	count = file->real_length - file->position;

	// if we have a lot of data to get, put them directly into "buffer"
	if( buffersize > sizeof( file->buff ) / 2 )
	{
		if( count > (fs_offset_t)buffersize )
			count = (fs_offset_t)buffersize;
		lseek( file->handle, file->offset + file->position, SEEK_SET );
		nb = read (file->handle, &((byte *)buffer)[done], count );

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

/*
============
FS_LoadFile

Filename are relative to the xash directory.
Always appends a 0 byte.
============
*/
byte *FS_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	file_t	*file;
	byte	*buf = NULL;
	fs_offset_t	filesize = 0;

	file = FS_Open( path, "rb", gamedironly );

	if( file )
	{
		filesize = file->real_length;

		buf = (byte *)Mem_Malloc( fs_mempool, filesize + 1 );
		buf[filesize] = '\0';
		FS_Read( file, buf, filesize );
		FS_Close( file );
	}
	else
	{
		buf = FS_LoadWADFile( path, &filesize, gamedironly );

		if( !buf )
			buf = FS_LoadZIPFile( path, &filesize, gamedironly );

	}

	if( filesizeptr )
		*filesizeptr = filesize;

	return buf;
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
	byte		buffer[1024];
	MD5Context_t	MD5_Hash;
	int		bytes;

	if(( file = FS_Open( pszFileName, "rb", false )) == NULL )
		return false;

	memset( &MD5_Hash, 0, sizeof( MD5Context_t ));

	MD5Init( &MD5_Hash );

	if( seed )
	{
		MD5Update( &MD5_Hash, (const byte *)seed, 16 );
	}

	while( 1 )
	{
		bytes = FS_Read( file, buffer, sizeof( buffer ));

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
	{
		return NULL;
	}

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
		Con_Reportf( S_ERROR "FS_WriteFile: failed on %s\n", filename);
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
	static char temp[MAX_SYSPATH];
	searchpath_t	*search;

	search = FS_FindFile( name, NULL, temp, sizeof( temp ), gamedironly );

	if( search )
	{
		if( search->type != SEARCHPATH_PLAIN ) // file in pack or wad
			return NULL;
		return temp;
	}

	return NULL;
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

	if( !COM_CheckString( oldname ) || !COM_CheckString( newname ))
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
			__FUNCTION__, oldpath, oldname2, newpath, newname2, strerror( errno ));
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

	if( !COM_CheckString( path ))
		return false;

	Q_strncpy( path2, path, sizeof( path2 ));
	COM_FixSlashes( path2 );

	if( !FS_FixFileCase( fs_writepath->dir, path2, real_path, sizeof( real_path ), true ))
		return true;

	ret = remove( real_path );
	if( ret < 0 )
	{
		Con_Printf( "%s: failed to delete file %s (%s): %s\n", __FUNCTION__, real_path, path, strerror( errno ));
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
			Con_Reportf( S_ERROR "FS_FileCopy: unexpected end of input file (%d < %d)\n", readSize, size );
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

void FS_InitMemory( void )
{
	fs_mempool = Mem_AllocPool( "FileSystem Pool" );
	fs_searchpaths = NULL;
}

fs_interface_t g_engfuncs =
{
	_Con_Printf,
	_Con_Printf,
	_Con_Printf,
	_Sys_Error,
	_Mem_AllocPool,
	_Mem_FreePool,
	_Mem_Alloc,
	_Mem_Realloc,
	_Mem_Free
};

static qboolean FS_InitInterface( int version, fs_interface_t *engfuncs )
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

	return true;
}

fs_api_t g_api =
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
};

int EXPORT GetFSAPI( int version, fs_api_t *api, fs_globals_t **globals, fs_interface_t *engfuncs )
{
	if( engfuncs && !FS_InitInterface( version, engfuncs ))
		return 0;

	memcpy( api, &g_api, sizeof( *api ));
	*globals = &FI;

	return FS_API_VERSION;
}
