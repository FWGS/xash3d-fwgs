/*
snd_mp3.c - mp3 format loading and streaming
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "soundlib.h"

#define MPG123_PORTABLE_API 1
#include <mpg123.h>

#pragma pack( push, 1 )
typedef struct did3v2_header_s
{
	char     ident[3];  // must be "ID3"
	uint8_t  major_ver; // must be 4
	uint8_t  minor_ver; // must be 0
	uint8_t  flags;
	uint32_t length; // size of extended header, padding and frames
} did3v2_header_t;
STATIC_CHECK_SIZEOF( did3v2_header_t, 10, 10 );

typedef struct did3v2_extended_header_s
{
	uint32_t length;
	uint8_t  flags_length;
	uint8_t  flags[1];
} did3v2_extended_header_t;
STATIC_CHECK_SIZEOF( did3v2_extended_header_t, 6, 6 );

typedef struct did3v2_frame_s
{
	char     frame_id[4];
	uint32_t length;
	uint8_t  flags[2];
} did3v2_frame_t;
STATIC_CHECK_SIZEOF( did3v2_frame_t, 10, 10 );
#pragma pack( pop )

typedef enum did3v2_header_flags_e
{
	ID3V2_HEADER_UNSYHCHRONIZATION = BIT( 7U ),
	ID3V2_HEADER_EXTENDED_HEADER   = BIT( 6U ),
	ID3V2_HEADER_EXPERIMENTAL      = BIT( 5U ),
	ID3V2_HEADER_FOOTER_PRESENT    = BIT( 4U ),
} did3v2_header_flags_t;

#define CHECK_IDENT( ident, b0, b1, b2 )        ((( ident )[0]) == ( b0 ) && (( ident )[1]) == ( b1 ) && (( ident )[2]) == ( b2 ))
#define CHECK_FRAME_ID( ident, b0, b1, b2, b3 ) ( CHECK_IDENT( ident, b0, b1, b2 ) && (( ident )[3]) == ( b3 ))

static uint32_t Sound_ParseSynchInteger( uint32_t v )
{
	uint32_t res = 0;

	// read as big endian
	res |= (( v >> 24 ) & 0x7f ) << 0;
	res |= (( v >> 16 ) & 0x7f ) << 7;
	res |= (( v >> 8  ) & 0x7f ) << 14;
	res |= (( v >> 0  ) & 0x7f ) << 21;

	return res;
}

static void Sound_HandleCustomID3Comment( const char *key, const char *value )
{
	if( !Q_strcmp( key, "LOOP_START" ) || !Q_strcmp( key, "LOOPSTART" ))
	{
		sound.loopstart = Q_atoi( value );
		SetBits( sound.flags, SOUND_LOOPED );
	}
	// unknown comment is not an error
}

static qboolean Sound_ParseID3Frame( const did3v2_frame_t *frame, const byte *buffer, size_t frame_length )
{
	if( CHECK_FRAME_ID( frame->frame_id, 'T', 'X', 'X', 'X' ))
	{
		string key, value;
		int32_t key_len, value_len;

		if( buffer[0] == 0x00 || buffer[0] == 0x03 )
		{
			key_len = Q_strncpy( key, &buffer[1], sizeof( key ));
			value_len = frame_length - (1 + key_len + 1);
			if( value_len <= 0 || value_len >= sizeof( value ) - 1 )
			{
				Con_Printf( S_ERROR "%s: invalid TXXX description, possibly broken file.\n", __func__ );
				return false;
			}

			memcpy( value, &buffer[1 + key_len + 1], value_len );
			value[value_len + 1] = 0;

			Sound_HandleCustomID3Comment( key, value );
		}
		else
		{
			if( buffer[0] == 0x01 || buffer[0] == 0x02 ) // UTF-16 with BOM
				Con_Printf( S_ERROR "%s: UTF-16 encoding is unsupported. Use UTF-8 or ISO-8859!\n", __func__ );
			else
				Con_Printf( S_ERROR "%s: unknown TXXX tag encoding %d, possibly broken file.\n", __func__, buffer[0] );
			return false;
		}
	}

	return true;
}

static qboolean Sound_ParseID3Tag( const byte *buffer, fs_offset_t filesize )
{
	const did3v2_header_t *header = (const did3v2_header_t *)buffer;
	const byte *buffer_begin = buffer;
	uint32_t tag_length;

	if( filesize < sizeof( *header ))
		 return false;

	buffer += sizeof( *header );

	// support only id3v2
	if( !CHECK_IDENT( header->ident, 'I', 'D', '3' ))
	{
		// old id3v1 header found
		if( CHECK_IDENT( header->ident, 'T', 'A', 'G' ))
			Con_Printf( S_ERROR "%s: ID3v1 is not supported! Convert to ID3v2.4!\n", __func__ );

		return true; // missing tag header is not an error
	}

	// support only latest id3 v2.4
	if( header->major_ver != 4 || header->minor_ver == 0xff )
	{
		Con_Printf( S_ERROR "%s: invalid ID3v2 tag version 2.%d.%d. Convert to ID3v2.4!\n", __func__, header->major_ver, header->minor_ver );
		return false;
	}

	tag_length = Sound_ParseSynchInteger( header->length );
	if( tag_length > filesize - sizeof( *header ))
	{
		Con_Printf( S_ERROR "%s: invalid tag length %u, possibly broken file.\n", __func__, tag_length );
		return false;
	}

	// just skip extended header
	if( FBitSet( header->flags, ID3V2_HEADER_EXTENDED_HEADER ))
	{
		const did3v2_extended_header_t *ext_header = (const did3v2_extended_header_t *)buffer;
		uint32_t ext_length = Sound_ParseSynchInteger( ext_header->length );

		if( ext_length > tag_length )
		{
			Con_Printf( S_ERROR "%s: invalid extended header length %u, possibly broken file.\n", __func__, ext_length );
			return false;
		}

		buffer += ext_length;
	}

	while( buffer - buffer_begin < tag_length )
	{
		const did3v2_frame_t *frame = (const did3v2_frame_t *)buffer;
		uint32_t frame_length = Sound_ParseSynchInteger( frame->length );

		if( frame_length > tag_length )
		{
			Con_Printf( S_ERROR "%s: invalid frame length %u, possibly broken file.\n", __func__, frame_length );
			return false;
		}

		buffer += sizeof( *frame );

		// parse can fail, but it's ok to continue
		Sound_ParseID3Frame( frame, buffer, frame_length );

		buffer += frame_length;
	}

	return true;
}

/*
=================================================================

	MPEG decompression

=================================================================
*/
// error codes
#define MP3_ERR       -1
#define MP3_OK        0
#define MP3_NEED_MORE 1

