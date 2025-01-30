/*
gl_warp.c - sky and water polygons
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
#include "wadfile.h"

#define SKYCLOUDS_QUALITY	12
#define MAX_CLIP_VERTS	128 // skybox clip vertices
#define TURBSCALE		( 256.0f / ( M_PI2 ))

static const int r_skyTexOrder[SKYBOX_MAX_SIDES] = { 0, 2, 1, 3, 4, 5 };

static const vec3_t skyclip[SKYBOX_MAX_SIDES] =
{
{  1,  1,  0 },
{  1, -1,  0 },
{  0, -1,  1 },
{  0,  1,  1 },
{  1,  0,  1 },
{ -1,  0,  1 }
};

// 1 = s, 2 = t, 3 = 2048
static const int st_to_vec[SKYBOX_MAX_SIDES][3] =
{
{  3, -1,  2 },
{ -3,  1,  2 },
{  1,  3,  2 },
{ -1, -3,  2 },
{ -2, -1,  3 },  // 0 degrees yaw, look straight up
{  2, -1, -3 }   // look straight down
};

// s = [0]/[2], t = [1]/[2]
static const int vec_to_st[SKYBOX_MAX_SIDES][3] =
{
{ -2,  3,  1 },
{  2,  3, -1 },
{  1,  3,  2 },
{ -1,  3, -2 },
{ -2, -1,  3 },
{ -2,  1, -3 }
};

// speed up sin calculations
static float r_turbsin[] =
{
#include "warpsin.h"
};

#define RIPPLES_CACHEWIDTH_BITS 7
#define RIPPLES_CACHEWIDTH ( 1 << RIPPLES_CACHEWIDTH_BITS )
#define RIPPLES_CACHEWIDTH_MASK (( RIPPLES_CACHEWIDTH ) - 1 )
#define RIPPLES_TEXSIZE ( RIPPLES_CACHEWIDTH * RIPPLES_CACHEWIDTH )
#define RIPPLES_TEXSIZE_MASK ( RIPPLES_TEXSIZE - 1 )

STATIC_ASSERT( RIPPLES_TEXSIZE == 0x4000, "fix the algorithm to work with custom resolution" );

static struct
{
	double time;
	double oldtime;

	short *curbuf, *oldbuf;
	short buf[2][RIPPLES_TEXSIZE];
	qboolean update;

	uint32_t texture[RIPPLES_TEXSIZE];
} g_ripple;


static void DrawSkyPolygon( int nump, vec3_t vecs )
{
	int	i, j, axis;
	float	s, t, dv, *vp;
	vec3_t	v, av;

	// decide which face it maps to
	VectorClear( v );

	for( i = 0, vp = vecs; i < nump; i++, vp += 3 )
		VectorAdd( vp, v, v );

	av[0] = fabs( v[0] );
	av[1] = fabs( v[1] );
	av[2] = fabs( v[2] );

	if( av[0] > av[1] && av[0] > av[2] )
		axis = (v[0] < 0) ? 1 : 0;
	else if( av[1] > av[2] && av[1] > av[0] )
		axis = (v[1] < 0) ? 3 : 2;
	else axis = (v[2] < 0) ? 5 : 4;

	// project new texture coords
	for( i = 0; i < nump; i++, vecs += 3 )
	{
		j = vec_to_st[axis][2];
		dv = (j > 0) ? vecs[j-1] : -vecs[-j-1];

		if( dv == 0.0f ) continue;

		j = vec_to_st[axis][0];
		s = (j < 0) ? -vecs[-j-1] / dv : vecs[j-1] / dv;

		j = vec_to_st[axis][1];
		t = (j < 0) ? -vecs[-j-1] / dv : vecs[j-1] / dv;

		if( s < RI.skyMins[0][axis] ) RI.skyMins[0][axis] = s;
		if( t < RI.skyMins[1][axis] ) RI.skyMins[1][axis] = t;
		if( s > RI.skyMaxs[0][axis] ) RI.skyMaxs[0][axis] = s;
		if( t > RI.skyMaxs[1][axis] ) RI.skyMaxs[1][axis] = t;
	}
}

/*
==============
ClipSkyPolygon
==============
*/
static void ClipSkyPolygon( int nump, vec3_t vecs, int stage )
{
	const float	*norm;
	float		*v, d, e;
	qboolean		front, back;
	float		dists[MAX_CLIP_VERTS + 1];
	int		sides[MAX_CLIP_VERTS + 1];
	vec3_t		newv[2][MAX_CLIP_VERTS + 1];
	int		newc[2];
	int		i, j;

	if( nump > MAX_CLIP_VERTS )
		gEngfuncs.Host_Error( "%s: MAX_CLIP_VERTS\n", __func__ );
loc1:
	if( stage == 6 )
	{
		// fully clipped, so draw it
		DrawSkyPolygon( nump, vecs );
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for( i = 0, v = vecs; i < nump; i++, v += 3 )
	{
		d = DotProduct( v, norm );
		if( d > ON_EPSILON )
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if( d < -ON_EPSILON )
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
		{
			sides[i] = SIDE_ON;
		}
		dists[i] = d;
	}

	if( !front || !back )
	{
		// not clipped
		stage++;
		goto loc1;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy( vecs, ( vecs + ( i * 3 )));
	newc[0] = newc[1] = 0;

	for( i = 0, v = vecs; i < nump; i++, v += 3 )
	{
		switch( sides[i] )
		{
		case SIDE_FRONT:
			VectorCopy( v, newv[0][newc[0]] );
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy( v, newv[1][newc[1]] );
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy( v, newv[0][newc[0]] );
			newc[0]++;
			VectorCopy( v, newv[1][newc[1]] );
			newc[1]++;
			break;
		}

		if( sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i] )
			continue;

		d = dists[i] / ( dists[i] - dists[i+1] );
		for( j = 0; j < 3; j++ )
		{
			e = v[j] + d * ( v[j+3] - v[j] );
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon( newc[0], newv[0][0], stage + 1 );
	ClipSkyPolygon( newc[1], newv[1][0], stage + 1 );
}

static void MakeSkyVec( float s, float t, int axis )
{
	int	j, k, farclip;
	vec3_t	v, b;

	farclip = RI.farClip;

	b[0] = s * (farclip >> 1);
	b[1] = t * (farclip >> 1);
	b[2] = (farclip >> 1);

	for( j = 0; j < 3; j++ )
	{
		k = st_to_vec[axis][j];
		v[j] = (k < 0) ? -b[-k-1] : b[k-1];
		v[j] += RI.cullorigin[j];
	}

	// avoid bilerp seam
	s = (s + 1.0f) * 0.5f;
	t = (t + 1.0f) * 0.5f;

	if( GL_Support( GL_CLAMPTOEDGE_EXT ))
	{
		s = bound( 0.0f, s, 1.0f );
		t = bound( 0.0f, t, 1.0f );
	}
	else
	{
		s = bound( 1.0f / 512.0f, s, 511.0f / 512.0f );
		t = bound( 1.0f / 512.0f, t, 511.0f / 512.0f );
	}

	t = 1.0f - t;

	pglTexCoord2f( s, t );
	pglVertex3fv( v );
}

/*
==============
R_ClearSkyBox
==============
*/
void R_ClearSkyBox( void )
{
	int	i;

	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		RI.skyMins[0][i] = RI.skyMins[1][i] = 9999999.0f;
		RI.skyMaxs[0][i] = RI.skyMaxs[1][i] = -9999999.0f;
	}
}

/*
=================
R_AddSkyBoxSurface
=================
*/
void R_AddSkyBoxSurface( msurface_t *fa )
{
	vec3_t	verts[MAX_CLIP_VERTS];
	glpoly2_t	*p;
	float	*v;
	int	i;

	if( FBitSet( tr.world->flags, FWORLD_SKYSPHERE ) && fa->polys && !FBitSet( tr.world->flags, FWORLD_CUSTOM_SKYBOX ))
	{
		glpoly2_t	*p = fa->polys;

		// draw the sky poly
		pglBegin( GL_POLYGON );
		for( i = 0, v = p->verts[0]; i < p->numverts; i++, v += VERTEXSIZE )
		{
			pglTexCoord2f( v[3], v[4] );
			pglVertex3fv( v );
		}
		pglEnd ();
	}

	// calculate vertex values for sky box
	for( p = fa->polys; p; p = p->next )
	{
		for( i = 0; i < p->numverts; i++ )
			VectorSubtract( p->verts[i], RI.cullorigin, verts[i] );
		ClipSkyPolygon( p->numverts, verts[0], 0 );
	}
}

/*
==============
R_UnloadSkybox

Unload previous skybox
==============
*/
void R_UnloadSkybox( void )
{
	int	i;

	// release old skybox
	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		if( !tr.skyboxTextures[i] ) continue;
		GL_FreeTexture( tr.skyboxTextures[i] );
	}

	tr.skyboxbasenum = SKYBOX_BASE_NUM;	// set skybox base (to let some mods load hi-res skyboxes)

	memset( tr.skyboxTextures, 0, sizeof( tr.skyboxTextures ));
	ClearBits( tr.world->flags, FWORLD_CUSTOM_SKYBOX );
}

/*
==============
R_DrawSkybox
==============
*/
void R_DrawSkyBox( void )
{
	int	i;

	RI.isSkyVisible = true;

	// don't fogging skybox (this fix old Half-Life bug)
	if( !RI.fogSkybox ) R_AllowFog( false );

	if( RI.fogEnabled )
		pglFogf( GL_FOG_DENSITY, RI.fogDensity * 0.5f );

	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );

	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		if( RI.skyMins[0][i] >= RI.skyMaxs[0][i] || RI.skyMins[1][i] >= RI.skyMaxs[1][i] )
			continue;

		if( tr.skyboxTextures[r_skyTexOrder[i]] )
			GL_Bind( XASH_TEXTURE0, tr.skyboxTextures[r_skyTexOrder[i]] );
		else GL_Bind( XASH_TEXTURE0, tr.grayTexture ); // stub

		pglBegin( GL_QUADS );
		MakeSkyVec( RI.skyMins[0][i], RI.skyMins[1][i], i );
		MakeSkyVec( RI.skyMins[0][i], RI.skyMaxs[1][i], i );
		MakeSkyVec( RI.skyMaxs[0][i], RI.skyMaxs[1][i], i );
		MakeSkyVec( RI.skyMaxs[0][i], RI.skyMins[1][i], i );
		pglEnd();
	}

	if( !RI.fogSkybox )
		R_AllowFog( true );

	if( RI.fogEnabled )
		pglFogf( GL_FOG_DENSITY, RI.fogDensity );

	R_LoadIdentity();
}

