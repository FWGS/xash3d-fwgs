/*
cl_parse.c - parse a message received from the server
Copyright (C) 2008 Uncle Mike

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
#include "particledef.h"
#include "cl_tent.h"
#include "shake.h"
#include "hltv.h"
#include "input.h"
#if XASH_LOW_MEMORY != 2
int CL_UPDATE_BACKUP = SINGLEPLAYER_BACKUP;
#endif
/*
===============
CL_UserMsgStub

Default stub for missed callbacks
===============
*/
static int CL_UserMsgStub( const char *pszName, int iSize, void *pbuf )
{
	return 1;
}

/*
==================
CL_ParseViewEntity

==================
*/
void CL_ParseViewEntity( sizebuf_t *msg )
{
	cl.viewentity = MSG_ReadWord( msg );

	// check entity bounds in case we want
	// to use this directly in clgame.entities[] array
	cl.viewentity = bound( 0, cl.viewentity, clgame.maxEntities - 1 );
}

/*
==================
CL_ParseSoundPacket

==================
*/
static void CL_ParseSoundPacket( sizebuf_t *msg )
{
	vec3_t	pos;
	int 	chan, sound;
	float 	volume, attn;
	int	flags, pitch, entnum;
	sound_t	handle = 0;

	flags = MSG_ReadUBitLong( msg, MAX_SND_FLAGS_BITS );
	sound = MSG_ReadUBitLong( msg, MAX_SOUND_BITS );
	chan = MSG_ReadUBitLong( msg, MAX_SND_CHAN_BITS );

	if( FBitSet( flags, SND_VOLUME ))
		volume = (float)MSG_ReadByte( msg ) / 255.0f;
	else volume = VOL_NORM;

	if( FBitSet( flags, SND_ATTENUATION ))
		attn = (float)MSG_ReadByte( msg ) / 64.0f;
	else attn = ATTN_NONE;

	if( FBitSet( flags, SND_PITCH ))
		pitch = MSG_ReadByte( msg );
	else pitch = PITCH_NORM;

	// entity reletive
	entnum = MSG_ReadUBitLong( msg, MAX_ENTITY_BITS );

	// positioned in space
	MSG_ReadVec3Coord( msg, pos );

	if( FBitSet( flags, SND_SENTENCE ))
	{
		char	sentenceName[32];

		if( FBitSet( flags, SND_SEQUENCE ))
			Q_snprintf( sentenceName, sizeof( sentenceName ), "!#%i", sound + MAX_SOUNDS_NONSENTENCE );
		else Q_snprintf( sentenceName, sizeof( sentenceName ), "!%i", sound );

		handle = S_RegisterSound( sentenceName );
	}
	else handle = cl.sound_index[sound];	// see precached sound

	if( !cl.audio_prepped )
		return; // too early

	// g-cont. sound and ambient sound have only difference with channel
	if( chan == CHAN_STATIC )
	{
		S_AmbientSound( pos, entnum, handle, volume, attn, pitch, flags );
	}
	else
	{
		S_StartSound( pos, entnum, chan, handle, volume, attn, pitch, flags );
	}
}

/*
==================
CL_ParseRestoreSoundPacket

==================
*/
void CL_ParseRestoreSoundPacket( sizebuf_t *msg )
{
	vec3_t	pos;
	int 	chan, sound;
	float 	volume, attn;
	int	flags, pitch, entnum;
	double	samplePos, forcedEnd;
	int	wordIndex;
	sound_t	handle = 0;

	flags = MSG_ReadUBitLong( msg, MAX_SND_FLAGS_BITS );
	sound = MSG_ReadUBitLong( msg, MAX_SOUND_BITS );
	chan = MSG_ReadUBitLong( msg, MAX_SND_CHAN_BITS );

	if( flags & SND_VOLUME )
		volume = (float)MSG_ReadByte( msg ) / 255.0f;
	else volume = VOL_NORM;

	if( flags & SND_ATTENUATION )
		attn = (float)MSG_ReadByte( msg ) / 64.0f;
	else attn = ATTN_NONE;

	if( flags & SND_PITCH )
		pitch = MSG_ReadByte( msg );
	else pitch = PITCH_NORM;

	// entity reletive
	entnum = MSG_ReadUBitLong( msg, MAX_ENTITY_BITS );

	// positioned in space
	MSG_ReadVec3Coord( msg, pos );

	if( flags & SND_SENTENCE )
	{
		char	sentenceName[32];

		if( flags & SND_SEQUENCE )
			Q_snprintf( sentenceName, sizeof( sentenceName ), "!#%i", sound + MAX_SOUNDS_NONSENTENCE );
		else Q_snprintf( sentenceName, sizeof( sentenceName ), "!%i", sound );

		handle = S_RegisterSound( sentenceName );
	}
	else handle = cl.sound_index[sound]; // see precached sound

	wordIndex = MSG_ReadByte( msg );

	// 16 bytes here
	MSG_ReadBytes( msg, &samplePos, sizeof( samplePos ));
	MSG_ReadBytes( msg, &forcedEnd, sizeof( forcedEnd ));

	if( !cl.audio_prepped )
		return; // too early

	S_RestoreSound( pos, entnum, chan, handle, volume, attn, pitch, flags, samplePos, forcedEnd, wordIndex );
}

/*
==================
CL_ParseServerTime

==================
*/
void CL_ParseServerTime( sizebuf_t *msg, connprotocol_t proto )
{
	double	dt;

	cl.mtime[1] = cl.mtime[0];
	cl.mtime[0] = MSG_ReadFloat( msg );

	if( proto == PROTO_QUAKE )
		return; // don't mess the time

	if( cl.maxclients == 1 )
		cl.time = cl.mtime[0];

	dt = cl.time - cl.mtime[0];

	if( fabs( dt ) > cl_clockreset.value )	// 0.1 by default
	{
		cl.time = cl.mtime[0];
		cl.timedelta = 0.0f;
	}
	else if( dt != 0.0 )
	{
		cl.timedelta = dt;
	}

	if( cl.oldtime > cl.time )
		cl.oldtime = cl.time;
}

/*
==================
CL_ParseSignon

==================
*/
void CL_ParseSignon( sizebuf_t *msg, connprotocol_t proto )
{
	int	i = MSG_ReadByte( msg );

	if( i <= cls.signon )
	{
		Con_Reportf( S_ERROR "received signon %i when at %i\n", i, cls.signon );
		CL_Disconnect();
		return;
	}

	cls.signon = i;
	CL_SignonReply( proto );
}

/*
==================
CL_ParseMovevars

==================
*/
void CL_ParseMovevars( sizebuf_t *msg )
{
	Delta_InitClient ();	// finalize client delta's

	MSG_ReadDeltaMovevars( msg, &clgame.oldmovevars, &clgame.movevars );

	// water alpha is not allowed
	if( !FBitSet( world.flags, FWORLD_WATERALPHA ))
		clgame.movevars.wateralpha = 1.0f;

	// update sky if changed
	if( Q_strcmp( clgame.oldmovevars.skyName, clgame.movevars.skyName ) && cl.video_prepped )
		R_SetupSky( clgame.movevars.skyName );

	clgame.oldmovevars = clgame.movevars;
	clgame.entities->curstate.scale = clgame.movevars.waveHeight;

	// keep features an actual!
	clgame.oldmovevars.features = clgame.movevars.features = host.features;
}

/*
==================
CL_ParseParticles

==================
*/
void CL_ParseParticles( sizebuf_t *msg, connprotocol_t proto )
{
	vec3_t		org, dir;
	int		i, count, color;
	float		life;

	MSG_ReadVec3Coord( msg, org );

	for( i = 0; i < 3; i++ )
		dir[i] = MSG_ReadChar( msg ) * 0.0625f;

	count = MSG_ReadByte( msg );
	color = MSG_ReadByte( msg );
	if( count == 255 )
		count = 1024;

	if( proto == PROTO_GOLDSRC )
		life = 0.0f;
	else life = MSG_ReadByte( msg ) * 0.125f;

	if( life != 0.0f && count == 1 )
	{
		particle_t	*p;

		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die += life;
		p->color = color;
		p->type = pt_static;

		VectorCopy( org, p->org );
		VectorCopy( dir, p->vel );
	}
	else R_RunParticleEffect( org, dir, color, count );
}

/*
==================
CL_ParseStaticEntity

static client entity
==================
*/
static void CL_ParseStaticEntity( sizebuf_t *msg )
{
	int		i, newnum;
	const entity_state_t from = { 0 };
	entity_state_t to;
	cl_entity_t	*ent;

	if( !clgame.static_entities )
		clgame.static_entities = Mem_Calloc( clgame.mempool, sizeof( cl_entity_t ) * MAX_STATIC_ENTITIES );

	newnum = MSG_ReadUBitLong( msg, MAX_ENTITY_BITS );
	MSG_ReadDeltaEntity( msg, &from, &to, 0, DELTA_STATIC, cl.mtime[0] );

	i = clgame.numStatics;
	if( i >= MAX_STATIC_ENTITIES )
	{
		Con_Printf( S_ERROR "MAX_STATIC_ENTITIES limit exceeded!\n" );
		return;
	}

	ent = &clgame.static_entities[i];
	clgame.numStatics++;

	// all states are same
	ent->baseline = ent->curstate = ent->prevstate = to;
	ent->index = 0; // static entities doesn't has the numbers

	// statics may be respawned in game e.g. for demo recording
	if( cls.state == ca_connected || cls.state == ca_validate )
		ent->trivial_accept = INVALID_HANDLE;

	// setup the new static entity
	VectorCopy( ent->curstate.origin, ent->origin );
	VectorCopy( ent->curstate.angles, ent->angles );
	ent->model = CL_ModelHandle( to.modelindex );
	ent->curstate.framerate = 1.0f;
	CL_ResetLatchedVars( ent, true );

	if( ent->curstate.rendermode == kRenderNormal && ent->model != NULL )
	{
		// auto 'solid' faces
		if( FBitSet( ent->model->flags, MODEL_TRANSPARENT ) && Host_IsQuakeCompatible( ))
		{
			ent->curstate.rendermode = kRenderTransAlpha;
			ent->curstate.renderamt = 255;
		}
	}

	R_AddEfrags( ent );	// add link
}


/*
==================
CL_WeaponAnim

Set new weapon animation
==================
*/
void GAME_EXPORT CL_WeaponAnim( int iAnim, int body )
{
	cl_entity_t	*view = &clgame.viewent;

	cl.local.weaponstarttime = 0.0f;
	cl.local.weaponsequence = iAnim;
	view->curstate.framerate = 1.0f;
	view->curstate.body = body;

#if 0	// g-cont. for GlowShell testing
	view->curstate.renderfx = kRenderFxGlowShell;
	view->curstate.rendercolor.r = 255;
	view->curstate.rendercolor.g = 128;
	view->curstate.rendercolor.b = 0;
	view->curstate.renderamt = 150;
#endif
}