typedef struct
{
	int rate;     // num samples per second (e.g. 11025 - 11 khz)
	int channels; // num channels (1 - mono, 2 - stereo)
	int playtime; // stream size in milliseconds
} wavinfo_t;

#ifdef _MSC_VER // a1ba: MSVC6 don't have ssize_t
typedef long mpg_ssize_t;
#else
typedef ssize_t mpg_ssize_t;
#endif

// custom stdio
typedef mpg_ssize_t (*pfread)( void *handle, void *buf, size_t count );
typedef fs_offset_t (*pfseek)( void *handle, fs_offset_t offset, int whence );

typedef struct mpg_decoder_s
{
	mpg123_handle *mh;
	void *file;
	pfread f_read;
	pfseek f_seek;
} mpg_decoder_t;

static int MPG_Read( void *handle, void *buf, size_t count, size_t *got )
{
	mpg_decoder_t *mpg = handle;
	mpg_ssize_t ret = mpg->f_read( mpg->file, buf, count );

	if( ret < 0 )
		return -1;

	*got = ret;
	return 0;
}

static int64_t MPG_Seek( void *handle, int64_t offset, int whence )
{
	mpg_decoder_t *mpg = handle;

	return mpg->f_seek( mpg->file, offset, whence );
}

static int MPG_GetFormat( mpg123_handle *mh, fs_offset_t streamsize, wavinfo_t *sc )
{
	long rate;
	int channels, encoding;

	if( mpg123_getformat( mh, &rate, &channels, &encoding ) != MPG123_OK )
		return 0;

	mpg123_format_none( mh );
	mpg123_format( mh, rate, channels, MPG123_ENC_SIGNED_16 );

	sc->rate = rate;
	sc->channels = channels;
	sc->playtime = 0;

	int64_t length = mpg123_length64( mh );
	if( length > 0 && rate > 0 )
	{
		int64_t playtime = ( length * 1000 + rate - 1 ) / rate;

		// length can come from Xing header, don't let it claim more than
		// the lowest MP3 bitrate (8 kbit/s, one byte per millisecond) allows
		if( streamsize > 0 && playtime > streamsize )
			playtime = streamsize;

		sc->playtime = Q_min( playtime, INT_MAX );
	}

	return 1;
}

