/*
gl_backend.c - rendering backend
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
#include "xash3d_mathlib.h"

static char r_speeds_msg[MAX_SYSPATH];
ref_speeds_t r_stats; // r_speeds counters

/*
===============
R_SpeedsMessage
===============
*/
qboolean R_SpeedsMessage( char *out, size_t size )
{
	if( gEngfuncs.drawFuncs->R_SpeedsMessage != NULL )
	{
		if( gEngfuncs.drawFuncs->R_SpeedsMessage( out, size ))
			return true;
		// otherwise pass to default handler
	}

	if( r_speeds->value <= 0 ) return false;
	if( !out || !size ) return false;

	Q_strncpy( out, r_speeds_msg, size );

	return true;
}

/*
==============
GL_BackendStartFrame
==============
*/
void GL_BackendStartFrame( void )
{
	r_speeds_msg[0] = '\0';
}

/*
==============
GL_BackendEndFrame
==============
*/
void GL_BackendEndFrame( void )
{
	mleaf_t	*curleaf;

	if( r_speeds->value <= 0 || !RI.drawWorld )
		return;

	if( !RI.viewleaf )
		curleaf = WORLDMODEL->leafs;
	else curleaf = RI.viewleaf;

	switch( (int)r_speeds->value )
	{
	case 1:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i wpoly, %3i apoly\n%3i epoly, %3i spoly",
			r_stats.c_world_polys, r_stats.c_alias_polys, r_stats.c_studio_polys, r_stats.c_sprite_polys );
		break;
	case 2:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ),
			"Renderer: ^1Engine^7\n\n"
			"visible leafs:\n%3i leafs\ncurrent leaf %3i\n"
			"ReciusiveWorldNode: %3lf secs\nDrawTextureChains %lf",
			r_stats.c_world_leafs, (int)( curleaf - WORLDMODEL->leafs ), r_stats.t_world_node, r_stats.t_world_draw );
		break;
	case 3:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i alias models drawn\n%3i studio models drawn\n%3i sprites drawn",
			r_stats.c_alias_models_drawn, r_stats.c_studio_models_drawn, r_stats.c_sprite_models_drawn );
		break;
	case 4:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i static entities\n%3i normal entities\n%3i server entities",
			r_numStatics, r_numEntities - r_numStatics, (int)ENGINE_GET_PARM( PARM_NUMENTITIES ));
		break;
	case 5:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i tempents\n%3i viewbeams\n%3i particles",
			r_stats.c_active_tents_count, r_stats.c_view_beams_count, r_stats.c_particle_count );
		break;
	}

	memset( &r_stats, 0, sizeof( r_stats ));
}

/*
=================
GL_LoadTexMatrixExt
=================
*/
void GL_LoadTexMatrixExt( const float *glmatrix )
{
	Assert( glmatrix != NULL );
	pglMatrixMode( GL_TEXTURE );
	pglLoadMatrixf( glmatrix );
	glState.texIdentityMatrix[glState.activeTMU] = false;
}

/*
=================
GL_LoadMatrix
=================
*/
void GL_LoadMatrix( const matrix4x4 source )
{
	GLfloat	dest[16];

	Matrix4x4_ToArrayFloatGL( source, dest );
	pglLoadMatrixf( dest );
}

/*
=================
GL_LoadIdentityTexMatrix
=================
*/
void GL_LoadIdentityTexMatrix( void )
{
	if( glState.texIdentityMatrix[glState.activeTMU] )
		return;

	pglMatrixMode( GL_TEXTURE );
	pglLoadIdentity();
	glState.texIdentityMatrix[glState.activeTMU] = true;
}

/*
=================
GL_SelectTexture
=================
*/
void GL_SelectTexture( int tmu )
{
	if( !GL_Support( GL_ARB_MULTITEXTURE ))
		return;

	// don't allow negative texture units
	if( tmu < 0 )
		return;

	if( tmu >= GL_MaxTextureUnits( ))
	{
		gEngfuncs.Con_Reportf( S_ERROR "%s: bad tmu state %i\n", __func__, tmu );
		return;
	}

	if( glState.activeTMU == tmu )
		return;

	glState.activeTMU = tmu;

	if( pglActiveTextureARB )
	{
		pglActiveTextureARB( tmu + GL_TEXTURE0_ARB );

		if( tmu < glConfig.max_texture_coords )
			pglClientActiveTextureARB( tmu + GL_TEXTURE0_ARB );
	}
}

