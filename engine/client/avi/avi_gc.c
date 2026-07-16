/*
avi_gc.c - GameCube retail AVI/Cinepak movie playback
Copyright (C) 2026 Xash3D GameCube port contributors
*/
#include "defaults.h"
#include "common.h"
#include "client.h"
#include "sound.h"
#include "avi_cinepak.h"
#include "avi_gc.h"
#include "platform/platform.h"

static qboolean avi_initialized;
static poolhandle_t avi_mempool;
static movie_state_t avi[2];

#define AVI_RL32( p ) ((uint32_t)((p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24)))
#define AVI_RL16( p ) ((uint16_t)((p)[0] | ((uint16_t)(p)[1] << 8)))
#define AVI_GCVID_MAGIC_0 'G'
#define AVI_GCVID_MAGIC_1 'C'
#define AVI_GCVID_MAGIC_2 'V'
#define AVI_GCVID_MAGIC_3 '2'
#define AVI_GCVID_HEADER_SIZE 28
#define AVI_GCVID_FLAG_STATIC_HOLD ( 1u << 31 )
#define AVI_GCVID_FLAG_BGRA32 ( 1u << 30 )
#define AVI_GCVID_KEYFRAME 0
#define AVI_GCVID_DELTAFRAME 1

static qboolean AVI_ParseHeader( movie_state_t *Avi, qboolean quiet );
qboolean AVI_HaveAudioTrack( const movie_state_t *Avi );
static fs_offset_t AVI_ScanFor( file_t *file, const char *tag, fs_offset_t start, fs_offset_t end );
static void AVI_ParseAudioFormat( movie_state_t *Avi, fs_offset_t file_size );

#if XASH_GAMECUBE
#define GC_AVI_AUDIO_SLICE_BYTES	16384
#define GC_AVI_AUDIO_LEAD_SAMPLES	2048
/* Quarter-res Cinepak fallback when no .gcvid companion is present. */
#define GC_AVI_DECODE_SCALE		4
#define GC_AVI_UPLOAD_MAX_W		160
#define GC_AVI_UPLOAD_MAX_H		120
#define GC_AVI_STATIC_FRAME_MAX_BYTES	( 320 * 240 * 2 )
static byte gc_avi_static_frame[GC_AVI_STATIC_FRAME_MAX_BYTES] __attribute__((aligned( 32 )));

static qboolean AVI_UsesStaticFrameBuffer( const movie_state_t *Avi )
{
	return Avi && Avi->frame == gc_avi_static_frame;
}
#endif

static void AVI_RGB24ToBGRA( const byte *src, int src_stride, byte *dst, int width, int height )
{
	int x, y;

	for( y = 0; y < height; y++ )
	{
		const byte *row = src + y * src_stride;
		byte *out = dst + y * width * 4;

		for( x = 0; x < width; x++ )
		{
			out[x * 4 + 0] = row[x * 3 + 2];
			out[x * 4 + 1] = row[x * 3 + 1];
			out[x * 4 + 2] = row[x * 3 + 0];
			out[x * 4 + 3] = 255;
		}
	}
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

static qboolean AVI_OpenGCVID( movie_state_t *Avi, const char *filename, qboolean quiet )
{
	byte header[AVI_GCVID_HEADER_SIZE];
	char path[MAX_SYSPATH];
	uint width, height, tile_size;
	size_t offset_table_bytes;

	if( !Avi || !AVI_GCVIDPath( filename, path, sizeof( path )))
		return false;

	Avi->file = FS_Open( path, "rb", false );
	if( !Avi->file )
	{
		Con_Reportf( "Xash3D GameCube: intro GCVID missing %s\n", path );
		return false;
	}

	if( FS_Read( Avi->file, header, sizeof( header )) != sizeof( header ) ||
		header[0] != AVI_GCVID_MAGIC_0 || header[1] != AVI_GCVID_MAGIC_1 ||
		header[2] != AVI_GCVID_MAGIC_2 || header[3] != AVI_GCVID_MAGIC_3 )
	{
		Con_Reportf( "Xash3D GameCube: intro GCVID bad magic %s\n", path );
		if( !quiet )
			Con_Printf( S_ERROR "%s is not a valid GameCube intro stream\n", path );
		AVI_CloseVideo( Avi );
		return false;
	}

	width = AVI_RL32( header + 4 );
	height = AVI_RL32( header + 8 );
	Avi->fps_num = AVI_RL32( header + 12 );
	Avi->fps_den = AVI_RL32( header + 16 );
	Avi->frame_count = AVI_RL32( header + 20 );
	tile_size = AVI_RL32( header + 24 );
	Avi->raw_static_frame = ( tile_size & AVI_GCVID_FLAG_STATIC_HOLD ) != 0;
	Avi->raw_rgb565 = ( tile_size & AVI_GCVID_FLAG_BGRA32 ) == 0;
	tile_size &= ~AVI_GCVID_FLAG_BGRA32;
	tile_size &= ~AVI_GCVID_FLAG_STATIC_HOLD;
	if( width == 0 || height == 0 || Avi->fps_num == 0 || Avi->fps_den == 0 || Avi->frame_count == 0 )
	{
		if( !quiet )
			Con_Printf( S_ERROR "%s has invalid GameCube intro metadata\n", path );
		AVI_CloseVideo( Avi );
		return false;
	}

	Avi->frame_size = (size_t)width * (size_t)height * ( Avi->raw_rgb565 ? 2 : 4 );
#if XASH_GAMECUBE
	if( Avi->raw_rgb565 && Avi->frame_size <= GC_AVI_STATIC_FRAME_MAX_BYTES )
	{
		memset( gc_avi_static_frame, 0, Avi->frame_size );
		Avi->frame = gc_avi_static_frame;
	}
	else
#endif
	Avi->frame = Mem_Malloc( avi_mempool, Avi->frame_size );
	if( !Avi->frame )
	{
		AVI_CloseVideo( Avi );
		return false;
	}

	offset_table_bytes = (size_t)Avi->frame_count * sizeof( uint32_t );
	Avi->raw_frame_offsets = Mem_Malloc( avi_mempool, offset_table_bytes );
	if( !Avi->raw_frame_offsets )
	{
		AVI_CloseVideo( Avi );
		return false;
	}
	if( FS_Read( Avi->file, Avi->raw_frame_offsets, offset_table_bytes ) != (fs_offset_t)offset_table_bytes )
	{
		AVI_CloseVideo( Avi );
		return false;
	}
#if XASH_BIG_ENDIAN
	for( uint i = 0; i < Avi->frame_count; i++ )
		Avi->raw_frame_offsets[i] = LittleLong( Avi->raw_frame_offsets[i] );
#endif

	Avi->raw_video = true;
	/* Static-hold companions store one keyframe; never walk a fake delta timeline. */
	Avi->raw_delta_tiles = !Avi->raw_static_frame && tile_size == 8;
	Avi->width = (int)width;
	Avi->height = (int)height;
	Avi->decode_scale = 1;
	Avi->upload_width = (int)width;
	Avi->upload_height = (int)height;
	Avi->data_offset = FS_Tell( Avi->file );
	Avi->current_frame = (uint)-1;
	Avi->active = true;
	Con_Reportf( "Xash3D GameCube: intro GCVID opened %s (%ux%u, %u frames%s, %s)\n",
		path, width, height, Avi->frame_count,
		Avi->raw_static_frame ? ", static hold" : "",
		Avi->raw_rgb565 ? "rgb565" : "bgra32" );
	return true;
}

static void AVI_ClearAudioState( movie_state_t *Avi )
{
	if( !Avi )
		return;

	if( Avi->audio_file )
		FS_Close( Avi->audio_file );
	if( Avi->audio_chunk )
		Mem_Free( Avi->audio_chunk );
	if( Avi->audio_index )
		Mem_Free( Avi->audio_index );

	Avi->audio_file = NULL;
	Avi->audio_chunk = NULL;
	Avi->audio_index = NULL;
	Avi->audio_chunk_count = 0;
	Avi->audio_current_chunk = 0;
	Avi->audio_chunk_size = 0;
	Avi->audio_chunk_offset = 0;
	Avi->audio_rate = 0;
	Avi->audio_width = 0;
	Avi->audio_channels = 0;
	Avi->audio_bytes_submitted = 0;
	Avi->audio_reported = false;
	Avi->audio_channel_ready = false;
	Avi->audio_playback_started = false;
}

static void AVI_BeginAudioPlayback( movie_state_t *Avi )
{
	rawchan_t *ch;

	if( !Avi || Avi->audio_playback_started )
		return;

	Avi->audio_current_chunk = 0;
	Avi->audio_chunk_size = 0;
	Avi->audio_chunk_offset = 0;
	Avi->audio_bytes_submitted = 0;
	Avi->audio_reported = false;
	Avi->audio_channel_ready = false;

	if( snd.initialized )
	{
		ch = S_FindRawChannel( S_RAW_SOUND_SOUNDTRACK, false );
		if( ch )
		{
			ch->s_rawend = snd.soundtime;
			ch->engine_reserved[0] = 0;
			ch->leftvol = ch->rightvol = 0;
		}
	}

	Avi->audio_playback_started = true;
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: intro AVI audio playback synced sound=%d painted=%d rate=%u\n",
		snd.soundtime, snd.paintedtime, Avi->audio_rate );
#endif
}

