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
FS_SeekEx
=================
*/
fs_offset_t FS_SeekEx( file_t *file, fs_offset_t offset, int whence )
{
	return FS_Seek( file, offset, whence ) == -1 ? -1 : FS_Tell( file );
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
	if( !open_mpeg_stream( mpeg, file, (void*)FS_Read, (void*)FS_SeekEx, &sc ))
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
