/*
avi_cinepak.h - lightweight Cinepak decoder for retail HL1 AVIs
Ported from FFmpeg libavcodec/cinepak.c (LGPL 2.1+)
Copyright (C) 2003 The FFmpeg project
Copyright (C) 2026 Xash3D GameCube port contributors
*/
#ifndef AVI_CINEPAK_H
#define AVI_CINEPAK_H

#include "common.h"

typedef struct cinepak_decoder_s
{
	byte	*rgb;
	int	width;
	int	height;
	int	stride;
	int	palette_video;
	int	sega_film_skip_bytes;
} cinepak_decoder_t;

qboolean Cinepak_Init( cinepak_decoder_t *dec, int width, int height, poolhandle_t mempool );
void Cinepak_Free( cinepak_decoder_t *dec );
qboolean Cinepak_Decode( cinepak_decoder_t *dec, const byte *data, size_t size );

#endif // AVI_CINEPAK_H