static mpg_decoder_t *create_decoder( int *error )
{
	mpg_decoder_t *mpg;
	int ret;

	if( error )
		*error = 0;

	mpg = Mem_Calloc( host.soundpool, sizeof( *mpg ));
	mpg->mh = mpg123_new( NULL, &ret );

	if( !mpg->mh )
	{
		Mem_Free( mpg );
		return NULL;
	}

	ret = mpg123_param2( mpg->mh, MPG123_FLAGS, MPG123_FUZZY | MPG123_SEEKBUFFER | MPG123_GAPLESS | MPG123_QUIET, 0.0 );
	if( ret != MPG123_OK && error )
		*error = 1;

	// let the seek index auto-grow and contain an entry for every frame
	ret = mpg123_param2( mpg->mh, MPG123_INDEX_SIZE, -1, 0.0 );
	if( ret != MPG123_OK && error )
		*error = 1;

	return mpg;
}

static int feed_mpeg_header( mpg_decoder_t *mpg, const byte *data, long bufsize, long streamsize, wavinfo_t *sc )
{
	size_t done;

	if( !mpg || !sc )
		return 0;

	if( mpg123_open_feed( mpg->mh ) != MPG123_OK )
		return 0;

	mpg123_set_filesize64( mpg->mh, streamsize );

	// feed input chunk and get first chunk of decoded audio.
	if( mpg123_decode( mpg->mh, data, bufsize, NULL, 0, &done ) != MPG123_NEW_FORMAT )
		return 0; // there were errors

	return MPG_GetFormat( mpg->mh, streamsize, sc );
}

static int feed_mpeg_stream( mpg_decoder_t *mpg, const byte *data, long bufsize, byte *outbuf, size_t *outsize )
{
	switch( mpg123_decode( mpg->mh, data, bufsize, outbuf, OUTBUF_SIZE, outsize ))
	{
	case MPG123_NEED_MORE:
		return MP3_NEED_MORE;
	case MPG123_OK:
		return MP3_OK;
	default:
		return MP3_ERR;
	}
}

static int open_mpeg_stream( mpg_decoder_t *mpg, void *file, pfread f_read, pfseek f_seek, wavinfo_t *sc )
{
	if( !mpg || !sc )
		return 0;

	mpg->file = file;
	mpg->f_read = f_read;
	mpg->f_seek = f_seek;

	if( mpg123_reader64( mpg->mh, MPG_Read, MPG_Seek, NULL ) != MPG123_OK )
		return 0;

	if( mpg123_open_handle64( mpg->mh, mpg ) != MPG123_OK )
		return 0;

	return MPG_GetFormat( mpg->mh, -1, sc );
}

static int read_mpeg_stream( mpg_decoder_t *mpg, byte *outbuf, size_t *outsize )
{
	switch( mpg123_read( mpg->mh, outbuf, OUTBUF_SIZE, outsize ))
	{
	case MPG123_OK:
		return MP3_OK;
	default:
		return MP3_ERR;
	}
}