static void AVI_ResetSoundtrackRawChannel( movie_state_t *Avi )
{
	rawchan_t *ch;
	uint audio_time;

	if( !Avi || Avi->audio_channel_ready || !snd.initialized )
		return;

	ch = S_FindRawChannel( S_RAW_SOUND_SOUNDTRACK, true );
	if( !ch )
		return;

	audio_time = snd.soundtime;

	ch->master_vol = 0;
	ch->leftvol = 0;
	ch->rightvol = 0;
	ch->dist_mult = 0.0f;
	ch->s_rawend = audio_time;
	ch->oldtime = -1.0f;
	ch->engine_reserved[0] = 0;
	Avi->audio_channel_ready = true;
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: intro AVI reset soundtrack raw channel sound=%d painted=%d base=%u max=%lu\n",
		snd.soundtime, snd.paintedtime, audio_time, (unsigned long)ch->max_samples );
#endif
}

static void AVI_AttachAudioFromAVI( movie_state_t *Avi, const char *filename )
{
	byte riff[12];
	byte idx_header[4];
	byte *idx_data = NULL;
	file_t *file = NULL;
	fs_offset_t file_size, movi_pos, idx_pos;
	uint idx_size, entries;
	uint i, audio_count = 0, chunk_capacity = 0;
	movie_state_t meta;

	if( !Avi || !filename || !filename[0] )
		return;

	file = FS_Open( filename, "rb", false );
	if( !file )
		return;
	memset( &meta, 0, sizeof( meta ));
	meta.file = file;

	if( FS_Seek( file, 0, SEEK_END ) == -1 )
		goto fail;
	file_size = FS_Tell( file );
	if( FS_Seek( file, 0, SEEK_SET ) == -1 )
		goto fail;
	if( FS_Read( file, riff, sizeof( riff )) != sizeof( riff ) ||
		memcmp( riff, "RIFF", 4 ) || memcmp( riff + 8, "AVI ", 4 ))
		goto fail;

	movi_pos = AVI_ScanFor( file, "movi", 0, file_size );
	idx_pos = AVI_ScanFor( file, "idx1", Q_max( 0, file_size - 65536 ), file_size );
	if( movi_pos < 0 || idx_pos < 0 )
		goto fail;

	AVI_ParseAudioFormat( &meta, file_size );
	if( meta.audio_rate == 0 )
		goto fail;

	if( FS_Seek( file, idx_pos + 4, SEEK_SET ) == -1 )
		goto fail;
	if( FS_Read( file, idx_header, sizeof( idx_header )) != sizeof( idx_header ))
		goto fail;
	idx_size = AVI_RL32( idx_header );
	if( idx_size < 16 || idx_size > 16 * 4096 )
		goto fail;

	idx_data = Mem_Malloc( avi_mempool, idx_size );
	if( !idx_data )
		goto fail;
	if( FS_Read( file, idx_data, idx_size ) != (fs_offset_t)idx_size )
		goto fail;

	entries = idx_size / 16;
	for( i = 0; i < entries; i++ )
	{
		const byte *entry = idx_data + i * 16;
		if( entry[2] == 'w' && entry[3] == 'b' )
		{
			uint size = AVI_RL32( entry + 12 );
			audio_count++;
			if( size > chunk_capacity )
				chunk_capacity = size;
		}
	}
	if( audio_count == 0 || chunk_capacity == 0 )
		goto fail;

	AVI_ClearAudioState( Avi );
	Avi->audio_file = file;
	Avi->audio_rate = meta.audio_rate;
	Avi->audio_width = meta.audio_width;
	Avi->audio_channels = meta.audio_channels;
	Avi->audio_index = Mem_Calloc( avi_mempool, audio_count * sizeof( avi_frame_index_t ));
	Avi->audio_chunk = Mem_Malloc( avi_mempool, chunk_capacity );
	if( !Avi->audio_index || !Avi->audio_chunk )
		goto fail;

	Avi->audio_chunk_count = 0;
	Avi->chunk_capacity = chunk_capacity;
	for( i = 0; i < entries; i++ )
	{
		const byte *entry = idx_data + i * 16;
		if( entry[2] == 'w' && entry[3] == 'b' )
		{
			Avi->audio_index[Avi->audio_chunk_count].offset = movi_pos + AVI_RL32( entry + 8 );
			Avi->audio_index[Avi->audio_chunk_count].size = AVI_RL32( entry + 12 );
			Avi->audio_chunk_count++;
		}
	}
	Avi->audio_current_chunk = 0;
	Avi->audio_chunk_size = 0;
	Avi->audio_chunk_offset = 0;
	Avi->audio_bytes_submitted = 0;
	Avi->audio_reported = false;
	Mem_Free( idx_data );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: intro AVI audio attached %s rate=%u width=%u channels=%u chunks=%u\n",
		filename, Avi->audio_rate, Avi->audio_width, Avi->audio_channels, Avi->audio_chunk_count );
