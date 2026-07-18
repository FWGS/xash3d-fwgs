/*
sv_save.c - save\restore implementation
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
#include "library.h"
#include "const.h"
#include "render_api.h"	// decallist_t
#include "sound.h"		// S_GetDynamicSounds
#include "ref_common.h" // decals
#if XASH_GAMECUBE
#include <stdlib.h>
#include "mod_local.h"
#endif

/*
==============================================================================
SAVE FILE

half-life implementation of saverestore system
==============================================================================
*/
#define SAVEFILE_HEADER		(('V'<<24)+('L'<<16)+('A'<<8)+'V')	// little-endian "VALV"
#define SAVEGAME_HEADER		(('V'<<24)+('A'<<16)+('S'<<8)+'J')	// little-endian "JSAV"
#define SAVEGAME_VERSION		0x0071				// Version 0.71 GoldSrc compatible
#define CLIENT_SAVEGAME_VERSION	0x0067				// Version 0.67

#if XASH_GAMECUBE
#define GC_SAVE_META_MAGIC		"XASHGC_SAVE_META"
#define GC_SAVE_META_VERSION		1
#define GC_SAVE_META_EXTENSION		".gcmeta"
/* G94: lean sysheap after PVS free — zone MEM1 cannot host SAVE_HEAPSIZE. */
#define GC_PROBE_SAVE_HEAPSIZE		0x30000			/* 192 KiB */
#define GC_PROBE_SAVE_HASHSTRINGS	0x1FF			/* 511 tokens */
void GC_ResetNewGameWorldForChangelevel( void );
void GC_MarkNewGameWorldStale( void );
static qboolean GC_LeanLandmarkStash( const char *landmark );
void GC_LeanLandmarkRestore( void );
void GC_LeanLandmarkProbePlantAmmo( void );
void GC_LeanLandmarkGrantWeapons( void );
static qboolean gc_save_use_sysheap;
qboolean gc_lean_weapon_grant_active;
#endif // XASH_GAMECUBE

#define SAVE_HEAPSIZE		0x400000				// reserve 4Mb for now
#define SAVE_HASHSTRINGS		0xFFF				// 4095 unique strings

#if XASH_GAMECUBE
static int SV_SaveHeapSize( void )
{
	if( Sys_CheckParm( "-gcnewsaveload" ))
		return GC_PROBE_SAVE_HEAPSIZE;
	return SAVE_HEAPSIZE;
}

static int SV_SaveHashStrings( void )
{
	if( Sys_CheckParm( "-gcnewsaveload" ))
		return GC_PROBE_SAVE_HASHSTRINGS;
	return SAVE_HASHSTRINGS;
}
#else
#define SV_SaveHeapSize()	SAVE_HEAPSIZE
#define SV_SaveHashStrings()	SAVE_HASHSTRINGS
#endif
// savedata headers
typedef struct
{
	char	mapName[32];
	char	comment[80];
	int	mapCount;
} GAME_HEADER;

typedef struct
{
	int	skillLevel;
	int	entityCount;
	int	connectionCount;
	int	lightStyleCount;
	float	time;
	char	mapName[32];
	char	skyName[32];
	int	skyColor_r;
	int	skyColor_g;
	int	skyColor_b;
	float	skyVec_x;
	float	skyVec_y;
	float	skyVec_z;
} SAVE_HEADER;

typedef struct
{
	int	decalCount;	// render decals count
	int	entityCount;	// static entity count
	int	soundCount;	// sounds count
	int	tempEntsCount;	// not used
	char	introTrack[64];
	char	mainTrack[64];
	int	trackPosition;
	short	viewentity;	// Xash3D added
	float	wateralpha;
	float	wateramp;		// world waves
} SAVE_CLIENT;

typedef struct
{
	int	index;
	char	style[256];
	float	time;
} SAVE_LIGHTSTYLE;

#if XASH_WIN32
static void (__cdecl *pfnSaveGameComment)( char *buffer, int max_length ) = NULL;
#else // XASH_WIN32
static void (*pfnSaveGameComment)( char *buffer, int max_length ) = NULL;
#endif // XASH_WIN32

