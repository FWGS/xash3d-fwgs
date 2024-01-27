/*
gl_vgui.c - OpenGL vgui draw methods
Copyright (C) 2011 Uncle Mike
Copyright (C) 2019 a1batross
Copyright (C) 2020 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gu_local.h"

#define VGUI_MAX_TEXTURES	( MAX_TEXTURES / 2 )	// a half of total textures count
#if 1
static short	g_textures[VGUI_MAX_TEXTURES];
#else
static int	g_textures[VGUI_MAX_TEXTURES];
#endif
static int	g_textureId = 0;
static int	g_iBoundTexture;

/*
================
VGUI_DrawInit

Startup VGUI backend
================
*/
void GAME_EXPORT VGUI_DrawInit( void )
{
	memset( g_textures, 0, sizeof( g_textures ));
	g_textureId = g_iBoundTexture = 0;
}

/*
================
VGUI_DrawShutdown

Release all textures
================
*/
void GAME_EXPORT VGUI_DrawShutdown( void )
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
int GAME_EXPORT VGUI_GenerateTexture( void )
{
	if( ++g_textureId >= VGUI_MAX_TEXTURES )
		gEngfuncs.Host_Error( "VGUI_GenerateTexture: VGUI_MAX_TEXTURES limit exceeded\n" );
	return g_textureId;
}

