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

#include "common.h"
#include "client.h"
#include "gl_local.h"
#include "r_efx.h"
#include "event_flags.h"
#include "entity_types.h"
#include "triangleapi.h"
#include "pm_local.h"
#include "cl_tent.h"
#include "studio.h"

#define PART_SIZE	Q_max( 0.5f, cl_draw_particles->value )

/*
==============================================================

PARTICLES MANAGEMENT

==============================================================
*/
// particle ramps
static int ramp1[8] = { 0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61 };
static int ramp2[8] = { 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66 };
static int ramp3[6] = { 0x6d, 0x6b, 6, 5, 4, 3 };
static float gTracerSize[11] = { 1.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static int gSparkRamp[9] = { 0xfe, 0xfd, 0xfc, 0x6f, 0x6e, 0x6d, 0x6c, 0x67, 0x60 };

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

convar_t		*tracerred;
convar_t		*tracergreen;
convar_t		*tracerblue;
convar_t		*traceralpha;
convar_t		*tracerspeed;
convar_t		*tracerlength;
convar_t		*traceroffset;

particle_t	*cl_active_particles;
particle_t	*cl_active_tracers;
particle_t	*cl_free_particles;
particle_t	*cl_particles = NULL;	// particle pool
static vec3_t	cl_avelocities[NUMVERTEXNORMALS];
static float	cl_lasttimewarn = 0.0f;

/*
================
R_LookupColor

find nearest color in particle palette
================
*/
short R_LookupColor( byte r, byte g, byte b )
{
	int	i, best;
	float	diff, bestdiff;
	float	rf, gf, bf;

	bestdiff = 999999;
	best = 65535;

	for( i = 0; i < 256; i++ )
	{
		rf = r - clgame.palette[i].r;
		gf = g - clgame.palette[i].g;
		bf = b - clgame.palette[i].b;

		// convert color to monochrome
		diff = rf * (rf * 0.2) + gf * (gf * 0.5) + bf * (bf * 0.3);

		if ( diff < bestdiff )
		{
			bestdiff = diff;
			best = i;
		}
	}

	return best;
}

/*
================
R_GetPackedColor

in hardware mode does nothing
================
*/
void R_GetPackedColor( short *packed, short color )
{
	if( packed ) *packed = 0;
}

/*
================
CL_InitParticles

================
*/
void CL_InitParticles( void )
{
	int	i;

	cl_particles = Mem_Calloc( cls.mempool, sizeof( particle_t ) * GI->max_particles );
	CL_ClearParticles ();

	// this is used for EF_BRIGHTFIELD
	for( i = 0; i < NUMVERTEXNORMALS; i++ )
	{
		cl_avelocities[i][0] = COM_RandomFloat( 0.0f, 2.55f );
		cl_avelocities[i][1] = COM_RandomFloat( 0.0f, 2.55f );
		cl_avelocities[i][2] = COM_RandomFloat( 0.0f, 2.55f );
	}

	tracerred = Cvar_Get( "tracerred", "0.8", 0, "tracer red component weight ( 0 - 1.0 )" );
	tracergreen = Cvar_Get( "tracergreen", "0.8", 0, "tracer green component weight ( 0 - 1.0 )" );
	tracerblue = Cvar_Get( "tracerblue", "0.4", 0, "tracer blue component weight ( 0 - 1.0 )" );
	traceralpha = Cvar_Get( "traceralpha", "0.5", 0, "tracer alpha amount ( 0 - 1.0 )" );
	tracerspeed = Cvar_Get( "tracerspeed", "6000", 0, "tracer speed" );
	tracerlength = Cvar_Get( "tracerlength", "0.8", 0, "tracer length factor" );
	traceroffset = Cvar_Get( "traceroffset", "30", 0, "tracer starting offset" );
}

/*
================
CL_ClearParticles

================
*/
void CL_ClearParticles( void )
{
	int	i;

	if( !cl_particles ) return;

	cl_free_particles = cl_particles;
	cl_active_particles = NULL;
	cl_active_tracers = NULL;

	for( i = 0; i < GI->max_particles - 1; i++ )
		cl_particles[i].next = &cl_particles[i+1];

	cl_particles[GI->max_particles-1].next = NULL;
}

/*
================
CL_FreeParticles

================
*/
void CL_FreeParticles( void )
{
	if( cl_particles )
		Mem_Free( cl_particles );
	cl_particles = NULL;
}

/*
================
CL_FreeParticle

move particle to freelist
================
*/
void CL_FreeParticle( particle_t *p )
{
	if( p->deathfunc )
	{
		// call right the deathfunc before die
		p->deathfunc( p );
		p->deathfunc = NULL;
	}

	p->next = cl_free_particles;
	cl_free_particles = p;
}

/*
================
R_AllocParticle

can return NULL if particles is out
================
*/
particle_t *R_AllocParticle( void (*callback)( particle_t*, float ))
{
	particle_t	*p;

	if( !cl_draw_particles->value )
		return NULL;

	// never alloc particles when we not in game
	if( tr.frametime == 0.0 ) return NULL;

	if( !cl_free_particles )
	{
		if( cl_lasttimewarn < host.realtime )
		{
			// don't spam about overflow
			Con_DPrintf( S_ERROR "Overflow %d particles\n", GI->max_particles );
			cl_lasttimewarn = host.realtime + 1.0f;
		}
		return NULL;
	}

	p = cl_free_particles;
	cl_free_particles = p->next;
	p->next = cl_active_particles;
	cl_active_particles = p;

	// clear old particle
	p->type = pt_static;
	VectorClear( p->vel );
	VectorClear( p->org );
	p->packedColor = 0;
	p->die = cl.time;
	p->color = 0;
	p->ramp = 0;

	if( callback )
	{
		p->type = pt_clientcustom;
		p->callback = callback;
	}

	return p;
}

/*
================
R_AllocTracer

can return NULL if particles is out
================
*/
particle_t *R_AllocTracer( const vec3_t org, const vec3_t vel, float life )
{
	particle_t	*p;

	if( !cl_draw_tracers->value )
		return NULL;

	// never alloc particles when we not in game
	if( tr.frametime == 0.0 ) return NULL;

	if( !cl_free_particles )
	{
		if( cl_lasttimewarn < host.realtime )
		{
			// don't spam about overflow
			Con_DPrintf( S_ERROR "Overflow %d tracers\n", GI->max_particles );
			cl_lasttimewarn = host.realtime + 1.0f;
		}
		return NULL;
	}

	p = cl_free_particles;
	cl_free_particles = p->next;
	p->next = cl_active_tracers;
	cl_active_tracers = p;

	// clear old particle
	p->type = pt_static;
	VectorCopy( org, p->org );
	VectorCopy( vel, p->vel );
	p->die = cl.time + life;
	p->ramp = tracerlength->value;
	p->color = 4; // select custom color
	p->packedColor = 255; // alpha

	return p;
}

/*
==============
R_FreeDeadParticles

Free particles that time has expired
==============
*/
void R_FreeDeadParticles( particle_t **ppparticles )
{
	particle_t	*p, *kill;

	// kill all the ones hanging direcly off the base pointer
	while( 1 ) 
	{
		kill = *ppparticles;
		if( kill && kill->die < cl.time )
		{
			if( kill->deathfunc )
				kill->deathfunc( kill );
			kill->deathfunc = NULL;
			*ppparticles = kill->next;
			kill->next = cl_free_particles;
			cl_free_particles = kill;
			continue;
		}
		break;
	}

	// kill off all the others
	for( p = *ppparticles; p; p = p->next )
	{
		while( 1 )
		{
			kill = p->next;
			if( kill && kill->die < cl.time )
			{
				if( kill->deathfunc )
					kill->deathfunc( kill );
				kill->deathfunc = NULL;
				p->next = kill->next;
				kill->next = cl_free_particles;
				cl_free_particles = kill;
				continue;
			}
			break;
		}
	}
}

/*
================
CL_DrawParticles

update particle color, position, free expired and draw it
================
*/
void CL_DrawParticles( double frametime )
{
	particle_t	*p;
	float		time3 = 15.0f * frametime;
	float		time2 = 10.0f * frametime;
	float		time1 = 5.0f * frametime;
	float		dvel = 4.0f * frametime;
	float		grav = frametime * clgame.movevars.gravity * 0.05f;
	vec3_t		right, up;
	color24		*pColor;
	int		alpha;
	float		size;

	if( !cl_draw_particles->value )
		return;

	R_FreeDeadParticles( &cl_active_particles );

	if( !cl_active_particles )
		return;	// nothing to draw?

	pglEnable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	GL_Bind( GL_TEXTURE0, tr.particleTexture );
	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglDepthMask( GL_FALSE );

	pglBegin( GL_QUADS );

	for( p = cl_active_particles; p; p = p->next )
	{
		if(( p->type != pt_blob ) || ( p->packedColor == 255 ))
		{
			size = PART_SIZE; // get initial size of particle

			// scale up to keep particles from disappearing
			size += (p->org[0] - RI.vieworg[0]) * RI.cull_vforward[0];
			size += (p->org[1] - RI.vieworg[1]) * RI.cull_vforward[1];
			size += (p->org[2] - RI.vieworg[2]) * RI.cull_vforward[2];

			if( size < 20.0f ) size = PART_SIZE;
			else size = PART_SIZE + size * 0.002f;

			// scale the axes by radius
			VectorScale( RI.cull_vright, size, right );
			VectorScale( RI.cull_vup, size, up );

			p->color = bound( 0, p->color, 255 );
			pColor = &clgame.palette[p->color];

			alpha = 255 * (p->die - cl.time) * 16.0f;
			if( alpha > 255 || p->type == pt_static )
				alpha = 255;

			pglColor4ub( LightToTexGamma( pColor->r ), LightToTexGamma( pColor->g ), LightToTexGamma( pColor->b ), alpha );

			pglTexCoord2f( 0.0f, 1.0f );
			pglVertex3f( p->org[0] - right[0] + up[0], p->org[1] - right[1] + up[1], p->org[2] - right[2] + up[2] );
			pglTexCoord2f( 0.0f, 0.0f );
			pglVertex3f( p->org[0] + right[0] + up[0], p->org[1] + right[1] + up[1], p->org[2] + right[2] + up[2] );
			pglTexCoord2f( 1.0f, 0.0f );
			pglVertex3f( p->org[0] + right[0] - up[0], p->org[1] + right[1] - up[1], p->org[2] + right[2] - up[2] );
			pglTexCoord2f( 1.0f, 1.0f );
			pglVertex3f( p->org[0] - right[0] - up[0], p->org[1] - right[1] - up[1], p->org[2] - right[2] - up[2] );
			r_stats.c_particle_count++;
		}

		if( p->type != pt_clientcustom )
		{
			// update position.
			VectorMA( p->org, frametime, p->vel, p->org );
		}

		switch( p->type )
		{
		case pt_static:
			break;
		case pt_fire:
			p->ramp += time1;
			if( p->ramp >= 6.0f ) p->die = -1.0f;
			else p->color = ramp3[(int)p->ramp];
			p->vel[2] += grav;
			break;
		case pt_explode:
			p->ramp += time2;
			if( p->ramp >= 8.0f ) p->die = -1.0f;
			else p->color = ramp1[(int)p->ramp];
			VectorMA( p->vel, dvel, p->vel, p->vel );
			p->vel[2] -= grav;
			break;
		case pt_explode2:
			p->ramp += time3;
			if( p->ramp >= 8.0f ) p->die = -1.0f;
			else p->color = ramp2[(int)p->ramp];
			VectorMA( p->vel,-frametime, p->vel, p->vel );
			p->vel[2] -= grav;
			break;
		case pt_blob:
			if( p->packedColor == 255 )
			{
				// normal blob explosion
				VectorMA( p->vel, dvel, p->vel, p->vel );
				p->vel[2] -= grav;
				break;
			}
		case pt_blob2:
			if( p->packedColor == 255 )
			{
				// normal blob explosion
				p->vel[0] -= p->vel[0] * dvel;
				p->vel[1] -= p->vel[1] * dvel;
				p->vel[2] -= grav;
			}
			else
			{
				p->ramp += time2;
				if( p->ramp >= 9.0f ) p->ramp = 0.0f;
				p->color = gSparkRamp[(int)p->ramp];
				VectorMA( p->vel, -frametime * 0.5f, p->vel, p->vel );
				p->type = COM_RandomLong( 0, 3 ) ? pt_blob : pt_blob2;
				p->vel[2] -= grav * 5.0f;
			}
			break;
		case pt_grav:
			p->vel[2] -= grav * 20.0f;
			break;
		case pt_slowgrav:
			p->vel[2] -= grav;
			break;
		case pt_vox_grav:
			p->vel[2] -= grav * 8.0f;
			break;
		case pt_vox_slowgrav:
			p->vel[2] -= grav * 4.0f;
			break;
		case pt_clientcustom:
			if( p->callback )
				p->callback( p, frametime );
			break;
		}
	}

	pglEnd();
	pglDepthMask( GL_TRUE );
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
	return R_CullBox( mins, maxs );
}

/*
================
CL_DrawTracers

update tracer color, position, free expired and draw it
================
*/
void CL_DrawTracers( double frametime )
{
	float		scale, atten, gravity;
	vec3_t		screenLast, screen;
	vec3_t		start, end, delta;
	particle_t	*p;

	if( !cl_draw_tracers->value )
		return;

	// update tracer color if this is changed
	if( FBitSet( tracerred->flags|tracergreen->flags|tracerblue->flags|traceralpha->flags, FCVAR_CHANGED ))
	{
		gTracerColors[4].r = (byte)(tracerred->value * traceralpha->value * 255);
		gTracerColors[4].g = (byte)(tracergreen->value * traceralpha->value * 255);
		gTracerColors[4].b = (byte)(tracerblue->value * traceralpha->value * 255);
		ClearBits( tracerred->flags, FCVAR_CHANGED );
		ClearBits( tracergreen->flags, FCVAR_CHANGED );
		ClearBits( tracerblue->flags, FCVAR_CHANGED );
		ClearBits( traceralpha->flags, FCVAR_CHANGED );
	}

	R_FreeDeadParticles( &cl_active_tracers );

	if( !cl_active_tracers )
		return;	// nothing to draw?

	if( !TriSpriteTexture( cl_sprite_dot, 0 ))
		return;

	pglEnable( GL_BLEND );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
	pglDisable( GL_ALPHA_TEST );
	pglDepthMask( GL_FALSE );

	gravity = frametime * clgame.movevars.gravity;
	scale = 1.0 - (frametime * 0.9);
	if( scale < 0.0f ) scale = 0.0f;

	for( p = cl_active_tracers; p; p = p->next )
	{
		atten = (p->die - cl.time);
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

			// build point along noraml line (normal is -y, x)
			VectorScale( RI.cull_vup, tmp[0] * gTracerSize[p->type], normal );
			VectorScale( RI.cull_vright, -tmp[1] * gTracerSize[p->type], tmp2 );
			VectorSubtract( normal, tmp2, normal );

			// compute four vertexes
			VectorSubtract( start, normal, verts[0] ); 
			VectorAdd( start, normal, verts[1] ); 
			VectorAdd( verts[0], delta, verts[2] ); 
			VectorAdd( verts[1], delta, verts[3] ); 

			pColor = &gTracerColors[p->color];
			pglColor4ub( pColor->r, pColor->g, pColor->b, p->packedColor );

			pglBegin( GL_QUADS );
				pglTexCoord2f( 0.0f, 0.8f );
				pglVertex3fv( verts[2] );
				pglTexCoord2f( 1.0f, 0.8f );
				pglVertex3fv( verts[3] );
				pglTexCoord2f( 1.0f, 0.0f );
				pglVertex3fv( verts[1] );
				pglTexCoord2f( 0.0f, 0.0f );
				pglVertex3fv( verts[0] );
			pglEnd();
		}

		// evaluate position
		VectorMA( p->org, frametime, p->vel, p->org );

		if( p->type == pt_grav )
		{
			p->vel[0] *= scale;
			p->vel[1] *= scale;
			p->vel[2] -= gravity;

			p->packedColor = 255 * (p->die - cl.time) * 2;
			if( p->packedColor > 255 ) p->packedColor = 255;
		}
		else if( p->type == pt_slowgrav )
		{
			p->vel[2] = gravity * 0.05;
		}
	}

	pglDepthMask( GL_TRUE );
}

