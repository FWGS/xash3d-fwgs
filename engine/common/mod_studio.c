/*
sv_studio.c - server studio utilities
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
#if XASH_GAMECUBE
#include "gamecube/mem_gamecube.h"
qboolean GC_IsNewGameWorldReady( void );
qboolean Mod_GCIsStudioBssCache( const void *ptr );
void Mod_GCStudioBssTrimLast( void *ptr, size_t kept );
byte *Mod_GCStudioBssGrowLast( void *ptr, size_t old_size, size_t new_size );
#endif
#include "server.h"
#include "studio.h"
#include "r_studioint.h"
#include "library.h"
#include "ref_common.h"
#include "swaplib.h"
#if XASH_GAMECUBE
static void Mod_GCFixStudioUvDomains( studiohdr_t *phdr );
static void Mod_GCForceVmChromakeyMasks( studiohdr_t *phdr );
#endif

typedef int (*STUDIOAPI)( int, sv_blending_interface_t**, server_studio_api_t*,  float (*transform)[3][4], float (*bones)[MAXSTUDIOBONES][3][4] );

typedef struct mstudiocache_s
{
	model_t *model;
	float   frame;
	int     sequence;
	vec3_t  angles;
	vec3_t  origin;
	vec3_t  size;
	byte    controller[4];
	byte    blending[2];
	uint    current_hull;
	uint    current_plane;
	uint    numhitboxes;
} mstudiocache_t;

#define STUDIO_CACHESIZE		16
#define STUDIO_CACHEMASK		(STUDIO_CACHESIZE - 1)

// trace global variables
static sv_blending_interface_t	*pBlendAPI = NULL;
static studiohdr_t			*mod_studiohdr;
static matrix3x4			studio_transform;
static hull_t			cache_hull[MAXSTUDIOBONES];
static hull_t			studio_hull[MAXSTUDIOBONES];
static matrix3x4			studio_bones[MAXSTUDIOBONES];
static uint			studio_hull_hitgroup[MAXSTUDIOBONES];
static uint			cache_hull_hitgroup[MAXSTUDIOBONES];
static mstudiocache_t		cache_studio[STUDIO_CACHESIZE];
static mplane_t			studio_planes[MAXSTUDIOBONES * 6];
static mplane_t			cache_planes[MAXSTUDIOBONES * 6];

// current cache state
static int			cache_current;
static int			cache_current_hull;
static int			cache_current_plane;

le_struct_begin( studiohdr_swap )
	le_struct_field( studiohdr_t, ident )
	le_struct_field( studiohdr_t, version )
	le_struct_field( studiohdr_t, length )
	le_struct_array( studiohdr_t, eyeposition, 3 )
	le_struct_array( studiohdr_t, min, 3 )
	le_struct_array( studiohdr_t, max, 3 )
	le_struct_array( studiohdr_t, bbmin, 3 )
	le_struct_array( studiohdr_t, bbmax, 3 )
	le_struct_field( studiohdr_t, flags )
	le_struct_field( studiohdr_t, numbones )
	le_struct_field( studiohdr_t, boneindex )
	le_struct_field( studiohdr_t, numbonecontrollers )
	le_struct_field( studiohdr_t, bonecontrollerindex )
	le_struct_field( studiohdr_t, numhitboxes )
	le_struct_field( studiohdr_t, hitboxindex )
	le_struct_field( studiohdr_t, numseq )
	le_struct_field( studiohdr_t, seqindex )
	le_struct_field( studiohdr_t, numseqgroups )
	le_struct_field( studiohdr_t, seqgroupindex )
	le_struct_field( studiohdr_t, numtextures )
	le_struct_field( studiohdr_t, textureindex )
	le_struct_field( studiohdr_t, texturedataindex )
	le_struct_field( studiohdr_t, numskinref )
	le_struct_field( studiohdr_t, numskinfamilies )
	le_struct_field( studiohdr_t, skinindex )
	le_struct_field( studiohdr_t, numbodyparts )
	le_struct_field( studiohdr_t, bodypartindex )
	le_struct_field( studiohdr_t, numattachments )
	le_struct_field( studiohdr_t, attachmentindex )
	le_struct_field( studiohdr_t, studiohdr2index )
	le_struct_field( studiohdr_t, numtransitions )
	le_struct_field( studiohdr_t, transitionindex )
le_struct_end();

le_struct_begin( mstudiobone_swap )
	le_struct_field( mstudiobone_t, parent )
	le_struct_field( mstudiobone_t, unused )
	le_struct_array( mstudiobone_t, bonecontroller, 6 )
	le_struct_array( mstudiobone_t, value, 6 )
	le_struct_array( mstudiobone_t, scale, 6 )
le_struct_end();

le_struct_begin( mstudiobonecontroller_swap )
	le_struct_field( mstudiobonecontroller_t, bone )
	le_struct_field( mstudiobonecontroller_t, type )
	le_struct_field( mstudiobonecontroller_t, start )
	le_struct_field( mstudiobonecontroller_t, end )
	le_struct_field( mstudiobonecontroller_t, unused )
	le_struct_field( mstudiobonecontroller_t, index )
le_struct_end();

le_struct_begin( mstudiobbox_swap )
	le_struct_field( mstudiobbox_t, bone )
	le_struct_field( mstudiobbox_t, group )
	le_struct_array( mstudiobbox_t, bbmin, 3 )
	le_struct_array( mstudiobbox_t, bbmax, 3 )
le_struct_end();

le_struct_begin( mstudioseqgroup_swap )
	// should we even swap seqgroup?
	le_struct_field( mstudioseqgroup_t, unused )
	le_struct_field( mstudioseqgroup_t, unused2 )
le_struct_end();

le_struct_begin( mstudioseqdesc_swap )
	le_struct_field( mstudioseqdesc_t, fps )
	le_struct_field( mstudioseqdesc_t, flags )
	le_struct_field( mstudioseqdesc_t, activity )
	le_struct_field( mstudioseqdesc_t, actweight )
	le_struct_field( mstudioseqdesc_t, numevents )
	le_struct_field( mstudioseqdesc_t, eventindex )
	le_struct_field( mstudioseqdesc_t, numframes )
	le_struct_field( mstudioseqdesc_t, weightlistindex )
	le_struct_field( mstudioseqdesc_t, iklockindex )
	le_struct_field( mstudioseqdesc_t, motiontype )
	le_struct_field( mstudioseqdesc_t, motionbone )
	le_struct_array( mstudioseqdesc_t, linearmovement, 3 )
	le_struct_field( mstudioseqdesc_t, autolayerindex )
	le_struct_field( mstudioseqdesc_t, keyvalueindex )
	le_struct_array( mstudioseqdesc_t, bbmin, 3 )
	le_struct_array( mstudioseqdesc_t, bbmax, 3 )
	le_struct_field( mstudioseqdesc_t, numblends )
	le_struct_field( mstudioseqdesc_t, animindex )
	le_struct_array( mstudioseqdesc_t, blendtype, 2 )
	le_struct_array( mstudioseqdesc_t, blendstart, 2 )
	le_struct_array( mstudioseqdesc_t, blendend, 2 )
	le_struct_field( mstudioseqdesc_t, seqgroup )
	le_struct_field( mstudioseqdesc_t, entrynode )
	le_struct_field( mstudioseqdesc_t, exitnode )
	le_struct_field( mstudioseqdesc_t, animdescindex )
le_struct_end();

le_struct_begin( mstudioevent_swap )
	le_struct_field( mstudioevent_t, frame )
	le_struct_field( mstudioevent_t, event )
	le_struct_field( mstudioevent_t, unused )
le_struct_end();

le_struct_begin( mstudioattachment_swap )
	le_struct_field( mstudioattachment_t, flags )
	le_struct_field( mstudioattachment_t, bone )
	le_struct_array( mstudioattachment_t, org, 3 )
le_struct_end();

le_struct_begin( mstudiobodyparts_swap )
	le_struct_field( mstudiobodyparts_t, nummodels )
	le_struct_field( mstudiobodyparts_t, base )
	le_struct_field( mstudiobodyparts_t, modelindex )
le_struct_end();

le_struct_begin( mstudiotexture_swap )
	le_struct_field( mstudiotexture_t, flags )
	le_struct_field( mstudiotexture_t, width )
	le_struct_field( mstudiotexture_t, height )
	le_struct_field( mstudiotexture_t, index )
le_struct_end();

le_struct_begin( mstudiomodel_swap )
	le_struct_field( mstudiomodel_t, unused )
	le_struct_field( mstudiomodel_t, unused2 )
	le_struct_field( mstudiomodel_t, nummesh )
	le_struct_field( mstudiomodel_t, meshindex )
	le_struct_field( mstudiomodel_t, numverts )
	le_struct_field( mstudiomodel_t, vertinfoindex )
	le_struct_field( mstudiomodel_t, vertindex )
	le_struct_field( mstudiomodel_t, numnorms )
	le_struct_field( mstudiomodel_t, norminfoindex )
	le_struct_field( mstudiomodel_t, normindex )
	le_struct_field( mstudiomodel_t, blendvertinfoindex )
	le_struct_field( mstudiomodel_t, blendnorminfoindex )
le_struct_end();

le_struct_begin( mstudiomesh_swap )
	le_struct_field( mstudiomesh_t, numtris )
	le_struct_field( mstudiomesh_t, triindex )
	le_struct_field( mstudiomesh_t, skinref )
	le_struct_field( mstudiomesh_t, numnorms )
	le_struct_field( mstudiomesh_t, unused )
le_struct_end();

le_struct_begin( mstudioanim_swap )
	le_struct_array( mstudioanim_t, offset, 6 )
le_struct_end();

/*
====================
Mod_InitStudioHull
====================
*/
void Mod_InitStudioHull( void )
{
	if( studio_hull[0].planes != NULL )
		return;	// already initailized

	for( int i = 0; i < MAXSTUDIOBONES; i++ )
	{
		studio_hull[i].clipnodes16 = (mclipnode16_t *)box_clipnodes16;
		studio_hull[i].planes = &studio_planes[i*6];
		studio_hull[i].firstclipnode = 0;
		studio_hull[i].lastclipnode = 5;
	}
}

/*
===============================================================================

	STUDIO MODELS CACHE

===============================================================================
*/
/*
===============
Mod_StudioExtradata

===============
*/
void *GAME_EXPORT Mod_StudioExtradata( model_t *mod )
{
	if( mod && mod->type == mod_studio )
		return mod->cache.data;
	return NULL;
}

/*
====================
ClearStudioCache
====================
*/
void Mod_ClearStudioCache( void )
{
	memset( cache_studio, 0, sizeof( cache_studio ));
	cache_current_hull = cache_current_plane = 0;

	cache_current = 0;
}