/*
================
VGUI_UploadTexture

Upload texture into video memory
================
*/
void GAME_EXPORT VGUI_UploadTexture( int id, const char *buffer, int width, int height )
{
	rgbdata_t	r_image;
	char	texName[32];

	if( id <= 0 || id >= VGUI_MAX_TEXTURES )
	{
		gEngfuncs.Con_DPrintf( S_ERROR "VGUI_UploadTexture: bad texture %i. Ignored\n", id );
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
VGUI_CreateTexture

Create empty rgba texture and upload them into video memory
================
*/
void GAME_EXPORT VGUI_CreateTexture( int id, int width, int height )
{
	rgbdata_t	r_image;
	char	texName[32];

	if( id <= 0 || id >= VGUI_MAX_TEXTURES )
	{
		gEngfuncs.Con_Reportf( S_ERROR  "VGUI_CreateTexture: bad texture %i. Ignored\n", id );
		return;
	}

	Q_snprintf( texName, sizeof( texName ), "*vgui%i", id );
	memset( &r_image, 0, sizeof( r_image ));

	r_image.width = width;
	r_image.height = height;
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * 4;
	r_image.flags = IMAGE_HAS_ALPHA;
	r_image.buffer = NULL;

	g_textures[id] = GL_LoadTextureInternal( texName, &r_image, TF_IMAGE|TF_NEAREST );
	g_iBoundTexture = id;
}

void GAME_EXPORT VGUI_UploadTextureBlock( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
	if( id <= 0 || id >= VGUI_MAX_TEXTURES || g_textures[id] == 0 || g_textures[id] == tr.whiteTexture )
	{
		gEngfuncs.Con_Reportf( S_ERROR  "VGUI_UploadTextureBlock: bad texture %i. Ignored\n", id );
		return;
	}
#if 1

#else
	pglTexSubImage2D( GL_TEXTURE_2D, 0, drawX, drawY, blockWidth, blockHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
#endif
	g_iBoundTexture = id;
}

void GAME_EXPORT VGUI_SetupDrawingRect( int *pColor )
{
#if 1
	sceGuEnable( GU_BLEND );
	sceGuDisable( GU_ALPHA_TEST );
	sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
	sceGuColor( GUCOLOR4UB( pColor[0], pColor[1], pColor[2], 255 - pColor[3] ) );
#else
	pglEnable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglColor4ub( pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
#endif
}

void GAME_EXPORT VGUI_SetupDrawingText( int *pColor )
{
#if 1
	sceGuEnable( GU_BLEND );
	sceGuEnable( GU_ALPHA_TEST );
	sceGuAlphaFunc( GU_GREATER, 0x00, 0xff );
	sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
	sceGuColor( GUCOLOR4UB( pColor[0], pColor[1], pColor[2], 255 - pColor[3] ) );
#else
	pglEnable( GL_BLEND );
	pglEnable( GL_ALPHA_TEST );
	pglAlphaFunc( GL_GREATER, 0.0f );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglColor4ub( pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
#endif
}

void GAME_EXPORT VGUI_SetupDrawingImage( int *pColor )
{
#if 1
	sceGuEnable( GU_BLEND );
	sceGuEnable( GU_ALPHA_TEST );
	sceGuAlphaFunc( GU_GREATER, 0x00, 0xff );
	sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
	sceGuColor( GUCOLOR4UB( pColor[0], pColor[1], pColor[2], 255 - pColor[3] ) );
#else
	pglEnable( GL_BLEND );
	pglEnable( GL_ALPHA_TEST );
	pglAlphaFunc( GL_GREATER, 0.0f );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglColor4ub( pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
#endif
}

void GAME_EXPORT VGUI_BindTexture( int id )
{
	if( id > 0 && id < VGUI_MAX_TEXTURES && g_textures[id] )
	{
		GL_Bind( XASH_TEXTURE0, g_textures[id] );
		g_iBoundTexture = id;
	}
	else
	{
		// NOTE: same as bogus index 2700 in GoldSrc
		id = g_iBoundTexture = 1;
		GL_Bind( XASH_TEXTURE0, g_textures[id] );
	}
}

/*
================
VGUI_GetTextureSizes

returns wide and tall for currently binded texture
================
*/
void GAME_EXPORT VGUI_GetTextureSizes( int *width, int *height )
{
	gl_texture_t	*glt;
	int		texnum;

	if( g_iBoundTexture )
		texnum = g_textures[g_iBoundTexture];
	else texnum = tr.defaultTexture;

	glt = R_GetTexture( texnum );
	if( width ) *width = glt->srcWidth;
	if( height ) *height = glt->srcHeight;
}

/*
================
VGUI_EnableTexture

disable texturemode for fill rectangle
================
*/
void GAME_EXPORT VGUI_EnableTexture( qboolean enable )
{
#if 1
	if( enable ) sceGuEnable( GU_TEXTURE_2D );
	else sceGuDisable( GU_TEXTURE_2D );
#else
	if( enable ) pglEnable( GL_TEXTURE_2D );
	else pglDisable( GL_TEXTURE_2D );
#endif
}

/*
================
VGUI_DrawQuad

generic method to fill rectangle
================
*/
void GAME_EXPORT VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr )
{
	int width, height;
	float xscale, yscale;
#if 1
	gl_texture_t	*glt;
	float twidth, theight;
#endif
	gEngfuncs.CL_GetScreenInfo( &width, &height );

	xscale = gpGlobals->width / (float)width;
	yscale = gpGlobals->height / (float)height;

	ASSERT( ul != NULL && lr != NULL );
#if 1
	glt = R_GetTexture( g_iBoundTexture ? g_textures[g_iBoundTexture] : tr.defaultTexture );
	
	gu_vert_htv_t* const out = ( gu_vert_htv_t* )sceGuGetMemory( sizeof( gu_vert_htv_t ) * 2 );
	out[0].u = ul->coord[0] * glt->width;
	out[0].v = ul->coord[1] * glt->height;
	out[0].x = ul->point[0] * xscale; 
	out[0].y = ul->point[1] * yscale;
	out[0].z = 0;	
	out[1].u = lr->coord[0] * glt->width;
	out[1].v = lr->coord[1] * glt->height;	
	out[1].x = lr->point[0] * xscale; 
	out[1].y = lr->point[1] * yscale;
	out[1].z = 0;	
	sceGuDrawArray( GU_SPRITES, GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, out );	
#else
	pglBegin( GL_QUADS );
		pglTexCoord2f( ul->coord[0], ul->coord[1] );
		pglVertex2f( ul->point[0] * xscale, ul->point[1] * yscale );

		pglTexCoord2f( lr->coord[0], ul->coord[1] );
		pglVertex2f( lr->point[0] * xscale, ul->point[1] * yscale );

		pglTexCoord2f( lr->coord[0], lr->coord[1] );
		pglVertex2f( lr->point[0] * xscale, lr->point[1] * yscale );

		pglTexCoord2f( ul->coord[0], lr->coord[1] );
		pglVertex2f( ul->point[0] * xscale, lr->point[1] * yscale );
	pglEnd();
#endif
}
