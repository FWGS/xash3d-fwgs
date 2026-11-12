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
#if XASH_GAMECUBE
#include "platform/platform.h"
#include "../../platform/gamecube/mem_gamecube.h"
#endif

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

#if XASH_GAMECUBE
/* Sound files read into the model skin scratch, idle during play: a 38 KB file
 * buffer was failing on the fragmented heap with 300 KB free. */
extern byte *Mod_GCSoundScratch( fs_offset_t size );
extern qboolean Mod_GCIsSoundScratch( const void *ptr );

/* CapFaces WorldDL holds studio *t skins in the shared tex scratch AND the
 * filesystem pool is often tip-empty (FS_LoadZIPFile OOM). Tip-safe combat
 * WAVs still need a place to land — keep a dedicated 20 KiB file buffer. */
static byte s_gc_sfx_filebuf[20u * 1024u];
static qboolean s_gc_sfx_filebuf_busy;

static qboolean SND_IsTempFileBuf( const void *ptr )
{
	return ptr == (const void *)s_gc_sfx_filebuf || Mod_GCIsSoundScratch( ptr );
}

static byte *SND_LOADFILE( const char *path, fs_offset_t *size )
{
	file_t *f;
	byte *buf;
	fs_offset_t cached_size;

	/* CapFaces listen: skip FS_FileSize prefetch probe — it Opens via G278
	 * and burns a file_t/DVD slot so the real Open often misses (garand
	 * FileSize G278 then Open miss → button10 fallback). */
	if( !Sys_CheckParm( "-gcmap" ) && !Sys_CheckParm( "-gclistenserver" ))
	{
		/* The menu prefetcher stages the opening tram VO and other read-ahead
		 * sounds in ARAM. Resolve the exact file size first, then copy directly
		 * into the normal sound scratch buffer so the decoder and lifetime rules
		 * remain unchanged. */
		cached_size = FS_FileSize( path, false );
		if( cached_size > 0 && cached_size <= 0xFFFFFFFFu
			&& GC_PrefetchContains( path, (unsigned int)cached_size ))
		{
			buf = Mod_GCSoundScratch( cached_size );
			if( buf && GC_PrefetchRead( path, buf, (unsigned int)cached_size ))
			{
				*size = cached_size;
				return buf;
			}
		}
	}

	/* Menu New Game AND CapFaces listen (-gcmap/-gclistenserver) are tip-
	 * starved — do not FS_LoadFile-malloc the WAV. Open into scratch/filebuf. */
	if( !GC_MenuRunsServer()
		&& !Sys_CheckParm( "-gcmap" )
		&& !Sys_CheckParm( "-gclistenserver" ))
		return FS_LoadFile( path, size, false );
	f = FS_Open( path, "rb", false );
	if( !f )
	{
		/* G278 SysOpen ENOENT is silent — still try FS_LoadFile so media/sfx_*
		 * can land via searchpath when the absolute gcdisc open misses. */
		Con_Reportf( "Xash3D GameCube: SFX FS_Open miss %s → FS_LoadFile\n", path );
		return FS_LoadFile( path, size, false );
	}
	*size = FS_FileLength( f );
	buf = Mod_GCSoundScratch( *size );
	if( !buf && !s_gc_sfx_filebuf_busy && *size <= (fs_offset_t)sizeof( s_gc_sfx_filebuf ))
	{
		buf = s_gc_sfx_filebuf;
		s_gc_sfx_filebuf_busy = true;
	}
	if( buf )
	{
		if( FS_Read( f, buf, *size ) != *size
			|| *size < 12
			|| memcmp( buf, "RIFF", 4 ) != 0 )
		{
			/* Hybrid FST can Open a weapons/* name at a stale offset — reject
			 * non-RIFF so the media/sfx_* fallback can take over. */
			Con_Reportf( "Xash3D GameCube: SFX RIFF reject %s size=%d head=%02x%02x%02x%02x filebuf=%d\n",
				path, (int)*size,
				(uint)( buf ? buf[0] : 0 ), (uint)( buf ? buf[1] : 0 ),
				(uint)( buf ? buf[2] : 0 ), (uint)( buf ? buf[3] : 0 ),
				buf == s_gc_sfx_filebuf ? 1 : 0 );
			if( buf == s_gc_sfx_filebuf )
				s_gc_sfx_filebuf_busy = false;
			buf = NULL;
		}
		FS_Close( f );
		if( buf )
			return buf;
		/* Fall through to FS_LoadFile / caller media fallback. */
		return NULL;
	}
	FS_Close( f );
	Con_Reportf( "Xash3D GameCube: SFX open ok but no scratch/filebuf %s size=%d → FS_LoadFile\n",
		path, (int)*size );
	/* Last resort: fragmented heap may still service a tiny FS_LoadFile. */
	return FS_LoadFile( path, size, false );
}
#define SND_FREEFILE( f ) do { \
	if( (f) == s_gc_sfx_filebuf ) s_gc_sfx_filebuf_busy = false; \
	else if( !Mod_GCIsSoundScratch( f )) Mem_Free( f ); \
} while( 0 )
#else
#define SND_LOADFILE( path, size ) FS_LoadFile( path, size, false )
#define SND_FREEFILE( f ) Mem_Free( f )
#endif

