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

static struct
{
	byte	*start;
	byte	*end;
	int	count;
	int	prim;
	uint	flags;
	float	lastTexCord[2];
	uint	lastColor;
}triContext = { 0 };

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
}

/*
=============
TriBegin

begin triangle sequence
=============
*/
void TriBegin( int mode )
{
	switch( mode )
	{
	case TRI_POINTS:
		triContext.prim = GU_POINTS;
		break;
	case TRI_TRIANGLES:
		triContext.prim = GU_TRIANGLES;
		break;
	case TRI_TRIANGLE_FAN:
		triContext.prim = GU_TRIANGLE_FAN;
		break;
	case TRI_QUADS:
		triContext.prim = GU_TRIANGLE_FAN;
		break;
	case TRI_LINES:
		triContext.prim = GU_LINES;
		break;
	case TRI_TRIANGLE_STRIP:
		triContext.prim = GU_TRIANGLE_STRIP;
		break;
	case TRI_QUAD_STRIP:
		triContext.prim = GU_TRIANGLE_STRIP;
		break;
	case TRI_POLYGON:
	default:
		triContext.prim = GU_TRIANGLE_FAN;
		break;
	}

	triContext.start = extGuBeginPacket( NULL );
	triContext.end   = triContext.start;
	triContext.count = 0;
	triContext.flags = 0;
}

/*
=============
TriEnd

draw triangle sequence
=============
*/
void TriEnd( void )
{
	if( !triContext.start || triContext.count == 0 )
		return;

	extGuEndPacket(( void* )triContext.end );
	sceGuDrawArray( triContext.prim, triContext.flags, triContext.count, 0, triContext.start );

	triContext.start = NULL;
	triContext.end   = NULL;
	triContext.count = 0;
}

/*
=============
_TriColor4f

=============
*/
static void _TriColor4f( float r, float g, float b, float a )
{
	if( triContext.start )
	{
		if( triContext.count > 0 && !( triContext.flags & GU_COLOR_8888 ))
			return;

		triContext.flags |= GU_COLOR_8888;
		triContext.lastColor = GUCOLOR4F( r, g, b, a );
	}
	else sceGuColor( GUCOLOR4F( r, g, b, a ));
}

/*
=============
_TriColor4ub

=============
*/
static void _TriColor4ub( byte r, byte g, byte b, byte a )
{
	if( triContext.start )
	{
		if( triContext.count > 0 && !( triContext.flags & GU_COLOR_8888 ))
			return;

		triContext.flags |= GU_COLOR_8888;
		triContext.lastColor = GUCOLOR4UB( r, g, b, a );
	}
	else sceGuColor( GUCOLOR4UB( r, g, b, a ));
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

	_TriColor4ub( r, g, b, 255 );
}

/*
=================
TriColor4f
=================
*/
void TriColor4f( float r, float g, float b, float a )
{
	if( ds.renderMode == kRenderTransAlpha )
		_TriColor4ub( r * 255.9f, g * 255.9f, b * 255.9f, a * 255.0f );
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
	if( !triContext.start )
		return;

	if( triContext.count > 0 && !( triContext.flags & GU_TEXTURE_32BITF ))
		return;

	triContext.flags |= GU_TEXTURE_32BITF;
	triContext.lastTexCord[0] = u;
	triContext.lastTexCord[1] = v;
}

/*
=============
TriVertex3fv

=============
*/
void TriVertex3fv( const float *v )
{
	if( !triContext.start )
		return;

	if( triContext.flags & GU_TEXTURE_32BITF )
	{
		*( float* )triContext.end = triContext.lastTexCord[0];
		triContext.end += sizeof( float );
		*( float* )triContext.end = triContext.lastTexCord[1];
		triContext.end += sizeof( float );
	}

	if( triContext.flags & GU_COLOR_8888 )
	{
		*( uint* )triContext.end = triContext.lastColor;
		triContext.end += sizeof( uint );
	}

	triContext.flags |= GU_VERTEX_32BITF;
	*( float* )triContext.end = v[0];
	triContext.end += sizeof( float );
	*( float* )triContext.end = v[1];
	triContext.end += sizeof( float );
	*( float* )triContext.end = v[2];
	triContext.end += sizeof( float );

	triContext.count++;
}

