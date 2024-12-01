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
#if XASH_SDL
#include <SDL_audio.h>
#endif // XASH_SDL

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
{ DEFAULT_SOUNDPATH "%s%s.%s", "ogg", Sound_LoadOggVorbis },
{ "%s%s.%s", "ogg", Sound_LoadOggVorbis },
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

static byte *Sound_Copy( size_t size )
{
	byte	*out;

	out = Mem_Realloc( host.soundpool, sound.tempbuffer, size );
	sound.tempbuffer = NULL;

	return out;
}

uint GAME_EXPORT Sound_GetApproxWavePlayLen( const char *filepath )
{
	string    name;
	file_t    *f;
	wavehdr_t wav;
	size_t    filesize;
	uint      msecs;

	Q_strncpy( name, filepath, sizeof( name ));
	COM_FixSlashes( name );

	f = FS_Open( name, "rb", false );
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

#define SOUND_FORMATCONVERT_BOILERPLATE( resamplemacro ) \
	if( inwidth == 1 ) \
	{ \
		const int8_t *data = (const int8_t *)sc->buffer; \
		if( outwidth == 1 ) \
		{ \
			int8_t *outdata = (int8_t *)sound.tempbuffer; \
			resamplemacro( 1 ) \
		} \
		else if( outwidth == 2 ) \
		{ \
			int16_t *outdata = (int16_t *)sound.tempbuffer; \
			resamplemacro( 256 ) \
		} \
		else \
			return false; \
	} \
	else if( inwidth == 2 ) \
	{ \
		const int16_t *data = (const int16_t *)sc->buffer; \
		if( outwidth == 1 ) \
		{ \
			int8_t *outdata = (int8_t *)sound.tempbuffer; \
			resamplemacro( 1 / 256.0 ) \
		} \
		else if( outwidth == 2 ) \
		{ \
			int16_t *outdata = (int16_t *)sound.tempbuffer; \
			resamplemacro( 1 ) \
		} \
		else \
			return false; \
	} \
	else \
		return false; \


#define SOUND_CONVERTNORESAMPLE_BOILERPLATE( multiplier ) \
	if( inchannels == 1 ) \
	{ \
		if( outchannels == 1 ) \
		{ \
			for( i = 0; i < outcount * outchannels; i++ ) \
				outdata[i] = data[i] * ( multiplier ); \
		} \
		else if( outchannels == 2 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				outdata[i * 2 + 0] = data[i] * ( multiplier ); \
				outdata[i * 2 + 1] = data[i] * ( multiplier ); \
			} \
		} \
		else \
			return false; \
	} \
	else if( inchannels == 2 ) \
	{ \
		if( outchannels == 1 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
				outdata[i] = ( data[i * 2 + 0] + data[i * 2 + 1] ) * ( multiplier ) / 2; \
		} \
		else if( outchannels == 2 ) \
		{ \
			for( i = 0; i < outcount * outchannels; i++ ) \
				outdata[i] = data[i] * ( multiplier ); \
		} \
		else \
			return false; \
	} \
	else \
		return false; \

#define SOUND_CONVERTDOWNSAMPLE_BOILERPLATE( multiplier ) \
	if( inchannels == 1 ) \
	{ \
		if( outchannels == 1 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i; \
				outdata[i] = data[(int)j] * ( multiplier ); \
			} \
		} \
		else if( outchannels == 2 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i;\
				outdata[i * 2 + 0] = data[(int)j] * ( multiplier ); \
				outdata[i * 2 + 1] = data[(int)j] * ( multiplier ); \
			} \
		} \
		else \
			return false; \
	} \
	else if( inchannels == 2 ) \
	{ \
		if( outchannels == 1 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i; \
				outdata[i] = ( data[((int)j) * 2 + 0] + data[((int)j) * 2 + 1] ) * ( multiplier ) / 2; \
			} \
		} \
		else if( outchannels == 2 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i; \
				outdata[i * 2 + 0] = data[((int)j) * 2 + 0] * ( multiplier ); \
				outdata[i * 2 + 1] = data[((int)j) * 2 + 1] * ( multiplier ); \
			} \
		} \
		else \
			return false; \
	} \
	else \
		return false; \