static TYPEDESCRIPTION gGameHeader[] =
{
	DEFINE_ARRAY( GAME_HEADER, mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( GAME_HEADER, comment, FIELD_CHARACTER, 80 ),
	DEFINE_FIELD( GAME_HEADER, mapCount, FIELD_INTEGER ),
};

static TYPEDESCRIPTION gSaveHeader[] =
{
	DEFINE_FIELD( SAVE_HEADER, skillLevel, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, entityCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, connectionCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, lightStyleCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, time, FIELD_TIME ),
	DEFINE_ARRAY( SAVE_HEADER, mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( SAVE_HEADER, skyName, FIELD_CHARACTER, 32 ),
	DEFINE_FIELD( SAVE_HEADER, skyColor_r, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, skyColor_g, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, skyColor_b, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, skyVec_x, FIELD_FLOAT ),
	DEFINE_FIELD( SAVE_HEADER, skyVec_y, FIELD_FLOAT ),
	DEFINE_FIELD( SAVE_HEADER, skyVec_z, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gAdjacency[] =
{
	DEFINE_ARRAY( LEVELLIST, mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( LEVELLIST, landmarkName, FIELD_CHARACTER, 32 ),
	DEFINE_FIELD( LEVELLIST, pentLandmark, FIELD_EDICT ),
	DEFINE_FIELD( LEVELLIST, vecLandmarkOrigin, FIELD_VECTOR ),
};

static TYPEDESCRIPTION gLightStyle[] =
{
	DEFINE_FIELD( SAVE_LIGHTSTYLE, index, FIELD_INTEGER ),
	DEFINE_ARRAY( SAVE_LIGHTSTYLE, style, FIELD_CHARACTER, 256 ),
	DEFINE_FIELD( SAVE_LIGHTSTYLE, time, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gEntityTable[] =
{
	DEFINE_FIELD( ENTITYTABLE, id, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, location, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, size, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, flags, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, classname, FIELD_STRING ),
};

static TYPEDESCRIPTION gSaveClient[] =
{
	DEFINE_FIELD( SAVE_CLIENT, decalCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_CLIENT, entityCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_CLIENT, soundCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_CLIENT, tempEntsCount, FIELD_INTEGER ),
	DEFINE_ARRAY( SAVE_CLIENT, introTrack, FIELD_CHARACTER, 64 ),
	DEFINE_ARRAY( SAVE_CLIENT, mainTrack, FIELD_CHARACTER, 64 ),
	DEFINE_FIELD( SAVE_CLIENT, trackPosition, FIELD_INTEGER ),
	// mods based on HLU SDK disallow usage of FIELD_SHORT
	DEFINE_ARRAY( SAVE_CLIENT, viewentity, FIELD_CHARACTER, sizeof( short )),
	DEFINE_FIELD( SAVE_CLIENT, wateralpha, FIELD_FLOAT ),
	DEFINE_FIELD( SAVE_CLIENT, wateramp, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gDecalEntry[] =
{
	DEFINE_FIELD( decallist_t, position, FIELD_VECTOR ),
	DEFINE_ARRAY( decallist_t, name, FIELD_CHARACTER, 64 ),
	// mods based on HLU SDK disallow usage of FIELD_SHORT
	DEFINE_ARRAY( decallist_t, entityIndex, FIELD_CHARACTER, sizeof( short )),
	DEFINE_FIELD( decallist_t, depth, FIELD_CHARACTER ),
	DEFINE_FIELD( decallist_t, flags, FIELD_CHARACTER ),
	DEFINE_FIELD( decallist_t, scale, FIELD_FLOAT ),
	DEFINE_FIELD( decallist_t, impactPlaneNormal, FIELD_VECTOR ),
	DEFINE_ARRAY( decallist_t, studio_state, FIELD_CHARACTER, sizeof( modelstate_t )),
};

// Can use any FIELD type here because only Xash3D games will spawn static entities
static TYPEDESCRIPTION gStaticEntry[] =
{
	DEFINE_FIELD( entity_state_t, messagenum, FIELD_MODELNAME ), // HACKHACK: store model into messagenum
	DEFINE_FIELD( entity_state_t, origin, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, angles, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, sequence, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, frame, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, colormap, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, skin, FIELD_SHORT ),
	DEFINE_FIELD( entity_state_t, body, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, scale, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, effects, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, framerate, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, mins, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, maxs, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, startpos, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, rendermode, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, renderamt, FIELD_FLOAT ),
	DEFINE_ARRAY( entity_state_t, rendercolor, FIELD_CHARACTER, sizeof( color24 )),
	DEFINE_FIELD( entity_state_t, renderfx, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, controller, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, blending, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, solid, FIELD_SHORT ),
	DEFINE_FIELD( entity_state_t, animtime, FIELD_TIME ),
	DEFINE_FIELD( entity_state_t, movetype, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, vuser1, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, vuser2, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, vuser3, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, vuser4, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, iuser1, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, iuser2, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, iuser3, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, iuser4, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, fuser1, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, fuser2, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, fuser3, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, fuser4, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gSoundEntry[] =
{
	DEFINE_ARRAY( soundlist_t, name, FIELD_CHARACTER, 64 ),
	// mods based on HLU SDK disallow usage of FIELD_SHORT
	DEFINE_ARRAY( soundlist_t, entnum, FIELD_CHARACTER, sizeof( short )),
	DEFINE_FIELD( soundlist_t, origin, FIELD_VECTOR ),
	DEFINE_FIELD( soundlist_t, volume, FIELD_FLOAT ),
	DEFINE_FIELD( soundlist_t, attenuation, FIELD_FLOAT ),
	DEFINE_FIELD( soundlist_t, looping, FIELD_BOOLEAN ),
	DEFINE_FIELD( soundlist_t, channel, FIELD_CHARACTER ),
	DEFINE_FIELD( soundlist_t, pitch, FIELD_CHARACTER ),
	DEFINE_FIELD( soundlist_t, wordIndex, FIELD_CHARACTER ),
	DEFINE_ARRAY( soundlist_t, samplePos, FIELD_CHARACTER, sizeof( double )),
	DEFINE_ARRAY( soundlist_t, forcedEnd, FIELD_CHARACTER, sizeof( double )),
};

static TYPEDESCRIPTION gTempEntvars[] =
{
	DEFINE_ENTITY_FIELD( classname, FIELD_STRING ),
	DEFINE_ENTITY_GLOBAL_FIELD( globalname, FIELD_STRING ),
};

static const struct
{
	const char *mapname;
	const char *titlename;
} gTitleComments[] =
{
	// default Half-Life map titles
	// ordering is important
	// strings hw.so| grep T0A0TITLE -B 50 -A 150
	{ "T0A0", "#T0A0TITLE" },
	{ "C0A0", "#C0A0TITLE" },
	{ "C1A0", "#C0A1TITLE" },
	{ "C1A1", "#C1A1TITLE" },
	{ "C1A2", "#C1A2TITLE" },
	{ "C1A3", "#C1A3TITLE" },
	{ "C1A4", "#C1A4TITLE" },
	{ "C2A1", "#C2A1TITLE" },
	{ "C2A2", "#C2A2TITLE" },
	{ "C2A3", "#C2A3TITLE" },
	{ "C2A4D", "#C2A4TITLE2" },
	{ "C2A4E", "#C2A4TITLE2" },
	{ "C2A4F", "#C2A4TITLE2" },
	{ "C2A4G", "#C2A4TITLE2" },
	{ "C2A4", "#C2A4TITLE1" },
	{ "C2A5", "#C2A5TITLE" },
	{ "C3A1", "#C3A1TITLE" },
	{ "C3A2", "#C3A2TITLE" },
	{ "C4A1A", "#C4A1ATITLE" },
	{ "C4A1B", "#C4A1ATITLE" },
	{ "C4A1C", "#C4A1ATITLE" },
	{ "C4A1D", "#C4A1ATITLE" },
	{ "C4A1E", "#C4A1ATITLE" },
	{ "C4A1", "#C4A1TITLE" },
	{ "C4A2", "#C4A2TITLE" },
	{ "C4A3", "#C4A3TITLE" },
	{ "C5A1", "#C5TITLE" },
	{ "OFBOOT", "#OF_BOOT0TITLE" },
	{ "OF0A", "#OF1A1TITLE" },
	{ "OF1A1", "#OF1A3TITLE" },
	{ "OF1A2", "#OF1A3TITLE" },
	{ "OF1A3", "#OF1A3TITLE" },
	{ "OF1A4", "#OF1A3TITLE" },
	{ "OF1A", "#OF1A5TITLE" },
	{ "OF2A1", "#OF2A1TITLE" },
	{ "OF2A2", "#OF2A1TITLE" },
	{ "OF2A3", "#OF2A1TITLE" },
	{ "OF2A", "#OF2A4TITLE" },
	{ "OF3A1", "#OF3A1TITLE" },
	{ "OF3A2", "#OF3A1TITLE" },
	{ "OF3A", "#OF3A3TITLE" },
	{ "OF4A1", "#OF4A1TITLE" },
	{ "OF4A2", "#OF4A1TITLE" },
	{ "OF4A3", "#OF4A1TITLE" },
	{ "OF4A", "#OF4A4TITLE" },
	{ "OF5A", "#OF5A1TITLE" },
	{ "OF6A1", "#OF6A1TITLE" },
	{ "OF6A2", "#OF6A1TITLE" },
	{ "OF6A3", "#OF6A1TITLE" },
	{ "OF6A4b", "#OF6A4TITLE" },
	{ "OF6A4", "#OF6A4TITLE" },
	{ "OF6A5", "#OF6A4TITLE" },
	{ "OF6A", "#OF6A4TITLE" },
	{ "OF7A", "#OF7A0TITLE" },
	{ "ba_tram", "#BA_TRAMTITLE" },
	{ "ba_security", "#BA_SECURITYTITLE" },
	{ "ba_main", "#BA_SECURITYTITLE" },
	{ "ba_elevator", "#BA_SECURITYTITLE" },
	{ "ba_canal", "#BA_CANALSTITLE" },
	{ "ba_yard", "#BA_YARDTITLE" },
	{ "ba_xen", "#BA_XENTITLE" },
	{ "ba_hazard", "#BA_HAZARD" },
	{ "ba_power", "#BA_POWERTITLE" },
	{ "ba_teleport1", "#BA_POWERTITLE" },
	{ "ba_teleport", "#BA_TELEPORTTITLE" },
	{ "ba_outro", "#BA_OUTRO" },
};

/*
=============
SaveBuildComment

build commentary for each savegame
typically it writes world message and level time
=============
*/
static void SaveBuildComment( char *text, int maxlength )
{
	string      comment;
	const char *pName = NULL;

	text[0] = '\0'; // clear

	if( pfnSaveGameComment != NULL )
	{
		// get save comment from gamedll
		pfnSaveGameComment( comment, MAX_STRING );
		pName = comment;
	}
	else
	{
		size_t i;
		const char *mapname = SV_GetString( svgame.globals->mapname );

		for( i = 0; i < ARRAYSIZE( gTitleComments ); i++ )
		{
			// compare if strings are equal at beginning
			size_t len = strlen( gTitleComments[i].mapname );
			if( !Q_strnicmp( mapname, gTitleComments[i].mapname, len ))
			{
				pName = gTitleComments[i].titlename;
				break;
			}
		}

		if( !pName )
		{
			if( svgame.edicts->v.message != 0 )
			{
				// trying to extract message from the world
				pName = SV_GetString( svgame.edicts->v.message );
			}
			else
			{
				// or use mapname
				pName = SV_GetString( svgame.globals->mapname );
			}
		}
	}

	Q_snprintf( text, maxlength, "%-64.64s %02d:%02d", pName, (int)(sv.time / 60.0 ), (int)fmod( sv.time, 60.0 ));
}

/*
=============
DirectoryCount

counting all the files with HL1-HL3 extension
in save folder
=============
*/
static int DirectoryCount( const char *pPath )
{
	int	count;
	search_t	*t;

#if XASH_GAMECUBE
	/* G94 probe bank is not visible to FS_Search. */
	if( Sys_CheckParm( "-gcnewsaveload" ))
	{
		char path[MAX_OSPATH];
		const char *exts[] = { "HL1", "HL2", "HL3" };
		int i, n = 0;

		(void)pPath;
		for( i = 0; i < 3; i++ )
		{
			Q_snprintf( path, sizeof( path ), DEFAULT_SAVE_DIRECTORY "%s.%s", sv.name, exts[i] );
			if( FS_FileExists( path, true ))
				n++;
		}
		return n;
	}
#endif

	t = FS_Search( pPath, true, true );	// lookup only in gamedir
	if( !t ) return 0; // empty

	count = t->numfilenames;
	Mem_Free( t );

	return count;
}

/*
=============
InitEntityTable

reserve space for ETABLE's
=============
*/
static void InitEntityTable( SAVERESTOREDATA *pSaveData, int entityCount )
{
#if XASH_GAMECUBE
	if( gc_save_use_sysheap )
		pSaveData->pTable = (ENTITYTABLE *)calloc( (size_t)entityCount, sizeof( ENTITYTABLE ));
	else
#endif
	pSaveData->pTable = Mem_Calloc( host.mempool, sizeof( ENTITYTABLE ) * entityCount );
	pSaveData->tableCount = entityCount;

	/* setup entitytable */
	for( int i = 0; i < entityCount; i++ )
	{
		ENTITYTABLE *pTable = &pSaveData->pTable[i];
		pTable->pent = SV_EdictNum( i );
		pTable->id = i;
	}
}

/*
=============
EntryInTable

check level in transition list
=============
*/
static int EntryInTable( SAVERESTOREDATA *pSaveData, const char *pMapName, int index )
{
	for( int i = index + 1; i < pSaveData->connectionCount; i++ )
	{
		if ( !Q_stricmp( pSaveData->levelList[i].mapName, pMapName ))
			return i;
	}

	return -1;
}

/*
=============
EdictFromTable

get edict from table
=============
*/
static edict_t *EdictFromTable( SAVERESTOREDATA *pSaveData, int entityIndex )
{
	if( pSaveData && pSaveData->pTable )
	{
		entityIndex = bound( 0, entityIndex, pSaveData->tableCount - 1 );
		return pSaveData->pTable[entityIndex].pent;
	}

	return NULL;
}

/*
=============
LandmarkOrigin

find global offset for a given landmark
=============
*/
static void LandmarkOrigin( SAVERESTOREDATA *pSaveData, vec3_t output, const char *pLandmarkName )
{
	for( int i = 0; i < pSaveData->connectionCount; i++ )
	{
		if( !Q_strcmp( pSaveData->levelList[i].landmarkName, pLandmarkName ))
		{
			VectorCopy( pSaveData->levelList[i].vecLandmarkOrigin, output );
			return;
		}
	}

	VectorClear( output );
}

/*
=============
EntityInSolid

some moved edicts on a next level cause stuck
outside of world. Find them and remove
=============
*/
static int EntityInSolid( edict_t *pent )
{
	edict_t	*aiment = pent->v.aiment;
	vec3_t	point;

	// if you're attached to a client, always go through
	if( pent->v.movetype == MOVETYPE_FOLLOW && SV_IsValidEdict( aiment ) && FBitSet( aiment->v.flags, FL_CLIENT ))
		return 0;

	VectorAverage( pent->v.absmin, pent->v.absmax, point );
	svs.groupmask = pent->v.groupinfo;

	return (SV_PointContents( point ) == CONTENTS_SOLID);
}

/*
=============
ClearSaveDir

remove all the temp files HL1-HL3
(it will be extracted again from another .sav file)
=============
*/
static void ClearSaveDir( void )
{
	search_t	*t;

	// just delete all HL? files
	t = FS_Search( DEFAULT_SAVE_DIRECTORY "*.HL?", true, true );
	if( !t ) return; // already empty

	for( int i = 0; i < t->numfilenames; i++ )
		FS_Delete( t->filenames[i] );

	Mem_Free( t );
}

/*
=============
IsValidSave

savegame is allowed?
=============
*/
static int IsValidSave( void )
{
	if( !svs.initialized || sv.state != ss_active )
	{
		Con_Printf( "Not playing a local game.\n" );
		return 0;
	}

	// ignore autosave during background
	if( sv.background || UI_CreditsActive( ))
		return 0;

	if( svgame.physFuncs.SV_AllowSaveGame != NULL )
	{
		if( !svgame.physFuncs.SV_AllowSaveGame( ))
		{
			Con_Printf( "Savegame is not allowed.\n" );
			return 0;
		}
	}

	if( !CL_Active( ))
	{
		Con_Printf( "Can't save if not active.\n" );
		return 0;
	}

	if( CL_IsIntermission( ))
	{
		Con_Printf( "Can't save during intermission.\n" );
		return 0;
	}

	if( svs.maxclients != 1 )
	{
		Con_Printf( "Can't save multiplayer games.\n" );
		return 0;
	}

	if( svs.clients && svs.clients[0].state == cs_spawned )
	{
		edict_t	*pl = svs.clients[0].edict;

		if( !pl )
		{
			Con_Printf( "Can't savegame without a player!\n" );
			return 0;
		}

		if( pl->v.deadflag || pl->v.health <= 0.0f )
		{
			Con_Printf( "Can't savegame with a dead player\n" );
			return 0;
		}

		// Passed all checks, it's ok to save
		return 1;
	}

	Con_Printf( "Can't savegame without a client!\n" );

	return 0;
}

/*
=============
AgeSaveList

scroll the name list down
=============
*/
static void AgeSaveList( const char *pName, int count )
{
	char	newName[MAX_OSPATH], oldName[MAX_OSPATH];
	char	newShot[MAX_OSPATH], oldShot[MAX_OSPATH];
#if XASH_GAMECUBE
	char	newMeta[MAX_OSPATH], oldMeta[MAX_OSPATH];
#endif // XASH_GAMECUBE

	// delete last quick/autosave (e.g. quick05.sav)
	Q_snprintf( newName, sizeof( newName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count );
	Q_snprintf( newShot, sizeof( newShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count );
#if XASH_GAMECUBE
	Q_snprintf( newMeta, sizeof( newMeta ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav" GC_SAVE_META_EXTENSION, pName, count );
#endif // XASH_GAMECUBE

	// only delete from game directory, basedir is read-only
	FS_Delete( newName );
	FS_Delete( newShot );
#if XASH_GAMECUBE
	FS_Delete( newMeta );
#endif // XASH_GAMECUBE

#if !XASH_DEDICATED
	// unloading the shot footprint
	GL_FreeImage( newShot );
#endif // XASH_DEDICATED

	while( count > 0 )
	{
		if( count == 1 )
		{
			// quick.sav
			Q_snprintf( oldName, sizeof( oldName ), DEFAULT_SAVE_DIRECTORY "%s.sav", pName );
			Q_snprintf( oldShot, sizeof( oldShot ), DEFAULT_SAVE_DIRECTORY "%s.bmp", pName );
#if XASH_GAMECUBE
			Q_snprintf( oldMeta, sizeof( oldMeta ), DEFAULT_SAVE_DIRECTORY "%s.sav" GC_SAVE_META_EXTENSION, pName );
#endif // XASH_GAMECUBE
		}
		else
		{
			// quick04.sav, etc.
			Q_snprintf( oldName, sizeof( oldName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count - 1 );
			Q_snprintf( oldShot, sizeof( oldShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count - 1 );
#if XASH_GAMECUBE
			Q_snprintf( oldMeta, sizeof( oldMeta ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav" GC_SAVE_META_EXTENSION, pName, count - 1 );
#endif // XASH_GAMECUBE
		}

		Q_snprintf( newName, sizeof( newName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count );
		Q_snprintf( newShot, sizeof( newShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count );
#if XASH_GAMECUBE
		Q_snprintf( newMeta, sizeof( newMeta ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav" GC_SAVE_META_EXTENSION, pName, count );
#endif // XASH_GAMECUBE

#if !XASH_DEDICATED
		// unloading the oldshot footprint too
		GL_FreeImage( oldShot );
#endif // XASH_DEDICATED

		// scroll the name list down (e.g. rename quick04.sav to quick05.sav)
		FS_Rename( oldName, newName );
		FS_Rename( oldShot, newShot );
#if XASH_GAMECUBE
		FS_Rename( oldMeta, newMeta );
#endif // XASH_GAMECUBE
		count--;
	}
}

#if XASH_GAMECUBE
static qboolean GC_SaveFileCRC32( const char *path, uint32_t *crcOut, fs_offset_t *sizeOut )
{
	byte	buffer[4096];
	file_t	*f;
	uint32_t	crc;
	fs_offset_t	total = 0;

	if(( f = FS_Open( path, "rb", true )) == NULL )
		return false;

	CRC32_Init( &crc );
	while( 1 )
	{
		fs_offset_t got = FS_Read( f, buffer, sizeof( buffer ));
		if( got <= 0 )
			break;
		CRC32_ProcessBuffer( &crc, buffer, (int)got );
		total += got;
	}
	FS_Close( f );

	*crcOut = CRC32_Final( crc );
	*sizeOut = total;
	return true;
}

static void GC_WriteSaveMetadata( const char *savePath, const char *saveName )
{
	char		metaPath[MAX_OSPATH];
	char		tmpPath[MAX_OSPATH];
	char		backupPath[MAX_OSPATH];
	char		writablePath[MAX_OSPATH];
	const char	*storageRoute = "none";
	uint32_t	crc = 0;
	fs_offset_t	payloadSize = 0;
	file_t		*f;

	if( !GCube_HasWritableStorage( ))
	{
		Con_Printf( S_WARN "GameCube save metadata skipped: no writable storage\n" );
		return;
	}

	/* Probe bank has no rename/delete; CRC metadata is SD/G46 only. */
	if( Sys_CheckParm( "-gcnewsaveload" ))
	{
		Con_Reportf( "Xash3D GameCube: G94 save metadata skipped (probe bank)\n" );
		return;
	}

	if( GCube_GetWritablePath( writablePath, sizeof( writablePath )))
		storageRoute = writablePath;

	if( !GC_SaveFileCRC32( savePath, &crc, &payloadSize ))
	{
		Con_Printf( S_WARN "GameCube save metadata skipped: could not read %s\n", savePath );
		return;
	}

	Q_snprintf( metaPath, sizeof( metaPath ), "%s%s", savePath, GC_SAVE_META_EXTENSION );
	Q_snprintf( tmpPath, sizeof( tmpPath ), "%s.tmp", metaPath );
	Q_snprintf( backupPath, sizeof( backupPath ), "%s.bak", metaPath );

	if(( f = FS_Open( tmpPath, "w", true )) == NULL )
	{
		Con_Printf( S_WARN "GameCube save metadata skipped: could not open %s\n", tmpPath );
		return;
	}

	FS_Printf( f, "magic=%s\n", GC_SAVE_META_MAGIC );
	FS_Printf( f, "version=%d\n", GC_SAVE_META_VERSION );
	FS_Printf( f, "save=%s\n", saveName );
	FS_Printf( f, "payload=%s\n", savePath );
	FS_Printf( f, "payload_size=%d\n", (int)payloadSize );
	FS_Printf( f, "payload_crc32=%08x\n", (unsigned)crc );
	FS_Printf( f, "map=%s\n", sv.name );
	FS_Printf( f, "build=%s\n", g_buildcommit );
	FS_Printf( f, "storage_route=%s\n", storageRoute );
	FS_Close( f );

	FS_Delete( backupPath );
	FS_Rename( metaPath, backupPath );
	if( !FS_Rename( tmpPath, metaPath ))
	{
		Con_Printf( S_WARN "GameCube save metadata commit failed for %s\n", metaPath );
		FS_Delete( tmpPath );
		FS_Rename( backupPath, metaPath );
		return;
	}
	FS_Delete( backupPath );
	Con_Printf( "GameCube save metadata: %s crc=%08x size=%d\n", metaPath, (unsigned)crc, (int)payloadSize );
}
#endif // XASH_GAMECUBE

/*
=============
DirectoryCopy

put the HL1-HL3 files into .sav file
=============
*/
static void DirectoryCopy( const char *pPath, file_t *pFile )
{
	search_t	*t;

#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnewsaveload" ))
	{
		const char *exts[] = { "HL1", "HL2", "HL3" };
		int i;

		(void)pPath;
		for( i = 0; i < 3; i++ )
		{
			char	path[MAX_OSPATH];
			char	szName[MAX_OSPATH];
			file_t	*pCopy;
			int	fileSize;

			Q_snprintf( path, sizeof( path ), DEFAULT_SAVE_DIRECTORY "%s.%s", sv.name, exts[i] );
			if( !FS_FileExists( path, true ))
				continue;
			pCopy = FS_Open( path, "rb", true );
			if( !pCopy )
				continue;
			fileSize = FS_FileLength( pCopy );
			memset( szName, 0, sizeof( szName ));
			Q_snprintf( szName, sizeof( szName ), "%s.%s", sv.name, exts[i] );
			FS_Write( pFile, szName, MAX_OSPATH );
			FS_Write( pFile, &fileSize, sizeof( int ));
			FS_FileCopy( pFile, pCopy, fileSize );
			FS_Close( pCopy );
		}
		return;
	}
#endif

	t = FS_Search( pPath, true, true );
	if( !t ) return; // nothing to copy ?

	for( int i = 0; i < t->numfilenames; i++ )
	{
		char	szName[MAX_OSPATH];
		file_t	*pCopy = FS_Open( t->filenames[i], "rb", true );
		int	fileSize = FS_FileLength( pCopy );

		memset( szName, 0, sizeof( szName )); // clearing the string to prevent garbage in output file
		Q_strncpy( szName, COM_FileWithoutPath( t->filenames[i] ), sizeof( szName ));
		FS_Write( pFile, szName, MAX_OSPATH );
		FS_Write( pFile, &fileSize, sizeof( int ));
		FS_FileCopy( pFile, pCopy, fileSize );
		FS_Close( pCopy );
	}
	Mem_Free( t );
}

/*
=============
DirectoryExtract

extract the HL1-HL3 files from the .sav file
=============
*/
static qboolean DirectoryExtract( file_t *pFile, int fileCount )
{
	for( int i = 0; i < fileCount; i++ )
	{
		char	szName[MAX_OSPATH];
		char	fileName[MAX_OSPATH];
		int	fileSize;
		file_t	*pCopy;

		// filename can only be as long as a map name + extension
		FS_Read( pFile, szName, MAX_OSPATH );
		FS_Read( pFile, &fileSize, sizeof( int ));
		Q_snprintf( fileName, sizeof( fileName ), DEFAULT_SAVE_DIRECTORY "%s", szName );
		COM_FixSlashes( fileName );

		pCopy = FS_Open( fileName, "wb", true );
		if( !pCopy )
		{
			Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, fileName );
			return false;
		}

		FS_FileCopy( pCopy, pFile, fileSize );
		FS_Close( pCopy );
	}

	return true;
}

/*
=============
SaveInit

initialize global save-restore buffer
=============
*/
static SAVERESTOREDATA *SaveInit( int size, int tokenCount )
{
	SAVERESTOREDATA	*pSaveData;

#if XASH_GAMECUBE
	gc_save_use_sysheap = false;
	if( Sys_CheckParm( "-gcnewsaveload" ))
	{
		pSaveData = (SAVERESTOREDATA *)calloc( 1, sizeof( SAVERESTOREDATA ) + (size_t)size );
		if( !pSaveData )
		{
			Con_Printf( S_ERROR "SaveInit: calloc %d bytes failed\n",
				(int)( sizeof( SAVERESTOREDATA ) + size ));
			return NULL;
		}
		pSaveData->pTokens = (char **)calloc( (size_t)tokenCount, sizeof( char* ));
		if( !pSaveData->pTokens )
		{
			free( pSaveData );
			Con_Printf( S_ERROR "SaveInit: calloc tokens failed\n" );
			return NULL;
		}
		gc_save_use_sysheap = true;
		Con_Reportf( "Xash3D GameCube: G94 SaveInit sysheap size=%d tokens=%d\n", size, tokenCount );
	}
	else
#endif
	{
		pSaveData = Mem_Calloc( host.mempool, sizeof( SAVERESTOREDATA ) + size );
		pSaveData->pTokens = (char **)Mem_Calloc( host.mempool, tokenCount * sizeof( char* ));
	}
	pSaveData->tokenCount = tokenCount;

	pSaveData->pBaseData = (char *)(pSaveData + 1); // skip the save structure);
	pSaveData->pCurrentData = pSaveData->pBaseData; // reset the pointer
	pSaveData->bufferSize = size;

	pSaveData->time = svgame.globals->time;	// Use DLL time

	// shared with dlls
	svgame.globals->pSaveData = pSaveData;

	return pSaveData;
}

/*
=============
SaveClear

clearing buffer for reuse
=============
*/
static void SaveClear( SAVERESTOREDATA *pSaveData )
{
	memset( pSaveData->pTokens, 0, pSaveData->tokenCount * sizeof( char* ));

	pSaveData->pBaseData = (char *)(pSaveData + 1); // skip the save structure);
	pSaveData->pCurrentData = pSaveData->pBaseData; // reset the pointer
	pSaveData->time = svgame.globals->time;	// Use DLL time
	pSaveData->tokenSize = 0;	// reset the hashtable
	pSaveData->size = 0;	// reset the pointer

	// shared with dlls
	svgame.globals->pSaveData = pSaveData;
}

/*
=============
SaveFinish

release global save-restore buffer
=============
*/
static void SaveFinish( SAVERESTOREDATA *pSaveData )
{
	if( !pSaveData ) return;

	if( pSaveData->pTokens )
	{
#if XASH_GAMECUBE
		if( gc_save_use_sysheap )
			free( pSaveData->pTokens );
		else
#endif
		Mem_Free( pSaveData->pTokens );
		pSaveData->pTokens = NULL;
		pSaveData->tokenCount = 0;
	}

	if( pSaveData->pTable )
	{
#if XASH_GAMECUBE
		if( gc_save_use_sysheap )
			free( pSaveData->pTable );
		else
#endif
		Mem_Free( pSaveData->pTable );
		pSaveData->pTable = NULL;
		pSaveData->tableCount = 0;
	}

	svgame.globals->pSaveData = NULL;
#if XASH_GAMECUBE
	if( gc_save_use_sysheap )
	{
		free( pSaveData );
		gc_save_use_sysheap = false;
	}
	else
#endif
	Mem_Free( pSaveData );
}

/*
=============
StoreHashTable

write the stringtable into file
=============
*/
static char *StoreHashTable( SAVERESTOREDATA *pSaveData )
{
	char	*pTokenData = pSaveData->pCurrentData;

	// Write entity string token table
	if( pSaveData->pTokens )
	{
		for( int i = 0; i < pSaveData->tokenCount; i++ )
		{
			const char *pszToken = pSaveData->pTokens[i] ? pSaveData->pTokens[i] : "";

			// just copy the token byte-by-byte
			while( *pszToken )
				*pSaveData->pCurrentData++ = *pszToken++;
			*pSaveData->pCurrentData++ = 0; // Write the term
		}
	}

	pSaveData->tokenSize = pSaveData->pCurrentData - pTokenData;

	return pTokenData;
}

/*
=============
BuildHashTable

build the stringtable from buffer
=============
*/
static void BuildHashTable( SAVERESTOREDATA *pSaveData, file_t *pFile )
{
	char	*pszTokenList = pSaveData->pBaseData;

	// Parse the symbol table
	if( pSaveData->tokenSize > 0 )
	{
		FS_Read( pFile, pszTokenList, pSaveData->tokenSize );

		// make sure the token strings pointed to by the pToken hashtable.
		for( int i = 0; i < pSaveData->tokenCount; i++ )
		{
			pSaveData->pTokens[i] = *pszTokenList ? pszTokenList : NULL;
			while( *pszTokenList++ );	// Find next token (after next null)
		}
	}

	// rebase the data pointer
	pSaveData->pBaseData = pszTokenList;	// pszTokenList now points after token data
	pSaveData->pCurrentData = pSaveData->pBaseData;
}

/*
=============
GetClientDataSize

g-cont: this routine is redundant
i'm write it just for more readable code
=============
*/
static int GetClientDataSize( const char *level )
{
	int	tokenCount, tokenSize;
	int	size, id, version;
	char	name[MAX_QPATH];
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL2", level );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
		return 0;

	FS_Read( pFile, &id, sizeof( id ));
	if( id != SAVEGAME_HEADER )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &version, sizeof( version ));
	if( version != CLIENT_SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &size, sizeof( int ));
	FS_Read( pFile, &tokenCount, sizeof( int ));
	FS_Read( pFile, &tokenSize, sizeof( int ));
	FS_Close( pFile );

	return ( size + tokenSize );
}

/*
=============
LoadSaveData

fill the save resore buffer
parse hash strings
=============
*/
static SAVERESTOREDATA *LoadSaveData( const char *level )
{
	int		tokenSize, tableCount;
	int		size, tokenCount;
	char		name[MAX_OSPATH];
	int		id, version;
	int		clientSize;
	SAVERESTOREDATA	*pSaveData;
	int		totalSize;
	file_t		*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL1", level );
	Con_Printf( "Loading game from %s...\n", name );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
	{
		Con_Printf( S_ERROR "Couldn't open save data file %s.\n", name );
		return NULL;
	}

	// Read the header
	FS_Read( pFile, &id, sizeof( int ));
	FS_Read( pFile, &version, sizeof( int ));

	// is this a valid save?
	if( id != SAVEFILE_HEADER || version != SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return NULL;
	}

	// Read the sections info and the data
	FS_Read( pFile, &size, sizeof( int ));		// total size of all data to initialize read buffer
	FS_Read( pFile, &tableCount, sizeof( int ));	// entities count to right initialize entity table
	FS_Read( pFile, &tokenCount, sizeof( int ));	// num hash tokens to prepare token table
	FS_Read( pFile, &tokenSize, sizeof( int ));	// total size of hash tokens

	// determine highest size of seve-restore buffer
	// because it's used twice: for HL1 and HL2 restore
	clientSize = GetClientDataSize( level );
	totalSize = Q_max( clientSize, ( size + tokenSize ));

	// init the read buffer
	pSaveData = SaveInit( totalSize, tokenCount );

	Q_strncpy( pSaveData->szCurrentMapName, level, sizeof( pSaveData->szCurrentMapName ));
	pSaveData->tableCount = tableCount;		// count ETABLE entries
	pSaveData->tokenCount = tokenCount;
	pSaveData->tokenSize = tokenSize;

	// Parse the symbol table
	BuildHashTable( pSaveData, pFile );

	// Set up the restore basis
	pSaveData->fUseLandmark = true;
	pSaveData->time = 0.0f;

	// now reading all the rest of data
	FS_Read( pFile, pSaveData->pBaseData, size );
	FS_Close( pFile ); // data is sucessfully moved into SaveRestore buffer (ETABLE will be init later)

	return pSaveData;
}

/*
=============
ParseSaveTables

reading global data, setup ETABLE's
=============
*/
static void ParseSaveTables( SAVERESTOREDATA *pSaveData, SAVE_HEADER *pHeader, int updateGlobals )
{
	SAVE_LIGHTSTYLE	light;
	int		i;

	// Re-base the savedata since we re-ordered the entity/table / restore fields
	InitEntityTable( pSaveData, pSaveData->tableCount );

	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ETABLE", &pSaveData->pTable[i], gEntityTable, ARRAYSIZE( gEntityTable ));
		pSaveData->pTable[i].pent = NULL;
	}

	pSaveData->pBaseData = pSaveData->pCurrentData;
	pSaveData->size = 0;

	// process SAVE_HEADER
	svgame.dllFuncs.pfnSaveReadFields( pSaveData, "Save Header", pHeader, gSaveHeader, ARRAYSIZE( gSaveHeader ));

	pSaveData->connectionCount = pHeader->connectionCount;
	VectorClear( pSaveData->vecLandmarkOffset );
	pSaveData->time = pHeader->time;
	pSaveData->fUseLandmark = true;

	// read adjacency list
	for( i = 0; i < pSaveData->connectionCount; i++ )
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ADJACENCY", &pSaveData->levelList[i], gAdjacency, ARRAYSIZE( gAdjacency ));

	if( updateGlobals )
		memset( sv.lightstyles, 0, sizeof( sv.lightstyles ));

	for( i = 0; i < pHeader->lightStyleCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "LIGHTSTYLE", &light, gLightStyle, ARRAYSIZE( gLightStyle ));
		if( updateGlobals ) SV_SetLightStyle( light.index, light.style, light.time );
	}
}

/*
=============
EntityPatchWrite

write out the list of entities that are no longer in the save file for this level
(they've been moved to another level)
=============
*/
static qboolean EntityPatchWrite( SAVERESTOREDATA *pSaveData, const char *level )
{
	char	name[MAX_QPATH];
	int	i, size = 0;
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL3", level );

	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		return false;
	}

	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		if( FBitSet( pSaveData->pTable[i].flags, FENTTABLE_REMOVED ))
			size++;
	}

	// patch count
	FS_Write( pFile, &size, sizeof( int ));

	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		if( FBitSet( pSaveData->pTable[i].flags, FENTTABLE_REMOVED ))
			FS_Write( pFile, &i, sizeof( int ));
	}

	FS_Close( pFile );

	return true;
}

/*
=============
EntityPatchRead

read the list of entities that are no longer in the save file for this level
(they've been moved to another level)
=============
*/
static void EntityPatchRead( SAVERESTOREDATA *pSaveData, const char *level )
{
	char	name[MAX_QPATH];
	int	size;
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL3", level );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
		return;

	// patch count
	FS_Read( pFile, &size, sizeof( int ));

	for( int i = 0; i < size; i++ )
	{
		int	entityId;

		FS_Read( pFile, &entityId, sizeof( int ));
		pSaveData->pTable[entityId].flags = FENTTABLE_REMOVED;
	}

	FS_Close( pFile );
}

/*
=============
RestoreDecal

restore decal\move across transition
=============
*/
static void RestoreDecal( SAVERESTOREDATA *pSaveData, decallist_t *entry, qboolean adjacent )
{
	int	decalIndex, entityIndex = 0;
	int	flags = entry->flags;
	int	modelIndex = 0;
	edict_t	*pEdict;

	// never move permanent decals
	if( adjacent && FBitSet( flags, FDECAL_PERMANENT ))
		return;

	// restore entity and model index
	pEdict = EdictFromTable( pSaveData, entry->entityIndex );

	if( SV_RestoreCustomDecal( entry, pEdict, adjacent ))
		return; // decal was sucessfully restored at the game-side

	// studio decals are handled at game-side
	if( FBitSet( flags, FDECAL_STUDIO ))
		return;

	if( SV_IsValidEdict( pEdict ))
		modelIndex = pEdict->v.modelindex;

	if( SV_IsValidEdict( pEdict ))
		entityIndex = NUM_FOR_EDICT( pEdict );

	decalIndex = pfnDecalIndex( entry->name );

	// this can happens if brush entity from previous level was turned into world geometry
	if( adjacent && entry->entityIndex != 0 && !SV_IsValidEdict( pEdict ))
	{
		trace_t	tr;

		Con_Printf( S_ERROR "RestoreDecal: couldn't restore entity index %i\n", entry->entityIndex );

		vec3_t testspot = Vec3( entry->position );
		VectorMA( testspot, 5.0f, entry->impactPlaneNormal, testspot );

		vec3_t testend = Vec3( entry->position );
		VectorMA( testend, -5.0f, entry->impactPlaneNormal, testend );

		tr = SV_Move( testspot, vec3_origin, vec3_origin, testend, MOVE_NOMONSTERS, NULL, false );

		// NOTE: this code may does wrong result on moving brushes e.g. func_tracktrain
		if( tr.fraction != 1.0f && !tr.allsolid )
		{
			// check impact plane normal
			float	dot = DotProduct( entry->impactPlaneNormal, tr.plane.normal );

			if( dot >= 0.95f )
			{
				entityIndex = pfnIndexOfEdict( tr.ent );
				if( entityIndex > 0 ) modelIndex = tr.ent->v.modelindex;
				SV_CreateDecal( &sv.signon, tr.endpos, decalIndex, entityIndex, modelIndex, flags, entry->scale );
			}
		}
	}
	else
	{
		// global entity is exist on new level so we can apply decal in local space
		SV_CreateDecal( &sv.signon, entry->position, decalIndex, entityIndex, modelIndex, flags, entry->scale );
	}
}

/*
=============
RestoreSound

continue playing sound from saved position
=============
*/
static void RestoreSound( SAVERESTOREDATA *pSaveData, soundlist_t *snd )
{
	edict_t	*ent = EdictFromTable( pSaveData, snd->entnum );
	int	flags = SND_RESTORE_POSITION;

	// this can happens if serialized map contain 4096 static decals...
	if( MSG_GetNumBytesLeft( &sv.signon ) < 36 )
		return;

	if( !snd->looping )
		SetBits( flags, SND_STOP_LOOPING );

	if( SV_BuildSoundMsg( &sv.signon, ent, snd->channel, snd->name, snd->volume * 255, snd->attenuation, flags, snd->pitch, snd->origin ))
	{
		// write extradata for svc_restoresound
		MSG_WriteByte( &sv.signon, snd->wordIndex );
		MSG_WriteBytes( &sv.signon, &snd->samplePos, sizeof( snd->samplePos ));
		MSG_WriteBytes( &sv.signon, &snd->forcedEnd, sizeof( snd->forcedEnd ));
	}
}

/*
=============
SaveClientState

write out the list of premanent decals for this level
=============
*/
static qboolean SaveClientState( SAVERESTOREDATA *pSaveData, const char *level, int changelevel )
{
	soundlist_t	soundInfo[MAX_CHANNELS];
	sv_client_t	*cl = svs.clients;
	char		name[MAX_QPATH];
	int		i;
	char		*pTokenData;
	decallist_t	*decalList = NULL;
	SAVE_CLIENT	header = { 0 };
	file_t		*pFile;

	// clearing the saving buffer to reuse
	SaveClear( pSaveData );

	header.entityCount = sv.num_static_entities;

	// initialize client header
#if !XASH_DEDICATED
	if( !Host_IsDedicated( ))
	{
		// g-cont. add space for studiodecals if present
		decalList = (decallist_t *)Mem_Calloc( host.mempool, sizeof( decallist_t ) * MAX_RENDER_DECALS * 2 );

		header.decalCount = ref.dllFuncs.R_CreateDecalList( decalList );

		if( !changelevel ) // sounds won't going across transition
		{
			header.soundCount = S_GetCurrentDynamicSounds( soundInfo, MAX_CHANNELS );

			// music not reqiured to save position: it's just continue playing on a next level
			S_StreamGetCurrentState(
				header.introTrack, sizeof( header.introTrack ),
				header.mainTrack, sizeof( header.mainTrack ),
				&header.trackPosition );
		}
	}
#endif // XASH_DEDICATED

	// save viewentity to allow camera works after save\restore
	if( SV_IsValidEdict( cl->pViewEntity ) && cl->pViewEntity != cl->edict )
		header.viewentity = NUM_FOR_EDICT( cl->pViewEntity );

	header.wateralpha = sv_wateralpha.value;
	header.wateramp = sv_wateramp.value;

	// Store the client header
	svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "ClientHeader", &header, gSaveClient, ARRAYSIZE( gSaveClient ));

	// store decals
	for( i = 0; decalList != NULL && i < header.decalCount; i++ )
	{
		// NOTE: apply landmark offset only for brush entities without origin brushes
		if( pSaveData->fUseLandmark && FBitSet( decalList[i].flags, FDECAL_USE_LANDMARK ))
			VectorSubtract( decalList[i].position, pSaveData->vecLandmarkOffset, decalList[i].position );

		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "DECALLIST", &decalList[i], gDecalEntry, ARRAYSIZE( gDecalEntry ));
	}

	if( decalList )
		Mem_Free( decalList );

	// write client entities
	for( i = 0; i < header.entityCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "STATICENTITY", &svs.static_entities[i], gStaticEntry, ARRAYSIZE( gStaticEntry ));

	// write sounds
	for( i = 0; i < header.soundCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "SOUNDLIST", &soundInfo[i], gSoundEntry, ARRAYSIZE( gSoundEntry ));

	// Write entity string token table
	pTokenData = StoreHashTable( pSaveData );

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL2", level );

	// output to disk
	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		return false;
	}

	int version = CLIENT_SAVEGAME_VERSION;
	int id = SAVEGAME_HEADER;

	FS_Write( pFile, &id, sizeof( id ));
	FS_Write( pFile, &version, sizeof( version ));
	FS_Write( pFile, &pSaveData->size, sizeof( int )); // does not include token table

	// write out the tokens first so we can load them before we load the entities
	FS_Write( pFile, &pSaveData->tokenCount, sizeof( int ));
	FS_Write( pFile, &pSaveData->tokenSize, sizeof( int ));
	FS_Write( pFile, pTokenData, pSaveData->tokenSize );
	FS_Write( pFile, pSaveData->pBaseData, pSaveData->size ); // header and globals
	FS_Close( pFile );

	return true;
}

/*
=============
LoadClientState

read the list of decals and reapply them again
=============
*/
static void LoadClientState( SAVERESTOREDATA *pSaveData, const char *level, qboolean changelevel, qboolean adjacent )
{
	int		tokenCount, tokenSize;
	int		i, size, id, version;
	sv_client_t	*cl = svs.clients;
	char		name[MAX_QPATH];
	soundlist_t	soundEntry;
	decallist_t	decalEntry;
	SAVE_CLIENT	header;
	file_t		*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL2", level );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
		return; // something bad is happens

	FS_Read( pFile, &id, sizeof( id ));
	if( id != SAVEGAME_HEADER )
	{
		FS_Close( pFile );
		return;
	}

	FS_Read( pFile, &version, sizeof( version ));
	if( version != CLIENT_SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return;
	}

	FS_Read( pFile, &size, sizeof( int ));
	FS_Read( pFile, &tokenCount, sizeof( int ));
	FS_Read( pFile, &tokenSize, sizeof( int ));

	// sanity check
	ASSERT( pSaveData->bufferSize >= ( size + tokenSize ));

	// clearing the restore buffer to reuse
	SaveClear( pSaveData );
	pSaveData->tokenCount = tokenCount;
	pSaveData->tokenSize = tokenSize;

	// Parse the symbol table
	BuildHashTable( pSaveData, pFile );

	FS_Read( pFile, pSaveData->pBaseData, size );
	FS_Close( pFile );

	// Read the client header
	svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ClientHeader", &header, gSaveClient, ARRAYSIZE( gSaveClient ));

	// restore decals
	for( i = 0; i < header.decalCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "DECALLIST", &decalEntry, gDecalEntry, ARRAYSIZE( gDecalEntry ));

		// NOTE: apply landmark offset only for brush entities without origin brushes
		if( pSaveData->fUseLandmark && FBitSet( decalEntry.flags, FDECAL_USE_LANDMARK ))
			VectorAdd( decalEntry.position, pSaveData->vecLandmarkOffset, decalEntry.position );
		RestoreDecal( pSaveData, &decalEntry, adjacent );
	}

	// clear old entities
	if( !adjacent )
	{
		memset( svs.static_entities, 0, sizeof( entity_state_t ) * MAX_STATIC_ENTITIES );
		sv.num_static_entities = 0;
	}

	// restore client entities
	for( i = 0; i < header.entityCount; i++ )
	{
		id = sv.num_static_entities;
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "STATICENTITY", &svs.static_entities[id], gStaticEntry, ARRAYSIZE( gStaticEntry ));
		if( adjacent ) continue; // static entities won't loading from adjacent levels

		if( SV_CreateStaticEntity( &sv.signon, id ))
			sv.num_static_entities++;
	}

	// restore sounds
	for( i = 0; i < header.soundCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "SOUNDLIST", &soundEntry, gSoundEntry, ARRAYSIZE( gSoundEntry ));
		if( adjacent ) continue; // sounds don't going across the levels

		RestoreSound( pSaveData, &soundEntry );
	}

	if( !adjacent )
	{
		// restore camera view here
		edict_t	*pent = pSaveData->pTable[bound( 0, (word)header.viewentity, pSaveData->tableCount )].pent;

		if( !COM_StringEmpty( header.introTrack ))
		{
			// NOTE: music is automatically goes across transition, never restore it on changelevel
			MSG_BeginServerCmd( &sv.signon, svc_stufftext );
			MSG_WriteStringf( &sv.signon, "music \"%s\" \"%s\" %i\n", header.introTrack, header.mainTrack, header.trackPosition );
		}

		// don't go camera across the levels
		if( header.viewentity > svs.maxclients && !changelevel )
			cl->pViewEntity = pent;

		// restore some client cvars
		Cvar_SetValue( "sv_wateralpha", header.wateralpha );
		Cvar_SetValue( "sv_wateramp", header.wateramp );
	}
}

/*
=============
CreateEntitiesInRestoreList

alloc private data for restored entities
=============
*/
static void CreateEntitiesInRestoreList( SAVERESTOREDATA *pSaveData, int levelMask, qboolean create_world )
{
	int		i, active;
	ENTITYTABLE	*pTable;
	edict_t		*pent;

	// create entity list
	if( svgame.physFuncs.pfnCreateEntitiesInRestoreList != NULL )
	{
		svgame.physFuncs.pfnCreateEntitiesInRestoreList( pSaveData, levelMask, create_world );
	}
	else
	{
		for( i = 0; i < pSaveData->tableCount; i++ )
		{
			pTable = &pSaveData->pTable[i];
			pent = NULL;

			if( pTable->classname && pTable->size && ( !FBitSet( pTable->flags, FENTTABLE_REMOVED ) || !create_world ))
			{
				if( !create_world )
					active = FBitSet( pTable->flags, levelMask ) ? 1 : 0;
				else active = 1;

				if( pTable->id == 0 && create_world ) // worldspawn
				{
					pent = SV_EdictNum( 0 );
					SV_InitEdict( pent );
					pent = SV_CreateNamedEntity( pent, pTable->classname );
				}
				else if(( pTable->id > 0 ) && ( pTable->id < svs.maxclients + 1 ))
				{
					edict_t	*ed = SV_EdictNum( pTable->id );

					if( !FBitSet( pTable->flags, FENTTABLE_PLAYER ))
						Con_Printf( S_ERROR "ENTITY IS NOT A PLAYER: %d\n", i );

					// create the player
					if( active && SV_IsValidEdict( ed ))
						pent = SV_CreateNamedEntity( ed, pTable->classname );
				}
				else if( active )
				{
					pent = SV_CreateNamedEntity( NULL, pTable->classname );
				}
			}

			pTable->pent = pent;
		}
	}
}

/*
=============
SaveGameState

save current game state
=============
*/
static SAVERESTOREDATA *SaveGameState( int changelevel )
{
	char		name[MAX_QPATH];
	int		i, id, version;
	char		*pTableData;
	char		*pTokenData;
	SAVERESTOREDATA	*pSaveData;
	int		tableSize;
	int		dataSize;
	ENTITYTABLE	*pTable;
	SAVE_HEADER	header;
	SAVE_LIGHTSTYLE	light;
	file_t		*pFile;

	if( !svgame.dllFuncs.pfnParmsChangeLevel )
		return NULL;

	pSaveData = SaveInit( SV_SaveHeapSize(), SV_SaveHashStrings() );
	if( !pSaveData )
		return NULL;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL1", sv.name );
	COM_FixSlashes( name );

#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnewsaveload" ))
		Con_Reportf( "Xash3D GameCube: G94 SaveGameState entities=%d\n", svgame.numEntities );
#endif
	// initialize entity table to count moved entities
	InitEntityTable( pSaveData, svgame.numEntities );
#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnewsaveload" ))
	{
		if( !pSaveData->pTable )
		{
			Con_Reportf( S_ERROR "Xash3D GameCube: G94 entity table alloc failed\n" );
			SaveFinish( pSaveData );
			return NULL;
		}
		Con_Reportf( "Xash3D GameCube: G94 entity table ready count=%d\n", pSaveData->tableCount );
	}
#endif

	// Build the adjacent map list
#if XASH_GAMECUBE
	/* G94: skip adjacency walk — New Game probe only needs same-map restore. */
	if( Sys_CheckParm( "-gcnewsaveload" ))
	{
		pSaveData->connectionCount = 0;
		Con_Reportf( "Xash3D GameCube: G94 ParmsChangeLevel skipped (probe)\n" );
	}
	else
#endif
	svgame.dllFuncs.pfnParmsChangeLevel();

	// Write the global data
	header.skillLevel = (int)skill.value;	// this is created from an int even though it's a float
	header.entityCount = pSaveData->tableCount;
	header.connectionCount = pSaveData->connectionCount;
	header.time = svgame.globals->time;	// use DLL time
	Q_strncpy( header.mapName, sv.name, sizeof( header.mapName ));
	Q_strncpy( header.skyName, sv_skyname.string, sizeof( header.skyName ));
	header.skyColor_r = sv_skycolor_r.value;
	header.skyColor_g = sv_skycolor_g.value;
	header.skyColor_b = sv_skycolor_b.value;
	header.skyVec_x = sv_skyvec_x.value;
	header.skyVec_y = sv_skyvec_y.value;
	header.skyVec_z = sv_skyvec_z.value;
	header.lightStyleCount = 0;

	// counting the lightstyles
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		if( sv.lightstyles[i].pattern[0] )
			header.lightStyleCount++;
	}

	// Write the main header
	pSaveData->time = 0.0f; // prohibits rebase of header.time (keep compatibility with old saves)
	svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "Save Header", &header, gSaveHeader, ARRAYSIZE( gSaveHeader ));
	pSaveData->time = header.time;

	// Write the adjacency list
	for( i = 0; i < pSaveData->connectionCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "ADJACENCY", &pSaveData->levelList[i], gAdjacency, ARRAYSIZE( gAdjacency ));

	// Write the lightstyles
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		if( !sv.lightstyles[i].pattern[0] )
			continue;

		Q_strncpy( light.style, sv.lightstyles[i].pattern, sizeof( light.style ));
		light.time = sv.lightstyles[i].time;
		light.index = i;

		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "LIGHTSTYLE", &light, gLightStyle, ARRAYSIZE( gLightStyle ));
	}

	// build the table of entities
	// this is used to turn pointers into savable indices
	// build up ID numbers for each entity, for use in pointer conversions
	// if an entity requires a certain edict number upon restore, save that as well
	for( i = 0; i < svgame.numEntities; i++ )
	{
		pTable = &pSaveData->pTable[i];
		pTable->location = pSaveData->size;
		pSaveData->currentIndex = i;
		pTable->size = 0;

		if( !SV_IsValidEdict( pTable->pent ))
			continue;

		svgame.dllFuncs.pfnSave( pTable->pent, pSaveData );

		if( FBitSet( pTable->pent->v.flags, FL_CLIENT ))
			SetBits( pTable->flags, FENTTABLE_PLAYER );
	}

	// total data what includes:
	// 1. save header
	// 2. adjacency list
	// 3. lightstyles
	// 4. all the entity data
	dataSize = pSaveData->size;

	// Write entity table
	pTableData = pSaveData->pCurrentData;

	for( i = 0; i < pSaveData->tableCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "ETABLE", &pSaveData->pTable[i], gEntityTable, ARRAYSIZE( gEntityTable ));

	tableSize = pSaveData->size - dataSize;

	// Write entity string token table
	pTokenData = StoreHashTable( pSaveData );

	// output to disk
	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		SaveFinish( pSaveData );
		return NULL;
	}

	// Write the header -- THIS SHOULD NEVER CHANGE STRUCTURE, USE SAVE_HEADER FOR NEW HEADER INFORMATION
	// THIS IS ONLY HERE TO IDENTIFY THE FILE AND GET IT'S SIZE.
	version = SAVEGAME_VERSION;
	id = SAVEFILE_HEADER;

	// write the header
	FS_Write( pFile, &id, sizeof( id ));
	FS_Write( pFile, &version, sizeof( version ));

	// Write out the tokens and table FIRST so they are loaded in the right order, then write out the rest of the data in the file.
	FS_Write( pFile, &pSaveData->size, sizeof( int ));	// total size of all data to initialize read buffer
	FS_Write( pFile, &pSaveData->tableCount, sizeof( int ));	// entities count to right initialize entity table
	FS_Write( pFile, &pSaveData->tokenCount, sizeof( int ));	// num hash tokens to prepare token table
	FS_Write( pFile, &pSaveData->tokenSize, sizeof( int ));	// total size of hash tokens
	FS_Write( pFile, pTokenData, pSaveData->tokenSize );	// write tokens into the file
	FS_Write( pFile, pTableData, tableSize );		// dump ETABLE structures
	FS_Write( pFile, pSaveData->pBaseData, dataSize );	// and finally store all the other data
	FS_Close( pFile );

	if( !EntityPatchWrite( pSaveData, sv.name ))
	{
		SaveFinish( pSaveData );
		return NULL;
	}

	if( !SaveClientState( pSaveData, sv.name, changelevel ))
	{
		SaveFinish( pSaveData );
		return NULL;
	}

	return pSaveData;
}

/*
=============
LoadGameState

load current game state
=============
*/
static int LoadGameState( char const *level, qboolean changelevel )
{
	SAVERESTOREDATA	*pSaveData;
	SAVE_HEADER	header;

	pSaveData = LoadSaveData( level );
	if( !pSaveData ) return 0; // couldn't load the file

	// must set mapname before calling into DLL
	Q_strncpy( sv.name, level, sizeof( sv.name ));
	svgame.globals->mapname = SV_MakeString( sv.name );

	ParseSaveTables( pSaveData, &header, true );
	EntityPatchRead( pSaveData, level );

	// pause until all clients connect
	sv.loadgame = sv.paused = true;

	Cvar_SetValue( "skill", header.skillLevel );
	Cvar_Set( "sv_skyname", header.skyName );

	// restore sky parms
	Cvar_SetValue( "sv_skycolor_r", header.skyColor_r );
	Cvar_SetValue( "sv_skycolor_g", header.skyColor_g );
	Cvar_SetValue( "sv_skycolor_b", header.skyColor_b );
	Cvar_SetValue( "sv_skyvec_x", header.skyVec_x );
	Cvar_SetValue( "sv_skyvec_y", header.skyVec_y );
	Cvar_SetValue( "sv_skyvec_z", header.skyVec_z );

	// create entity list
	CreateEntitiesInRestoreList( pSaveData, 0, true );

	// now spawn entities
	for( int i = 0; i < pSaveData->tableCount; i++ )
	{
		ENTITYTABLE	*pTable = &pSaveData->pTable[i];
		edict_t		*pent;

		pSaveData->pCurrentData = pSaveData->pBaseData + pTable->location;
		pSaveData->size = pTable->location;
		pSaveData->currentIndex = i;
		pent = pTable->pent;

		if( pent != NULL )
		{
			if( svgame.dllFuncs.pfnRestore( pent, pSaveData, 0 ) < 0 )
			{
				SetBits( pent->v.flags, FL_KILLME );
				pTable->pent = NULL;
			}
			else
			{
				// force the entity to be relinked
//				SV_LinkEdict( pent, false );
			}
		}
	}

	LoadClientState( pSaveData, level, changelevel, false );

	SaveFinish( pSaveData );

	// restore server time
	sv.time = header.time;

	return 1;
}

/*
=============
SaveGameSlot

do a save game
=============
*/
static qboolean SaveGameSlot( const char *pSaveName, const char *pSaveComment )
{
	char		hlPath[MAX_QPATH];
	char		name[MAX_QPATH];
	char		*pTokenData;
	SAVERESTOREDATA	*pSaveData;
	GAME_HEADER	gameHeader;
	file_t		*pFile;

	pSaveData = SaveGameState( false );
	if( !pSaveData )
		return false;

	SaveFinish( pSaveData );
	pSaveData = SaveInit( SV_SaveHeapSize(), SV_SaveHashStrings() ); // re-init the buffer
	if( !pSaveData )
		return false;

	Q_strncpy( hlPath, DEFAULT_SAVE_DIRECTORY "*.HL?", sizeof( hlPath ) );
	Q_strncpy( gameHeader.mapName, sv.name, sizeof( gameHeader.mapName )); // get the name of level where a player
	Q_strncpy( gameHeader.comment, pSaveComment, sizeof( gameHeader.comment ));
	gameHeader.mapCount = DirectoryCount( hlPath ); // counting all the adjacency maps

	// Store the game header
	svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "GameHeader", &gameHeader, gGameHeader, ARRAYSIZE( gGameHeader ));

	// Write the game globals
	svgame.dllFuncs.pfnSaveGlobalState( pSaveData );

	// Write entity string token table
	pTokenData = StoreHashTable( pSaveData );

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.sav", pSaveName );
	COM_FixSlashes( name );

	// output to disk
	if( !Q_stricmp( pSaveName, "quick" ))
		AgeSaveList( pSaveName, GI->quicksave_aged_count );
	else if( !Q_stricmp( pSaveName, "autosave" ))
		AgeSaveList( pSaveName, GI->autosave_aged_count );

	// output to disk
	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		SaveFinish( pSaveData );
		return false;
	}

	// pending the preview image for savegame
#if XASH_GAMECUBE
	if( !Sys_CheckParm( "-gcnewsaveload" ))
#endif
	Cbuf_AddTextf( "saveshot \"%s\"\n", pSaveName );
	Con_Printf( "Saving game to %s...\n", name );

	int version = SAVEGAME_VERSION;
	int id = SAVEGAME_HEADER;

	FS_Write( pFile, &id, sizeof( id ));
	FS_Write( pFile, &version, sizeof( version ));
	FS_Write( pFile, &pSaveData->size, sizeof( int )); // does not include token table

	// write out the tokens first so we can load them before we load the entities
	FS_Write( pFile, &pSaveData->tokenCount, sizeof( int ));
	FS_Write( pFile, &pSaveData->tokenSize, sizeof( int ));
	FS_Write( pFile, pTokenData, pSaveData->tokenSize );
	FS_Write( pFile, pSaveData->pBaseData, pSaveData->size ); // header and globals

	DirectoryCopy( hlPath, pFile );
	SaveFinish( pSaveData );
	FS_Close( pFile );
#if XASH_GAMECUBE
	GC_WriteSaveMetadata( name, pSaveName );
#endif // XASH_GAMECUBE

	return true;
}

/*
=============
SaveReadHeader

read header of .sav file
=============
*/
static int SaveReadHeader( file_t *pFile, GAME_HEADER *pHeader )
{
	int		tokenCount, tokenSize;
	int		size, id, version;
	SAVERESTOREDATA	*pSaveData;

	FS_Read( pFile, &id, sizeof( id ));
	if( id != SAVEGAME_HEADER )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &version, sizeof( version ));
	if( version != SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &size, sizeof( int ));
	FS_Read( pFile, &tokenCount, sizeof( int ));
	FS_Read( pFile, &tokenSize, sizeof( int ));

	pSaveData = SaveInit( size + tokenSize, tokenCount );
	pSaveData->tokenCount = tokenCount;
	pSaveData->tokenSize = tokenSize;

	// Parse the symbol table
	BuildHashTable( pSaveData, pFile );

	// Set up the restore basis
	pSaveData->fUseLandmark = false;
	pSaveData->time = 0.0f;

	FS_Read( pFile, pSaveData->pBaseData, size );

	svgame.dllFuncs.pfnSaveReadFields( pSaveData, "GameHeader", pHeader, gGameHeader, ARRAYSIZE( gGameHeader ));

	svgame.dllFuncs.pfnRestoreGlobalState( pSaveData );

	SaveFinish( pSaveData );

	return 1;
}

/*
=============
CreateEntityTransitionList

moving edicts to another level
=============
*/
static int CreateEntityTransitionList( SAVERESTOREDATA *pSaveData, int levelMask )
{
	int		movedCount;

	movedCount = 0;

	// create entity list
	CreateEntitiesInRestoreList( pSaveData, levelMask, false );

	// now spawn entities
	for( int i = 0; i < pSaveData->tableCount; i++ )
	{
		ENTITYTABLE	*pTable = &pSaveData->pTable[i];
		edict_t		*pent;

		pSaveData->pCurrentData = pSaveData->pBaseData + pTable->location;
		pSaveData->size = pTable->location;
		pSaveData->currentIndex = i;
		pent = pTable->pent;

		if( SV_IsValidEdict( pent ) && FBitSet( pTable->flags, levelMask )) // screen out the player if he's not to be spawned
		{
			if( FBitSet( pTable->flags, FENTTABLE_GLOBAL ))
			{
				entvars_t	tmpVars;
				edict_t	*pNewEnt;

				// NOTE: we need to update table pointer so decals on the global entities with brush models can be
				// correctly moved. found the classname and the globalname for our globalentity
				svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ENTVARS", &tmpVars, gTempEntvars, ARRAYSIZE( gTempEntvars ));

				// reset the save pointers, so dll can read this too
				pSaveData->pCurrentData = pSaveData->pBaseData + pTable->location;
				pSaveData->size = pTable->location;

				// IMPORTANT: we should find the already spawned or local restored global entity
				pNewEnt = SV_FindGlobalEntity( tmpVars.classname, tmpVars.globalname );

				Con_DPrintf( "Merging changes for global: %s\n", SV_GetString( pTable->classname ));

				// -------------------------------------------------------------------------
				// Pass the "global" flag to the DLL to indicate this entity should only override
				// a matching entity, not be spawned
				if( svgame.dllFuncs.pfnRestore( pent, pSaveData, 1 ) > 0 )
				{
					movedCount++;
				}
				else
				{
					if( SV_IsValidEdict( pNewEnt )) // update the table so decals can find parent entity
						pTable->pent = pNewEnt;
					SetBits( pent->v.flags, FL_KILLME );
				}
			}
			else
			{
				Con_Reportf( "Transferring %s (%d)\n", SV_GetString( pTable->classname ), NUM_FOR_EDICT( pent ));

				if( svgame.dllFuncs.pfnRestore( pent, pSaveData, 0 ) < 0 )
				{
					SetBits( pent->v.flags, FL_KILLME );
				}
				else
				{
					if( !FBitSet( pTable->flags, FENTTABLE_PLAYER ) && EntityInSolid( pent ))
					{
						// this can happen during normal processing - PVS is just a guess,
						// some map areas won't exist in the new map
						Con_Reportf( "Suppressing %s\n", SV_GetString( pTable->classname ));
						SetBits( pent->v.flags, FL_KILLME );
					}
					else
					{
						pTable->flags = FENTTABLE_REMOVED;
						movedCount++;
					}
				}
			}

			// remove any entities that were removed using UTIL_Remove()
			// as a result of the above calls to UTIL_RemoveImmediate()
			SV_FreeOldEntities ();
		}
	}

	return movedCount;
}

/*
=============
LoadAdjacentEnts

loading edicts from adjacency levels
=============
*/
static void LoadAdjacentEnts( const char *pOldLevel, const char *pLandmarkName )
{
	SAVE_HEADER	header;
	SAVERESTOREDATA	currentLevelData, *pSaveData;
	int		i, test, flags, index, movedCount = 0;
	qboolean		foundprevious = false;
	vec3_t		landmarkOrigin;

	memset( &currentLevelData, 0, sizeof( SAVERESTOREDATA ));
	svgame.globals->pSaveData = &currentLevelData;
	sv.loadgame = sv.paused = true;

	// build the adjacent map list
	svgame.dllFuncs.pfnParmsChangeLevel();

	for( i = 0; i < currentLevelData.connectionCount; i++ )
	{
		// make sure the previous level is in the connection list so we can
		// bring over the player.
		if( !Q_stricmp( currentLevelData.levelList[i].mapName, pOldLevel ))
			foundprevious = true;

		for( test = 0; test < i; test++ )
		{
			// only do maps once
			if( !Q_stricmp( currentLevelData.levelList[i].mapName, currentLevelData.levelList[test].mapName ))
				break;
		}

		// map was already in the list
		if( test < i ) continue;

		pSaveData = LoadSaveData( currentLevelData.levelList[i].mapName );

		if( pSaveData )
		{
			ParseSaveTables( pSaveData, &header, false );
			EntityPatchRead( pSaveData, currentLevelData.levelList[i].mapName );

			pSaveData->time = sv.time; // - header.time;
			pSaveData->fUseLandmark = true;
			flags = movedCount = 0;
			index = -1;

			// calculate landmark offset
			LandmarkOrigin( &currentLevelData, landmarkOrigin, pLandmarkName );
			LandmarkOrigin( pSaveData, pSaveData->vecLandmarkOffset, pLandmarkName );
			VectorSubtract( landmarkOrigin, pSaveData->vecLandmarkOffset, pSaveData->vecLandmarkOffset );

			if( !Q_stricmp( currentLevelData.levelList[i].mapName, pOldLevel ))
				SetBits( flags, FENTTABLE_PLAYER );

			while( 1 )
			{
				index = EntryInTable( pSaveData, sv.name, index );
				if( index < 0 ) break;
				SetBits( flags, BIT( index ));
			}

			if( flags ) movedCount = CreateEntityTransitionList( pSaveData, flags );

			// if ents were moved, rewrite entity table to save file
			if( movedCount )
			{
				if( !EntityPatchWrite( pSaveData, currentLevelData.levelList[i].mapName ))
				{
					SaveFinish( pSaveData );

					Host_Error( "Level transition ERROR\nCan't write entity table for %s while transitioning to %s from %s\n",
						currentLevelData.levelList[i].mapName, pOldLevel, sv.name );
				}
			}

			// move the decals from another level
			LoadClientState( pSaveData, currentLevelData.levelList[i].mapName, true, true );

			SaveFinish( pSaveData );
		}
	}

	svgame.globals->pSaveData = NULL;

	if( !foundprevious )
		Host_Error( "Level transition ERROR\nCan't find connection to %s from %s\n", pOldLevel, sv.name );
}

/*
=============
SV_LoadGameState

loading entities from the savegame
=============
*/
int SV_LoadGameState( char const *level )
{
	return LoadGameState( level, false );
}

/*
=============
SV_ClearGameState

clear current game state
=============
*/
void SV_ClearGameState( void )
{
	ClearSaveDir();

	if( svgame.dllFuncs.pfnResetGlobalState != NULL )
		svgame.dllFuncs.pfnResetGlobalState();
}

/*
=============
SV_ChangeLevel
=============
*/
void SV_ChangeLevel( qboolean loadfromsavedgame, const char *mapname, const char *start, qboolean background )
{
	char		level[MAX_QPATH];
	char		oldlevel[MAX_QPATH];
	char		_startspot[MAX_QPATH];
	char		*startspot = NULL;
	SAVERESTOREDATA	*pSaveData = NULL;
#if XASH_GAMECUBE
	qboolean	lean_landmark = false;
#endif

	if( sv.state != ss_active )
	{
		Con_Printf( S_ERROR "server not running\n");
		return;
	}

#if XASH_GAMECUBE
	/* G92: drop New Game PVS pins before the world model is unloaded. */
	GC_ResetNewGameWorldForChangelevel();
#endif

	if( start )
	{
		Q_strncpy( _startspot, start, sizeof( _startspot ));
		startspot = _startspot;
	}

	Q_strncpy( level, mapname, sizeof( level ));
	Q_strncpy( oldlevel, sv.name, sizeof( oldlevel ));

	if( loadfromsavedgame )
	{
#if XASH_GAMECUBE
		/* G97–G100: full SaveGameState OOMs under MEM1 — lean BSS landmark hop. */
		if( startspot && startspot[0] && GC_LeanLandmarkStash( startspot ))
		{
			lean_landmark = true;
			loadfromsavedgame = false;
			svgame.globals->changelevel = true;
		}
		else
#endif
		{
		// smooth transition in-progress
		svgame.globals->changelevel = true;

		// save the current level's state
		pSaveData = SaveGameState( true );

		if( !pSaveData )
		{
			// make user notice the error
			// do not use Host_Error, so the game progress won't be lost
			Sys_Warn( "Can't write save file for performaing change level; check permissions" );
			svgame.globals->changelevel = false;
			return;
		}
		}
	}

	SV_InactivateClients ();
	SV_FinalMessage( "", true );
	SV_DeactivateServer ();

	if( !SV_SpawnServer( level, startspot, background ))
		return;	// ???

	if( loadfromsavedgame )
	{
		// finish saving gamestate
		SaveFinish( pSaveData );

		if( !LoadGameState( level, true ))
			SV_SpawnEntities( level );
		LoadAdjacentEnts( oldlevel, startspot );

		if( sv_newunit.value )
			ClearSaveDir();
		SV_ActivateServer( false );
	}
	else
	{
		// classic quake changelevel
		svgame.dllFuncs.pfnResetGlobalState();
		SV_SpawnEntities( level );
		SV_ActivateServer( true );
	}
}

#if XASH_GAMECUBE
#define GC_G94_SAVE_MAGIC		"G94SAVE1"
#define GC_G100_LAND_MAGIC		"G100LAND"

/* CBasePlayer / CBasePlayerItem layout (powerpc-eabi HLSDK GameCube build,
 * measured 2026-07-18: sizeof(CBasePlayer)=1920, Item=140). */
#define GC_HL_PLAYER_AMMO_OFF		0x4ec	/* 1260 */
#define GC_HL_PLAYER_ITEMS_OFF		0x4c8	/* 1224 m_rgpPlayerItems[6] */
#define GC_HL_PLAYER_ACTIVE_OFF		0x4e0	/* 1248 m_pActiveItem */
#define GC_HL_PLAYER_ANIMEXT_OFF	0x624	/* 1572 m_szAnimExtention[32] */
#define GC_HL_ITEM_PLAYER_OFF		0x80	/* 128 m_pPlayer */
#define GC_HL_ITEM_NEXT_OFF		0x84	/* 132 m_pNext */
#define GC_HL_ITEM_ID_OFF		0x88	/* 136 m_iId */
#define GC_HL_ENTITY_TOUCH_OFF		24	/* CBaseEntity::m_pfnTouch */
#define GC_HL_MAX_AMMO_SLOTS		32
#define GC_HL_MAX_ITEM_TYPES		6
#define GC_HL_ANIMEXT_SIZE		32
#define GC_SF_NORESPAWN			( 1 << 30 )

typedef struct gc_g94_save_s
{
	char	magic[8];
	char	map[32];
	vec3_t	origin;
	vec3_t	angles;
	float	health;
} gc_g94_save_t;

/* G97–G99 inventory blob + G100 weapon-entity re-grant from weapons bits. */
typedef struct gc_g100_landmark_s
{
	char	magic[8];
	char	landmark[32];
	char	from_map[32];
	vec3_t	player_origin;
	vec3_t	player_angles;
	vec3_t	landmark_origin;
	float	health;
	float	armorvalue;
	int	weapons;
	int	ammo[GC_HL_MAX_AMMO_SLOTS];
	qboolean	have_landmark;
	qboolean	have_ammo;
} gc_g100_landmark_t;

static gc_g94_save_t	gc_g94_pending;
static qboolean		gc_g94_pending_valid;
static gc_g100_landmark_t	gc_g100_pending;
static qboolean		gc_g100_pending_valid;
static int		gc_g100_grant_weapons;
static int		gc_g100_grant_ammo[GC_HL_MAX_AMMO_SLOTS];
static qboolean		gc_g100_grant_pending;
static qboolean		gc_g100_grant_have_ammo;

/* Updated from pfnPvAllocEntPrivateData when size looks like CBasePlayer. */
edict_t			*gc_hl_player_priv_edict;
void			*gc_hl_player_priv_ptr;

/* HL weapon bit → classname (matches portable HLSDK LINK_ENTITY names). */
static const char *GC_WeaponClassForBit( int bit )
{
	switch( bit )
	{
	case 1: return "weapon_crowbar";
	case 2: return "weapon_9mmhandgun";
	case 3: return "weapon_357";
	case 4: return "weapon_9mmAR";
	case 6: return "weapon_crossbow";
	case 7: return "weapon_shotgun";
	case 8: return "weapon_rpg";
	case 9: return "weapon_gauss";
	case 10: return "weapon_egon";
	case 11: return "weapon_hornetgun";
	case 12: return "weapon_handgrenade";
	case 13: return "weapon_tripmine";
	case 14: return "weapon_satchel";
	case 15: return "weapon_snark";
	default: return NULL;
	}
}

/*
 * On GameCube, the client edict (often #1) may lack a live CBasePlayer block
 * while GetClassPtr allocated it on a neighboring edict (~1920+ bytes). Prefer
 * the tracked large private-data host, then classname/FL_CLIENT, never a
 * private-data-less client slot.
 */
static edict_t *GC_LeanPlayerPrivateEdict( void )
{
	edict_t	*pl;
	edict_t	*by_class = NULL;
	edict_t	*by_flag = NULL;
	edict_t	*by_vtbl = NULL;
	int	i;

	if( gc_hl_player_priv_edict
		&& gc_hl_player_priv_edict->pvPrivateData == gc_hl_player_priv_ptr
		&& SV_IsValidEdict( gc_hl_player_priv_edict ))
	{
		return gc_hl_player_priv_edict;
	}

	pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
	if( pl && pl->pvPrivateData && *(void **)pl->pvPrivateData )
		return pl;

	if( !svgame.edicts )
		return NULL;

	for( i = 1; i < svgame.numEntities; i++ )
	{
		edict_t		*e = &svgame.edicts[i];
		const char	*cn;

		if( !SV_IsValidEdict( e ) || !e->pvPrivateData )
			continue;
		/* Constructed C++ entity has a non-null vtable word. */
		if( !*(void **)e->pvPrivateData )
			continue;
		cn = SV_GetString( e->v.classname );
		if( cn && cn[0] && !Q_strcmp( cn, "player" ))
		{
			by_class = e;
			break;
		}
		if( !by_flag && FBitSet( e->v.flags, FL_CLIENT ))
			by_flag = e;
		if( !by_vtbl )
			by_vtbl = e;
	}
	if( by_class )
		return by_class;
	if( by_flag )
		return by_flag;
	return by_vtbl;
}

static int *GC_PlayerAmmoSlots( edict_t *priv_ed )
{
	byte	*priv;

	if( !priv_ed || !priv_ed->pvPrivateData )
		return NULL;
	priv = (byte *)priv_ed->pvPrivateData;
	return (int *)( priv + GC_HL_PLAYER_AMMO_OFF );
}

static int GC_WeaponSlotForBit( int bit )
{
	switch( bit )
	{
	case 1: return 0; /* crowbar */
	case 2: return 1; /* glock */
	case 3: return 1; /* 357 */
	case 4: return 2; /* mp5 */
	case 6: return 2; /* crossbow */
	case 7: return 2; /* shotgun */
	case 8: return 3; /* rpg */
	case 9: return 3; /* gauss */
	case 10: return 3; /* egon */
	case 11: return 3; /* hornet */
	case 12: return 4; /* handgrenade */
	case 13: return 4; /* tripmine */
	case 14: return 4; /* satchel */
	case 15: return 4; /* snark */
	default: return -1;
	}
}

/*
 * G103: link a spawned weapon into CBasePlayer::m_rgpPlayerItems like
 * AddPlayerItem, without relying on DefaultTouch (no-ops after changelevel).
 */
static qboolean GC_LeanInventoryAttachWeapon( edict_t *player, edict_t *weapon, int bit )
{
	byte	*ppriv;
	byte	*wpriv;
	void	**items;
	int	slot;
	const void *touch_fn;

	if( !player || !weapon || !player->pvPrivateData || !weapon->pvPrivateData )
		return false;

	slot = GC_WeaponSlotForBit( bit );
	if( slot < 0 || slot >= GC_HL_MAX_ITEM_TYPES )
		return false;

	ppriv = (byte *)player->pvPrivateData;
	wpriv = (byte *)weapon->pvPrivateData;
	touch_fn = *(const void * const *)( wpriv + GC_HL_ENTITY_TOUCH_OFF );

	/* m_pPlayer = CBasePlayer* (private block). */
	*(void **)( wpriv + GC_HL_ITEM_PLAYER_OFF ) = ppriv;
	*(int *)( wpriv + GC_HL_ITEM_ID_OFF ) = bit;

	items = (void **)( ppriv + GC_HL_PLAYER_ITEMS_OFF );
	*(void **)( wpriv + GC_HL_ITEM_NEXT_OFF ) = items[slot];
	items[slot] = wpriv;
	*(void **)( ppriv + GC_HL_PLAYER_ACTIVE_OFF ) = wpriv;

	weapon->v.owner = player;
	weapon->v.aiment = player;
	weapon->v.movetype = MOVETYPE_FOLLOW;
	weapon->v.solid = SOLID_NOT;
	weapon->v.effects |= EF_NODRAW;
	SetBits( player->v.weapons, ( 1 << bit ));

	Con_Reportf( "Xash3D GameCube: G103 inventory-attach classname=%s slot=%d touchfn=%p active=%p\n",
		SV_GetString( weapon->v.classname ), slot, touch_fn, wpriv );
	return true;
}

/*
 * G104: lean DefaultDeploy — set viewmodel/weaponmodel + anim extension without
 * calling HLSDK Deploy (studio/SendWeaponAnim still MEM1-risky after hop).
 * Assemble "models/<leaf>" at runtime from leaf names (keeps this TU's rodata
 * smaller; full paths are allocated into the string pool via SV_AllocString).
 */
static qboolean GC_LeanDeployWeapon( edict_t *player, edict_t *client_ed, int bit )
{
	const char	*viewleaf = NULL;
	const char	*weaponleaf = NULL;
	const char	*animext = NULL;
	const char	*viewmodel;
	const char	*weaponmodel;
	char		viewpath[64];
	char		weaponpath[64];
	byte		*ppriv;
	edict_t		*sync[2];
	int		i, n;

	switch( bit )
	{
	case 1:
		viewleaf = "v_crowbar.mdl";
		weaponleaf = "p_crowbar.mdl";
		animext = "crowbar";
		break;
	case 2:
		viewleaf = "v_9mmhandgun.mdl";
		weaponleaf = "p_9mmhandgun.mdl";
		animext = "onehanded";
		break;
	default:
		return false;
	}

	Q_snprintf( viewpath, sizeof( viewpath ), "models/%s", viewleaf );
	Q_snprintf( weaponpath, sizeof( weaponpath ), "models/%s", weaponleaf );
	viewmodel = viewpath;
	weaponmodel = weaponpath;

	if( !player || !player->pvPrivateData || !viewmodel )
		return false;

	ppriv = (byte *)player->pvPrivateData;
	Q_strncpy( (char *)( ppriv + GC_HL_PLAYER_ANIMEXT_OFF ), animext, GC_HL_ANIMEXT_SIZE );

	n = 0;
	sync[n++] = player;
	if( client_ed && client_ed != player )
		sync[n++] = client_ed;

	for( i = 0; i < n; i++ )
	{
		sync[i]->v.viewmodel = SV_AllocString( viewmodel );
		sync[i]->v.weaponmodel = SV_AllocString( weaponmodel );
	}

	Con_Reportf( "Xash3D GameCube: G104 deploy viewmodel=%s weaponmodel=%s anim=%s bit=%d\n",
		viewmodel, weaponmodel, animext, bit );
#if XASH_GAMECUBE
	/* G105: promote the Deployed first-person mesh so V_SetupViewModel can bind it. */
	if( !Mod_GCEnsureLandmarkViewModel( viewmodel ))
		Con_Reportf( S_WARN "Xash3D GameCube: G105 viewmodel promote failed %s\n", viewmodel );
#endif
	return true;
}

/*
 * G100/G103/G104: recreate owned weapons — spawn, inventory-attach, lean-deploy.
 */
static int GC_LeanGiveWeaponsFromBits( edict_t *touch_player, int weapons )
{
	int	bit;
	int	granted = 0;
	int	deploy_bit = 0;
	edict_t	*client_ed;

	if( !touch_player || !weapons )
		return 0;

	client_ed = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : touch_player;

	ClearBits( touch_player->v.flags, FL_KILLME );
	SetBits( touch_player->v.flags, FL_CLIENT );
	touch_player->v.deadflag = DEAD_NO;
	if( touch_player->v.health <= 0.0f )
		touch_player->v.health = 100.0f;
	if( !touch_player->v.classname )
		touch_player->v.classname = SV_AllocString( "player" );

	if( !touch_player->pvPrivateData )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: G103 give skipped (no player private data)\n" );
		return 0;
	}

	for( bit = 1; bit <= 15; bit++ )
	{
		const char	*classname;
		edict_t		*ent;
		string_t	cls;
		int		spawn_ret;

		if( !( weapons & ( 1 << bit )))
			continue;
		classname = GC_WeaponClassForBit( bit );
		if( !classname )
			continue;

		cls = SV_AllocString( classname );
		gc_lean_weapon_grant_active = true;
		ent = SV_CreateNamedEntity( NULL, cls );
		if( !ent )
		{
			gc_lean_weapon_grant_active = false;
			Con_Reportf( S_WARN "Xash3D GameCube: G103 give failed classname=%s\n", classname );
			continue;
		}

		VectorCopy( touch_player->v.origin, ent->v.origin );
		SetBits( ent->v.spawnflags, GC_SF_NORESPAWN );
		ClearBits( ent->v.flags, FL_KILLME );

		Con_Reportf( "Xash3D GameCube: G103 give spawn begin classname=%s edict=%d\n",
			classname, NUM_FOR_EDICT( ent ));
		spawn_ret = svgame.dllFuncs.pfnSpawn ? svgame.dllFuncs.pfnSpawn( ent ) : -1;
		Con_Reportf( "Xash3D GameCube: G103 give spawn done classname=%s ret=%d\n",
			classname, spawn_ret );

		if( spawn_ret == -1 || !SV_IsValidEdict( ent ) || ent->free || !ent->pvPrivateData )
		{
			if( SV_IsValidEdict( ent ) && !ent->free )
				SV_FreeEdict( ent );
			gc_lean_weapon_grant_active = false;
			Con_Reportf( S_WARN "Xash3D GameCube: G103 give spawn rejected classname=%s\n", classname );
			continue;
		}

		/* Still attempt Touch first; fall back to direct inventory link. */
		ClearBits( ent->v.flags, FL_KILLME );
		if( svgame.dllFuncs.pfnTouch )
			svgame.dllFuncs.pfnTouch( ent, touch_player );

		if( !ent->free && ent->v.owner == touch_player )
		{
			Con_Reportf( "Xash3D GameCube: G103 give touch-attach classname=%s owner=%d weapons=0x%x\n",
				classname, NUM_FOR_EDICT( ent->v.owner ),
				(unsigned)touch_player->v.weapons );
			granted++;
			deploy_bit = bit;
		}
		else if( !ent->free && GC_LeanInventoryAttachWeapon( touch_player, ent, bit ))
		{
			granted++;
			deploy_bit = bit;
		}
		else
		{
			if( SV_IsValidEdict( ent ) && !ent->free )
				SV_FreeEdict( ent );
			Con_Reportf( S_WARN "Xash3D GameCube: G103 give attach failed classname=%s\n", classname );
		}

		gc_lean_weapon_grant_active = false;
	}
	gc_lean_weapon_grant_active = false;
	touch_player->v.weapons |= weapons;

	/* Prefer glock (bit 2) for deploy when present; else last attached. */
	if( weapons & ( 1 << 2 ))
		deploy_bit = 2;
	else if( weapons & ( 1 << 1 ))
		deploy_bit = 1;
	if( deploy_bit && granted > 0 )
		GC_LeanDeployWeapon( touch_player, client_ed, deploy_bit );

	return granted;
}

/* Shared probe helper: plant distinctive inventory into entvars + private data. */
void GC_LeanLandmarkProbePlantAmmo( void )
{
	edict_t	*entvars_ed;
	edict_t	*priv_ed;
	int	*ammo;

	entvars_ed = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
	priv_ed = GC_LeanPlayerPrivateEdict();
	if( entvars_ed )
	{
		entvars_ed->v.health = 77.0f;
		entvars_ed->v.armorvalue = 50.0f;
		entvars_ed->v.weapons = ( 1 << 1 ) | ( 1 << 2 );
	}
	ammo = GC_PlayerAmmoSlots( priv_ed );
	if( ammo )
	{
		ammo[1] = 99;
		ammo[2] = 88;
	}
	Con_Reportf( "Xash3D GameCube: G100 probe inventory set health=77 armor=50 weapons=0x6 ammo1=99 ammo2=88 priv_edict=%d\n",
		priv_ed ? NUM_FOR_EDICT( priv_ed ) : -1 );
}

static qboolean GC_FindInfoLandmarkOrigin( const char *name, vec3_t out )
{
	int	i;

	if( !name || !name[0] || !svgame.edicts )
		return false;

	for( i = 0; i < svgame.numEntities; i++ )
	{
		edict_t		*e = &svgame.edicts[i];
		const char	*cn;
		const char	*tn;

		if( !SV_IsValidEdict( e ))
			continue;
		cn = SV_GetString( e->v.classname );
		if( Q_strcmp( cn, "info_landmark" ))
			continue;
		tn = SV_GetString( e->v.targetname );
		if( Q_strcmp( tn, name ))
			continue;
		VectorCopy( e->v.origin, out );
		return true;
	}
	return false;
}

/*
=============
GC_LeanLandmarkStash / GC_LeanLandmarkRestore

G97–G100: MEM1 cannot host SaveGameState for smooth changelevel. Keep health,
armor, weapons bitmask, m_rgAmmo in BSS; G100 re-grants weapon entities.
=============
*/
static qboolean GC_LeanLandmarkStash( const char *landmark )
{
	edict_t	*pl;
	edict_t	*priv_ed;
	int	*ammo;

	if( !landmark || !landmark[0] || !sv.name[0] )
		return false;
	pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
	if( !pl )
		return false;

	memset( &gc_g100_pending, 0, sizeof( gc_g100_pending ));
	memcpy( gc_g100_pending.magic, GC_G100_LAND_MAGIC, sizeof( gc_g100_pending.magic ));
	Q_strncpy( gc_g100_pending.landmark, landmark, sizeof( gc_g100_pending.landmark ));
	Q_strncpy( gc_g100_pending.from_map, sv.name, sizeof( gc_g100_pending.from_map ));
	VectorCopy( pl->v.origin, gc_g100_pending.player_origin );
	VectorCopy( pl->v.angles, gc_g100_pending.player_angles );
	gc_g100_pending.health = pl->v.health;
	gc_g100_pending.armorvalue = pl->v.armorvalue;
	gc_g100_pending.weapons = pl->v.weapons;
	gc_g100_pending.have_landmark = GC_FindInfoLandmarkOrigin( landmark, gc_g100_pending.landmark_origin );
	priv_ed = GC_LeanPlayerPrivateEdict();
	ammo = GC_PlayerAmmoSlots( priv_ed );
	if( ammo )
	{
		memcpy( gc_g100_pending.ammo, ammo, sizeof( gc_g100_pending.ammo ));
		gc_g100_pending.have_ammo = true;
	}
	gc_g100_pending_valid = true;
	Con_Reportf( "Xash3D GameCube: G100 landmark stash from=%s to_landmark=%s health=%.0f armor=%.0f weapons=0x%x ammo1=%d ammo2=%d have_lm=%d priv_edict=%d\n",
		gc_g100_pending.from_map, gc_g100_pending.landmark, gc_g100_pending.health,
		gc_g100_pending.armorvalue, (unsigned)gc_g100_pending.weapons,
		gc_g100_pending.ammo[1], gc_g100_pending.ammo[2],
		gc_g100_pending.have_landmark ? 1 : 0,
		priv_ed ? NUM_FOR_EDICT( priv_ed ) : -1 );
	return true;
}

void GC_LeanLandmarkRestore( void )
{
	edict_t	*pl;
	edict_t	*priv_ed;
	vec3_t	new_lm;
	vec3_t	offset;
	int	*ammo;

	if( !gc_g100_pending_valid
		|| memcmp( gc_g100_pending.magic, GC_G100_LAND_MAGIC, sizeof( gc_g100_pending.magic )))
		return;

	pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
	if( !pl )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: G100 landmark restore missing player\n" );
		return;
	}

	VectorClear( offset );
	if( gc_g100_pending.have_landmark
		&& GC_FindInfoLandmarkOrigin( gc_g100_pending.landmark, new_lm ))
	{
		VectorSubtract( new_lm, gc_g100_pending.landmark_origin, offset );
		VectorAdd( gc_g100_pending.player_origin, offset, pl->v.origin );
	}
	else
	{
		/* Landmark missing: drop at spawn origin kept from stash as best effort. */
		VectorCopy( gc_g100_pending.player_origin, pl->v.origin );
	}
	VectorCopy( gc_g100_pending.player_angles, pl->v.angles );
	VectorCopy( gc_g100_pending.player_angles, pl->v.v_angle );
	if( gc_g100_pending.health > 0.0f )
		pl->v.health = gc_g100_pending.health;
	if( gc_g100_pending.armorvalue > 0.0f )
		pl->v.armorvalue = gc_g100_pending.armorvalue;
	pl->v.weapons = gc_g100_pending.weapons;

	priv_ed = GC_LeanPlayerPrivateEdict();
	if( !priv_ed )
		priv_ed = pl;

	if( gc_g100_pending.have_ammo )
	{
		ammo = GC_PlayerAmmoSlots( priv_ed );
		if( ammo )
			memcpy( ammo, gc_g100_pending.ammo, sizeof( gc_g100_pending.ammo ));
	}

	/* Queue weapon entity rebuild after world present (SetModel is unsafe here). */
	gc_g100_grant_weapons = gc_g100_pending.weapons;
	gc_g100_grant_have_ammo = gc_g100_pending.have_ammo;
	if( gc_g100_pending.have_ammo )
		memcpy( gc_g100_grant_ammo, gc_g100_pending.ammo, sizeof( gc_g100_grant_ammo ));
	gc_g100_grant_pending = ( gc_g100_grant_weapons != 0 );

	Con_Reportf( "Xash3D GameCube: G100 landmark restore health=%.0f armor=%.0f weapons=0x%x ammo1=%d ammo2=%d origin=(%.0f,%.0f,%.0f) landmark=%s\n",
		pl->v.health, pl->v.armorvalue, (unsigned)pl->v.weapons,
		gc_g100_pending.ammo[1], gc_g100_pending.ammo[2],
		pl->v.origin[0], pl->v.origin[1], pl->v.origin[2],
		gc_g100_pending.landmark );
	gc_g100_pending_valid = false;
	svgame.globals->changelevel = false;
}

