/*
gu_triapi.c - TriAPI draw methods
Copyright (C) 2011 Uncle Mike
Copyright (C) 2019 a1batross
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
#include "const.h"

static struct
{
	int		renderMode;		// override kRenderMode from TriAPI
	vec4_t		triRGBA;
} ds;

/*
===============================================================

  TRIAPI IMPLEMENTATION

===============================================================
*/
#if 1
static struct
{
	void			*start;
	gu_vert_ftcv_t	*list;
	qboolean		begin;
	int				count;
	int 			prim;
	unsigned int	flags;
}tri_gucontext = 
{ 
	.start	= NULL,
	.list	= NULL,
	.begin	= false, 
	.count	= 0, 
	.prim	= 0, 
	.flags	= 0 
};
#endif

/*
=============
TriRenderMode

set rendermode
=============
*/
void TriRenderMode( int mode )
{
#if 1
	ds.renderMode = mode;
	switch( mode )
	{
	case kRenderNormal:
		sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
		sceGuDisable( GU_BLEND );
		sceGuDepthMask( GU_FALSE );
		break;
	case kRenderTransAlpha:
		sceGuEnable( GU_BLEND );
		sceGuTexFunc( GU_TFX_MODULATE, GU_TCC_RGBA );
		sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
		sceGuDepthMask( GU_TRUE );
		break;
	case kRenderTransColor:
	case kRenderTransTexture:
		sceGuEnable( GU_BLEND );
		sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0 );
		break;
	case kRenderGlow:
	case kRenderTransAdd:
		sceGuBlendFunc( GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, GUBLEND1 );
		sceGuEnable( GU_BLEND );
		sceGuDepthMask( GU_TRUE );
		break;
	}
#else
	ds.renderMode = mode;
	switch( mode )
	{
	case kRenderNormal:
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglDisable( GL_BLEND );
		pglDepthMask( GL_TRUE );
		break;
	case kRenderTransAlpha:
		pglEnable( GL_BLEND );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglDepthMask( GL_FALSE );
		break;
	case kRenderTransColor:
	case kRenderTransTexture:
		pglEnable( GL_BLEND );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		break;
	case kRenderGlow:
	case kRenderTransAdd:
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
		pglEnable( GL_BLEND );
		pglDepthMask( GL_FALSE );
		break;
	}
#endif
}

/*
=============
TriBegin

begin triangle sequence
=============
*/
void TriBegin( int mode )
{
#if 1
	switch( mode )
	{
	case TRI_POINTS:
		tri_gucontext.prim = GU_POINTS;
		break;
	case TRI_TRIANGLES:
		tri_gucontext.prim = GU_TRIANGLES;
		break;
	case TRI_TRIANGLE_FAN:
		tri_gucontext.prim = GU_TRIANGLE_FAN;
		break;
	case TRI_QUADS:
		tri_gucontext.prim = GU_TRIANGLE_FAN;
		break;
	case TRI_LINES:
		tri_gucontext.prim = GU_LINES;
		break;
	case TRI_TRIANGLE_STRIP:
		tri_gucontext.prim = GU_TRIANGLE_STRIP;
		break;
	case TRI_QUAD_STRIP:
		tri_gucontext.prim = GU_TRIANGLE_STRIP;
		break;
	case TRI_POLYGON:
	default:
		tri_gucontext.prim = GU_TRIANGLE_FAN;
		break;
	}	
	tri_gucontext.start = extGuBeginPacket( NULL );
	tri_gucontext.list  = ( gu_vert_ftcv_t* )tri_gucontext.start;	
	tri_gucontext.begin	= true;
	tri_gucontext.count	= 0;
	tri_gucontext.flags	= 0;
#else
	switch( mode )
	{
	case TRI_POINTS:
		mode = GL_POINTS;
		break;
	case TRI_TRIANGLES:
		mode = GL_TRIANGLES;
		break;
	case TRI_TRIANGLE_FAN:
		mode = GL_TRIANGLE_FAN;
		break;
	case TRI_QUADS:
		mode = GL_QUADS;
		break;
	case TRI_LINES:
		mode = GL_LINES;
		break;
	case TRI_TRIANGLE_STRIP:
		mode = GL_TRIANGLE_STRIP;
		break;
	case TRI_QUAD_STRIP:
		mode = GL_QUAD_STRIP;
		break;
	case TRI_POLYGON:
	default:
		mode = GL_POLYGON;
		break;
	}

	pglBegin( mode );
#endif
}

