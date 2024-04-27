/*
physint.h - Server Physics Interface
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef PHYSINT_H
#define PHYSINT_H

#include "eiface.h" // offsetof

#define SV_PHYSICS_INTERFACE_VERSION	6

#define STRUCT_FROM_LINK( l, t, m )	((t *)((byte *)l - offsetof(t, m)))
#define EDICT_FROM_AREA( l )		STRUCT_FROM_LINK( l, edict_t, area )

// values that can be returned with pfnServerState
#define SERVER_DEAD			0
#define SERVER_LOADING		1
#define SERVER_ACTIVE		2

// LUMP reading errors
#define LUMP_LOAD_OK		0
#define LUMP_LOAD_COULDNT_OPEN	1
#define LUMP_LOAD_BAD_HEADER		2
#define LUMP_LOAD_BAD_VERSION		3
#define LUMP_LOAD_NO_EXTRADATA	4
#define LUMP_LOAD_INVALID_NUM		5
#define LUMP_LOAD_NOT_EXIST		6
#define LUMP_LOAD_MEM_FAILED		7
#define LUMP_LOAD_CORRUPTED		8

// LUMP saving errors
#define LUMP_SAVE_OK		0
#define LUMP_SAVE_COULDNT_OPEN	1
#define LUMP_SAVE_BAD_HEADER		2
#define LUMP_SAVE_BAD_VERSION		3
#define LUMP_SAVE_NO_EXTRADATA	4
#define LUMP_SAVE_INVALID_NUM		5
#define LUMP_SAVE_ALREADY_EXIST	6
#define LUMP_SAVE_NO_DATA		7
#define LUMP_SAVE_CORRUPTED		8

#ifndef ALLOC_CHECK
#define ALLOC_CHECK( x )
#endif

typedef struct areanode_s
{
	int		axis;		// -1 = leaf node
	float		dist;
	struct areanode_s	*children[2];
	link_t		trigger_edicts;
	link_t		solid_edicts;
	link_t		portal_edicts;
} areanode_t;

typedef struct server_physics_api_s
{
	// unlink edict from old position and link onto new
	void		( *pfnLinkEdict) ( edict_t *ent, qboolean touch_triggers );
	double		( *pfnGetServerTime )( void ); // unclamped
	double		( *pfnGetFrameTime )( void );	// unclamped
	void*		( *pfnGetModel )( int modelindex );
	areanode_t*	( *pfnGetHeadnode )( void ); // AABB tree for all physic entities
	int		( *pfnServerState )( void );
	void		( *pfnHost_Error )( const char *error, ... );	// cause Host Error
// ONLY ADD NEW FUNCTIONS TO THE END OF THIS STRUCT.  INTERFACE VERSION IS FROZEN AT 6
	struct triangleapi_s *pTriAPI;	// draw coliisions etc. Only for local system

	// draw debug messages (must be called from DrawOrthoTriangles). Only for local system
	int		( *pfnDrawConsoleString )( int x, int y, char *string );
	void		( *pfnDrawSetTextColor )( float r, float g, float b );
	void		( *pfnDrawConsoleStringLen )( const char *string, int *length, int *height );
	void		( *Con_NPrintf )( int pos, const char *fmt, ... );
	void		( *Con_NXPrintf )( struct con_nprint_s *info, const char *fmt, ... );
	const char	*( *pfnGetLightStyle )( int style ); // read custom appreance for selected lightstyle
	void		( *pfnUpdateFogSettings )( unsigned int packed_fog );
	char		**(*pfnGetFilesList)( const char *pattern, int *numFiles, int gamedironly );
	struct msurface_s	*(*pfnTraceSurface)( edict_t *pTextureEntity, const float *v1, const float *v2 );
	const byte	*(*pfnGetTextureData)( unsigned int texnum );

	// static allocations
	void		*(*pfnMemAlloc)( size_t cb, const char *filename, const int fileline ) ALLOC_CHECK( 1 );
	void		(*pfnMemFree)( void *mem, const char *filename, const int fileline );

	// trace & contents
	int		(*pfnMaskPointContents)( const float *pos, int groupmask );
	trace_t		(*pfnTrace)( const float *p0, float *mins, float *maxs, const float *p1, int type, edict_t *e );
	trace_t		(*pfnTraceNoEnts)( const float *p0, float *mins, float *maxs, const float *p1, int type, edict_t *e );
	int		(*pfnBoxInPVS)( const float *org, const float *boxmins, const float *boxmaxs );

	// message handler (missed function to write raw bytes)
	void		(*pfnWriteBytes)( const byte *bytes, int count );

	// BSP lump management
	int		(*pfnCheckLump)( const char *filename, const int lump, int *lumpsize );
	int		(*pfnReadLump)( const char *filename, const int lump, void **lumpdata, int *lumpsize );
	int		(*pfnSaveLump)( const char *filename, const int lump, void *lumpdata, int lumpsize );

	// FS tools
	int		(*pfnSaveFile)( const char *filename, const void *data, int len );
	const byte	*(*pfnLoadImagePixels)( const char *filename, int *width, int *height );

	const char *(*pfnGetModelName)( int modelindex );

	// FWGS extension
	void       *(*pfnGetNativeObject)( const char *object );
} server_physics_api_t;

// physic callbacks
typedef struct physics_interface_s
{
	int		version;
	// passed through pfnCreate (0 is attempt to create, -1 is reject)
	int		( *SV_CreateEntity )( edict_t *pent, const char *szName );
	// run custom physics for each entity (return 0 to use built-in engine physic)
	int		( *SV_PhysicsEntity	)( edict_t *pEntity );
	// spawn entities with internal mod function e.g. for re-arrange spawn order (0 - use engine parser, 1 - use mod parser)
	int		( *SV_LoadEntities )( const char *mapname, char *entities );
	// update conveyor belt for clients
	void		( *SV_UpdatePlayerBaseVelocity )( edict_t *ent );
	// The game .dll should return 1 if save game should be allowed
	int		( *SV_AllowSaveGame )( void );
// ONLY ADD NEW FUNCTIONS TO THE END OF THIS STRUCT.  INTERFACE VERSION IS FROZEN AT 6
	// override trigger area checking and touching
	int		( *SV_TriggerTouch )( edict_t *pent, edict_t *trigger );
	// some engine features can be enabled only through this function
	unsigned int	( *SV_CheckFeatures )( void );
	// used for draw debug collisions for custom physic engine etc
	void		( *DrawDebugTriangles )( void );
	// used for draw debug overlay (textured)
	void		( *DrawNormalTriangles )( void );
	// used for draw debug messages (2d mode)
	void		( *DrawOrthoTriangles )( void );
	// tracing entities with SOLID_CUSTOM mode on a server (not used by pmove code)
	void		( *ClipMoveToEntity)( edict_t *ent, const float *start, float *mins, float *maxs, const float *end, trace_t *trace );
	// tracing entities with SOLID_CUSTOM mode on a server (only used by pmove code)
	void		( *ClipPMoveToEntity)( struct physent_s *pe, const float *start, float *mins, float *maxs, const float *end, struct pmtrace_s *tr );
	// called at end the frame of SV_Physics call
	void		( *SV_EndFrame )( void );
	// obsolete
	void		(*pfnPrepWorldFrame)( void );
	// called through save\restore process
	void		(*pfnCreateEntitiesInRestoreList)( SAVERESTOREDATA *pSaveData, int levelMask, qboolean create_world );
	// allocate custom string (e.g. using user implementation of stringtable, not engine strings)
	string_t		(*pfnAllocString)( const char *szValue );
	// make custom string (e.g. using user implementation of stringtable, not engine strings)
	string_t		(*pfnMakeString)( const char *szValue );
	// read custom string (e.g. using user implementation of stringtable, not engine strings)
	const char*	(*pfnGetString)( string_t iString );
	// helper for restore custom decals that have custom message (e.g. Paranoia)
	int		(*pfnRestoreDecal)( struct decallist_s *entry, edict_t *pEdict, qboolean adjacent );
	// handle custom trigger touching for player
	void		(*PM_PlayerTouch)( struct playermove_s *ppmove, edict_t *client );
	// alloc or destroy model custom data (called only for dedicated servers, otherwise using an client version)
	void		(*Mod_ProcessUserData)( struct model_s *mod, qboolean create, const byte *buffer );
	// select BSP-hull for trace with specified mins\maxs
	void		*(*SV_HullForBsp)( edict_t *ent, const float *mins, const float *maxs, float *offset );
	// handle player custom think function
	int		(*SV_PlayerThink)( edict_t *ent, float frametime, double time );
} physics_interface_t;

#endif//PHYSINT_H