#endif
	return;

fail:
	if( idx_data )
		Mem_Free( idx_data );
	AVI_ClearAudioState( Avi );
}

#if XASH_GAMECUBE
static void AVI_GCConfigureVideoPath( movie_state_t *Avi )
{
	Avi->decode_scale = 1;
	/* Fullscreen intros + logo: decode at half resolution via Cinepak coord_scale. */
	if( Avi->width >= 320 && Avi->height >= 64 )
		Avi->decode_scale = GC_AVI_DECODE_SCALE;
	if( Avi->decode_scale < 1 )
		Avi->decode_scale = 1;
	Avi->upload_width = Avi->width / Avi->decode_scale;
	Avi->upload_height = Avi->height / Avi->decode_scale;
	if( Avi->upload_width < 1 )
		Avi->upload_width = 1;
	if( Avi->upload_height < 1 )
		Avi->upload_height = 1;
	Con_Reportf( "Xash3D GameCube: intro AVI path %dx%d -> decode/upload %dx%d (scale %d)\n",
		Avi->width, Avi->height, Avi->upload_width, Avi->upload_height, Avi->decode_scale );
}

static void AVI_GCDownsampleToBGRA( const movie_state_t *Avi )
{
	const byte *src = Avi->decoder.rgb;
	int src_stride = Avi->decoder.stride;
	int step = 1;
	int dw = Avi->upload_width;
	int dh = Avi->upload_height;
	int x, y;

	if( !src || !Avi->frame || dw <= 0 || dh <= 0 )
		return;

	/* Prefer 1:1 convert when Cinepak already decoded into upload size. */
	if( Avi->decoder.width > dw && dw > 0 && ( Avi->decoder.width % dw ) == 0 )
		step = Avi->decoder.width / dw;

	for( y = 0; y < dh; y++ )
	{
		const byte *row = src + ( y * step ) * src_stride;
		byte *out = Avi->frame + y * dw * 4;

		for( x = 0; x < dw; x++ )
		{
			const byte *p = row + x * step * 3;

			out[x * 4 + 0] = p[2];
			out[x * 4 + 1] = p[1];
			out[x * 4 + 2] = p[0];
			out[x * 4 + 3] = 255;
		}
	}
}
#endif

static fs_offset_t AVI_ScanFor( file_t *file, const char *tag, fs_offset_t start, fs_offset_t end )
{
	byte buf[4096];
	fs_offset_t pos = start;

	while( pos + 4 <= end )
	{
		size_t to_read = Q_min( sizeof( buf ), end - pos );
		size_t i;

		if( FS_Seek( file, pos, SEEK_SET ) == -1 )
			break;
		if( FS_Read( file, buf, to_read ) != (fs_offset_t)to_read )
			break;

		for( i = 0; i + 4 <= to_read; i++ )
		{
			if( !memcmp( buf + i, tag, 4 ))
				return pos + i;
		}

		if( to_read <= 3 )
			break;
		pos += to_read - 3;
	}

	return -1;
}

static void AVI_ParseAudioFormat( movie_state_t *Avi, fs_offset_t file_size )
{
	byte fmt[16];
	byte strf_header[8];
	fs_offset_t auds_pos, strf_pos;
	uint strf_size;

	Avi->audio_rate = 0;
	Avi->audio_width = 0;
	Avi->audio_channels = 0;

	auds_pos = AVI_ScanFor( Avi->file, "auds", 0, Q_min( file_size, 65536 ));
	if( auds_pos < 0 )
		return;

	strf_pos = AVI_ScanFor( Avi->file, "strf", auds_pos, Q_min( auds_pos + 4096, file_size ));
	if( strf_pos < 0 )
		return;

	if( FS_Seek( Avi->file, strf_pos, SEEK_SET ) == -1 ||
		FS_Read( Avi->file, strf_header, sizeof( strf_header )) != sizeof( strf_header ))
		return;

	strf_size = AVI_RL32( strf_header + 4 );
	if( strf_size < sizeof( fmt ))
		return;

	if( FS_Read( Avi->file, fmt, sizeof( fmt )) != sizeof( fmt ))
		return;

	/* GoldSrc's retail valve.avi uses PCM unsigned 8-bit mono audio. */
	if( AVI_RL16( fmt + 0 ) != 1 )
		return;

	Avi->audio_channels = AVI_RL16( fmt + 2 );
	Avi->audio_rate = AVI_RL32( fmt + 4 );
	Avi->audio_width = AVI_RL16( fmt + 14 ) / 8;

	if( Avi->audio_channels < 1 || Avi->audio_channels > 2 ||
		Avi->audio_width < 1 || Avi->audio_width > 2 || Avi->audio_rate == 0 )
	{
		Avi->audio_rate = 0;
		Avi->audio_width = 0;
		Avi->audio_channels = 0;
	}
#if XASH_GAMECUBE
	else
	{
		Con_Reportf( "Xash3D GameCube: intro AVI audio PCM rate=%u width=%u channels=%u\n",
			Avi->audio_rate, Avi->audio_width, Avi->audio_channels );
	}
#endif
}

