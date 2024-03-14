/*
movie_api.h - standalone video player API
Copyright (C) 2007 Uncle Mike
Copyright (C) 2024 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef MOVIE_API_H
#define MOVIE_API_H

#include "xash3d_types.h"
#include "filesystem.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#define MOVIE_API_VERSION 1 // not stable yet!

enum
{
	MOVIE_LOAD_AUDIO = BIT( 0 ),
	MOVIE_LOAD_QUIET = BIT( 1 ),
};

typedef struct movie_s movie_t;

typedef struct movie_interface_t
{
	qboolean (*Initialize)( void );
	void (*Shutdown)( void );

	movie_t *(*LoadVideo)( const char *filename, uint flags );
	void (*FreeVideo)( movie_t *movie );
	qboolean (*IsActive)( movie_t *movie );

	qboolean (*GetVideoInfo)( movie_t *movie, int *xres, int *yres, float *duration );
	qboolean (*GetAudioInfo)( movie_t *movie, int *rate, int *channels, int *width );

	int (*GetVideoFrameNumber)( movie_t *movie, float time );
	int (*TimeToSoundPosition)( movie_t *movie, int time );

	byte *(*GetVideoFrame)( movie_t *movie, int frame );
	int (*GetAudioChunk)( movie_t *movie, char *audiodata, int offset, int length );
} movie_interface_t;

typedef struct movie_api_t
{
	// logging
	void    (*_Con_Printf)( const char *fmt, ... ) _format( 1 ); // typical console allowed messages
	void    (*_Con_DPrintf)( const char *fmt, ... ) _format( 1 ); // -dev 1
	void    (*_Con_Reportf)( const char *fmt, ... ) _format( 1 ); // -dev 2
	void    (*_Sys_Error)( const char *fmt, ... ) _format( 1 );

	// memory
	poolhandle_t (*_Mem_AllocPool)( const char *name, const char *filename, int fileline );
	void  (*_Mem_FreePool)( poolhandle_t *poolptr, const char *filename, int fileline );
	void *(*_Mem_Alloc)( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline ) ALLOC_CHECK( 2 );
	void *(*_Mem_Realloc)( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline ) ALLOC_CHECK( 3 );
	void  (*_Mem_Free)( void *data, const char *filename, int fileline );

	// platform
	void *(*Sys_GetNativeObject)( const char *object );

	// library loading
	qboolean (*Sys_LoadLibrary)( dll_info_t *dll );
	void *(*Sys_GetProcAddress)( dll_info_t *dll, const char* name );
	qboolean (*Sys_FreeLibrary)( dll_info_t *dll );

	// filesystem
	fs_api_t *fs;
} movie_api_t;

typedef int (*MOVIEAPI)( int version, movie_api_t *api, movie_interface_t *interface );
#define GET_MOVIE_API "GetMovieAPI"

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // MOVIE_API_H
