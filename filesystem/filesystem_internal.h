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

#ifndef FILESYSTEM_INTERNAL_H
#define FILESYSTEM_INTERNAL_H

#include "xash3d_types.h"
#include "filesystem.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct zip_s zip_t;
typedef struct pack_s pack_t;
typedef struct wfile_s wfile_t;

#define FILE_BUFF_SIZE		(2048)

struct file_s
{
	int		handle;			// file descriptor
	int		ungetc;			// single stored character from ungetc, cleared to EOF when read
	fs_offset_t		real_length;		// uncompressed file size (for files opened in "read" mode)
	fs_offset_t		position;			// current position in the file
	fs_offset_t		offset;			// offset into the package (0 if external file)
	time_t		filetime;			// pak, wad or real filetime
						// contents buffer
	fs_offset_t		buff_ind, buff_len;		// buffer current index and length
	byte		buff[FILE_BUFF_SIZE];	// intermediate buffer
#ifdef XASH_REDUCE_FD
	const char *backup_path;
	fs_offset_t backup_position;
	uint backup_options;
#endif
};

enum
{
	SEARCHPATH_PLAIN = 0,
	SEARCHPATH_PAK,
	SEARCHPATH_WAD,
	SEARCHPATH_ZIP
};

typedef struct stringlist_s
{
	// maxstrings changes as needed, causing reallocation of strings[] array
	int		maxstrings;
	int		numstrings;
	char		**strings;
} stringlist_t;

typedef struct searchpath_s
{
	string  filename;
	int     type;
	int     flags;
	
	union
	{
		pack_t  *pack;
		wfile_t *wad;
		zip_t   *zip;
	};

	struct searchpath_s *next;

	void	( *printinfo )( struct searchpath_s *search, char *dst, size_t size );
	void	( *close )( struct searchpath_s *search );
	file_t	*( *openfile )( struct searchpath_s *search, const char *filename, const char *mode, int pack_ind );
	int		( *filetime )( struct searchpath_s *search, const char *filename );
	int		( *findfile )( struct searchpath_s *search, const char *path );
	void	( *search )( struct searchpath_s *search, stringlist_t *list, const char *pattern, int caseinsensitive );
} searchpath_t;

extern fs_globals_t  FI;
extern searchpath_t *fs_searchpaths;
extern poolhandle_t  fs_mempool;
extern fs_interface_t g_engfuncs;
extern qboolean      fs_ext_path;
extern char          fs_rodir[MAX_SYSPATH];
extern char          fs_rootdir[MAX_SYSPATH];
extern char          fs_writedir[MAX_SYSPATH];
extern fs_api_t      g_api;

#define GI FI.GameInfo

#define Mem_Malloc( pool, size ) g_engfuncs._Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) g_engfuncs._Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) g_engfuncs._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) g_engfuncs._Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) g_engfuncs._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) g_engfuncs._Mem_FreePool( pool, __FILE__, __LINE__ )

#define Con_Printf  (*g_engfuncs._Con_Printf)
#define Con_DPrintf (*g_engfuncs._Con_DPrintf)
#define Con_Reportf (*g_engfuncs._Con_Reportf)
#define Sys_Error   (*g_engfuncs._Sys_Error)

//
// filesystem.c
//
qboolean FS_InitStdio( qboolean caseinsensitive, const char *rootdir, const char *basedir, const char *gamedir, const char *rodir );
void FS_ShutdownStdio( void );

// search path utils
void FS_Rescan( void );
void FS_ClearSearchPath( void );
void FS_AllowDirectPaths( qboolean enable );
void FS_AddGameDirectory( const char *dir, uint flags );
void FS_AddGameHierarchy( const char *dir, uint flags );
search_t *FS_Search( const char *pattern, int caseinsensitive, int gamedironly );
int FS_SetCurrentDirectory( const char *path );
void FS_Path_f( void );

// gameinfo utils
void FS_LoadGameInfo( const char *rootfolder );

// file ops
file_t *FS_Open( const char *filepath, const char *mode, qboolean gamedironly );
fs_offset_t FS_Write( file_t *file, const void *data, size_t datasize );
fs_offset_t FS_Read( file_t *file, void *buffer, size_t buffersize );
int FS_Seek( file_t *file, fs_offset_t offset, int whence );
fs_offset_t FS_Tell( file_t *file );
qboolean FS_Eof( file_t *file );
int FS_Flush( file_t *file );
int FS_Close( file_t *file );
int FS_Gets( file_t *file, byte *string, size_t bufsize );
int FS_UnGetc( file_t *file, byte c );
int FS_Getc( file_t *file );
int FS_VPrintf( file_t *file, const char *format, va_list ap );
int FS_Printf( file_t *file, const char *format, ... ) _format( 2 );
int FS_Print( file_t *file, const char *msg );
fs_offset_t FS_FileLength( file_t *f );
qboolean FS_FileCopy( file_t *pOutput, file_t *pInput, int fileSize );

