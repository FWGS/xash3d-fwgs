/*
gl_fbo.c - offscreen render targets
Copyright (C) 2026 Alibek Omarov

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

#if !XASH_GLES || !XASH_GL_STATIC
static void GL_FreeRenderTarget( gl_rendertarget_t *target )
{
	if( target->fbo )
		pglDeleteFramebuffers( 1, &target->fbo );

	if( target->color )
		pglDeleteRenderbuffers( 1, &target->color );

	if( target->depth )
		pglDeleteRenderbuffers( 1, &target->depth );

	if( target->texnum )
		GL_FreeTexture( target->texnum );

	memset( target, 0, sizeof( *target ));
}

static GLuint GL_CreateRenderbuffer( GLenum format, int width, int height, int samples )
{
	GLuint rb;

	pglGenRenderbuffers( 1, &rb );
	pglBindRenderbuffer( GL_RENDERBUFFER, rb );

	if( samples )
		pglRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, format, width, height );
	else
		pglRenderbufferStorage( GL_RENDERBUFFER, format, width, height );

	return rb;
}

static qboolean GL_CreateRenderTarget( gl_rendertarget_t *rt, const char *name, int width, int height, int samples, rt_flags_t flags )
{
	if( unlikely( samples && FBitSet( flags, RT_COLOR_TEXTURE )))
	{
		// MSAA doesn't need texture, so it's unused path
		gEngfuncs.Con_Printf( S_ERROR "%s: multisampled color textures are not supported yet (%s)\n", __func__, name );
		return false;
	}

	pglGenFramebuffers( 1, &rt->fbo );
	pglBindFramebuffer( GL_FRAMEBUFFER, rt->fbo );

	if( !FBitSet( flags, RT_COLOR_TEXTURE ))
	{
		rt->color = GL_CreateRenderbuffer( GL_RGBA8, width, height, samples );
		pglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rt->color );
	}
	else
	{
		rt->texnum = GL_CreateTexture( name, width, height, NULL, TF_NOMIPMAP|TF_CLAMP|TF_HAS_ALPHA );
		if( !rt->texnum )
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: can't create %dx%d color texture for %s\n", __func__, width, height, name );
			pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
			GL_FreeRenderTarget( rt );
			return false;
		}

		gl_texture_t *tex = R_GetTexture( rt->texnum );

		if( tex->width != width || tex->height != height )
		{
			gEngfuncs.Con_Printf( S_ERROR "%s: for %s got %dx%d texture, expected %dx%d\n", __func__, name, tex->width, tex->height, width, height );
			pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
			GL_FreeRenderTarget( rt );
			return false;
		}

		pglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->texnum, 0 );
	}

	if( FBitSet( flags, RT_DEPTH ))
	{
		rt->depth = GL_CreateRenderbuffer( GL_DEPTH24_STENCIL8, width, height, samples );
		pglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt->depth );
		if( glState.stencilEnabled )
			pglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rt->depth );
	}

	GLenum status = pglCheckFramebufferStatus( GL_FRAMEBUFFER );

	pglBindRenderbuffer( GL_RENDERBUFFER, 0 );
	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );

	if( status != GL_FRAMEBUFFER_COMPLETE )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: %s is incomplete (0x%x)\n", __func__, name, status );
		GL_FreeRenderTarget( rt );
		return false;
	}

	rt->width = width;
	rt->height = height;
	rt->samples = samples;

	return true;
}

/*
================
GL_FreeRenderTargets

deletes all render targets, rendering goes directly to the window afterwards
================
*/
void GL_FreeRenderTargets( void )
{
	GL_FreeRenderTarget( &tr.targets.msaa );
	GL_FreeRenderTarget( &tr.targets.scene );
	GL_FreeRenderTarget( &tr.targets.screen );
	glState.sceneTargetDrawn = false;
}

static int GL_SceneTargetSamples( void )
{
	if( glConfig.max_multisamples <= 1 || !gl_msaa.value )
		return 0;

	if( !GL_Support( GL_FRAMEBUFFER_MULTISAMPLE_EXT ) || !GL_Support( GL_FRAMEBUFFER_BLIT_EXT ))
		return 0;

	GLint max_samples = 0;
	pglGetIntegerv( GL_MAX_SAMPLES, &max_samples );

	return Q_min( glConfig.max_multisamples, max_samples );
}

