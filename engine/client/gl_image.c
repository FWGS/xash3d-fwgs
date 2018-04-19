/*
gl_image.c - texture uploading and processing
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
#include "studio.h"

#define TEXTURES_HASH_SIZE	(MAX_TEXTURES >> 2)

static gltexture_t		r_textures[MAX_TEXTURES];
static gltexture_t		*r_texturesHashTable[TEXTURES_HASH_SIZE];
static byte		data2D[1024];				// intermediate texbuffer
static int		r_numTextures;
static rgbdata_t		r_image;					// generic pixelbuffer used for internal textures

// internal tables
static vec3_t		r_luminanceTable[256];			// RGB to luminance

#define IsLightMap( tex )	( FBitSet(( tex )->flags, TF_ATLAS_PAGE ))
/*
=================
R_GetTexture

acess to array elem
=================
*/
gltexture_t *R_GetTexture( GLenum texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return &r_textures[texnum];
}

/*
=================
GL_TargetToString
=================
*/
static const char *GL_TargetToString( GLenum target )
{
	switch( target )
	{
	case GL_TEXTURE_1D:
		return "1D";
	case GL_TEXTURE_2D:
		return "2D";
	case GL_TEXTURE_3D:
		return "3D";
	case GL_TEXTURE_CUBE_MAP_ARB:
		return "Cube";
	case GL_TEXTURE_2D_ARRAY_EXT:
		return "Array";
	case GL_TEXTURE_RECTANGLE_EXT:
		return "Rect";
	}
	return "??";
}

/*
=================
GL_Bind
=================
*/
void GL_Bind( GLint tmu, GLenum texnum )
{
	gltexture_t	*texture;
	GLuint		glTarget;

	// missed texture ?
	if( texnum <= 0 ) texnum = tr.defaultTexture;
	Assert( texnum > 0 && texnum < MAX_TEXTURES );

	if( tmu != GL_KEEP_UNIT )
		GL_SelectTexture( tmu );
	else tmu = glState.activeTMU;

	texture = &r_textures[texnum];
	glTarget = texture->target;

	if( glTarget == GL_TEXTURE_2D_ARRAY_EXT )
		glTarget = GL_TEXTURE_2D;

	if( glState.currentTextureTargets[tmu] != glTarget )
	{
		if( glState.currentTextureTargets[tmu] != GL_NONE )
			pglDisable( glState.currentTextureTargets[tmu] );
		glState.currentTextureTargets[tmu] = glTarget;
		pglEnable( glState.currentTextureTargets[tmu] );
	}

	if( glState.currentTextures[tmu] == texture->texnum )
		return;

	pglBindTexture( texture->target, texture->texnum );
	glState.currentTextures[tmu] = texture->texnum;
}

/*
=================
GL_ApplyTextureParams
=================
*/
void GL_ApplyTextureParams( gltexture_t *tex )
{
	vec4_t	border = { 0.0f, 0.0f, 0.0f, 1.0f };

	Assert( tex != NULL );

	// set texture filter
	if( FBitSet( tex->flags, TF_DEPTHMAP ))
	{
		if( !FBitSet( tex->flags, TF_NOCOMPARE ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE_ARB );
			pglTexParameteri( tex->target, GL_TEXTURE_COMPARE_FUNC_ARB, GL_LEQUAL );
		}

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			pglTexParameteri( tex->target, GL_DEPTH_TEXTURE_MODE_ARB, GL_LUMINANCE );
		else pglTexParameteri( tex->target, GL_DEPTH_TEXTURE_MODE_ARB, GL_INTENSITY );

		if( FBitSet( tex->flags, TF_NEAREST ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}

		// allow max anisotropy as 1.0f on depth textures
		if( GL_Support( GL_ANISOTROPY_EXT ))
			pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f );
	}
	else if( FBitSet( tex->flags, TF_NOMIPMAP ) || tex->numMips <= 1 )
	{
		if( FBitSet( tex->flags, TF_NEAREST ) || ( IsLightMap( tex ) && gl_lightmap_nearest->value ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}
	}
	else
	{
		if( FBitSet( tex->flags, TF_NEAREST ) || gl_texture_nearest->value )
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}

		// set texture anisotropy if available
		if( GL_Support( GL_ANISOTROPY_EXT ) && ( tex->numMips > 1 ))
			pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy->value );

		// set texture LOD bias if available
		if( GL_Support( GL_TEXTURE_LOD_BIAS ) && ( tex->numMips > 1 ))
			pglTexParameterf( tex->target, GL_TEXTURE_LOD_BIAS_EXT, gl_texture_lodbias->value );
	}

	// check if border is not supported
	if( FBitSet( tex->flags, TF_BORDER ) && !GL_Support( GL_CLAMP_TEXBORDER_EXT ))
	{
		ClearBits( tex->flags, TF_BORDER );
		SetBits( tex->flags, TF_CLAMP );
	}

	// only seamless cubemaps allows wrap 'clamp_to_border"
	if( tex->target == GL_TEXTURE_CUBE_MAP_ARB && !GL_Support( GL_ARB_SEAMLESS_CUBEMAP ) && FBitSet( tex->flags, TF_BORDER ))
		ClearBits( tex->flags, TF_BORDER );

	// set texture wrap
	if( FBitSet( tex->flags, TF_BORDER ))
	{
		pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );

		if( tex->target != GL_TEXTURE_1D )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );

		if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER );

		pglTexParameterfv( tex->target, GL_TEXTURE_BORDER_COLOR, border );
	}
	else if( FBitSet( tex->flags, TF_CLAMP ))
	{
		if( GL_Support( GL_CLAMPTOEDGE_EXT ))
		{
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );

			if( tex->target != GL_TEXTURE_1D )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

			if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_CLAMP );

			if( tex->target != GL_TEXTURE_1D )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_CLAMP );

			if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
				pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_CLAMP );
		}
	}
	else
	{
		pglTexParameteri( tex->target, GL_TEXTURE_WRAP_S, GL_REPEAT );

		if( tex->target != GL_TEXTURE_1D )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_T, GL_REPEAT );

		if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_CUBE_MAP_ARB )
			pglTexParameteri( tex->target, GL_TEXTURE_WRAP_R, GL_REPEAT );
	}
}

/*
=================
GL_UpdateTextureParams
=================
*/
static void GL_UpdateTextureParams( int iTexture )
{
	gltexture_t	*tex = &r_textures[iTexture];

	Assert( tex != NULL );

	if( !tex->texnum ) return; // free slot

	GL_Bind( XASH_TEXTURE0, iTexture );

	// set texture anisotropy if available
	if( GL_Support( GL_ANISOTROPY_EXT ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_DEPTHMAP ))
		pglTexParameterf( tex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy->value );

	// set texture LOD bias if available
	if( GL_Support( GL_TEXTURE_LOD_BIAS ) && ( tex->numMips > 1 ) && !FBitSet( tex->flags, TF_DEPTHMAP ))
		pglTexParameterf( tex->target, GL_TEXTURE_LOD_BIAS_EXT, gl_texture_lodbias->value );

	if( IsLightMap( tex ))
	{
		if( gl_lightmap_nearest->value )
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		}
		else
		{
			pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		}
	}

	if( tex->numMips <= 1 ) return;

	if( FBitSet( tex->flags, TF_NEAREST ) || gl_texture_nearest->value )
	{
		pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
		pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	}
	else
	{
		pglTexParameteri( tex->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		pglTexParameteri( tex->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}
}

/*
=================
R_SetTextureParameters
=================
*/
void R_SetTextureParameters( void )
{
	int	i;

	if( GL_Support( GL_ANISOTROPY_EXT ))
	{
		if( gl_texture_anisotropy->value > glConfig.max_texture_anisotropy )
			Cvar_SetValue( "gl_anisotropy", glConfig.max_texture_anisotropy );
		else if( gl_texture_anisotropy->value < 1.0f )
			Cvar_SetValue( "gl_anisotropy", 1.0f );
	}

	if( GL_Support( GL_TEXTURE_LOD_BIAS ))
	{
		if( gl_texture_lodbias->value < -glConfig.max_texture_lod_bias )
			Cvar_SetValue( "gl_mipmap_bias", -glConfig.max_texture_lod_bias );
		else if( gl_texture_lodbias->value > glConfig.max_texture_lod_bias )
			Cvar_SetValue( "gl_mipmap_bias", glConfig.max_texture_lod_bias );
	}

	ClearBits( gl_texture_anisotropy->flags, FCVAR_CHANGED );
	ClearBits( gl_texture_lodbias->flags, FCVAR_CHANGED );
	ClearBits( gl_texture_nearest->flags, FCVAR_CHANGED );
	ClearBits( gl_lightmap_nearest->flags, FCVAR_CHANGED );

	// change all the existing mipmapped texture objects
	for( i = 0; i < r_numTextures; i++ )
		GL_UpdateTextureParams( i );
}

/*
================
GL_CalcTextureSamples
================
*/
static int GL_CalcTextureSamples( int flags )
{
	if( FBitSet( flags, IMAGE_HAS_COLOR ))
		return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 4 : 3;
	return FBitSet( flags, IMAGE_HAS_ALPHA ) ? 2 : 1;
}

/*
==================
GL_CalcImageSize
==================
*/
static size_t GL_CalcImageSize( pixformat_t format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case PF_RGB_24:
	case PF_BGR_24:
		size = width * height * depth * 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		size = width * height * depth * 4;
		break;
	case PF_DXT1:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	}

	return size;
}