/*
=============
GC_LeanLandmarkGrantWeapons

G100: recreate owned weapons after world present (not during ActivateServer).
=============
*/
void GC_LeanLandmarkGrantWeapons( void )
{
	edict_t	*pl;
	edict_t	*priv_ed;
	int	*ammo;
	int	granted;

	if( !gc_g100_grant_pending )
		return;
	gc_g100_grant_pending = false;

	pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
	if( !pl || !gc_g100_grant_weapons )
		return;

	priv_ed = GC_LeanPlayerPrivateEdict();
	if( !priv_ed || !priv_ed->pvPrivateData )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: G103 grant missing player private data (client_edict=%d)\n",
			pl ? NUM_FOR_EDICT( pl ) : -1 );
		return;
	}
	if( priv_ed != pl )
	{
		VectorCopy( pl->v.origin, priv_ed->v.origin );
		Con_Reportf( "Xash3D GameCube: G103 grant using priv_edict=%d (client_edict=%d)\n",
			NUM_FOR_EDICT( priv_ed ), NUM_FOR_EDICT( pl ));
	}

	granted = GC_LeanGiveWeaponsFromBits( priv_ed, gc_g100_grant_weapons );
	if( gc_g100_grant_have_ammo )
	{
		ammo = GC_PlayerAmmoSlots( priv_ed );
		if( ammo )
			memcpy( ammo, gc_g100_grant_ammo, sizeof( gc_g100_grant_ammo ));
	}
	if( priv_ed != pl && priv_ed->v.weapons )
		pl->v.weapons = priv_ed->v.weapons;
	else
		pl->v.weapons = gc_g100_grant_weapons;

	Con_Reportf( "Xash3D GameCube: G104 landmark weapons granted=%d weapons=0x%x ammo1=%d ammo2=%d viewmodel=%s\n",
		granted, (unsigned)pl->v.weapons,
		gc_g100_grant_have_ammo ? gc_g100_grant_ammo[1] : 0,
		gc_g100_grant_have_ammo ? gc_g100_grant_ammo[2] : 0,
		pl->v.viewmodel ? SV_GetString( pl->v.viewmodel ) : "-" );
}

