/*
pak.c - PAK support for filesystem
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
	file_t *handle;
	int		numfiles;
	dpackfile_t files[]; // flexible
};

/*
====================
FS_SortPak

====================
*/
static int FS_SortPak( const void *_a, const void *_b )
{
	const dpackfile_t *a = _a, *b = _b;

	return Q_stricmp( a->name, b->name );
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
	file_t *packhandle;
	int         numpackfiles;
	pack_t      *pack;
	fs_size_t     c;

	// TODO: use FS_Open to allow PK3 to be included into other archives
	// Currently, it doesn't work with rodir due to FS_FindFile logic
	// when it will use FS_Open, check that FS_CheckForQuakePak correctly
	// detects Quake gamedirs in RoDir
	packhandle = FS_SysOpen( packfile, "rb" );

	if( packhandle == NULL )
	{
		Con_Reportf( "%s couldn't open: %s\n", packfile, strerror( errno ));
		if( error ) *error = PAK_LOAD_COULDNT_OPEN;
		return NULL;
	}

	c = FS_Read( packhandle, (void *)&header, sizeof( header ));

	if( c != sizeof( header ) || header.ident != IDPACKV1HEADER )
	{
		Con_Reportf( "%s is not a packfile. Ignored.\n", packfile );
		if( error ) *error = PAK_LOAD_BAD_HEADER;
		FS_Close( packhandle );
		return NULL;
	}

	if( header.dirlen % sizeof( dpackfile_t ))
	{
		Con_Reportf( S_ERROR "%s has an invalid directory size. Ignored.\n", packfile );
		if( error ) *error = PAK_LOAD_BAD_FOLDERS;
		FS_Close( packhandle );
		return NULL;
	}

	numpackfiles = header.dirlen / sizeof( dpackfile_t );

	if( numpackfiles > MAX_FILES_IN_PACK )
	{
		Con_Reportf( S_ERROR "%s has too many files ( %i ). Ignored.\n", packfile, numpackfiles );
		if( error ) *error = PAK_LOAD_TOO_MANY_FILES;
		FS_Close( packhandle );
		return NULL;
	}

	if( numpackfiles <= 0 )
	{
		Con_Reportf( "%s has no files. Ignored.\n", packfile );
		if( error ) *error = PAK_LOAD_NO_FILES;
		FS_Close( packhandle );
		return NULL;
	}

	pack = (pack_t *)Mem_Calloc( fs_mempool, sizeof( pack_t ) + sizeof( dpackfile_t ) * numpackfiles );
	FS_Seek( packhandle, header.dirofs, SEEK_SET );

	if( header.dirlen != FS_Read( packhandle, (void *)pack->files, header.dirlen ))
	{
		Con_Reportf( "%s is an incomplete PAK, not loading\n", packfile );
		if( error )
			*error = PAK_LOAD_CORRUPTED;
		FS_Close( packhandle );
		Mem_Free( pack );
		return NULL;
	}

	// TODO: validate directory?

	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	qsort( pack->files, pack->numfiles, sizeof( pack->files[0] ), FS_SortPak );

#ifdef XASH_REDUCE_FD
	// will reopen when needed
	close( pack->handle );
	pack->handle = -1;
#endif

	if( error )
		*error = PAK_LOAD_OK;

	return pack;
}

/*
===========
FS_OpenPackedFile

Open a packed file using its package file descriptor
===========
*/
static file_t *FS_OpenFile_PAK( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	dpackfile_t	*pfile;

	pfile = &search->pack->files[pack_ind];

	return FS_OpenHandle( search, search->pack->handle->handle, pfile->filepos, pfile->filelen );
}

/*
===========
FS_FindFile_PAK

===========
*/
static int FS_FindFile_PAK( searchpath_t *search, const char *path, char *fixedname, size_t len )
{
	int	left, right, middle;

	// look for the file (binary search)
	left = 0;
	right = search->pack->numfiles - 1;
	while( left <= right )
	{
		int	diff;

		middle = (left + right) / 2;
		diff = Q_stricmp( search->pack->files[middle].name, path );

		// Found it
		if( !diff )
		{
			if( fixedname )
				Q_strncpy( fixedname, search->pack->files[middle].name, len );
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
FS_Search_PAK

===========
*/
static void FS_Search_PAK( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive )
{
	string temp;
	const char *slash, *backslash, *colon, *separator;
	int j, i;

	for( i = 0; i < search->pack->numfiles; i++ )
	{
		Q_strncpy( temp, search->pack->files[i].name, sizeof( temp ));
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
FS_FileTime_PAK

===========
*/
static int FS_FileTime_PAK( searchpath_t *search, const char *filename )
{
	return search->pack->handle->filetime;
}

/*
===========
FS_PrintInfo_PAK

===========
*/
static void FS_PrintInfo_PAK( searchpath_t *search, char *dst, size_t size )
{
	if( search->pack->handle->searchpath )
		Q_snprintf( dst, size, "%s (%i files)" S_CYAN " from %s" S_DEFAULT, search->filename, search->pack->numfiles, search->pack->handle->searchpath->filename );
	else Q_snprintf( dst, size, "%s (%i files)", search->filename, search->pack->numfiles );
}

/*
===========
FS_Close_PAK

===========
*/
static void FS_Close_PAK( searchpath_t *search )
{
	if( search->pack->handle != NULL )
		FS_Close( search->pack->handle );
	Mem_Free( search->pack );
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
searchpath_t *FS_AddPak_Fullpath( const char *pakfile, int flags )
{
	searchpath_t *search;
	pack_t *pak;
	int errorcode = PAK_LOAD_COULDNT_OPEN;

	pak = FS_LoadPackPAK( pakfile, &errorcode );

	if( !pak )
	{
		if( errorcode != PAK_LOAD_NO_FILES )
			Con_Reportf( S_ERROR "%s: unable to load pak \"%s\"\n", __func__, pakfile );
		return NULL;
	}

	search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ));
	Q_strncpy( search->filename, pakfile, sizeof( search->filename ));
	search->pack = pak;
	search->type = SEARCHPATH_PAK;
	search->flags = flags;

	search->pfnPrintInfo = FS_PrintInfo_PAK;
	search->pfnClose = FS_Close_PAK;
	search->pfnOpenFile = FS_OpenFile_PAK;
	search->pfnFileTime = FS_FileTime_PAK;
	search->pfnFindFile = FS_FindFile_PAK;
	search->pfnSearch = FS_Search_PAK;

	Con_Reportf( "Adding PAK: %s (%i files)\n", pakfile, pak->numfiles );

	return search;
}

/*
================
FS_CheckForQuakePak

To generate fake gameinfo for Quake directory, we need to parse pak0.pak
and find progs.dat in it
================
*/
qboolean FS_CheckForQuakePak( const char *pakfile, const char *files[], size_t num_files )
{
	qboolean is_quake = false;
	pack_t *pak;
	int i;

	pak = FS_LoadPackPAK( pakfile, NULL );
	if( !pak )
		return false;

	for( i = 0; i < num_files; i++ )
	{
		int j;

		for( j = 0; j < pak->numfiles; j++ )
		{
			if( Q_strchr( pak->files[j].name, '/' ))
				continue; // exclude subdirectories

			if( !Q_stricmp( pak->files[j].name, files[i] ))
			{
				is_quake = true;
				break;
			}
		}

		if( is_quake )
			break;
	}

	Mem_Free( pak );
	return is_quake;
}
