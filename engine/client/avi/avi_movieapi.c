/*
avi_stub.c - playing AVI files (stub)
Copyright (C) 2018 a1batross

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
#include "common.h"
#include "library.h"

#define MAX_MOVIE_DLLS 3 // two implementations are planned + one user-defined

typedef struct movie_state_s
{
	movie_t *handle;
	uint dll_num;
} movie_state_t;

static struct
{
	uint num_dlls_loaded;

	uint num_dlls_available;
	const char *names[MAX_MOVIE_DLLS];

	struct
	{
		HINSTANCE hInstance;
		movie_interface_t funcs;
	} dll[MAX_MOVIE_DLLS];
} avi;

#define MovieFuncs( s ) avi.dll[(s)->dll_num].funcs

static qboolean AVI_ValidateState( movie_state_t *state )
{
	if( !state || !state->handle || state->dll_num >= avi.num_dlls_loaded)
		return false;

	return true;
}

int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time )
{
	if( !AVI_ValidateState( Avi ))
		return 0;

	return MovieFuncs( Avi ).GetVideoFrameNumber( Avi->handle, time );
}

byte *AVI_GetVideoFrame( movie_state_t *Avi, int frame )
{
	if( !AVI_ValidateState( Avi ))
		return NULL;

	return MovieFuncs( Avi ).GetVideoFrame( Avi->handle, frame );
}

qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration )
{
	if( !AVI_ValidateState( Avi ))
		return false;

	return MovieFuncs( Avi ).GetVideoInfo( Avi->handle, xres, yres, duration );
}

qboolean AVI_GetAudioInfo( movie_state_t *Avi, wavdata_t *snd_info )
{
	int rate, channels, width;

	if( !AVI_ValidateState( Avi ) || !snd_info )
		return false;

	if( !MovieFuncs( Avi ).GetAudioInfo( Avi->handle, &rate, &channels, &width ))
		return false;

	snd_info->rate = rate;
	snd_info->channels = channels;
	snd_info->width = width;
	snd_info->size = rate * channels * width;
	snd_info->loopStart = 0; // using loopStart as streampos

	return true;
}

int AVI_GetAudioChunk( movie_state_t *Avi, char *audiodata, int offset, int length )
{
	if( !AVI_ValidateState( Avi ))
		return 0;

	return MovieFuncs( Avi ).GetAudioChunk( Avi->handle, audiodata, offset, length );
}

movie_state_t *AVI_LoadVideo( const char *filename, uint flags )
{
	movie_state_t *state = NULL;

	for( int i = 0; i < avi.num_dlls_loaded; i++ )
	{
		movie_t *ptr = avi.dll[i].funcs.LoadVideo( filename, flags );

		if( ptr )
		{
			state = Mem_Malloc( host.mempool, sizeof( *state ));

			state->handle = ptr;
			state->dll_num = i;
			break;
		}
	}

	return state;
}

int AVI_TimeToSoundPosition( movie_state_t *Avi, int time )
{
	if( !AVI_ValidateState( Avi ))
		return 0;

	return MovieFuncs( Avi ).TimeToSoundPosition( Avi->handle, time );
}

qboolean AVI_IsActive( movie_state_t *Avi )
{
	if( !AVI_ValidateState( Avi ))
		return false;

	return MovieFuncs( Avi ).IsActive( Avi->handle );
}

void AVI_FreeVideo( movie_state_t **Avi )
{
	if( Avi || *Avi )
		return;

	if( AVI_ValidateState( *Avi ))
		MovieFuncs( *Avi ).FreeVideo(( *Avi )->handle );

	Mem_Free( *Avi );
	*Avi = NULL;
}

static void AVI_UnloadProgs( void )
{
	for( int i = 0; i < avi.num_dlls_loaded; i++ )
	{
		avi.dll[i].funcs.Shutdown();
		memset( &avi.dll[i].funcs, 0, sizeof( avi.dll[i].funcs ));

		COM_FreeLibrary( avi.dll[i].hInstance );
		avi.dll[i].hInstance = 0;
	}

	avi.num_dlls_loaded = 0;
}

static void AVI_CollectDllsNames( void )
{
	static string custom_dll;

	avi.num_dlls_available = 0;

	// ordering is important, this way we can try to load WebM using new decoder
	// and fallback to AVIKit if it can't open it
	if( Sys_GetParmFromCmdLine( "-avi", custom_dll ))
		avi.names[avi.num_dlls_available++] = custom_dll;

#if XASH_AVI_WEBM_ENABLED
	avi.names[avi.num_dlls_available++] = "webm";
#endif

#if XASH_AVI_WIN32_ENABLED
	avi.names[avi.num_dlls_available++] = "win32";
#endif
}

static movie_api_t gEngfuncs =
{
	Con_Printf,
	Con_DPrintf,
	Con_Reportf,
	Sys_Error,
	_Mem_AllocPool,
	_Mem_FreePool,
	_Mem_Alloc,
	_Mem_Realloc,
	_Mem_Free,
	Sys_GetNativeObject,
	Sys_LoadLibrary,
	Sys_GetProcAddress,
	Sys_FreeLibrary,
	&g_fsapi,
};

static void AVI_LoadProgs( void )
{
	avi.num_dlls_loaded = 0;

	for( int i = 0; i < avi.num_dlls_available; i++ )
	{
		string name;
		MOVIEAPI GetMovieAPI;
		HINSTANCE hInstance;
		movie_interface_t dllFuncs = { 0 };

		Q_snprintf( name, sizeof( name ), "avi_%s.%s", avi.names[i], OS_LIB_EXT );

		FS_AllowDirectPaths( true );
		hInstance = COM_LoadLibrary( name, false, true );
		if( !hInstance )
		{
			FS_AllowDirectPaths( false );
			Con_Reportf( "%s: can't load movie library %s: %s\n", __func__, name, COM_GetLibraryError( ));
			continue;
		}
		FS_AllowDirectPaths( false );

		if( !( GetMovieAPI = (MOVIEAPI)COM_GetProcAddress( hInstance, GET_MOVIE_API )))
		{
			COM_FreeLibrary( hInstance );
			Con_Reportf( "%s: can't find %s entry point in %s\n", __func__, GET_MOVIE_API, name );
			continue;
		}

		if( GetMovieAPI( MOVIE_API_VERSION, &gEngfuncs, &dllFuncs ) != MOVIE_API_VERSION )
		{
			COM_FreeLibrary( hInstance );
			Con_Reportf( "%s: can't init movie API in %s: wrong version\n", __func__, name );
			continue;
		}

		if( !dllFuncs.Initialize( ))
		{
			COM_FreeLibrary( hInstance );
			Con_Reportf( "%s: can't init movie API in %s!\n", __func__, name );
			continue;
		}

		avi.dll[avi.num_dlls_loaded].hInstance = hInstance;
		memcpy( &avi.dll[avi.num_dlls_loaded].funcs, &dllFuncs, sizeof( dllFuncs ));
		avi.num_dlls_loaded++;
	}
}

qboolean AVI_Initailize( void )
{
	if( Sys_CheckParm( "-noavi" ))
	{
		Con_Printf( "AVI: Disabled\n" );
		return false;
	}

	AVI_CollectDllsNames();
	AVI_LoadProgs();
	return avi.num_dlls_loaded != 0;
}

void AVI_Shutdown( void )
{
	AVI_UnloadProgs();
	memset( &avi, 0, sizeof( avi ));
}