static qboolean AVI_ParseHeader( movie_state_t *Avi, qboolean quiet )
{
	byte avih[56];
	byte riff[12];
	byte idx_header[4];
	fs_offset_t file_size, avih_pos, movi_pos, idx_pos;
	uint idx_size = 0;
	byte *idx_data = NULL;
	uint i, entries;

	if( FS_Seek( Avi->file, 0, SEEK_END ) == -1 )
		return false;
	file_size = FS_Tell( Avi->file );
	if( FS_Seek( Avi->file, 0, SEEK_SET ) == -1 )
		return false;

	if( FS_Read( Avi->file, riff, sizeof( riff )) != sizeof( riff ) ||
		memcmp( riff, "RIFF", 4 ) || memcmp( riff + 8, "AVI ", 4 ))
	{
		if( !quiet )
		{
#if XASH_GAMECUBE
			Con_Reportf( "Xash3D GameCube: AVI header bytes %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
				riff[0], riff[1], riff[2], riff[3], riff[4], riff[5],
				riff[6], riff[7], riff[8], riff[9], riff[10], riff[11] );
#endif
			Con_Printf( S_ERROR "Intro video is not an AVI file\n" );
		}
		return false;
	}

	avih_pos = AVI_ScanFor( Avi->file, "avih", 0, Q_min( file_size, 65536 ));
	if( avih_pos < 0 )
	{
		if( !quiet ) Con_Printf( S_ERROR "Couldn't find AVI header\n" );
		return false;
	}

	movi_pos = AVI_ScanFor( Avi->file, "movi", 0, file_size );
	if( movi_pos < 0 )
	{
		if( !quiet ) Con_Printf( S_ERROR "Couldn't find AVI movie data\n" );
		return false;
	}
	Avi->movie_list_pos = movi_pos;

	idx_pos = AVI_ScanFor( Avi->file, "idx1", Q_max( 0, file_size - 65536 ), file_size );
	if( idx_pos < 0 )
	{
		if( !quiet ) Con_Printf( S_ERROR "Couldn't find AVI index\n" );
		return false;
	}

	if( FS_Seek( Avi->file, avih_pos + 8, SEEK_SET ) == -1 ||
		FS_Read( Avi->file, avih, sizeof( avih )) != sizeof( avih ))
		return false;

	Avi->fps_num = 1000000;
	Avi->fps_den = AVI_RL32( avih );
	if( Avi->fps_den == 0 )
		Avi->fps_den = 66667;
	Avi->frame_count = AVI_RL32( avih + 16 );
	Avi->width = AVI_RL32( avih + 32 );
	Avi->height = AVI_RL32( avih + 36 );
	AVI_ParseAudioFormat( Avi, file_size );

	if( FS_Seek( Avi->file, idx_pos + 4, SEEK_SET ) == -1 )
		return false;
	if( FS_Read( Avi->file, idx_header, sizeof( idx_header )) != sizeof( idx_header ))
		return false;
	idx_size = AVI_RL32( idx_header );

	if( idx_size < 16 || idx_size > 16 * 4096 )
	{
		if( !quiet ) Con_Printf( S_ERROR "Invalid AVI index size\n" );
		return false;
	}

	idx_data = Mem_Malloc( avi_mempool, idx_size );
	if( !idx_data )
		return false;

	if( FS_Read( Avi->file, idx_data, idx_size ) != (fs_offset_t)idx_size )
	{
		Mem_Free( idx_data );
		return false;
	}

	entries = idx_size / 16;
	Avi->frame_count = 0;
	Avi->audio_chunk_count = 0;
	for( i = 0; i < entries; i++ )
	{
		const byte *entry = idx_data + i * 16;
		if( entry[0] == '0' && entry[1] == '0' && entry[2] == 'd' && entry[3] == 'c' )
			Avi->frame_count++;
		else if( entry[2] == 'w' && entry[3] == 'b' && Avi->audio_rate > 0 )
			Avi->audio_chunk_count++;
	}

	if( Avi->frame_count == 0 )
	{
		Mem_Free( idx_data );
		if( !quiet ) Con_Printf( S_ERROR "No Cinepak frames in AVI index\n" );
		return false;
	}

	Avi->index = Mem_Calloc( avi_mempool, Avi->frame_count * sizeof( avi_frame_index_t ));
	if( !Avi->index )
	{
		Mem_Free( idx_data );
		return false;
	}
	if( Avi->audio_chunk_count > 0 )
	{
		Avi->audio_index = Mem_Calloc( avi_mempool, Avi->audio_chunk_count * sizeof( avi_frame_index_t ));
		if( !Avi->audio_index )
		{
			Mem_Free( idx_data );
			return false;
		}
	}

	Avi->frame_count = 0;
	Avi->audio_chunk_count = 0;
	Avi->chunk_capacity = 0;
	for( i = 0; i < entries; i++ )
	{
		const byte *entry = idx_data + i * 16;
		if( entry[0] == '0' && entry[1] == '0' && entry[2] == 'd' && entry[3] == 'c' )
		{
			Avi->index[Avi->frame_count].offset = Avi->movie_list_pos + AVI_RL32( entry + 8 );
			Avi->index[Avi->frame_count].size = AVI_RL32( entry + 12 );
			if( Avi->index[Avi->frame_count].size > Avi->chunk_capacity )
				Avi->chunk_capacity = Avi->index[Avi->frame_count].size;
			Avi->frame_count++;
		}
		else if( entry[2] == 'w' && entry[3] == 'b' && Avi->audio_index )
		{
			Avi->audio_index[Avi->audio_chunk_count].offset = Avi->movie_list_pos + AVI_RL32( entry + 8 );
			Avi->audio_index[Avi->audio_chunk_count].size = AVI_RL32( entry + 12 );
			if( Avi->audio_index[Avi->audio_chunk_count].size > Avi->chunk_capacity )
				Avi->chunk_capacity = Avi->audio_index[Avi->audio_chunk_count].size;
			Avi->audio_chunk_count++;
		}
	}
	Mem_Free( idx_data );

	Avi->chunk = Mem_Malloc( avi_mempool, Avi->chunk_capacity );
	if( !Avi->chunk )
		return false;
	Avi->audio_chunk = Mem_Malloc( avi_mempool, Avi->chunk_capacity );
	if( !Avi->audio_chunk )
		return false;

#if XASH_GAMECUBE
	AVI_GCConfigureVideoPath( Avi );
	Avi->frame = Mem_Malloc( avi_mempool, Avi->upload_width * Avi->upload_height * 4 );
	if( !Avi->frame )
		return false;
	Avi->upload_frame = Avi->frame;

	/* Decode directly into upload resolution (coord_scale) to cut MEM1 + CPU. */
	if( !Cinepak_Init( &Avi->decoder, Avi->upload_width, Avi->upload_height,
		Avi->width, Avi->height, avi_mempool ))
		return false;
#else
	Avi->upload_width = Avi->width;
	Avi->upload_height = Avi->height;
	Avi->frame = Mem_Malloc( avi_mempool, Avi->width * Avi->height * 4 );
	if( !Avi->frame )
		return false;
	Avi->upload_frame = Avi->frame;

	if( !Cinepak_Init( &Avi->decoder, Avi->width, Avi->height, Avi->width, Avi->height, avi_mempool ))
		return false;
#endif

	return Avi->width > 0 && Avi->height > 0;
}

