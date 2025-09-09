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
#include "server.h"
#include "studio.h"
#include "r_studioint.h"
#include "library.h"
#include "ref_common.h"

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

/*
====================
Mod_InitStudioHull
====================
*/
void Mod_InitStudioHull( void )
{
	int	i;

	if( studio_hull[0].planes != NULL )
		return;	// already initailized

	for( i = 0; i < MAXSTUDIOBONES; i++ )
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
	mstudiocache_t *pCache;

	if( numhitboxes + cache_current_hull >= MAXSTUDIOBONES )
		Mod_ClearStudioCache();

	cache_current++;
	pCache = &cache_studio[cache_current & STUDIO_CACHEMASK];

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
	mstudiocache_t	*pCached;
	int		i;

	for( i = 0; i < STUDIO_CACHESIZE; i++ )
	{
		pCached = &cache_studio[(cache_current - i) & STUDIO_CACHEMASK];

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
	vec3_t		angles2;
	mstudiocache_t	*bonecache;
	mstudiobbox_t	*phitbox;
	qboolean		bSkipShield;
	int		i, j;

	bSkipShield = false;
	*numhitboxes = 0; // assume error

	if( mod_studiocache.value )
	{
		bonecache = Mod_CheckStudioCache( model, frame, sequence, angles, origin, size, pcontroller, pblending );

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

	VectorCopy( angles, angles2 );

	if( !FBitSet( host.features, ENGINE_COMPENSATE_QUAKE_BUG ))
		angles2[PITCH] = -angles2[PITCH]; // stupid quake bug

	pBlendAPI->SV_StudioSetupBones( model, frame, sequence, angles2, origin, pcontroller, pblending, -1, pEdict );
	phitbox = (mstudiobbox_t *)((byte *)mod_studiohdr + mod_studiohdr->hitboxindex);

	if( SV_IsValidEdict( pEdict ) && pEdict->v.gamestate == 1 )
		bSkipShield = 1;

	for( i = j = 0; i < mod_studiohdr->numhitboxes; i++, j += 6 )
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


/*
====================
StudioGetAnim

====================
*/
void *R_StudioGetAnim( studiohdr_t *m_pStudioHeader, model_t *m_pSubModel, mstudioseqdesc_t *pseqdesc )
{
	mstudioseqgroup_t	*pseqgroup;
	cache_user_t	*paSequences;
	fs_offset_t	filesize;
	byte		*buf;

	pseqgroup = (mstudioseqgroup_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqgroupindex) + pseqdesc->seqgroup;
	if( pseqdesc->seqgroup == 0 )
		return ((byte *)m_pStudioHeader + pseqdesc->animindex);

	paSequences = (cache_user_t *)m_pSubModel->submodels;

	if( paSequences == NULL )
	{
		paSequences = (cache_user_t *)Mem_Calloc( com_studiocache, MAXSTUDIOGROUPS * sizeof( cache_user_t ));
		m_pSubModel->submodels = (void *)paSequences;
	}

	// check for already loaded
	if( !Mod_CacheCheck(( cache_user_t *)&( paSequences[pseqdesc->seqgroup] )))
	{
		string	filepath, modelname, modelpath;

		COM_FileBase( m_pSubModel->name, modelname, sizeof( modelname ));
		COM_ExtractFilePath( m_pSubModel->name, modelpath );

		// NOTE: here we build real sub-animation filename because stupid user may rename model without recompile
		Q_snprintf( filepath, sizeof( filepath ), "%s/%s%i%i.mdl", modelpath, modelname, pseqdesc->seqgroup / 10, pseqdesc->seqgroup % 10 );

		buf = FS_LoadFile( filepath, &filesize, false );
		if( !buf || !filesize ) Host_Error( "%s: can't load %s\n", __func__, filepath );
		if( IDSEQGRPHEADER != *(uint *)buf ) Host_Error( "%s: %s is corrupted\n", __func__, filepath );

		Con_Printf( "loading: %s\n", filepath );

		paSequences[pseqdesc->seqgroup].data = Mem_Calloc( com_studiocache, filesize );
		memcpy( paSequences[pseqdesc->seqgroup].data, buf, filesize );
		Mem_Free( buf );
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
	vec3_t			angles2;
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

	VectorCopy( e->v.angles, angles2 );

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
void Mod_StudioComputeBounds( void *buffer, vec3_t mins, vec3_t maxs, qboolean ignore_sequences )
{
	int		i, j, k, numseq;
	studiohdr_t	*pstudiohdr;
	mstudiobodyparts_t	*pbodypart;
	mstudiomodel_t	*m_pSubModel;
	mstudioseqgroup_t	*pseqgroup;
	mstudioseqdesc_t	*pseqdesc;
	mstudiobone_t	*pbones;
	mstudioanim_t	*panim;
	vec3_t		bone_mins, bone_maxs;
	vec3_t		vert_mins, vert_maxs;
	int		vert_count, bone_count;
	int		bodyCount = 0;
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
		bodyCount += pbodypart[i].nummodels;

	// The studio models we want are vec3_t mins, vec3_t maxsight after the bodyparts (still need to
	// find a detailed breakdown of the mdl format).  Move pointer there.
	m_pSubModel = (mstudiomodel_t *)(&pbodypart[pstudiohdr->numbodyparts]);

	for( i = 0; i < bodyCount; i++ )
	{
		pverts = (vec3_t *)((byte *)pstudiohdr + m_pSubModel[i].vertindex);

		for( j = 0; j < m_pSubModel[i].numverts; j++ )
			Mod_StudioBoundVertex( bone_mins, bone_maxs, &vert_count, pverts[j] );
	}

	pbones = (mstudiobone_t *)((byte *)pstudiohdr + pstudiohdr->boneindex);
	numseq = (ignore_sequences) ? 1 : pstudiohdr->numseq;

	for( i = 0; i < numseq; i++ )
	{
		pseqdesc = (mstudioseqdesc_t *)((byte *)pstudiohdr + pstudiohdr->seqindex) + i;
		pseqgroup = (mstudioseqgroup_t *)((byte *)pstudiohdr + pstudiohdr->seqgroupindex) + pseqdesc->seqgroup;

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

/*
=================
R_StudioLoadHeader
=================
*/
static studiohdr_t *R_StudioLoadHeader( model_t *mod, const void *buffer )
{
	byte		*pin;
	studiohdr_t	*phdr;
	int		i;

	if( !buffer ) return NULL;

	pin = (byte *)buffer;
	phdr = (studiohdr_t *)pin;
	i = phdr->version;

	if( i != STUDIO_VERSION )
	{
		Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", mod->name, i, STUDIO_VERSION );
		return NULL;
	}

	return (studiohdr_t *)buffer;
}

static studiohdr_t *Mod_MaybeTruncateStudioTextureData( model_t *mod )
{
#if XASH_LOW_MEMORY
	studiohdr_t *phdr = (studiohdr_t *)mod->cache.data;

	mod->cache.data = Mem_Realloc( mod->mempool, mod->cache.data, phdr->texturedataindex );
	phdr = (studiohdr_t *)mod->cache.data; // get the new pointer on studiohdr
	phdr->length = phdr->texturedataindex; // update model size

	return phdr;
#else
	// NOTE: some mods potentially might require full studio model data
	return (studiohdr_t *)mod->cache.data;
#endif
}

/*
=================
Mod_LoadStudioModel
=================
*/
void Mod_LoadStudioModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	char poolname[MAX_VA_STRING];
	studiohdr_t	*phdr;
	qboolean textures_loaded = false;

	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );

	if( loaded ) *loaded = false;
	mod->mempool = Mem_AllocPool( poolname );
	mod->type = mod_studio;

	phdr = R_StudioLoadHeader( mod, buffer );
	if( !phdr || phdr->length < sizeof( studiohdr_t )) // garbage value in length
		return;	// bad model

#if !XASH_DEDICATED
	if( !Host_IsDedicated( ) && phdr->numtextures == 0 )
	{
		studiohdr_t *thdr;
		void *buffer2;

		buffer2 = FS_LoadFile( Mod_StudioTexName( mod->name ), NULL, false );
		thdr = R_StudioLoadHeader( mod, buffer2 );

		if( thdr != NULL )
		{
			byte *in, *out;
			size_t size1, size2;

			// TODO: Mod_StudioLoadTextures will crash if passed a merged studio model!
			ref.dllFuncs.Mod_StudioLoadTextures( mod, thdr );
			textures_loaded = true;

			// give space for textures and skinrefs
			size1 = thdr->numtextures * sizeof( mstudiotexture_t );
			size2 = thdr->numskinfamilies * thdr->numskinref * sizeof( short );
			mod->cache.data = Mem_Calloc( mod->mempool, phdr->length + size1 + size2 );
			memcpy( mod->cache.data, buffer, phdr->length ); // copy main mdl buffer
			phdr = (studiohdr_t *)mod->cache.data; // get the new pointer on studiohdr
			phdr->numskinfamilies = thdr->numskinfamilies;
			phdr->numtextures = thdr->numtextures;
			phdr->numskinref = thdr->numskinref;
			phdr->textureindex = phdr->length;
			phdr->skinindex = phdr->textureindex + size1;

			in = (byte *)thdr + thdr->textureindex;
			out = (byte *)phdr + phdr->textureindex;
			memcpy( out, in, size1 + size2 );	// copy textures + skinrefs
			phdr->length += size1 + size2;
		}
		else Con_Printf( S_WARN "%s: %s missing textures file\n", __func__, mod->name );

		if( buffer2 )
			Mem_Free( buffer2 ); // release T.mdl
	}
#endif

	if( !textures_loaded )
	{
		// NOTE: don't modify source buffer because it's used for CRC computing
		mod->cache.data = Mem_Calloc( mod->mempool, phdr->length );
		memcpy( mod->cache.data, buffer, phdr->length );
		phdr = mod->cache.data;

#if !XASH_DEDICATED
		if( !Host_IsDedicated( ))
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
		RoundUpHullSize( mod->mins );
		RoundUpHullSize( mod->maxs );
	}

	mod->numframes = Mod_StudioBodyVariations( mod );
	mod->radius = RadiusFromBounds( mod->mins, mod->maxs );
	mod->flags = phdr->flags; // copy header flags

	if( loaded ) *loaded = true;
}

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
