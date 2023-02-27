/*
cl_part.c - particles and tracers
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

#include "vk_rpart.h"
#include "vk_triapi.h"
#include "camera.h"
#include "vk_textures.h" // tglob.particleTexture
#include "vk_common.h"
#include "vk_sprite.h" // R_GetSpriteTexture

#include "xash3d_types.h" // vec3_t for r_efx.h
#include "const.h" // color24 for r_efx.h
#include "r_efx.h"
#include "event_flags.h"
#include "entity_types.h"
#include "triangleapi.h"
#include "pm_local.h"
#include "studio.h"
#include "client/cl_tent.h" // TriColor4ub
#include "pm_movevars.h" // movevars_t

static float gTracerSize[11] = { 1.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static color24 gTracerColors[] =
{
{ 255, 255, 255 },		// White
{ 255, 0, 0 },		// Red
{ 0, 255, 0 },		// Green
{ 0, 0, 255 },		// Blue
{ 0, 0, 0 },		// Tracer default, filled in from cvars, etc.
{ 255, 167, 17 },		// Yellow-orange sparks
{ 255, 130, 90 },		// Yellowish streaks (garg)
{ 55, 60, 144 },		// Blue egon streak
{ 255, 130, 90 },		// More Yellowish streaks (garg)
{ 255, 140, 90 },		// More Yellowish streaks (garg)
{ 200, 130, 90 },		// More red streaks (garg)
{ 255, 120, 70 },		// Darker red streaks (garg)
};

/*
================
CL_DrawParticles

update particle color, position, free expired and draw it
================
*/
void CL_DrawParticles( double frametime, particle_t *cl_active_particles, float partsize )
{
	particle_t	*p;
	vec3_t		right, up;
	color24		*pColor;
	int		alpha;
	float		size;

	if( !cl_active_particles )
		return;	// nothing to draw?

	TriRenderMode( kRenderTransAlpha );
	TriSetTexture( tglob.particleTexture );

	TriBegin( TRI_QUADS );

	for( p = cl_active_particles; p; p = p->next )
	{
		if(( p->type != pt_blob ) || ( p->packedColor == 255 ))
		{
			size = partsize; // get initial size of particle

			// scale up to keep particles from disappearing
			// FIXME are these really the same?
			vec3_t cull_vforward, cull_vup, cull_vright;
			VectorCopy(g_camera.vforward, cull_vforward);
			VectorCopy(g_camera.vup, cull_vup);
			VectorCopy(g_camera.vright, cull_vright);
			size += (p->org[0] - g_camera.vieworg[0]) * cull_vforward[0];
			size += (p->org[1] - g_camera.vieworg[1]) * cull_vforward[1];
			size += (p->org[2] - g_camera.vieworg[2]) * cull_vforward[2];

			if( size < 20.0f ) size = partsize;
			else size = partsize + size * 0.002f;

			// scale the axes by radius
			VectorScale( cull_vright, size, right );
			VectorScale( cull_vup, size, up );

			p->color = bound( 0, p->color, 255 );
			pColor = gEngine.CL_GetPaletteColor( p->color );

			alpha = 255 * (p->die - gpGlobals->time) * 16.0f;
			if( alpha > 255 || p->type == pt_static )
				alpha = 255;

			TriColor4ub( gEngine.LightToTexGamma( pColor->r ),
				gEngine.LightToTexGamma( pColor->g ),
				gEngine.LightToTexGamma( pColor->b ), alpha );

			TriTexCoord2f( 0.0f, 1.0f );
			TriVertex3f( p->org[0] - right[0] + up[0], p->org[1] - right[1] + up[1], p->org[2] - right[2] + up[2] );
			TriTexCoord2f( 0.0f, 0.0f );
			TriVertex3f( p->org[0] + right[0] + up[0], p->org[1] + right[1] + up[1], p->org[2] + right[2] + up[2] );
			TriTexCoord2f( 1.0f, 0.0f );
			TriVertex3f( p->org[0] + right[0] - up[0], p->org[1] + right[1] - up[1], p->org[2] + right[2] - up[2] );
			TriTexCoord2f( 1.0f, 1.0f );
			TriVertex3f( p->org[0] - right[0] - up[0], p->org[1] - right[1] - up[1], p->org[2] - right[2] - up[2] );
		}

		gEngine.CL_ThinkParticle( frametime, p );
	}

	const vec4_t color = { 1, 1, 1, 1 }; // pColor->r / 255.f, pColor->g / 255.f, pColor->b / 255.f, 1.f };
	TriEndEx( color, "particle");
}

/*
================
CL_CullTracer

check tracer bbox
================
*/
static qboolean CL_CullTracer( particle_t *p, const vec3_t start, const vec3_t end )
{
	vec3_t	mins, maxs;
	int	i;

	// compute the bounding box
	for( i = 0; i < 3; i++ )
	{
		if( start[i] < end[i] )
		{
			mins[i] = start[i];
			maxs[i] = end[i];
		}
		else
		{
			mins[i] = end[i];
			maxs[i] = start[i];
		}

		// don't let it be zero sized
		if( mins[i] == maxs[i] )
		{
			maxs[i] += gTracerSize[p->type] * 2.0f;
		}
	}

	// check bbox
	return false; // FIXME VK R_CullBox( mins, maxs );
}

