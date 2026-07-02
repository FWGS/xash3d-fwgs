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
	file_t			*audio_file;
	byte			*frame;
	byte			*upload_frame;
	byte			*chunk;
	byte			*audio_chunk;
	size_t			chunk_capacity;
	avi_frame_index_t	*index;
	avi_frame_index_t	*audio_index;
	uint32_t		*raw_frame_offsets;
	cinepak_decoder_t	decoder;
	fs_offset_t		movie_list_pos;
	fs_offset_t		data_offset;
	size_t			frame_size;
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
	qboolean		audio_channel_ready;
	qboolean		audio_playback_started;
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
	qboolean		raw_video;
	qboolean		raw_rgb565;
	qboolean		raw_delta_tiles;
	qboolean		raw_static_frame;
	qboolean		active;
	qboolean		paused;
	qboolean		playback_started;
	qboolean		frame_on_gpu;
	qboolean		ui_logo;
	uint			debug_think_calls;
};

#endif // AVI_GC_H
