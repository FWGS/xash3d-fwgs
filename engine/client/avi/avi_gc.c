/*
avi_gc.c - GameCube retail AVI/Cinepak movie playback
Copyright (C) 2026 Xash3D GameCube port contributors
*/
#include "defaults.h"
#include "common.h"
#include "client.h"
#include "avi_cinepak.h"
#include "avi_gc.h"

extern poolhandle_t avi_mempool;

#define AVI_RL32( p ) ((uint32_t)((p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24)))
#define AVI_RL16( p ) ((uint16_t)((p)[0] | ((uint16_t)(p)[1] << 8)))

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

static qboolean AVI_ParseHeader( movie_state_t *Avi, qboolean quiet )
{
	byte avih[56];
	byte riff[12];
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
		if( !quiet ) Con_Printf( S_ERROR "Intro video is not an AVI file\n" );
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

	if( FS_Seek( Avi->file, idx_pos + 4, SEEK_SET ) == -1 )
		return false;
	if( FS_Read( Avi->file, &idx_size, sizeof( idx_size )) != sizeof( idx_size ))
		return false;

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
	for( i = 0; i < entries; i++ )
	{
		const byte *entry = idx_data + i * 16;
		if( entry[0] == '0' && entry[1] == '0' && entry[2] == 'd' && entry[3] == 'c' )
			Avi->frame_count++;
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

	Avi->frame_count = 0;
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
	}
	Mem_Free( idx_data );

	Avi->chunk = Mem_Malloc( avi_mempool, Avi->chunk_capacity );
	if( !Avi->chunk )
		return false;

	Avi->frame = Mem_Malloc( avi_mempool, Avi->width * Avi->height * 4 );
	if( !Avi->frame )
		return false;

	if( !Cinepak_Init( &Avi->decoder, Avi->width, Avi->height, avi_mempool ))
		return false;

	return Avi->width > 0 && Avi->height > 0;
}

static qboolean AVI_DecodeFrame( movie_state_t *Avi, uint frame )
{
	byte header[8];
	fs_offset_t pos;
	uint chunk_size;

	if( !Avi || frame >= Avi->frame_count )
		return false;

	pos = Avi->index[frame].offset;
	if( FS_Seek( Avi->file, pos, SEEK_SET ) == -1 )
		return false;
	if( FS_Read( Avi->file, header, sizeof( header )) != sizeof( header ))
		return false;
	if( header[2] != 'd' || header[3] != 'c' )
		return false;

	chunk_size = AVI_RL32( header + 4 );
	if( chunk_size < 4 || chunk_size > Avi->chunk_capacity )
		return false;

	if( FS_Read( Avi->file, Avi->chunk, chunk_size ) != (fs_offset_t)chunk_size )
		return false;

	if( !Cinepak_Decode( &Avi->decoder, Avi->chunk, chunk_size ))
		return false;

	AVI_RGB24ToBGRA( Avi->decoder.rgb, Avi->decoder.stride, Avi->frame, Avi->width, Avi->height );
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

	if((uint)frame != Avi->current_frame && !AVI_DecodeFrame( Avi, frame ))
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
	(void)Avi;
	return false;
}

void AVI_OpenVideo( movie_state_t *Avi, const char *filename, qboolean load_audio, int quiet )
{
	if( !Avi )
		return;

	if( Avi->active )
		AVI_CloseVideo( Avi );

	Avi->file = FS_Open( filename, "rb", false );
	if( !Avi->file )
	{
		if( !quiet )
			Con_Printf( S_ERROR "Couldn't open intro video %s\n", filename );
		return;
	}

	if( !AVI_ParseHeader( Avi, quiet ))
	{
		if( !quiet )
			Con_Printf( S_ERROR "Couldn't parse intro video %s\n", filename );
		AVI_CloseVideo( Avi );
		return;
	}

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
	if( Avi->chunk )
		Mem_Free( Avi->chunk );
	if( Avi->index )
		Mem_Free( Avi->index );
	Cinepak_Free( &Avi->decoder );

	memset( Avi, 0, sizeof( *Avi ));
}

qboolean AVI_Think( movie_state_t *Avi )
{
	uint target_frame;
	double elapsed;

	if( !Avi || !Avi->active || !Avi->file || !Avi->frame )
		return false;

	if( Avi->paused )
		return true;

	elapsed = Platform_DoubleTime() - Avi->start_time;
	target_frame = (uint)( elapsed * (double)Avi->fps_num / (double)Avi->fps_den );
	if( target_frame >= Avi->frame_count )
		return false;

	if( target_frame != Avi->current_frame )
	{
		if( !AVI_DecodeFrame( Avi, target_frame ))
			return false;
		Avi->current_frame = target_frame;

		if( Avi->texture == 0 )
			ref.dllFuncs.GL_UpdateTexture( SCR_GetCinematicTexture(), Avi->width, Avi->height, Avi->width, Avi->height, Avi->frame, PF_BGRA_32 );
		else if( Avi->texture > 0 )
			ref.dllFuncs.GL_UpdateTexture( Avi->texture, Avi->width, Avi->height, Avi->w, Avi->h, Avi->frame, PF_BGRA_32 );
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
