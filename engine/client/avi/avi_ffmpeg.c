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
#include "avi_ffmpeg.h"

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
				pavcodec_flush_buffers( Avi->audio_ctx );
			pavcodec_flush_buffers( Avi->video_ctx );
			Avi->cached_audio_len = Avi->cached_audio_pos = 0;
			Avi->last_time = -1;
			Avi->first_time = 0;
			pav_seek_frame( Avi->fmt_ctx, -1, 0, AVSEEK_FLAG_FRAME | AVSEEK_FLAG_BACKWARD );
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
	{
		char errstr[AV_ERROR_MAX_STRING_SIZE];
		pav_strerror( numerr, errstr, sizeof( errstr ));
		Con_Printf( S_ERROR "%s: %s (%d)\n", func, errstr, numerr );
	}
}

static int AVI_OpenCodecContext( AVCodecContext **dst_dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type, qboolean quiet )
{
	const AVCodec *dec;
	AVCodecContext *dec_ctx;
	AVStream *st;
	int idx, ret;

	if(( ret = pav_find_best_stream( fmt_ctx, type, -1, -1, NULL, 0 )) < 0 )
	{
		AVI_SpewAvError( quiet, "av_find_best_stream", ret );
		return ret;
	}

	idx = ret;
	st = fmt_ctx->streams[idx];

	if( !( dec = pavcodec_find_decoder( st->codecpar->codec_id )))
	{
		AVI_SpewError( quiet, S_ERROR "Failed to find %s codec\n", pav_get_media_type_string( type ));
		return AVERROR( EINVAL );
	}

	if( !( dec_ctx = pavcodec_alloc_context3( dec )))
	{
		AVI_SpewError( quiet, S_ERROR "Failed to allocate %s codec context", dec->name );
		return AVERROR( ENOMEM );
	}
	if(( ret = pavcodec_parameters_to_context( dec_ctx, st->codecpar )) < 0 )
	{
		AVI_SpewAvError( quiet, "avcodec_parameters_to_context", ret );
		pavcodec_free_context( &dec_ctx );
		return ret;
	}

	dec_ctx->pkt_timebase = st->time_base;

	if(( ret = pavcodec_open2( dec_ctx, dec, NULL )) < 0 )
	{
		AVI_SpewAvError( quiet, "avcodec_open2", ret );
		pavcodec_free_context( &dec_ctx );
		return ret;
	}

	*dst_dec_ctx = dec_ctx;
	return idx; // always positive
}

int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time )
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
	rawchan_t *ch;

	// keep the same semantics, when S_RAW_SOUND_SOUNDTRACK doesn't play if S_StartStreaming wasn't enabled
	qboolean disable_stream = Avi->entnum == S_RAW_SOUND_SOUNDTRACK ? !snd.streaming : false;

	if( !snd.initialized || disable_stream || cl.paused || !Avi->cached_audio )
		return;

	ch = S_FindRawChannel( Avi->entnum, true );

	if( !ch )
		return;

	ch->master_vol = Avi->volume;
	ch->dist_mult = (Avi->attn / SND_CLIP_DISTANCE);

	if( ch->s_rawend < snd.soundtime )
		ch->s_rawend = snd.soundtime;

	while( ch->s_rawend < snd.soundtime + ch->max_samples )
	{
		int buffer_samples = ch->max_samples - (ch->s_rawend - snd.soundtime);
		int file_samples = buffer_samples * ((float)Avi->rate / SOUND_DMA_SPEED);
		int file_bytes;
		size_t copy;

		if( file_samples <= 1 ) return; // no more samples need

		file_bytes = file_samples * pav_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels;

		if( file_bytes > ch->max_samples )
		{
			file_bytes = ch->max_samples;
			file_samples = file_bytes / ( pav_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels );
		}

		copy = Q_min( file_bytes, Q_max( Avi->cached_audio_len - Avi->cached_audio_pos, 0 ));

		if( !copy )
			break;

		if( file_bytes > copy )
		{
			file_bytes = copy;
			file_samples = file_bytes / ( pav_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels );
		}

		ch->s_rawend = S_RawSamplesStereo( ch->rawsamples, ch->s_rawend, ch->max_samples, file_samples, Avi->rate, pav_get_bytes_per_sample( Avi->s_fmt ), Avi->channels, Avi->cached_audio + Avi->cached_audio_pos );
		Avi->cached_audio_pos += copy;
	}
}

