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
	int i;
	float frametime = (gp_cl->time - gp_cl->oldtime);

	if( !WORLDMODEL )
		return;

	if( !WORLDMODEL->lightdata )
	{
		for( i = 0; i < MAX_LIGHTSTYLES; i++ )
			tr.lightstylevalue[i] = 256 * 256;
		return;
	}

	// light animations
	// 'm' is normal light, 'a' is no light, 'z' is double bright
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		int k, flight, clight;
		float l, lerpfrac, backlerp;

		if( !gp_cl->paused && frametime <= 0.1f )
			ls[i].time += frametime; // evaluate local time

		if( !ls[i].length )
		{
			tr.lightstylevalue[i] = 256;
			continue;
		}
		else if( ls[i].length == 1 )
		{
			// single length style so don't bother interpolating
			tr.lightstylevalue[i] = ( ls[i].map[0] ) * 22;
			continue;
		}

		flight = (int)Q_floor( ls[i].time * 10 );

		if( !ls[i].interp || !cl_lightstyle_lerping->value )
		{
			tr.lightstylevalue[i] = ls[i].map[flight%ls[i].length] * 22;
			continue;
		}

		clight = (int)Q_ceil( ls[i].time * 10 );
		lerpfrac = ( ls[i].time * 10 ) - flight;
		backlerp = 1.0f - lerpfrac;

		// interpolate animating light
		// frame just gone
		k = ls[i].map[flight % ls[i].length];
		l = (float)( k * 22.0f ) * backlerp;

		// upcoming frame
		k = ls[i].map[clight % ls[i].length];
		l += (float)( k * 22.0f ) * lerpfrac;

		tr.lightstylevalue[i] = (int)l;
	}
}