#if XASH_GAMECUBE
/* Tip CapFaces: soundpool TryMalloc often fails while libc still has ~12 KiB. */
#define GC_SFX_PACK_SLOTS 4
#define GC_SFX_PACK_BYTES ( sizeof( wavdata_t ) + ( 12u * 1024u ))
static byte s_gc_sfx_pack_slots[GC_SFX_PACK_SLOTS][GC_SFX_PACK_BYTES];
static qboolean s_gc_sfx_pack_busy[GC_SFX_PACK_SLOTS];

static wavdata_t *SoundPackAlloc( size_t bytes )
{
	wavdata_t *pack = Mem_TryMalloc( host.soundpool, bytes );
	int i;

	if( pack )
		return pack;
	if( bytes > GC_SFX_PACK_BYTES )
		return NULL;
	for( i = 0; i < GC_SFX_PACK_SLOTS; i++ )
	{
		if( s_gc_sfx_pack_busy[i] )
			continue;
		memset( s_gc_sfx_pack_slots[i], 0, bytes );
		s_gc_sfx_pack_busy[i] = true;
		Con_Reportf( "Xash3D GameCube: SoundPack static slot=%d bytes=%u\n", i, (uint)bytes );
		return (wavdata_t *)s_gc_sfx_pack_slots[i];
	}
	return NULL;
}
#endif

static MALLOC_LIKE( FS_FreeSound, 1 ) wavdata_t *SoundPack( void )
{
#if XASH_GAMECUBE
	wavdata_t *pack = SoundPackAlloc( sizeof( *pack ) + sound.size );

	if( !pack )
		return NULL;
#else
	wavdata_t *pack = Mem_Malloc( host.soundpool, sizeof( *pack ) + sound.size );
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

		/* A smaller copy is only worth it if it saves real memory. */
		owned = filesize - need >= 4096 ? Mem_Malloc( host.soundpool, need ) : NULL;
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

			byte *f = SND_LOADFILE( path, &filesize );
			if( f && filesize > 0 )
			{
				success = format->loadfunc( path, f, filesize );
#if XASH_GAMECUBE
				if( success && sound.gc_inplace_file )
				{
					/* Scratch/static filebufs must SoundPack()-copy; inplace
					 * would alias a non-heap buffer into the sfx cache. */
					wavdata_t *pack = SND_IsTempFileBuf( f ) ? NULL : SoundPackInPlaceFile( f, filesize );
					if( pack )
						return pack;
					pack = SoundPack();
					SND_FREEFILE( f );
					if( pack )
						return pack;
					return NULL;
				}
#endif
				SND_FREEFILE( f ); // release buffer
			}

			if( success )
				return SoundPack(); // loaded

			Q_snprintf( path, sizeof( path ), "%s.%s", loadname, format->ext );
			f = SND_LOADFILE( path, &filesize );
			if( f && filesize > 0 )
			{
				success = format->loadfunc( path, f, filesize );
#if XASH_GAMECUBE
				if( success && sound.gc_inplace_file )
				{
					wavdata_t *pack = SND_IsTempFileBuf( f ) ? NULL : SoundPackInPlaceFile( f, filesize );
					if( pack )
						return pack;
					pack = SoundPack();
					SND_FREEFILE( f );
					if( pack )
						return pack;
					return NULL;
				}
#endif
				SND_FREEFILE( f ); // release buffer
			}

			if( success )
				return SoundPack();
		}
	}

#if XASH_GAMECUBE
	/* G278: CapFaces combat clips mirrored to shallow media/sfx_<base>.wav.
	 * Try after sound/ so a failed media Open cannot burn DVD/file_t slots
	 * before the known-good sound/buttons|weapons G278 path. */
	if( filename[0] != '#' )
	{
		const char *slash = Q_strrchr( loadname, '/' );
		const char *base = slash ? slash + 1 : loadname;

		for( const loadwavfmt_t *format = sound.loadformats; format && format->ext; format++ )
		{
			if( anyformat || !Q_stricmp( ext, format->ext ))
			{
				qboolean success = false;
				fs_offset_t filesize = 0;
				string path;

				Q_snprintf( path, sizeof( path ), "media/sfx_%s.%s", base, format->ext );
				byte *f = SND_LOADFILE( path, &filesize );
				if( f && filesize > 0 )
				{
					success = format->loadfunc( path, f, filesize );
					if( success && sound.gc_inplace_file )
					{
						wavdata_t *pack = SND_IsTempFileBuf( f ) ? NULL : SoundPackInPlaceFile( f, filesize );
						if( pack )
						{
							Con_Reportf( "Xash3D GameCube: SFX media hit %s\n", path );
							return pack;
						}
						pack = SoundPack();
						SND_FREEFILE( f );
						if( pack )
						{
							Con_Reportf( "Xash3D GameCube: SFX media hit %s\n", path );
							return pack;
						}
						return NULL;
					}
					SND_FREEFILE( f );
				}
				if( success )
				{
					Con_Reportf( "Xash3D GameCube: SFX media hit %s\n", path );
					return SoundPack();
				}
			}
		}
	}
#endif

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
#if XASH_GAMECUBE
	{
		int i;
		for( i = 0; i < GC_SFX_PACK_SLOTS; i++ )
		{
			if( pack == (wavdata_t *)s_gc_sfx_pack_slots[i] )
			{
				s_gc_sfx_pack_busy[i] = false;
				return;
			}
		}
	}
#endif
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

#if XASH_LLVM_LIBFUZZER
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
#endif // XASH_LLVM_LIBFUZZER