/*
===============
CL_DrawParticlesExternal

allow to draw effects from custom renderer
===============
*/
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime )
{
	ref_instance_t	oldRI = RI;

	memcpy( &oldRI, &RI, sizeof( ref_instance_t ));
	R_SetupRefParams( rvp );
	R_SetupFrustum();
	R_SetupGL( false );	// don't touch GL-states

	// setup PVS for frame
	memcpy( RI.visbytes, tr.visbytes, world.visbytes );
	tr.frametime = frametime;

	if( trans_pass == false )
	{
		CL_DrawBeams( false );
	}
	else
	{
		CL_DrawBeams( true );
		CL_DrawParticles( tr.frametime );
		CL_DrawTracers( tr.frametime );
	}

	// restore internal state
	memcpy( &RI, &oldRI, sizeof( ref_instance_t ));
}

/*
===============
R_EntityParticles

set EF_BRIGHTFIELD effect
===============
*/
void R_EntityParticles( cl_entity_t *ent )
{
	float		angle;
	float		sr, sp, sy, cr, cp, cy;
	vec3_t		forward;	
	particle_t	*p;
	int		i;

	for( i = 0; i < NUMVERTEXNORMALS; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		angle = cl.time * cl_avelocities[i][0];
		SinCos( angle, &sy, &cy );
		angle = cl.time * cl_avelocities[i][1];
		SinCos( angle, &sp, &cp );
		angle = cl.time * cl_avelocities[i][2];
		SinCos( angle, &sr, &cr );
	
		VectorSet( forward, cp * cy, cp * sy, -sp ); 

		p->die = cl.time + 0.001f;
		p->color = 111; // yellow

		VectorMAMAM( 1.0f, ent->origin, 64.0f, m_bytenormals[i], 16.0f, forward, p->org );
	}
}

