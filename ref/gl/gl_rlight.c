/*
gl_rlight.c - dynamic and static lights
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

#include "gl_local.h"
#include "pm_local.h"
#include "studio.h"
#include "xash3d_mathlib.h"
#include "ref_params.h"

/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/
/*
==================
CL_RunLightStyles

==================
*/
void CL_RunLightStyles( lightstyle_t *ls )
{
	CL_RunLightStyles_( ls, tr.lightstylevalue );
}


/*
=======================================================================

	AMBIENT LIGHTING

=======================================================================
*/
static vec3_t	g_trace_lightspot;
static vec3_t	g_trace_lightvec;
static float	g_trace_fraction;

/*
=================
R_RecursiveLightPoint
=================
*/
static qboolean R_RecursiveLightPoint( model_t *model, mnode_t *node, float p1f, float p2f, colorVec *cv, const vec3_t start, const vec3_t end )
{
start:
	// didn't hit anything
	if( !node || node->contents < 0 )
	{
		cv->r = cv->g = cv->b = cv->a = 0;
		return false;
	}

	// calculate mid point
	float front = PlaneDiff( start, node->plane );
	float back = PlaneDiff( end, node->plane );

	int side = front < 0;
	if(( back < 0 ) == side )
	{
		node = node_child( node, side, model );
		goto start;
	}

	float frac = front / ( front - back );

	vec3_t mid;
	VectorLerp( start, frac, end, mid );

	float midf = p1f + ( p2f - p1f ) * frac;

	// co down front side
	if( R_RecursiveLightPoint( model, node_child( node, side, model ), p1f, midf, cv, start, mid ))
		return true; // hit something

	if(( back < 0 ) == side )
	{
		cv->r = cv->g = cv->b = cv->a = 0;
		return false; // didn't hit anything
	}

	// check for impact on this node
	int firstsurface = node_firstsurface( node, model );
	int numsurfaces = node_numsurfaces( node, model );

	VectorCopy( mid, g_trace_lightspot );

	for( int i = 0; i < numsurfaces; i++ )
	{
		const msurface_t *surf = &model->surfaces[firstsurface + i];
		const mextrasurf_t *info = surf->info;

		if( FBitSet( surf->flags, SURF_DRAWTILED ))
			continue;	// no lightmaps

		float s = DotProduct( mid, info->lmvecs[0] ) + info->lmvecs[0][3];
		float t = DotProduct( mid, info->lmvecs[1] ) + info->lmvecs[1][3];

		if( s < info->lightmapmins[0] || t < info->lightmapmins[1] )
			continue;

		float ds = s - info->lightmapmins[0];
		float dt = t - info->lightmapmins[1];

		if ( ds > info->lightextents[0] || dt > info->lightextents[1] )
			continue;

		cv->r = cv->g = cv->b = cv->a = 0;

		if( !surf->samples )
			return true;

		int sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
		int smax = (info->lightextents[0] / sample_size) + 1;
		int tmax = (info->lightextents[1] / sample_size) + 1;

		ds /= sample_size;
		dt /= sample_size;

		g_trace_fraction = midf;

		const color24 *lm = surf->samples + Q_rint( dt ) * smax + Q_rint( ds );
		const color24 *dm = NULL;
		matrix3x4 tbn;

		if( surf->info->deluxemap )
		{
			vec3_t	faceNormal;

			dm = surf->info->deluxemap + Q_rint( dt ) * smax + Q_rint( ds );

			if( FBitSet( surf->flags, SURF_PLANEBACK ))
				VectorNegate( surf->plane->normal, faceNormal );
			else VectorCopy( surf->plane->normal, faceNormal );

			// compute face TBN
#if 1
			Vector4Set( tbn[0], surf->info->lmvecs[0][0], surf->info->lmvecs[0][1], surf->info->lmvecs[0][2], 0.0f );
			Vector4Set( tbn[1], -surf->info->lmvecs[1][0], -surf->info->lmvecs[1][1], -surf->info->lmvecs[1][2], 0.0f );
			Vector4Set( tbn[2], faceNormal[0], faceNormal[1], faceNormal[2], 0.0f );
#else
			Vector4Set( tbn[0], surf->info->lmvecs[0][0], -surf->info->lmvecs[1][0], faceNormal[0], 0.0f );
			Vector4Set( tbn[1], surf->info->lmvecs[0][1], -surf->info->lmvecs[1][1], faceNormal[1], 0.0f );
			Vector4Set( tbn[2], surf->info->lmvecs[0][2], -surf->info->lmvecs[1][2], faceNormal[2], 0.0f );
#endif
			VectorNormalize( tbn[0] );
			VectorNormalize( tbn[1] );
			VectorNormalize( tbn[2] );
		}

		int size = smax * tmax;

		for( int map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
		{
			uint	scale = tr.lightstylevalue[surf->styles[map]];

			cv->r += lm->r * scale;
			cv->g += lm->g * scale;
			cv->b += lm->b * scale;

			lm += size; // skip to next lightmap

			if( dm != NULL )
			{
				vec3_t	srcNormal, lightNormal;
				float	f = (1.0f / 128.0f);

				VectorSet( srcNormal, ((float)dm->r - 128.0f) * f, ((float)dm->g - 128.0f) * f, ((float)dm->b - 128.0f) * f );
				Matrix3x4_VectorIRotate( tbn, srcNormal, lightNormal );		// turn to world space
				VectorScale( lightNormal, (float)scale * -1.0f, lightNormal );	// turn direction from light
				VectorAdd( g_trace_lightvec, lightNormal, g_trace_lightvec );
				dm += size; // skip to next deluxmap
			}
		}

		return true;
	}

	// go down back side
	return R_RecursiveLightPoint( model, node_child( node, !side, model ), midf, p2f, cv, mid, end );
}

/*
=================
R_LightVec

check bspmodels to get light from
=================
*/
static colorVec R_LightVecInternal( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec )
{
	if( lspot )
		VectorClear( lspot );

	if( lvec )
		VectorClear( lvec );

	if( !tr.worldmodel || !tr.worldmodel->lightdata )
		return (colorVec){ 255, 255, 255, 0 };

	float last_fraction = 1.0f;
	int max_ents = r_lighting_extended.value ? MAX_PHYSENTS : 1; // get light from bmodels too
	colorVec light = { 0 };

	// check all the bsp-models
	for( int i = 0; i < max_ents; i++ )
	{
		const physent_t *pe = gEngfuncs.EV_GetPhysent( i );

		if( !pe )
			break;

		if( !pe->model || pe->model->type != mod_brush )
			continue; // skip non-bsp models

		mnode_t *pnodes = &pe->model->nodes[pe->model->hulls[0].firstclipnode];
		vec3_t offset, start_l, end_l;

		VectorSubtract( pe->model->hulls[0].clip_mins, vec3_origin, offset );
		VectorAdd( offset, pe->origin, offset );
		VectorSubtract( start, offset, start_l );
		VectorSubtract( end, offset, end_l );

		// rotate start and end into the models frame of reference
		if( !VectorIsNull( pe->angles ))
		{
			matrix4x4 matrix;
			Matrix4x4_CreateFromEntity( matrix, pe->angles, offset, 1.0f );
			Matrix4x4_VectorITransform( matrix, start, start_l );
			Matrix4x4_VectorITransform( matrix, end, end_l );
		}

		VectorClear( g_trace_lightspot );
		VectorClear( g_trace_lightvec );
		g_trace_fraction = 1.0f;

		colorVec cv;
		if( !R_RecursiveLightPoint( pe->model, pnodes, 0.0f, 1.0f, &cv, start_l, end_l ))
			continue;	// didn't hit anything

		if( g_trace_fraction < last_fraction )
		{
			if( lspot ) VectorCopy( g_trace_lightspot, lspot );
			if( lvec ) VectorNormalize2( g_trace_lightvec, lvec );

			light.r = Q_min(( cv.r >> 8 ), 255 );
			light.g = Q_min(( cv.g >> 8 ), 255 );
			light.b = Q_min(( cv.b >> 8 ), 255 );
			last_fraction = g_trace_fraction;

			if(( light.r + light.g + light.b ) != 0 )
				break; // we get light now
		}
	}

	return light;
}

/*
=================
R_LightVec

check bspmodels to get light from
=================
*/
colorVec R_LightVec( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec )
{
	colorVec	light = R_LightVecInternal( start, end, lspot, lvec );

	if( r_lighting_extended.value && lspot != NULL && lvec != NULL )
	{
		// trying to get light from ceiling (but ignore gradient analyze)
		if(( light.r + light.g + light.b ) == 0 )
			return R_LightVecInternal( end, start, lspot, lvec );
	}

	return light;
}

/*
=================
R_LightPoint

light from floor
=================
*/
colorVec R_LightPoint( const vec3_t p0 )
{
	vec3_t	p1;

	VectorSet( p1, p0[0], p0[1], p0[2] - 2048.0f );

	return R_LightVec( p0, p1, NULL, NULL );
}