/*
==================
CL_ParseStaticDecal

==================
*/
void CL_ParseStaticDecal( sizebuf_t *msg )
{
	vec3_t		origin;
	int		decalIndex, entityIndex, modelIndex;
	float		scale;
	int		flags;

	MSG_ReadVec3Coord( msg, origin );
	decalIndex = MSG_ReadWord( msg );
	entityIndex = MSG_ReadShort( msg );

	if( entityIndex > 0 )
		modelIndex = MSG_ReadWord( msg );
	else modelIndex = 0;
	flags = MSG_ReadByte( msg );
	scale = (float)MSG_ReadWord( msg ) / 4096.0f;

	CL_FireCustomDecal( CL_DecalIndex( decalIndex ), entityIndex, modelIndex, origin, flags, scale );
}

/*
==================
CL_ParseSoundFade

==================
*/
void CL_ParseSoundFade( sizebuf_t *msg )
{
	float	fadePercent, fadeOutSeconds;
	float	holdTime, fadeInSeconds;

	fadePercent = (float)MSG_ReadByte( msg );
	holdTime = (float)MSG_ReadByte( msg );
	fadeOutSeconds = (float)MSG_ReadByte( msg );
	fadeInSeconds = (float)MSG_ReadByte( msg );

	S_FadeClientVolume( fadePercent, fadeOutSeconds, holdTime, fadeInSeconds );
}

/*
==================
CL_RequestMissingResources

==================
*/
qboolean CL_RequestMissingResources( void )
{
	resource_t	*p;

	if( !cls.dl.doneregistering && ( cls.dl.custom || cls.state == ca_validate ))
	{
		p = cl.resourcesneeded.pNext;

		if( p == &cl.resourcesneeded )
		{
			cls.dl.doneregistering = true;
			host.downloadcount = 0;
			cls.dl.custom = false;
		}
		else if( !FBitSet( p->ucFlags, RES_WASMISSING ))
		{
			CL_MoveToOnHandList( cl.resourcesneeded.pNext );
			return true;
		}
	}
	return false;
}

void CL_BatchResourceRequest( qboolean initialize )
{
	byte		data[MAX_INIT_MSG];
	resource_t	*p, *n;
	sizebuf_t		msg;
	qboolean		done_downloading = true;

	MSG_Init( &msg, "Resource Batch", data, sizeof( data ));

	// client resources is not precached by server
	if( initialize ) CL_AddClientResources();

	for( p = cl.resourcesneeded.pNext; p && p != &cl.resourcesneeded; p = n )
	{
		n = p->pNext;

		if( !FBitSet( p->ucFlags, RES_WASMISSING ))
		{
			CL_MoveToOnHandList( p );
			continue;
		}

		if( cls.state == ca_active && !cl_download_ingame.value )
		{
			Con_Printf( "skipping in game download of %s\n", p->szFileName );
			CL_MoveToOnHandList( p );
			continue;
		}

		switch( p->type )
		{
		case t_sound:
		case t_model:
		case t_eventscript:
			if( !CL_CheckFile( &msg, p ))
			{
				done_downloading = false;
				break;
			}
			CL_MoveToOnHandList( p );
			break;
		case t_skin:
			CL_MoveToOnHandList( p );
			break;
		case t_decal:
			if( !HPAK_GetDataPointer( hpk_custom_file.string, p, NULL, NULL ))
			{
				if( !FBitSet( p->ucFlags, RES_REQUESTED ))
				{
					MSG_BeginClientCmd( &msg, clc_stringcmd );
					MSG_WriteStringf( &msg, "dlfile !MD5%s", MD5_Print( p->rgucMD5_hash ));;
					SetBits( p->ucFlags, RES_REQUESTED );
					done_downloading = false;
				}
				break;
			}
			CL_MoveToOnHandList( p );
			break;
		case t_generic:
			if( !COM_IsSafeFileToDownload( p->szFileName ))
			{
				CL_RemoveFromResourceList( p );
				Mem_Free( p );
				break;
			}
			if( !CL_CheckFile( &msg, p ))
				break;
			CL_MoveToOnHandList( p );
			break;
		case t_world:
			ASSERT( 0 );
			break;
		}
	}

	if( cls.state != ca_disconnected )
	{
		if( done_downloading && CL_PrecacheResources( ))
		{
			CL_RegisterResources( &msg, cls.legacymode );
		}

		Netchan_CreateFragments( &cls.netchan, &msg );
		Netchan_FragSend( &cls.netchan );
	}
}

int CL_EstimateNeededResources( void )
{
	resource_t	*p;
	int		nTotalSize = 0;

	for( p = cl.resourcesneeded.pNext; p != &cl.resourcesneeded; p = p->pNext )
	{
		switch( p->type )
		{
		case t_sound:
			if( p->szFileName[0] != '*' && !FS_FileExists( va( DEFAULT_SOUNDPATH "%s", p->szFileName ), false ) )
			{
				SetBits( p->ucFlags, RES_WASMISSING );
				nTotalSize += p->nDownloadSize;
			}
			break;
		case t_model:
			if( p->szFileName[0] != '*' && !FS_FileExists( p->szFileName, false ) )
			{
				SetBits( p->ucFlags, RES_WASMISSING );
				nTotalSize += p->nDownloadSize;
			}
			break;
		case t_skin:
		case t_generic:
		case t_eventscript:
			if( !FS_FileExists( p->szFileName, false ) )
			{
				SetBits( p->ucFlags, RES_WASMISSING );
				nTotalSize += p->nDownloadSize;
			}
			break;
		case t_decal:
			if( FBitSet( p->ucFlags, RES_CUSTOM ))
			{
				SetBits( p->ucFlags, RES_WASMISSING );
				nTotalSize += p->nDownloadSize;
			}
			break;
		case t_world:
			ASSERT( 0 );
			break;
		}
	}

	return nTotalSize;
}

static void CL_StartResourceDownloading( const char *pszMessage, qboolean bCustom )
{
	resourceinfo_t	ri;

	if( COM_CheckString( pszMessage ))
		Con_DPrintf( "%s", pszMessage );

	cls.dl.nTotalSize = COM_SizeofResourceList( &cl.resourcesneeded, &ri );
	cls.dl.nTotalToTransfer = CL_EstimateNeededResources();

	if( bCustom )
	{
		cls.dl.custom = true;
	}
	else
	{
		HTTP_ResetProcessState();

		cls.state = ca_validate;
		cls.dl.custom = false;
	}

	cls.dl.doneregistering = false;
	cls.dl.fLastStatusUpdate = host.realtime;
	cls.dl.nRemainingToTransfer = cls.dl.nTotalToTransfer;
	memset( cls.dl.rgStats, 0, sizeof( cls.dl.rgStats ));
	cls.dl.nCurStat = 0;

	CL_BatchResourceRequest( !bCustom );
}

static customization_t *CL_PlayerHasCustomization( int nPlayerNum, resourcetype_t type )
{
	customization_t	*pList;

	for( pList = cl.players[nPlayerNum].customdata.pNext; pList; pList = pList->pNext )
	{
		if( pList->resource.type == type )
			return pList;
	}
	return NULL;
}

static void CL_RemoveCustomization( int nPlayerNum, customization_t *pRemove )
{
	customization_t	*pList;
	customization_t	*pNext;

	for( pList = cl.players[nPlayerNum].customdata.pNext; pList; pList = pNext )
	{
		pNext = pList->pNext;

		if( pRemove != pList )
			continue;

		if( pList->bInUse && pList->pBuffer )
			Mem_Free( pList->pBuffer );

		if( pList->bInUse && pList->pInfo )
		{
			if( pList->resource.type == t_decal )
			{
				if( cls.state == ca_active )
					ref.dllFuncs.R_DecalRemoveAll( pList->nUserData1 );
				FS_FreeImage( pList->pInfo );
			}
		}

		cl.players[nPlayerNum].customdata.pNext = pNext;
		Mem_Free( pList );
		break;
	}
}

/*
==================
CL_ParseCustomization

==================
*/
void CL_ParseCustomization( sizebuf_t *msg )
{
	customization_t	*pExistingCustomization;
	customization_t	*pList;
	qboolean		bFound;
	resource_t	*pRes;
	int		i;

	i = MSG_ReadByte( msg );
	if( i >= MAX_CLIENTS )
		Host_Error( "Bogus player index during customization parsing.\n" );

	pRes = Mem_Calloc( cls.mempool, sizeof( resource_t ));
	pRes->type = MSG_ReadByte( msg );

	Q_strncpy( pRes->szFileName, MSG_ReadString( msg ), sizeof( pRes->szFileName ));
	pRes->nIndex = MSG_ReadShort( msg );
	pRes->nDownloadSize = MSG_ReadLong( msg );
	pRes->ucFlags = MSG_ReadByte( msg ) & ~RES_WASMISSING;
	pRes->pNext = pRes->pPrev = NULL;

	if( FBitSet( pRes->ucFlags, RES_CUSTOM ))
		MSG_ReadBytes( msg, pRes->rgucMD5_hash, 16 );
	pRes->playernum = i;

	if( !cl_allow_download.value )
	{
		Con_DPrintf( "Refusing new resource, cl_allowdownload set to 0\n" );
		Mem_Free( pRes );
		return;
	}

	if( cls.state == ca_active && !cl_download_ingame.value )
	{
		Con_DPrintf( "Refusing new resource, cl_download_ingame set to 0\n" );
		Mem_Free( pRes );
		return;
	}

	pExistingCustomization = CL_PlayerHasCustomization( i, pRes->type );

	if( pExistingCustomization )
		CL_RemoveCustomization( i, pExistingCustomization );
	bFound = false;

	for( pList = cl.players[pRes->playernum].customdata.pNext; pList; pList = pList->pNext )
	{
		if( !memcmp( pList->resource.rgucMD5_hash, pRes->rgucMD5_hash, 16 ))
		{
			bFound = true;
			break;
		}
	}

	if( HPAK_GetDataPointer( hpk_custom_file.string, pRes, NULL, NULL ))
	{
		qboolean	bError = false;

		if( !bFound )
		{
			pList = &cl.players[pRes->playernum].customdata;

			if( !COM_CreateCustomization( pList, pRes, pRes->playernum, FCUST_FROMHPAK, NULL, NULL ))
				bError = true;
		}
		else
		{
			Con_DPrintf( "Duplicate resource ignored for local client\n" );
		}

		if( bError ) Con_DPrintf( "Error loading customization\n" );
		Mem_Free( pRes );
	}
	else
	{
		SetBits( pRes->ucFlags, RES_WASMISSING );
		CL_AddToResourceList( pRes, &cl.resourcesneeded );
		Con_Printf( "Requesting %s from server\n", pRes->szFileName );
		CL_StartResourceDownloading( "Custom resource propagation...\n", true );
	}
}

