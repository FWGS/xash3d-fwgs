/*
probe_save_gc.c - GameCube probe-only RAM save bank for G94 (-gcnewsaveload)
Copyright (C) 2026 xash3d-gc contributors

When Dolphin boots without SD, GCube_HasWritableStorage is false and save is
skipped. -gcnewsaveload enables a small in-memory file bank so the same
SV_SaveGame / SV_LoadGame / G46 confirm path can round-trip on disc-only probes.
Hardware/SD continues to use real fat:/sd: storage (G28/G71).
*/
#include "build.h"

#if XASH_GAMECUBE

#include <string.h>
#include <stdio.h>
#include "crtlib.h"
#include "filesystem.h"
#include "filesystem_internal.h"
#include "common/com_strings.h"

extern int Sys_CheckParm( const char *parm );

#define GC_PROBE_SAVE_FD      (-9001)
#define GC_PROBE_SAVE_CAP     (160 * 1024)
#define GC_PROBE_SAVE_FILES   8
#define GC_PROBE_SAVE_NAMESZ  96

typedef struct gc_probe_save_file_s
{
	char name[GC_PROBE_SAVE_NAMESZ];
	size_t offset;
	size_t length;
	qboolean used;
} gc_probe_save_file_t;

typedef struct gc_probe_save_open_s
{
	int slot;
	fs_offset_t pos;
	qboolean writing;
} gc_probe_save_open_t;

static byte *gc_probe_save_blob;
static size_t gc_probe_save_used;
static gc_probe_save_file_t gc_probe_save_files[GC_PROBE_SAVE_FILES];
static int gc_probe_save_nfiles;
static gc_probe_save_open_t gc_probe_save_opens[4];
static int gc_probe_save_nopens;
static qboolean gc_probe_save_logged;
static qboolean gc_probe_opens_inited;

static qboolean GC_ProbeSaveEnsureBlob( void )
{
	if( gc_probe_save_blob )
		return true;
	gc_probe_save_blob = (byte *)malloc( GC_PROBE_SAVE_CAP );
	if( !gc_probe_save_blob )
	{
		Con_Reportf( S_ERROR "Xash3D GameCube: G94 probe save bank alloc failed (%d Kb)\n",
			GC_PROBE_SAVE_CAP / 1024 );
		return false;
	}
	memset( gc_probe_save_blob, 0, GC_PROBE_SAVE_CAP );
	return true;
}

static qboolean GC_ProbeSaveEnabled( void )
{
	return Sys_CheckParm( "-gcnewsaveload" ) != 0;
}

static const char *GC_ProbeSaveBasename( const char *path )
{
	const char *slash;

	if( !path || !path[0] )
		return NULL;
	slash = Q_strrchr( path, '/' );
	if( !slash )
		slash = Q_strrchr( path, '\\' );
	return slash ? slash + 1 : path;
}

static qboolean GC_ProbeSavePathMatch( const char *path )
{
	if( !GC_ProbeSaveEnabled() || !path )
		return false;
	/* Engine save paths are DEFAULT_SAVE_DIRECTORY "save/" under the write root. */
	if( !Q_strnicmp( path, "save/", 5 ) || !Q_strnicmp( path, "save\\", 5 ))
		return true;
	if( Q_stristr( path, "/save/" ) || Q_stristr( path, "\\save\\" ))
		return true;
	if( !Q_strnicmp( path, "gcprobe:", 8 ))
		return true;
	return false;
}

static int GC_ProbeSaveFindSlot( const char *basename )
{
	int i;

	for( i = 0; i < gc_probe_save_nfiles; i++ )
	{
		if( gc_probe_save_files[i].used && !Q_stricmp( gc_probe_save_files[i].name, basename ))
			return i;
	}
	return -1;
}

static int GC_ProbeSaveAllocSlot( const char *basename )
{
	int i = GC_ProbeSaveFindSlot( basename );

	if( i >= 0 )
	{
		/* Truncate for rewrite. */
		gc_probe_save_files[i].length = 0;
		return i;
	}
	if( gc_probe_save_nfiles >= GC_PROBE_SAVE_FILES )
		return -1;
	i = gc_probe_save_nfiles++;
	Q_strncpy( gc_probe_save_files[i].name, basename, sizeof( gc_probe_save_files[i].name ));
	gc_probe_save_files[i].offset = gc_probe_save_used;
	gc_probe_save_files[i].length = 0;
	gc_probe_save_files[i].used = true;
	return i;
}