/*
=============
GC_G94LeanSave / GC_G94LeanLoad

G94 New Game probe path: MEM1 cannot host SAVE_HEAPSIZE or a full entity dump.
Keep a tiny same-map blob in BSS (no 160 KiB probe FS bank) so post-load
CapturePVS still has sysheap headroom.
=============
*/
static qboolean GC_G94LeanSave( const char *savename )
{
	edict_t	*pl;

	if( !sv.name[0] )
		return false;
	pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
	if( !pl )
		return false;

	memset( &gc_g94_pending, 0, sizeof( gc_g94_pending ));
	memcpy( gc_g94_pending.magic, GC_G94_SAVE_MAGIC, sizeof( gc_g94_pending.magic ));
	Q_strncpy( gc_g94_pending.map, sv.name, sizeof( gc_g94_pending.map ));
	VectorCopy( pl->v.origin, gc_g94_pending.origin );
	VectorCopy( pl->v.angles, gc_g94_pending.angles );
	gc_g94_pending.health = pl->v.health;
	gc_g94_pending_valid = true;
	Con_Reportf( "Xash3D GameCube: G94 lean save ready name=%s map=%s origin=(%.0f,%.0f,%.0f)\n",
		savename, gc_g94_pending.map,
		gc_g94_pending.origin[0], gc_g94_pending.origin[1], gc_g94_pending.origin[2] );
	return true;
}