/*
==================
GL_CalcTextureSize
==================
*/
static size_t GL_CalcTextureSize( GLenum format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	case GL_RGBA8:
	case GL_RGBA:
		size = width * height * depth * 4;
		break;
	case GL_RGB8:
	case GL_RGB:
		size = width * height * depth * 3;
		break;
	case GL_RGB5:
		size = (width * height * depth * 3) / 2;
		break;
	case GL_RGBA4:
		size = (width * height * depth * 4) / 2;
		break;
	case GL_INTENSITY:
	case GL_LUMINANCE:
	case GL_INTENSITY8:
	case GL_LUMINANCE8:
		size = (width * height * depth);
		break;
	case GL_LUMINANCE_ALPHA:
	case GL_LUMINANCE8_ALPHA8:
		size = width * height * depth * 2;
		break;
	case GL_R8:
		size = width * height * depth;
		break;
	case GL_RG8:
		size = width * height * depth * 2;
		break;
	case GL_R16:
		size = width * height * depth * 2;
		break;
	case GL_RG16:
		size = width * height * depth * 4;
		break;
	case GL_R16F:
	case GL_LUMINANCE16F_ARB:
		size = width * height * depth * 2;	// half-floats
		break;
	case GL_R32F:
	case GL_LUMINANCE32F_ARB:
		size = width * height * depth * 4;
		break;
	case GL_RG16F:
	case GL_LUMINANCE_ALPHA16F_ARB:
		size = width * height * depth * 4;
		break;
	case GL_RG32F:
	case GL_LUMINANCE_ALPHA32F_ARB:
		size = width * height * depth * 8;
		break;
	case GL_RGB16F_ARB:
	case GL_RGBA16F_ARB:
		size = width * height * depth * 8;
		break;
	case GL_RGB32F_ARB:
	case GL_RGBA32F_ARB:
		size = width * height * depth * 16;
		break;
	case GL_DEPTH_COMPONENT16:
		size = width * height * depth * 2;
		break;
	case GL_DEPTH_COMPONENT24:
		size = width * height * depth * 4;
		break;
	case GL_DEPTH_COMPONENT32F:
		size = width * height * depth * 4;
		break;
	default:
		Host_Error( "GL_CalcTextureSize: bad texture internal format (%u)\n", format );
		break;
	}

	return size;
}

static int GL_CalcMipmapCount( gltexture_t *tex, qboolean haveBuffer ) 
{
	int	width, height;
	int	mipcount;

	Assert( tex != NULL );

	if( !haveBuffer || tex->target == GL_TEXTURE_3D )
		return 1;

	// generate mip-levels by user request
	if( FBitSet( tex->flags, TF_NOMIPMAP ))
		return 1;
		
	// mip-maps can't exceeds 16
	for( mipcount = 0; mipcount < 16; mipcount++ )
	{
		width = Q_max( 1, ( tex->width >> mipcount ));
		height = Q_max( 1, ( tex->height >> mipcount ));
		if( width == 1 && height == 1 )
			break;
	}

	return mipcount + 1;
}

/*
================
GL_SetTextureDimensions
================
*/
static void GL_SetTextureDimensions( gltexture_t *tex, int width, int height, int depth )
{
	int	maxTextureSize;
	int	maxDepthSize = 1;

	Assert( tex != NULL );

	switch( tex->target )
	{
	case GL_TEXTURE_1D:
	case GL_TEXTURE_2D:
		maxTextureSize = glConfig.max_2d_texture_size;
		break;
	case GL_TEXTURE_2D_ARRAY_EXT:
		maxDepthSize = glConfig.max_2d_texture_layers;
		maxTextureSize = glConfig.max_2d_texture_size;
		break;
	case GL_TEXTURE_RECTANGLE_EXT:
		maxTextureSize = glConfig.max_2d_rectangle_size;
		break;
	case GL_TEXTURE_CUBE_MAP_ARB:
		maxTextureSize = glConfig.max_cubemap_size;
		break;
	case GL_TEXTURE_3D:
		maxDepthSize = glConfig.max_3d_texture_size;
		maxTextureSize = glConfig.max_3d_texture_size;
		break;
	}

	// store original sizes
	tex->srcWidth = width;
	tex->srcHeight = height;

	if( !GL_Support( GL_ARB_TEXTURE_NPOT_EXT ))
	{
		int	step = (int)gl_round_down->value;
		int	scaled_width, scaled_height;

		for( scaled_width = 1; scaled_width < width; scaled_width <<= 1 );

		if( step > 0 && width < scaled_width && ( step == 1 || ( scaled_width - width ) > ( scaled_width >> step )))
			scaled_width >>= 1;

		for( scaled_height = 1; scaled_height < height; scaled_height <<= 1 );

		if( step > 0 && height < scaled_height && ( step == 1 || ( scaled_height - height ) > ( scaled_height >> step )))
			scaled_height >>= 1;

		width = scaled_width;
		height = scaled_height;
	}

#if 1	// TESTTEST
	width = (width + 3) & ~3;
	height = (height + 3) & ~3;
#endif
	if( width > maxTextureSize || height > maxTextureSize || depth > maxDepthSize )
	{
		if( tex->target == GL_TEXTURE_1D )
		{
			while( width > maxTextureSize )
				width >>= 1;
		}
		else if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_2D_ARRAY_EXT )
		{
			while( width > maxTextureSize || height > maxTextureSize || depth > maxDepthSize )
			{
				width >>= 1;
				height >>= 1;
				depth >>= 1;
			}
		}
		else // all remaining cases
		{
			while( width > maxTextureSize || height > maxTextureSize )
			{
				width >>= 1;
				height >>= 1;
			}
		}
	}

	// set the texture dimensions
	tex->width = Q_max( 1, width );
	tex->height = Q_max( 1, height );
	tex->depth = Q_max( 1, depth );
}

