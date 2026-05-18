/*
io.c - file I/O operations and content load/save for filesystem
Copyright (C) 2003-2006 Mathieu Olivier
Copyright (C) 2000-2007 DarkPlaces contributors
Copyright (C) 2007 Uncle Mike
Copyright (C) 2015-2023 Xash3D FWGS contributors

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
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#if XASH_WIN32
#include <io.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "port.h"
#include "crtlib.h"
#include "crclib.h"
#include "filesystem.h"
#include "filesystem_internal.h"
#include "common/com_strings.h"

#define FILE_COPY_SIZE		(1024 * 1024)

static void FS_Purge( file_t *file )
{
	file->buff_len = 0;
	file->buff_ind = 0;
	file->ungetc = EOF;
}

static void *FS_CustomAlloc( size_t size )
{
	return Mem_Malloc( fs_mempool, size );
}

static void FS_CustomFree( void *data )
{
	Mem_Free( data );
}

/*
===========
FS_OpenReadFile

Look for a file in the search paths and open it in read-only mode
===========
*/
file_t *FS_OpenReadFile( const char *filename, const char *mode, qboolean gamedironly )
{
	searchpath_t *search;
	char netpath[MAX_SYSPATH];
	int pack_ind;

	search = FS_FindFile( filename, &pack_ind, netpath, sizeof( netpath ), gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 );

	// not found?
	if( search == NULL )
		return NULL;

	return search->pfnOpenFile( search, netpath, mode, pack_ind );
}

