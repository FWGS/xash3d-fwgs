/*
gl_backend.c - rendering backend
Copyright (C) 2010 Uncle Mike
Copyright (C) 2021 Sergey Galushko

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
#include "xash3d_mathlib.h"

char		r_speeds_msg[MAX_SYSPATH];
ref_speeds_t	r_stats;	// r_speeds counters

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
R_Speeds_Printf

helper to print into r_speeds message
==============
*/
void R_Speeds_Printf( const char *msg, ... )
{
	va_list	argptr;
	char	text[2048];

	va_start( argptr, msg );
	Q_vsprintf( text, msg, argptr );
	va_end( argptr );

	Q_strncat( r_speeds_msg, text, sizeof( r_speeds_msg ));
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

	R_Speeds_Printf( "Renderer: ^1Engine^7\n\n" );

	switch( (int)r_speeds->value )
	{
	case 1:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i wpoly, %3i apoly\n%3i epoly, %3i spoly",
		r_stats.c_world_polys, r_stats.c_alias_polys, r_stats.c_studio_polys, r_stats.c_sprite_polys );
		break;
	case 2:
		R_Speeds_Printf( "visible leafs:\n%3i leafs\ncurrent leaf %3i\n", r_stats.c_world_leafs, curleaf - WORLDMODEL->leafs );
		R_Speeds_Printf( "ReciusiveWorldNode: %3lf secs\nDrawTextureChains %lf\n", r_stats.t_world_node, r_stats.t_world_draw );
		break;
	case 3:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i alias models drawn\n%3i studio models drawn\n%3i sprites drawn",
		r_stats.c_alias_models_drawn, r_stats.c_studio_models_drawn, r_stats.c_sprite_models_drawn );
		break;
	case 4:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i static entities\n%3i normal entities\n%3i server entities",
		r_numStatics, r_numEntities - r_numStatics, ENGINE_GET_PARM( PARM_NUMENTITIES ));
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
GL_LoadTexMatrix
=================
*/
void GL_LoadTexMatrix( const matrix4x4 m )
{
	sceGumMatrixMode( GU_TEXTURE );
	GL_LoadMatrix( m );
	sceGumUpdateMatrix();

	glState.texIdentityMatrix = false;
}

/*
=================
GL_LoadTexMatrixExt
=================
*/
void GL_LoadTexMatrixExt( const float *glmatrix )
{
	Assert( glmatrix != NULL );

	sceGumMatrixMode( GU_TEXTURE );
	sceGumLoadMatrix( ( const ScePspFMatrix4 * ) glmatrix );
	sceGumUpdateMatrix();

	glState.texIdentityMatrix = false;
}

/*
=================
GL_LoadMatrix
=================
*/
void GL_LoadMatrix( const matrix4x4 source )
{
	ScePspFMatrix4	dest;

	Matrix4x4_ToFMatrix4( source, &dest );
	sceGumLoadMatrix( &dest );
}

/*
=================
GL_LoadIdentityTexMatrix
=================
*/
void GL_LoadIdentityTexMatrix( void )
{
	if( glState.texIdentityMatrix )
		return;

	sceGumMatrixMode( GU_TEXTURE );
	sceGumLoadIdentity();
	sceGumUpdateMatrix();

	glState.texIdentityMatrix = true;
}

/*
=================
GL_SelectTexture
=================
*/
void GL_SelectTexture( GLint tmu )
{

}

/*
==============
GL_DisableAllTexGens
==============
*/
void GL_DisableAllTexGens( void )
{

}

/*
==============
GL_CleanUpTextureUnits
==============
*/
void GL_CleanUpTextureUnits( int last )
{

}

/*
==============
GL_CleanupAllTextureUnits
==============
*/
void GL_CleanupAllTextureUnits( void )
{

}

/*
=================
GL_MultiTexCoord2f
=================
*/
void GL_MultiTexCoord2f( GLenum texture, GLfloat s, GLfloat t )
{

}

/*
=================
GL_TextureTarget
=================
*/
void GL_TextureTarget( uint target )
{

}

/*
=================
GL_TexGen
=================
*/
void GL_TexGen( GLenum coord, GLenum mode )
{

}

