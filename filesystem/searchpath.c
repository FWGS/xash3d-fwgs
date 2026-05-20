/*
searchpath.c - search path management, archive/dir mounting, file lookup
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
#include <stdlib.h>
#include "port.h"
#include "defaults.h"
#include "crtlib.h"
#include "filesystem.h"
#include "filesystem_internal.h"
#include "common/com_strings.h"
#include "library_suffix.h"

searchpath_t *fs_searchpaths = NULL; // chain
char fs_basedir[MAX_SYSPATH];        // base game directory
char fs_rodir[MAX_SYSPATH];
static char fs_gamedir[MAX_SYSPATH]; // game current directory
static string fs_language;
static qboolean fs_ext_path = false; // attempt to read\write from ./ or ../ pathes

typedef struct fs_archive_s
{
	const char *ext;
	int type;
	FS_ADDARCHIVE_FULLPATH pfnAddArchive_Fullpath;
	qboolean load_wads;    // load wads from this archive
	qboolean real_archive; // not a simulated archive like pk3dir
	qboolean allow_exec;   // only PAK are allowed to carry executables
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
		.allow_exec = true,
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
	}, {
		.ext = "wad",
		.type = SEARCHPATH_WAD,
		.pfnAddArchive_Fullpath = FS_AddWad_Fullpath,
		.real_archive = true,
	},
};

// special fs_archive_t for plain directories
static const fs_archive_t g_directory_archive =
{
	.type = SEARCHPATH_PLAIN,
	.pfnAddArchive_Fullpath = FS_AddDir_Fullpath,
	.allow_exec = true,
};

#if XASH_ANDROID
static const fs_archive_t g_android_archive =
{
	.type = SEARCHPATH_ANDROID_ASSETS,
	.pfnAddArchive_Fullpath = FS_AddAndroidAssets_Fullpath
};
#endif

static searchpath_t *FS_AddArchive_Fullpath( const fs_archive_t *archive, const char *file, int flags )
{
	searchpath_t *search;

	if( !archive )
	{
		const char *ext = COM_FileExtension( file );

		for( int i = 0; i < sizeof( g_archives ) / sizeof( g_archives[0] ); i++ )
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

	// remove exec flag from archives
	if( !archive->allow_exec )
		ClearBits( flags, FS_EXEC_PATH );

	// speed up lookups, can't modify archives anyway
	if( archive->real_archive )
		SetBits( flags, FS_NOWRITE_PATH );

	search = archive->pfnAddArchive_Fullpath( file, flags );

	if( !search )
		return NULL;

	search->next = fs_searchpaths;
	fs_searchpaths = search;

	// time to add in search list all the wads from this archive
	if( archive->load_wads && !FBitSet( flags, FS_SKIP_ARCHIVED_WADS ))
	{
		stringlist_t list;

		stringlistinit( &list );
		search->pfnSearch( search, &list, "*.wad", true );
		stringlistsort( &list ); // keep always sorted

		// wad files can't have executables
		ClearBits( flags, FS_EXEC_PATH );
		SetBits( flags, FS_NOWRITE_PATH );

		for( int i = 0; i < list.numstrings; i++ )
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

	stringlistinit( &list );
	listdirectory( &list, dir, false );
	stringlistsort( &list );

	for( int j = 0; j < sizeof( g_archives ) / sizeof( g_archives[0] ); j++ )
	{
		char fullpath[MAX_SYSPATH];

		for( int i = 0; i < list.numstrings; i++ )
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
	searchpath_t *cur, **prev = &fs_searchpaths;

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

	for( int i = 0; i < FI.numgames; i++ )
	{
		if( FI.games[i] )
			FI.games[i]->added = false;
	}
}

/*
================
FS_CheckNastyPath

================
*/
int FS_CheckNastyPath( const char *path )
{
	// all: never allow an empty path, as for gamedir it would access the parent directory and a non-gamedir path it is just useless
	if( COM_StringEmptyOrNULL( path ))
		return 2;

	if( fs_ext_path )
		return 0; // allow any path

	return COM_CheckNastyPath( path );
}