/*
====================
AddToStudioCache
====================
*/
static void Mod_AddToStudioCache( float frame, int sequence, vec3_t angles, vec3_t origin, vec3_t size, byte *pcontroller, byte *pblending, model_t *model, hull_t *hull, int numhitboxes )
{
	if( numhitboxes + cache_current_hull >= MAXSTUDIOBONES )
		Mod_ClearStudioCache();

	cache_current++;
	mstudiocache_t *pCache = &cache_studio[cache_current & STUDIO_CACHEMASK];

	pCache->frame = frame;
	pCache->sequence = sequence;
	VectorCopy( angles, pCache->angles );
	VectorCopy( origin, pCache->origin );
	VectorCopy( size, pCache->size );

	memcpy( pCache->controller, pcontroller, 4 );
	memcpy( pCache->blending, pblending, 2 );

	pCache->model = model;
	pCache->current_hull = cache_current_hull;
	pCache->current_plane = cache_current_plane;

	memcpy( &cache_hull[cache_current_hull], hull, numhitboxes * sizeof( hull_t ));
	memcpy( &cache_planes[cache_current_plane], studio_planes, numhitboxes * sizeof( mplane_t ) * 6 );
	memcpy( &cache_hull_hitgroup[cache_current_hull], studio_hull_hitgroup, numhitboxes * sizeof( uint ));

	cache_current_hull += numhitboxes;
	cache_current_plane += numhitboxes * 6;
	pCache->numhitboxes = numhitboxes;
}

/*
====================
CheckStudioCache
====================
*/
static mstudiocache_t *Mod_CheckStudioCache( model_t *model, float frame, int sequence, vec3_t angles, vec3_t origin, vec3_t size, byte *controller, byte *blending )
{
	for( int i = 0; i < STUDIO_CACHESIZE; i++ )
	{
		mstudiocache_t *pCached = &cache_studio[(cache_current - i) & STUDIO_CACHEMASK];

		if( pCached->model != model )
			continue;

		if( pCached->frame != frame )
			continue;

		if( pCached->sequence != sequence )
			continue;

		if( !VectorCompare( pCached->angles, angles ))
			continue;

		if( !VectorCompare( pCached->origin, origin ))
			continue;

		if( !VectorCompare( pCached->size, size ))
			continue;

		if( memcmp( pCached->controller, controller, 4 ) != 0 )
			continue;

		if( memcmp( pCached->blending, blending, 2 ) != 0 )
			continue;

		return pCached;
	}

	return NULL;
}

/*
===============================================================================

	STUDIO MODELS TRACING

===============================================================================
*/
/*
====================
SetStudioHullPlane
====================
*/
static void Mod_SetStudioHullPlane( int planenum, int bone, int axis, float offset, const vec3_t size )
{
	mplane_t	*pl = &studio_planes[planenum];

	pl->type = 5;

	pl->normal[0] = studio_bones[bone][0][axis];
	pl->normal[1] = studio_bones[bone][1][axis];
	pl->normal[2] = studio_bones[bone][2][axis];

	pl->dist = (pl->normal[0] * studio_bones[bone][0][3]) + (pl->normal[1] * studio_bones[bone][1][3]) + (pl->normal[2] * studio_bones[bone][2][3]) + offset;

	if( planenum & 1 ) pl->dist -= DotProductFabs( pl->normal, size );
	else pl->dist += DotProductFabs( pl->normal, size );

}

/*
====================
HullForStudio

NOTE: pEdict may be NULL
====================
*/
hull_t *Mod_HullForStudio( model_t *model, float frame, int sequence, vec3_t angles, vec3_t origin, vec3_t size, byte *pcontroller, byte *pblending, int *numhitboxes, edict_t *pEdict )
{
	qboolean	bSkipShield = false;

	*numhitboxes = 0; // assume error

	if( mod_studiocache.value )
	{
		mstudiocache_t *bonecache = Mod_CheckStudioCache( model, frame, sequence, angles, origin, size, pcontroller, pblending );

		if( bonecache != NULL )
		{
			memcpy( studio_planes, &cache_planes[bonecache->current_plane], bonecache->numhitboxes * sizeof( mplane_t ) * 6 );
			memcpy( studio_hull_hitgroup, &cache_hull_hitgroup[bonecache->current_hull], bonecache->numhitboxes * sizeof( uint ));
			memcpy( studio_hull, &cache_hull[bonecache->current_hull], bonecache->numhitboxes * sizeof( hull_t ));

			*numhitboxes = bonecache->numhitboxes;
			return studio_hull;
		}
	}

	mod_studiohdr = Mod_StudioExtradata( model );
	if( !mod_studiohdr ) return NULL; // probably not a studiomodel

	vec3_t angles2 = Vec3( angles );

	if( !FBitSet( host.features, ENGINE_COMPENSATE_QUAKE_BUG ))
		angles2[PITCH] = -angles2[PITCH]; // stupid quake bug

	pBlendAPI->SV_StudioSetupBones( model, frame, sequence, angles2, origin, pcontroller, pblending, -1, pEdict );
	mstudiobbox_t *phitbox = (mstudiobbox_t *)((byte *)mod_studiohdr + mod_studiohdr->hitboxindex);

	if( SV_IsValidEdict( pEdict ) && pEdict->v.gamestate == 1 )
		bSkipShield = 1;

	for( int i = 0, j = 0; i < mod_studiohdr->numhitboxes; i++, j += 6 )
	{
		if( world.version == QBSP2_VERSION )
			studio_hull[i].clipnodes32 = (mclipnode32_t *)box_clipnodes32;
		else
			studio_hull[i].clipnodes16 = (mclipnode16_t *)box_clipnodes16;

		if( bSkipShield && i == 21 )
			continue;	// CS stuff

		studio_hull_hitgroup[i] = phitbox[i].group;

		Mod_SetStudioHullPlane( j + 0, phitbox[i].bone, 0, phitbox[i].bbmax[0], size );
		Mod_SetStudioHullPlane( j + 1, phitbox[i].bone, 0, phitbox[i].bbmin[0], size );
		Mod_SetStudioHullPlane( j + 2, phitbox[i].bone, 1, phitbox[i].bbmax[1], size );
		Mod_SetStudioHullPlane( j + 3, phitbox[i].bone, 1, phitbox[i].bbmin[1], size );
		Mod_SetStudioHullPlane( j + 4, phitbox[i].bone, 2, phitbox[i].bbmax[2], size );
		Mod_SetStudioHullPlane( j + 5, phitbox[i].bone, 2, phitbox[i].bbmin[2], size );
	}

	// tell trace code about hitbox count
	*numhitboxes = (bSkipShield) ? (mod_studiohdr->numhitboxes - 1) : (mod_studiohdr->numhitboxes);

	if( mod_studiocache.value )
		Mod_AddToStudioCache( frame, sequence, angles, origin, size, pcontroller, pblending, model, studio_hull, *numhitboxes );

	return studio_hull;
}

/*
===============================================================================

	STUDIO MODELS SETUP BONES

===============================================================================
*/
/*
====================
StudioCalcBoneAdj

====================
*/
static void Mod_StudioCalcBoneAdj( float *adj, const byte *pcontroller )
{
	int			i, j;
	float			value;
	mstudiobonecontroller_t	*pbonecontroller;

	pbonecontroller = (mstudiobonecontroller_t *)((byte *)mod_studiohdr + mod_studiohdr->bonecontrollerindex);

	for( j = 0; j < mod_studiohdr->numbonecontrollers; j++ )
	{
		i = pbonecontroller[j].index;

		if( i == STUDIO_MOUTH )
			continue; // ignore mouth

		if( i >= MAXSTUDIOCONTROLLERS )
			continue;

		// check for 360% wrapping
		if( pbonecontroller[j].type & STUDIO_RLOOP )
		{
			value = pcontroller[i] * (360.0f / 256.0f) + pbonecontroller[j].start;
		}
		else
		{
			value = pcontroller[i] / 255.0f;
			value = bound( 0.0f, value, 1.0f );
			value = (1.0f - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
		}

		switch( pbonecontroller[j].type & STUDIO_TYPES )
		{
		case STUDIO_XR:
		case STUDIO_YR:
		case STUDIO_ZR:
			adj[j] = value * (M_PI_F / 180.0f);
			break;
		case STUDIO_X:
		case STUDIO_Y:
		case STUDIO_Z:
			adj[j] = value;
			break;
		}
	}
}

/*
====================
StudioCalcRotations

====================
*/
static void Mod_StudioCalcRotations( int boneused[], int numbones, const byte *pcontroller, float pos[][3], vec4_t *q, mstudioseqdesc_t *pseqdesc, mstudioanim_t *panim, float f )
{
	int		i, j, frame;
	mstudiobone_t	*pbone;
	float		adj[MAXSTUDIOCONTROLLERS];
	float		s;

	// bah, fix this bug with changing sequences too fast
	if( f > pseqdesc->numframes - 1 )
	{
		f = 0.0f;
	}
	else if( f < -0.01f )
	{
		// BUG ( somewhere else ) but this code should validate this data.
		// This could cause a crash if the frame # is negative, so we'll go ahead
		// and clamp it here
		f = -0.01f;
	}

	frame = (int)f;
	s = (f - frame);

	// add in programtic controllers
	pbone = (mstudiobone_t *)((byte *)mod_studiohdr + mod_studiohdr->boneindex);

	memset( adj, 0, sizeof( adj ));
	Mod_StudioCalcBoneAdj( adj, pcontroller );

	for( j = numbones - 1; j >= 0; j-- )
	{
		i = boneused[j];
		R_StudioCalcBones( frame, s, &pbone[i], &panim[i], adj, pos[i], q[i] );
	}

	if( pseqdesc->motiontype & STUDIO_X ) pos[pseqdesc->motionbone][0] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Y ) pos[pseqdesc->motionbone][1] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Z ) pos[pseqdesc->motionbone][2] = 0.0f;
}


void Mod_SwapStudioSeqGroupAnims( studiohdr_t *phdr, mstudioseqdesc_t *pseq, byte *buf )
{
	mstudioanim_t *panim = (mstudioanim_t *)( buf + pseq->animindex );
	int numanims = pseq->numblends * phdr->numbones;

	for( int j = 0; j < numanims; j++, panim++ )
	{
		le_struct_swap( mstudioanim_swap, panim );

		for( int k = 0; k < 6; k++ )
		{
			if( panim->offset[k] == 0 )
				continue;

			mstudioanimvalue_t *panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[k] );

			int frames = pseq->numframes;
			while( frames > 0 )
			{
				int valid = panimvalue->num.valid;
				int total = panimvalue->num.total;

				for( int l = 1; l <= valid; l++ )
					panimvalue[l].value = LittleShort( panimvalue[l].value );

				panimvalue += valid + 1;
				frames -= total;
			}
		}
	}
}

