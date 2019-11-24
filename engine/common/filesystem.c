/*
filesystem.c - game filesystem based on DP fs
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "build.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#if XASH_WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <errno.h>
#endif
#include "miniz.h" // header-only zlib replacement
#include "common.h"
#include "wadfile.h"
#include "filesystem.h"
#include "library.h"
#include "mathlib.h"
#include "protocol.h"

#define FILE_COPY_SIZE		(1024 * 1024)
#define FILE_BUFF_SIZE		(2048)

// PAK errors
#define PAK_LOAD_OK			0
#define PAK_LOAD_COULDNT_OPEN		1
#define PAK_LOAD_BAD_HEADER		2
#define PAK_LOAD_BAD_FOLDERS		3
#define PAK_LOAD_TOO_MANY_FILES	4
#define PAK_LOAD_NO_FILES		5
#define PAK_LOAD_CORRUPTED		6

// WAD errors
#define WAD_LOAD_OK			0
#define WAD_LOAD_COULDNT_OPEN		1
#define WAD_LOAD_BAD_HEADER		2
#define WAD_LOAD_BAD_FOLDERS		3
#define WAD_LOAD_TOO_MANY_FILES	4
#define WAD_LOAD_NO_FILES		5
#define WAD_LOAD_CORRUPTED		6

// ZIP errors
#define ZIP_LOAD_OK			0
#define ZIP_LOAD_COULDNT_OPEN		1
#define ZIP_LOAD_BAD_HEADER		2
#define ZIP_LOAD_BAD_FOLDERS		3
#define ZIP_LOAD_NO_FILES		5
#define ZIP_LOAD_CORRUPTED		6

typedef struct stringlist_s
{
	// maxstrings changes as needed, causing reallocation of strings[] array
	int		maxstrings;
	int		numstrings;
	char		**strings;
} stringlist_t;

typedef struct wadtype_s
{
	char		*ext;
	signed char		type;
} wadtype_t;

struct file_s
{
	int		handle;			// file descriptor
	fs_offset_t		real_length;		// uncompressed file size (for files opened in "read" mode)
	fs_offset_t		position;			// current position in the file
	fs_offset_t		offset;			// offset into the package (0 if external file)
	int		ungetc;			// single stored character from ungetc, cleared to EOF when read
	time_t		filetime;			// pak, wad or real filetime
						// contents buffer
	fs_offset_t		buff_ind, buff_len;		// buffer current index and length
	byte		buff[FILE_BUFF_SIZE];	// intermediate buffer
};

struct wfile_s
{
	string		filename;
	int		infotableofs;
	byte		*mempool;			// W_ReadLump temp buffers
	int		numlumps;
	file_t		*handle;
	dlumpinfo_t	*lumps;
	time_t		filetime;
};

typedef struct pack_s
{
	string		filename;
	int		handle;
	int		numfiles;
	time_t		filetime;			// common for all packed files
	dpackfile_t	*files;
} pack_t;

typedef struct zipfile_s
{
	char		name[MAX_SYSPATH];
	fs_offset_t	offset; // offset of local file header
	fs_offset_t	size; //original file size
	fs_offset_t	compressed_size; // compressed file size
} zipfile_t;

typedef struct zip_s
{
	string		filename;
	byte		*mempool;
	file_t		*handle;
	int		numfiles;
	time_t		filetime;
	zipfile_t	*files;
} zip_t;

typedef struct searchpath_s
{
	string		filename;
	pack_t		*pack;
	wfile_t		*wad;
	zip_t		*zip;
	int		flags;
	struct searchpath_s *next;
} searchpath_t;

byte			*fs_mempool;
searchpath_t		*fs_searchpaths = NULL;	// chain
searchpath_t		fs_directpath;		// static direct path
char			fs_basedir[MAX_SYSPATH];	// base game directory
char			fs_gamedir[MAX_SYSPATH];	// game current directory
char			fs_writedir[MAX_SYSPATH];	// path that game allows to overwrite, delete and rename files (and create new of course)

qboolean		fs_ext_path = false;	// attempt to read\write from ./ or ../ pathes
#if !XASH_WIN32
qboolean		fs_caseinsensitive = true; // try to search missing files
#endif


static void FS_InitMemory( void );
static searchpath_t *FS_FindFile( const char *name, int *index, qboolean gamedironly );
static dlumpinfo_t *W_FindLump( wfile_t *wad, const char *name, const signed char matchtype );
static dpackfile_t *FS_AddFileToPack( const char* name, pack_t *pack, fs_offset_t offset, fs_offset_t size );
void Zip_Close( zip_t *zip );
static byte *W_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly );
static wfile_t *W_Open( const char *filename, int *errorcode );
static qboolean FS_SysFolderExists( const char *path );
static int FS_SysFileTime( const char *filename );
static signed char W_TypeFromExt( const char *lumpname );
static const char *W_ExtFromType( signed char lumptype );
static void FS_Purge( file_t* file );

/*
=============================================================================

FILEMATCH COMMON SYSTEM

=============================================================================
*/
static void stringlistinit( stringlist_t *list )
{
	memset( list, 0, sizeof( *list ));
}

static void stringlistfreecontents( stringlist_t *list )
{
	int	i;

	for( i = 0; i < list->numstrings; i++ )
	{
		if( list->strings[i] )
			Mem_Free( list->strings[i] );
		list->strings[i] = NULL;
	}

	if( list->strings )
		Mem_Free( list->strings );

	list->numstrings = 0;
	list->maxstrings = 0;
	list->strings = NULL;
}

static void stringlistappend( stringlist_t *list, char *text )
{
	size_t	textlen;

	if( !Q_stricmp( text, "." ) || !Q_stricmp( text, ".." ))
		return; // ignore the virtual directories

	if( list->numstrings >= list->maxstrings )
	{
		list->maxstrings += 4096;
		list->strings = Mem_Realloc( fs_mempool, list->strings, list->maxstrings * sizeof( *list->strings ));
	}

	textlen = Q_strlen( text ) + 1;
	list->strings[list->numstrings] = Mem_Calloc( fs_mempool, textlen );
	memcpy( list->strings[list->numstrings], text, textlen );
	list->numstrings++;
}

static void stringlistsort( stringlist_t *list )
{
	char	*temp;
	int	i, j;

	// this is a selection sort (finds the best entry for each slot)
	for( i = 0; i < list->numstrings - 1; i++ )
	{
		for( j = i + 1; j < list->numstrings; j++ )
		{
			if( Q_strcmp( list->strings[i], list->strings[j] ) > 0 )
			{
				temp = list->strings[i];
				list->strings[i] = list->strings[j];
				list->strings[j] = temp;
			}
		}
	}
}

// convert names to lowercase because windows doesn't care, but pattern matching code often does
static void listlowercase( stringlist_t *list )
{
	char	*c;
	int	i;

	for( i = 0; i < list->numstrings; i++ )
	{
		for( c = list->strings[i]; *c; c++ )
			*c = Q_tolower( *c );
	}
}

static void listdirectory( stringlist_t *list, const char *path, qboolean lowercase )
{
	int		i;
	signed char *c;
#if XASH_WIN32
	char pattern[4096];
	struct _finddata_t	n_file;
	int		hFile;
#else
	DIR *dir;
	struct dirent *entry;
#endif

#if XASH_WIN32
	Q_snprintf( pattern, sizeof( pattern ), "%s*", path );

	// ask for the directory listing handle
	hFile = _findfirst( pattern, &n_file );
	if( hFile == -1 ) return;

	// start a new chain with the the first name
	stringlistappend( list, n_file.name );
	// iterate through the directory
	while( _findnext( hFile, &n_file ) == 0 )
		stringlistappend( list, n_file.name );
	_findclose( hFile );
#else
	if( !( dir = opendir( path ) ) )
		return;

	// iterate through the directory
	while( ( entry = readdir( dir ) ))
		stringlistappend( list, entry->d_name );
	closedir( dir );
#endif

	// seems not needed anymore
#if 0
	// convert names to lowercase because windows doesn't care, but pattern matching code often does
	if( lowercase )
		listlowercase( list );
#endif
}

/*
=============================================================================

OTHER PRIVATE FUNCTIONS

=============================================================================
*/
/*
==================
FS_FixFileCase

emulate WIN32 FS behaviour when opening local file
==================
*/
static const char *FS_FixFileCase( const char *path )
{
#if !XASH_WIN32 && !XASH_IOS // assume case insensitive
	DIR *dir; struct dirent *entry;
	char path2[PATH_MAX], *fname;

	if( !fs_caseinsensitive )
		return path;

	if( path[0] != '/' )
		Q_snprintf( path2, sizeof( path2 ), "./%s", path );
	else Q_strncpy( path2, path, PATH_MAX );

	fname = Q_strrchr( path2, '/' );

	if( fname )
		*fname++ = 0;
	else
	{
		fname = (char*)path;
		Q_strcpy( path2, ".");
	}

	/* android has too slow directory scanning,
	   so drop out some not useful cases */
	if( fname - path2 > 4 )
	{
		char *point;
		// too many wad textures
		if( !Q_stricmp( fname - 5, ".wad") )
			return path;
		point = Q_strchr( fname, '.' );
		if( point )
		{
			if( !Q_strcmp( point, ".mip") || !Q_strcmp( point, ".dds" ) || !Q_strcmp( point, ".ent" ) )
				return path;
			if( fname[0] == '{' )
				return path;
		}
	}

	//Con_Reportf( "FS_FixFileCase: %s\n", path );

	if( !( dir = opendir( path2 ) ) )
		return path;

	while( ( entry = readdir( dir ) ) )
	{
		if( Q_stricmp( entry->d_name, fname ) )
			continue;

		path = va( "%s/%s", path2, entry->d_name );
		//Con_Reportf( "FS_FixFileCase: %s %s %s\n", path2, fname, entry->d_name );
		break;
	}
	closedir( dir );
#endif
	return path;
}

/*
====================
FS_AddFileToPack

Add a file to the list of files contained into a package
====================
*/
static dpackfile_t *FS_AddFileToPack( const char *name, pack_t *pack, fs_offset_t offset, fs_offset_t size )
{
	int		left, right, middle;
	dpackfile_t	*pfile;

	// look for the slot we should put that file into (binary search)
	left = 0;
	right = pack->numfiles - 1;

	while( left <= right )
	{
		int diff;

		middle = (left + right) / 2;
		diff = Q_stricmp( pack->files[middle].name, name );

		// If we found the file, there's a problem
		if( !diff ) Con_Reportf( S_WARN "package %s contains the file %s several times\n", pack->filename, name );

		// If we're too far in the list
		if( diff > 0 ) right = middle - 1;
		else left = middle + 1;
	}

	// We have to move the right of the list by one slot to free the one we need
	pfile = &pack->files[left];
	memmove( pfile + 1, pfile, (pack->numfiles - left) * sizeof( *pfile ));
	pack->numfiles++;

	Q_strncpy( pfile->name, name, sizeof( pfile->name ));
	pfile->filepos = offset;
	pfile->filelen = size;

	return pfile;
}

/*
============
FS_CreatePath

Only used for FS_Open.
============
*/
void FS_CreatePath( char *path )
{
	char	*ofs, save;

	for( ofs = path + 1; *ofs; ofs++ )
	{
		if( *ofs == '/' || *ofs == '\\' )
		{
			// create the directory
			save = *ofs;
			*ofs = 0;
			_mkdir( path );
			*ofs = save;
		}
	}
}

/*
============
FS_Path_f

debug info
============
*/
void FS_Path_f( void )
{
	searchpath_t	*s;

	Con_Printf( "Current search path:\n" );

	for( s = fs_searchpaths; s; s = s->next )
	{
		if( s->pack ) Con_Printf( "%s (%i files)", s->pack->filename, s->pack->numfiles );
		else if( s->wad ) Con_Printf( "%s (%i files)", s->wad->filename, s->wad->numlumps );
		else if( s->zip ) Con_Printf( "%s (%i files)", s->zip->filename, s->zip->numfiles );
		else Con_Printf( "%s", s->filename );

		if( s->flags & FS_GAMERODIR_PATH ) Con_Printf( " ^2rodir^7" );
		if( s->flags & FS_GAMEDIR_PATH ) Con_Printf( " ^2gamedir^7" );
		if( s->flags & FS_CUSTOM_PATH ) Con_Printf( " ^2custom^7" );
		if( s->flags & FS_NOWRITE_PATH ) Con_Printf( " ^2nowrite^7" );
		if( s->flags & FS_STATIC_PATH ) Con_Printf( " ^2static^7" );

		Con_Printf( "\n" );
	}
}

/*
============
FS_ClearPath_f

only for debug targets
============
*/
void FS_ClearPaths_f( void )
{
	FS_ClearSearchPath();
}

