/*
avi_gc.h - GameCube retail AVI/Cinepak movie state
Copyright (C) 2026 Xash3D GameCube port contributors
*/
#ifndef AVI_GC_H
#define AVI_GC_H

#include "avi_cinepak.h"

typedef struct avi_frame_index_s
{
	fs_offset_t	offset;
	uint		size;
} avi_frame_index_t;

struct movie_state_s
{
	file_t			*file;
	byte			*frame;
	byte			*upload_frame;
	byte			*chunk;
	byte			*audio_chunk;
	size_t			chunk_capacity;
	avi_frame_index_t	*index;
	avi_frame_index_t	*audio_index;
	cinepak_decoder_t	decoder;
	fs_offset_t		movie_list_pos;
	uint			frame_count;
	uint			audio_chunk_count;
	uint			audio_current_chunk;
	uint			audio_rate;
	uint			audio_width;
	uint			audio_channels;
	uint			audio_bytes_submitted;
	uint			audio_chunk_size;
	uint			audio_chunk_offset;
	qboolean		audio_reported;
	uint			fps_num;
	uint			fps_den;
	uint			current_frame;
	double			start_time;
	int			width;
	int			height;
	int			decode_scale;
	int			upload_width;
	int			upload_height;
	int			x, y, w, h;
	int			texture;
	qboolean		active;
	qboolean		paused;
	qboolean		playback_started;
	qboolean		frame_on_gpu;
};

#endif // AVI_GC_H
