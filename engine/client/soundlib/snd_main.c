/*
snd_main.c - load & save various sound formats
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

static void Sound_Reset( void )
{
	// reset global variables
	sound.width = sound.rate = 0;
	sound.channels = sound.loopstart = 0;
	sound.samples = sound.flags = 0;
	sound.type = WF_UNKNOWN;

	sound.wav = NULL;
	sound.size = 0;
#if XASH_GAMECUBE
	sound.gc_inplace_file = false;
#endif
}

static MALLOC_LIKE( FS_FreeSound, 1 ) wavdata_t *SoundPack( void )
{
	wavdata_t *pack = Mem_Malloc( host.soundpool, sizeof( *pack ) + sound.size );

#if XASH_GAMECUBE
	if( !pack )
		return NULL;
#endif

	pack->size = sound.size;
	pack->loop_start = sound.loopstart;
	pack->samples = sound.samples;
	pack->type = sound.type;
	pack->flags = sound.flags;
	pack->rate = sound.rate;
	pack->width = sound.width;
	pack->channels = sound.channels;
	memcpy( pack->buffer, sound.wav, sound.size );

#if XASH_GAMECUBE
	/* Borrowed FS file PCM must not be Mem_Free'd — caller frees the file. */
	if( !sound.gc_inplace_file )
#endif
		Mem_Free( sound.wav );
	sound.wav = NULL;
#if XASH_GAMECUBE
	sound.gc_inplace_file = false;
#endif

	return pack;
}

#if XASH_GAMECUBE
/*
================
SoundPackInPlaceFile

G122: reuse the FS_LoadFile buffer as wavdata_t + PCM so MEM1 never needs
file + decode + pack peak (~3x). Stock pl_gun3 fits (file 13336 >= header+PCM).

G123: when SoundLib can take a tight copy, migrate off the FS buffer so the
larger file allocation returns to the freelist for subsequent small SFX.
================
*/
static MALLOC_LIKE( FS_FreeSound, 1 ) wavdata_t *SoundPackInPlaceFile( byte *f, fs_offset_t filesize )
{
	size_t need = sizeof( wavdata_t ) + sound.size;
	byte *pcm = sound.wav;

	if( !f || !pcm || !sound.gc_inplace_file )
		return NULL;
	if( pcm < f || (size_t)( pcm - f ) + sound.size > (size_t)filesize )
		return NULL;
	if( (size_t)filesize < need )
		return NULL;

	memmove( f + sizeof( wavdata_t ), pcm, sound.size );

	{
		wavdata_t *pack = (wavdata_t *)f;
		wavdata_t *owned;

		pack->size = sound.size;
		pack->loop_start = sound.loopstart;
		pack->samples = sound.samples;
		pack->type = sound.type;
		pack->flags = sound.flags;
		pack->rate = (word)sound.rate;
		pack->width = (byte)sound.width;
		pack->channels = (byte)sound.channels;
		sound.wav = NULL;
		sound.gc_inplace_file = false;

		owned = Mem_Malloc( host.soundpool, need );
		if( owned )
		{
			memcpy( owned, pack, need );
			Mem_Free( f );
			Con_Reportf( "Xash3D GameCube: G123 WAV migrated bytes=%u file=%u\n",
				(uint)owned->size, (uint)filesize );
			return owned;
		}

		Con_Reportf( "Xash3D GameCube: G122 WAV in-place pack bytes=%u file=%u\n",
			(uint)pack->size, (uint)filesize );
		return pack;
	}
}
#endif

/*
================
FS_LoadSound

loading and unpack to wav any known sound
================
*/
wavdata_t *FS_LoadSound( const char *filename, const byte *buffer, size_t size )
{
	const char *ext = COM_FileExtension( filename );
	string loadname;
	qboolean anyformat = true;

	Sound_Reset(); // clear old sounddata
	Q_strncpy( loadname, filename, sizeof( loadname ));

	if( !COM_StringEmpty( ext ))
	{
		// we needs to compare file extension with list of supported formats
		// and be sure what is real extension, not a filename with dot
		for( const loadwavfmt_t *format = sound.loadformats; format && format->ext; format++ )
		{
			if( !Q_stricmp( format->ext, ext ))
			{
				COM_StripExtension( loadname );
				anyformat = false;
				break;
			}
		}
	}

	// special mode: skip any checks, load file from buffer
	if( filename[0] == '#' && buffer && size )
		goto load_internal;

	// now try all the formats in the selected list
	for( const loadwavfmt_t *format = sound.loadformats; format && format->ext; format++)
	{
		if( anyformat || !Q_stricmp( ext, format->ext ))
		{
			qboolean success = false;
			fs_offset_t filesize = 0;
			string path;

			Q_snprintf( path, sizeof( path ), DEFAULT_SOUNDPATH "%s.%s", loadname, format->ext );

			byte *f = FS_LoadFile( path, &filesize, false );
			if( f && filesize > 0 )
			{
				success = format->loadfunc( path, f, filesize );
#if XASH_GAMECUBE
				if( success && sound.gc_inplace_file )
				{
					wavdata_t *pack = SoundPackInPlaceFile( f, filesize );
					if( pack )
						return pack;
					pack = SoundPack();
					Mem_Free( f );
					if( pack )
						return pack;
					return NULL;
				}
#endif
				Mem_Free( f ); // release buffer
			}

			if( success )
				return SoundPack(); // loaded

			Q_snprintf( path, sizeof( path ), "%s.%s", loadname, format->ext );
			f = FS_LoadFile( path, &filesize, false );
			if( f && filesize > 0 )
			{
				success = format->loadfunc( path, f, filesize );
#if XASH_GAMECUBE
				if( success && sound.gc_inplace_file )
				{
					wavdata_t *pack = SoundPackInPlaceFile( f, filesize );
					if( pack )
						return pack;
					pack = SoundPack();
					Mem_Free( f );
					if( pack )
						return pack;
					return NULL;
				}
#endif
				Mem_Free( f ); // release buffer
			}

			if( success )
				return SoundPack();
		}
	}

load_internal:
	for( const loadwavfmt_t *format = sound.loadformats; format && format->ext; format++ )
	{
		if( anyformat || !Q_stricmp( ext, format->ext ))
		{
			if( buffer && size > 0  )
			{
				if( format->loadfunc( loadname, buffer, size ))
					return SoundPack(); // loaded
			}
		}
	}

	if( filename[0] != '#' )
		Con_DPrintf( S_WARN "%s: couldn't load \"%s\"\n", __func__, loadname );

	return NULL;
}