static qboolean AVI_DecodeFrame( movie_state_t *Avi, uint frame, qboolean upload )
{
	byte header[8];
	byte tag;
	byte count_bytes[2];
	fs_offset_t pos;
	uint chunk_size;

	if( !Avi || frame >= Avi->frame_count )
		return false;

	if( Avi->raw_video )
	{
		if( Avi->raw_static_frame && Avi->current_frame != (uint)-1 )
			return true;

		uint start = 0;

		if( Avi->raw_delta_tiles )
		{
			if( frame < Avi->current_frame )
			{
				Avi->current_frame = (uint)-1;
				start = 0;
			}
			else start = ( Avi->current_frame == (uint)-1 ) ? 0 : ( Avi->current_frame + 1 );

			for( uint f = start; f <= frame; f++ )
			{
				fs_offset_t offset = Avi->data_offset + (fs_offset_t)Avi->raw_frame_offsets[f];
				if( FS_Seek( Avi->file, offset, SEEK_SET ) == -1 )
					return false;
				if( FS_Read( Avi->file, &tag, 1 ) != 1 )
					return false;
				if( tag == AVI_GCVID_KEYFRAME )
				{
					if( FS_Read( Avi->file, Avi->frame, Avi->frame_size ) != (fs_offset_t)Avi->frame_size )
						return false;
				}
				else if( tag == AVI_GCVID_DELTAFRAME )
				{
					uint changed_tiles;
					const uint tile_size = 8;
					const uint tiles_x = (uint)Avi->width / tile_size;
					byte tilebuf[tile_size * tile_size * 2];

					if( FS_Read( Avi->file, count_bytes, sizeof( count_bytes )) != sizeof( count_bytes ))
						return false;
					changed_tiles = AVI_RL16( count_bytes );
					for( uint tile = 0; tile < changed_tiles; tile++ )
					{
						byte index_bytes[2];
						uint tile_index, tile_x, tile_y;

						if( FS_Read( Avi->file, index_bytes, sizeof( index_bytes )) != sizeof( index_bytes ))
							return false;
						tile_index = AVI_RL16( index_bytes );
						tile_x = tile_index % tiles_x;
						tile_y = tile_index / tiles_x;
						if( FS_Read( Avi->file, tilebuf, sizeof( tilebuf )) != sizeof( tilebuf ))
							return false;
						for( uint row = 0; row < tile_size; row++ )
						{
							byte *dst = Avi->frame + ((( tile_y * tile_size + row ) * Avi->width ) + tile_x * tile_size ) * 2;
							memcpy( dst, tilebuf + row * tile_size * 2, tile_size * 2 );
						}
					}
				}
				else return false;
			}
			return true;
		}

		{
			fs_offset_t offset = Avi->data_offset + (fs_offset_t)frame * (fs_offset_t)Avi->frame_size;
			if( FS_Seek( Avi->file, offset, SEEK_SET ) == -1 )
				return false;
			if( FS_Read( Avi->file, Avi->frame, Avi->frame_size ) != (fs_offset_t)Avi->frame_size )
				return false;
		}
		return true;
	}

	pos = Avi->index[frame].offset;
	if( FS_Seek( Avi->file, pos, SEEK_SET ) == -1 )
	{
#if XASH_GAMECUBE
		Con_Reportf( "Xash3D GameCube: intro AVI decode seek failed frame=%u pos=%ld\n",
			frame, (long)pos );
#endif
		return false;
	}
	if( FS_Read( Avi->file, header, sizeof( header )) != sizeof( header ))
	{
#if XASH_GAMECUBE
		Con_Reportf( "Xash3D GameCube: intro AVI decode header read failed frame=%u pos=%ld\n",
			frame, (long)pos );
#endif
		return false;
	}
	if( header[2] != 'd' || header[3] != 'c' )
	{
#if XASH_GAMECUBE
		Con_Reportf( "Xash3D GameCube: intro AVI unsupported chunk %c%c%c%c frame=%u pos=%ld\n",
			header[0], header[1], header[2], header[3], frame, (long)pos );
#endif
		return false;
	}

	chunk_size = AVI_RL32( header + 4 );
	if( chunk_size < 4 || chunk_size > Avi->chunk_capacity )
	{
#if XASH_GAMECUBE
		Con_Reportf( "Xash3D GameCube: intro AVI invalid chunk size frame=%u size=%u capacity=%lu\n",
			frame, chunk_size, (unsigned long)Avi->chunk_capacity );
#endif
		return false;
	}

	if( FS_Read( Avi->file, Avi->chunk, chunk_size ) != (fs_offset_t)chunk_size )
	{
#if XASH_GAMECUBE
		Con_Reportf( "Xash3D GameCube: intro AVI chunk read failed frame=%u size=%u\n",
			frame, chunk_size );
#endif
		return false;
	}

	if( !Cinepak_Decode( &Avi->decoder, Avi->chunk, chunk_size ))
	{
#if XASH_GAMECUBE
		Con_Reportf( "Xash3D GameCube: intro AVI Cinepak decode failed frame=%u size=%u\n",
			frame, chunk_size );
#endif
		return false;
	}

#if XASH_GAMECUBE
	if(( frame == 0 || frame == 15 || frame == 30 || frame == 60 ) && Avi->decoder.rgb )
	{
		const byte *mid = Avi->decoder.rgb +
			( Avi->decoder.height / 2 ) * Avi->decoder.stride +
			( Avi->decoder.width / 2 ) * 3;
		Con_Reportf( "Xash3D GameCube: intro AVI frame %u mid rgb=%u,%u,%u\n",
			frame, mid[0], mid[1], mid[2] );
	}
#endif

	if( upload )
	{
#if XASH_GAMECUBE
		AVI_GCDownsampleToBGRA( Avi );
#else
		AVI_RGB24ToBGRA( Avi->decoder.rgb, Avi->decoder.stride, Avi->frame, Avi->width, Avi->height );
#endif
	}
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

	if((uint)frame != Avi->current_frame && !AVI_DecodeFrame( Avi, frame, true ))
		return NULL;

	return Avi->frame;
}