qboolean GC_ProbeSaveIsHandle( const file_t *file );
fs_offset_t GC_ProbeSaveWrite( file_t *file, const void *data, size_t datasize );
fs_offset_t GC_ProbeSaveRead( file_t *file, void *buffer, size_t buffersize );
void GC_ProbeSaveClose( file_t *file );
fs_offset_t GC_ProbeSaveSeek( file_t *file, fs_offset_t offset, int whence );
void GC_ProbeSaveInitOpens( void );

static int GC_ProbeSaveOpenIndex( const file_t *file )
{
	int i;

	if( !file || file->handle != GC_PROBE_SAVE_FD )
		return -1;
	i = (int)file->offset - 1;
	if( i < 0 || i >= (int)( sizeof( gc_probe_save_opens ) / sizeof( gc_probe_save_opens[0] )))
		return -1;
	if( gc_probe_save_opens[i].slot < 0 )
		return -1;
	return i;
}

qboolean GC_ProbeSaveActive( void )
{
	return GC_ProbeSaveEnabled();
}

qboolean GC_ProbeSaveFileExists( const char *filename )
{
	const char *base;

	if( !GC_ProbeSavePathMatch( filename ))
		return false;
	base = GC_ProbeSaveBasename( filename );
	return base && GC_ProbeSaveFindSlot( base ) >= 0;
}

file_t *GC_ProbeSaveOpen( const char *filepath, const char *mode )
{
	const char *base;
	file_t *file;
	int slot;
	int oi;
	qboolean writing;

	if( !GC_ProbeSavePathMatch( filepath ))
		return NULL;
	base = GC_ProbeSaveBasename( filepath );
	if( !base || !base[0] )
		return NULL;

	if( !gc_probe_opens_inited )
	{
		GC_ProbeSaveInitOpens();
		gc_probe_opens_inited = true;
	}
	if( !GC_ProbeSaveEnsureBlob())
		return NULL;

	writing = ( mode[0] == 'w' || mode[0] == 'a' || mode[0] == 'e' || Q_strchr( mode, '+' ));

	if( writing )
		slot = GC_ProbeSaveAllocSlot( base );
	else
		slot = GC_ProbeSaveFindSlot( base );

	if( slot < 0 )
		return NULL;

	for( oi = 0; oi < (int)( sizeof( gc_probe_save_opens ) / sizeof( gc_probe_save_opens[0] )); oi++ )
	{
		if( gc_probe_save_opens[oi].slot < 0 )
			break;
	}
	if( oi >= (int)( sizeof( gc_probe_save_opens ) / sizeof( gc_probe_save_opens[0] )))
		return NULL;

	file = (file_t *)Mem_Calloc( fs_mempool, sizeof( *file ));
	file->handle = GC_PROBE_SAVE_FD;
	file->ungetc = EOF;
	file->filetime = 0;
	file->searchpath = NULL;
	file->real_length = (fs_offset_t)gc_probe_save_files[slot].length;
	file->position = ( mode[0] == 'a' ) ? file->real_length : 0;
	file->offset = (fs_offset_t)( oi + 1 ); /* open-table cookie */
	file->flags = 0;
	file->ztk = NULL;
	file->buff_ind = 0;
	file->buff_len = 0;

	gc_probe_save_opens[oi].slot = slot;
	gc_probe_save_opens[oi].pos = file->position;
	gc_probe_save_opens[oi].writing = writing;
	if( oi >= gc_probe_save_nopens )
		gc_probe_save_nopens = oi + 1;

	if( !gc_probe_save_logged )
	{
		Con_Reportf( "Xash3D GameCube: G94 probe save bank ready (RAM, no SD)\n" );
		gc_probe_save_logged = true;
	}
	Con_Reportf( "Xash3D GameCube: G94 probe save %s name=%s\n",
		writing ? "write" : "read", base );
	return file;
}

qboolean GC_ProbeSaveIsHandle( const file_t *file )
{
	return file && file->handle == GC_PROBE_SAVE_FD;
}

