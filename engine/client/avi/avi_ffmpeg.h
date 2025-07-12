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
#ifndef AVI_FFMPEG_H
#define AVI_FFMPEG_H

#if XASH_AVI == AVI_FFMPEG
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#if XASH_FFMPEG_DLOPEN

// the following symbols were taken from ffmpeg public headers
// on each major ffmpeg uprgade, they must be validated
// ffmpeg guarantees API and ABI compatibility between major versions
// so complain if this gets compiled against unsupported yet version
// the same check will be done in runtime to ensure compatibility
#define SUPPORTED_AVU_VERSION_MAJOR 59
#define SUPPORTED_AVF_VERSION_MAJOR 61
#define SUPPORTED_AVC_VERSION_MAJOR 61
#define SUPPORTED_SWR_VERSION_MAJOR 5
#define SUPPORTED_SWS_VERSION_MAJOR 8

#if SUPPORTED_AVU_VERSION_MAJOR != LIBAVUTIL_VERSION_MAJOR
#error
#endif

#if SUPPORTED_AVF_VERSION_MAJOR != LIBAVFORMAT_VERSION_MAJOR
#error
#endif

#if SUPPORTED_AVC_VERSION_MAJOR != LIBAVCODEC_VERSION_MAJOR
#error
#endif

#if SUPPORTED_SWR_VERSION_MAJOR != LIBSWRESAMPLE_VERSION_MAJOR
#error
#endif

#if SUPPORTED_SWS_VERSION_MAJOR != LIBSWSCALE_VERSION_MAJOR
#error
#endif

// libavutil
unsigned          (*pavutil_version)( void );
AVFrame           *(*pav_frame_alloc)( void );
void              (*pav_frame_free)( AVFrame **frame );
int               (*pav_frame_ref)( AVFrame *dst, const AVFrame *src );
void              (*pav_frame_unref)( AVFrame *frame );
int               (*pav_strerror)( int errnum, char *errbuf, size_t errbuf_size );
void              (*pav_free)( void *ptr );
int               (*pav_get_bytes_per_sample)( enum AVSampleFormat sample_fmt );
const char        *(*pav_get_media_type_string)( enum AVMediaType media_type );
int               (*pav_image_alloc)( uint8_t *pointers[4], int linesizes[4], int w, int h, enum AVPixelFormat pix_fmt, int align );

// libavformat
unsigned          (*pavformat_version)( void );
int               (*pav_find_best_stream)( AVFormatContext *ic, enum AVMediaType type, int wanted_stream_nb, int related_stream, const struct AVCodec **decoder_ret, int flags );
int               (*pav_read_frame)( AVFormatContext *s, AVPacket *pkt );
int               (*pav_seek_frame)( AVFormatContext *s, int stream_index, int64_t timestamp, int flags );
void              (*pavformat_close_input)(AVFormatContext **s);
int               (*pavformat_find_stream_info)(AVFormatContext *ic, AVDictionary **options);
int               (*pavformat_open_input)(AVFormatContext **ps, const char *url, const AVInputFormat *fmt, AVDictionary **options);

// libavcodec
unsigned          (*pavcodec_version)( void );
AVPacket          *(*pav_packet_alloc)( void );
void              (*pav_packet_free)( AVPacket **pkt );
void              (*pav_packet_unref)( AVPacket *pkt );
AVCodecContext    *(*pavcodec_alloc_context3)( const AVCodec *codec );
const AVCodec     *(*pavcodec_find_decoder)( enum AVCodecID id );
void              (*pavcodec_flush_buffers)( AVCodecContext *avctx );
void              (*pavcodec_free_context)( AVCodecContext **avctx );
int               (*pavcodec_open2)( AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options );
int               (*pavcodec_parameters_to_context)( AVCodecContext *codec, const struct AVCodecParameters *par );
int               (*pavcodec_receive_frame)( AVCodecContext *avctx, AVFrame *frame );
int               (*pavcodec_send_packet)( AVCodecContext *avctx, const AVPacket *avpkt );

// libswresample
unsigned          (*pswresample_version)( void );
int               (*pswr_alloc_set_opts2)( struct SwrContext **ps, const AVChannelLayout *out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate, const AVChannelLayout *in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate, int log_offset, void *log_ctx );
int               (*pswr_convert)( struct SwrContext *s, uint8_t *const *out, int out_count, const uint8_t *const *in, int in_count );
void              (*pswr_free)( struct SwrContext **s );
int               (*pswr_init)( struct SwrContext *s );

// libswscale
unsigned          (*pswscale_version)( void );
void              (*psws_freeContext)( struct SwsContext *swsContext );
struct SwsContext *(*psws_getContext)( int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat, int flags, SwsFilter *srcFilter, SwsFilter *dstFilter, const double *param );
int               (*psws_scale)( struct SwsContext *c, const uint8_t *const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH, uint8_t *const dst[], const int dstStride[] );

#else // !XASH_FFMPEG_DLOPEN

#define SUPPORTED_AVU_VERSION_MAJOR LIBAVUTIL_VERSION_MAJOR
#define SUPPORTED_AVF_VERSION_MAJOR LIBAVFORMAT_VERSION_MAJOR
#define SUPPORTED_AVC_VERSION_MAJOR LIBAVCODEC_VERSION_MAJOR
#define SUPPORTED_SWR_VERSION_MAJOR LIBSWRESAMPLE_VERSION_MAJOR
#define SUPPORTED_SWS_VERSION_MAJOR LIBSWSCALE_VERSION_MAJOR

// libavutil
#define pavutil_version           avutil_version
#define pav_frame_alloc           av_frame_alloc
#define pav_frame_ref             av_frame_ref
#define pav_frame_unref           av_Frame_unref
#define pav_strerror              av_strerror
#define pav_free                  av_free
#define pav_get_bytes_per_sample  av_get_bytes_per_sample
#define pav_get_media_type_string av_get_media_type_string
#define pav_image_alloc           av_image_alloc

// libavformat
#define pavformat_version    avformat_version
#define pav_find_best_stream av_find_best_stream
#define pav_read_frame       av_read_frame
#define pav_seek_frame       av_seek_frame
#define pavformat_close_input avformat_close_input
#define pavformat_find_stream_info avformat_find_stream_info
#define pavformat_open_input avformat_open_input

// libavcodec
#define pavcodec_version               avcodec_version
#define pav_packet_alloc               av_packet_alloc
#define pav_packet_free                av_packet_free
#define pav_packet_unref               av_packet_unref
#define pavcodec_alloc_context3        avcodec_alloc_context3
#define pavcodec_find_decoder          avcodec_find_decoder
#define pavcodec_flush_buffers         avcodec_flush_buffers
#define pavcodec_free_context          avcodec_free_context
#define pavcodec_open2                 avcodec_open2
#define pavcodec_parameters_to_context avcodec_parameters_to_context
#define pavcodec_receive_frame         avcodec_receive_frame
#define pavcodec_send_packet           avcodec_send_packet

// libswresample
#define pswresample_version  swresample_version
#define pswr_alloc_set_opts2 swr_alloc_set_opts2
#define pswr_convert         swr_convert
#define pswr_free            swr_free
#define pswr_init            swr_init

// libswscale
#define pswscale_version swscale_version
#define psws_freeContext sws_freeContext
#define psws_getContext  sws_getContext
#define psws_scale       sws_scale

#endif // !XASH_FFMPEG_DLOPEN
#endif // XASH_AVI == AVI_FFMPEG
#endif // AVI_FFMPEG_H