//==============================================================================
//
//  RENDER CLOUDS
//
//==============================================================================
/*
==============
R_CloudVertex
==============
*/
static void R_CloudVertex( float s, float t, int axis, vec3_t v )
{
	int	j, k, farclip;
	vec3_t	b;

	farclip = RI.farClip;

	b[0] = s * (farclip >> 1);
	b[1] = t * (farclip >> 1);
	b[2] = (farclip >> 1);

	for( j = 0; j < 3; j++ )
	{
		k = st_to_vec[axis][j];
		v[j] = (k < 0) ? -b[-k-1] : b[k-1];
		v[j] += RI.cullorigin[j];
	}
}

/*
=============
R_CloudTexCoord
=============
*/
static void R_CloudTexCoord( const vec3_t v, float speed, float *s, float *t )
{
	float	length, speedscale;
	vec3_t	dir;

	speedscale = gp_cl->time * speed;
	speedscale -= (int)speedscale & ~127;

	VectorSubtract( v, RI.vieworg, dir );
	dir[2] *= 3.0f; // flatten the sphere

	length = VectorLength( dir );
	length = 6.0f * 63.0f / length;

	*s = ( speedscale + dir[0] * length ) * (1.0f / 128.0f);
	*t = ( speedscale + dir[1] * length ) * (1.0f / 128.0f);
}

