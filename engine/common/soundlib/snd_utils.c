/*
snd_utils.c - sound common tools
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

/*
=============================================================================

	XASH3D LOAD SOUND FORMATS

=============================================================================
*/
// stub
static const loadwavfmt_t load_null[] =
{
{ NULL, NULL, NULL }
};

static const loadwavfmt_t load_game[] =
{
{ DEFAULT_SOUNDPATH "%s%s.%s", "wav", Sound_LoadWAV },
{ "%s%s.%s", "wav", Sound_LoadWAV },
{ DEFAULT_SOUNDPATH "%s%s.%s", "mp3", Sound_LoadMPG },
{ "%s%s.%s", "mp3", Sound_LoadMPG },
{ NULL, NULL, NULL }
};

/*
=============================================================================

	XASH3D PROCESS STREAM FORMATS

=============================================================================
*/
// stub
static const streamfmt_t stream_null[] =
{
{ NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

static const streamfmt_t stream_game[] =
{
{ "%s%s.%s", "mp3", Stream_OpenMPG, Stream_ReadMPG, Stream_SetPosMPG, Stream_GetPosMPG, Stream_FreeMPG },
{ "%s%s.%s", "wav", Stream_OpenWAV, Stream_ReadWAV, Stream_SetPosWAV, Stream_GetPosWAV, Stream_FreeWAV },
{ NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

void Sound_Init( void )
{
	// init pools
	host.soundpool = Mem_AllocPool( "SoundLib Pool" );

	// install image formats (can be re-install later by Sound_Setup)
	switch( host.type )
	{
	case HOST_NORMAL:
		sound.loadformats = load_game;
		sound.streamformat = stream_game;
		break;
	default:	// all other instances not using soundlib or will be reinstalling later
		sound.loadformats = load_null;
		sound.streamformat = stream_null;
		break;
	}
	sound.tempbuffer = NULL;
}

void Sound_Shutdown( void )
{
	Mem_Check(); // check for leaks
	Mem_FreePool( &host.soundpool );
}

byte *Sound_Copy( size_t size )
{
	byte	*out;

	out = Mem_Malloc( host.soundpool, size );
	memcpy( out, sound.tempbuffer, size );

	return out;
}

uint GAME_EXPORT Sound_GetApproxWavePlayLen( const char *filepath )
{
	file_t    *f;
	wavehdr_t wav;
	size_t    filesize;
	uint      msecs;

	f = FS_Open( filepath, "rb", false );
	if( !f )
		return 0;

	if( FS_Read( f, &wav, sizeof( wav )) != sizeof( wav ))
	{
		FS_Close( f );
		return 0;
	}

	filesize = FS_FileLength( f );
	filesize -= 128; // magic number from GoldSrc, seems to be header size

	FS_Close( f );

	// is real wav file ?
	if( wav.riff_id != RIFFHEADER || wav.wave_id != WAVEHEADER || wav.fmt_id != FORMHEADER )
		return 0;

	if( wav.nAvgBytesPerSec >= 1000 )
		msecs = (uint)((float)filesize / ((float)wav.nAvgBytesPerSec / 1000.0f));
	else msecs = (uint)(((float)filesize / (float)wav.nAvgBytesPerSec) * 1000.0f);

	return msecs;
}

#define drint( v ) (int)( v + 0.5 )

/*
================
Sound_ResampleInternal

We need convert sound to signed even if nothing to resample
================
*/
qboolean Sound_ResampleInternal( wavdata_t *sc, int inrate, int inwidth, int outrate, int outwidth )
{
	double stepscale, j;
	int	outcount;
	int	i;
	qboolean handled = false;

	if( inrate == outrate && inwidth == outwidth )
		return false;

	stepscale = (double)inrate / outrate;	// this is usually 0.5, 1, or 2
	outcount = sc->samples / stepscale;
	sc->size = outcount * outwidth * sc->channels;

	sound.tempbuffer = (byte *)Mem_Realloc( host.soundpool, sound.tempbuffer, sc->size );

	sc->samples = outcount;
	if( sc->loopStart != -1 )
		sc->loopStart = sc->loopStart / stepscale;

	if( inrate == outrate )
	{
		if( inwidth == 1 && outwidth == 2 ) // S8 to S16
		{
			for( i = 0; i < outcount * sc->channels; i++ )
				((int16_t*)sound.tempbuffer)[i] = ((int8_t *)sc->buffer)[i] * 256;
			handled = true;
		}
		else if( inwidth == 2 && outwidth == 1 ) // S16 to S8
		{
			for( i = 0; i < outcount * sc->channels; i++ )
				((int8_t*)sound.tempbuffer)[i] = ((int16_t *)sc->buffer)[i] / 256;
			handled = true;
		}
	}
	else // resample case
	{
		if( inwidth == 1 )
		{
			int8_t *data = (int8_t *)sc->buffer;

			if( outwidth == 1 )
			{
				if( sc->channels == 2 )
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
					{
						((int8_t*)sound.tempbuffer)[i*2+0] = data[((int)j)*2+0];
						((int8_t*)sound.tempbuffer)[i*2+1] = data[((int)j)*2+1];
					}
				}
				else
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
						((int8_t*)sound.tempbuffer)[i] = data[(int)j];
				}
				handled = true;
			}
			else if( outwidth == 2 )
			{
				if( sc->channels == 2 )
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
					{
						((int16_t*)sound.tempbuffer)[i*2+0] = data[((int)j)*2+0] * 256;
						((int16_t*)sound.tempbuffer)[i*2+1] = data[((int)j)*2+1] * 256;
					}
				}
				else
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
						((int16_t*)sound.tempbuffer)[i] = data[(int)j] * 256;
				}
				handled = true;
			}
		}
		else if( inwidth == 2 )
		{
			int16_t *data = (int16_t *)sc->buffer;

			if( outwidth == 1 )
			{
				if( sc->channels == 2 )
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
					{
						((int8_t*)sound.tempbuffer)[i*2+0] = data[((int)j)*2+0] / 256;
						((int8_t*)sound.tempbuffer)[i*2+1] = data[((int)j)*2+1] / 256;
					}
				}
				else
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
						((int8_t*)sound.tempbuffer)[i] = data[(int)j] / 256;
				}
				handled = true;
			}
			else if( outwidth == 2 )
			{
				if( sc->channels == 2 )
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
					{
						((int16_t*)sound.tempbuffer)[i*2+0] = data[((int)j)*2+0];
						((int16_t*)sound.tempbuffer)[i*2+1] = data[((int)j)*2+1];
					}
				}
				else
				{
					for( i = 0, j = 0; i < outcount; i++, j += stepscale )
						((int16_t*)sound.tempbuffer)[i] = data[(int)j];
				}
				handled = true;
			}
		}
	}

	if( handled )
		Con_Reportf( "Sound_Resample: from [%d bit %d Hz] to [%d bit %d Hz]\n", inwidth * 8, inrate, outwidth * 8, outrate );
	else
		Con_Reportf( S_ERROR "Sound_Resample: unsupported from [%d bit %d Hz] to [%d bit %d Hz]\n", inwidth * 8, inrate, outwidth * 8, outrate );

	sc->rate = outrate;
	sc->width = outwidth;

	return handled;
}

qboolean Sound_Process( wavdata_t **wav, int rate, int width, uint flags )
{
	wavdata_t	*snd = *wav;
	qboolean	result = true;

	// check for buffers
	if( !snd || !snd->buffer )
		return false;

	if(( flags & SOUND_RESAMPLE ) && ( width > 0 || rate > 0 ))
	{
		if( Sound_ResampleInternal( snd, snd->rate, snd->width, rate, width ))
		{
			Mem_Free( snd->buffer );		// free original image buffer
			snd->buffer = Sound_Copy( snd->size );	// unzone buffer (don't touch image.tempbuffer)
		}
		else
		{
			// not resampled
			result = false;
		}
	}

	*wav = snd;

	return false;
}

qboolean Sound_SupportedFileFormat( const char *fileext )
{
	const loadwavfmt_t *format;
	if( COM_CheckStringEmpty( fileext ))
	{
		for( format = sound.loadformats; format && format->formatstring; format++ )
		{
			if( !Q_stricmp( format->ext, fileext ))
				return true;
		}
	}
	return false;
}