/*
=================
FS_LoadPackPAK

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
pack_t *FS_LoadPackPAK( const char *packfile, int *error )
{
	dpackheader_t	header;
	int		packhandle;
	int		i, numpackfiles;
	pack_t		*pack;
	dpackfile_t	*info;

	packhandle = open( packfile, O_RDONLY|O_BINARY );

#if !XASH_WIN32
	if( packhandle < 0 )
	{
		const char *fpackfile = FS_FixFileCase( packfile );
		if( fpackfile!= packfile )
			packhandle = open( fpackfile, O_RDONLY|O_BINARY );
	}
#endif

	if( packhandle < 0 )
	{
		Con_Reportf( "%s couldn't open\n", packfile );
		if( error ) *error = PAK_LOAD_COULDNT_OPEN;
		return NULL;
	}

	read( packhandle, (void *)&header, sizeof( header ));

	if( header.ident != IDPACKV1HEADER )
	{
		Con_Reportf( "%s is not a packfile. Ignored.\n", packfile );
		if( error ) *error = PAK_LOAD_BAD_HEADER;
		close( packhandle );
		return NULL;
	}

	if( header.dirlen % sizeof( dpackfile_t ))
	{
		Con_Reportf( S_ERROR "%s has an invalid directory size. Ignored.\n", packfile );
		if( error ) *error = PAK_LOAD_BAD_FOLDERS;
		close( packhandle );
		return NULL;
	}

	numpackfiles = header.dirlen / sizeof( dpackfile_t );

	if( numpackfiles > MAX_FILES_IN_PACK )
	{
		Con_Reportf( S_ERROR "%s has too many files ( %i ). Ignored.\n", packfile, numpackfiles );
		if( error ) *error = PAK_LOAD_TOO_MANY_FILES;
		close( packhandle );
		return NULL;
	}

	if( numpackfiles <= 0 )
	{
		Con_Reportf( "%s has no files. Ignored.\n", packfile );
		if( error ) *error = PAK_LOAD_NO_FILES;
		close( packhandle );
		return NULL;
	}

	info = (dpackfile_t *)Mem_Malloc( fs_mempool, sizeof( *info ) * numpackfiles );
	lseek( packhandle, header.dirofs, SEEK_SET );

	if( header.dirlen != read( packhandle, (void *)info, header.dirlen ))
	{
		Con_Reportf( "%s is an incomplete PAK, not loading\n", packfile );
		if( error ) *error = PAK_LOAD_CORRUPTED;
		close( packhandle );
		Mem_Free( info );
		return NULL;
	}

	pack = (pack_t *)Mem_Calloc( fs_mempool, sizeof( pack_t ));
	Q_strncpy( pack->filename, packfile, sizeof( pack->filename ));
	pack->files = (dpackfile_t *)Mem_Calloc( fs_mempool, numpackfiles * sizeof( dpackfile_t ));
	pack->filetime = FS_SysFileTime( packfile );
	pack->handle = packhandle;
	pack->numfiles = 0;

	// parse the directory
	for( i = 0; i < numpackfiles; i++ )
		FS_AddFileToPack( info[i].name, pack, info[i].filepos, info[i].filelen );

	if( error ) *error = PAK_LOAD_OK;
	Mem_Free( info );

	return pack;
}

static zipfile_t *FS_AddFileToZip( const char *name, zip_t *zip, fs_offset_t offset, fs_offset_t size, fs_offset_t compressed_size)
{
	zipfile_t *zipfile = NULL;

	zipfile = &zip->files[zip->numfiles];

	Q_strncpy( zipfile->name, name, MAX_SYSPATH );

	zipfile->size = size;
	zipfile->offset = offset;
	zipfile->compressed_size = compressed_size;

	zip->numfiles++;

	return zipfile;
}

static zip_t *FS_LoadZip( const char *zipfile, int *error )
{
	int		  numpackfiles = 0, i;
	zip_cdf_header_t  header_cdf;
	zip_header_eocd_t header_eocd;
	uint		  signature;
	fs_offset_t	  filepos = 0;
	zipfile_t	  *info = NULL;
	char		  filename_buffer[MAX_SYSPATH];
	zip_t *zip = (zip_t *)Mem_Calloc( fs_mempool, sizeof( zip_t ) );

	zip->handle = FS_Open( zipfile, "rb", true );

#if !XASH_WIN32
	if( !zip->handle )
	{
		const char *fzipfile = FS_FixFileCase( zipfile );
		if( fzipfile != zipfile )
			zip->handle = FS_Open( fzipfile, "rb", true );
	}
#endif

	if( !zip->handle )
	{
		Con_Reportf( S_ERROR "%s couldn't open\n", zipfile );

		if( error )
			*error = ZIP_LOAD_COULDNT_OPEN;

		Zip_Close( zip );
		return NULL;
	}

	if( FS_FileLength( zip->handle ) > UINT_MAX )
	{
		Con_Reportf( S_ERROR "%s bigger than 4GB.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_COULDNT_OPEN;

		Zip_Close( zip );
		return NULL;
	}

	FS_Read( zip->handle, (void *)&signature, sizeof( uint ) );

	if( signature == ZIP_HEADER_EOCD )
	{
		Con_Reportf( S_WARN "%s has no files. Ignored.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_NO_FILES;

		Zip_Close( zip );
		return NULL;
	}

	if( signature != ZIP_HEADER_LF )
	{
		Con_Reportf( S_ERROR "%s is not a zip file. Ignored.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_BAD_HEADER;

		Zip_Close( zip );
		return NULL;
	}

	// Find oecd
	FS_Seek( zip->handle, 0, SEEK_SET );
	filepos = zip->handle->real_length;

	while ( filepos > 0 )
	{
		FS_Seek( zip->handle, filepos, SEEK_SET );
		FS_Read( zip->handle, (void *)&signature, sizeof( signature ) );

		if( signature == ZIP_HEADER_EOCD )
			break;

		filepos -= sizeof( char ); // step back one byte
	}

	if( ZIP_HEADER_EOCD != signature )
	{
		Con_Reportf( S_ERROR "Cannot find EOCD in %s. Zip file corrupted.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_BAD_HEADER;

		Zip_Close( zip );
		return NULL;
	}

	FS_Read( zip->handle, (void *)&header_eocd, sizeof( zip_header_eocd_t ) );

	// Move to CDF start

	FS_Seek( zip->handle, header_eocd.central_directory_offset, SEEK_SET );

	// Calc count of files in archive

	info = (zipfile_t *)Mem_Calloc( fs_mempool, sizeof( zipfile_t ) * header_eocd.total_central_directory_record );

	for( i = 0; i < header_eocd.total_central_directory_record; i++ )
	{
		FS_Read( zip->handle, (void *)&header_cdf, sizeof( header_cdf ) );

		if( header_cdf.signature != ZIP_HEADER_CDF )
		{
			Con_Reportf( S_ERROR "CDF signature mismatch in %s. Zip file corrupted.\n", zipfile );

			if( error )
				*error = ZIP_LOAD_BAD_HEADER;

			Mem_Free( info );
			Zip_Close( zip );
			return NULL;
		}

		if( header_cdf.uncompressed_size && header_cdf.filename_len )
		{
			if(header_cdf.filename_len > MAX_SYSPATH) // ignore files with bigger than 1024 bytes
			{
				Con_Reportf( S_WARN "File name bigger than buffer. Ignored.\n" );
				continue;
			}

			memset( &filename_buffer, '\0', MAX_SYSPATH );
			FS_Read( zip->handle, &filename_buffer, header_cdf.filename_len );
			Q_strncpy( info[numpackfiles].name, filename_buffer, MAX_SYSPATH );

			info[numpackfiles].size = header_cdf.uncompressed_size;
			info[numpackfiles].compressed_size = header_cdf.compressed_size;
			info[numpackfiles].offset = header_cdf.local_header_offset;
			numpackfiles++;
		}
		else
			FS_Seek( zip->handle, header_cdf.filename_len, SEEK_CUR );

		if( header_cdf.extrafield_len )
			FS_Seek( zip->handle, header_cdf.extrafield_len, SEEK_CUR );

		if( header_cdf.file_commentary_len )
			FS_Seek( zip->handle, header_cdf.file_commentary_len, SEEK_CUR );
	}

	Q_strncpy( zip->filename, zipfile, sizeof( zip->filename ) );
	zip->mempool = Mem_AllocPool( zipfile );
	zip->filetime = FS_SysFileTime( zipfile );
	zip->numfiles = 0;
	zip->files = (zipfile_t *)Mem_Calloc( fs_mempool, sizeof( zipfile_t ) * numpackfiles );

	for( i = 0; i < numpackfiles; i++ )
		FS_AddFileToZip( info[i].name, zip, info[i].offset, info[i].size, info[i].compressed_size );

	if( error )
		*error = ZIP_LOAD_OK;

	Mem_Free( info );

	return zip;
}

void Zip_Close( zip_t *zip )
{
	if( !zip )
		return;

	Mem_FreePool( &zip->mempool );

	if( zip->handle != NULL )
		FS_Close( zip->handle );

	Mem_Free( zip );
}

static byte *Zip_LoadFile( const char *path, fs_offset_t *sizeptr, qboolean gamedironly )
{
	searchpath_t	*search;
	int		index;
	zip_header_t	header;
	zipfile_t	*file = NULL;
	byte		*compressed_buffer = NULL, *decompressed_buffer = NULL;
	int		zlib_result = 0;
	dword		test_crc, final_crc;
	z_stream	decompress_stream;

	if( sizeptr ) *sizeptr = 0;

	search = FS_FindFile( path, &index, gamedironly );

	if( !search || !search->zip )
		return  NULL;

	file = &search->zip->files[index];

	FS_Seek( search->zip->handle, file->offset, SEEK_SET );
	FS_Read( search->zip->handle, (void*)&header, sizeof( header ) );

	if( header.signature != ZIP_HEADER_LF )
	{
		Con_Reportf( S_ERROR "Zip_LoadFile: %s signature error\n", file->name );
		return NULL;
	}

	if( header.compression_flags == ZIP_COMPRESSION_NO_COMPRESSION )
	{
		if( header.filename_len )
			FS_Seek( search->zip->handle, header.filename_len, SEEK_CUR );

		if( header.extrafield_len )
			FS_Seek( search->zip->handle, header.extrafield_len, SEEK_CUR );

		decompressed_buffer = Mem_Malloc( search->zip->mempool, file->size + 1 );
		decompressed_buffer[file->size] = '\0';

		FS_Read( search->zip->handle, decompressed_buffer, file->size );

		CRC32_Init( &test_crc );
		CRC32_ProcessBuffer( &test_crc, decompressed_buffer, file->size );

		final_crc = CRC32_Final( test_crc );

		if( final_crc != header.crc32 )
		{
			Con_Reportf( S_ERROR "Zip_LoadFile: %s file crc32 mismatch\n", file->name );
			Mem_Free( decompressed_buffer );
			return NULL;
		}

		if( sizeptr ) *sizeptr = file->size;

		return decompressed_buffer;
	}
	else if( header.compression_flags == ZIP_COMPRESSION_DEFLATED )
	{
		if( header.filename_len )
			FS_Seek( search->zip->handle, header.filename_len, SEEK_CUR );

		if( header.extrafield_len )
			FS_Seek( search->zip->handle, header.extrafield_len, SEEK_CUR );

		compressed_buffer = Mem_Malloc( search->zip->mempool, file->compressed_size + 1 );
		decompressed_buffer = Mem_Malloc( search->zip->mempool, file->size + 1 );
		decompressed_buffer[file->size] = '\0';

		FS_Read( search->zip->handle, compressed_buffer, file->compressed_size );

		memset( &decompress_stream, 0, sizeof( decompress_stream ) );

		decompress_stream.total_in = decompress_stream.avail_in = file->compressed_size;
		decompress_stream.next_in = (Bytef *)compressed_buffer;
		decompress_stream.total_out = decompress_stream.avail_out = file->size;
		decompress_stream.next_out = (Bytef *)decompressed_buffer;

		decompress_stream.zalloc = Z_NULL;
		decompress_stream.zfree = Z_NULL;
		decompress_stream.opaque = Z_NULL;

		if( inflateInit2( &decompress_stream, -MAX_WBITS ) != Z_OK )
		{
			Con_Printf( S_ERROR "Zip_LoadFile: inflateInit2 failed\n" );
			Mem_Free( compressed_buffer );
			Mem_Free( decompressed_buffer );
			return NULL;
		}

		zlib_result = inflate( &decompress_stream, Z_NO_FLUSH );
		inflateEnd( &decompress_stream );

		if( zlib_result == Z_OK || zlib_result == Z_STREAM_END )
		{
			Mem_Free( compressed_buffer ); // finaly free compressed buffer

			CRC32_Init( &test_crc );
			CRC32_ProcessBuffer( &test_crc, decompressed_buffer, file->size );

			final_crc = CRC32_Final( test_crc );

			if( final_crc != header.crc32 )
			{
				Con_Reportf( S_ERROR "Zip_LoadFile: %s file crc32 mismatch\n", file->name );
				Mem_Free( decompressed_buffer );
				return NULL;
			}

			if( sizeptr ) *sizeptr = file->size;

			return decompressed_buffer;
		}
		else
		{
			Con_Reportf( S_ERROR "Zip_LoadFile: %s : error while file decompressing. Zlib return code %d.\n", file->name, zlib_result );
			Mem_Free( compressed_buffer );
			Mem_Free( decompressed_buffer );
			return NULL;
		}

	}
	else
	{
		Con_Reportf( S_ERROR "Zip_LoadFile: %s : file compressed with unknown algorithm.\n", file->name );
		return NULL;
	}

	return NULL;
}

/*
====================
FS_AddWad_Fullpath
====================
*/
static qboolean FS_AddWad_Fullpath( const char *wadfile, qboolean *already_loaded, int flags )
{
	searchpath_t	*search;
	wfile_t		*wad = NULL;
	const char	*ext = COM_FileExtension( wadfile );
	int		errorcode = WAD_LOAD_COULDNT_OPEN;

	for( search = fs_searchpaths; search; search = search->next )
	{
		if( search->wad && !Q_stricmp( search->wad->filename, wadfile ))
		{
			if( already_loaded ) *already_loaded = true;
			return true; // already loaded
		}
	}

	if( already_loaded )
		*already_loaded = false;

	if( !Q_stricmp( ext, "wad" ))
		wad = W_Open( wadfile, &errorcode );

	if( wad )
	{
		search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ));
		search->wad = wad;
		search->next = fs_searchpaths;
		search->flags |= flags;
		fs_searchpaths = search;

		Con_Reportf( "Adding wadfile: %s (%i files)\n", wadfile, wad->numlumps );
		return true;
	}
	else
	{
		if( errorcode != WAD_LOAD_NO_FILES )
			Con_Reportf( S_ERROR "FS_AddWad_Fullpath: unable to load wad \"%s\"\n", wadfile );
		return false;
	}
}

