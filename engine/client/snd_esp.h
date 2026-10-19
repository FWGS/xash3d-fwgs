#pragma once
#include <stdint.h>

/*
 * snd_esp.h - a engine publica cada som dinamico (passos, tiros, reload)
 * num arquivo mapeado em memoria. O client le o mesmo arquivo e desenha
 * os circulos do sound ESP.
 *
 * Arquivo proprio (nao mexe no /tmp/cs16_hitbox.bin do overlay externo).
 */

#define SND_ESP_MAGIC 0x444E5331u  /* '1SND' */
#define SND_ESP_MAX   64
#define SND_ESP_PATH  "/tmp/cs16_sound_esp.bin"

#pragma pack(push, 1)
typedef struct
{
	float    origin[3];
	int32_t  entnum;
	int32_t  channel;
	uint32_t time_ms;
} snd_esp_rec_t;

typedef struct
{
	uint32_t magic;
	uint32_t seq;      /* impar = escrita em andamento */
	uint32_t time_ms;
	int32_t  count;
	snd_esp_rec_t rec[SND_ESP_MAX];
} snd_esp_frame_t;
#pragma pack(pop)

void SndEsp_Push( const float *origin, int entnum, int channel );
void SndEsp_Shutdown( void );