static qboolean GC_G94LeanLoad( const char *pPath )
{
	if( !gc_g94_pending_valid
		|| memcmp( gc_g94_pending.magic, GC_G94_SAVE_MAGIC, sizeof( gc_g94_pending.magic )))
		return false;

	Con_Printf( "Loading game from %s...\n", pPath ? pPath : "g94-ram" );
	Con_Reportf( "Xash3D GameCube: G94 lean load map=%s origin=(%.0f,%.0f,%.0f)\n",
		gc_g94_pending.map,
		gc_g94_pending.origin[0], gc_g94_pending.origin[1], gc_g94_pending.origin[2] );

	/* Same-map: keep PVS, restore origin, mark world stale for re-present. */
	if( SV_Active() && !Q_stricmp( gc_g94_pending.map, sv.name ))
	{
		GC_G94ApplyPendingRestore();
		GC_MarkNewGameWorldStale();
		Con_Reportf( "Xash3D GameCube: G94 lean same-map restore queued\n" );
		return true;
	}

	if( !SV_InitGame( false ))
		return false;
	svs.initialized = true;
	Cvar_FullSet( "maxplayers", "1", FCVAR_LATCH );
	Cvar_SetValue( "deathmatch", 0 );
	Cvar_SetValue( "coop", 0 );
	COM_LoadGame( gc_g94_pending.map );
	return true;
}

