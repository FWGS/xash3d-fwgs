/*
gl_triapi.c - TriAPI draw methods
Copyright (C) 2011 Uncle Mike
Copyright (C) 2019 a1batross

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
#include "const.h"

static struct
{
	int		renderMode;		// override kRenderMode from TriAPI
	vec4_t		triRGBA;
} ds;

finalvert_t triv[3];
int vertcount, n;
int mode;
short s,t;

/*
===============================================================

  TRIAPI IMPLEMENTATION

===============================================================
*/
/*
=============
TriRenderMode

set rendermode
=============
*/
void TriRenderMode( int mode )
{
	ds.renderMode = vid.rendermode = mode;
#if 0
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
void TriBegin( int mode1 )
{
#if 0
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
	mode = mode1;
	vertcount = n = vertcount = 0;
}

/*
=============
TriEnd

draw triangle sequence
=============
*/
void TriEnd( void )
{
	//if( vertcount == 3 )
	//pglEnd( );
}

/*
=============
_TriColor4f

=============
*/
void _TriColor4f( float rr, float gg, float bb, float aa )
{
	//pglColor4f( r, g, b, a );
	unsigned short r,g,b;
	unsigned int major, minor;


	vid.alpha = aa * 7;
	if( vid.alpha > 7 )
		vid.alpha = 7;
	//gEngfuncs.Con_Printf("%d\n", vid.alpha);

	if( rr == 1 && gg == 1 && bb == 1 )
	{
		vid.color = COLOR_WHITE;
		return;
	}

	r = rr * 31, g = gg * 63, b = bb * 31;


	major = (((r >> 2) & MASK(3)) << 5) |( (( (g >> 3) & MASK(3)) << 2 )  )| (((b >> 3) & MASK(2)));

	// save minor GBRGBRGB
	minor = MOVE_BIT(r,1,5) | MOVE_BIT(r,0,2) | MOVE_BIT(g,2,7) | MOVE_BIT(g,1,4) | MOVE_BIT(g,0,1) | MOVE_BIT(b,2,6)| MOVE_BIT(b,1,3)|MOVE_BIT(b,0,0);

	vid.color =  major << 8 | (minor & 0xFF);
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

	_TriColor4f( ds.triRGBA[0], ds.triRGBA[1], ds.triRGBA[2], ds.triRGBA[3] ); //1.0f );
}

/*
=================
TriColor4f
=================
*/
void TriColor4f( float r, float g, float b, float a )
{
	if( ds.renderMode == kRenderTransAlpha )
		TriColor4ub( r * 255.9f, g * 255.9f, b * 255.9f, a * 255.0f );
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
	//pglTexCoord2f( u, v );
	s = r_affinetridesc.skinwidth * u;
	t = r_affinetridesc.skinheight * v;
}

/*
=============
TriVertex3fv

=============
*/
void TriVertex3fv( const float *v )
{
	//pglVertex3fv( v );
	TriVertex3f( v[0], v[1], v[2] );
}

/*
=============
TriVertex3f

=============
*/
void TriVertex3f( float x, float y, float z )
{
	if( mode == TRI_TRIANGLE_FAN )
	{
		R_SetupFinalVert( &triv[vertcount], x, y, z, 0,s,t);
		vertcount++;
		if( vertcount >= 3 )
		{
			R_RenderTriangle( &triv[0], &triv[1], &triv[2] );
			triv[1] = triv[2];
			vertcount = 2;
		}
	}
	if( mode == TRI_TRIANGLE_STRIP )
	{
		R_SetupFinalVert( &triv[n], x, y, z, 0,s,t);
		n++;
		vertcount++;
		if( n == 3 )
			n = 0;
		if (vertcount >= 3)
		{
			if( vertcount & 1 )
				R_RenderTriangle( &triv[0], &triv[1], &triv[2] );
			else
				R_RenderTriangle( &triv[2], &triv[1], &triv[0] );
		}
	}
#if 0
		if( mode == TRI_TRIANGLE_STRIP )
		{
			R_SetupFinalVert( &triv[vertcount], x, y, z, 0,s,t);
			vertcount++;
			if( vertcount == 3 )
			{

				R_RenderTriangle( triv );
				finalvert_t fv = triv[0];

				triv[0] = triv[2];
				triv[2] = fv;
				R_RenderTriangle( triv );
				fv = triv[0];
				triv[0] = triv[2];
				triv[2] = fv;
				triv[0] = triv[1];
				triv[1] = triv[2];
				vertcount = 2;
			}
		}
#endif
}

/*
=============
TriWorldToScreen

convert world coordinates (x,y,z) into screen (x, y)
=============
*/
int TriWorldToScreen( float *world, float *screen )
{
	int	retval;

//	retval = R_WorldToScreen( world, screen );

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
#if 0
	// overrided by internal fog
	if( RI.fogEnabled ) return;
	RI.fogCustom = bOn;

	// check for invalid parms
	if( flEnd <= flStart )
	{
		RI.fogCustom = false;
		pglDisable( GL_FOG );
		return;
	}

	if( RI.fogCustom )
		pglEnable( GL_FOG );
	else pglDisable( GL_FOG );

	// copy fog params
	RI.fogColor[0] = flFogColor[0] / 255.0f;
	RI.fogColor[1] = flFogColor[1] / 255.0f;
	RI.fogColor[2] = flFogColor[2] / 255.0f;
	RI.fogStart = flStart;
	RI.fogColor[3] = 1.0f;
	RI.fogDensity = 0.0f;
	RI.fogSkybox = true;
	RI.fogEnd = flEnd;

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
	//pglGetFloatv( pname, matrix );
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
#if 0
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
#endif
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

