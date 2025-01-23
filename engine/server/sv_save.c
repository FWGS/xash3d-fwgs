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

#define SAVE_HEAPSIZE		0x400000				// reserve 4Mb for now
#define SAVE_HASHSTRINGS		0xFFF				// 4095 unique strings

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

static void (__cdecl *pfnSaveGameComment)( char *buffer, int max_length ) = NULL;

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
		const char *mapname = STRING( svgame.globals->mapname );

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
				pName = STRING( svgame.edicts->v.message );
			}
			else
			{
				// or use mapname
				pName = STRING( svgame.globals->mapname );
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
	ENTITYTABLE	*pTable;
	int		i;

	pSaveData->pTable = Mem_Calloc( host.mempool, sizeof( ENTITYTABLE ) * entityCount );
	pSaveData->tableCount = entityCount;

	// setup entitytable
	for( i = 0; i < entityCount; i++ )
	{
		pTable = &pSaveData->pTable[i];
		pTable->pent = EDICT_NUM( i );
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
	int	i;

	for( i = index + 1; i < pSaveData->connectionCount; i++ )
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
	int	i;

	for( i = 0; i < pSaveData->connectionCount; i++ )
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
	int	i;

	// just delete all HL? files
	t = FS_Search( DEFAULT_SAVE_DIRECTORY "*.HL?", true, true );
	if( !t ) return; // already empty

	for( i = 0; i < t->numfilenames; i++ )
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

	// delete last quick/autosave (e.g. quick05.sav)
	Q_snprintf( newName, sizeof( newName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count );
	Q_snprintf( newShot, sizeof( newShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count );

	// only delete from game directory, basedir is read-only
	FS_Delete( newName );
	FS_Delete( newShot );

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
		}
		else
		{
			// quick04.sav, etc.
			Q_snprintf( oldName, sizeof( oldName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count - 1 );
			Q_snprintf( oldShot, sizeof( oldShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count - 1 );
		}

		Q_snprintf( newName, sizeof( newName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count );
		Q_snprintf( newShot, sizeof( newShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count );

#if !XASH_DEDICATED
		// unloading the oldshot footprint too
		GL_FreeImage( oldShot );
#endif // XASH_DEDICATED

		// scroll the name list down (e.g. rename quick04.sav to quick05.sav)
		FS_Rename( oldName, newName );
		FS_Rename( oldShot, newShot );
		count--;
	}
}

/*
=============
DirectoryCopy

put the HL1-HL3 files into .sav file
=============
*/
static void DirectoryCopy( const char *pPath, file_t *pFile )
{
	char	szName[MAX_OSPATH];
	int	i, fileSize;
	file_t	*pCopy;
	search_t	*t;

	t = FS_Search( pPath, true, true );
	if( !t ) return; // nothing to copy ?

	for( i = 0; i < t->numfilenames; i++ )
	{
		pCopy = FS_Open( t->filenames[i], "rb", true );
		fileSize = FS_FileLength( pCopy );

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
static void DirectoryExtract( file_t *pFile, int fileCount )
{
	char	szName[MAX_OSPATH];
	char	fileName[MAX_OSPATH];
	int	i, fileSize;
	file_t	*pCopy;

	for( i = 0; i < fileCount; i++ )
	{
		// filename can only be as long as a map name + extension
		FS_Read( pFile, szName, MAX_OSPATH );
		FS_Read( pFile, &fileSize, sizeof( int ));
		Q_snprintf( fileName, sizeof( fileName ), DEFAULT_SAVE_DIRECTORY "%s", szName );
		COM_FixSlashes( fileName );

		pCopy = FS_Open( fileName, "wb", true );
		FS_FileCopy( pCopy, pFile, fileSize );
		FS_Close( pCopy );
	}
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

	pSaveData = Mem_Calloc( host.mempool, sizeof( SAVERESTOREDATA ) + size );
	pSaveData->pTokens = (char **)Mem_Calloc( host.mempool, tokenCount * sizeof( char* ));
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
		Mem_Free( pSaveData->pTokens );
		pSaveData->pTokens = NULL;
		pSaveData->tokenCount = 0;
	}

	if( pSaveData->pTable )
	{
		Mem_Free( pSaveData->pTable );
		pSaveData->pTable = NULL;
		pSaveData->tableCount = 0;
	}

	svgame.globals->pSaveData = NULL;
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
	int	i;

	// Write entity string token table
	if( pSaveData->pTokens )
	{
		for( i = 0; i < pSaveData->tokenCount; i++ )
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
	int	i;

	// Parse the symbol table
	if( pSaveData->tokenSize > 0 )
	{
		FS_Read( pFile, pszTokenList, pSaveData->tokenSize );

		// make sure the token strings pointed to by the pToken hashtable.
		for( i = 0; i < pSaveData->tokenCount; i++ )
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
static void EntityPatchWrite( SAVERESTOREDATA *pSaveData, const char *level )
{
	char	name[MAX_QPATH];
	int	i, size = 0;
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL3", level );

	if(( pFile = FS_Open( name, "wb", true )) == NULL )
		return;

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
	int	i, size, entityId;
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL3", level );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
		return;

	// patch count
	FS_Read( pFile, &size, sizeof( int ));

	for( i = 0; i < size; i++ )
	{
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
		vec3_t	testspot, testend;
		trace_t	tr;

		Con_Printf( S_ERROR "RestoreDecal: couldn't restore entity index %i\n", entry->entityIndex );

		VectorCopy( entry->position, testspot );
		VectorMA( testspot, 5.0f, entry->impactPlaneNormal, testspot );

		VectorCopy( entry->position, testend );
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
static void SaveClientState( SAVERESTOREDATA *pSaveData, const char *level, int changelevel )
{
	soundlist_t	soundInfo[MAX_CHANNELS];
	sv_client_t	*cl = svs.clients;
	char		name[MAX_QPATH];
	int		i, id, version;
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
		return; // something bad is happens

	version = CLIENT_SAVEGAME_VERSION;
	id = SAVEGAME_HEADER;

	FS_Write( pFile, &id, sizeof( id ));
	FS_Write( pFile, &version, sizeof( version ));
	FS_Write( pFile, &pSaveData->size, sizeof( int )); // does not include token table

	// write out the tokens first so we can load them before we load the entities
	FS_Write( pFile, &pSaveData->tokenCount, sizeof( int ));
	FS_Write( pFile, &pSaveData->tokenSize, sizeof( int ));
	FS_Write( pFile, pTokenData, pSaveData->tokenSize );
	FS_Write( pFile, pSaveData->pBaseData, pSaveData->size ); // header and globals
	FS_Close( pFile );
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

		if( COM_CheckStringEmpty( header.introTrack ) )
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
					pent = EDICT_NUM( 0 );
					SV_InitEdict( pent );
					pent = SV_CreateNamedEntity( pent, pTable->classname );
				}
				else if(( pTable->id > 0 ) && ( pTable->id < svs.maxclients + 1 ))
				{
					edict_t	*ed = EDICT_NUM( pTable->id );

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

	pSaveData = SaveInit( SAVE_HEAPSIZE, SAVE_HASHSTRINGS );

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL1", sv.name );
	COM_FixSlashes( name );

	// initialize entity table to count moved entities
	InitEntityTable( pSaveData, svgame.numEntities );

	// Build the adjacent map list
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
		// something bad is happens
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

	EntityPatchWrite( pSaveData, sv.name );

	SaveClientState( pSaveData, sv.name, changelevel );

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
	ENTITYTABLE	*pTable;
	SAVE_HEADER	header;
	edict_t		*pent;
	int		i;

	pSaveData = LoadSaveData( level );
	if( !pSaveData ) return 0; // couldn't load the file

	// must set mapname before calling into DLL
	Q_strncpy( sv.name, level, sizeof( sv.name ));
	svgame.globals->mapname = MAKE_STRING( sv.name );

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
	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		pTable = &pSaveData->pTable[i];
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
	int		id, version;
	char		*pTokenData;
	SAVERESTOREDATA	*pSaveData;
	GAME_HEADER	gameHeader;
	file_t		*pFile;

	pSaveData = SaveGameState( false );
	if( !pSaveData ) return false;

	SaveFinish( pSaveData );
	pSaveData = SaveInit( SAVE_HEAPSIZE, SAVE_HASHSTRINGS ); // re-init the buffer

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
		// something bad is happens
		SaveFinish( pSaveData );
		return false;
	}

	// pending the preview image for savegame
	Cbuf_AddTextf( "saveshot \"%s\"\n", pSaveName );
	Con_Printf( "Saving game to %s...\n", name );

	version = SAVEGAME_VERSION;
	id = SAVEGAME_HEADER;

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
	int		i, movedCount;
	ENTITYTABLE	*pTable;
	edict_t		*pent;

	movedCount = 0;

	// create entity list
	CreateEntitiesInRestoreList( pSaveData, levelMask, false );

	// now spawn entities
	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		pTable = &pSaveData->pTable[i];
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

				Con_DPrintf( "Merging changes for global: %s\n", STRING( pTable->classname ));

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
				Con_Reportf( "Transferring %s (%d)\n", STRING( pTable->classname ), NUM_FOR_EDICT( pent ));

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
						Con_Reportf( "Suppressing %s\n", STRING( pTable->classname ));
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
			if( movedCount ) EntityPatchWrite( pSaveData, currentLevelData.levelList[i].mapName );

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

	if( sv.state != ss_active )
	{
		Con_Printf( S_ERROR "server not running\n");
		return;
	}

	if( start )
	{
		Q_strncpy( _startspot, start, sizeof( _startspot ));
		startspot = _startspot;
	}

	Q_strncpy( level, mapname, sizeof( level ));
	Q_strncpy( oldlevel, sv.name, sizeof( oldlevel ));

	if( loadfromsavedgame )
	{
		// smooth transition in-progress
		svgame.globals->changelevel = true;

		// save the current level's state
		pSaveData = SaveGameState( true );
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

	if( !COM_CheckString( pPath ))
		return false;

	// silently ignore if missed
	if( !FS_FileExists( pPath, true ))
		return false;

	// initialize game if needs
	if( !SV_InitGame( ))
		return false;

	svs.initialized = true;
	pFile = FS_Open( pPath, "rb", true );

	if( pFile )
	{
		SV_ClearGameState();

		if( SaveReadHeader( pFile, &gameHeader ))
		{
			DirectoryExtract( pFile, gameHeader.mapCount );
			validload = true;
		}
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

	if( !COM_CheckString( pName ))
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
	int		newest = 0, ft;
	int		i, found = 0;
	search_t		*t;

	if(( t = FS_Search( DEFAULT_SAVE_DIRECTORY "*.sav" , true, true )) == NULL )
		return NULL;

	for( i = 0; i < t->numfilenames; i++ )
	{
		ft = FS_FileTime( t->filenames[i], true );

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
	if( COM_CheckStringEmpty( mapName ) )
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
