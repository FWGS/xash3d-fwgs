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
	int    renderMode;                      // override kRenderMode from TriAPI
	vec4_t triRGBA;
} ds;

static finalvert_t triv[3];
static int         vertcount, n;
static int         mode;
static short       s, t;
static uint        light;

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
void GAME_EXPORT TriRenderMode( int mode )
{
	ds.renderMode = vid.rendermode = mode;
}

/*
=============
TriBegin

begin triangle sequence
=============
*/
void GAME_EXPORT TriBegin( int mode1 )
{
	if( mode1 == TRI_QUADS )
		mode1 = TRI_TRIANGLE_FAN;
	mode = mode1;
	n = vertcount = 0;
}

/*
=============
TriEnd

draw triangle sequence
=============
*/
void GAME_EXPORT TriEnd( void )
{
}

/*
=============
_TriColor4f

=============
*/
void GAME_EXPORT _TriColor4f( float rr, float gg, float bb, float aa )
{
	// pglColor4f( r, g, b, a );
	unsigned short r, g, b;
	unsigned int   major, minor;

	if( vid.rendermode == kRenderTransAdd || vid.rendermode == kRenderGlow )
		rr *= aa, gg *= aa, bb *= aa;

	// gEngfuncs.Con_Printf("%d\n", vid.alpha);

	light = ( rr + gg + bb ) * 31 / 3;
	if( light > 31 )
		light = 31;

	if( !vid.is2d && vid.rendermode == kRenderNormal )
		return;

	vid.alpha = aa * 7;
	if( vid.alpha > 7 )
		vid.alpha = 7;

	if( rr == 1 && gg == 1 && bb == 1 )
	{
		vid.color = COLOR_WHITE;
		return;
	}
	r = rr * 31, g = gg * 63, b = bb * 31;
	if( r > 31 )
		r = 31;
	if( g > 63 )
		g = 63;
	if( b > 31 )
		b = 31;


	major = ((( r >> 2 ) & MASK( 3 )) << 5 ) | (((( g >> 3 ) & MASK( 3 )) << 2 )) | ((( b >> 3 ) & MASK( 2 )));

	// save minor GBRGBRGB
	minor = MOVE_BIT( r, 1, 5 ) | MOVE_BIT( r, 0, 2 ) | MOVE_BIT( g, 2, 7 ) | MOVE_BIT( g, 1, 4 ) | MOVE_BIT( g, 0, 1 ) | MOVE_BIT( b, 2, 6 ) | MOVE_BIT( b, 1, 3 ) | MOVE_BIT( b, 0, 0 );

	vid.color = major << 8 | ( minor & 0xFF );
}

/*
=============
TriColor4ub

=============
*/
void TriColor4ub( byte r, byte g, byte b, byte a )
{
	ds.triRGBA[0] = r * ( 1.0f / 255.0f );
	ds.triRGBA[1] = g * ( 1.0f / 255.0f );
	ds.triRGBA[2] = b * ( 1.0f / 255.0f );
	ds.triRGBA[3] = a * ( 1.0f / 255.0f );

	_TriColor4f( ds.triRGBA[0], ds.triRGBA[1], ds.triRGBA[2], 1.0f );
}

/*
=============
TriColor4ub

=============
*/
void GAME_EXPORT _TriColor4ub( byte r, byte g, byte b, byte a )
{
	_TriColor4f( r * ( 1.0f / 255.0f ),
		     g * ( 1.0f / 255.0f ),
		     b * ( 1.0f / 255.0f ),
		     a * ( 1.0f / 255.0f ));
}