/*
===============
R_CloudDrawPoly
===============
*/
static void R_CloudDrawPoly( const float *verts )
{
	const float	*v;
	float	s, t;
	int		i;

	GL_SetRenderMode( kRenderNormal );
	GL_Bind( XASH_TEXTURE0, tr.solidskyTexture );

	pglBegin( GL_QUADS );
	for( i = 0, v = verts; i < 4; i++, v += VERTEXSIZE )
	{
		R_CloudTexCoord( v, 8.0f, &s, &t );
		pglTexCoord2f( s, t );
		pglVertex3fv( v );
	}
	pglEnd();

	GL_SetRenderMode( kRenderTransTexture );
	GL_Bind( XASH_TEXTURE0, tr.alphaskyTexture );

	pglBegin( GL_QUADS );
	for( i = 0, v = verts; i < 4; i++, v += VERTEXSIZE )
	{
		R_CloudTexCoord( v, 16.0f, &s, &t );
		pglTexCoord2f( s, t );
		pglVertex3fv( v );
	}
	pglEnd();

	pglDisable( GL_BLEND );
}

/*
==============
R_CloudRenderSide
==============
*/
static void R_CloudRenderSide( int axis )
{
	vec3_t	verts[4];
	float	final_verts[4][VERTEXSIZE];
	float	di, qi, dj, qj;
	vec3_t	vup, vright;
	vec3_t	temp, temp2;
	int	i, j;

	R_CloudVertex( -1.0f, -1.0f, axis, verts[0] );
	R_CloudVertex( -1.0f,  1.0f, axis, verts[1] );
	R_CloudVertex(  1.0f,  1.0f, axis, verts[2] );
	R_CloudVertex(  1.0f, -1.0f, axis, verts[3] );

	VectorSubtract( verts[2], verts[3], vup );
	VectorSubtract( verts[2], verts[1], vright );

	di = SKYCLOUDS_QUALITY;
	qi = 1.0f / di;
	dj = (axis < 4) ? di * 2 : di; //subdivide vertically more than horizontally on skybox sides
	qj = 1.0f / dj;

	for( i = 0; i < di; i++ )
	{
		for( j = 0; j < dj; j++ )
		{
			if( i * qi < RI.skyMins[0][axis] / 2 + 0.5f - qi
			 || i * qi > RI.skyMaxs[0][axis] / 2 + 0.5f
			 || j * qj < RI.skyMins[1][axis] / 2 + 0.5f - qj
			 || j * qj > RI.skyMaxs[1][axis] / 2 + 0.5f )
				continue;

			VectorScale( vright, qi * i, temp );
			VectorScale( vup, qj * j, temp2 );
			VectorAdd( temp, temp2, temp );
			VectorAdd( verts[0], temp, final_verts[0] );

			VectorScale( vup, qj, temp );
			VectorAdd( final_verts[0], temp, final_verts[1] );

			VectorScale( vright, qi, temp );
			VectorAdd( final_verts[1], temp, final_verts[2] );

			VectorAdd( final_verts[0], temp, final_verts[3] );

			R_CloudDrawPoly( final_verts[0] );
		}
	}
}