/*
====================
StudioGetAnim

====================
*/
void *R_StudioGetAnim( studiohdr_t *m_pStudioHeader, model_t *m_pSubModel, mstudioseqdesc_t *pseqdesc )
{
	if( pseqdesc->seqgroup == 0 )
		return ((byte *)m_pStudioHeader + pseqdesc->animindex);

	cache_user_t *paSequences = (cache_user_t *)m_pSubModel->submodels;
#if XASH_GAMECUBE
	/* Menu New Game: the group table and files live in the studio arena,
	 * next to the resident mesh (com_studiocache is emptied at play start). */
	const qboolean arena = GC_MenuRunsServer();

	if( paSequences == NULL && arena )
	{
		paSequences = (cache_user_t *)Mod_GCStudioBssAlloc( MAXSTUDIOGROUPS * sizeof( cache_user_t ));
		if( paSequences )
			memset( paSequences, 0, MAXSTUDIOGROUPS * sizeof( cache_user_t ));
		m_pSubModel->submodels = (void *)paSequences;
	}
#endif
	if( paSequences == NULL )
	{
		paSequences = (cache_user_t *)Mem_Calloc( com_studiocache, MAXSTUDIOGROUPS * sizeof( cache_user_t ));
		m_pSubModel->submodels = (void *)paSequences;
	}

	// check for already loaded
	if( !Mod_CacheCheck(( cache_user_t *)&( paSequences[pseqdesc->seqgroup] )))
	{
		string modelname;
		COM_FileBase( m_pSubModel->name, modelname, sizeof( modelname ));

		string modelpath;
		COM_ExtractFilePath( m_pSubModel->name, modelpath );

		// NOTE: here we build real sub-animation filename because stupid user may rename model without recompile
		string filepath;
		Q_snprintf( filepath, sizeof( filepath ), "%s/%s%i%i.mdl", modelpath, modelname, pseqdesc->seqgroup / 10, pseqdesc->seqgroup % 10 );

		fs_offset_t filesize;
		byte *buf;
#if XASH_GAMECUBE
		if( arena )
		{
			buf = Mod_GCSeqGroupLoad( filepath, &paSequences[pseqdesc->seqgroup], &filesize );
			if( buf && LittleLong( IDSEQGRPHEADER ) == *(uint *)buf )
			{
				Mod_SwapStudioSeqGroupAnims( m_pStudioHeader, pseqdesc, buf );
				return buf + pseqdesc->animindex;
			}
		}
#endif
		buf = FS_LoadFile( filepath, &filesize, false );
#if XASH_GAMECUBE
		if(( !buf || !filesize ) && arena )
		{
			/* Out of room: play sequence 0's animation rather than ending
			 * the game, when its blend layout covers this sequence. */
			mstudioseqdesc_t *seq0 = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex);
			static int warned;

			if( seq0->seqgroup == 0 && pseqdesc->numblends <= seq0->numblends )
			{
				if( warned++ < 4 )
					Con_Reportf( S_WARN "Xash3D GameCube: no room for %s, using sequence 0\n", filepath );
				return (byte *)m_pStudioHeader + seq0->animindex;
			}
		}
#endif
		if( !buf || !filesize )
			Host_Error( "%s: can't load %s\n", __func__, filepath );
		if( LittleLong( IDSEQGRPHEADER ) != *(uint *)buf )
			Host_Error( "%s: %s is corrupted\n", __func__, filepath );

		Con_Printf( "loading: %s\n", filepath );

		paSequences[pseqdesc->seqgroup].data = Mem_Calloc( com_studiocache, filesize );
		memcpy( paSequences[pseqdesc->seqgroup].data, buf, filesize );
		Mem_Free( buf );

		Mod_SwapStudioSeqGroupAnims( m_pStudioHeader, pseqdesc,
			(byte *)paSequences[pseqdesc->seqgroup].data );
	}

	return ((byte *)paSequences[pseqdesc->seqgroup].data + pseqdesc->animindex);
}

/*
====================
StudioSetupBones

NOTE: pEdict is unused
====================
*/
static void SV_StudioSetupBones( model_t *pModel,	float frame, int sequence, const vec3_t angles, const vec3_t origin,
	const byte *pcontroller, const byte *pblending, int iBone, const edict_t *pEdict )
{
	int		i, j, numbones = 0;
	int		boneused[MAXSTUDIOBONES];
	float		f = 0.0;

	mstudiobone_t	*pbones;
	mstudioseqdesc_t	*pseqdesc;
	mstudioanim_t	*panim;

	static float	pos[MAXSTUDIOBONES][3];
	static vec4_t	q[MAXSTUDIOBONES];
	matrix3x4		bonematrix;

	static float	pos2[MAXSTUDIOBONES][3];
	static vec4_t	q2[MAXSTUDIOBONES];
	static float	pos3[MAXSTUDIOBONES][3];
	static vec4_t	q3[MAXSTUDIOBONES];
	static float	pos4[MAXSTUDIOBONES][3];
	static vec4_t	q4[MAXSTUDIOBONES];

	if( sequence < 0 || sequence >= mod_studiohdr->numseq )
	{
		// only show warn if sequence that out of range was specified intentionally
		if( sequence > mod_studiohdr->numseq )
			Con_Reportf( S_WARN "%s: sequence %i/%i out of range for model %s\n", __func__, sequence, mod_studiohdr->numseq, pModel->name );
		sequence = 0;
	}

	pseqdesc = (mstudioseqdesc_t *)((byte *)mod_studiohdr + mod_studiohdr->seqindex) + sequence;
	pbones = (mstudiobone_t *)((byte *)mod_studiohdr + mod_studiohdr->boneindex);
	panim = R_StudioGetAnim( mod_studiohdr, pModel, pseqdesc );

	if( iBone < -1 || iBone >= mod_studiohdr->numbones )
		iBone = 0;

	if( iBone == -1 )
	{
		numbones = mod_studiohdr->numbones;
		for( i = 0; i < mod_studiohdr->numbones; i++ )
			boneused[(numbones - i) - 1] = i;
	}
	else
	{
		// only the parent bones
		for( i = iBone; i != -1; i = pbones[i].parent )
			boneused[numbones++] = i;
	}

	if( pseqdesc->numframes > 1 )
		f = ( frame * ( pseqdesc->numframes - 1 )) / 256.0f;

	Mod_StudioCalcRotations( boneused, numbones, pcontroller, pos, q, pseqdesc, panim, f );

	if( pseqdesc->numblends > 1 )
	{
		float	s;

		panim += mod_studiohdr->numbones;
		Mod_StudioCalcRotations( boneused, numbones, pcontroller, pos2, q2, pseqdesc, panim, f );

		s = (float)pblending[0] / 255.0f;

		R_StudioSlerpBones( mod_studiohdr->numbones, q, pos, q2, pos2, s );

		if( pseqdesc->numblends == 4 )
		{
			panim += mod_studiohdr->numbones;
			Mod_StudioCalcRotations( boneused, numbones, pcontroller, pos3, q3, pseqdesc, panim, f );

			panim += mod_studiohdr->numbones;
			Mod_StudioCalcRotations( boneused, numbones, pcontroller, pos4, q4, pseqdesc, panim, f );

			s = (float)pblending[0] / 255.0f;
			R_StudioSlerpBones( mod_studiohdr->numbones, q3, pos3, q4, pos4, s );

			s = (float)pblending[1] / 255.0f;
			R_StudioSlerpBones( mod_studiohdr->numbones, q, pos, q3, pos3, s );
		}
	}

	Matrix3x4_CreateFromEntity( studio_transform, angles, origin, 1.0f );

	for( j = numbones - 1; j >= 0; j-- )
	{
		i = boneused[j];

		Matrix3x4_FromOriginQuat( bonematrix, q[i], pos[i] );
		if( pbones[i].parent == -1 )
			Matrix3x4_ConcatTransforms( studio_bones[i], studio_transform, bonematrix );
		else Matrix3x4_ConcatTransforms( studio_bones[i], studio_bones[pbones[i].parent], bonematrix );
	}
}

/*
====================
StudioGetAttachment
====================
*/
void Mod_StudioGetAttachment( const edict_t *e, int iAtt, float *origin, float *angles )
{
	mstudioattachment_t		*pAtt;
	matrix3x4			localPose;
	matrix3x4			worldPose;
	model_t			*mod;

	mod = SV_ModelHandle( e->v.modelindex );
	mod_studiohdr = (studiohdr_t *)Mod_StudioExtradata( mod );
	if( !mod_studiohdr ) return;

	if( mod_studiohdr->numattachments <= 0 )
	{
		if( origin ) VectorCopy( e->v.origin, origin );

		if( FBitSet( host.features, ENGINE_COMPUTE_STUDIO_LERP ) && angles )
			VectorCopy( e->v.angles, angles );
		return;
	}

	iAtt = bound( 0, iAtt, mod_studiohdr->numattachments - 1 );

	// calculate attachment origin and angles
	pAtt = (mstudioattachment_t *)((byte *)mod_studiohdr + mod_studiohdr->attachmentindex) + iAtt;

	vec3_t angles2 = Vec3( e->v.angles );

	if( !FBitSet( host.features, ENGINE_COMPENSATE_QUAKE_BUG ))
		angles2[PITCH] = -angles2[PITCH];

	pBlendAPI->SV_StudioSetupBones( mod, e->v.frame, e->v.sequence, angles2, e->v.origin, e->v.controller, e->v.blending, pAtt->bone, e );

	Matrix3x4_LoadIdentity( localPose );
	Matrix3x4_SetOrigin( localPose, pAtt->org[0], pAtt->org[1], pAtt->org[2] );
	Matrix3x4_ConcatTransforms( worldPose, studio_bones[pAtt->bone], localPose );

	if( origin != NULL ) // origin is used always
		Matrix3x4_OriginFromMatrix( worldPose, origin );

	if( FBitSet( host.features, ENGINE_COMPUTE_STUDIO_LERP ) && angles != NULL )
		Matrix3x4_AnglesFromMatrix( worldPose, angles );
}

/*
====================
GetBonePosition
====================
*/
void Mod_GetBonePosition( const edict_t *e, int iBone, float *origin, float *angles )
{
	model_t	*mod;

	mod = SV_ModelHandle( e->v.modelindex );
	mod_studiohdr = (studiohdr_t *)Mod_StudioExtradata( mod );
	if( !mod_studiohdr ) return;

	pBlendAPI->SV_StudioSetupBones( mod, e->v.frame, e->v.sequence, e->v.angles, e->v.origin, e->v.controller, e->v.blending, iBone, e );

	if( origin ) Matrix3x4_OriginFromMatrix( studio_bones[iBone], origin );
	if( angles ) Matrix3x4_AnglesFromMatrix( studio_bones[iBone], angles );
}