fs_offset_t GC_ProbeSaveWrite( file_t *file, const void *data, size_t datasize )
{
	int oi;
	gc_probe_save_file_t *ent;
	size_t end;

	if( !GC_ProbeSaveIsHandle( file ) || !data )
		return 0;
	oi = GC_ProbeSaveOpenIndex( file );
	if( oi < 0 )
		return 0;
	ent = &gc_probe_save_files[gc_probe_save_opens[oi].slot];

	/* Place new writes at end of blob if this is a fresh truncate slot. */
	if( ent->length == 0 && gc_probe_save_opens[oi].pos == 0 )
		ent->offset = gc_probe_save_used;

	end = ent->offset + (size_t)gc_probe_save_opens[oi].pos + datasize;
	if( end > GC_PROBE_SAVE_CAP )
	{
		Con_Reportf( S_ERROR "Xash3D GameCube: G94 probe save bank full (%s)\n", ent->name );
		return 0;
	}
	memcpy( gc_probe_save_blob + ent->offset + (size_t)gc_probe_save_opens[oi].pos, data, datasize );
	gc_probe_save_opens[oi].pos += (fs_offset_t)datasize;
	if( (size_t)gc_probe_save_opens[oi].pos > ent->length )
		ent->length = (size_t)gc_probe_save_opens[oi].pos;
	if( ent->offset + ent->length > gc_probe_save_used )
		gc_probe_save_used = ent->offset + ent->length;
	file->position = gc_probe_save_opens[oi].pos;
	file->real_length = (fs_offset_t)ent->length;
	return (fs_offset_t)datasize;
}

fs_offset_t GC_ProbeSaveRead( file_t *file, void *buffer, size_t buffersize )
{
	int oi;
	gc_probe_save_file_t *ent;
	size_t avail;
	size_t n;

	if( !GC_ProbeSaveIsHandle( file ) || !buffer || buffersize == 0 )
		return 0;
	oi = GC_ProbeSaveOpenIndex( file );
	if( oi < 0 )
		return 0;
	ent = &gc_probe_save_files[gc_probe_save_opens[oi].slot];
	if( (size_t)gc_probe_save_opens[oi].pos >= ent->length )
		return 0;
	avail = ent->length - (size_t)gc_probe_save_opens[oi].pos;
	n = ( buffersize < avail ) ? buffersize : avail;
	memcpy( buffer, gc_probe_save_blob + ent->offset + (size_t)gc_probe_save_opens[oi].pos, n );
	gc_probe_save_opens[oi].pos += (fs_offset_t)n;
	file->position = gc_probe_save_opens[oi].pos;
	return (fs_offset_t)n;
}

void GC_ProbeSaveClose( file_t *file )
{
	int oi;

	if( !GC_ProbeSaveIsHandle( file ))
		return;
	oi = GC_ProbeSaveOpenIndex( file );
	if( oi >= 0 )
	{
		if( gc_probe_save_opens[oi].writing )
		{
			gc_probe_save_file_t *ent = &gc_probe_save_files[gc_probe_save_opens[oi].slot];
			Con_Reportf( "Xash3D GameCube: G94 probe save committed name=%s bytes=%d\n",
				ent->name, (int)ent->length );
		}
		gc_probe_save_opens[oi].slot = -1;
	}
}

fs_offset_t GC_ProbeSaveSeek( file_t *file, fs_offset_t offset, int whence )
{
	int oi;
	gc_probe_save_file_t *ent;
	fs_offset_t pos;

	if( !GC_ProbeSaveIsHandle( file ))
		return -1;
	oi = GC_ProbeSaveOpenIndex( file );
	if( oi < 0 )
		return -1;
	ent = &gc_probe_save_files[gc_probe_save_opens[oi].slot];
	pos = gc_probe_save_opens[oi].pos;
	if( whence == SEEK_SET )
		pos = offset;
	else if( whence == SEEK_CUR )
		pos += offset;
	else if( whence == SEEK_END )
		pos = (fs_offset_t)ent->length + offset;
	if( pos < 0 )
		pos = 0;
	gc_probe_save_opens[oi].pos = pos;
	file->position = pos;
	return pos;
}

void GC_ProbeSaveInitOpens( void )
{
	int i;

	for( i = 0; i < (int)( sizeof( gc_probe_save_opens ) / sizeof( gc_probe_save_opens[0] )); i++ )
		gc_probe_save_opens[i].slot = -1;
	gc_probe_save_nopens = 0;
}

#else
/* Non-GameCube: stubs unused. */
#endif /* XASH_GAMECUBE */