#define SOUND_CONVERTUPSAMPLE_BOILERPLATE( multiplier ) \
	if( inchannels == 1 ) \
	{ \
		if( outchannels == 1 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i; \
				outdata[i] = data[(int)j] * ( multiplier ); \
				if( j != (int)j && j < incount ) \
				{ \
					double frac = ( j - (int)j ) * ( multiplier ); \
					outdata[i] += (data[(int)j+1] - data[(int)j]) * frac; \
				} \
			} \
		} \
		else if( outchannels == 2 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i; \
				outdata[i * 2 + 0] = data[(int)j] * ( multiplier ); \
				if( j != (int)j && j < incount ) \
				{ \
					double frac = ( j - (int)j ) * ( multiplier ); \
					outdata[i * 2 + 0] += (data[(int)j+1] - data[(int)j]) * frac; \
				} \
				outdata[i * 2 + 1] = outdata[i * 2 + 0]; \
			} \
		} \
		else \
			return false; \
	} \
	else if( inchannels == 2 ) \
	{ \
		if( outchannels == 1 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i; \
				outdata[i] = ( data[((int)j) * 2 + 0] + data[((int)j) * 2 + 1] ) * ( multiplier ) / 2; \
				if( j != (int)j && j < incount ) \
				{ \
					double frac = ( j - (int)j ) * ( multiplier ) / 2; \
					outdata[i] += (data[((int)j + 1 ) * 2 + 0] - data[((int)j) * 2 + 0]) * frac; \
					outdata[i] += (data[((int)j + 1 ) * 2 + 1] - data[((int)j) * 2 + 1]) * frac; \
				} \
			} \
		} \
		else if( outchannels == 2 ) \
		{ \
			for( i = 0; i < outcount; i++ ) \
			{ \
				double j = stepscale * i; \
				outdata[i*2+0] = data[((int)j)*2+0] * ( multiplier ); \
				outdata[i*2+1] = data[((int)j)*2+1] * ( multiplier ); \
				if( j != (int)j && j < incount ) \
				{ \
					double frac = ( j - (int)j ) * ( multiplier ); \
					outdata[i*2+0] += (data[((int)j+1)*2+0] - data[((int)j)*2+0]) * frac; \
					outdata[i*2+1] += (data[((int)j+1)*2+1] - data[((int)j)*2+1]) * frac; \
				} \
			} \
		} \
		else \
			return false; \
	} \
	else \
		return false; \

static qboolean Sound_ConvertNoResample( wavdata_t *sc, int inwidth, int inchannels, int outwidth, int outchannels, int outcount )
{
	size_t i;

	// I could write a generic case here but I also want to make it easier for compiler
	// to unroll these for-loops

	SOUND_FORMATCONVERT_BOILERPLATE( SOUND_CONVERTNORESAMPLE_BOILERPLATE )

	return true;
}

static qboolean Sound_ConvertDownsample( wavdata_t *sc, int inwidth, int inchannels, int outwidth, int outchannels, int outcount, double stepscale )
{
	size_t i;

	SOUND_FORMATCONVERT_BOILERPLATE( SOUND_CONVERTDOWNSAMPLE_BOILERPLATE )

	return true;
}

static qboolean Sound_ConvertUpsample( wavdata_t *sc, int inwidth, int inchannels, int incount, int outwidth, int outchannels, int outcount, double stepscale )
{
	size_t i;

	incount--; // to not go past last sample while interpolating

	SOUND_FORMATCONVERT_BOILERPLATE( SOUND_CONVERTUPSAMPLE_BOILERPLATE )

	return true;
}

#undef SOUND_FORMATCONVERT_BOILERPLATE
#undef SOUND_CONVERTNORESAMPLE_BOILERPLATE
#undef SOUND_CONVERTDOWNSAMPLE_BOILERPLATE
#undef SOUND_CONVERTUPSAMPLE_BOILERPLATE