/*
====================
FS_Open

Open a file. The syntax is the same as fopen
====================
*/
file_t *FS_Open( const char *filepath, const char *mode, qboolean gamedironly )
{
	if( !fs_searchpaths )
		return NULL;

	// some mappers used leading '/' or '\' in path to models or sounds
	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( filepath[0] == '/' || filepath[0] == '\\' )
		filepath++;

	if( FS_CheckNastyPath( filepath ))
		return NULL;

	// if the file is opened in "write", "append", or "read/write" mode
	if( mode[0] == 'w' || mode[0] == 'a'|| mode[0] == 'e' || Q_strchr( mode, '+' ))
	{
		char	real_path[MAX_SYSPATH];

		// open the file on disk directly
		if( !FS_FixFileCase( fs_writepath->dir, filepath, real_path, sizeof( real_path ), true ))
			return NULL;

		FS_CreatePath( real_path ); // Create directories up to the file

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

	FS_BackupFileName( file, NULL, 0 );

	if( file->handle >= 0 )
	{
		if( close( file->handle ))
			return EOF;
	}

	if( file->ztk )
	{
		inflateEnd( &file->ztk->zstream );
		Mem_Free( file->ztk );
	}

	Mem_Free( file );
	return 0;
}

/*
====================
FS_Flush

flushes written data to disk
====================
*/
int FS_Flush( file_t *file )
{
	if( !file ) return 0;

	// purge cached data
	FS_Purge( file );

	// sync
#if XASH_POSIX
	if( fsync( file->handle ) < 0 )
		return EOF;
#else
	if( _commit( file->handle ) < 0 )
		return EOF;
#endif

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
	result = write( file->handle, data, datasize );
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
	fs_offset_t	done;
	fs_offset_t	nb;
	fs_offset_t	count;

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
		count = ( buffersize > count ) ? count : (fs_offset_t)buffersize;

		done += count;
		memcpy( buffer, &file->buff[file->buff_ind], count );
		file->buff_ind += count;

		buffersize -= count;
		if( buffersize == 0 )
			return done;
	}

	// NOTE: at this point, the read buffer is always empty

	FS_EnsureOpenFile( file ); // FIXME: broken XASH_REDUCE_FD in case of compressed files!

	if( FBitSet( file->flags, FILE_DEFLATED ))
	{
		// If the file is compressed, it's more complicated...
		// We cycle through a few operations until we have read enough data
		while( buffersize > 0 )
		{
			ztoolkit_t *ztk = file->ztk;
			int error;

			// NOTE: at this point, the read buffer is always empty

			// If "input" is also empty, we need to refill it
			if( ztk->in_ind == ztk->in_len )
			{
				// If we are at the end of the file
				if( file->position == file->real_length )
					return done;

				count = (fs_offset_t)( ztk->comp_length - ztk->in_position );
				if( count > (fs_offset_t)sizeof( ztk->input ))
					count = (fs_offset_t)sizeof( ztk->input );
				lseek( file->handle, file->offset + (fs_offset_t)ztk->in_position, SEEK_SET );
				if( read( file->handle, ztk->input, count ) != count )
				{
					Con_Printf( "%s: unexpected end of file\n", __func__ );
					break;
				}

				ztk->in_ind = 0;
				ztk->in_len = count;
				ztk->in_position += count;
			}

			ztk->zstream.next_in = &ztk->input[ztk->in_ind];
			ztk->zstream.avail_in = (unsigned int)( ztk->in_len - ztk->in_ind );

			// Now that we are sure we have compressed data available, we need to determine
			// if it's better to inflate it in "file->buff" or directly in "buffer"

			// Inflate the data in "file->buff"
			if( buffersize < sizeof( file->buff ) / 2 )
			{
				ztk->zstream.next_out = file->buff;
				ztk->zstream.avail_out = sizeof( file->buff );
			}
			else
			{
				ztk->zstream.next_out = &((unsigned char*)buffer)[done];
				ztk->zstream.avail_out = (unsigned int)buffersize;
			}

			error = inflate( &ztk->zstream, Z_SYNC_FLUSH );
			if( error != Z_OK && error != Z_STREAM_END )
			{
				Con_Printf( "%s: Can't inflate file (%d)\n", __func__, error );
				break;
			}
			ztk->in_ind = ztk->in_len - ztk->zstream.avail_in;

			if( buffersize < sizeof( file->buff ) / 2 )
			{
				file->buff_len = (fs_offset_t)sizeof( file->buff ) - ztk->zstream.avail_out;
				file->position += file->buff_len;

				// Copy the requested data in "buffer" (as much as we can)
				count = (fs_offset_t)buffersize > file->buff_len ? file->buff_len : (fs_offset_t)buffersize;
				memcpy( &((unsigned char*)buffer)[done], file->buff, count );
				file->buff_ind = count;
			}
			else
			{
				count = (fs_offset_t)( buffersize - ztk->zstream.avail_out );
				file->position += count;

				// Purge cached data
				FS_Purge( file );
			}

			done += count;
			buffersize -= count;
		}

		return done;
	}

	// we must take care to not read after the end of the file
	count = file->real_length - file->position;

	// if we have a lot of data to get, put them directly into "buffer"
	if( buffersize > sizeof( file->buff ) / 2 )
	{
		if( count > (fs_offset_t)buffersize )
			count = (fs_offset_t)buffersize;
		lseek( file->handle, file->offset + file->position, SEEK_SET );
		nb = read( file->handle, &((byte *)buffer)[done], count );

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
		len = Q_vsnprintf( tempbuff, buff_size, format, ap );

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
int FS_UnGetc( file_t *file, char c )
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
int FS_Gets( file_t *file, char *string, size_t bufsize )
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
			FS_UnGetc( file, c );
	}

	return c;
}

