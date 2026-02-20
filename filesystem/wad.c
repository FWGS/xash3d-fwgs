/*
wad.c - WAD support for filesystem
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
#include "wadfile.h"

/*
========================================================================
.WAD archive format	(WhereAllData - WAD)

List of compressed files, that can be identify only by TYPE_*

<format>
header:	dwadinfo_t[dwadinfo_t]
file_1:	byte[dwadinfo_t[num]->disksize]
file_2:	byte[dwadinfo_t[num]->disksize]
file_3:	byte[dwadinfo_t[num]->disksize]
...
file_n:	byte[dwadinfo_t[num]->disksize]
infotable	dlumpinfo_t[dwadinfo_t->numlumps]
========================================================================
*/
#define HINT_NAMELEN	5	// e.g. _mask, _norm
#define MAX_FILES_IN_WAD	65535	// real limit as above <2Gb size not a lumpcount

#include "const.h"

struct wfile_s
{
	int		infotableofs;
	int		numlumps;
	poolhandle_t mempool;			// W_ReadLump temp buffers
	file_t		*handle;
	dlumpinfo_t	*lumps;
	time_t		filetime;
};

// WAD errors
#define WAD_LOAD_OK			0
#define WAD_LOAD_COULDNT_OPEN		1
#define WAD_LOAD_BAD_HEADER		2
#define WAD_LOAD_BAD_FOLDERS		3
#define WAD_LOAD_TOO_MANY_FILES	4
#define WAD_LOAD_NO_FILES		5
#define WAD_LOAD_CORRUPTED		6

typedef struct wadtype_s
{
	char        ext[4];
	signed char type;
} wadtype_t;

// associate extension with wad type
static const wadtype_t wad_types[] =
{
{ "pal", TYP_PALETTE }, // palette
{ "dds", TYP_DDSTEX  }, // DDS image
{ "lmp", TYP_GFXPIC  }, // quake1, hl pic
{ "fnt", TYP_QFONT   }, // hl qfonts
{ "mip", TYP_MIPTEX  }, // hl/q1 mip
{ "txt", TYP_SCRIPT  }, // scripts
};