static int get_stream_pos( mpg_decoder_t *mpg )
{
	return mpg123_tell64( mpg->mh );
}

static int set_stream_pos( mpg_decoder_t *mpg, int curpos )
{
	return mpg123_seek64( mpg->mh, curpos, SEEK_SET );
}

static void close_decoder( mpg_decoder_t *mpg )
{
	if( !mpg )
		return;

	mpg123_delete( mpg->mh );
	Mem_Free( mpg );
}

static const char *get_error( mpg_decoder_t *mpg )
{
	if( !mpg )
		return mpg123_plain_strerror( MPG123_BAD_HANDLE );

	return mpg123_strerror( mpg->mh );
}

qboolean Sound_LoadMPG( const char *name, const byte *buffer, fs_offset_t filesize )
{
	mpg_decoder_t *mpeg;
	size_t	pos = 0;
	size_t	bytesWrite = 0;
	byte	out[OUTBUF_SIZE];
	size_t	outsize, padsize;
	int	ret;
	wavinfo_t	sc;

	// load the file
	if( !buffer || filesize < FRAME_SIZE )
		return false;

	// couldn't create decoder
	if(( mpeg = create_decoder( &ret )) == NULL )
		return false;

	if( ret ) Con_DPrintf( S_ERROR "%s\n", get_error( mpeg ));

	// trying to read header
	if( !feed_mpeg_header( mpeg, buffer, FRAME_SIZE, filesize, &sc ))
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): %s\n", __func__, name, get_error( mpeg ));
		close_decoder( mpeg );
		return false;
	}

	sound.channels = sc.channels;
	sound.rate = sc.rate;
	sound.width = 2; // always 16-bit PCM
	sound.size = (int64_t)sound.channels * sound.rate * sound.width * sc.playtime / 1000; // in bytes
	padsize = sound.size % FRAME_SIZE;
	pos += FRAME_SIZE; // evaluate pos

	if( !Sound_ParseID3Tag( buffer, filesize ))
	{
		Con_DPrintf( S_WARN "%s: (%s) failed to extract LOOP_START tag\n", __func__, name );
	}

	if( !sound.size )
	{
		// bad mpeg file ?
		Con_DPrintf( S_ERROR "%s: (%s) is probably corrupted\n", __func__, name );
		close_decoder( mpeg );
		return false;
	}

	// add sentinel make sure we not overrun
	sound.wav = (byte *)Mem_Calloc( host.soundpool, sound.size + padsize );
	sound.type = WF_PCMDATA;

	// decompress mpg into pcm wav format
	while( bytesWrite < sound.size )
	{
		int	size;

		if( feed_mpeg_stream( mpeg, NULL, 0, out, &outsize ) != MP3_OK && outsize <= 0 )
		{
			const byte *data = buffer + pos;
			int	bufsize;

			// if there are no bytes remainig so we can decompress the new frame
			if( pos + FRAME_SIZE > filesize )
				bufsize = ( filesize - pos );
			else bufsize = FRAME_SIZE;
			pos += bufsize;

			if( feed_mpeg_stream( mpeg, data, bufsize, out, &outsize ) != MP3_OK )
				break; // there was end of the stream
		}

		if( bytesWrite + outsize > sound.size )
			size = ( sound.size - bytesWrite );
		else size = outsize;

		memcpy( &sound.wav[bytesWrite], out, size );
		bytesWrite += size;
	}

	sound.samples = bytesWrite / ( sound.width * sound.channels );
	close_decoder( mpeg );

	return true;
}

static fs_offset_t FS_SeekMpg( void *file, fs_offset_t offset, int whence )
{
	return g_fsapi.Seek((file_t *)file, offset, whence ) == -1 ? -1 : g_fsapi.Tell((file_t *)file );
}

static mpg_ssize_t FS_ReadMpg( void *file, void *buf, size_t count )
{
	return g_fsapi.Read((file_t *)file, buf, count );
}

