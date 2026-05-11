/*
gl_sprite.c - sprite rendering
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
#include "sprite.h"
#include "studio.h"
#include "entity_types.h"

#define GLARE_FALLOFF 19000.0f

/*
================
R_CullSpriteModel

Cull sprite model by bbox
================
*/
static qboolean R_CullSpriteModel( cl_entity_t *e, vec3_t origin )
{
	vec3_t sprite_mins, sprite_maxs;
	float  scale = 1.0f;

	if( !e->model->cache.data )
		return true;

	if( e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	// scale original bbox (no rotation for sprites)
	VectorScale( e->model->mins, scale, sprite_mins );
	VectorScale( e->model->maxs, scale, sprite_maxs );

	VectorAdd( sprite_mins, origin, sprite_mins );
	VectorAdd( sprite_maxs, origin, sprite_maxs );

	return R_CullModel( e, sprite_mins, sprite_maxs );
}

/*
================
R_GlowSightDistance

Set sprite brightness factor
================
*/
static float R_SpriteGlowBlend( vec3_t origin, int rendermode, int renderfx, float *pscale )
{
	float  dist, brightness;
	vec3_t glowDist;

	VectorSubtract( origin, RI.rvp.vieworigin, glowDist );
	dist = VectorLength( glowDist );

	pmtrace_t *tr = gEngfuncs.EV_VisTraceLine( RI.rvp.vieworigin, origin, r_traceglow.value ? PM_GLASS_IGNORE : ( PM_GLASS_IGNORE | PM_STUDIO_IGNORE ));
	if(( 1.0f - tr->fraction ) * dist > 8.0f )
		return 0.0f;

	if( renderfx == kRenderFxNoDissipation )
		return 1.0f;

	brightness = GLARE_FALLOFF / ( dist * dist );
	brightness = bound( 0.05f, brightness, 1.0f );
	*pscale *= dist * ( 1.0f / 200.0f );

	return brightness;
}

/*
================
R_SpriteOccluded

Do occlusion test for glow-sprites
================
*/
static qboolean R_SpriteOccluded( cl_entity_t *e, vec3_t origin, float *pscale )
{
	if( e->curstate.rendermode == kRenderGlow )
	{
		float  blend;
		vec3_t v;

		TriWorldToScreen( origin, v );

		if( v[0] < RI.rvp.viewport[0] || v[0] > RI.rvp.viewport[0] + RI.rvp.viewport[2] )
			return true; // do scissor
		if( v[1] < RI.rvp.viewport[1] || v[1] > RI.rvp.viewport[1] + RI.rvp.viewport[3] )
			return true; // do scissor

		blend = R_SpriteGlowBlend( origin, e->curstate.rendermode, e->curstate.renderfx, pscale );
		tr.blend *= blend;

		if( blend <= 0.01f )
			return true; // faded
	}
	else
	{
		if( R_CullSpriteModel( e, origin ))
			return true;
	}

	return false;
}

/*
=================
R_DrawSpriteQuad
=================
*/
static void R_DrawSpriteQuad( mspriteframe_t *frame, vec3_t org, vec3_t v_right, vec3_t v_up, float scale )
{
	vec3_t  point;

	r_stats.c_sprite_polys++;
	/*image = R_GetTexture(frame->gl_texturenum);
	r_affinetridesc.pskin = image->pixels[0];
	r_affinetridesc.skinwidth = image->width;
	r_affinetridesc.skinheight = image->height;*/

	TriBegin( TRI_QUADS );
	TriTexCoord2f( 0.0f, 1.0f );
	VectorMA( org, frame->down * scale, v_up, point );
	VectorMA( point, frame->left * scale, v_right, point );
	TriVertex3fv( point );
	TriTexCoord2f( 0.0f, 0.0f );
	VectorMA( org, frame->up * scale, v_up, point );
	VectorMA( point, frame->left * scale, v_right, point );
	TriVertex3fv( point );
	TriTexCoord2f( 1.0f, 0.0f );
	VectorMA( org, frame->up * scale, v_up, point );
	VectorMA( point, frame->right * scale, v_right, point );
	TriVertex3fv( point );
	TriTexCoord2f( 1.0f, 1.0f );
	VectorMA( org, frame->down * scale, v_up, point );
	VectorMA( point, frame->right * scale, v_right, point );
	TriVertex3fv( point );
	TriEnd();
}

/*
=================
R_DrawSpriteModel
=================
*/
void R_DrawSpriteModel( cl_entity_t *e )
{
	mspriteframe_t *frame = NULL;
	msprite_t      *psprite;
	model_t        *model;
	int i, type;
	float          angle, dot, sr, cr, scale;
	vec3_t         v_forward, v_right, v_up;
	vec3_t         origin, color;

	model = e->model;
	psprite = (msprite_t *)model->cache.data;
	VectorCopy( e->origin, origin ); // set render origin

	// do movewith
	if( e->curstate.aiment > 0 && e->curstate.movetype == MOVETYPE_FOLLOW )
	{
		cl_entity_t *parent;

		parent = CL_GetEntityByIndex( e->curstate.aiment );

		if( parent && parent->model )
		{
			if( parent->model->type == mod_studio && e->curstate.body > 0 )
			{
				int num = bound( 1, e->curstate.body, MAXSTUDIOATTACHMENTS );
				VectorCopy( parent->attachment[num - 1], origin );
			}
			else
				VectorCopy( parent->origin, origin );
		}
	}

	scale = e->curstate.scale;
	if( !scale )
		scale = 1.0f;

	if( R_SpriteOccluded( e, origin, &scale ))
		return; // sprite culled

	r_stats.c_sprite_models_drawn++;

	if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( false );

	GL_SetRenderMode( e->curstate.rendermode );

	// NOTE: never pass sprites with rendercolor '0 0 0' it's a stupid Valve Hammer Editor bug
	if( e->curstate.rendercolor.r || e->curstate.rendercolor.g || e->curstate.rendercolor.b )
	{
		color[0] = (float)e->curstate.rendercolor.r * ( 1.0f / 255.0f );
		color[1] = (float)e->curstate.rendercolor.g * ( 1.0f / 255.0f );
		color[2] = (float)e->curstate.rendercolor.b * ( 1.0f / 255.0f );
	}
	else
	{
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
	}

	frame = gEngfuncs.R_GetSpriteFrame( model, e->curstate.frame, e->angles[YAW] );

	type = psprite->type;

	// automatically roll parallel sprites if requested
	if( e->angles[ROLL] != 0.0f && type == SPR_FWD_PARALLEL )
		type = SPR_FWD_PARALLEL_ORIENTED;

	switch( type )
	{
	case SPR_ORIENTED:
		AngleVectors( e->angles, v_forward, v_right, v_up );
		VectorScale( v_forward, 0.01f, v_forward ); // to avoid z-fighting
		VectorSubtract( origin, v_forward, origin );
		break;
	case SPR_FACING_UPRIGHT:
		VectorSet( v_right, origin[1] - RI.rvp.vieworigin[1], -( origin[0] - RI.rvp.vieworigin[0] ), 0.0f );
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_UPRIGHT:
		dot = RI.vforward[2];
		if(( dot > 0.999848f ) || ( dot < -0.999848f )) // cos(1 degree) = 0.999848
			return; // invisible
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorSet( v_right, RI.vforward[1], -RI.vforward[0], 0.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_ORIENTED:
		angle = e->angles[ROLL] * ( M_PI2 / 360.0f );
		SinCos( angle, &sr, &cr );
		for( i = 0; i < 3; i++ )
		{
			v_right[i] = ( RI.vright[i] * cr + RI.vup[i] * sr );
			v_up[i] = RI.vright[i] * -sr + RI.vup[i] * cr;
		}
		break;
	case SPR_FWD_PARALLEL: // normal sprite
	default:
		VectorCopy( RI.vright, v_right );
		VectorCopy( RI.vup, v_up );
		break;
	}

	// if( psprite->facecull == SPR_CULL_NONE )
	// GL_Cull( GL_NONE );

	// draw the single non-lerped frame
	_TriColor4f( color[0], color[1], color[2], tr.blend );
	GL_Bind( XASH_TEXTURE0, frame->gl_texturenum );
	R_DrawSpriteQuad( frame, origin, v_right, v_up, scale );
}