/*
===============
GL_SetTextureTarget
===============
*/
static void GL_SetTextureTarget( gltexture_t *tex, rgbdata_t *pic )
{
	Assert( pic != NULL );
	Assert( tex != NULL );

	// correct depth size
	pic->depth = Q_max( 1, pic->depth );
	tex->numMips = 0; // begin counting

	// correct mip count
	pic->numMips = Q_max( 1, pic->numMips );

	// trying to determine texture type
	if( pic->width > 1 && pic->height <= 1 )
		tex->target = GL_TEXTURE_1D;
	else if( FBitSet( pic->flags, IMAGE_CUBEMAP ))
		tex->target = GL_TEXTURE_CUBE_MAP_ARB;
	else if( FBitSet( pic->flags, IMAGE_MULTILAYER ) && pic->depth >= 1 )
		tex->target = GL_TEXTURE_2D_ARRAY_EXT;
	else if( pic->width > 1 && pic->height > 1 && pic->depth > 1 )
		tex->target = GL_TEXTURE_3D;
	else if( FBitSet( tex->flags, TF_TEXTURE_RECTANGLE ) && pic->width == glState.width && pic->height == glState.height )
		tex->target = GL_TEXTURE_RECTANGLE_EXT;
	else tex->target = GL_TEXTURE_2D; // default case

	// check for hardware support
	if(( tex->target == GL_TEXTURE_CUBE_MAP_ARB ) && !GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
		tex->target = GL_NONE;

	if(( tex->target == GL_TEXTURE_RECTANGLE_EXT ) && !GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		tex->target = GL_TEXTURE_2D;	// fallback

	if(( tex->target == GL_TEXTURE_2D_ARRAY_EXT ) && !GL_Support( GL_TEXTURE_ARRAY_EXT ))
		tex->target = GL_NONE;

	if(( tex->target == GL_TEXTURE_3D ) && !GL_Support( GL_TEXTURE_3D_EXT ))
		tex->target = GL_NONE;

	// check if depth textures are not supported
	if( FBitSet( tex->flags, TF_DEPTHMAP ) && !GL_Support( GL_DEPTH_TEXTURE ))
		tex->target = GL_NONE;

	// depth cubemaps only allowed when GL_EXT_gpu_shader4 is supported
	if( tex->target == GL_TEXTURE_CUBE_MAP_ARB && !GL_Support( GL_EXT_GPU_SHADER4 ) && FBitSet( tex->flags, TF_DEPTHMAP ))
		tex->target = GL_NONE;

	if( tex->target == GL_TEXTURE_CUBE_MAP_ARB )
		tex->flags |= TF_CUBEMAP; // it's cubemap!
}

/*
===============
GL_SetTextureFormat
===============
*/
static void GL_SetTextureFormat( gltexture_t *tex, pixformat_t format, int channelMask )
{
	qboolean	haveColor = ( channelMask & IMAGE_HAS_COLOR );
	qboolean	haveAlpha = ( channelMask & IMAGE_HAS_ALPHA );

	Assert( tex != NULL );

	if( ImageDXT( format ))
	{
		switch( format )
		{
		case PF_DXT1: tex->format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT; break;	// never use DXT1 with 1-bit alpha
		case PF_DXT3: tex->format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; break;
		case PF_DXT5: tex->format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
		}
		return;
	}
	else if( FBitSet( tex->flags, TF_DEPTHMAP ))
	{
		if( FBitSet( tex->flags, TF_ARB_16BIT ))
			tex->format = GL_DEPTH_COMPONENT16;
		else if( FBitSet( tex->flags, TF_ARB_FLOAT ) && GL_Support( GL_ARB_DEPTH_FLOAT_EXT ))
			tex->format = GL_DEPTH_COMPONENT32F;
		else tex->format = GL_DEPTH_COMPONENT24;
	}
	else if( FBitSet( tex->flags, TF_ARB_FLOAT ) && GL_Support( GL_ARB_TEXTURE_FLOAT_EXT ))
	{
		if( haveColor && haveAlpha )
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || glw_state.desktopBitsPixel == 16 )
				tex->format = GL_RGBA16F_ARB;
			else tex->format = GL_RGBA32F_ARB;
		}
		else if( haveColor )
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || glw_state.desktopBitsPixel == 16 )
				tex->format = GL_RGB16F_ARB;
			else tex->format = GL_RGB32F_ARB;
		}
		else if( haveAlpha )
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || glw_state.desktopBitsPixel == 16 )
				tex->format = GL_LUMINANCE_ALPHA16F_ARB;
			else tex->format = GL_LUMINANCE_ALPHA32F_ARB;
		}
		else
		{
			if( FBitSet( tex->flags, TF_ARB_16BIT ) || glw_state.desktopBitsPixel == 16 )
				tex->format = GL_LUMINANCE16F_ARB;
			else tex->format = GL_LUMINANCE32F_ARB;
		}
	}
	else
	{
		// NOTE: not all the types will be compressed
		int	bits = glw_state.desktopBitsPixel;

		switch( GL_CalcTextureSamples( channelMask ))
		{
		case 1: tex->format = GL_LUMINANCE8; break;
		case 2: tex->format = GL_LUMINANCE8_ALPHA8; break;
		case 3:
			switch( bits )
			{
			case 16: tex->format = GL_RGB5; break;
			case 32: tex->format = GL_RGB8; break;
			default: tex->format = GL_RGB; break;
			}
			break;	
		case 4:
		default:
			switch( bits )
			{
			case 16: tex->format = GL_RGBA4; break;
			case 32: tex->format = GL_RGBA8; break;
			default: tex->format = GL_RGBA; break;
			}
			break;
		}
	}
}

/*
=================
GL_ResampleTexture

Assume input buffer is RGBA
=================
*/
byte *GL_ResampleTexture( const byte *source, int inWidth, int inHeight, int outWidth, int outHeight, qboolean isNormalMap )
{
	uint		frac, fracStep;
	uint		*in = (uint *)source;
	uint		p1[0x1000], p2[0x1000];
	byte		*pix1, *pix2, *pix3, *pix4;
	uint		*out, *inRow1, *inRow2;
	static byte	*scaledImage = NULL;	// pointer to a scaled image
	vec3_t		normal;
	int		i, x, y;

	if( !source ) return NULL;

	scaledImage = Mem_Realloc( r_temppool, scaledImage, outWidth * outHeight * 4 );
	fracStep = inWidth * 0x10000 / outWidth;
	out = (uint *)scaledImage;

	frac = fracStep >> 2;
	for( i = 0; i < outWidth; i++ )
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	frac = (fracStep >> 2) * 3;
	for( i = 0; i < outWidth; i++ )
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	if( isNormalMap )
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				normal[0] = MAKE_SIGNED( pix1[0] ) + MAKE_SIGNED( pix2[0] ) + MAKE_SIGNED( pix3[0] ) + MAKE_SIGNED( pix4[0] );
				normal[1] = MAKE_SIGNED( pix1[1] ) + MAKE_SIGNED( pix2[1] ) + MAKE_SIGNED( pix3[1] ) + MAKE_SIGNED( pix4[1] );
				normal[2] = MAKE_SIGNED( pix1[2] ) + MAKE_SIGNED( pix2[2] ) + MAKE_SIGNED( pix3[2] ) + MAKE_SIGNED( pix4[2] );

				if( !VectorNormalizeLength( normal ))
					VectorSet( normal, 0.5f, 0.5f, 1.0f );

				((byte *)(out+x))[0] = 128 + (byte)(127.0f * normal[0]);
				((byte *)(out+x))[1] = 128 + (byte)(127.0f * normal[1]);
				((byte *)(out+x))[2] = 128 + (byte)(127.0f * normal[2]);
				((byte *)(out+x))[3] = 255;
			}
		}
	}
	else
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				((byte *)(out+x))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
				((byte *)(out+x))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
				((byte *)(out+x))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
				((byte *)(out+x))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
			}
		}
	}

	return scaledImage;
}

/*
=================
GL_BoxFilter3x3

box filter 3x3
=================
*/
void GL_BoxFilter3x3( byte *out, const byte *in, int w, int h, int x, int y )
{
	int		r = 0, g = 0, b = 0, a = 0;
	int		count = 0, acount = 0;
	int		i, j, u, v;
	const byte	*pixel;

	for( i = 0; i < 3; i++ )
	{
		u = ( i - 1 ) + x;

		for( j = 0; j < 3; j++ )
		{
			v = ( j - 1 ) + y;

			if( u >= 0 && u < w && v >= 0 && v < h )
			{
				pixel = &in[( u + v * w ) * 4];

				if( pixel[3] != 0 )
				{
					r += pixel[0];
					g += pixel[1];
					b += pixel[2];
					a += pixel[3];
					acount++;
				}
			}
		}
	}

	if(  acount == 0 )
		acount = 1;

	out[0] = r / acount;
	out[1] = g / acount;
	out[2] = b / acount;
//	out[3] = (int)( SimpleSpline( ( a / 12.0f ) / 255.0f ) * 255 );
}

/*
=================
GL_ApplyFilter

Apply box-filter to 1-bit alpha
=================
*/
byte *GL_ApplyFilter( const byte *source, int width, int height )
{
	byte	*in = (byte *)source;
	byte	*out = (byte *)source;
	int	i;

	if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		return in;

	for( i = 0; source && i < width * height; i++, in += 4 )
	{
		if( in[0] == 0 && in[1] == 0 && in[2] == 0 && in[3] == 0 )
			GL_BoxFilter3x3( in, source, width, height, i % width, i / width );
	}

	return out;
}