/*
=================
GL_Bind
=================
*/
void GL_Bind( int tmu, unsigned int texnum )
{
	const gl_texture_t *texture;
	GLuint glTarget;

	// missed or invalid texture?
	if( texnum <= 0 || texnum >= MAX_TEXTURES )
	{
		if( texnum != 0 )
			gEngfuncs.Con_DPrintf( S_ERROR "%s: invalid texturenum %d\n", __func__, texnum );
		texnum = tr.defaultTexture;
	}

	if( tmu != GL_KEEP_UNIT )
		GL_SelectTexture( tmu );
	else tmu = glState.activeTMU;

	texture = R_GetTexture( texnum );
	glTarget = texture->target;

	if( glTarget == GL_TEXTURE_2D_ARRAY_EXT )
		glTarget = GL_TEXTURE_2D;

	if( glState.currentTextureTargets[tmu] != glTarget )
	{
		GL_EnableTextureUnit( tmu, false );
		glState.currentTextureTargets[tmu] = glTarget;
		GL_EnableTextureUnit( tmu, true );
	}

	if( glState.currentTextures[tmu] == texture->texnum )
		return;

	pglBindTexture( texture->target, texture->texnum );
	glState.currentTextures[tmu] = texture->texnum;
	glState.currentTexturesIndex[tmu] = texnum;
}

/*
==============
GL_DisableAllTexGens
==============
*/
void GL_DisableAllTexGens( void )
{
	GL_TexGen( GL_S, 0 );
	GL_TexGen( GL_T, 0 );
	GL_TexGen( GL_R, 0 );
	GL_TexGen( GL_Q, 0 );
}

/*
==============
GL_CleanUpTextureUnits
==============
*/
void GL_CleanUpTextureUnits( int last )
{
	int	i;

	for( i = glState.activeTMU; i > (last - 1); i-- )
	{
		// disable upper units
		if( glState.currentTextureTargets[i] != GL_NONE )
		{
			pglDisable( glState.currentTextureTargets[i] );
			glState.currentTextureTargets[i] = GL_NONE;
			glState.currentTextures[i] = -1; // unbind texture
			glState.currentTexturesIndex[i] = 0;
		}

		GL_SetTexCoordArrayMode( GL_NONE );
		GL_LoadIdentityTexMatrix();
		GL_DisableAllTexGens();
		GL_SelectTexture( i - 1 );
	}
}

/*
==============
GL_CleanupAllTextureUnits
==============
*/
void GL_CleanupAllTextureUnits( void )
{
	if( !glw_state.initialized ) return;
	// force to cleanup all the units
	GL_SelectTexture( GL_MaxTextureUnits() - 1 );
	GL_CleanUpTextureUnits( 0 );
}

/*
=================
GL_MultiTexCoord2f
=================
*/
void GL_MultiTexCoord2f( int tmu, GLfloat s, GLfloat t )
{
	if( !GL_Support( GL_ARB_MULTITEXTURE ))
		return;

#ifndef XASH_GL_STATIC
	if( pglMultiTexCoord2f != NULL )
#endif
		pglMultiTexCoord2f( tmu + GL_TEXTURE0_ARB, s, t );
}

/*
====================
GL_EnableTextureUnit
====================
*/
void GL_EnableTextureUnit( int tmu, qboolean enable )
{
	// only enable fixed-function pipeline units
	if( tmu < glConfig.max_texture_units )
	{
		if( enable )
		{
			pglEnable( glState.currentTextureTargets[tmu] );
		}
		else if( glState.currentTextureTargets[tmu] != GL_NONE )
		{
			pglDisable( glState.currentTextureTargets[tmu] );
		}
	}
}

/*
=================
GL_TextureTarget
=================
*/
void GL_TextureTarget( uint target )
{
	if( glState.activeTMU < 0 || glState.activeTMU >= GL_MaxTextureUnits( ))
	{
		gEngfuncs.Con_Reportf( S_ERROR "%s: bad tmu state %i\n", __func__, glState.activeTMU );
		return;
	}

	if( glState.currentTextureTargets[glState.activeTMU] != target )
	{
		GL_EnableTextureUnit( glState.activeTMU, false );
		glState.currentTextureTargets[glState.activeTMU] = target;
		if( target != GL_NONE )
			GL_EnableTextureUnit( glState.activeTMU, true );
	}
}