/*
====================
FS_Seek

Move the position index in a file
NOTE: when porting code, check return value!
NOTE: it's not compatible with lseek!
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

	FS_EnsureOpenFile( file );
	// Purge cached data
	FS_Purge( file );

	if( FBitSet( file->flags, FILE_DEFLATED ))
	{
		// Seeking in compressed files is more a hack than anything else,
		// but we need to support it, so here we go.
		ztoolkit_t *ztk = file->ztk;
		unsigned char *buffer;
		fs_offset_t buffersize;

		// If we have to go back in the file, we need to restart from the beginning
		if( offset <= file->position )
		{
			ztk->in_ind = 0;
			ztk->in_len = 0;
			ztk->in_position = 0;
			file->position = 0;
			if( lseek( file->handle, file->offset, SEEK_SET ) == -1 )
				Con_Printf("IMPOSSIBLE: couldn't seek in already opened pk3 file.\n");

			// Reset the Zlib stream
			ztk->zstream.next_in = ztk->input;
			ztk->zstream.avail_in = 0;
			inflateReset( &ztk->zstream );
		}

		// We need a big buffer to force inflating into it directly
		buffersize = 2 * sizeof( file->buff );
		buffer = (unsigned char *)Mem_Malloc( fs_mempool, buffersize );

		// Skip all data until we reach the requested offset
		while( offset > ( file->position - file->buff_len + file->buff_ind ))
		{
			fs_offset_t diff = offset - ( file->position - file->buff_len + file->buff_ind );
			fs_offset_t count;

			count = ( diff > buffersize ) ? buffersize : diff;
			if( FS_Read( file, buffer, count ) != count )
			{
				Mem_Free( buffer );
				return -1;
			}
		}

		Mem_Free( buffer );
		return 0;
	}

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
fs_offset_t FS_Tell( const file_t *file )
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
qboolean FS_Eof( const file_t *file )
{
	if( !file ) return true;
	return (( file->position - file->buff_len + file->buff_ind ) == file->real_length ) ? true : false;
}

/*
============
FS_LoadFileFromArchive

============
*/
byte *FS_LoadFileFromArchive( searchpath_t *sp, const char *path, int pack_ind, fs_offset_t *filesizeptr, const qboolean sys_malloc )
{
	fs_offset_t	filesize;
	file_t *file;
	byte *buf;
	void *( *pfnAlloc )( size_t ) = sys_malloc ? malloc : FS_CustomAlloc;
	void ( *pfnFree )( void * ) = sys_malloc ? free : FS_CustomFree;

	// custom load file function for compressed files
	if( sp->pfnLoadFile )
		return sp->pfnLoadFile( sp, path, pack_ind, filesizeptr, pfnAlloc, pfnFree );

	file = sp->pfnOpenFile( sp, path, "rb", pack_ind );

	if( !file ) // TODO: indicate errors
		return NULL;

	filesize = file->real_length;
	buf = (byte *)pfnAlloc( filesize + 1 );

	if( unlikely( !buf )) // TODO: indicate errors
	{
		Con_Reportf( "%s: can't alloc %li bytes, no free memory\n", __func__, (long)filesize + 1 );
		FS_Close( file );
		return NULL;
	}

	buf[filesize] = '\0';
	FS_Read( file, buf, filesize );
	FS_Close( file );
	if( filesizeptr ) *filesizeptr = filesize;

	return buf;
}

/*
============
FS_LoadFile

Filename are relative to the xash directory.
Always appends a 0 byte.
============
*/
static byte *FS_LoadFile_( const char *path, fs_offset_t *filesizeptr, const qboolean gamedironly, const qboolean custom_alloc )
{
	searchpath_t *search;
	char netpath[MAX_SYSPATH];
	int pack_ind;

	// some mappers used leading '/' or '\' in path to models or sounds
	if( path[0] == '/' || path[0] == '\\' )
		path++;

	if( path[0] == '/' || path[0] == '\\' )
		path++;

	if( !fs_searchpaths || FS_CheckNastyPath( path ))
		return NULL;

	search = FS_FindFile( path, &pack_ind, netpath, sizeof( netpath ), gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 );

	if( !search )
		return NULL;

	return FS_LoadFileFromArchive( search, netpath, pack_ind, filesizeptr, !custom_alloc );
}

byte *FS_LoadFileMalloc( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	return FS_LoadFile_( path, filesizeptr, gamedironly, false );
}

byte *FS_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	return FS_LoadFile_( path, filesizeptr, gamedironly, true );
}