/*
===============
R_ParticleExplosion

===============
*/
void R_ParticleExplosion( const vec3_t org )
{
	particle_t	*p;
	int		i, j;

	for( i = 0; i < 1024; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 5.0f;
		p->ramp = COM_RandomLong( 0, 3 );
		p->color = ramp1[0];

		for( j = 0; j < 3; j++ )
		{
			p->org[j] = org[j] + COM_RandomFloat( -16.0f, 16.0f );
			p->vel[j] = COM_RandomFloat( -256.0f, 256.0f );
		}

		if( i & 1 ) p->type = pt_explode;
		else p->type = pt_explode2;
	}
}

/*
===============
R_ParticleExplosion2

===============
*/
void R_ParticleExplosion2( const vec3_t org, int colorStart, int colorLength )
{
	int		i, j;
	int		colorMod = 0;
	particle_t	*p;

	for( i = 0; i < 512; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 0.3f;
		p->color = colorStart + ( colorMod % colorLength );
		p->packedColor = 255; // use old code for blob particles
		colorMod++;

		p->type = pt_blob;

		for( j = 0; j < 3; j++ )
		{
			p->org[j] = org[j] + COM_RandomFloat( -16.0f, 16.0f );
			p->vel[j] = COM_RandomFloat( -256.0f, 256.0f );
		}
	}
}

