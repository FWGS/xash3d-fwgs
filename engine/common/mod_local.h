/*
mod_local.h - model loader
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef MOD_LOCAL_H
#define MOD_LOCAL_H

#include "common.h"
#include "edict.h"
#include "eiface.h"

// 1/32 epsilon to keep floating point happy
#define DIST_EPSILON		(1.0f / 32.0f)
#define FRAC_EPSILON		(1.0f / 1024.0f)
#define BACKFACE_EPSILON		0.01f
#define MAX_BOX_LEAFS		256
#define ANIM_CYCLE			2
#define MOD_FRAMES			20

// remapping info
#define SUIT_HUE_START		192
#define SUIT_HUE_END		223
#define PLATE_HUE_START		160
#define PLATE_HUE_END		191

#define SHIRT_HUE_START		16
#define SHIRT_HUE_END		32
#define PANTS_HUE_START		96
#define PANTS_HUE_END		112

#define LM_SAMPLE_SIZE		16
#define LM_SAMPLE_EXTRASIZE		8

#define MAX_MAP_WADS		256	// max wads that can be referenced per one map

#define CHECKVISBIT( vis, b )		((b) >= 0 ? (byte)((vis)[(b) >> 3] & (1 << ((b) & 7))) : (byte)false )
#define SETVISBIT( vis, b )( void )	((b) >= 0 ? (byte)((vis)[(b) >> 3] |= (1 << ((b) & 7))) : (byte)false )
#define CLEARVISBIT( vis, b )( void )	((b) >= 0 ? (byte)((vis)[(b) >> 3] &= ~(1 << ((b) & 7))) : (byte)false )

#define REFPVS_RADIUS		2.0f	// radius for rendering
#define FATPVS_RADIUS		8.0f	// FatPVS use radius smaller than the FatPHS
#define FATPHS_RADIUS		16.0f

#define WORLD_INDEX			(1)	// world index is always 1

// model flags (stored in model_t->flags)
#define MODEL_CONVEYOR		BIT( 0 )
#define MODEL_HAS_ORIGIN		BIT( 1 )
#define MODEL_LIQUID		BIT( 2 )	// model has only point hull
#define MODEL_TRANSPARENT		BIT( 3 )	// have transparent surfaces
#define MODEL_COLORED_LIGHTING	BIT( 4 )	// lightmaps stored as RGB

#define MODEL_WORLD			BIT( 29 )	// it's a worldmodel
#define MODEL_CLIENT		BIT( 30 )	// client sprite

// goes into world.flags
#define FWORLD_SKYSPHERE		BIT( 0 )
#define FWORLD_CUSTOM_SKYBOX		BIT( 1 )
#define FWORLD_WATERALPHA		BIT( 2 )
#define FWORLD_HAS_DELUXEMAP		BIT( 3 )

typedef struct consistency_s
{
	const char	*filename;
	int		orig_index;
	int		check_type;
	qboolean		issound;
	int		value;
	vec3_t		mins;
	vec3_t		maxs;
} consistency_t;

#define FCRC_SHOULD_CHECKSUM	BIT( 0 )
#define FCRC_CHECKSUM_DONE	BIT( 1 )

typedef struct
{
	int		flags;
	CRC32_t		initialCRC;
} model_info_t;

// values for model_t's needload
#define NL_UNREFERENCED	0		// this model can be freed after sequence precaching is done
#define NL_NEEDS_LOADED	1
#define NL_PRESENT		2

typedef struct hullnode_s
{
	struct hullnode_s	*next;
	struct hullnode_s	*prev;
} hullnode_t;

typedef struct winding_s
{
	const mplane_t	*plane;
	struct winding_s	*pair;
	hullnode_t	chain;
	int		numpoints;
	vec3_t		p[4];		// variable sized
} winding_t;

typedef struct
{
	hullnode_t	polys;
	uint		num_polys;
} hull_model_t;

typedef struct
{
	msurface_t	*surf;
	int		cull;
} sortedface_t;

typedef struct
{
	qboolean		loading;		// true if worldmodel is loading
	int		flags;		// misc flags

	// mapstats info
	char		message[2048];	// just for debug
	char		compiler[256];	// map compiler
	char		generator[256];	// map editor

	// translucent sorted array
	sortedface_t	*draw_surfaces;	// used for sorting translucent surfaces
	int		max_surfaces;	// max surfaces per submodel (for all models)

	hull_model_t	*hull_models;
	int		num_hull_models;

	// visibility info
	size_t		visbytes;		// cluster size
	size_t		fatbytes;		// fatpvs size

	// world bounds
	vec3_t		mins;		// real accuracy world bounds
	vec3_t		maxs;
	vec3_t		size;
} world_static_t;

extern world_static_t	world;
extern byte		*com_studiocache;
extern model_t		*loadmodel;
extern convar_t		*mod_studiocache;
extern convar_t		*r_wadtextures;
extern convar_t		*r_showhull;

//
// model.c
//
void Mod_Init( void );
void Mod_FreeAll( void );
void Mod_Shutdown( void );
void Mod_ClearUserData( void );
model_t *Mod_LoadWorld( const char *name, qboolean preload );
void *Mod_Calloc( int number, size_t size );
void *Mod_CacheCheck( struct cache_user_s *c );
void Mod_LoadCacheFile( const char *path, struct cache_user_s *cu );
void *Mod_AliasExtradata( model_t *mod );
void *Mod_StudioExtradata( model_t *mod );
model_t *Mod_FindName( const char *name, qboolean trackCRC );
model_t *Mod_LoadModel( model_t *mod, qboolean crash );
model_t *Mod_ForName( const char *name, qboolean crash, qboolean trackCRC );
qboolean Mod_ValidateCRC( const char *name, CRC32_t crc );
void Mod_NeedCRC( const char *name, qboolean needCRC );
void Mod_FreeUnused( void );

//
// mod_bmodel.c
//
void Mod_LoadBrushModel( model_t *mod, const void *buffer, qboolean *loaded );
qboolean Mod_TestBmodelLumps( const char *name, const byte *mod_base, qboolean silent );
qboolean Mod_HeadnodeVisible( mnode_t *node, const byte *visbits, int *lastleaf );
int Mod_FatPVS( const vec3_t org, float radius, byte *visbuffer, int visbytes, qboolean merge, qboolean fullvis );
qboolean Mod_BoxVisible( const vec3_t mins, const vec3_t maxs, const byte *visbits );
int Mod_CheckLump( const char *filename, const int lump, int *lumpsize );
int Mod_ReadLump( const char *filename, const int lump, void **lumpdata, int *lumpsize );
int Mod_SaveLump( const char *filename, const int lump, void *lumpdata, int lumpsize );
mleaf_t *Mod_PointInLeaf( const vec3_t p, mnode_t *node );
void Mod_AmbientLevels( const vec3_t p, byte *pvolumes );
int Mod_SampleSizeForFace( msurface_t *surf );
byte *Mod_GetPVSForPoint( const vec3_t p );
void Mod_UnloadBrushModel( model_t *mod );
void Mod_PrintWorldStats_f( void );

//
// mod_dbghulls.c
//
void Mod_InitDebugHulls( void );
void Mod_CreatePolygonsForHull( int hullnum );
void Mod_ReleaseHullPolygons( void );

//
// mod_studio.c
//
void Mod_InitStudioAPI( void );
void Mod_InitStudioHull( void );
void Mod_ResetStudioAPI( void );
const char *Mod_StudioTexName( const char *modname );
qboolean Mod_GetStudioBounds( const char *name, vec3_t mins, vec3_t maxs );
void Mod_StudioGetAttachment( const edict_t *e, int iAttachment, float *org, float *ang );
void Mod_GetBonePosition( const edict_t *e, int iBone, float *org, float *ang );
hull_t *Mod_HullForStudio( model_t *m, float frame, int seq, vec3_t ang, vec3_t org, vec3_t size, byte *pcnt, byte *pbl, int *hitboxes, edict_t *ed );
void R_StudioSlerpBones( int numbones, vec4_t q1[], float pos1[][3], vec4_t q2[], float pos2[][3], float s );
void R_StudioCalcBoneQuaternion( int frame, float s, void *pbone, void *panim, float *adj, vec4_t q );
void R_StudioCalcBonePosition( int frame, float s, void *pbone, void *panim, vec3_t adj, vec3_t pos );
void *R_StudioGetAnim( void *m_pStudioHeader, void *m_pSubModel, void *pseqdesc );
void Mod_StudioComputeBounds( void *buffer, vec3_t mins, vec3_t maxs, qboolean ignore_sequences );
void Mod_StudioLoadTextures( model_t *mod, void *data );
void Mod_StudioUnloadTextures( void *data );
int Mod_HitgroupForStudioHull( int index );
void Mod_ClearStudioCache( void );

#endif//MOD_LOCAL_H