/*
================
Sound_ResampleInternal

================
*/
static qboolean Sound_ResampleInternal( wavdata_t *sc, int outrate, int outwidth, int outchannels )
{
	const int inrate = sc->rate;
	const int inwidth = sc->width;
	const int inchannels = sc->channels;
	const int incount = sc->samples;
	qboolean handled = false;
	double stepscale;
	double t1, t2;
	int	outcount;

	if( inrate == outrate && inwidth == outwidth && inchannels == outchannels )
		return false;

	t1 = Sys_DoubleTime();

	stepscale = (double)inrate / outrate;	// this is usually 0.5, 1, or 2
	outcount = sc->samples / stepscale;
	sc->size = outcount * outwidth * outchannels;
	sc->channels = outchannels;

	sc->samples = outcount;
	if( FBitSet( sc->flags, SOUND_LOOPED ))
		sc->loopStart = sc->loopStart / stepscale;

#if 0 && XASH_SDL // slow but somewhat accurate (wasn't updated to channel manipulation!!!)
	{
		const SDL_AudioFormat infmt  = inwidth  == 1 ? AUDIO_S8 : AUDIO_S16;
		const SDL_AudioFormat outfmt = outwidth == 1 ? AUDIO_S8 : AUDIO_S16;
		SDL_AudioCVT cvt;

		// SDL_AudioCVT does conversion in place, original buffer is used for it
		if( SDL_BuildAudioCVT( &cvt, infmt, inchannels, inrate, outfmt, outchannels, outrate ) > 0 && cvt.needed )
		{
			sc->buffer = (byte *)Mem_Realloc( host.soundpool, sc->buffer, oldsize * cvt.len_mult );
			cvt.len = oldsize;
			cvt.buf = sc->buffer;

			if( !SDL_ConvertAudio( &cvt ))
			{
				t2 = Sys_DoubleTime();
				Con_Reportf( "Sound_Resample: from [%d bit %d Hz] to [%d bit %d Hz] (took %.3fs through SDL)\n", inwidth * 8, inrate, outwidth * 8, outrate, t2 - t1 );
				sc->rate = outrate;
				sc->width = outwidth;
				return false; // HACKHACK: return false so Sound_Process won't reallocate buffer
			}
		}
	}
#endif

	sound.tempbuffer = (byte *)Mem_Realloc( host.soundpool, sound.tempbuffer, sc->size );

	if( inrate == outrate ) // no resampling, just copy data
		handled = Sound_ConvertNoResample( sc, inwidth, inchannels, outwidth, outchannels, outcount );
	else if( inrate > outrate ) // fast case, usually downsample but is also ok for upsampling
		handled = Sound_ConvertDownsample( sc, inwidth, inchannels, outwidth, outchannels, outcount, stepscale );
	else // upsample case, w/ interpolation
		handled = Sound_ConvertUpsample( sc, inwidth, inchannels, incount, outwidth, outchannels, outcount, stepscale );

	t2 = Sys_DoubleTime();

	if( handled )
	{
		if( t2 - t1 > 0.01f ) // critical, report to mod developer
			Con_Printf( S_WARN "%s: from [%d bit %d Hz %dch] to [%d bit %d Hz %dch] (took %.3fs)\n", __func__, inwidth * 8, inrate, inchannels, outwidth * 8, outrate, outchannels, t2 - t1 );
		else
			Con_Reportf( "%s: from [%d bit %d Hz %dch] to [%d bit %d Hz %dch] (took %.3fs)\n", __func__, inwidth * 8, inrate, inchannels, outwidth * 8, outrate, outchannels, t2 - t1 );
	}
	else
		Con_Printf( S_ERROR "%s: unsupported from [%d bit %d Hz %dch] to [%d bit %d Hz %dch]\n", __func__, inwidth * 8, inrate, inchannels, outwidth * 8, outrate, outchannels );

	sc->rate = outrate;
	sc->width = outwidth;

	return handled;
}

qboolean Sound_Process( wavdata_t **wav, int rate, int width, int channels, uint flags )
{
	wavdata_t	*snd = *wav;
	qboolean	result = true;

	// check for buffers
	if( unlikely( !snd || !snd->buffer ))
		return false;

	if( likely( FBitSet( flags, SOUND_RESAMPLE ) && ( width > 0 || rate > 0	|| channels > 0 )))
	{
		result = Sound_ResampleInternal( snd, rate, width, channels );

		if( result )
		{
			Mem_Free( snd->buffer );		// free original image buffer
			snd->buffer = Sound_Copy( snd->size );	// unzone buffer
		}
	}

	*wav = snd;

	return result;
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