/*
===============
R_BlobExplosion

===============
*/
void R_BlobExplosion( const vec3_t org )
{
	particle_t	*p;
	int		i, j;

	for( i = 0; i < 1024; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + COM_RandomFloat( 2.0f, 2.4f );
		p->packedColor = 255; // use old code for blob particles

		if( i & 1 )
		{
			p->type = pt_blob;
			p->color = COM_RandomLong( 66, 71 );
		}
		else
		{
			p->type = pt_blob2;
			p->color = COM_RandomLong( 150, 155 );
		}

		for( j = 0; j < 3; j++ )
		{
			p->org[j] = org[j] + COM_RandomFloat( -16.0f, 16.0f );
			p->vel[j] = COM_RandomFloat( -256.0f, 256.0f );
		}
	}
}

/*
===============
ParticleEffect

PARTICLE_EFFECT on server
===============
*/
void R_RunParticleEffect( const vec3_t org, const vec3_t dir, int color, int count )
{
	particle_t	*p;
	int		i;

	if( count == 1024 )
	{
		// rocket explosion
		R_ParticleExplosion( org );
		return;
	}
	
	for( i = 0; i < count; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->color = (color & ~7) + COM_RandomLong( 0, 7 );
		p->die = cl.time + COM_RandomFloat( 0.1f, 0.4f );
		p->type = pt_slowgrav;

		VectorAddScalar( org, COM_RandomFloat( -8.0f, 8.0f ), p->org );
		VectorScale( dir, 15.0f, p->vel );
	}
}

