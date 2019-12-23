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
	float	frame;
	int	sequence;
	vec3_t	angles;
	vec3_t	origin;
	vec3_t	size;
	byte	controller[4];
	byte	blending[2];
	model_t	*model;
	uint	current_hull;
	uint	current_plane;
	uint	numhitboxes;
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
static mclipnode_t			studio_clipnodes[6];
static mplane_t			studio_planes[768];
static mplane_t			cache_planes[768];

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
	int	i, side;

	if( studio_hull[0].planes != NULL )
		return;	// already initailized

	for( i = 0; i < 6; i++ )
	{
		studio_clipnodes[i].planenum = i;

		side = i & 1;

		studio_clipnodes[i].children[side] = CONTENTS_EMPTY;
		if( i != 5 ) studio_clipnodes[i].children[side^1] = i + 1;
		else studio_clipnodes[i].children[side^1] = CONTENTS_SOLID;
	}

	for( i = 0; i < MAXSTUDIOBONES; i++ )
	{
		studio_hull[i].clipnodes = studio_clipnodes;
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
void Mod_AddToStudioCache( float frame, int sequence, vec3_t angles, vec3_t origin, vec3_t size, byte *pcontroller, byte *pblending, model_t *model, hull_t *hull, int numhitboxes )
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
mstudiocache_t *Mod_CheckStudioCache( model_t *model, float frame, int sequence, vec3_t angles, vec3_t origin, vec3_t size, byte *controller, byte *blending )
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
void Mod_SetStudioHullPlane( int planenum, int bone, int axis, float offset, const vec3_t size )
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

	if( mod_studiocache->value )
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

	if( mod_studiocache->value )
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

	Mod_StudioCalcBoneAdj( adj, pcontroller );

	for( j = numbones - 1; j >= 0; j-- )
	{
		i = boneused[j];
		R_StudioCalcBoneQuaternion( frame, s, &pbone[i], &panim[i], adj, q[i] );
		R_StudioCalcBonePosition( frame, s, &pbone[i], &panim[i], adj, pos[i] );
	}

	if( pseqdesc->motiontype & STUDIO_X ) pos[pseqdesc->motionbone][0] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Y ) pos[pseqdesc->motionbone][1] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Z ) pos[pseqdesc->motionbone][2] = 0.0f;
}

/*
====================
StudioCalcBoneQuaternion

====================
*/
void R_StudioCalcBoneQuaternion( int frame, float s, mstudiobone_t *pbone, mstudioanim_t *panim, float *adj, vec4_t q )
{
	vec3_t	angles1;
	vec3_t	angles2;
	int	j, k;

	for( j = 0; j < 3; j++ )
	{
		if( !panim || panim->offset[j+3] == 0 )
		{
			angles2[j] = angles1[j] = pbone->value[j+3]; // default;
		}
		else
		{
			mstudioanimvalue_t *panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[j+3]);

			k = frame;
			
			// debug
			if( panimvalue->num.total < panimvalue->num.valid )
				k = 0;

			// find span of values that includes the frame we want			
			while( panimvalue->num.total <= k )
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;

				// debug
				if( panimvalue->num.total < panimvalue->num.valid )
					k = 0;
			}

			// bah, missing blend!
			if( panimvalue->num.valid > k )
			{
				angles1[j] = panimvalue[k+1].value;

				if( panimvalue->num.valid > k + 1 )
				{
					angles2[j] = panimvalue[k+2].value;
				}
				else
				{
					if( panimvalue->num.total > k + 1 )
						angles2[j] = angles1[j];
					else angles2[j] = panimvalue[panimvalue->num.valid+2].value;
				}
			}
			else
			{
				angles1[j] = panimvalue[panimvalue->num.valid].value;
				if( panimvalue->num.total > k + 1 )
					angles2[j] = angles1[j];
				else angles2[j] = panimvalue[panimvalue->num.valid+2].value;
			}

			angles1[j] = pbone->value[j+3] + angles1[j] * pbone->scale[j+3];
			angles2[j] = pbone->value[j+3] + angles2[j] * pbone->scale[j+3];
		}

		if( pbone->bonecontroller[j+3] != -1 && adj != NULL )
		{
			angles1[j] += adj[pbone->bonecontroller[j+3]];
			angles2[j] += adj[pbone->bonecontroller[j+3]];
		}
	}

	if( !VectorCompare( angles1, angles2 ))
	{
		vec4_t	q1, q2;

		AngleQuaternion( angles1, q1, true );
		AngleQuaternion( angles2, q2, true );
		QuaternionSlerp( q1, q2, s, q );
	}
	else
	{
		AngleQuaternion( angles1, q, true );
	}
}