/*
=================
GL_SetTexCoordArrayMode
=================
*/
void GL_SetTexCoordArrayMode( GLenum mode )
{

}

/*
=================
GL_Cull
=================
*/
void GL_Cull( GLenum cull )
{
	if( glState.faceCull == cull )
		return;

	if( !cull )
	{
		sceGuDisable( GU_CULL_FACE );
		glState.faceCull = 0;
		return;
	}

	sceGuEnable( GU_CULL_FACE );
	sceGuFrontFace( cull - 1 );
	glState.faceCull = cull;
}

void GL_SetRenderMode( int mode )
{
	sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );

	switch( mode )
	{
	case kRenderNormal:
	default:
		sceGuDisable( GU_BLEND );
		sceGuDisable( GU_ALPHA_TEST );
		break;
	case kRenderTransColor:
	case kRenderTransTexture:
		sceGuEnable( GU_BLEND );
		sceGuDisable( GU_ALPHA_TEST );
		sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
		break;
	case kRenderTransAlpha:
		sceGuDisable( GU_BLEND );
		sceGuEnable( GU_ALPHA_TEST );
		break;
	case kRenderGlow:
	case kRenderTransAdd:
		sceGuEnable( GU_BLEND );
		sceGuDisable( GU_ALPHA_TEST );
		sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, GUBLEND1 );
		break;
	}
}