static void AVI_HandleAudio( movie_state_t *Avi, const AVFrame *frame )
{
	int samples = frame->nb_samples;
	size_t len = samples * pav_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels;

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

	uint8_t *ptr = Avi->cached_audio + Avi->cached_audio_len;
	int outsamples = pswr_convert( Avi->swr_ctx, &ptr, samples, (void *)frame->data, samples );
	Avi->cached_audio_len += outsamples * pav_get_bytes_per_sample( Avi->s_fmt ) * Avi->channels;

	// Con_Printf( "%s: got audio chunk of size %d samples\n", __func__, outsamples );
}

qboolean AVI_Think( movie_state_t *Avi )
{
	qboolean decoded = false;
	qboolean flushing = false;
	qboolean redraw = false;

	if( !Avi->video_ctx )
		return false;

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

	// Con_NPrintf( 1, "cached_audio_buf_len = %zu", Avi->cached_audio_buf_len );

	while( 1 ) // try to get multiple decoded frames to keep up when we're running at low fps
	{
		int res;

		AVI_StreamAudio( Avi ); // always flush audio buffers

		// recalc time so we always play last possible frame
		curtime = round( Platform_DoubleTime() * timebase );

		if( Avi->last_time > curtime )
			break;

		if(( res = pav_read_frame( Avi->fmt_ctx, Avi->pkt )) >= 0 )
		{
			if( Avi->pkt->stream_index == Avi->audio_stream && Avi->audio_ctx )
			{
				res = pavcodec_send_packet( Avi->audio_ctx, Avi->pkt );
				if( res < 0 )
					AVI_SpewAvError( Avi->quiet, "avcodec_send_packet (audio)", res );
			}
			else if( Avi->pkt->stream_index == Avi->video_stream )
			{
				res = pavcodec_send_packet( Avi->video_ctx, Avi->pkt );
				if( res < 0 )
					AVI_SpewAvError( Avi->quiet, "avcodec_send_packet (video)", res );
			}
			pav_packet_unref( Avi->pkt );
		}
		else
		{
			if( res != AVERROR_EOF )
				AVI_SpewAvError( Avi->quiet, "av_read_frame", res );

			if( Avi->audio_ctx )
				pavcodec_flush_buffers( Avi->audio_ctx );

			pavcodec_flush_buffers( Avi->video_ctx );
			flushing = true;
			break;
		}

		if( Avi->audio_ctx )
		{
			while( pavcodec_receive_frame( Avi->audio_ctx, Avi->aframe ) == 0 )
			{
				AVI_HandleAudio( Avi, Avi->aframe );
				decoded = true;
			}
		}

		while( pavcodec_receive_frame( Avi->video_ctx, Avi->vframe ) == 0 )
		{
			Avi->last_time = Avi->first_time + Avi->vframe->best_effort_timestamp;
			decoded = true;

			if( FBitSet( Avi->vframe->flags, AV_FRAME_FLAG_CORRUPT|AV_FRAME_FLAG_DISCARD ))
				continue;

			if( Avi->vframe->decode_error_flags != 0 )
				continue;

			pav_frame_unref( Avi->vframe_copy );
			if( pav_frame_ref( Avi->vframe_copy, Avi->vframe ) == 0 )
				redraw = true;
		}
	}

	if( redraw )
	{
		psws_scale( Avi->sws_ctx, (void*)Avi->vframe_copy->data, Avi->vframe_copy->linesize, 0, Avi->video_ctx->height,
			&Avi->dst, &Avi->dst_linesize );
		pav_frame_unref( Avi->vframe_copy );
	}

	if( Avi->texture == 0 )
	{
		int cinTexture = SCR_GetCinematicTexture();
		int w = Avi->w >= 0 ? Avi->w : refState.width;
		int h = Avi->h >= 0 ? Avi->h : refState.height;

		if( redraw )
			ref.dllFuncs.GL_UpdateTexture( cinTexture, Avi->xres, Avi->yres, Avi->xres, Avi->yres, Avi->dst, PF_BGRA_32 );
		ref.dllFuncs.R_DrawStretchPic( Avi->x, Avi->y, w, h, 0, 0, 1, 1, cinTexture );
	}
	else if( redraw && Avi->texture > 0 )
		ref.dllFuncs.GL_UpdateTexture( Avi->texture, Avi->xres, Avi->yres, Avi->w, Avi->h, Avi->dst, PF_BGRA_32 );

	if( flushing && !decoded )
		return false; // probably hit an EOF

	return true;
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	int ret;

	if( Avi->active )
		AVI_CloseVideo( Avi );

	if( !filename || !avi_initialized )
		return;

	Avi->active = false;
	Avi->quiet = quiet;
	Avi->video_ctx = Avi->audio_ctx = NULL;
	Avi->fmt_ctx = NULL;

	if(( ret = pavformat_open_input( &Avi->fmt_ctx, filename, NULL, NULL )) < 0 )
	{
		AVI_SpewAvError( quiet, "avformat_open_input", ret );
		return;
	}

	if(( ret = pavformat_find_stream_info( Avi->fmt_ctx, NULL )) < 0 )
	{
		AVI_SpewAvError( quiet, "avformat_find_stream_info", ret );
		return;
	}

	if( !( Avi->pkt = pav_packet_alloc( )))
	{
		AVI_SpewAvError( quiet, "av_packet_alloc", 0 );
		return;
	}

	if( !( Avi->vframe = pav_frame_alloc( )))
	{
		AVI_SpewAvError( quiet, "av_frame_alloc (video)", 0 );
		return;
	}

	if( !( Avi->vframe_copy = pav_frame_alloc( )))
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

	if( !( Avi->sws_ctx = psws_getContext( Avi->xres, Avi->yres, Avi->pix_fmt,
		Avi->xres, Avi->yres, AV_PIX_FMT_BGR0, SWS_POINT, NULL, NULL, NULL )))
	{
		AVI_SpewAvError( quiet, "sws_getContext", 0 );
		return;
	}

	byte *dst[4];
	int dst_linesize[4];
	if(( ret = pav_image_alloc( dst, dst_linesize, Avi->xres, Avi->yres, AV_PIX_FMT_BGR0, 1 )) < 0 )
	{
		AVI_SpewAvError( quiet, "av_image_alloc", ret );
		return;
	}

	Avi->dst = dst[0];
	Avi->dst_linesize = dst_linesize[0];

	if( load_audio )
	{
		if( !( Avi->aframe = pav_frame_alloc( )))
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

		if(( ret = pswr_alloc_set_opts2( &Avi->swr_ctx, &Avi->audio_ctx->ch_layout, Avi->s_fmt, Avi->rate,
			&Avi->audio_ctx->ch_layout, Avi->audio_ctx->sample_fmt, Avi->audio_ctx->sample_rate, 0, 0 )) < 0 )
		{
			AVI_SpewAvError( quiet, "swr_alloc_set_opts2", ret );
			return;
		}

		if(( ret = pswr_init( Avi->swr_ctx )) < 0 )
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

		pswr_free( &Avi->swr_ctx );
		pavcodec_free_context( &Avi->audio_ctx );
		pav_frame_free( &Avi->aframe );

		pav_free( Avi->dst );
		psws_freeContext( Avi->sws_ctx );
		pavcodec_free_context( &Avi->video_ctx );
		pav_frame_free( &Avi->vframe );
		pav_frame_free( &Avi->vframe_copy );

		pav_packet_free( &Avi->pkt );

		pavformat_close_input( &Avi->fmt_ctx );
	}

	memset( Avi, 0, sizeof( *Avi ));
}