/*
====================
HitgroupForStudioHull
====================
*/
int Mod_HitgroupForStudioHull( int index )
{
	return studio_hull_hitgroup[index];
}

/*
====================
StudioBoundVertex
====================
*/
static void Mod_StudioBoundVertex( vec3_t mins, vec3_t maxs, int *numverts, const vec3_t vertex )
{
	if((*numverts) == 0 )
		ClearBounds( mins, maxs );

	AddPointToBounds( vertex, mins, maxs );
	(*numverts)++;
}

/*
====================
StudioAccumulateBoneVerts
====================
*/
static void Mod_StudioAccumulateBoneVerts( vec3_t mins, vec3_t maxs, int *numverts, vec3_t bone_mins, vec3_t bone_maxs, int *numbones )
{
	vec3_t	delta;
	vec3_t	point;

	if( *numbones <= 0 )
		return;

	// calculate the midpoint of the second vertex,
	VectorSubtract( bone_maxs, bone_mins, delta );

	VectorScale( delta, 0.5f, point );
	Mod_StudioBoundVertex( mins, maxs, numverts, point );

	VectorClear( bone_mins );
	VectorClear( bone_maxs );
	*numbones = 0;
}

/*
====================
StudioComputeBounds
====================
*/
static void Mod_StudioComputeBounds( void *buffer, vec3_t mins, vec3_t maxs, qboolean ignore_sequences )
{
	int		i, j, k, numseq;
	studiohdr_t	*pstudiohdr;
	mstudiobodyparts_t	*pbodypart;
	mstudiomodel_t	*m_pSubModel;
	mstudioseqdesc_t	*pseqdesc;
	mstudiobone_t	*pbones;
	mstudioanim_t	*panim;
	vec3_t		bone_mins, bone_maxs;
	vec3_t		vert_mins, vert_maxs;
	int		vert_count, bone_count;
	vec3_t		pos, *pverts;

	vert_count = bone_count = 0;
	VectorClear( bone_mins );
	VectorClear( bone_maxs );
	VectorClear( vert_mins );
	VectorClear( vert_maxs );

	// Get the body part portion of the model
	pstudiohdr = (studiohdr_t *)buffer;
	pbodypart = (mstudiobodyparts_t *)((byte *)pstudiohdr + pstudiohdr->bodypartindex);

	// each body part has nummodels variations so there are as many total variations as there
	// are in a matrix of each part by each other part
	for( i = 0; i < pstudiohdr->numbodyparts; i++ )
	{
		m_pSubModel = (mstudiomodel_t *)((byte *)pstudiohdr + pbodypart[i].modelindex);

		for( j = 0; j < pbodypart[i].nummodels; j++ )
		{
			pverts = (vec3_t *)((byte *)pstudiohdr + m_pSubModel[j].vertindex);

			for( k = 0; k < m_pSubModel[j].numverts; k++ )
				Mod_StudioBoundVertex( bone_mins, bone_maxs, &vert_count, pverts[k] );
		}
	}

	pbones = (mstudiobone_t *)((byte *)pstudiohdr + pstudiohdr->boneindex);
	numseq = (ignore_sequences) ? 1 : pstudiohdr->numseq;

	for( i = 0; i < numseq; i++ )
	{
		pseqdesc = (mstudioseqdesc_t *)((byte *)pstudiohdr + pstudiohdr->seqindex) + i;

		if( pseqdesc->seqgroup == 0 )
			panim = (mstudioanim_t *)((byte *)pstudiohdr + pseqdesc->animindex);
		else continue;

		for( j = 0; j < pstudiohdr->numbones; j++ )
		{
			for( k = 0; k < pseqdesc->numframes; k++ )
			{
				R_StudioCalcBones( k, 0, &pbones[j], panim, NULL, pos, NULL );
				Mod_StudioBoundVertex( vert_mins, vert_maxs, &bone_count, pos );
			}
		}

		Mod_StudioAccumulateBoneVerts( bone_mins, bone_maxs, &vert_count, vert_mins, vert_maxs, &bone_count );
	}

	VectorCopy( bone_mins, mins );
	VectorCopy( bone_maxs, maxs );
}

/*
====================
Mod_GetStudioBounds
====================
*/
qboolean Mod_GetStudioBounds( const char *name, vec3_t mins, vec3_t maxs )
{
	int	result = false;
	byte	*f;

	if( !Q_strstr( name, "models" ) || !Q_strstr( name, ".mdl" ))
		return false;

	f = FS_LoadFile( name, NULL, false );
	if( !f ) return false;

	if( *(uint *)f == IDSTUDIOHEADER )
	{
		VectorClear( mins );
		VectorClear( maxs );
		Mod_StudioComputeBounds( f, mins, maxs, false );
		result = true;
	}
	Mem_Free( f );

	return result;
}

/*
===============
Mod_StudioTexName

extract texture filename from modelname
===============
*/
const char *Mod_StudioTexName( const char *modname )
{
	static char	texname[MAX_QPATH+1];

	Q_strncpy( texname, modname, sizeof( texname ));
	COM_StripExtension( texname );
	Q_strncat( texname, "T.mdl", sizeof( texname ));

	return texname;
}

#if XASH_GAMECUBE
/*
=============
Mod_GCLoadStudioTexFile

GoldSrc ships skins as models/foxt.mdl (lowercase t). Mod_StudioTexName uses
T.mdl — try both so lean mesh-only promote can upload Flipper skins (G376).

G381: tip CapFaces leaves heap unable to malloc ~127 KiB scientistt.mdl.
Fall back to a dedicated BSS scratch so Flipper skins still upload.
=============
*/
/* 96 KiB: DoD *t companions stay under ~64 KiB. Reclaim tip so FULL BSP can
 * malloc-rescue planes/verts out of the map arena for nodes+leafs. */
/* 96 KiB tip-safe floor. Garand/tommy/mp40 @128² fit (~88 KiB); 98k falls
 * back to 64² companion at pack time. 128 KiB scratch starved tip and
 * tip-crashed after VM attach on heavy clusters (ava-play99-48..51). */
#define GC_STUDIO_TEX_SCRATCH_BYTES (96 * 1024)
static byte     gc_studio_tex_scratch[GC_STUDIO_TEX_SCRATCH_BYTES] __attribute__((aligned( 32 )));
static qboolean gc_studio_tex_scratch_busy;

/* A file too big for it (gmanT.mdl, 144 KiB) borrows the texture hot window. */
static void *gc_studio_tex_borrowed;

static qboolean Mod_GCIsStudioTexScratch( const void *ptr )
{
	return ptr == (void *)gc_studio_tex_scratch || ( ptr && ptr == gc_studio_tex_borrowed );
}

byte *Mod_GCSoundScratch( fs_offset_t size )
{
	return !gc_studio_tex_scratch_busy && size <= (fs_offset_t)GC_STUDIO_TEX_SCRATCH_BYTES ? gc_studio_tex_scratch : NULL;
}

qboolean Mod_GCIsSoundScratch( const void *ptr )
{
	return ptr == (void *)gc_studio_tex_scratch;
}

static void Mod_GCReleaseStudioTexBuf( void *ptr )
{
	if( !ptr )
		return;
	if( Mod_GCIsStudioTexScratch( ptr ))
	{
		gc_studio_tex_scratch_busy = false;
		gc_studio_tex_borrowed = NULL;
		return;
	}
	Mem_Free( ptr );
}

static void *Mod_GCLoadFileToStudioTexScratch( const char *path, fs_offset_t *size_out )
{
	extern void *R_GCHotBorrow( unsigned bytes );
	file_t *f;
	fs_offset_t sz;
	byte *dst = gc_studio_tex_scratch;

	if( !path || !path[0] || gc_studio_tex_scratch_busy )
		return NULL;
	f = FS_Open( path, "rb", false );
	if( !f )
		return NULL;
	sz = FS_FileLength( f );
	if( sz > (fs_offset_t)GC_STUDIO_TEX_SCRATCH_BYTES )
		dst = gc_studio_tex_borrowed = R_GCHotBorrow( (unsigned)sz );
	if( sz < (fs_offset_t)sizeof( studiohdr_t ) || !dst )
	{
		FS_Close( f );
		return NULL;
	}
	if( FS_Read( f, dst, (size_t)sz ) != sz )
	{
		FS_Close( f );
		return NULL;
	}
	FS_Close( f );
	gc_studio_tex_scratch_busy = true;
	if( size_out )
		*size_out = sz;
	Con_Reportf( "Xash3D GameCube: G381 studio T.mdl scratch '%s' (%s)\n",
		path, Q_memprint( (size_t)sz ));
	return dst;
}

static void *Mod_GCLoadStudioTexFile( const char *modname, fs_offset_t *size_out )
{
	void *buf;
	string	tex_upper;
	string	tex_lower;
	char bare[64];
	char mirror[MAX_QPATH];

	COM_FileBase( modname, bare, sizeof( bare ));
	/* Prefer bootstrap companion (full skins) before disc models/ — tip heap
	 * often cannot malloc 127 KiB, so the BSS scratch comes first. */
	Q_snprintf( mirror, sizeof( mirror ), "gc_studio/%st.mdl", bare );
	buf = Mod_GCLoadFileToStudioTexScratch( mirror, size_out );
	if( buf )
		return buf;
	buf = FS_LoadFile( mirror, size_out, false );
	if( buf )
		return buf;

	Q_strncpy( tex_upper, Mod_StudioTexName( modname ), sizeof( tex_upper ));
	buf = FS_LoadFile( tex_upper, size_out, false );
	if( buf )
		return buf;
	buf = Mod_GCLoadFileToStudioTexScratch( tex_upper, size_out );
	if( buf )
		return buf;

	Q_strncpy( tex_lower, modname, sizeof( tex_lower ));
	COM_StripExtension( tex_lower );
	Q_strncat( tex_lower, "t.mdl", sizeof( tex_lower ));
	buf = FS_LoadFile( tex_lower, size_out, false );
	if( buf )
		return buf;
	buf = Mod_GCLoadFileToStudioTexScratch( tex_lower, size_out );
	if( buf )
		return buf;

	if( size_out )
		*size_out = 0;
	return NULL;
}
#endif

