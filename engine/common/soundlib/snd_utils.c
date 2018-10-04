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

/*
================
Sound_ConvertToSigned

Convert unsigned data to signed
================
*/
void Sound_ConvertToSigned( const byte *data, int channels, int samples )
{
	int	i;

	if( channels == 2 )
	{
		for( i = 0; i < samples; i++ )
		{
			((signed char *)sound.tempbuffer)[i*2+0] = (int)((byte)(data[i*2+0]) - 128);
			((signed char *)sound.tempbuffer)[i*2+1] = (int)((byte)(data[i*2+1]) - 128);
		}
	}
	else
	{
		for( i = 0; i < samples; i++ )
			((signed char *)sound.tempbuffer)[i] = (int)((unsigned char)(data[i]) - 128);
	}
}

/*
================
Sound_ResampleInternal

We need convert sound to signed even if nothing to resample
================
*/
qboolean Sound_ResampleInternal( wavdata_t *sc, int inrate, int inwidth, int outrate, int outwidth )
{
	float	stepscale;
	int	outcount, srcsample;
	int	i, sample, sample2, samplefrac, fracstep;
	byte	*data;

	data = sc->buffer;
	stepscale = (float)inrate / outrate;	// this is usually 0.5, 1, or 2
	outcount = sc->samples / stepscale;
	sc->size = outcount * outwidth * sc->channels;

	sound.tempbuffer = (byte *)Mem_Realloc( host.soundpool, sound.tempbuffer, sc->size );

	sc->samples = outcount;
	if( sc->loopStart != -1 )
		sc->loopStart = sc->loopStart / stepscale;

	// resample / decimate to the current source rate
	if( stepscale == 1.0f && inwidth == 1 && outwidth == 1 )
	{
		Sound_ConvertToSigned( data, sc->channels, outcount );
	}
	else
	{
		// general case
		samplefrac = 0;
		fracstep = stepscale * 256;

		if( sc->channels == 2 )
		{
			for( i = 0; i < outcount; i++ )
			{
				srcsample = samplefrac >> 8;
				samplefrac += fracstep;

				if( inwidth == 2 )
				{
					sample = ((short *)data)[srcsample*2+0];
					sample2 = ((short *)data)[srcsample*2+1];
				}
				else
				{
					sample = (int)((char)(data[srcsample*2+0])) << 8;
					sample2 = (int)((char)(data[srcsample*2+1])) << 8;
				}

				if( outwidth == 2 )
				{
					((short *)sound.tempbuffer)[i*2+0] = sample;
					((short *)sound.tempbuffer)[i*2+1] = sample2;
				}
				else
				{
					((signed char *)sound.tempbuffer)[i*2+0] = sample >> 8;
					((signed char *)sound.tempbuffer)[i*2+1] = sample2 >> 8;
				}
			}
		}
		else
		{
			for( i = 0; i < outcount; i++ )
			{
				srcsample = samplefrac >> 8;
				samplefrac += fracstep;

				if( inwidth == 2 ) sample = ((short *)data)[srcsample];
				else sample = (int)( (char)(data[srcsample])) << 8;

				if( outwidth == 2 ) ((short *)sound.tempbuffer)[i] = sample;
				else ((signed char *)sound.tempbuffer)[i] = sample >> 8;
			}
		}

		Con_Reportf( "Sound_Resample: from[%d bit %d kHz] to [%d bit %d kHz]\n", inwidth * 8, inrate, outwidth * 8, outrate );
	}

	sc->rate = outrate;
	sc->width = outwidth;

	return true;
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