/*
=============
R_MarkLights
=============
*/
void R_MarkLights( const dlight_t *light, int bit, const mnode_t *node )
{
	const float virtual_radius = light->radius * Q_max( 1.0f, r_dlight_virtual_radius.value );
	const float maxdist = light->radius * light->radius;
	float dist;
	int i;
	mnode_t *children[2];
	int firstsurface, numsurfaces;

start:

	if( !node || node->contents < 0 )
		return;

	dist = PlaneDiff( light->origin, node->plane );

	node_children( children, node, RI.currentmodel );

	if( dist > virtual_radius )
	{
		node = children[0];
		goto start;
	}

	if( dist < -virtual_radius )
	{
		node = children[1];
		goto start;
	}

	// mark the polygons
	firstsurface = node_firstsurface( node, RI.currentmodel );
	numsurfaces = node_numsurfaces( node, RI.currentmodel );

	for( i = 0; i < numsurfaces; i++ )
	{
		vec3_t impact;
		float s, t, l;
		msurface_t *surf = &RI.currentmodel->surfaces[firstsurface + i];
		const mextrasurf_t *info = surf->info;

		if( surf->plane->type < 3 )
		{
			VectorCopy( light->origin, impact );
			impact[surf->plane->type] -= dist;
		}
		else VectorMA( light->origin, -dist, surf->plane->normal, impact );

		// a1ba: the fix was taken from JoeQuake, which traces back to FitzQuake,
		// which attributes it to LadyHavoc (Darkplaces author)
		// clamp center of light to corner and check brightness
		l = DotProduct( impact, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		s = l + 0.5;
		s = bound( 0, s, info->lightextents[0] );
		s = l - s;

		l = DotProduct( impact, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
		t = l + 0.5;
		t = bound( 0, t, info->lightextents[1] );
		t = l - t;

		if( s * s + t * t + dist * dist >= maxdist )
			continue;

		if( surf->dlightframe != tr.dlightframecount )
		{
			surf->dlightbits = bit;
			surf->dlightframe = tr.dlightframecount;
		}
		else surf->dlightbits |= bit;
	}

	R_MarkLights( light, bit, children[0] );
	R_MarkLights( light, bit, children[1] );
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights( void )
{
	int	i;

	tr.dlightframecount = tr.framecount;

	RI.currententity = CL_GetEntityByIndex( 0 );

	// no world -- no dlights
	if( !RI.currententity )
		return;

	RI.currentmodel = RI.currententity->model;

	for( i = 0; i < MAX_DLIGHTS; i++ )
	{
		dlight_t *l = &tr.dlights[i];

		if( l->die < gp_cl->time || !l->radius )
			continue;

		if( GL_FrustumCullSphere( &RI.frustum, l->origin, l->radius, 15 ))
			continue;

		R_MarkLights( l, 1<<i, RI.currentmodel->nodes );
	}
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
	float		front, back, frac, midf;
	int		i, map, side, size;
	float		ds, dt, s, t;
	int		sample_size;
	color24		*lm, *dm;
	mextrasurf_t	*info;
	msurface_t	*surf;
	mtexinfo_t	*tex;
	matrix3x4		tbn;
	vec3_t		mid;
	mnode_t *children[2];
	int firstsurface, numsurfaces;

	// didn't hit anything
	if( !node || node->contents < 0 )
	{
		cv->r = cv->g = cv->b = cv->a = 0;
		return false;
	}

	node_children( children, node, model );
	firstsurface = node_firstsurface( node, model );
	numsurfaces = node_numsurfaces( node, model );

	// calculate mid point
	front = PlaneDiff( start, node->plane );
	back = PlaneDiff( end, node->plane );

	side = front < 0;
	if(( back < 0 ) == side )
		return R_RecursiveLightPoint( model, children[side], p1f, p2f, cv, start, end );

	frac = front / ( front - back );

	VectorLerp( start, frac, end, mid );
	midf = p1f + ( p2f - p1f ) * frac;

	// co down front side
	if( R_RecursiveLightPoint( model, children[side], p1f, midf, cv, start, mid ))
		return true; // hit something

	if(( back < 0 ) == side )
	{
		cv->r = cv->g = cv->b = cv->a = 0;
		return false; // didn't hit anything
	}

	// check for impact on this node
	surf = model->surfaces + firstsurface;
	VectorCopy( mid, g_trace_lightspot );

	for( i = 0; i < numsurfaces; i++, surf++ )
	{
		int	smax, tmax;

		tex = surf->texinfo;
		info = surf->info;

		if( FBitSet( surf->flags, SURF_DRAWTILED ))
			continue;	// no lightmaps

		s = DotProduct( mid, info->lmvecs[0] ) + info->lmvecs[0][3];
		t = DotProduct( mid, info->lmvecs[1] ) + info->lmvecs[1][3];

		if( s < info->lightmapmins[0] || t < info->lightmapmins[1] )
			continue;

		ds = s - info->lightmapmins[0];
		dt = t - info->lightmapmins[1];

		if ( ds > info->lightextents[0] || dt > info->lightextents[1] )
			continue;

		cv->r = cv->g = cv->b = cv->a = 0;

		if( !surf->samples )
			return true;

		sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
		smax = (info->lightextents[0] / sample_size) + 1;
		tmax = (info->lightextents[1] / sample_size) + 1;
		ds /= sample_size;
		dt /= sample_size;

		lm = surf->samples + Q_rint( dt ) * smax + Q_rint( ds );
		g_trace_fraction = midf;
		size = smax * tmax;
		dm = NULL;

		if( surf->info->deluxemap )
		{
			vec3_t	faceNormal;

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
			dm = surf->info->deluxemap + Q_rint( dt ) * smax + Q_rint( ds );
		}

		for( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
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
	return R_RecursiveLightPoint( model, children[!side], midf, p2f, cv, mid, end );
}

/*
=================
R_LightVec

check bspmodels to get light from
=================
*/
static colorVec R_LightVecInternal( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec )
{
	float	last_fraction;
	int	i, maxEnts = 1;
	colorVec	light, cv;

	if( lspot ) VectorClear( lspot );
	if( lvec ) VectorClear( lvec );

	if( WORLDMODEL && WORLDMODEL->lightdata )
	{
		light.r = light.g = light.b = light.a = 0;
		last_fraction = 1.0f;

		// get light from bmodels too
		if( r_lighting_extended.value )
			maxEnts = MAX_PHYSENTS;

		// check all the bsp-models
		for( i = 0; i < maxEnts; i++ )
		{
			physent_t	*pe = gEngfuncs.EV_GetPhysent( i );
			vec3_t	offset, start_l, end_l;
			mnode_t	*pnodes;
			matrix4x4	matrix;

			if( !pe )
				break;

			if( !pe->model || pe->model->type != mod_brush )
				continue; // skip non-bsp models

			pnodes = &pe->model->nodes[pe->model->hulls[0].firstclipnode];
			VectorSubtract( pe->model->hulls[0].clip_mins, vec3_origin, offset );
			VectorAdd( offset, pe->origin, offset );
			VectorSubtract( start, offset, start_l );
			VectorSubtract( end, offset, end_l );

			// rotate start and end into the models frame of reference
			if( !VectorIsNull( pe->angles ))
			{
				Matrix4x4_CreateFromEntity( matrix, pe->angles, offset, 1.0f );
				Matrix4x4_VectorITransform( matrix, start, start_l );
				Matrix4x4_VectorITransform( matrix, end, end_l );
			}

			VectorClear( g_trace_lightspot );
			VectorClear( g_trace_lightvec );
			g_trace_fraction = 1.0f;

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
	}
	else
	{
		light.r = light.g = light.b = 255;
		light.a = 0;
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