/*
================
Mod_StudioBodyVariations

calc studio body variations
================
*/
static int Mod_StudioBodyVariations( model_t *mod )
{
	studiohdr_t	*pstudiohdr;
	mstudiobodyparts_t	*pbodypart;
	int		i, count = 1;

	pstudiohdr = (studiohdr_t *)Mod_StudioExtradata( mod );
	if( !pstudiohdr ) return 0;

	pbodypart = (mstudiobodyparts_t *)((byte *)pstudiohdr + pstudiohdr->bodypartindex);

	// each body part has nummodels variations so there are as many total variations as there
	// are in a matrix of each part by each other part
	for( i = 0; i < pstudiohdr->numbodyparts; i++ )
		count = count * pbodypart[i].nummodels;

	return count;
}

static qboolean Mod_SwapStudioModel( const char *name, void *buffer, size_t buffersize )
{
	studiohdr_t *phdr = buffer;

	if( buffersize < sizeof( studiohdr_t ))
		return false;

	le_struct_swap( studiohdr_swap, phdr );

	if( phdr->ident != IDSTUDIOHEADER || phdr->version != STUDIO_VERSION )
		return false;

#if XASH_BIG_ENDIAN
	byte *mod_base = buffer;

	if( phdr->studiohdr2index > 0 && phdr->studiohdr2index < phdr->length )
	{
		Con_Printf( S_ERROR "byteswapping extended studio model \"%s\" is unsupoprted\n", name );
		return false;
	}

	for( int i = 0; i < phdr->numbones; i++ )
		le_struct_swap( mstudiobone_swap, (mstudiobone_t *)( mod_base + phdr->boneindex ) + i );

	for( int i = 0; i < phdr->numbonecontrollers; i++ )
		le_struct_swap( mstudiobonecontroller_swap, (mstudiobonecontroller_t *)( mod_base + phdr->bonecontrollerindex ) + i );

	for( int i = 0; i < phdr->numhitboxes; i++ )
		le_struct_swap( mstudiobbox_swap, (mstudiobbox_t *)( mod_base + phdr->hitboxindex ) + i );

	for( int i = 0; i < phdr->numseqgroups; i++ )
		le_struct_swap( mstudioseqgroup_swap, (mstudioseqgroup_t *)( mod_base + phdr->seqgroupindex ) + i );

	for( int i = 0; i < phdr->numattachments; i++ )
	{
		mstudioattachment_t *pattach = (mstudioattachment_t *)( mod_base + phdr->attachmentindex ) + i;
		le_struct_swap( mstudioattachment_swap, pattach );
		le_array_swap((float *)pattach->vectors, 3 * 3 );
	}

	for( int i = 0; i < phdr->numtextures; i++ )
		le_struct_swap( mstudiotexture_swap, (mstudiotexture_t *)( mod_base + phdr->textureindex ) + i );

	for( int i = 0; i < phdr->numbodyparts; i++ )
		le_struct_swap( mstudiobodyparts_swap, (mstudiobodyparts_t *)( mod_base + phdr->bodypartindex ) + i );

	for( int i = 0; i < phdr->numseq; i++ )
	{
		mstudioseqdesc_t *pseq = (mstudioseqdesc_t *)( mod_base + phdr->seqindex ) + i;
		le_struct_swap( mstudioseqdesc_swap, pseq );

		for( int j = 0; j < pseq->numevents; j++ )
			le_struct_swap( mstudioevent_swap, (mstudioevent_t *)( mod_base + pseq->eventindex ) + j );

		if( pseq->seqgroup != 0 )
			continue;

		Mod_SwapStudioSeqGroupAnims( phdr, pseq, mod_base );
	}

	for( int i = 0; i < phdr->numbodyparts; i++ )
	{
		mstudiobodyparts_t *pbodypart = (mstudiobodyparts_t *)( mod_base + phdr->bodypartindex ) + i;

		for( int j = 0; j < pbodypart->nummodels; j++ )
		{
			mstudiomodel_t *pmodel = (mstudiomodel_t *)( mod_base + pbodypart->modelindex ) + j;
			le_struct_swap( mstudiomodel_swap, pmodel );

			le_array_swap((float *)( mod_base + pmodel->vertindex ), pmodel->numverts * 3 );
			le_array_swap((float *)( mod_base + pmodel->normindex ), pmodel->numnorms * 3 );

			for( int k = 0; k < pmodel->nummesh; k++ )
			{
				mstudiomesh_t *pmesh = (mstudiomesh_t *)( mod_base + pmodel->meshindex ) + k;
				le_struct_swap( mstudiomesh_swap, pmesh );

				short *ptricmds = (short *)( mod_base + pmesh->triindex );
				short n;
				while(( n = LittleShort( *ptricmds )))
				{
					*ptricmds++ = n;
					int count = abs( n ) * 4;
					for( int l = 0; l < count; l++, ptricmds++ )
						*ptricmds = LittleShort( *ptricmds );
				}
				*ptricmds = 0;
			}
		}
	}

	short *pskinref = (short *)( mod_base + phdr->skinindex );
	for( int i = 0; i < phdr->numskinfamilies * phdr->numskinref; i++ )
		pskinref[i] = LittleShort( pskinref[i] );
#endif

	return true;
}

/*
=================
R_StudioLoadHeader
=================
*/
static studiohdr_t *R_StudioLoadHeader( model_t *mod, void *buffer, size_t buffersize )
{
	studiohdr_t *phdr;

	if( !buffer )
		return NULL;

	if( !Mod_SwapStudioModel( mod->name, buffer, buffersize ))
	{
		phdr = (studiohdr_t *)buffer;
		Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", mod->name, phdr->version, STUDIO_VERSION );
		return NULL;
	}

	return (studiohdr_t *)buffer;
}

static studiohdr_t *Mod_MaybeTruncateStudioTextureData( model_t *mod )
{
#if XASH_LOW_MEMORY
	studiohdr_t *phdr = (studiohdr_t *)mod->cache.data;

	if( !phdr )
		return NULL;

	/* Mesh-only New Game studios already truncated into a malloc block. */
	if( !mod->mempool )
		return phdr;

	if( phdr->texturedataindex <= sizeof( studiohdr_t ) || phdr->texturedataindex > phdr->length )
		return phdr;

	mod->cache.data = Mem_Realloc( mod->mempool, mod->cache.data, phdr->texturedataindex );
	phdr = (studiohdr_t *)mod->cache.data; // get the new pointer on studiohdr
	phdr->length = phdr->texturedataindex; // update model size

	return phdr;
#else
	// NOTE: some mods potentially might require full studio model data
	return (studiohdr_t *)mod->cache.data;
#endif
}

#if XASH_GAMECUBE
/*
 * G61: shared GameCube quality profile policy for engine-side systems.
 *
 * 0 = smoke: diagnostics, minimal HUD/model/texture work, fastest boot proof.
 * 1 = release: default real-hardware profile, conservative visuals/audio/HUD.
 * 2 = high: telemetry-only profile for experiments after memory/frame proof.
 */
int GC_GetVisualQuality( void )
{
	int quality = 1;

	if( Cvar_FindVar( "gc_quality" ))
		quality = Cvar_VariableInteger( "gc_quality" );

	quality = bound( 0, quality, 2 );
#if XASH_LOW_MEMORY
	if( quality > 1 )
		quality = 1;
#endif
	return quality;
}

const char *GC_GetQualityProfileName( void )
{
	switch( GC_GetVisualQuality() )
	{
	case 0: return "smoke";
	case 2: return "high";
	default: return "release";
	}
}

void GC_ReportQualityProfile( const char *stage )
{
	const int quality = GC_GetVisualQuality();
	const char *profile = GC_GetQualityProfileName();
	const char *hud = quality == 0 ? "stub" : "real";
	const char *audio = quality == 0 ? "minimal" : "asnd";
	const char *lightmaps = quality == 0 ? "reduced" : "enabled";
	const char *overlay = quality == 0 ? "essential" : "available";
	const char *purpose = quality == 0 ? "diagnostics" : ( quality == 2 ? "telemetry-only" : "real-hardware-default" );

	Con_Reportf( "Xash3D GameCube: quality profile stage=%s gc_quality=%d name=%s low_memory=%d hud=%s audio=%s lightmaps=%s overlay=%s purpose=%s\n",
		stage ? stage : "unknown", quality, profile, XASH_LOW_MEMORY ? 1 : 0,
		hud, audio, lightmaps, overlay, purpose );
}

#endif /* XASH_GAMECUBE */

/*
=================
Mod_StudioTexSizes

Texture and skinref block sizes of a companion T.mdl.
Returns false when the blocks do not fit into the file.
=================
*/
MAYBE_UNUSED static qboolean Mod_StudioTexSizes( studiohdr_t *thdr, size_t texsize, size_t *size1, size_t *size2 )
{
	if( thdr->numtextures < 0 || thdr->numskinfamilies < 0 || thdr->numskinref < 0 || thdr->textureindex < 0 )
		return false;

	// wider than size_t: the multiply wraps around on 32-bit
	const uint64_t texbytes = (uint64_t)thdr->numtextures * sizeof( mstudiotexture_t );
	const uint64_t skinbytes = (uint64_t)thdr->numskinfamilies * (uint64_t)thdr->numskinref * sizeof( short );

	// the copy in Mod_LoadStudioModel starts at textureindex and reads both blocks
	if((uint64_t)thdr->textureindex + texbytes + skinbytes > texsize )
		return false;

	*size1 = texbytes;
	*size2 = skinbytes;

	return true;
}

/*
=================
Mod_LoadStudioModel
=================
*/
void Mod_LoadStudioModel( model_t *mod, void *buffer, size_t buffersize, qboolean *loaded )
{
#if XASH_GAMECUBE
	extern int GC_GetVisualQuality( void );
#endif
	char poolname[MAX_VA_STRING];
	qboolean textures_loaded = false;
	qboolean skip_studio_textures = false;
	qboolean gc_real_studio = false;
	qboolean gc_mesh_only = false;

#if XASH_GAMECUBE
	skip_studio_textures = (GC_GetVisualQuality( ) == 0);
	/* Allowlisted MDLs: lean mesh (+ optional *t skins via G376) on the
	 * studio BSS path. Covers -gcmap smoke maps too — without this, c1a1f
	 * tried Mem_AllocPool studio loads under memopt and left null pstudiohdr. */
	if( buffer && buffersize > sizeof( studiohdr_t )
		&& ( Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcmap" )
			|| GC_MapLoadMemoryOpt() || GC_IsNewGameWorldReady()
			|| GC_MenuRunsServer()))
	{
		/* Keep skins when quality > 0; never demote the session just to load.
		 * Pre-entity promote forces mesh-only so G376 uploads do not tip spawn.
		 * Boss / Xen leads keep Flipper skins even at gc_quality=0 — mesh-only
		 * Nihilanth is a fidelity compromise the full-campaign goal rejects. */
		{
			char bare[64];
			qboolean boss_skins = false;

			COM_FileBase( mod->name, bare, sizeof( bare ));
			/* Only the chapter boss during spawn — uploading controller/islave
			 * atlases mid-entity-lump hung c4a3 (probe 20261104-025929). */
			if( !Q_stricmp( bare, "nihilanth" ) || !Q_stricmp( bare, "big_mom" ))
				boss_skins = true;

			skip_studio_textures = Mod_GCStudioPreEntityMeshOnly()
				|| (( GC_GetVisualQuality() == 0 ) && !boss_skins );
		}
		gc_real_studio = true;
		gc_mesh_only = true;
	}
#endif

	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );

	if( loaded ) *loaded = false;
