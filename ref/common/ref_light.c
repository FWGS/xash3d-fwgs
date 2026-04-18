/*
ref_light.c - shared light code
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

#include "ref_common.h"
#include "xash3d_mathlib.h"
#include "enginefeatures.h"
#include "pm_local.h"

CVAR_DEFINE_AUTO( r_dlight_virtual_radius, "3", FCVAR_GLCONFIG, "increase dlight radius virtually by this amount" );
CVAR_DEFINE_AUTO( r_lighting_extended, "1", FCVAR_GLCONFIG, "allow to get lighting from world and bmodels" );

/*
==================
CL_RunLightStyles

==================
*/
void CL_RunLightStyles( lightstyle_t *ls )
{
	const model_t *world = gp_cl->models[1];
	int i;
	float frametime = gp_cl->time - gp_cl->oldtime;

	if( !world )
		return;

	if( r_fullbright->value || !world->lightdata )
	{
		for( i = 0; i < MAX_LIGHTSTYLES; i++ )
			g_lightstylevalue[i] = 256 * 256;
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
			g_lightstylevalue[i] = 256;
			continue;
		}
		else if( ls[i].length == 1 )
		{
			// single length style so don't bother interpolating
			g_lightstylevalue[i] = ls[i].map[0] * 22;
			continue;
		}

		flight = (int)Q_floor( ls[i].time * 10 );

		if( !ls[i].interp || !cl_lightstyle_lerping->value )
		{
			g_lightstylevalue[i] = ls[i].map[flight % ls[i].length] * 22;
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

		g_lightstylevalue[i] = (int)l;
	}
}

/*
=============
R_MarkLights
=============
*/
static void R_MarkLights( const dlight_t *light, int bit, const mnode_t *node, model_t *model, int dlightframecount )
{
	const float virtual_radius = light->radius * Q_max( 1.0f, r_dlight_virtual_radius.value );
	const float maxdist = light->radius * light->radius;
start:
	if( !node || node->contents < 0 )
		return;

	float dist = PlaneDiff( light->origin, node->plane );

	if( dist > virtual_radius )
	{
		node = node_child( node, 0, model );
		goto start;
	}

	if( dist < -virtual_radius )
	{
		node = node_child( node, 1, model );
		goto start;
	}

	const float dist_sq = dist * dist;

	// mark the polygons
	int firstsurface = node_firstsurface( node, model );
	int numsurfaces = node_numsurfaces( node, model );

	for( int i = 0; i < numsurfaces && dist_sq < maxdist; i++ )
	{
		vec3_t impact;
		float s, t, l;
		msurface_t *surf = &model->surfaces[firstsurface + i];
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

		if( s * s + t * t + dist_sq >= maxdist )
			continue;

		if( surf->dlightframe != dlightframecount )
		{
			surf->dlightbits = bit;
			surf->dlightframe = dlightframecount;
		}
		else surf->dlightbits |= bit;
	}

	R_MarkLights( light, bit, node_child( node, 0, model ), model, dlightframecount );
	node = node_child( node, 1, model );
	goto start;
}

/*
=============
R_PushDlights
=============
*/
int R_PushDlights( model_t *model, int framecount )
{
	if( !model )
		return framecount;

	for( int i = 0; i < MAX_DLIGHTS; i++ )
	{
		const dlight_t *l = &gp_dlights[i];

		if( l->die < gp_cl->time || !l->radius )
			continue;

		R_MarkLights( l, 1 << i, model->nodes, model, framecount );
	}

	return framecount;
}