#if XASH_FFMPEG_DLOPEN
#define F( x ) #x, (void **)&p##x
static const dllfunc_t libavutil_funcs[] =
{
	{ F( avutil_version ) },
	{ F( av_frame_alloc ) },
	{ F( av_frame_free ) },
	{ F( av_frame_ref ) },
	{ F( av_frame_unref ) },
	{ F( av_strerror ) },
	{ F( av_free ) },
	{ F( av_get_bytes_per_sample ) },
	{ F( av_get_media_type_string ) },
	{ F( av_image_alloc ) },
};

static const dllfunc_t libavformat_funcs[] =
{
	{ F( avformat_version ) },
	{ F( av_find_best_stream ) },
	{ F( av_read_frame ) },
	{ F( av_seek_frame ) },
	{ F( avformat_close_input ) },
	{ F( avformat_find_stream_info ) },
	{ F( avformat_open_input ) },
};

static const dllfunc_t libavcodec_funcs[] =
{
	{ F( avcodec_version ) },
	{ F( av_packet_alloc ) },
	{ F( av_packet_free ) },
	{ F( av_packet_unref ) },
	{ F( avcodec_alloc_context3 ) },
	{ F( avcodec_find_decoder ) },
	{ F( avcodec_flush_buffers ) },
	{ F( avcodec_free_context ) },
	{ F( avcodec_open2 ) },
	{ F( avcodec_parameters_to_context ) },
	{ F( avcodec_receive_frame ) },
	{ F( avcodec_send_packet ) },
};