/*
==================
CL_ParseResourceRequest

==================
*/
void CL_ParseResourceRequest( sizebuf_t *msg )
{
	byte	buffer[MAX_INIT_MSG];
	int	i, arg, nStartIndex;
	sizebuf_t	sbuf;

	MSG_Init( &sbuf, "ResourceBlock", buffer, sizeof( buffer ));

	arg = MSG_ReadLong( msg );
	nStartIndex = MSG_ReadLong( msg );

	if( cl.servercount != arg )
		return;

	if( nStartIndex < 0 && nStartIndex > cl.num_resources )
		return;

	MSG_BeginClientCmd( &sbuf, clc_resourcelist );
	MSG_WriteShort( &sbuf, cl.num_resources );

	for( i = nStartIndex; i < cl.num_resources; i++ )
	{
		MSG_WriteString( &sbuf, cl.resourcelist[i].szFileName );
		MSG_WriteByte( &sbuf, cl.resourcelist[i].type );
		MSG_WriteShort( &sbuf, cl.resourcelist[i].nIndex );
		MSG_WriteLong( &sbuf, cl.resourcelist[i].nDownloadSize );
		MSG_WriteByte( &sbuf, cl.resourcelist[i].ucFlags );

		if( FBitSet( cl.resourcelist[i].ucFlags, RES_CUSTOM ))
			MSG_WriteBytes( &sbuf, cl.resourcelist[i].rgucMD5_hash, 16 );
	}

	// a1ba: useless check? MSG_BeginClientCmd and MSG_WriteShort will always
	// write to the buffer
	// if( MSG_GetNumBytesWritten( &sbuf ) > 0 )
	{
		Netchan_CreateFragments( &cls.netchan, &sbuf );
		Netchan_FragSend( &cls.netchan );
	}
}

/*
==================
CL_CreateCustomizationList

loading custom decal for self
==================
*/
static void CL_CreateCustomizationList( void )
{
	resource_t	*pResource;
	player_info_t	*pPlayer;
	int		i;

	pPlayer = &cl.players[cl.playernum];
	pPlayer->customdata.pNext = NULL;

	for( i = 0; i < cl.num_resources; i++ )
	{
		pResource = &cl.resourcelist[i];

		if( !COM_CreateCustomization( &pPlayer->customdata, pResource, cl.playernum, 0, NULL, NULL ))
			Con_Printf( "problem with client customization %s, ignoring...", pResource->szFileName );
	}
}

/*
==================
CL_ParseFileTransferFailed

==================
*/
void CL_ParseFileTransferFailed( sizebuf_t *msg )
{
	const char	*name = MSG_ReadString( msg );

	if( !cls.demoplayback )
		CL_ProcessFile( false, name );
}

/*
=====================================================================

  SERVER CONNECTING MESSAGES

=====================================================================
*/
/*
==================
CL_ParseServerData
==================
*/
void CL_ParseServerData( sizebuf_t *msg, connprotocol_t proto )
{
	char	gamefolder[MAX_QPATH];
	string	mapfile;
	qboolean	background;
	int	i, required_version;
	uint32_t	mapCRC;

	HPAK_CheckSize( hpk_custom_file.string );

	switch( proto )
	{
	case PROTO_LEGACY:
		required_version = PROTOCOL_LEGACY_VERSION;
		Con_Reportf( "Legacy serverdata packet received.\n" );
		break;
	case PROTO_GOLDSRC:
		required_version = PROTOCOL_GOLDSRC_VERSION;
		Con_Reportf( "GoldSrc serverdata packet received.\n" );
		break;
	default:
		required_version = PROTOCOL_VERSION;
		Con_Reportf( "Serverdata packet received.\n" );
		break;
	}

	cls.timestart = Sys_DoubleTime();
	cls.demowaiting = false;	// server is changed

	// wipe the client_t struct
	if( !cls.changelevel && !cls.changedemo )
		CL_ClearState ();

	// Re-init hud video, especially if we changed game directories
	clgame.dllFuncs.pfnVidInit();

	cls.state = ca_connected;

	// parse protocol version number
	i = MSG_ReadLong( msg );
	if( i != required_version ) // GoldSrc protocol version is 48, same as Xash3D 48
		Host_Error( "Server use invalid protocol (%i should be %i)\n", i, required_version );

	cl.servercount = MSG_ReadLong( msg );
	cl.checksum = MSG_ReadLong( msg );
	if( proto == PROTO_GOLDSRC )
	{
		byte clientdllmd5[16];
		const char *s;

		MSG_ReadBytes( msg, clientdllmd5, sizeof( clientdllmd5 ));
		cl.maxclients = MSG_ReadByte( msg );
		cl.playernum = MSG_ReadByte( msg );
		COM_UnMunge3((byte *)&cl.checksum, sizeof( cl.checksum ), ( 0xff - cl.playernum ) & 0xff );

		MSG_SeekToBit( msg, sizeof( uint8_t ) << 3, SEEK_CUR ); // quake leftover, coop flag

		Q_strncpy( gamefolder, MSG_ReadString( msg ), sizeof( gamefolder ));
		Con_Printf( "Remote host: %s\n", MSG_ReadString( msg ));
		Q_strncpy( clgame.mapname, COM_FileWithoutPath( MSG_ReadString( msg )), sizeof( clgame.mapname ));
		COM_StripExtension( clgame.mapname );

		s = MSG_ReadString( msg );
		if( COM_CheckStringEmpty( s ))
			Con_Printf( "Server map cycle: %s\n", s ); // VALVEWHY?

		if( MSG_ReadByte( msg ))
			Con_Printf( "Uh, server says it's VAC2 secured.\n" );

		background = false;
		clgame.maxEntities = GI->max_edicts + (( cl.maxclients - 1 ) * 15 );
		clgame.maxEntities = bound( MIN_LEGACY_EDICTS, clgame.maxEntities, MAX_GOLDSRC_EDICTS );
		clgame.maxModels = 512; // ???
		Q_strncpy( clgame.maptitle, clgame.mapname, sizeof( clgame.maptitle ));

		Host_ValidateEngineFeatures( 0, 0 );
	}
	else
	{
		uint32_t mask;

		cl.playernum = MSG_ReadByte( msg );
		cl.maxclients = MSG_ReadByte( msg );
		clgame.maxEntities = MSG_ReadWord( msg );
		if( proto == PROTO_LEGACY )
		{
			clgame.maxEntities = bound( MIN_LEGACY_EDICTS, clgame.maxEntities, MAX_LEGACY_EDICTS );
			clgame.maxModels = 512; // ???
			mask = ENGINE_LEGACY_FEATURES_MASK;
		}
		else
		{
			clgame.maxEntities = bound( MIN_EDICTS, clgame.maxEntities, MAX_EDICTS );
			clgame.maxModels = MSG_ReadWord( msg );
			mask = ENGINE_FEATURES_MASK;
		}
		Q_strncpy( clgame.mapname, MSG_ReadString( msg ), sizeof( clgame.mapname ));
		Q_strncpy( clgame.maptitle, MSG_ReadString( msg ), sizeof( clgame.maptitle ));
		background = MSG_ReadOneBit( msg );
		Q_strncpy( gamefolder, MSG_ReadString( msg ), sizeof( gamefolder ));
		Host_ValidateEngineFeatures( mask, MSG_ReadDword( msg ));

		if( proto != PROTO_LEGACY )
		{
			// receive the player hulls
			for( i = 0; i < MAX_MAP_HULLS * 3; i++ )
			{
				host.player_mins[i/3][i%3] = MSG_ReadChar( msg );
				host.player_maxs[i/3][i%3] = MSG_ReadChar( msg );
			}
		}
	}

	Q_snprintf( mapfile, sizeof( mapfile ), "maps/%s.bsp", clgame.mapname );
	if( CRC32_MapFile( &cl.worldmapCRC, mapfile, cl.maxclients > 1 ))
	{
		// validate map checksum
		if( cl.worldmapCRC != cl.checksum )
		{
			Con_Printf( S_ERROR "Your map [%s] differs from the server's.\n", clgame.mapname );
			CL_Disconnect_f(); // for local game, call EndGame
			Host_AbortCurrentFrame(); // to avoid svc_bad
		}
	}

	if( clgame.maxModels > MAX_MODELS )
		Con_Printf( S_WARN "server model limit is above client model limit %i > %i\n", clgame.maxModels, MAX_MODELS );

	if( Con_FixedFont( ))
	{
		// seperate the printfs so the server message can have a color
		Con_Print( "\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n" );
		Con_Print( va( "%c%s\n\n", 2, clgame.maptitle ));
	}

	// multiplayer game?
	if( cl.maxclients > 1 )
	{
		// allow console in multiplayer games
		host.allow_console = true;

		// loading user settings
		CSCR_LoadDefaultCVars( "user.scr" );

		if( r_decals.value > mp_decals.value )
			Cvar_DirectSet( &r_decals, mp_decals.string );
	}
	else Cvar_DirectSet( &r_decals, NULL );

	// set the background state
	if( cls.demoplayback && ( cls.demonum != -1 ))
		cl.background = true;
	else cl.background = background;

	if( cl.background )	// tell the game parts about background state
		Cvar_FullSet( "cl_background", "1", FCVAR_READ_ONLY );
	else Cvar_FullSet( "cl_background", "0", FCVAR_READ_ONLY );

	if( !cls.changelevel )
	{
		// continue playing if we are changing level
		S_StopBackgroundTrack ();
	}

	if( !cls.changedemo )
		UI_SetActiveMenu( cl.background );
	else if( !cls.demoplayback )
		Key_SetKeyDest( key_menu );

	// don't reset cursor in background mode
	if( cl.background )
		IN_MouseRestorePos();

	// will be changed later
	cl.viewentity = cl.playernum + 1;
	gameui.globals->maxClients = cl.maxclients;
	Q_strncpy( gameui.globals->maptitle, clgame.maptitle, sizeof( gameui.globals->maptitle ));

	if( !cls.changelevel && !cls.changedemo )
		CL_InitEdicts( cl.maxclients ); // re-arrange edicts

	// get splash name
	if( cls.demoplayback && ( cls.demonum != -1 ))
		Cvar_Set( "cl_levelshot_name", va( "levelshots/%s_%s", cls.demoname, refState.wideScreen ? "16x9" : "4x3" ));
	else Cvar_Set( "cl_levelshot_name", va( "levelshots/%s_%s", clgame.mapname, refState.wideScreen ? "16x9" : "4x3" ));
	Cvar_SetValue( "scr_loading", 0.0f ); // reset progress bar

	if(( cl_allow_levelshots.value && !cls.changelevel ) || cl.background )
	{
		if( !FS_FileExists( va( "%s.bmp", cl_levelshot_name.string ), true ))
			Cvar_Set( "cl_levelshot_name", "*black" ); // render a black screen
		cls.scrshot_request = scrshot_plaque; // request levelshot even if exist (check filetime)
	}

	for( i = 0; i < MAX_CLIENTS; i++ )
		COM_ClearCustomizationList( &cl.players[i].customdata, true );
	CL_CreateCustomizationList();

	// request resources from server
	if( proto == PROTO_GOLDSRC )
	{
		CL_ServerCommand( true, "sendres" );
	}
	else if( proto != PROTO_LEGACY )
	{
		CL_ServerCommand( true, "sendres %i\n", cl.servercount );
	}

	memset( &clgame.movevars, 0, sizeof( clgame.movevars ));
	memset( &clgame.oldmovevars, 0, sizeof( clgame.oldmovevars ));
	memset( &clgame.centerPrint, 0, sizeof( clgame.centerPrint ));
	cl.video_prepped = false;
	cl.audio_prepped = false;
}