/*
=================
GL_ApplyGamma

Assume input buffer is RGBA
=================
*/
byte *GL_ApplyGamma( const byte *source, int pixels, qboolean isNormalMap )
{
	byte	*in = (byte *)source;
	byte	*out = (byte *)source;
	int	i;

	if( source && !isNormalMap )
	{
		for( i = 0; i < pixels; i++, in += 4 )
		{
			in[0] = TextureToGamma( in[0] );
			in[1] = TextureToGamma( in[1] );
			in[2] = TextureToGamma( in[2] );
		}
	}
	return out;
}

/*
=================
GL_BuildMipMap

Operates in place, quartering the size of the texture
=================
*/
static void GL_BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, qboolean isNormalMap )
{
	byte	*out = in;
	int	instride = ALIGN( srcWidth * 4, 1 );
	int	mipWidth, mipHeight, outpadding;
	int	row, x, y, z;
	vec3_t	normal;

	if( !in ) return;

	mipWidth = max( 1, ( srcWidth >> 1 ));
	mipHeight = max( 1, ( srcHeight >> 1 ));
	outpadding = ALIGN( mipWidth * 4, 1 ) - mipWidth * 4;
	row = srcWidth << 2;

	// move through all layers
	for( z = 0; z < srcDepth; z++ )
	{
		if( isNormalMap )
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( in[row+4] )
						+ MAKE_SIGNED( next[row+0] ) + MAKE_SIGNED( next[row+4] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( in[row+5] )
						+ MAKE_SIGNED( next[row+1] ) + MAKE_SIGNED( next[row+5] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( in[row+6] )
						+ MAKE_SIGNED( next[row+2] ) + MAKE_SIGNED( next[row+6] );
					}
					else
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( next[row+0] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( next[row+1] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( next[row+2] );
					}


					if( !VectorNormalizeLength( normal ))
						VectorSet( normal, 0.5f, 0.5f, 1.0f );

					out[0] = 128 + (byte)(127.0f * normal[0]);
					out[1] = 128 + (byte)(127.0f * normal[1]);
					out[2] = 128 + (byte)(127.0f * normal[2]);
					out[3] = 255;
				}
			}
		}
		else
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						out[0] = (in[row+0] + in[row+4] + next[row+0] + next[row+4]) >> 2;
						out[1] = (in[row+1] + in[row+5] + next[row+1] + next[row+5]) >> 2;
						out[2] = (in[row+2] + in[row+6] + next[row+2] + next[row+6]) >> 2;
						out[3] = (in[row+3] + in[row+7] + next[row+3] + next[row+7]) >> 2;
					}
					else
					{
						out[0] = (in[row+0] + next[row+0]) >> 1;
						out[1] = (in[row+1] + next[row+1]) >> 1;
						out[2] = (in[row+2] + next[row+2]) >> 1;
						out[3] = (in[row+3] + next[row+3]) >> 1;
					}
				}
			}
		}
	}
}

/*
=================
GL_MakeLuminance

Converts the given image to luminance
=================
*/
void GL_MakeLuminance( rgbdata_t *in )
{
	byte	luminance;
	float	r, g, b;
	int	x, y;

	for( y = 0; y < in->height; y++ )
	{
		for( x = 0; x < in->width; x++ )
		{
			r = r_luminanceTable[in->buffer[4*(y*in->width+x)+0]][0];
			g = r_luminanceTable[in->buffer[4*(y*in->width+x)+1]][1];
			b = r_luminanceTable[in->buffer[4*(y*in->width+x)+2]][2];

			luminance = (byte)(r + g + b);

			in->buffer[4*(y*in->width+x)+0] = luminance;
			in->buffer[4*(y*in->width+x)+1] = luminance;
			in->buffer[4*(y*in->width+x)+2] = luminance;
		}
	}
}

static void GL_TextureImageRAW( gltexture_t *tex, GLint side, GLint level, GLint width, GLint height, GLint depth, GLint type, const void *data )
{
	GLuint	cubeTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB;
	qboolean	subImage = ( tex->flags & TF_IMG_UPLOADED );
	GLenum	inFormat = PFDesc[type].glFormat;
	GLint	dataType = GL_UNSIGNED_BYTE;

	Assert( tex != NULL );

	if( tex->flags & TF_DEPTHMAP )
		inFormat = GL_DEPTH_COMPONENT;

	if( tex->target == GL_TEXTURE_1D )
	{
		if( subImage ) pglTexSubImage1D( tex->target, level, 0, width, inFormat, dataType, data );
		else pglTexImage1D( tex->target, level, tex->format, width, 0, inFormat, dataType, data );
	}
	else if( tex->target == GL_TEXTURE_CUBE_MAP_ARB )
	{
		if( subImage ) pglTexSubImage2D( cubeTarget + side, level, 0, 0, width, height, inFormat, dataType, data );
		else pglTexImage2D( cubeTarget + side, level, tex->format, width, height, 0, inFormat, dataType, data );
	}
	else if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_2D_ARRAY_EXT )
	{
		if( subImage ) pglTexSubImage3D( tex->target, level, 0, 0, 0, width, height, depth, inFormat, dataType, data );
		else pglTexImage3D( tex->target, level, tex->format, width, height, depth, 0, inFormat, dataType, data );
	}
	else // 2D or RECT
	{
		if( subImage ) pglTexSubImage2D( tex->target, level, 0, 0, width, height, inFormat, dataType, data );
		else pglTexImage2D( tex->target, level, tex->format, width, height, 0, inFormat, dataType, data );
	}
}

static void GL_TextureImageDXT( gltexture_t *tex, GLint side, GLint level, GLint width, GLint height, GLint depth, size_t size, const void *data )
{
	GLuint	cubeTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB;
	qboolean	subImage = ( tex->flags & TF_IMG_UPLOADED );

	Assert( tex != NULL );

	if( tex->target == GL_TEXTURE_1D )
	{
		if( subImage ) pglCompressedTexSubImage1DARB( tex->target, level, 0, width, tex->format, size, data );
		else pglCompressedTexImage1DARB( tex->target, level, tex->format, width, 0, size, data );
	}
	else if( tex->target == GL_TEXTURE_CUBE_MAP_ARB )
	{
		if( subImage ) pglCompressedTexSubImage2DARB( cubeTarget + side, level, 0, 0, width, height, tex->format, size, data );
		else pglCompressedTexImage2DARB( cubeTarget + side, level, tex->format, width, height, 0, size, data );
	}
	else if( tex->target == GL_TEXTURE_3D || tex->target == GL_TEXTURE_2D_ARRAY_EXT )
	{
		if( subImage ) pglCompressedTexSubImage3DARB( tex->target, level, 0, 0, 0, width, height, depth, tex->format, size, data );
		else pglCompressedTexImage3DARB( tex->target, level, tex->format, width, height, depth, 0, size, data );
	}
	else // 2D or RECT
	{
		if( subImage ) pglCompressedTexSubImage2DARB( tex->target, level, 0, 0, width, height, tex->format, size, data );
		else pglCompressedTexImage2DARB( tex->target, level, tex->format, width, height, 0, size, data );
	}
}

/*
===============
GL_CheckTexImageError

show GL-errors on load images
===============
*/
static void GL_CheckTexImageError( gltexture_t *tex )
{
	int	err;

	Assert( tex != NULL );

	// catch possible errors
	if(( err = pglGetError()) != GL_NO_ERROR )
		MsgDev( D_ERROR, "GL_UploadTexture: error %x while uploading %s [%s]\n", err, tex->name, GL_TargetToString( tex->target ));
}