/*
====================
StudioCalcBonePosition

====================
*/
void R_StudioCalcBonePosition( int frame, float s, mstudiobone_t *pbone, mstudioanim_t *panim, float *adj, vec3_t pos )
{
	vec3_t	origin1;
	vec3_t	origin2;
	int	j, k;

	for( j = 0; j < 3; j++ )
	{
		if( !panim || panim->offset[j] == 0 )
		{
			origin2[j] = origin1[j] = pbone->value[j]; // default;
		}
		else
		{
			mstudioanimvalue_t	*panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[j]);

			k = frame;

			// debug
			if( panimvalue->num.total < panimvalue->num.valid )
				k = 0;

			// find span of values that includes the frame we want
			while( panimvalue->num.total <= k )
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;

  				// debug
				if( panimvalue->num.total < panimvalue->num.valid )
					k = 0;
			}

			// bah, missing blend!
			if( panimvalue->num.valid > k )
			{
				origin1[j] = panimvalue[k+1].value;

				if( panimvalue->num.valid > k + 1 )
				{
					origin2[j] = panimvalue[k+2].value;
				}
				else
				{
					if( panimvalue->num.total > k + 1 )
						origin2[j] = origin1[j];
					else origin2[j] = panimvalue[panimvalue->num.valid+2].value;
				}
			}
			else
			{
				origin1[j] = panimvalue[panimvalue->num.valid].value;
				if( panimvalue->num.total > k + 1 )
					origin2[j] = origin1[j];
				else origin2[j] = panimvalue[panimvalue->num.valid+2].value;
			}

			origin1[j] = pbone->value[j] + origin1[j] * pbone->scale[j];
			origin2[j] = pbone->value[j] + origin2[j] * pbone->scale[j];
		}

		if( pbone->bonecontroller[j] != -1 && adj != NULL )
		{
			origin1[j] += adj[pbone->bonecontroller[j]];
			origin2[j] += adj[pbone->bonecontroller[j]];
		}
	}

	if( !VectorCompare( origin1, origin2 ))
	{
		VectorLerp( origin1, s, origin2, pos );
	}
	else
	{
		VectorCopy( origin1, pos );
	}
}

