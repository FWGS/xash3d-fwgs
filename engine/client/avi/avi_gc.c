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

static qboolean avi_initialized;
static poolhandle_t avi_mempool;
static movie_state_t avi[2];

#define AVI_RL32( p ) ((uint32_t)((p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24)))
#define AVI_RL16( p ) ((uint16_t)((p)[0] | ((uint16_t)(p)[1] << 8)))

#if XASH_GAMECUBE
#define GC_AVI_AUDIO_LEAD_SEC		0.20
#define GC_AVI_AUDIO_SLICE_BYTES	4096
#define GC_AVI_MAX_CATCHUP_FRAMES	8
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

	Avi->upload_width = Avi->width;
	Avi->upload_height = Avi->height;
	Avi->frame = Mem_Malloc( avi_mempool, Avi->width * Avi->height * 4 );
	if( !Avi->frame )
		return false;
	Avi->upload_frame = Avi->frame;

	if( !Cinepak_Init( &Avi->decoder, Avi->width, Avi->height, avi_mempool ))
		return false;

	return Avi->width > 0 && Avi->height > 0;
}

static qboolean AVI_DecodeFrame( movie_state_t *Avi, uint frame, qboolean upload )
{
	byte header[8];
	fs_offset_t pos;
	uint chunk_size;

	if( !Avi || frame >= Avi->frame_count )
		return false;

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
		const byte *top = Avi->decoder.rgb;
		const byte *mid = Avi->decoder.rgb + ( Avi->height / 2 ) * Avi->decoder.stride + ( Avi->width / 2 ) * 3;
		Con_Reportf( "Xash3D GameCube: intro AVI frame %u samples rgb0=%u,%u,%u rgbmid=%u,%u,%u\n",
			frame, top[0], top[1], top[2], mid[0], mid[1], mid[2] );
	}
#endif

	if( upload )
	{
#if XASH_GAMECUBE
		if( !Avi->decoder.rgb || Avi->decoder.stride != Avi->width * 3 )
#endif
		AVI_RGB24ToBGRA( Avi->decoder.rgb, Avi->decoder.stride, Avi->frame, Avi->width, Avi->height );
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

	if( xres ) *xres = Avi->width;
	if( yres ) *yres = Avi->height;
	if( duration ) *duration = (float)((double)Avi->frame_count * (double)Avi->fps_den / (double)Avi->fps_num);
	return true;
}

qboolean AVI_HaveAudioTrack( const movie_state_t *Avi )
{
	return Avi && Avi->audio_index && Avi->audio_chunk_count > 0 &&
		Avi->audio_rate > 0 && Avi->audio_width > 0 && Avi->audio_channels > 0;
}

static void AVI_StreamAudio( movie_state_t *Avi, double elapsed )
{
	uint desired_bytes;
	uint bytes_per_sample;

	if( !AVI_HaveAudioTrack( Avi ) || !snd.initialized || elapsed < 0.0 )
		return;

	bytes_per_sample = Avi->audio_width * Avi->audio_channels;
#if XASH_GAMECUBE
	desired_bytes = (uint)(( elapsed + GC_AVI_AUDIO_LEAD_SEC ) *
		(double)Avi->audio_rate * (double)bytes_per_sample );
#else
	desired_bytes = (uint)( elapsed * (double)Avi->audio_rate * (double)bytes_per_sample );
#endif
	while( Avi->audio_current_chunk < Avi->audio_chunk_count && Avi->audio_bytes_submitted < desired_bytes )
	{
		avi_frame_index_t *chunk = &Avi->audio_index[Avi->audio_current_chunk];
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

			if( FS_Seek( Avi->file, chunk->offset, SEEK_SET ) == -1 ||
				FS_Read( Avi->file, header, sizeof( header )) != sizeof( header ))
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
				FS_Read( Avi->file, Avi->chunk, chunk_size ) != (fs_offset_t)chunk_size )
				break;

			Avi->audio_chunk_size = chunk_size;
		}

		slice_size = Avi->audio_chunk_size - Avi->audio_chunk_offset;
#if XASH_GAMECUBE
		if( slice_size > GC_AVI_AUDIO_SLICE_BYTES )
			slice_size = GC_AVI_AUDIO_SLICE_BYTES;
#endif
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
			Avi->chunk + Avi->audio_chunk_offset, 255, ATTN_NONE );
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
			Con_Reportf( "Xash3D GameCube: intro AVI audio submitted chunks=%u bytes=%u rate=%u\n",
				Avi->audio_current_chunk, Avi->audio_bytes_submitted, Avi->audio_rate );
			Avi->audio_reported = true;
		}
#endif
	}
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	string safe_filename;
#if XASH_GAMECUBE
	string gc_fallback;
#endif

	if( !Avi )
		return;

	if( Avi->active )
		AVI_CloseVideo( Avi );

	Q_strncpy( safe_filename, filename, sizeof( safe_filename ));

	Avi->file = FS_Open( safe_filename, "rb", false );