/*
=================
GL_TexGen
=================
*/
void GL_TexGen( GLenum coord, GLenum mode )
{
	int	tmu = Q_min( glConfig.max_texture_coords, glState.activeTMU );
	int	bit, gen;

	switch( coord )
	{
	case GL_S:
		bit = 1;
		gen = GL_TEXTURE_GEN_S;
		break;
	case GL_T:
		bit = 2;
		gen = GL_TEXTURE_GEN_T;
		break;
	case GL_R:
		bit = 4;
		gen = GL_TEXTURE_GEN_R;
		break;
	case GL_Q:
		bit = 8;
		gen = GL_TEXTURE_GEN_Q;
		break;
	default: return;
	}

	if( mode )
	{
		if( !FBitSet( glState.genSTEnabled[tmu], bit ))
		{
			pglEnable( gen );
			SetBits( glState.genSTEnabled[tmu], bit );
		}
		pglTexGeni( coord, GL_TEXTURE_GEN_MODE, mode );
	}
	else
	{
		if( FBitSet( glState.genSTEnabled[tmu], bit ))
		{
			pglDisable( gen );
			ClearBits( glState.genSTEnabled[tmu], bit );
		}
	}
}

/*
=================
GL_SetTexCoordArrayMode
=================
*/
void GL_SetTexCoordArrayMode( GLenum mode )
{
	int	tmu = Q_min( glConfig.max_texture_coords, glState.activeTMU );
	int	bit, cmode = glState.texCoordArrayMode[tmu];

	if( mode == GL_TEXTURE_COORD_ARRAY )
		bit = 1;
	else if( mode == GL_TEXTURE_CUBE_MAP_ARB )
		bit = 2;
	else
		bit = 0;

	if( cmode != bit )
	{
		if( cmode == 1 )
			pglDisableClientState( GL_TEXTURE_COORD_ARRAY );
		else if( cmode == 2 )
			pglDisable( GL_TEXTURE_CUBE_MAP_ARB );

		if( bit == 1 )
			pglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		else if( bit == 2 )
			pglEnable( GL_TEXTURE_CUBE_MAP_ARB );

		glState.texCoordArrayMode[tmu] = bit;
	}
}

/*
=================
GL_Cull
=================
*/
void GL_Cull( GLenum cull )
{
	if( !cull )
	{
		pglDisable( GL_CULL_FACE );
		glState.faceCull = 0;
		return;
	}

	pglEnable( GL_CULL_FACE );
	pglCullFace( cull );
	glState.faceCull = cull;
}

void GL_SetRenderMode( int mode )
{
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	switch( mode )
	{
	case kRenderNormal:
	default:
		R_AllowFog( true );
		pglDisable( GL_BLEND );
		pglDisable( GL_ALPHA_TEST );
		break;
	case kRenderTransColor:
	case kRenderTransTexture:
		R_AllowFog( true );
		pglEnable( GL_BLEND );
		pglDisable( GL_ALPHA_TEST );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		break;
	case kRenderTransAlpha:
		R_AllowFog( true );
		pglDisable( GL_BLEND );
		pglEnable( GL_ALPHA_TEST );
		break;
	case kRenderGlow:
	case kRenderTransAdd:
		R_AllowFog( false );
		pglEnable( GL_BLEND );
		pglDisable( GL_ALPHA_TEST );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
		break;
	case kRenderScreenFadeModulate:
		R_AllowFog( true );
		pglEnable( GL_BLEND );
		pglDisable( GL_ALPHA_TEST );
		pglBlendFunc( GL_ZERO, GL_SRC_COLOR );
	}
}

/*
==============================================================================

SCREEN SHOTS

==============================================================================
*/
// used for 'env' and 'sky' shots
typedef struct envmap_s
{
	vec3_t	angles;
	int	flags;
} envmap_t;

static const envmap_t r_skyBoxInfo[6] =
{
{{   0, 270, 180}, IMAGE_FLIP_X },
{{   0,  90, 180}, IMAGE_FLIP_X },
{{ -90,   0, 180}, IMAGE_FLIP_X },
{{  90,   0, 180}, IMAGE_FLIP_X },
{{   0,   0, 180}, IMAGE_FLIP_X },
{{   0, 180, 180}, IMAGE_FLIP_X },
};

