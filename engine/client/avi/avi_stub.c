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

#ifndef _WIN32
#include "common.h"

long AVI_GetVideoFrameNumber( movie_state_t *Avi, float time )
{
	return 0;
}

byte *AVI_GetVideoFrame( movie_state_t *Avi, long frame )
{
	return NULL;
}

qboolean AVI_GetVideoInfo( movie_state_t *Avi, long *xres, long *yres, float *duration )
{
	return false;
}

qboolean AVI_GetAudioInfo( movie_state_t *Avi, wavdata_t *snd_info )
{
	return false;
}

long AVI_GetAudioChunk( movie_state_t *Avi, char *audiodata, long offset, long length )
{
	return 0;
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	;
}

movie_state_t *AVI_LoadVideo( const char *filename, qboolean load_audio )
{
	return NULL;
}

long AVI_TimeToSoundPosition( movie_state_t *Avi, long time )
{
	return 0;
}

long AVI_GetVideoFrameCount( movie_state_t *Avi )
{
	return 0;
}

void AVI_CloseVideo( movie_state_t *Avi )
{
	;
}

qboolean AVI_IsActive( movie_state_t *Avi )
{
	return false;
}

void AVI_FreeVideo( movie_state_t *Avi )
{
	;
}

movie_state_t *AVI_GetState( int num )
{
	return NULL;
}

qboolean AVI_Initailize( void )
{
	return false;
}

void AVI_Shutdown( void )
{
	;
}

#endif // WIN32
