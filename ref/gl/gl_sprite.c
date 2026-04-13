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

#include "gl_local.h"
#include "pm_local.h"
#include "sprite.h"
#include "studio.h"
#include "entity_types.h"

#define GLARE_FALLOFF	19000.0f

static float sprite_radius;

/*
================
R_GetSpriteFrameInterpolant

NOTE: we using prevblending[0] and [1] for holds interval
between frames where are we lerping
================
*/
static float R_GetSpriteFrameInterpolant( cl_entity_t *ent, mspriteframe_t **oldframe, mspriteframe_t **curframe )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	int		i, j, numframes, frame;
	float		lerpFrac, time, jtime, jinterval;
	float		*pintervals, fullinterval, targettime;
	int		m_fDoInterp;

	psprite = ent->model->cache.data;
	frame = (int)ent->curstate.frame;
	lerpFrac = 1.0f;

	// misc info
	m_fDoInterp = (ent->curstate.effects & EF_NOINTERP) ? false : true;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		gEngfuncs.Con_Reportf( S_WARN "%s: no such frame %d (%s)\n", __func__, frame, ent->model->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == FRAME_SINGLE )
	{
		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != FRAME_SINGLE )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 1.0f;
			}

			if( ent->latched.sequencetime < gp_cl->time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = gp_cl->time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (gp_cl->time - ent->latched.sequencetime) * 11.0f;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		if( ent->latched.prevblending[0] >= psprite->numframes )
		{
			// reset interpolation on change model
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			ent->latched.sequencetime = gp_cl->time;
			lerpFrac = 0.0f;
		}

		// get the interpolated frames
		if( oldframe ) *oldframe = psprite->frames[ent->latched.prevblending[0]].frameptr;
		if( curframe ) *curframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == FRAME_GROUP )
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];
		jinterval = pintervals[1] - pintervals[0];
		time = gp_cl->time;
		jtime = 0.0f;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		// LordHavoc: since I can't measure the time properly when it loops from numframes - 1 to 0,
		// i instead measure the time of the first frame, hoping it is consistent
		for( i = 0, j = numframes - 1; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
			j = i;
			jinterval = pintervals[i] - jtime;
			jtime = pintervals[i];
		}

		if( m_fDoInterp )
			lerpFrac = (targettime - jtime) / jinterval;
		else j = i; // no lerping

		// get the interpolated frames
		if( oldframe ) *oldframe = pspritegroup->frames[j];
		if( curframe ) *curframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == FRAME_ANGLED )
	{
		// e.g. doom-style sprite monsters
		float	yaw = ent->angles[YAW];
		int	angleframe = (int)(Q_rint(( RI.rvp.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != FRAME_ANGLED )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 1.0f;
			}

			if( ent->latched.sequencetime < gp_cl->time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = gp_cl->time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (gp_cl->time - ent->latched.sequencetime) * ent->curstate.framerate;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = gp_cl->time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		pspritegroup = (mspritegroup_t *)psprite->frames[ent->latched.prevblending[0]].frameptr;
		if( oldframe ) *oldframe = pspritegroup->frames[angleframe];

		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		if( curframe ) *curframe = pspritegroup->frames[angleframe];
	}

	return lerpFrac;
}

/*
================
R_CullSpriteModel

Cull sprite model by bbox
================
*/
static qboolean R_CullSpriteModel( cl_entity_t *e, vec3_t origin )
{
	vec3_t	sprite_mins, sprite_maxs;
	float	scale = 1.0f;

	if( !e->model->cache.data )
		return true;

	if( e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	// scale original bbox (no rotation for sprites)
	VectorScale( e->model->mins, scale, sprite_mins );
	VectorScale( e->model->maxs, scale, sprite_maxs );

	sprite_radius = RadiusFromBounds( sprite_mins, sprite_maxs );

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
	float	dist, brightness;
	vec3_t	glowDist;
	pmtrace_t	*tr;

	VectorSubtract( origin, RI.rvp.vieworigin, glowDist );
	dist = VectorLength( glowDist );

	if( !FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
	{
		tr = gEngfuncs.EV_VisTraceLine( RI.rvp.vieworigin, origin, r_traceglow.value ? PM_GLASS_IGNORE : (PM_GLASS_IGNORE|PM_STUDIO_IGNORE));

		if(( 1.0f - tr->fraction ) * dist > 8.0f )
			return 0.0f;
	}

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
		float	blend;
		vec3_t	v;

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
	vec3_t	point;

	r_stats.c_sprite_polys++;

	pglBegin( GL_QUADS );
		pglTexCoord2f( 0.0f, 1.0f );
		VectorMA( org, frame->down * scale, v_up, point );
		VectorMA( point, frame->left * scale, v_right, point );
		pglVertex3fv( point );
		pglTexCoord2f( 0.0f, 0.0f );
		VectorMA( org, frame->up * scale, v_up, point );
		VectorMA( point, frame->left * scale, v_right, point );
		pglVertex3fv( point );
		pglTexCoord2f( 1.0f, 0.0f );
		VectorMA( org, frame->up * scale, v_up, point );
		VectorMA( point, frame->right * scale, v_right, point );
		pglVertex3fv( point );
		pglTexCoord2f( 1.0f, 1.0f );
		VectorMA( org, frame->down * scale, v_up, point );
		VectorMA( point, frame->right * scale, v_right, point );
		pglVertex3fv( point );
	pglEnd();
}

static qboolean R_SpriteHasLightmap( cl_entity_t *e, int texFormat )
{
	if( !r_sprite_lighting->value )
		return false;

	if( texFormat != SPR_ALPHTEST )
		return false;

	if( FBitSet( e->curstate.effects, EF_FULLBRIGHT ))
		return false;

	if( e->curstate.renderamt <= 127 )
		return false;

	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
	case kRenderTransAlpha:
	case kRenderTransTexture:
		break;
	default:
		return false;
	}

	return true;
}

/*
=================
R_SpriteAllowLerping
=================
*/
static qboolean R_SpriteAllowLerping( cl_entity_t *e, msprite_t *psprite )
{
	if( !r_sprite_lerping->value )
		return false;

	if( psprite->numframes <= 1 )
		return false;

	if( psprite->texFormat != SPR_ADDITIVE )
		return false;

	if( e->curstate.rendermode == kRenderNormal || e->curstate.rendermode == kRenderTransAlpha )
		return false;

	return true;
}

/*
=================
R_DrawSpriteModel
=================
*/
void R_DrawSpriteModel( cl_entity_t *e )
{
	mspriteframe_t	*frame = NULL, *oldframe = NULL;
	msprite_t		*psprite;
	model_t		*model;
	int		i, type;
	float		angle, dot, sr, cr;
	float		lerp = 1.0f, ilerp, scale;
	vec3_t		v_forward, v_right, v_up;
	vec3_t		origin, color, color2 = { 0.0f };

	if( FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
		return;

	model = e->model;
	psprite = (msprite_t * )model->cache.data;
	VectorCopy( e->origin, origin );	// set render origin

	// do movewith
	if( e->curstate.aiment > 0 && e->curstate.movetype == MOVETYPE_FOLLOW )
	{
		cl_entity_t	*parent;

		parent = CL_GetEntityByIndex( e->curstate.aiment );

		if( parent && parent->model )
		{
			if( parent->model->type == mod_studio && e->curstate.body > 0 )
			{
				int num = bound( 1, e->curstate.body, MAXSTUDIOATTACHMENTS );
				VectorCopy( parent->attachment[num-1], origin );
			}
			else VectorCopy( parent->origin, origin );
		}
	}

	scale = e->curstate.scale;
	if( !scale ) scale = 1.0f;

	if( R_SpriteOccluded( e, origin, &scale ))
		return; // sprite culled

	r_stats.c_sprite_models_drawn++;

	if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( false );

	// select properly rendermode
	switch( e->curstate.rendermode )
	{
	case kRenderTransAlpha:
		pglDepthMask( GL_FALSE );
		// fallthrough
	case kRenderTransColor:
	case kRenderTransTexture:
		pglEnable( GL_BLEND );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		break;
	case kRenderGlow:
		pglDisable( GL_DEPTH_TEST );
		// fallthrough
	case kRenderTransAdd:
		pglEnable( GL_BLEND );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
		pglDepthMask( GL_FALSE );
		break;
	case kRenderNormal:
	default:
		pglDisable( GL_BLEND );
		break;
	}

	// all sprites can have color
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglEnable( GL_ALPHA_TEST );

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

	if( R_SpriteHasLightmap( e, psprite->texFormat ))
	{
		colorVec lightColor = R_LightPoint( origin );
		// FIXME: collect light from dlights?
		color2[0] = (float)lightColor.r * ( 1.0f / 255.0f );
		color2[1] = (float)lightColor.g * ( 1.0f / 255.0f );
		color2[2] = (float)lightColor.b * ( 1.0f / 255.0f );
		// NOTE: sprites with 'lightmap' looks ugly when alpha func is GL_GREATER 0.0
		// NOTE: make them easier to see with 0.3333, was 0.5 in original
		pglAlphaFunc( GL_GREATER, 1.0f / 3.0f );
	}

	if( R_SpriteAllowLerping( e, psprite ))
		lerp = R_GetSpriteFrameInterpolant( e, &oldframe, &frame );
	else frame = oldframe = gEngfuncs.R_GetSpriteFrame( model, e->curstate.frame, e->angles[YAW] );

	type = psprite->type;

	// automatically roll parallel sprites if requested
	if( e->angles[ROLL] != 0.0f && type == SPR_FWD_PARALLEL )
		type = SPR_FWD_PARALLEL_ORIENTED;

	switch( type )
	{
	case SPR_ORIENTED:
		AngleVectors( e->angles, v_forward, v_right, v_up );
		VectorScale( v_forward, 0.01f, v_forward );	// to avoid z-fighting
		VectorSubtract( origin, v_forward, origin );
		break;
	case SPR_FACING_UPRIGHT:
		VectorSet( v_right, origin[1] - RI.rvp.vieworigin[1], -(origin[0] - RI.rvp.vieworigin[0]), 0.0f );
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_UPRIGHT:
		dot = RI.vforward[2];
		if(( dot > 0.999848f ) || ( dot < -0.999848f ))	// cos(1 degree) = 0.999848
			return; // invisible
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorSet( v_right, RI.vforward[1], -RI.vforward[0], 0.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_ORIENTED:
		angle = e->angles[ROLL] * (M_PI2 / 360.0f);
		SinCos( angle, &sr, &cr );
		for( i = 0; i < 3; i++ )
		{
			v_right[i] = (RI.vright[i] * cr + RI.vup[i] * sr);
			v_up[i] = RI.vright[i] * -sr + RI.vup[i] * cr;
		}
		break;
	case SPR_FWD_PARALLEL: // normal sprite
	default:
		VectorCopy( RI.vright, v_right );
		VectorCopy( RI.vup, v_up );
		break;
	}

	if( psprite->facecull == SPR_CULL_NONE )
		GL_Cull( GL_NONE );

	if( oldframe == frame )
	{
		// draw the single non-lerped frame
		pglColor4f( color[0], color[1], color[2], tr.blend );
		GL_Bind( XASH_TEXTURE0, frame->gl_texturenum );
		R_DrawSpriteQuad( frame, origin, v_right, v_up, scale );
	}
	else
	{
		// draw two combined lerped frames
		lerp = bound( 0.0f, lerp, 1.0f );
		ilerp = 1.0f - lerp;

		if( ilerp != 0.0f )
		{
			pglColor4f( color[0], color[1], color[2], tr.blend * ilerp );
			GL_Bind( XASH_TEXTURE0, oldframe->gl_texturenum );
			R_DrawSpriteQuad( oldframe, origin, v_right, v_up, scale );
		}

		if( lerp != 0.0f )
		{
			pglColor4f( color[0], color[1], color[2], tr.blend * lerp );
			GL_Bind( XASH_TEXTURE0, frame->gl_texturenum );
			R_DrawSpriteQuad( frame, origin, v_right, v_up, scale );
		}
	}

	// draw the sprite 'lightmap' :-)
	if( R_SpriteHasLightmap( e, psprite->texFormat ))
	{
		if( !r_lightmap->value )
			pglEnable( GL_BLEND );
		else pglDisable( GL_BLEND );
		pglDepthFunc( GL_EQUAL );
		pglDisable( GL_ALPHA_TEST );
		pglBlendFunc( GL_ZERO, GL_SRC_COLOR );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

		pglColor4f( color2[0], color2[1], color2[2], tr.blend );
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
		R_DrawSpriteQuad( frame, origin, v_right, v_up, scale );
		pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
		pglDepthFunc( GL_LEQUAL );
		pglDisable( GL_BLEND );
	}

	if( psprite->facecull == SPR_CULL_NONE )
		GL_Cull( GL_FRONT );

	pglDisable( GL_ALPHA_TEST );
	pglDepthMask( GL_TRUE );

	if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( true );

	if( e->curstate.rendermode != kRenderNormal )
	{
		pglDisable( GL_BLEND );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
		pglEnable( GL_DEPTH_TEST );
	}
}