/*
===============
GL_UploadTexture

upload texture into video memory
===============
*/
static qboolean GL_UploadTexture( gltexture_t *tex, rgbdata_t *pic )
{
	byte		*buf, *data;
	size_t		texsize, size;
	uint		width, height;
	uint		i, j, numSides;
	uint		offset = 0;
	qboolean		normalMap;
	const byte	*bufend;

	Assert( pic != NULL );
	Assert( tex != NULL );

	GL_SetTextureTarget( tex, pic ); // must be first

	// make sure what target is correct
	if( tex->target == GL_NONE )
	{
		MsgDev( D_ERROR, "GL_UploadTexture: %s is not supported by your hardware\n", tex->name );
		return false;
	}

	GL_SetTextureDimensions( tex, pic->width, pic->height, pic->depth );
	GL_SetTextureFormat( tex, pic->type, pic->flags );

	tex->fogParams[0] = pic->fogParams[0];
	tex->fogParams[1] = pic->fogParams[1];
	tex->fogParams[2] = pic->fogParams[2];
	tex->fogParams[3] = pic->fogParams[3];

	if(( pic->width * pic->height ) & 3 )
	{
		// will be resampled, just tell me for debug targets
		Con_Reportf( "GL_UploadTexture: %s s&3 [%d x %d]\n", tex->name, pic->width, pic->height );
	}

	buf = pic->buffer;
	bufend = pic->buffer + pic->size; // total image size include all the layers, cube sides, mipmaps
	offset = GL_CalcImageSize( pic->type, pic->width, pic->height, pic->depth );
	texsize = GL_CalcTextureSize( tex->format, tex->width, tex->height, tex->depth );
	normalMap = FBitSet( tex->flags, TF_NORMALMAP ) ? true : false;
	numSides = FBitSet( pic->flags, IMAGE_CUBEMAP ) ? 6 : 1;

	// uploading texture into video memory, change the binding
	glState.currentTextures[glState.activeTMU] = tex->texnum;
	pglBindTexture( tex->target, tex->texnum );

	for( i = 0; i < numSides; i++ )
	{
		// track the buffer bounds
		if( buf != NULL && buf >= bufend )
			Host_Error( "GL_UploadTexture: %s image buffer overflow\n", tex->name );

		if( ImageDXT( pic->type ))
		{
			for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageDXT( tex, i, j, width, height, tex->depth, size, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;

				GL_CheckTexImageError( tex );
			}
		}
		else if( Q_max( 1, pic->numMips ) > 1 )	// not-compressed DDS
		{
			for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;

				GL_CheckTexImageError( tex );

			}
		}
		else // RGBA32
		{
			int mipCount = GL_CalcMipmapCount( tex, ( buf != NULL ));

			// NOTE: only single uncompressed textures can be resamples, no mips, no layers, no sides
			if(( tex->depth == 1 ) && ( pic->width != tex->width ) || ( pic->height != tex->height ))
				data = GL_ResampleTexture( buf, pic->width, pic->height, tex->width, tex->height, normalMap );
			else data = buf;

			if( !ImageDXT( pic->type ) && !FBitSet( tex->flags, TF_NOMIPMAP|TF_SKYSIDE ))
				data = GL_ApplyGamma( data, tex->width * tex->height * tex->depth, FBitSet( tex->flags, TF_NORMALMAP ));

			if( !ImageDXT( pic->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ))
				data = GL_ApplyFilter( data, tex->width, tex->height );

			// mips will be auto-generated if desired
			for( j = 0; j < mipCount; j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, data );
				if( mipCount > 1 )
					GL_BuildMipMap( data, width, height, tex->depth, normalMap );
				tex->size += texsize;
				tex->numMips++;

				GL_CheckTexImageError( tex );
			}

			// move to next side
			if( numSides > 1 && ( buf != NULL ))
				buf += GL_CalcImageSize( pic->type, pic->width, pic->height, 1 );
		}
	}

	tex->flags |= TF_IMG_UPLOADED; // done
	tex->numMips /= numSides;

	return true;
}

/*
===============
GL_ProcessImage

do specified actions on pixels
===============
*/
static void GL_ProcessImage( gltexture_t *tex, rgbdata_t *pic, imgfilter_t *filter )
{
	uint	img_flags = 0; 

	// force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	tex->encode = pic->encode; // share encode method

	if( ImageDXT( pic->type ))
	{
		if( !pic->numMips )
			tex->flags |= TF_NOMIPMAP; // disable mipmapping by user request

		// clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// copy flag about luma pixels
		if( pic->flags & IMAGE_HAS_LUMA )
			tex->flags |= TF_HAS_LUMA;

		if( pic->flags & IMAGE_QUAKEPAL )
			tex->flags |= TF_QUAKEPAL;

		// create luma texture from quake texture
		if( tex->flags & TF_MAKELUMA )
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
			tex->original = FS_CopyImage( pic ); // because current pic will be expanded to rgba

		// we need to expand image into RGBA buffer
		if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
			img_flags |= IMAGE_FORCE_RGBA;

		// processing image before uploading (force to rgba, make luma etc)
		if( pic->buffer ) Image_Process( &pic, 0, 0, img_flags, filter );

		if( tex->flags & TF_LUMINANCE )
		{
			if( !( tex->flags & TF_DEPTHMAP ))
			{
				GL_MakeLuminance( pic );
				tex->flags &= ~TF_LUMINANCE;
			}
			pic->flags &= ~IMAGE_HAS_COLOR;
		}
	}
}

/*
================
GL_LoadTexture
================
*/
int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags, imgfilter_t *filter )
{
	gltexture_t	*tex;
	rgbdata_t		*pic;
	uint		i, hash;
	uint		picFlags = 0;

	if( !COM_CheckString( name ) || !glw_state.initialized )
		return 0;

	if( Q_strlen( name ) >= sizeof( r_textures->name ))
	{
		Con_Printf( S_ERROR "LoadTexture: too long name %s (%d)\n", name, Q_strlen( name ));
		return 0;
	}

	// see if already loaded
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = r_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
			return (tex - r_textures);
	}

	if( flags & TF_NOFLIP_TGA )
		picFlags |= IL_DONTFLIP_TGA;

	if( FBitSet( flags, TF_KEEP_SOURCE ) && !FBitSet( flags, TF_EXPAND_SOURCE ))
		picFlags |= IL_KEEP_8BIT;	

	// set some image flags
	Image_SetForceFlags( picFlags );

	pic = FS_LoadImage( name, buf, size );
	if( !pic ) return 0; // couldn't loading image

	// find a free texture slot
	if( r_numTextures == MAX_TEXTURES )
		Host_Error( "GL_LoadTexture: MAX_TEXTURES limit exceeds\n" );

	// find a free texture_t slot
	for( i = 0, tex = r_textures; i < r_numTextures; i++, tex++ )
		if( !tex->name[0] ) break;

	if( i == r_numTextures )
	{
		if( r_numTextures == MAX_TEXTURES )
			Host_Error( "GL_LoadTexture: MAX_TEXTURES limit exceeds\n" );
		r_numTextures++;
	}

	tex = &r_textures[i];
	Q_strncpy( tex->name, name, sizeof( tex->name ));
	tex->flags = flags;

	if( flags & TF_SKYSIDE )
		tex->texnum = tr.skyboxbasenum++;
	else tex->texnum = i; // texnum is used for fast acess into r_textures array too

	GL_ProcessImage( tex, pic, filter );

	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gltexture_t ));
		FS_FreeImage( pic ); // release source texture
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc
	FS_FreeImage( pic ); // release source texture

	// add to hash table
	tex->hashValue = COM_HashKey( tex->name, TEXTURES_HASH_SIZE );
	tex->nextHash = r_texturesHashTable[tex->hashValue];
	r_texturesHashTable[tex->hashValue] = tex;

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return i;
}

