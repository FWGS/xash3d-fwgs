/*
avi.h -- common avi support header
Copyright (C) 2018 a1batross, Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef AVI_H
#define AVI_H

#include "movie_api.h"

//
// avikit.c
//
typedef struct movie_state_s movie_state_t;

qboolean AVI_Initailize( void );
void AVI_Shutdown( void );

movie_state_t *AVI_LoadVideo( const char *filename, uint flags );
void AVI_FreeVideo( movie_state_t **Avi );
qboolean AVI_IsActive( movie_state_t *Avi );

qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration );
qboolean AVI_GetAudioInfo( movie_state_t *Avi, wavdata_t *snd_info );

int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time );
int AVI_TimeToSoundPosition( movie_state_t *Avi, int time );

byte *AVI_GetVideoFrame( movie_state_t *Avi, int frame );
int AVI_GetAudioChunk( movie_state_t *Avi, char *audiodata, int offset, int length );


#endif // AVI_H
