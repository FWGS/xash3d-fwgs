/*
pak.c - PAK support for filesystem
Copyright (C) 2007 Uncle Mike
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

#include "build.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#if XASH_POSIX
#include <unistd.h>
#endif
#include <errno.h>
#include <stddef.h>
#include "port.h"
#include "filesystem_internal.h"
#include "crtlib.h"
#include "common/com_strings.h"

/*
========================================================================
PAK FILES

The .pak files are just a linear collapse of a directory tree
========================================================================
*/
// header
#define IDPACKV1HEADER	(('K'<<24)+('C'<<16)+('A'<<8)+'P')	// little-endian "PACK"

#define MAX_FILES_IN_PACK	65536 // pak

typedef struct
{
	int		ident;
	int		dirofs;
	int		dirlen;
} dpackheader_t;

typedef struct
{
	char		name[56];		// total 64 bytes
	int		filepos;
	int		filelen;
} dpackfile_t;

// PAK errors
#define PAK_LOAD_OK			0
#define PAK_LOAD_COULDNT_OPEN		1
#define PAK_LOAD_BAD_HEADER		2
#define PAK_LOAD_BAD_FOLDERS		3
#define PAK_LOAD_TOO_MANY_FILES	4
#define PAK_LOAD_NO_FILES		5
#define PAK_LOAD_CORRUPTED		6

struct pack_s
{
	string		filename;
	int		handle;
	int		numfiles;
	time_t		filetime;			// common for all packed files
	dpackfile_t	*files;
};

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
=================
FS_LoadPackPAK

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *FS_LoadPackPAK( const char *packfile, int *error )
{
	dpackheader_t header;
	int         packhandle;
	int         i, numpackfiles;
	pack_t      *pack;
	dpackfile_t *info;
	fs_size_t     c;

	packhandle = open( packfile, O_RDONLY|O_BINARY );

#if !XASH_WIN32
	if( packhandle < 0 )
	{
		const char *fpackfile = FS_FixFileCase( packfile );
		if( fpackfile != packfile )
			packhandle = open( fpackfile, O_RDONLY|O_BINARY );
	}
#endif

	if( packhandle < 0 )
	{
		Con_Reportf( "%s couldn't open: %s\n", packfile, strerror( errno ));
		if( error ) *error = PAK_LOAD_COULDNT_OPEN;
		return NULL;
	}

	c = read( packhandle, (void *)&header, sizeof( header ));

	if( c != sizeof( header ) || header.ident != IDPACKV1HEADER )
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

#ifdef XASH_REDUCE_FD
	// will reopen when needed
	close( pack->handle );
	pack->handle = -1;
#endif

	if( error ) *error = PAK_LOAD_OK;
	Mem_Free( info );

	return pack;
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

	pfile = &pack->files[pack_ind];

	return FS_OpenHandle( pack->filename, pack->handle, pfile->filepos, pfile->filelen );
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
qboolean FS_AddPak_Fullpath( const char *pakfile, qboolean *already_loaded, int flags )
{
	searchpath_t	*search;
	pack_t		*pak = NULL;
	const char	*ext = COM_FileExtension( pakfile );
	int		i, errorcode = PAK_LOAD_COULDNT_OPEN;

	for( search = fs_searchpaths; search; search = search->next )
	{
		if( search->type == SEARCHPATH_PAK && !Q_stricmp( search->pack->filename, pakfile ))
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
		search->type = SEARCHPATH_PAK;
		search->next = fs_searchpaths;
		search->flags |= flags;
		fs_searchpaths = search;

		Con_Reportf( "Adding pakfile: %s (%i files)\n", pakfile, pak->numfiles );

		// time to add in search list all the wads that contains in current pakfile (if do)
		for( i = 0; i < pak->numfiles; i++ )
		{
			if( !Q_stricmp( COM_FileExtension( pak->files[i].name ), "wad" ))
			{
				Q_snprintf( fullpath, MAX_STRING, "%s/%s", pakfile, pak->files[i].name );
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

int FS_FindFilePAK( pack_t *pack, const char *name )
{
	int	left, right, middle;

	// look for the file (binary search)
	left = 0;
	right = pack->numfiles - 1;
	while( left <= right )
	{
		int	diff;

		middle = (left + right) / 2;
		diff = Q_stricmp( pack->files[middle].name, name );

		// Found it
		if( !diff )
		{
			return middle;
		}

		// if we're too far in the list
		if( diff > 0 )
			right = middle - 1;
		else left = middle + 1;
	}

	return -1;
}

void FS_SearchPAK( stringlist_t *list, pack_t *pack, const char *pattern )
{
	string temp;
	const char *slash, *backslash, *colon, *separator;
	int j, i;

	for( i = 0; i < pack->numfiles; i++ )
	{
		Q_strncpy( temp, pack->files[i].name, sizeof( temp ));
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

int FS_FileTimePAK( pack_t *pack )
{
	return pack->filetime;
}

void FS_PrintPAKInfo( char *dst, size_t size, pack_t *pack )
{
	Q_snprintf( dst, size, "%s (%i files)", pack->filename, pack->numfiles );
}

void FS_ClosePAK( pack_t *pack )
{
	if( pack->files )
		Mem_Free( pack->files );
	if( pack->handle >= 0 )
		close( pack->handle );
	Mem_Free( pack );
}