static const dllfunc_t libswresample_funcs[] =
{
	{ F( swresample_version ) },
	{ F( swr_alloc_set_opts2 ) },
	{ F( swr_convert ) },
	{ F( swr_free ) },
	{ F( swr_init ) },
};

static const dllfunc_t libswscale_funcs[] =
{
	{ F( swscale_version ) },
	{ F( sws_freeContext ) },
	{ F( sws_getContext ) },
	{ F( sws_scale ) },
};
#undef F

#define SS( x ) #x
#define S( x ) SS( x )

static dll_info_t libavutil_info =
{
#if XASH_WIN32
	.name = "avutil-" S( SUPPORTED_AVU_VERSION_MAJOR ) ".dll",
#else
	.name = "libavutil.so." S( SUPPORTED_AVU_VERSION_MAJOR ),
#endif
	.fcts = libavutil_funcs,
	.num_fcts = ARRAYSIZE( libavutil_funcs ),
};

static dll_info_t libavformat_info =
{
#if XASH_WIN32
	.name = "avformat-" S( SUPPORTED_AVF_VERSION_MAJOR ) ".dll",
#else
	.name = "libavformat.so." S( SUPPORTED_AVF_VERSION_MAJOR ),
#endif
	.fcts = libavformat_funcs,
	.num_fcts = ARRAYSIZE( libavformat_funcs ),
};

static dll_info_t libavcodec_info =
{
#if XASH_WIN32
	.name = "avcodec-" S( SUPPORTED_AVC_VERSION_MAJOR ) ".dll",
#else
	.name = "libavcodec.so." S( SUPPORTED_AVC_VERSION_MAJOR ),
#endif
	.fcts = libavcodec_funcs,
	.num_fcts = ARRAYSIZE( libavcodec_funcs ),
};

static dll_info_t libswresample_info =
{
#if XASH_WIN32
	.name = "swresample-" S( SUPPORTED_SWR_VERSION_MAJOR ) ".dll",
#else
	.name = "libswresample.so." S( SUPPORTED_SWR_VERSION_MAJOR ),
#endif
	.fcts = libswresample_funcs,
	.num_fcts = ARRAYSIZE( libswresample_funcs ),
};

