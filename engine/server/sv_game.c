/*
sv_game.c - gamedll interaction
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
#include "server.h"
#include "net_encode.h"
#include "event_flags.h"
#include "library.h"
#include "pm_defs.h"
#include "studio.h"
#include "const.h"
#include "render_api.h"	// modelstate_t
#include "ref_common.h" // decals

#define ENTVARS_COUNT	ARRAYSIZE( gEntvarsDescription )

// fatpvs stuff
static byte fatpvs[MAX_MAP_LEAFS/8];
static byte fatphs[MAX_MAP_LEAFS/8];
static byte clientpvs[MAX_MAP_LEAFS/8];	// for find client in PVS
static vec3_t viewPoint[MAX_CLIENTS];

// exports
typedef void (__cdecl *LINK_ENTITY_FUNC)( entvars_t *pev );
typedef void (__stdcall *GIVEFNPTRSTODLL)( enginefuncs_t* engfuncs, globalvars_t *pGlobals );

edict_t *SV_EdictNum( int n )
{
	if(( n >= 0 ) && ( n < GI->max_edicts ))
		return svgame.edicts + n;
	return NULL;	
}

#ifndef NDEBUG
qboolean SV_CheckEdict( const edict_t *e, const char *file, const int line )
{
	int	n;

	if( !e ) return false; // may be NULL

	n = ((int)((edict_t *)(e) - svgame.edicts));

	if(( n >= 0 ) && ( n < GI->max_edicts ))
		return !e->free;
	Con_Printf( "bad entity %i (called at %s:%i)\n", n, file, line );

	return false;	
}
#endif

/*
=============
EntvarsDescription

entavrs table for FindEntityByString
=============
*/
static TYPEDESCRIPTION gEntvarsDescription[] = 
{
	DEFINE_ENTITY_FIELD( classname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( globalname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( model, FIELD_MODELNAME ),
	DEFINE_ENTITY_FIELD( viewmodel, FIELD_MODELNAME ),
	DEFINE_ENTITY_FIELD( weaponmodel, FIELD_MODELNAME ),
	DEFINE_ENTITY_FIELD( target, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( targetname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( netname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( message, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( noise, FIELD_SOUNDNAME ),
	DEFINE_ENTITY_FIELD( noise1, FIELD_SOUNDNAME ),
	DEFINE_ENTITY_FIELD( noise2, FIELD_SOUNDNAME ),
	DEFINE_ENTITY_FIELD( noise3, FIELD_SOUNDNAME ),
};

/*
=============
SV_GetEntvarsDescription

entavrs table for FindEntityByString
=============
*/
TYPEDESCRIPTION *SV_GetEntvarsDescirption( int number )
{
	if( number < 0 && number >= ENTVARS_COUNT )
		return NULL;
	return &gEntvarsDescription[number];
}

/*
=============
SV_SysError

tell the game.dll about system error
=============
*/
void SV_SysError( const char *error_string )
{
	Log_Printf( "FATAL ERROR (shutting down): %s\n", error_string );

	if( svgame.hInstance != NULL )
		svgame.dllFuncs.pfnSys_Error( error_string );
}

/*
=============
SV_Serverinfo

get server infostring
=============
*/
char *SV_Serverinfo( void )
{
	return svs.serverinfo;
}

/*
=============
SV_LocalInfo

get local infostring
=============
*/
char *SV_Localinfo( void )
{
	return svs.localinfo;
}

/*
=============
SV_AngleMod

do modulo on entity angles
=============
*/
float SV_AngleMod( float ideal, float current, float speed )
{
	float	move;

	current = anglemod( current );

	if( current == ideal ) // already there?
		return current; 

	move = ideal - current;

	if( ideal > current )
	{
		if( move >= 180 )
			move = move - 360;
	}
	else
	{
		if( move <= -180 )
			move = move + 360;
	}

	if( move > 0 )
	{
		if( move > speed )
			move = speed;
	}
	else
	{
		if( move < -speed )
			move = -speed;
	}

	return anglemod( current + move );
}

/*
=============
SV_SetMinMaxSize

update entity bounds, relink into world
=============
*/
void SV_SetMinMaxSize( edict_t *e, const float *mins, const float *maxs, qboolean relink )
{
	int	i;

	if( !SV_IsValidEdict( e ))
		return;

	for( i = 0; i < 3; i++ )
	{
		if( mins[i] > maxs[i] )
		{
			Con_Printf( S_ERROR "%s[%i] has backwards mins/maxs\n", SV_ClassName( e ), NUM_FOR_EDICT( e ));
			if( relink ) SV_LinkEdict( e, false ); // just relink edict and exit
			return;
		}
	}

	VectorCopy( mins, e->v.mins );
	VectorCopy( maxs, e->v.maxs );
	VectorSubtract( maxs, mins, e->v.size );
	if( relink ) SV_LinkEdict( e, false );
}

/*
=============
SV_CopyTraceToGlobal

each trace will share their result into global state
=============
*/
void SV_CopyTraceToGlobal( trace_t *trace )
{
	svgame.globals->trace_allsolid = trace->allsolid;
	svgame.globals->trace_startsolid = trace->startsolid;
	svgame.globals->trace_fraction = trace->fraction;
	svgame.globals->trace_plane_dist = trace->plane.dist;
	svgame.globals->trace_inopen = trace->inopen;
	svgame.globals->trace_inwater = trace->inwater;
	VectorCopy( trace->endpos, svgame.globals->trace_endpos );
	VectorCopy( trace->plane.normal, svgame.globals->trace_plane_normal );
	svgame.globals->trace_hitgroup = trace->hitgroup;
	svgame.globals->trace_flags = 0; // g-cont: always reset config flags when trace is finished

	if( SV_IsValidEdict( trace->ent ))
		svgame.globals->trace_ent = trace->ent;
	else svgame.globals->trace_ent = svgame.edicts;
}

/*
=============
SV_ConvertTrace

convert trace_t to TraceResult
=============
*/
void SV_ConvertTrace( TraceResult *dst, trace_t *src )
{
	if( !src || !dst ) return; 

	dst->fAllSolid = src->allsolid;
	dst->fStartSolid = src->startsolid;
	dst->fInOpen = src->inopen;
	dst->fInWater = src->inwater;
	dst->flFraction = src->fraction;
	VectorCopy( src->endpos, dst->vecEndPos );
	dst->flPlaneDist = src->plane.dist;
	VectorCopy( src->plane.normal, dst->vecPlaneNormal );
	dst->pHit = src->ent;
	dst->iHitgroup = src->hitgroup;

	// g-cont: always reset config flags when trace is finished
	svgame.globals->trace_flags = 0;
}

/*
=============
SV_CheckClientVisiblity

Check visibility through client camera, portal camera, etc
=============
*/
qboolean SV_CheckClientVisiblity( sv_client_t *cl, const byte *mask )
{
	int	i, clientnum;
	vec3_t	vieworg;
	mleaf_t	*leaf;

	if( !mask ) return true; // GoldSrc rules

	clientnum = cl - svs.clients;
	VectorCopy( viewPoint[clientnum], vieworg );

	// Invasion issues: wrong camera position received in ENGINE_SET_PVS
	if( cl->pViewEntity && !VectorCompare( vieworg, cl->pViewEntity->v.origin ))
		VectorCopy( cl->pViewEntity->v.origin, vieworg );

	leaf = Mod_PointInLeaf( vieworg, sv.worldmodel->nodes );

	if( CHECKVISBIT( mask, leaf->cluster ))
		return true; // visible from player view or camera view

	// now check all the portal cameras
	for( i = 0; i < cl->num_viewents; i++ )
	{
		edict_t	*view = cl->viewentity[i];

		if( !SV_IsValidEdict( view ))
			continue;

		VectorAdd( view->v.origin, view->v.view_ofs, vieworg );
		leaf = Mod_PointInLeaf( vieworg, sv.worldmodel->nodes );

		if( CHECKVISBIT( mask, leaf->cluster ))
			return true; // visible from portal camera view
	}

	// not visible from any viewpoint
	return false;
}

/*
=================
SV_Multicast

Sends the contents of sv.multicast to a subset of the clients,
then clears sv.multicast.

MSG_INIT	write message into signon buffer
MSG_ONE	send to one client (ent can't be NULL)
MSG_ALL	same as broadcast (origin can be NULL)
MSG_PVS	send to clients potentially visible from org
MSG_PHS	send to clients potentially audible from org
=================
*/
static int SV_Multicast( int dest, const vec3_t origin, const edict_t *ent, qboolean usermessage, qboolean filter )
{
	byte		*mask = NULL;
	int		j, numclients = svs.maxclients;
	sv_client_t	*cl, *current = svs.clients;
	qboolean		reliable = false;
	qboolean		specproxy = false;
	int		numsends = 0;

	// some mods trying to send messages after SV_FinalMessage
	if( !svs.initialized || sv.state == ss_dead )
	{
		MSG_Clear( &sv.multicast );
		return 0;
	}

	switch( dest )
	{
	case MSG_INIT:
		if( sv.state == ss_loading )
		{
			// copy to signon buffer
			MSG_WriteBits( &sv.signon, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
			MSG_Clear( &sv.multicast );
			return 1;
		}
		// intentional fallthrough (in-game MSG_INIT it's a MSG_ALL reliable)
	case MSG_ALL:
		reliable = true;
		// intentional fallthrough
	case MSG_BROADCAST:
		// nothing to sort	
		break;
	case MSG_PAS_R:
		reliable = true;
		// intentional fallthrough
	case MSG_PAS:
		if( origin == NULL ) return false;
		// NOTE: GoldSource not using PHS for singleplayer
		Mod_FatPVS( origin, FATPHS_RADIUS, fatphs, world.fatbytes, false, ( svs.maxclients == 1 ));
		mask = fatphs; // using the FatPVS like a PHS
		break;
	case MSG_PVS_R:
		reliable = true;
		// intentional fallthrough
	case MSG_PVS:
		if( origin == NULL ) return 0;
		mask = Mod_GetPVSForPoint( origin );
		break;
	case MSG_ONE:
		reliable = true;
		// intentional fallthrough
	case MSG_ONE_UNRELIABLE:
		if( !SV_IsValidEdict( ent )) return 0;
		j = NUM_FOR_EDICT( ent );
		if( j < 1 || j > numclients ) return 0;
		current = svs.clients + (j - 1);
		numclients = 1; // send to one
		break;
	case MSG_SPEC:
		specproxy = reliable = true;
		break;
	default:
		Host_Error( "SV_Multicast: bad dest: %i\n", dest );
		return 0;
	}

	// send the data to all relevent clients (or once only)
	for( j = 0, cl = current; j < numclients; j++, cl++ )
	{
		if( cl->state == cs_free || cl->state == cs_zombie )
			continue;

		if( cl->state != cs_spawned && ( !reliable || usermessage ))
			continue;

		if( specproxy && !FBitSet( cl->flags, FCL_HLTV_PROXY ))
			continue;

		if( !cl->edict || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		// reject step sounds while predicting is enabled
		// FIXME: make sure what this code doesn't cutoff something important!!!
		if( filter && cl == sv.current_client && FBitSet( sv.current_client->flags, FCL_PREDICT_MOVEMENT ))
			continue;

		if( SV_IsValidEdict( ent ) && ent->v.groupinfo && cl->edict->v.groupinfo )
		{
			if( svs.groupop == GROUP_OP_AND && !FBitSet( cl->edict->v.groupinfo, ent->v.groupinfo ))
				continue;

			if( svs.groupop == GROUP_OP_NAND && FBitSet( cl->edict->v.groupinfo, ent->v.groupinfo ))
				continue;
		}

		if( !SV_CheckClientVisiblity( cl, mask ))
			continue;

		if( specproxy ) MSG_WriteBits( &sv.spec_datagram, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
		else if( reliable ) MSG_WriteBits( &cl->netchan.message, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
		else MSG_WriteBits( &cl->datagram, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
		numsends++;
	}

	MSG_Clear( &sv.multicast );

	return numsends; // just for debug
}

/*
=======================
SV_GetReliableDatagram

Get shared reliable buffer
=======================
*/
sizebuf_t *SV_GetReliableDatagram( void )
{
	return &sv.reliable_datagram;
}

/*
=======================
SV_RestoreCustomDecal

Let the user spawn decal in game code
=======================
*/
qboolean SV_RestoreCustomDecal( decallist_t *entry, edict_t *pEdict, qboolean adjacent )
{
	if( svgame.physFuncs.pfnRestoreDecal != NULL )
	{
		if( !pEdict ) pEdict = EDICT_NUM( entry->entityIndex );
		// true if decal was sucessfully restored at the game-side
		return svgame.physFuncs.pfnRestoreDecal( entry, pEdict, adjacent );
	}
	return false;
}

/*
=======================
SV_CreateDecal

NOTE: static decals only accepted when game is loading
=======================
*/
void SV_CreateDecal( sizebuf_t *msg, const float *origin, int decalIndex, int entityIndex, int modelIndex, int flags, float scale )
{
	if( msg == &sv.signon && sv.state != ss_loading )
		return;

	// this can happens if serialized map contain 4096 static decals...
	if( MSG_GetNumBytesLeft( msg ) < 20 )
	{
		sv.ignored_world_decals++;
		return;
	}

	// static decals are posters, it's always reliable
	MSG_BeginServerCmd( msg, svc_bspdecal );
	MSG_WriteVec3Coord( msg, origin );
	MSG_WriteWord( msg, decalIndex );
	MSG_WriteShort( msg, entityIndex );
	if( entityIndex > 0 )
		MSG_WriteWord( msg, modelIndex );
	MSG_WriteByte( msg, flags );
	MSG_WriteWord( msg, scale * 4096 );
}

/*
=======================
SV_CreateStaticEntity

NOTE: static entities only accepted when game is loading
=======================
*/
qboolean SV_CreateStaticEntity( sizebuf_t *msg, int index )
{
	entity_state_t	nullstate, *baseline;	
	entity_state_t	*state;
	int		offset;

	if( index >= ( MAX_STATIC_ENTITIES - 1 ))
	{
		if( !sv.static_ents_overflow )
		{
			Con_Printf( S_WARN "MAX_STATIC_ENTITIES limit exceeded (%d)\n", MAX_STATIC_ENTITIES );
			sv.static_ents_overflow = true;
		}

		sv.ignored_static_ents++; // continue overflowed entities
		return false;
	}

	// this can happens if serialized map contain too many static entities...
	if( MSG_GetNumBytesLeft( msg ) < 50 )
	{
		sv.ignored_static_ents++;
		return false;
	}

	state = &svs.static_entities[index]; // allocate a new one
	memset( &nullstate, 0, sizeof( nullstate ));
	baseline = &nullstate;

	// restore modelindex from modelname (already precached)
	state->modelindex = pfnModelIndex( STRING( state->messagenum ));
	state->entityType = ENTITY_NORMAL; // select delta-encode
	state->number = 0;

	// trying to compress with previous delta's
	offset = SV_FindBestBaselineForStatic( index, &baseline, state );

	MSG_BeginServerCmd( msg, svc_spawnstatic );
	MSG_WriteDeltaEntity( baseline, state, msg, true, DELTA_STATIC, sv.time, offset );

	return true;
}

/*
=================
SV_RestartStaticEnts

Write all the static ents into demo
=================
*/
void SV_RestartStaticEnts( void )
{
	int	i;

	// remove all the static entities on the client
	CL_ClearStaticEntities();

	// resend them again
	for( i = 0; i < sv.num_static_entities; i++ )
		SV_CreateStaticEntity( &sv.reliable_datagram, i );
}

/*
=================
SV_RestartAmbientSounds

Write ambient sounds into demo
=================
*/
void SV_RestartAmbientSounds( void )
{
	soundlist_t	soundInfo[256];
	string		curtrack, looptrack;
	int		i, nSounds;
	int		position;

	if( !SV_Active( )) return;

	nSounds = S_GetCurrentStaticSounds( soundInfo, 256 );
	
	for( i = 0; i < nSounds; i++ )
	{
		soundlist_t *si = &soundInfo[i];

		if( !si->looping || si->entnum == -1 )
			continue;

		S_StopSound( si->entnum, si->channel, si->name );
		SV_StartSound( pfnPEntityOfEntIndex( si->entnum ), CHAN_STATIC, si->name, si->volume, si->attenuation, 0, si->pitch );
	}

#if !XASH_DEDICATED // TODO: ???
	// restart soundtrack
	if( S_StreamGetCurrentState( curtrack, looptrack, &position ))
	{
		SV_StartMusic( curtrack, looptrack, position );
	}
#endif
}

/*
=================
SV_RestartDecals

Write all the decals into demo
=================
*/
void SV_RestartDecals( void )
{
	decallist_t	*entry;
	int		decalIndex;
	int		modelIndex;
	sizebuf_t		*msg;
	int		i;

	if( !SV_Active( )) return;

	// g-cont. add space for studiodecals if present
	host.decalList = (decallist_t *)Z_Calloc( sizeof( decallist_t ) * MAX_RENDER_DECALS * 2 );

#if !XASH_DEDICATED
	if( !Host_IsDedicated() )
	{
		host.numdecals = ref.dllFuncs.R_CreateDecalList( host.decalList );

		// remove decals from map
		ref.dllFuncs.R_ClearAllDecals();
	}
	else
#endif // XASH_DEDICATED
	{
		// we probably running a dedicated server
		host.numdecals = 0;
	}

	// write decals into reliable datagram
	msg = SV_GetReliableDatagram();

	// restore decals and write them into network message
	for( i = 0; i < host.numdecals; i++ )
	{
		entry = &host.decalList[i];
		modelIndex = pfnPEntityOfEntIndex( entry->entityIndex )->v.modelindex;

		// game override
		if( SV_RestoreCustomDecal( entry, pfnPEntityOfEntIndex( entry->entityIndex ), false ))
			continue;

		decalIndex = pfnDecalIndex( entry->name );

		// studiodecals will be restored at game-side
		if( !FBitSet( entry->flags, FDECAL_STUDIO ))
			SV_CreateDecal( msg, entry->position, decalIndex, entry->entityIndex, modelIndex, entry->flags, entry->scale );
	}

	Z_Free( host.decalList );
	host.decalList = NULL;
	host.numdecals = 0;
}

/*
==============
SV_BoxInPVS

check brush boxes in fat pvs
==============
*/
qboolean SV_BoxInPVS( const vec3_t org, const vec3_t absmin, const vec3_t absmax )
{
	if( !Mod_BoxVisible( absmin, absmax, Mod_GetPVSForPoint( org )))
		return false;
	return true;
}

/*
=============
SV_ChangeLevel

Issue changing level
=============
*/
void SV_QueueChangeLevel( const char *level, const char *landname )
{
	int	flags, smooth = false;
	char	mapname[MAX_QPATH];
	char	*spawn_entity;

	// hold mapname to other place
	Q_strncpy( mapname, level, sizeof( mapname ));
	COM_StripExtension( mapname );

	if( COM_CheckString( landname ))
		smooth = true;

	// determine spawn entity classname
	if( svs.maxclients == 1 )
		spawn_entity = GI->sp_entity;
	else spawn_entity = GI->mp_entity;

	flags = SV_MapIsValid( mapname, spawn_entity, landname );

	if( FBitSet( flags, MAP_INVALID_VERSION ))
	{
		Con_Printf( S_ERROR "changelevel: %s is invalid or not supported\n", mapname );
		return;
	}
	
	if( !FBitSet( flags, MAP_IS_EXIST ))
	{
		Con_Printf( S_ERROR "changelevel: map %s doesn't exist\n", mapname );
		return;
	}

	if( smooth && !FBitSet( flags, MAP_HAS_LANDMARK ))
	{
		if( sv_validate_changelevel->value )
		{
			// NOTE: we find valid map but specified landmark it's doesn't exist
			// run simple changelevel like in q1, throw warning
			Con_Printf( S_WARN "changelevel: %s doesn't contain landmark [%s]. smooth transition was disabled\n", mapname, landname );
			smooth = false;
		}
	}

	if( svs.maxclients > 1 )
		smooth = false; // multiplayer doesn't support smooth transition

	if( smooth && !Q_stricmp( sv.name, level ))
	{
		Con_Printf( S_ERROR "can't changelevel with same map. Ignored.\n" );
		return;	
	}

	if( !smooth && !FBitSet( flags, MAP_HAS_SPAWNPOINT ))
	{
		if( sv_validate_changelevel->value )
		{
			Con_Printf( S_ERROR "changelevel: %s doesn't have a valid spawnpoint. Ignored.\n", mapname );
			return;	
		}
	}

	// bad changelevel position invoke enables in one-way transition
	if( sv.framecount < 15 )
	{
		if( sv_validate_changelevel->value )
		{
			Con_Printf( S_WARN "an infinite changelevel was detected and will be disabled until a next save\\restore\n" );
			return; // lock with svs.spawncount here
		}
	}

	SV_SkipUpdates ();

	// changelevel will be executed on a next frame
	if( smooth ) COM_ChangeLevel( mapname, landname, sv.background );	// Smoothed Half-Life changelevel
	else COM_ChangeLevel( mapname, NULL, sv.background );		// Classic Quake changlevel
}

/*
==============
SV_WriteEntityPatch

Create entity patch for selected map
==============
*/
void SV_WriteEntityPatch( const char *filename )
{
	int		lumpofs = 0, lumplen = 0;
	byte		buf[MAX_TOKEN]; // 1 kb
	string		bspfilename;
	dheader_t		*header;
	file_t		*f;

	Q_strncpy( bspfilename, va( "maps/%s.bsp", filename ), sizeof( bspfilename ));
	f = FS_Open( bspfilename, "rb", false );
	if( !f ) return;

	memset( buf, 0, MAX_TOKEN );
	FS_Read( f, buf, MAX_TOKEN );
	header = (dheader_t *)buf;

	// check all the lumps and some other errors
	if( !Mod_TestBmodelLumps( bspfilename, buf, true ))
	{
		FS_Close( f );
		return;
	}

	lumpofs = header->lumps[LUMP_ENTITIES].fileofs;
	lumplen = header->lumps[LUMP_ENTITIES].filelen;

	if( lumplen >= 10 )
	{
		char	*entities = NULL;
		
		FS_Seek( f, lumpofs, SEEK_SET );
		entities = (char *)Z_Calloc( lumplen + 1 );
		FS_Read( f, entities, lumplen );
		FS_WriteFile( va( "maps/%s.ent", filename ), entities, lumplen );
		Con_Printf( "Write 'maps/%s.ent'\n", filename );
		Mem_Free( entities );
	}

	FS_Close( f );
}

/*
==============
SV_ReadEntityScript

pfnMapIsValid use this
==============
*/
static char *SV_ReadEntityScript( const char *filename, int *flags )
{
	string		bspfilename, entfilename;
	int		lumpofs = 0, lumplen = 0;
	byte		buf[MAX_TOKEN];
	char		*ents = NULL;
	dheader_t		*header;
	size_t		ft1, ft2;
	file_t		*f;

	*flags = 0;

	Q_strncpy( bspfilename, va( "maps/%s.bsp", filename ), sizeof( bspfilename ));			
	f = FS_Open( bspfilename, "rb", false );
	if( !f ) return NULL;

	SetBits( *flags, MAP_IS_EXIST );
	memset( buf, 0, MAX_TOKEN );
	FS_Read( f, buf, MAX_TOKEN );
	header = (dheader_t *)buf;

	// check all the lumps and some other errors
	if( !Mod_TestBmodelLumps( bspfilename, buf, (host_developer.value) ? false : true ))
	{
		SetBits( *flags, MAP_INVALID_VERSION );
		FS_Close( f );
		return NULL;
	}

	// after call Mod_TestBmodelLumps we gurantee what map is valid
	lumpofs = header->lumps[LUMP_ENTITIES].fileofs;
	lumplen = header->lumps[LUMP_ENTITIES].filelen;

	// check for entfile too
	Q_strncpy( entfilename, va( "maps/%s.ent", filename ), sizeof( entfilename ));

	// make sure what entity patch is newer than bsp
	ft1 = FS_FileTime( bspfilename, false );
	ft2 = FS_FileTime( entfilename, true );

	if( ft2 != -1 && ft1 < ft2 )
	{
		// grab .ent files only from gamedir
		ents = (char *)FS_LoadFile( entfilename, NULL, true );
	}

	// at least entities should contain "{ "classname" "worldspawn" }\0"
	// for correct spawn the level
	if( !ents && lumplen >= 32 )
	{
		FS_Seek( f, lumpofs, SEEK_SET );
		ents = Z_Calloc( lumplen + 1 );
		FS_Read( f, ents, lumplen );
	}
	FS_Close( f ); // all done

	return ents;
}

/*
==============
SV_MapIsValid

Validate map
==============
*/
int SV_MapIsValid( const char *filename, const char *spawn_entity, const char *landmark_name )
{
	int	flags = 0;
	char	*pfile;
	char	*ents;

	ents = SV_ReadEntityScript( filename, &flags );

	if( ents )
	{
		qboolean	need_landmark = Q_strlen( landmark_name ) > 0 ? true : false;
		char	token[MAX_TOKEN];
		string	check_name;

		// g-cont. in-dev mode we can entering on map even without "info_player_start"
		if( !need_landmark && host_developer.value )
		{
			// not transition 
			Mem_Free( ents );

			// skip spawnpoint checks in devmode
			return (flags|MAP_HAS_SPAWNPOINT);
		}

		pfile = ents;

		while(( pfile = COM_ParseFile( pfile, token )) != NULL )
		{
			if( !Q_strcmp( token, "classname" ))
			{
				// check classname for spawn entity
				pfile = COM_ParseFile( pfile, check_name );
				if( !Q_strcmp( spawn_entity, check_name ))
				{
					SetBits( flags, MAP_HAS_SPAWNPOINT );

					// we already find landmark, stop the parsing
					if( need_landmark && FBitSet( flags, MAP_HAS_LANDMARK ))
						break;
				}
			}
			else if( need_landmark && !Q_strcmp( token, "targetname" ))
			{
				// check targetname for landmark entity
				pfile = COM_ParseFile( pfile, check_name );

				if( !Q_strcmp( landmark_name, check_name ))
				{
					SetBits( flags, MAP_HAS_LANDMARK );

					// we already find spawnpoint, stop the parsing
					if( FBitSet( flags, MAP_HAS_SPAWNPOINT ))
						break;
				}
			}
		}

		Mem_Free( ents );
	}

	return flags;
}

/*
==============
SV_FreePrivateData

release private edict memory
==============
*/
void SV_FreePrivateData( edict_t *pEdict )
{
	if( !pEdict || !pEdict->pvPrivateData )
		return;

	// NOTE: new interface can be missing
	if( svgame.dllFuncs2.pfnOnFreeEntPrivateData != NULL )
		svgame.dllFuncs2.pfnOnFreeEntPrivateData( pEdict );

	if( Mem_IsAllocatedExt( svgame.mempool, pEdict->pvPrivateData ))
		Mem_Free( pEdict->pvPrivateData );

	pEdict->pvPrivateData = NULL;
}

/*
==============
SV_InitEdict

clear edict for reuse
==============
*/
void SV_InitEdict( edict_t *pEdict )
{
	Assert( pEdict != NULL );

	SV_FreePrivateData( pEdict );
	memset( &pEdict->v, 0, sizeof( entvars_t ));
	pEdict->v.pContainingEntity = pEdict;
	pEdict->v.controller[0] = 0x7F;
	pEdict->v.controller[1] = 0x7F;
	pEdict->v.controller[2] = 0x7F;
	pEdict->v.controller[3] = 0x7F;
	pEdict->free = false;
}

/*
==============
SV_FreeEdict

unlink edict from world and free it
==============
*/
void SV_FreeEdict( edict_t *pEdict )
{
	Assert( pEdict != NULL );
	if( pEdict->free ) return;

	// unlink from world
	SV_UnlinkEdict( pEdict );

	SV_FreePrivateData( pEdict );

	// mark edict as freed
	pEdict->freetime = sv.time;
	pEdict->serialnumber++; // invalidate EHANDLE's
	pEdict->v.solid = SOLID_NOT;
	pEdict->v.flags = 0;
	pEdict->v.model = 0;
	pEdict->v.takedamage = 0;
	pEdict->v.modelindex = 0;
	pEdict->v.nextthink = -1;
	pEdict->v.colormap = 0;
	pEdict->v.frame = 0;
	pEdict->v.scale = 0;
	pEdict->v.gravity = 0;
	pEdict->v.skin = 0;

	VectorClear( pEdict->v.angles );
	VectorClear( pEdict->v.origin );
	pEdict->free = true;
}

/*
==============
SV_AllocEdict

allocate new or reuse existing
==============
*/
edict_t *SV_AllocEdict( void )
{
	edict_t	*e;
	int	i;

	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		e = EDICT_NUM( i );
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if( e->free && ( e->freetime < 2.0f || ( sv.time - e->freetime ) > 0.5f ))
		{
			SV_InitEdict( e );
			return e;
		}
	}

	if( i >= GI->max_edicts )
		Host_Error( "ED_AllocEdict: no free edicts (max is %d)\n", GI->max_edicts );

	svgame.numEntities++;
	e = EDICT_NUM( i );
	SV_InitEdict( e );

	return e;
}

/*
==============
SV_GetEntityClass

get pointer for entity class
==============
*/
LINK_ENTITY_FUNC SV_GetEntityClass( const char *pszClassName )
{
	// allocate edict private memory (passed by dlls)
	return (LINK_ENTITY_FUNC)COM_GetProcAddress( svgame.hInstance, pszClassName );
}

/*
==============
SV_AllocPrivateData

allocate private data for a given edict
==============
*/
edict_t* SV_AllocPrivateData( edict_t *ent, string_t className )
{
	const char	*pszClassName;
	LINK_ENTITY_FUNC	SpawnEdict;

	pszClassName = STRING( className );

	if( !ent )
	{
		// allocate a new one
		ent = SV_AllocEdict();
	}
	else if( ent->free )
	{
		SV_InitEdict( ent ); // re-init edict
	}

	ent->v.classname = className;
	ent->v.pContainingEntity = ent; // re-link
	
	// allocate edict private memory (passed by dlls)
	SpawnEdict = SV_GetEntityClass( pszClassName );

	if( !SpawnEdict )
	{
		// attempt to create custom entity (Xash3D extension)
		if( svgame.physFuncs.SV_CreateEntity && svgame.physFuncs.SV_CreateEntity( ent, pszClassName ) != -1 )
			return ent;

		SpawnEdict = SV_GetEntityClass( "custom" );

		if( !SpawnEdict )
		{
			Con_Printf( S_ERROR "No spawn function for %s\n", STRING( className ));

			// free entity immediately
			SV_FreeEdict( ent );

			return NULL;
		}

		SetBits( ent->v.flags, FL_CUSTOMENTITY ); // it's a custom entity but not a beam!
	}

	SpawnEdict( &ent->v );

	return ent;
}

/*
==============
SV_CreateNamedEntity

create specified entity, alloc private data
==============
*/
edict_t* SV_CreateNamedEntity( edict_t *ent, string_t className )
{
	edict_t *ed = SV_AllocPrivateData( ent, className );
	
	// for some reasons this flag should be immediately cleared
	if( ed ) ClearBits( ed->v.flags, FL_CUSTOMENTITY );

	return ed;
}

/*
==============
SV_FreeEdicts

release all the edicts from server
==============
*/
void SV_FreeEdicts( void )
{
	int	i = 0;
	edict_t	*ent;

	for( i = 0; i < svgame.numEntities; i++ )
	{
		ent = EDICT_NUM( i );
		if( ent->free ) continue;
		SV_FreeEdict( ent );
	}
}

/*
==============
SV_PlaybackReliableEvent

reliable event is must be delivered always
==============
*/
void SV_PlaybackReliableEvent( sizebuf_t *msg, word eventindex, float delay, event_args_t *args )
{
	event_args_t nullargs;

	memset( &nullargs, 0, sizeof( nullargs ));

	MSG_BeginServerCmd( msg, svc_event_reliable );

	// send event index
	MSG_WriteUBitLong( msg, eventindex, MAX_EVENT_BITS );

	if( delay )
	{
		// send event delay
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteWord( msg, ( delay * 100.0f ));
	}
	else MSG_WriteOneBit( msg, 0 );

	// reliable events not use delta-compression just null-compression
	MSG_WriteDeltaEvent( msg, &nullargs, args );
}

/*
==============
SV_ClassName

template to get edict classname
==============
*/
const char *SV_ClassName( const edict_t *e )
{
	if( !e ) return "(null)";
	if( e->free ) return "freed";
	return STRING( e->v.classname );
}

/*
==============
SV_IsValidCmd

command validation
==============
*/
static qboolean SV_IsValidCmd( const char *pCmd )
{
	size_t	len = Q_strlen( pCmd );

	// valid commands all have a ';' or newline '\n' as their last character
	if( len && ( pCmd[len-1] == '\n' || pCmd[len-1] == ';' ))
		return true;
	return false;
}

/*
==============
SV_AllocPrivateData

get edict that attached to the client structure
==============
*/
sv_client_t *SV_ClientFromEdict( const edict_t *pEdict, qboolean spawned_only )
{
	int	i;

	if( !SV_IsValidEdict( pEdict ))
		return NULL;

	i = NUM_FOR_EDICT( pEdict ) - 1;

	if( i < 0 || i >= svs.maxclients )
		return NULL;

	if( spawned_only )
	{
		if( svs.clients[i].state != cs_spawned )
			return NULL;
	}

	return (svs.clients + i);
}

/*
===============================================================================

	Game Builtin Functions

===============================================================================
*/
/*
=========
pfnPrecacheModel

=========
*/
int pfnPrecacheModel( const char *s )
{
	qboolean	optional = false;
	int	i;

	if( *s == '!' )
	{
		optional = true;
		s++;
	}

	if(( i = SV_ModelIndex( s )) == 0 )
		return 0;

	sv.models[i] = Mod_ForName( sv.model_precache[i], false, true );

	if( !optional )
		SetBits( sv.model_precache_flags[i], RES_FATALIFMISSING );

	return i;
}

/*
=================
pfnSetModel

=================
*/
void pfnSetModel( edict_t *e, const char *m )
{
	char	name[MAX_QPATH];
	qboolean	found = false;
	model_t	*mod;
	int	i = 1;

	if( !SV_IsValidEdict( e ))
		return;

	if( *m == '\\' || *m == '/' ) m++;
	Q_strncpy( name, m, sizeof( name ));
	COM_FixSlashes( name );

	if( COM_CheckString( name ))
	{
		// check to see if model was properly precached
		for( ; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
		{
			if( !Q_stricmp( sv.model_precache[i], name ))
			{
				found = true;
				break;
			}
		}

		if( !found )
		{
			Con_Printf( S_ERROR "Failed to set model %s: was not precached\n", name );
			return;
		}
	}

	if( e == svgame.edicts )
	{
		if( sv.state == ss_active )
			Con_Printf( S_ERROR "Failed to set model %s: world model cannot be changed\n", name );
		return;
	}

	if( COM_CheckString( name ))
	{
		e->v.model = MAKE_STRING( sv.model_precache[i] );
		e->v.modelindex = i;
		mod = sv.models[i];
	}
	else
	{
		// model will be cleared
		e->v.model = e->v.modelindex = 0;
		mod = NULL;
	}

	// set the model size
	if( mod && mod->type != mod_studio )
		SV_SetMinMaxSize( e, mod->mins, mod->maxs, true );
	else SV_SetMinMaxSize( e, vec3_origin, vec3_origin, true );
}

/*
=================
pfnModelIndex

=================
*/
int pfnModelIndex( const char *m )
{
	char	name[MAX_QPATH];
	int	i;

	if( !COM_CheckString( m ))
		return 0;

	if( *m == '\\' || *m == '/' ) m++;
	Q_strncpy( name, m, sizeof( name ));
	COM_FixSlashes( name );

	for( i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
	{
		if( !Q_stricmp( sv.model_precache[i], name ))
			return i;
	}

	Con_Printf( S_ERROR "Cannot get index for model %s: not precached\n", name );
	return 0;
}

/*
=================
pfnModelFrames

=================
*/
int pfnModelFrames( int modelIndex )
{
	model_t	*pmodel = SV_ModelHandle( modelIndex );

	if( pmodel != NULL )
		return pmodel->numframes;
	return 1;
}

/*
=================
pfnSetSize

=================
*/
void pfnSetSize( edict_t *e, const float *rgflMin, const float *rgflMax )
{
	if( !SV_IsValidEdict( e ))
		return;

	SV_SetMinMaxSize( e, rgflMin, rgflMax, true );
}

/*
=================
pfnChangeLevel

=================
*/
void pfnChangeLevel( const char *level, const char *landmark )
{
	static uint	last_spawncount = 0;
	char		landname[MAX_QPATH];
	char		*text;

	if( !COM_CheckString( level ) || sv.state != ss_active )
		return; // ???

	// make sure we don't issue two changelevels
	if( svs.spawncount == last_spawncount )
		return;
	last_spawncount = svs.spawncount;
	landname[0] ='\0';

#ifdef HACKS_RELATED_HLMODS
	// g-cont. some level-designers wrote landmark name with space
	// and Cmd_TokenizeString separating all the after space as next argument
	// emulate this bug for compatibility
	if( COM_CheckString( landmark ))
	{
		text = (char *)landname;
		while( *landmark && ((byte)*landmark) != ' ' )
			*text++ = *landmark++;
		*text = '\0';
	}
#else
	Q_strncpy( landname, landmark, sizeof( landname ));
#endif
	SV_QueueChangeLevel( level, landname );
}

/*
=================
pfnGetSpawnParms

OBSOLETE, UNUSED
=================
*/
void pfnGetSpawnParms( edict_t *ent )
{
}

/*
=================
pfnSaveSpawnParms

OBSOLETE, UNUSED
=================
*/
void pfnSaveSpawnParms( edict_t *ent )
{
}

/*
=================
pfnVecToYaw

=================
*/
float pfnVecToYaw( const float *rgflVector )
{
	return SV_VecToYaw( rgflVector );
}

/*
=================
pfnMoveToOrigin

=================
*/
void pfnMoveToOrigin( edict_t *ent, const float *pflGoal, float dist, int iMoveType )
{
	if( !pflGoal || !SV_IsValidEdict( ent ))
		return;

	SV_MoveToOrigin( ent, pflGoal, dist, iMoveType );
}

/*
==============
pfnChangeYaw

==============
*/
void pfnChangeYaw( edict_t* ent )
{
	if( !SV_IsValidEdict( ent ))
		return;

	ent->v.angles[YAW] = SV_AngleMod( ent->v.ideal_yaw, ent->v.angles[YAW], ent->v.yaw_speed );
}

/*
==============
pfnChangePitch

==============
*/
void pfnChangePitch( edict_t* ent )
{
	if( !SV_IsValidEdict( ent ))
		return;

	ent->v.angles[PITCH] = SV_AngleMod( ent->v.idealpitch, ent->v.angles[PITCH], ent->v.pitch_speed );	
}

/*
=========
SV_FindEntityByString

=========
*/
edict_t *SV_FindEntityByString( edict_t *pStartEdict, const char *pszField, const char *pszValue )
{
	int		index = 0, e = 0;
	TYPEDESCRIPTION	*desc = NULL;
	edict_t		*ed;
	const char	*t;

	if( !COM_CheckString( pszValue ))
		return svgame.edicts;

	if( pStartEdict ) e = NUM_FOR_EDICT( pStartEdict );

	while(( desc = SV_GetEntvarsDescirption( index++ )) != NULL )
	{
		if( !Q_strcmp( pszField, desc->fieldName ))
			break;
	}

	if( desc == NULL )
	{
		Con_Printf( S_ERROR "FindEntityByString: field %s not a string\n", pszField );
		return svgame.edicts;
	}
	
	for( e++; e < svgame.numEntities; e++ )
	{
		ed = EDICT_NUM( e );
		if( !SV_IsValidEdict( ed )) continue;

		if( e <= svs.maxclients && !SV_ClientFromEdict( ed, ( svs.maxclients != 1 )))
			continue;

		switch( desc->fieldType )
		{
		case FIELD_STRING:
		case FIELD_MODELNAME:
		case FIELD_SOUNDNAME:
			t = STRING( *(string_t *)&((byte *)&ed->v)[desc->fieldOffset] );
			if( t != NULL && t != svgame.globals->pStringBase )
			{
				if( !Q_strcmp( t, pszValue ))
					return ed;
			}
			break;
		default:
			ASSERT( 0 );
			break;
		}
	}

	return svgame.edicts;
}

/*
=========
SV_FindGlobalEntity

ripped out from the hl.dll
=========
*/
edict_t *SV_FindGlobalEntity( string_t classname, string_t globalname )
{
	edict_t *pent = SV_FindEntityByString( NULL,  "globalname", STRING( globalname ));

	if( SV_IsValidEdict( pent ))
	{
		// don't spam about error - game code already tell us
		if( Q_strcmp( SV_ClassName( pent ), STRING( classname )))
			pent = NULL;
	}

	return pent;
}

/*
==============
pfnGetEntityIllum

returns averaged lightvalue for entity
==============
*/
int pfnGetEntityIllum( edict_t* pEnt )
{
	if( !SV_IsValidEdict( pEnt ))
		return -1;

	return SV_LightForEntity( pEnt );
}

/*
=================
pfnFindEntityInSphere

find the entity in sphere
=================
*/
edict_t *pfnFindEntityInSphere( edict_t *pStartEdict, const float *org, float flRadius )
{
	float	distSquared;
	int	j, e = 0;
	float	eorg;
	edict_t	*ent;

	flRadius *= flRadius;

	if( SV_IsValidEdict( pStartEdict ))
		e = NUM_FOR_EDICT( pStartEdict );

	for( e++; e < svgame.numEntities; e++ )
	{
		ent = EDICT_NUM( e );

		if( !SV_IsValidEdict( ent ))
			continue;

		// ignore clients that not in a game
		if( e <= svs.maxclients && !SV_ClientFromEdict( ent, true ))
			continue;

		distSquared = 0.0f;

		for( j = 0; j < 3 && distSquared <= flRadius; j++ )
		{
			if( org[j] < ent->v.absmin[j] )
				eorg = org[j] - ent->v.absmin[j];
			else if( org[j] > ent->v.absmax[j] )
				eorg = org[j] - ent->v.absmax[j];
			else eorg = 0.0f;

			distSquared += eorg * eorg;
		}

		if( distSquared < flRadius )
			return ent;
	}

	return svgame.edicts;
}

/*
=================
SV_CheckClientPVS

build the new client PVS
=================
*/
int SV_CheckClientPVS( int check, qboolean bMergePVS )
{
	byte		*pvs;
	vec3_t		vieworg;
	sv_client_t	*cl;
	int		i, j, k;
	edict_t		*ent = NULL;

	// cycle to the next one
	check = bound( 1, check, svs.maxclients );

	if( check == svs.maxclients )
		i = 1; // reset cycle
	else i = check + 1;

	for( ;; i++ )
	{
		if( i == ( svs.maxclients + 1 ))
			i = 1;

		ent = EDICT_NUM( i );
		if( i == check ) break; // didn't find anything else

		if( ent->free || !ent->pvPrivateData || FBitSet( ent->v.flags, FL_NOTARGET ))
			continue;

		// anything that is a client, or has a client as an enemy
		break;
	}

	cl = SV_ClientFromEdict( ent, true );
	memset( clientpvs, 0xFF, world.visbytes );

	// get the PVS for the entity
	VectorAdd( ent->v.origin, ent->v.view_ofs, vieworg );
	pvs = Mod_GetPVSForPoint( vieworg );
	if( pvs ) memcpy( clientpvs, pvs, world.visbytes );

	// transition in progress
	if( !cl ) return i;

	// now merge PVS with all the portal cameras
	for( k = 0; k < cl->num_viewents && bMergePVS; k++ )
	{
		edict_t	*view = cl->viewentity[k];

		if( !SV_IsValidEdict( view ))
			continue;

		VectorAdd( view->v.origin, view->v.view_ofs, vieworg );
		pvs = Mod_GetPVSForPoint( vieworg );

		for( j = 0; j < world.visbytes && pvs; j++ )
			SetBits( clientpvs[j], pvs[j] );
	}

	return i;
}

/*
=================
pfnFindClientInPVS

=================
*/
edict_t* pfnFindClientInPVS( edict_t *pEdict )
{
	edict_t	*pClient;
	vec3_t	view;
	float	delta;
	model_t	*mod;
	qboolean	bMergePVS;
	mleaf_t	*leaf;

	if( !SV_IsValidEdict( pEdict ))
		return svgame.edicts;

	delta = ( sv.time - sv.lastchecktime );

	// don't merge visibility for portal entity, only for monsters
	bMergePVS = FBitSet( pEdict->v.flags, FL_MONSTER ) ? true : false;

	// find a new check if on a new frame
	if( delta < 0.0f || delta >= 0.1f )
	{
		sv.lastcheck = SV_CheckClientPVS( sv.lastcheck, bMergePVS );
		sv.lastchecktime = sv.time;
	}

	// return check if it might be visible	
	pClient = EDICT_NUM( sv.lastcheck );

	if( !SV_ClientFromEdict( pClient, true ))
		return svgame.edicts;

	mod = SV_ModelHandle( pEdict->v.modelindex );

	// portals & monitors
	// NOTE: this specific break "radiaton tick" in normal half-life. use only as feature
	if( FBitSet( host.features, ENGINE_PHYSICS_PUSHER_EXT ) && mod && mod->type == mod_brush && !FBitSet( mod->flags, MODEL_HAS_ORIGIN ))
	{
		// handle PVS origin for bmodels
		VectorAverage( pEdict->v.mins, pEdict->v.maxs, view );
		VectorAdd( view, pEdict->v.origin, view );
	}
	else
	{
		VectorAdd( pEdict->v.origin, pEdict->v.view_ofs, view );
	}

	leaf = Mod_PointInLeaf( view, sv.worldmodel->nodes );

	if( CHECKVISBIT( clientpvs, leaf->cluster ))
		return pClient; // client which currently in PVS

	return svgame.edicts;
}

/*
=================
pfnEntitiesInPVS

=================
*/
edict_t *pfnEntitiesInPVS( edict_t *pview )
{
	edict_t	*pchain, *ptest;
	vec3_t	viewpoint;
	edict_t	*pent;
	int	i;

	if( !SV_IsValidEdict( pview ))
		return NULL;

	VectorAdd( pview->v.origin, pview->v.view_ofs, viewpoint );
	pchain = EDICT_NUM( 0 ); 

	for( i = 1; i < svgame.numEntities; i++ )
	{
		pent = EDICT_NUM( i );

		if( !SV_IsValidEdict( pent ))
			continue;

		if( pent->v.movetype == MOVETYPE_FOLLOW && SV_IsValidEdict( pent->v.aiment ))
			ptest = pent->v.aiment;
		else ptest = pent;

		if( SV_BoxInPVS( viewpoint, ptest->v.absmin, ptest->v.absmax ))
		{
			pent->v.chain = pchain;
			pchain = pent;
		}
	}

	return pchain;
}

/*
==============
pfnMakeVectors

==============
*/
void pfnMakeVectors( const float *rgflVector )
{
	AngleVectors( rgflVector, svgame.globals->v_forward, svgame.globals->v_right, svgame.globals->v_up );
}

/*
==============
pfnRemoveEntity

free edict private mem, unlink physics etc
==============
*/
void pfnRemoveEntity( edict_t *e )
{
	if( !SV_IsValidEdict( e ))
		return;

	// never free client or world entity
	if( NUM_FOR_EDICT( e ) < ( svs.maxclients + 1 ))
	{
		Con_Printf( S_ERROR "can't delete %s\n", ( e == EDICT_NUM( 0 )) ? "world" : "client" );
		return;
	}

	SV_FreeEdict( e );
}

/*
==============
pfnCreateNamedEntity

==============
*/
edict_t* pfnCreateNamedEntity( string_t className )
{
	return SV_CreateNamedEntity( NULL, className );
}

/*
=============
pfnMakeStatic

move entity to client
=============
*/
static void pfnMakeStatic( edict_t *ent )
{
	entity_state_t	*state;

	if( !SV_IsValidEdict( ent ))
		return;

	// fill the entity state
	state = &svs.static_entities[sv.num_static_entities];	// allocate a new one
	svgame.dllFuncs.pfnCreateBaseline( false, NUM_FOR_EDICT( ent ), state, ent, 0, vec3_origin, vec3_origin );
	state->messagenum = ent->v.model; // member modelname

	if( SV_CreateStaticEntity( &sv.signon, sv.num_static_entities ))
		sv.num_static_entities++;

	// remove at end of the frame
	SetBits( ent->v.flags, FL_KILLME );
}

/*
=============
pfnEntIsOnFloor

legacy builtin
=============
*/
static int pfnEntIsOnFloor( edict_t *e )
{
	if( !SV_IsValidEdict( e ))
		return 0;

	return SV_CheckBottom( e, MOVE_NORMAL );
}
	
/*
===============
pfnDropToFloor

===============
*/
int pfnDropToFloor( edict_t* e )
{
	qboolean	monsterClip;
	trace_t	trace;
	vec3_t	end;

	if( !SV_IsValidEdict( e ))
		return 0;

	monsterClip = FBitSet( e->v.flags, FL_MONSTERCLIP ) ? true : false;
	VectorCopy( e->v.origin, end );
	end[2] -= 256.0f;

	trace = SV_Move( e->v.origin, e->v.mins, e->v.maxs, end, MOVE_NORMAL, e, monsterClip );

	if( trace.allsolid )
		return -1;

	if( trace.fraction == 1.0f )
		return 0;

	VectorCopy( trace.endpos, e->v.origin );
	SV_LinkEdict( e, false );
	SetBits( e->v.flags, FL_ONGROUND );
	e->v.groundentity = trace.ent;

	return 1;
}

/*
===============
pfnWalkMove

===============
*/
int pfnWalkMove( edict_t *ent, float yaw, float dist, int iMode )
{
	vec3_t	move;

	if( !SV_IsValidEdict( ent ))
		return 0;

	if( !FBitSet( ent->v.flags, FL_FLY|FL_SWIM|FL_ONGROUND ))
		return 0;

	yaw = DEG2RAD( yaw );
	VectorSet( move, cos( yaw ) * dist, sin( yaw ) * dist, 0.0f );

	switch( iMode )
	{
	case WALKMOVE_NORMAL:
		return SV_MoveStep( ent, move, true );
	case WALKMOVE_WORLDONLY:
		return SV_MoveTest( ent, move, true );
	case WALKMOVE_CHECKONLY:
		return SV_MoveStep( ent, move, false);
	}
	return 0;
}

/*
=================
pfnSetOrigin

=================
*/
void pfnSetOrigin( edict_t *e, const float *rgflOrigin )
{
	if( !SV_IsValidEdict( e ))
		return;

	VectorCopy( rgflOrigin, e->v.origin );
	SV_LinkEdict( e, false );
}

/*
=================
SV_BuildSoundMsg

=================
*/
int SV_BuildSoundMsg( sizebuf_t *msg, edict_t *ent, int chan, const char *sample, int vol, float attn, int flags, int pitch, const vec3_t pos )
{
	int	entityIndex;
	int	sound_idx;
	qboolean	spawn;

	if( vol < 0 || vol > 255 )
	{
		Con_Reportf( S_ERROR "SV_StartSound: volume = %i\n", vol );
		vol = bound( 0, vol, 255 );
	}

	if( attn < 0.0f || attn > 4.0f )
	{
		Con_Reportf( S_ERROR "SV_StartSound: attenuation %g must be in range 0-4\n", attn );
		attn = bound( 0.0f, attn, 4.0f );
	}

	if( chan < 0 || chan > 7 )
	{
		Con_Reportf( S_ERROR "SV_StartSound: channel must be in range 0-7\n" );
		chan = bound( 0, chan, 7 );
	}

	if( pitch < 0 || pitch > 255 )
	{
		Con_Reportf( S_ERROR "SV_StartSound: pitch = %i\n", pitch );
		pitch = bound( 0, pitch, 255 );
	}

	if( !COM_CheckString( sample ))
	{
		Con_Reportf( S_ERROR "SV_StartSound: passed NULL sample\n" );
		return 0;
	}

	if( sample[0] == '!' && Q_isdigit( sample + 1 ))
	{
		sound_idx = Q_atoi( sample + 1 );

		if( sound_idx >= MAX_SOUNDS_NONSENTENCE )
		{
			SetBits( flags, SND_SENTENCE|SND_SEQUENCE );
			sound_idx -= MAX_SOUNDS_NONSENTENCE;
		}
		else SetBits( flags, SND_SENTENCE );
	}
	else if( sample[0] == '#' && Q_isdigit( sample + 1 ))
	{
		SetBits( flags, SND_SENTENCE|SND_SEQUENCE );
		sound_idx = Q_atoi( sample + 1 );
	}
	else
	{
		// TESTTEST
		if( *sample == '*' ) chan = CHAN_AUTO;

		// precache_sound can be used twice: cache sounds when loading
		// and return sound index when server is active
		sound_idx = SV_SoundIndex( sample );
	}

	if( !sound_idx )
	{
		Con_Printf( S_ERROR "SV_StartSound: %s not precached (%d)\n", sample, sound_idx );
		return 0;
	}

	spawn = FBitSet( flags, SND_RESTORE_POSITION ) ? false : true;

	if( SV_IsValidEdict( ent ) && SV_IsValidEdict( ent->v.aiment ))
		entityIndex = NUM_FOR_EDICT( ent->v.aiment );
	else if( SV_IsValidEdict( ent ))
		entityIndex = NUM_FOR_EDICT( ent );
	else entityIndex = 0; // assume world

	if( vol != 255 ) SetBits( flags, SND_VOLUME );
	if( attn != ATTN_NONE ) SetBits( flags, SND_ATTENUATION );
	if( pitch != PITCH_NORM ) SetBits( flags, SND_PITCH );

	// not sending (because this is out of range)
	ClearBits( flags, SND_RESTORE_POSITION );
	ClearBits( flags, SND_FILTER_CLIENT );
	ClearBits( flags, SND_SPAWNING );

	if( spawn ) MSG_BeginServerCmd( msg, svc_sound );
	else MSG_BeginServerCmd( msg, svc_restoresound );
	MSG_WriteUBitLong( msg, flags, MAX_SND_FLAGS_BITS );
	MSG_WriteUBitLong( msg, sound_idx, MAX_SOUND_BITS );
	MSG_WriteUBitLong( msg, chan, MAX_SND_CHAN_BITS );

	if( FBitSet( flags, SND_VOLUME )) MSG_WriteByte( msg, vol );
	if( FBitSet( flags, SND_ATTENUATION )) MSG_WriteByte( msg, attn * 64 );
	if( FBitSet( flags, SND_PITCH )) MSG_WriteByte( msg, pitch );

	MSG_WriteUBitLong( msg, entityIndex, MAX_ENTITY_BITS );
	MSG_WriteVec3Coord( msg, pos );

	return 1;
}

/*
=================
SV_StartSound

=================
*/
void SV_StartSound( edict_t *ent, int chan, const char *sample, float vol, float attn, int flags, int pitch )
{
	qboolean	filter = false;
	int	msg_dest;
	vec3_t	origin;

	if( !SV_IsValidEdict( ent ))
		return;

	VectorAverage( ent->v.mins, ent->v.maxs, origin );
	VectorAdd( origin, ent->v.origin, origin );

	if( FBitSet( flags, SND_SPAWNING ))
		msg_dest = MSG_INIT;
	else if( chan == CHAN_STATIC )
		msg_dest = MSG_ALL;
	else if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		msg_dest = MSG_ALL;
	else msg_dest = (svs.maxclients <= 1 ) ? MSG_ALL : MSG_PAS_R;

	// always sending stop sound command
	if( FBitSet( flags, SND_STOP ))
		msg_dest = MSG_ALL;

	if( FBitSet( flags, SND_FILTER_CLIENT ))
		filter = true;

	if( SV_BuildSoundMsg( &sv.multicast, ent, chan, sample, vol * 255, attn, flags, pitch, origin ))
		SV_Multicast( msg_dest, origin, NULL, false, filter );
}

/*
=================
pfnEmitAmbientSound

=================
*/
void pfnEmitAmbientSound( edict_t *ent, float *pos, const char *sample, float vol, float attn, int flags, int pitch )
{
	int	msg_dest;

	if( sv.state == ss_loading )
		SetBits( flags, SND_SPAWNING );

	if( FBitSet( flags, SND_SPAWNING ))
		msg_dest = MSG_INIT;
	else msg_dest = MSG_ALL;

	// always sending stop sound command
	if( FBitSet( flags, SND_STOP ))
		msg_dest = MSG_ALL;

	if( SV_BuildSoundMsg( &sv.multicast, ent, CHAN_STATIC, sample, vol * 255, attn, flags, pitch, pos ))
		SV_Multicast( msg_dest, pos, NULL, false, false );
}

/*
=================
SV_StartMusic

=================
*/
void SV_StartMusic( const char *curtrack, const char *looptrack, int position )
{
	MSG_BeginServerCmd( &sv.multicast, svc_stufftext );
	MSG_WriteString( &sv.multicast, va( "music \"%s\" \"%s\" %d\n", curtrack, looptrack, position ));
	SV_Multicast( MSG_ALL, NULL, NULL, false, false );
}

/*
=================
pfnTraceLine

=================
*/
static void pfnTraceLine( const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr )
{
	trace_t	trace;

	trace = SV_Move( v1, vec3_origin, vec3_origin, v2, fNoMonsters, pentToSkip, false );
	if( !SV_IsValidEdict( trace.ent ))
		trace.ent = svgame.edicts;
	SV_ConvertTrace( ptr, &trace );
}

/*
=================
pfnTraceToss

=================
*/
static void pfnTraceToss( edict_t *pent, edict_t *pentToIgnore, TraceResult *ptr )
{
	trace_t	trace;

	if( !SV_IsValidEdict( pent ))
		return;

	trace = SV_MoveToss( pent, pentToIgnore );
	SV_ConvertTrace( ptr, &trace );
}

/*
=================
pfnTraceHull

=================
*/
static void pfnTraceHull( const float *v1, const float *v2, int fNoMonsters, int hullNumber, edict_t *pentToSkip, TraceResult *ptr )
{
	trace_t	trace;

	if( hullNumber < 0 || hullNumber > 3 )
		hullNumber = 0;

	trace = SV_Move( v1, sv.worldmodel->hulls[hullNumber].clip_mins, sv.worldmodel->hulls[hullNumber].clip_maxs, v2, fNoMonsters, pentToSkip, false );
	SV_ConvertTrace( ptr, &trace );
}

/*
=============
pfnTraceMonsterHull

=============
*/
static int pfnTraceMonsterHull( edict_t *pEdict, const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr )
{
	qboolean	monsterClip;
	trace_t	trace;

	if( !SV_IsValidEdict( pEdict ))
		return 0;

	monsterClip = FBitSet( pEdict->v.flags, FL_MONSTERCLIP ) ? true : false;
	trace = SV_Move( v1, pEdict->v.mins, pEdict->v.maxs, v2, fNoMonsters, pentToSkip, monsterClip );
	SV_ConvertTrace( ptr, &trace );

	if( trace.allsolid || trace.fraction != 1.0f )
		return true;
	return false;
}

/*
=============
pfnTraceModel

=============
*/
static void pfnTraceModel( const float *v1, const float *v2, int hullNumber, edict_t *pent, TraceResult *ptr )
{
	float	*mins, *maxs;
	model_t	*model;
	trace_t	trace;

	if( !SV_IsValidEdict( pent ))
		return;

	if( hullNumber < 0 || hullNumber > 3 )
		hullNumber = 0;

	mins = sv.worldmodel->hulls[hullNumber].clip_mins;
	maxs = sv.worldmodel->hulls[hullNumber].clip_maxs;
	model = SV_ModelHandle( pent->v.modelindex );

	if( pent->v.solid == SOLID_CUSTOM )
	{
		// NOTE: always goes through custom clipping move
		// even if our callbacks is not initialized
		SV_CustomClipMoveToEntity( pent, v1, mins, maxs, v2, &trace );
	}
	else if( model && model->type == mod_brush )
	{
		int oldmovetype = pent->v.movetype;
		int oldsolid = pent->v.solid;
		pent->v.movetype = MOVETYPE_PUSH;
		pent->v.solid = SOLID_BSP;

		SV_ClipMoveToEntity( pent, v1, mins, maxs, v2, &trace );

		pent->v.movetype = oldmovetype;
		pent->v.solid = oldsolid;
	}
	else
	{
		SV_ClipMoveToEntity( pent, v1, mins, maxs, v2, &trace );
	}

	SV_ConvertTrace( ptr, &trace );
}

/*
=============
pfnTraceTexture

returns texture basename
=============
*/
static const char *pfnTraceTexture( edict_t *pTextureEntity, const float *v1, const float *v2 )
{
	if( !SV_IsValidEdict( pTextureEntity ))
		return NULL;

	return SV_TraceTexture( pTextureEntity, v1, v2 );
}

/*
=============
pfnTraceSphere

OBSOLETE, UNUSED
=============
*/
void pfnTraceSphere( const float *v1, const float *v2, int fNoMonsters, float radius, edict_t *pentToSkip, TraceResult *ptr )
{
}

/*
=============
pfnGetAimVector

NOTE: speed is unused
=============
*/
void pfnGetAimVector( edict_t* ent, float speed, float *rgflReturn )
{
	edict_t		*check;
	vec3_t		start, dir, end, bestdir;
	float		dist, bestdist;
	int		i, j;
	trace_t		tr;

	VectorCopy( svgame.globals->v_forward, rgflReturn );	// assume failure if it returns early

	if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_FAKECLIENT ))
		return;

	VectorCopy( ent->v.origin, start );
	VectorAdd( start, ent->v.view_ofs, start );

	// try sending a trace straight
	VectorCopy( svgame.globals->v_forward, dir );
	VectorMA( start, 2048, dir, end );
	tr = SV_Move( start, vec3_origin, vec3_origin, end, MOVE_NORMAL, ent, false );

	// don't aim at teammate
	if( tr.ent && ( tr.ent->v.takedamage == DAMAGE_AIM || ent->v.team <= 0 || ent->v.team != tr.ent->v.team ))
		return;

	// try all possible entities
	VectorCopy( svgame.globals->v_forward, bestdir );
	bestdist = Cvar_VariableValue( "sv_aim" );

	check = EDICT_NUM( 1 ); // start at first client
	for( i = 1; i < svgame.numEntities; i++, check++ )
	{
		if( check->v.takedamage != DAMAGE_AIM )
			continue;

		if( FBitSet( check->v.flags, FL_FAKECLIENT ))
			continue;

		if( ent->v.team > 0 && ent->v.team == check->v.team )
			continue;

		if( check == ent )
			continue;

		for( j = 0; j < 3; j++ )
			end[j] = check->v.origin[j] + 0.5f * (check->v.mins[j] + check->v.maxs[j]);

		VectorSubtract( end, start, dir );
		VectorNormalize( dir );
		dist = DotProduct( dir, svgame.globals->v_forward );

		if( dist < bestdist )
			continue; // to far to turn

		tr = SV_Move( start, vec3_origin, vec3_origin, end, MOVE_NORMAL, ent, false );

		if( tr.ent == check )
		{	
			// can shoot at this one
			VectorCopy( dir, bestdir );
			bestdist = dist;
		}
	}

	VectorCopy( bestdir, rgflReturn );
}

/*
=========
pfnServerCommand

=========
*/
void pfnServerCommand( const char* str )
{
	if( !SV_IsValidCmd( str ))
		Con_Printf( S_ERROR "bad server command %s\n", str );
	else Cbuf_AddText( str );
}

/*
=========
pfnServerExecute

=========
*/
void pfnServerExecute( void )
{
	Cbuf_Execute();

	if( svgame.config_executed )
		return;

	// here we restore arhcived cvars only from game.dll
	host.apply_game_config = true;
	Cbuf_AddText( "exec config.cfg\n" );
	Cbuf_Execute();

	if( host.sv_cvars_restored > 0 )
		Con_Reportf( "server executing ^2config.cfg^7 (%i cvars)\n", host.sv_cvars_restored );

	host.apply_game_config = false;
	svgame.config_executed = true;
	host.sv_cvars_restored = 0;
}

/*
=========
pfnClientCommand

=========
*/
void pfnClientCommand( edict_t* pEdict, char* szFmt, ... ) _format( 2 );
void pfnClientCommand( edict_t* pEdict, char* szFmt, ... )
{
	sv_client_t	*cl;
	string		buffer;
	va_list		args;

	if( sv.state != ss_active )
		return; // early out

	if(( cl = SV_ClientFromEdict( pEdict, true )) == NULL )
	{
		Con_Printf( S_ERROR "stuffcmd: client is not spawned!\n" );
		return;
	}

	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, MAX_STRING, szFmt, args );
	va_end( args );

	if( SV_IsValidCmd( buffer ))
	{
		MSG_BeginServerCmd( &cl->netchan.message, svc_stufftext );
		MSG_WriteString( &cl->netchan.message, buffer );
	}
	else Con_Printf( S_ERROR "Tried to stuff bad command %s\n", buffer );
}

/*
=================
pfnParticleEffect

Make sure the event gets sent to all clients
=================
*/
void pfnParticleEffect( const float *org, const float *dir, float color, float count )
{
	int	v;

	if( MSG_GetNumBytesLeft( &sv.datagram ) < 16 )
		return;

	MSG_BeginServerCmd( &sv.datagram, svc_particle );
	MSG_WriteVec3Coord( &sv.datagram, org );
	v = bound( -128, dir[0] * 16.0f, 127 );
	MSG_WriteChar( &sv.datagram, v );
	v = bound( -128, dir[1] * 16.0f, 127 );
	MSG_WriteChar( &sv.datagram, v );
	v = bound( -128, dir[2] * 16.0f, 127 );
	MSG_WriteChar( &sv.datagram, v );
	MSG_WriteByte( &sv.datagram, count );
	MSG_WriteByte( &sv.datagram, color );
	MSG_WriteByte( &sv.datagram, 0 ); // z-vel
}

/*
===============
pfnLightStyle

===============
*/
void pfnLightStyle( int style, const char* val )
{
	if( style < 0 ) style = 0;
	if( style >= MAX_LIGHTSTYLES )
		Host_Error( "SV_LightStyle: style: %i >= %d", style, MAX_LIGHTSTYLES );
	if( sv.loadgame ) return; // don't let the world overwrite our restored styles

	SV_SetLightStyle( style, val, 0.0f ); // set correct style
}

/*
=================
pfnDecalIndex

register decal name on client
=================
*/
int pfnDecalIndex( const char *m )
{
	int	i;

	if( !COM_CheckString( m ))
		return -1;

	for( i = 1; i < MAX_DECALS && host.draw_decals[i][0]; i++ )
	{
		if( !Q_stricmp( host.draw_decals[i], m ))
			return i;
	}

	return -1;	
}

/*
=============
pfnMessageBegin

=============
*/
void pfnMessageBegin( int msg_dest, int msg_num, const float *pOrigin, edict_t *ed )
{
	int	i, iSize;

	if( svgame.msg_started )
		Host_Error( "MessageBegin: New message started when msg '%s' has not been sent yet\n", svgame.msg_name );
	svgame.msg_started = true;

	// check range
	msg_num = bound( svc_bad, msg_num, 255 );

	if( msg_num <= svc_lastmsg )
	{
		svgame.msg_index = -msg_num; // this is a system message
		svgame.msg_name = svc_strings[msg_num];

		if( msg_num == svc_temp_entity )
			iSize = -1; // temp entity have variable size
		else iSize = 0;
	}
	else
	{
		// check for existing
		for( i = 1; i < MAX_USER_MESSAGES && svgame.msg[i].name[0]; i++ )
		{
			if( svgame.msg[i].number == msg_num )
				break; // found
		}

		if( i == MAX_USER_MESSAGES )
		{
			Host_Error( "MessageBegin: tried to send unregistered message %i\n", msg_num );
			return;
		}

		svgame.msg_name = svgame.msg[i].name;
		iSize = svgame.msg[i].size;
		svgame.msg_index = i;
	}

	MSG_WriteCmdExt( &sv.multicast, msg_num, NS_SERVER, svgame.msg_name );

	// save message destination
	if( pOrigin ) VectorCopy( pOrigin, svgame.msg_org );
	else VectorClear( svgame.msg_org );

	if( iSize == -1 )
	{
		// variable sized messages sent size as first short
		svgame.msg_size_index = MSG_GetNumBytesWritten( &sv.multicast );
		MSG_WriteWord( &sv.multicast, 0 ); // reserve space for now
	}
	else svgame.msg_size_index = -1; // message has constant size

	svgame.msg_realsize = 0;
	svgame.msg_dest = msg_dest;
	svgame.msg_ent = ed;
}

/*
=============
pfnMessageEnd

=============
*/
void pfnMessageEnd( void )
{
	const char	*name = "Unknown";
	float		*org = NULL;

	if( svgame.msg_name ) name = svgame.msg_name;
	if( !svgame.msg_started ) Host_Error( "MessageEnd: called with no active message\n" );
	svgame.msg_started = false;

	if( MSG_CheckOverflow( &sv.multicast ))
	{
		Con_Printf( S_ERROR "MessageEnd: %s has overflow multicast buffer\n", name );
		MSG_Clear( &sv.multicast );
		return;
	}

	// check for system message
	if( svgame.msg_index < 0 )
	{
		if( svgame.msg_size_index != -1 )
		{
			// variable sized message
			if( svgame.msg_realsize > MAX_USERMSG_LENGTH )
			{
				Con_Printf( S_ERROR "SV_Multicast: %s too long (more than %d bytes)\n", name, MAX_USERMSG_LENGTH );
				MSG_Clear( &sv.multicast );
				return;
			}
			else if( svgame.msg_realsize < 0 )
			{
				Con_Printf( S_ERROR "SV_Multicast: %s writes NULL message\n", name );
				MSG_Clear( &sv.multicast );
				return;
			}

			sv.multicast.pData[svgame.msg_size_index] = svgame.msg_realsize;
		}
	}
	else if( svgame.msg[svgame.msg_index].size != -1 )
	{
		int	expsize = svgame.msg[svgame.msg_index].size;
		int	realsize = svgame.msg_realsize;
	
		// compare sizes
		if( expsize != realsize )
		{
			Con_Printf( S_ERROR "SV_Multicast: %s expected %i bytes, it written %i. Ignored.\n", name, expsize, realsize );
			MSG_Clear( &sv.multicast );
			return;
		}
	}
	else if( svgame.msg_size_index != -1 )
	{
		// variable sized message
		if( svgame.msg_realsize > MAX_USERMSG_LENGTH )
		{
			Con_Printf( S_ERROR "SV_Multicast: %s too long (more than %d bytes)\n", name, MAX_USERMSG_LENGTH );
			MSG_Clear( &sv.multicast );
			return;
		}
		else if( svgame.msg_realsize < 0 )
		{
			Con_Printf( S_ERROR "SV_Multicast: %s writes NULL message\n", name );
			MSG_Clear( &sv.multicast );
			return;
		}

		*(word *)&sv.multicast.pData[svgame.msg_size_index] = svgame.msg_realsize;
	}
	else
	{
		// this should never happen
		Con_Printf( S_ERROR "SV_Multicast: %s have encountered error\n", name );
		MSG_Clear( &sv.multicast );
		return;
	}

	// update some messages in case their was format was changed and we want to keep backward compatibility
	if( svgame.msg_index < 0 )
	{
		int	svc_msg = abs( svgame.msg_index );

		if(( svc_msg == svc_finale || svc_msg == svc_cutscene ) && svgame.msg_realsize == 0 )
			MSG_WriteChar( &sv.multicast, 0 ); // write null string
	}

	if( !VectorIsNull( svgame.msg_org )) org = svgame.msg_org;
	svgame.msg_dest = bound( MSG_BROADCAST, svgame.msg_dest, MSG_SPEC );

	SV_Multicast( svgame.msg_dest, org, svgame.msg_ent, true, false );
}

/*
=============
pfnWriteByte

=============
*/
void pfnWriteByte( int iValue )
{
	if( iValue == -1 ) iValue = 0xFF; // convert char to byte 
	MSG_WriteByte( &sv.multicast, (byte)iValue );
	svgame.msg_realsize++;
}

/*
=============
pfnWriteChar

=============
*/
void pfnWriteChar( int iValue )
{
	MSG_WriteChar( &sv.multicast, (char)iValue );
	svgame.msg_realsize++;
}

/*
=============
pfnWriteShort

=============
*/
void pfnWriteShort( int iValue )
{
	MSG_WriteShort( &sv.multicast, (short)iValue );
	svgame.msg_realsize += 2;
}

/*
=============
pfnWriteLong

=============
*/
void pfnWriteLong( int iValue )
{
	MSG_WriteLong( &sv.multicast, iValue );
	svgame.msg_realsize += 4;
}

/*
=============
pfnWriteAngle

this is low-res angle
=============
*/
void pfnWriteAngle( float flValue )
{
	int	iAngle = ((int)(( flValue ) * 256 / 360) & 255);

	MSG_WriteChar( &sv.multicast, iAngle );
	svgame.msg_realsize += 1;
}

/*
=============
pfnWriteCoord

=============
*/
void pfnWriteCoord( float flValue )
{
	MSG_WriteCoord( &sv.multicast, flValue );
	svgame.msg_realsize += 2;
}

/*
=============
pfnWriteBytes

=============
*/
void pfnWriteBytes( const byte *bytes, int count )
{
	MSG_WriteBytes( &sv.multicast, bytes, count );
	svgame.msg_realsize += count;
}

/*
=============
pfnWriteString

=============
*/
void pfnWriteString( const char *src )
{
	static char	string[MAX_USERMSG_LENGTH];
	int		len = Q_strlen( src ) + 1;
	int		rem = sizeof( string ) - 1;
	char		*dst;

	if( len == 1 )
	{
		MSG_WriteChar( &sv.multicast, 0 );
		svgame.msg_realsize += 1; 
		return; // fast exit
	}

	// prepare string to sending
	dst = string;

	while( 1 )
	{
		// some escaped chars parsed as two symbols - merge it here
		if( src[0] == '\\' && src[1] == 'n' )
		{
			*dst++ = '\n';
			src += 2;
			len -= 1;
		}
		else if( src[0] == '\\' && src[1] == 'r' )
		{
			*dst++ = '\r';
			src += 2;
			len -= 1;
		}
		else if( src[0] == '\\' && src[1] == 't' )
		{
			*dst++ = '\t';
			src += 2;
			len -= 1;
		}
		else if(( *dst++ = *src++ ) == 0 )
			break;

		if( --rem <= 0 )
		{
			Con_Printf( S_ERROR "pfnWriteString: exceeds %i symbols\n", len );
			*dst = '\0'; // string end (not included in count)
			len = Q_strlen( string ) + 1;
			break;
		}
	}

	*dst = '\0'; // string end (not included in count)
	MSG_WriteString( &sv.multicast, string );

	// NOTE: some messages with constant string length can be marked as known sized
	svgame.msg_realsize += len;
}

/*
=============
pfnWriteEntity

=============
*/
void pfnWriteEntity( int iValue )
{
	if( iValue < 0 || iValue >= svgame.numEntities )
		Host_Error( "MSG_WriteEntity: invalid entnumber %i\n", iValue );
	MSG_WriteShort( &sv.multicast, (short)iValue );
	svgame.msg_realsize += 2;
}

/*
=============
pfnAlertMessage

=============
*/
static void pfnAlertMessage( ALERT_TYPE type, char *szFmt, ... ) _format( 2 );
static void pfnAlertMessage( ALERT_TYPE type, char *szFmt, ... )
{
	char	buffer[2048];
	va_list	args;

	if( type == at_logged && svs.maxclients > 1 )
	{
		va_start( args, szFmt );
		Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
		va_end( args );
		Log_Printf( "%s", buffer );
		return;
	}

	if( host_developer.value <= DEV_NONE )
		return;

	// g-cont: some mods have wrong aiconsole messages that crash the engine
	if( type == at_aiconsole && host_developer.value < DEV_EXTENDED )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	// check message for pass
	switch( type )
	{
	case at_notice:
		Con_Printf( S_NOTE "%s", buffer );
		break;
	case at_console:
		Con_Printf( "%s", buffer );
		break;
	case at_aiconsole:
		Con_DPrintf( "%s", buffer );
		break;
	case at_warning:
		Con_Printf( S_WARN "%s", buffer );
		break;
	case at_error:
		Con_Printf( S_ERROR "%s", buffer );
		break;
	}
}

/*
=============
pfnEngineFprintf

OBSOLETE, UNUSED
=============
*/
static void pfnEngineFprintf( FILE *pfile, char *szFmt, ... ) _format( 2 );
static void pfnEngineFprintf( FILE *pfile, char *szFmt, ... )
{
}
	
/*
=============
pfnBuildSoundMsg

Customizable sound message
=============
*/
void pfnBuildSoundMsg( edict_t *pSource, int chan, const char *samp, float fvol, float attn, int fFlags, int pitch, int msg_dest, int msg_type, const float *pOrigin, edict_t *pSend )
{
	pfnMessageBegin( msg_dest, msg_type, pOrigin, pSend );
	SV_BuildSoundMsg( &sv.multicast, pSource, chan, samp, fvol * 255, attn, fFlags, pitch, pOrigin );
	pfnMessageEnd();
}

/*
=============
pfnPvAllocEntPrivateData

=============
*/
void *pfnPvAllocEntPrivateData( edict_t *pEdict, long cb )
{
	Assert( pEdict != NULL );

	SV_FreePrivateData( pEdict );

	if( cb > 0 )
	{
		// a poke646 have memory corrupt in somewhere - this is trashed last sixteen bytes :(
		pEdict->pvPrivateData = Mem_Calloc( svgame.mempool, (cb + 15) & ~15 );
	}

	return pEdict->pvPrivateData;
}

/*
=============
pfnPvEntPrivateData

we already have copy of this function in 'enginecallback.h' :-)
=============
*/
void *pfnPvEntPrivateData( edict_t *pEdict )
{
	if( pEdict )
		return pEdict->pvPrivateData;
	return NULL;
}


#ifdef XASH_64BIT
static struct str64_s
{
	size_t maxstringarray;
	qboolean allowdup;
	char *staticstringarray;
	char *pstringarray;
	char *pstringarraystatic;
	char *pstringbase;
	char *poldstringbase;
	char *plast;
	qboolean dynamic;
	size_t maxalloc;
	size_t numdups;
	size_t numoverflows;
	size_t totalalloc;
} str64;
#endif

/*
==================
SV_EmptyStringPool

Free strings on server stop. Reset string pointer on 64 bits
==================
*/
void SV_EmptyStringPool( void )
{
#ifdef XASH_64BIT
	if( str64.dynamic ) // switch only after array fill (more space for multiplayer games)
		str64.pstringbase = str64.pstringarray;
	else
	{
		str64.pstringbase = str64.poldstringbase = str64.pstringarraystatic;
		str64.plast = str64.pstringbase + 1;
	}
#else
	Mem_EmptyPool( svgame.stringspool );
#endif
}

/*
===============
SV_SetStringArrayMode

use different arrays on 64 bit platforms
set dynamic after complete server spawn
this helps not to lose strings that belongs to static game part
===============
*/
void SV_SetStringArrayMode( qboolean dynamic )
{
#ifdef XASH_64BIT
	Con_Reportf( "SV_SetStringArrayMode(%d) %d\n", dynamic, str64.dynamic );

	if( dynamic == str64.dynamic )
		return;

	str64.dynamic = dynamic;

	SV_EmptyStringPool();
#endif
}

#ifdef XASH_64BIT
#if !XASH_WIN32
#define USE_MMAP
#include <sys/mman.h>
#endif
#endif

/*
==================
SV_AllocStringPool

alloc string pool on 32bit platforms
alloc string array near the server library on 64bit platforms if possible
alloc string array somewhere if not (MAKE_STRING will not work. Always call ALLOC_STRING instead, or crash)
this case need patched game dll with MAKE_STRING checking ptrdiff size
==================
*/
void SV_AllocStringPool( void )
{
#ifdef XASH_64BIT
	void *ptr = NULL;
	string lenstr;

	Con_Reportf( "SV_AllocStringPool()\n" );
	if( Sys_GetParmFromCmdLine( "-str64alloc", lenstr ) )
	{
		str64.maxstringarray = Q_atoi( lenstr );
		if( str64.maxstringarray < 1024 || str64.maxstringarray >= INT_MAX )
			str64.maxstringarray = 65536;
	}
	else str64.maxstringarray = 65536;
	if( Sys_CheckParm( "-str64dup" ) )
		str64.allowdup = true;

#ifdef USE_MMAP
	{
		size_t pagesize = sysconf( _SC_PAGESIZE );
		int arrlen = (str64.maxstringarray * 2) & ~(pagesize - 1);
		void *base = svgame.dllFuncs.pfnGameInit;
		void *start = svgame.hInstance - arrlen;

		while( start - base > INT_MIN )
		{
			void *mapptr = mmap((void*)((unsigned long)start & ~(pagesize - 1)), arrlen, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0 );
			if( mapptr && mapptr != (void*)-1 && mapptr - base > INT_MIN && mapptr - base < INT_MAX )
			{
				ptr = mapptr;
				break;
			}
			if( mapptr ) munmap( mapptr, arrlen );
			start -= arrlen;
		}

		if( !ptr )
		{
			start = base;
			while( start - base < INT_MAX )
			{
				void *mapptr = mmap((void*)((unsigned long)start & ~(pagesize - 1)), arrlen, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0 );
				if( mapptr && mapptr != (void*)-1  && mapptr - base > INT_MIN && mapptr - base < INT_MAX )
				{
					ptr = mapptr;
					break;
				}
				if( mapptr ) munmap( mapptr, arrlen );
				start += arrlen;
			}
		}


		if( ptr )
		{
			Con_Reportf( "SV_AllocStringPool: Allocated string array near the server library: %p %p\n", base, ptr );

		}
		else
		{
			Con_Reportf( "SV_AllocStringPool: Failed to allocate string array near the server library!\n" );
			ptr = str64.staticstringarray = Mem_Calloc(host.mempool, str64.maxstringarray * 2);
		}
	}
#else
	ptr = str64.staticstringarray = Mem_Calloc(host.mempool, str64.maxstringarray * 2);
#endif

	str64.pstringarray = ptr;
	str64.pstringarraystatic = ptr + str64.maxstringarray;
	str64.pstringbase = str64.poldstringbase = ptr;
	str64.plast = ptr + 1;
	svgame.globals->pStringBase = ptr;
#else
	svgame.stringspool = Mem_AllocPool( "Server Strings" );
	svgame.globals->pStringBase = "";
#endif
}

void SV_FreeStringPool( void )
{
#ifdef XASH_64BIT
	Con_Reportf( "SV_FreeStringPool()\n" );

	if( str64.pstringarray != str64.staticstringarray )
		munmap( str64.pstringarray, (str64.maxstringarray * 2) & ~(sysconf( _SC_PAGESIZE ) - 1) );
	else
		Mem_Free( str64.staticstringarray );
#else
	Mem_FreePool( &svgame.stringspool );
#endif
}

/*
=============
SV_AllocString

allocate new engine string
on 64bit platforms find in array string if deduplication enabled (default)
if not found, add to array
use -str64dup to disable deduplication, -str64alloc to set array size
=============
*/
string_t GAME_EXPORT SV_AllocString( const char *szValue )
{
	const char *newString = NULL;
	int cmp;

	if( svgame.physFuncs.pfnAllocString != NULL )
		return svgame.physFuncs.pfnAllocString( szValue );

#ifdef XASH_64BIT
	cmp = 1;

	if( !str64.allowdup )
		for( newString = str64.poldstringbase + 1;
			newString < str64.plast && ( cmp = Q_strcmp( newString, szValue ) );
			newString += Q_strlen( newString ) + 1 );

	if( cmp )
	{
		uint len = Q_strlen( szValue );

		if( str64.plast - str64.poldstringbase + len + 2 > str64.maxstringarray )
		{
			str64.plast = str64.pstringbase + 1;
			str64.poldstringbase = str64.pstringbase;
			str64.numoverflows++;
		}

		//MsgDev( D_NOTE, "SV_AllocString: %ld %s\n", str64.plast - svgame.globals->pStringBase, szValue );
		memcpy( str64.plast, szValue, len + 1 );
		str64.totalalloc += len + 1;

		newString = str64.plast;
		str64.plast += len + 1;
	}
	else
		str64.numdups++;
		//MsgDev( D_NOTE, "SV_AllocString: dup %ld %s\n", newString - svgame.globals->pStringBase, szValue );

	if( newString - str64.pstringarray > str64.maxalloc )
		str64.maxalloc = newString - str64.pstringarray;

	return newString - svgame.globals->pStringBase;
#else
	newString = _copystring( svgame.stringspool, szValue, __FILE__, __LINE__ );
	return newString - svgame.globals->pStringBase;
#endif
}

#ifdef XASH_64BIT
void SV_PrintStr64Stats_f( void )
{
	Msg( "====================\n" );
	Msg( "64 bit string pool statistics\n" );
	Msg( "====================\n" );
	Msg( "string array size: %lu\n", str64.maxstringarray );
	Msg( "total alloc %lu\n", str64.totalalloc );
	Msg( "maximum array usage: %lu\n", str64.maxalloc );
	Msg( "overflow counter: %lu\n", str64.numoverflows );
	Msg( "dup string counter: %lu\n", str64.numdups );
}
#endif

/*
=============
SV_MakeString

make constant string
=============
*/
string_t SV_MakeString( const char *szValue )
{
	if( svgame.physFuncs.pfnMakeString != NULL )
		return svgame.physFuncs.pfnMakeString( szValue );
#ifdef XASH_64BIT
	{
		long long ptrdiff = szValue - svgame.globals->pStringBase;
		if( ptrdiff > INT_MAX || ptrdiff < INT_MIN )
			return SV_AllocString(szValue);
		else
			return (int)ptrdiff;
	}
#else
	return szValue - svgame.globals->pStringBase;
#endif
}

/*
=============
SV_GetString

=============
*/
const char *SV_GetString( string_t iString )
{
	if( svgame.physFuncs.pfnGetString != NULL )
		return svgame.physFuncs.pfnGetString( iString );
	return (svgame.globals->pStringBase + iString);
}

/*
=============
pfnGetVarsOfEnt

=============
*/
entvars_t *pfnGetVarsOfEnt( edict_t *pEdict )
{
	if( pEdict )
		return &pEdict->v;
	return NULL;
}

/*
=============
pfnPEntityOfEntOffset

=============
*/
edict_t* pfnPEntityOfEntOffset( int iEntOffset )
{
	return (edict_t *)((byte *)svgame.edicts + iEntOffset);
}

/*
=============
pfnEntOffsetOfPEntity

=============
*/
int pfnEntOffsetOfPEntity( const edict_t *pEdict )
{
	return (byte *)pEdict - (byte *)svgame.edicts;
}

/*
=============
pfnIndexOfEdict

=============
*/
int pfnIndexOfEdict( const edict_t *pEdict )
{
	int	number;

	if( !pEdict ) return 0; // world ?

	number = NUM_FOR_EDICT( pEdict );
	if( number < 0 || number > GI->max_edicts )
		Host_Error( "bad entity number %d\n", number );
	return number;
}

/*
=============
pfnPEntityOfEntIndex

=============
*/
edict_t *pfnPEntityOfEntIndex( int iEntIndex )
{
	if( iEntIndex >= 0 && iEntIndex < GI->max_edicts )
	{
		edict_t	*pEdict = EDICT_NUM( iEntIndex );

		if( !iEntIndex || FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
			return pEdict; // just get access to array

		if( SV_IsValidEdict( pEdict ) && pEdict->pvPrivateData )
			return pEdict;

		// g-cont: world and clients can be acessed even without private data!
		if( SV_IsValidEdict( pEdict ) && SV_IsPlayerIndex( iEntIndex ))
			return pEdict;
	}

	return NULL;
}

/*
=============
pfnFindEntityByVars

debug thing
=============
*/
edict_t* pfnFindEntityByVars( entvars_t *pvars )
{
	edict_t	*pEdict;
	int	i;

	// don't pass invalid arguments
	if( !pvars ) return NULL;

	for( i = 0; i < GI->max_edicts; i++ )
	{
		pEdict = EDICT_NUM( i );

		// g-cont: we should compare pointers
		if( &pEdict->v == pvars )
			return pEdict; // found it
	}

	return NULL;
}

/*
=============
pfnGetModelPtr

returns pointer to a studiomodel
=============
*/
static void *pfnGetModelPtr( edict_t *pEdict )
{
	model_t	*mod;

	if( !SV_IsValidEdict( pEdict ))
		return NULL;

	mod = SV_ModelHandle( pEdict->v.modelindex );
	return Mod_StudioExtradata( mod );
}

/*
=============
SV_SendUserReg

=============
*/
void SV_SendUserReg( sizebuf_t *msg, sv_user_message_t *user )
{
	MSG_BeginServerCmd( msg, svc_usermessage );
	MSG_WriteByte( msg, user->number );
	MSG_WriteWord( msg, (word)user->size );
	MSG_WriteString( msg, user->name );
}

/*
=============
pfnRegUserMsg

=============
*/
int pfnRegUserMsg( const char *pszName, int iSize )
{
	int	i;
	
	if( !COM_CheckString( pszName ))
		return svc_bad;

	if( Q_strlen( pszName ) >= sizeof( svgame.msg[0].name ))
	{
		Con_Printf( S_ERROR "REG_USER_MSG: too long name %s\n", pszName );
		return svc_bad; // force error
	}

	if( iSize > MAX_USERMSG_LENGTH )
	{
		Con_Printf( S_ERROR "REG_USER_MSG: %s has too big size %i\n", pszName, iSize );
		return svc_bad; // force error
	}

	// make sure what size inrange
	iSize = bound( -1, iSize, MAX_USERMSG_LENGTH );

	// message 0 is reserved for svc_bad
	for( i = 1; i < MAX_USER_MESSAGES && svgame.msg[i].name[0]; i++ )
	{
		// see if already registered
		if( !Q_strcmp( svgame.msg[i].name, pszName ))
			return svc_lastmsg + i; // offset
	}

	if( i == MAX_USER_MESSAGES ) 
	{
		Con_Printf( S_ERROR "REG_USER_MSG: user messages limit exceeded\n" );
		return svc_bad;
	}

	// register new message
	Q_strncpy( svgame.msg[i].name, pszName, sizeof( svgame.msg[i].name ));
	svgame.msg[i].number = svc_lastmsg + i;
	svgame.msg[i].size = iSize;

	if( sv.state == ss_active )
	{
		// tell the client about new user message
		SV_SendUserReg( &sv.multicast, &svgame.msg[i] );
		SV_Multicast( MSG_ALL, NULL, NULL, false, false );
	}

	return svgame.msg[i].number;
}

/*
=============
pfnAnimationAutomove

OBSOLETE, UNUSED
=============
*/
void pfnAnimationAutomove( const edict_t* pEdict, float flTime )
{
}

/*
=============
pfnGetBonePosition

=============
*/
static void pfnGetBonePosition( const edict_t* pEdict, int iBone, float *rgflOrigin, float *rgflAngles )
{
	if( !SV_IsValidEdict( pEdict ))
		return;
	Mod_GetBonePosition( pEdict, iBone, rgflOrigin, rgflAngles );
}

/*
=============
pfnFunctionFromName

=============
*/
void *pfnFunctionFromName( const char *pName )
{
	return COM_FunctionFromName_SR( svgame.hInstance, pName );
}

/*
=============
pfnNameForFunction

=============
*/
const char *pfnNameForFunction( void *function )
{
	return COM_NameForFunction( svgame.hInstance, function );
}

/*
=============
pfnClientPrintf

=============
*/
void pfnClientPrintf( edict_t* pEdict, PRINT_TYPE ptype, const char *szMsg )
{
	sv_client_t	*client;

	if(( client = SV_ClientFromEdict( pEdict, false )) == NULL )
	{
		Con_Printf( "tried to sprint to a non-client\n" );
		return;
	}

	if( FBitSet( client->flags, FCL_FAKECLIENT ))
		return;

	switch( ptype )
	{
	case print_console:
	case print_chat:
		SV_ClientPrintf( client, "%s", szMsg );
		break;
	case print_center:
		MSG_BeginServerCmd( &client->netchan.message, svc_centerprint );
		MSG_WriteString( &client->netchan.message, szMsg );
		break;
	}
}

/*
=============
pfnServerPrint

print to the server console
=============
*/
void pfnServerPrint( const char *szMsg )
{
	if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		SV_BroadcastPrintf( NULL, "%s", szMsg );
	else Con_Printf( "%s", szMsg );
}

/*
=============
pfnGetAttachment

=============
*/
static void pfnGetAttachment( const edict_t *pEdict, int iAttachment, float *rgflOrigin, float *rgflAngles )
{
	if( !SV_IsValidEdict( pEdict ))
		return;
	Mod_StudioGetAttachment( pEdict, iAttachment, rgflOrigin, rgflAngles );
}

/*
=============
pfnCrosshairAngle

=============
*/
void pfnCrosshairAngle( const edict_t *pClient, float pitch, float yaw )
{
	sv_client_t	*client;

	if(( client = SV_ClientFromEdict( pClient, true )) == NULL )
		return;

	// fakeclients ignores it silently
	if( FBitSet( client->flags, FCL_FAKECLIENT ))
		return;

	if( pitch > 180.0f ) pitch -= 360;
	if( pitch < -180.0f ) pitch += 360;
	if( yaw > 180.0f ) yaw -= 360;
	if( yaw < -180.0f ) yaw += 360;

	MSG_BeginServerCmd( &client->netchan.message, svc_crosshairangle );
	MSG_WriteChar( &client->netchan.message, pitch * 5 );
	MSG_WriteChar( &client->netchan.message, yaw * 5 );
}

/*
=============
pfnSetView

=============
*/
void pfnSetView( const edict_t *pClient, const edict_t *pViewent )
{
	sv_client_t	*client;
	int		viewEnt;

	if( !SV_IsValidEdict( pClient ))
		return;

	if(( client = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "PF_SetView_I: not a client!\n" );
		return;
	}

	if( !SV_IsValidEdict( pViewent ) || pClient == pViewent )
		client->pViewEntity = NULL; // just reset viewentity
	else client->pViewEntity = (edict_t *)pViewent;

	// fakeclients ignore to send client message (but can see into the trigger_camera through the PVS)
	if( FBitSet( client->flags, FCL_FAKECLIENT ))
		return;

	if( client->pViewEntity )
		viewEnt = NUM_FOR_EDICT( client->pViewEntity );
	else viewEnt = NUM_FOR_EDICT( client->edict );

	MSG_BeginServerCmd( &client->netchan.message, svc_setview );
	MSG_WriteWord( &client->netchan.message, viewEnt );
}

/*
=============
pfnStaticDecal

=============
*/
void pfnStaticDecal( const float *origin, int decalIndex, int entityIndex, int modelIndex )
{
	SV_CreateDecal( &sv.signon, origin, decalIndex, entityIndex, modelIndex, FDECAL_PERMANENT, 1.0f );
}

/*
=============
pfnIsDedicatedServer

=============
*/
int pfnIsDedicatedServer( void )
{
	return Host_IsDedicated();
}

/*
=============
pfnGetPlayerWONId

OBSOLETE, UNUSED
=============
*/
uint pfnGetPlayerWONId( edict_t *e )
{
	return (uint)-1;
}

/*
=============
pfnIsMapValid

vaild map must contain one info_player_deatchmatch
=============
*/
int pfnIsMapValid( char *filename )
{
	int	flags = SV_MapIsValid( filename, GI->mp_entity, NULL );

	if( FBitSet( flags, MAP_IS_EXIST ) && FBitSet( flags, MAP_HAS_SPAWNPOINT ))
		return true;
	return false;
}

/*
=============
pfnFadeClientVolume

=============
*/
void pfnFadeClientVolume( const edict_t *pEdict, int fadePercent, int fadeOutSeconds, int holdTime, int fadeInSeconds )
{
	sv_client_t	*cl;

	if(( cl = SV_ClientFromEdict( pEdict, true )) == NULL )
		return;

	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
		return;

	MSG_BeginServerCmd( &cl->netchan.message, svc_soundfade );
	MSG_WriteByte( &cl->netchan.message, fadePercent );
	MSG_WriteByte( &cl->netchan.message, holdTime );
	MSG_WriteByte( &cl->netchan.message, fadeOutSeconds );
	MSG_WriteByte( &cl->netchan.message, fadeInSeconds );
}

/*
=============
pfnSetClientMaxspeed

fakeclients can be changed speed to
=============
*/
void pfnSetClientMaxspeed( const edict_t *pEdict, float fNewMaxspeed )
{
	sv_client_t	*cl;

	// not spawned clients allowed
	if(( cl = SV_ClientFromEdict( pEdict, false )) == NULL )
		return;

	fNewMaxspeed = bound( -svgame.movevars.maxspeed, fNewMaxspeed, svgame.movevars.maxspeed );
	Info_SetValueForKey( cl->physinfo, "maxspd", va( "%.f", fNewMaxspeed ), MAX_INFO_STRING );
	cl->edict->v.maxspeed = fNewMaxspeed;
}

/*
=============
pfnRunPlayerMove

=============
*/
void pfnRunPlayerMove( edict_t *pClient, const float *viewangles, float fmove, float smove, float upmove, word buttons, byte impulse, byte msec )
{
	sv_client_t	*cl, *oldcl;
	usercmd_t		cmd;
	uint		seed;

	if(( cl = SV_ClientFromEdict( pClient, true )) == NULL )
		return;

	if( !FBitSet( cl->flags, FCL_FAKECLIENT ))
		return; // only fakeclients allows

	oldcl = sv.current_client;

	sv.current_client = SV_ClientFromEdict( pClient, true );
	sv.current_client->timebase = (sv.time + sv.frametime) - ((double)msec / 1000.0);

	memset( &cmd, 0, sizeof( cmd ));
	VectorCopy( viewangles, cmd.viewangles );
	cmd.forwardmove = fmove;
	cmd.sidemove = smove;
	cmd.upmove = upmove;
	cmd.buttons = buttons;
	cmd.impulse = impulse;
	cmd.msec = msec;

	seed = COM_RandomLong( 0, 0x7fffffff ); // full range

	SV_RunCmd( cl, &cmd, seed );

	cl->lastcmd = cmd;
	sv.current_client = oldcl;
}

/*
=============
pfnNumberOfEntities

returns actual entity count
=============
*/
int pfnNumberOfEntities( void )
{
	int	i, total = 0;

	for( i = 0; i < svgame.numEntities; i++ )
	{
		if( svgame.edicts[i].free )
			continue;
		total++;
	}

	return total;
}

/*
=============
pfnGetInfoKeyBuffer

=============
*/
char *pfnGetInfoKeyBuffer( edict_t *e )
{
	sv_client_t	*cl;

	// NULL passes localinfo
	if( !SV_IsValidEdict( e ))
		return SV_Localinfo();

	// world passes serverinfo
	if( e == svgame.edicts )
		return SV_Serverinfo();

	// userinfo for specified edict
	if(( cl = SV_ClientFromEdict( e, false )) != NULL )
		return cl->userinfo;

	return ""; // assume error
}

/*
=============
pfnSetValueForKey

=============
*/
void pfnSetValueForKey( char *infobuffer, char *key, char *value )
{
	if( infobuffer == svs.localinfo )
		Info_SetValueForStarKey( infobuffer, key, value, MAX_LOCALINFO_STRING );
	else if( infobuffer == svs.serverinfo )
		Info_SetValueForStarKey( infobuffer, key, value, MAX_SERVERINFO_STRING );
	else Con_Printf( S_ERROR "can't set client keys with SetValueForKey\n" );
}

/*
=============
pfnSetClientKeyValue

=============
*/
void pfnSetClientKeyValue( int clientIndex, char *infobuffer, char *key, char *value )
{
	sv_client_t	*cl;

	if( infobuffer == svs.localinfo || infobuffer == svs.serverinfo )
		return;

	clientIndex -= 1;

	if( !svs.clients || clientIndex < 0 || clientIndex >= svs.maxclients )
		return;

	// value not changed?
	if( !Q_strcmp( Info_ValueForKey( infobuffer, key ), value ))
		return;

	cl = &svs.clients[clientIndex]; 

	Info_SetValueForStarKey( infobuffer, key, value, MAX_INFO_STRING );
	SetBits( cl->flags, FCL_RESEND_USERINFO );
	cl->next_sendinfotime = 0.0;	// send immediately
}

/*
=============
pfnGetPhysicsKeyValue

=============
*/
const char *pfnGetPhysicsKeyValue( const edict_t *pClient, const char *key )
{
	sv_client_t	*cl;

	// pfnUserInfoChanged passed
	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "GetPhysicsKeyValue: tried to a non-client!\n" );
		return "";
	}

	return Info_ValueForKey( cl->physinfo, key );
}

/*
=============
pfnSetPhysicsKeyValue

=============
*/
void pfnSetPhysicsKeyValue( const edict_t *pClient, const char *key, const char *value )
{
	sv_client_t	*cl;

	// pfnUserInfoChanged passed
	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "SetPhysicsKeyValue: tried to a non-client!\n" );
		return;
	}

	Info_SetValueForKey( cl->physinfo, key, value, MAX_INFO_STRING );
}

/*
=============
pfnGetPhysicsInfoString

=============
*/
const char *pfnGetPhysicsInfoString( const edict_t *pClient )
{
	sv_client_t	*cl;

	// pfnUserInfoChanged passed
	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "GetPhysicsInfoString: tried to a non-client!\n" );
		return "";
	}

	return cl->physinfo;
}

/*
=============
pfnPrecacheEvent

register or returns already registered event id
a type of event is ignored at this moment
=============
*/
word pfnPrecacheEvent( int type, const char *psz )
{
	return (word)SV_EventIndex( psz );
}

/*
=============
pfnPlaybackEvent

=============
*/
void SV_PlaybackEventFull( int flags, const edict_t *pInvoker, word eventindex, float delay, float *origin,
	float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 )
{
	sv_client_t	*cl;
	event_state_t	*es;
	event_args_t	args;
	event_info_t	*ei = NULL;
	int		j, slot, bestslot;
	int		invokerIndex;
	byte		*mask = NULL;
	vec3_t		pvspoint;

	if( FBitSet( flags, FEV_CLIENT ))
		return;	// someone stupid joke

	// first check event for out of bounds
	if( eventindex < 1 || eventindex > MAX_EVENTS )
	{
		Con_Printf( S_ERROR "EV_Playback: invalid eventindex %i\n", eventindex );
		return;
	}

	// check event for precached
	if( !COM_CheckString( sv.event_precache[eventindex] ))
	{
		Con_Printf( S_ERROR "EV_Playback: event %i was not precached\n", eventindex );
		return;		
	}

	memset( &args, 0, sizeof( args ));

	if( origin && !VectorIsNull( origin ))
	{
		VectorCopy( origin, args.origin );
		args.flags |= FEVENT_ORIGIN;
	}

	if( angles && !VectorIsNull( angles ))
	{
		VectorCopy( angles, args.angles );
		args.flags |= FEVENT_ANGLES;
	}

	// copy other parms
	args.fparam1 = fparam1;
	args.fparam2 = fparam2;
	args.iparam1 = iparam1;
	args.iparam2 = iparam2;
	args.bparam1 = bparam1;
	args.bparam2 = bparam2;

	VectorClear( pvspoint );

	if( SV_IsValidEdict( pInvoker ))
	{
		// add the view_ofs to avoid problems with crossed contents line
		VectorAdd( pInvoker->v.origin, pInvoker->v.view_ofs, pvspoint );
		args.entindex = invokerIndex = NUM_FOR_EDICT( pInvoker );

		// g-cont. allow 'ducking' param for all entities
		args.ducking = FBitSet( pInvoker->v.flags, FL_DUCKING ) ? true : false;

		// this will be send only for reliable event
		if( !FBitSet( args.flags, FEVENT_ORIGIN ))
			VectorCopy( pInvoker->v.origin, args.origin );

		// this will be send only for reliable event
		if( !FBitSet( args.flags, FEVENT_ANGLES ))
			VectorCopy( pInvoker->v.angles, args.angles );
	}
	else
	{
		VectorCopy( args.origin, pvspoint );
		args.entindex = 0;
		invokerIndex = -1;
	}

	if( !FBitSet( flags, FEV_GLOBAL ) && VectorIsNull( pvspoint ))
	{
		Con_DPrintf( S_ERROR "%s: not a FEV_GLOBAL event missing origin. Ignored.\n", sv.event_precache[eventindex] );
		return;
	}

	// check event for some user errors
	if( FBitSet( flags, FEV_NOTHOST|FEV_HOSTONLY ))
	{
		if( !SV_ClientFromEdict( pInvoker, true ))
		{
			const char *ev_name = sv.event_precache[eventindex];

			if( FBitSet( flags, FEV_NOTHOST ))
			{
				Con_DPrintf( S_WARN "%s: specified FEV_NOTHOST when invoker not a client\n", ev_name );
				ClearBits( flags, FEV_NOTHOST );
			}

			if( FBitSet( flags, FEV_HOSTONLY ))
			{
				Con_DPrintf( S_WARN "%s: specified FEV_HOSTONLY when invoker not a client\n", ev_name );
				ClearBits( flags, FEV_HOSTONLY );
			}
		}
	}

	SetBits( flags, FEV_SERVER );		// it's a server event!
	if( delay < 0.0f ) delay = 0.0f;	// fixup negative delays

	// setup pvs cluster for invoker
	if( !FBitSet( flags, FEV_GLOBAL ))
	{
		Mod_FatPVS( pvspoint, FATPHS_RADIUS, fatphs, world.fatbytes, false, ( svs.maxclients == 1 ));
		mask = fatphs; // using the FatPVS like a PHS
	}

	// process all the clients
	for( slot = 0, cl = svs.clients; slot < svs.maxclients; slot++, cl++ )
	{
		if( cl->state != cs_spawned || !cl->edict || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		if( SV_IsValidEdict( pInvoker ) && pInvoker->v.groupinfo && cl->edict->v.groupinfo )
		{
			if( svs.groupop == GROUP_OP_AND && !FBitSet( cl->edict->v.groupinfo, pInvoker->v.groupinfo ))
				continue;

			if( svs.groupop == GROUP_OP_NAND && FBitSet( cl->edict->v.groupinfo, pInvoker->v.groupinfo ))
				continue;
		}

		if( SV_IsValidEdict( pInvoker ))
		{
			if( !SV_CheckClientVisiblity( cl, mask ))
				continue;
		}

		if( FBitSet( flags, FEV_NOTHOST ) && cl == sv.current_client && FBitSet( cl->flags, FCL_LOCAL_WEAPONS ))
			continue;	// will be played on client side

		if( FBitSet( flags, FEV_HOSTONLY ) && cl->edict != pInvoker )
			continue;	// sending only to invoker

		// all checks passed, send the event

		// reliable event
		if( FBitSet( flags, FEV_RELIABLE ))
		{
			// skipping queue, write direct into reliable datagram
			SV_PlaybackReliableEvent( &cl->netchan.message, eventindex, delay, &args );
			continue;
		}

		// unreliable event (stores in queue)
		es = &cl->events;
		bestslot = -1;

		if( FBitSet( flags, FEV_UPDATE ))
		{
			for( j = 0; j < MAX_EVENT_QUEUE; j++ )
			{
				ei = &es->ei[j];

				if( ei->index == eventindex && invokerIndex != -1 && invokerIndex == ei->entity_index )
				{
					bestslot = j;
					break;
				}
			}
		}

		if( bestslot == -1 )
		{
			for( j = 0; j < MAX_EVENT_QUEUE; j++ )
			{
				ei = &es->ei[j];
		
				if( ei->index == 0 )
				{
					// found an empty slot
					bestslot = j;
					break;
				}
			}
		}
				
		// no slot found for this player, oh well
		if( bestslot == -1 ) continue;

		// add event to queue
		ei->index = eventindex;
		ei->fire_time = delay;
		ei->entity_index = invokerIndex;
		ei->packet_index = -1;
		ei->flags = flags;
		ei->args = args;
	}
}

/*
=============
pfnSetFatPVS

The client will interpolate the view position,
so we can't use a single PVS point
=============
*/
byte *pfnSetFatPVS( const float *org )
{
	qboolean	fullvis = false;

	if( !sv.worldmodel->visdata || sv_novis->value || !org || CL_DisableVisibility( ))
		fullvis = true;

	ASSERT( pfnGetCurrentPlayer() != -1 );

	// portals can't change viewpoint!
	if( !FBitSet( sv.hostflags, SVF_MERGE_VISIBILITY ))
	{
		vec3_t	viewPos, offset;

		// see code from client.cpp for understanding:
		// org = pView->v.origin + pView->v.view_ofs;
		// if ( pView->v.flags & FL_DUCKING )
		// {
		//	org = org + ( VEC_HULL_MIN - VEC_DUCK_HULL_MIN );
		// }
		// so we have unneeded duck calculations who have affect when player
		// is ducked into water. Remove offset to restore right PVS position
		if( FBitSet( sv.current_client->edict->v.flags, FL_DUCKING ))
		{
			VectorSubtract( svgame.pmove->player_mins[0], svgame.pmove->player_mins[1], offset );
			VectorSubtract( org, offset, viewPos );
		}
		else VectorCopy( org, viewPos );

		// build a new PVS frame
		Mod_FatPVS( viewPos, FATPVS_RADIUS, fatpvs, world.fatbytes, false, fullvis );
		VectorCopy( viewPos, viewPoint[pfnGetCurrentPlayer()] );
	}
	else
	{
		// merge PVS
		Mod_FatPVS( org, FATPVS_RADIUS, fatpvs, world.fatbytes, true, fullvis );
	}

	return fatpvs;
}

/*
=============
pfnSetFatPHS

The client will interpolate the hear position,
so we can't use a single PHS point
=============
*/
byte *pfnSetFatPAS( const float *org )
{
	qboolean	fullvis = false;

	if( !sv.worldmodel->visdata || sv_novis->value || !org || CL_DisableVisibility( ))
		fullvis = true;

	ASSERT( pfnGetCurrentPlayer() != -1 );

	// portals can't change viewpoint!
	if( !FBitSet( sv.hostflags, SVF_MERGE_VISIBILITY ))
	{
		vec3_t	viewPos, offset;

		// see code from client.cpp for understanding:
		// org = pView->v.origin + pView->v.view_ofs;
		// if ( pView->v.flags & FL_DUCKING )
		// {
		//	org = org + ( VEC_HULL_MIN - VEC_DUCK_HULL_MIN );
		// }
		// so we have unneeded duck calculations who have affect when player
		// is ducked into water. Remove offset to restore right PVS position
		if( FBitSet( sv.current_client->edict->v.flags, FL_DUCKING ))
		{
			VectorSubtract( svgame.pmove->player_mins[0], svgame.pmove->player_mins[1], offset );
			VectorSubtract( org, offset, viewPos );
		}
		else VectorCopy( org, viewPos );

		// build a new PHS frame
		Mod_FatPVS( viewPos, FATPHS_RADIUS, fatphs, world.fatbytes, false, fullvis );
	}
	else
	{
		// merge PHS
		Mod_FatPVS( org, FATPHS_RADIUS, fatphs, world.fatbytes, true, fullvis );
	}

	return fatphs;
}

/*
=============
pfnCheckVisibility

=============
*/
int pfnCheckVisibility( const edict_t *ent, byte *pset )
{
	int	i, leafnum;

	if( !SV_IsValidEdict( ent ))
		return 0;

	// vis not set - fullvis enabled
	if( !pset ) return 1;

	if( FBitSet( ent->v.flags, FL_CUSTOMENTITY ) && ent->v.owner && FBitSet( ent->v.owner->v.flags, FL_CLIENT ))
		ent = ent->v.owner;	// upcast beams to my owner

	if( ent->headnode < 0 )
	{
		// check individual leafs
		for( i = 0; i < ent->num_leafs; i++ )
		{
			if( CHECKVISBIT( pset, ent->leafnums[i] ))
				return 1;	// visible passed by leaf
		}

		return 0;
	}
	else
	{
		for( i = 0; i < MAX_ENT_LEAFS; i++ )
		{
			leafnum = ent->leafnums[i];
			if( leafnum == -1 ) break;

			if( CHECKVISBIT( pset, leafnum ))
				return 1;	// visible passed by leaf
		}

		// too many leafs for individual check, go by headnode
		if( !Mod_HeadnodeVisible( &sv.worldmodel->nodes[ent->headnode], pset, &leafnum ))
			return 0;

		((edict_t *)ent)->leafnums[ent->num_leafs] = leafnum;
		((edict_t *)ent)->num_leafs = (ent->num_leafs + 1) % MAX_ENT_LEAFS;

		return 2;	// visible passed by headnode
	}
}

/*
=============
pfnCanSkipPlayer

=============
*/
int pfnCanSkipPlayer( const edict_t *player )
{
	sv_client_t	*cl;

	if(( cl = SV_ClientFromEdict( player, false )) == NULL )
		return false;

	return FBitSet( cl->flags, FCL_LOCAL_WEAPONS ) ? true : false;
}

/*
=============
pfnGetCurrentPlayer

=============
*/
int pfnGetCurrentPlayer( void )
{
	int	idx = sv.current_client - svs.clients;

	if( idx < 0 || idx >= svs.maxclients )
		return -1;
	return idx;
}

/*
=============
pfnSetGroupMask

=============
*/
void pfnSetGroupMask( int mask, int op )
{
	svs.groupmask = mask;
	svs.groupop = op;
}

/*
=============
pfnCreateInstancedBaseline

=============
*/
int pfnCreateInstancedBaseline( int classname, struct entity_state_s *baseline )
{
	if( !baseline || sv.num_instanced >= MAX_CUSTOM_BASELINES )
		return 0;

	// g-cont. must sure that classname is really allocated
	sv.instanced[sv.num_instanced].classname = SV_CopyString( STRING( classname ));
	sv.instanced[sv.num_instanced].baseline = *baseline;
	sv.num_instanced++;

	return sv.num_instanced;
}

/*
=============
pfnEndSection

=============
*/
void pfnEndSection( const char *pszSection )
{
	if( !Q_stricmp( "oem_end_credits", pszSection ))
		Host_Credits ();
	else Cbuf_AddText( "\ndisconnect\n" );
}

/*
=============
pfnGetPlayerUserId

=============
*/
int pfnGetPlayerUserId( edict_t *e )
{
	sv_client_t	*cl;

	if(( cl = SV_ClientFromEdict( e, false )) == NULL )
		return -1;
	return cl->userid;
}

/*
=============
pfnGetPlayerStats

=============
*/
void pfnGetPlayerStats( const edict_t *pClient, int *ping, int *packet_loss )
{
	sv_client_t	*cl;

	if( packet_loss ) *packet_loss = 0;
	if( ping ) *ping = 0;

	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
		return;

	if( packet_loss ) *packet_loss = cl->packet_loss;
	if( ping ) *ping = cl->latency * 1000;
}
	
/*
=============
pfnForceUnmodified

=============
*/
void pfnForceUnmodified( FORCE_TYPE type, float *mins, float *maxs, const char *filename )
{
	consistency_t	*pc;
	int		i;

	if( !COM_CheckString( filename ))
		return;

	if( sv.state == ss_loading )
	{
		for( i = 0; i < MAX_MODELS; i++ )
		{
			pc = &sv.consistency_list[i];

			if( !pc->filename )
			{
				if( mins ) VectorCopy( mins, pc->mins );
				if( maxs ) VectorCopy( maxs, pc->maxs );
				pc->filename = SV_CopyString( filename );
				pc->check_type = type;
				return;
			}
			else if( !Q_strcmp( filename, pc->filename ))
				return;
		}
		Host_Error( "MAX_MODELS limit exceeded (%d)\n", MAX_MODELS );
	}
	else
	{
		for( i = 0; i < MAX_MODELS; i++ )
		{
			pc = &sv.consistency_list[i];
			if( !pc->filename ) continue;

			if( !Q_strcmp( filename, pc->filename ))
				return;
		}
		Con_Printf( S_ERROR "Failed to enforce consistency for %s: was not precached\n", filename );
	}
}

/*
=============
pfnVoice_GetClientListening

=============
*/
qboolean pfnVoice_GetClientListening( int iReceiver, int iSender )
{
	iReceiver -= 1;
	iSender -= 1;

	if( iReceiver < 0 || iReceiver >= svs.maxclients || iSender < 0 || iSender > svs.maxclients )
		return false;

	return (FBitSet( svs.clients[iSender].listeners, BIT( iReceiver )) != 0 );
}

/*
=============
pfnVoice_SetClientListening

=============
*/
qboolean pfnVoice_SetClientListening( int iReceiver, int iSender, qboolean bListen )
{
	iReceiver -= 1;
	iSender -= 1;

	if( iReceiver < 0 || iReceiver >= svs.maxclients || iSender < 0 || iSender > svs.maxclients )
		return false;

	if( bListen ) SetBits( svs.clients[iSender].listeners, BIT( iReceiver ));
	else ClearBits( svs.clients[iSender].listeners, BIT( iReceiver ));

	return true;
}

/*
=============
pfnGetPlayerAuthId

These function must returns cd-key hashed value
but Xash3D currently doesn't have any security checks
return nullstring for now
=============
*/
const char *pfnGetPlayerAuthId( edict_t *e )
{
	return SV_GetClientIDString( SV_ClientFromEdict( e, false ));
}

/*
=============
pfnQueryClientCvarValue

request client cvar value
=============
*/
void pfnQueryClientCvarValue( const edict_t *player, const char *cvarName )
{
	sv_client_t *cl;

	if( !COM_CheckString( cvarName ))
		return;

	if(( cl = SV_ClientFromEdict( player, true )) != NULL )
	{
		MSG_BeginServerCmd( &cl->netchan.message, svc_querycvarvalue );
		MSG_WriteString( &cl->netchan.message, cvarName );
	}
	else
	{
		if( svgame.dllFuncs2.pfnCvarValue )
			svgame.dllFuncs2.pfnCvarValue( player, "Bad Player" );
		Con_Printf( S_ERROR "QueryClientCvarValue: tried to send to a non-client!\n" );
	}
}

/*
=============
pfnQueryClientCvarValue2

request client cvar value (bugfixed)
=============
*/
void pfnQueryClientCvarValue2( const edict_t *player, const char *cvarName, int requestID )
{
	sv_client_t *cl;

	if( !COM_CheckString( cvarName ))
		return;

	if(( cl = SV_ClientFromEdict( player, true )) != NULL )
	{
		MSG_BeginServerCmd( &cl->netchan.message, svc_querycvarvalue2 );
		MSG_WriteLong( &cl->netchan.message, requestID );
		MSG_WriteString( &cl->netchan.message, cvarName );
	}
	else
	{
		if( svgame.dllFuncs2.pfnCvarValue2 )
			svgame.dllFuncs2.pfnCvarValue2( player, requestID, cvarName, "Bad Player" );
		Con_Printf( S_ERROR "QueryClientCvarValue: tried to send to a non-client!\n" );
	}
}

/*
=============
pfnEngineStub

extended iface stubs
=============
*/
static int pfnGetFileSize( char *filename )
{
	return 0;
}
static unsigned int pfnGetApproxWavePlayLen(const char *filepath)
{
	return 0;
}
static int pfnGetLocalizedStringLength(const char *label)
{
	return 0;
}

// engine callbacks
static enginefuncs_t gEngfuncs = 
{
	pfnPrecacheModel,
	SV_SoundIndex,	
	pfnSetModel,
	pfnModelIndex,
	pfnModelFrames,
	pfnSetSize,	
	pfnChangeLevel,
	pfnGetSpawnParms,
	pfnSaveSpawnParms,
	pfnVecToYaw,
	VectorAngles,
	pfnMoveToOrigin,
	pfnChangeYaw,
	pfnChangePitch,
	SV_FindEntityByString,
	pfnGetEntityIllum,
	pfnFindEntityInSphere,
	pfnFindClientInPVS,
	pfnEntitiesInPVS,
	pfnMakeVectors,
	AngleVectors,
	SV_AllocEdict,
	pfnRemoveEntity,
	pfnCreateNamedEntity,
	pfnMakeStatic,
	pfnEntIsOnFloor,
	pfnDropToFloor,
	pfnWalkMove,
	pfnSetOrigin,
	SV_StartSound,
	pfnEmitAmbientSound,
	pfnTraceLine,
	pfnTraceToss,
	pfnTraceMonsterHull,
	pfnTraceHull,
	pfnTraceModel,
	pfnTraceTexture,
	pfnTraceSphere,
	pfnGetAimVector,
	pfnServerCommand,
	pfnServerExecute,
	pfnClientCommand,
	pfnParticleEffect,
	pfnLightStyle,
	pfnDecalIndex,
	SV_PointContents,
	pfnMessageBegin,
	pfnMessageEnd,
	pfnWriteByte,
	pfnWriteChar,
	pfnWriteShort,
	pfnWriteLong,
	pfnWriteAngle,
	pfnWriteCoord,
	pfnWriteString,
	pfnWriteEntity,
	pfnCvar_RegisterServerVariable,
	Cvar_VariableValue,
	Cvar_VariableString,
	Cvar_SetValue,
	Cvar_Set,
	pfnAlertMessage,
	pfnEngineFprintf,
	pfnPvAllocEntPrivateData,
	pfnPvEntPrivateData,
	SV_FreePrivateData,
	SV_GetString,
	SV_AllocString,
	pfnGetVarsOfEnt,
	pfnPEntityOfEntOffset,
	pfnEntOffsetOfPEntity,
	pfnIndexOfEdict,
	pfnPEntityOfEntIndex,
	pfnFindEntityByVars,
	pfnGetModelPtr,
	pfnRegUserMsg,
	pfnAnimationAutomove,
	pfnGetBonePosition,
	(void*)pfnFunctionFromName,
	(void*)pfnNameForFunction,
	pfnClientPrintf,
	pfnServerPrint,	
	Cmd_Args,
	Cmd_Argv,
	(void*)Cmd_Argc,
	pfnGetAttachment,
	CRC32_Init,
	CRC32_ProcessBuffer,
	CRC32_ProcessByte,
	CRC32_Final,
	COM_RandomLong,
	COM_RandomFloat,
	pfnSetView,
	pfnTime,
	pfnCrosshairAngle,
	COM_LoadFileForMe,
	COM_FreeFile,
	pfnEndSection,
	COM_CompareFileTime,
	pfnGetGameDir,
	pfnCvar_RegisterEngineVariable,
	pfnFadeClientVolume,
	pfnSetClientMaxspeed,
	SV_FakeConnect,
	pfnRunPlayerMove,
	pfnNumberOfEntities,
	pfnGetInfoKeyBuffer,
	Info_ValueForKey,
	pfnSetValueForKey,
	pfnSetClientKeyValue,
	pfnIsMapValid,
	pfnStaticDecal,
	SV_GenericIndex,
	pfnGetPlayerUserId,
	pfnBuildSoundMsg,
	pfnIsDedicatedServer,
	pfnCVarGetPointer,
	pfnGetPlayerWONId,
	(void*)Info_RemoveKey,
	pfnGetPhysicsKeyValue,
	pfnSetPhysicsKeyValue,
	pfnGetPhysicsInfoString,
	pfnPrecacheEvent,
	SV_PlaybackEventFull,
	pfnSetFatPVS,
	pfnSetFatPAS,
	pfnCheckVisibility,
	Delta_SetField,
	Delta_UnsetField,
	Delta_AddEncoder,
	pfnGetCurrentPlayer,
	pfnCanSkipPlayer,
	Delta_FindField,
	Delta_SetFieldByIndex,
	Delta_UnsetFieldByIndex,
	pfnSetGroupMask,	
	pfnCreateInstancedBaseline,
	pfnCVarDirectSet,
	pfnForceUnmodified,
	pfnGetPlayerStats,
	Cmd_AddServerCommand,
	pfnVoice_GetClientListening,
	pfnVoice_SetClientListening,
	pfnGetPlayerAuthId,
	pfnSequenceGet,
	pfnSequencePickSentence,
	pfnGetFileSize,
	pfnGetApproxWavePlayLen,
	pfnIsCareerMatch,
	pfnGetLocalizedStringLength,
	pfnRegisterTutorMessageShown,
	pfnGetTimesTutorMessageShown,
	pfnProcessTutorMessageDecayBuffer,
	pfnConstructTutorMessageDecayBuffer,
	pfnResetTutorMessageDecayData,
	pfnQueryClientCvarValue,
	pfnQueryClientCvarValue2,
	COM_CheckParm,
};

/*
====================
SV_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
====================
*/
qboolean SV_ParseEdict( char **pfile, edict_t *ent )
{
	KeyValueData	pkvd[256]; // per one entity
	qboolean		adjust_origin = false;
	int		i, numpairs = 0;
	char		*classname = NULL;
	char		token[2048];
	vec3_t		origin;

	// go through all the dictionary pairs
	while( 1 )
	{	
		string	keyname;

		// parse key
		if(( *pfile = COM_ParseFile( *pfile, token )) == NULL )
			Host_Error( "ED_ParseEdict: EOF without closing brace\n" );
		if( token[0] == '}' ) break; // end of desc

		Q_strncpy( keyname, token, sizeof( keyname ));

		// parse value	
		if(( *pfile = COM_ParseFile( *pfile, token )) == NULL ) 
			Host_Error( "ED_ParseEdict: EOF without closing brace\n" );

		if( token[0] == '}' )
			Host_Error( "ED_ParseEdict: closing brace without data\n" );

		// ignore attempts to set key ""
		if( !keyname[0] ) continue;

		// "wad" field is already handled
		if( !Q_strcmp( keyname, "wad" ))
			continue;

		// keynames with a leading underscore are used for
		// utility comments and are immediately discarded by engine
		if( FBitSet( world.flags, FWORLD_SKYSPHERE ) && keyname[0] == '_' )
			continue;

		// ignore attempts to set value ""
		if( !token[0] ) continue;

		// create keyvalue strings
		pkvd[numpairs].szClassName = ""; // unknown at this moment
		pkvd[numpairs].szKeyName = copystring( keyname );
		pkvd[numpairs].szValue = copystring( token );
		pkvd[numpairs].fHandled = false;		

		if( !Q_strcmp( keyname, "classname" ) && classname == NULL )
			classname = copystring( pkvd[numpairs].szValue );
		if( ++numpairs >= 256 ) break;
	}
	
	ent = SV_AllocPrivateData( ent, ALLOC_STRING( classname ));

	if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME ))
	{
		// release allocated strings
		for( i = 0; i < numpairs; i++ )
		{
			Mem_Free( pkvd[i].szKeyName );
			Mem_Free( pkvd[i].szValue );
		}
		return false;
	}

	if( FBitSet( ent->v.flags, FL_CUSTOMENTITY ))
	{
		if( numpairs < 256 )
		{
			pkvd[numpairs].szClassName = "custom";
			pkvd[numpairs].szKeyName = "customclass";
			pkvd[numpairs].szValue = classname;
			pkvd[numpairs].fHandled = false;
			numpairs++;
		}

		// clear it now - no longer used
		ClearBits( ent->v.flags, FL_CUSTOMENTITY );
	}

#ifdef HACKS_RELATED_HLMODS
	// chemical existence have broked changelevels
	if( !Q_stricmp( GI->gamefolder, "ce" ))
	{
	 	if( !Q_stricmp( sv.name, "ce08_02" ) && !Q_stricmp( classname, "info_player_start_force" ))
			adjust_origin = true;
	}
#endif

	for( i = 0; i < numpairs; i++ )
	{
		if( !Q_strcmp( pkvd[i].szKeyName, "angle" ))
		{
			float	flYawAngle = Q_atof( pkvd[i].szValue );

			Mem_Free( pkvd[i].szKeyName ); // will be replace with 'angles'
			Mem_Free( pkvd[i].szValue );	// release old value, so we don't need these
			pkvd[i].szKeyName = copystring( "angles" );

			if( flYawAngle >= 0.0f )
				pkvd[i].szValue = copystring( va( "%g %g %g", ent->v.angles[0], flYawAngle, ent->v.angles[2] ));
			else if( flYawAngle == -1.0f )
				pkvd[i].szValue = copystring( "-90 0 0" );
			else if( flYawAngle == -2.0f )
				pkvd[i].szValue = copystring( "90 0 0" );
			else pkvd[i].szValue = copystring( "0 0 0" ); // technically an error
		}

#ifdef HACKS_RELATED_HLMODS
		if( adjust_origin && !Q_strcmp( pkvd[i].szKeyName, "origin" ))
		{
			char	*pstart = pkvd[i].szValue;

			COM_ParseVector( &pstart, origin, 3 );
			Mem_Free( pkvd[i].szValue );	// release old value, so we don't need these
			pkvd[i].szValue = copystring( va( "%g %g %g", origin[0], origin[1], origin[2] - 16.0f ));
		}
#endif
		if( !Q_strcmp( pkvd[i].szKeyName, "light" ))
		{
			Mem_Free( pkvd[i].szKeyName );
			pkvd[i].szKeyName = copystring( "light_level" );
		}

		if( !pkvd[i].fHandled )
		{
			pkvd[i].szClassName = classname;
			svgame.dllFuncs.pfnKeyValue( ent, &pkvd[i] );
		}

		// no reason to keep this data
		if( Mem_IsAllocatedExt( host.mempool, pkvd[i].szKeyName ))
			Mem_Free( pkvd[i].szKeyName );

		if( Mem_IsAllocatedExt( host.mempool, pkvd[i].szValue ))
			Mem_Free( pkvd[i].szValue );
	}

	if( classname && Mem_IsAllocatedExt( host.mempool, classname ))
		Mem_Free( classname );

	return true;
}

/*
================
SV_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.
================
*/
void SV_LoadFromFile( const char *mapname, char *entities )
{
	char	token[2048];
	qboolean	create_world = true;
	int	inhibited;
	edict_t	*ent;

	Assert( entities != NULL );

	// user dll can override spawn entities function (Xash3D extension)
	if( !svgame.physFuncs.SV_LoadEntities || !svgame.physFuncs.SV_LoadEntities( mapname, entities ))
	{
		inhibited = 0;

		// parse ents
		while(( entities = COM_ParseFile( entities, token )) != NULL )
		{
			if( token[0] != '{' )
				Host_Error( "ED_LoadFromFile: found %s when expecting {\n", token );

			if( create_world )
			{
				create_world = false;
				ent = EDICT_NUM( 0 ); // already initialized
			}
			else ent = SV_AllocEdict();

			if( !SV_ParseEdict( &entities, ent ))
				continue;

			if( svgame.dllFuncs.pfnSpawn( ent ) == -1 )
			{
				// game rejected the spawn
				if( !FBitSet( ent->v.flags, FL_KILLME ))
				{
					SV_FreeEdict( ent );
					inhibited++;
				}
			}
		}

		Con_DPrintf( "\n%i entities inhibited\n", inhibited );
	}

	// reset world origin and angles for some reason
	VectorClear( svgame.edicts->v.origin );
	VectorClear( svgame.edicts->v.angles );
}

/*
==============
SpawnEntities

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.
==============
*/
void SV_SpawnEntities( const char *mapname )
{
	edict_t	*ent;

	// reset misc parms
	Cvar_Reset( "sv_zmax" );
	Cvar_Reset( "sv_wateramp" );
	Cvar_Reset( "sv_wateralpha" );

	// reset sky parms	
	Cvar_Reset( "sv_skycolor_r" );
	Cvar_Reset( "sv_skycolor_g" );
	Cvar_Reset( "sv_skycolor_b" );
	Cvar_Reset( "sv_skyvec_x" );
	Cvar_Reset( "sv_skyvec_y" );
	Cvar_Reset( "sv_skyvec_z" );
	Cvar_Reset( "sv_skyname" );

	ent = EDICT_NUM( 0 );
	if( ent->free ) SV_InitEdict( ent );
	ent->v.model = MAKE_STRING( sv.model_precache[1] );
	ent->v.modelindex = WORLD_INDEX; // world model
	ent->v.solid = SOLID_BSP;
	ent->v.movetype = MOVETYPE_PUSH;
	svgame.movevars.fog_settings = 0;

	svgame.globals->maxEntities = GI->max_edicts;
	svgame.globals->mapname = MAKE_STRING( sv.name );
	svgame.globals->startspot = MAKE_STRING( sv.startspot );
	svgame.globals->time = sv.time;

	// spawn the rest of the entities on the map
	SV_LoadFromFile( mapname, sv.worldmodel->entities );
}

void SV_UnloadProgs( void )
{
	if( !svgame.hInstance )
		return;

	SV_DeactivateServer ();
	Delta_Shutdown ();
	/// TODO: reenable this when
	/// SV_UnloadProgs will be disabled
	//Mod_ClearUserData ();

	SV_FreeStringPool();

	if( svgame.dllFuncs2.pfnGameShutdown != NULL )
		svgame.dllFuncs2.pfnGameShutdown ();

	// now we can unload cvars
	Cvar_FullSet( "host_gameloaded", "0", FCVAR_READ_ONLY );
	Cvar_FullSet( "sv_background", "0", FCVAR_READ_ONLY );

	// free entity baselines
	Z_Free( svs.static_entities );
	Z_Free( svs.baselines );
	svs.baselines = NULL;

	// remove server cmds
	SV_KillOperatorCommands();

	// must unlink all game cvars,
	// before pointers on them will be lost...
	Cvar_Unlink( FCVAR_EXTDLL );
	Cmd_Unlink( CMD_SERVERDLL );

	Mod_ResetStudioAPI ();

	COM_FreeLibrary( svgame.hInstance );
	Mem_FreePool( &svgame.mempool );
	memset( &svgame, 0, sizeof( svgame ));
}

qboolean SV_LoadProgs( const char *name )
{
	int			i, version;
	static APIFUNCTION		GetEntityAPI;
	static APIFUNCTION2		GetEntityAPI2;
	static GIVEFNPTRSTODLL	GiveFnptrsToDll;
	static NEW_DLL_FUNCTIONS_FN	GiveNewDllFuncs;
	static enginefuncs_t	gpEngfuncs;
	static globalvars_t		gpGlobals;
	static playermove_t		gpMove;
	edict_t			*e;

	if( svgame.hInstance ) SV_UnloadProgs();

	// fill it in
	svgame.pmove = &gpMove;
	svgame.globals = &gpGlobals;
	svgame.mempool = Mem_AllocPool( "Server Edicts Zone" );

	svgame.hInstance = COM_LoadLibrary( name, true, false );
	if( !svgame.hInstance )
	{
		Mem_FreePool(&svgame.mempool);
		return false;
	}

	// make sure what new dll functions is cleared
	memset( &svgame.dllFuncs2, 0, sizeof( svgame.dllFuncs2 ));

	// make sure what physic functions is cleared
	memset( &svgame.physFuncs, 0, sizeof( svgame.physFuncs ));

	// make local copy of engfuncs to prevent overwrite it with bots.dll
	memcpy( &gpEngfuncs, &gEngfuncs, sizeof( gpEngfuncs ));

	GetEntityAPI = (APIFUNCTION)COM_GetProcAddress( svgame.hInstance, "GetEntityAPI" );
	GetEntityAPI2 = (APIFUNCTION2)COM_GetProcAddress( svgame.hInstance, "GetEntityAPI2" );
	GiveNewDllFuncs = (NEW_DLL_FUNCTIONS_FN)COM_GetProcAddress( svgame.hInstance, "GetNewDLLFunctions" );

	if( !GetEntityAPI && !GetEntityAPI2 )
	{
		COM_FreeLibrary( svgame.hInstance );
		Con_Printf( S_ERROR "SV_LoadProgs: failed to get address of GetEntityAPI proc\n" );
		svgame.hInstance = NULL;
		Mem_FreePool(&svgame.mempool);
		return false;
	}

	GiveFnptrsToDll = (GIVEFNPTRSTODLL)COM_GetProcAddress( svgame.hInstance, "GiveFnptrsToDll" );

	if( !GiveFnptrsToDll )
	{
		COM_FreeLibrary( svgame.hInstance );
		Con_Printf( S_ERROR "SV_LoadProgs: failed to get address of GiveFnptrsToDll proc\n" );
		svgame.hInstance = NULL;
		Mem_FreePool(&svgame.mempool);
		return false;
	}

	GiveFnptrsToDll( &gpEngfuncs, svgame.globals );

	// get extended callbacks
	if( GiveNewDllFuncs )
	{
		version = NEW_DLL_FUNCTIONS_VERSION;
	
		if( !GiveNewDllFuncs( &svgame.dllFuncs2, &version ))
		{
			if( version != NEW_DLL_FUNCTIONS_VERSION )
				Con_Printf( S_WARN "SV_LoadProgs: new interface version %i should be %i\n", NEW_DLL_FUNCTIONS_VERSION, version );
			memset( &svgame.dllFuncs2, 0, sizeof( svgame.dllFuncs2 ));
		}
	}

	version = INTERFACE_VERSION;

	if( GetEntityAPI2 )
	{
		if( !GetEntityAPI2( &svgame.dllFuncs, &version ))
		{
			Con_Printf( S_WARN "SV_LoadProgs: interface version %i should be %i\n", INTERFACE_VERSION, version );

			// fallback to old API
			if( !GetEntityAPI( &svgame.dllFuncs, version ))
			{
				COM_FreeLibrary( svgame.hInstance );
				Con_Printf( S_ERROR "SV_LoadProgs: couldn't get entity API\n" );
				svgame.hInstance = NULL;
				Mem_FreePool(&svgame.mempool);
				return false;
			}
		}
		else Con_Reportf( "SV_LoadProgs: ^2initailized extended EntityAPI ^7ver. %i\n", version );
	}
	else if( !GetEntityAPI( &svgame.dllFuncs, version ))
	{
		COM_FreeLibrary( svgame.hInstance );
		Con_Printf( S_ERROR "SV_LoadProgs: couldn't get entity API\n" );
		svgame.hInstance = NULL;
		Mem_FreePool(&svgame.mempool);
		return false;
	}

	SV_InitOperatorCommands();
	Mod_InitStudioAPI();

	if( !SV_InitPhysicsAPI( ))
	{
		Con_Printf( S_WARN "SV_LoadProgs: couldn't get physics API\n" );
	}

	// grab function SV_SaveGameComment
	SV_InitSaveRestore ();

	svgame.globals->pStringBase = ""; // setup string base

	svgame.globals->maxEntities = GI->max_edicts;
	svgame.globals->maxClients = svs.maxclients;
	svgame.edicts = Mem_Calloc( svgame.mempool, sizeof( edict_t ) * GI->max_edicts );
	svs.static_entities = Z_Calloc( sizeof( entity_state_t ) * MAX_STATIC_ENTITIES );
	svs.baselines = Z_Calloc( sizeof( entity_state_t ) * GI->max_edicts );
	svgame.numEntities = svs.maxclients + 1; // clients + world

	for( i = 0, e = svgame.edicts; i < GI->max_edicts; i++, e++ )
		e->free = true; // mark all edicts as freed

	Cvar_FullSet( "host_gameloaded", "1", FCVAR_READ_ONLY );
	SV_AllocStringPool();

	// fire once
	Con_Printf( "Dll loaded for game ^2\"%s\"\n", svgame.dllFuncs.pfnGetGameDescription( ));

	// all done, initialize game
	svgame.dllFuncs.pfnGameInit();

	// initialize pm_shared
	SV_InitClientMove();

	Delta_Init ();

	// register custom encoders
	svgame.dllFuncs.pfnRegisterEncoders();

	return true;
}