/*
============
CRC32_File

============
*/
qboolean CRC32_File( dword *crcvalue, const char *filename )
{
	char	buffer[1024];
	int	num_bytes;
	file_t	*f;

	f = FS_Open( filename, "rb", false );
	if( !f ) return false;

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

/*
============
MD5_HashFile

============
*/
qboolean MD5_HashFile( byte digest[16], const char *pszFileName, uint seed[4] )
{
	file_t		*file;
	MD5Context_t	MD5_Hash = { 0 };

	if(( file = FS_Open( pszFileName, "rb", false )) == NULL )
		return false;

	MD5Init( &MD5_Hash );

	if( seed )
		MD5Update( &MD5_Hash, (const byte *)seed, 16 );

	while( 1 )
	{
		byte buffer[1024];
		int bytes = FS_Read( file, buffer, sizeof( buffer ));

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
FS_LoadDirectFile

============
*/
byte *FS_LoadDirectFile( const char *path, fs_offset_t *filesizeptr )
{
	file_t		*file;
	byte		*buf = NULL;
	fs_offset_t	filesize = 0;

	file = FS_SysOpen( path, "rb" );

	if( !file )
		return NULL;

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
		Con_Reportf( S_ERROR "%s: failed on %s\n", __func__, filename );
		return false;
	}

	FS_Write( file, data, len );
	FS_Close( file );

	return true;
}

/*
==================
FS_FileExists

Look for a file in the packages and in the filesystem
==================
*/
int GAME_EXPORT FS_FileExists( const char *filename, int gamedironly )
{
	return FS_FindFile( filename, NULL, NULL, 0, gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 ) != NULL;
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
	static char diskpath[MAX_SYSPATH];

	if( FS_GetFullDiskPath( diskpath, sizeof( diskpath ), name, gamedironly ))
		return diskpath;

	return NULL;
}

/*
==================
FS_GetFullDiskPath

Build full path for file on disk
return false for file in pack
==================
*/
qboolean FS_GetFullDiskPath( char *buffer, size_t size, const char *name, qboolean gamedironly )
{
	searchpath_t *search;
	char temp[MAX_SYSPATH];

	search = FS_FindFile( name, NULL, temp, sizeof( temp ), gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 );

	if( search && search->type == SEARCHPATH_PLAIN )
	{
		Q_snprintf( buffer, size, "%s%s", search->filename, temp );
		return true;
	}

	return false;
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
fs_offset_t FS_FileLength( const file_t *f )
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
	searchpath_t *search;
	char netpath[MAX_SYSPATH];
	int pack_ind;

	search = FS_FindFile( filename, &pack_ind, netpath, sizeof( netpath ), gamedironly ? FS_GAMEDIRONLY_SEARCH_FLAGS : 0 );
	if( !search )
		return -1; // doesn't exist

	return search->pfnFileTime( search, netpath );
}

/*
==================
FS_Rename

rename specified file from gamefolder
==================
*/
qboolean FS_Rename( const char *oldname, const char *newname )
{
	char oldname2[MAX_SYSPATH], newname2[MAX_SYSPATH], oldpath[MAX_SYSPATH], newpath[MAX_SYSPATH];
	int ret;

	// a1ba: disallow path traversal
	if( FS_CheckNastyPath( oldname ) || FS_CheckNastyPath( newname ))
		return false;

	// fix up slashes
	Q_strncpy( oldname2, oldname, sizeof( oldname2 ));
	Q_strncpy( newname2, newname, sizeof( newname2 ));

	COM_FixSlashes( oldname2 );
	COM_FixSlashes( newname2 );

	// no work done
	if( !Q_stricmp( oldname2, newname2 ))
		return true;

	// no writing directory is set, no changes should be made
	if( !fs_writepath )
		return false;

	// file does not exist
	if( !FS_FixFileCase( fs_writepath->dir, oldname2, oldpath, sizeof( oldpath ), false ))
		return false;

	// exit if overflowed
	if( !FS_FixFileCase( fs_writepath->dir, newname2, newpath, sizeof( newpath ), true ))
		return false;

	ret = rename( oldpath, newpath );
	if( ret < 0 )
	{
		Con_Printf( "%s: failed to rename file %s (%s) to %s (%s): %s\n",
			__func__, oldpath, oldname2, newpath, newname2, strerror( errno ));
		return false;
	}

	return true;
}

/*
==================
FS_Delete

delete specified file from gamefolder
==================
*/
qboolean GAME_EXPORT FS_Delete( const char *path )
{
	char path2[MAX_SYSPATH], real_path[MAX_SYSPATH];
	int ret;

	// a1ba: disallow path traversal
	if( FS_CheckNastyPath( path ))
		return false;

	if( !fs_writepath )
		return false;

	Q_strncpy( path2, path, sizeof( path2 ));
	COM_FixSlashes( path2 );

	if( !FS_FixFileCase( fs_writepath->dir, path2, real_path, sizeof( real_path ), true ))
		return true;

	ret = remove( real_path );
	if( ret < 0 && errno != ENOENT )
	{
		Con_Printf( "%s: failed to delete file %s (%s): %s\n", __func__, real_path, path, strerror( errno ));
		return false;
	}

	return true;
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
			Con_Reportf( S_ERROR "%s: unexpected end of input file (%d < %d)\n", __func__, readSize, size );
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