void GC_G94ApplyPendingRestore( void )
{
	edict_t	*pl;

	if( !gc_g94_pending_valid )
		return;
	if( Q_stricmp( sv.name, gc_g94_pending.map ))
	{
		Con_Reportf( S_WARN "Xash3D GameCube: G94 pending restore map mismatch (%s vs %s)\n",
			sv.name, gc_g94_pending.map );
		gc_g94_pending_valid = false;
		return;
	}
	pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
	if( !pl )
		return;
	VectorCopy( gc_g94_pending.origin, pl->v.origin );
	VectorCopy( gc_g94_pending.angles, pl->v.angles );
	VectorCopy( gc_g94_pending.angles, pl->v.v_angle );
	if( gc_g94_pending.health > 0.0f )
		pl->v.health = gc_g94_pending.health;
	gc_g94_pending_valid = false;
	Con_Reportf( "Xash3D GameCube: G94 lean restore applied origin=(%.0f,%.0f,%.0f)\n",
		pl->v.origin[0], pl->v.origin[1], pl->v.origin[2] );
}
#endif // XASH_GAMECUBE

/*
=============
SV_LoadGame
=============
*/
qboolean SV_LoadGame( const char *pPath )
{
	qboolean		validload = false;
	GAME_HEADER	gameHeader;
	file_t		*pFile;
	uint		flags;

	if( Host_IsDedicated() )
		return false;

	if( UI_CreditsActive( ))
		return false;

	if( COM_StringEmptyOrNULL( pPath ))
		return false;

#if XASH_GAMECUBE
	/* G94 RAM slot — no disc/SD file required. */
	if( Sys_CheckParm( "-gcnewsaveload" ) && GC_G94LeanLoad( pPath ))
		return true;
#endif

	// silently ignore if missed
	if( !FS_FileExists( pPath, true ))
		return false;

	// initialize game if needs
	if( !SV_InitGame( false ))
		return false;

	svs.initialized = true;
	pFile = FS_Open( pPath, "rb", true );

	if( pFile )
	{
		SV_ClearGameState();

		if( SaveReadHeader( pFile, &gameHeader ))
			validload = DirectoryExtract( pFile, gameHeader.mapCount );

		FS_Close( pFile );

		if( validload )
		{
			// now check for map problems
			flags = SV_MapIsValid( gameHeader.mapName, NULL );

			if( FBitSet( flags, MAP_INVALID_VERSION ))
			{
				Con_Printf( S_ERROR "map %s is invalid or not supported\n", gameHeader.mapName );
				validload = false;
			}

			if( !FBitSet( flags, MAP_IS_EXIST ))
			{
				Con_Printf( S_ERROR "map %s doesn't exist\n", gameHeader.mapName );
				validload = false;
			}
		}
	}

	if( !validload )
	{
		Con_Printf( S_ERROR "Couldn't load %s\n", pPath );
		return false;
	}

	Con_Printf( "Loading game from %s...\n", pPath );
	Cvar_FullSet( "maxplayers", "1", FCVAR_LATCH );
	Cvar_SetValue( "deathmatch", 0 );
	Cvar_SetValue( "coop", 0 );
	COM_LoadGame( gameHeader.mapName );

	return true;
}

