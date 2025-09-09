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

#include "r_local.h"
#include "r_efx.h"
#include "event_flags.h"
#include "entity_types.h"
#include "triangleapi.h"
#include "pm_local.h"
#include "studio.h"

static float   gTracerSize[11] = { 1.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static color24 gTracerColors[] =
{
	{ 255, 255, 255 }, // White
	{ 255, 0, 0 },     // Red
	{ 0, 255, 0 },     // Green
	{ 0, 0, 255 },     // Blue
	{ 0, 0, 0 },       // Tracer default, filled in from cvars, etc.
	{ 255, 167, 17 },  // Yellow-orange sparks
	{ 255, 130, 90 },  // Yellowish streaks (garg)
	{ 55, 60, 144 },   // Blue egon streak
	{ 255, 130, 90 },  // More Yellowish streaks (garg)
	{ 255, 140, 90 },  // More Yellowish streaks (garg)
	{ 200, 130, 90 },  // More red streaks (garg)
	{ 255, 120, 70 },  // Darker red streaks (garg)
};

/*
================
CL_DrawParticles

update particle color, position, free expired and draw it
================
*/
void GAME_EXPORT CL_DrawParticles( double frametime, particle_t *cl_active_particles, float partsize )
{
	particle_t *p;
	vec3_t     right, up;
	color24    color;
	int        alpha;
	float      size;

	if( !cl_active_particles )
		return; // nothing to draw?

	// pglEnable( GL_BLEND );
	// pglDisable( GL_ALPHA_TEST );
	// pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	GL_SetRenderMode( kRenderTransAdd );

	GL_Bind( XASH_TEXTURE0, tr.particleTexture );
	// pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	// pglDepthMask( GL_FALSE );

	for( p = cl_active_particles; p; p = p->next )
	{
		if(( p->type != pt_blob ) || ( p->packedColor == 255 ))
		{
			size = partsize; // get initial size of particle

			// scale up to keep particles from disappearing
			size += ( p->org[0] - RI.vieworg[0] ) * RI.cull_vforward[0];
			size += ( p->org[1] - RI.vieworg[1] ) * RI.cull_vforward[1];
			size += ( p->org[2] - RI.vieworg[2] ) * RI.cull_vforward[2];

			if( size < 20.0f )
				size = partsize;
			else
				size = partsize + size * 0.002f;

			// scale the axes by radius
			VectorScale( RI.cull_vright, size, right );
			VectorScale( RI.cull_vup, size, up );

			p->color = bound( 0, p->color, 255 );
			color = tr.palette[p->color];

			alpha = 255 * ( p->die - gp_cl->time ) * 16.0f;
			if( alpha > 255 || p->type == pt_static )
				alpha = 255;

			// TriColor4ub( LightToTexGamma( color.r ),
			//	LightToTexGamma( color.g ),
			//		LightToTexGamma( color.b ), alpha );
			// TriBrightness( alpha / 255.0f );
			_TriColor4f( 1.0f * alpha / 255 / 255 * color.r, 1.0f * alpha / 255 / 255 * color.g, 1.0f * alpha / 255 / 255 * color.b, 1.0f );

			TriBegin( TRI_QUADS );
			TriTexCoord2f( 0.0f, 1.0f );
			TriVertex3f( p->org[0] - right[0] + up[0], p->org[1] - right[1] + up[1], p->org[2] - right[2] + up[2] );
			TriTexCoord2f( 0.0f, 0.0f );
			TriVertex3f( p->org[0] + right[0] + up[0], p->org[1] + right[1] + up[1], p->org[2] + right[2] + up[2] );
			TriTexCoord2f( 1.0f, 0.0f );
			TriVertex3f( p->org[0] + right[0] - up[0], p->org[1] + right[1] - up[1], p->org[2] + right[2] - up[2] );
			TriTexCoord2f( 1.0f, 1.0f );
			TriVertex3f( p->org[0] - right[0] - up[0], p->org[1] - right[1] - up[1], p->org[2] - right[2] - up[2] );
			TriEnd();
			r_stats.c_particle_count++;
		}

		gEngfuncs.CL_ThinkParticle( frametime, p );
	}

	TriEnd();
	// pglDepthMask( GL_TRUE );
}

/*
================
CL_CullTracer

check tracer bbox
================
*/
static qboolean CL_CullTracer( particle_t *p, const vec3_t start, const vec3_t end )
{
	vec3_t mins, maxs;
	int    i;
	return false;
/*
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
	return R_CullBox( mins, maxs );*/
}

/*
================
CL_DrawTracers

update tracer color, position, free expired and draw it
================
*/
void GAME_EXPORT CL_DrawTracers( double frametime, particle_t *cl_active_tracers )
{
	float      scale, atten, gravity;
	vec3_t     screenLast, screen;
	vec3_t     start, end, delta;
	particle_t *p;

	// update tracer color if this is changed
	if( FBitSet( tracerred->flags | tracergreen->flags | tracerblue->flags | traceralpha->flags, FCVAR_CHANGED ))
	{
		color24 *customColors = &gTracerColors[4];
		customColors->r = (byte)( tracerred->value * traceralpha->value * 255 );
		customColors->g = (byte)( tracergreen->value * traceralpha->value * 255 );
		customColors->b = (byte)( tracerblue->value * traceralpha->value * 255 );
		ClearBits( tracerred->flags, FCVAR_CHANGED );
		ClearBits( tracergreen->flags, FCVAR_CHANGED );
		ClearBits( tracerblue->flags, FCVAR_CHANGED );
		ClearBits( traceralpha->flags, FCVAR_CHANGED );
	}

	if( !cl_active_tracers )
		return; // nothing to draw?

	GL_SetRenderMode( kRenderTransAdd );

	if( !TriSpriteTexture( gEngfuncs.GetDefaultSprite( REF_DOT_SPRITE ), 0 ))
		return;

	// pglEnable( GL_BLEND );
	// pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
	// pglDisable( GL_ALPHA_TEST );
	// pglDepthMask( GL_FALSE );

	gravity = frametime * tr.movevars->gravity;
	scale = 1.0 - ( frametime * 0.9 );
	if( scale < 0.0f )
		scale = 0.0f;

	for( p = cl_active_tracers; p; p = p->next )
	{
		atten = ( p->die - gp_cl->time );
		if( atten > 0.1f )
			atten = 0.1f;

		VectorScale( p->vel, ( p->ramp * atten ), delta );
		VectorAdd( p->org, delta, end );
		VectorCopy( p->org, start );

		if( !CL_CullTracer( p, start, end ))
		{
			vec3_t  verts[4], tmp2;
			vec3_t  tmp, normal;
			color24 color;
			short   alpha = p->packedColor;

			// Transform point into screen space
			TriWorldToScreen( start, screen );
			TriWorldToScreen( end, screenLast );

			// build world-space normal to screen-space direction vector
			VectorSubtract( screen, screenLast, tmp );

			// we don't need Z, we're in screen space
			tmp[2] = 0;
			VectorNormalize( tmp );

			// build point along noraml line (normal is -y, x)
			VectorScale( RI.cull_vup, tmp[0] * gTracerSize[p->type], normal );
			VectorScale( RI.cull_vright, -tmp[1] * gTracerSize[p->type], tmp2 );
			VectorSubtract( normal, tmp2, normal );

			// compute four vertexes
			VectorSubtract( start, normal, verts[0] );
			VectorAdd( start, normal, verts[1] );
			VectorAdd( verts[0], delta, verts[2] );
			VectorAdd( verts[1], delta, verts[3] );

			if( p->color < 0 || p->color >= sizeof( gTracerColors ) / sizeof( gTracerColors[0] ))
			{
				p->color = TRACER_COLORINDEX_DEFAULT;
			}

			color = gTracerColors[p->color];
			// TriColor4ub( color.r, color.g, color.b, p->packedColor );
			_TriColor4f( 1.0f * alpha / 255 / 255 * color.r, 1.0f * alpha / 255 / 255 * color.g, 1.0f * alpha / 255 / 255 * color.b, 1.0f );


			TriBegin( TRI_QUADS );
			TriTexCoord2f( 0.0f, 0.8f );
			TriVertex3fv( verts[2] );
			TriTexCoord2f( 1.0f, 0.8f );
			TriVertex3fv( verts[3] );
			TriTexCoord2f( 1.0f, 0.0f );
			TriVertex3fv( verts[1] );
			TriTexCoord2f( 0.0f, 0.0f );
			TriVertex3fv( verts[0] );
			TriEnd();
		}

		// evaluate position
		VectorMA( p->org, frametime, p->vel, p->org );

		if( p->type == pt_grav )
		{
			p->vel[0] *= scale;
			p->vel[1] *= scale;
			p->vel[2] -= gravity;

			p->packedColor = 255 * ( p->die - gp_cl->time ) * 2;
			if( p->packedColor > 255 )
				p->packedColor = 255;
		}
		else if( p->type == pt_slowgrav )
		{
			p->vel[2] = gravity * 0.05;
		}
	}

	// pglDepthMask( GL_TRUE );
}

/*
===============
CL_DrawParticlesExternal

allow to draw effects from custom renderer
===============
*/
void GAME_EXPORT CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime )
{
	ref_instance_t oldRI = RI;

	R_SetupRefParams( rvp );
	R_SetupFrustum();
//	R_SetupGL( false );	// don't touch GL-states

	// setup PVS for frame
	memcpy( RI.visbytes, tr.visbytes, gpGlobals->visbytes );
	tr.frametime = frametime;

	gEngfuncs.CL_DrawEFX( frametime, trans_pass );

	// restore internal state
	RI = oldRI;
}