/*
===============
R_Blood

particle spray
===============
*/
void R_Blood( const vec3_t org, const vec3_t ndir, int pcolor, int speed )
{
	vec3_t		pos, dir, vec;
	float		pspeed = speed * 3.0f;
	int		i, j;
	particle_t	*p;

	VectorNormalize2( ndir, dir );

	for( i = 0; i < (speed / 2); i++ )
	{
		VectorAddScalar( org, COM_RandomFloat( -3.0f, 3.0f ), pos );
		VectorAddScalar( dir, COM_RandomFloat( -0.06f, 0.06f ), vec );

		for( j = 0; j < 7; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			p->die = cl.time + 1.5f;
			p->color = pcolor + COM_RandomLong( 0, 9 );
			p->type = pt_vox_grav;

			VectorAddScalar( pos, COM_RandomFloat( -1.0f, 1.0f ), p->org );
			VectorScale( vec, pspeed, p->vel );
		}
	}
}

/*
===============
R_BloodStream

particle spray 2
===============
*/
void R_BloodStream( const vec3_t org, const vec3_t dir, int pcolor, int speed )
{
	particle_t	*p;
	int		i, j;
	float		arc;
	float		accel = speed;

	for( arc = 0.05f, i = 0; i < 100; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 2.0f;
		p->type = pt_vox_grav;
		p->color = pcolor + COM_RandomLong( 0, 9 );

		VectorCopy( org, p->org );
		VectorCopy( dir, p->vel );

		p->vel[2] -= arc;
		arc -= 0.005f;
		VectorScale( p->vel, accel, p->vel );
		accel -= 0.00001f; // so last few will drip
	}

	for( arc = 0.075f, i = 0; i < ( speed / 5 ); i++ )
	{
		float	num;

		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 3.0f;
		p->color = pcolor + COM_RandomLong( 0, 9 );
		p->type = pt_vox_slowgrav;

		VectorCopy( org, p->org );
		VectorCopy( dir, p->vel );

		p->vel[2] -= arc;
		arc -= 0.005f;

		num = COM_RandomFloat( 0.0f, 1.0f );
		accel = speed * num;
		num *= 1.7f;

		VectorScale( p->vel, num, p->vel );
		VectorScale( p->vel, accel, p->vel );

		for( j = 0; j < 2; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			p->die = cl.time + 3.0f;
			p->color = pcolor + COM_RandomLong( 0, 9 );
			p->type = pt_vox_slowgrav;

			p->org[0] = org[0] + COM_RandomFloat( -1.0f, 1.0f );
			p->org[1] = org[1] + COM_RandomFloat( -1.0f, 1.0f );
			p->org[2] = org[2] + COM_RandomFloat( -1.0f, 1.0f );

			VectorCopy( dir, p->vel );
			p->vel[2] -= arc;

			VectorScale( p->vel, num, p->vel );
			VectorScale( p->vel, accel, p->vel );
		}
	}
}

/*
===============
R_LavaSplash

===============
*/
void R_LavaSplash( const vec3_t org )
{
	particle_t	*p;
	float		vel;
	vec3_t		dir;
	int		i, j, k;

	for( i = -16; i < 16; i++ )
	{
		for( j = -16; j <16; j++ )
		{
			for( k = 0; k < 1; k++ )
			{
				p = R_AllocParticle( NULL );
				if( !p ) return;

				p->die = cl.time + COM_RandomFloat( 2.0f, 2.62f );
				p->color = COM_RandomLong( 224, 231 );
				p->type = pt_slowgrav;
				
				dir[0] = j * 8.0f + COM_RandomFloat( 0.0f, 7.0f );
				dir[1] = i * 8.0f + COM_RandomFloat( 0.0f, 7.0f );
				dir[2] = 256.0f;

				p->org[0] = org[0] + dir[0];
				p->org[1] = org[1] + dir[1];
				p->org[2] = org[2] + COM_RandomFloat( 0.0f, 63.0f );

				VectorNormalize( dir );
				vel = COM_RandomFloat( 50.0f, 113.0f );
				VectorScale( dir, vel, p->vel );
			}
		}
	}
}

/*
===============
R_ParticleBurst

===============
*/
void R_ParticleBurst( const vec3_t org, int size, int color, float life )
{
	particle_t	*p;
	vec3_t		dir, dest;
	int		i, j;
	float		dist;

	for( i = 0; i < 32; i++ )
	{
		for( j = 0; j < 32; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			p->die = cl.time + life + COM_RandomFloat( -0.5f, 0.5f );
			p->color = color + COM_RandomLong( 0, 10 );
			p->ramp = 1.0f;

			VectorCopy( org, p->org );
			VectorAddScalar( org, COM_RandomFloat( -size, size ), dest );
			VectorSubtract( dest, p->org, dir );
			dist = VectorNormalizeLength( dir );
			VectorScale( dir, ( dist / life ), p->vel );
		}
	}
}