qboolean AVI_GetVideoInfo( movie_state_t *Avi, int *xres, int *yres, float *duration )
{
	if( !Avi || !Avi->active )
		return false;

	if( xres ) *xres = Avi->upload_width;
	if( yres ) *yres = Avi->upload_height;
	if( duration ) *duration = (float)((double)Avi->frame_count * (double)Avi->fps_den / (double)Avi->fps_num);
	return true;
}

qboolean AVI_HaveAudioTrack( const movie_state_t *Avi )
{
	return Avi && Avi->audio_index && Avi->audio_chunk_count > 0 &&
		Avi->audio_rate > 0 && Avi->audio_width > 0 && Avi->audio_channels > 0;
}

static void AVI_StreamAudio( movie_state_t *Avi )
{
	uint bytes_per_sample;
	rawchan_t *ch;
	int movie_vol;
	file_t *audio_file;
	uint target_ahead;

	if( !AVI_HaveAudioTrack( Avi ) || !snd.initialized || cl.paused || !snd.streaming || !Avi->playback_started )
		return;
	audio_file = Avi->audio_file ? Avi->audio_file : Avi->file;
	if( !audio_file )
		return;

	AVI_ResetSoundtrackRawChannel( Avi );

	ch = S_FindRawChannel( S_RAW_SOUND_SOUNDTRACK, true );
	if( !ch )
		return;

	movie_vol = 256;

	ch->master_vol = movie_vol;
	ch->dist_mult = 0.0f;
	if( ch->s_rawend < snd.soundtime )
		ch->s_rawend = snd.soundtime;

	target_ahead = ch->max_samples;
#if XASH_GAMECUBE
	if( target_ahead > GC_AVI_AUDIO_LEAD_SAMPLES )
		target_ahead = GC_AVI_AUDIO_LEAD_SAMPLES;
#endif

	bytes_per_sample = Avi->audio_width * Avi->audio_channels;
	while( Avi->audio_current_chunk < Avi->audio_chunk_count &&
		ch->s_rawend < snd.soundtime + target_ahead )
	{
		avi_frame_index_t *chunk = &Avi->audio_index[Avi->audio_current_chunk];
		uint buffer_samples;
		uint file_samples;
		uint file_bytes;
		uint slice_size;

		if( Avi->audio_chunk_size == 0 || Avi->audio_chunk_offset >= Avi->audio_chunk_size )
		{
			byte header[8];
			uint chunk_size;

			Avi->audio_chunk_size = 0;
			Avi->audio_chunk_offset = 0;

			if( chunk->size == 0 || chunk->size > Avi->chunk_capacity )
			{
				Avi->audio_current_chunk++;
				continue;
			}

			if( FS_Seek( audio_file, chunk->offset, SEEK_SET ) == -1 ||
				FS_Read( audio_file, header, sizeof( header )) != sizeof( header ))
				break;

			if( header[2] != 'w' || header[3] != 'b' )
			{
#if XASH_GAMECUBE
				Con_Reportf( "Xash3D GameCube: intro AVI unsupported audio chunk %c%c%c%c pos=%ld\n",
					header[0], header[1], header[2], header[3], (long)chunk->offset );
#endif
				Avi->audio_current_chunk++;
				continue;
			}

			chunk_size = AVI_RL32( header + 4 );
			if( chunk_size == 0 || chunk_size > Avi->chunk_capacity ||
				FS_Read( audio_file, Avi->audio_chunk, chunk_size ) != (fs_offset_t)chunk_size )
				break;

			Avi->audio_chunk_size = chunk_size;
		}

		buffer_samples = target_ahead - ( ch->s_rawend - snd.soundtime );
		file_samples = buffer_samples * ((float)Avi->audio_rate / SOUND_DMA_SPEED);
		if( file_samples <= 1 )
			return;
		file_bytes = file_samples * bytes_per_sample;

		slice_size = Avi->audio_chunk_size - Avi->audio_chunk_offset;
#if XASH_GAMECUBE
		if( slice_size > GC_AVI_AUDIO_SLICE_BYTES )
			slice_size = GC_AVI_AUDIO_SLICE_BYTES;
#endif
		if( slice_size > file_bytes )
			slice_size = file_bytes;
		slice_size -= slice_size % bytes_per_sample;
		if( slice_size == 0 )
		{
			Avi->audio_current_chunk++;
			Avi->audio_chunk_size = 0;
			Avi->audio_chunk_offset = 0;
			continue;
		}

		S_RawEntSamples( S_RAW_SOUND_SOUNDTRACK, slice_size / bytes_per_sample,
			Avi->audio_rate, Avi->audio_width, Avi->audio_channels,
			Avi->audio_chunk + Avi->audio_chunk_offset, movie_vol, ATTN_NONE );
		Avi->audio_bytes_submitted += slice_size;
		Avi->audio_chunk_offset += slice_size;
		if( Avi->audio_chunk_offset >= Avi->audio_chunk_size )
		{
			Avi->audio_current_chunk++;
			Avi->audio_chunk_size = 0;
			Avi->audio_chunk_offset = 0;
		}
#if XASH_GAMECUBE
		if( !Avi->audio_reported )
		{
			Con_Reportf( "Xash3D GameCube: intro AVI audio queued bytes=%u rate=%u raw=%u/%u dma=%u\n",
				Avi->audio_bytes_submitted, Avi->audio_rate,
				ch->s_rawend - snd.soundtime, target_ahead, SOUND_DMA_SPEED );
			Avi->audio_reported = true;
		}
#endif
	}
}