static void GL_GetWindowSize( int *width, int *height )
{
	if( tr.rotation & 1 )
	{
		*width = gpGlobals->height;
		*height = gpGlobals->width;
	}
	else
	{
		*width = gpGlobals->width;
		*height = gpGlobals->height;
	}
}

/*
================
GL_GetSceneTargetSize

size of the 3D scene surface in the same orientation as gpGlobals,
window size when render targets are inactive
================
*/
void GL_GetSceneTargetSize( int *width, int *height )
{
	if( !GL_RenderTargetsActive( ))
	{
		*width = gpGlobals->width;
		*height = gpGlobals->height;
	}
	else if( tr.rotation & 1 )
	{
		*width = tr.targets.scene.height;
		*height = tr.targets.scene.width;
	}
	else
	{
		*width = tr.targets.scene.width;
		*height = tr.targets.scene.height;
	}
}

/*
================
GL_CheckRenderTargets

called at frame start, creates, resizes or frees render targets
to match r_scene_scale and MSAA settings, binds the screen target
================
*/
void GL_CheckRenderTargets( void )
{
	int width, height;
	float scale = bound( 0.1f, r_scene_scale.value, 8.0f );
	qboolean wanted = scale != 1.0f;

	GL_GetWindowSize( &width, &height );

	if( FBitSet( r_scene_scale.flags, FCVAR_CHANGED ))
	{
		ClearBits( r_scene_scale.flags, FCVAR_CHANGED );
		tr.targets.failed = false;

		if( wanted && !GL_Support( GL_FRAMEBUFFER_OBJECT_EXT ))
			gEngfuncs.Con_Printf( S_WARN "r_scene_scale requires framebuffer objects, which are not supported\n" );
	}

	if( !wanted || !GL_Support( GL_FRAMEBUFFER_OBJECT_EXT ))
	{
		GL_FreeRenderTargets();
		return;
	}

	int scene_width = Q_max( 1, Q_rint( width * scale ));
	int scene_height = Q_max( 1, Q_rint( height * scale ));
	int samples = GL_SceneTargetSamples();

	if( GL_RenderTargetsActive( ) && tr.targets.scene.width == scene_width && tr.targets.scene.height == scene_height
		&& tr.targets.screen.width == width && tr.targets.screen.height == height
		&& tr.targets.msaa.samples == samples )
	{
		pglBindFramebuffer( GL_FRAMEBUFFER, tr.targets.screen.fbo );
		glState.sceneTargetDrawn = false;
		return;
	}

	GL_FreeRenderTargets();

	if( tr.targets.failed )
		return;

	if( samples && !GL_CreateRenderTarget( &tr.targets.msaa, "*fbo_msaa", scene_width, scene_height, samples, RT_DEPTH ))
	{
		gEngfuncs.Con_Printf( S_WARN "r_scene_scale: can't create %dx multisampled render target, continuing without\n", samples );
		samples = 0;
	}

	rt_flags_t scene_flags = RT_COLOR_TEXTURE;

	if( !samples )
		SetBits( scene_flags, RT_DEPTH );

	if( !GL_CreateRenderTarget( &tr.targets.scene, "*fbo_scene", scene_width, scene_height, 0, scene_flags )
		|| !GL_CreateRenderTarget( &tr.targets.screen, "*fbo_screen", width, height, 0, RT_COLOR_TEXTURE|RT_DEPTH ))
	{
		GL_FreeRenderTargets();
		tr.targets.failed = true;
		gEngfuncs.Con_Printf( S_ERROR "r_scene_scale: can't create render targets, rendering directly to the window\n" );
		return;
	}

	gEngfuncs.Con_Reportf( "%s: scene %dx%d (%dx MSAA), screen %dx%d\n", __func__, scene_width, scene_height, samples, width, height );

	pglBindFramebuffer( GL_FRAMEBUFFER, tr.targets.screen.fbo );
}