/*
===================
CL_ParseClientData
===================
*/
void CL_ParseClientData( sizebuf_t *msg, connprotocol_t proto )
{
	float		parsecounttime;
	int		i, j, command_ack;
	clientdata_t	*from_cd, *to_cd;
	weapon_data_t	*from_wd, *to_wd;
	weapon_data_t	nullwd[64];
	clientdata_t	nullcd;
	frame_t		*frame;
	int		idx;

	// This is the last movement that the server ack'd
	command_ack = cls.netchan.incoming_acknowledged;

	// this is the frame update that this message corresponds to
	i = cls.netchan.incoming_sequence;

	// did we drop some frames?
	if( i > cl.last_incoming_sequence + 1 )
	{
		// mark as dropped
		for( j = cl.last_incoming_sequence + 1; j < i; j++ )
		{
			if( cl.frames[j & CL_UPDATE_MASK].receivedtime >= 0.0 )
			{
				cl.frames[j & CL_UPDATE_MASK].receivedtime = -1.0f;
				cl.frames[j & CL_UPDATE_MASK].latency = 0;
			}
		}
	}

	cl.parsecount = i;					// ack'd incoming messages.
	cl.parsecountmod = cl.parsecount & CL_UPDATE_MASK;	// index into window.
	frame = &cl.frames[cl.parsecountmod];			// frame at index.

	frame->time = cl.mtime[0];				// mark network received time
	frame->receivedtime = host.realtime;			// time now that we are parsing.

	memset( &frame->graphdata, 0, sizeof( netbandwidthgraph_t ));

	// send time for that frame.
	parsecounttime = cl.commands[command_ack & CL_UPDATE_MASK].senttime;

	// current time that we got a response to the command packet.
	cl.commands[command_ack & CL_UPDATE_MASK].receivedtime = host.realtime;

	if( cl.last_command_ack != -1 )
	{
		int		last_predicted;
		clientdata_t	*pcd, *ppcd;
		entity_state_t	*ps, *pps;
		weapon_data_t	*wd, *pwd;

		if( !cls.spectator )
		{
			last_predicted = ( cl.last_incoming_sequence + ( command_ack - cl.last_command_ack )) & CL_UPDATE_MASK;

			pps = &cl.predicted_frames[last_predicted].playerstate;
			pwd = cl.predicted_frames[last_predicted].weapondata;
			ppcd = &cl.predicted_frames[last_predicted].client;

			ps = &frame->playerstate[cl.playernum];
			wd = frame->weapondata;
			pcd = &frame->clientdata;
		}
		else
		{
			ps = &cls.spectator_state.playerstate;
			pps = &cls.spectator_state.playerstate;
			pcd = &cls.spectator_state.client;
			ppcd = &cls.spectator_state.client;
			wd = cls.spectator_state.weapondata;
			pwd = cls.spectator_state.weapondata;
		}

		clgame.dllFuncs.pfnTxferPredictionData( ps, pps, pcd, ppcd, wd, pwd );
	}

	// do this after all packets read for this frame?
	cl.last_command_ack = cls.netchan.incoming_acknowledged;
	cl.last_incoming_sequence = cls.netchan.incoming_sequence;

	if( !cls.demoplayback )
	{
		// calculate latency of this frame.
		// sent time is set when usercmd is sent to server in CL_Move
		// this is the # of seconds the round trip took.
		float	latency = host.realtime - parsecounttime;

		// fill into frame latency
		frame->latency = latency;

		// negative latency makes no sense.  Huge latency is a problem.
		if( latency >= 0.0f && latency <= 2.0f )
		{
			// drift the average latency towards the observed latency
			// if round trip was fastest so far, just use that for latency value
			// otherwise, move in 1 ms steps toward observed channel latency.
			if( latency < cls.latency )
				cls.latency = latency;
			else cls.latency += 0.001f; // drift up, so corrections are needed
		}
	}
	else
	{
		frame->latency = 0.0f;
	}

	// clientdata for spectators ends here
	if( cls.spectator )
	{
		cl.local.health = 1;
		return;
	}

	to_cd = &frame->clientdata;
	to_wd = frame->weapondata;

	// clear to old value before delta parsing
	if( MSG_ReadOneBit( msg ))
	{
		int	delta_sequence = MSG_ReadByte( msg );

		from_cd = &cl.frames[delta_sequence & CL_UPDATE_MASK].clientdata;
		from_wd = cl.frames[delta_sequence & CL_UPDATE_MASK].weapondata;
	}
	else
	{
		memset( &nullcd, 0, sizeof( nullcd ));
		memset( nullwd, 0, sizeof( nullwd ));
		from_cd = &nullcd;
		from_wd = nullwd;
	}

	if( proto == PROTO_GOLDSRC )
		Delta_ReadGSFields( msg, DT_CLIENTDATA_T, from_cd, to_cd, cl.mtime[0] );
	else MSG_ReadClientData( msg, from_cd, to_cd, cl.mtime[0] );

	for( i = 0; i < 64; i++ )
	{
		// check for end of weapondata (and clientdata_t message)
		if( !MSG_ReadOneBit( msg )) break;

		// read the weapon idx
		idx = MSG_ReadUBitLong( msg, proto == PROTO_LEGACY ? MAX_LEGACY_WEAPON_BITS : MAX_WEAPON_BITS );

		if( proto == PROTO_GOLDSRC )
			Delta_ReadGSFields( msg, DT_WEAPONDATA_T, &from_wd[idx], &to_wd[idx], cl.mtime[0] );
		else MSG_ReadWeaponData( msg, &from_wd[idx], &to_wd[idx], cl.mtime[0] );
	}

	// make a local copy of physinfo
	Q_strncpy( cls.physinfo, frame->clientdata.physinfo, sizeof( cls.physinfo ));

	cl.local.maxspeed = frame->clientdata.maxspeed;
	cl.local.pushmsec = frame->clientdata.pushmsec;
	cl.local.weapons = frame->clientdata.weapons;
	cl.local.health = frame->clientdata.health;
}

/*
==================
CL_ParseBaseline
==================
*/
void CL_ParseBaseline( sizebuf_t *msg, connprotocol_t proto )
{
	const entity_state_t nullstate = { 0 };

	Delta_InitClient ();	// finalize client delta's

	while( 1 )
	{
		cl_entity_t *ent;
		qboolean player;
		int newnum;

		if( proto == PROTO_LEGACY )
		{
			newnum = MSG_ReadWord( msg );
		}
		else if( proto == PROTO_GOLDSRC )
		{
			uint value = MSG_ReadWord( msg );

			if( value == 0xffff ) break; // end of baselines

			MSG_SeekToBit( msg, -16, SEEK_CUR );
			newnum = MSG_ReadUBitLong( msg, MAX_GOLDSRC_ENTITY_BITS );
		}
		else
		{
			newnum = MSG_ReadUBitLong( msg, MAX_ENTITY_BITS );
			if( newnum == LAST_EDICT ) break; // end of baselines
		}

		player = CL_IsPlayerIndex( newnum );

		if( newnum >= clgame.maxEntities )
			Host_Error( "%s: no free edicts\n", __func__ );

		ent = CL_EDICT_NUM( newnum );
		ent->prevstate = nullstate;
		ent->index = newnum;

		if( proto == PROTO_GOLDSRC )
		{
			int type = MSG_ReadUBitLong( msg, 2 );
			int delta_type;

			if( player ) delta_type = DT_ENTITY_STATE_PLAYER_T;
			else if( type != ENTITY_NORMAL ) delta_type = DT_CUSTOM_ENTITY_STATE_T;
			else delta_type = DT_ENTITY_STATE_T;

			Delta_ReadGSFields( msg, delta_type, &ent->prevstate, &ent->baseline, 1.0f );
			ent->baseline.entityType = type;
		}
		else MSG_ReadDeltaEntity( msg, &nullstate, &ent->baseline, newnum, player, 1.0f );

		if( proto == PROTO_LEGACY )
			break; // only one baseline allowed in legacy protocol
	}

	if( proto != PROTO_LEGACY )
	{
		int i;

		cl.instanced_baseline_count = MSG_ReadUBitLong( msg, 6 );

		for( i = 0; i < cl.instanced_baseline_count; i++ )
		{
			if( proto == PROTO_GOLDSRC )
				Delta_ReadGSFields( msg, DT_ENTITY_STATE_T, &nullstate, &cl.instanced_baseline[i], 1.0f );
			else
			{
				int newnum = MSG_ReadUBitLong( msg, MAX_ENTITY_BITS );
				MSG_ReadDeltaEntity( msg, &nullstate, &cl.instanced_baseline[i], newnum, false, 1.0f );
			}
		}
	}
}

/*
================
CL_ParseLightStyle
================
*/
void CL_ParseLightStyle( sizebuf_t *msg, connprotocol_t proto )
{
	int		style;
	const char	*s;
	float		f = cl.mtime[0];

	style = MSG_ReadByte( msg );
	s = MSG_ReadString( msg );
	if( proto != PROTO_GOLDSRC && proto != PROTO_QUAKE )
		f = MSG_ReadFloat( msg );

	CL_SetLightstyle( style, s, f );
}

/*
================
CL_ParseSetAngle

set the view angle to this absolute value
================
*/
void CL_ParseSetAngle( sizebuf_t *msg )
{
	MSG_ReadVec3Angles( msg, cl.viewangles );
}