/*
================
FS_AddPak_Fullpath

Adds the given pack to the search path.
The pack type is autodetected by the file extension.

Returns true if the file was successfully added to the
search path or if it was already included.

If keep_plain_dirs is set, the pack will be added AFTER the first sequence of
plain directories.
================
*/
static qboolean FS_AddPak_Fullpath( const char *pakfile, qboolean *already_loaded, int flags )
{
	searchpath_t	*search;
	pack_t		*pak = NULL;
	const char	*ext = COM_FileExtension( pakfile );
	int		i, errorcode = PAK_LOAD_COULDNT_OPEN;
	
	for( search = fs_searchpaths; search; search = search->next )
	{
		if( search->pack && !Q_stricmp( search->pack->filename, pakfile ))
		{
			if( already_loaded ) *already_loaded = true;
			return true; // already loaded
		}
	}

	if( already_loaded )
		*already_loaded = false;

	if( !Q_stricmp( ext, "pak" ))
		pak = FS_LoadPackPAK( pakfile, &errorcode );

	if( pak )
	{
		string	fullpath;

		search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ));
		search->pack = pak;
		search->next = fs_searchpaths;
		search->flags |= flags;
		fs_searchpaths = search;

		Con_Reportf( "Adding pakfile: %s (%i files)\n", pakfile, pak->numfiles );

		// time to add in search list all the wads that contains in current pakfile (if do)
		for( i = 0; i < pak->numfiles; i++ )
		{
			if( !Q_stricmp( COM_FileExtension( pak->files[i].name ), "wad" ))
			{
				Q_sprintf( fullpath, "%s/%s", pakfile, pak->files[i].name );
				FS_AddWad_Fullpath( fullpath, NULL, flags );
			}
		}

		return true;
	}
	else
	{
		if( errorcode != PAK_LOAD_NO_FILES )
			Con_Reportf( S_ERROR "FS_AddPak_Fullpath: unable to load pak \"%s\"\n", pakfile );
		return false;
	}
}

qboolean FS_AddZip_Fullpath( const char *zipfile, qboolean *already_loaded, int flags )
{
	searchpath_t	*search;
	zip_t		*zip = NULL;
	const char	*ext = COM_FileExtension( zipfile );
	int		errorcode = ZIP_LOAD_COULDNT_OPEN;
  
	for( search = fs_searchpaths; search; search = search->next )
	{
		if( search->pack && !Q_stricmp( search->pack->filename, zipfile ))
		{
			if( already_loaded ) *already_loaded = true;
			return true; // already loaded
		}
	}
  
	if( already_loaded ) *already_loaded = false;
  
	if( !Q_stricmp( ext, "zip" ) || !Q_stricmp( ext, "pk3" ) )
		zip = FS_LoadZip( zipfile, &errorcode );
  
	if( zip )
	{
		search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ) );
		search->zip = zip;
		search->next = fs_searchpaths;
		search->flags |= flags;
		fs_searchpaths = search;

		Con_Reportf( "Adding zipfile: %s (%i files)\n", zipfile, zip->numfiles );
		return true;
	}
	else
	{
		if( errorcode != ZIP_LOAD_NO_FILES )
			Con_Reportf( S_ERROR "FS_AddZip_Fullpath: unable to load zip \"%s\"\n", zipfile );
		return false;
	}
}

/*
================
FS_AddGameDirectory

Sets fs_writedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
void FS_AddGameDirectory( const char *dir, uint flags )
{
	stringlist_t	list;
	searchpath_t	*search;
	string		fullpath;
	int		i;

	if( !FBitSet( flags, FS_NOWRITE_PATH ))
		Q_strncpy( fs_writedir, dir, sizeof( fs_writedir ));

	stringlistinit( &list );
	listdirectory( &list, dir, false );
	stringlistsort( &list );

	// add any PAK package in the directory
	for( i = 0; i < list.numstrings; i++ )
	{
		if( !Q_stricmp( COM_FileExtension( list.strings[i] ), "pak" ))
		{
			Q_sprintf( fullpath, "%s%s", dir, list.strings[i] );
			FS_AddPak_Fullpath( fullpath, NULL, flags );
		}
	}

	FS_AllowDirectPaths( true );

	// add any WAD package in the directory
	for( i = 0; i < list.numstrings; i++ )
	{
		if( !Q_stricmp( COM_FileExtension( list.strings[i] ), "wad" ))
		{
			Q_sprintf( fullpath, "%s%s", dir, list.strings[i] );
			FS_AddWad_Fullpath( fullpath, NULL, flags );
		}
	}

	// add any Zip package in the directory
	for( i = 0; i < list.numstrings; i++ )
	{
		if( !Q_stricmp( COM_FileExtension( list.strings[i] ), "zip" ) || !Q_stricmp( COM_FileExtension( list.strings[i] ), "pk3" ))
		{
			Q_sprintf( fullpath, "%s%s", dir, list.strings[i] );
			FS_AddZip_Fullpath( fullpath, NULL, flags );
		}
	 }

	stringlistfreecontents( &list );
	FS_AllowDirectPaths( false );

	// add the directory to the search path
	// (unpacked files have the priority over packed files)
	search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ));
	Q_strncpy( search->filename, dir, sizeof ( search->filename ));
	search->next = fs_searchpaths;
	search->flags = flags;
	fs_searchpaths = search;
}

/*
================
FS_AddGameHierarchy
================
*/
void FS_AddGameHierarchy( const char *dir, uint flags )
{
	int i;
	qboolean isGameDir = flags & FS_GAMEDIR_PATH;

	GI->added = true;

	if( !COM_CheckString( dir ))
		return;

	// add the common game directory

	// recursive gamedirs
	// for example, czeror->czero->cstrike->valve
	for( i = 0; i < SI.numgames; i++ )
	{
		if( !Q_strnicmp( SI.games[i]->gamefolder, dir, 64 ))
		{
			Con_Reportf( "FS_AddGameHierarchy: %d %s %s\n", i, SI.games[i]->gamefolder, SI.games[i]->basedir );
			if( !SI.games[i]->added && Q_stricmp( SI.games[i]->gamefolder, SI.games[i]->basedir ) )
			{
				SI.games[i]->added = true;
				FS_AddGameHierarchy( SI.games[i]->basedir, flags & (~FS_GAMEDIR_PATH) );
			}
			break;
		}
	}

	if( host.rodir[0] )
	{
		// append new flags to rodir, except FS_GAMEDIR_PATH and FS_CUSTOM_PATH
		uint newFlags = FS_NOWRITE_PATH | (flags & (~FS_GAMEDIR_PATH|FS_CUSTOM_PATH));
		if( isGameDir )
			newFlags |= FS_GAMERODIR_PATH;

		FS_AllowDirectPaths( true );
		FS_AddGameDirectory( va( "%s/%s/", host.rodir, dir ), newFlags );
		FS_AllowDirectPaths( false );
	}

	if( isGameDir )
		FS_AddGameDirectory( va( "%s/downloaded/", dir ), FS_NOWRITE_PATH | FS_CUSTOM_PATH );
	FS_AddGameDirectory( va( "%s/", dir ), flags );
	if( isGameDir )
		FS_AddGameDirectory( va( "%s/custom/", dir ), FS_NOWRITE_PATH | FS_CUSTOM_PATH );
}

/*
================
FS_ClearSearchPath
================
*/
void FS_ClearSearchPath( void )
{
	while( fs_searchpaths )
	{
		searchpath_t	*search = fs_searchpaths;

		if( !search ) break;

		if( FBitSet( search->flags, FS_STATIC_PATH ))
		{
			// skip read-only pathes
			if( search->next )
				fs_searchpaths = search->next->next;
			else break;
		}
		else fs_searchpaths = search->next;

		if( search->pack )
		{
			if( search->pack->files ) 
				Mem_Free( search->pack->files );
			Mem_Free( search->pack );
		}

		if( search->wad )
		{
			W_Close( search->wad );
		}

		if( search->zip )
		{
			Zip_Close(search->zip);
		}

		Mem_Free( search );
	}
}

/*
====================
FS_CheckNastyPath

Return true if the path should be rejected due to one of the following:
1: path elements that are non-portable
2: path elements that would allow access to files outside the game directory,
   or are just not a good idea for a mod to be using.
====================
*/
int FS_CheckNastyPath( const char *path, qboolean isgamedir )
{
	// all: never allow an empty path, as for gamedir it would access the parent directory and a non-gamedir path it is just useless
	if( !COM_CheckString( path )) return 2;

	if( fs_ext_path ) return 0;	// allow any path

	// Mac: don't allow Mac-only filenames - : is a directory separator
	// instead of /, but we rely on / working already, so there's no reason to
	// support a Mac-only path
	// Amiga and Windows: : tries to go to root of drive
	if( Q_strstr( path, ":" )) return 1; // non-portable attempt to go to root of drive

	// Amiga: // is parent directory
	if( Q_strstr( path, "//" )) return 1; // non-portable attempt to go to parent directory

	// all: don't allow going to parent directory (../ or /../)
	if( Q_strstr( path, ".." )) return 2; // attempt to go outside the game directory

	// Windows and UNIXes: don't allow absolute paths
	if( path[0] == '/' ) return 2; // attempt to go outside the game directory

	// all: forbid trailing slash on gamedir
	if( isgamedir && path[Q_strlen( path )-1] == '/' ) return 2;

	// all: forbid leading dot on any filename for any reason
	if( Q_strstr( path, "/." )) return 2; // attempt to go outside the game directory

	// after all these checks we're pretty sure it's a / separated filename
	// and won't do much if any harm
	return 0;
}

/*
================
FS_Rescan
================
*/
void FS_Rescan( void )
{
	const char *str;
	const int extrasFlags = FS_NOWRITE_PATH | FS_CUSTOM_PATH;
	Con_Reportf( "FS_Rescan( %s )\n", GI->title );

	FS_ClearSearchPath();

#if XASH_IOS
	{
		FS_AddPak_Fullpath( va( "%sextras.pak", SDL_GetBasePath() ), NULL, extrasFlags );
		FS_AddPak_Fullpath( va( "%sextras_%s.pak", SDL_GetBasePath(), GI->gamefolder ), NULL, extrasFlags );
	}
#else
	if( ( str = getenv( "XASH3D_EXTRAS_PAK1" ) ) )
		FS_AddPak_Fullpath( str, NULL, extrasFlags );
	if( ( str = getenv( "XASH3D_EXTRAS_PAK2" ) ) )
		FS_AddPak_Fullpath( str, NULL, extrasFlags );
#endif

	if( Q_stricmp( GI->basedir, GI->gamefolder ))
		FS_AddGameHierarchy( GI->basedir, 0 );
	if( Q_stricmp( GI->basedir, GI->falldir ) && Q_stricmp( GI->gamefolder, GI->falldir ))
		FS_AddGameHierarchy( GI->falldir, 0 );
	FS_AddGameHierarchy( GI->gamefolder, FS_GAMEDIR_PATH );

	if( FS_FileExists( va( "%s.rc", SI.basedirName ), false ))
		Q_strncpy( SI.rcName, SI.basedirName, sizeof( SI.rcName ));	// e.g. valve.rc
	else Q_strncpy( SI.rcName, SI.exeName, sizeof( SI.rcName ));	// e.g. quake.rc
}

/*
================
FS_Rescan_f
================
*/
void FS_Rescan_f( void )
{
	FS_Rescan();
}

/*
================
FS_WriteGameInfo

assume GameInfo is valid
================
*/
static void FS_WriteGameInfo( const char *filepath, gameinfo_t *GameInfo )
{
	file_t	*f = FS_Open( filepath, "w", false ); // we in binary-mode
	int	i, write_ambients = false;

	if( !f ) Sys_Error( "FS_WriteGameInfo: can't write %s\n", filepath );	// may be disk-space is out?

	FS_Printf( f, "// generated by %s %s-%s (%s-%s)\n\n\n", XASH_ENGINE_NAME, XASH_VERSION, Q_buildcommit(), Q_buildos(), Q_buildarch() );

	if( Q_strlen( GameInfo->basedir ))
		FS_Printf( f, "basedir\t\t\"%s\"\n", GameInfo->basedir );

	// DEPRECATED: gamedir key isn't supported by FWGS fork
	// but write it anyway to keep compability with original Xash3D
	if( Q_strlen( GameInfo->gamefolder ))
		FS_Printf( f, "gamedir\t\t\"%s\"\n", GameInfo->gamefolder );

	if( Q_strlen( GameInfo->falldir ))
		FS_Printf( f, "fallback_dir\t\"%s\"\n", GameInfo->falldir );

	if( Q_strlen( GameInfo->title ))
		FS_Printf( f, "title\t\t\"%s\"\n", GameInfo->title );

	if( Q_strlen( GameInfo->startmap ))
		FS_Printf( f, "startmap\t\t\"%s\"\n", GameInfo->startmap );

	if( Q_strlen( GameInfo->trainmap ))
		FS_Printf( f, "trainmap\t\t\"%s\"\n", GameInfo->trainmap );

	if( GameInfo->version != 0.0f )
		FS_Printf( f, "version\t\t%g\n", GameInfo->version );

	if( GameInfo->size != 0 )
		FS_Printf( f, "size\t\t%lu\n", GameInfo->size );

	if( Q_strlen( GameInfo->game_url ))
		FS_Printf( f, "url_info\t\t\"%s\"\n", GameInfo->game_url );

	if( Q_strlen( GameInfo->update_url ))
		FS_Printf( f, "url_update\t\t\"%s\"\n", GameInfo->update_url );

	if( Q_strlen( GameInfo->type ))
		FS_Printf( f, "type\t\t\"%s\"\n", GameInfo->type );

	if( Q_strlen( GameInfo->date ))
		FS_Printf( f, "date\t\t\"%s\"\n", GameInfo->date );

	if( Q_strlen( GameInfo->dll_path ))
		FS_Printf( f, "dllpath\t\t\"%s\"\n", GameInfo->dll_path );	

	if( Q_strlen( GameInfo->game_dll ))
		FS_Printf( f, "gamedll\t\t\"%s\"\n", GameInfo->game_dll );

	if( Q_strlen( GameInfo->game_dll_linux ))
		FS_Printf( f, "gamedll_linux\t\t\"%s\"\n", GameInfo->game_dll_linux );

	if( Q_strlen( GameInfo->game_dll_osx ))
		FS_Printf( f, "gamedll_osx\t\t\"%s\"\n", GameInfo->game_dll_osx );


	if( Q_strlen( GameInfo->iconpath ))
		FS_Printf( f, "icon\t\t\"%s\"\n", GameInfo->iconpath );

	switch( GameInfo->gamemode )
	{
	case 1: FS_Print( f, "gamemode\t\t\"singleplayer_only\"\n" ); break;
	case 2: FS_Print( f, "gamemode\t\t\"multiplayer_only\"\n" ); break;
	}

	if( Q_strlen( GameInfo->sp_entity ))
		FS_Printf( f, "sp_entity\t\t\"%s\"\n", GameInfo->sp_entity );
	if( Q_strlen( GameInfo->mp_entity ))
		FS_Printf( f, "mp_entity\t\t\"%s\"\n", GameInfo->mp_entity );
	if( Q_strlen( GameInfo->mp_filter ))
		FS_Printf( f, "mp_filter\t\t\"%s\"\n", GameInfo->mp_filter );

	if( GameInfo->secure )
		FS_Printf( f, "secure\t\t\"%i\"\n", GameInfo->secure );

	if( GameInfo->nomodels )
		FS_Printf( f, "nomodels\t\t\"%i\"\n", GameInfo->nomodels );

	if( GameInfo->max_edicts > 0 )
		FS_Printf( f, "max_edicts\t%i\n", GameInfo->max_edicts );
	if( GameInfo->max_tents > 0 )
		FS_Printf( f, "max_tempents\t%i\n", GameInfo->max_tents );
	if( GameInfo->max_beams > 0 )
		FS_Printf( f, "max_beams\t\t%i\n", GameInfo->max_beams );
	if( GameInfo->max_particles > 0 )
		FS_Printf( f, "max_particles\t%i\n", GameInfo->max_particles );

	for( i = 0; i < NUM_AMBIENTS; i++ )
	{
		if( *GameInfo->ambientsound[i] )
		{
			if( !write_ambients )
			{
				FS_Print( f, "\n" );
				write_ambients = true;
			}
			FS_Printf( f, "ambient%i\t\t%s\n", i, GameInfo->ambientsound[i] );
		}
	}

	FS_Print( f, "\n\n\n" );
	FS_Close( f );	// all done
}