/*
================
Sound_FreeSound

free WAV buffer
================
*/
void FS_FreeSound( wavdata_t *pack )
{
	if( !pack ) return;
	Mem_Free( pack );
}

/*
================
FS_OpenStream

open and reading basic info from sound stream
================
*/
stream_t *FS_OpenStream( const char *filename )
{
	const char	*ext = COM_FileExtension( filename );
	string		loadname;
	qboolean		anyformat = true;
	stream_t		*stream = NULL;

	Sound_Reset(); // clear old streaminfo
	Q_strncpy( loadname, filename, sizeof( loadname ));

	if( !COM_StringEmpty( ext ))
	{
		// we needs to compare file extension with list of supported formats
		// and be sure what is real extension, not a filename with dot
		for( const streamfmt_t *format = sound.streamformat; format && format->ext; format++ )
		{
			if( !Q_stricmp( format->ext, ext ))
			{
				COM_StripExtension( loadname );
				anyformat = false;
				break;
			}
		}
	}

	// now try all the formats in the selected list
	for( const streamfmt_t *format = sound.streamformat; format && format->ext; format++)
	{
		if( anyformat || !Q_stricmp( ext, format->ext ))
		{
			string path;

			Q_snprintf( path, sizeof( path ), "%s.%s", loadname, format->ext );

			if(( stream = format->openfunc( path )) != NULL )
			{
				stream->format = format;
				return stream; // done
			}
		}
	}

	// compatibility with original Xash3D, try media/ folder
	if( Q_strncmp( filename, "media/", sizeof( "media/" ) - 1 ))
	{
		Q_snprintf( loadname, sizeof( loadname ), "media/%s", filename );
		stream = FS_OpenStream( loadname );
	}
	else
	{
		Con_Reportf( "%s: couldn't open \"%s\" or \"%s\"\n", __func__, filename + 6, filename );
	}

	return stream;
}

/*
================
FS_ReadStream

extract stream as wav-data and put into buffer, move file pointer
================
*/
int FS_ReadStream( stream_t *stream, int bytes, void *buffer )
{
	if( !stream || !stream->format || !stream->format->readfunc )
		return 0;

	if( bytes <= 0 || buffer == NULL )
		return 0;

	return stream->format->readfunc( stream, bytes, buffer );
}

/*
================
FS_GetStreamPos

get stream position (in bytes)
================
*/
int FS_GetStreamPos( stream_t *stream )
{
	if( !stream || !stream->format || !stream->format->getposfunc )
		return -1;

	return stream->format->getposfunc( stream );
}

/*
================
FS_SetStreamPos

set stream position (in bytes)
================
*/
int FS_SetStreamPos( stream_t *stream, int newpos )
{
	if( !stream || !stream->format || !stream->format->setposfunc )
		return -1;

	return stream->format->setposfunc( stream, newpos );
}

/*
================
FS_FreeStream

close sound stream
================
*/
void FS_FreeStream( stream_t *stream )
{
	if( !stream || !stream->format || !stream->format->freefunc )
		return;

	stream->format->freefunc( stream );
}

#if XASH_ENGINE_TESTS
#define IMPLEMENT_SOUNDLIB_FUZZ_TARGET( export, target ) \
int EXPORT export( const uint8_t *Data, size_t Size ); \
int EXPORT export( const uint8_t *Data, size_t Size ) \
{ \
	wavdata_t *wav; \
	host.type = HOST_NORMAL; \
	Memory_Init(); \
	Sound_Init(); \
	if( target( "#internal", Data, Size )) \
	{ \
		wav = SoundPack(); \
		FS_FreeSound( wav ); \
	} \
	Sound_Shutdown(); \
	return 0; \
} \

IMPLEMENT_SOUNDLIB_FUZZ_TARGET( Fuzz_Sound_LoadMPG, Sound_LoadMPG )
IMPLEMENT_SOUNDLIB_FUZZ_TARGET( Fuzz_Sound_LoadWAV, Sound_LoadWAV )
#endif