/*
================
CL_DrawTracers

update tracer color, position, free expired and draw it
================
*/
void CL_DrawTracers( double frametime, particle_t *cl_active_tracers )
{
	float		scale, atten, gravity;
	vec3_t		screenLast, screen;
	vec3_t		start, end, delta;
	particle_t	*p;

	/* FIXME VK
	// update tracer color if this is changed
	if( FBitSet( tracerred->flags|tracergreen->flags|tracerblue->flags|traceralpha->flags, FCVAR_CHANGED ))
	{
		color24 *customColors = &gTracerColors[4];
		customColors->r = (byte)(tracerred->value * traceralpha->value * 255);
		customColors->g = (byte)(tracergreen->value * traceralpha->value * 255);
		customColors->b = (byte)(tracerblue->value * traceralpha->value * 255);
		ClearBits( tracerred->flags, FCVAR_CHANGED );
		ClearBits( tracergreen->flags, FCVAR_CHANGED );
		ClearBits( tracerblue->flags, FCVAR_CHANGED );
		ClearBits( traceralpha->flags, FCVAR_CHANGED );
	}
	*/

	if( !cl_active_tracers )
		return;	// nothing to draw?

	if( !TriSpriteTexture( gEngine.GetDefaultSprite( REF_DOT_SPRITE ), 0 ))
		return;

	TriRenderMode( kRenderTransAdd );

#define MOVEVARS (gEngine.pfnGetMoveVars())
	gravity = frametime * MOVEVARS->gravity;
	scale = 1.0 - (frametime * 0.9);
	if( scale < 0.0f ) scale = 0.0f;

	for( p = cl_active_tracers; p; p = p->next )
	{
		atten = (p->die - gpGlobals->time);
		if( atten > 0.1f ) atten = 0.1f;

		VectorScale( p->vel, ( p->ramp * atten ), delta );
		VectorAdd( p->org, delta, end );
		VectorCopy( p->org, start );

		if( !CL_CullTracer( p, start, end ))
		{
			vec3_t	verts[4], tmp2;
			vec3_t	tmp, normal;
			color24	*pColor;

			// Transform point into screen space
			TriWorldToScreen( start, screen );
			TriWorldToScreen( end, screenLast );

			// build world-space normal to screen-space direction vector
			VectorSubtract( screen, screenLast, tmp );

			// we don't need Z, we're in screen space
			tmp[2] = 0;
			VectorNormalize( tmp );

			vec3_t cull_vforward, cull_vup, cull_vright;
			VectorCopy(g_camera.vforward, cull_vforward);
			VectorCopy(g_camera.vup, cull_vup);
			VectorCopy(g_camera.vright, cull_vright);

			// build point along noraml line (normal is -y, x)
			VectorScale( cull_vup, tmp[0] * gTracerSize[p->type], normal );
			VectorScale( cull_vright, -tmp[1] * gTracerSize[p->type], tmp2 );
			VectorSubtract( normal, tmp2, normal );

			// compute four vertexes
			VectorSubtract( start, normal, verts[0] );
			VectorAdd( start, normal, verts[1] );
			VectorAdd( verts[0], delta, verts[2] );
			VectorAdd( verts[1], delta, verts[3] );

			if( p->color > sizeof( gTracerColors ) / sizeof( color24 ) )
			{
				gEngine.Con_Printf( S_ERROR "UserTracer with color > %d\n", (int)(sizeof( gTracerColors ) / sizeof( color24 )));
				p->color = 0;
			}

			pColor = &gTracerColors[p->color];
			TriColor4ub( pColor->r, pColor->g, pColor->b, p->packedColor );

			TriBegin( TRI_QUADS );
				TriTexCoord2f( 0.0f, 0.8f );
				TriVertex3fv( verts[2] );
				TriTexCoord2f( 1.0f, 0.8f );
				TriVertex3fv( verts[3] );
				TriTexCoord2f( 1.0f, 0.0f );
				TriVertex3fv( verts[1] );
				TriTexCoord2f( 0.0f, 0.0f );
				TriVertex3fv( verts[0] );
			const vec4_t color = { 1, 1, 1, 1 }; //pColor->r / 255.f, pColor->g / 255.f, pColor->b / 255.f, p->packedColor / 255.f };
			TriEndEx( color, "tracer" );
		}

		// evaluate position
		VectorMA( p->org, frametime, p->vel, p->org );

		if( p->type == pt_grav )
		{
			p->vel[0] *= scale;
			p->vel[1] *= scale;
			p->vel[2] -= gravity;

			p->packedColor = 255 * (p->die - gpGlobals->time) * 2;
			if( p->packedColor > 255 ) p->packedColor = 255;
		}
		else if( p->type == pt_slowgrav )
		{
			p->vel[2] = gravity * 0.05f;
		}
	}
}

/*
===============
CL_DrawParticlesExternal

allow to draw effects from custom renderer
===============
*/

/* FIXME VK
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime )
{
	ref_instance_t	oldRI = RI;

	memcpy( &oldRI, &RI, sizeof( ref_instance_t ));
	R_SetupRefParams( rvp );
	R_SetupFrustum();
	R_SetuTri( false );	// don't touch GL-states
	tr.frametime = frametime;

	gEngfuncs.CL_DrawEFX( frametime, trans_pass );

	// restore internal state
	memcpy( &RI, &oldRI, sizeof( ref_instance_t ));
}
*/
