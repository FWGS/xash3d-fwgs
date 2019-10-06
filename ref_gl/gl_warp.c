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
const char*		r_skyBoxSuffix[6] = { "rt", "bk", "lf", "ft", "up", "dn" };
static const int		r_skyTexOrder[6] = { 0, 2, 1, 3, 4, 5 };

static const vec3_t skyclip[6] =
{
{  1,  1,  0 },
{  1, -1,  0 },
{  0, -1,  1 },
{  0,  1,  1 },
{  1,  0,  1 },
{ -1,  0,  1 }
};

// 1 = s, 2 = t, 3 = 2048
static const int st_to_vec[6][3] =
{
{  3, -1,  2 },
{ -3,  1,  2 },
{  1,  3,  2 },
{ -1, -3,  2 },
{ -2, -1,  3 },  // 0 degrees yaw, look straight up
{  2, -1, -3 }   // look straight down
};

// s = [0]/[2], t = [1]/[2]
static const int vec_to_st[6][3] =
{
{ -2,  3,  1 },
{  2,  3, -1 },
{  1,  3,  2 },
{ -1,  3, -2 },
{ -2, -1,  3 },
{ -2,  1, -3 }
};

// speed up sin calculations
float r_turbsin[] =
{
	#include "warpsin.h"
};

#define SKYBOX_MISSED	0
#define SKYBOX_HLSTYLE	1
#define SKYBOX_Q1STYLE	2

static int CheckSkybox( const char *name )
{
	const char	*skybox_ext[3] = { "dds", "tga", "bmp" };
	int		i, j, num_checked_sides;
	const char	*sidename;

	// search for skybox images				
	for( i = 0; i < 3; i++ )
	{	
		num_checked_sides = 0;
		for( j = 0; j < 6; j++ )
		{
			// build side name
			sidename = va( "%s%s.%s", name, r_skyBoxSuffix[j], skybox_ext[i] );
			if( gEngfuncs.FS_FileExists( sidename, false ))
				num_checked_sides++;

		}

		if( num_checked_sides == 6 )
			return SKYBOX_HLSTYLE; // image exists

		for( j = 0; j < 6; j++ )
		{
			// build side name
			sidename = va( "%s_%s.%s", name, r_skyBoxSuffix[j], skybox_ext[i] );
			if( gEngfuncs.FS_FileExists( sidename, false ))
				num_checked_sides++;
		}

		if( num_checked_sides == 6 )
			return SKYBOX_Q1STYLE; // images exists
	}

	return SKYBOX_MISSED;
}

void DrawSkyPolygon( int nump, vec3_t vecs )
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
void ClipSkyPolygon( int nump, vec3_t vecs, int stage )
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
		gEngfuncs.Host_Error( "ClipSkyPolygon: MAX_CLIP_VERTS\n" );
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