#if XASH_GAMECUBE
	if( gc_mesh_only )
		/* Lean New Game studios: keep cache on malloc, not MEM1 pools. */
		mod->mempool = 0;
	else
#endif
		mod->mempool = Mem_AllocPool( poolname );
	mod->type = mod_studio;

#if XASH_GAMECUBE
	if( skip_studio_textures )
	{
		/* Stub loading removed - real studio loading required */
	}
#endif

	studiohdr_t *phdr = R_StudioLoadHeader( mod, buffer, buffersize );
	if( !phdr || phdr->length < (int32_t)sizeof( studiohdr_t ) || (size_t)phdr->length > buffersize ) // garbage value in length
		return;	// bad model

#if !XASH_DEDICATED
	if( !gc_mesh_only && !skip_studio_textures && !Host_IsDedicated( ) && phdr->numtextures == 0 )
	{
		fs_offset_t size2_len;
		size_t size1 = 0, size2 = 0;

		void *buffer2;
#if XASH_GAMECUBE
		buffer2 = Mod_GCLoadStudioTexFile( mod->name, &size2_len );
#else
		buffer2 = FS_LoadFile( Mod_StudioTexName( mod->name ), &size2_len, false );
#endif
		studiohdr_t *thdr = R_StudioLoadHeader( mod, buffer2, size2_len );

		// NOTE: keep phdr->length + size1 + size2 from wrapping
		if( thdr != NULL && Mod_StudioTexSizes( thdr, size2_len, &size1, &size2 ) &&
			size1 + size2 <= SIZE_MAX - (size_t)phdr->length )
		{
			// TODO: Mod_StudioLoadTextures will crash if passed a merged studio model!
			Mod_GCForceVmChromakeyMasks( thdr );
			ref.dllFuncs.Mod_StudioLoadTextures( mod, thdr );
			textures_loaded = true;

			// give space for textures and skinrefs
			mod->cache.data = Mem_Calloc( mod->mempool, phdr->length + size1 + size2 );
			memcpy( mod->cache.data, buffer, phdr->length ); // copy main mdl buffer
			phdr = (studiohdr_t *)mod->cache.data; // get the new pointer on studiohdr
			phdr->numskinfamilies = thdr->numskinfamilies;
			phdr->numtextures = thdr->numtextures;
			phdr->numskinref = thdr->numskinref;
			phdr->textureindex = phdr->length;
			phdr->skinindex = phdr->textureindex + size1;

			byte *in = (byte *)thdr + thdr->textureindex;
			byte *out = (byte *)phdr + phdr->textureindex;
			memcpy( out, in, size1 + size2 );	// copy textures + skinrefs
			phdr->length += size1 + size2;
		}
		else Con_Printf( S_WARN "%s: %s missing textures file\n", __func__, mod->name );

		if( buffer2 )
#if XASH_GAMECUBE
			Mod_GCReleaseStudioTexBuf( buffer2 ); // release T.mdl / scratch
#else
			Mem_Free( buffer2 ); // release T.mdl
#endif
	}
#endif

	if( !textures_loaded )
	{
		size_t cache_length = phdr->length;

		// NOTE: don't modify source buffer because it's used for CRC computing
#if XASH_GAMECUBE
		if( gc_mesh_only )
		{
			/* Upload soft skins from the file buffer first, then malloc only the
			 * mesh+texhdr slice — avoids file+full-cache peak (crowbar ~46KB
			 * failed calloc while the promote buffer was still live).
			 * G376: GoldSrc NPCs keep skins in *t.mdl (numtextures=0 on base).
			 * Merge tex headers + upload Flipper skins like the retail path. */
			size_t mesh = cache_length;
			studiohdr_t *src = (studiohdr_t *)buffer;
			void *tex_buf = NULL;
			fs_offset_t tex_len = 0;
			studiohdr_t *thdr = NULL;

			if( src->length > 0 && (size_t)src->length < mesh )
				mesh = (size_t)src->length;
			if( mesh > buffersize )
				mesh = buffersize;

#if !XASH_DEDICATED
			if( !skip_studio_textures && !Host_IsDedicated( )
				&& src->numtextures > 0 && src->textureindex > 0
				&& src->texturedataindex > (int)sizeof( studiohdr_t )
				&& (size_t)src->texturedataindex <= buffersize )
			{
				Mod_GCForceVmChromakeyMasks( src );
				ref.dllFuncs.Mod_StudioLoadTextures( mod, src );
				mesh = (size_t)src->texturedataindex;
			}
			else if( !skip_studio_textures && !Host_IsDedicated( )
				&& src->numtextures == 0 )
			{
				tex_buf = Mod_GCLoadStudioTexFile( mod->name, &tex_len );
				thdr = R_StudioLoadHeader( mod, tex_buf, tex_len );
				if( thdr && thdr->numtextures > 0 && thdr->textureindex > 0 )
				{
					size_t size1 = thdr->numtextures * sizeof( mstudiotexture_t );
					size_t size2 = thdr->numskinfamilies * thdr->numskinref * sizeof( short );
					size_t merged = mesh + size1 + size2;

					/* Retail order: upload skins from T.mdl (pixel data live),
					 * then keep only mesh+texhdr in resident cache.
					 * G381: tip heap is empty — grow the BSS mesh slice in place
					 * when calloc(merged) fails (was mesh-only white NPC). */
					/* The source mesh is already in the static arena on the
					 * GameCube route.  Extend that slice before considering calloc:
					 * calling the fragmented runtime heap here was the post-present
					 * `_malloc_r` DSI path on scientist/Barney/headcrab promotion.
					 * Grow before uploading skins: on the menu route the skin
					 * pixels are bump-allocated from the same arena. */
					if( Mod_GCIsStudioBssCache( buffer ))
					{
						byte *slot = Mod_GCStudioBssGrowLast( buffer, mesh, merged );

						if( slot )
							mod->cache.data = slot;
					}
					Image_GCPurgeDecodeScratch();
					Mod_GCForceVmChromakeyMasks( thdr );
					ref.dllFuncs.Mod_StudioLoadTextures( mod, thdr );
					textures_loaded = true;
					if( !mod->cache.data )
					{
						Image_GCPurgeDecodeScratch();
						mod->cache.data = calloc( 1, merged );
						if( !mod->cache.data )
						{
							Image_GCPurgeDecodeScratch();
							mod->cache.data = calloc( 1, merged );
						}
					}
					if( mod->cache.data )
					{
						byte *in, *out;
						mstudiotexture_t *dst_tex;
						int ti;

						if( mod->cache.data != buffer )
							memcpy( mod->cache.data, buffer, mesh );
						phdr = (studiohdr_t *)mod->cache.data;
						phdr->length = (int)mesh;
						phdr->numskinfamilies = thdr->numskinfamilies;
						phdr->numtextures = thdr->numtextures;
						phdr->numskinref = thdr->numskinref;
						phdr->textureindex = (int)mesh;
						phdr->skinindex = phdr->textureindex + (int)size1;
						in = (byte *)thdr + thdr->textureindex;
						out = (byte *)phdr + phdr->textureindex;
						memcpy( out, in, size1 + size2 );
						phdr->length = (int)merged;
						/* -gcmap skips Mod_StudioLoadTextures during CapFaces
						 * bake — zero .index so deferred attach does not treat
						 * companion file offsets as live GX texnums. */
						if( Sys_CheckParm( "-gcmap" ) && !Sys_CheckParm( "-gcnewgame" ))
						{
							dst_tex = (mstudiotexture_t *)out;
							for( ti = 0; ti < phdr->numtextures; ti++ )
								dst_tex[ti].index = 0;
						}
						Con_Reportf( "Xash3D GameCube: G376 studio T.mdl skins '%s' n=%d kept=%s\n",
							mod->name, phdr->numtextures, Q_memprint( merged ));
						Mod_GCFixStudioUvDomains( phdr );
						Mod_GCForceVmChromakeyMasks( phdr );
						Mod_GCReleaseStudioTexBuf( tex_buf );
						tex_buf = NULL;
						thdr = NULL;
						goto gc_mesh_only_done;
					}
					Con_Reportf( S_WARN "Xash3D GameCube: G376 T.mdl merge malloc failed '%s' (%s)\n",
						mod->name, Q_memprint( merged ));
				}
				if( tex_buf )
				{
					Mod_GCReleaseStudioTexBuf( tex_buf );
					tex_buf = NULL;
				}
				thdr = NULL;
			}
#endif
			mod->cache.data = calloc( 1, mesh );
			if( !mod->cache.data )
			{
				/* G287/G289: reuse tip-safe BSS bump slice as the resident cache. */
				extern qboolean Mod_GCIsStudioBssCache( const void *ptr );
				if( Mod_GCIsStudioBssCache( buffer ) && mesh <= buffersize )
				{
					mod->cache.data = buffer;
					Con_Reportf( "Xash3D GameCube: G289 studio BSS-resident '%s' (%s)\n",
						mod->name, Q_memprint( mesh ));
				}
				else
				{
					Con_Reportf( S_ERROR "Xash3D GameCube: studio malloc failed '%s' (%s)\n",
						mod->name, Q_memprint( mesh ));
					return;
				}
			}
			else
				memcpy( mod->cache.data, buffer, mesh );
			phdr = (studiohdr_t *)mod->cache.data;
			phdr->length = (int)mesh;
			if( src->numtextures > 0 && !skip_studio_textures && !Host_IsDedicated() )
				Con_Reportf( "Xash3D GameCube: studio skins loaded '%s' n=%d kept=%s\n",
					mod->name, phdr->numtextures, Q_memprint( mesh ));
			else if( src->numtextures > 0 )
			{
				/* Clear file-offset indices so deferred upload does not treat
				 * unloaded embedded skins as already GX-bound. */
				if( phdr->textureindex > 0
					&& (size_t)phdr->textureindex
						+ (size_t)phdr->numtextures * sizeof( mstudiotexture_t )
						<= (size_t)phdr->length )
				{
					mstudiotexture_t *tex = (mstudiotexture_t *)((byte *)phdr
						+ phdr->textureindex );
					int ti;

					for( ti = 0; ti < phdr->numtextures; ti++ )
						tex[ti].index = 0;
				}
				Con_Reportf( "Xash3D GameCube: studio skin upload skipped '%s' n=%d kept=%s\n",
					mod->name, phdr->numtextures, Q_memprint( mesh ));
			}
			else
				Con_Reportf( "Xash3D GameCube: lean studio cache '%s' %s\n",
					mod->name, Q_memprint( mesh ));
gc_mesh_only_done: ;
			/* Release unused BSS tail after mesh truncate (embedded skin upload). */
			if( mod->cache.data && Mod_GCIsStudioBssCache( mod->cache.data ))
			{
				studiohdr_t *kept_hdr = (studiohdr_t *)mod->cache.data;
				if( kept_hdr->length > 0 && (size_t)kept_hdr->length < buffersize )
					Mod_GCStudioBssTrimLast( mod->cache.data, (size_t)kept_hdr->length );
				/* Mesh-only / quality-0: reserve header room for post-MAP_READY
				 * T.mdl merge so GrowLast still works after later BSS companions. */
				if( skip_studio_textures && kept_hdr->length > 0
					&& kept_hdr->numtextures == 0 )
				{
					const size_t pad = 4 * 1024;
					const size_t kept = (size_t)kept_hdr->length;
					if( Mod_GCStudioBssGrowLast( mod->cache.data, kept, kept + pad ))
						Con_Reportf( "Xash3D GameCube: studio BSS skin-pad '%s' +%s\n",
							mod->name, Q_memprint( pad ));
				}
			}
		}
		else
#endif
		{
		mod->cache.data = Mem_Calloc( mod->mempool, cache_length );
		memcpy( mod->cache.data, buffer, cache_length );
		phdr = mod->cache.data;
#if XASH_GAMECUBE
		if( skip_studio_textures )
		{
			phdr->numtextures = 0;
			phdr->textureindex = 0;
			phdr->texturedataindex = 0;
		}
#endif
		}

#if !XASH_DEDICATED
		if( !gc_mesh_only && !skip_studio_textures && !Host_IsDedicated( ))
			ref.dllFuncs.Mod_StudioLoadTextures( mod, phdr );
#endif
	}

	// NOTE: we may not want to keep raw textures in memory. just cutoff model pointer above texture base
	phdr = Mod_MaybeTruncateStudioTextureData( mod );

	// setup bounding box
	if( !VectorCompare( vec3_origin, phdr->bbmin ))
	{
		// clipping bounding box
		VectorCopy( phdr->bbmin, mod->mins );
		VectorCopy( phdr->bbmax, mod->maxs );
	}
	else if( !VectorCompare( vec3_origin, phdr->min ))
	{
		// movement bounding box
		VectorCopy( phdr->min, mod->mins );
		VectorCopy( phdr->max, mod->maxs );
	}
	else
	{
		// well compute bounds from vertices and round to nearest even values
		Mod_StudioComputeBounds( phdr, mod->mins, mod->maxs, true );
		RoundUpHullSize( mod->mins, mod->maxs );
	}

	mod->numframes = Mod_StudioBodyVariations( mod );
	mod->radius = RadiusFromBounds( mod->mins, mod->maxs );
	mod->flags = phdr->flags; // copy header flags

	if( loaded ) *loaded = true;
}