/*
===============
R_LargeFunnel

===============
*/
void R_LargeFunnel( const vec3_t org, int reverse )
{
	particle_t	*p;
	float		vel, dist;
	vec3_t		dir, dest;
	int		i, j;

	for( i = -8; i < 8; i++ )
	{
		for( j = -8; j < 8; j++ )
		{
			p = R_AllocParticle( NULL );
			if( !p ) return;

			dest[0] = (i * 32.0f) + org[0];
			dest[1] = (j * 32.0f) + org[1];
			dest[2] = org[2] + COM_RandomFloat( 100.0f, 800.0f );

			if( reverse )
			{
				VectorCopy( org, p->org );
				VectorSubtract( dest, p->org, dir );
			}
			else
			{
				VectorCopy( dest, p->org );
				VectorSubtract( org, p->org, dir );
			}

			vel = dest[2] / 8.0f;
			if( vel < 64.0f ) vel = 64.0f;

			dist = VectorNormalizeLength( dir );
			vel += COM_RandomFloat( 64.0f, 128.0f );
			VectorScale( dir, vel, p->vel );
			p->die = cl.time + (dist / vel );
			p->color = 244; // green color
		}
	}
}

/*
===============
R_TeleportSplash

===============
*/
void R_TeleportSplash( const vec3_t org )
{
	particle_t	*p;
	vec3_t		dir;
	float		vel;
	int		i, j, k;

	for( i = -16; i < 16; i += 4 )
	{
		for( j = -16; j < 16; j += 4 )
		{
			for( k = -24; k < 32; k += 4 )
			{
				p = R_AllocParticle( NULL );
				if( !p ) return;
		
				p->die = cl.time + COM_RandomFloat( 0.2f, 0.34f );
				p->color = COM_RandomLong( 7, 14 );
				p->type = pt_slowgrav;
				
				dir[0] = j * 8.0f;
				dir[1] = i * 8.0f;
				dir[2] = k * 8.0f;
	
				p->org[0] = org[0] + i + COM_RandomFloat( 0.0f, 3.0f );
				p->org[1] = org[1] + j + COM_RandomFloat( 0.0f, 3.0f );
				p->org[2] = org[2] + k + COM_RandomFloat( 0.0f, 3.0f );
	
				VectorNormalize( dir );
				vel = COM_RandomFloat( 50.0f, 113.0f );
				VectorScale( dir, vel, p->vel );
			}
		}
	}
}

/*
===============
R_RocketTrail

===============
*/
void R_RocketTrail( vec3_t start, vec3_t end, int type )
{
	vec3_t		vec, right, up;
	static int	tracercount;
	float		s, c, x, y;
	float		len, dec;
	particle_t	*p;

	VectorSubtract( end, start, vec );
	len = VectorNormalizeLength( vec );

	if( type == 7 )
	{
		VectorVectors( vec, right, up );
	}

	if( type < 128 )
	{
		dec = 3.0f;
	}
	else
	{
		dec = 1.0f;
		type -= 128;
	}

	VectorScale( vec, dec, vec );

	while( len > 0 )
	{
		len -= dec;

		p = R_AllocParticle( NULL );
		if( !p ) return;
		
		p->die = cl.time + 2.0f;

		switch( type )
		{
		case 0:	// rocket trail
			p->ramp = COM_RandomLong( 0, 3 );
			p->color = ramp3[(int)p->ramp];
			p->type = pt_fire;
			VectorAddScalar( start, COM_RandomFloat( -3.0f, 3.0f ), p->org );
			break;
		case 1:	// smoke smoke
			p->ramp = COM_RandomLong( 2, 5 );
			p->color = ramp3[(int)p->ramp];
			p->type = pt_fire;
			VectorAddScalar( start, COM_RandomFloat( -3.0f, 3.0f ), p->org );
			break;
		case 2:	// blood
			p->type = pt_grav;
			p->color = COM_RandomLong( 67, 74 );
			VectorAddScalar( start, COM_RandomFloat( -3.0f, 3.0f ), p->org );
			break;
		case 3:
		case 5:	// tracer
			p->die = cl.time + 0.5f;

			if( type == 3 ) p->color = 52 + (( tracercount & 4 )<<1 );
			else p->color = 230 + (( tracercount & 4 )<<1 );

			VectorCopy( start, p->org );
			tracercount++;

			if( FBitSet( tracercount, 1 ))
			{
				p->vel[0] = 30.0f *  vec[1];
				p->vel[1] = 30.0f * -vec[0];
			}
			else
			{
				p->vel[0] = 30.0f * -vec[1];
				p->vel[1] = 30.0f *  vec[0];
			}
			break;
		case 4:	// slight blood
			p->type = pt_grav;
			p->color = COM_RandomLong( 67, 70 );
			VectorAddScalar( start, COM_RandomFloat( -3.0f, 3.0f ), p->org );
			len -= 3.0f;
			break;
		case 6:	// voor trail
			p->color = COM_RandomLong( 152, 155 );
			p->die += 0.3f;
			VectorAddScalar( start, COM_RandomFloat( -8.0f, 8.0f ), p->org );
			break;
		case 7:	// explosion tracer
			x = COM_RandomLong( 0, 65535 );
			y = COM_RandomLong( 8, 16 );
			SinCos( x, &s, &c );
			s *= y;
			c *= y;

			VectorMAMAM( 1.0f, start, s, right, c, up, p->org );
			VectorSubtract( start, p->org, p->vel );
			VectorScale( p->vel, 2.0f, p->vel );
			VectorMA( p->vel, COM_RandomFloat( 96.0f, 111.0f ), vec, p->vel );
			p->ramp = COM_RandomLong( 0, 3 );
			p->color = ramp3[(int)p->ramp];
			p->type = pt_explode2;
			break;
		default:
			// just build line to show error
			VectorCopy( start, p->org );
			break;
		}

		VectorAdd( start, vec, start );
	}
}