/*
====================
StudioSlerpBones

====================
*/
void R_StudioSlerpBones( int numbones, vec4_t q1[], float pos1[][3], vec4_t q2[], float pos2[][3], float s )
{
	int	i;

	s = bound( 0.0f, s, 1.0f );

	for( i = 0; i < numbones; i++ )
	{
		QuaternionSlerp( q1[i], q2[i], s, q1[i] );
		VectorLerp( pos1[i], s, pos2[i], pos1[i] );
	}
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

		COM_FileBase( m_pSubModel->name, modelname );
		COM_ExtractFilePath( m_pSubModel->name, modelpath );

		// NOTE: here we build real sub-animation filename because stupid user may rename model without recompile
		Q_snprintf( filepath, sizeof( filepath ), "%s/%s%i%i.mdl", modelpath, modelname, pseqdesc->seqgroup / 10, pseqdesc->seqgroup % 10 );

		buf = FS_LoadFile( filepath, &filesize, false );
		if( !buf || !filesize ) Host_Error( "StudioGetAnim: can't load %s\n", filepath );
		if( IDSEQGRPHEADER != *(uint *)buf ) Host_Error( "StudioGetAnim: %s is corrupted\n", filepath );

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
			Con_Reportf( S_WARN "SV_StudioSetupBones: sequence %i/%i out of range for model %s\n", sequence, mod_studiohdr->numseq, pModel->name );
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
void Mod_StudioBoundVertex( vec3_t mins, vec3_t maxs, int *numverts, const vec3_t vertex )
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
void Mod_StudioAccumulateBoneVerts( vec3_t mins, vec3_t maxs, int *numverts, vec3_t bone_mins, vec3_t bone_maxs, int *numbones )
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
				R_StudioCalcBonePosition( k, 0, &pbones[j], panim, NULL, pos );
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
	static char	texname[MAX_QPATH];

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
studiohdr_t *R_StudioLoadHeader( model_t *mod, const void *buffer )
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

/*
=================
Mod_LoadStudioModel
=================
*/
void Mod_LoadStudioModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	studiohdr_t	*phdr;

	if( loaded ) *loaded = false;
	loadmodel->mempool = Mem_AllocPool( va( "^2%s^7", loadmodel->name ));
	loadmodel->type = mod_studio;

	phdr = R_StudioLoadHeader( mod, buffer );
	if( !phdr ) return;	// bad model

	if( !Host_IsDedicated() )
	{
		if( phdr->numtextures == 0 )
		{
			studiohdr_t	*thdr;
			byte		*in, *out;
			void		*buffer2 = NULL;
			size_t		size1, size2;

			buffer2 = FS_LoadFile( Mod_StudioTexName( mod->name ), NULL, false );
			thdr = R_StudioLoadHeader( mod, buffer2 );

			if( !thdr )
			{
				Con_Printf( S_WARN "Mod_LoadStudioModel: %s missing textures file\n", mod->name );
				if( buffer2 ) Mem_Free( buffer2 );
			}
			else
			{
				ref.dllFuncs.Mod_StudioLoadTextures( mod, thdr );

				// give space for textures and skinrefs
				size1 = thdr->numtextures * sizeof( mstudiotexture_t );
				size2 = thdr->numskinfamilies * thdr->numskinref * sizeof( short );
				mod->cache.data = Mem_Calloc( loadmodel->mempool, phdr->length + size1 + size2 );
				memcpy( loadmodel->cache.data, buffer, phdr->length ); // copy main mdl buffer
				phdr = (studiohdr_t *)loadmodel->cache.data; // get the new pointer on studiohdr
				phdr->numskinfamilies = thdr->numskinfamilies;
				phdr->numtextures = thdr->numtextures;
				phdr->numskinref = thdr->numskinref;
				phdr->textureindex = phdr->length;
				phdr->skinindex = phdr->textureindex + size1;

				in = (byte *)thdr + thdr->textureindex;
				out = (byte *)phdr + phdr->textureindex;
				memcpy( out, in, size1 + size2 );	// copy textures + skinrefs
				phdr->length += size1 + size2;
				Mem_Free( buffer2 ); // release T.mdl
			}
		}
		else
		{
			// NOTE: don't modify source buffer because it's used for CRC computing
			loadmodel->cache.data = Mem_Calloc( loadmodel->mempool, phdr->length );
			memcpy( loadmodel->cache.data, buffer, phdr->length );
			phdr = (studiohdr_t *)loadmodel->cache.data; // get the new pointer on studiohdr
			ref.dllFuncs.Mod_StudioLoadTextures( mod, phdr );

			// NOTE: we wan't keep raw textures in memory. just cutoff model pointer above texture base
			loadmodel->cache.data = Mem_Realloc( loadmodel->mempool, loadmodel->cache.data, phdr->texturedataindex );
			phdr = (studiohdr_t *)loadmodel->cache.data; // get the new pointer on studiohdr
			phdr->length = phdr->texturedataindex;	// update model size
		}
	}
	else
	{
		// just copy model into memory
		loadmodel->cache.data = Mem_Calloc( loadmodel->mempool, phdr->length );
		memcpy( loadmodel->cache.data, buffer, phdr->length );

		phdr = loadmodel->cache.data;
	}

	// setup bounding box
	if( !VectorCompare( vec3_origin, phdr->bbmin ))
	{
		// clipping bounding box
		VectorCopy( phdr->bbmin, loadmodel->mins );
		VectorCopy( phdr->bbmax, loadmodel->maxs );
	}
	else if( !VectorCompare( vec3_origin, phdr->min ))
	{
		// movement bounding box
		VectorCopy( phdr->min, loadmodel->mins );
		VectorCopy( phdr->max, loadmodel->maxs );
	}
	else
	{
		// well compute bounds from vertices and round to nearest even values
		Mod_StudioComputeBounds( phdr, loadmodel->mins, loadmodel->maxs, true );
		RoundUpHullSize( loadmodel->mins );
		RoundUpHullSize( loadmodel->maxs );
	}

	loadmodel->numframes = Mod_StudioBodyVariations( loadmodel );
	loadmodel->radius = RadiusFromBounds( loadmodel->mins, loadmodel->maxs );
	loadmodel->flags = phdr->flags; // copy header flags

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
		Con_Reportf( "SV_LoadProgs: ^2initailized Server Blending interface ^7ver. %i\n", SV_BLENDING_INTERFACE_VERSION );
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