/*
================
GL_LoadTextureArray
================
*/
int GL_LoadTextureArray( const char **names, int flags, imgfilter_t *filter )
{
	gltexture_t	*tex;
	rgbdata_t		*pic, *src;
	char		basename[256];
	uint		numLayers = 0;
	uint		picFlags = 0;
	char		name[256];
	uint		i, j, hash;

	if( !names || !names[0] || !glw_state.initialized )
		return 0;

	// count layers (g-cont. this is pontentially unsafe loop)
	for( i = 0; i < glConfig.max_2d_texture_layers && ( *names[i] != '\0' ); i++ )
		numLayers++;
	name[0] = '\0';

	if( numLayers <= 0 ) return 0;

	// create complexname from layer names
	for( i = 0; i < numLayers; i++ )
	{
		COM_FileBase( names[i], basename );
		Q_strncat( name, va( "%s", basename ), sizeof( name ));
		if( i != ( numLayers - 1 )) Q_strncat( name, "|", sizeof( name ));
	}

	Q_strncat( name, va( "[%i]", numLayers ), sizeof( name ));

	if( Q_strlen( name ) >= sizeof( r_textures->name ))
	{
		Con_Printf( S_ERROR "LoadTextureArray: too long name %s (%d)\n", name, Q_strlen( name ));
		return 0;
	}

	// see if already loaded
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = r_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
			return (tex - r_textures);
	}

	// load all the images and pack it into single image
	for( i = 0, pic = NULL; i < numLayers; i++ )
	{
		size_t	srcsize, dstsize, mipsize;

		src = FS_LoadImage( names[i], NULL, 0 );
		if( !src ) break; // coldn't find layer

		if( pic )
		{
			// mixed mode: DXT + RGB
			if( pic->type != src->type )
			{
				MsgDev( D_ERROR, "GL_LoadTextureArray: mismatch image format for %s and %s\n", names[0], names[i] );
				break;
			}

			// different mipcount
			if( pic->numMips != src->numMips )
			{
				MsgDev( D_ERROR, "GL_LoadTextureArray: mismatch mip count for %s and %s\n", names[0], names[i] );
				break;
			}

			if( pic->encode != src->encode )
			{
				MsgDev( D_ERROR, "GL_LoadTextureArray: mismatch custom encoding for %s and %s\n", names[0], names[i] );
				break;
			}

			// but allow to rescale raw images
			if( ImageRAW( pic->type ) && ImageRAW( src->type ) && ( pic->width != src->width || pic->height != src->height ))
				Image_Process( &src, pic->width, pic->height, IMAGE_RESAMPLE, NULL );

			if( pic->size != src->size )
			{
				MsgDev( D_ERROR, "GL_LoadTextureArray: mismatch image size for %s and %s\n", names[0], names[i] );
				break;
			}
		}
		else
		{
			// create new image
			pic = Mem_Alloc( host.imagepool, sizeof( rgbdata_t ));
			memcpy( pic, src, sizeof( rgbdata_t ));

			// expand pic buffer for all layers
			pic->buffer = Mem_Alloc( host.imagepool, pic->size * numLayers );
			pic->depth = 0;
		}

		mipsize = srcsize = dstsize = 0;

		for( j = 0; j < max( 1, pic->numMips ); j++ )
		{
			int width = max( 1, ( pic->width >> j ));
			int height = max( 1, ( pic->height >> j ));
			mipsize = GL_CalcImageSize( pic->type, width, height, 1 );
			memcpy( pic->buffer + dstsize + mipsize * i, src->buffer + srcsize, mipsize );
			dstsize += mipsize * numLayers;
			srcsize += mipsize;
		}

		FS_FreeImage( src );

		// increase layers
		pic->depth++;
	}

	// there were errors
	if( !pic || ( pic->depth != numLayers ))
	{
		MsgDev( D_ERROR, "GL_LoadTextureArray: not all layers were loaded. Texture array is not created\n" );
		if( pic ) FS_FreeImage( pic );
		return 0;
	}	

	// it's multilayer image!
	pic->flags |= IMAGE_MULTILAYER;
	pic->size *= numLayers;

	// find a free texture slot
	if( r_numTextures == MAX_TEXTURES )
		Host_Error( "GL_LoadTexture: MAX_TEXTURES limit exceeds\n" );

	// find a free texture_t slot
	for( i = 0, tex = r_textures; i < r_numTextures; i++, tex++ )
		if( !tex->name[0] ) break;

	if( i == r_numTextures )
	{
		if( r_numTextures == MAX_TEXTURES )
			Host_Error( "GL_LoadTexture: MAX_TEXTURES limit exceeds\n" );
		r_numTextures++;
	}

	tex = &r_textures[i];
	Q_strncpy( tex->name, name, sizeof( tex->name ));
	tex->flags = flags;
	tex->texnum = i; // texnum is used for fast acess into r_textures array too

	GL_ProcessImage( tex, pic, filter );
	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gltexture_t ));
		FS_FreeImage( pic ); // release source texture
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc
	FS_FreeImage( pic ); // release source texture

	// add to hash table
	tex->hashValue = COM_HashKey( tex->name, TEXTURES_HASH_SIZE );
	tex->nextHash = r_texturesHashTable[tex->hashValue];
	r_texturesHashTable[tex->hashValue] = tex;

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return i;
}

/*
================
GL_LoadTextureInternal
================
*/
int GL_LoadTextureInternal( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	gltexture_t	*tex;
	uint		i, hash;

	if( !COM_CheckString( name ) || !glw_state.initialized )
		return 0;

	if( Q_strlen( name ) >= sizeof( r_textures->name ))
	{
		Con_Printf( S_ERROR "LoadTexture: too long name %s (%d)\n", name, Q_strlen( name ));
		return 0;
	}

	// see if already loaded
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = r_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
		{
			if( update ) break;
			return (tex - r_textures);
		}
	}

	if( !pic ) return 0; // couldn't loading image
	if( update && !tex )
	{
		Host_Error( "Couldn't find texture %s for update\n", name );
	}

	// find a free texture slot
	if( r_numTextures == MAX_TEXTURES )
		Host_Error( "GL_LoadTexture: MAX_TEXTURES limit exceeds\n" );

	if( !update )
	{
		// find a free texture_t slot
		for( i = 0, tex = r_textures; i < r_numTextures; i++, tex++ )
			if( !tex->name[0] ) break;

		if( i == r_numTextures )
		{
			if( r_numTextures == MAX_TEXTURES )
				Host_Error( "GL_LoadTexture: MAX_TEXTURES limit exceeds\n" );
			r_numTextures++;
		}

		tex = &r_textures[i];
		hash = COM_HashKey( name, TEXTURES_HASH_SIZE );
		Q_strncpy( tex->name, name, sizeof( tex->name ));
		tex->texnum = i;	// texnum is used for fast acess into r_textures array too
		tex->flags = flags;
	}
	else
	{
		tex->flags |= flags;
	}

	GL_ProcessImage( tex, pic, NULL );
	if( !GL_UploadTexture( tex, pic ))
	{
		memset( tex, 0, sizeof( gltexture_t ));
		return 0;
	}

	GL_ApplyTextureParams( tex ); // update texture filter, wrap etc

	if( !update )
          {
		// add to hash table
		tex->hashValue = COM_HashKey( tex->name, TEXTURES_HASH_SIZE );
		tex->nextHash = r_texturesHashTable[tex->hashValue];
		r_texturesHashTable[tex->hashValue] = tex;
	}

	return (tex - r_textures);
}

/*
================
GL_CreateTexture

creates texture from buffer
================
*/
int GL_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags )
{
	rgbdata_t	r_empty;
	int	texture;

	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = width;
	r_empty.height = height;
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * 4;
	r_empty.flags = IMAGE_HAS_COLOR | (( flags & TF_HAS_ALPHA ) ? IMAGE_HAS_ALPHA : 0 );
	r_empty.buffer = (byte *)buffer;

	if( FBitSet( flags, TF_TEXTURE_1D ))
	{
		r_empty.height = 1;
		r_empty.size = r_empty.width * 4;
	}
	else if( FBitSet( flags, TF_TEXTURE_3D ))
	{
		if( !GL_Support( GL_TEXTURE_3D_EXT ))
			return 0;

		r_empty.depth = r_empty.width; // assume 3D texture as cube
		r_empty.size = r_empty.width * r_empty.height * r_empty.depth * 4;
	}
	else if( FBitSet( flags, TF_CUBEMAP ))
	{
		SetBits( r_empty.flags, IMAGE_CUBEMAP );
		ClearBits( flags, TF_CUBEMAP ); // will be set later
		r_empty.size *= 6;
	}

	texture = GL_LoadTextureInternal( name, &r_empty, flags, false );

	return texture;
}

/*
================
GL_CreateTextureArray

creates texture array from buffer
================
*/
int GL_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	rgbdata_t	r_empty;
	int	texture;

	memset( &r_empty, 0, sizeof( r_empty ));
	r_empty.width = width;
	r_empty.height = height;
	r_empty.depth = depth;
	r_empty.type = PF_RGBA_32;
	r_empty.size = r_empty.width * r_empty.height * r_empty.depth * 4;
	r_empty.flags = IMAGE_HAS_COLOR | (( flags & TF_HAS_ALPHA ) ? IMAGE_HAS_ALPHA : 0 );
	r_empty.buffer = (byte *)buffer;

	if( FBitSet( flags, TF_TEXTURE_3D ))
	{
		if( !GL_Support( GL_TEXTURE_3D_EXT ))
			return 0;
	}
	else
	{
		if( !GL_Support( GL_TEXTURE_ARRAY_EXT ))
			return 0;
		SetBits( r_empty.flags, IMAGE_MULTILAYER );
	}

	texture = GL_LoadTextureInternal( name, &r_empty, flags, false );

	return texture;
}