void FS_InitGameInfo( gameinfo_t *GameInfo, const char *gamedir )
{
	memset( GameInfo, 0, sizeof( *GameInfo ));

	// filesystem info
	Q_strncpy( GameInfo->gamefolder, gamedir, sizeof( GameInfo->gamefolder ));
	Q_strncpy( GameInfo->basedir, "valve", sizeof( GameInfo->basedir ));
	GameInfo->falldir[0] = 0;
	Q_strncpy( GameInfo->startmap, "c0a0", sizeof( GameInfo->startmap ));
	Q_strncpy( GameInfo->trainmap, "t0a0", sizeof( GameInfo->trainmap ));
	Q_strncpy( GameInfo->title, "New Game", sizeof( GameInfo->title ));
	GameInfo->version = 1.0f;

	// .dll pathes
	Q_strncpy( GameInfo->dll_path, "cl_dlls", sizeof( GameInfo->dll_path ));
	Q_strncpy( GameInfo->game_dll, "dlls/hl.dll", sizeof( GameInfo->game_dll ));
	Q_strncpy( GameInfo->game_dll_linux, "dlls/hl.so", sizeof( GameInfo->game_dll_linux ));
	Q_strncpy( GameInfo->game_dll_osx, "dlls/hl.dylib", sizeof( GameInfo->game_dll_osx ));

	// .ico path
	Q_strncpy( GameInfo->iconpath, "game.ico", sizeof( GameInfo->iconpath ));

	Q_strncpy( GameInfo->sp_entity, "info_player_start", sizeof( GameInfo->sp_entity ));
	Q_strncpy( GameInfo->mp_entity, "info_player_deathmatch", sizeof( GameInfo->mp_entity ));

	GameInfo->max_edicts     = 900; // default value if not specified
	GameInfo->max_tents      = 500;
	GameInfo->max_beams      = 128;
	GameInfo->max_particles  = 4096;
}

void FS_ParseGenericGameInfo( gameinfo_t *GameInfo, const char *buf, const qboolean isGameInfo )
{
	char *pfile = (char*) buf;
	qboolean found_linux = false, found_osx = false;
	string token;

	while(( pfile = COM_ParseFile( pfile, token )) != NULL )
	{
		// different names in liblist/gameinfo
		if( !Q_stricmp( token, isGameInfo ? "title" : "game" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->title );
		}
		// valid for both
		else if( !Q_stricmp( token, "fallback_dir" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->falldir );
		}
		// valid for both
		else if( !Q_stricmp( token, "startmap" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->startmap );
			COM_StripExtension( GameInfo->startmap ); // HQ2:Amen has extension .bsp
		}
		// only trainmap is valid for gameinfo
		else if( !Q_stricmp( token, "trainmap" ) ||
			(!isGameInfo && !Q_stricmp( token, "trainingmap" )))
		{
			pfile = COM_ParseFile( pfile, GameInfo->trainmap );
			COM_StripExtension( GameInfo->trainmap ); // HQ2:Amen has extension .bsp
		}
		// valid for both
		else if( !Q_stricmp( token, "url_info" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_url );
		}
		// different names
		else if( !Q_stricmp( token, isGameInfo ? "url_update" : "url_dl" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->update_url );
		}
		// valid for both
		else if( !Q_stricmp( token, "gamedll" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_dll );
			COM_FixSlashes( GameInfo->game_dll );
		}
		// valid for both
		else if( !Q_stricmp( token, "gamedll_linux" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_dll_linux );
			found_linux = true;
		}
		// valid for both
		else if( !Q_stricmp( token, "gamedll_osx" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->game_dll_osx );
			found_osx = true;
		}
		// valid for both
		else if( !Q_stricmp( token, "icon" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->iconpath );
			COM_FixSlashes( GameInfo->iconpath );
			COM_DefaultExtension( GameInfo->iconpath, ".ico" );
		}
		else if( !Q_stricmp( token, "type" ))
		{
			pfile = COM_ParseFile( pfile, token );

			if( !isGameInfo && !Q_stricmp( token, "singleplayer_only" ))
			{
				// TODO: Remove this ugly hack too.
				// This was made because Half-Life has multiplayer,
				// but for some reason it's marked as singleplayer_only.
				// Old WON version is fine.
				if( !Q_stricmp( GameInfo->gamefolder, "valve") )
					GameInfo->gamemode = GAME_NORMAL;
				else
					GameInfo->gamemode = GAME_SINGLEPLAYER_ONLY;
				Q_strncpy( GameInfo->type, "Single", sizeof( GameInfo->type ));
			}
			else if( !isGameInfo && !Q_stricmp( token, "multiplayer_only" ))
			{
				GameInfo->gamemode = GAME_MULTIPLAYER_ONLY;
				Q_strncpy( GameInfo->type, "Multiplayer", sizeof( GameInfo->type ));
			}
			else
			{
				// pass type without changes
				if( !isGameInfo )
					GameInfo->gamemode = GAME_NORMAL;
				Q_strncpy( GameInfo->type, token, sizeof( GameInfo->type ));
			}
		}
		// valid for both
		else if( !Q_stricmp( token, "version" ))
		{
			pfile = COM_ParseFile( pfile, token );
			GameInfo->version = Q_atof( token );
		}
		// valid for both
		else if( !Q_stricmp( token, "size" ))
		{
			pfile = COM_ParseFile( pfile, token );
			GameInfo->size = Q_atoi( token );
		}
		else if( !Q_stricmp( token, "edicts" ))
		{
			pfile = COM_ParseFile( pfile, token );
			GameInfo->max_edicts = Q_atoi( token );
		}
		else if( !Q_stricmp( token, isGameInfo ? "mp_entity" : "mpentity" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->mp_entity );
		}
		else if( !Q_stricmp( token, isGameInfo ? "mp_filter" : "mpfilter" ))
		{
			pfile = COM_ParseFile( pfile, GameInfo->mp_filter );
		}
		// valid for both
		else if( !Q_stricmp( token, "secure" ))
		{
			pfile = COM_ParseFile( pfile, token );
			GameInfo->secure = Q_atoi( token );
		}
		// valid for both
		else if( !Q_stricmp( token, "nomodels" ))
		{
			pfile = COM_ParseFile( pfile, token );
			GameInfo->nomodels = Q_atoi( token );
		}
		else if( !Q_stricmp( token, isGameInfo ? "max_edicts" : "edicts" ))
		{
			pfile = COM_ParseFile( pfile, token );
			GameInfo->max_edicts = bound( MIN_EDICTS, Q_atoi( token ), MAX_EDICTS );
		}
		// only for gameinfo
		else if( isGameInfo )
		{
			if( !Q_stricmp( token, "basedir" ))
			{
				string fs_path;
				pfile = COM_ParseFile( pfile, fs_path );
				if( Q_stricmp( fs_path, GameInfo->basedir ) || Q_stricmp( fs_path, GameInfo->gamefolder ))
					Q_strncpy( GameInfo->basedir, fs_path, sizeof( GameInfo->basedir ));
			}
			else if( !Q_stricmp( token, "sp_entity" ))
			{
				pfile = COM_ParseFile( pfile, GameInfo->sp_entity );
			}
			else if( isGameInfo && !Q_stricmp( token, "dllpath" ))
			{
				pfile = COM_ParseFile( pfile, GameInfo->dll_path );
			}
			else if( isGameInfo && !Q_stricmp( token, "date" ))
			{
				pfile = COM_ParseFile( pfile, GameInfo->date );
			}
			else if( !Q_stricmp( token, "max_tempents" ))
			{
				pfile = COM_ParseFile( pfile, token );
				GameInfo->max_tents = bound( 300, Q_atoi( token ), 2048 );
			}
			else if( !Q_stricmp( token, "max_beams" ))
			{
				pfile = COM_ParseFile( pfile, token );
				GameInfo->max_beams = bound( 64, Q_atoi( token ), 512 );
			}
			else if( !Q_stricmp( token, "max_particles" ))
			{
				pfile = COM_ParseFile( pfile, token );
				GameInfo->max_particles = bound( 4096, Q_atoi( token ), 32768 );
			}
			else if( !Q_stricmp( token, "gamemode" ))
			{
				pfile = COM_ParseFile( pfile, token );
				// TODO: Remove this ugly hack too.
				// This was made because Half-Life has multiplayer,
				// but for some reason it's marked as singleplayer_only.
				// Old WON version is fine.
				if( !Q_stricmp( token, "singleplayer_only" ) && Q_stricmp( GameInfo->gamefolder, "valve") )
					GameInfo->gamemode = GAME_SINGLEPLAYER_ONLY;
				else if( !Q_stricmp( token, "multiplayer_only" ))
					GameInfo->gamemode = GAME_MULTIPLAYER_ONLY;
			}
			else if( !Q_strnicmp( token, "ambient", 7 ))
			{
				int	ambientNum = Q_atoi( token + 7 );

				if( ambientNum < 0 || ambientNum > ( NUM_AMBIENTS - 1 ))
					ambientNum = 0;
				pfile = COM_ParseFile( pfile, GameInfo->ambientsound[ambientNum] );
			}
			else if( !Q_stricmp( token, "noskills" ))
			{
				pfile = COM_ParseFile( pfile, token );
				GameInfo->noskills = Q_atoi( token );
			}
		}
	}

	if( !found_linux || !found_osx )
	{
		// just replace extension from dll to so/dylib
		char gamedll[64];
		Q_strncpy( gamedll, GameInfo->game_dll, sizeof( gamedll ));
		COM_StripExtension( gamedll );

		if( !found_linux )
			Q_snprintf( GameInfo->game_dll_linux, sizeof( GameInfo->game_dll_linux ), "%s.so", gamedll );

		if( !found_osx )
			Q_snprintf( GameInfo->game_dll_osx, sizeof( GameInfo->game_dll_osx ), "%s.dylib", gamedll );
	}

	// make sure what gamedir is really exist
	if( !FS_SysFolderExists( va( "%s"PATH_SPLITTER"%s", host.rootdir, GameInfo->falldir )))
		GameInfo->falldir[0] = '\0';
}

/*
================
FS_CreateDefaultGameInfo
================
*/
void FS_CreateDefaultGameInfo( const char *filename )
{
	gameinfo_t	defGI;

	FS_InitGameInfo( &defGI, fs_basedir );

	// make simple gameinfo.txt
	FS_WriteGameInfo( filename, &defGI );
}

/*
================
FS_ParseLiblistGam
================
*/
static qboolean FS_ParseLiblistGam( const char *filename, const char *gamedir, gameinfo_t *GameInfo )
{
	char	*afile;

	if( !GameInfo ) return false;	
	afile = (char *)FS_LoadFile( filename, NULL, false );
	if( !afile ) return false;

	FS_InitGameInfo( GameInfo, gamedir );

	FS_ParseGenericGameInfo( GameInfo, afile, false );

	Mem_Free( afile );

	return true;
}

/*
================
FS_ConvertGameInfo
================
*/
static qboolean FS_ConvertGameInfo( const char *gamedir, const char *gameinfo_path, const char *liblist_path )
{
	gameinfo_t	GameInfo;

	memset( &GameInfo, 0, sizeof( GameInfo ));

	if( FS_ParseLiblistGam( liblist_path, gamedir, &GameInfo ))
	{
		Con_DPrintf( "Convert %s to %s\n", liblist_path, gameinfo_path );
		FS_WriteGameInfo( gameinfo_path, &GameInfo );

		return true;
	}

	return false;
}