static const envmap_t r_envMapInfo[6] =
{
{{  0,   0,  90}, 0 },
{{  0, 180, -90}, 0 },
{{  0,  90,   0}, 0 },
{{  0, 270, 180}, 0 },
{{-90, 180, -90}, 0 },
{{ 90,   0,  90}, 0 }
};

qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	rgbdata_t *r_shot;
	uint	flags = IMAGE_FLIP_Y;
	int	width = 0, height = 0;
	qboolean	result;

	r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
	r_shot->width = (gpGlobals->width + 3) & ~3;
	r_shot->height = (gpGlobals->height + 3) & ~3;
	r_shot->flags = IMAGE_HAS_COLOR;
	r_shot->type = PF_RGBA_32;
	r_shot->size = r_shot->width * r_shot->height * gEngfuncs.Image_GetPFDesc( r_shot->type )->bpp;
	r_shot->palette = NULL;
	r_shot->buffer = Mem_Malloc( r_temppool, r_shot->size );

	// get screen frame
	pglReadPixels( 0, 0, r_shot->width, r_shot->height, GL_RGBA, GL_UNSIGNED_BYTE, r_shot->buffer );

	switch( shot_type )
	{
	case VID_SCREENSHOT:
		break;
	case VID_SNAPSHOT:
		gEngfuncs.fsapi->AllowDirectPaths( true );
		break;
	case VID_LEVELSHOT:
	case VID_MINISHOT:
		flags |= IMAGE_RESAMPLE;
		height = shot_type == VID_MINISHOT ? 200 : 480;
		width = Q_rint( height * ((double)r_shot->width / r_shot->height ));
		break;
	case VID_MAPSHOT:
		flags |= IMAGE_RESAMPLE|IMAGE_QUANTIZE;	// GoldSrc request overviews in 8-bit format
		height = 768;
		width = 1024;
		break;
	}

	gEngfuncs.Image_Process( &r_shot, width, height, flags, 0.0f );

	// write image
	result = gEngfuncs.FS_SaveImage( filename, r_shot );
	gEngfuncs.fsapi->AllowDirectPaths( false );			// always reset after store screenshot
	gEngfuncs.FS_FreeImage( r_shot );

	return result;
}

/*
=================
VID_CubemapShot
=================
*/
qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	rgbdata_t		*r_shot, *r_side;
	byte		*temp = NULL;
	byte		*buffer = NULL;
	string		basename;
	int		i = 1, flags, result;

	if( !RI.drawWorld || !WORLDMODEL )
		return false;

	// make sure the specified size is valid
	while( i < size ) i<<=1;

	if( i != size ) return false;
	if( size > gpGlobals->width || size > gpGlobals->height )
		return false;

	// alloc space
	temp = Mem_Malloc( r_temppool, size * size * 3 );
	buffer = Mem_Malloc( r_temppool, size * size * 3 * 6 );
	r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
	r_side = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));

	// use client vieworg
	if( !vieworg ) vieworg = RI.vieworg;

	for( i = 0; i < 6; i++ )
	{
		// go into 3d mode
		R_Set2DMode( false );

		if( skyshot )
		{
			R_DrawCubemapView( vieworg, r_skyBoxInfo[i].angles, size );
			flags = r_skyBoxInfo[i].flags;
		}
		else
		{
			R_DrawCubemapView( vieworg, r_envMapInfo[i].angles, size );
			flags = r_envMapInfo[i].flags;
		}

		pglReadPixels( 0, 0, size, size, GL_RGB, GL_UNSIGNED_BYTE, temp );
		r_side->flags = IMAGE_HAS_COLOR;
		r_side->width = r_side->height = size;
		r_side->type = PF_RGB_24;
		r_side->size = r_side->width * r_side->height * 3;
		r_side->buffer = temp;

		if( flags ) gEngfuncs.Image_Process( &r_side, 0, 0, flags, 0.0f );
		memcpy( buffer + (size * size * 3 * i), r_side->buffer, size * size * 3 );
	}

	r_shot->flags = IMAGE_HAS_COLOR;
	r_shot->flags |= (skyshot) ? IMAGE_SKYBOX : IMAGE_CUBEMAP;
	r_shot->width = size;
	r_shot->height = size;
	r_shot->type = PF_RGB_24;
	r_shot->size = r_shot->width * r_shot->height * 3 * 6;
	r_shot->palette = NULL;
	r_shot->buffer = buffer;

	// make sure what we have right extension
	Q_strncpy( basename, base, sizeof( basename ));
	COM_ReplaceExtension( basename, ".tga", sizeof( basename ));

	// write image as 6 sides
	result = gEngfuncs.FS_SaveImage( basename, r_shot );
	gEngfuncs.FS_FreeImage( r_shot );
	gEngfuncs.FS_FreeImage( r_side );

	return result;
}