/*
==================
SV_SaveGame
==================
*/
qboolean SV_SaveGame( const char *pName )
{
	char   comment[80];
	string savename;

	if( COM_StringEmptyOrNULL( pName ))
		return false;

	// can we save at this point?
	if( !IsValidSave( ))
		return false;

	if( !Q_stricmp( pName, "new" ))
	{
		int n;

		// scan for a free filename
		for( n = 0; n < 1000; n++ )
		{
			Q_snprintf( savename, sizeof( savename ), "save%03d", n );

			if( !FS_FileExists( va( DEFAULT_SAVE_DIRECTORY "%s.sav", savename ), true ))
				break;
		}

		if( n == 1000 )
		{
			Con_Printf( S_ERROR "no free slots for savegame\n" );
			return false;
		}
	}
	else Q_strncpy( savename, pName, sizeof( savename ));

#if XASH_GAMECUBE
	/* G94: lean probe blob — full SaveGameSlot needs ~4 MiB zone heap. */
	if( Sys_CheckParm( "-gcnewsaveload" ))
		return GC_G94LeanSave( savename );
#endif

#if !XASH_DEDICATED
	// unload previous image from memory (it's will be overwritten)
	GL_FreeImage( va( DEFAULT_SAVE_DIRECTORY "%s.bmp", savename ) );
#endif // XASH_DEDICATED

	SaveBuildComment( comment, sizeof( comment ));
	return SaveGameSlot( savename, comment );
}