qboolean AVI_IsSoundtrackActive( void )
{
#if XASH_GAMECUBE
	movie_state_t *Avi = AVI_GetState( CIN_MAIN );

	if( !Avi || !Avi->active || Avi->paused )
		return false;

	return AVI_HaveAudioTrack( Avi );
#else
	return false;
#endif
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	string safe_filename;
#if XASH_GAMECUBE
	string gc_fallback;
#endif

	if( !Avi )
		return;

	if( !filename || !filename[0] )
	{
		if( !quiet )
			Con_Printf( S_ERROR "Couldn't open intro video (empty path)\n" );
		return;
	}

	if( Avi->active )
		AVI_CloseVideo( Avi );

	Avi->ui_logo = !load_audio;

	safe_filename[0] = '\0';
	Q_strncpy( safe_filename, filename, sizeof( safe_filename ));

	/* Static-hold .gcvid companions are the lean MEM1 path. Native Cinepak AVI
	 * remains supported as fallback when no companion is present. */
	if( AVI_OpenGCVID( Avi, safe_filename, true ))
	{
		if( load_audio )
			AVI_AttachAudioFromAVI( Avi, safe_filename );
		Avi->x = 0;
		Avi->y = 0;
		Avi->w = -1;
		Avi->h = -1;
		Avi->texture = 0;
		Avi->current_frame = (uint)-1;
		Avi->start_time = 0;
		Avi->playback_started = false;
		Avi->audio_playback_started = false;
		Avi->frame_on_gpu = false;
		Avi->debug_think_calls = 0;
		Avi->paused = false;
		Avi->active = true;
		if( AVI_DecodeFrame( Avi, 0, true ))
			Avi->current_frame = 0;
		return;
	}

	Avi->file = FS_Open( safe_filename, "rb", false );
#if XASH_GAMECUBE
	if( !Avi->file && !Q_strncmp( safe_filename, "media/", 6 ))
	{
		Q_snprintf( gc_fallback, sizeof( gc_fallback ), "valve/%s", safe_filename );
		if( AVI_OpenGCVID( Avi, gc_fallback, true ))
		{
			if( load_audio )
				AVI_AttachAudioFromAVI( Avi, gc_fallback );
			Avi->x = 0;
			Avi->y = 0;
			Avi->w = -1;
			Avi->h = -1;
			Avi->texture = 0;
			Avi->current_frame = (uint)-1;
			Avi->start_time = 0;
			Avi->playback_started = false;
			Avi->audio_playback_started = false;
			Avi->frame_on_gpu = false;
			Avi->debug_think_calls = 0;
			Avi->paused = false;
			Avi->active = true;
			if( AVI_DecodeFrame( Avi, 0, true ))
				Avi->current_frame = 0;
			Con_Reportf( "Xash3D GameCube: intro GCVID opened via root fallback %s\n", gc_fallback );
			return;
		}
		Avi->file = FS_Open( gc_fallback, "rb", false );
		if( Avi->file )
		{
			Q_strncpy( safe_filename, gc_fallback, sizeof( safe_filename ));
			Con_Reportf( "Xash3D GameCube: intro AVI opened via root fallback %s\n", gc_fallback );
		}
	}
#endif
	if( Avi->file && AVI_ParseHeader( Avi, quiet ))
	{
		Con_Reportf( "Xash3D GameCube: intro AVI opened %s (%ux%u source, %ux%u upload, %u frames)\n",
			safe_filename, Avi->width, Avi->height, Avi->upload_width, Avi->upload_height, Avi->frame_count );
		Avi->x = 0;
		Avi->y = 0;
		Avi->w = -1;
		Avi->h = -1;
		Avi->texture = 0;
		Avi->current_frame = (uint)-1;
		Avi->start_time = 0;
		Avi->playback_started = false;
		Avi->audio_playback_started = false;
		Avi->frame_on_gpu = false;
		Avi->debug_think_calls = 0;
		Avi->paused = false;
		Avi->active = true;
		if( AVI_DecodeFrame( Avi, 0, true ))
			Avi->current_frame = 0;
		return;
	}

	if( Avi->file )
	{
		FS_Close( Avi->file );
		Avi->file = NULL;
	}
	AVI_ClearAudioState( Avi );
#if XASH_GAMECUBE
	if( Avi->decoder.rgb )
	{
		Mem_Free( Avi->decoder.rgb );
		Avi->decoder.rgb = NULL;
	}
	memset( &Avi->decoder, 0, sizeof( Avi->decoder ));
	if( Avi->frame && !AVI_UsesStaticFrameBuffer( Avi ))
		Mem_Free( Avi->frame );
#else
	Cinepak_Free( &Avi->decoder );
	if( Avi->frame )
		Mem_Free( Avi->frame );
#endif
	Avi->frame = NULL;
	Avi->upload_frame = NULL;
	if( Avi->index )
		Mem_Free( Avi->index );
	Avi->index = NULL;
	if( Avi->chunk )
		Mem_Free( Avi->chunk );
	Avi->chunk = NULL;
	if( !quiet )
		Con_Printf( S_ERROR "Couldn't open intro video %s\n", safe_filename );
}

void AVI_CloseVideo( movie_state_t *Avi )
{
	if( !Avi )
		return;

#if XASH_GAMECUBE
	/*
	 * The GameCube intro path only opens a handful of AVI states during boot.
	 * We currently prefer preserving those decode buffers over returning them to
	 * the CRT heap, because Dolphin is reporting an invalid write inside _free_r
	 * during the intro-to-menu transition. Keeping the buffers alive avoids that
	 * fragile cleanup edge while we stabilize the boot flow.
	 */
	if( Avi->file )
		FS_Close( Avi->file );
	if( Avi->audio_file )
		FS_Close( Avi->audio_file );
	Avi->file = NULL;
	Avi->audio_file = NULL;
	Avi->active = false;
	Avi->paused = false;
	Avi->playback_started = false;
	Avi->audio_playback_started = false;
	Avi->frame_on_gpu = false;
	Avi->current_frame = (uint)-1;
	Avi->audio_current_chunk = 0;
	Avi->audio_chunk_size = 0;
	Avi->audio_chunk_offset = 0;
	Avi->audio_rate = 0;
	Avi->audio_width = 0;
	Avi->audio_channels = 0;
	Avi->audio_bytes_submitted = 0;
	Avi->audio_reported = false;
	Avi->audio_playback_started = false;
	Avi->audio_channel_ready = false;
	Avi->debug_think_calls = 0;
	Avi->ui_logo = false;
	return;
#else
	if( Avi->file )
		FS_Close( Avi->file );
	if( Avi->audio_file )
		FS_Close( Avi->audio_file );
	if( Avi->frame )
	{
#if XASH_GAMECUBE
		if( !AVI_UsesStaticFrameBuffer( Avi ))
#endif
			Mem_Free( Avi->frame );
	}
	if( Avi->upload_frame && Avi->upload_frame != Avi->frame )
		Mem_Free( Avi->upload_frame );
	if( Avi->chunk )
		Mem_Free( Avi->chunk );
	if( Avi->audio_chunk )
		Mem_Free( Avi->audio_chunk );
	if( Avi->index )
		Mem_Free( Avi->index );
	if( Avi->audio_index )
		Mem_Free( Avi->audio_index );
	Cinepak_Free( &Avi->decoder );

	memset( Avi, 0, sizeof( *Avi ));
#endif
}