/*
==============
R_DrawClouds

Quake-style clouds
==============
*/
void R_DrawClouds( void )
{
	int	i;

	RI.isSkyVisible = true;

	if( RI.fogEnabled )
		pglFogf( GL_FOG_DENSITY, RI.fogDensity * 0.25f );
	pglDepthFunc( GL_GEQUAL );
	pglDepthMask( GL_FALSE );

	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		if( RI.skyMins[0][i] >= RI.skyMaxs[0][i] || RI.skyMins[1][i] >= RI.skyMaxs[1][i] )
			continue;
		R_CloudRenderSide( i );
	}

	pglDepthFunc( GL_LEQUAL );
	pglDepthMask( GL_TRUE );

	if( RI.fogEnabled )
		pglFogf( GL_FOG_DENSITY, RI.fogDensity );
}

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void EmitWaterPolys( msurface_t *warp, qboolean reverse, qboolean ripples )
{
	float	*v, nv, waveHeight;
	float	s, t, os, ot;
	glpoly2_t	*p;
	int	i;

	const qboolean useQuads = FBitSet( warp->flags, SURF_DRAWTURB_QUADS ) && glConfig.context == CONTEXT_TYPE_GL;

	if( !warp->polys ) return;

	// set the current waveheight
	if( warp->polys->verts[0][2] >= RI.vieworg[2] )
		waveHeight = -RI.currententity->curstate.scale;
	else waveHeight = RI.currententity->curstate.scale;

	// reset fog color for nonlightmapped water
	GL_ResetFogColor();

	if( useQuads )
		pglBegin( GL_QUADS );

	for( p = warp->polys; p; p = p->next )
	{
		if( reverse )
			v = p->verts[0] + ( p->numverts - 1 ) * VERTEXSIZE;
		else v = p->verts[0];

		if( !useQuads )
			pglBegin( GL_POLYGON );

		for( i = 0; i < p->numverts; i++ )
		{
			if( waveHeight )
			{
				nv = r_turbsin[(int)(gp_cl->time * 160.0f + v[1] + v[0]) & 255] + 8.0f;
				nv = (r_turbsin[(int)(v[0] * 5.0f + gp_cl->time * 171.0f - v[1]) & 255] + 8.0f ) * 0.8f + nv;
				nv = nv * waveHeight + v[2];
			}
			else nv = v[2];

			os = v[3];
			ot = v[4];

			if( !ripples )
			{
				s = os + r_turbsin[(int)((ot * 0.125f + gp_cl->time) * TURBSCALE) & 255];
				t = ot + r_turbsin[(int)((os * 0.125f + gp_cl->time) * TURBSCALE) & 255];
			}
			else
			{
				s = os;
				t = ot;
			}

			s *= ( 1.0f / SUBDIVIDE_SIZE );
			t *= ( 1.0f / SUBDIVIDE_SIZE );

			pglTexCoord2f( s, t );
			pglVertex3f( v[0], v[1], nv );

			if( reverse )
				v -= VERTEXSIZE;
			else v += VERTEXSIZE;
		}

		if( !useQuads )
			pglEnd();
	}

	if( useQuads )
		pglEnd();

	GL_SetupFogColorForSurfaces();
}