#if XASH_GAMECUBE
extern void Image_GCPurgeDecodeScratch( void );
extern void R_GXStudioInvalidateBindings( void );
qboolean Mod_GCPromoteStudioPath( const char *path );

static qboolean gc_force_studio_skins;

qboolean Mod_GCForceStudioSkinUploadActive( void )
{
	return gc_force_studio_skins;
}

/*
 * Lean *t companions often advertise 64/128 skins while the mesh UV cmds
 * still span the retail texel domain (0..255 / 0..511). Draw uses
 * u = cmd / ptexture.width — a shrunk header doubles UVs and CLAMP turns
 * the Garand into stretched prism skins (ava-play99-79). Grow width/height
 * to cover the mesh UV domain; GL residency stays at the decoded size.
 */
static void Mod_GCFixStudioUvDomains( studiohdr_t *phdr )
{
	mstudiotexture_t *ptexture;
	short *pskinref;
	mstudiobodyparts_t *pbodypart;
	int i, j, k, need_log = 0;

	if( !phdr || phdr->numtextures <= 0 || phdr->textureindex <= 0
		|| phdr->numskinref <= 0 || phdr->skinindex <= 0
		|| phdr->numbodyparts <= 0 || phdr->bodypartindex <= 0 )
		return;

	ptexture = (mstudiotexture_t *)((byte *)phdr + phdr->textureindex );
	pskinref = (short *)((byte *)phdr + phdr->skinindex );
	pbodypart = (mstudiobodyparts_t *)((byte *)phdr + phdr->bodypartindex );

	for( i = 0; i < phdr->numbodyparts; i++ )
	{
		mstudiomodel_t *pmodel = (mstudiomodel_t *)((byte *)phdr + pbodypart[i].modelindex );

		for( j = 0; j < pbodypart[i].nummodels; j++, pmodel++ )
		{
			mstudiomesh_t *pmesh = (mstudiomesh_t *)((byte *)phdr + pmodel->meshindex );

			for( k = 0; k < pmodel->nummesh; k++, pmesh++ )
			{
				short *cmd = (short *)((byte *)phdr + pmesh->triindex );
				int tex, max_s = 0, max_t = 0;
				int n;

				if( pmesh->skinref < 0 || pmesh->skinref >= phdr->numskinref )
					continue;
				tex = pskinref[pmesh->skinref];
				if( tex < 0 || tex >= phdr->numtextures )
					continue;

				while(( n = *cmd++ ))
				{
					int absn = n < 0 ? -n : n;
					for( ; absn > 0; absn--, cmd += 4 )
					{
						if( cmd[2] > max_s ) max_s = cmd[2];
						if( cmd[3] > max_t ) max_t = cmd[3];
					}
				}

				if( max_s >= ptexture[tex].width )
				{
					need_log = 1;
					ptexture[tex].width = max_s + 1;
				}
				if( max_t >= ptexture[tex].height )
				{
					need_log = 1;
					ptexture[tex].height = max_t + 1;
				}
			}
		}
	}

	if( need_log )
	{
		static int uv_fix_logs;
		if( uv_fix_logs < 8 )
		{
			uv_fix_logs++;
			Con_Reportf( "Xash3D GameCube: G378 studio UV domain fix '%s' ntex=%d\n",
				phdr->name[0] ? phdr->name : "?", phdr->numtextures );
		}
	}
}

/* Lean T.mdl hands.bmp ships flags=0 (ava-play99-93/96) so chromakey stays
 * opaque RGB and ghosts the silhouette. Force MASKED on hand skins. */
static void Mod_GCForceVmChromakeyMasks( studiohdr_t *phdr )
{
	mstudiotexture_t *ptexture;
	int i, n = 0;

	if( !phdr || phdr->numtextures <= 0 || phdr->textureindex <= 0 )
		return;
	if( (size_t)phdr->textureindex
		+ (size_t)phdr->numtextures * sizeof( mstudiotexture_t )
		> (size_t)phdr->length )
		return;

	ptexture = (mstudiotexture_t *)((byte *)phdr + phdr->textureindex );
	for( i = 0; i < phdr->numtextures; i++ )
	{
		if( !Q_stristr( ptexture[i].name, "hands" ))
			continue;
		if( !FBitSet( ptexture[i].flags, STUDIO_NF_MASKED ))
		{
			SetBits( ptexture[i].flags, STUDIO_NF_MASKED );
			n++;
		}
	}
	if( n > 0 )
	{
		static int mask_logs;
		if( mask_logs < 8 )
		{
			mask_logs++;
			Con_Reportf( "Xash3D GameCube: G378 VM hands MASKED '%s' n=%d\n",
				phdr->name[0] ? phdr->name : "?", n );
		}
	}
}

static qboolean Mod_GCStudioSkinsBound( const studiohdr_t *phdr )
{
	const mstudiotexture_t *t;
	int i;

	if( !phdr || phdr->numtextures <= 0 || phdr->textureindex <= 0 )
		return false;
	if( (size_t)phdr->textureindex + sizeof( mstudiotexture_t ) > (size_t)phdr->length )
		return false;
	t = (const mstudiotexture_t *)((const byte *)phdr + phdr->textureindex );
	/* Embedded MDLs store file offsets in index until Mod_StudioLoadTextures
	 * replaces them with GL/GX texnums. Offsets for full DoD VMs are >>4K;
	 * live texnums stay in 1..4095. CapFaces *t companions are ~26 KiB so
	 * raw offsets can also land in 1..4095 — Mod_StudioLoadTextures must
	 * actually run (-gclistenserver) to replace them with real skins. */
	for( i = 0; i < phdr->numtextures; i++ )
	{
		const int idx = t[i].index;

		if( idx <= 0 || idx >= 4096 )
			return false;
	}
	return true;
}