/*
================
FS_ReadGameInfo
================
*/
static qboolean FS_ReadGameInfo( const char *filepath, const char *gamedir, gameinfo_t *GameInfo )
{
	char	*afile;

	afile = (char *)FS_LoadFile( filepath, NULL, false );
	if( !afile ) return false;

	FS_InitGameInfo( GameInfo, gamedir );

	FS_ParseGenericGameInfo( GameInfo, afile, true );

	Mem_Free( afile );

	return true;
}

/*
================
FS_CheckForGameDir
================
*/
static qboolean FS_CheckForGameDir( const char *gamedir )
{
	// if directory contain config.cfg it's 100% gamedir
	if( FS_FileExists( va( "%s/config.cfg", gamedir ), false ))
		return true;

	// if directory contain progs.dat it's 100% gamedir
	if( FS_FileExists( va( "%s/progs.dat", gamedir ), false ))
		return true;

	// quake mods probably always archived but can missed config.cfg before first running
	if( FS_FileExists( va( "%s/pak0.pak", gamedir ), false ))
		return true;

	// NOTE; adds here some additional checks if you wished

	return false;
}

/*
================
FS_ParseGameInfo
================
*/
static qboolean FS_ParseGameInfo( const char *gamedir, gameinfo_t *GameInfo )
{
	string		liblist_path, gameinfo_path;
	string		default_gameinfo_path;
	gameinfo_t	tmpGameInfo;
	qboolean	haveUpdate = false;

	Q_snprintf( default_gameinfo_path, sizeof( default_gameinfo_path ), "%s/gameinfo.txt", fs_basedir );
	Q_snprintf( gameinfo_path, sizeof( gameinfo_path ), "%s/gameinfo.txt", gamedir );
	Q_snprintf( liblist_path, sizeof( liblist_path ), "%s/liblist.gam", gamedir );

	// here goes some RoDir magic...
	if( host.rodir[0] )
	{
		string	filepath_ro, liblist_ro;
		fs_offset_t roLibListTime, roGameInfoTime, rwGameInfoTime;

		Q_snprintf( filepath_ro, sizeof( filepath_ro ), "%s/%s/gameinfo.txt", host.rodir, gamedir );
		Q_snprintf( liblist_ro, sizeof( liblist_ro ), "%s/%s/liblist.gam", host.rodir, gamedir );

		roLibListTime = FS_SysFileTime( liblist_ro );
		roGameInfoTime = FS_SysFileTime( filepath_ro );
		rwGameInfoTime = FS_SysFileTime( gameinfo_path );

		if( roLibListTime > rwGameInfoTime )
		{
			haveUpdate = FS_ConvertGameInfo( gamedir, gameinfo_path, liblist_ro );
		}
		else if( roGameInfoTime > rwGameInfoTime )
		{
			char *afile_ro = (char *)FS_LoadDirectFile( filepath_ro, NULL );

			if( afile_ro )
			{
				gameinfo_t gi;

				haveUpdate = true;

				FS_InitGameInfo( &gi, gamedir );
				FS_ParseGenericGameInfo( &gi, afile_ro, true );
				FS_WriteGameInfo( gameinfo_path, &gi );
				Mem_Free( afile_ro );
			}
		}
	}

	// if user change liblist.gam update the gameinfo.txt
	if( FS_FileTime( liblist_path, false ) > FS_FileTime( gameinfo_path, false ))
		FS_ConvertGameInfo( gamedir, gameinfo_path, liblist_path );

	// force to create gameinfo for specified game if missing
	if(( FS_CheckForGameDir( gamedir ) || !Q_stricmp( fs_gamedir, gamedir )) && !FS_FileExists( gameinfo_path, false ))
	{
		memset( &tmpGameInfo, 0, sizeof( tmpGameInfo ));

		if( FS_ReadGameInfo( default_gameinfo_path, gamedir, &tmpGameInfo ))
		{
			// now we have copy of game info from basedir but needs to change gamedir
			Con_DPrintf( "Convert %s to %s\n", default_gameinfo_path, gameinfo_path );
			Q_strncpy( tmpGameInfo.gamefolder, gamedir, sizeof( tmpGameInfo.gamefolder ));
			FS_WriteGameInfo( gameinfo_path, &tmpGameInfo );
		}
		else FS_CreateDefaultGameInfo( gameinfo_path );
	}

	if( !GameInfo || !FS_FileExists( gameinfo_path, false ))
		return false; // no dest

	if( FS_ReadGameInfo( gameinfo_path, gamedir, GameInfo ))
		return true;
	return false;
}

/*
================
FS_LoadGameInfo

can be passed null arg
================
*/
void FS_LoadGameInfo( const char *rootfolder )
{
	int	i;

	// lock uplevel of gamedir for read\write
	fs_ext_path = false;

	if( rootfolder ) Q_strcpy( fs_gamedir, rootfolder );
	Con_Reportf( "FS_LoadGameInfo( %s )\n", fs_gamedir );

	// clear any old pathes
	FS_ClearSearchPath();

	// validate gamedir
	for( i = 0; i < SI.numgames; i++ )
	{
		if( !Q_stricmp( SI.games[i]->gamefolder, fs_gamedir ))
			break;
	}

	if( i == SI.numgames )
		Sys_Error( "Couldn't find game directory '%s'\n", fs_gamedir );

	SI.GameInfo = SI.games[i];

	if( !Sys_GetParmFromCmdLine( "-dll", SI.gamedll ) )
	{
		SI.gamedll[0] = 0;
	}

	if( !Sys_GetParmFromCmdLine( "-clientlib", SI.clientlib ) )
	{
		SI.clientlib[0] = 0;
	}
	
	FS_Rescan(); // create new filesystem

	Image_CheckPaletteQ1 ();
	Host_InitDecals ();	// reload decals
}

/*
================
FS_Init
================
*/
void FS_Init( void )
{
	stringlist_t	dirs;
	qboolean		hasBaseDir = false;
	qboolean		hasGameDir = false;
	int		i;
	
	FS_InitMemory();

	Cmd_AddCommand( "fs_rescan", FS_Rescan_f, "rescan filesystem search pathes" );
	Cmd_AddCommand( "fs_path", FS_Path_f, "show filesystem search pathes" );
	Cmd_AddCommand( "fs_clearpaths", FS_ClearPaths_f, "clear filesystem search pathes" );

#if !XASH_WIN32
	if( Sys_CheckParm( "-casesensitive" ) )
		fs_caseinsensitive = false;

	if( !fs_caseinsensitive )
	{
		if( host.rodir[0] && !Q_strcmp( host.rodir, host.rootdir ) )
		{
			Sys_Error( "RoDir and default rootdir can't point to same directory!" );
		}
	}
	else
#endif
	{
		if( host.rodir[0] && !Q_stricmp( host.rodir, host.rootdir ) )
		{
			Sys_Error( "RoDir and default rootdir can't point to same directory!" );
		}
	}

	// ignore commandlineoption "-game" for other stuff
	SI.numgames = 0;

	Q_strncpy( fs_basedir, SI.basedirName, sizeof( fs_basedir )); // default dir
	
	if( !Sys_GetParmFromCmdLine( "-game", fs_gamedir ))
		Q_strncpy( fs_gamedir, fs_basedir, sizeof( fs_gamedir )); // gamedir == basedir

	if( FS_CheckNastyPath( fs_basedir, true ))
	{
		// this is completely fatal...
		Sys_Error( "invalid base directory \"%s\"\n", fs_basedir );
	}

	if( FS_CheckNastyPath( fs_gamedir, true ))
	{
		Con_Printf( S_ERROR "invalid game directory \"%s\"\n", fs_gamedir );
		Q_strncpy( fs_gamedir, fs_basedir, sizeof( fs_gamedir )); // default dir
	}

	// add readonly directories first
	if( host.rodir[0] )
	{
		stringlistinit( &dirs );
		listdirectory( &dirs, host.rodir, false );
		stringlistsort( &dirs );

		for( i = 0; i < dirs.numstrings; i++ )
		{
			// no need to check folders here, FS_CreatePath will not fail if path exists
			// and listdirectory returns only really existing directories
			FS_CreatePath( va( "%s" PATH_SPLITTER "%s/", host.rootdir, dirs.strings[i] ));
		}

		stringlistfreecontents( &dirs );
	}

	// validate directories
	stringlistinit( &dirs );
	listdirectory( &dirs, "./", false );
	stringlistsort( &dirs );

	for( i = 0; i < dirs.numstrings; i++ )
	{
		if( !Q_stricmp( fs_basedir, dirs.strings[i] ))
			hasBaseDir = true;

		if( !Q_stricmp( fs_gamedir, dirs.strings[i] ))
			hasGameDir = true;
	}

	if( !hasGameDir )
	{
		Con_Printf( S_ERROR "game directory \"%s\" not exist\n", fs_gamedir );
		if( hasBaseDir ) Q_strncpy( fs_gamedir, fs_basedir, sizeof( fs_gamedir ));
	}

	// build list of game directories here
	FS_AddGameDirectory( "./", 0 );

	for( i = 0; i < dirs.numstrings; i++ )
	{
		if( !FS_SysFolderExists( dirs.strings[i] ) || ( !Q_stricmp( dirs.strings[i], ".." ) && !fs_ext_path ))
			continue;

		if( SI.games[SI.numgames] == NULL )
			SI.games[SI.numgames] = (gameinfo_t *)Mem_Calloc( fs_mempool, sizeof( gameinfo_t ));

		if( FS_ParseGameInfo( dirs.strings[i], SI.games[SI.numgames] ))
			SI.numgames++; // added
	}

	stringlistfreecontents( &dirs );
	Con_Reportf( "FS_Init: done\n" );
}

void FS_AllowDirectPaths( qboolean enable )
{
	fs_ext_path = enable;
}

/*
================
FS_Shutdown
================
*/
void FS_Shutdown( void )
{
	int	i;

	// release gamedirs
	for( i = 0; i < SI.numgames; i++ )
		if( SI.games[i] ) Mem_Free( SI.games[i] );

	memset( &SI, 0, sizeof( sysinfo_t ));

	FS_ClearSearchPath(); // release all wad files too
	Mem_FreePool( &fs_mempool );
}

/*
====================
FS_SysFileTime

Internal function used to determine filetime
====================
*/
static int FS_SysFileTime( const char *filename )
{
	struct stat buf;
	
	if( stat( filename, &buf ) == -1 )
		return -1;

	return buf.st_mtime;
}

/*
====================
FS_SysOpen

Internal function used to create a file_t and open the relevant non-packed file on disk
====================
*/
static file_t *FS_SysOpen( const char *filepath, const char *mode )
{
	file_t	*file;
	int	mod, opt;
	uint	ind;

	// Parse the mode string
	switch( mode[0] )
	{
	case 'r':	// read
		mod = O_RDONLY;
		opt = 0;
		break;
	case 'w': // write
		mod = O_WRONLY;
		opt = O_CREAT | O_TRUNC;
		break;
	case 'a': // append
		mod = O_WRONLY;
		opt = O_CREAT | O_APPEND;
		break;
	case 'e': // edit
		mod = O_WRONLY;
		opt = O_CREAT;
		break;
	default:
		return NULL;
	}

	for( ind = 1; mode[ind] != '\0'; ind++ )
	{
		switch( mode[ind] )
		{
		case '+':
			mod = O_RDWR;
			break;
		case 'b':
			opt |= O_BINARY;
			break;
		default:
			break;
		}
	}

	file = (file_t *)Mem_Calloc( fs_mempool, sizeof( *file ));
	file->filetime = FS_SysFileTime( filepath );
	file->ungetc = EOF;

	file->handle = open( filepath, mod|opt, 0666 );

#if !XASH_WIN32
	if( file->handle < 0 )
	{
		const char *ffilepath = FS_FixFileCase( filepath );
		if( ffilepath != filepath )
			file->handle = open( ffilepath, mod|opt, 0666 );
	}
#endif

	if( file->handle < 0 )
	{
		Mem_Free( file );
		return NULL;
	}

	file->real_length = lseek( file->handle, 0, SEEK_END );

	// For files opened in append mode, we start at the end of the file
	if( mod & O_APPEND ) file->position = file->real_length;
	else lseek( file->handle, 0, SEEK_SET );

	return file;
}

/*
===========
FS_OpenPackedFile

Open a packed file using its package file descriptor
===========
*/
file_t *FS_OpenPackedFile( pack_t *pack, int pack_ind )
{
	dpackfile_t	*pfile;
	int		dup_handle;
	file_t		*file;

	pfile = &pack->files[pack_ind];

	if( lseek( pack->handle, pfile->filepos, SEEK_SET ) == -1 )
		return NULL;

	dup_handle = dup( pack->handle );

	if( dup_handle < 0 )
		return NULL;

	file = (file_t *)Mem_Calloc( fs_mempool, sizeof( *file ));
	file->handle = dup_handle;
	file->real_length = pfile->filelen;
	file->offset = pfile->filepos;
	file->position = 0;
	file->ungetc = EOF;

	return file;
}

/*
==================
FS_SysFileExists

Look for a file in the filesystem only
==================
*/
qboolean FS_SysFileExists( const char *path, qboolean caseinsensitive )
{
#if XASH_WIN32
	int desc;

	if(( desc = open( path, O_RDONLY|O_BINARY )) < 0 )
		return false;

	close( desc );
	return true;
#else
	int ret;
	struct stat buf;

	ret = stat( path, &buf );

	// speedup custom path search
	if( caseinsensitive && ( ret < 0 ) )
	{
		const char *fpath = FS_FixFileCase( path );
		if( fpath != path )
			ret = stat( fpath, &buf );
	}

	if( ret < 0 )
		return false;

	return S_ISREG( buf.st_mode );
#endif
}

/*
==================
FS_SysFolderExists

Look for a existing folder
==================
*/
qboolean FS_SysFolderExists( const char *path )
{
#if XASH_WIN32
	DWORD	dwFlags = GetFileAttributes( path );

	return ( dwFlags != -1 ) && ( dwFlags & FILE_ATTRIBUTE_DIRECTORY );
#else
	DIR *dir = opendir( path );

	if( dir )
	{
		closedir( dir );
		return true;
	}
	else if( (errno == ENOENT) || (errno == ENOTDIR) )
	{
		return false;
	}
	else
	{
		Con_Reportf( S_ERROR "FS_SysFolderExists: problem while opening dir: %s\n", strerror( errno ) );
		return false;
	}
#endif
}