/*
=================
GL_SetColor4ub
=================
*/
void GL_SetColor4ub( byte r, byte g, byte b, byte a )
{
	sceGuColor( GU_RGBA( r, g, b, a ));
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

const envmap_t r_skyBoxInfo[6] =
{
{{   0, 270, 180}, IMAGE_FLIP_X },
{{   0,  90, 180}, IMAGE_FLIP_X },
{{ -90,   0, 180}, IMAGE_FLIP_X },
{{  90,   0, 180}, IMAGE_FLIP_X },
{{   0,   0, 180}, IMAGE_FLIP_X },
{{   0, 180, 180}, IMAGE_FLIP_X },
};

const envmap_t r_envMapInfo[6] =
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
	uint	flags = 0;
	int	width = 0, height = 0;
	qboolean	result;
	int bpp;

	r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
	r_shot->width = (gpGlobals->width + 3) & ~3;
	r_shot->height = (gpGlobals->height + 3) & ~3;
	r_shot->flags = IMAGE_HAS_COLOR;
	r_shot->type = PF_RGB_24;
	bpp = gEngfuncs.Image_GetPFDesc( r_shot->type )->bpp;
	r_shot->size = r_shot->width * r_shot->height * bpp;
	r_shot->palette = NULL;
	r_shot->buffer = Mem_Malloc( r_temppool, r_shot->size );

	// get screen frame
	byte *src = ( byte* )guRender.disp_buffer;
	byte *dst = r_shot->buffer;
	int cheight = r_shot->height;

	// stride copy
	while( cheight-- )
	{
		GL_PixelConverter( dst, src, r_shot->width, PC_HWF( guRender.buffer_format ), PC_SWF( r_shot->type ) );
		dst += r_shot->width * bpp;
		src += guRender.buffer_width * guRender.buffer_bpp;
	}

	switch( shot_type )
	{
	case VID_SCREENSHOT:
		break;
	case VID_SNAPSHOT:
		gEngfuncs.FS_AllowDirectPaths( true );
		break;
	case VID_LEVELSHOT:
		flags |= IMAGE_RESAMPLE;
		if( gpGlobals->wideScreen )
		{
			height = 480;
			width = 800;
		}
		else
		{
			height = 480;
			width = 640;
		}
		break;
	case VID_MINISHOT:
		flags |= IMAGE_RESAMPLE;
		height = 200;
		width = 320;
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
	gEngfuncs.FS_AllowDirectPaths( false );			// always reset after store screenshot
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
#if 0
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

	R_CheckGamma();

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
	Q_strncpy( basename, base, MAX_STRING );
	COM_StripExtension( basename );
	COM_DefaultExtension( basename, ".tga" );

	// write image as 6 sides
	result = gEngfuncs.FS_SaveImage( basename, r_shot );
	gEngfuncs.FS_FreeImage( r_shot );
	gEngfuncs.FS_FreeImage( r_side );

	return result;
#else
	return 0;
#endif
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
#if 0
	gl_texture_t	*image;
	float		x, y, w, h;
	int		total, start, end;
	int		i, j, k, base_w, base_h;
	rgba_t		color = { 192, 192, 192, 255 };
	int		charHeight, numTries = 0;
	static qboolean	showHelp = true;
	string		shortname;

	if( !CVAR_TO_BOOL( gl_showtextures ))
		return;

	if( showHelp )
	{
		gEngfuncs.CL_CenterPrint( "use '<-' and '->' keys to change atlas page, ESC to quit", 0.25f );
		showHelp = false;
	}

	GL_SetRenderMode( kRenderNormal );
	pglClear( GL_COLOR_BUFFER_BIT );
	pglFinish();

	base_w = 8;	// textures view by horizontal
	base_h = 6;	// textures view by vertical

rebuild_page:
	total = base_w * base_h;
	start = total * (gl_showtextures->value - 1);
	end = total * gl_showtextures->value;
	if( end > MAX_TEXTURES ) end = MAX_TEXTURES;

	w = gpGlobals->width / base_w;
	h = gpGlobals->height / base_h;

	gEngfuncs.Con_DrawStringLen( NULL, NULL, &charHeight );

	for( i = j = 0; i < MAX_TEXTURES; i++ )
	{
		image = R_GetTexture( i );
		if( j == start ) break; // found start
		if( pglIsTexture( image->texnum )) j++;
	}

	if( i == MAX_TEXTURES && gl_showtextures->value != 1 )
	{
		// bad case, rewind to one and try again
		gEngfuncs.Cvar_SetValue( "r_showtextures", max( 1, gl_showtextures->value - 1 ));
		if( ++numTries < 2 ) goto rebuild_page;	// to prevent infinite loop
	}

	for( k = 0; i < MAX_TEXTURES; i++ )
	{
		if( j == end ) break; // page is full

		image = R_GetTexture( i );
		if( !pglIsTexture( image->texnum ))
			continue;

		x = k % base_w * w;
		y = k / base_w * h;

		pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		GL_Bind( XASH_TEXTURE0, i ); // NOTE: don't use image->texnum here, because skybox has a 'wrong' indexes

		if( FBitSet( image->flags, TF_DEPTHMAP ) && !FBitSet( image->flags, TF_NOCOMPARE ))
			pglTexParameteri( image->target, GL_TEXTURE_COMPARE_MODE_ARB, GL_NONE );

		pglBegin( GL_QUADS );
		pglTexCoord2f( 0, 0 );
		pglVertex2f( x, y );
		if( image->target == GL_TEXTURE_RECTANGLE_EXT )
			pglTexCoord2f( image->width, 0 );
		else pglTexCoord2f( 1, 0 );
		pglVertex2f( x + w, y );
		if( image->target == GL_TEXTURE_RECTANGLE_EXT )
			pglTexCoord2f( image->width, image->height );
		else pglTexCoord2f( 1, 1 );
		pglVertex2f( x + w, y + h );
		if( image->target == GL_TEXTURE_RECTANGLE_EXT )
			pglTexCoord2f( 0, image->height );
		else pglTexCoord2f( 0, 1 );
		pglVertex2f( x, y + h );
		pglEnd();

		if( FBitSet( image->flags, TF_DEPTHMAP ) && !FBitSet( image->flags, TF_NOCOMPARE ))
			pglTexParameteri( image->target, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB );

		COM_FileBase( image->name, shortname );
		if( Q_strlen( shortname ) > 18 )
		{
			// cutoff too long names, it looks ugly
			shortname[16] = '.';
			shortname[17] = '.';
			shortname[18] = '\0';
		}
		gEngfuncs.Con_DrawString( x + 1, y + h - charHeight, shortname, color );
		j++, k++;
	}

	gEngfuncs.CL_DrawCenterPrint ();
	pglFinish();
#endif
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
#if 0
		pglDrawBuffer( GL_FRONT );
#endif
		for( i = 0; i < 128; i++ )
		{
			gpGlobals->viewangles[1] = i / 128.0f * 360.0f;
			R_RenderScene();
		}
#if 0
		pglFinish();
#endif
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
