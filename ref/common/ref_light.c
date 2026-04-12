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
#include "pm_local.h"

CVAR_DEFINE_AUTO( r_dlight_virtual_radius, "3", FCVAR_GLCONFIG, "increase dlight radius virtually by this amount" );

/*
==================
CL_RunLightStyles_

==================
*/
void CL_RunLightStyles_( lightstyle_t *ls, int *lightstylevalue )
{
	const model_t *world = gp_cl->models[1];
	int i;
	float frametime = gp_cl->time - gp_cl->oldtime;

	if( !world )
		return;

	if( r_fullbright->value || !world->lightdata )
	{
		for( i = 0; i < MAX_LIGHTSTYLES; i++ )
			lightstylevalue[i] = 256 * 256;
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
			lightstylevalue[i] = 256;
			continue;
		}
		else if( ls[i].length == 1 )
		{
			// single length style so don't bother interpolating
			lightstylevalue[i] = ls[i].map[0] * 22;
			continue;
		}

		flight = (int)Q_floor( ls[i].time * 10 );

		if( !ls[i].interp || !cl_lightstyle_lerping->value )
		{
			lightstylevalue[i] = ls[i].map[flight % ls[i].length] * 22;
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

		lightstylevalue[i] = (int)l;
	}
}

/*
=============
R_MarkLights
=============
*/
void R_MarkLights( const dlight_t *light, int bit, const mnode_t *node, model_t *model, int dlightframecount )
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
