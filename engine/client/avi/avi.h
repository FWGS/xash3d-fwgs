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

//
// avikit.c
//
typedef struct movie_state_s movie_state_t;
int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time );
byte *AVI_GetVideoFrame( movie_state_t *Avi, int frame );
qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration );
qboolean AVI_HaveAudioTrack( const movie_state_t *Avi );
void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet );
movie_state_t *AVI_LoadVideo( const char *filename, qboolean load_audio );
int AVI_TimeToSoundPosition( movie_state_t *Avi, int time );
void AVI_CloseVideo( movie_state_t *Avi );
qboolean AVI_IsActive( movie_state_t *Avi );
void AVI_FreeVideo( movie_state_t *Avi );
movie_state_t *AVI_GetState( int num );
qboolean AVI_Initailize( void );
void AVI_Shutdown( void );

qboolean AVI_SetParm( movie_state_t *Avi, enum movie_parms_e parm, ... );
qboolean AVI_Think( movie_state_t *Avi );

#endif // AVI_H
