/*
 * snd_esp.c - publica os sons dinamicos num mmap compartilhado.
 *
 * O client dll (sound_esp.cpp) le o mesmo arquivo e desenha o sound ESP.
 * Escrita com seq impar/par para o leitor nunca pegar um frame pela metade.
 */

#include "common.h"
#include "client.h"
#include "snd_esp.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

static snd_esp_frame_t *s_frame;
static int      s_fd = -1;
static uint32_t s_seq;
static int      s_head;

static uint32_t SndEsp_Time( void )
{
	struct timeval tv;

	gettimeofday( &tv, NULL );
	return (uint32_t)( tv.tv_sec * 1000u + tv.tv_usec / 1000u );
}

static qboolean SndEsp_Open( void )
{
	if( s_frame )
		return true;

	s_fd = open( SND_ESP_PATH, O_RDWR | O_CREAT, 0600 );
	if( s_fd < 0 )
		return false;

	if( ftruncate( s_fd, sizeof( snd_esp_frame_t )) < 0 )
	{
		close( s_fd );
		s_fd = -1;
		return false;
	}

	s_frame = mmap( NULL, sizeof( snd_esp_frame_t ), PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0 );
	if( s_frame == MAP_FAILED )
	{
		s_frame = NULL;
		close( s_fd );
		s_fd = -1;
		return false;
	}

	memset( s_frame, 0, sizeof( *s_frame ));
	s_frame->magic = SND_ESP_MAGIC;
	return true;
}

void SndEsp_Push( const float *origin, int entnum, int channel )
{
	snd_esp_rec_t *r;
	uint32_t now;

	if( !origin )
		return;

	if( !SndEsp_Open())
		return;

	now = SndEsp_Time();

	s_frame->seq = ( ++s_seq ) | 1u;   /* impar: escrita em andamento */
	s_frame->time_ms = now;

	r = &s_frame->rec[s_head];
	r->origin[0] = origin[0];
	r->origin[1] = origin[1];
	r->origin[2] = origin[2];
	r->entnum = entnum;
	r->channel = channel;
	r->time_ms = now;

	s_head = ( s_head + 1 ) % SND_ESP_MAX;

	if( s_frame->count < SND_ESP_MAX )
		s_frame->count++;

	s_frame->seq = ++s_seq;            /* par: frame consistente */
}

void SndEsp_Shutdown( void )
{
	if( s_frame )
	{
		munmap( s_frame, sizeof( snd_esp_frame_t ));
		s_frame = NULL;
	}

	if( s_fd >= 0 )
	{
		close( s_fd );
		s_fd = -1;
	}
}