// file buffer ops
byte *FS_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly );
byte *FS_LoadDirectFile( const char *path, fs_offset_t *filesizeptr );
qboolean FS_WriteFile( const char *filename, const void *data, fs_offset_t len );

// file hashing
qboolean CRC32_File( dword *crcvalue, const char *filename );
qboolean MD5_HashFile( byte digest[16], const char *pszFileName, uint seed[4] );

// filesystem ops
int FS_FileExists( const char *filename, int gamedironly );
int FS_FileTime( const char *filename, qboolean gamedironly );
fs_offset_t FS_FileSize( const char *filename, qboolean gamedironly );
qboolean FS_Rename( const char *oldname, const char *newname );
qboolean FS_Delete( const char *path );
qboolean FS_SysFileExists( const char *path, qboolean casesensitive );
const char *FS_GetDiskPath( const char *name, qboolean gamedironly );
void 	 stringlistinit( stringlist_t *list );
void 	 stringlistfreecontents( stringlist_t *list );
void     stringlistappend( stringlist_t *list, char *text );
void 	 listdirectory( stringlist_t *list, const char *path, qboolean lowercase );
void     FS_CreatePath( char *path );
qboolean FS_SysFolderExists( const char *path );
file_t  *FS_OpenReadFile( const char *filename, const char *mode, qboolean gamedironly );

int           FS_SysFileTime( const char *filename );
file_t       *FS_OpenHandle( const char *syspath, int handle, fs_offset_t offset, fs_offset_t len );
file_t       *FS_SysOpen( const char *filepath, const char *mode );
const char   *FS_FixFileCase( const char *path );
searchpath_t *FS_FindFile( const char *name, int *index, qboolean gamedironly );

//
// pak.c
//
int      FS_FileTime_PAK( searchpath_t *search, const char *filename );
int      FS_FindFile_PAK( searchpath_t *search, const char *path );
void     FS_PrintInfo_PAK( searchpath_t *search, char *dst, size_t size );
void     FS_Close_PAK( searchpath_t *search );
void     FS_Search_PAK( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive );
file_t  *FS_OpenFile_PAK( searchpath_t *search, const char *filename, const char *mode, int pack_ind );
qboolean FS_AddPak_Fullpath( const char *pakfile, qboolean *already_loaded, int flags );

//
// wad.c
//
int      FS_FileTime_WAD( searchpath_t *search, const char *filename );
int      FS_FindFile_WAD( searchpath_t *search, const char *path );
void     FS_PrintInfo_WAD( searchpath_t *search, char *dst, size_t size );
void     FS_Close_WAD( searchpath_t *search );
void     FS_Search_WAD( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive );
byte    *FS_LoadWADFile( const char *path, fs_offset_t *sizeptr, qboolean gamedironly );
qboolean FS_AddWad_Fullpath( const char *wadfile, qboolean *already_loaded, int flags );

//
// watch.c
//
qboolean FS_WatchInitialize( void );
int FS_AddWatch( const char *path, fs_event_callback_t callback );
void FS_WatchFrame( void );

//
// zip.c
//
int      FS_FileTime_ZIP( searchpath_t *search, const char *filename );
int      FS_FindFile_ZIP( searchpath_t *search, const char *path );
void     FS_PrintInfo_ZIP( searchpath_t *search, char *dst, size_t size );
void     FS_Close_ZIP( searchpath_t *search );
void     FS_Search_ZIP( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive );
byte    *FS_LoadZIPFile( const char *path, fs_offset_t *sizeptr, qboolean gamedironly );
file_t  *FS_OpenFile_ZIP( searchpath_t *search, const char *filename, const char *mode, int pack_ind );
qboolean FS_AddZip_Fullpath( const char *zipfile, qboolean *already_loaded, int flags );

//
// dir.c
//
void	FS_PrintInfo_DIR( searchpath_t *search, char *dst, size_t size );
void	FS_Close_DIR( searchpath_t *search );
file_t *FS_OpenFile_DIR( searchpath_t *search, const char *filename, const char *mode, int pack_ind );
int		FS_FileTime_DIR( searchpath_t *search, const char *filename );
int		FS_FindFile_DIR( searchpath_t *search, const char *path );
void	FS_Search_DIR( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive );

#ifdef __cplusplus
}
#endif

#endif // FILESYSTEM_INTERNAL_H