/*
=============
TriEnd

draw triangle sequence
=============
*/
void TriEnd( void )
{
#if 1
	unsigned int vert_size;
	
	if( !tri_gucontext.begin )
		return;

	extGuEndPacket( ( void* )tri_gucontext.list );
	sceGuDrawArray( tri_gucontext.prim, tri_gucontext.flags, tri_gucontext.count, 0, tri_gucontext.start );
	
	tri_gucontext.begin = false;
#else	
	pglEnd( );
#endif
}

/*
=============
_TriColor4f

=============
*/
void _TriColor4f( float r, float g, float b, float a )
{
#if 1
	if( tri_gucontext.begin )
	{
		tri_gucontext.flags |= GU_COLOR_8888; 
		tri_gucontext.list->c = GUCOLOR4F( r, g, b, a );
	}
	else	
	{		
		sceGuColor( GUCOLOR4F( r, g, b, a ) );
	}
#else
	pglColor4f( r, g, b, a );
#endif
}

/*
=============
_TriColor4ub

=============
*/
void _TriColor4ub( byte r, byte g, byte b, byte a )
{
#if 1
	if( tri_gucontext.begin )
	{
		tri_gucontext.flags |= GU_COLOR_8888; 	
		tri_gucontext.list->c = GUCOLOR4UB( r, g, b, a );
	}
	else	
	{	
		sceGuColor( GUCOLOR4UB( r, g, b, a ) );
	}
#else
	pglColor4ub( r, g, b, a );
#endif
}


/*
=============
TriColor4ub

=============
*/
void TriColor4ub( byte r, byte g, byte b, byte a )
{
	ds.triRGBA[0] = r * (1.0f / 255.0f);
	ds.triRGBA[1] = g * (1.0f / 255.0f);
	ds.triRGBA[2] = b * (1.0f / 255.0f);
	ds.triRGBA[3] = a * (1.0f / 255.0f);

	_TriColor4f( ds.triRGBA[0], ds.triRGBA[1], ds.triRGBA[2], 1.0f );
}

/*
=================
TriColor4f
=================
*/
void TriColor4f( float r, float g, float b, float a )
{
	if( ds.renderMode == kRenderTransAlpha )
		TriColor4ub( r * 255.0f, g * 255.0f, b * 255.0f, a * 255.0f );
	else _TriColor4f( r * a, g * a, b * a, 1.0 );

	ds.triRGBA[0] = r;
	ds.triRGBA[1] = g;
	ds.triRGBA[2] = b;
	ds.triRGBA[3] = a;
}

/*
=============
TriTexCoord2f

=============
*/
void TriTexCoord2f( float u, float v )
{
#if 1
	if( !tri_gucontext.begin ) 
		return;

	tri_gucontext.flags |= GU_TEXTURE_32BITF;	
	tri_gucontext.list->u = u;
	tri_gucontext.list->v = v;
#else
	pglTexCoord2f( u, v );
#endif
}

/*
=============
TriVertex3fv

=============
*/
void TriVertex3fv( const float *v )
{
#if 1
	if( !tri_gucontext.begin ) 
		return;

	if( !(tri_gucontext.flags & GU_TEXTURE_32BITF) )
	{
		tri_gucontext.list->u = 0;
		tri_gucontext.list->v = 0;
	}
	
	if( !(tri_gucontext.flags & GU_COLOR_8888) )
	{
		tri_gucontext.list->c = 0xffffffff;
	}

	tri_gucontext.flags |= GU_VERTEX_32BITF; 
	tri_gucontext.list->x = v[0];
	tri_gucontext.list->y = v[1];
	tri_gucontext.list->z = v[2];
	tri_gucontext.list++;
	
	tri_gucontext.count++;
#else
	pglVertex3fv( v );
#endif
}

/*
=============
TriVertex3f

=============
*/
void TriVertex3f( float x, float y, float z )
{
#if 1
	if( !tri_gucontext.begin ) 
		return;
	
	if( !(tri_gucontext.flags & GU_TEXTURE_32BITF) )
	{
		tri_gucontext.list->u = 0;
		tri_gucontext.list->v = 0;
	}
	
	if( !(tri_gucontext.flags & GU_COLOR_8888) )
	{
		tri_gucontext.list->c = 0xffffffff;
	}

	tri_gucontext.flags |= GU_VERTEX_32BITF; 
	tri_gucontext.list->x = x;
	tri_gucontext.list->y = y;
	tri_gucontext.list->z = z;
	tri_gucontext.list++;

	tri_gucontext.count++;
#else
	pglVertex3f( x, y, z );
#endif
}