void R_PushDlightsForBmodel( model_t *model, int framecount, const matrix4x4 object_matrix )
{
	for( int i = 0; i < MAX_DLIGHTS; i++ )
	{
		dlight_t *l = &gp_dlights[i];

		if( l->die < gp_cl->time || !l->radius )
			continue;

		vec3_t oldorigin;
		VectorCopy( l->origin, oldorigin );

		Matrix4x4_VectorITransform( object_matrix, oldorigin, l->origin );
		R_MarkLights( l, 1 << i, model->nodes + model->hulls[0].firstclipnode, model, framecount );

		VectorCopy( oldorigin, l->origin );
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
			uint	scale = g_lightstylevalue[surf->styles[map]];

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
R_LightVecInternal

check bspmodels to get light from
=================
*/
static colorVec R_LightVecInternal( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec )
{
	if( lspot )
		VectorClear( lspot );

	if( lvec )
		VectorClear( lvec );

	if( !gp_cl->models[1] || !gp_cl->models[1]->lightdata )
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
	colorVec light = R_LightVecInternal( start, end, lspot, lvec );

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
	vec3_t p1;

	VectorSet( p1, p0[0], p0[1], p0[2] - 2048.0f );

	return R_LightVec( p0, p1, NULL, NULL );
}

/*
=================
R_GatherPlayerLight

get light level of an entity and set it as player's light level
=================
*/
void R_GatherPlayerLight( cl_entity_t *view )
{
	colorVec c = R_LightPoint( view->origin );

	gEngfuncs.SetLocalLightLevel(( c.r + c.g + c.b ) / 3 );
}

/*
===============
R_EntityDynamicLight

===============
*/
void R_EntityDynamicLight( cl_entity_t *ent, alight_t *plight, qboolean draw_world, double time, vec3_t lightspot, vec3_t lightvec )
{
	movevars_t	*mv = gp_movevars;
	vec3_t		lightDir, vecSrc, vecEnd;
	vec3_t		origin, dist, finalLight;
	float		add, radius, total;
	colorVec		light;
	uint		lnum;
	dlight_t		*dl;

	if( !plight || !ent )
		return;

	if( !draw_world || r_fullbright->value || FBitSet( ent->curstate.effects, EF_FULLBRIGHT ))
	{
		plight->shadelight = 0;
		plight->ambientlight = 192;

		VectorSet( plight->plightvec, 0.0f, 0.0f, -1.0f );
		VectorSet( plight->color, 1.0f, 1.0f, 1.0f );
		return;
	}

	// determine plane to get lightvalues from: ceil or floor
	if( FBitSet( ent->curstate.effects, EF_INVLIGHT ))
		VectorSet( lightDir, 0.0f, 0.0f, 1.0f );
	else
		VectorSet( lightDir, 0.0f, 0.0f, -1.0f );

	VectorCopy( ent->origin, origin );

	VectorSet( vecSrc, origin[0], origin[1], origin[2] - lightDir[2] * 8.0f );
	light.r = light.g = light.b = light.a = 0;

	if(( mv->skycolor[0] + mv->skycolor[1] + mv->skycolor[2] ) != 0 )
	{
		msurface_t	*psurf = NULL;
		pmtrace_t		trace;
		vec3_t skyvec;

		if( FBitSet( gp_host->features, ENGINE_WRITE_LARGE_COORD ))
			VectorScale( mv->skyvec, 65536.0f, skyvec );
		else
			VectorScale( mv->skyvec, 8192.0f, skyvec );

		VectorSubtract( origin, skyvec, vecEnd );

		trace = gEngfuncs.CL_TraceLine( vecSrc, vecEnd, PM_WORLD_ONLY );
		if( trace.ent > 0 )
			psurf = gEngfuncs.EV_TraceSurface( trace.ent, vecSrc, vecEnd );
		else
			psurf = gEngfuncs.EV_TraceSurface( 0, vecSrc, vecEnd );

		if(( ent->model->type == mod_studio && FBitSet( ent->model->flags, STUDIO_FORCE_SKYLIGHT ))
			|| ( psurf && FBitSet( psurf->flags, SURF_DRAWSKY )))
		{
			VectorCopy( mv->skyvec, lightDir );

			light.r = mv->skycolor[0];
			light.g = mv->skycolor[1];
			light.b = mv->skycolor[2];
		}
	}

	if(( light.r + light.g + light.b ) == 0 )
	{
		colorVec	gcolor;
		float	grad[4];

		VectorScale( lightDir, 2048.0f, vecEnd );
		VectorAdd( vecEnd, vecSrc, vecEnd );

		light = R_LightVec( vecSrc, vecEnd, lightspot, lightvec );

		if( VectorIsNull( lightvec ))
		{
			vecSrc[0] -= 16.0f;
			vecSrc[1] -= 16.0f;
			vecEnd[0] -= 16.0f;
			vecEnd[1] -= 16.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[0] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			vecSrc[0] += 32.0f;
			vecEnd[0] += 32.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[1] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			vecSrc[1] += 32.0f;
			vecEnd[1] += 32.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[2] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			vecSrc[0] -= 32.0f;
			vecEnd[0] -= 32.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[3] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			lightDir[0] = grad[0] - grad[1] - grad[2] + grad[3];
			lightDir[1] = grad[1] + grad[0] - grad[2] - grad[3];
			VectorNormalize( lightDir );
		}
		else
		{
			VectorCopy( lightvec, lightDir );
		}
	}

	if( ent->curstate.renderfx == kRenderFxLightMultiplier && ent->curstate.iuser4 != 10 )
	{
		light.r *= ent->curstate.iuser4 / 10.0f;
		light.g *= ent->curstate.iuser4 / 10.0f;
		light.b *= ent->curstate.iuser4 / 10.0f;
	}

	VectorSet( finalLight, light.r, light.g, light.b );
	ent->cvFloorColor = light;

	total = Q_max( Q_max( light.r, light.g ), light.b );
	if( total == 0.0f )
		total = 1.0f;

	// scale lightdir by light intentsity
	VectorScale( lightDir, total, lightDir );

	for( lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		dl = &gp_dlights[lnum];

		if( dl->die < time || !r_dynamic->value )
			continue;

		VectorSubtract( ent->origin, dl->origin, dist );

		radius = VectorLength( dist );
		add = ( dl->radius - radius );

		if( add > 0.0f )
		{
			total += add;

			if( radius > 1.0f )
				VectorScale( dist, ( add / radius ), dist );
			else
				VectorScale( dist, add, dist );

			VectorAdd( lightDir, dist, lightDir );

			finalLight[0] += dl->color.r * ( add / 256.0f );
			finalLight[1] += dl->color.g * ( add / 256.0f );
			finalLight[2] += dl->color.b * ( add / 256.0f );
		}
	}

	if( ent->model->type == mod_alias )
		add = 0.9f;
	else if( ent->model->type == mod_studio && FBitSet( ent->model->flags, STUDIO_AMBIENT_LIGHT ))
		add = 0.6f;
	else
		add = bound( 0.75f, v_direct->value, 1.0f );

	VectorScale( lightDir, add, lightDir );

	plight->shadelight = VectorLength( lightDir );
	plight->ambientlight = total - plight->shadelight;

	total = Q_max( Q_max( finalLight[0], finalLight[1] ), finalLight[2] );

	if( total > 0.0f )
	{
		plight->color[0] = finalLight[0] * ( 1.0f / total );
		plight->color[1] = finalLight[1] * ( 1.0f / total );
		plight->color[2] = finalLight[2] * ( 1.0f / total );
	}
	else
		VectorSet( plight->color, 1.0f, 1.0f, 1.0f );

	if( plight->ambientlight > 128 )
		plight->ambientlight = 128;

	if( plight->ambientlight + plight->shadelight > 255 )
		plight->shadelight = 255 - plight->ambientlight;

	VectorNormalize2( lightDir, plight->plightvec );
}

/*
================
R_SetCacheState
================
*/
void R_UpdateSurfaceCachedLight( msurface_t *surf )
{
	for( int i = 0; i < MAXLIGHTMAPS && surf->styles[i] != 255; i++ )
		surf->cached_light[i] = g_lightstylevalue[surf->styles[i]];
}
