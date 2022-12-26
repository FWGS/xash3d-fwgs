/*
VFileSystem009.h - C++ interface for filesystem_stdio
Copyright (C) 2022 Alibek Omarov

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

#if __cplusplus < 201103L
#define override
#endif

// GoldSrc Directories and ID
// GAME          gamedir
// GAMECONFIG    gamedir (rodir integration?)
// GAMEDOWNLOAD  gamedir_downloads (gamedir/downloads for us)
// GAME_FALLBACK liblist.gam's fallback_dir
// ROOT and BASE rootdir
// PLATFORM      platform
// CONFIG        platform/config

static inline qboolean IsIdGamedir( const char *id )
{
	return !Q_strcmp( id, "GAME" ) ||
		!Q_strcmp( id, "GAMECONFIG" ) ||
		!Q_strcmp( id, "GAMEDOWNLOAD" );
}

static inline const char *IdToDir( const char *id )
{
	if( !Q_strcmp( id, "GAME" ))
		return GI->gamefolder;
	else if( !Q_strcmp( id, "GAMEDOWNLOAD" ))
		return va( "%s/downloaded", GI->gamefolder );
	else if( !Q_strcmp( id, "GAMECONFIG" ))
		return fs_writepath->filename; // full path here so it's totally our write allowed directory
	else if( !Q_strcmp( id, "PLATFORM" ))
		return "platform"; // stub
	else if( !Q_strcmp( id, "CONFIG" ))
		return "platform/config"; // stub
	else // ROOT || BASE
		return fs_rootdir; // give at least root directory
}

static inline void CopyAndFixSlashes( char *p, const char *in )
{
	Q_strcpy( p, in );
	COM_FixSlashes( p );
}

class CXashFS : public IVFileSystem009
{
private:
	class CSearchState
	{
	public:
		CSearchState( CSearchState **head, search_t *search ) :
			next( *head ), search( search ), index( 0 )
		{
			if( *head )
				handle = (*head)->handle + 1;
			else handle = 0;

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
			{
				return state;
			}
		}

		Con_DPrintf( "Can't find search state by handle %d\n", handle );
		return NULL;
	}

public:
	CXashFS() : searchHead( NULL )
	{
	}

	void RemoveAllSearchPaths() override
	{
		FS_ClearSearchPath();
	}

	void AddSearchPath( const char *path, const char *id ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );

		FS_AddGameDirectory( p, FS_CUSTOM_PATH );
	}

	void AddSearchPathNoWrite( const char *path, const char *id ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );

		FS_AddGameDirectory( p, FS_NOWRITE_PATH | FS_CUSTOM_PATH );
	}

	bool RemoveSearchPath( const char *id ) override
	{
		// TODO:
		return true;
	}

	void RemoveFile( const char *path, const char *id ) override
	{
		FS_Delete( path ); // FS_Delete is aware of slashes
	}

	void CreateDirHierarchy( const char *path, const char *id ) override
	{
		FS_CreatePath( va( "%s/%s", IdToDir( id ), path )); // FS_CreatePath is aware of slashes
	}

	bool FileExists( const char *path ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );

		return FS_FileExists( p, false );
	}

	bool IsDirectory( const char *path ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );

		return FS_SysFolderExists( p );
	}

	FileHandle_t Open( const char *path, const char *mode, const char *id ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );

		file_t *fd = FS_Open( p, mode, IsIdGamedir( id ) );

		return fd;
	}

	void Close( FileHandle_t handle ) override
	{
		FS_Close( (file_t *)handle );
	}

	void Seek( FileHandle_t handle, int offset, FileSystemSeek_t whence ) override
	{
		int whence_ = SEEK_SET;
		switch( whence )
		{
		case FILESYSTEM_SEEK_HEAD: whence_ = SEEK_SET; break;
		case FILESYSTEM_SEEK_CURRENT: whence_ = SEEK_CUR; break;
		case FILESYSTEM_SEEK_TAIL: whence_ = SEEK_END; break;
		}

		FS_Seek( (file_t *)handle, offset, whence_ );
	}

	unsigned int Tell( FileHandle_t handle ) override
	{
		return FS_Tell( (file_t *)handle );
	}

	unsigned int Size( FileHandle_t handle ) override
	{
		file_t *fd = (file_t *)handle;
		return fd->real_length;
	}

	unsigned int Size( const char *path ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );
		return FS_FileSize( p, false );
	}

	long int GetFileTime( const char *path ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );
		return FS_FileTime( p, false );
	}

	void FileTimeToString( char *p, int size, long int time ) override
	{
		time_t curtime = time;
		char *buf = ctime( &curtime );
		Q_strncpy( p, buf, size );
	}

	bool IsOk( FileHandle_t handle ) override
	{
		return !FS_Eof( (file_t *)handle );
	}

	void Flush( FileHandle_t handle ) override
	{
		FS_Flush( (file_t *)handle );
	}

	bool EndOfFile( FileHandle_t handle ) override
	{
		return FS_Eof( (file_t *)handle );
	}

	int Read( void *buf, int size, FileHandle_t handle ) override
	{
		return FS_Read( (file_t *)handle, buf, size );
	}

	int Write( const void *buf, int size, FileHandle_t handle ) override
	{
		return FS_Write( (file_t *)handle, buf, size );
	}

	char *ReadLine( char *buf, int size, FileHandle_t handle ) override
	{
		int c = FS_Gets( (file_t *)handle, (byte*)buf, size );

		return c >= 0 ? buf : NULL;
	}

	int FPrintf( FileHandle_t handle, char *fmt, ... ) override
	{
		va_list ap;
		int ret;

		va_start( ap, fmt );
		ret = FS_VPrintf( (file_t *)handle, fmt, ap );
		va_end( ap );

		return ret;
	}

	void * GetReadBuffer(FileHandle_t, int *size, bool) override
	{
		// deprecated by Valve
		*size = 0;
		return NULL;
	}

	void ReleaseReadBuffer(FileHandle_t, void *) override
	{
		// deprecated by Valve
		return;
	}

	const char *FindFirst(const char *pattern, FileFindHandle_t *handle, const char *id) override
	{
		if( !handle || !pattern )
			return NULL;

		char *p = (char *)alloca( Q_strlen( pattern ) + 1 );
		CopyAndFixSlashes( p, pattern );
		search_t *search = FS_Search( p, true, IsIdGamedir( id ));

		if( !search )
			return NULL;

		CSearchState *state = new CSearchState( &searchHead, search );

		*handle = state->handle;

		return state->search->filenames[0];
	}

	const char *FindNext( FileFindHandle_t handle ) override
	{
		CSearchState *state = GetSearchStateByHandle( handle );

		if( !state ) return NULL;

		if( state->index + 1 >= state->search->numfilenames )
			return NULL;

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
		for( CSearchState *state = searchHead, **prev = NULL;
			  state;
			  *prev = state, state = state->next )
		{
			if( state->handle == handle )
			{
				if( prev )
					(*prev)->next = state->next;
				else searchHead = state->next;

				delete state;

				return;
			}
		}

		Con_DPrintf( "FindClose: Can't find search state by handle %d\n", handle );
		return;
	}

	const char * GetLocalPath( const char *name, char *buf, int size ) override
	{
		if( !name ) return NULL;

		char *p = (char *)alloca( Q_strlen( name ) + 1 );
		CopyAndFixSlashes( p, name );

#if !XASH_WIN32
		if( p[0] == '/' )
#else
		if( Q_strchr( p, ':' ))
#endif
		{
			Q_strncpy( buf, p, size );

			return buf;
		}


		const char *fullpath = FS_GetDiskPath( p, false );
		if( !fullpath )
			return NULL;

		Q_strncpy( buf, fullpath, size );
		return buf;
	}

	char *ParseFile( char *buf, char *token, bool *quoted ) override
	{
		qboolean qquoted;

		char *p = COM_ParseFileSafe( buf, token, PFILE_FS_TOKEN_MAX_LENGTH, 0, NULL, &qquoted );
		if( quoted ) *quoted = qquoted;

		return p;
	}

	bool FullPathToRelativePath( const char *path, char *out ) override
	{
		if( !COM_CheckString( path ))
		{
			*out = 0;
			return false;
		}

		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );
		searchpath_t *sp;

		for( sp = fs_searchpaths; sp; sp = sp->next )
		{
			size_t splen = Q_strlen( sp->filename );

			if( !Q_strnicmp( sp->filename, p, splen ))
			{
				Q_strcpy( out, p + splen + 1 );
				return true;
			}
		}

		Q_strcpy( out, p );
		return false;
	}

	bool GetCurrentDirectory( char *p, int size ) override
	{
		Q_strncpy( p, fs_rootdir, size );

		return true;
	}

	void PrintOpenedFiles() override
	{
		// we don't track this yet
		return;
	}

	void SetWarningFunc(void (*)(const char *, ...)) override
	{
		// TODO:
		return;
	}

	void SetWarningLevel(FileWarningLevel_t) override
	{
		// TODO:
		return;
	}

	int SetVBuf( FileHandle_t handle, char *buf, int mode, long int size ) override
	{
		// TODO:
		return 0;
	}

	void GetInterfaceVersion(char *p, int size) override
	{
		Q_strncpy( p, "Stdio", size );
	}

	bool AddPackFile( const char *path, const char *id ) override
	{
		char *p = va( "%s/%s", IdToDir( id ), path );
		CopyAndFixSlashes( p, path );

		return !!FS_AddPak_Fullpath( p, NULL, FS_CUSTOM_PATH );
	}

	FileHandle_t OpenFromCacheForRead( const char *path , const char *mode, const char *id ) override
	{
		char *p = (char *)alloca( Q_strlen( path ) + 1 );
		CopyAndFixSlashes( p, path );

		return FS_OpenReadFile( p, mode, IsIdGamedir( id ));
	}

	// stubs
	void Mount() override {}
	void Unmount() override {}
	void GetLocalCopy(const char *) override {}
	void LogLevelLoadStarted(const char *) override {}
	void LogLevelLoadFinished(const char *) override {}
	void CancelWaitForResources(WaitForResourcesHandle_t) override {}
	int HintResourceNeed(const char *, int) override { return 0; }
	WaitForResourcesHandle_t WaitForResources(const char *) override { return 0; }
	int PauseResourcePreloading() override { return 0; }
	int ResumeResourcePreloading() override { return 0; }
	bool IsAppReadyForOfflinePlay(int) override { return true; }
	bool IsFileImmediatelyAvailable(const char *) override { return true; }
	bool GetWaitForResourcesProgress(WaitForResourcesHandle_t, float *pProgress, bool *pOverride) override
	{
		if( pProgress ) *pProgress = 0;
		if( pOverride ) *pOverride = true;
		return false;
	}
} g_VFileSystem009;

extern "C" void EXPORT *CreateInterface( const char *interface, int *retval )
{
	if( !Q_strcmp( interface, "VFileSystem009" ))
	{
		if( retval ) *retval = 0;
		return &g_VFileSystem009;
	}

	if( !Q_strcmp( interface, FS_API_CREATEINTERFACE_TAG ))
	{
		// return a copy, to disallow overriding
		static fs_api_t copy = { 0 };

		if( !copy.InitStdio )
			memcpy( &copy, &g_api, sizeof( copy ));

		if( retval ) *retval = 0;
		return &copy;
	}

	if( retval ) *retval = 1;
	return NULL;
}
