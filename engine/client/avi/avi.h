#ifndef AVI_H
#define AVI_H

//
// avikit.c
//
typedef struct movie_state_s  movie_state_t;
int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time );
byte *AVI_GetVideoFrame( movie_state_t *Avi, int frame );
qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration );
qboolean AVI_GetAudioInfo( movie_state_t *Avi, wavdata_t *snd_info );
int AVI_GetAudioChunk( movie_state_t *Avi, char *audiodata, int offset, int length );
void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet );
movie_state_t *AVI_LoadVideo( const char *filename, qboolean load_audio );
int AVI_TimeToSoundPosition( movie_state_t *Avi, int time );
void AVI_CloseVideo( movie_state_t *Avi );
qboolean AVI_IsActive( movie_state_t *Avi );
void AVI_FreeVideo( movie_state_t *Avi );
movie_state_t *AVI_GetState( int num );
qboolean AVI_Initailize( void );
void AVI_Shutdown( void );

#endif // AVI_H
