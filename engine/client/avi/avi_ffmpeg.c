/*
avi_ffmpreg.c - playing AVI files (ffmpeg backend)
Copyright (C) FTEQW developers (for plugins/avplug/avdecode.c)
Copyright (C) Sam Lantinga (for tests/testffmpeg.c)
Copyright (C) 2023-2024 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "defaults.h"
#include "common.h"
#include "client.h"

static qboolean avi_initialized;
static poolhandle_t avi_mempool;

#if XASH_AVI == AVI_FFMPEG
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

struct movie_state_s
{
	// ffmpeg contexts
	AVFormatContext *fmt_ctx;
	AVCodecContext *video_ctx;
	AVCodecContext *audio_ctx;
	struct SwsContext *sws_ctx;
	struct SwrContext *swr_ctx;

	AVPacket *pkt;
	AVFrame *aframe;
	AVFrame *vframe;
	AVFrame *vframe_copy;

	int64_t first_time;
	int64_t last_time;

	// video stream
	byte *dst;
	double duration;
	int video_stream;
	int xres;
	int yres;
	int dst_linesize;
	enum AVPixelFormat pix_fmt;

	// rendering video parameters
	int x, y, w, h; // passed to R_DrawStretchRaw
	int texture; // passed to R_UploadStretchRaw

	// audio stream
	int audio_stream;
	int channels;
	int rate;
	enum AVSampleFormat s_fmt;

	byte *cached_audio;
	size_t cached_audio_buf_len; // absolute size of cached_audio array
	size_t cached_audio_len; // how many data in bytes we have in cached_audio array
	size_t cached_audio_pos; // how far we've read into cached_audio array

	// rendering audio parameters
	float attn;
	int16_t entnum; // MAX_ENTITY_BITS is 13
	byte volume;
	byte active : 1;
	byte quiet  : 1;
	byte paused : 1;
};

qboolean AVI_SetParm( movie_state_t *Avi, enum movie_parms_e parm, ... )
{
	qboolean ret = true;
	va_list va;
	va_start( va, parm );

	while( parm != AVI_PARM_LAST )
	{
		float fval;
		int val;

		switch( parm )
		{
		case AVI_RENDER_TEXNUM:
			Avi->texture = va_arg( va, int );
			break;
		case AVI_RENDER_X:
			Avi->x = va_arg( va, int );
			break;
		case AVI_RENDER_Y:
			Avi->y = va_arg( va, int );
			break;
		case AVI_RENDER_W:
			Avi->w = va_arg( va, int );
			break;
		case AVI_RENDER_H:
			Avi->h = va_arg( va, int );
			break;
		case AVI_REWIND:
			if( Avi->audio_ctx )
				avcodec_flush_buffers( Avi->audio_ctx );
			avcodec_flush_buffers( Avi->video_ctx );
			Avi->cached_audio_len = Avi->cached_audio_pos = 0;
			Avi->last_time = -1;
			Avi->first_time = 0;
			av_seek_frame( Avi->fmt_ctx, -1, 0, AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD );
			break;
		case AVI_ENTNUM:
			val = va_arg( va, int );
			Avi->entnum = bound( 0, val, MAX_EDICTS );
			break;
		case AVI_VOLUME:
			val = va_arg( va, int );
			Avi->volume = bound( 0, val, 255 );
			break;
		case AVI_ATTN:
			fval = va_arg( va, double );
			Avi->attn = Q_max( 0.0f, fval );
			break;
		case AVI_PAUSE:
			Avi->paused = true;
			break;
		case AVI_RESUME:
			Avi->paused = false;
			break;
		default:
			ret = false;
		}

		parm = va_arg( va, enum movie_parms_e );
	}

	va_end( va );

	return ret;
}

static void AVI_SpewError( qboolean quiet, const char *fmt, ... ) FORMAT_CHECK( 2 );
static void AVI_SpewError( qboolean quiet, const char *fmt, ... )
{
	char buf[MAX_VA_STRING];
	va_list va;

	if( quiet )
		return;

	va_start( va, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	Con_Printf( S_ERROR "%s", buf );
}

static void AVI_SpewAvError( qboolean quiet, const char *func, int numerr )
{
	if( !quiet )
		Con_Printf( S_ERROR "%s: %s (%d)\n", func, av_err2str( numerr ), numerr );
}

static int AVI_OpenCodecContext( AVCodecContext **dst_dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type, qboolean quiet )
{
	const AVCodec *dec;
	AVCodecContext *dec_ctx;
	AVStream *st;
	int idx, ret;

	if(( ret = av_find_best_stream( fmt_ctx, type, -1, -1, NULL, 0 )) < 0 )
	{
		AVI_SpewAvError( quiet, "av_find_best_stream", ret );
		return ret;
	}

	idx = ret;
	st = fmt_ctx->streams[idx];

	if( !( dec = avcodec_find_decoder( st->codecpar->codec_id )))
	{
		AVI_SpewError( quiet, S_ERROR "Failed to find %s codec\n", av_get_media_type_string( type ));
		return AVERROR( EINVAL );
	}

	if( !( dec_ctx = avcodec_alloc_context3( dec )))
	{
		AVI_SpewError( quiet, S_ERROR "Failed to allocate %s codec context", dec->name );
		return AVERROR( ENOMEM );
	}

	if(( ret = avcodec_parameters_to_context( dec_ctx, st->codecpar )) < 0 )
	{
		AVI_SpewAvError( quiet, "avcodec_parameters_to_context", ret );
		avcodec_free_context( &dec_ctx );
		return ret;
	}

	dec_ctx->pkt_timebase = st->time_base;

	if(( ret = avcodec_open2( dec_ctx, dec, NULL )) < 0 )
	{
		AVI_SpewAvError( quiet, "avcodec_open2", ret );

		avcodec_free_context( &dec_ctx );
		return ret;
	}

	*dst_dec_ctx = dec_ctx;
	return idx; // always positive
}

int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time )
{
	return 0;
}

int AVI_TimeToSoundPosition( movie_state_t *Avi, int time )
{
	return 0;
}

qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration )
{
	if( !Avi->active )
		return false;

	if( xres )
		*xres = Avi->xres;

	if( yres )
		*yres = Avi->yres;

	if( duration )
		*duration = Avi->duration;

	return true;
}

qboolean AVI_HaveAudioTrack( const movie_state_t *Avi )
{
	return Avi ? Avi->active && Avi->audio_ctx : false;
}

// just let it compile, bruh!
byte *AVI_GetVideoFrame( movie_state_t *Avi, int target )
{
	return Avi->dst;
}

static void AVI_StreamAudio( movie_state_t *Avi )
{
	int buffer_samples, file_samples, file_bytes;
	rawchan_t *ch = NULL;

	// keep the same semantics, when S_RAW_SOUND_SOUNDTRACK doesn't play if S_StartStreaming wasn't enabled
	qboolean disable_stream = Avi->entnum == S_RAW_SOUND_SOUNDTRACK ? !s_listener.streaming : false;

	if( !dma.initialized || disable_stream || s_listener.paused || !Avi->cached_audio )
		return;

	ch = S_FindRawChannel( Avi->entnum, true );

	if( !ch )
		return;

	ch->master_vol = Avi->volume;
	ch->dist_mult = (Avi->attn / SND_CLIP_DISTANCE);

	if( ch->s_rawend < soundtime )
		ch->s_rawend = soundtime;

	while( ch->s_rawend < soundtime + ch->max_samples )
	{
		size_t copy;

		buffer_samples = ch->max_samples - (ch->s_rawend - soundtime);

		file_samples = buffer_samples * ((float)Avi->rate / SOUND_DMA_SPEED);
		if( file_samples <= 1 ) return; // no more samples need

		file_bytes = file_samples * av_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels;

		if( file_bytes > ch->max_samples )
		{
			file_bytes = ch->max_samples;
			file_samples = file_bytes / ( av_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels );
		}

		copy = Q_min( file_bytes, Q_max( Avi->cached_audio_len - Avi->cached_audio_pos, 0 ));

		if( !copy )
			break;

		if( file_bytes > copy )
		{
			file_bytes = copy;
			file_samples = file_bytes / ( av_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels );
		}

		ch->s_rawend = S_RawSamplesStereo( ch->rawsamples, ch->s_rawend, ch->max_samples, file_samples, Avi->rate, av_get_bytes_per_sample( Avi->s_fmt ), Avi->channels, Avi->cached_audio + Avi->cached_audio_pos );
		Avi->cached_audio_pos += copy;
	}
}

static void AVI_HandleAudio( movie_state_t *Avi, const AVFrame *frame )
{
	int samples = frame->nb_samples;
	size_t len = samples * av_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels;
	int outsamples;
	uint8_t *ptr;

	// allocate data
	if( !Avi->cached_audio )
	{
		Avi->cached_audio_buf_len = len;
		Avi->cached_audio_pos = 0;
		Avi->cached_audio_len = 0;
		Avi->cached_audio = Mem_Malloc( avi_mempool, len );
	}
	else
	{
		if( Avi->cached_audio_pos )
		{
			// Con_Printf( "%s: erasing old data of size %d\n", __func__, Avi->cached_audio_pos );
			Avi->cached_audio_len -= Avi->cached_audio_pos;
			memmove( Avi->cached_audio, Avi->cached_audio + Avi->cached_audio_pos, Avi->cached_audio_len );
			Avi->cached_audio_pos = 0;
		}

		if( len + Avi->cached_audio_len > Avi->cached_audio_buf_len )
		{
			// Con_Printf( "%s: resizing old buffer of size %d to size %d\n", __func__, Avi->cached_audio_buf_len, len + Avi->cached_audio_buf_len );
			Avi->cached_audio_buf_len = len + Avi->cached_audio_len;
			Avi->cached_audio = Mem_Realloc( avi_mempool, Avi->cached_audio, Avi->cached_audio_buf_len );
		}
	}

	ptr = Avi->cached_audio + Avi->cached_audio_len;
	outsamples = swr_convert( Avi->swr_ctx, &ptr, samples, (void *)frame->data, samples );
	Avi->cached_audio_len += outsamples * av_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels;

	// Con_Printf( "%s: got audio chunk of size %d samples\n", __func__, outsamples );
}

qboolean AVI_Think( movie_state_t *Avi )
{
	qboolean decoded = false;
	qboolean flushing = false;
	qboolean redraw = false;
	const double timebase = (double)Avi->video_ctx->pkt_timebase.den / Avi->video_ctx->pkt_timebase.num;
	int64_t curtime = round( Platform_DoubleTime() * timebase );

	if( !Avi->first_time ) // always remember at which timestamp we started playing
		Avi->first_time = curtime;

	if( Avi->paused )
	{
		// FIXME: there might be a better way to do this
		Avi->last_time = curtime;
		return true;
	}

	Con_NPrintf( 1, "cached_audio_buf_len = %zu", Avi->cached_audio_buf_len );

	while( 1 ) // try to get multiple decoded frames to keep up when we're running at low fps
	{
		int res;

		AVI_StreamAudio( Avi ); // always flush audio buffers

		// recalc time so we always play last possible frame
		curtime = round( Platform_DoubleTime() * timebase );

		if( Avi->last_time > curtime )
			break;

		if(( res = av_read_frame( Avi->fmt_ctx, Avi->pkt )) >= 0 )
		{
			if( Avi->pkt->stream_index == Avi->audio_stream )
			{
				res = avcodec_send_packet( Avi->audio_ctx, Avi->pkt );
				if( res < 0 )
					AVI_SpewAvError( Avi->quiet, "avcodec_send_packet (audio)", res );
			}
			else if( Avi->pkt->stream_index == Avi->video_stream )
			{
				res = avcodec_send_packet( Avi->video_ctx, Avi->pkt );
				if( res < 0 )
					AVI_SpewAvError( Avi->quiet, "avcodec_send_packet (video)", res );
			}
			av_packet_unref( Avi->pkt );
		}
		else
		{
			if( res != AVERROR_EOF )
				AVI_SpewAvError( Avi->quiet, "av_read_frame", res );

			if( Avi->audio_ctx )
				avcodec_flush_buffers( Avi->audio_ctx );

			avcodec_flush_buffers( Avi->video_ctx );
			flushing = true;
			break;
		}

		if( Avi->audio_ctx )
		{
			while( avcodec_receive_frame( Avi->audio_ctx, Avi->aframe ) == 0 )
			{
				AVI_HandleAudio( Avi, Avi->aframe );
				decoded = true;
			}
		}

		while( avcodec_receive_frame( Avi->video_ctx, Avi->vframe ) == 0 )
		{
			Avi->last_time = Avi->first_time + Avi->vframe->best_effort_timestamp;
			decoded = true;

			if( FBitSet( Avi->vframe->flags, AV_FRAME_FLAG_CORRUPT|AV_FRAME_FLAG_DISCARD ))
				continue;

			if( Avi->vframe->decode_error_flags != 0 )
				continue;

			av_frame_unref( Avi->vframe_copy );
			if( av_frame_ref( Avi->vframe_copy, Avi->vframe ) == 0 )
				redraw = true;
		}
	}

	if( redraw )
	{
		sws_scale( Avi->sws_ctx, (void*)Avi->vframe_copy->data, Avi->vframe_copy->linesize, 0, Avi->video_ctx->height,
			&Avi->dst, &Avi->dst_linesize );
		av_frame_unref( Avi->vframe_copy );
	}

	if( Avi->texture == 0 )
	{
		int w = Avi->w >= 0 ? Avi->w : refState.width;
		int h = Avi->h >= 0 ? Avi->h : refState.height;

		ref.dllFuncs.R_DrawStretchRaw( Avi->x, Avi->y, w, h, Avi->xres, Avi->yres, Avi->dst, redraw );
	}
	else if( redraw && Avi->texture > 0 )
		ref.dllFuncs.AVI_UploadRawFrame( Avi->texture, Avi->xres, Avi->yres, Avi->w, Avi->h, Avi->dst );

	if( flushing && !decoded )
		return false; // probably hit an EOF

	return true;
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	byte *dst[4];
	int dst_linesize[4];
	int ret;

	if( Avi->active )
		AVI_CloseVideo( Avi );

	if( !filename || !avi_initialized )
		return;

	Avi->active = false;
	Avi->quiet = quiet;
	Avi->video_ctx = Avi->audio_ctx = NULL;
	Avi->fmt_ctx = NULL;

	if(( ret = avformat_open_input( &Avi->fmt_ctx, filename, NULL, NULL )) < 0 )
	{
		AVI_SpewAvError( quiet, "avformat_open_input", ret );
		return;
	}

	if(( ret = avformat_find_stream_info( Avi->fmt_ctx, NULL )) < 0 )
	{
		AVI_SpewAvError( quiet, "avformat_find_stream_info", ret );
		return;
	}

	if( !( Avi->pkt = av_packet_alloc( )))
	{
		AVI_SpewAvError( quiet, "av_packet_alloc", 0 );
		return;
	}

	if( !( Avi->vframe = av_frame_alloc( )))
	{
		AVI_SpewAvError( quiet, "av_frame_alloc (video)", 0 );
		return;
	}

	if( !( Avi->vframe_copy = av_frame_alloc( )))
	{
		AVI_SpewAvError( quiet, "av_frame_alloc (video)", 0 );
		return;
	}


	Avi->video_stream = AVI_OpenCodecContext( &Avi->video_ctx, Avi->fmt_ctx, AVMEDIA_TYPE_VIDEO, quiet );

	if( Avi->video_stream < 0 )
		return;

	Avi->xres     = Avi->video_ctx->width;
	Avi->yres     = Avi->video_ctx->height;
	Avi->pix_fmt  = Avi->video_ctx->pix_fmt;
	Avi->duration = Avi->fmt_ctx->duration / (double)AV_TIME_BASE;
	Avi->entnum   = S_RAW_SOUND_SOUNDTRACK;
	Avi->attn     = ATTN_NONE;
	Avi->volume   = 255;

	if( !( Avi->sws_ctx = sws_getContext( Avi->xres, Avi->yres, Avi->pix_fmt,
		Avi->xres, Avi->yres, AV_PIX_FMT_BGR0, SWS_POINT, NULL, NULL, NULL )))
	{
		AVI_SpewAvError( quiet, "sws_getContext", 0 );
		return;
	}

	if(( ret = av_image_alloc( dst, dst_linesize, Avi->xres, Avi->yres, AV_PIX_FMT_BGR0, 1 )) < 0 )
	{
		AVI_SpewAvError( quiet, "av_image_alloc", ret );
		return;
	}

	Avi->dst = dst[0];
	Avi->dst_linesize = dst_linesize[0];

	if( load_audio )
	{
		if( !( Avi->aframe = av_frame_alloc( )))
		{
			AVI_SpewAvError( quiet, "av_frame_alloc (audio)", 0 );
			return;
		}

		Avi->audio_stream = AVI_OpenCodecContext( &Avi->audio_ctx, Avi->fmt_ctx, AVMEDIA_TYPE_AUDIO, quiet );

		// audio stream was requested but it wasn't found
		if( Avi->audio_stream < 0 )
			return;

		Avi->channels = Q_min( Avi->audio_ctx->ch_layout.nb_channels, 2 );
		if( Avi->audio_ctx->sample_fmt == AV_SAMPLE_FMT_U8 || Avi->audio_ctx->sample_fmt == AV_SAMPLE_FMT_U8P )
			Avi->s_fmt = AV_SAMPLE_FMT_U8;
		else Avi->s_fmt = AV_SAMPLE_FMT_S16;
		Avi->rate = Avi->audio_ctx->sample_rate;

		if(( ret = swr_alloc_set_opts2( &Avi->swr_ctx, &Avi->audio_ctx->ch_layout, Avi->s_fmt, Avi->rate,
			&Avi->audio_ctx->ch_layout, Avi->audio_ctx->sample_fmt, Avi->audio_ctx->sample_rate, 0, 0 )) < 0 )
		{
			AVI_SpewAvError( quiet, "swr_alloc_set_opts2", ret );
			return;
		}

		if(( ret = swr_init( Avi->swr_ctx )) < 0 )
		{
			AVI_SpewAvError( quiet, "swr_init", ret );
			return;
		}
	}

	Avi->active = true;
}

void AVI_CloseVideo( movie_state_t *Avi )
{
	if( Avi->active )
	{
		if( Avi->cached_audio )
			Mem_Free( Avi->cached_audio );

		swr_free( &Avi->swr_ctx );
		avcodec_free_context( &Avi->audio_ctx );
		av_frame_free( &Avi->aframe );

		av_free( Avi->dst );
		sws_freeContext( Avi->sws_ctx );
		avcodec_free_context( &Avi->video_ctx );
		av_frame_free( &Avi->vframe );
		av_frame_free( &Avi->vframe_copy );

		av_packet_free( &Avi->pkt );

		avformat_close_input( &Avi->fmt_ctx );
	}

	memset( Avi, 0, sizeof( *Avi ));
}

static void AVI_PrintFFmpegVersion( void )
{
	uint ver;

	// print version we're compiled with and which version we're running with
	ver = avutil_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBAVUTIL_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	ver = avformat_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBAVFORMAT_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	ver = avformat_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBAVCODEC_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	ver = swscale_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBSWSCALE_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	ver = swresample_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBSWRESAMPLE_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));
}
#else
struct movie_state_s
{
	qboolean active;
};

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

qboolean AVI_HaveAudioTrack( const movie_state_t *Avi )
{
	return false;
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	;
}

int AVI_TimeToSoundPosition( movie_state_t *Avi, int time )
{
	return 0;
}

void AVI_CloseVideo( movie_state_t *Avi )
{
	;
}

qboolean AVI_Think( movie_state_t *Avi )
{
	return false;
}

qboolean AVI_SetParm( movie_state_t *Avi, enum movie_parms_e parm, ... )
{
	return false;
}

static void AVI_PrintFFmpegVersion( void )
{

}
#endif // XASH_AVI == AVI_NULL

static movie_state_t avi[2];
movie_state_t *AVI_GetState( int num )
{
	return &avi[num];
}

qboolean AVI_IsActive( movie_state_t *Avi )
{
	return Avi ? Avi->active : false;
}

qboolean AVI_Initailize( void )
{
	if( XASH_AVI == AVI_NULL )
	{
		Con_Printf( "AVI: Not supported\n" );
		return false;
	}

	if( Sys_CheckParm( "-noavi" ))
	{
		Con_Printf( "AVI: Disabled\n" );
		return false;
	}

	AVI_PrintFFmpegVersion();

	avi_initialized = true;
	avi_mempool = Mem_AllocPool( "AVI Zone" );

	return false;
}

void AVI_Shutdown( void )
{
	Mem_FreePool( &avi_mempool );
	avi_initialized = XASH_AVI != AVI_NULL;
}

movie_state_t *AVI_LoadVideo( const char *filename, qboolean load_audio )
{
	movie_state_t	*Avi;
	string		path;
	const char	*fullpath;

	// fast reject
	if( !avi_initialized )
		return NULL;

	// open cinematic
	Q_snprintf( path, sizeof( path ), "media/%s", filename );
	COM_DefaultExtension( path, ".avi", sizeof( path ));
	fullpath = FS_GetDiskPath( path, false );

	if( FS_FileExists( path, false ) && !fullpath )
	{
		Con_Printf( "Couldn't load %s from packfile. Please extract it\n", path );
		return NULL;
	}

	Avi = Mem_Calloc( avi_mempool, sizeof( movie_state_t ));
	AVI_OpenVideo( Avi, fullpath, load_audio, false );

	if( !AVI_IsActive( Avi ))
	{
		AVI_FreeVideo( Avi ); // something bad happens
		return NULL;
	}

	// all done
	return Avi;
}

void AVI_FreeVideo( movie_state_t *Avi )
{
	if( !Avi )
		return;

	AVI_CloseVideo( Avi );

	if( Mem_IsAllocatedExt( avi_mempool, Avi ))
		Mem_Free( Avi );
}