#if XASH_GAMECUBE
	if( !Avi->file && !Q_strncmp( safe_filename, "media/", 6 ))
	{
		Q_snprintf( gc_fallback, sizeof( gc_fallback ), "valve/%s", safe_filename );
		Avi->file = FS_Open( gc_fallback, "rb", false );
		if( Avi->file )
			Con_Reportf( "Xash3D GameCube: intro AVI opened via root fallback %s\n", gc_fallback );
	}
#endif
	if( !Avi->file )
	{
		if( !quiet )
			Con_Printf( S_ERROR "Couldn't open intro video %s\n", safe_filename );
		return;
	}

	if( !AVI_ParseHeader( Avi, quiet ))
	{
		if( !quiet )
			Con_Printf( S_ERROR "Couldn't parse intro video %s\n", filename );
		AVI_CloseVideo( Avi );
		return;
	}

	Con_Reportf( "Xash3D GameCube: intro AVI opened %s (%ux%u, %u frames)\n",
		safe_filename, Avi->width, Avi->height, Avi->frame_count );
	Avi->x = 0;
	Avi->y = 0;
	Avi->w = -1;
	Avi->h = -1;
	Avi->texture = 0;
	Avi->current_frame = (uint)-1;
	Avi->start_time = Platform_DoubleTime();
	Avi->paused = false;
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
	if( Avi->upload_frame && Avi->upload_frame != Avi->frame )
		Mem_Free( Avi->upload_frame );
	if( Avi->chunk )
		Mem_Free( Avi->chunk );
	if( Avi->index )
		Mem_Free( Avi->index );
	if( Avi->audio_index )
		Mem_Free( Avi->audio_index );
	Cinepak_Free( &Avi->decoder );

	memset( Avi, 0, sizeof( *Avi ));
}

qboolean AVI_Think( movie_state_t *Avi )
{
	uint target_frame;
	uint decode_frame;
	qboolean first_decode;
	const byte *upload_pixels;
	pixformat_t upload_fmt;
	double elapsed;

	if( !Avi || !Avi->active || !Avi->file )
		return false;

	if( Avi->paused )
		return true;

	if( Avi->current_frame == (uint)-1 )
	{
		target_frame = 0;
		Avi->start_time = Platform_DoubleTime();
	}
	else
	{
		elapsed = Platform_DoubleTime() - Avi->start_time;
		target_frame = (uint)( elapsed * (double)Avi->fps_num / (double)Avi->fps_den );
#if XASH_GAMECUBE
		if( target_frame > Avi->current_frame + GC_AVI_MAX_CATCHUP_FRAMES )
			target_frame = Avi->current_frame + GC_AVI_MAX_CATCHUP_FRAMES;
#endif
	}
	if( target_frame >= Avi->frame_count )
		return false;

	AVI_StreamAudio( Avi, Platform_DoubleTime() - Avi->start_time );

	if( target_frame != Avi->current_frame )
	{
		first_decode = ( Avi->current_frame == (uint)-1 );
		decode_frame = first_decode ? 0 : Avi->current_frame + 1;
#if XASH_GAMECUBE
		if( target_frame > decode_frame &&
			( decode_frame <= 3 || target_frame == 15 || target_frame == 30 || target_frame == 60 ))
		{
			Con_Reportf( "Xash3D GameCube: intro AVI catchup from=%u to=%u\n",
				decode_frame, target_frame );
		}
#endif
		for( ; decode_frame <= target_frame; decode_frame++ )
		{
			if( !AVI_DecodeFrame( Avi, decode_frame, decode_frame == target_frame ))
				return false;
		}
		Avi->current_frame = target_frame;
		if( first_decode )
			Con_Reportf( "Xash3D GameCube: intro AVI decoded first frame\n" );

#if XASH_GAMECUBE
		if( Avi->decoder.rgb && Avi->decoder.stride == Avi->width * 3 )
		{
			upload_pixels = Avi->decoder.rgb;
			upload_fmt = PF_RGB_24;
		}
		else
#endif
		{
			upload_pixels = Avi->upload_frame;
			upload_fmt = PF_BGRA_32;
		}

		if( Avi->texture == 0 )
			ref.dllFuncs.GL_UpdateTexture( SCR_GetCinematicTexture(), Avi->upload_width, Avi->upload_height,
				Avi->upload_width, Avi->upload_height, upload_pixels, upload_fmt );
		else if( Avi->texture > 0 )
			ref.dllFuncs.GL_UpdateTexture( Avi->texture, Avi->upload_width, Avi->upload_height,
				Avi->upload_width, Avi->upload_height, upload_pixels, upload_fmt );
#if XASH_GAMECUBE
		if( target_frame <= 2 || target_frame == 15 || target_frame == 30 || target_frame == 60 )
		{
			Con_Reportf( "Xash3D GameCube: intro AVI uploaded frame=%u fmt=%d upload=%dx%d\n",
				target_frame, upload_fmt, Avi->upload_width, Avi->upload_height );
		}
#endif
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
			Avi->current_frame = (uint)-1;
			Avi->start_time = Platform_DoubleTime();
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