/*
================
CL_ParseAddAngle

add the view angle yaw
================
*/
void CL_ParseAddAngle( sizebuf_t *msg )
{
	pred_viewangle_t	*a;
	float		delta_yaw;

	delta_yaw = MSG_ReadBitAngle( msg, 16 );
#if 0
	cl.viewangles[YAW] += delta_yaw;
	return;
#endif
	// update running counter
	cl.addangletotal += delta_yaw;

	// select entry into circular buffer
	cl.angle_position = (cl.angle_position + 1) & ANGLE_MASK;
	a = &cl.predicted_angle[cl.angle_position];

	// record update
	a->starttime = cl.mtime[0];
	a->total = cl.addangletotal;
}

/*
================
CL_ParseCrosshairAngle

offset crosshair angles
================
*/
void CL_ParseCrosshairAngle( sizebuf_t *msg )
{
	cl.crosshairangle[0] = MSG_ReadChar( msg ) * 0.2f;
	cl.crosshairangle[1] = MSG_ReadChar( msg ) * 0.2f;
	cl.crosshairangle[2] = 0.0f; // not used for screen space
}

/*
================
CL_ParseRestore

reading decals, etc.
================
*/
void CL_ParseRestore( sizebuf_t *msg )
{
	string		filename;
	int		i, mapCount;
	char		*pMapName;

	// mapname.HL2
	Q_strncpy( filename, MSG_ReadString( msg ), sizeof( filename ));
	mapCount = MSG_ReadByte( msg );

	// g-cont. acutally in Xash3D this does nothing.
	// decals already restored on a server, and correctly transferred through levels
	// but i'm leave this message for backward compatibility
	for( i = 0; i < mapCount; i++ )
	{
		pMapName = MSG_ReadString( msg );
		Con_Printf( "Loading decals from %s\n", pMapName );
	}
}

/*
================
CL_RegisterUserMessage

register new user message or update existing
================
*/
void CL_RegisterUserMessage( sizebuf_t *msg, connprotocol_t proto )
{
	char *pszName;
	char szName[17];
	int size;
	int svc_num = MSG_ReadByte( msg );

	if( proto == PROTO_LEGACY || proto == PROTO_GOLDSRC )
	{
		size = MSG_ReadByte( msg );
		if( size == UINT8_MAX )
			size = -1;
	}
	else
	{
		size = MSG_ReadWord( msg );
		if( size == UINT16_MAX )
			size = -1;
	}

	if( proto == PROTO_GOLDSRC )
	{
		MSG_ReadBytes( msg, szName, sizeof( szName ) - 1 );
		szName[16] = 0;
		pszName = szName;
	}
	else pszName = MSG_ReadString( msg );

	CL_LinkUserMessage( pszName, svc_num, size );
}

/*
================
CL_UpdateUserinfo

collect userinfo from all players
================
*/
void CL_UpdateUserinfo( sizebuf_t *msg, connprotocol_t proto )
{
	int		slot, id;
	qboolean		active;
	player_info_t	*player;

	if( proto == PROTO_GOLDSRC )
	{
		slot = MSG_ReadByte( msg );
		id = MSG_ReadLong( msg );
		active = true;
	}
	else if( proto == PROTO_LEGACY )
	{
		slot = MSG_ReadUBitLong( msg, MAX_CLIENT_BITS );
		id = 0; // bogus
		active = MSG_ReadOneBit( msg ) ? true : false;
	}
	else
	{
		slot = MSG_ReadUBitLong( msg, MAX_CLIENT_BITS );
		id = MSG_ReadLong( msg );	// unique user ID
		active = MSG_ReadOneBit( msg ) ? true : false;
	}

	if( slot >= MAX_CLIENTS )
		Host_Error( "%s: svc_updateuserinfo >= MAX_CLIENTS\n", __func__ );

	player = &cl.players[slot];

	if( active )
	{
		Q_strncpy( player->userinfo, MSG_ReadString( msg ), sizeof( player->userinfo ));
		Q_strncpy( player->name, Info_ValueForKey( player->userinfo, "name" ), sizeof( player->name ));
		Q_strncpy( player->model, Info_ValueForKey( player->userinfo, "model" ), sizeof( player->model ));
		player->topcolor = Q_atoi( Info_ValueForKey( player->userinfo, "topcolor" ));
		player->bottomcolor = Q_atoi( Info_ValueForKey( player->userinfo, "bottomcolor" ));
		player->spectator = Q_atoi( Info_ValueForKey( player->userinfo, "*hltv" ));
		if( proto != PROTO_LEGACY )
			MSG_ReadBytes( msg, player->hashedcdkey, sizeof( player->hashedcdkey ));

		if( proto == PROTO_GOLDSRC && ( !COM_CheckStringEmpty( player->userinfo ) || !COM_CheckStringEmpty( player->name )))
			active = false;

		if( active && slot == cl.playernum )
			gameui.playerinfo = *player;
	}


	if( !active )
	{
		COM_ClearCustomizationList( &player->customdata, true );

		memset( player, 0, sizeof( *player ));
	}

	// in GoldSrc userinfo might be empty but userid is always sent as separate value
	// thus avoids clean up even after client disconnect
	player->userid = id;
}

/*
==============
CL_ParseResource

downloading and precache resource in-game
==============
*/
void CL_ParseResource( sizebuf_t *msg )
{
	resource_t	*pResource;

	pResource = Mem_Calloc( cls.mempool, sizeof( resource_t ));
	pResource->type = MSG_ReadUBitLong( msg, 4 );

	Q_strncpy( pResource->szFileName, MSG_ReadString( msg ), sizeof( pResource->szFileName ));
	pResource->nIndex = MSG_ReadUBitLong( msg, MAX_MODEL_BITS );
	pResource->nDownloadSize = MSG_ReadSBitLong( msg, 24 );
	pResource->ucFlags = MSG_ReadUBitLong( msg, 3 ) & ~RES_WASMISSING;

	if( FBitSet( pResource->ucFlags, RES_CUSTOM ))
		MSG_ReadBytes( msg, pResource->rgucMD5_hash, sizeof( pResource->rgucMD5_hash ));

	if( MSG_ReadOneBit( msg ))
		MSG_ReadBytes( msg, pResource->rguc_reserved, sizeof( pResource->rguc_reserved ));

	if( pResource->type == t_sound && pResource->nIndex > MAX_SOUNDS )
	{
		Mem_Free( pResource );
		Host_Error( "bad sound index\n" );
	}

	if( pResource->type == t_model && pResource->nIndex > MAX_MODELS )
	{
		Mem_Free( pResource );
		Host_Error( "bad model index\n" );
	}

	if( pResource->type == t_eventscript && pResource->nIndex > MAX_EVENTS )
	{
		Mem_Free( pResource );
		Host_Error( "bad event index\n" );
	}

	if( pResource->type == t_generic && pResource->nIndex > MAX_CUSTOM )
	{
		Mem_Free( pResource );
		Host_Error( "bad file index\n" );
	}

	if( pResource->type == t_decal && pResource->nIndex > MAX_DECALS )
	{
		Mem_Free( pResource );
		Host_Error( "bad decal index\n" );
	}

	CL_AddToResourceList( pResource, &cl.resourcesneeded );
}

/*
================
CL_UpdateUserPings

collect pings and packet lossage from clients
================
*/
void CL_UpdateUserPings( sizebuf_t *msg )
{
	// a1ba: there was a MAX_PLAYERS check but it doesn't make sense
	// because pings message always ends by null bit
	while( 1 )
	{
		int slot;
		player_info_t *player;

		if( !MSG_ReadOneBit( msg ))
			break; // end of message

		slot = MSG_ReadUBitLong( msg, MAX_CLIENT_BITS );

		if( unlikely( slot >= MAX_CLIENTS ))
		{
			Host_Error( "%s: svc_pings > MAX_CLIENTS\n", __func__ );
			return;
		}

		player = &cl.players[slot];
		player->ping = MSG_ReadUBitLong( msg, 12 );
		player->packet_loss = MSG_ReadUBitLong( msg, 7 );
	}
}

static const char *CL_CheckTypeToString( int check_type )
{
	// renamed so they have same width and look better in console output
	switch( check_type )
	{
	case force_exactfile:
		return "exactfile";
	case force_model_samebounds:
		return "samebounds";
	case force_model_specifybounds:
		return "specbounds";
	case force_model_specifybounds_if_avail:
		return "specbounds2";
	}
	return "unknown";
}

static void CL_SendConsistencyInfo( sizebuf_t *msg, connprotocol_t proto )
{
	qboolean		user_changed_diskfile;
	vec3_t		mins, maxs;
	string		filename;
	CRC32_t		crcFile;
	byte		md5[16] = { 0 };
	consistency_t	*pc;
	int		i, pos;

	if( !cl.need_force_consistency_response )
		return;
	cl.need_force_consistency_response = false;

	MSG_BeginClientCmd( msg, clc_fileconsistency );
	pos = MSG_GetNumBytesWritten( msg );
	if( proto == PROTO_GOLDSRC )
	{
		MSG_WriteShort( msg, 0 );
		MSG_StartBitWriting( msg );
	}

	FS_AllowDirectPaths( true );

	for( i = 0; i < cl.num_consistency; i++ )
	{
		qboolean have_file = true;

		pc = &cl.consistency_list[i];

		user_changed_diskfile = false;
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteUBitLong( msg, pc->orig_index, MAX_MODEL_BITS );

		if( pc->issound )
			Q_snprintf( filename, sizeof( filename ), DEFAULT_SOUNDPATH "%s", pc->filename );
		else Q_strncpy( filename, pc->filename, sizeof( filename ));

		COM_FixSlashes( filename );
		have_file = FS_FileExists( filename, false );

		if( Q_strstr( filename, "models/" ) && have_file )
		{
			CRC32_Init( &crcFile );
			CRC32_File( &crcFile, filename );
			crcFile = CRC32_Final( crcFile );
			user_changed_diskfile = !Mod_ValidateCRC( filename, crcFile );
		}

		switch( pc->check_type )
		{
		case force_exactfile:
			// servers rely on md5 not being initialized after previous file
			// if current file doesn't exist
			MD5_HashFile( md5, filename, NULL );
			memcpy( &pc->value, md5, sizeof( pc->value ));
			LittleLongSW( pc->value );

			if( user_changed_diskfile )
				MSG_WriteUBitLong( msg, 0, 32 );
			else MSG_WriteUBitLong( msg, pc->value, 32 );
			break;

		case force_model_specifybounds_if_avail:
			if( have_file )
			{
				if( !Mod_GetStudioBounds( filename, mins, maxs ))
					Host_Error( "unable to find %s\n", filename );

				if( user_changed_diskfile )
				{
					VectorSet( mins, -9999.9f, -9999.9f, -9999.9f );
					VectorSet( maxs, 9999.9f, 9999.9f, 9999.9f );
				}
			}
			else
			{
				VectorSet( mins, -1.0f, -1.0f, -1.0f );
				VectorCopy( mins, maxs );
			}

			MSG_WriteBytes( msg, mins, 12 );
			MSG_WriteBytes( msg, maxs, 12 );
			break;
		case force_model_samebounds:
		case force_model_specifybounds:
			if( !Mod_GetStudioBounds( filename, mins, maxs ))
				Host_Error( "unable to find %s\n", filename );
			if( user_changed_diskfile )
			{
				VectorSet( mins, -9999.9f, -9999.9f, -9999.9f );
				VectorSet( maxs, 9999.9f, 9999.9f, 9999.9f );
			}
			MSG_WriteBytes( msg, mins, 12 );
			MSG_WriteBytes( msg, maxs, 12 );
			break;
		default:
			Host_Error( "Unknown consistency type %i\n", pc->check_type );
			break;
		}
	}

	FS_AllowDirectPaths( false );

	MSG_WriteOneBit( msg, 0 );

	if( proto == PROTO_GOLDSRC )
	{
		int len;
		MSG_EndBitWriting( msg );

		len = MSG_GetNumBytesWritten( msg ) - pos - 2;
		*(short *)&msg->pData[pos] = len;

		COM_Munge( &msg->pData[pos + 2], len, cl.servercount );
	}
}