/*
============================================================

	HALF-LIFE SOFTWARE WATER

============================================================
*/
void R_ResetRipples( void )
{
	g_ripple.curbuf = g_ripple.buf[0];
	g_ripple.oldbuf = g_ripple.buf[1];
	g_ripple.time = g_ripple.oldtime = gp_cl->time - 0.1;
	memset( g_ripple.buf, 0, sizeof( g_ripple.buf ));
}

static void R_SwapBufs( void )
{
	short *tempbufp = g_ripple.curbuf;
	g_ripple.curbuf = g_ripple.oldbuf;
	g_ripple.oldbuf = tempbufp;
}

static void R_SpawnNewRipple( int x, int y, short val )
{
#define PIXEL( x, y ) ((( x ) & RIPPLES_CACHEWIDTH_MASK ) + ((( y ) & RIPPLES_CACHEWIDTH_MASK) << 7 ))
	g_ripple.oldbuf[PIXEL( x, y )] += val;

	val >>= 2;
	g_ripple.oldbuf[PIXEL( x + 1, y )] += val;
	g_ripple.oldbuf[PIXEL( x - 1, y )] += val;
	g_ripple.oldbuf[PIXEL( x, y + 1 )] += val;
	g_ripple.oldbuf[PIXEL( x, y - 1 )] += val;
#undef PIXEL
}

static void R_RunRipplesAnimation( const short *oldbuf, short *pbuf )
{
	size_t i = 0;
	const int w = RIPPLES_CACHEWIDTH;
	const int m = RIPPLES_TEXSIZE_MASK;

	for( i = w; i < m + w; i++, pbuf++ )
	{
		*pbuf = (
			( (int)oldbuf[( i - ( w * 2 )) & m]
			+ (int)oldbuf[( i - ( w + 1 )) & m]
			+ (int)oldbuf[( i - ( w - 1 )) & m]
			+ (int)oldbuf[( i ) & m]) >> 1 ) - (int)*pbuf;

		*pbuf -= ( *pbuf >> 6 );
	}
}