/*
================
R_ParticleLine

================
*/
void R_ParticleLine( const vec3_t start, const vec3_t end, byte r, byte g, byte b, float life )
{
	int	pcolor;

	pcolor = R_LookupColor( r, g, b );
	PM_ParticleLine( start, end, pcolor, life, 0 );
}

/*
================
R_ParticleBox

================
*/
void R_ParticleBox( const vec3_t absmin, const vec3_t absmax, byte r, byte g, byte b, float life )
{
	vec3_t	mins, maxs;
	vec3_t	origin;
	int	pcolor;

	pcolor = R_LookupColor( r, g, b );

	VectorAverage( absmax, absmin, origin );
	VectorSubtract( absmax, origin, maxs );
	VectorSubtract( absmin, origin, mins );

	PM_DrawBBox( mins, maxs, origin, pcolor, life );
}

/*
================
R_ShowLine

================
*/
void R_ShowLine( const vec3_t start, const vec3_t end )
{
	vec3_t		dir, org;
	float		len;
	particle_t	*p;

	VectorSubtract( end, start, dir );
	len = VectorNormalizeLength( dir );
	VectorScale( dir, 5.0f, dir );
	VectorCopy( start, org );
	
	while( len > 0 )
	{
		len -= 5.0f;

		p = R_AllocParticle( NULL );
		if( !p ) return;

		p->die = cl.time + 30;
		p->color = 75;

		VectorCopy( org, p->org );
		VectorAdd( org, dir, org );
	}
}

/*
===============
R_BulletImpactParticles

===============
*/
void R_BulletImpactParticles( const vec3_t pos )
{
	int		i, quantity;
	int		color;
	float		dist;
	vec3_t		dir;
	particle_t	*p;
	
	VectorSubtract( pos, RI.vieworg, dir );
	dist = VectorLength( dir );
	if( dist > 1000.0f ) dist = 1000.0f;

	quantity = (1000.0f - dist) / 100.0f;
	if( quantity == 0 ) quantity = 1;

	color = 3 - ((30 * quantity) / 100 );
	R_SparkStreaks( pos, 2, -200, 200 );

	for( i = 0; i < quantity * 4; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		VectorCopy( pos, p->org);

		p->vel[0] = COM_RandomFloat( -1.0f, 1.0f );
		p->vel[1] = COM_RandomFloat( -1.0f, 1.0f );
		p->vel[2] = COM_RandomFloat( -1.0f, 1.0f );
		VectorScale( p->vel, COM_RandomFloat( 50.0f, 100.0f ), p->vel );

		p->die = cl.time + 0.5;
		p->color = 3 - color;
		p->type = pt_grav;
	}
}

/*
===============
R_FlickerParticles

===============
*/
void R_FlickerParticles( const vec3_t org )
{
	particle_t	*p;
	int		i;

	for( i = 0; i < 15; i++ )
	{
		p = R_AllocParticle( NULL );
		if( !p ) return;

		VectorCopy( org, p->org );
		p->vel[0] = COM_RandomFloat( -32.0f, 32.0f );
		p->vel[1] = COM_RandomFloat( -32.0f, 32.0f );
		p->vel[2] = COM_RandomFloat( 80.0f, 143.0f );

		p->die = cl.time + 2.0f;
		p->type = pt_blob2;
		p->color = 254;
	}
}

/*
===============
R_StreakSplash

create a splash of streaks
===============
*/
void R_StreakSplash( const vec3_t pos, const vec3_t dir, int color, int count, float speed, int velocityMin, int velocityMax )
{
	vec3_t		vel, vel2;
	particle_t	*p;	
	int		i;

	VectorScale( dir, speed, vel );

	for( i = 0; i < count; i++ )
	{
		VectorAddScalar( vel, COM_RandomFloat( velocityMin, velocityMax ), vel2 );
		p = R_AllocTracer( pos, vel2, COM_RandomFloat( 0.1f, 0.5f ));
		if( !p ) return;

		p->type = pt_grav;
		p->color = color;
		p->ramp = 1.0f;
	}
}

/*
===============
R_DebugParticle

just for debug purposes
===============
*/
void R_DebugParticle( const vec3_t pos, byte r, byte g, byte b )
{
	particle_t	*p;

	p = R_AllocParticle( NULL );
	if( !p ) return;

	VectorCopy( pos, p->org );
	p->color = R_LookupColor( r, g, b );
	p->die = cl.time + 0.01f;
}