static dll_info_t libswscale_info =
{
#if XASH_WIN32
	.name = "swscale-" S( SUPPORTED_SWS_VERSION_MAJOR ) ".dll",
#else
	.name = "libswscale.so." S( SUPPORTED_SWS_VERSION_MAJOR ),
#endif
	.fcts = libswscale_funcs,
	.num_fcts = ARRAYSIZE( libswscale_funcs ),
};

static qboolean AVI_LoadFFmpeg( void )
{
	if( !Sys_LoadLibrary( &libavutil_info ))
		return false;

	if( !Sys_LoadLibrary( &libavformat_info ))
		return false;

	if( !Sys_LoadLibrary( &libavcodec_info ))
		return false;

	if( !Sys_LoadLibrary( &libswresample_info ))
		return false;

	if( !Sys_LoadLibrary( &libswscale_info ))
		return false;

	return true;
}

static void AVI_UnloadFFmpeg( void )
{
	Sys_FreeLibrary( &libavutil_info );
	Sys_FreeLibrary( &libavformat_info );
	Sys_FreeLibrary( &libavcodec_info );
	Sys_FreeLibrary( &libswresample_info );
	Sys_FreeLibrary( &libswscale_info );
}

#else
static qboolean AVI_LoadFFmpeg( void )
{
	return true;
}

static void AVI_UnloadFFmpeg( void )
{

}
#endif

static qboolean AVI_ValidateFFmpegVersion( void )
{
	uint ver;

	// print version we're compiled with and which version we're running with
	ver = pavutil_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBAVUTIL_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	if( AV_VERSION_MAJOR( ver ) != SUPPORTED_AVU_VERSION_MAJOR )
	{
		Con_Printf( S_ERROR "AVI: Unsupported libavutil version.\n" );
		return false;
	}

	ver = pavformat_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBAVFORMAT_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	if( AV_VERSION_MAJOR( ver ) != SUPPORTED_AVF_VERSION_MAJOR )
	{
		Con_Printf( S_ERROR "AVI: Unsupported libavformat version.\n" );
		return false;
	}

	ver = pavcodec_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBAVCODEC_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	if( AV_VERSION_MAJOR( ver ) != SUPPORTED_AVC_VERSION_MAJOR )
	{
		Con_Printf( S_ERROR "AVI: Unsupported libavcodec version.\n" );
		return false;
	}

	ver = pswscale_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBSWSCALE_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	if( AV_VERSION_MAJOR( ver ) != SUPPORTED_SWS_VERSION_MAJOR )
	{
		Con_Printf( S_ERROR "AVI: Unsupported libswscale version.\n" );
		return false;
	}

	ver = pswresample_version();
	Con_Reportf( "AVI: %s (runtime %d.%d.%d)\n", LIBSWRESAMPLE_IDENT, AV_VERSION_MAJOR( ver ), AV_VERSION_MINOR( ver ), AV_VERSION_MICRO( ver ));

	if( AV_VERSION_MAJOR( ver ) != SUPPORTED_SWR_VERSION_MAJOR )
	{
		Con_Printf( S_ERROR "AVI: Unsupported libswresample version.\n" );
		return false;
	}

	return true;
}
#else
#if XASH_GAMECUBE
#define GCVID_HEADER_SIZE 24
#define GCVID_MAGIC_0 'G'
#define GCVID_MAGIC_1 'C'
#define GCVID_MAGIC_2 'V'
#define GCVID_MAGIC_3 '1'

struct movie_state_s
{
	file_t *file;
	byte *frame;
	double start_time;
	fs_offset_t data_offset;
	size_t frame_size;
	uint frame_count;
	uint fps_num;
	uint fps_den;
	uint current_frame;
	int xres;
	int yres;
	int x, y, w, h;
	int texture;
	qboolean active;
};

