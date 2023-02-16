/*
filesystem.h - engine FS
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

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#define FS_API_VERSION 2 // not stable yet!
#define FS_API_CREATEINTERFACE_TAG "XashFileSystem002" // follow FS_API_VERSION!!!

// search path flags
enum
{
	FS_STATIC_PATH    = BIT( 0 ), // FS_ClearSearchPath will be ignore this path
	FS_NOWRITE_PATH   = BIT( 1 ), // default behavior - last added gamedir set as writedir. This flag disables it
	FS_GAMEDIR_PATH   = BIT( 2 ), // just a marker for gamedir path
	FS_CUSTOM_PATH    = BIT( 3 ), // gamedir but with custom/mod data
	FS_GAMERODIR_PATH = BIT( 4 ), // gamedir but read-only

	FS_GAMEDIRONLY_SEARCH_FLAGS = FS_GAMEDIR_PATH | FS_CUSTOM_PATH | FS_GAMERODIR_PATH
};

typedef struct
{
	int	numfilenames;
	char	**filenames;
	char	*filenamesbuffer;
} search_t;

typedef struct gameinfo_s
{
	// filesystem info
	char		gamefolder[MAX_QPATH];	// used for change game '-game x'
	char		basedir[MAX_QPATH];	// base game directory (like 'id1' for Quake or 'valve' for Half-Life)
	char		falldir[MAX_QPATH];	// used as second basedir
	char		startmap[MAX_QPATH];// map to start singleplayer game
	char		trainmap[MAX_QPATH];// map to start hazard course (if specified)
	char		title[64];	// Game Main Title
	float		version;		// game version (optional)

	// .dll pathes
	char		dll_path[MAX_QPATH];	// e.g. "bin" or "cl_dlls"
	char		game_dll[MAX_QPATH];	// custom path for game.dll

	// .ico path
	char		iconpath[MAX_QPATH];	// "game.ico" by default

	// about mod info
	string		game_url;		// link to a developer's site
	string		update_url;	// link to updates page
	char		type[MAX_QPATH];	// single, toolkit, multiplayer etc
	char		date[MAX_QPATH];
	size_t		size;

	int		gamemode;
	qboolean	secure;		// prevent to console acess
	qboolean	nomodels;		// don't let player to choose model (use player.mdl always)
	qboolean	noskills;		// disable skill menu selection
	qboolean	render_picbutton_text; // use font renderer to render WON buttons
	qboolean	internal_vgui_support; // skip loading VGUI, pass ingame UI support API to client

	char		sp_entity[32];	// e.g. info_player_start
	char		mp_entity[32];	// e.g. info_player_deathmatch
	char		mp_filter[32];	// filtering multiplayer-maps

	char		ambientsound[NUM_AMBIENTS][MAX_QPATH];	// quake ambient sounds

	int		max_edicts;	// min edicts is 600, max edicts is 8196
	int		max_tents;	// min temp ents is 300, max is 2048
	int		max_beams;	// min beams is 64, max beams is 512
	int		max_particles;	// min particles is 4096, max particles is 32768

	char		game_dll_linux[64];	// custom path for game.dll
	char		game_dll_osx[64];	// custom path for game.dll

	qboolean	added;
} gameinfo_t;

typedef enum
{
	GAME_NORMAL,
	GAME_SINGLEPLAYER_ONLY,
	GAME_MULTIPLAYER_ONLY
} gametype_t;

typedef struct fs_dllinfo_t
{
	string fullPath;
	string shortPath;
	qboolean encrypted;
	qboolean custom_loader;
} fs_dllinfo_t;

typedef struct fs_globals_t
{
	gameinfo_t	*GameInfo;	// current GameInfo
	gameinfo_t	*games[MAX_MODS];	// environment games (founded at each engine start)
	int		numgames;
} fs_globals_t;

typedef void (*fs_event_callback_t)( const char *path );


typedef struct fs_api_t
{
	qboolean (*InitStdio)( qboolean caseinsensitive, const char *rootdir, const char *basedir, const char *gamedir, const char *rodir );
	void (*ShutdownStdio)( void );

	// search path utils
	void (*Rescan)( void );
	void (*ClearSearchPath)( void );
	void (*AllowDirectPaths)( qboolean enable );
	void (*AddGameDirectory)( const char *dir, uint flags );
	void (*AddGameHierarchy)( const char *dir, uint flags );
	search_t *(*Search)( const char *pattern, int caseinsensitive, int gamedironly );
	int (*SetCurrentDirectory)( const char *path );
	qboolean (*FindLibrary)( const char *dllname, qboolean directpath, fs_dllinfo_t *dllinfo );
	void (*Path_f)( void );

	// gameinfo utils
	void (*LoadGameInfo)( const char *rootfolder );

	// file ops
	file_t *(*Open)( const char *filepath, const char *mode, qboolean gamedironly );
	fs_offset_t (*Write)( file_t *file, const void *data, size_t datasize );
	fs_offset_t (*Read)( file_t *file, void *buffer, size_t buffersize );
	int (*Seek)( file_t *file, fs_offset_t offset, int whence );
	fs_offset_t (*Tell)( file_t *file );
	qboolean (*Eof)( file_t *file );
	int (*Flush)( file_t *file );
	int (*Close)( file_t *file );
	int (*Gets)( file_t *file, byte *string, size_t bufsize );
	int (*UnGetc)( file_t *file, byte c );
	int (*Getc)( file_t *file );
	int (*VPrintf)( file_t *file, const char *format, va_list ap );
	int (*Printf)( file_t *file, const char *format, ... ) _format( 2 );
	int (*Print)( file_t *file, const char *msg );
	fs_offset_t (*FileLength)( file_t *f );
	qboolean (*FileCopy)( file_t *pOutput, file_t *pInput, int fileSize );

	// file buffer ops
	byte *(*LoadFile)( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly );
	byte *(*LoadDirectFile)( const char *path, fs_offset_t *filesizeptr );
	qboolean (*WriteFile)( const char *filename, const void *data, fs_offset_t len );

	// file hashing
	qboolean (*CRC32_File)( dword *crcvalue, const char *filename );
	qboolean (*MD5_HashFile)( byte digest[16], const char *pszFileName, uint seed[4] );

	// filesystem ops
	int (*FileExists)( const char *filename, int gamedironly );
	int (*FileTime)( const char *filename, qboolean gamedironly );
	fs_offset_t (*FileSize)( const char *filename, qboolean gamedironly );
	qboolean (*Rename)( const char *oldname, const char *newname );
	qboolean (*Delete)( const char *path );
	qboolean (*SysFileExists)( const char *path );
	const char *(*GetDiskPath)( const char *name, qboolean gamedironly );

	// file watcher
	void (*WatchFrame)( void ); // engine will read all events and call appropriate callbacks
	qboolean (*AddWatch)( const char *path, fs_event_callback_t callback );
} fs_api_t;

typedef struct fs_interface_t
{
	// logging
	void    (*_Con_Printf)( const char *fmt, ... ) _format( 1 ); // typical console allowed messages
	void    (*_Con_DPrintf)( const char *fmt, ... ) _format( 1 ); // -dev 1
	void    (*_Con_Reportf)( const char *fmt, ... ) _format( 1 ); // -dev 2

	void    (*_Sys_Error)( const char *fmt, ... ) _format( 1 );

	// memory
	poolhandle_t (*_Mem_AllocPool)( const char *name, const char *filename, int fileline );
	void  (*_Mem_FreePool)( poolhandle_t *poolptr, const char *filename, int fileline );
	void *(*_Mem_Alloc)( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline );
	void *(*_Mem_Realloc)( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline );
	void  (*_Mem_Free)( void *data, const char *filename, int fileline );
} fs_interface_t;

typedef int (*FSAPI)( int version, fs_api_t *api, fs_globals_t **globals, fs_interface_t *interface );
#define GET_FS_API "GetFSAPI"

#ifdef __cplusplus
}
#endif // __cplusplus

#endif//FILESYSTEM_H
