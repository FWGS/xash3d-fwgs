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

#include "r_local.h"
#include "pm_local.h"
#include "studio.h"
#include "xash3d_mathlib.h"
#include "ref_params.h"

//unused, need refactor
unsigned		blocklights[10240];

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
void GAME_EXPORT CL_RunLightStyles( void )
{
	int		i, k, flight, clight;
	float		l, lerpfrac, backlerp;
	float		frametime = (gpGlobals->time -   gpGlobals->oldtime);
	float		scale;
	lightstyle_t	*ls;

	if( !WORLDMODEL ) return;

	scale = 1; //r_lighting_modulate->value;

	// light animations
	// 'm' is normal light, 'a' is no light, 'z' is double bright
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		ls = gEngfuncs.GetLightStyle( i );
		if( !WORLDMODEL->lightdata )
		{
			tr.lightstylevalue[i] = 256 * 256;
			continue;
		}

		if( !ENGINE_GET_PARM( PARAM_GAMEPAUSED ) && frametime <= 0.1f )
			ls->time += frametime; // evaluate local time

		flight = (int)Q_floor( ls->time * 10 );
		clight = (int)Q_ceil( ls->time * 10 );
		lerpfrac = ( ls->time * 10 ) - flight;
		backlerp = 1.0f - lerpfrac;

		if( !ls->length )
		{
			tr.lightstylevalue[i] = 256 * scale;
			continue;
		}
		else if( ls->length == 1 )
		{
			// single length style so don't bother interpolating
			tr.lightstylevalue[i] = ls->map[0] * 22 * scale;
			continue;
		}
		else if( !ls->interp ) // || !CVAR_TO_BOOL( cl_lightstyle_lerping ))
		{
			tr.lightstylevalue[i] = ls->map[flight%ls->length] * 22 * scale;
			continue;
		}

		// interpolate animating light
		// frame just gone
		k = ls->map[flight % ls->length];
		l = (float)( k * 22.0f ) * backlerp;

		// upcoming frame
		k = ls->map[clight % ls->length];
		l += (float)( k * 22.0f ) * lerpfrac;

		tr.lightstylevalue[i] = (int)l * scale;
	}
}

/*
=============
R_MarkLights
=============
*/
void R_MarkLights( dlight_t *light, int bit, mnode_t *node )
{
	float		dist;
	msurface_t	*surf;
	int		i;
	
	if( !node || node->contents < 0 )
		return;

	dist = PlaneDiff( light->origin, node->plane );

	if( dist > light->radius )
	{
		R_MarkLights( light, bit, node->children[0] );
		return;
	}
	if( dist < -light->radius )
	{
		R_MarkLights( light, bit, node->children[1] );
		return;
	}
		
	// mark the polygons
	surf = RI.currentmodel->surfaces + node->firstsurface;

	for( i = 0; i < node->numsurfaces; i++, surf++ )
	{
		if( !BoundsAndSphereIntersect( surf->info->mins, surf->info->maxs, light->origin, light->radius ))
			continue;	// no intersection

		if( surf->dlightframe !=  tr.framecount )//tr.dlightframecount )
		{
			surf->dlightbits = 0;
			surf->dlightframe = tr.framecount; //tr.dlightframecount;
		}
		surf->dlightbits |= bit;
	}

	R_MarkLights( light, bit, node->children[0] );
	R_MarkLights( light, bit, node->children[1] );
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights( void )
{
	dlight_t	*l;
	int	i;

	tr.dlightframecount = tr.framecount;

	RI.currententity = gEngfuncs.GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;

	for( i = 0; i < MAX_DLIGHTS; i++, l++ )
	{
		l = gEngfuncs.GetDynamicLight( i );

		if( l->die < gpGlobals->time || !l->radius )
			continue;

		//if( GL_FrustumCullSphere( &RI.frustum, l->origin, l->radius, 15 ))
			//continue;

		R_MarkLights( l, 1<<i, RI.currentmodel->nodes );
	}
}

/*
=============
R_CountDlights
=============
*/
int R_CountDlights( void )
{
	dlight_t	*l;
	int	i, numDlights = 0;

	for( i = 0; i < MAX_DLIGHTS; i++ )
	{
		l = gEngfuncs.GetDynamicLight( i );

		if( l->die < gpGlobals->time || !l->radius )
			continue;

		numDlights++;
	}

	return numDlights;
}

/*
=============
R_CountSurfaceDlights
=============
*/
int R_CountSurfaceDlights( msurface_t *surf )
{
	int	i, numDlights = 0;

	for( i = 0; i < MAX_DLIGHTS; i++ )
	{
		if(!( surf->dlightbits & BIT( i )))
			continue;	// not lit by this light

		numDlights++;
	}

	return numDlights;
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

	// didn't hit anything
	if( !node || node->contents < 0 )
	{
		cv->r = cv->g = cv->b = cv->a = 0;
		return false;
	}

	// calculate mid point
	front = PlaneDiff( start, node->plane );
	back = PlaneDiff( end, node->plane );

	side = front < 0;
	if(( back < 0 ) == side )
		return R_RecursiveLightPoint( model, node->children[side], p1f, p2f, cv, start, end );

	frac = front / ( front - back );

	VectorLerp( start, frac, end, mid );
	midf = p1f + ( p2f - p1f ) * frac;

	// co down front side	
	if( R_RecursiveLightPoint( model, node->children[side], p1f, midf, cv, start, mid ))
		return true; // hit something

	if(( back < 0 ) == side )
	{
		cv->r = cv->g = cv->b = cv->a = 0;
		return false; // didn't hit anything
	}

	// check for impact on this node
	surf = model->surfaces + node->firstsurface;
	VectorCopy( mid, g_trace_lightspot );

	for( i = 0; i < node->numsurfaces; i++, surf++ )
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

			if( tr.ignore_lightgamma )
			{
				cv->r += lm->r * scale * 2.5; // scale;
				cv->g += lm->g * scale * 2.5; // scale;
				cv->b += lm->b * scale * 2.5; // scale;
			}
			else
			{
				cv->r += gEngfuncs.LightToTexGamma( lm->r ) * scale;
				cv->g += gEngfuncs.LightToTexGamma( lm->g ) * scale;
				cv->b += gEngfuncs.LightToTexGamma( lm->b ) * scale;
			}
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
	return R_RecursiveLightPoint( model, node->children[!side], midf, p2f, cv, mid, end );
}

/*
=================
R_LightVec

check bspmodels to get light from
=================
*/
colorVec R_LightVecInternal( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec )
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
		//if( CVAR_TO_BOOL( r_lighting_extended ))
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
				light.r = Q_min(( cv.r >> 7 ), 255 );
				light.g = Q_min(( cv.g >> 7 ), 255 );
				light.b = Q_min(( cv.b >> 7 ), 255 );
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
colorVec GAME_EXPORT R_LightVec( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec )
{
	colorVec	light = R_LightVecInternal( start, end, lspot, lvec );

	//light.r = light.g = light.b = 255;

	if( lspot != NULL && lvec != NULL ) // CVAR_TO_BOOL( r_lighting_extended ) &&
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
colorVec GAME_EXPORT R_LightPoint( const vec3_t p0 )
{
	vec3_t	p1;

	VectorSet( p1, p0[0], p0[1], p0[2] - 2048.0f );

	return R_LightVec( p0, p1, NULL, NULL );
}