static uint AVI_GCVIDReadLE32( const byte *data )
{
	return (uint)data[0] | ((uint)data[1] << 8) | ((uint)data[2] << 16) | ((uint)data[3] << 24);
}

static qboolean AVI_GCVIDPath( const char *filename, char *path, size_t size )
{
	char *dot, *slash;

	if( !filename || !filename[0] )
		return false;

	Q_strncpy( path, filename, size );
	COM_FixSlashes( path );
	dot = strrchr( path, '.' );
	slash = strrchr( path, '/' );
	if( dot && ( !slash || dot > slash ))
		*dot = '\0';

	Q_strncat( path, ".gcvid", size );
	return true;
}

int AVI_GetVideoFrameNumber( movie_state_t *Avi, float time )
{
	if( !Avi || !Avi->active || Avi->fps_den == 0 )
		return 0;

	return bound( 0, (int)((double)time * (double)Avi->fps_num / (double)Avi->fps_den), (int)Avi->frame_count - 1 );
}

byte *AVI_GetVideoFrame( movie_state_t *Avi, int frame )
{
	if( !Avi || !Avi->active || frame < 0 || (uint)frame >= Avi->frame_count )
		return NULL;

	return Avi->frame;
}

qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration )
{
	if( !Avi || !Avi->active )
		return false;

	if( xres ) *xres = Avi->xres;
	if( yres ) *yres = Avi->yres;
	if( duration ) *duration = (float)((double)Avi->frame_count * (double)Avi->fps_den / (double)Avi->fps_num);

	return true;
}

qboolean AVI_HaveAudioTrack( const movie_state_t *Avi )
{
	return false;
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	byte header[GCVID_HEADER_SIZE];
	char path[MAX_SYSPATH];
	uint width, height;

	if( !Avi )
		return;

	if( Avi->active )
		AVI_CloseVideo( Avi );

	if( !AVI_GCVIDPath( filename, path, sizeof( path )))
		return;

	Avi->file = FS_Open( path, "rb", false );
	if( !Avi->file )
	{
		if( !quiet )
			Con_Printf( S_ERROR "Couldn't open GameCube intro stream %s\n", path );
		return;
	}

	if( FS_Read( Avi->file, header, sizeof( header )) != sizeof( header ) ||
		header[0] != GCVID_MAGIC_0 || header[1] != GCVID_MAGIC_1 ||
		header[2] != GCVID_MAGIC_2 || header[3] != GCVID_MAGIC_3 )
	{
		if( !quiet )
			Con_Printf( S_ERROR "%s is not a GameCube intro stream\n", path );
		AVI_CloseVideo( Avi );
		return;
	}

	width = AVI_GCVIDReadLE32( header + 4 );
	height = AVI_GCVIDReadLE32( header + 8 );
	Avi->fps_num = AVI_GCVIDReadLE32( header + 12 );
	Avi->fps_den = AVI_GCVIDReadLE32( header + 16 );
	Avi->frame_count = AVI_GCVIDReadLE32( header + 20 );

	if( width == 0 || height == 0 || Avi->fps_num == 0 || Avi->fps_den == 0 || Avi->frame_count == 0 )
	{
		if( !quiet )
			Con_Printf( S_ERROR "%s has invalid GameCube intro metadata\n", path );
		AVI_CloseVideo( Avi );
		return;
	}

	Avi->frame_size = width * height * 4;
	Avi->frame = Mem_Malloc( avi_mempool, Avi->frame_size );
	if( !Avi->frame )
	{
		AVI_CloseVideo( Avi );
		return;
	}

	Avi->xres = width;
	Avi->yres = height;
	Avi->x = 0;
	Avi->y = 0;
	Avi->w = -1;
	Avi->h = -1;
	Avi->texture = 0;
	Avi->data_offset = FS_Tell( Avi->file );
	Avi->current_frame = (uint)-1;
	Avi->start_time = Platform_DoubleTime();
	Avi->active = true;
	(void)load_audio;
}