/*
==================
CL_StartDark
==================
*/
static void CL_StartDark( void )
{
	if( v_dark.value )
	{
		screenfade_t		*sf = &clgame.fade;
		float			fadetime = 5.0f;
		client_textmessage_t	*title;

		title = CL_TextMessageGet( "GAMETITLE" );
		if( Host_IsQuakeCompatible( ))
			fadetime = 1.0f;

		if( title )
		{
			// get settings from titles.txt
			sf->fadeEnd = title->holdtime + title->fadeout;
			sf->fadeReset = title->fadeout;
		}
		else sf->fadeEnd = sf->fadeReset = fadetime;

		sf->fadeFlags = FFADE_IN;
		sf->fader = sf->fadeg = sf->fadeb = 0;
		sf->fadealpha = 255;
		sf->fadeSpeed = (float)sf->fadealpha / sf->fadeReset;
		sf->fadeReset += cl.time;
		sf->fadeEnd += sf->fadeReset;

		Cvar_DirectSet( &v_dark, "0" );
	}
}

/*
==================
CL_RegisterResources

Clean up and move to next part of sequence.
==================
*/
void CL_RegisterResources( sizebuf_t *msg, connprotocol_t proto )
{
	model_t	*mod;
	int	i;

	if( cls.dl.custom || ( cls.signon == SIGNONS && cls.state == ca_active ) )
	{
		cls.dl.custom = false;
		return;
	}

	if( !cls.demoplayback )
		CL_SendConsistencyInfo( msg, proto );

	// All done precaching.
	cl.worldmodel = CL_ModelHandle( 1 ); // get world pointer

	if( cl.worldmodel && cl.maxclients > 0 )
	{
		ASSERT( clgame.entities != NULL );
		clgame.entities->model = cl.worldmodel;

		if( !cl.video_prepped && !cl.audio_prepped )
		{
			Con_Printf( "Setting up renderer...\n" );

			// load tempent sprites (glowshell, muzzleflashes etc)
			CL_LoadClientSprites ();

			// invalidate all decal indexes
			memset( cl.decal_index, 0, sizeof( cl.decal_index ));
			cl.video_prepped = true;
			cl.audio_prepped = true;

			CL_ClearWorld ();

			// load skybox
			R_SetupSky( clgame.movevars.skyName );

			// tell rendering system we have a new set of models.
			ref.dllFuncs.R_NewMap ();

			// check if this map must start from dark screen
			CL_StartDark ();

			CL_SetupOverviewParams();

			// release unused SpriteTextures
			for( i = 1, mod = clgame.sprites; i < MAX_CLIENT_SPRITES; i++, mod++ )
			{
				if( mod->needload == NL_UNREFERENCED && COM_CheckString( mod->name ))
					Mod_FreeModel( mod );
			}

			Mod_FreeUnused ();

			if( host_developer.value <= DEV_NONE )
				Con_ClearNotify(); // clear any lines of console text

			// done with all resources, issue prespawn command.
			// Include server count in case server disconnects and changes level during d/l
			MSG_BeginClientCmd( msg, clc_stringcmd );
			if( proto == PROTO_GOLDSRC )
			{
				int32_t crc = cl.worldmapCRC;
				COM_Munge2((byte*)&crc, sizeof( crc ), ( 0xff - cl.servercount ) & 0xff );
				MSG_WriteStringf( msg, "spawn %i %i", cl.servercount, crc );
			}
			else MSG_WriteStringf( msg, "spawn %i", cl.servercount );
		}
	}
	else
	{
		Con_Printf( S_ERROR "client world model is NULL\n" );
		CL_Disconnect();
	}
}

static void CL_ParseConsistencyInfo( sizebuf_t *msg, connprotocol_t proto )
{
	int		lastcheck;
	int		delta;
	int		i;
	int		isdelta;
	resource_t	*pResource;
	resource_t	*skip_crc_change;
	int		skip;
	consistency_t	*pc;
	byte		nullbuffer[32];

	memset( nullbuffer, 0, 32 );

	cl.need_force_consistency_response = MSG_ReadOneBit( msg );
	pResource = cl.resourcesneeded.pNext;

	if( !cl.need_force_consistency_response )
		return;

	if( !pResource )
	{
		Host_Error( "%s: malformed consistency info packet (resources needed is NULL)\n", __func__ );
		return;
	}

	if( cl_trace_consistency.value )
		Con_Printf( "Server wants consistency of the following resources:\n" );

	skip_crc_change = NULL;
	lastcheck = 0;

	while( MSG_ReadOneBit( msg ))
	{
		isdelta = MSG_ReadOneBit( msg );

		if( isdelta ) delta = MSG_ReadUBitLong( msg, 5 ) + lastcheck;
		else delta = MSG_ReadUBitLong( msg, proto == PROTO_GOLDSRC ? MAX_GOLDSRC_MODEL_BITS : MAX_MODEL_BITS );

		skip = delta - lastcheck;

		for( i = 0; i < skip; i++ )
		{
			if( pResource != skip_crc_change && Q_strstr( pResource->szFileName, "models/" ))
				Mod_NeedCRC( pResource->szFileName, false );
			pResource = pResource->pNext;

			if( !pResource )
			{
				Host_Error( "%s: malformed consistency info packet (last check %d, delta %d, position %d)\n", __func__, lastcheck, delta, i );
				return;
			}
		}

		if( cl.num_consistency >= MAX_MODELS )
			Host_Error( "%s: MAX_MODELS limit exceeded (%d)\n", __func__, MAX_MODELS );

		pc = &cl.consistency_list[cl.num_consistency];
		cl.num_consistency++;

		memset( pc, 0, sizeof( *pc ));
		pc->filename = pResource->szFileName;
		pc->issound = (pResource->type == t_sound);
		pc->orig_index = delta;
		pc->value = 0;

		if( pResource->type == t_model && memcmp( nullbuffer, pResource->rguc_reserved, 32 ))
		{
			if( proto == PROTO_GOLDSRC )
				COM_UnMunge( pResource->rguc_reserved, sizeof( pResource->rguc_reserved ), cl.servercount );
			pc->check_type = pResource->rguc_reserved[0];
		}

		if( cl_trace_consistency.value )
			Con_Printf( "%s\t%s\t%s\n", COM_ResourceTypeFromIndex( pResource->type ), CL_CheckTypeToString( pc->check_type ), pc->filename );

		skip_crc_change = pResource;
		lastcheck = delta;
	}
}

/*
==============
CL_ParseResourceList

==============
*/
void CL_ParseResourceList( sizebuf_t *msg, connprotocol_t proto )
{
	resource_t	*pResource;
	int		i, total;

	total = MSG_ReadUBitLong( msg, proto == PROTO_GOLDSRC ? MAX_GOLDSRC_RESOURCE_BITS : MAX_RESOURCE_BITS );

	for( i = 0; i < total; i++ )
	{
		pResource = Mem_Calloc( cls.mempool, sizeof( resource_t ));
		pResource->type = MSG_ReadUBitLong( msg, 4 );

		Q_strncpy( pResource->szFileName, MSG_ReadString( msg ), sizeof( pResource->szFileName ));
		pResource->nIndex = MSG_ReadUBitLong( msg, MAX_MODEL_BITS );
		pResource->nDownloadSize = MSG_ReadBitLong( msg, 24, proto != PROTO_GOLDSRC );
		pResource->ucFlags = MSG_ReadUBitLong( msg, 3 ) & ~RES_WASMISSING;

		if( FBitSet( pResource->ucFlags, RES_CUSTOM ))
			MSG_ReadBytes( msg, pResource->rgucMD5_hash, sizeof( pResource->rgucMD5_hash ));

		if( MSG_ReadOneBit( msg ))
			MSG_ReadBytes( msg, pResource->rguc_reserved, sizeof( pResource->rguc_reserved ));

		CL_AddToResourceList( pResource, &cl.resourcesneeded );
	}

	CL_ParseConsistencyInfo( msg, proto );

	CL_StartResourceDownloading( "Verifying and downloading resources...\n", false );
}

/*
==================
CL_ParseVoiceInit

==================
*/
void CL_ParseVoiceInit( sizebuf_t *msg )
{
	char *pszCodec = MSG_ReadString( msg );
	int quality = MSG_ReadByte( msg );

	Voice_Init( pszCodec, quality, false ); // init requested codec and the device
}

/*
==================
CL_ParseVoiceData

==================
*/
void CL_ParseVoiceData( sizebuf_t *msg, connprotocol_t proto )
{
	int size, idx, frames = 0;
	byte received[VOICE_MAX_DATA_SIZE];

	idx = MSG_ReadByte( msg ) + 1;

	if ( idx <= 0 || idx > cl.maxclients )
		return;

	// must notify client.dll about server ack'ing our local client voice data
	if( idx == cl.playernum + 1 )
		Voice_LoopbackAck();

	if( proto == PROTO_GOLDSRC )
	{
		size = MSG_ReadShort( msg );
		if( size > VOICE_MAX_GS_DATA_SIZE )
		{
			Con_Printf( S_ERROR "Voice data size is too large: %d bytes (max: %d)\n", size, VOICE_MAX_GS_DATA_SIZE );
			return;
		}
	}
	else
	{
		frames = MSG_ReadByte( msg );
		size = MSG_ReadShort( msg );
		if( size > VOICE_MAX_DATA_SIZE )
		{
			Con_Printf( S_ERROR "Voice data size is too large: %d bytes (max: %d)\n", size, VOICE_MAX_DATA_SIZE );
			return;
		}
	}

	size = Q_min( size, sizeof( received ));

	if ( !size )
		return;

	MSG_ReadBytes( msg, received, size );

	Voice_AddIncomingData( idx, received, size, frames );
}