/*
====================
FS_FindFile

Look for a file in the packages and in the filesystem

Return the searchpath where the file was found (or NULL)
and the file index in the package if relevant
====================
*/
static searchpath_t *FS_FindFile( const char *name, int *index, qboolean gamedironly )
{
	searchpath_t	*search;
	char		*pEnvPath;

	// search through the path, one element at a time
	for( search = fs_searchpaths; search; search = search->next )
	{
		if( gamedironly & !FBitSet( search->flags, FS_GAMEDIRONLY_SEARCH_FLAGS ))
			continue;

		// is the element a pak file?
		if( search->pack )
		{
			int	left, right, middle;
			pack_t	*pak;

			pak = search->pack;

			// look for the file (binary search)
			left = 0;
			right = pak->numfiles - 1;
			while( left <= right )
			{
				int	diff;

				middle = (left + right) / 2;
				diff = Q_stricmp( pak->files[middle].name, name );

				// Found it
				if( !diff )
				{
					if( index ) *index = middle;
					return search;
				}

				// if we're too far in the list
				if( diff > 0 )
					right = middle - 1;
				else left = middle + 1;
			}
		}
		else if( search->wad )
		{
			dlumpinfo_t	*lump;	
			signed char		type = W_TypeFromExt( name );
			qboolean		anywadname = true;
			string		wadname, wadfolder;
			string		shortname;

			// quick reject by filetype
			if( type == TYP_NONE ) continue;
			COM_ExtractFilePath( name, wadname );
			wadfolder[0] = '\0';

			if( Q_strlen( wadname ))
			{
				COM_FileBase( wadname, wadname );
				Q_strncpy( wadfolder, wadname, sizeof( wadfolder ));
				COM_DefaultExtension( wadname, ".wad" );
				anywadname = false;
			}

			// make wadname from wad fullpath
			COM_FileBase( search->wad->filename, shortname );
			COM_DefaultExtension( shortname, ".wad" );

			// quick reject by wadname
			if( !anywadname && Q_stricmp( wadname, shortname ))
				continue;

			// NOTE: we can't using long names for wad,
			// because we using original wad names[16];
			COM_FileBase( name, shortname );

			lump = W_FindLump( search->wad, shortname, type );

			if( lump )
			{
				if( index )
					*index = lump - search->wad->lumps;
				return search;
			}
		}
		else if( search->zip )
		{
			int i;
			for( i = 0; search->zip->numfiles > i; i++)
			{
				if( !Q_stricmp( search->zip->files[i].name, name ) )
				{
					if( index )
						*index = i;
					return search;
				}
			}
		}
		else
		{
			char	netpath[MAX_SYSPATH];

			Q_sprintf( netpath, "%s%s", search->filename, name );

			if( FS_SysFileExists( netpath, !( search->flags & FS_CUSTOM_PATH ) ))
			{
				if( index != NULL ) *index = -1;
				return search;
			}
		}
	}

	if( fs_ext_path )
	{
		char	netpath[MAX_SYSPATH];

		// clear searchpath
		search = &fs_directpath;
		memset( search, 0, sizeof( searchpath_t ));

		// root folder has a more priority than netpath
		Q_strncpy( search->filename, host.rootdir, sizeof( search->filename ));
		Q_strcat( search->filename, PATH_SPLITTER );
		Q_snprintf( netpath, MAX_SYSPATH, "%s%s", search->filename, name );

		if( FS_SysFileExists( netpath, !( search->flags & FS_CUSTOM_PATH ) ))
		{
			if( index != NULL )
				*index = -1;
			return search;
		}

#if 0
		// search for environment path
		while( ( pEnvPath = getenv( "Path" ) ) )
		{
			char *end = Q_strchr( pEnvPath, ';' );
			if( !end ) break;
			Q_strncpy( search->filename, pEnvPath, (end - pEnvPath) + 1 );
			Q_strcat( search->filename, PATH_SPLITTER );
			Q_snprintf( netpath, MAX_SYSPATH, "%s%s", search->filename, name );

			if( FS_SysFileExists( netpath, !( search->flags & FS_CUSTOM_PATH ) ))
			{
				if( index != NULL )
					*index = -1;
				return search;
			}
			pEnvPath += (end - pEnvPath) + 1; // move pointer
		}
#endif // 0
	}

	if( index != NULL )
		*index = -1;

	return NULL;
}


/*
===========
FS_OpenReadFile

Look for a file in the search paths and open it in read-only mode
===========
*/
file_t *FS_OpenReadFile( const char *filename, const char *mode, qboolean gamedironly )
{
	searchpath_t	*search;
	int		pack_ind;

	search = FS_FindFile( filename, &pack_ind, gamedironly );

	// not found?
	if( search == NULL )
		return NULL; 

	if( search->pack )
		return FS_OpenPackedFile( search->pack, pack_ind );
	else if( search->wad )
		return NULL; // let W_LoadFile get lump correctly
	else if( pack_ind < 0 )
	{
		char	path [MAX_SYSPATH];

		// found in the filesystem?
		Q_sprintf( path, "%s%s", search->filename, filename );
		return FS_SysOpen( path, mode );
	} 

	return NULL;
}

/*
=============================================================================

MAIN PUBLIC FUNCTIONS

=============================================================================
*/
/*
====================
FS_Open

Open a file. The syntax is the same as fopen
====================
*/
file_t *FS_Open( const char *filepath, const char *mode, qboolean gamedironly )
{
	// some stupid mappers used leading '/' or '\' in path to models or sounds
	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( FS_CheckNastyPath( filepath, false ))
		return NULL;

	// if the file is opened in "write", "append", or "read/write" mode
	if( mode[0] == 'w' || mode[0] == 'a'|| mode[0] == 'e' || Q_strchr( mode, '+' ))
	{
		char	real_path[MAX_SYSPATH];

		// open the file on disk directly
		Q_sprintf( real_path, "%s/%s", fs_writedir, filepath );
		FS_CreatePath( real_path );// Create directories up to the file
		return FS_SysOpen( real_path, mode );
	}
	
	// else, we look at the various search paths and open the file in read-only mode
	return FS_OpenReadFile( filepath, mode, gamedironly );
}

/*
====================
FS_Close

Close a file
====================
*/
int FS_Close( file_t *file )
{
	if( !file ) return 0;

	if( close( file->handle ))
		return EOF;

	Mem_Free( file );
	return 0;
}

/*
====================
FS_Write

Write "datasize" bytes into a file
====================
*/
fs_offset_t FS_Write( file_t *file, const void *data, size_t datasize )
{
	fs_offset_t	result;

	if( !file ) return 0;

	// if necessary, seek to the exact file position we're supposed to be
	if( file->buff_ind != file->buff_len )
		lseek( file->handle, file->buff_ind - file->buff_len, SEEK_CUR );

	// purge cached data
	FS_Purge( file );

	// write the buffer and update the position
	result = write( file->handle, data, (fs_offset_t)datasize );
	file->position = lseek( file->handle, 0, SEEK_CUR );

	if( file->real_length < file->position )
		file->real_length = file->position;

	if( result < 0 )
		return 0;
	return result;
}

/*
====================
FS_Read

Read up to "buffersize" bytes from a file
====================
*/
fs_offset_t FS_Read( file_t *file, void *buffer, size_t buffersize )
{
	fs_offset_t	count, done;
	fs_offset_t	nb;

	// nothing to copy
	if( buffersize == 0 ) return 1;

	// Get rid of the ungetc character
	if( file->ungetc != EOF )
	{
		((char*)buffer)[0] = file->ungetc;
		buffersize--;
		file->ungetc = EOF;
		done = 1;
	}
	else done = 0;

	// first, we copy as many bytes as we can from "buff"
	if( file->buff_ind < file->buff_len )
	{
		count = file->buff_len - file->buff_ind;

		done += ((fs_offset_t)buffersize > count ) ? count : (fs_offset_t)buffersize;
		memcpy( buffer, &file->buff[file->buff_ind], done );
		file->buff_ind += done;

		buffersize -= done;
		if( buffersize == 0 )
			return done;
	}

	// NOTE: at this point, the read buffer is always empty

	// we must take care to not read after the end of the file
	count = file->real_length - file->position;

	// if we have a lot of data to get, put them directly into "buffer"
	if( buffersize > sizeof( file->buff ) / 2 )
	{
		if( count > (fs_offset_t)buffersize )
			count = (fs_offset_t)buffersize;
		lseek( file->handle, file->offset + file->position, SEEK_SET );
		nb = read (file->handle, &((byte *)buffer)[done], count );

		if( nb > 0 )
		{
			done += nb;
			file->position += nb;
			// purge cached data
			FS_Purge( file );
		}
	}
	else
	{
		if( count > (fs_offset_t)sizeof( file->buff ))
			count = (fs_offset_t)sizeof( file->buff );
		lseek( file->handle, file->offset + file->position, SEEK_SET );
		nb = read( file->handle, file->buff, count );

		if( nb > 0 )
		{
			file->buff_len = nb;
			file->position += nb;

			// copy the requested data in "buffer" (as much as we can)
			count = (fs_offset_t)buffersize > file->buff_len ? file->buff_len : (fs_offset_t)buffersize;
			memcpy( &((byte *)buffer)[done], file->buff, count );
			file->buff_ind = count;
			done += count;
		}
	}

	return done;
}

/*
====================
FS_Print

Print a string into a file
====================
*/
int FS_Print( file_t *file, const char *msg )
{
	return FS_Write( file, msg, Q_strlen( msg ));
}

/*
====================
FS_Printf

Print a string into a file
====================
*/
int FS_Printf( file_t *file, const char *format, ... )
{
	int	result;
	va_list	args;

	va_start( args, format );
	result = FS_VPrintf( file, format, args );
	va_end( args );

	return result;
}

/*
====================
FS_VPrintf

Print a string into a file
====================
*/
int FS_VPrintf( file_t *file, const char *format, va_list ap )
{
	int	len;
	fs_offset_t	buff_size = MAX_SYSPATH;
	char	*tempbuff;

	if( !file ) return 0;

	while( 1 )
	{
		tempbuff = (char *)Mem_Malloc( fs_mempool, buff_size );
		len = Q_vsprintf( tempbuff, format, ap );

		if( len >= 0 && len < buff_size )
			break;

		Mem_Free( tempbuff );
		buff_size *= 2;
	}

	len = write( file->handle, tempbuff, len );
	Mem_Free( tempbuff );

	return len;
}

/*
====================
FS_Getc

Get the next character of a file
====================
*/
int FS_Getc( file_t *file )
{
	char	c;

	if( FS_Read( file, &c, 1 ) != 1 )
		return EOF;

	return c;
}

/*
====================
FS_UnGetc

Put a character back into the read buffer (only supports one character!)
====================
*/
int FS_UnGetc( file_t *file, byte c )
{
	// If there's already a character waiting to be read
	if( file->ungetc != EOF )
		return EOF;

	file->ungetc = c;
	return c;
}

/*
====================
FS_Gets

Same as fgets
====================
*/
int FS_Gets( file_t *file, byte *string, size_t bufsize )
{
	int	c, end = 0;

	while( 1 )
	{
		c = FS_Getc( file );

		if( c == '\r' || c == '\n' || c < 0 )
			break;

		if( end < bufsize - 1 )
			string[end++] = c;
	}
	string[end] = 0;

	// remove \n following \r
	if( c == '\r' )
	{
		c = FS_Getc( file );

		if( c != '\n' )
			FS_UnGetc( file, (byte)c );
	}

	return c;
}

/*
====================
FS_Seek

Move the position index in a file
====================
*/
int FS_Seek( file_t *file, fs_offset_t offset, int whence )
{
	// compute the file offset
	switch( whence )
	{
	case SEEK_CUR:
		offset += file->position - file->buff_len + file->buff_ind;
		break;
	case SEEK_SET:
		break;
	case SEEK_END:
		offset += file->real_length;
		break;
	default: 
		return -1;
	}
	
	if( offset < 0 || offset > file->real_length )
		return -1;

	// if we have the data in our read buffer, we don't need to actually seek
	if( file->position - file->buff_len <= offset && offset <= file->position )
	{
		file->buff_ind = offset + file->buff_len - file->position;
		return 0;
	}

	// Purge cached data
	FS_Purge( file );

	if( lseek( file->handle, file->offset + offset, SEEK_SET ) == -1 )
		return -1;
	file->position = offset;

	return 0;
}

/*
====================
FS_Tell

Give the current position in a file
====================
*/
fs_offset_t FS_Tell( file_t *file )
{
	if( !file ) return 0;
	return file->position - file->buff_len + file->buff_ind;
}

/*
====================
FS_Eof

indicates at reached end of file
====================
*/
qboolean FS_Eof( file_t *file )
{
	if( !file ) return true;
	return (( file->position - file->buff_len + file->buff_ind ) == file->real_length ) ? true : false;
}

/*
====================
FS_Purge

Erases any buffered input or output data
====================
*/
void FS_Purge( file_t *file )
{
	file->buff_len = 0;
	file->buff_ind = 0;
	file->ungetc = EOF;
}

/*
============
FS_LoadFile

Filename are relative to the xash directory.
Always appends a 0 byte.
============
*/
byte *FS_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	file_t	*file;
	byte	*buf = NULL;
	fs_offset_t	filesize = 0;

	file = FS_Open( path, "rb", gamedironly );

	if( file )
	{
		filesize = file->real_length;
		buf = (byte *)Mem_Malloc( fs_mempool, filesize + 1 );
		buf[filesize] = '\0';
		FS_Read( file, buf, filesize );
		FS_Close( file );
	}
	else
	{
		buf = W_LoadFile( path, &filesize, gamedironly );

		if( !buf )
			buf = Zip_LoadFile( path, &filesize, gamedironly );

	}

	if( filesizeptr )
		*filesizeptr = filesize;

	return buf;
}