void MakeSkyVec( float s, float t, int axis )
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

	if( s < 1.0f / 512.0f )
		s = 1.0f / 512.0f;
	else if( s > 511.0f / 512.0f )
		s = 511.0f / 512.0f;
	if( t < 1.0f / 512.0f )
		t = 1.0f / 512.0f;
	else if( t > 511.0f / 512.0f )
		t = 511.0f / 512.0f;

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

	for( i = 0; i < 6; i++ )
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
	glpoly_t	*p;
	float	*v;
	int	i;

	if( ENGINE_GET_PARM( PARM_SKY_SPHERE ) && fa->polys && !tr.fCustomSkybox )
	{
		glpoly_t	*p = fa->polys;

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
	for( i = 0; i < 6; i++ )
	{
		if( !tr.skyboxTextures[i] ) continue;
		GL_FreeTexture( tr.skyboxTextures[i] );
	}

	tr.skyboxbasenum = 5800;	// set skybox base (to let some mods load hi-res skyboxes)

	memset( tr.skyboxTextures, 0, sizeof( tr.skyboxTextures ));
	tr.fCustomSkybox = false;
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

	for( i = 0; i < 6; i++ )
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

/*
===============
R_SetupSky
===============
*/
void R_SetupSky( const char *skyboxname )
{
	char	loadname[MAX_STRING];
	char	sidename[MAX_STRING];
	int	i, result;

	if( !COM_CheckString( skyboxname ))
	{
		R_UnloadSkybox();
		return; // clear old skybox
	}

	Q_snprintf( loadname, sizeof( loadname ), "gfx/env/%s", skyboxname );
	COM_StripExtension( loadname );

	// kill the underline suffix to find them manually later
	if( loadname[Q_strlen( loadname ) - 1] == '_' )
		loadname[Q_strlen( loadname ) - 1] = '\0';
	result = CheckSkybox( loadname );

	// to prevent infinite recursion if default skybox was missed
	if( result == SKYBOX_MISSED && Q_stricmp( loadname, DEFAULT_SKYBOX_PATH ))
	{
		gEngfuncs.Con_Reportf( S_WARN "missed or incomplete skybox '%s'\n", skyboxname );
		R_SetupSky( "desert" ); // force to default
		return; 
	}

	// release old skybox
	R_UnloadSkybox();
	gEngfuncs.Con_DPrintf( "SKY:  " );

	for( i = 0; i < 6; i++ )
	{
		if( result == SKYBOX_HLSTYLE )
			Q_snprintf( sidename, sizeof( sidename ), "%s%s", loadname, r_skyBoxSuffix[i] );
		else Q_snprintf( sidename, sizeof( sidename ), "%s_%s", loadname, r_skyBoxSuffix[i] );

		tr.skyboxTextures[i] = GL_LoadTexture( sidename, NULL, 0, TF_CLAMP|TF_SKY );
		if( !tr.skyboxTextures[i] ) break;
		gEngfuncs.Con_DPrintf( "%s%s%s", skyboxname, r_skyBoxSuffix[i], i != 5 ? ", " : ". " );
	}

	if( i == 6 )
	{
		tr.fCustomSkybox = true;
		gEngfuncs.Con_DPrintf( "done\n" );
		return; // loaded
	}

	gEngfuncs.Con_DPrintf( "^2failed\n" );
	R_UnloadSkybox();
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
void R_CloudVertex( float s, float t, int axis, vec3_t v )
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
void R_CloudTexCoord( vec3_t v, float speed, float *s, float *t )
{
	float	length, speedscale;
	vec3_t	dir;

	speedscale = gpGlobals->time * speed;
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
void R_CloudDrawPoly( glpoly_t *p )
{
	float	s, t;
	float	*v;
	int		i;

	GL_SetRenderMode( kRenderNormal );
	GL_Bind( XASH_TEXTURE0, tr.solidskyTexture );

	pglBegin( GL_QUADS );
	for( i = 0, v = p->verts[0]; i < 4; i++, v += VERTEXSIZE )
	{
		R_CloudTexCoord( v, 8.0f, &s, &t );
		pglTexCoord2f( s, t );
		pglVertex3fv( v );
	}
	pglEnd();

	GL_SetRenderMode( kRenderTransTexture );
	GL_Bind( XASH_TEXTURE0, tr.alphaskyTexture );

	pglBegin( GL_QUADS );
	for( i = 0, v = p->verts[0]; i < 4; i++, v += VERTEXSIZE )
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
void R_CloudRenderSide( int axis )
{
	vec3_t	verts[4];
	float	di, qi, dj, qj;
	vec3_t	vup, vright;
	vec3_t	temp, temp2;
	glpoly_t	p[1];
	int	i, j;

	R_CloudVertex( -1.0f, -1.0f, axis, verts[0] );
	R_CloudVertex( -1.0f,  1.0f, axis, verts[1] );
	R_CloudVertex(  1.0f,  1.0f, axis, verts[2] );
	R_CloudVertex(  1.0f, -1.0f, axis, verts[3] );

	VectorSubtract( verts[2], verts[3], vup );
	VectorSubtract( verts[2], verts[1], vright );

	p->numverts = 4;
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
			VectorAdd( verts[0], temp, p->verts[0] );

			VectorScale( vup, qj, temp );
			VectorAdd( p->verts[0], temp, p->verts[1] );

			VectorScale( vright, qi, temp );
			VectorAdd( p->verts[1], temp, p->verts[2] );

			VectorAdd( p->verts[0], temp, p->verts[3] );

			R_CloudDrawPoly( p );
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

	for( i = 0; i < 6; i++ )
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
R_InitSkyClouds

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSkyClouds( mip_t *mt, texture_t *tx, qboolean custom_palette )
{
	rgbdata_t	r_temp, *r_sky;
	uint	*trans, *rgba;
	uint	transpix;
	int	r, g, b;
	int	i, j, p;
	char	texname[32];

	if( !glw_state.initialized )
		return;

	Q_snprintf( texname, sizeof( texname ), "%s%s.mip", ( mt->offsets[0] > 0 ) ? "#" : "", tx->name );

	if( mt->offsets[0] > 0 )
	{
		int	size = (int)sizeof( mip_t ) + ((mt->width * mt->height * 85)>>6);

		if( custom_palette ) size += sizeof( short ) + 768;
		r_sky = gEngfuncs.FS_LoadImage( texname, (byte *)mt, size );
	}
	else
	{
		// okay, loading it from wad
		r_sky = gEngfuncs.FS_LoadImage( texname, NULL, 0 );
	}

	// make sure what sky image is valid
	if( !r_sky || !r_sky->palette || r_sky->type != PF_INDEXED_32 || r_sky->height == 0 )
	{
		gEngfuncs.Con_Reportf( S_ERROR "R_InitSky: unable to load sky texture %s\n", tx->name );
		if( r_sky ) gEngfuncs.FS_FreeImage( r_sky );
		return;
	}

	// make an average value for the back to avoid
	// a fringe on the top level
	trans = Mem_Malloc( r_temppool, r_sky->height * r_sky->height * sizeof( *trans ));
	r = g = b = 0;

	for( i = 0; i < r_sky->width >> 1; i++ )
	{
		for( j = 0; j < r_sky->height; j++ )
		{
			p = r_sky->buffer[i * r_sky->width + j + r_sky->height];
			rgba = (uint *)r_sky->palette + p;
			trans[(i * r_sky->height) + j] = *rgba;
			r += ((byte *)rgba)[0];
			g += ((byte *)rgba)[1];
			b += ((byte *)rgba)[2];
		}
	}

	((byte *)&transpix)[0] = r / ( r_sky->height * r_sky->height );
	((byte *)&transpix)[1] = g / ( r_sky->height * r_sky->height );
	((byte *)&transpix)[2] = b / ( r_sky->height * r_sky->height );
	((byte *)&transpix)[3] = 0;

	// build a temporary image
	r_temp = *r_sky;
	r_temp.width = r_sky->width >> 1;
	r_temp.height = r_sky->height;
	r_temp.type = PF_RGBA_32;
	r_temp.flags = IMAGE_HAS_COLOR;
	r_temp.size = r_temp.width * r_temp.height * 4;
	r_temp.buffer = (byte *)trans;
	r_temp.palette = NULL;

	// load it in
	tr.solidskyTexture = GL_LoadTextureInternal( REF_SOLIDSKY_TEXTURE, &r_temp, TF_NOMIPMAP );

	for( i = 0; i < r_sky->width >> 1; i++ )
	{
		for( j = 0; j < r_sky->height; j++ )
		{
			p = r_sky->buffer[i * r_sky->width + j];

			if( p == 0 )
			{
				trans[(i * r_sky->height) + j] = transpix;
			}
			else
			{
				rgba = (uint *)r_sky->palette + p;
				trans[(i * r_sky->height) + j] = *rgba;
			}
		}
	}

	r_temp.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA;

	// load it in
	tr.alphaskyTexture = GL_LoadTextureInternal( REF_ALPHASKY_TEXTURE, &r_temp, TF_NOMIPMAP );

	// clean up
	gEngfuncs.FS_FreeImage( r_sky );
	Mem_Free( trans );
}

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void EmitWaterPolys( msurface_t *warp, qboolean reverse )
{
	float	*v, nv, waveHeight;
	float	s, t, os, ot;
	glpoly_t	*p;
	int	i;

	if( !warp->polys ) return;

	// set the current waveheight
	if( warp->polys->verts[0][2] >= RI.vieworg[2] )
		waveHeight = -RI.currententity->curstate.scale;
	else waveHeight = RI.currententity->curstate.scale;

	// reset fog color for nonlightmapped water
	GL_ResetFogColor();

	if( FBitSet( warp->flags, SURF_DRAWTURB_QUADS ))
		pglBegin( GL_QUADS );

	for( p = warp->polys; p; p = p->next )
	{
		if( reverse )
			v = p->verts[0] + ( p->numverts - 1 ) * VERTEXSIZE;
		else v = p->verts[0];

		if( !FBitSet( warp->flags, SURF_DRAWTURB_QUADS ))
			pglBegin( GL_POLYGON );

		for( i = 0; i < p->numverts; i++ )
		{
			if( waveHeight )
			{
				nv = r_turbsin[(int)(gpGlobals->time * 160.0f + v[1] + v[0]) & 255] + 8.0f;
				nv = (r_turbsin[(int)(v[0] * 5.0f + gpGlobals->time * 171.0f - v[1]) & 255] + 8.0f ) * 0.8f + nv;
				nv = nv * waveHeight + v[2];
			}
			else nv = v[2];

			os = v[3];
			ot = v[4];

			s = os + r_turbsin[(int)((ot * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			s *= ( 1.0f / SUBDIVIDE_SIZE );

			t = ot + r_turbsin[(int)((os * 0.125f + gpGlobals->time) * TURBSCALE) & 255];
			t *= ( 1.0f / SUBDIVIDE_SIZE );

			pglTexCoord2f( s, t );
			pglVertex3f( v[0], v[1], nv );

			if( reverse )
				v -= VERTEXSIZE;
			else v += VERTEXSIZE;
		}

		if( !FBitSet( warp->flags, SURF_DRAWTURB_QUADS ))
			pglEnd();
	}

	if( FBitSet( warp->flags, SURF_DRAWTURB_QUADS ))
		pglEnd();

	GL_SetupFogColorForSurfaces();
}
