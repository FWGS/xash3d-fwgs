/*
sv_custom.c - downloading custom resources
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
#include "server.h"

static void SV_CreateCustomizationList( sv_client_t *cl )
{
	resource_t	*pResource;
	customization_t	*pList, *pCust;
	qboolean		bFound;
	int		nLumps;

	cl->customdata.pNext = NULL;

	for( pResource = cl->resourcesonhand.pNext; pResource != &cl->resourcesonhand; pResource = pResource->pNext )
	{
		bFound = false;

		for( pList = cl->customdata.pNext; pList != NULL; pList = pList->pNext )
		{
			if( !memcmp( pList->resource.rgucMD5_hash, pResource->rgucMD5_hash, 16 ))
			{
				bFound = true;
				break;
			}
		}

		if( !bFound )
		{
			nLumps = 0;

			if( COM_CreateCustomization( &cl->customdata, pResource, -1, FCUST_FROMHPAK|FCUST_WIPEDATA, &pCust, &nLumps ))
			{
				pCust->nUserData2 = nLumps;
				svgame.dllFuncs.pfnPlayerCustomization( cl->edict, pCust );
			}
			else
			{
				if( sv_allow_upload.value )
					Con_Printf( "Ignoring invalid custom decal from %s\n", cl->name );
				else Con_Printf( "Ignoring custom decal from %s\n", cl->name );
			}
		}
		else
		{
			Con_Printf( S_WARN "%s: ignoring dup. resource for player %s\n", __func__, cl->name );
		}
	}
}

static qboolean SV_FileInConsistencyList( const char *filename, consistency_t **ppout )
{
	int	i;

	if( ppout != NULL )
		*ppout = NULL;

	for( i = 0; i < MAX_MODELS; i++ )
	{
		consistency_t	*pc = &sv.consistency_list[i];

		if( !pc->filename )
			break;

		if( !Q_stricmp( pc->filename, filename ))
		{
			if( ppout != NULL )
				*ppout = pc;
			return true;
		}
	}

	return false;
}

void SV_ParseConsistencyResponse( sv_client_t *cl, sizebuf_t *msg )
{
	int		i, c, idx, value;
	byte		readbuffer[32];
	byte		nullbuffer[32];
	byte		resbuffer[32];
	qboolean		invalid_type;
	vec3_t		cmins, cmaxs;
	int		badresindex;
	vec3_t		mins, maxs;
	FORCE_TYPE	ft;
	resource_t	*r;

	memset( nullbuffer, 0, sizeof( nullbuffer ));
	invalid_type = false;
	badresindex = 0;
	c = 0;

	while( MSG_ReadOneBit( msg ))
	{
		idx = MSG_ReadUBitLong( msg, MAX_MODEL_BITS );
		if( idx < 0 || idx >= sv.num_resources )
			break;

		r = &sv.resources[idx];

		if( !FBitSet( r->ucFlags, RES_CHECKFILE ))
			break;

		memcpy( readbuffer, r->rguc_reserved, 32 );

		if( !memcmp( readbuffer, nullbuffer, 32 ))
		{
			value = MSG_ReadUBitLong( msg, 32 );

			LittleLongSW( value );

			// will be compare only first 4 bytes
			if( memcmp( &value, r->rgucMD5_hash, 4 ))
				badresindex = idx + 1;
		}
		else
		{
			MSG_ReadBytes( msg, cmins, sizeof( cmins ));
			MSG_ReadBytes( msg, cmaxs, sizeof( cmaxs ));

			memcpy( resbuffer, r->rguc_reserved, 32 );
			ft = resbuffer[0];

			switch( ft )
			{
			case force_model_samebounds:
				memcpy( mins, &resbuffer[0x01], sizeof( mins ));
				memcpy( maxs, &resbuffer[0x0D], sizeof( maxs ));

				if( !VectorCompare( cmins, mins ) || !VectorCompare( cmaxs, maxs ))
					badresindex = idx + 1;
				break;
			case force_model_specifybounds:
				memcpy( mins, &resbuffer[0x01], sizeof( mins ));
				memcpy( maxs, &resbuffer[0x0D], sizeof( maxs ));

				for( i = 0; i < 3; i++ )
				{
					if( cmins[i] < mins[i] || cmaxs[i] > maxs[i] )
					{
						badresindex = idx + 1;
						break;
					}
				}
				break;
			default:
				invalid_type = true;
				break;
			}
		}

		if( invalid_type )
			break;
		c++;
	}

	if( sv.num_consistency != c )
	{
		Con_Printf( S_WARN "%s:%s sent bad file data\n", cl->name, NET_AdrToString( cl->netchan.remote_address ));
		SV_DropClient( cl, false );
		return;
	}

	if( badresindex != 0 )
	{
		char	dropmessage[256];

		dropmessage[0] = 0;
		if( svgame.dllFuncs.pfnInconsistentFile( cl->edict, sv.resources[badresindex - 1].szFileName, dropmessage ))
		{
			if( COM_CheckString( dropmessage ))
				SV_ClientPrintf( cl, "%s", dropmessage );
			SV_DropClient( cl, false );
		}
	}
	else
	{
		ClearBits( cl->flags, FCL_FORCE_UNMODIFIED );
	}
}

void SV_TransferConsistencyInfo( void )
{
	vec3_t		mins, maxs;
	int		i, total = 0;
	resource_t	*pResource;
	string		filepath;
	consistency_t	*pc;

	for( i = 0; i < sv.num_resources; i++ )
	{
		pResource = &sv.resources[i];

		if( FBitSet( pResource->ucFlags, RES_CHECKFILE ))
			continue;	// already checked?

		if( !SV_FileInConsistencyList( pResource->szFileName, &pc ))
			continue;

		SetBits( pResource->ucFlags, RES_CHECKFILE );

		if( pResource->type == t_sound )
			Q_snprintf( filepath, sizeof( filepath ), DEFAULT_SOUNDPATH "%s", pResource->szFileName );
		else Q_strncpy( filepath, pResource->szFileName, sizeof( filepath ));

		MD5_HashFile( pResource->rgucMD5_hash, filepath, NULL );

		if( pResource->type == t_model )
		{
			switch( pc->check_type )
			{
			case force_exactfile:
				// only MD5 hash compare
				break;
			case force_model_samebounds:
				if( !Mod_GetStudioBounds( filepath, mins, maxs ))
					Host_Error( "%s: couldn't get bounds for %s\n", __func__, filepath );
				memcpy( &pResource->rguc_reserved[0x01], mins, sizeof( mins ));
				memcpy( &pResource->rguc_reserved[0x0D], maxs, sizeof( maxs ));
				pResource->rguc_reserved[0] = pc->check_type;
				break;
			case force_model_specifybounds:
				memcpy( &pResource->rguc_reserved[0x01], pc->mins, sizeof( pc->mins ));
				memcpy( &pResource->rguc_reserved[0x0D], pc->maxs, sizeof( pc->maxs ));
				pResource->rguc_reserved[0] = pc->check_type;
				break;
			}
		}
		total++;
	}

	sv.num_consistency = total;
}

static void SV_SendConsistencyList( sv_client_t *cl, sizebuf_t *msg )
{
	int	i, lastcheck;
	int	delta;

	if( svs.maxclients == 1 || !sv_consistency.value || !sv.num_consistency || FBitSet( cl->flags, FCL_HLTV_PROXY ))
	{
		ClearBits( cl->flags, FCL_FORCE_UNMODIFIED );
		MSG_WriteOneBit( msg, 0 );
		return;
	}

	SetBits( cl->flags, FCL_FORCE_UNMODIFIED );
	MSG_WriteOneBit( msg, 1 );
	lastcheck = 0;

	for( i = 0; i < sv.num_resources; i++ )
	{
		if( !FBitSet( sv.resources[i].ucFlags, RES_CHECKFILE ))
			continue;

		delta = i - lastcheck;
		MSG_WriteOneBit( msg, 1 );

		if( delta > 31 )
		{
			MSG_WriteOneBit( msg, 0 );
			MSG_WriteUBitLong( msg, i, MAX_MODEL_BITS );
		}
		else
		{
			MSG_WriteOneBit( msg, 1 );
			MSG_WriteUBitLong( msg, delta, 5 );
		}

		lastcheck = i;
	}

	// write end of the list
	MSG_WriteOneBit( msg, 0 );
}

static qboolean SV_CheckFile( sizebuf_t *msg, const char *filename )
{
	resource_t	p;

	memset( &p, 0, sizeof( resource_t ));

	if( Q_strlen( filename ) == 36 && !Q_strnicmp( filename, "!MD5", 4 ))
	{
		COM_HexConvert( filename + 4, 32, p.rgucMD5_hash );

		if( HPAK_GetDataPointer( hpk_custom_file.string, &p, NULL, NULL ))
			return true;
	}

	if( !sv_allow_upload.value )
		return true;

	MSG_BeginServerCmd( msg, svc_stufftext );
	MSG_WriteStringf( msg, "upload \"!MD5%s\"\n", MD5_Print( p.rgucMD5_hash ));

	return false;
}

void SV_MoveToOnHandList( sv_client_t *cl, resource_t *pResource )
{
	if( !pResource )
	{
		Con_Reportf( "Null resource passed to SV_MoveToOnHandList\n" );
		return;
	}

	SV_RemoveFromResourceList( pResource );
	SV_AddToResourceList( pResource, &cl->resourcesonhand );
}

void SV_AddToResourceList( resource_t *pResource, resource_t *pList )
{
	if( pResource->pPrev != NULL || pResource->pNext != NULL )
	{
		Con_Reportf( S_ERROR "Resource already linked\n" );
		return;
	}

	pResource->pPrev = pList->pPrev;
	pResource->pNext = pList;
	pList->pPrev->pNext = pResource;
	pList->pPrev = pResource;
}

static void SV_SendCustomization( sv_client_t *cl, int playernum, resource_t *pResource )
{
	MSG_BeginServerCmd( &cl->netchan.message, svc_customization );
	MSG_WriteByte( &cl->netchan.message, playernum );	// playernum
	MSG_WriteByte( &cl->netchan.message, pResource->type );
	MSG_WriteString( &cl->netchan.message, pResource->szFileName );
	MSG_WriteShort( &cl->netchan.message, pResource->nIndex );
	MSG_WriteLong( &cl->netchan.message, pResource->nDownloadSize );
	MSG_WriteByte( &cl->netchan.message, pResource->ucFlags );

	if( FBitSet( pResource->ucFlags, RES_CUSTOM ))
		MSG_WriteBytes( &cl->netchan.message, pResource->rgucMD5_hash, 16 );
}

void SV_RemoveFromResourceList( resource_t *pResource )
{
	pResource->pPrev->pNext = pResource->pNext;
	pResource->pNext->pPrev = pResource->pPrev;
	pResource->pPrev = NULL;
	pResource->pNext = NULL;
}

void SV_ClearResourceList( resource_t *pList )
{
	resource_t *p;
	resource_t *n;

	for( p = pList->pNext; pList != p && p; p = n )
	{
		n = p->pNext;

		SV_RemoveFromResourceList( p );
		Mem_Free( p );
	}

	pList->pPrev = pList;
	pList->pNext = pList;
}

void SV_ClearResourceLists( sv_client_t *cl )
{
	SV_ClearResourceList( &cl->resourcesneeded );
	SV_ClearResourceList( &cl->resourcesonhand );
}

int SV_EstimateNeededResources( sv_client_t *cl )
{
	int		missing = 0;
	int		size = 0;
	resource_t	*p;

	for( p = cl->resourcesneeded.pNext; p != &cl->resourcesneeded; p = p->pNext )
	{
		if( p->type != t_decal )
			continue;

		if( !HPAK_ResourceForHash( hpk_custom_file.string, p->rgucMD5_hash, NULL ))
		{
			if( p->nDownloadSize != 0 )
			{
				SetBits( p->ucFlags, RES_WASMISSING );
				size += p->nDownloadSize;
			}
			else
			{
				missing++;
			}
		}
	}

	return size;
}

static void SV_Customization( sv_client_t *pClient, resource_t *pResource, qboolean bSkipPlayer )
{
	int		i, nPlayerNumber = -1;
	sv_client_t	*cl;

	i = pClient - svs.clients;
	if( i >= 0 && i < svs.maxclients )
		nPlayerNumber = i;
	else Host_Error( "Couldn't find player index for customization.\n" );

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state != cs_spawned )
			continue;

		if( FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		if( cl == pClient && bSkipPlayer )
			continue;

		SV_SendCustomization( cl, nPlayerNumber, pResource );
	}
}

static void SV_PropagateCustomizations( sv_client_t *pHost )
{
	customization_t	*pCust;
	resource_t	*pResource;
	sv_client_t	*cl;
	int		i;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state != cs_spawned )
			continue;

		if( FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		for( pCust = cl->customdata.pNext; pCust != NULL; pCust = pCust->pNext )
		{
			if( !pCust->bInUse ) continue;
			pResource = &pCust->resource;
			SV_SendCustomization( pHost, i, pResource );
		}
	}
}

static void SV_RegisterResources( sv_client_t *pHost )
{
	resource_t	*pResource;

	for( pResource = pHost->resourcesonhand.pNext; pResource != &pHost->resourcesonhand; pResource = pResource->pNext )
	{
		SV_CreateCustomizationList( pHost );
		SV_Customization( pHost, pResource, true );
	}
}

static qboolean SV_UploadComplete( sv_client_t *cl )
{
	if( &cl->resourcesneeded != cl->resourcesneeded.pNext )
		return false;

	SV_RegisterResources( cl );
	SV_PropagateCustomizations( cl );

	if( sv_allow_upload.value )
		Con_Printf( "Custom resource propagation complete.\n" );
	cl->upstate = us_complete;

	return true;
}

void SV_RequestMissingResources( void )
{
	sv_client_t	*cl;
	int		i;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state != cs_spawned )
			continue;

		if( cl->upstate == us_processing )
			SV_UploadComplete( cl );
	}
}

void SV_BatchUploadRequest( sv_client_t *cl )
{
	string		filename;
	resource_t	*p, *n;

	for( p = cl->resourcesneeded.pNext; p != &cl->resourcesneeded; p = n )
	{
		n = p->pNext;

		if( !FBitSet( p->ucFlags, RES_WASMISSING ))
		{
			SV_MoveToOnHandList( cl, p );
			continue;
		}

		if( p->type == t_decal )
		{
			if( FBitSet( p->ucFlags, RES_CUSTOM ))
			{
				Q_snprintf( filename, sizeof( filename ), "!MD5%s", MD5_Print( p->rgucMD5_hash ));

				if( SV_CheckFile( &cl->netchan.message, filename ))
					SV_MoveToOnHandList( cl, p );
			}
			else
			{
				Con_Reportf( S_ERROR "Non customization in upload queue!\n" );
				SV_MoveToOnHandList( cl, p );
			}
		}
	}
}

void SV_SendResource( resource_t *pResource, sizebuf_t *msg )
{
	static byte	nullrguc[sizeof( pResource->rguc_reserved )];

	MSG_WriteUBitLong( msg, pResource->type, 4 );
	MSG_WriteString( msg, pResource->szFileName );
	MSG_WriteUBitLong( msg, pResource->nIndex, MAX_MODEL_BITS );
	MSG_WriteSBitLong( msg, pResource->nDownloadSize, 24 ); // prevent to download a very big files?
	MSG_WriteUBitLong( msg, pResource->ucFlags & ( RES_FATALIFMISSING|RES_WASMISSING ), 3 );

	if( FBitSet( pResource->ucFlags, RES_CUSTOM ))
		MSG_WriteBytes( msg, pResource->rgucMD5_hash, sizeof( pResource->rgucMD5_hash ));

	if( memcmp( nullrguc, pResource->rguc_reserved, sizeof( nullrguc )))
	{
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteBytes( msg, pResource->rguc_reserved, sizeof( pResource->rguc_reserved ));
	}
	else MSG_WriteOneBit( msg, 0 );
}

void SV_SendResources( sv_client_t *cl, sizebuf_t *msg )
{
	int	i;

	MSG_BeginServerCmd( msg, svc_resourcerequest );
	MSG_WriteLong( msg, svs.spawncount );
	MSG_WriteLong( msg, 0 );

	if( COM_CheckString( sv_downloadurl.string ) && Q_strlen( sv_downloadurl.string ) < 256 )
	{
		MSG_BeginServerCmd( msg, svc_resourcelocation );
		MSG_WriteString( msg, sv_downloadurl.string );
	}

	MSG_BeginServerCmd( msg, svc_resourcelist );
	MSG_WriteUBitLong( msg, sv.num_resources, MAX_RESOURCE_BITS );

	for( i = 0; i < sv.num_resources; i++ )
	{
		SV_SendResource( &sv.resources[i], msg );
	}

	SV_SendConsistencyList( cl, msg );
}