/*
================
FS_PathExecFlag

guess if path is executable
only implemented for Android for now
================
*/
static uint32_t FS_PathExecFlag( const char *dir )
{
#if XASH_ANDROID
	// on Android, where directories usually lie outside of internal app directory
	// we can't reliably load libraries from
	//
	// currently, launcher passes /data/... path as rodir where downloaded game libraries
	// are stored in. Catch that and mark such path as executable
	if( !Q_strncmp( dir, "/data/", 6 ))
		return FS_EXEC_PATH;
	return 0;
#else // !XASH_ANDROID
	// FIXME: read noexec flag on *nix systems?
	(void)dir;
	return FS_EXEC_PATH;
#endif // !XASH_ANDROID
}

/*
================
FS_AddGameHierarchy

================
*/
void FS_AddGameHierarchy( const char *dir, uint flags )
{
	const qboolean is_game_dir = FBitSet( flags, FS_GAMEDIR_PATH );
	const uint32_t mount_flags = FBitSet( flags, FS_MOUNT_FLAGS );

	if( COM_StringEmptyOrNULL( dir ))
		return;

	Con_Printf( "%s( %s )\n", __func__, dir );

	// add the common game directory

	// recursive gamedirs
	// for example, czeror->czero->cstrike->valve
	for( int i = 0; i < FI.numgames; i++ )
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

	// clear flags not applicable to searchpaths
	ClearBits( flags, FS_MOUNT_FLAGS );

	char buf[MAX_VA_STRING];
	if( !COM_StringEmpty( fs_rodir ))
	{
		uint32_t new_flags = flags
			| FS_NOWRITE_PATH
			| ( is_game_dir ? FS_GAMERODIR_PATH : 0 )
			| FS_PathExecFlag( fs_rodir );

		// clear flags not applicable to read-only directory
		ClearBits( new_flags, FS_GAMEDIR_PATH | FS_CUSTOM_PATH );

		Q_snprintf( buf, sizeof( buf ), "%s/%s/", fs_rodir, dir );

		FS_AllowDirectPaths( true );
		FS_AddGameDirectory( buf, new_flags );
		FS_AllowDirectPaths( false );
	}

	if( is_game_dir )
	{
		Q_snprintf( buf, sizeof( buf ), "%s" DEFAULT_DOWNLOADED_DIRECTORY_SUFFIX "/", dir );
		FS_AddGameDirectory( buf, flags | FS_NOWRITE_PATH | FS_CUSTOM_PATH );
	}

	Q_snprintf( buf, sizeof( buf ), "%s/", dir );
	FS_AddGameDirectory( buf, flags | FS_PathExecFlag( buf ) | FS_PathExecFlag( fs_rootdir ));

	// paths after can only be addon paths
	SetBits( flags, FS_NOWRITE_PATH | FS_CUSTOM_PATH );

	if( FBitSet( mount_flags, FS_MOUNT_HD ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_hd/", dir );
		FS_AddGameDirectory( buf, flags );
	}

	if( FBitSet( mount_flags, FS_MOUNT_ADDON ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_addon/", dir );
		FS_AddGameDirectory( buf, flags );
	}

	if( FBitSet( mount_flags, FS_MOUNT_LV ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_lv/", dir );
		FS_AddGameDirectory( buf, flags );
	}

	if( FBitSet( mount_flags, FS_MOUNT_L10N ))
	{
		Q_snprintf( buf, sizeof( buf ), "%s_%s/", dir, fs_language );
		FS_AddGameDirectory( buf, flags );
	}

	if( is_game_dir )
	{
		Q_snprintf( buf, sizeof( buf ), "%s/" DEFAULT_CUSTOM_DIRECTORY, dir );
		FS_AddGameDirectory( buf, flags );
	}
}

/*
================
FS_Rescan

================
*/
void FS_Rescan( uint32_t flags, const char *language )
{
	Con_Reportf( "%s( %s, 0x%x, %s )\n", __func__, GI->title, flags, language );

	FS_ClearSearchPath();

	// don't let rescan set searchpath flags
	flags = FBitSet( flags, FS_MOUNT_FLAGS );

	if( FBitSet( flags, FS_MOUNT_L10N ) && !COM_StringEmpty( language ) && Q_isalpha( language ))
	{
		Q_strncpy( fs_language, language, sizeof( fs_language ));
	}
	else
	{
		fs_language[0] = 0;
		ClearBits( flags, FS_MOUNT_L10N );
	}

	const char *str = getenv( "XASH3D_EXTRAS_PAK1" );
	if( !COM_StringEmptyOrNULL( str ))
		FS_MountArchive_Fullpath( str, FS_NOWRITE_PATH | FS_CUSTOM_PATH );

	str = getenv( "XASH3D_EXTRAS_PAK2" );
	if( !COM_StringEmptyOrNULL( str ))
		FS_MountArchive_Fullpath( str, FS_NOWRITE_PATH | FS_CUSTOM_PATH );

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
const char *FS_Gamedir( void )
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
void FS_LoadGameInfo( uint32_t flags, const char *language )
{
	int i;

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
	file_t *f;
	int key;

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
	int len = Q_snprintf( idiot_relpath, sizeof( idiot_relpath ), "../%s/", gamefolder );

	if( len >= 4 )
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

static void FS_ValidateDirectories( const char *path, qboolean *has_base_dir, qboolean *has_game_dir )
{
	stringlist_t dirs;

	stringlistinit( &dirs );
	listdirectory( &dirs, path, true );
	stringlistsort( &dirs );

	for( int i = 0; i < dirs.numstrings; i++ )
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
	int rodir_num_games;
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
		FS_AddGameDirectory( buf, FS_STATIC_PATH | FS_NOWRITE_PATH | FS_PathExecFlag( fs_rodir ));
	}

	FS_AddGameDirectory( "./", FS_STATIC_PATH | FS_PathExecFlag( fs_rootdir ));

	// but scan rodir for games first
	if( !COM_StringEmpty( fs_rodir ))
	{
		stringlistinit( &dirs );
		listdirectory( &dirs, fs_rodir, true );
		stringlistsort( &dirs );

		for( int i = 0; i < dirs.numstrings; i++ )
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

	for( int i = 0; i < dirs.numstrings; i++ )
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
	// release gamedirs
	for( int i = 0; i < FI.numgames; i++ )
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
	Con_Printf( "Current search path:\n" );

	for( searchpath_t *s = fs_searchpaths; s; s = s->next )
	{
		string fl;
		string info;

		s->pfnPrintInfo( s, info, sizeof( info ));

		fl[0] = 0;
		Q_strncat( fl, FBitSet( s->flags, FS_GAMERODIR_PATH ) ? "^1R" : "^7-", sizeof( fl ));
		Q_strncat( fl, FBitSet( s->flags, FS_NOWRITE_PATH )   ? "^7-" : "^2W", sizeof( fl ));
		Q_strncat( fl, FBitSet( s->flags, FS_EXEC_PATH )      ? "^3X" : "^7-", sizeof( fl ));
		Q_strncat( fl, FBitSet( s->flags, FS_GAMEDIR_PATH )   ? "^4G" : "^7-", sizeof( fl ));
		Q_strncat( fl, FBitSet( s->flags, FS_CUSTOM_PATH )    ? "^5C" : "^7-", sizeof( fl ));
		Q_strncat( fl, FBitSet( s->flags, FS_STATIC_PATH )    ? "^6S" : "^7-", sizeof( fl ));
		Q_strncat( fl, "^7", sizeof( fl ));

		Con_Printf( "%s\t%s\n", fl, info );
	}
}

/*
====================
FS_FindFile_f

Print all search paths where the file was found,
ordered by priority (first one is used for FS operations)
====================
*/
void FS_FindFile_f( const char *filename )
{
	int	count = 0;
	Con_Printf( "File " S_YELLOW "%s" S_DEFAULT " occurences:\n", filename );

	for( searchpath_t *s = fs_searchpaths; s; s = s->next )
	{
		string fixedname;
		if( s->pfnFindFile( s, filename, fixedname, sizeof( fixedname )) >= 0 )
		{
			string info;
			count++;
			s->pfnPrintInfo( s, info, sizeof( info ));
			Con_Printf( "  " S_CYAN "%s%s\n", info, count == 1 ? " " S_GREEN "(active)" : "" );
		}
	}

	if( count == 0 )
		Con_Printf( "  " S_RED "(not found)\n" );
}

/*
====================
FS_FindFile

Look for a file in the packages and in the filesystem

Return the searchpath where the file was found (or NULL)
and the file index in the package if relevant
====================
*/
searchpath_t *FS_FindFile( const char *name, int *index, char *fixedname, size_t len, uint32_t flags )
{
	searchpath_t *search;

	// search through the path, one element at a time
	for( search = fs_searchpaths; search; search = search->next )
	{
		int pack_ind;

		if( flags && !FBitSet( search->flags, flags ))
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
==================
FS_FindLibrary

search for library, assume index is valid
==================
*/
qboolean FS_FindLibrary( const char *dllname, qboolean directpath, fs_dllinfo_t *dllInfo )
{
	string fixedname;
	searchpath_t *search;
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

	search = FS_FindFile( dllInfo->shortPath, &index, fixedname, sizeof( fixedname ), FS_EXEC_PATH );

	if( search )
	{
		Q_strncpy( dllInfo->shortPath, fixedname, sizeof( dllInfo->shortPath ));
	}
	else if( !directpath )
	{
		FS_AllowDirectPaths( false );

		// trying check also 'bin' folder for indirect paths
		search = FS_FindFile( dllname, &index, fixedname, sizeof( fixedname ), FS_EXEC_PATH );
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

/*
===========================
FS_FullPathToRelativePath

Converts full path to the relative path considering current searchpaths
(do not use this function, implemented only for VFileSystem009)
===========================
*/
qboolean FS_FullPathToRelativePath( char *dst, const char *src, size_t size )
{
	for( searchpath_t *sp = fs_searchpaths; sp; sp = sp->next )
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
FS_Search

Allocate and fill a search structure with information on matching filenames.
===========
*/
search_t *FS_Search( const char *pattern, int caseinsensitive, int gamedironly )
{
	search_t *search = NULL;
	int numfiles, numchars;
	stringlist_t resultlist;

	if( pattern[0] == '.' || pattern[0] == ':' || pattern[0] == '/' || pattern[0] == '\\' )
		return NULL; // punctuation issues

	stringlistinit( &resultlist );

	// search through the path, one element at a time
	for( searchpath_t *searchpath = fs_searchpaths; searchpath; searchpath = searchpath->next )
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

		for( int i = 0; i < resultlist.numstrings; i++ )
			numchars += (int)Q_strlen( resultlist.strings[i]) + 1;
		search = Mem_Calloc( fs_mempool, sizeof(search_t) + numchars + numfiles * sizeof( char* ));
		search->filenames = (char **)((char *)search + sizeof( search_t ));
		search->filenamesbuffer = (char *)((char *)search + sizeof( search_t ) + numfiles * sizeof( char* ));
		search->numfilenames = (int)numfiles;
		numfiles = numchars = 0;

		for( int i = 0; i < resultlist.numstrings; i++ )
		{
			size_t textlen;

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

qboolean FS_IsArchiveExtensionSupported( const char *ext, uint flags )
{
	if( ext == NULL )
		return false;

	for( int i = 0; i < sizeof( g_archives ) / sizeof( g_archives[0] ); i++ )
	{
		if( FBitSet( flags, IAES_ONLY_REAL_ARCHIVES ) && !g_archives[i].real_archive )
			continue;

		if( !Q_stricmp( ext, g_archives[i].ext ))
			return true;
	}

	return false;
}

searchpath_t *FS_GetArchiveByName( const char *name, searchpath_t *prev )
{
	searchpath_t *sp = prev ? prev->next : fs_searchpaths;

	for( ; sp; sp = sp->next )
	{
		if( !Q_stricmp( COM_FileWithoutPath( sp->filename ), name ))
			return sp;
	}

	return NULL;
}

int FS_FindFileInArchive( searchpath_t *sp, const char *path, char *truepath, size_t len )
{
	return sp->pfnFindFile( sp, path, truepath, len );
}

file_t *FS_OpenFileFromArchive( searchpath_t *sp, const char *path, const char *mode, int pack_ind )
{
	return sp->pfnOpenFile( sp, path, mode, pack_ind );
}