/*
=============
TriVertex3f

=============
*/
void TriVertex3f( float x, float y, float z )
{
	if( !triContext.start )
		return;

	if( triContext.flags & GU_TEXTURE_32BITF )
	{
		*( float* )triContext.end = triContext.lastTexCord[0];
		triContext.end += sizeof( float );
		*( float* )triContext.end = triContext.lastTexCord[1];
		triContext.end += sizeof( float );
	}

	if( triContext.flags & GU_COLOR_8888 )
	{
		*( uint* )triContext.end = triContext.lastColor;
		triContext.end += sizeof( uint );
	}

	triContext.flags |= GU_VERTEX_32BITF;
	*( float* )triContext.end = x;
	triContext.end += sizeof( float );
	*( float* )triContext.end = y;
	triContext.end += sizeof( float );
	*( float* )triContext.end = z;
	triContext.end += sizeof( float );

	triContext.count++;
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

	GL_Cull( glMode );
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

	if( brightness < 0.0f ) return 0;

	color[0] = ds.triRGBA[0] * ds.triRGBA[3] * brightness;
	color[1] = ds.triRGBA[1] * ds.triRGBA[3] * brightness;
	color[2] = ds.triRGBA[2] * ds.triRGBA[3] * brightness;

	//VectorNormalizeFast( color );

	return GUCOLOR3FV( color );
}

/*
=============
TriBoxInPVS

check box in pvs (absmin, absmax)
=============
*/
int TriBoxInPVS( float *mins, float *maxs )
{
	return gEngfuncs.Mod_BoxVisible( mins, maxs, Mod_GetCurrentVis( ));
}

/*
=============
TriLightAtPoint
NOTE: dlights are ignored
=============
*/
void TriLightAtPoint( float *pos, float *value )
{
	colorVec	vLightColor;

	if( !pos || !value ) return;

	vLightColor = R_LightPoint( pos );

	value[0] = vLightColor.r;
	value[1] = vLightColor.g;
	value[2] = vLightColor.b;
}

/*
=============
TriColor4fRendermode
Heavy legacy of Quake...
=============
*/
void TriColor4fRendermode( float r, float g, float b, float a, int rendermode )
{
	if( ds.renderMode == kRenderTransAlpha )
	{
		ds.triRGBA[3] = a / 255.0f;
		_TriColor4f( r, g, b, a );
	}
	else _TriColor4f( r * a, g * a, b * a, 1.0f );
}

/*
=============
getTriAPI
export
=============
*/
int getTriAPI( int version, triangleapi_t *api )
{
	api->version		= TRI_API_VERSION;

	if( version != TRI_API_VERSION )
		return 0;

	api->RenderMode		= TriRenderMode;
	api->Begin		= TriBegin;
	api->End		= TriEnd;
	api->Color4f		= TriColor4f;
	api->Color4ub		= TriColor4ub;
	api->TexCoord2f		= TriTexCoord2f;
	api->Vertex3fv		= TriVertex3fv;
	api->Vertex3f		= TriVertex3f;
	api->Brightness		= TriBrightness;
	api->CullFace		= TriCullFace;
	api->SpriteTexture	= TriSpriteTexture;
	api->WorldToScreen	= R_WorldToScreen;
	api->Fog		= TriFog;
	api->ScreenToWorld	= R_ScreenToWorld;
	api->GetMatrix		= TriGetMatrix;
	api->BoxInPVS		= TriBoxInPVS;
	api->LightAtPoint	= TriLightAtPoint;
	api->Color4fRendermode	= TriColor4fRendermode;
	api->FogParams		= TriFogParams;

	return 1;
}
