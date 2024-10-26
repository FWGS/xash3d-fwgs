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

	if( pResource->type == t_sound )
		Q_snprintf( filepath, sizeof( filepath ), DEFAULT_SOUNDPATH "%s", pResource->szFileName );
	else Q_strncpy( filepath, pResource->szFileName, sizeof( filepath ));

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
	if( Host_IsLocalClient() || FS_FileExists( filepath, false ))
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
	resource_t	*p, *n;

	for( p = pList->pNext; p != pList && p; p = n )
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
