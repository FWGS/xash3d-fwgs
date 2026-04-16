/*
gl_draw.c - orthogonal drawing stuff
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

/*
=============
R_GetImageParms
=============
*/
void R_GetTextureParms( int *w, int *h, int texnum )
{
	gl_texture_t	*glt;

	glt = R_GetTexture( texnum );
	if( w ) *w = glt->srcWidth;
	if( h ) *h = glt->srcHeight;
}

/*
=============
R_DrawStretchPic
=============
*/
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	GL_Bind( XASH_TEXTURE0, texnum );

	pglBegin( GL_QUADS );
		pglTexCoord2f( s1, t1 );
		pglVertex2f( x, y );

		pglTexCoord2f( s2, t1 );
		pglVertex2f( x + w, y );

		pglTexCoord2f( s2, t2 );
		pglVertex2f( x + w, y + h );

		pglTexCoord2f( s1, t2 );
		pglVertex2f( x, y + h );
	pglEnd();
}

/*
=============
GL_UpdateTexture
=============
*/
void GL_UpdateTexture( int texnum, int cols, int rows, int width, int height, const byte *buffer, pixformat_t fmt )
{
	byte		*raw = NULL;
	gl_texture_t	*tex;
	GLenum		gl_format;

	switch( fmt )
	{
	case PF_RGBA_32:
		gl_format = GL_RGBA;
		break;
	case PF_BGRA_32:
		gl_format = GL_BGRA;
		break;
	case PF_RGB_24:
		gl_format = GL_RGB;
		break;
	case PF_BGR_24:
		gl_format = GL_BGR;
		break;
	case PF_LUMINANCE:
		gl_format = GL_LUMINANCE;
		break;
	default:
		gEngfuncs.Con_DPrintf( S_ERROR "%s: unsupported pixel format %i\n", __func__, fmt );
		return;
	}

	if( !GL_Support( GL_ARB_TEXTURE_NPOT_EXT ))
	{
		width = NearestPOW( width, true );
		height = NearestPOW( height, false );
	}

	if( cols != width || rows != height )
	{
		raw = GL_ResampleTexture( buffer, cols, rows, width, height, false );
		cols = width;
		rows = height;
	}
	else
		raw = (byte *)buffer;

	if( cols > glConfig.max_2d_texture_size )
		gEngfuncs.Host_Error( "%s: size %i exceeds hardware limits\n", __func__, cols );
	if( rows > glConfig.max_2d_texture_size )
		gEngfuncs.Host_Error( "%s: size %i exceeds hardware limits\n", __func__, rows );

	tex = R_GetTexture( texnum );
	GL_Bind( GL_KEEP_UNIT, texnum );

	if( cols == tex->width && rows == tex->height )
		pglTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, gl_format, GL_UNSIGNED_BYTE, raw );
	else
	{
		tex->width = cols;
		tex->height = rows;
		pglTexImage2D( GL_TEXTURE_2D, 0, tex->format, cols, rows, 0, gl_format, GL_UNSIGNED_BYTE, raw );
	}

	GL_ApplyTextureParams( tex );
}

/*
===============
R_Set2DMode
===============
*/
void R_Set2DMode( qboolean enable )
{
	if( enable )
	{
		matrix4x4 projection_matrix, worldview_matrix;

		if( glState.in2DMode )
			return;

		// set 2D virtual screen size
		switch( tr.rotation )
		{
		case REF_ROTATE_CW:
			pglViewport( 0, 0, gpGlobals->height, gpGlobals->width );
			Matrix4x4_CreateOrtho( projection_matrix, 0, gpGlobals->height, gpGlobals->width, 0, -99999, 99999 );
			Matrix4x4_ConcatRotate( projection_matrix, 90, 0, 0, 1 );
			Matrix4x4_ConcatTranslate( projection_matrix, 0, -gpGlobals->height, 0 );
			break;
		case REF_ROTATE_CCW:
			pglViewport( 0, 0, gpGlobals->height, gpGlobals->width );
			Matrix4x4_CreateOrtho( projection_matrix, 0, gpGlobals->height, gpGlobals->width, 0, -99999, 99999 );
			Matrix4x4_ConcatRotate( projection_matrix, -90, 0, 0, 1 );
			Matrix4x4_ConcatTranslate( projection_matrix, -gpGlobals->width, 0, 0 );
			break;
		default:
			pglViewport( 0, 0, gpGlobals->width, gpGlobals->height );
			Matrix4x4_CreateOrtho( projection_matrix, 0, gpGlobals->width, gpGlobals->height, 0, -99999, 99999 );
			break;
		}

		pglMatrixMode( GL_PROJECTION );
		GL_LoadMatrix( projection_matrix );

		pglMatrixMode( GL_MODELVIEW );
		Matrix4x4_LoadIdentity( worldview_matrix );
		GL_LoadMatrix( worldview_matrix );

		GL_Cull( GL_NONE );

		pglDepthMask( GL_FALSE );
		pglDisable( GL_DEPTH_TEST );
		pglEnable( GL_ALPHA_TEST );
		pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

		if( glConfig.max_multisamples > 1 && gl_msaa.value )
			pglDisable( GL_MULTISAMPLE_ARB );

		glState.in2DMode = true;
		RI.currententity = NULL;
		RI.currentmodel = NULL;
	}
	else
	{
		pglDepthMask( GL_TRUE );
		pglEnable( GL_DEPTH_TEST );
		glState.in2DMode = false;

		pglMatrixMode( GL_PROJECTION );
		GL_LoadMatrix( RI.projectionMatrix );

		pglMatrixMode( GL_MODELVIEW );
		GL_LoadMatrix( RI.worldviewMatrix );

		if( glConfig.max_multisamples > 1 )
		{
			if( gl_msaa.value )
				pglEnable( GL_MULTISAMPLE_ARB );
			else pglDisable( GL_MULTISAMPLE_ARB );
		}

		GL_Cull( GL_FRONT );
	}
}