/*
===========
W_TypeFromExt

Extracts file type from extension
===========
*/
static signed char W_TypeFromExt( const char *lumpname )
{
	const char *ext = COM_FileExtension( lumpname );
	int i;

	// we not known about filetype, so match only by filename
	if( !Q_strcmp( ext, "*" ) || COM_StringEmpty( ext ))
		return TYP_ANY;

	for( i = 0; i < sizeof( wad_types ) / sizeof( wad_types[0] ); i++ )
	{
		if( !Q_stricmp( ext, wad_types[i].ext ))
			return wad_types[i].type;
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
	int i;

	// we not known aboyt filetype, so match only by filename
	if( lumptype == TYP_NONE || lumptype == TYP_ANY )
		return "";

	for( i = 0; i < sizeof( wad_types ) / sizeof( wad_types[0] ); i++ )
	{
		if( lumptype == wad_types[i].type )
			return wad_types[i].ext;
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
static dlumpinfo_t *W_AddFileToWad( const char *wadfile, const char *name, wfile_t *wad, dlumpinfo_t *newlump )
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
			else Con_Reportf( S_WARN "Wad %s contains the file %s several times\n", wadfile, name );
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
FS_CloseWAD

finalize wad or just close
===========
*/
static void FS_CloseWAD( wfile_t *wad )
{
	Mem_FreePool( &wad->mempool );
	if( wad->handle != NULL )
		FS_Close( wad->handle );
	Mem_Free( wad ); // free himself
}

/*
===========
FS_Close_WAD
===========
*/
static void FS_Close_WAD( searchpath_t *search )
{
	FS_CloseWAD( search->wad );
}

/*
===========
FS_OpenFile_WAD
===========
*/
static file_t *FS_OpenFile_WAD( searchpath_t *search, const char *filename, const char *mode, int pack_ind )
{
	return NULL;
}

/*
===========
W_Open

open the wad for reading & writing
===========
*/
static wfile_t *W_Open( const char *filename, int *error, uint flags )
{
	wfile_t		*wad = (wfile_t *)Mem_Calloc( fs_mempool, sizeof( wfile_t ));
	int		i, lumpcount;
	dlumpinfo_t	*srclumps;
	size_t		lat_size;
	dwadinfo_t	header;

	if( FBitSet( flags, FS_LOAD_PACKED_WAD ))
	{
		const char *basename = COM_FileWithoutPath( filename );
		wad->handle = FS_Open( basename, "rb", false );
	}
	else
	{
		wad->handle = FS_SysOpen( filename, "rb" );
	}

	if( wad->handle == NULL )
	{
		Con_Reportf( S_ERROR "%s: couldn't open %s: %s\n", __func__, filename, strerror( errno ));
		if( error ) *error = WAD_LOAD_COULDNT_OPEN;
		FS_CloseWAD( wad );
		return NULL;
	}

	// copy wad name
	wad->filetime = FS_SysFileTime( filename );
	wad->mempool = Mem_AllocPool( filename );

	if( FS_Read( wad->handle, &header, sizeof( dwadinfo_t )) != sizeof( dwadinfo_t ))
	{
		Con_Reportf( S_ERROR "%s: %s can't read header\n", __func__, filename );
		if( error ) *error = WAD_LOAD_BAD_HEADER;
		FS_CloseWAD( wad );
		return NULL;
	}

	if( header.ident != LittleLong( IDWAD2HEADER ) && header.ident != LittleLong( IDWAD3HEADER ))
	{
		Con_Reportf( S_ERROR "%s: %s is not a WAD2 or WAD3 file\n", __func__, filename );
		if( error ) *error = WAD_LOAD_BAD_HEADER;
		FS_CloseWAD( wad );
		return NULL;
	}

	header.ident = LittleLong( header.ident );
	header.numlumps = LittleLong( header.numlumps );
	header.infotableofs = LittleLong( header.infotableofs );

	lumpcount = header.numlumps;

	if( lumpcount >= MAX_FILES_IN_WAD )
	{
		Con_Reportf( S_WARN "%s: %s is full (%i lumps)\n", __func__, filename, lumpcount );
		if( error ) *error = WAD_LOAD_TOO_MANY_FILES;
	}
	else if( lumpcount <= 0 )
	{
		Con_Reportf( S_ERROR "%s: %s has no lumps\n", __func__, filename );
		if( error ) *error = WAD_LOAD_NO_FILES;
		FS_CloseWAD( wad );
		return NULL;
	}
	else if( error ) *error = WAD_LOAD_OK;

	wad->infotableofs = header.infotableofs; // save infotableofs position

	if( FS_Seek( wad->handle, wad->infotableofs, SEEK_SET ) == -1 )
	{
		Con_Reportf( S_ERROR "%s: %s can't find lump allocation table\n", __func__, filename );
		if( error ) *error = WAD_LOAD_BAD_FOLDERS;
		FS_CloseWAD( wad );
		return NULL;
	}

	lat_size = lumpcount * sizeof( dlumpinfo_t );

	// NOTE: lumps table can be reallocated for O_APPEND mode
	srclumps = (dlumpinfo_t *)Mem_Malloc( wad->mempool, lat_size );

	if( FS_Read( wad->handle, srclumps, lat_size ) != lat_size )
	{
		Con_Reportf( S_ERROR "%s: %s has corrupted lump allocation table\n", __func__, filename );
		if( error ) *error = WAD_LOAD_CORRUPTED;
		Mem_Free( srclumps );
		FS_CloseWAD( wad );
		return NULL;
	}

	for( i = 0; i < lumpcount; i++ )
	{
		srclumps[i].filepos = LittleLong( srclumps[i].filepos );
		srclumps[i].disksize = LittleLong( srclumps[i].disksize );
		srclumps[i].size = LittleLong( srclumps[i].size );
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

		W_AddFileToWad( filename, name, wad, &srclumps[i] );
	}

	// release source lumps
	Mem_Free( srclumps );

	// and leave the file open
	return wad;
}

/*
===========
FS_FileTime_WAD

===========
*/
static int FS_FileTime_WAD( searchpath_t *search, const char *filename )
{
	return search->wad->filetime;
}

/*
===========
FS_PrintInfo_WAD

===========
*/
static void FS_PrintInfo_WAD( searchpath_t *search, char *dst, size_t size )
{
	if( search->wad->handle->searchpath )
		Q_snprintf( dst, size, "%s (%i files)" S_CYAN " from %s" S_DEFAULT, search->filename, search->wad->numlumps, search->wad->handle->searchpath->filename );
	else Q_snprintf( dst, size, "%s (%i files)", search->filename, search->wad->numlumps );
}

/*
===========
FS_FindFile_WAD

===========
*/
static int FS_FindFile_WAD( searchpath_t *search, const char *path, char *fixedname, size_t len )
{
	dlumpinfo_t	*lump;
	signed char		type = W_TypeFromExt( path );
	qboolean		anywadname = true;
	string		wadname;
	string		shortname;

	// quick reject by filetype
	if( type == TYP_NONE )
		return -1;

	COM_ExtractFilePath( path, wadname );

	if( !COM_StringEmpty( wadname ))
	{
		string wadbasename;

		COM_FileBase( wadname, wadbasename, sizeof( wadbasename ));
		Q_snprintf( wadname, sizeof( wadname ), "%s.wad", wadbasename );
		anywadname = false;
	}

	// make wadname from wad fullpath
	COM_FileBase( search->filename, shortname, sizeof( shortname ));
	COM_DefaultExtension( shortname, ".wad", sizeof( shortname ));

	// quick reject by wadname
	if( !anywadname && Q_stricmp( wadname, shortname ))
		return -1;

	// NOTE: we can't using long names for wad,
	// because we using original wad names[16];
	COM_FileBase( path, shortname, sizeof( shortname ));

	lump = W_FindLump( search->wad, shortname, type );

	if( lump )
	{
		if( fixedname )
			Q_strncpy( fixedname, lump->name, len );
		return lump - search->wad->lumps;
	}

	return -1;
}

/*
===========
FS_Search_WAD

===========
*/
static void FS_Search_WAD( searchpath_t *search, stringlist_t *list, const char *pattern, int caseinsensitive )
{
	string	wadpattern, wadname, temp2;
	signed char	type = W_TypeFromExt( pattern );
	qboolean	anywadname = true;
	string	wadfolder, temp;
	int j, i;
	const char *slash, *backslash, *colon, *separator;
	char buf[MAX_VA_STRING];

	// quick reject by filetype
	if( type == TYP_NONE )
		return;

	COM_ExtractFilePath( pattern, wadname );
	COM_FileBase( pattern, wadpattern, sizeof( wadpattern ));
	wadfolder[0] = '\0';

	if( !COM_StringEmpty( wadname ))
	{
		string wadbasename;

		COM_FileBase( wadname, wadbasename, sizeof( wadbasename ));

		Q_strncpy( wadfolder, wadbasename, sizeof( wadfolder ));
		Q_snprintf( wadname, sizeof( wadname ), "%s.wad", wadbasename );
		anywadname = false;
	}

	// make wadname from wad fullpath
	COM_FileBase( search->filename, temp2, sizeof( temp2 ));
	COM_DefaultExtension( temp2, ".wad", sizeof( temp2 ));

	// quick reject by wadname
	if( !anywadname && Q_stricmp( wadname, temp2 ))
		return;

	for( i = 0; i < search->wad->numlumps; i++ )
	{
		// if type not matching, we already have no chance ...
		if( type != TYP_ANY && search->wad->lumps[i].type != type )
			continue;

		// build the lumpname with image suffix (if present)
		Q_strncpy( temp, search->wad->lumps[i].name, sizeof( temp ));

		while( temp[0] )
		{
			if( matchpattern( temp, wadpattern, true ))
			{
				for( j = 0; j < list->numstrings; j++ )
				{
					if( !Q_strcmp( list->strings[j], temp ))
						break;
				}

				if( j == list->numstrings )
				{
					// build path: wadname/lumpname.ext
					Q_snprintf( temp2, sizeof( temp2 ), "%s/%s", wadfolder, temp );
					Q_snprintf( buf, sizeof( buf ), ".%s", W_ExtFromType( search->wad->lumps[i].type ));
					COM_DefaultExtension( temp2, buf, sizeof( temp2 ));
					stringlistappend( list, temp2 );
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


/*
===========
W_ReadLump

reading lump into temp buffer
===========
*/
static byte *W_ReadLump( searchpath_t *search, const char *path, int pack_ind, fs_offset_t *lumpsizeptr, void *( *pfnAlloc )( size_t ), void ( *pfnFree )( void * ))
{
	const wfile_t *wad = search->wad;
	const dlumpinfo_t *lump = &wad->lumps[pack_ind];
	size_t	oldpos, size = 0;
	byte	*buf;

	// assume error
	if( lumpsizeptr ) *lumpsizeptr = 0;

	// no wads loaded
	if( !wad || !lump ) return NULL;

	oldpos = FS_Tell( wad->handle ); // don't forget restore original position

	if( FS_Seek( wad->handle, lump->filepos, SEEK_SET ) == -1 )
	{
		Con_Reportf( S_ERROR "%s: %s is corrupted\n", __func__, lump->name );
		FS_Seek( wad->handle, oldpos, SEEK_SET );
		return NULL;
	}

	buf = (byte *)pfnAlloc( lump->disksize );
	if( unlikely( !buf ))
	{
		Con_Reportf( S_ERROR "%s: can't alloc %d bytes, no free memory\n", __func__, lump->disksize );
		FS_Seek( wad->handle, oldpos, SEEK_SET );
		return NULL;
	}

	size = FS_Read( wad->handle, buf, lump->disksize );
	FS_Seek( wad->handle, oldpos, SEEK_SET );

	if( size < lump->disksize )
	{
		Con_Reportf( S_WARN "%s: %s is probably corrupted\n", __func__, lump->name );
		pfnFree( buf );
		return NULL;
	}

	if( lumpsizeptr ) *lumpsizeptr = lump->disksize;

	return buf;
}

/*
====================
FS_AddWad_Fullpath
====================
*/
searchpath_t *FS_AddWad_Fullpath( const char *wadfile, int flags )
{
	searchpath_t *search;
	wfile_t *wad;
	int errorcode = WAD_LOAD_COULDNT_OPEN;

	wad = W_Open( wadfile, &errorcode, flags );

	if( !wad )
	{
		if( errorcode != WAD_LOAD_NO_FILES )
			Con_Reportf( S_ERROR "%s: unable to load wad \"%s\"\n", __func__, wadfile );
		return NULL;
	}

	search = (searchpath_t *)Mem_Calloc( fs_mempool, sizeof( searchpath_t ));
	Q_strncpy( search->filename, wadfile, sizeof( search->filename ));
	search->wad = wad;
	search->type = SEARCHPATH_WAD;
	search->flags = flags;

	search->pfnPrintInfo = FS_PrintInfo_WAD;
	search->pfnClose = FS_Close_WAD;
	search->pfnOpenFile = FS_OpenFile_WAD;
	search->pfnFileTime = FS_FileTime_WAD;
	search->pfnFindFile = FS_FindFile_WAD;
	search->pfnSearch = FS_Search_WAD;
	search->pfnLoadFile = W_ReadLump;

	Con_Reportf( "Adding WAD: %s (%i files)\n", wadfile, wad->numlumps );
	return search;
}
