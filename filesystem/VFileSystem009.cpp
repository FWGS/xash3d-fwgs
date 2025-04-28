/*
VFileSystem009.h - C++ interface for filesystem_stdio
Copyright (C) 2022-2023 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
*/
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include ALLOCA_H
#include "crtlib.h"
#include "filesystem.h"
#include "filesystem_internal.h"
#include "VFileSystem009.h"
#include "common/com_strings.h"

#if __cplusplus < 201103L
#define override
#define nullptr NULL
#endif

// GoldSrc Directories and ID
// GAME          gamedir
// GAMECONFIG    gamedir (rodir integration?)
// GAMEDOWNLOAD  gamedir_downloads (gamedir/downloads for us)
// GAME_FALLBACK liblist.gam's fallback_dir
// ROOT and BASE rootdir
// PLATFORM      platform
// CONFIG        platform/config

// This is a macro because pointers returned by alloca
// shouldn't leave current scope
#define FixupPath( var, str ) \
	const size_t var ## _size = Q_strlen(( str )) + 1; \
	char * const var = static_cast<char *>( alloca( var ## _size )); \
	CopyAndFixSlashes( var, ( str ), var ## _size )

static inline bool IsIdGamedir( const char *id )
{
	return !Q_strcmp( id, "GAME" ) ||
		!Q_strcmp( id, "GAMECONFIG" ) ||
		!Q_strcmp( id, "GAMEDOWNLOAD" );
}

static inline const char *IdToDir( char *dir, size_t size, const char *id )
{
	if( !Q_strcmp( id, "GAME" ))
		return GI->gamefolder;

	if( !Q_strcmp( id, "GAMEDOWNLOAD" ))
	{
		Q_snprintf( dir, size, "%s/" DEFAULT_DOWNLOADED_DIRECTORY , GI->gamefolder );
		return dir;
	}

	if( !Q_strcmp( id, "GAMECONFIG" ))
		return fs_writepath->filename; // full path here so it's totally our write allowed directory

	if( !Q_strcmp( id, "PLATFORM" ))
		return "platform"; // stub

	if( !Q_strcmp( id, "CONFIG" ))
		return "platform/config"; // stub

	// ROOT || BASE
	return fs_rootdir; // give at least root directory
}

static inline void CopyAndFixSlashes( char *p, const char *in, size_t size )
{
	Q_strncpy( p, in, size );
	COM_FixSlashes( p );
}

class CXashFS : public IFileSystem
{
private:
	class CSearchState
	{
	public:
		CSearchState( CSearchState **head, search_t *search ) :
			next( *head ),
			search( search ),
			index( 0 ),
			handle( *head ? ( *head )->handle + 1 : 0 )
		{
			*head = this;
		}
		~CSearchState()
		{
			Mem_Free( search );
		}

		CSearchState *next;
		search_t *search;
		int index;
		FileFindHandle_t handle;
	};

	CSearchState *searchHead;

	CSearchState *GetSearchStateByHandle( FileFindHandle_t handle )
	{
		for( CSearchState *state = searchHead; state; state = state->next )
		{
			if( state->handle == handle )
				return state;
		}

		Con_DPrintf( "Can't find search state by handle %d\n", handle );
		return nullptr;
	}

public:
	CXashFS() : searchHead( nullptr )
	{
	}

	void RemoveAllSearchPaths() override
	{
		FS_ClearSearchPath();
	}

	void AddSearchPath( const char *path, const char *id ) override
	{
		FixupPath( p, path );
		FS_AddGameDirectory( p, FS_CUSTOM_PATH );
	}

	void AddSearchPathNoWrite( const char *path, const char *id ) override
	{
		FixupPath( p, path );
		FS_AddGameDirectory( p, FS_NOWRITE_PATH | FS_CUSTOM_PATH );
	}

	bool RemoveSearchPath( const char *id ) override
	{
		// TODO:
		return true;
	}

	void RemoveFile( const char *path, const char *id ) override
	{
		char dir[MAX_VA_STRING], fullpath[MAX_VA_STRING];

		Q_snprintf( fullpath, sizeof( fullpath ), "%s/%s", IdToDir( dir, sizeof( dir ), id ), path );
		FS_Delete( fullpath ); // FS_Delete is aware of slashes
	}

	void CreateDirHierarchy( const char *path, const char *id ) override
	{
		char dir[MAX_VA_STRING], fullpath[MAX_VA_STRING];

		Q_snprintf( fullpath, sizeof( fullpath ), "%s/%s", IdToDir( dir, sizeof( dir ), id ), path );
		FS_CreatePath( fullpath ); // FS_CreatePath is aware of slashes
	}

	bool FileExists( const char *path ) override
	{
		FixupPath( p, path );
		return FS_FileExists( p, false );
	}

	bool IsDirectory( const char *path ) override
	{
		FixupPath( p, path );
		return FS_SysFolderExists( p );
	}

	FileHandle_t Open( const char *path, const char *mode, const char *id ) override
	{
		file_t *fd;

		FixupPath( p, path );
		fd = FS_Open( p, mode, IsIdGamedir( id ));

		return fd;
	}

	void Close( FileHandle_t handle ) override
	{
		FS_Close( static_cast<file_t *>( handle ));
	}

	void Seek( FileHandle_t handle, int offset, FileSystemSeek_t whence ) override
	{
		int whence_ = SEEK_SET;

		switch( whence )
		{
		case FILESYSTEM_SEEK_HEAD:
			whence_ = SEEK_SET;
			break;
		case FILESYSTEM_SEEK_CURRENT:
			whence_ = SEEK_CUR;
			break;
		case FILESYSTEM_SEEK_TAIL:
			whence_ = SEEK_END;
			break;
		}

		FS_Seek( static_cast<file_t *>( handle ), offset, whence_ );
	}

	unsigned int Tell( FileHandle_t handle ) override
	{
		return FS_Tell( static_cast<file_t *>( handle ));
	}

	unsigned int Size( FileHandle_t handle ) override
	{
		return static_cast<file_t *>( handle )->real_length;
	}

	unsigned int Size( const char *path ) override
	{
		FixupPath( p, path );
		return FS_FileSize( p, false );
	}

	long int GetFileTime( const char *path ) override
	{
		FixupPath( p, path );
		return FS_FileTime( p, false );
	}

	long int GetFileModificationTime( const char *path ) override
	{
		// TODO: properly reverse-engineer this
		FixupPath( p, path );
		return FS_FileTime( p, false );
	}

	void FileTimeToString( char *p, int size, long int time ) override
	{
		const time_t curtime = time;
		char *buf = ctime( &curtime );

		Q_strncpy( p, buf, size );
	}

	bool IsOk( FileHandle_t handle ) override
	{
		return !FS_Eof( static_cast<file_t *>( handle ));
	}

	void Flush( FileHandle_t handle ) override
	{
		FS_Flush( static_cast<file_t *>( handle ));
	}

	bool EndOfFile( FileHandle_t handle ) override
	{
		return FS_Eof( static_cast<file_t *>( handle ));
	}

	int Read( void *buf, int size, FileHandle_t handle ) override
	{
		return FS_Read( static_cast<file_t *>( handle ), buf, size );
	}

	int Write( const void *buf, int size, FileHandle_t handle ) override
	{
		return FS_Write( static_cast<file_t *>( handle ), buf, size );
	}

	char *ReadLine( char *buf, int size, FileHandle_t handle ) override
	{
		const int c = FS_Gets( static_cast<file_t *>( handle ), buf, size );

		return c >= 0 ? buf : nullptr;
	}

	int FPrintf( FileHandle_t handle, char *fmt, ... ) override
	{
		va_list ap;
		int ret;

		va_start( ap, fmt );
		ret = FS_VPrintf( static_cast<file_t *>( handle ), fmt, ap );
		va_end( ap );

		return ret;
	}

	void *GetReadBuffer( FileHandle_t, int *size, bool ) override
	{
		// deprecated by Valve
		*size = 0;
		return nullptr;
	}

	void ReleaseReadBuffer( FileHandle_t, void * ) override
	{
		// deprecated by Valve
		return;
	}

	const char *FindFirst( const char *pattern, FileFindHandle_t *handle, const char *id ) override
	{
		CSearchState *state;
		search_t *search;

		if( !handle || !pattern )
			return nullptr;

		FixupPath( p, pattern );
		search = FS_Search( p, true, IsIdGamedir( id ));

		if( !search )
			return nullptr;

		state = new CSearchState( &searchHead, search );
		if( !state )
		{
			Mem_Free( search );
			return nullptr;
		}

		*handle = state->handle;
		return state->search->filenames[0];
	}

	const char *FindNext( FileFindHandle_t handle ) override
	{
		CSearchState *state = GetSearchStateByHandle( handle );

		if( !state )
			return nullptr;

		if( state->index + 1 >= state->search->numfilenames )
			return nullptr;

		return state->search->filenames[++state->index];
	}

	bool FindIsDirectory( FileFindHandle_t handle ) override
	{
		CSearchState *state = GetSearchStateByHandle( handle );

		if( !state )
			return false;

		if( state->index >= state->search->numfilenames )
			return false;

		return IsDirectory( state->search->filenames[state->index] );
	}

	void FindClose( FileFindHandle_t handle ) override
	{
		CSearchState *prev;
		CSearchState *i;

		for( prev = nullptr, i = searchHead;
			i != nullptr && i->handle != handle;
			prev = i, i = i->next );

		if( i == nullptr )
		{
			Con_DPrintf( "%s: Can't find search state by handle %d\n", __func__, handle );
			return;
		}

		if( prev != nullptr )
			prev->next = i->next;
		else
			searchHead = i->next;

		delete i;
	}

	const char *GetLocalPath( const char *name, char *buf, int size ) override
	{
		const char *fullpath;

		if( !name )
			return nullptr;

		FixupPath( p, name );

#if !XASH_WIN32
		if( p[0] == '/' )
#else
		if( Q_strchr( p, ':' ))
#endif
		{
			Q_strncpy( buf, p, size );

			return buf;
		}

		fullpath = FS_GetDiskPath( p, false );
		if( !fullpath )
			return nullptr;

		Q_strncpy( buf, fullpath, size );
		return buf;
	}

	char *ParseFile( char *buf, char *token, bool *quoted ) override
	{
		qboolean qquoted;
		char *p;

		p = COM_ParseFileSafe( buf, token, PFILE_FS_TOKEN_MAX_LENGTH, 0, nullptr, &qquoted );

		if( quoted )
			*quoted = qquoted;

		return p;
	}

	bool FullPathToRelativePath( const char *path, char *out ) override
	{
		if( !COM_CheckString( path ))
		{
			*out = 0;
			return false;
		}

		FixupPath( p, path );

		return FS_FullPathToRelativePath( out, p, 512 );
	}

	bool GetCurrentDirectory( char *p, int size ) override
	{
		return FS_GetRootDirectory( p, size );
	}

	void PrintOpenedFiles() override
	{
		// we don't track this yet
		return;
	}

	void SetWarningFunc( void (*)( const char *, ... )) override
	{
		// TODO:
		return;
	}

	void SetWarningLevel( FileWarningLevel_t ) override
	{
		// TODO:
		return;
	}

	int SetVBuf( FileHandle_t handle, char *buf, int mode, long int size ) override
	{
		// TODO:
		return 0;
	}

	void GetInterfaceVersion( char *p, int size ) override
	{
		Q_strncpy( p, "Stdio", size );
	}

	bool AddPackFile( const char *path, const char *id ) override
	{
		char dir[MAX_VA_STRING], fullpath[MAX_VA_STRING];

		IdToDir( dir, sizeof( dir ), id );
		Q_snprintf( fullpath, sizeof( fullpath ), "%s/%s", dir, path );
		COM_FixSlashes( fullpath );
		return FS_MountArchive_Fullpath( fullpath, FS_CUSTOM_PATH ) != NULL;
	}

	FileHandle_t OpenFromCacheForRead( const char *path , const char *mode, const char *id ) override
	{
		FixupPath( p, path );

		return FS_OpenReadFile( p, mode, IsIdGamedir( id ));
	}

	// stubs
	void Mount() override {}
	void Unmount() override {}
	void GetLocalCopy( const char * ) override {}
	void LogLevelLoadStarted( const char * ) override {}
	void LogLevelLoadFinished( const char * ) override {}
	void CancelWaitForResources( WaitForResourcesHandle_t ) override {}
	int HintResourceNeed( const char *, int ) override { return 0; }
	WaitForResourcesHandle_t WaitForResources( const char * ) override { return 0; }
	int PauseResourcePreloading() override { return 0; }
	int ResumeResourcePreloading() override { return 0; }
	bool IsAppReadyForOfflinePlay( int ) override { return true; }
	bool IsFileImmediatelyAvailable( const char * ) override { return true; }
	bool GetWaitForResourcesProgress( WaitForResourcesHandle_t, float *pProgress, bool *pOverride ) override
	{
		if( pProgress )
			*pProgress = 0;

		if( pOverride )
			*pOverride = true;

		return false;
	}
} g_VFileSystem009;

extern "C" void EXPORT *CreateInterface( const char *interface, int *retval )
{
	if( !Q_strcmp( interface, FILESYSTEM_INTERFACE_VERSION ))
	{
		if( retval )
			*retval = 0;

		return &g_VFileSystem009;
	}

	if( !Q_strcmp( interface, FS_API_CREATEINTERFACE_TAG ))
	{
		static fs_api_t copy; // return a copy, to disallow overriding

		copy = g_api;

		if( retval )
			*retval = 0;

		return &copy;
	}

	if( retval )
		*retval = 1;

	return nullptr;
}
