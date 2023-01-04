/*
zip.c - ZIP support for filesystem
Copyright (C) 2019 Mr0maks
Copyright (C) 2022 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#if XASH_POSIX
#include <unistd.h>
#endif
#include <errno.h>
#include <stddef.h>
#include STDINT_H
#include "port.h"
#include "filesystem_internal.h"
#include "crtlib.h"
#include "common/com_strings.h"
#include "miniz.h"

#define ZIP_HEADER_LF      (('K'<<8)+('P')+(0x03<<16)+(0x04<<24))
#define ZIP_HEADER_SPANNED ((0x08<<24)+(0x07<<16)+('K'<<8)+'P')

#define ZIP_HEADER_CDF ((0x02<<24)+(0x01<<16)+('K'<<8)+'P')
#define ZIP_HEADER_EOCD ((0x06<<24)+(0x05<<16)+('K'<<8)+'P')

#define ZIP_COMPRESSION_NO_COMPRESSION	    0
#define ZIP_COMPRESSION_DEFLATED	    8

#define ZIP_ZIP64 0xffffffff

#pragma pack( push, 1 )
typedef struct zip_header_s
{
	uint32_t	signature; // little endian ZIP_HEADER
	uint16_t	version; // version of pkzip need to unpack
	uint16_t	flags; // flags (16 bits == 16 flags)
	uint16_t	compression_flags; // compression flags (bits)
	uint32_t	dos_date; // file modification time and file modification date
	uint32_t	crc32; //crc32
	uint32_t	compressed_size;
	uint32_t	uncompressed_size;
	uint16_t	filename_len;
	uint16_t	extrafield_len;
} zip_header_t;

/*
  in zip64 comp and uncompr size == 0xffffffff remeber this
  compressed and uncompress filesize stored in extra field
*/

typedef struct zip_header_extra_s
{
	uint32_t	signature; // ZIP_HEADER_SPANNED
	uint32_t	crc32;
	uint32_t	compressed_size;
	uint32_t	uncompressed_size;
} zip_header_extra_t;

typedef struct zip_cdf_header_s
{
	uint32_t	signature;
	uint16_t	version;
	uint16_t	version_need;
	uint16_t	generalPurposeBitFlag;
	uint16_t	flags;
	uint16_t	modification_time;
	uint16_t	modification_date;
	uint32_t	crc32;
	uint32_t	compressed_size;
	uint32_t	uncompressed_size;
	uint16_t	filename_len;
	uint16_t	extrafield_len;
	uint16_t	file_commentary_len;
	uint16_t	disk_start;
	uint16_t	internal_attr;
	uint32_t	external_attr;
	uint32_t	local_header_offset;
} zip_cdf_header_t;

typedef struct zip_header_eocd_s
{
	uint16_t	disk_number;
	uint16_t	start_disk_number;
	uint16_t	number_central_directory_record;
	uint16_t	total_central_directory_record;
	uint32_t	size_of_central_directory;
	uint32_t	central_directory_offset;
	uint16_t	commentary_len;
} zip_header_eocd_t;
#pragma pack( pop )

// ZIP errors
enum
{
	ZIP_LOAD_OK = 0,
	ZIP_LOAD_COULDNT_OPEN,
	ZIP_LOAD_BAD_HEADER,
	ZIP_LOAD_BAD_FOLDERS,
	ZIP_LOAD_NO_FILES,
	ZIP_LOAD_CORRUPTED
};

typedef struct zipfile_s
{
	char		name[MAX_SYSPATH];
	fs_offset_t	offset; // offset of local file header
	fs_offset_t	size; //original file size
	fs_offset_t	compressed_size; // compressed file size
	uint16_t flags;
} zipfile_t;

struct zip_s
{
	int		handle;
	int		numfiles;
	time_t		filetime;
	zipfile_t	*files;
};

#ifdef XASH_REDUCE_FD
static void FS_EnsureOpenZip( zip_t *zip )
{
	if( fs_last_zip == zip )
		return;

	if( fs_last_zip && (fs_last_zip->handle != -1) )
	{
		close( fs_last_zip->handle );
		fs_last_zip->handle = -1;
	}
	fs_last_zip = zip;
	if( zip && (zip->handle == -1) )
		zip->handle = open( zip->filename, O_RDONLY|O_BINARY );
}
#else
static void FS_EnsureOpenZip( zip_t *zip ) {}
#endif

/*
============
FS_CloseZIP
============
*/
static void FS_CloseZIP( zip_t *zip )
{
	if( zip->files )
		Mem_Free( zip->files );

	FS_EnsureOpenZip( NULL );

	if( zip->handle >= 0 )
		close( zip->handle );

	Mem_Free( zip );
}

/*
============
FS_Close_ZIP
============
*/
static void FS_Close_ZIP( searchpath_t *search )
{
	FS_CloseZIP( search->zip );
}