/*
=============
TriWorldToScreen

convert world coordinates (x,y,z) into screen (x, y)
=============
*/
int TriWorldToScreen( const float *world, float *screen )
{
	int	retval;

	retval = R_WorldToScreen( world, screen );

	screen[0] =  0.5f * screen[0] * (float)RI.viewport[2];
	screen[1] = -0.5f * screen[1] * (float)RI.viewport[3];
	screen[0] += 0.5f * (float)RI.viewport[2];
	screen[1] += 0.5f * (float)RI.viewport[3];

	return retval;
}

/*
=============
TriSpriteTexture

bind current texture
=============
*/
int TriSpriteTexture( model_t *pSpriteModel, int frame )
{
	int	gl_texturenum;

	if(( gl_texturenum = R_GetSpriteTexture( pSpriteModel, frame )) == 0 )
		return 0;

	if( gl_texturenum <= 0 || gl_texturenum > MAX_TEXTURES )
		gl_texturenum = tr.defaultTexture;

	GL_Bind( XASH_TEXTURE0, gl_texturenum );

	return 1;
}

/*
=============
TriFog

enables global fog on the level
=============
*/
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	// overrided by internal fog
	if( RI.fogEnabled ) return;
	RI.fogCustom = bOn;

	// check for invalid parms
	if( flEnd <= flStart )
	{
		glState.isFogEnabled = RI.fogCustom = false;
#if 1
		sceGuDisable( GU_FOG );	
#else
		pglDisable( GL_FOG );
#endif
		return;
	}
#if 1
	if( RI.fogCustom )
		sceGuEnable( GU_FOG );
	else sceGuDisable( GU_FOG );
#else
	if( RI.fogCustom )
		pglEnable( GL_FOG );
	else pglDisable( GL_FOG );
#endif
	// copy fog params
	RI.fogColor[0] = flFogColor[0] / 255.0f;
	RI.fogColor[1] = flFogColor[1] / 255.0f;
	RI.fogColor[2] = flFogColor[2] / 255.0f;
	RI.fogStart = flStart;
	RI.fogColor[3] = 1.0f;
	RI.fogDensity = 0.0f;
	RI.fogSkybox = true;
	RI.fogEnd = flEnd;
	
#if 1
	printf("TRI FOG:  s %f  e %f c %f %f %f %f \n", RI.fogStart, RI.fogEnd, 
			RI.fogColor[0], RI.fogColor[1], RI.fogColor[2], RI.fogColor[3] );
			
	//glState.fogDensity = RI.fogDensity
	glState.fogColor = GUCOLOR4F( RI.fogColor[0], RI.fogColor[1], RI.fogColor[2], glState.fogDensity );
	glState.fogStart = RI.fogStart;
	glState.fogEnd = RI.fogEnd;

	sceGuFog( glState.fogStart, glState.fogEnd, glState.fogColor );
#else
	pglFogi( GL_FOG_MODE, GL_LINEAR );
	pglFogfv( GL_FOG_COLOR, RI.fogColor );
	pglFogf( GL_FOG_START, RI.fogStart );
	pglFogf( GL_FOG_END, RI.fogEnd );
	pglHint( GL_FOG_HINT, GL_NICEST );
#endif
}

/*
=============
TriGetMatrix

very strange export
=============
*/
void TriGetMatrix( const int pname, float *matrix )
{
#if 1
	printf("%s:%i:%s - Not implemented\n", __FILE__, __LINE__, __FUNCTION__ );
#else
	pglGetFloatv( pname, matrix );
#endif
}

/*
=============
TriForParams

=============
*/
void TriFogParams( float flDensity, int iFogSkybox )
{
	RI.fogDensity = flDensity;
	RI.fogSkybox = iFogSkybox;
}

/*
=============
TriCullFace

=============
*/
void TriCullFace( TRICULLSTYLE mode )
{
	int glMode;

	switch( mode )
	{
	case TRI_FRONT:
		glMode = GL_FRONT;
		break;
	default:
		glMode = GL_NONE;
		break;
	}

	GL_Cull( mode );
}

/*
=============
TriBrightness
=============
*/
void TriBrightness( float brightness )
{
	float	r, g, b;

	r = ds.triRGBA[0] * ds.triRGBA[3] * brightness;
	g = ds.triRGBA[1] * ds.triRGBA[3] * brightness;
	b = ds.triRGBA[2] * ds.triRGBA[3] * brightness;

	_TriColor4f( r, g, b, 1.0f );
}

unsigned int getTriBrightness( float brightness )
{
	vec3_t color;

	if(brightness < 0.0f) return 0;

	color[0] = ds.triRGBA[0] * ds.triRGBA[3] * brightness;
	color[1] = ds.triRGBA[1] * ds.triRGBA[3] * brightness;
	color[2] = ds.triRGBA[2] * ds.triRGBA[3] * brightness;

	VectorNormalizeFast( color );

	return GUCOLOR3FV( color );		
}