qboolean AVI_Think( movie_state_t *Avi )
{
	uint target_frame;
	qboolean need_upload;
	const byte *upload_pixels;
	pixformat_t upload_fmt;
	double elapsed;

	if( !Avi || !Avi->active || !Avi->file )
		return false;

	if( Avi->paused )
		return true;

#if XASH_GAMECUBE
	Avi->debug_think_calls++;
#endif

	if( AVI_HaveAudioTrack( Avi ) )
		AVI_StreamAudio( Avi );

	if( !Avi->playback_started )
	{
		/* Hold frame 0 on screen while boot finishes; clock starts after first present. */
		target_frame = Avi->current_frame == (uint)-1 ? 0 : Avi->current_frame;
	}
	else
	{
		elapsed = Platform_DoubleTime() - Avi->start_time;
		target_frame = (uint)( elapsed * (double)Avi->fps_num / (double)Avi->fps_den );
#if XASH_GAMECUBE
		/* Prefer wall-clock pacing on GameCube (Cinepak and GCVID). If we fall
		 * behind, jump forward instead of stretching the intro into a slideshow. */
#else
		if( target_frame > Avi->current_frame + 1 )
			target_frame = Avi->current_frame + 1;
#endif
	}

#if XASH_GAMECUBE
	if( Avi->debug_think_calls <= 8 )
	{
		Con_Reportf( "Xash3D GameCube: intro AVI think call=%u started=%u current=%u target=%u start=%.3f now=%.3f\n",
			Avi->debug_think_calls, Avi->playback_started ? 1u : 0u, Avi->current_frame,
			target_frame, Avi->start_time, Platform_DoubleTime() );
	}
	if( Avi->playback_started &&
		(( Avi->current_frame < 15 && target_frame >= 15 ) ||
		 ( Avi->current_frame < 30 && target_frame >= 30 ) ||
		 ( Avi->current_frame < 60 && target_frame >= 60 ) ||
		 ( Avi->current_frame < 120 && target_frame >= 120 )))
	{
		Con_Reportf( "Xash3D GameCube: intro AVI progress frame=%u/%u elapsed=%.2f\n",
			target_frame, Avi->frame_count, elapsed );
	}
#endif

	if( target_frame >= Avi->frame_count )
	{
#if XASH_GAMECUBE
		Con_Reportf( "Xash3D GameCube: intro AVI reached end frame=%u/%u elapsed=%.2f\n",
			target_frame, Avi->frame_count, elapsed );
#endif
		return false;
	}

	need_upload = false;
	if( Avi->raw_static_frame && Avi->frame_on_gpu && Avi->current_frame != (uint)-1 )
	{
		Avi->current_frame = target_frame;
	}
	else if( target_frame != Avi->current_frame )
	{
		if( !AVI_DecodeFrame( Avi, target_frame, true ))
			return false;
		Avi->current_frame = target_frame;
		need_upload = true;
	}
	else if( !Avi->frame_on_gpu )
		need_upload = true;

	if( need_upload )
	{
		upload_pixels = Avi->frame;
		upload_fmt = Avi->raw_rgb565 ? PF_RGB_565 : PF_BGRA_32;

		if( Avi->texture == 0 )
			ref.dllFuncs.GL_UpdateTexture( SCR_GetCinematicTexture(), Avi->upload_width, Avi->upload_height,
				Avi->upload_width, Avi->upload_height, upload_pixels, upload_fmt );
		else if( Avi->texture > 0 )
			ref.dllFuncs.GL_UpdateTexture( Avi->texture, Avi->upload_width, Avi->upload_height,
				Avi->upload_width, Avi->upload_height, upload_pixels, upload_fmt );

		Avi->frame_on_gpu = true;
#if XASH_GAMECUBE
		if( AVI_HaveAudioTrack( Avi ) && !snd.streaming )
		{
			S_StartStreaming();
			AVI_BeginAudioPlayback( Avi );
			Con_Reportf( "Xash3D GameCube: intro AVI audio start synced to first uploaded frame\n" );
		}
		if( target_frame == 0 )
		{
			Con_Reportf( "Xash3D GameCube: intro AVI decoded first frame\n" );
			GC_ReportBootPhase( GC_BOOT_INTRO );
		}
		if( target_frame <= 2 || target_frame == 15 || target_frame == 30 || target_frame == 60 )
		{
			Con_Reportf( "Xash3D GameCube: intro AVI uploaded frame=%u upload=%dx%d\n",
				target_frame, Avi->upload_width, Avi->upload_height );
		}
#endif
	}
	if( Avi->texture == 0 )
	{
		int w = Avi->w >= 0 ? Avi->w : refState.width;
		int h = Avi->h >= 0 ? Avi->h : refState.height;
		ref.dllFuncs.R_DrawStretchPic( Avi->x, Avi->y, w, h, 0, 0, 1, 1, SCR_GetCinematicTexture() );
	}

	if( !Avi->playback_started )
	{
		Avi->start_time = Platform_DoubleTime();
		Avi->playback_started = true;
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
			Avi->current_frame = (uint)-1;
			Avi->start_time = 0;
			Avi->playback_started = false;
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

	Con_Reportf( "AVI: GameCube Cinepak AVI\n" );
	avi_mempool = Mem_AllocPool( "AVI Zone" );
	avi_initialized = true;
	return true;
}

void AVI_Shutdown( void )
{
	Mem_FreePool( &avi_mempool );
	avi_initialized = false;
}

movie_state_t *AVI_LoadVideo( const char *filename, qboolean load_audio )
{
	movie_state_t *Avi;
	string path;

	if( !avi_initialized )
		return NULL;

	Q_snprintf( path, sizeof( path ), "media/%s", filename );
	COM_DefaultExtension( path, ".avi", sizeof( path ));

	Avi = Mem_Calloc( avi_mempool, sizeof( movie_state_t ));
	AVI_OpenVideo( Avi, path, load_audio, false );
	if( !AVI_IsActive( Avi ))
	{
		AVI_FreeVideo( Avi );
		return NULL;
	}

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