static void GL_DrawTargetQuad( const gl_rendertarget_t *rt, int width, int height, GLint filter )
{
	matrix4x4 m;

	if( !rt->texnum )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: render target has no color texture\n", __func__ );
		return;
	}

	pglViewport( 0, 0, width, height );
	Matrix4x4_CreateOrtho( m, 0, width, height, 0, -99999, 99999 );
	pglMatrixMode( GL_PROJECTION );
	GL_LoadMatrix( m );

	Matrix4x4_LoadIdentity( m );
	pglMatrixMode( GL_MODELVIEW );
	GL_LoadMatrix( m );

	GL_Cull( GL_NONE );
	pglDisable( GL_DEPTH_TEST );
	pglDepthMask( GL_FALSE );
	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_FOG );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	GL_Bind( XASH_TEXTURE0, rt->texnum );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );

	pglBegin( GL_QUADS );
		pglTexCoord2f( 0.0f, 1.0f );
		pglVertex2f( 0.0f, 0.0f );
		pglTexCoord2f( 1.0f, 1.0f );
		pglVertex2f( width, 0.0f );
		pglTexCoord2f( 1.0f, 0.0f );
		pglVertex2f( width, height );
		pglTexCoord2f( 0.0f, 0.0f );
		pglVertex2f( 0.0f, height );
	pglEnd();

	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
}

static void GL_BlitTarget( const gl_rendertarget_t *src, GLuint dst_fbo, int dst_width, int dst_height, GLenum filter )
{
	pglBindFramebuffer( GL_READ_FRAMEBUFFER, src->fbo );
	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, dst_fbo );
	pglBlitFramebuffer( 0, 0, src->width, src->height, 0, 0, dst_width, dst_height, GL_COLOR_BUFFER_BIT, filter );
}

/*
================
GL_BindSceneTarget

binds the surface 3D passes draw into
================
*/
void GL_BindSceneTarget( void )
{
	if( !GL_RenderTargetsActive( ))
		return;

	if( tr.targets.msaa.fbo )
		pglBindFramebuffer( GL_FRAMEBUFFER, tr.targets.msaa.fbo );
	else
		pglBindFramebuffer( GL_FRAMEBUFFER, tr.targets.scene.fbo );

	glState.sceneTargetDrawn = true;
}

/*
================
GL_BindWindowTarget

binds the window framebuffer for passes that read back their result, like cubemap shots
================
*/
void GL_BindWindowTarget( void )
{
	if( !GL_RenderTargetsActive( ))
		return;

	pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

/*
================
GL_BindScreenTarget

resolves and upscales the scene into the screen target if it was drawn this frame,
then binds the screen target for 2D drawing
================
*/
void GL_BindScreenTarget( void )
{
	const gl_rendertarget_t *screen = &tr.targets.screen;

	if( !GL_RenderTargetsActive( ))
		return;

	if( glState.sceneTargetDrawn )
	{
		GLint filter = gl_fbo_nearest.value ? GL_NEAREST : GL_LINEAR;

		if( tr.targets.msaa.fbo )
			GL_BlitTarget( &tr.targets.msaa, tr.targets.scene.fbo, tr.targets.scene.width, tr.targets.scene.height, GL_NEAREST );

		if( GL_Support( GL_FRAMEBUFFER_BLIT_EXT ))
		{
			GL_BlitTarget( &tr.targets.scene, screen->fbo, screen->width, screen->height, filter );
		}
		else
		{
			pglBindFramebuffer( GL_FRAMEBUFFER, screen->fbo );
			GL_DrawTargetQuad( &tr.targets.scene, screen->width, screen->height, filter );
		}

		glState.sceneTargetDrawn = false;
	}

	pglBindFramebuffer( GL_FRAMEBUFFER, screen->fbo );
}

/*
================
GL_PresentScreenTarget

copies the screen target into the window before the buffer swap
================
*/
void GL_PresentScreenTarget( void )
{
	const gl_rendertarget_t *screen = &tr.targets.screen;

	if( !GL_RenderTargetsActive( ))
		return;

	if( GL_Support( GL_FRAMEBUFFER_BLIT_EXT ))
	{
		GL_BlitTarget( screen, 0, screen->width, screen->height, GL_NEAREST );
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
	else
	{
		pglBindFramebuffer( GL_FRAMEBUFFER, 0 );
		GL_DrawTargetQuad( screen, screen->width, screen->height, GL_NEAREST );
	}
}

#endif // !XASH_GLES || !XASH_GL_STATIC