static int SV_CompareFileTime( int ft1, int ft2 )
{
	if( ft1 < ft2 )
	{
		return -1;
	}
	else if( ft1 > ft2 )
	{
		return 1;
	}
	return 0;
}

/*
==================
SV_GetLatestSave

used for reload game after player death
==================
*/
const char *SV_GetLatestSave( void )
{
	static char	savename[MAX_QPATH];
	int		newest = 0;
	int		found = 0;
	search_t		*t;

	if(( t = FS_Search( DEFAULT_SAVE_DIRECTORY "*.sav" , true, true )) == NULL )
		return NULL;

	for( int i = 0; i < t->numfilenames; i++ )
	{
		int	ft = FS_FileTime( t->filenames[i], true );

		// found a match?
		if( ft > 0 )
		{
			// should we use the matched?
			if( !found || SV_CompareFileTime( newest, ft ) < 0 )
			{
				Q_strncpy( savename, t->filenames[i], sizeof( savename ));
				newest = ft;
				found = 1;
			}
		}
	}

	Mem_Free( t ); // release search

	if( found )
		return savename;
	return NULL;
}

/*
==================
SV_GetSaveComment

check savegame for valid
==================
*/
int GAME_EXPORT SV_GetSaveComment( const char *savename, char *comment )
{
	int	i, tag, size, nNumberOfFields, nFieldSize, tokenSize, tokenCount;
	char	*pData, *pSaveData, *pFieldName, **pTokenList;
	string	mapName, description;
	file_t	*f;

	if(( f = FS_Open( savename, "rb", true )) == NULL )
	{
		// just not exist - clear comment
		comment[0] = '\0';
		return 0;
	}

	FS_Read( f, &tag, sizeof( int ));
	if( tag != SAVEGAME_HEADER )
	{
		// invalid header
		Q_strncpy( comment, "<corrupted>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	FS_Read( f, &tag, sizeof( int ));

	if( tag == 0x0065 )
	{
		Q_strncpy( comment, "<old version "XASH_ENGINE_NAME" unsupported>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	if( tag < SAVEGAME_VERSION )
	{
		Q_strncpy( comment, "<old version>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	if( tag > SAVEGAME_VERSION )
	{
		// old xash version ?
		Q_strncpy( comment, "<invalid version>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	mapName[0] = '\0';
	comment[0] = '\0';

	FS_Read( f, &size, sizeof( int ));
	FS_Read( f, &tokenCount, sizeof( int ));	// These two ints are the token list
	FS_Read( f, &tokenSize, sizeof( int ));
	size += tokenSize;

	// sanity check.
	if( tokenCount < 0 || tokenCount > SAVE_HASHSTRINGS )
	{
		Q_strncpy( comment, "<corrupted hashtable>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	if( tokenSize < 0 || tokenSize > SAVE_HEAPSIZE )
	{
		Q_strncpy( comment, "<corrupted hashtable>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	pSaveData = (char *)Mem_Malloc( host.mempool, size );
	FS_Read( f, pSaveData, size );
	pData = pSaveData;

	// allocate a table for the strings, and parse the table
	if( tokenSize > 0 )
	{
		pTokenList = Mem_Calloc( host.mempool, tokenCount * sizeof( char* ));

		// make sure the token strings pointed to by the pToken hashtable.
		for( i = 0; i < tokenCount; i++ )
		{
			pTokenList[i] = *pData ? pData : NULL;	// point to each string in the pToken table
			while( *pData++ );			// find next token (after next null)
		}
	}
	else pTokenList = NULL;

	// short, short (size, index of field name)
	nFieldSize = *(short *)pData;
	pData += sizeof( short );
	pFieldName = pTokenList[*(short *)pData];

	if( Q_stricmp( pFieldName, "GameHeader" ))
	{
		Q_strncpy( comment, "<missing GameHeader>", MAX_STRING );
		if( pTokenList ) Mem_Free( pTokenList );
		if( pSaveData ) Mem_Free( pSaveData );
		FS_Close( f );
		return 0;
	}

	// int (fieldcount)
	pData += sizeof( short );
	nNumberOfFields = (int)*pData;
	pData += nFieldSize;

	// each field is a short (size), short (index of name), binary string of "size" bytes (data)
	for( i = 0; i < nNumberOfFields; i++ )
	{
		size_t size;
		// Data order is:
		// Size
		// szName
		// Actual Data
		nFieldSize = *(short *)pData;
		pData += sizeof( short );

		pFieldName = pTokenList[*(short *)pData];
		pData += sizeof( short );

		size = Q_min( nFieldSize, MAX_STRING );

		if( !Q_stricmp( pFieldName, "comment" ))
		{
			Q_strncpy( description, pData, size );
		}
		else if( !Q_stricmp( pFieldName, "mapName" ))
		{
			Q_strncpy( mapName, pData, size );
		}

		// move to start of next field.
		pData += nFieldSize;
	}

	// delete the string table we allocated
	if( pTokenList ) Mem_Free( pTokenList );
	if( pSaveData ) Mem_Free( pSaveData );
	FS_Close( f );

	// at least mapname should be filled
	if( !COM_StringEmpty( mapName ))
	{
		time_t		fileTime;
		const struct tm	*file_tm;
		string		timestring;
		uint		flags;

		// now check for map problems
		flags = SV_MapIsValid( mapName, NULL );

		if( FBitSet( flags, MAP_INVALID_VERSION ))
		{
			Q_snprintf( comment, MAX_STRING, "<map %s has invalid format>", mapName );
			return 0;
		}

		if( !FBitSet( flags, MAP_IS_EXIST ))
		{
			Q_snprintf( comment, MAX_STRING, "<map %s is missed>", mapName );
			return 0;
		}

		fileTime = FS_FileTime( savename, true );
		file_tm = localtime( &fileTime );

		// split comment to sections
		if( Q_strstr( savename, "quick" ))
			Q_snprintf( comment, CS_SIZE, "[quick]%s", description );
		else if( Q_strstr( savename, "autosave" ))
			Q_snprintf( comment, CS_SIZE, "[autosave]%s", description );
		else Q_strncpy( comment, description, CS_SIZE );
		strftime( timestring, sizeof ( timestring ), "%b%d %Y", file_tm );
		Q_strncpy( comment + CS_SIZE, timestring, CS_TIME );
		strftime( timestring, sizeof( timestring ), "%H:%M", file_tm );
		Q_strncpy( comment + CS_SIZE + CS_TIME, timestring, CS_TIME );
		Q_strncpy( comment + CS_SIZE + (CS_TIME * 2), description + CS_SIZE, CS_SIZE );

		return 1;
	}

	Q_strncpy( comment, "<unknown version>", MAX_STRING );

	return 0;
}

void SV_InitSaveRestore( void )
{
	pfnSaveGameComment = COM_GetProcAddress( svgame.hInstance, "SV_SaveGameComment" );
}
