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
#if !XASH_PSP
#include "libmpg/libmpg.h"

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
		Con_DPrintf( S_ERROR "Sound_LoadMPG: failed to load (%s): %s\n", name, get_error( mpeg ));
		close_decoder( mpeg );
		return false;
	}

	sound.channels = sc.channels;
	sound.rate = sc.rate;
	sound.width = 2; // always 16-bit PCM
	sound.loopstart = -1;
	sound.size = ( sound.channels * sound.rate * sound.width ) * ( sc.playtime / 1000 ); // in bytes
	padsize = sound.size % FRAME_SIZE;
	pos += FRAME_SIZE; // evaluate pos

	if( !sound.size )
	{
		// bad mpeg file ?
		Con_DPrintf( S_ERROR "Sound_LoadMPG: (%s) is probably corrupted\n", name );
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
		Con_DPrintf( S_ERROR "Stream_OpenMPG: couldn't create decoder: %s\n", get_error( mpeg ) );
		Mem_Free( stream );
		FS_Close( file );
		return NULL;
	}

	if( ret ) Con_DPrintf( S_ERROR "%s\n", get_error( mpeg ));

	// trying to open stream and read header
	if( !open_mpeg_stream( mpeg, file, (void*)FS_Read, (void*)FS_Seek, &sc ))
	{
		Con_DPrintf( S_ERROR "Stream_OpenMPG: failed to load (%s): %s\n", filename, get_error( mpeg ));
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

#else // XASH_PSP
#include "platform/psp/scemp3/pspmp3.h"

/*
=================================================================

	PSP MPEG decoding

=================================================================
*/
#define MP3_ID3V1_ID		"TAG"
#define MP3_ID3V1_ID_SZ		3
#define MP3_ID3V1_SZ		128

#define MP3_ID3V2_ID		"ID3"
#define MP3_ID3V2_ID_SZ		3
#define MP3_ID3V2_SIZE_OFF	6
#define MP3_ID3V2_SIZE_SZ	4
#define MP3_ID3V2_HEADER_SZ	10

#define	MP3_ERRORS_MAX		3

#define PCM_WIDTH		2 // always 16-bit PCM
#define PCM_CHANNELS		2 // always 2

#define MP3_BUFFER_SIZE		( 128 * 1024 )
#define PCM_BUFFER_SIZE		(( 1152 * PCM_WIDTH * PCM_CHANNELS ) * 2 ) // framesample * width * channels * 2(double buffer)

typedef struct
{
	int	handle;
	int	cachePos;
	int	frameCount;
	byte	*pcmTempPtr;
} mp3_decoder_t;

/*
=================
Sound_GetID3V2SizeMPG
=================
*/
__inline int Sound_GetID3V2SizeMPG( const byte *tagSize )
{
	int	size;
	byte	*sizePtr = (byte*)&size;

	// 7 bit per byte, invert endian
	sizePtr[0] = (( tagSize[3] >> 0 ) & 0x7F ) | (( tagSize[2] & 0x01 ) << 7 );
	sizePtr[1] = (( tagSize[2] >> 1 ) & 0x3F ) | (( tagSize[1] & 0x03 ) << 6 );
	sizePtr[2] = (( tagSize[1] >> 2 ) & 0x1F ) | (( tagSize[0] & 0x07 ) << 5 );
	sizePtr[3] = (( tagSize[0] >> 3 ) & 0x0F );

	return size;
}

#if 0
/*
=================
Sound_FindHeadMPG
=================
*/
fs_offset_t Sound_FindHeadMPG( const byte *buffer, fs_offset_t filesize )
{
	fs_offset_t	result;

	result = 0;
	if( filesize >= MP3_ID3V2_HEADER_SZ )
	{
		if( !memcmp( buffer, MP3_ID3V2_ID, MP3_ID3V2_ID_SZ ))
			result = Sound_GetID3V2SizeMPG( &buffer[MP3_ID3V2_SIZE_OFF] ) + MP3_ID3V2_HEADER_SZ;
	}
	return result;
}

/*
=================
Sound_FindTailMPG
=================
*/
fs_offset_t Sound_FindTailMPG( const byte *buffer, fs_offset_t filesize )
{
	fs_offset_t	result;

	result = filesize;
	if( filesize >= MP3_ID3V1_SZ )
	{
		if( !memcmp( &buffer[filesize - MP3_ID3V1_SZ], MP3_ID3V1_ID, MP3_ID3V1_ID_SZ ))
			result -= MP3_ID3V1_SZ;
	}
	return result;
}
#endif

qboolean Sound_LoadMPG( const char *name, const byte *buffer, fs_offset_t filesize )
{
#if 0
	int		status;
	int		handle;
	size_t		bytesWrite = 0;
	byte		out[PCM_BUFFER_SIZE] __attribute__((aligned(64)));
	fs_offset_t	headOffset;
	fs_offset_t	tailOffset;
	fs_offset_t	contentSize;
	int		frameSample;
	uint		bps;
	int		frameLen;
	int		frameSize;
	int		frameCount;

	headOffset = Sound_FindHeadMPG( buffer, filesize );
	tailOffset = Sound_FindTailMPG( buffer, filesize );
	contentSize = tailOffset - headOffset;
	// TODO: read Xing header

	handle = sceMp3ReserveMp3Handle( NULL );
	if ( handle < 0 )
	{
		Con_DPrintf( S_ERROR "sceMp3ReserveMp3Handle returned 0x%08X\n", handle );
		return false;
	}

	status = sceMp3LowLevelInit( handle, &buffer[headOffset] );
	if ( status < 0 )
	{
		Con_DPrintf( S_ERROR "sceMp3LowLevelInit returned 0x%08X\n", status );
		return false;
	}

	frameSample = sceMp3GetMaxOutputSample( handle );
	frameSize = (( frameSample / 8 ) * sceMp3GetBitRate( handle ) * 1000 ) / sceMp3GetSamplingRate( handle );
	frameCount = contentSize / frameSize;

	sound.channels = sceMp3GetMp3ChannelNum( handle );
	sound.rate = sceMp3GetSamplingRate( handle );
	sound.width = PCM_WIDTH; // always 16-bit PCM
	sound.loopstart = -1;
	sound.size = (frameCount * frameSample) * sound.channels * sound.width; // invalid for VBR
/*
	status = sceMp3LowLevelDecode(handle, &buffer[headOffset + mp3usedsize], &mp3usedsize, pcm, &pcmoutsize);
	if(status < 0)
	{
		Con_DPrintf( S_ERROR "sceMp3LowLevelDecode returned 0x%08X\n", status);
	}
	sceMp3ReleaseMp3Handle( handle );
*/
	return true;
#else
	Con_DPrintf( S_ERROR "Sound_LoadMPG unimplemented function!\n");
	return false;
#endif
}

/*
=================
Stream_FindHeadMPG
=================
*/
SceOff Stream_FindHeadMPG( file_t *file )
{
	SceOff	result;
	byte	tagId[MP3_ID3V2_ID_SZ];
	byte	tagSize[MP3_ID3V2_SIZE_SZ];

	result = 0;

	FS_Seek( file, 0, SEEK_SET );

	if( FS_Read( file, tagId,  MP3_ID3V2_ID_SZ) < MP3_ID3V2_ID_SZ )
	{
		Con_DPrintf( S_ERROR "Sound_Mp3FindHead: ID3V2 ID read error\n");
		return result;
	}

	if( !memcmp( tagId, MP3_ID3V2_ID, MP3_ID3V2_ID_SZ ))
	{
		FS_Seek( file, MP3_ID3V2_SIZE_OFF, SEEK_SET );
		if( FS_Read( file, tagSize, MP3_ID3V2_SIZE_SZ ) < MP3_ID3V2_SIZE_SZ )
			Con_DPrintf( S_ERROR "Sound_Mp3FindHead: ID3V2 SIZE read error\n");
		else result = Sound_GetID3V2SizeMPG( tagSize ) + MP3_ID3V2_HEADER_SZ;
	}

	return result;
}

/*
=================
Stream_FindTailMPG
=================
*/
SceOff Stream_FindTailMPG( file_t *file )
{
	SceOff	result;
	byte	tagId[MP3_ID3V1_ID_SZ];

	result = FS_FileLength( file );

	FS_Seek( file, result - MP3_ID3V1_SZ, SEEK_SET );

	if( FS_Read( file, tagId,  MP3_ID3V1_ID_SZ ) < MP3_ID3V1_ID_SZ )
	{
		Con_DPrintf( S_ERROR "Sound_Mp3FindTail: ID3V1 ID read error\n");
		return result;
	}

	if( !memcmp( tagId, MP3_ID3V1_ID, MP3_ID3V1_ID_SZ ))
		result -= MP3_ID3V1_SZ;

	return result;
}

/*
=================
Stream_FillBufferMPG
=================
*/
int Stream_FillBufferMPG( file_t *file, mp3_decoder_t *desc )
{
	int		status;
	byte		*dstPtr;
	int		dstSize;
	int		dstPos;
	fs_offset_t	readSize;

	if( sceMp3CheckStreamDataNeeded( desc->handle ) <= 0 )
		return 0;

	// get Info on the stream (where to fill to, how much to fill, where to fill from)
	status = sceMp3GetInfoToAddStreamData( desc->handle, &dstPtr, &dstSize, &dstPos );
	if( status < 0 )
	{
		Con_DPrintf( S_ERROR "sceMp3GetInfoToAddStreamData returned 0x%08X\n", status);
		return status;
	}

	// seek file to position requested
	if( desc->cachePos != dstPos )
	{
		FS_Seek( file, dstPos, SEEK_SET );
		desc->cachePos = dstPos;
	}

	// read the amount of data
	readSize = FS_Read( file, dstPtr, dstSize );
	desc->cachePos += ( int )readSize;

	// notify mp3 library about how much we really wrote to the stream buffer
	status = sceMp3NotifyAddStreamData( desc->handle, ( int )readSize );
	if ( status < 0 )
		Con_DPrintf( S_ERROR "sceMp3NotifyAddStreamData returned 0x%08X\n", status);

	return status;
}

/*
=================
Stream_OpenMPG
=================
*/
stream_t *Stream_OpenMPG( const char *filename )
{
	stream_t	*stream;
	mp3_decoder_t	*desc;
	file_t		*file;
	int		status;
	void		*bufferBase;

	file = FS_Open( filename, "rbh", false ); // hold mode
	if( !file ) return NULL;

	// at this point we have valid stream
	stream = Mem_Calloc( host.soundpool, sizeof( stream_t ));
	stream->file = file;
	stream->pos = 0;

	desc = Mem_Calloc( host.soundpool, sizeof( mp3_decoder_t ) + MP3_BUFFER_SIZE + PCM_BUFFER_SIZE + 63 );
	bufferBase = (void *)(((( uintptr_t )desc + sizeof( mp3_decoder_t )) & (~( 64 - 1 ))) + 64 );

	// reserve a mp3 handle
	SceMp3InitArg mp3Init;
	mp3Init.mp3StreamStart = Stream_FindHeadMPG( file );
	mp3Init.mp3StreamEnd = Stream_FindTailMPG( file );
	mp3Init.mp3Buf = bufferBase;
	mp3Init.mp3BufSize = MP3_BUFFER_SIZE;
	mp3Init.pcmBuf = (void *)(( uintptr_t )bufferBase + MP3_BUFFER_SIZE );
	mp3Init.pcmBufSize = PCM_BUFFER_SIZE;

	desc->cachePos = 0;
	desc->pcmTempPtr = mp3Init.pcmBuf;
	desc->handle = sceMp3ReserveMp3Handle( &mp3Init );
	if ( desc->handle < 0 )
	{
		Con_DPrintf( S_ERROR "sceMp3ReserveMp3Handle returned 0x%08X\n", desc->handle );
		Mem_Free( stream );
		Mem_Free( desc );
		FS_Close( file );
		return NULL;
	}

	// Fill the stream buffer with some data so that sceMp3Init has something to work with
	if( Stream_FillBufferMPG( file, desc ) != 0 )
	{
		Mem_Free( stream );
		Mem_Free( desc );
		FS_Close( file );
		return NULL;
	}

	status = sceMp3Init( desc->handle );
	if ( status < 0 )
	{
		Con_DPrintf( S_ERROR "sceMp3Init returned 0x%08X\n", status );
		Mem_Free( stream );
		Mem_Free( desc );
		FS_Close( file );
		return NULL;
	}

	desc->frameCount = sceMp3GetFrameNum( desc->handle );

	stream->buffsize = 0; // how many samples left from previous frame
	stream->channels = PCM_CHANNELS; // always 2 PCM channels
	stream->rate = sceMp3GetSamplingRate( desc->handle );
	stream->width = PCM_WIDTH; // always 16 bit PCM
	stream->ptr = (void *)desc;
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
	int		bytesWritten = 0;
	mp3_decoder_t	*desc;
	int		errdec;

	desc = ( mp3_decoder_t* )stream->ptr;

	while(1)
	{
		byte	*data;
		int	outsize;

		if( !stream->buffsize )
		{
			// Check if we need to fill our stream buffer
			if( Stream_FillBufferMPG( stream->file, desc ) != 0 )
				return 0;

			for( errdec = 0; errdec < MP3_ERRORS_MAX; errdec++ )
			{
				stream->pos = sceMp3Decode( desc->handle, (SceShort16**)&desc->pcmTempPtr );
				if(( int )stream->pos >= 0 ) // decoding successful
				{
					break;
				}
				else if( stream->pos == 0x80671402 ) // next frame header
				{
					if( Stream_FillBufferMPG( stream->file, desc ) != 0 )
						return 0;
				}
				else break;
			}

			if(( int )stream->pos < 0 )
			{
				if( stream->pos != 0x80671402 )
					Con_DPrintf( S_ERROR "sceMp3Decode returned 0x%08X\n", stream->pos );
				return 0; // ???
			}
		}

		// check remaining size
		if( bytesWritten + stream->pos > needBytes )
			outsize = ( needBytes - bytesWritten );
		else outsize = stream->pos;

		// copy raw sample to output buffer
		data = (byte *)buffer + bytesWritten;
		memcpy( data, &desc->pcmTempPtr[stream->buffsize], outsize );
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
	mp3_decoder_t	*desc;
	int		frame;
	int		status;

	desc = ( mp3_decoder_t* )stream->ptr;

	// get frame num
	frame = newpos / sceMp3GetMaxOutputSample( desc->handle ); // VBR?

	if( frame < 1 || frame >= desc->frameCount - 1 )
		return false;

	status = sceMp3ResetPlayPositionByFrame( desc->handle, frame );
	if( status < 0 )
	{
		Con_DPrintf( S_ERROR "sceMp3ResetPlayPositionByFrame returned 0x%08X\n", status );

		// failed to seek for some reasons
		return false;
	}

	// flush any previous data
	stream->buffsize = 0;

	return true;
}

/*
=================
Stream_GetPosMPG

assume stream is valid
=================
*/
int Stream_GetPosMPG( stream_t *stream )
{
	mp3_decoder_t	*desc;

	desc = ( mp3_decoder_t * )stream->ptr;

	return sceMp3GetSumDecodedSample( desc->handle );
}

/*
=================
Stream_FreeMPG

assume stream is valid
=================
*/
void Stream_FreeMPG( stream_t *stream )
{
	mp3_decoder_t	*desc;

	desc = ( mp3_decoder_t * )stream->ptr;
	if( desc )
	{
		sceMp3ReleaseMp3Handle( desc->handle );
		Mem_Free( desc );
		stream->ptr = NULL;
	}

	if( stream->file )
	{
		FS_Close( stream->file );
		stream->file = NULL;
	}

	Mem_Free( stream );
}
#endif // XASH_PSP