/*
==================
CL_ParseResLocation

==================
*/
void CL_ParseResLocation( sizebuf_t *msg )
{
	char *data = MSG_ReadString( msg );
	char token[256];

	if( Q_strlen( data ) > 256 )
	{
		Con_Printf( S_ERROR "Resource location too long!\n" );
		return;
	}

	while(( data = COM_ParseFile( data, token, sizeof( token ))))
	{
		Con_Reportf( "Adding %s as download location\n", token );
		cl.http_download = true;
		HTTP_AddCustomServer( token );
	}
}

/*
==============
CL_ParseHLTV

spectator message (hltv)
sended from game.dll
==============
*/
void CL_ParseHLTV( sizebuf_t *msg )
{
	switch( MSG_ReadByte( msg ))
	{
	case HLTV_ACTIVE:
		cl.proxy_redirect = true;
		cls.spectator = true;
		break;
	case HLTV_STATUS:
			MSG_ReadLong( msg );
			MSG_ReadShort( msg );
			MSG_ReadWord( msg );
			MSG_ReadLong( msg );
			MSG_ReadLong( msg );
			MSG_ReadWord( msg );
		break;
	case HLTV_LISTEN:
		cls.signon = SIGNONS;
#if 1
		MSG_ReadString( msg );
#else
		NET_StringToAdr( MSG_ReadString( msg ), &cls.hltv_listen_address );
		NET_JoinGroup( cls.netchan.sock, cls.hltv_listen_address );
#endif
		SCR_EndLoadingPlaque();
		break;
	default:
		break;
	}
}

/*
==============
CL_ParseDirector

spectator message (director)
sended from game.dll
==============
*/
void CL_ParseDirector( sizebuf_t *msg )
{
	int	iSize = MSG_ReadByte( msg );
	byte	pbuf[256];

	// parse user message into buffer
	MSG_ReadBytes( msg, pbuf, iSize );
	clgame.dllFuncs.pfnDirectorMessage( iSize, pbuf );
}

/*
==============
CL_ParseScreenShake

Set screen shake
==============
*/
static void CL_ParseScreenShake( sizebuf_t *msg )
{
	float amplitude = (float)(word)MSG_ReadShort( msg ) * ( 1.0f / (float)( 1 << 12 ));
	float duration  = (float)(word)MSG_ReadShort( msg ) * ( 1.0f / (float)( 1 << 12 ));
	float frequency = (float)(word)MSG_ReadShort( msg ) * ( 1.0f / (float)( 1 << 8 ));

	// don't overwrite larger existing shake
	if( amplitude > clgame.shake.amplitude )
		clgame.shake.amplitude = amplitude;

	clgame.shake.duration = duration;
	clgame.shake.time = cl.time + clgame.shake.duration;
	clgame.shake.frequency = frequency;
	clgame.shake.next_shake = 0.0f; // apply immediately
}

/*
==============
CL_ParseScreenFade

Set screen fade
==============
*/
static void CL_ParseScreenFade( sizebuf_t *msg )
{
	float		duration, holdTime;
	screenfade_t	*sf = &clgame.fade;
	float		flScale;

	duration = (float)MSG_ReadWord( msg );
	holdTime = (float)MSG_ReadWord( msg );
	sf->fadeFlags = MSG_ReadShort( msg );
	flScale = FBitSet( sf->fadeFlags, FFADE_LONGFADE ) ? (1.0f / 256.0f) : (1.0f / 4096.0f);

	sf->fader = MSG_ReadByte( msg );
	sf->fadeg = MSG_ReadByte( msg );
	sf->fadeb = MSG_ReadByte( msg );
	sf->fadealpha = MSG_ReadByte( msg );
	sf->fadeSpeed = 0.0f;
	sf->fadeEnd = duration * flScale;
	sf->fadeReset = holdTime * flScale;

	// calc fade speed
	if( duration > 0 )
	{
		if( FBitSet( sf->fadeFlags, FFADE_OUT ))
		{
			if( sf->fadeEnd )
			{
				sf->fadeSpeed = -(float)sf->fadealpha / sf->fadeEnd;
			}

			sf->fadeEnd += cl.time;
			sf->fadeTotalEnd = sf->fadeEnd;
			sf->fadeReset += sf->fadeEnd;
		}
		else
		{
			if( sf->fadeEnd )
			{
				sf->fadeSpeed = (float)sf->fadealpha / sf->fadeEnd;
			}

			sf->fadeReset += cl.time;
			sf->fadeEnd += sf->fadeReset;
		}
	}
}

/*
==============
CL_ParseCvarValue

Find the client cvar value
and sent it back to the server
==============
*/
void CL_ParseCvarValue( sizebuf_t *msg, const qboolean ext, const connprotocol_t proto )
{
	const char *cvarName, *response = NULL;
	convar_t *cvar;
	int requestID;

	if( ext )
		requestID = MSG_ReadLong( msg );

	cvarName = MSG_ReadString( msg );

	if( proto == PROTO_GOLDSRC )
	{
		if( !Q_stricmp( cvarName, "sv_version" ))
			response = "1.1.2.2/Stdio,48,10211";
	}

	if( !response )
	{
		cvar = Cvar_FindVar( cvarName );

		if( cvar )
		{
			if( cvar->flags & FCVAR_PRIVILEGED )
				response = "CVAR is privileged";
			else if( cvar->flags & FCVAR_SERVER )
				response = "CVAR is server-only";
			else if( cvar->flags & FCVAR_PROTECTED )
				response = "CVAR is protected";
			else
				response = cvar->string;
		}
		else if( proto == PROTO_LEGACY )
		{
			response = "Not Found";
		}
		else
		{
			response = "Bad CVAR request";
		}
	}

	if( ext )
	{
		int clc_msg = proto == PROTO_GOLDSRC ? clc_goldsrc_requestcvarvalue2 : clc_requestcvarvalue2;

		MSG_BeginClientCmd( &cls.netchan.message, clc_msg );
		MSG_WriteLong( &cls.netchan.message, requestID );
		MSG_WriteString( &cls.netchan.message, cvarName );
	}
	else
	{
		int clc_msg = proto == PROTO_GOLDSRC ? clc_goldsrc_requestcvarvalue : clc_requestcvarvalue;

		MSG_BeginClientCmd( &cls.netchan.message, clc_msg );
	}
	MSG_WriteString( &cls.netchan.message, response );
}

/*
==============
CL_ParseExec

Exec map/class specific configs
==============
*/
void CL_ParseExec( sizebuf_t *msg )
{
	qboolean is_class;
	int class_idx;
	string mapname;
	const char *class_cfgs[] = {
		"",
		"exec scout.cfg\n",
		"exec sniper.cfg\n",
		"exec soldier.cfg\n",
		"exec demoman.cfg\n",
		"exec medic.cfg\n",
		"exec hwguy.cfg\n",
		"exec pyro.cfg\n",
		"exec spy.cfg\n",
		"exec engineer.cfg\n",
		"",
		"exec civilian.cfg\n"
	};

	is_class = MSG_ReadByte( msg );

	if ( is_class )
	{
		class_idx = MSG_ReadByte( msg );

		if ( class_idx >= 0 && class_idx <= 11 && !Q_stricmp( GI->gamefolder, "tfc" ) )
			Cbuf_AddText( class_cfgs[class_idx] );
	}
	else if ( !Q_stricmp( GI->gamefolder, "tfc" ) )
	{
		Cbuf_AddText( "exec mapdefault.cfg\n" );

		COM_FileBase( clgame.mapname, mapname, sizeof( mapname ));

		if ( COM_CheckString( mapname ) )
			Cbuf_AddTextf( "exec %s.cfg\n", mapname );
	}
}

/*
==============
CL_DispatchUserMessage

Dispatch user message by engine request
==============
*/
qboolean CL_DispatchUserMessage( const char *pszName, int iSize, void *pbuf )
{
	int	i;

	if( !COM_CheckString( pszName ))
		return false;

	for( i = 0; i < MAX_USER_MESSAGES; i++ )
	{
		// search for user message
		if( !Q_strcmp( clgame.msg[i].name, pszName ))
			break;
	}

	if( i == MAX_USER_MESSAGES )
	{
		Con_DPrintf( S_ERROR "UserMsg: bad message %s\n", pszName );
		return false;
	}

	if( clgame.msg[i].func )
	{
		clgame.msg[i].func( pszName, iSize, pbuf );
	}
	else
	{
		Con_DPrintf( S_ERROR "UserMsg: No pfn %s %d\n", clgame.msg[i].name, clgame.msg[i].number );
		clgame.msg[i].func = CL_UserMsgStub; // throw warning only once
	}
	return true;
}

/*
==============
CL_ParseUserMessage

handles all user messages
==============
*/
void CL_ParseUserMessage( sizebuf_t *msg, int svc_num, connprotocol_t proto )
{
	byte	pbuf[MAX_USERMSG_LENGTH];
	int	i, iSize;

	// NOTE: any user message is really parse at engine, not in client.dll
	if( svc_num <= svc_lastmsg || svc_num > ( MAX_USER_MESSAGES + svc_lastmsg ))
	{
		// out or range
		Host_Error( "%s: illegible server message %d\n", __func__, svc_num );
		return;
	}

	for( i = 0; i < MAX_USER_MESSAGES; i++ )
	{
		// search for user message
		if( clgame.msg[i].number == svc_num )
			break;
	}

	if( i == MAX_USER_MESSAGES ) // probably unregistered
		Host_Error( "%s: illegible server message %d\n", __func__, svc_num );

	// NOTE: some user messages handled into engine
	if( !Q_strcmp( clgame.msg[i].name, "ScreenShake" ))
	{
		CL_ParseScreenShake( msg );
		return;
	}
	else if( !Q_strcmp( clgame.msg[i].name, "ScreenFade" ))
	{
		CL_ParseScreenFade( msg );
		return;
	}

	iSize = clgame.msg[i].size;

	// message with variable sizes receive an actual size as first byte
	if( iSize == -1 )
	{
		if( proto == PROTO_GOLDSRC || proto == PROTO_LEGACY )
			iSize = MSG_ReadByte( msg );
		else iSize = MSG_ReadWord( msg );
	}

	if( iSize >= MAX_USERMSG_LENGTH )
	{
		Con_Reportf( "%s: user message %s size limit hit (%d > %d)!\n", __func__, clgame.msg[i].name, iSize, MAX_USERMSG_LENGTH );
		return;
	}

	// parse user message into buffer
	MSG_ReadBytes( msg, pbuf, iSize );

	if( cl_trace_messages.value )
	{
		Con_Reportf( "^3USERMSG %s SIZE %i SVC_NUM %i\n",
			clgame.msg[i].name, iSize, clgame.msg[i].number );
	}

	if( clgame.msg[i].func )
	{
		clgame.msg[i].func( clgame.msg[i].name, iSize, pbuf );

#ifdef HACKS_RELATED_HLMODS
		// run final credits for Half-Life because hl1 doesn't have call END_SECTION
		if( !Q_stricmp( clgame.msg[i].name, "HudText" ) && !Q_stricmp( GI->gamefolder, "valve" ))
		{
			// it's a end, so we should run credits
			if( !Q_strcmp( (char *)pbuf, "END3" ))
				Host_Credits();
		}
#endif
	}
	else
	{
		Con_DPrintf( S_ERROR "%s: No pfn %s %d\n", __func__, clgame.msg[i].name, clgame.msg[i].number );
		clgame.msg[i].func = CL_UserMsgStub; // throw warning only once
	}
}