void AVI_CloseVideo( movie_state_t *Avi )
{
	if( !Avi )
		return;

	if( Avi->file )
		FS_Close( Avi->file );
	if( Avi->frame )
		Mem_Free( Avi->frame );

	memset( Avi, 0, sizeof( *Avi ));
}

qboolean AVI_Think( movie_state_t *Avi )
{
	uint target_frame;
	fs_offset_t offset;
	double elapsed;

	if( !Avi || !Avi->active || !Avi->file || !Avi->frame )
		return false;

	elapsed = Platform_DoubleTime() - Avi->start_time;
	target_frame = (uint)( elapsed * (double)Avi->fps_num / (double)Avi->fps_den );
	if( target_frame >= Avi->frame_count )
		return false;

	if( target_frame != Avi->current_frame )
	{
		offset = Avi->data_offset + (fs_offset_t)target_frame * (fs_offset_t)Avi->frame_size;
		if( FS_Seek( Avi->file, offset, SEEK_SET ) == -1 ||
			FS_Read( Avi->file, Avi->frame, Avi->frame_size ) != (fs_offset_t)Avi->frame_size )
			return false;

		Avi->current_frame = target_frame;
		if( Avi->texture == 0 )
			ref.dllFuncs.GL_UpdateTexture( SCR_GetCinematicTexture(), Avi->xres, Avi->yres, Avi->xres, Avi->yres, Avi->frame, PF_BGRA_32 );
		else if( Avi->texture > 0 )
			ref.dllFuncs.GL_UpdateTexture( Avi->texture, Avi->xres, Avi->yres, Avi->w, Avi->h, Avi->frame, PF_BGRA_32 );
	}

	if( Avi->texture == 0 )
	{
		int w = Avi->w >= 0 ? Avi->w : refState.width;
		int h = Avi->h >= 0 ? Avi->h : refState.height;
		ref.dllFuncs.R_DrawStretchPic( Avi->x, Avi->y, w, h, 0, 0, 1, 1, SCR_GetCinematicTexture() );
	}

	return true;
}

qboolean AVI_SetParm( movie_state_t *Avi, enum movie_parms_e parm, ... )
{
	qboolean ret = true;
	va_list va;
	va_start( va, parm );

	if( !Avi )
	{
		va_end( va );
		return false;
	}

	while( parm != AVI_PARM_LAST )
	{
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
			Avi->start_time = Platform_DoubleTime();
			Avi->current_frame = (uint)-1;
			break;
		default:
			(void)va_arg( va, int );
			ret = false;
			break;
		}
		parm = va_arg( va, enum movie_parms_e );
	}

	va_end( va );
	return ret;
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
#endif

static qboolean AVI_ValidateFFmpegVersion( void )
{
	return false;
}

static qboolean AVI_LoadFFmpeg( void )
{
	return false;
}

static void AVI_UnloadFFmpeg( void )
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
	if( Sys_CheckParm( "-noavi" ))
	{
		Con_Printf( "AVI: Disabled\n" );
		return false;
	}

	if( XASH_AVI == AVI_NULL )
	{
#if XASH_GAMECUBE
		Con_Reportf( "AVI: GameCube raw video\n" );
		avi_initialized = true;
		avi_mempool = Mem_AllocPool( "AVI Zone" );
		return true;
#else
		Con_Printf( "AVI: Not supported\n" );
		return false;
#endif
	}

	if( !AVI_LoadFFmpeg( ))
		return false;

	if( !AVI_ValidateFFmpegVersion( ))
		return false;

	avi_initialized = true;
	avi_mempool = Mem_AllocPool( "AVI Zone" );

	return false;
}

void AVI_Shutdown( void )
{
	Mem_FreePool( &avi_mempool );
	avi_initialized = false;

	AVI_UnloadFFmpeg();
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
#if XASH_GAMECUBE
	AVI_OpenVideo( Avi, path, load_audio, false );
#else
	AVI_OpenVideo( Avi, fullpath, load_audio, false );
#endif

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