/*
=================
TriColor4f
=================
*/
void TriColor4f( float r, float g, float b, float a )
{
	// if( a < 0.5 )
	//	a = 1;
	if( ds.renderMode == kRenderTransAlpha )
		TriColor4ub( r * 255.0f, g * 255.0f, b * 255.0f, a * 255.0f );
	else
		_TriColor4f( r * a, g * a, b * a, 1.0 );

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
void GAME_EXPORT TriTexCoord2f( float u, float v )
{
	double u1 = 0, v1 = 0;
	u = fmodf( u, 10 );
	v = fmodf( v, 10 );
	if( u < 1000 && u > -1000 )
		u1 = u;
	if( v < 1000 && v > -1000 )
		v1 = v;
	while( u1 < 0 )
		u1 = u1 + 1;
	while( v1 < 0 )
		v1 = v1 + 1;

	while( u1 > 1 )
		u1 = u1 - 1;
	while( v1 > 1 )
		v1 = v1 - 1;


	s = r_affinetridesc.skinwidth * bound( 0.01, u1, 0.99 );
	t = r_affinetridesc.skinheight * bound( 0.01, v1, 0.99 );
}

/*
=============
TriVertex3fv

=============
*/
void GAME_EXPORT TriVertex3fv( const float *v )
{
	TriVertex3f( v[0], v[1], v[2] );
}

/*
=============
TriVertex3f

=============
*/
void GAME_EXPORT TriVertex3f( float x, float y, float z )
{
	if( mode == TRI_TRIANGLES )
	{
		R_SetupFinalVert( &triv[vertcount], x, y, z, light << 8, s, t );
		vertcount++;
		if( vertcount == 3 )
		{
			R_RenderTriangle( &triv[0], &triv[1], &triv[2] );
			// R_RenderTriangle( &triv[2], &triv[1], &triv[0] );
			vertcount = 0;
		}
	}
	if( mode == TRI_TRIANGLE_FAN )
	{
		R_SetupFinalVert( &triv[vertcount], x, y, z, light << 8, s, t );
		vertcount++;
		if( vertcount >= 3 )
		{
			R_RenderTriangle( &triv[0], &triv[1], &triv[2] );
			// R_RenderTriangle( &triv[2], &triv[1], &triv[0] );
			triv[1] = triv[2];
			vertcount = 2;
		}
	}
	if( mode == TRI_TRIANGLE_STRIP )
	{
		R_SetupFinalVert( &triv[n], x, y, z, light << 8, s, t );
		n++;
		vertcount++;
		if( n == 3 )
			n = 0;
		if( vertcount >= 3 )
		{
			if( vertcount & 1 )
				R_RenderTriangle( &triv[0], &triv[1], &triv[2] );
			else
				R_RenderTriangle( &triv[2], &triv[1], &triv[0] );
		}
	}
}

/*
=============
TriWorldToScreen

convert world coordinates (x,y,z) into screen (x, y)
=============
*/
int GAME_EXPORT TriWorldToScreen( const float *world, float *screen )
{
	return R_WorldToScreen( world, screen );
}

/*
=============
TriSpriteTexture

bind current texture
=============
*/
int TriSpriteTexture( model_t *pSpriteModel, int frame )
{
	int gl_texturenum;

	if(( gl_texturenum = R_GetSpriteTexture( pSpriteModel, frame )) == 0 )
		return 0;

	if( gl_texturenum <= 0 || gl_texturenum >= MAX_TEXTURES )
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
void GAME_EXPORT TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
}

/*
=============
TriGetMatrix

very strange export
=============
*/
void GAME_EXPORT TriGetMatrix( const int pname, float *matrix )
{
}

/*
=============
TriForParams

=============
*/
void GAME_EXPORT TriFogParams( float flDensity, int iFogSkybox )
{
}

/*
=============
TriCullFace

=============
*/
void GAME_EXPORT TriCullFace( TRICULLSTYLE mode )
{
}

/*
=============
TriBrightness
=============
*/
void TriBrightness( float brightness )
{
	float r, g, b;

	r = ds.triRGBA[0] * ds.triRGBA[3] * brightness;
	g = ds.triRGBA[1] * ds.triRGBA[3] * brightness;
	b = ds.triRGBA[2] * ds.triRGBA[3] * brightness;

	_TriColor4f( r, g, b, 1.0f );
}
