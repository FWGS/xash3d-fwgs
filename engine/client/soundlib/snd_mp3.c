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
#include "libmpg/libmpg.h"

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
			if( value_len <= 0 || value_len >= sizeof( value ))
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

#if XASH_ENGINE_TESTS
int EXPORT Fuzz_Sound_ParseID3Tag( const uint8_t *Data, size_t Size );
int EXPORT Fuzz_Sound_ParseID3Tag( const uint8_t *Data, size_t Size )
{
	memset( &sound, 0, sizeof( sound ));
	Sound_ParseID3Tag( Data, Size );
	return 0;
}
#endif

/*
=================================================================

	MPEG decompression

=================================================================
*/
qboolean Sound_LoadMPG( const char *name, const byte *buffer, fs_offset_t filesize )
{
	void	*mpeg;
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
	sound.size = ( sound.channels * sound.rate * sound.width ) * ( sc.playtime / 1000 ); // in bytes
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
	void	*mpeg;
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
	void	*mpg;

	mpg = stream->ptr;

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