/*
=====================================================================

ACTION MESSAGES

=====================================================================
*/

/*
============
CL_ParseCommonDLLMessage

parse a message which structure is enforced by DLL compatibility
it should always be the same regardless of protocol used
============
*/
qboolean CL_ParseCommonDLLMessage( sizebuf_t *msg, connprotocol_t proto, int svc_num, int startoffset )
{
	int param1, param2;

	switch( svc_num )
	{
	case svc_temp_entity:
		CL_ParseTempEntity( msg, proto ); // need protocol because message header differs
		cl.frames[cl.parsecountmod].graphdata.tentities += MSG_GetNumBytesRead( msg ) - startoffset;
		break;
	case svc_intermission:
		cl.intermission = 1;
		break;
	case svc_cdtrack:
		param1 = MSG_ReadByte( msg );
		param1 = bound( 1, param1, MAX_CDTRACKS ); // tracknum
		param2 = MSG_ReadByte( msg );
		param2 = bound( 1, param2, MAX_CDTRACKS ); // loopnum
		S_StartBackgroundTrack( clgame.cdtracks[param1-1], clgame.cdtracks[param2-1], 0, false );
		break;
	case svc_weaponanim:
		param1 = MSG_ReadByte( msg );	// iAnim
		param2 = MSG_ReadByte( msg );	// body
		CL_WeaponAnim( param1, param2 );
		break;
	case svc_roomtype:
		param1 = MSG_ReadShort( msg );
		Cvar_SetValue( "room_type", param1 );
		break;
	case svc_director:
		CL_ParseDirector( msg );
		break;
	default:
		return false;
	}

	return true;
}

/*
=====================
CL_ParseServerMessage

dispatch messages
=====================
*/
void CL_ParseServerMessage( sizebuf_t *msg )
{
	size_t		bufStart, playerbytes;
	int		cmd;
	int		old_background;
	const char	*s;

	// parse the message
	while( 1 )
	{
		if( MSG_CheckOverflow( msg ))
		{
			Host_Error( "%s: overflow!\n", __func__ );
			return;
		}

		// mark start position
		bufStart = MSG_GetNumBytesRead( msg );

		// end of message (align bits)
		if( MSG_GetNumBitsLeft( msg ) < 8 )
			break;

		cmd = MSG_ReadServerCmd( msg );

		// record command for debugging spew on parse problem
		CL_Parse_RecordCommand( cmd, bufStart );

		if( CL_ParseCommonDLLMessage( msg, PROTO_CURRENT, cmd, bufStart ))
			continue;

		// other commands
		switch( cmd )
		{
		case svc_bad:
			Host_Error( "svc_bad\n" );
			break;
		case svc_nop:
			// this does nothing
			break;
		case svc_disconnect:
			CL_Drop ();
			Host_AbortCurrentFrame ();
			break;
		case svc_event:
			CL_ParseEvent( msg, PROTO_CURRENT );
			cl.frames[cl.parsecountmod].graphdata.event += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_changing:
			old_background = cl.background;
			if( MSG_ReadOneBit( msg ))
			{
				int maxclients = cl.maxclients;

				cls.changelevel = true;
				S_StopAllSounds( true );

				Con_Printf( "Server changing, reconnecting\n" );

				if( cls.demoplayback )
				{
					SCR_BeginLoadingPlaque( cl.background );
					cls.changedemo = true;
				}

				CL_ClearState();
				CL_InitEdicts( maxclients ); // re-arrange edicts
			}
			else Con_Printf( "Server disconnected, reconnecting\n" );

			if( cls.demoplayback )
			{
				cl.background = (cls.demonum != -1) ? true : false;
				cls.state = ca_connected;
			}
			else
			{
				// g-cont. local client skip the challenge
				if( SV_Active( ))
					cls.state = ca_disconnected;
				else cls.state = ca_connecting;
				cl.background = old_background;
				cls.connect_time = MAX_HEARTBEAT;
				cls.connect_retry = 0;
			}
			break;
		case svc_setview:
			CL_ParseViewEntity( msg );
			break;
		case svc_sound:
			CL_ParseSoundPacket( msg );
			cl.frames[cl.parsecountmod].graphdata.sound += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_time:
			CL_ParseServerTime( msg, PROTO_CURRENT );
			break;
		case svc_print:
			Con_Printf( "%s", MSG_ReadString( msg ));
			break;
		case svc_stufftext:
			s = MSG_ReadString( msg );
			if( cl_trace_stufftext.value )
			{
				size_t len = Q_strlen( s );
				Con_Printf( "Stufftext: %s%c", s, len && s[len-1] == '\n' ? '\0' : '\n' );
			}

#ifdef HACKS_RELATED_HLMODS
			// disable Cry Of Fear antisave protection
			if( !Q_strnicmp( s, "disconnect", 10 ) && cls.signon != SIGNONS )
				break; // too early
#endif

			Cbuf_AddFilteredText( s );
			break;
		case svc_setangle:
			CL_ParseSetAngle( msg );
			break;
		case svc_serverdata:
			Cbuf_Execute(); // make sure any stuffed commands are done
			CL_ParseServerData( msg, PROTO_CURRENT );
			break;
		case svc_lightstyle:
			CL_ParseLightStyle( msg, PROTO_CURRENT );
			break;
		case svc_updateuserinfo:
			CL_UpdateUserinfo( msg, PROTO_CURRENT );
			break;
		case svc_deltatable:
			Delta_ParseTableField( msg );
			break;
		case svc_clientdata:
			CL_ParseClientData( msg, PROTO_CURRENT );
			cl.frames[cl.parsecountmod].graphdata.client += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_resource:
			CL_ParseResource( msg );
			break;
		case svc_pings:
			CL_UpdateUserPings( msg );
			break;
		case svc_particle:
			CL_ParseParticles( msg, PROTO_CURRENT );
			break;
		case svc_restoresound:
			CL_ParseRestoreSoundPacket( msg );
			cl.frames[cl.parsecountmod].graphdata.sound += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_spawnstatic:
			CL_ParseStaticEntity( msg );
			break;
		case svc_event_reliable:
			CL_ParseReliableEvent( msg, PROTO_CURRENT );
			cl.frames[cl.parsecountmod].graphdata.event += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_spawnbaseline:
			CL_ParseBaseline( msg, PROTO_CURRENT );
			break;
		case svc_setpause:
			cl.paused = ( MSG_ReadOneBit( msg ) != 0 );
			break;
		case svc_signonnum:
			CL_ParseSignon( msg, PROTO_CURRENT );
			break;
		case svc_centerprint:
			CL_CenterPrint( MSG_ReadString( msg ), 0.25f );
			break;
		case svc_finale:
			CL_ParseFinaleCutscene( msg, 2 );
			break;
		case svc_restore:
			CL_ParseRestore( msg );
			break;
		case svc_cutscene:
			CL_ParseFinaleCutscene( msg, 3 );
			break;
		case svc_bspdecal:
			CL_ParseStaticDecal( msg );
			break;
		case svc_addangle:
			CL_ParseAddAngle( msg );
			break;
		case svc_usermessage:
			CL_RegisterUserMessage( msg, PROTO_CURRENT );
			break;
		case svc_packetentities:
			playerbytes = CL_ParsePacketEntities( msg, false, PROTO_CURRENT );
			cl.frames[cl.parsecountmod].graphdata.players += playerbytes;
			cl.frames[cl.parsecountmod].graphdata.entities += MSG_GetNumBytesRead( msg ) - bufStart - playerbytes;
			break;
		case svc_deltapacketentities:
			playerbytes = CL_ParsePacketEntities( msg, true, PROTO_CURRENT );
			cl.frames[cl.parsecountmod].graphdata.players += playerbytes;
			cl.frames[cl.parsecountmod].graphdata.entities += MSG_GetNumBytesRead( msg ) - bufStart - playerbytes;
			break;
		case svc_choke:
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].choked = true;
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].receivedtime = -2.0;
			break;
		case svc_resourcelist:
			CL_ParseResourceList( msg, PROTO_CURRENT );
			break;
		case svc_deltamovevars:
			CL_ParseMovevars( msg );
			break;
		case svc_resourcerequest:
			CL_ParseResourceRequest( msg );
			break;
		case svc_customization:
			CL_ParseCustomization( msg );
			break;
		case svc_crosshairangle:
			CL_ParseCrosshairAngle( msg );
			break;
		case svc_soundfade:
			CL_ParseSoundFade( msg );
			break;
		case svc_filetxferfailed:
			CL_ParseFileTransferFailed( msg );
			break;
		case svc_hltv:
			CL_ParseHLTV( msg );
			break;
		case svc_voiceinit:
			CL_ParseVoiceInit( msg );
			break;
		case svc_voicedata:
			CL_ParseVoiceData( msg, PROTO_CURRENT );
			cl.frames[cl.parsecountmod].graphdata.voicebytes += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_resourcelocation:
			CL_ParseResLocation( msg );
			break;
		case svc_querycvarvalue:
			CL_ParseCvarValue( msg, false, PROTO_CURRENT );
			break;
		case svc_querycvarvalue2:
			CL_ParseCvarValue( msg, true, PROTO_CURRENT );
			break;
		case svc_exec:
			CL_ParseExec( msg );
			break;
		default:
			CL_ParseUserMessage( msg, cmd, PROTO_CURRENT );
			cl.frames[cl.parsecountmod].graphdata.usr += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		}
	}
}