/*
============
FS_SortZip
============
*/
static int FS_SortZip( const void *a, const void *b )
{
	return Q_stricmp( ( ( zipfile_t* )a )->name, ( ( zipfile_t* )b )->name );
}

/*
============
FS_LoadZip
============
*/
static zip_t *FS_LoadZip( const char *zipfile, int *error )
{
	int		  numpackfiles = 0, i;
	zip_cdf_header_t  header_cdf;
	zip_header_eocd_t header_eocd;
	uint32_t          signature;
	fs_offset_t	  filepos = 0, length;
	zipfile_t	  *info = NULL;
	char		  filename_buffer[MAX_SYSPATH];
	zip_t         *zip = (zip_t *)Mem_Calloc( fs_mempool, sizeof( *zip ));
	fs_size_t       c;

	zip->handle = open( zipfile, O_RDONLY|O_BINARY );

	if( zip->handle < 0 )
	{
		Con_Reportf( S_ERROR "%s couldn't open\n", zipfile );

		if( error )
			*error = ZIP_LOAD_COULDNT_OPEN;

		FS_CloseZIP( zip );
		return NULL;
	}

	length = lseek( zip->handle, 0, SEEK_END );

	if( length > UINT_MAX )
	{
		Con_Reportf( S_ERROR "%s bigger than 4GB.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_COULDNT_OPEN;

		FS_CloseZIP( zip );
		return NULL;
	}

	lseek( zip->handle, 0, SEEK_SET );

	c = read( zip->handle, &signature, sizeof( signature ) );

	if( c != sizeof( signature ) || signature == ZIP_HEADER_EOCD )
	{
		Con_Reportf( S_WARN "%s has no files. Ignored.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_NO_FILES;

		FS_CloseZIP( zip );
		return NULL;
	}

	if( signature != ZIP_HEADER_LF )
	{
		Con_Reportf( S_ERROR "%s is not a zip file. Ignored.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_BAD_HEADER;

		FS_CloseZIP( zip );
		return NULL;
	}

	// Find oecd
	lseek( zip->handle, 0, SEEK_SET );
	filepos = length;

	while ( filepos > 0 )
	{
		lseek( zip->handle, filepos, SEEK_SET );
		c = read( zip->handle, &signature, sizeof( signature ) );

		if( c == sizeof( signature ) && signature == ZIP_HEADER_EOCD )
			break;

		filepos -= sizeof( char ); // step back one byte
	}

	if( ZIP_HEADER_EOCD != signature )
	{
		Con_Reportf( S_ERROR "cannot find EOCD in %s. Zip file corrupted.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_BAD_HEADER;

		FS_CloseZIP( zip );
		return NULL;
	}

	c = read( zip->handle, &header_eocd, sizeof( header_eocd ) );

	if( c != sizeof( header_eocd ))
	{
		Con_Reportf( S_ERROR "invalid EOCD header in %s. Zip file corrupted.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_BAD_HEADER;

		FS_CloseZIP( zip );
		return NULL;
	}

	// Move to CDF start
	lseek( zip->handle, header_eocd.central_directory_offset, SEEK_SET );

	// Calc count of files in archive
	info = (zipfile_t *)Mem_Calloc( fs_mempool, sizeof( *info ) * header_eocd.total_central_directory_record );

	for( i = 0; i < header_eocd.total_central_directory_record; i++ )
	{
		c = read( zip->handle, &header_cdf, sizeof( header_cdf ) );

		if( c != sizeof( header_cdf ) || header_cdf.signature != ZIP_HEADER_CDF )
		{
			Con_Reportf( S_ERROR "CDF signature mismatch in %s. Zip file corrupted.\n", zipfile );

			if( error )
				*error = ZIP_LOAD_BAD_HEADER;

			Mem_Free( info );
			FS_CloseZIP( zip );
			return NULL;
		}

		if( header_cdf.uncompressed_size && header_cdf.filename_len && ( header_cdf.filename_len < MAX_SYSPATH ) )
		{
			memset( &filename_buffer, '\0', MAX_SYSPATH );
			c = read( zip->handle, &filename_buffer, header_cdf.filename_len );

			if( c != header_cdf.filename_len )
			{
				Con_Reportf( S_ERROR "filename length mismatch in %s. Zip file corrupted.\n", zipfile );

				if( error )
					*error = ZIP_LOAD_CORRUPTED;

				Mem_Free( info );
				FS_CloseZIP( zip );
				return NULL;
			}

			Q_strncpy( info[numpackfiles].name, filename_buffer, MAX_SYSPATH );

			info[numpackfiles].size = header_cdf.uncompressed_size;
			info[numpackfiles].compressed_size = header_cdf.compressed_size;
			info[numpackfiles].offset = header_cdf.local_header_offset;
			numpackfiles++;
		}
		else
			lseek( zip->handle, header_cdf.filename_len, SEEK_CUR );

		if( header_cdf.extrafield_len )
			lseek( zip->handle, header_cdf.extrafield_len, SEEK_CUR );

		if( header_cdf.file_commentary_len )
			lseek( zip->handle, header_cdf.file_commentary_len, SEEK_CUR );
	}

	// recalculate offsets
	for( i = 0; i < numpackfiles; i++ )
	{
		zip_header_t header;

		lseek( zip->handle, info[i].offset, SEEK_SET );
		c = read( zip->handle, &header, sizeof( header ) );

		if( c != sizeof( header ))
		{
			Con_Reportf( S_ERROR "header length mismatch in %s. Zip file corrupted.\n", zipfile );

			if( error )
				*error = ZIP_LOAD_CORRUPTED;

			Mem_Free( info );
			FS_CloseZIP( zip );
			return NULL;
		}

		info[i].flags = header.compression_flags;
		info[i].offset = info[i].offset + header.filename_len + header.extrafield_len + sizeof( header );
	}

	zip->filetime = FS_SysFileTime( zipfile );
	zip->numfiles = numpackfiles;
	zip->files = info;

	qsort( zip->files, zip->numfiles, sizeof( *zip->files ), FS_SortZip );

#ifdef XASH_REDUCE_FD
	// will reopen when needed
	close(zip->handle);
	zip->handle = -1;
#endif

	if( error )
		*error = ZIP_LOAD_OK;

	return zip;
}

/*
===========
FS_OpenZipFile

Open a packed file using its package file descriptor
===========
*/
file_t *FS_OpenFile_ZIP( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	zipfile_t	*pfile;
	pfile = &search->zip->files[pack_ind];

	// compressed files handled in Zip_LoadFile
	if( pfile->flags != ZIP_COMPRESSION_NO_COMPRESSION )
	{
		Con_Printf( S_ERROR "%s: can't open compressed file %s\n", __FUNCTION__, pfile->name );
		return NULL;
	}

	return FS_OpenHandle( search->filename, search->zip->handle, pfile->offset, pfile->size );
}

/*
===========
FS_LoadZIPFile

===========
*/
byte *FS_LoadZIPFile( const char *path, fs_offset_t *sizeptr, qboolean gamedironly )
{
	searchpath_t	*search;
	int		index;
	zipfile_t	*file = NULL;
	byte		*compressed_buffer = NULL, *decompressed_buffer = NULL;
	int		zlib_result = 0;
	dword		test_crc, final_crc;
	z_stream	decompress_stream;
	size_t      c;

	if( sizeptr ) *sizeptr = 0;

	search = FS_FindFile( path, &index, NULL, 0, gamedironly );

	if( !search || search->type != SEARCHPATH_ZIP )
		return  NULL;

	file = &search->zip->files[index];

	FS_EnsureOpenZip( search->zip );

	if( lseek( search->zip->handle, file->offset, SEEK_SET ) == -1 )
		return NULL;

	/*if( read( search->zip->handle, &header, sizeof( header ) ) < 0 )
		return NULL;

	if( header.signature != ZIP_HEADER_LF )
	{
		Con_Reportf( S_ERROR "Zip_LoadFile: %s signature error\n", file->name );
		return NULL;
	}*/

	if( file->flags == ZIP_COMPRESSION_NO_COMPRESSION )
	{
		decompressed_buffer = Mem_Malloc( fs_mempool, file->size + 1 );
		decompressed_buffer[file->size] = '\0';

		c = read( search->zip->handle, decompressed_buffer, file->size );
		if( c != file->size )
		{
			Con_Reportf( S_ERROR "Zip_LoadFile: %s size doesn't match\n", file->name );
			return NULL;
		}

#if 0
		CRC32_Init( &test_crc );
		CRC32_ProcessBuffer( &test_crc, decompressed_buffer, file->size );

		final_crc = CRC32_Final( test_crc );

		if( final_crc != file->crc32 )
		{
			Con_Reportf( S_ERROR "Zip_LoadFile: %s file crc32 mismatch\n", file->name );
			Mem_Free( decompressed_buffer );
			return NULL;
		}
#endif
		if( sizeptr ) *sizeptr = file->size;

		FS_EnsureOpenZip( NULL );
		return decompressed_buffer;
	}
	else if( file->flags == ZIP_COMPRESSION_DEFLATED )
	{
		compressed_buffer = Mem_Malloc( fs_mempool, file->compressed_size + 1 );
		decompressed_buffer = Mem_Malloc( fs_mempool, file->size + 1 );
		decompressed_buffer[file->size] = '\0';

		c = read( search->zip->handle, compressed_buffer, file->compressed_size );
		if( c != file->compressed_size )
		{
			Con_Reportf( S_ERROR "Zip_LoadFile: %s compressed size doesn't match\n", file->name );
			return NULL;
		}

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
#if 0
			CRC32_Init( &test_crc );
			CRC32_ProcessBuffer( &test_crc, decompressed_buffer, file->size );

			final_crc = CRC32_Final( test_crc );

			if( final_crc != file->crc32 )
			{
				Con_Reportf( S_ERROR "Zip_LoadFile: %s file crc32 mismatch\n", file->name );
				Mem_Free( decompressed_buffer );
				return NULL;
			}
#endif
			if( sizeptr ) *sizeptr = file->size;

			FS_EnsureOpenZip( NULL );
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

	FS_EnsureOpenZip( NULL );
	return NULL;
}

/*
===========
FS_FileTime_ZIP

===========
*/
int FS_FileTime_ZIP( searchpath_t *search, const char *filename )
{
	return search->zip->filetime;
}

/*
===========
FS_PrintInfo_ZIP

===========
*/
void FS_PrintInfo_ZIP( searchpath_t *search, char *dst, size_t size )
{
	Q_snprintf( dst, size, "%s (%i files)", search->filename, search->zip->numfiles );
}

/*
===========
FS_FindFile_ZIP

===========
*/
int FS_FindFile_ZIP( searchpath_t *search, const char *path, char *fixedname, size_t len )
{
	int	left, right, middle;

	// look for the file (binary search)
	left = 0;
	right = search->zip->numfiles - 1;
	while( left <= right )
	{
		int	diff;

		middle = (left + right) / 2;
		diff = Q_stricmp( search->zip->files[middle].name, path );

		// Found it
		if( !diff )
		{
			if( fixedname )
				Q_strncpy( fixedname, search->zip->files[middle].name, len );
			return middle;
		}

		// if we're too far in the list
		if( diff > 0 )
			right = middle - 1;
		else left = middle + 1;
	}

	return -1;
}

/*
===========
FS_Search_ZIP

===========
*/
void FS_Search_ZIP( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive )
{
	string temp;
	const char *slash, *backslash, *colon, *separator;
	int j, i;

	for( i = 0; i < search->zip->numfiles; i++ )
	{
		Q_strncpy( temp, search->zip->files[i].name, sizeof( temp ));
		while( temp[0] )
		{
			if( matchpattern( temp, pattern, true ))
			{
				for( j = 0; j < list->numstrings; j++ )
				{
					if( !Q_strcmp( list->strings[j], temp ))
						break;
				}

				if( j == list->numstrings )
					stringlistappend( list, temp );
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

/*
===========
FS_AddZip_Fullpath

===========
*/
qboolean FS_AddZip_Fullpath( const char *zipfile, qboolean *already_loaded, int flags )
{
	searchpath_t	*search;
	zip_t		*zip = NULL;
	const char	*ext = COM_FileExtension( zipfile );
	int		errorcode = ZIP_LOAD_COULDNT_OPEN;

	for( search = fs_searchpaths; search; search = search->next )
	{
		if( search->type == SEARCHPATH_ZIP && !Q_stricmp( search->filename, zipfile ))
		{
			if( already_loaded ) *already_loaded = true;
			return true; // already loaded
		}
	}

	if( already_loaded ) *already_loaded = false;

	if( !Q_stricmp( ext, "pk3" ) )
		zip = FS_LoadZip( zipfile, &errorcode );

	if( zip )
	{
		string	fullpath;
		int i;

		search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ) );
		Q_strncpy( search->filename, zipfile, sizeof( search->filename ));
		search->zip = zip;
		search->type = SEARCHPATH_ZIP;
		search->next = fs_searchpaths;
		search->flags = flags;

		search->pfnPrintInfo = FS_PrintInfo_ZIP;
		search->pfnClose = FS_Close_ZIP;
		search->pfnOpenFile = FS_OpenFile_ZIP;
		search->pfnFileTime = FS_FileTime_ZIP;
		search->pfnFindFile = FS_FindFile_ZIP;
		search->pfnSearch = FS_Search_ZIP;

		fs_searchpaths = search;

		Con_Reportf( "Adding zipfile: %s (%i files)\n", zipfile, zip->numfiles );

		// time to add in search list all the wads that contains in current pakfile (if do)
		for( i = 0; i < zip->numfiles; i++ )
		{
			if( !Q_stricmp( COM_FileExtension( zip->files[i].name ), "wad" ))
			{
				Q_snprintf( fullpath, MAX_STRING, "%s/%s", zipfile, zip->files[i].name );
				FS_AddWad_Fullpath( fullpath, NULL, flags );
			}
		}
		return true;
	}
	else
	{
		if( errorcode != ZIP_LOAD_NO_FILES )
			Con_Reportf( S_ERROR "FS_AddZip_Fullpath: unable to load zip \"%s\"\n", zipfile );
		return false;
	}
}