/*
================
GL_ProcessTexture
================
*/
void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	gltexture_t	*image;
	rgbdata_t		*pic;
	int		flags = 0;

	if( texnum <= 0 ) return; // missed image
	Assert( texnum > 0 && texnum < MAX_TEXTURES );
	image = &r_textures[texnum];

	// select mode
	if( gamma != -1.0f )
	{
		flags = IMAGE_LIGHTGAMMA;
	}
	else if( topColor != -1 && bottomColor != -1 )
	{
		flags = IMAGE_REMAP;
	}
	else
	{
		MsgDev( D_ERROR, "GL_ProcessTexture: bad operation for %s\n", image->name );
		return;
	}

	if( !image->original )
	{
		MsgDev( D_ERROR, "GL_ProcessTexture: no input data for %s\n", image->name );
		return;
	}

	if( ImageDXT( image->original->type ))
	{
		MsgDev( D_ERROR, "GL_ProcessTexture: can't process compressed texture %s\n", image->name );
		return;
	}

	// all the operations makes over the image copy not an original
	pic = FS_CopyImage( image->original );
	Image_Process( &pic, topColor, bottomColor, flags, NULL );

	GL_UploadTexture( image, pic );
	GL_ApplyTextureParams( image ); // update texture filter, wrap etc

	FS_FreeImage( pic );
}

/*
================
GL_LoadTexture
================
*/
int GL_FindTexture( const char *name )
{
	gltexture_t	*tex;
	uint		hash;

	if( !COM_CheckString( name ) || !glw_state.initialized )
		return 0;

	if( Q_strlen( name ) >= sizeof( r_textures->name ))
	{
		Con_Printf( S_ERROR "FindTexture: too long name %s (%d)\n", name, Q_strlen( name ));
		return 0;
	}

	// see if already loaded
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = r_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
			return (tex - r_textures);
	}

	return 0;
}

/*
================
GL_FreeImage

Frees image by name
================
*/
void GL_FreeImage( const char *name )
{
	gltexture_t	*tex;
	uint		hash;

	if( !COM_CheckString( name ) || !glw_state.initialized )
		return;

	if( Q_strlen( name ) >= sizeof( r_textures->name ))
	{
		Con_Printf( S_ERROR "FreeTexture: too long name %s (%d)\n", name, Q_strlen( name ));
		return;
	}

	// see if already loaded
	hash = COM_HashKey( name, TEXTURES_HASH_SIZE );

	for( tex = r_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash )
	{
		if( !Q_stricmp( tex->name, name ))
		{
			R_FreeImage( tex );
			return;
		}
	}
}

/*
================
GL_FreeTexture
================
*/
void GL_FreeTexture( GLenum texnum )
{
	// number 0 it's already freed
	if( texnum <= 0 || !glw_state.initialized )
		return;

	Assert( texnum > 0 && texnum < MAX_TEXTURES );
	R_FreeImage( &r_textures[texnum] );
}

/*
================
R_FreeImage
================
*/
void R_FreeImage( gltexture_t *image )
{
	gltexture_t	*cur;
	gltexture_t	**prev;

	Assert( image != NULL );

	if( !image->name[0] )
	{
		if( image->texnum != 0 )
			MsgDev( D_ERROR, "trying to free unnamed texture with texnum %i\n", image->texnum );
		return;
	}

	// remove from hash table
	prev = &r_texturesHashTable[image->hashValue];

	while( 1 )
	{
		cur = *prev;
		if( !cur ) break;

		if( cur == image )
		{
			*prev = cur->nextHash;
			break;
		}
		prev = &cur->nextHash;
	}

	// release source
	if( image->original )
		FS_FreeImage( image->original );

	pglDeleteTextures( 1, &image->texnum );
	memset( image, 0, sizeof( *image ));
}

/*
==============================================================================

INTERNAL TEXTURES

==============================================================================
*/
/*
==================
R_InitDefaultTexture
==================
*/
static rgbdata_t *R_InitDefaultTexture( texFlags_t *flags )
{
	int	x, y;

	// also use this for bad textures, but without alpha
	r_image.width = r_image.height = 16;
	r_image.buffer = data2D;
	r_image.flags = IMAGE_HAS_COLOR;
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * 4;

	*flags = 0;

	// emo-texture from quake1
	for( y = 0; y < 16; y++ )
	{
		for( x = 0; x < 16; x++ )
		{
			if(( y < 8 ) ^ ( x < 8 ))
				((uint *)&data2D)[y*16+x] = 0xFFFF00FF;
			else ((uint *)&data2D)[y*16+x] = 0xFF000000;
		}
	}
	return &r_image;
}

/*
==================
R_InitParticleTexture
==================
*/
static rgbdata_t *R_InitParticleTexture( texFlags_t *flags )
{
	int	x, y;
	int	dx2, dy, d;

	// particle texture
	r_image.width = r_image.height = 16;
	r_image.buffer = data2D;
	r_image.flags = (IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA);
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * 4;

	*flags = TF_CLAMP;

	for( x = 0; x < 16; x++ )
	{
		dx2 = x - 8;
		dx2 = dx2 * dx2;

		for( y = 0; y < 16; y++ )
		{
			dy = y - 8;
			d = 255 - 35 * sqrt( dx2 + dy * dy );
			data2D[( y*16 + x ) * 4 + 3] = bound( 0, d, 255 );
		}
	}
	return &r_image;
}

/*
==================
R_InitCinematicTexture
==================
*/
static rgbdata_t *R_InitCinematicTexture( texFlags_t *flags )
{
	r_image.type = PF_RGBA_32;
	r_image.flags = IMAGE_HAS_COLOR;
	r_image.width = 640; // same as menu head
	r_image.height = 100;
	r_image.size = r_image.width * r_image.height * 4;
	r_image.buffer = NULL;

	*flags = TF_NOMIPMAP|TF_CLAMP;

	return &r_image;
}

/*
==================
R_InitSolidColorTexture
==================
*/
static rgbdata_t *R_InitSolidColorTexture( texFlags_t *flags, int color )
{
	// solid color texture
	r_image.width = r_image.height = 1;
	r_image.buffer = data2D;
	r_image.flags = IMAGE_HAS_COLOR;
	r_image.type = PF_RGB_24;
	r_image.size = r_image.width * r_image.height * 3;

	*flags = 0;

	data2D[0] = data2D[1] = data2D[2] = color;
	return &r_image;
}

/*
==================
R_InitWhiteTexture
==================
*/
static rgbdata_t *R_InitWhiteTexture( texFlags_t *flags )
{
	return R_InitSolidColorTexture( flags, 255 );
}

/*
==================
R_InitGrayTexture
==================
*/
static rgbdata_t *R_InitGrayTexture( texFlags_t *flags )
{
	return R_InitSolidColorTexture( flags, 127 );
}

/*
==================
R_InitBlackTexture
==================
*/
static rgbdata_t *R_InitBlackTexture( texFlags_t *flags )
{
	return R_InitSolidColorTexture( flags, 0 );
}

/*
==================
R_InitDlightTexture
==================
*/
void R_InitDlightTexture( void )
{
	if( tr.dlightTexture != 0 )
		return; // already initialized

	r_image.width = BLOCK_SIZE; 
	r_image.height = BLOCK_SIZE;
	r_image.flags = IMAGE_HAS_COLOR;
	r_image.type = PF_RGBA_32;
	r_image.size = r_image.width * r_image.height * 4;
	r_image.buffer = NULL;

	tr.dlightTexture = GL_LoadTextureInternal( "*dlight", &r_image, TF_NOMIPMAP|TF_CLAMP|TF_ATLAS_PAGE, false );
}

/*
==================
R_InitBuiltinTextures
==================
*/
static void R_InitBuiltinTextures( void )
{
	rgbdata_t		*pic;
	texFlags_t	flags;

	const struct
	{
		char	*name;
		int	*texnum;
		rgbdata_t	*(*init)( texFlags_t *flags );
	}

	textures[] =
	{
	{ "*default", &tr.defaultTexture, R_InitDefaultTexture },
	{ "*particle", &tr.particleTexture, R_InitParticleTexture },
	{ "*white", &tr.whiteTexture, R_InitWhiteTexture },
	{ "*gray", &tr.grayTexture, R_InitGrayTexture },
	{ "*black", &tr.blackTexture, R_InitBlackTexture },	// not used by engine
	{ "*cintexture", &tr.cinTexture, R_InitCinematicTexture },	// intermediate buffer to renderer cinematic textures
	{ NULL, NULL, NULL }
	};
	size_t	i, num_builtin_textures = ARRAYSIZE( textures ) - 1;

	for( i = 0; i < num_builtin_textures; i++ )
	{
		memset( &r_image, 0, sizeof( rgbdata_t ));
		memset( data2D, 0xFF, sizeof( data2D ));

		pic = textures[i].init( &flags );
		if( pic == NULL ) continue;
		*textures[i].texnum = GL_LoadTextureInternal( textures[i].name, pic, flags, false );
	}
}

