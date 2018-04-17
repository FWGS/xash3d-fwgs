/*
libmpg.c - compact version of famous library mpg123
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "mpg123.h"
#include "libmpg.h"

void *create_decoder( int *error )
{
	void	*mpg;
	int	ret;

	if( error ) *error = 0;
	mpg123_init();
	
	mpg = mpg123_new( &ret );
	if( !mpg ) return NULL;

	ret = mpg123_param( mpg, MPG123_FLAGS, MPG123_FUZZY|MPG123_SEEKBUFFER|MPG123_GAPLESS );
	if( ret != MPG123_OK && error )
		*error = 1;

	// let the seek index auto-grow and contain an entry for every frame
	ret = mpg123_param( mpg, MPG123_INDEX_SIZE, -1 );
	if( ret != MPG123_OK && error )
		*error = 1;

	return mpg;
}

int feed_mpeg_header( void *mpg, const char *data, long bufsize, long streamsize, wavinfo_t *sc )
{
	mpg123_handle_t	*mh = (mpg123_handle_t *)mpg;
	int		ret, no;

	if( !mh || !sc ) return 0;

	ret = mpg123_open_feed( mh );
	if( ret != MPG123_OK )
		return 0;

	// feed input chunk and get first chunk of decoded audio.
	ret = mpg123_decode( mh, data, bufsize, NULL, 0, NULL );

	if( ret != MPG123_NEW_FORMAT )
		return 0;	// there were errors

	mpg123_getformat( mh, &sc->rate, &sc->channels, &no );
	mpg123_format_none( mh );
	mpg123_format( mh, sc->rate, sc->channels, MPG123_ENC_SIGNED_16 );

	// some hacking to get function get_songlen to working properly
	mh->rdat.filelen = streamsize;
	sc->playtime = get_songlen( mh, -1 ) * 1000;

	return 1;
}

int feed_mpeg_stream( void *mpg, const char *data, long bufsize, char *outbuf, size_t *outsize )
{
	switch( mpg123_decode( mpg, data, bufsize, outbuf, OUTBUF_SIZE, outsize ))
	{
	case MPG123_NEED_MORE:
		return MP3_NEED_MORE;
	case MPG123_OK:
		return MP3_OK;
	default:
		return MP3_ERR;
	}
}

int open_mpeg_stream( void *mpg, void *file, pfread f_read, pfseek f_seek, wavinfo_t *sc )
{
	mpg123_handle_t	*mh = (mpg123_handle_t *)mpg;
	int		ret, no;

	if( !mh || !sc ) return 0;

	ret = mpg123_replace_reader_handle( mh, f_read, f_seek, NULL );
	if( ret != MPG123_OK )
		return 0;

	ret = mpg123_open_handle( mh, file );
	if( ret != MPG123_OK )
		return 0;

	ret = mpg123_getformat( mh, &sc->rate, &sc->channels, &no );
	if( ret != MPG123_OK )
		return 0;

	mpg123_format_none( mh );
	mpg123_format( mh, sc->rate, sc->channels, MPG123_ENC_SIGNED_16 );
	sc->playtime = get_songlen( mh, -1 ) * 1000;

	return 1;
}

int read_mpeg_stream( void *mpg, char *outbuf, size_t *outsize  )
{
	switch( mpg123_read( mpg, outbuf, OUTBUF_SIZE, outsize ))
	{
	case MPG123_OK:
		return MP3_OK;
	default:
		return MP3_ERR;
	}
}

int get_stream_pos( void *mpg )
{
	return mpg123_tell( mpg );
}

int set_stream_pos( void *mpg, int curpos )
{
	return mpg123_seek( mpg, curpos, SEEK_SET );
}

void close_decoder( void *mpg )
{
	mpg123_delete( mpg );
	mpg123_exit();
}