qboolean CRC32_File( dword *crcvalue, const char *filename )
{
	char	buffer[1024];
	int	num_bytes;
	file_t	*f;

	f = FS_Open( filename, "rb", false );
	if( !f ) return false;

	Assert( crcvalue != NULL );
	CRC32_Init( crcvalue );

	while( 1 )
	{
		num_bytes = FS_Read( f, buffer, sizeof( buffer ));

		if( num_bytes > 0 )
			CRC32_ProcessBuffer( crcvalue, buffer, num_bytes );

		if( FS_Eof( f )) break;
	}

	FS_Close( f );
	return true;
}

qboolean MD5_HashFile( byte digest[16], const char *pszFileName, uint seed[4] )
{
	file_t		*file;
	byte		buffer[1024];
	MD5Context_t	MD5_Hash;
	int		bytes;

	if(( file = FS_Open( pszFileName, "rb", false )) == NULL )
		return false;

	memset( &MD5_Hash, 0, sizeof( MD5Context_t ));

	MD5Init( &MD5_Hash );

	if( seed )
	{
		MD5Update( &MD5_Hash, (const byte *)seed, 16 );
	}

	while( 1 )
	{
		bytes = FS_Read( file, buffer, sizeof( buffer ));

		if( bytes > 0 )
			MD5Update( &MD5_Hash, buffer, bytes );

		if( FS_Eof( file ))
			break;
	}

	FS_Close( file );
	MD5Final( digest, &MD5_Hash );

	return true;
}

/*
============
FS_LoadFile

Filename are relative to the xash directory.
Always appends a 0 byte.
============
*/
byte *FS_LoadDirectFile( const char *path, fs_offset_t *filesizeptr )
{
	file_t		*file;
	byte		*buf = NULL;
	fs_offset_t	filesize = 0;

	file = FS_SysOpen( path, "rb" );

	if( !file )
	{
		return NULL;
	}

	// Try to load
	filesize = file->real_length;
	buf = (byte *)Mem_Malloc( fs_mempool, filesize + 1 );
	buf[filesize] = '\0';
	FS_Read( file, buf, filesize );
	FS_Close( file );

	if( filesizeptr )
		*filesizeptr = filesize;

	return buf;
}


/*
============
FS_WriteFile

The filename will be prefixed by the current game directory
============
*/
qboolean FS_WriteFile( const char *filename, const void *data, fs_offset_t len )
{
	file_t *file;

	file = FS_Open( filename, "wb", false );

	if( !file )
	{
		Con_Reportf( S_ERROR "FS_WriteFile: failed on %s\n", filename);
		return false;
	}

	FS_Write( file, data, len );
	FS_Close( file );

	return true;
}

/*
=============================================================================

OTHERS PUBLIC FUNCTIONS

=============================================================================
*/
/*
==================
FS_FileExists

Look for a file in the packages and in the filesystem
==================
*/
int FS_FileExists( const char *filename, int gamedironly )
{
	if( FS_FindFile( filename, NULL, gamedironly ))
		return true;
	return false;
}

/*
==================
FS_GetDiskPath

Build direct path for file in the filesystem
return NULL for file in pack
==================
*/
const char *FS_GetDiskPath( const char *name, qboolean gamedironly )
{
	int		index;
	searchpath_t	*search;

	search = FS_FindFile( name, &index, gamedironly );

	if( search )
	{
		if( index != -1 ) // file in pack or wad
			return NULL;
		return va( "%s%s", search->filename, name );
	}

	return NULL;
}

/*
==================
FS_CheckForCrypt

return true if library is crypted
==================
*/
qboolean FS_CheckForCrypt( const char *dllname )
{
	file_t	*f;
	int	key;

	f = FS_Open( dllname, "rb", false );
	if( !f ) return false;

	FS_Seek( f, 64, SEEK_SET );	// skip first 64 bytes
	FS_Read( f, &key, sizeof( key ));
	FS_Close( f );

	return ( key == 0x12345678 ) ? true : false;
}

/*
==================
FS_FindLibrary

search for library, assume index is valid
only for internal use
==================
*/
dll_user_t *FS_FindLibrary( const char *dllname, qboolean directpath )
{
	string		dllpath;
	searchpath_t	*search;
	dll_user_t	*hInst;
	int		i, index;
	int		start = 0;

	// check for bad exports
	if( !COM_CheckString( dllname ))
		return NULL;

	fs_ext_path = directpath;

	// HACKHACK remove absoulte path to valve folder
	if( !Q_strnicmp( dllname, "..\\valve\\", 9 ) || !Q_strnicmp( dllname, "../valve/", 9 ))
		start += 9;

	// replace all backward slashes
	for( i = 0; i < Q_strlen( dllname ); i++ )
	{
		if( dllname[i+start] == '\\' ) dllpath[i] = '/';
		else dllpath[i] = Q_tolower( dllname[i+start] );
	}
	dllpath[i] = '\0';

	COM_DefaultExtension( dllpath, "."OS_LIB_EXT );	// apply ext if forget
	search = FS_FindFile( dllpath, &index, false );

	if( !search && !directpath )
	{
		fs_ext_path = false;

		// trying check also 'bin' folder for indirect paths
		Q_strncpy( dllpath, dllname, sizeof( dllpath ));
		search = FS_FindFile( dllpath, &index, false );
		if( !search ) return NULL; // unable to find
	}

	// NOTE: for libraries we not fail even if search is NULL
	// let the OS find library himself
	hInst = Mem_Calloc( host.mempool, sizeof( dll_user_t ));	

	// save dllname for debug purposes
	Q_strncpy( hInst->dllName, dllname, sizeof( hInst->dllName ));

	// shortPath is used for LibraryLoadSymbols only
	Q_strncpy( hInst->shortPath, dllpath, sizeof( hInst->shortPath ));

	hInst->encrypted = FS_CheckForCrypt( dllpath );

	if( index < 0 && !hInst->encrypted && search )
	{
		Q_snprintf( hInst->fullPath, sizeof( hInst->fullPath ), "%s%s", search->filename, dllpath );
		hInst->custom_loader = false;	// we can loading from disk and use normal debugging
	}
	else
	{
		// NOTE: if search is NULL let the OS found library himself
		Q_strncpy( hInst->fullPath, dllpath, sizeof( hInst->fullPath ));
		hInst->custom_loader = (search) ? true : false;
	}
	fs_ext_path = false; // always reset direct paths
		
	return hInst;
}

/*
==================
FS_FileSize

return size of file in bytes
==================
*/
fs_offset_t FS_FileSize( const char *filename, qboolean gamedironly )
{
	int	length = -1; // in case file was missed
	file_t	*fp;	

	fp = FS_Open( filename, "rb", gamedironly );

	if( fp )
	{
		// it exists
		FS_Seek( fp, 0, SEEK_END );
		length = FS_Tell( fp );
		FS_Close( fp );
	}

	return length;
}

/*
==================
FS_FileLength

return size of file in bytes
==================
*/
fs_offset_t FS_FileLength( file_t *f )
{
	if( !f ) return 0;
	return f->real_length;
}

/*
==================
FS_FileTime

return time of creation file in seconds
==================
*/
int FS_FileTime( const char *filename, qboolean gamedironly )
{
	searchpath_t	*search;
	int		pack_ind;
	
	search = FS_FindFile( filename, &pack_ind, gamedironly );
	if( !search ) return -1; // doesn't exist

	if( search->pack ) // grab pack filetime
		return search->pack->filetime;
	else if( search->wad ) // grab wad filetime
		return search->wad->filetime;
	else if( search->zip )
		return search->zip->filetime;
	else if( pack_ind < 0 )
	{
		// found in the filesystem?
		char	path [MAX_SYSPATH];

		Q_sprintf( path, "%s%s", search->filename, filename );
		return FS_SysFileTime( path );
	}

	return -1; // doesn't exist
}

/*
==================
FS_Rename

rename specified file from gamefolder
==================
*/
qboolean FS_Rename( const char *oldname, const char *newname )
{
	char	oldpath[MAX_SYSPATH], newpath[MAX_SYSPATH];
	qboolean	iRet;

	if( !oldname || !newname || !*oldname || !*newname )
		return false;

	Q_snprintf( oldpath, sizeof( oldpath ), "%s%s", fs_writedir, oldname );
	Q_snprintf( newpath, sizeof( newpath ), "%s%s", fs_writedir, newname );

	COM_FixSlashes( oldpath );
	COM_FixSlashes( newpath );

	iRet = rename( oldpath, newpath );

	return (iRet == 0);
}

/*
==================
FS_Delete

delete specified file from gamefolder
==================
*/
qboolean FS_Delete( const char *path )
{
	char	real_path[MAX_SYSPATH];
	qboolean	iRet;

	if( !path || !*path )
		return false;

	Q_snprintf( real_path, sizeof( real_path ), "%s%s", fs_writedir, path );
	COM_FixSlashes( real_path );
	iRet = remove( real_path );

	return (iRet == 0);
}

/*
==================
FS_FileCopy

==================
*/
qboolean FS_FileCopy( file_t *pOutput, file_t *pInput, int fileSize )
{
	char	*buf = Mem_Malloc( fs_mempool, FILE_COPY_SIZE );
	int	size, readSize;
	qboolean	done = true;

	while( fileSize > 0 )
	{
		if( fileSize > FILE_COPY_SIZE )
			size = FILE_COPY_SIZE;
		else size = fileSize;

		if(( readSize = FS_Read( pInput, buf, size )) < size )
		{
			Con_Reportf( S_ERROR "FS_FileCopy: unexpected end of input file (%d < %d)\n", readSize, size );
			fileSize = 0;
			done = false;
			break;
		}

		FS_Write( pOutput, buf, readSize );
		fileSize -= size;
	}

	Mem_Free( buf );
	return done;
}

/*
===========
FS_Search

Allocate and fill a search structure with information on matching filenames.
===========
*/
search_t *FS_Search( const char *pattern, int caseinsensitive, int gamedironly )
{
	search_t		*search = NULL;
	searchpath_t	*searchpath;
	pack_t		*pak;
	wfile_t		*wad;
	int		i, basepathlength, numfiles, numchars;
	int		resultlistindex, dirlistindex;
	const char	*slash, *backslash, *colon, *separator;
	string		netpath, temp;
	stringlist_t	resultlist;
	stringlist_t	dirlist;
	char		*basepath;

	if( pattern[0] == '.' || pattern[0] == ':' || pattern[0] == '/' || pattern[0] == '\\' )
		return NULL; // punctuation issues

	stringlistinit( &resultlist );
	stringlistinit( &dirlist );
	slash = Q_strrchr( pattern, '/' );
	backslash = Q_strrchr( pattern, '\\' );
	colon = Q_strrchr( pattern, ':' );
	separator = max( slash, backslash );
	separator = max( separator, colon );
	basepathlength = separator ? (separator + 1 - pattern) : 0;
	basepath = Mem_Calloc( fs_mempool, basepathlength + 1 );
	if( basepathlength ) memcpy( basepath, pattern, basepathlength );
	basepath[basepathlength] = 0;

	// search through the path, one element at a time
	for( searchpath = fs_searchpaths; searchpath; searchpath = searchpath->next )
	{
		if( gamedironly && !FBitSet( searchpath->flags, FS_GAMEDIRONLY_SEARCH_FLAGS ))
			continue;

		// is the element a pak file?
		if( searchpath->pack )
		{
			// look through all the pak file elements
			pak = searchpath->pack;
			for( i = 0; i < pak->numfiles; i++ )
			{
				Q_strncpy( temp, pak->files[i].name, sizeof( temp ));
				while( temp[0] )
				{
					if( matchpattern( temp, (char *)pattern, true ))
					{
						for( resultlistindex = 0; resultlistindex < resultlist.numstrings; resultlistindex++ )
						{
							if( !Q_strcmp( resultlist.strings[resultlistindex], temp ))
								break;
						}

						if( resultlistindex == resultlist.numstrings )
							stringlistappend( &resultlist, temp );
					}

					// strip off one path element at a time until empty
					// this way directories are added to the listing if they match the pattern
					slash = Q_strrchr( temp, '/' );
					backslash = Q_strrchr( temp, '\\' );
					colon = Q_strrchr( temp, ':' );
					separator = temp;
					if( separator < slash )
						separator = slash;
					if( separator < backslash )
						separator = backslash;
					if( separator < colon )
						separator = colon;
					*((char *)separator) = 0;
				}
			}
		}
		else if( searchpath->wad )
		{
			string	wadpattern, wadname, temp2;
			signed char	type = W_TypeFromExt( pattern );
			qboolean	anywadname = true;
			string	wadfolder;

			// quick reject by filetype
			if( type == TYP_NONE ) continue;
			COM_ExtractFilePath( pattern, wadname );
			COM_FileBase( pattern, wadpattern );
			wadfolder[0] = '\0';

			if( Q_strlen( wadname ))
			{
				COM_FileBase( wadname, wadname );
				Q_strncpy( wadfolder, wadname, sizeof( wadfolder ));
				COM_DefaultExtension( wadname, ".wad" );
				anywadname = false;
			}

			// make wadname from wad fullpath
			COM_FileBase( searchpath->wad->filename, temp2 );
			COM_DefaultExtension( temp2, ".wad" );

			// quick reject by wadname
			if( !anywadname && Q_stricmp( wadname, temp2 ))
				continue;

			// look through all the wad file elements
			wad = searchpath->wad;

			for( i = 0; i < wad->numlumps; i++ )
			{
				// if type not matching, we already have no chance ...
				if( type != TYP_ANY && wad->lumps[i].type != type )
					continue;

				// build the lumpname with image suffix (if present)
				Q_strncpy( temp, wad->lumps[i].name, sizeof( temp ));

				while( temp[0] )
				{
					if( matchpattern( temp, wadpattern, true ))
					{
						for( resultlistindex = 0; resultlistindex < resultlist.numstrings; resultlistindex++ )
						{
							if( !Q_strcmp( resultlist.strings[resultlistindex], temp ))
								break;
						}

						if( resultlistindex == resultlist.numstrings )
						{
							// build path: wadname/lumpname.ext
							Q_snprintf( temp2, sizeof(temp2), "%s/%s", wadfolder, temp );
							COM_DefaultExtension( temp2, va(".%s", W_ExtFromType( wad->lumps[i].type )));
							stringlistappend( &resultlist, temp2 );
						}
					}

					// strip off one path element at a time until empty
					// this way directories are added to the listing if they match the pattern
					slash = Q_strrchr( temp, '/' );
					backslash = Q_strrchr( temp, '\\' );
					colon = Q_strrchr( temp, ':' );
					separator = temp;
					if( separator < slash )
						separator = slash;
					if( separator < backslash )
						separator = backslash;
					if( separator < colon )
						separator = colon;
					*((char *)separator) = 0;
				}
			}
		}
		else
		{
			// get a directory listing and look at each name
			Q_sprintf( netpath, "%s%s", searchpath->filename, basepath );
			stringlistinit( &dirlist );
			listdirectory( &dirlist, netpath, caseinsensitive );

			for( dirlistindex = 0; dirlistindex < dirlist.numstrings; dirlistindex++ )
			{
				Q_sprintf( temp, "%s%s", basepath, dirlist.strings[dirlistindex] );

				if( matchpattern( temp, (char *)pattern, true ))
				{
					for( resultlistindex = 0; resultlistindex < resultlist.numstrings; resultlistindex++ )
					{
						if( !Q_strcmp( resultlist.strings[resultlistindex], temp ))
							break;
					}

					if( resultlistindex == resultlist.numstrings )
						stringlistappend( &resultlist, temp );
				}
			}

			stringlistfreecontents( &dirlist );
		}
	}

	if( resultlist.numstrings )
	{
		stringlistsort( &resultlist );
		numfiles = resultlist.numstrings;
		numchars = 0;

		for( resultlistindex = 0; resultlistindex < resultlist.numstrings; resultlistindex++ )
			numchars += (int)Q_strlen( resultlist.strings[resultlistindex]) + 1;
		search = Mem_Calloc( fs_mempool, sizeof(search_t) + numchars + numfiles * sizeof( char* ));
		search->filenames = (char **)((char *)search + sizeof( search_t ));
		search->filenamesbuffer = (char *)((char *)search + sizeof( search_t ) + numfiles * sizeof( char* ));
		search->numfilenames = (int)numfiles;
		numfiles = numchars = 0;

		for( resultlistindex = 0; resultlistindex < resultlist.numstrings; resultlistindex++ )
		{
			size_t	textlen;

			search->filenames[numfiles] = search->filenamesbuffer + numchars;
			textlen = Q_strlen(resultlist.strings[resultlistindex]) + 1;
			memcpy( search->filenames[numfiles], resultlist.strings[resultlistindex], textlen );
			numfiles++;
			numchars += (int)textlen;
		}
	}

	stringlistfreecontents( &resultlist );

	Mem_Free( basepath );

	return search;
}