/*
===============
R_TextureList_f
===============
*/
void R_TextureList_f( void )
{
	gltexture_t	*image;
	int		i, texCount, bytes = 0;

	Con_Printf( "\n" );
	Con_Printf( " -id-   -w-  -h-     -size- -fmt- -type- -data-  -encode- -wrap- -depth- -name--------\n" );

	for( i = texCount = 0, image = r_textures; i < r_numTextures; i++, image++ )
	{
		if( !image->texnum ) continue;

		bytes += image->size;
		texCount++;

		Con_Printf( "%4i: ", i );
		Con_Printf( "%4i %4i ", image->width, image->height );
		Con_Printf( "%12s ", Q_memprint( image->size ));

		switch( image->format )
		{
		case GL_COMPRESSED_RGBA_ARB:
			Con_Printf( "CRGBA " );
			break;
		case GL_COMPRESSED_RGB_ARB:
			Con_Printf( "CRGB  " );
			break;
		case GL_COMPRESSED_LUMINANCE_ALPHA_ARB:
			Con_Printf( "CLA   " );
			break;
		case GL_COMPRESSED_LUMINANCE_ARB:
			Con_Printf( "CL    " );
			break;
		case GL_COMPRESSED_ALPHA_ARB:
			Con_Printf( "CA    " );
			break;
		case GL_COMPRESSED_INTENSITY_ARB:
			Con_Printf( "CI    " );
			break;
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			Con_Printf( "DXT1c " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			Con_Printf( "DXT1a " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			Con_Printf( "DXT3  " );
			break;
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			Con_Printf( "DXT5  " );
			break;
		case GL_RGBA:
			Con_Printf( "RGBA  " );
			break;
		case GL_RGBA8:
			Con_Printf( "RGBA8 " );
			break;
		case GL_RGBA4:
			Con_Printf( "RGBA4 " );
			break;
		case GL_RGB:
			Con_Printf( "RGB   " );
			break;
		case GL_RGB8:
			Con_Printf( "RGB8  " );
			break;
		case GL_RGB5:
			Con_Printf( "RGB5  " );
			break;
		case GL_LUMINANCE4_ALPHA4:
			Con_Printf( "L4A4  " );
			break;
		case GL_LUMINANCE_ALPHA:
		case GL_LUMINANCE8_ALPHA8:
			Con_Printf( "L8A8  " );
			break;
		case GL_LUMINANCE4:
			Con_Printf( "L4    " );
			break;
		case GL_LUMINANCE:
		case GL_LUMINANCE8:
			Con_Printf( "L8    " );
			break;
		case GL_ALPHA8:
			Con_Printf( "A8    " );
			break;
		case GL_INTENSITY8:
			Con_Printf( "I8    " );
			break;
		case GL_DEPTH_COMPONENT:
		case GL_DEPTH_COMPONENT24:
			Con_Printf( "DPTH24" );
			break;			
		case GL_DEPTH_COMPONENT32F:
			Con_Printf( "DPTH32" );
			break;
		case GL_LUMINANCE16F_ARB:
			Con_Printf( "L16F  " );
			break;
		case GL_LUMINANCE32F_ARB:
			Con_Printf( "L32F  " );
			break;
		case GL_LUMINANCE_ALPHA16F_ARB:
			Con_Printf( "LA16F " );
			break;
		case GL_LUMINANCE_ALPHA32F_ARB:
			Con_Printf( "LA32F " );
			break;
		case GL_RGB16F_ARB:
			Con_Printf( "RGB16F" );
			break;
		case GL_RGB32F_ARB:
			Con_Printf( "RGB32F" );
			break;
		case GL_RGBA16F_ARB:
			Con_Printf( "RGBA16F" );
			break;
		case GL_RGBA32F_ARB:
			Con_Printf( "RGBA32F" );
			break;
		default:
			Con_Printf( " ^1ERROR^7 " );
			break;
		}

		switch( image->target )
		{
		case GL_TEXTURE_1D:
			Con_Printf( " 1D   " );
			break;
		case GL_TEXTURE_2D:
			Con_Printf( " 2D   " );
			break;
		case GL_TEXTURE_3D:
			Con_Printf( " 3D   " );
			break;
		case GL_TEXTURE_CUBE_MAP_ARB:
			Con_Printf( "CUBE  " );
			break;
		case GL_TEXTURE_RECTANGLE_EXT:
			Con_Printf( "RECT  " );
			break;
		case GL_TEXTURE_2D_ARRAY_EXT:
			Con_Printf( "ARRAY " );
			break;
		default:
			Con_Printf( "????  " );
			break;
		}

		if( image->flags & TF_NORMALMAP )
			Con_Printf( "normal  " );
		else Con_Printf( "diffuse " );

		switch( image->encode )
		{
		case DXT_ENCODE_COLOR_YCoCg:
			Con_Printf( "YCoCg     " );
			break;
		case DXT_ENCODE_NORMAL_AG_ORTHO:
			Con_Printf( "ortho     " );
			break;
		case DXT_ENCODE_NORMAL_AG_STEREO:
			Con_Printf( "stereo    " );
			break;
		case DXT_ENCODE_NORMAL_AG_PARABOLOID:
			Con_Printf( "parabolic " );
			break;
		case DXT_ENCODE_NORMAL_AG_QUARTIC:
			Con_Printf( "quartic   " );
			break;
		case DXT_ENCODE_NORMAL_AG_AZIMUTHAL:
			Con_Printf( "azimuthal " );
			break;
		default:
			Con_Printf( "default   " );
			break;
		}

		if( image->flags & TF_CLAMP )
			Con_Printf( "clamp  " );
		else if( image->flags & TF_BORDER )
			Con_Printf( "border " );
		else Con_Printf( "repeat " );
		Con_Printf( "   %d  ", image->depth );
		Con_Printf( "  %s\n", image->name );
	}

	Con_Printf( "---------------------------------------------------------\n" );
	Con_Printf( "%i total textures\n", texCount );
	Con_Printf( "%s total memory used\n", Q_memprint( bytes ));
	Con_Printf( "\n" );
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void )
{
	float	f;
	uint	i;

	memset( r_textures, 0, sizeof( r_textures ));
	memset( r_texturesHashTable, 0, sizeof( r_texturesHashTable ));
	r_numTextures = 0;

	// create unused 0-entry
	Q_strncpy( r_textures->name, "*unused*", sizeof( r_textures->name ));
	r_textures->hashValue = COM_HashKey( r_textures->name, TEXTURES_HASH_SIZE );
	r_textures->nextHash = r_texturesHashTable[r_textures->hashValue];
	r_texturesHashTable[r_textures->hashValue] = r_textures;
	r_numTextures = 1;

	// build luminance table
	for( i = 0; i < 256; i++ )
	{
		f = (float)i;
		r_luminanceTable[i][0] = f * 0.299f;
		r_luminanceTable[i][1] = f * 0.587f;
		r_luminanceTable[i][2] = f * 0.114f;
	}

	// set texture parameters
	R_SetTextureParameters();
	R_InitBuiltinTextures();

	R_ParseTexFilters( "scripts/texfilter.txt" );
	Cmd_AddCommand( "texturelist", R_TextureList_f, "display loaded textures list" );
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages( void )
{
	gltexture_t	*image;
	int		i;

	if( !glw_state.initialized ) return;

	Cmd_RemoveCommand( "texturelist" );
	GL_CleanupAllTextureUnits();

	for( i = 0, image = r_textures; i < r_numTextures; i++, image++ )
		R_FreeImage( image );

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	memset( r_texturesHashTable, 0, sizeof( r_texturesHashTable ));
	memset( r_textures, 0, sizeof( r_textures ));
	r_numTextures = 0;
}