/*
=================
Stream_OpenMPG
=================
*/
stream_t *Stream_OpenMPG( const char *filename )
{
	stream_t	*stream;
	mpg_decoder_t *mpeg;
	file_t	*file;
	int	ret;
	wavinfo_t	sc;

	file = FS_Open( filename, "rb", false );
	if( !file ) return NULL;

	// at this point we have valid stream
	stream = Mem_Calloc( host.soundpool, sizeof( stream_t ));
	stream->file = file;
	stream->pos = 0;

	// couldn't create decoder
	if(( mpeg = create_decoder( &ret )) == NULL )
	{
		Con_DPrintf( S_ERROR "%s: couldn't create decoder: %s\n", __func__, get_error( mpeg ) );
		Mem_Free( stream );
		FS_Close( file );
		return NULL;
	}

	if( ret ) Con_DPrintf( S_ERROR "%s\n", get_error( mpeg ));

	// trying to open stream and read header
	if( !open_mpeg_stream( mpeg, file, FS_ReadMpg, FS_SeekMpg, &sc ))
	{
		Con_DPrintf( S_ERROR "%s: failed to load (%s): %s\n", __func__, filename, get_error( mpeg ));
		close_decoder( mpeg );
		Mem_Free( stream );
		FS_Close( file );

		return NULL;
	}

	stream->buffsize = 0; // how many samples left from previous frame
	stream->channels = sc.channels;
	stream->rate = sc.rate;
	stream->width = 2;	// always 16 bit
	stream->ptr = mpeg;
	stream->type = WF_MPGDATA;

	return stream;
}

/*
=================
Stream_ReadMPG

assume stream is valid
=================
*/
int Stream_ReadMPG( stream_t *stream, int needBytes, void *buffer )
{
	// buffer handling
	int	bytesWritten = 0;
	mpg_decoder_t *mpg = stream->ptr;

	while( 1 )
	{
		byte	*data;
		int	outsize;

		if( !stream->buffsize )
		{
			if( read_mpeg_stream( mpg, (byte*)stream->temp, &stream->pos ) != MP3_OK )
				break; // there was end of the stream
		}

		// check remaining size
		if( bytesWritten + stream->pos > needBytes )
			outsize = ( needBytes - bytesWritten );
		else outsize = stream->pos;

		// copy raw sample to output buffer
		data = (byte *)buffer + bytesWritten;
		memcpy( data, &stream->temp[stream->buffsize], outsize );
		bytesWritten += outsize;
		stream->pos -= outsize;
		stream->buffsize += outsize;

		// continue from this sample on a next call
		if( bytesWritten >= needBytes )
			return bytesWritten;

		stream->buffsize = 0; // no bytes remaining
	}

	return 0;
}

/*
=================
Stream_SetPosMPG

assume stream is valid
=================
*/
int Stream_SetPosMPG( stream_t *stream, int newpos )
{
	if( set_stream_pos( stream->ptr, newpos ) != -1 )
	{
		// flush any previous data
		stream->buffsize = 0;
		return true;
	}

	// failed to seek for some reasons
	return false;
}

/*
=================
Stream_GetPosMPG

assume stream is valid
=================
*/
int Stream_GetPosMPG( stream_t *stream )
{
	return get_stream_pos( stream->ptr );
}

/*
=================
Stream_FreeMPG

assume stream is valid
=================
*/
void Stream_FreeMPG( stream_t *stream )
{
	if( stream->ptr )
	{
		close_decoder( stream->ptr );
		stream->ptr = NULL;
	}

	if( stream->file )
	{
		FS_Close( stream->file );
		stream->file = NULL;
	}

	Mem_Free( stream );
}

#if XASH_LLVM_LIBFUZZER
int EXPORT Fuzz_Sound_ParseID3Tag( const uint8_t *Data, size_t Size );
int EXPORT Fuzz_Sound_ParseID3Tag( const uint8_t *Data, size_t Size )
{
	memset( &sound, 0, sizeof( sound ));
	Sound_ParseID3Tag( Data, Size );
	return 0;
}
#endif // XASH_LLVM_LIBFUZZER