/*
===============
CL_Particle

pmove debugging particle
===============
*/
void CL_Particle( const vec3_t org, int color, float life, int zpos, int zvel )
{
	particle_t	*p;

	p = R_AllocParticle( NULL );
	if( !p ) return;

	if( org ) VectorCopy( org, p->org );
	p->die = cl.time + life;
	p->vel[2] += zvel;	// ???
	p->color = color;
}

/*
===============
R_TracerEffect

===============
*/
void R_TracerEffect( const vec3_t start, const vec3_t end )
{
	vec3_t	pos, vel, dir;
	float	len, speed;
	float	offset;

	speed = Q_max( tracerspeed->value, 3.0f );

	VectorSubtract( end, start, dir );
	len = VectorLength( dir );
	if( len == 0.0f ) return;

	VectorScale( dir, 1.0f / len, dir ); // normalize
	offset = COM_RandomFloat( -10.0f, 9.0f ) + traceroffset->value;
	VectorScale( dir, offset, vel );
	VectorAdd( start, vel, pos );
	VectorScale( dir, speed, vel );

	R_AllocTracer( pos, vel, len / speed );
}

/*
===============
R_UserTracerParticle

===============
*/
void R_UserTracerParticle( float *org, float *vel, float life, int colorIndex, float length, byte deathcontext, void (*deathfunc)( particle_t *p ))
{
	particle_t	*p;

	if( colorIndex < 0 )
		return;

	if( colorIndex > ARRAYSIZE( gTracerColors ))
	{
		Con_Printf( S_ERROR "UserTracer with color > %d\n", ARRAYSIZE( gTracerColors ));
		return;
	}

	if(( p = R_AllocTracer( org, vel, life )) != NULL )
	{
		p->context = deathcontext;
		p->deathfunc = deathfunc;
		p->color = colorIndex;
		p->ramp = length;
	}
}

/*
===============
R_TracerParticles

allow more customization
===============
*/
particle_t *R_TracerParticles( float *org, float *vel, float life )
{
	return R_AllocTracer( org, vel, life );
}

/*
===============
R_SparkStreaks

create a streak tracers
===============
*/
void R_SparkStreaks( const vec3_t pos, int count, int velocityMin, int velocityMax )
{
	particle_t	*p;
	vec3_t		vel;
	int		i;
	
	for( i = 0; i<count; i++ )
	{
		vel[0] = COM_RandomFloat( velocityMin, velocityMax );
		vel[1] = COM_RandomFloat( velocityMin, velocityMax );
		vel[2] = COM_RandomFloat( velocityMin, velocityMax );

		p = R_AllocTracer( pos, vel, COM_RandomFloat( 0.1f, 0.5f ));
		if( !p ) return;

		p->color = 5;
		p->type = pt_grav;
		p->ramp = 0.5f;
	}
}

/*
===============
R_Implosion

make implosion tracers
===============
*/
void R_Implosion( const vec3_t end, float radius, int count, float life )
{
	float		dist = ( radius / 100.0f );
	vec3_t		start, temp, vel;
	float		factor;
	particle_t	*p;
	int		i;

	if( life <= 0.0f ) life = 0.1f; // to avoid divide by zero
	factor = -1.0 / life;

	for ( i = 0; i < count; i++ )
	{
		temp[0] = dist * COM_RandomFloat( -100.0f, 100.0f );
		temp[1] = dist * COM_RandomFloat( -100.0f, 100.0f );
		temp[2] = dist * COM_RandomFloat( 0.0f, 100.0f );
		VectorScale( temp, factor, vel );
		VectorAdd( temp, end, start );

		if(( p = R_AllocTracer( start, vel, life )) == NULL )
			return;

		p->type = pt_explode;
	}
}

/*
===============
CL_ReadPointFile_f

===============
*/
void CL_ReadPointFile_f( void )
{
	char		*afile, *pfile;
	vec3_t		org;
	int		count;
	particle_t	*p;
	char		filename[64];
	string		token;
	
	Q_snprintf( filename, sizeof( filename ), "maps/%s.pts", clgame.mapname );
	afile = FS_LoadFile( filename, NULL, false );

	if( !afile )
	{
		Con_Printf( S_ERROR "couldn't open %s\n", filename );
		return;
	}
	
	Con_Printf( "Reading %s...\n", filename );

	count = 0;
	pfile = afile;

	while( 1 )
	{
		pfile = COM_ParseFile( pfile, token );
		if( !pfile ) break;
		org[0] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token );
		if( !pfile ) break;
		org[1] = Q_atof( token );

		pfile = COM_ParseFile( pfile, token );
		if( !pfile ) break;
		org[2] = Q_atof( token );

		count++;
		
		if( !cl_free_particles )
		{
			Con_Printf( S_ERROR "not enough free particles!\n" );
			break;
		}

		// NOTE: can't use R_AllocParticle because this command
		// may be executed from the console, while frametime is 0
		p = cl_free_particles;
		cl_free_particles = p->next;
		p->next = cl_active_particles;
		cl_active_particles = p;

		p->ramp = 0;		
		p->type = pt_static;
		p->die = cl.time + 99999;
		p->color = (-count) & 15;
		VectorCopy( org, p->org );
		VectorClear( p->vel );
	}

	Mem_Free( afile );

	if( count ) Con_Printf( "%i points read\n", count );
	else Con_Printf( "map %s has no leaks!\n", clgame.mapname );
}