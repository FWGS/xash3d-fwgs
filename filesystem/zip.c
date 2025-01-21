/*
zip.c - ZIP support for filesystem
Copyright (C) 2019 Mr0maks
Copyright (C) 2019-2023 Xash3D FWGS contributors

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
	file_t *handle;
	int		numfiles;
	zipfile_t files[]; // flexible
};

// #define ENABLE_CRC_CHECK // known to be buggy because of possible libpublic crc32 bug, disabled

/*
============
FS_CloseZIP
============
*/
static void FS_CloseZIP( zip_t *zip )
{
	if( zip->handle != NULL )
		FS_Close( zip->handle );

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
	return Q_stricmp(((zipfile_t *)a )->name, ((zipfile_t *)b )->name );
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
	fs_offset_t	  filepos = 0;
	zipfile_t	  *info = NULL;
	char		  filename_buffer[MAX_SYSPATH];
	zip_t         *zip = (zip_t *)Mem_Calloc( fs_mempool, sizeof( *zip ));
	fs_size_t       c;

	// TODO: use FS_Open to allow PK3 to be included into other archives
	// Currently, it doesn't work with rodir due to FS_FindFile logic
	zip->handle = FS_SysOpen( zipfile, "rb" );

	if( zip->handle == NULL )
	{
		Con_Reportf( S_ERROR "%s couldn't open\n", zipfile );

		if( error )
			*error = ZIP_LOAD_COULDNT_OPEN;

		FS_CloseZIP( zip );
		return NULL;
	}

	if( zip->handle->real_length > UINT32_MAX )
	{
		Con_Reportf( S_ERROR "%s bigger than 4GB.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_COULDNT_OPEN;

		FS_CloseZIP( zip );
		return NULL;
	}

	FS_Seek( zip->handle, 0, SEEK_SET );

	c = FS_Read( zip->handle, &signature, sizeof( signature ));

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
	filepos = zip->handle->real_length;

	while( filepos > 0 )
	{
		FS_Seek( zip->handle, filepos, SEEK_SET );
		c = FS_Read( zip->handle, &signature, sizeof( signature ));

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

	c = FS_Read( zip->handle, &header_eocd, sizeof( header_eocd ));

	if( c != sizeof( header_eocd ))
	{
		Con_Reportf( S_ERROR "invalid EOCD header in %s. Zip file corrupted.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_BAD_HEADER;

		FS_CloseZIP( zip );
		return NULL;
	}

	if( header_eocd.total_central_directory_record == 0 ) // refuse to load empty ZIP archives
	{
		Con_Reportf( S_WARN "%s has no files (total records is zero). Ignored.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_NO_FILES;

		FS_CloseZIP( zip );
		return NULL;
	}

	// Move to CDF start
	FS_Seek( zip->handle, header_eocd.central_directory_offset, SEEK_SET );

	// Calc count of files in archive
	zip = (zip_t *)Mem_Realloc( fs_mempool, zip, sizeof( *zip ) + sizeof( *info ) * header_eocd.total_central_directory_record );
	info = zip->files;

	for( i = 0; i < header_eocd.total_central_directory_record; i++ )
	{
		c = FS_Read( zip->handle, &header_cdf, sizeof( header_cdf ));

		if( c != sizeof( header_cdf ) || header_cdf.signature != ZIP_HEADER_CDF )
		{
			Con_Reportf( S_ERROR "CDF signature mismatch in %s. Zip file corrupted.\n", zipfile );

			if( error )
				*error = ZIP_LOAD_BAD_HEADER;

			FS_CloseZIP( zip );
			return NULL;
		}

		if( header_cdf.uncompressed_size && header_cdf.filename_len && header_cdf.filename_len < sizeof( filename_buffer ))
		{
			memset( &filename_buffer, '\0', sizeof( filename_buffer ));
			c = FS_Read( zip->handle, &filename_buffer, header_cdf.filename_len );

			if( c != header_cdf.filename_len )
			{
				Con_Reportf( S_ERROR "filename length mismatch in %s. Zip file corrupted.\n", zipfile );

				if( error )
					*error = ZIP_LOAD_CORRUPTED;

				FS_CloseZIP( zip );
				return NULL;
			}

			Q_strncpy( info[numpackfiles].name, filename_buffer, sizeof( info[numpackfiles].name ));
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

	// refuse to load empty files again
	if( numpackfiles == 0 )
	{
		Con_Reportf( S_WARN "%s has no files (recalculated). Ignored.\n", zipfile );

		if( error )
			*error = ZIP_LOAD_NO_FILES;

		FS_CloseZIP( zip );
		return NULL;
	}

	// recalculate offsets
	for( i = 0; i < numpackfiles; i++ )
	{
		zip_header_t header;

		FS_Seek( zip->handle, info[i].offset, SEEK_SET );
		c = FS_Read( zip->handle, &header, sizeof( header ) );

		if( c != sizeof( header ))
		{
			Con_Reportf( S_ERROR "header length mismatch in %s. Zip file corrupted.\n", zipfile );

			if( error )
				*error = ZIP_LOAD_CORRUPTED;

			FS_CloseZIP( zip );
			return NULL;
		}

		info[i].flags = header.compression_flags;
		info[i].offset = info[i].offset + header.filename_len + header.extrafield_len + sizeof( header );
	}

	zip->numfiles = numpackfiles;
	qsort( zip->files, zip->numfiles, sizeof( *zip->files ), FS_SortZip );

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
static file_t *FS_OpenFile_ZIP( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	zipfile_t *pfile = &search->zip->files[pack_ind];
	file_t *f = FS_OpenHandle( search, search->zip->handle->handle, pfile->offset, pfile->size );

	if( !f )
		return NULL;

	if( pfile->flags == ZIP_COMPRESSION_DEFLATED )
	{
		ztoolkit_t *ztk;

		SetBits( f->flags, FILE_DEFLATED );

		ztk = Mem_Calloc( fs_mempool, sizeof( *ztk ));
		ztk->comp_length = pfile->compressed_size;
		ztk->zstream.next_in = ztk->input;
		ztk->zstream.avail_in = 0;

		if( inflateInit2( &ztk->zstream, -MAX_WBITS ) != Z_OK )
		{
			Con_Printf( "%s: inflate init error (file: %s)\n", __func__, filename );
			FS_Close( f );
			Mem_Free( ztk );
			return NULL;
		}

		ztk->zstream.next_out = f->buff;
		ztk->zstream.avail_out = sizeof( f->buff );

		f->ztk = ztk;
	}
	else if( pfile->flags != ZIP_COMPRESSION_NO_COMPRESSION )
	{
		Con_Reportf( S_ERROR "%s: %s: file compressed with unknown algorithm.\n", __func__, filename );
		FS_Close( f );
		return NULL;
	}

	return f;
}

/*
===========
FS_LoadZIPFile

===========
*/
static byte *FS_LoadZIPFile( searchpath_t *search, const char *path, int pack_ind, fs_offset_t *sizeptr, void *( *pfnAlloc )( size_t ), void ( *pfnFree )( void * ))
{
	zipfile_t *file;
	byte		*compressed_buffer = NULL, *decompressed_buffer = NULL;
	int		zlib_result = 0;
	z_stream	decompress_stream;
	size_t      c;
#ifdef ENABLE_CRC_CHECK
	dword		test_crc, final_crc;
#endif // ENABLE_CRC_CHECK

	if( sizeptr ) *sizeptr = 0;

	file = &search->zip->files[pack_ind];

	if( FS_Seek( search->zip->handle, file->offset, SEEK_SET ) == -1 )
		return NULL;

	/*if( FS_Read( search->zip->handle, &header, sizeof( header )) < 0 )
		return NULL;

	if( header.signature != ZIP_HEADER_LF )
	{
		Con_Reportf( S_ERROR "%s: %s signature error\n", __func__, file->name );
		return NULL;
	}*/

	decompressed_buffer = (byte *)pfnAlloc( file->size + 1 );
	if( unlikely( !decompressed_buffer ))
	{
		Con_Reportf( S_ERROR "%s: can't alloc %li bytes, no free memory\n", __func__, (long)file->size + 1 );
		return NULL;
	}
	decompressed_buffer[file->size] = '\0';

	if( file->flags == ZIP_COMPRESSION_NO_COMPRESSION )
	{
		c = FS_Read( search->zip->handle, decompressed_buffer, file->size );
		if( c != file->size )
		{
			Con_Reportf( S_ERROR "%s: %s size doesn't match\n", __func__, file->name );
			return NULL;
		}

#ifdef ENABLE_CRC_CHECK
		CRC32_Init( &test_crc );
		CRC32_ProcessBuffer( &test_crc, decompressed_buffer, file->size );

		final_crc = CRC32_Final( test_crc );

		if( final_crc != file->crc32 )
		{
			Con_Reportf( S_ERROR "%s: %s file crc32 mismatch\n", __func__, file->name );
			pfnFree( decompressed_buffer );
			return NULL;
		}
#endif // ENABLE_CRC_CHECK

		if( sizeptr ) *sizeptr = file->size;

		return decompressed_buffer;
	}
	else if( file->flags == ZIP_COMPRESSION_DEFLATED )
	{
		compressed_buffer = (byte *)Mem_Malloc( fs_mempool, file->compressed_size + 1 );

		c = FS_Read( search->zip->handle, compressed_buffer, file->compressed_size );
		if( c != file->compressed_size )
		{
			Con_Reportf( S_ERROR "%s: %s compressed size doesn't match\n", __func__, file->name );
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
			Con_Printf( S_ERROR "%s: inflateInit2 failed\n", __func__ );
			Mem_Free( compressed_buffer );
			Mem_Free( decompressed_buffer );
			return NULL;
		}

		zlib_result = inflate( &decompress_stream, Z_NO_FLUSH );
		inflateEnd( &decompress_stream );

		if( zlib_result == Z_OK || zlib_result == Z_STREAM_END )
		{
			Mem_Free( compressed_buffer ); // finaly free compressed buffer
#if ENABLE_CRC_CHECK
			CRC32_Init( &test_crc );
			CRC32_ProcessBuffer( &test_crc, decompressed_buffer, file->size );

			final_crc = CRC32_Final( test_crc );

			if( final_crc != file->crc32 )
			{
				Con_Reportf( S_ERROR "%s: %s file crc32 mismatch\n", __func__, file->name );
				pfnFree( decompressed_buffer );
				return NULL;
			}
#endif
			if( sizeptr ) *sizeptr = file->size;

			return decompressed_buffer;
		}
		else
		{
			Con_Reportf( S_ERROR "%s: %s: error while file decompressing. Zlib return code %d.\n", __func__, file->name, zlib_result );
			Mem_Free( compressed_buffer );
			pfnFree( decompressed_buffer );
			return NULL;
		}

	}
	else
	{
		Con_Reportf( S_ERROR "%s: %s: file compressed with unknown algorithm.\n", __func__, file->name );
		pfnFree( decompressed_buffer );
		return NULL;
	}

	return NULL;
}

/*
===========
FS_FileTime_ZIP

===========
*/
static int FS_FileTime_ZIP( searchpath_t *search, const char *filename )
{
	return search->zip->handle->filetime;
}

/*
===========
FS_PrintInfo_ZIP

===========
*/
static void FS_PrintInfo_ZIP( searchpath_t *search, char *dst, size_t size )
{
	if( search->zip->handle->searchpath )
		Q_snprintf( dst, size, "%s (%i files)" S_CYAN " from %s" S_DEFAULT, search->filename, search->zip->numfiles, search->zip->handle->searchpath->filename );
	else Q_snprintf( dst, size, "%s (%i files)", search->filename, search->zip->numfiles );
}

/*
===========
FS_FindFile_ZIP

===========
*/
static int FS_FindFile_ZIP( searchpath_t *search, const char *path, char *fixedname, size_t len )
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
static void FS_Search_ZIP( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive )
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
searchpath_t *FS_AddZip_Fullpath( const char *zipfile, int flags )
{
	searchpath_t *search;
	zip_t *zip;
	int errorcode = ZIP_LOAD_COULDNT_OPEN;

	zip = FS_LoadZip( zipfile, &errorcode );

	if( !zip )
	{
		if( errorcode != ZIP_LOAD_NO_FILES )
			Con_Reportf( S_ERROR "%s: unable to load zip \"%s\"\n", __func__, zipfile );
		return NULL;
	}

	search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ) );
	Q_strncpy( search->filename, zipfile, sizeof( search->filename ));
	search->zip = zip;
	search->type = SEARCHPATH_ZIP;
	search->flags = flags;

	search->pfnPrintInfo = FS_PrintInfo_ZIP;
	search->pfnClose = FS_Close_ZIP;
	search->pfnOpenFile = FS_OpenFile_ZIP;
	search->pfnFileTime = FS_FileTime_ZIP;
	search->pfnFindFile = FS_FindFile_ZIP;
	search->pfnSearch = FS_Search_ZIP;
	search->pfnLoadFile = FS_LoadZIPFile;

	Con_Reportf( "Adding ZIP: %s (%i files)\n", zipfile, zip->numfiles );
	return search;
}

