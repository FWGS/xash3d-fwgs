/*
s_stream.c - sound streaming
Copyright (C) 2009 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "sound.h"
#include "client.h"
#include "soundlib.h"

static bg_track_t		s_bgTrack;
static musicfade_t		musicfade;	// controlled by game dlls

/*
=================
S_PrintBackgroundTrackState
=================
*/
void S_PrintBackgroundTrackState( void )
{
	Con_Printf( "BackgroundTrack: " );

	if( s_bgTrack.current[0] && s_bgTrack.loopName[0] )
		Con_Printf( "intro %s, loop %s\n", s_bgTrack.current, s_bgTrack.loopName );
	else if( s_bgTrack.current[0] )
		Con_Printf( "%s\n", s_bgTrack.current );
	else if( s_bgTrack.loopName[0] )
		Con_Printf( "%s [loop]\n", s_bgTrack.loopName );
	else Con_Printf( "not playing\n" );
}

/*
=================
S_FadeMusicVolume
=================
*/
void S_FadeMusicVolume( float fadePercent )
{
	musicfade.percent = bound( 0.0f, fadePercent, 100.0f );
}

/*
=================
S_GetMusicVolume
=================
*/
float S_GetMusicVolume( void )
{
	float	scale = 1.0f;

	if( host.status == HOST_NOFOCUS && snd_mute_losefocus.value != 0.0f )
	{
		// we return zero volume to keep sounds running
		return 0.0f;
	}

	if( !s_listener.inmenu && musicfade.percent != 0 )
	{
		scale = bound( 0.0f, musicfade.percent / 100.0f, 1.0f );
		scale = 1.0f - scale;
	}

	return s_musicvolume.value * scale;
}

/*
=================
S_StartBackgroundTrack
=================
*/
void S_StartBackgroundTrack( const char *introTrack, const char *mainTrack, int position, qboolean fullpath )
{
	S_StopBackgroundTrack();

	if( !dma.initialized ) return;

	// check for special symbols
	if( introTrack && *introTrack == '*' )
		introTrack = NULL;

	if( mainTrack && *mainTrack == '*' )
		mainTrack = NULL;

	if( !COM_CheckString( introTrack ) && !COM_CheckString( mainTrack ))
		return;

	if( !introTrack ) introTrack = mainTrack;
	if( !*introTrack ) return;

	if( !COM_CheckString( mainTrack ))
		s_bgTrack.loopName[0] = '\0';
	else Q_strncpy( s_bgTrack.loopName, mainTrack, sizeof( s_bgTrack.loopName ));

	// open stream
	s_bgTrack.stream = FS_OpenStream( introTrack );

	Q_strncpy( s_bgTrack.current, introTrack, sizeof( s_bgTrack.current ));
	memset( &musicfade, 0, sizeof( musicfade )); // clear any soundfade
	s_bgTrack.source = cls.key_dest;

	if( position != 0 )
	{
		// restore message, update song position
		FS_SetStreamPos( s_bgTrack.stream, position );
	}
}

/*
=================
S_StopBackgroundTrack
=================
*/
void S_StopBackgroundTrack( void )
{
	s_listener.stream_paused = false;

	if( !dma.initialized ) return;
	if( !s_bgTrack.stream ) return;

	FS_FreeStream( s_bgTrack.stream );
	memset( &s_bgTrack, 0, sizeof( bg_track_t ));
	memset( &musicfade, 0, sizeof( musicfade ));
}

/*
=================
S_StreamSetPause
=================
*/
void S_StreamSetPause( int pause )
{
	s_listener.stream_paused = pause;
}

/*
=================
S_StreamGetCurrentState

save\restore code
=================
*/
qboolean S_StreamGetCurrentState( char *currentTrack, size_t currentTrackSize, char *loopTrack, size_t loopTrackSize, int *position )
{
	if( !s_bgTrack.stream )
		return false; // not active

	if( currentTrack )
	{
		if( s_bgTrack.current[0] )
			Q_strncpy( currentTrack, s_bgTrack.current, currentTrackSize );
		else Q_strncpy( currentTrack, "*", currentTrackSize ); // no track
	}

	if( loopTrack )
	{
		if( s_bgTrack.loopName[0] )
			Q_strncpy( loopTrack, s_bgTrack.loopName, loopTrackSize );
		else Q_strncpy( loopTrack, "*", loopTrackSize ); // no track
	}

	if( position )
		*position = FS_GetStreamPos( s_bgTrack.stream );

	return true;
}

/*
=================
S_StreamBackgroundTrack
=================
*/
void S_StreamBackgroundTrack( void )
{
	int	bufferSamples;
	int	fileSamples;
	byte	raw[MAX_RAW_SAMPLES];
	int	r, fileBytes;
	rawchan_t	*ch = NULL;

	if( !dma.initialized || !s_bgTrack.stream || s_listener.streaming )
		return;

	// don't bother playing anything if musicvolume is 0
	if( !s_musicvolume.value || s_listener.paused || s_listener.stream_paused )
		return;

	if( !cl.background )
	{
		// pause music by source type
		if( s_bgTrack.source == key_game && cls.key_dest == key_menu ) return;
		if( s_bgTrack.source == key_menu && cls.key_dest != key_menu ) return;
	}
	else if( cls.key_dest == key_console )
		return;

	ch = S_FindRawChannel( S_RAW_SOUND_BACKGROUNDTRACK, true );

	Assert( ch != NULL );

	// see how many samples should be copied into the raw buffer
	if( ch->s_rawend < soundtime )
		ch->s_rawend = soundtime;

	while( ch->s_rawend < soundtime + ch->max_samples )
	{
		const stream_t *info = s_bgTrack.stream;

		bufferSamples = ch->max_samples - (ch->s_rawend - soundtime);

		// decide how much data needs to be read from the file
		fileSamples = bufferSamples * ((float)info->rate / SOUND_DMA_SPEED );
		if( fileSamples <= 1 ) return; // no more samples need

		// our max buffer size
		fileBytes = fileSamples * ( info->width * info->channels );

		if( fileBytes > sizeof( raw ))
		{
			fileBytes = sizeof( raw );
			fileSamples = fileBytes / ( info->width * info->channels );
		}

		// read
		r = FS_ReadStream( s_bgTrack.stream, fileBytes, raw );

		if( r < fileBytes )
		{
			fileBytes = r;
			fileSamples = r / ( info->width * info->channels );
		}

		if( r > 0 )
		{
			// add to raw buffer
			int music_vol = (int)(255.0f * S_GetMusicVolume());
			S_RawEntSamples( S_RAW_SOUND_BACKGROUNDTRACK, fileSamples, info->rate, info->width, info->channels, raw, music_vol );
		}
		else
		{
			// loop
			if( s_bgTrack.loopName[0] )
			{
				FS_FreeStream( s_bgTrack.stream );
				s_bgTrack.stream = FS_OpenStream( s_bgTrack.loopName );
				Q_strncpy( s_bgTrack.current, s_bgTrack.loopName, sizeof( s_bgTrack.current ));

				if( !s_bgTrack.stream ) return;
			}
			else
			{
				S_StopBackgroundTrack();
				return;
			}
		}

	}
}

/*
=================
S_StartStreaming
=================
*/
void S_StartStreaming( void )
{
	if( !dma.initialized ) return;
	// begin streaming movie soundtrack
	s_listener.streaming = true;
}

/*
=================
S_StopStreaming
=================
*/
void S_StopStreaming( void )
{
	if( !dma.initialized ) return;
	s_listener.streaming = false;
}
