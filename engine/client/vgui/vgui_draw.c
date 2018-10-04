/*
vgui_draw.c - vgui draw methods
Copyright (C) 2011 Uncle Mike

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
#include "vgui_draw.h"

convar_t	*vgui_colorstrings;
int	g_textures[VGUI_MAX_TEXTURES];
int	g_textureId = 0;

/*
================
VGUI_DrawInit

Startup VGUI backend
================
*/
void VGUI_DrawInit( void )
{
	memset( g_textures, 0, sizeof( g_textures ));
	g_textureId = 0;

	vgui_colorstrings = Cvar_Get( "vgui_colorstrings", "0", FCVAR_ARCHIVE, "allow colorstrings in VGUI texts" );
}

/*
================
VGUI_DrawShutdown

Release all the textures
================
*/
void VGUI_DrawShutdown( void )
{
	int	i;

	for( i = 1; i < g_textureId; i++ )
	{
		GL_FreeTexture( g_textures[i] );
	}
}

/*
================
VGUI_GenerateTexture

generate unique texture number
================
*/
int VGUI_GenerateTexture( void )
{
	if( ++g_textureId >= VGUI_MAX_TEXTURES )
		Host_Error( "VGUI_GenerateTexture: VGUI_MAX_TEXTURES limit exceeded\n" );
	return g_textureId;
}

/*
================
VGUI_UploadTexture

Upload texture into video memory
================
*/
void VGUI_UploadTexture( int id, const char *buffer, int width, int height )
{
	rgbdata_t	r_image;
	char	texName[32];

	if( id <= 0 || id >= VGUI_MAX_TEXTURES )
	{
		Con_DPrintf( S_ERROR "VGUI_UploadTexture: bad texture %i. Ignored\n", id );
		return;
	}

	Q_snprintf( texName, sizeof( texName ), "*vgui%i", id );
	memset( &r_image, 0, sizeof( r_image ));

	r_image.width = width;
	r_image.height = height;
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * 4;
	r_image.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA;
	r_image.buffer = (byte *)buffer;

	g_textures[id] = GL_LoadTextureInternal( texName, &r_image, TF_IMAGE );
}

/*
================
VGUI_SetupDrawingRect

setup transparency etc
================
*/
void VGUI_SetupDrawingRect( int *pColor )
{
	pglEnable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglColor4ub( pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
}

/*
================
VGUI_SetupDrawingImage

setup transparency etc
================
*/
void VGUI_SetupDrawingImage( int *pColor )
{
	pglEnable( GL_BLEND );
	pglEnable( GL_ALPHA_TEST );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglColor4ub( pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
}

/*
================
VGUI_BindTexture

bind VGUI texture through private index
================
*/
void VGUI_BindTexture( int id )
{
	if( id > 0 && id < VGUI_MAX_TEXTURES && g_textures[id] )
	{
		GL_Bind( GL_TEXTURE0, g_textures[id] );
	}
	else
	{
		// NOTE: same as bogus index 2700 in GoldSrc
		GL_Bind( GL_TEXTURE0, g_textures[1] );
	}
}

/*
================
VGUI_EnableTexture

disable texturemode for fill rectangle
================
*/
void VGUI_EnableTexture( qboolean enable )
{
	if( enable ) pglEnable( GL_TEXTURE_2D );
	else pglDisable( GL_TEXTURE_2D );
}

/*
================
VGUI_DrawQuad

generic method to fill rectangle
================
*/
void VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr )
{
	pglBegin( GL_QUADS );
		pglTexCoord2f( ul->coord[0], ul->coord[1] );
		pglVertex2f( ul->point[0], ul->point[1] );

		pglTexCoord2f( lr->coord[0], ul->coord[1] );
		pglVertex2f( lr->point[0], ul->point[1] );

		pglTexCoord2f( lr->coord[0], lr->coord[1] );
		pglVertex2f( lr->point[0], lr->point[1] );

		pglTexCoord2f( ul->coord[0], lr->coord[1] );
		pglVertex2f( ul->point[0], lr->point[1] );
	pglEnd();
}

/*
================
VGUI_DrawBuffer

render the quads array
================
*/
void VGUI_DrawBuffer( const vpoint_t *buffer, int numVerts )
{
	if( numVerts <= 0 ) return;

	pglEnable( GL_BLEND );
	pglEnable( GL_ALPHA_TEST );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	pglEnableClientState( GL_VERTEX_ARRAY );
	pglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	pglEnableClientState( GL_COLOR_ARRAY );

	pglTexCoordPointer( 2, GL_FLOAT, sizeof( vpoint_t ), &buffer->coord[0] );
	pglColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( vpoint_t ), &buffer->color[0] );
	pglVertexPointer( 2, GL_FLOAT, sizeof( vpoint_t ), &buffer->point[0] );
	pglDrawArrays( GL_QUADS, 0, numVerts );

	pglDisableClientState( GL_VERTEX_ARRAY );
	pglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	pglDisableClientState( GL_COLOR_ARRAY );
}