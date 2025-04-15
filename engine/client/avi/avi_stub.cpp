#include "build.h"
#if !XASH_WIN32
#include "common.h"

int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time )
{
	return 0;
}

byte *AVI_GetVideoFrame( movie_state_t *Avi, int frame )
{
	return NULL;
}

qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration )
{
	return false;
}

qboolean AVI_GetAudioInfo( movie_state_t *Avi, wavdata_t *snd_info )
{
	return false;
}

int AVI_GetAudioChunk( movie_state_t *Avi, char *audiodata, int offset, int length )
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

int AVI_TimeToSoundPosition( movie_state_t *Avi, int time )
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
