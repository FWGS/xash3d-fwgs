/*
cl_custom.c - downloading custom resources
Copyright (C) 2018 Uncle Mike

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
#include "client.h"
#include "net_encode.h"

/*
=====================
CL_ResourcePath

=====================
*/
void CL_ResourcePath( char *filepath, size_t size, const resource_t *pResource )
{
	if( pResource->type == t_sound )
		Q_snprintf( filepath, size, DEFAULT_SOUNDPATH "%s", pResource->szFileName );
	else Q_strncpy( filepath, pResource->szFileName, size );
}

/*
=====================
CL_HasResourceFile

=====================
*/
qboolean CL_HasResourceFile( const resource_t *pResource, const char *filepath )
{
	char cachepath[MAX_SYSPATH];

	// only the download cache is ours to verify and refetch, whatever the game itself provides is none of the server's business
	COM_DownloadCachePath( cachepath, sizeof( cachepath ), filepath, false );

	FS_AllowDirectPaths( true );
	fs_offset_t size = FS_FileSize( cachepath, false );
	FS_AllowDirectPaths( false );

	// FIXME: proof of concept, we only catch files shorter than the server's copy, i.e. leftovers of an interrupted download.
	// A complete file of a different size still passes. GoldSrc never compares sizes at all.
	//
	// comparing against a wrapped nDownloadSize (it's sent as a signed 24-bit value, see SV_SendResource) is harmless here, as wrapping only ever makes it smaller
	if( size >= 0 && size < pResource->nDownloadSize )
	{
		Con_Printf( S_WARN "%s is %ld bytes but server reports %d, downloading it again\n", cachepath, (long)size, pResource->nDownloadSize );

		FS_AllowDirectPaths( true );
		FS_Delete( cachepath );
		FS_AllowDirectPaths( false );
	}

	return FS_FileExists( filepath, false );
}

qboolean CL_CheckFile( sizebuf_t *msg, resource_t *pResource )
{
	char	filepath[MAX_QPATH];

	switch( pResource->type )
	{
	case t_sound:
	case t_model:
		// built-in resources not needs to be downloaded
		if( pResource->szFileName[0] == '*' )
			return true;
		break;
	}

	// resource was missed on server
	if( pResource->nDownloadSize == -1 )
	{
		ClearBits( pResource->ucFlags, RES_FATALIFMISSING );
		return true;
	}

	CL_ResourcePath( filepath, sizeof( filepath ), pResource );

	if( !COM_IsSafeFileToDownload( filepath ))
	{
		Con_Reportf( "refusing to download %s\n", filepath );
		return true;
	}

	if( !cl_allow_download.value )
	{
		Con_Reportf( "Download refused, cl_allowdownload is 0\n" );
		return true;
	}

	if( cls.state == ca_active && !cl_download_ingame.value )
	{
		Con_Reportf( "In-game download refused...\n" );
		return true;
	}

	// don't request downloads from local client it's silly
	if( Host_IsLocalClient() || CL_HasResourceFile( pResource, filepath ))
		return true;

	if( cls.demoplayback )
	{
		Con_Reportf( S_WARN "file %s missing during demo playback.\n", filepath );
		return true;
	}

	host.downloadcount++;

	if( cl.http_download )
	{
		HTTP_AddDownload( filepath, pResource->nDownloadSize, true, pResource );
	}
	else
	{
		MSG_BeginClientCmd( msg, clc_stringcmd );
		MSG_WriteStringf( msg, "dlfile %s", filepath );
	}

	return false;
}

void CL_AddToResourceList( resource_t *pResource, resource_t *pList )
{
	if( pResource->pPrev != NULL || pResource->pNext != NULL )
	{
		Con_Reportf( S_ERROR "Resource already linked\n" );
		return;
	}

	if( pList->pPrev == NULL || pList->pNext == NULL )
		Host_Error( "Resource list corrupted.\n" );

	pResource->pPrev = pList->pPrev;
	pResource->pNext = pList;
	pList->pPrev->pNext = pResource;
	pList->pPrev = pResource;
}

void CL_RemoveFromResourceList( resource_t *pResource )
{
	if( pResource->pPrev == NULL || pResource->pNext == NULL )
		Host_Error( "mislinked resource in %s\n", __func__ );

	if( pResource->pNext == pResource || pResource->pPrev == pResource )
		Host_Error( "attempt to free last entry in list.\n" );

	pResource->pPrev->pNext = pResource->pNext;
	pResource->pNext->pPrev = pResource->pPrev;
	pResource->pPrev = NULL;
	pResource->pNext = NULL;
}

void CL_MoveToOnHandList( resource_t *pResource )
{
	if( !pResource )
	{
		Con_Reportf( "Null resource passed to %s\n", __func__ );
		return;
	}

	CL_RemoveFromResourceList( pResource );
	CL_AddToResourceList( pResource, &cl.resourcesonhand );
}

static void CL_ClearResourceList( resource_t *pList )
{
	for( resource_t *p = pList->pNext, *n; p != pList && p; p = n )
	{
		n = p->pNext;

		CL_RemoveFromResourceList( p );
		Mem_Free( p );
	}

	pList->pPrev = pList;
	pList->pNext = pList;
}

void CL_ClearResourceLists( void )
{
	CL_ClearResourceList( &cl.resourcesneeded );
	CL_ClearResourceList( &cl.resourcesonhand );
}
