/*
hpak.c - custom user package to send other clients
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

#include "common.h"
#include "hpak.h"

#define HPAK_MAX_ENTRIES	0x8000

typedef struct hash_pack_queue_s
{
	char			*name;
	resource_t		resource;
	size_t			size;
	void			*data;
	struct hash_pack_queue_s	*next;
} hash_pack_queue_t;

static CVAR_DEFINE( hpk_maxsize, "hpk_max_size", "64", FCVAR_ARCHIVE|FCVAR_PRIVILEGED, "set limit by size for all HPK-files in megabytes ( 0 - unlimited )" );
CVAR_DEFINE_AUTO( hpk_custom_file, "custom.hpk", FCVAR_ARCHIVE|FCVAR_PRIVILEGED, "set custom path for players customizations cache file" );
static hash_pack_queue_t	*gp_hpak_queue = NULL;
static hpak_header_t	hash_pack_header;
static hpak_info_t	hash_pack_info;

static void HPAK_MaxSize_f( void )
{
	Con_Printf( S_ERROR "hpk_maxsize is deprecated, use hpk_max_size\n" );
}

const char *COM_ResourceTypeFromIndex( int type )
{
	switch( type )
	{
	case t_sound: return "decal";
	case t_skin: return "skin";
	case t_model: return "model";
	case t_decal: return "decal";
	case t_generic: return "generic";
	case t_eventscript: return "event";
	case t_world: return "map";
	}
	return "?";
}

static inline void HPAK_ResourceToCompat( dresource_t *dest, resource_t *src )
{
	memcpy( dest, src, sizeof( *dest ));
	dest->pNext = dest->pPrev = 0xDEADBEEF;
}

static inline void HPAK_ResourceFromCompat( resource_t *dest, dresource_t *src )
{
	memcpy( dest, src, sizeof( *src ));
	dest->pNext = dest->pPrev = (void*)0xDEADBEEF;
}

static void HPAK_AddToQueue( const char *name, resource_t *pResource, void *data, file_t *f )
{
	hash_pack_queue_t	*p;

	p = Z_Malloc( sizeof( hash_pack_queue_t ));
	p->name = copystring( name );
	p->resource = *pResource;
	p->size = pResource->nDownloadSize;
	p->data = Z_Malloc( p->size );

	if( data != NULL ) memcpy( p->data, data, p->size );
	else if( f != NULL ) FS_Read( f, p->data, p->size );
	else Host_Error( "%s: data == NULL.\n", __func__ );

	p->next = gp_hpak_queue;
	gp_hpak_queue = p;
}

void HPAK_FlushHostQueue( void )
{
	hash_pack_queue_t	*p;

	for( p = gp_hpak_queue; p != NULL; p = gp_hpak_queue )
	{
		gp_hpak_queue = p->next;
		HPAK_AddLump( false, p->name, &p->resource, p->data, NULL );
		freestring( p->name );
		Mem_Free( p->data );
		Mem_Free( p );
	}
	gp_hpak_queue = NULL;
}

static void HPAK_CreatePak( const char *filename, resource_t *pResource, byte *pData, file_t *fin )
{
	int		filelocation;
	string		pakname;
	byte		md5[16];
	file_t		*fout;
	MD5Context_t	ctx = { 0 };

	if( !COM_CheckString( filename ))
		return;

	if(( fin != NULL && pData != NULL ) || ( fin == NULL && pData == NULL ))
		return;

	Q_strncpy( pakname, filename, sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));

	Con_Printf( "creating HPAK %s.\n", pakname );

	fout = FS_Open( pakname, "wb", true );
	if( !fout )
	{
		Con_DPrintf( S_ERROR "%s: can't write %s.\n", __func__, pakname );
		return;
	}

	// let's hash it.
	MD5Init( &ctx );

	if( pData == NULL )
	{
		byte *temp;

		// there are better ways
		filelocation = FS_Tell( fin );
		temp = Z_Malloc( pResource->nDownloadSize );
		FS_Read( fin, temp, pResource->nDownloadSize );
		FS_Seek( fin, filelocation, SEEK_SET );
		MD5Update( &ctx, temp, pResource->nDownloadSize );
		Mem_Free( temp );
	}
	else
	{
		MD5Update( &ctx, pData, pResource->nDownloadSize );
	}

	MD5Final( md5, &ctx );

	if( memcmp( md5, pResource->rgucMD5_hash, 16 ))
	{
		Con_DPrintf( S_ERROR "%s: bad checksum for %s. Ignored\n", __func__, pakname );
		return;
	}

	hash_pack_header.ident = IDHPAKHEADER;
	hash_pack_header.version = IDHPAK_VERSION;
	hash_pack_header.infotableofs = 0;

	FS_Write( fout, &hash_pack_header, sizeof( hash_pack_header ));

	hash_pack_info.count = 1;
	hash_pack_info.entries = Z_Malloc( sizeof( hpak_lump_t ));
	HPAK_ResourceToCompat( &hash_pack_info.entries[0].resource, pResource );
	hash_pack_info.entries[0].filepos = FS_Tell( fout );
	hash_pack_info.entries[0].disksize = pResource->nDownloadSize;

	if( pData == NULL )
	{
		FS_FileCopy( fout, fin, hash_pack_info.entries[0].disksize );
	}
	else
	{
		FS_Write( fout, pData, hash_pack_info.entries[0].disksize );
	}

	filelocation = FS_Tell( fout );
	FS_Write( fout, &hash_pack_info.count, sizeof( hash_pack_info.count ));
	FS_Write( fout, &hash_pack_info.entries[0], sizeof( hpak_lump_t ));

	if( hash_pack_info.entries )
		Mem_Free( hash_pack_info.entries );
	memset( &hash_pack_info, 0, sizeof( hpak_info_t ));

	hash_pack_header.infotableofs = filelocation;
	FS_Seek( fout, 0, SEEK_SET );
	FS_Write( fout, &hash_pack_header, sizeof( hpak_header_t ));
	FS_Close( fout );
}

static qboolean HPAK_FindResource( hpak_info_t *hpk, byte *hash, resource_t *pResource )
{
	int	i;

	for( i = 0; i < hpk->count; i++ )
	{
		if( !memcmp( hpk->entries[i].resource.rgucMD5_hash, hash, 16 ))
		{
			if( pResource )
				HPAK_ResourceFromCompat( pResource, &hpk->entries[i].resource );
			return true;
		}
	}

	return false;
}

void HPAK_AddLump( qboolean bUseQueue, const char *name, resource_t *pResource, byte *pData, file_t *pFile )
{
	int		i, j, position, length;
	hpak_lump_t	*pCurrentEntry = NULL;
	string		srcname, dstname;
	hpak_info_t	srcpak, dstpak;
	file_t		*file_src;
	file_t		*file_dst;
	byte		md5[16];
	MD5Context_t	ctx = { 0 };

	if( pData == NULL && pFile == NULL )
		return;

	if( pResource->nDownloadSize < HPAK_ENTRY_MIN_SIZE || pResource->nDownloadSize > HPAK_ENTRY_MAX_SIZE )
	{
		Con_Printf( S_ERROR "%s: invalid size %s\n", name, Q_memprint( pResource->nDownloadSize ));
		return;
	}

	// hash it
	MD5Init( &ctx );

	if( !pData )
	{
		byte		*temp;

		// there are better ways
		position = FS_Tell( pFile );
		temp = Z_Malloc( pResource->nDownloadSize );
		FS_Read( pFile, temp, pResource->nDownloadSize );
		FS_Seek( pFile, position, SEEK_SET );
		MD5Update( &ctx, temp, pResource->nDownloadSize );
		Mem_Free( temp );
	}
	else
	{
		MD5Update( &ctx, pData, pResource->nDownloadSize );
	}

	MD5Final( md5, &ctx );

	if( memcmp( md5, pResource->rgucMD5_hash, 16 ))
	{
		Con_DPrintf( S_ERROR "%s: bad checksum for %s. Ignored\n", __func__, pResource->szFileName );
		return;
	}

	if( bUseQueue )
	{
		HPAK_AddToQueue( name, pResource, pData, pFile );
		return;
	}

	Q_strncpy( srcname, name, sizeof( srcname ));
	COM_ReplaceExtension( srcname, ".hpk", sizeof( srcname ));

	file_src = FS_Open( srcname, "rb", true );

	if( !file_src )
	{
		// just create new pack
		HPAK_CreatePak( name, pResource, pData, pFile );
		return;
	}

	Q_strncpy( dstname, srcname, sizeof( dstname ));
	COM_ReplaceExtension( dstname, ".hp2", sizeof( dstname ));

	file_dst = FS_Open( dstname, "wb", true );

	if( !file_dst )
	{
		Con_DPrintf( S_ERROR "%s: couldn't open %s.\n", __func__, srcname );
		FS_Close( file_src );
		return;
	}

	// load headers
	FS_Read( file_src, &hash_pack_header, sizeof( hpak_header_t ));

	if( hash_pack_header.version != IDHPAK_VERSION )
	{
		// we don't check the HPAK bit for some reason.
		Con_DPrintf( S_ERROR "%s: %s does not have a valid header.\n", __func__, srcname );
		FS_Close( file_src );
		FS_Close( file_dst );
		return;
	}

	length = FS_FileLength( file_src );
	FS_Seek( file_src, 0, SEEK_SET ); // rewind to start of file
	FS_FileCopy( file_dst, file_src, length );

	FS_Seek( file_src, hash_pack_header.infotableofs, SEEK_SET );
	FS_Read( file_src, &srcpak.count, sizeof( srcpak.count ));

	if( srcpak.count < 1 || srcpak.count > HPAK_MAX_ENTRIES )
	{
		Con_DPrintf( S_ERROR "%s: %s contain too many lumps.\n", __func__, srcname );
		FS_Close( file_src );
		FS_Close( file_dst );
		return;
	}

	// load the data
	srcpak.entries = Z_Malloc( sizeof( hpak_lump_t ) * srcpak.count );
	FS_Read( file_src, srcpak.entries, sizeof( hpak_lump_t ) * srcpak.count );

	// check if already exists
	if( HPAK_FindResource( &srcpak, pResource->rgucMD5_hash, NULL ))
	{
		Z_Free( srcpak.entries );
		FS_Close( file_src );
		FS_Close( file_dst );
		FS_Delete( dstname );
		return;
	}

	// make a new container
	dstpak.count = srcpak.count + 1;
	dstpak.entries = Z_Malloc( sizeof( hpak_lump_t ) * dstpak.count );
	memcpy( dstpak.entries, srcpak.entries, sizeof( hpak_lump_t ) * srcpak.count );

	// check is there are entry with same hash
	for( i = 0; i < srcpak.count; i++ )
	{
		if( memcmp( md5, srcpak.entries[i].resource.rgucMD5_hash, 16 ) == 0 )
		{
			pCurrentEntry = &dstpak.entries[i];

			for( j = i; j < srcpak.count; j++ )
				dstpak.entries[j + 1] = srcpak.entries[j];
		}
	}

	if( !pCurrentEntry )
		pCurrentEntry = &dstpak.entries[dstpak.count-1];

	memset( pCurrentEntry, 0, sizeof( hpak_lump_t ));
	FS_Seek( file_dst, hash_pack_header.infotableofs, SEEK_SET );
	HPAK_ResourceToCompat( &pCurrentEntry->resource, pResource );
	pCurrentEntry->filepos = FS_Tell( file_dst );
	pCurrentEntry->disksize = pResource->nDownloadSize;

	if( !pData )
		FS_FileCopy( file_dst, pFile, pCurrentEntry->disksize );
	else
		FS_Write( file_dst, pData, pCurrentEntry->disksize );

	hash_pack_header.infotableofs = FS_Tell( file_dst );
	FS_Write( file_dst, &dstpak.count, sizeof( dstpak.count ));

	for( i = 0; i < dstpak.count; i++ )
	{
		FS_Write( file_dst, &dstpak.entries[i], sizeof( hpak_lump_t ));
	}

	// finalize
	if( srcpak.entries )
		Mem_Free( srcpak.entries );
	if( dstpak.entries )
		Mem_Free( dstpak.entries );

	FS_Seek( file_dst, 0, SEEK_SET );
	FS_Write( file_dst, &hash_pack_header, sizeof( hpak_header_t ));

	FS_Close( file_src );
	FS_Close( file_dst );

	FS_Delete( srcname );
	FS_Rename( dstname, srcname );
}

static qboolean HPAK_Validate( const char *filename, qboolean quiet, qboolean delete )
{
	file_t		*f;
	hpak_lump_t	*dataDir;
	hpak_header_t	hdr;
	byte		*dataPak;
	int		i, num_lumps;
	MD5Context_t	MD5_Hash;
	string		pakname;
	dresource_t	*pRes;
	byte		md5[16];

	if( quiet ) HPAK_FlushHostQueue();

	// not an error - just flush queue
	if( !COM_CheckString( filename ) )
		return true;

	Q_strncpy( pakname, filename, sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));

	f = FS_Open( pakname, "rb", true );
	if( !f )
	{
		if( !quiet )
			Con_DPrintf( S_ERROR "Couldn't find %s.\n", pakname );
		return true;
	}

	if( !quiet ) Con_Printf( "Validating %s\n", pakname );

	FS_Read( f, &hdr, sizeof( hdr ));
	if( hdr.ident != IDHPAKHEADER || hdr.version != IDHPAK_VERSION )
	{
		Con_DPrintf( S_ERROR "%s: %s does not have a valid HPAK header.\n", __func__, pakname );
		FS_Close( f );
		if( delete ) FS_Delete( pakname );
		return false;
	}

	FS_Seek( f, hdr.infotableofs, SEEK_SET );
	FS_Read( f, &num_lumps, sizeof( num_lumps ));

	if( num_lumps < 1 || num_lumps > HPAK_MAX_ENTRIES )
	{
		Con_DPrintf( S_ERROR "%s: %s has too many lumps %u.\n", __func__, pakname, num_lumps );
		FS_Close( f );
		if( delete ) FS_Delete( pakname );
		return false;
	}

	if( !quiet ) Con_Printf( "# of Entries:  %i\n", num_lumps );

	dataDir = Z_Malloc( sizeof( hpak_lump_t ) * num_lumps );
	FS_Read( f, dataDir, sizeof( hpak_lump_t ) * num_lumps );

	if( !quiet ) Con_Printf( "# Type Size FileName : MD5 Hash\n" );

	for( i = 0; i < num_lumps; i++ )
	{
		if( dataDir[i].disksize < HPAK_ENTRY_MIN_SIZE || dataDir[i].disksize > HPAK_ENTRY_MAX_SIZE )
		{
			// odd max size
			Con_DPrintf( S_ERROR "%s: lump %i has invalid size %s\n", __func__, i, Q_memprint( dataDir[i].disksize ));
			Mem_Free( dataDir );
			FS_Close( f );
			if( delete ) FS_Delete( pakname );
			return false;
		}

		dataPak = Z_Malloc( dataDir[i].disksize );
		FS_Seek( f, dataDir[i].filepos, SEEK_SET );
		FS_Read( f, dataPak, dataDir[i].disksize );

		memset( &MD5_Hash, 0, sizeof( MD5Context_t ));
		MD5Init( &MD5_Hash );
		MD5Update( &MD5_Hash, dataPak, dataDir[i].disksize );
		MD5Final( md5, &MD5_Hash );

		pRes = &dataDir[i].resource;

		if( !quiet )
		{
			Con_Printf( "%i:      %s %s %s:   ", i, COM_ResourceTypeFromIndex( pRes->type ),
				Q_memprint( pRes->nDownloadSize ), pRes->szFileName );
		}

		if( memcmp( md5, pRes->rgucMD5_hash, 0x10 ))
		{
			if( quiet )
			{
				Con_DPrintf( S_ERROR "%s: %s has invalid checksum.\n", __func__, pakname );
				Mem_Free( dataPak );
				Mem_Free( dataDir );
				FS_Close( f );
				if( delete ) FS_Delete( pakname );
				return false;
			}
			else Con_DPrintf( S_ERROR "failed\n" );
		}
		else
		{
			if( !quiet ) Con_Printf( "OK\n" );
		}

		// at this point, it's passed our checks.
		Mem_Free( dataPak );
	}

	Mem_Free( dataDir );
	FS_Close( f );
	return true;
}

void HPAK_CheckIntegrity( const char *filename )
{
	string	pakname;

	if( !COM_CheckString( filename ) )
		return;

	Q_strncpy( pakname, filename, sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));

	HPAK_Validate( pakname, true, true );
}

void HPAK_CheckSize( const char *filename )
{
	string	pakname;
	int	maxsize;

	maxsize = hpk_maxsize.value;
	if( maxsize <= 0 ) return;

	if( !COM_CheckString( filename ) )
		return;

	Q_strncpy( pakname, filename, sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));

	if( FS_FileSize( pakname, false ) > ( maxsize * 1024 * 1024 ))
	{
		Con_Printf( "Server: Size of %s > %f MB, deleting.\n", filename, hpk_maxsize.value );
		Log_Printf( "Server: Size of %s > %f MB, deleting.\n", filename, hpk_maxsize.value );
		FS_Delete( filename );
	}
}

qboolean HPAK_ResourceForHash( const char *filename, byte *hash, resource_t *pResource )
{
	hpak_info_t	directory;
	hpak_header_t	header;
	string		pakname;
	qboolean		bFound;
	file_t		*f;
	hash_pack_queue_t	*p;

	if( !COM_CheckString( filename ))
		return false;

	for( p = gp_hpak_queue; p != NULL; p = p->next )
	{
		if( !Q_stricmp( p->name, filename ) && !memcmp( p->resource.rgucMD5_hash, hash, 16 ))
		{
			if( pResource != NULL )
				*pResource = p->resource;
			return true;
		}
	}

	Q_strncpy( pakname, filename, sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));

	f = FS_Open( pakname, "rb", true );
	if( !f ) return false;

	FS_Read( f, &header, sizeof( header ));

	if( header.ident != IDHPAKHEADER )
	{
		FS_Close( f );
		return false;
	}

	if( header.version != IDHPAK_VERSION )
	{
		FS_Close( f );
		return false;
	}

	FS_Seek( f, header.infotableofs, SEEK_SET );
	FS_Read( f, &directory.count, sizeof( directory.count ));

	if( directory.count < 1 || directory.count > HPAK_MAX_ENTRIES )
	{
		FS_Close( f );
		return false;
	}

	directory.entries = Z_Malloc( sizeof( hpak_lump_t ) * directory.count );
	FS_Read( f, directory.entries, sizeof( hpak_lump_t ) * directory.count );
	bFound = HPAK_FindResource( &directory, hash, pResource );
	Mem_Free( directory.entries );
	FS_Close( f );

	return bFound;
}

static qboolean HPAK_ResourceForIndex( const char *filename, int index, resource_t *pResource )
{
	hpak_header_t	header;
	hpak_info_t	directory;
	string		pakname;
	file_t		*f;

	if( !COM_CheckString( filename ) )
		return false;

	Q_strncpy( pakname, filename, sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));

	f = FS_Open( pakname, "rb", true );
	if( !f )
	{
		Con_DPrintf( S_ERROR "couldn't open %s.\n", pakname );
		return false;
	}

	FS_Read( f, &header, sizeof( header ));
	if( header.ident != IDHPAKHEADER )
	{
		Con_DPrintf( S_ERROR "%s is not an HPAK file\n", pakname );
		FS_Close( f );
		return false;
	}

	if( header.version != IDHPAK_VERSION )
	{
		Con_DPrintf( S_ERROR "%s has invalid version (%i should be %i).\n", pakname, header.version, IDHPAK_VERSION );
		FS_Close( f );
		return false;
	}

	FS_Seek( f, header.infotableofs, SEEK_SET );
	FS_Read( f, &directory.count, sizeof( directory.count ));

	if( directory.count < 1 || directory.count > HPAK_MAX_ENTRIES )
	{
		Con_DPrintf( S_ERROR "%s has too many lumps %u.\n", pakname, directory.count );
		FS_Close( f );
		return false;
	}

	if( index < 1 || index > directory.count )
	{
		Con_DPrintf( S_ERROR "%s, lump with index %i doesn't exist.\n", pakname, index );
		FS_Close( f );
		return false;
	}

	directory.entries = Z_Malloc( sizeof( hpak_lump_t ) * directory.count );
	FS_Read( f, directory.entries, sizeof( hpak_lump_t ) * directory.count );
	HPAK_ResourceFromCompat( pResource, &directory.entries[index-1].resource );
	Z_Free( directory.entries );
	FS_Close( f );

	return true;
}

qboolean HPAK_GetDataPointer( const char *filename, resource_t *pResource, byte **buffer, int *bufsize )
{
	byte		*tmpbuf;
	string		pakname;
	hpak_header_t	header;
	hpak_info_t	directory;
	hpak_lump_t	*entry;
	hash_pack_queue_t	*p;
	file_t		*f;
	int		i;

	if( !COM_CheckString( filename ))
		return false;

	if( buffer ) *buffer = NULL;
	if( bufsize ) *bufsize = 0;

	for( p = gp_hpak_queue; p != NULL; p = p->next )
	{
		if( !Q_stricmp( p->name, filename ) && !memcmp( p->resource.rgucMD5_hash, pResource->rgucMD5_hash, 16 ))
		{
			if( buffer )
			{
				tmpbuf = Z_Malloc( p->size );
				memcpy( tmpbuf, p->data, p->size );
				*buffer = tmpbuf;
			}

			if( bufsize )
				*bufsize = p->size;

			return true;
		}
	}

	Q_strncpy( pakname, filename, sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));

	f = FS_Open( pakname, "rb", true );
	if( !f ) return false;

	FS_Read( f, &header, sizeof( header ));

	if( header.ident != IDHPAKHEADER )
	{
		Con_DPrintf( S_ERROR "%s it's not a HPK file.\n", pakname );
		FS_Close( f );
		return false;
	}

	if( header.version != IDHPAK_VERSION )
	{
		Con_DPrintf( S_ERROR "%s has invalid version (%i should be %i).\n", pakname, header.version, IDHPAK_VERSION );
		FS_Close( f );
		return false;
	}

	FS_Seek( f, header.infotableofs, SEEK_SET );
	FS_Read( f, &directory.count, sizeof( directory.count ));

	if( directory.count < 1 || directory.count > HPAK_MAX_ENTRIES )
	{
		Con_DPrintf( S_ERROR "%s: %s has too many lumps %u.\n", __func__, filename, directory.count );
		FS_Close( f );
		return false;
	}

	directory.entries = Z_Malloc( sizeof( hpak_lump_t ) * directory.count );
	FS_Read( f, directory.entries, sizeof( hpak_lump_t ) * directory.count );

	for( i = 0; i < directory.count; i++ )
	{
		entry = &directory.entries[i];

		if( entry->filepos > 0 &&
			entry->disksize > 0 &&
			!memcmp( entry->resource.rgucMD5_hash, pResource->rgucMD5_hash, 16 ))
		{
			FS_Seek( f, entry->filepos, SEEK_SET );

			if( buffer )
			{
				tmpbuf = Z_Malloc( entry->disksize );
				FS_Read( f, tmpbuf, entry->disksize );
				*buffer = tmpbuf;
			}

			if( bufsize )
				*bufsize = entry->disksize;

			Mem_Free( directory.entries );
			FS_Close( f );

			return true;
		}
	}

	Mem_Free( directory.entries );
	FS_Close( f );

	return false;
}

void HPAK_RemoveLump( const char *name, resource_t *pResource )
{
	string		read_path;
	string		save_path;
	file_t		*file_src;
	file_t		*file_dst;
	hpak_info_t	hpak_read;
	hpak_info_t	hpak_save;
	int		i, j;

	if( !COM_CheckString( name ) || !pResource )
		return;

	HPAK_FlushHostQueue();

	Q_strncpy( read_path, name, sizeof( read_path ));
	COM_ReplaceExtension( read_path, ".hpk", sizeof( read_path ));

	file_src = FS_Open( read_path, "rb", true );
	if( !file_src )
	{
		Con_DPrintf( S_ERROR "%s couldn't open.\n", read_path );
		return;
	}

	Q_strncpy( save_path, read_path, sizeof( save_path ));
	COM_ReplaceExtension( save_path, ".hp2", sizeof( save_path ));
	file_dst = FS_Open( save_path, "wb", true );

	if( !file_dst )
	{
		Con_DPrintf( S_ERROR "%s couldn't open.\n", save_path );
		FS_Close( file_src );
		return;
	}

	FS_Seek( file_src, 0, SEEK_SET );
	FS_Seek( file_dst, 0, SEEK_SET );

	// header copy
	FS_Read( file_src, &hash_pack_header, sizeof( hpak_header_t ));
	FS_Write( file_dst, &hash_pack_header, sizeof( hpak_header_t ));

	if( hash_pack_header.ident != IDHPAKHEADER || hash_pack_header.version != IDHPAK_VERSION )
	{
		Con_DPrintf( S_ERROR "%s has invalid header.\n", read_path );
		FS_Close( file_src );
		FS_Close( file_dst );
		FS_Delete( save_path ); // delete temp file
		return;
	}

	FS_Seek( file_src, hash_pack_header.infotableofs, SEEK_SET );
	FS_Read( file_src, &hpak_read.count, sizeof( hpak_read.count ));

	if( hpak_read.count < 1 || hpak_read.count > HPAK_MAX_ENTRIES )
	{
		Con_DPrintf( S_ERROR "%s has invalid number of lumps.\n", read_path );
		FS_Close( file_src );
		FS_Close( file_dst );
		FS_Delete( save_path ); // delete temp file
		return;
	}

	if( hpak_read.count == 1 )
	{
		Con_DPrintf( S_WARN "%s only has one element, so HPAK will be removed\n", read_path );
		FS_Close( file_src );
		FS_Close( file_dst );
		FS_Delete( read_path );
		FS_Delete( save_path );
		return;
	}

	hpak_save.count = hpak_read.count - 1;
	hpak_read.entries = Z_Malloc( sizeof( hpak_lump_t ) * hpak_read.count );
	hpak_save.entries = Z_Malloc( sizeof( hpak_lump_t ) * hpak_save.count );

	FS_Read( file_src, hpak_read.entries, sizeof( hpak_lump_t ) * hpak_read.count );

	if( !HPAK_FindResource( &hpak_read, pResource->rgucMD5_hash, NULL ))
	{
		Con_DPrintf( S_ERROR "HPAK %s doesn't contain specified lump: %s\n", read_path, pResource->szFileName );
		Mem_Free( hpak_read.entries );
		Mem_Free( hpak_save.entries );
		FS_Close( file_src );
		FS_Close( file_dst );
		FS_Delete( save_path );
		return;
	}

	Con_Printf( "Removing %s from HPAK %s.\n", pResource->szFileName, read_path );

	// If there's a collision, we've just corrupted this hpak.
	for( i = 0, j = 0; i < hpak_read.count; i++ )
	{
		if( !memcmp( hpak_read.entries[i].resource.rgucMD5_hash, pResource->rgucMD5_hash, 16 ))
			continue;

		hpak_save.entries[j] = hpak_read.entries[i];
		hpak_save.entries[j].filepos = FS_Tell( file_dst );
		FS_Seek( file_src, hpak_read.entries[j].filepos, SEEK_SET );
		FS_FileCopy( file_dst, file_src, hpak_save.entries[j].disksize );
		j++;
	}

	hash_pack_header.infotableofs = FS_Tell( file_dst );
	FS_Write( file_dst, &hpak_save.count, sizeof( hpak_save.count ));

	for( i = 0; i < hpak_save.count; i++ )
		FS_Write( file_dst, &hpak_save.entries[i], sizeof( hpak_lump_t ));

	FS_Seek( file_dst, 0, SEEK_SET );
	FS_Write( file_dst, &hash_pack_header, sizeof( hpak_header_t ));

	Mem_Free( hpak_read.entries );
	Mem_Free( hpak_save.entries );
	FS_Close( file_src );
	FS_Close( file_dst );

	FS_Delete( read_path );
	FS_Rename( save_path, read_path );
}

static void HPAK_List_f( void )
{
	int		nCurrent;
	hpak_header_t	header;
	hpak_info_t	directory;
	hpak_lump_t	*entry;
	string		lumpname;
	string		pakname;
	const char	*type;
	const char	*size;
	file_t		*f;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "hpklist <hpk>\n" );
		return;
	}

	HPAK_FlushHostQueue();

	Q_strncpy( pakname, Cmd_Argv( 1 ), sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));
	Con_Printf( "Contents for %s.\n", pakname );

	f = FS_Open( pakname, "rb", true );
	if( !f )
	{
		Con_DPrintf( S_ERROR "couldn't open %s.\n", pakname );
		return;
	}

	FS_Read( f, &header, sizeof( hpak_header_t ));

	if( header.ident != IDHPAKHEADER )
	{
		Con_DPrintf( S_ERROR "%s is not an HPAK file\n", pakname );
		FS_Close( f );
		return;
	}

	if( header.version != IDHPAK_VERSION )
	{
		Con_DPrintf( S_ERROR "%s has invalid version (%i should be %i).\n", pakname, header.version, IDHPAK_VERSION );
		FS_Close( f );
		return;
	}

	FS_Seek( f, header.infotableofs, SEEK_SET );
	FS_Read( f, &directory.count, sizeof( directory.count ));

	if( directory.count < 1 || directory.count > HPAK_MAX_ENTRIES )
	{
		Con_DPrintf( S_ERROR "%s has too many lumps %u.\n", pakname, directory.count );
		FS_Close( f );
		return;
	}

	Con_Printf( "# of Entries:  %i\n", directory.count );
	Con_Printf( "# Type Size FileName : MD5 Hash\n" );

	directory.entries = Z_Malloc( directory.count * sizeof( hpak_lump_t ));
	FS_Read( f, directory.entries, directory.count * sizeof( hpak_lump_t ));

	for( nCurrent = 0; nCurrent < directory.count; nCurrent++ )
	{
		entry = &directory.entries[nCurrent];
		COM_FileBase( entry->resource.szFileName, lumpname, sizeof( lumpname ));
		type = COM_ResourceTypeFromIndex( entry->resource.type );
		size = Q_memprint( entry->resource.nDownloadSize );

		Con_Printf( "%i: %10s %s %s\n  :  %s\n", nCurrent + 1, type, size, lumpname, MD5_Print( entry->resource.rgucMD5_hash ));
	}

	if( directory.entries )
		Mem_Free( directory.entries );
	FS_Close( f );
}

static void HPAK_Extract_f( void )
{
	int		nCurrent;
	hpak_header_t	header;
	hpak_info_t	directory;
	hpak_lump_t	*entry;
	string		lumpname;
	string		pakname;
	string		szFileOut;
	int		nIndex;
	byte		*pData;
	int		nDataSize;
	const char	*type;
	const char	*size;
	file_t		*f;

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "hpkextract hpkname [all | single index]\n" );
		return;
	}

	if( !Q_stricmp( Cmd_Argv( 2 ), "all" ))
	{
		nIndex = -1;
	}
	else
	{
		nIndex = Q_atoi( Cmd_Argv( 2 ) );
	}

	HPAK_FlushHostQueue();

	Q_strncpy( pakname, Cmd_Argv( 1 ), sizeof( pakname ));
	COM_ReplaceExtension( pakname, ".hpk", sizeof( pakname ));
	Con_Printf( "Contents for %s.\n", pakname );

	f = FS_Open( pakname, "rb", true );
	if( !f )
	{
		Con_DPrintf( S_ERROR "couldn't open %s.\n", pakname );
		return;
	}

	FS_Read( f, &header, sizeof( hpak_header_t ));

	if( header.ident != IDHPAKHEADER )
	{
		Con_DPrintf( S_ERROR "%s is not an HPAK file\n", pakname );
		FS_Close( f );
		return;
	}

	if( header.version != IDHPAK_VERSION )
	{
		Con_DPrintf( S_ERROR "%s has invalid version (%i should be %i).\n", pakname, header.version, IDHPAK_VERSION );
		FS_Close( f );
		return;
	}

	FS_Seek( f, header.infotableofs, SEEK_SET );
	FS_Read( f, &directory.count, sizeof( directory.count ));

	if( directory.count < 1 || directory.count > HPAK_MAX_ENTRIES )
	{
		Con_DPrintf( S_ERROR "%s has too many lumps %u.\n", pakname, directory.count );
		FS_Close( f );
		return;
	}

	if( nIndex == -1 ) Con_Printf( "Extracting all lumps from %s.\n", pakname );
	else Con_Printf( "Extracting lump %i from %s\n", nIndex, pakname );

	directory.entries = Z_Malloc( directory.count * sizeof( hpak_lump_t ));
	FS_Read( f, directory.entries, directory.count * sizeof( hpak_lump_t ));

	for( nCurrent = 0; nCurrent < directory.count; nCurrent++ )
	{
		entry = &directory.entries[nCurrent];

		if( nIndex != -1 && nIndex != nCurrent )
			continue;

		COM_FileBase( entry->resource.szFileName, lumpname, sizeof( lumpname ) );
		type = COM_ResourceTypeFromIndex( entry->resource.type );
		size = Q_memprint( entry->resource.nDownloadSize );

		Con_Printf( "Extracting %i: %10s %s %s\n", nCurrent + 1, type, size, lumpname );

		if( entry->disksize < HPAK_ENTRY_MIN_SIZE || entry->disksize > HPAK_ENTRY_MAX_SIZE )
		{
			Con_DPrintf( S_WARN "Unable to extract data, size invalid:  %s\n", Q_memprint( entry->disksize ));
			continue;
		}

		nDataSize = entry->disksize;
		pData = Z_Malloc( nDataSize + 1 );
		FS_Seek( f, entry->filepos, SEEK_SET );
		FS_Read( f, pData, nDataSize );

		Q_snprintf( szFileOut, sizeof( szFileOut ), "hpklmps/lmp%04i.bmp", nCurrent );
		FS_WriteFile( szFileOut, pData, nDataSize );
		if( pData ) Mem_Free( pData );
	}

	if( directory.entries )
		Mem_Free( directory.entries );

	FS_Close( f );
}

static void HPAK_Remove_f( void )
{
	resource_t	resource;

	HPAK_FlushHostQueue();

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "hpkremove <hpk> <index>\n" );
		return;
	}

	if( HPAK_ResourceForIndex( Cmd_Argv( 1 ), Q_atoi( Cmd_Argv( 2 )), &resource ))
	{
		HPAK_RemoveLump( Cmd_Argv( 1 ), &resource );
	}
	else
	{
		Con_DPrintf( S_ERROR "Could not locate resource %i in %s\n", Q_atoi( Cmd_Argv( 2 )), Cmd_Argv( 1 ));
	}
}

static void HPAK_Validate_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "hpkval <filename>\n" );
		return;
	}

	HPAK_Validate( Cmd_Argv( 1 ), false, false );
}

void HPAK_Init( void )
{
	Cmd_AddRestrictedCommand( "hpklist", HPAK_List_f, "list all files in specified HPK-file" );
	Cmd_AddRestrictedCommand( "hpkremove", HPAK_Remove_f, "remove specified file from HPK-file" );
	Cmd_AddRestrictedCommand( "hpkval", HPAK_Validate_f, "validate specified HPK-file" );
	Cmd_AddRestrictedCommand( "hpkextract", HPAK_Extract_f, "extract all lumps from specified HPK-file" );
	Cmd_AddRestrictedCommand( "hpk_maxsize", HPAK_MaxSize_f, "deprecation notice for hpk_maxsize" );
	Cvar_RegisterVariable( &hpk_maxsize );
	Cvar_RegisterVariable( &hpk_custom_file );

	gp_hpak_queue = NULL;
}