void R_AnimateRipples( void )
{
	double frametime = gp_cl->time - g_ripple.time;

	g_ripple.update = r_ripple.value && frametime >= r_ripple_updatetime.value;

	if( !g_ripple.update )
		return;

	g_ripple.time = gp_cl->time;

	R_SwapBufs();

	if( g_ripple.time - g_ripple.oldtime > r_ripple_spawntime.value )
	{
		int x, y, val;

		g_ripple.oldtime = g_ripple.time;

		x = rand() & 0x7fff;
		y = rand() & 0x7fff;
		val = rand() & 0x3ff;

		R_SpawnNewRipple( x, y, val );
	}

	R_RunRipplesAnimation( g_ripple.oldbuf, g_ripple.curbuf );
}

static void R_GetRippleTextureSize( const texture_t *image, int *width, int *height )
{
	// try to preserve aspect ratio
	if( image->width > image->height )
	{
		*width = RIPPLES_CACHEWIDTH;
		*height = (float)image->height / image->width * RIPPLES_CACHEWIDTH;
	}
	else if( image->width < image->height )
	{
		*width = (float)image->width / image->height * RIPPLES_CACHEWIDTH;
		*height = RIPPLES_CACHEWIDTH;
	}
	else
	{
		*width = *height = RIPPLES_CACHEWIDTH;
	}
}

qboolean R_UploadRipples( texture_t *image )
{
	const gl_texture_t *glt;
	const uint32_t *pixels;
	int y;
	int width, height, size;
	qboolean update = g_ripple.update;

	if( !r_ripple.value )
	{
		GL_Bind( XASH_TEXTURE0, image->gl_texturenum );
		return false;
	}

	// discard unuseful textures
	glt = R_GetTexture( image->gl_texturenum );
	if( !glt || !glt->original || !glt->original->buffer )
	{
		GL_Bind( XASH_TEXTURE0, image->gl_texturenum );
		return false;
	}

	if( !image->fb_texturenum )
	{
		rgbdata_t pic = { 0 };
		string name;

		Q_snprintf( name, sizeof( name ), "*rippletex_%s", image->name );
		R_GetRippleTextureSize( image, &width, &height );

		pic.width = width;
		pic.height = height;
		pic.depth = 1;
		pic.flags = IMAGE_HAS_COLOR;
		pic.buffer = (byte *)g_ripple.texture;
		pic.type = PF_RGBA_32;
		pic.size = width * height * 4;
		pic.numMips = 1;
		memset( pic.buffer, 0, pic.size );

		image->fb_texturenum = GL_LoadTextureInternal( name, &pic, TF_NOMIPMAP | TF_ALLOW_NEAREST );

		update = true;
		image->dt_texturenum = ( tr.framecount - 1 ) & 0xFFFF;
	}

	GL_Bind( XASH_TEXTURE0, image->fb_texturenum );

	// no updates this frame
	if( !update || image->dt_texturenum == ( tr.framecount & 0xFFFF ))
		return true;

	// prevent rendering texture multiple times in frame
	image->dt_texturenum = tr.framecount & 0xFFFF;

	R_GetRippleTextureSize( image, &width, &height );

	size = r_ripple.value == 1.0f ? 64 : RIPPLES_CACHEWIDTH;
	pixels = (const uint32_t *)glt->original->buffer;

	for( y = 0; y < height; y++ )
	{
		int ry = (float)y / height * size;
		int x;

		for( x = 0; x < width; x++ )
		{
			int rx = (float)x / width * size;
			int val = g_ripple.curbuf[ry * RIPPLES_CACHEWIDTH + rx] / 16;

			// transform it to texture space and get nice tiling effect
			int rpy = ( y - val ) % height;
			int rpx = ( x + val ) % width;

			int py = (float)rpy / height * image->height;
			int px = (float)rpx / width * image->width;

			if( py < 0 ) py = image->height + py;
			if( px < 0 ) px = image->width + px;

			g_ripple.texture[y * width + x] = pixels[py * image->width + px];
		}
	}

	pglTexImage2D( GL_TEXTURE_2D, 0, glt->format, width, height, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, g_ripple.texture );

	return true;
}