//=======================================================

/*
===============
R_ShowTextures

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.
===============
*/
void R_ShowTextures( void )
{
	float		w, h;
	int		start, k;
	int base_w, base_h;
	rgba_t		color = { 255, 255, 255, 255 };
	int		charHeight;
	static qboolean	showHelp = true;
	float	time;	//nc add
	float	time_cubemap;	//nc add
	float	cbm_cos, cbm_sin;	//nc add
	int		per_page; //nc add
	qboolean empty_page;
	int skipped_empty_pages;

	if( !r_showtextures->value )
		return;

	if( showHelp )
	{
		gEngfuncs.CL_CenterPrint( "use '<-' and '->' keys to change atlas page, ESC to quit", 0.25f );
		showHelp = false;
	}

	pglClear( GL_COLOR_BUFFER_BIT );

	w = 200;
	h = 200;

	time = gp_cl->time * 0.5f;
	time -= floor( time );
	time_cubemap = gp_cl->time * 0.25f;
	time_cubemap -= floor( time_cubemap );
	time_cubemap *= M_PI2_F;
	SinCos( time_cubemap, &cbm_sin, &cbm_cos );

	gEngfuncs.Con_DrawStringLen( NULL, NULL, &charHeight );

	base_w = gpGlobals->width / w;
	base_h = gpGlobals->height / ( h + charHeight * 2 );
	per_page = base_w * base_h;
	start = per_page * ( r_showtextures->value - 1 ) + 1; // skip empty null texture

	GL_SetRenderMode( kRenderTransTexture );	// nc changed from normal to trans, Con_DrawString does this anyway

	empty_page = true;
	skipped_empty_pages = 0;
	while( empty_page )
	{
		for( k = 0; k < per_page; k++ )
		{
			const gl_texture_t *image;
			int i;

			i = k + start;
			if( i >= MAX_TEXTURES )
			{
				empty_page = false;
				break;
			}

			image = R_GetTexture( i );
			if( pglIsTexture( image->texnum ))
			{
				empty_page = false;
				break;
			}
		}

		if( empty_page )
		{
			start += per_page;
			skipped_empty_pages++;
		}
	}

	if( skipped_empty_pages > 0 )
	{
		char text[MAX_VA_STRING];
		Q_snprintf( text, sizeof( text ), "%s: skipped %d empty texture pages", __func__, skipped_empty_pages );
		gEngfuncs.CL_CenterPrint( text, 0.25f );
	}

	for( k = 0; k < per_page; k++ )
	{
		const gl_texture_t *image;
		int textlen, i;
		char text[MAX_VA_STRING];
		string shortname;
		float x, y;

		i = k + start;
		if ( i >= MAX_TEXTURES )
			break;

		image = R_GetTexture( i );
		if( !pglIsTexture( image->texnum ))
			continue;

		x = k % base_w * gpGlobals->width / base_w;
		y = k / base_w * gpGlobals->height / base_h;

		pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		GL_Bind( XASH_TEXTURE0, i );

		if( FBitSet( image->flags, TF_DEPTHMAP ) && !FBitSet( image->flags, TF_NOCOMPARE ))
			pglTexParameteri( image->target, GL_TEXTURE_COMPARE_MODE_ARB, GL_NONE );

		pglBegin( GL_QUADS );

#if XASH_NANOGL
#undef pglTexCoord3f
#define pglTexCoord3f( s, t, u ) pglTexCoord2f( s, t ) // not really correct but it requires nanogl rework
#endif // XASH_GLES

		if( image->target == GL_TEXTURE_CUBE_MAP_ARB )
		{
			pglTexCoord3f( 0.75 * cbm_cos - cbm_sin, 0.75 * cbm_sin + cbm_cos, 1.0 );
			pglVertex2f( x, y );
			pglTexCoord3f( 0.75 * cbm_cos + cbm_sin, 0.75 * cbm_sin - cbm_cos, 1.0 );
			pglVertex2f( x + w, y );
			pglTexCoord3f( 0.75 * cbm_cos + cbm_sin, 0.75 * cbm_sin - cbm_cos, -1.0 );
			pglVertex2f( x + w, y + h );
			pglTexCoord3f( 0.75 * cbm_cos - cbm_sin, 0.75 * cbm_sin + cbm_cos, -1.0 );
			pglVertex2f( x, y + h );
		}
		else if( image->target == GL_TEXTURE_RECTANGLE_EXT )
		{
			pglTexCoord2f( 0, 0 );
			pglVertex2f( x, y );
			pglTexCoord2f( image->width, 0 );
			pglVertex2f( x + w, y );
			pglTexCoord2f( image->width, image->height );
			pglVertex2f( x + w, y + h );
			pglTexCoord2f( 0, image->height );
			pglVertex2f( x, y + h );
		}
		else
		{
			pglTexCoord3f( 0, 0, time );
			pglVertex2f( x, y );
			pglTexCoord3f( 1, 0, time );
			pglVertex2f( x + w, y );
			pglTexCoord3f( 1, 1, time );
			pglVertex2f( x + w, y + h );
			pglTexCoord3f( 0, 1, time);
			pglVertex2f( x, y + h );
		}
		pglEnd();

		if( FBitSet( image->flags, TF_DEPTHMAP ) && !FBitSet( image->flags, TF_NOCOMPARE ))
			pglTexParameteri( image->target, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB );

		COM_FileBase( image->name, shortname, sizeof( shortname ));
		gEngfuncs.Con_DrawStringLen( shortname, &textlen, NULL );

		if( textlen > w )
		{
			// cutoff too long names, it looks ugly
			shortname[16] = '.';
			shortname[17] = '.';
			shortname[18] = '\0';
		}

		gEngfuncs.Con_DrawString( x + 1, y + h, shortname, color );
		if( image->target == GL_TEXTURE_3D || image->target == GL_TEXTURE_2D_ARRAY_EXT )
			Q_snprintf( text, sizeof( text ), "%ix%ix%i %s", image->width, image->height, image->depth, GL_TargetToString( image->target ));
		else
			Q_snprintf( text, sizeof( text ), "%ix%i %s", image->width, image->height, GL_TargetToString( image->target ));
		gEngfuncs.Con_DrawString( x + 1, y + h + charHeight, text, color );

		Q_strncpy( text, Q_memprint( image->size ), sizeof( text ));
		gEngfuncs.Con_DrawStringLen( text, &textlen, NULL );
		gEngfuncs.Con_DrawString(( x + w ) - textlen - 1, y + h + charHeight, text, color );
	}

	gEngfuncs.CL_DrawCenterPrint ();
	pglFinish();
}

/*
================
SCR_TimeRefresh_f

timerefresh [noflip]
================
*/
void SCR_TimeRefresh_f( void )
{
	int	i;
	double	start, stop;
	double	time;

	if( ENGINE_GET_PARM( PARM_CONNSTATE ) != ca_active )
		return;

	start = gEngfuncs.pfnTime();

	// run without page flipping like GoldSrc
	if( gEngfuncs.Cmd_Argc() == 1 )
	{
		pglDrawBuffer( GL_FRONT );
		for( i = 0; i < 128; i++ )
		{
			gpGlobals->viewangles[1] = i / 128.0f * 360.0f;
			R_RenderScene();
		}
		pglFinish();
		R_EndFrame();
	}
	else
	{
		for( i = 0; i < 128; i++ )
		{
			R_BeginFrame( true );
			gpGlobals->viewangles[1] = i / 128.0f * 360.0f;
			R_RenderScene();
			R_EndFrame();
		}
	}

	stop = gEngfuncs.pfnTime ();
	time = (stop - start);
	gEngfuncs.Con_Printf( "%f seconds (%f fps)\n", time, 128 / time );
}