void FS_InitMemory( void )
{
	fs_mempool = Mem_AllocPool( "FileSystem Pool" );	
	fs_searchpaths = NULL;
}

/*
=============================================================================

WADSYSTEM PRIVATE ROUTINES

=============================================================================
*/
// associate extension with wad type
static const wadtype_t wad_types[7] =
{
{ "pal", TYP_PALETTE	}, // palette
{ "dds", TYP_DDSTEX 	}, // DDS image
{ "lmp", TYP_GFXPIC		}, // quake1, hl pic
{ "fnt", TYP_QFONT		}, // hl qfonts
{ "mip", TYP_MIPTEX		}, // hl/q1 mip
{ "txt", TYP_SCRIPT		}, // scripts
{ NULL,  TYP_NONE		}
};

/*
===========
W_TypeFromExt

Extracts file type from extension
===========
*/
static signed char W_TypeFromExt( const char *lumpname )
{
	const char	*ext = COM_FileExtension( lumpname );
	const wadtype_t	*type;

	// we not known about filetype, so match only by filename
	if( !Q_strcmp( ext, "*" ) || !Q_strcmp( ext, "" ))
		return TYP_ANY;
	
	for( type = wad_types; type->ext; type++ )
	{
		if( !Q_stricmp( ext, type->ext ))
			return type->type;
	}
	return TYP_NONE;
}

/*
===========
W_ExtFromType

Convert type to extension
===========
*/
static const char *W_ExtFromType( signed char lumptype )
{
	const wadtype_t	*type;

	// we not known aboyt filetype, so match only by filename
	if( lumptype == TYP_NONE || lumptype == TYP_ANY )
		return "";

	for( type = wad_types; type->ext; type++ )
	{
		if( lumptype == type->type )
			return type->ext;
	}
	return "";
}

/*
===========
W_FindLump

Serach for already existed lump
===========
*/
static dlumpinfo_t *W_FindLump( wfile_t *wad, const char *name, const signed char matchtype )
{
	int	left, right;

	if( !wad || !wad->lumps || matchtype == TYP_NONE )
		return NULL;

	// look for the file (binary search)
	left = 0;
	right = wad->numlumps - 1;
	
	while( left <= right )
	{
		int	middle = (left + right) / 2;
		int	diff = Q_stricmp( wad->lumps[middle].name, name );

		if( !diff )
		{
			if(( matchtype == TYP_ANY ) || ( matchtype == wad->lumps[middle].type ))
				return &wad->lumps[middle]; // found
			else if( wad->lumps[middle].type < matchtype )
				diff = 1;
			else if( wad->lumps[middle].type > matchtype )
				diff = -1;
			else break; // not found
		}

		// if we're too far in the list
		if( diff > 0 ) right = middle - 1;
		else left = middle + 1;
	}

	return NULL;
}

/*
====================
W_AddFileToWad

Add a file to the list of files contained into a package
and sort LAT in alpha-bethical order
====================
*/
static dlumpinfo_t *W_AddFileToWad( const char *name, wfile_t *wad, dlumpinfo_t *newlump )
{
	int		left, right;
	dlumpinfo_t	*plump;

	// look for the slot we should put that file into (binary search)
	left = 0;
	right = wad->numlumps - 1;

	while( left <= right )
	{
		int	middle = ( left + right ) / 2;
		int	diff = Q_stricmp( wad->lumps[middle].name, name );

		if( !diff )
		{
			if( wad->lumps[middle].type < newlump->type )
				diff = 1;
			else if( wad->lumps[middle].type > newlump->type )
				diff = -1;
			else Con_Reportf( S_WARN "Wad %s contains the file %s several times\n", wad->filename, name );
		}

		// If we're too far in the list
		if( diff > 0 ) right = middle - 1;
		else left = middle + 1;
	}

	// we have to move the right of the list by one slot to free the one we need
	plump = &wad->lumps[left];
	memmove( plump + 1, plump, ( wad->numlumps - left ) * sizeof( *plump ));
	wad->numlumps++;

	*plump = *newlump;
	memcpy( plump->name, name, sizeof( plump->name ));

	return plump;
}

/*
===========
W_ReadLump

reading lump into temp buffer
===========
*/
byte *W_ReadLump( wfile_t *wad, dlumpinfo_t *lump, fs_offset_t *lumpsizeptr )
{
	size_t	oldpos, size = 0;
	byte	*buf;

	// assume error
	if( lumpsizeptr ) *lumpsizeptr = 0;

	// no wads loaded
	if( !wad || !lump ) return NULL;

	oldpos = FS_Tell( wad->handle ); // don't forget restore original position

	if( FS_Seek( wad->handle, lump->filepos, SEEK_SET ) == -1 )
	{
		Con_Reportf( S_ERROR "W_ReadLump: %s is corrupted\n", lump->name );
		FS_Seek( wad->handle, oldpos, SEEK_SET );
		return NULL;
	}

	buf = (byte *)Mem_Malloc( wad->mempool, lump->disksize );
	size = FS_Read( wad->handle, buf, lump->disksize );

	if( size < lump->disksize )
	{
		Con_Reportf( S_WARN "W_ReadLump: %s is probably corrupted\n", lump->name );
		FS_Seek( wad->handle, oldpos, SEEK_SET );
		Mem_Free( buf );
		return NULL;
	}

	if( lumpsizeptr ) *lumpsizeptr = lump->disksize;
	FS_Seek( wad->handle, oldpos, SEEK_SET );

	return buf;
}

/*
=============================================================================

WADSYSTEM PUBLIC BASE FUNCTIONS

=============================================================================
*/
/*
===========
W_Open

open the wad for reading & writing
===========
*/
wfile_t *W_Open( const char *filename, int *error )
{
	wfile_t		*wad = (wfile_t *)Mem_Calloc( fs_mempool, sizeof( wfile_t ));
	const char *basename;
	int		i, lumpcount;
	dlumpinfo_t	*srclumps;
	size_t		lat_size;
	dwadinfo_t	header;

	// NOTE: FS_Open is load wad file from the first pak in the list (while fs_ext_path is false)
	if( fs_ext_path ) basename = filename;
	else basename = COM_FileWithoutPath( filename );

	wad->handle = FS_Open( basename, "rb", false );

	// HACKHACK: try to open WAD by full path for RoDir, when searchpaths are not ready
	if( host.rodir[0] && fs_ext_path && wad->handle == NULL )
		wad->handle = FS_SysOpen( filename, "rb" );

	if( wad->handle == NULL )
	{	
		Con_Reportf( S_ERROR "W_Open: couldn't open %s\n", filename );
		if( error ) *error = WAD_LOAD_COULDNT_OPEN;
		W_Close( wad );
		return NULL;
	}

	// copy wad name
	Q_strncpy( wad->filename, filename, sizeof( wad->filename ));
	wad->filetime = FS_SysFileTime( filename );
	wad->mempool = Mem_AllocPool( filename );

	if( FS_Read( wad->handle, &header, sizeof( dwadinfo_t )) != sizeof( dwadinfo_t ))
	{
		Con_Reportf( S_ERROR "W_Open: %s can't read header\n", filename );
		if( error ) *error = WAD_LOAD_BAD_HEADER;
		W_Close( wad );
		return NULL;
	}

	if( header.ident != IDWAD2HEADER && header.ident != IDWAD3HEADER )
	{
		Con_Reportf( S_ERROR "W_Open: %s is not a WAD2 or WAD3 file\n", filename );
		if( error ) *error = WAD_LOAD_BAD_HEADER;
		W_Close( wad );
		return NULL;
	}

	lumpcount = header.numlumps;

	if( lumpcount >= MAX_FILES_IN_WAD )
	{
		Con_Reportf( S_WARN "W_Open: %s is full (%i lumps)\n", filename, lumpcount );
		if( error ) *error = WAD_LOAD_TOO_MANY_FILES;
	}
	else if( lumpcount <= 0 )
	{
		Con_Reportf( S_ERROR "W_Open: %s has no lumps\n", filename );
		if( error ) *error = WAD_LOAD_NO_FILES;
		W_Close( wad );
		return NULL;
	}
	else if( error ) *error = WAD_LOAD_OK;

	wad->infotableofs = header.infotableofs; // save infotableofs position

	if( FS_Seek( wad->handle, wad->infotableofs, SEEK_SET ) == -1 )
	{
		Con_Reportf( S_ERROR "W_Open: %s can't find lump allocation table\n", filename );
		if( error ) *error = WAD_LOAD_BAD_FOLDERS;
		W_Close( wad );
		return NULL;
	}

	lat_size = lumpcount * sizeof( dlumpinfo_t );

	// NOTE: lumps table can be reallocated for O_APPEND mode
	srclumps = (dlumpinfo_t *)Mem_Malloc( wad->mempool, lat_size );

	if( FS_Read( wad->handle, srclumps, lat_size ) != lat_size )
	{
		Con_Reportf( S_ERROR "W_ReadLumpTable: %s has corrupted lump allocation table\n", wad->filename );
		if( error ) *error = WAD_LOAD_CORRUPTED;
		Mem_Free( srclumps );
		W_Close( wad );
		return NULL;
	}

	// starting to add lumps
	wad->lumps = (dlumpinfo_t *)Mem_Calloc( wad->mempool, lat_size );
	wad->numlumps = 0;

	// sort lumps for binary search
	for( i = 0; i < lumpcount; i++ )
	{
		char	name[16];
		int	k;

		// cleanup lumpname
		Q_strnlwr( srclumps[i].name, name, sizeof( srclumps[i].name ));

		// check for '*' symbol issues (quake1)
		k = Q_strlen( Q_strrchr( name, '*' ));
		if( k ) name[Q_strlen( name ) - k] = '!';

		// check for Quake 'conchars' issues (only lmp loader really allows to read this lame pic)
		if( srclumps[i].type == 68 && !Q_stricmp( srclumps[i].name, "conchars" ))
			srclumps[i].type = TYP_GFXPIC; 

		W_AddFileToWad( name, wad, &srclumps[i] );
	}

	// release source lumps
	Mem_Free( srclumps );

	// and leave the file open
	return wad;
}

/*
===========
W_Close

finalize wad or just close
===========
*/
void W_Close( wfile_t *wad )
{
	if( !wad ) return;

	Mem_FreePool( &wad->mempool );
	if( wad->handle != NULL )
		FS_Close( wad->handle );	
	Mem_Free( wad ); // free himself
}

/*
=============================================================================

FILESYSTEM IMPLEMENTATION

=============================================================================
*/
/*
===========
W_LoadFile

loading lump into the tmp buffer
===========
*/
static byte *W_LoadFile( const char *path, fs_offset_t *lumpsizeptr, qboolean gamedironly )
{
	searchpath_t	*search;
	int		index;

	search = FS_FindFile( path, &index, gamedironly );
	if( search && search->wad )
		return W_ReadLump( search->wad, &search->wad->lumps[index], lumpsizeptr ); 
	return NULL;
}