/* Post-MAP_READY: upload skins skipped at gc_quality=0 during spawn. */
qboolean Mod_GCAttachStudioSkins( model_t *mod )
{
	studiohdr_t *phdr;
	void *buf = NULL;
	fs_offset_t buf_len = 0;
	studiohdr_t *src_hdr;
	int i;

	if( !mod || !mod->cache.data || Host_IsDedicated() )
		return false;
	phdr = (studiohdr_t *)mod->cache.data;
	if( Mod_GCStudioSkinsBound( phdr ))
		return true;

	Image_GCPurgeDecodeScratch();
	gc_force_studio_skins = true;

	if( phdr->numtextures > 0 && phdr->textureindex > 0
		&& phdr->texturedataindex > (int)sizeof( studiohdr_t )
		&& phdr->texturedataindex < phdr->length )
	{
		/* Quality-0 spawn often keeps the full MDL resident but skips GX upload.
		 * Try in-place first only when pixel blobs still live in cache.
		 * CapFaces lean+*t merge leaves headers with index=0 and no pixels —
		 * in-place upload then samples garbage (ava-vm11 zebra gun). */
		Image_GCPurgeDecodeScratch();
		Mod_GCForceVmChromakeyMasks( phdr );
		ref.dllFuncs.Mod_StudioLoadTextures( mod, phdr );
		if( Mod_GCStudioSkinsBound( phdr ))
		{
			gc_force_studio_skins = false;
			Con_Reportf( "Xash3D GameCube: deferred resident skins '%s' n=%d\n",
				mod->name, phdr->numtextures );
			return true;
		}
		{
			mstudiotexture_t *src_tex, *dst_tex;

			/* CapFaces rifles keep lean mesh in gc_studio/<stem>.mdl (n=0
			 * pixels). Reloading that mirror wiped live texnums (ava-vm6
			 * GX bind fail px=0). Prefer the downsampled *t companion. */
			buf = Mod_GCLoadStudioTexFile( mod->name, &buf_len );
			if( !buf )
				buf = Mod_GCLoadFileToStudioTexScratch( mod->name, &buf_len );
			if( !buf )
			{
				gc_force_studio_skins = false;
				Con_Reportf( S_WARN "Xash3D GameCube: deferred embedded skins miss '%s' n=%d len=%d tdi=%d\n",
					mod->name, phdr->numtextures, phdr->length, phdr->texturedataindex );
				return false;
			}
			src_hdr = R_StudioLoadHeader( mod, buf, (size_t)buf_len );
			if( !src_hdr || src_hdr->numtextures <= 0
				|| src_hdr->texturedataindex <= (int)sizeof( studiohdr_t ))
			{
				Mod_GCReleaseStudioTexBuf( buf );
				gc_force_studio_skins = false;
				return false;
			}
			Mod_GCForceVmChromakeyMasks( src_hdr );
			ref.dllFuncs.Mod_StudioLoadTextures( mod, src_hdr );
			src_tex = (mstudiotexture_t *)((byte *)src_hdr + src_hdr->textureindex );
			dst_tex = (mstudiotexture_t *)((byte *)phdr + phdr->textureindex );
			for( i = 0; i < phdr->numtextures && i < src_hdr->numtextures; i++ )
				dst_tex[i].index = src_tex[i].index;
			Mod_GCReleaseStudioTexBuf( buf );
			gc_force_studio_skins = false;
			Con_Reportf( "Xash3D GameCube: deferred embedded skins '%s' n=%d\n",
				mod->name, phdr->numtextures );
			return Mod_GCStudioSkinsBound( phdr );
		}
	}

	/* Separate *t.mdl atlas (controller, islave, …). CapFaces viewmodels
	 * often ship mesh-only under gc_studio/ with textures still in the full
	 * models/*.mdl on disc (no *T.mdl companion) — fall back to that. */
	buf = Mod_GCLoadStudioTexFile( mod->name, &buf_len );
	src_hdr = R_StudioLoadHeader( mod, buf, (size_t)buf_len );
	if( !src_hdr || src_hdr->numtextures <= 0 )
	{
		if( buf )
		{
			Mod_GCReleaseStudioTexBuf( buf );
			buf = NULL;
		}
		buf = Mod_GCLoadFileToStudioTexScratch( mod->name, &buf_len );
		src_hdr = R_StudioLoadHeader( mod, buf, (size_t)buf_len );
	}
	if( !src_hdr || src_hdr->numtextures <= 0 )
	{
		if( buf )
			Mod_GCReleaseStudioTexBuf( buf );
		Con_Reportf( S_WARN "Xash3D GameCube: deferred skins miss '%s' (no T.mdl / full MDL tex)\n",
			mod->name );
		return false;
	}
	{
		size_t mesh = phdr->length > 0 ? (size_t)phdr->length : 0;
		size_t size1 = src_hdr->numtextures * sizeof( mstudiotexture_t );
		size_t size2 = src_hdr->numskinfamilies * src_hdr->numskinref * sizeof( short );
		size_t merged = mesh + size1 + size2;
		byte *slot = NULL;
		mstudiotexture_t *src_tex, *dst_tex;

		if( Mod_GCIsStudioBssCache( mod->cache.data ))
			slot = Mod_GCStudioBssGrowLast( mod->cache.data, mesh, merged );
		Image_GCPurgeDecodeScratch();
		Mod_GCForceVmChromakeyMasks( src_hdr );
		ref.dllFuncs.Mod_StudioLoadTextures( mod, src_hdr );
		if( !slot )
			slot = (byte *)calloc( 1, merged );
		if( !slot )
			Con_Reportf( S_WARN "Xash3D GameCube: deferred T.mdl merge OOM '%s' need=%s\n",
				mod->name, Q_memprint( merged ));
		if( slot )
		{
			byte *in, *out;

			if( slot != (byte *)mod->cache.data )
				memcpy( slot, mod->cache.data, mesh );
			mod->cache.data = slot;
			phdr = (studiohdr_t *)slot;
			phdr->length = (int)merged;
			phdr->numskinfamilies = src_hdr->numskinfamilies;
			phdr->numtextures = src_hdr->numtextures;
			phdr->numskinref = src_hdr->numskinref;
			phdr->textureindex = (int)mesh;
			phdr->skinindex = phdr->textureindex + (int)size1;
			in = (byte *)src_hdr + src_hdr->textureindex;
			out = (byte *)phdr + phdr->textureindex;
			memcpy( out, in, size1 + size2 );
			src_tex = (mstudiotexture_t *)((byte *)src_hdr + src_hdr->textureindex );
			dst_tex = (mstudiotexture_t *)out;
			for( i = 0; i < phdr->numtextures; i++ )
				dst_tex[i].index = src_tex[i].index;
			Con_Reportf( "Xash3D GameCube: deferred T.mdl skins '%s' n=%d kept=%s\n",
				mod->name, phdr->numtextures, Q_memprint( merged ));
			Mod_GCFixStudioUvDomains( phdr );
			Mod_GCForceVmChromakeyMasks( phdr );
		}
	}
	Mod_GCReleaseStudioTexBuf( buf );
	gc_force_studio_skins = false;
	if( mod->cache.data )
	{
		Mod_GCFixStudioUvDomains( (studiohdr_t *)mod->cache.data );
		Mod_GCForceVmChromakeyMasks( (studiohdr_t *)mod->cache.data );
	}
	return Mod_GCStudioSkinsBound( (studiohdr_t *)mod->cache.data );
}

void Mod_GCUploadDeferredStudioSkins( void )
{
#if XASH_DODGC_NETWORK
	/* CapFaces: skin only the landmark team rifle after claim. Early present≈24
	 * deferred used to also hit a precached mp40 and tip-OOM before Allies pick. */
	static const char *paths_storage[2];
	const char **paths;
	const char *landmark = Mod_GCLandmarkViewModelPath();

	if( !landmark || !landmark[0] )
		return;
	paths_storage[0] = landmark;
	paths_storage[1] = NULL;
	paths = paths_storage;
#else
	static const char *paths[] = {
		/* Hallway cast first (G381): scientistT+barneyT soft skins were the
		 * 22/31 defaultTexture case — attach after tip recovers so the 384 KiB
		 * overflow + adaptive 96→64 cap can hold both. Controller/islave next;
		 * viewmodels last (in-place full MDL, tightens MEM1). */
		"models/scientist.mdl",
		"models/barney.mdl",
		"models/controller.mdl",
		"models/islave.mdl",
		"models/v_crowbar.mdl",
		"models/v_9mmhandgun.mdl",
		"models/v_colt.mdl",
		"models/v_luger.mdl",
		"models/v_garand.mdl",
		"models/v_mp40.mdl",
		NULL
	};
#endif
	static char last_map[64];
	int i, ready = 0, attempted = 0;

	if( Host_IsDedicated() )
		return;
	/* Direct -gcnewgame and menu playstart both need post-tip soft skins;
	 * WorldReady alone can lag the first presents after changelevel.
	 * CapFaces listen (-gcworldrender+-gcplayerinput) needs the same for
	 * team viewmodels once Flipper is past present≈4. */
	if( !GC_IsNewGameWorldReady() && !Sys_CheckParm( "-gcnewgame" )
		&& !Sys_CheckParm( "-gcmenuplaystart" )
		&& !( Sys_CheckParm( "-gcworldrender" ) && Sys_CheckParm( "-gcplayerinput" )))
		return;
	if( sv.name[0] && Q_stricmp( last_map, sv.name ))
	{
		Q_strncpy( last_map, sv.name, sizeof( last_map ));
	}

	for( i = 0; paths[i]; i++ )
	{
		model_t *mod = Mod_FindName( paths[i], false );
		qboolean ok;

#if XASH_DODGC_NETWORK
		/* CapFaces: never promote the other team's rifle just to skin it. */
		if( !mod || !mod->cache.data )
		{
			Con_Reportf( S_WARN "Xash3D GameCube: deferred skins skip '%s' (no cache)\n",
				paths[i] );
			continue;
		}
#else
		if( !mod || !mod->cache.data )
			Mod_GCPromoteStudioPath( paths[i] );
		mod = Mod_FindName( paths[i], false );
		if( !mod || !mod->cache.data )
		{
			Con_Reportf( S_WARN "Xash3D GameCube: deferred skins skip '%s' (no cache)\n",
				paths[i] );
			continue;
		}
#endif
		attempted++;
		ok = Mod_GCAttachStudioSkins( mod );
		Con_Reportf( "Xash3D GameCube: deferred skins try '%s' ok=%d n=%d\n",
			paths[i], ok ? 1 : 0,
			mod->cache.data ? ((studiohdr_t *)mod->cache.data)->numtextures : -1 );
		if( ok )
			ready++;
	}
	Con_Reportf( "Xash3D GameCube: deferred studio skins ready=%d/%d map=%s\n",
		ready, attempted, sv.name[0] ? sv.name : "?" );
	if( ready > 0 )
		R_GXStudioInvalidateBindings();
}
#endif

static sv_blending_interface_t gBlendAPI =
{
	SV_BLENDING_INTERFACE_VERSION,
	SV_StudioSetupBones,
};

static server_studio_api_t gStudioAPI =
{
	Mod_Calloc,
	Mod_CacheCheck,
	Mod_LoadCacheFile,
	Mod_StudioExtradata,
};

/*
===============
Mod_InitStudioAPI

Initialize server studio (blending interface)
===============
*/
void Mod_InitStudioAPI( void )
{
	static STUDIOAPI	pBlendIface;

	pBlendAPI = &gBlendAPI;

	pBlendIface = (STUDIOAPI)COM_GetProcAddress( svgame.hInstance, "Server_GetBlendingInterface" );
	if( pBlendIface && pBlendIface( SV_BLENDING_INTERFACE_VERSION, &pBlendAPI, &gStudioAPI, &studio_transform, &studio_bones ))
	{
		Con_Reportf( "%s: ^2initailized Server Blending interface ^7ver. %i\n", __func__, SV_BLENDING_INTERFACE_VERSION );
		return;
	}

	// just restore pointer to builtin function
	pBlendAPI = &gBlendAPI;
}

/*
===============
Mod_ResetStudioAPI

Returns to default callbacks
===============
*/
void Mod_ResetStudioAPI( void )
{
	pBlendAPI = &gBlendAPI;
}
