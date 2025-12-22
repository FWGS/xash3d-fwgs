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
}

static MALLOC_LIKE( FS_FreeSound, 1 ) wavdata_t *SoundPack( void )
{
	wavdata_t *pack = Mem_Malloc( host.soundpool, sizeof( *pack ) + sound.size );

	pack->size = sound.size;
	pack->loop_start = sound.loopstart;
	pack->samples = sound.samples;
	pack->type = sound.type;
	pack->flags = sound.flags;
	pack->rate = sound.rate;
	pack->width = sound.width;
	pack->channels = sound.channels;
	memcpy( pack->buffer, sound.wav, sound.size );

	Mem_Free( sound.wav );
	sound.wav = NULL;

	return pack;
}

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
	const loadwavfmt_t *format;

	Sound_Reset(); // clear old sounddata
	Q_strncpy( loadname, filename, sizeof( loadname ));

	if( !COM_StringEmpty( ext ))
	{
		// we needs to compare file extension with list of supported formats
		// and be sure what is real extension, not a filename with dot
		for( format = sound.loadformats; format && format->ext; format++ )
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
	for( format = sound.loadformats; format && format->ext; format++)
	{
		if( anyformat || !Q_stricmp( ext, format->ext ))
		{
			qboolean success = false;
			fs_offset_t filesize = 0;
			byte *f;
			string path;

			Q_snprintf( path, sizeof( path ), DEFAULT_SOUNDPATH "%s.%s", loadname, format->ext );

			f = FS_LoadFile( path, &filesize, false );
			if( f && filesize > 0 )
			{
				success = format->loadfunc( path, f, filesize );
				Mem_Free( f ); // release buffer
			}

			if( success )
				return SoundPack(); // loaded

			Q_snprintf( path, sizeof( path ), "%s.%s", loadname, format->ext );
			f = FS_LoadFile( path, &filesize, false );
			if( f && filesize > 0 )
			{
				success = format->loadfunc( path, f, filesize );
				Mem_Free( f ); // release buffer
			}

			if( success )
				return SoundPack();
		}
	}

load_internal:
	for( format = sound.loadformats; format && format->ext; format++ )
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
	const streamfmt_t	*format;
	stream_t		*stream = NULL;

	Sound_Reset(); // clear old streaminfo
	Q_strncpy( loadname, filename, sizeof( loadname ));

	if( !COM_StringEmpty( ext ))
	{
		// we needs to compare file extension with list of supported formats
		// and be sure what is real extension, not a filename with dot
		for( format = sound.streamformat; format && format->ext; format++ )
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
	for( format = sound.streamformat; format && format->ext; format++)
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
