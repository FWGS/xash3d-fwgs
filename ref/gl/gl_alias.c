/*
gl_alias.c - alias model renderer
Copyright (C) 2017 Uncle Mike

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
#include "const.h"
#include "r_studioint.h"
#include "triangleapi.h"
#include "alias.h"
#include "pm_local.h"
#include "pmtrace.h"

typedef struct
{
	double		time;
	double		frametime;
	int		framecount;	// alias framecount
	qboolean		interpolate;

	float		ambientlight;
	float		shadelight;
	vec3_t		lightvec;		// averaging light direction
	vec3_t		lightvec_local;	// light direction in local space
	vec3_t		lightspot;	// shadow spot
	vec3_t		lightcolor;	// averaging lightcolor
	int		oldpose;		// shadow used
	int		newpose;		// shadow used
	float		lerpfrac;		// lerp frames
} alias_draw_state_t;

static alias_draw_state_t	g_alias;		// global alias state

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/
static qboolean	m_fDoRemap;
static aliashdr_t	*m_pAliasHeader;
static dtriangle_t	g_triangles[MAXALIASTRIS];
static stvert_t	g_stverts[MAXALIASVERTS];
static int	g_used[8192];

// the command list holds counts and s/t values that are valid for
// every frame
static int	g_commands[8192];
static int	g_numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
static int	g_vertexorder[8192];
static int	g_numorder;

static int	g_stripverts[128];
static int	g_striptris[128];
static int	g_stripcount;

/*
====================
R_StudioInit

====================
*/
void R_AliasInit( void )
{
	g_alias.interpolate = true;
	m_fDoRemap = false;
}

/*
================
StripLength
================
*/
static int StripLength( int starttri, int startv )
{
	int		m1, m2, j, k;
	dtriangle_t	*last, *check;

	g_used[starttri] = 2;

	last = &g_triangles[starttri];

	g_stripverts[0] = last->vertindex[(startv+0) % 3];
	g_stripverts[1] = last->vertindex[(startv+1) % 3];
	g_stripverts[2] = last->vertindex[(startv+2) % 3];

	g_striptris[0] = starttri;
	g_stripcount = 1;

	m1 = last->vertindex[(startv+2)%3];
	m2 = last->vertindex[(startv+1)%3];
nexttri:
	// look for a matching triangle
	for( j = starttri + 1, check = &g_triangles[starttri + 1]; j < m_pAliasHeader->numtris; j++, check++ )
	{
		if( check->facesfront != last->facesfront )
			continue;

		for( k = 0; k < 3; k++ )
		{
			if( check->vertindex[k] != m1 )
				continue;
			if( check->vertindex[(k+1) % 3] != m2 )
				continue;

			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if( g_used[j] ) goto done;

			// the new edge
			if( g_stripcount & 1 )
				m2 = check->vertindex[(k+2) % 3];
			else m1 = check->vertindex[(k+2) % 3];

			g_stripverts[g_stripcount+2] = check->vertindex[(k+2) % 3];
			g_striptris[g_stripcount] = j;
			g_stripcount++;

			g_used[j] = 2;
			goto nexttri;
		}
	}
done:
	// clear the temp used flags
	for( j = starttri + 1; j < m_pAliasHeader->numtris; j++ )
	{
		if( g_used[j] == 2 )
			g_used[j] = 0;
	}

	return g_stripcount;
}

/*
===========
FanLength
===========
*/
static int FanLength( int starttri, int startv )
{
	int		m1, m2, j, k;
	dtriangle_t	*last, *check;

	g_used[starttri] = 2;

	last = &g_triangles[starttri];

	g_stripverts[0] = last->vertindex[(startv+0) % 3];
	g_stripverts[1] = last->vertindex[(startv+1) % 3];
	g_stripverts[2] = last->vertindex[(startv+2) % 3];

	g_striptris[0] = starttri;
	g_stripcount = 1;

	m1 = last->vertindex[(startv+0) % 3];
	m2 = last->vertindex[(startv+2) % 3];

nexttri:
	// look for a matching triangle
	for( j = starttri + 1, check = &g_triangles[starttri + 1]; j < m_pAliasHeader->numtris; j++, check++ )
	{
		if( check->facesfront != last->facesfront )
			continue;

		for( k = 0; k < 3; k++ )
		{
			if( check->vertindex[k] != m1 )
				continue;
			if( check->vertindex[(k+1) % 3] != m2 )
				continue;

			// this is the next part of the fan
			// if we can't use this triangle, this tristrip is done
			if( g_used[j] ) goto done;

			// the new edge
			m2 = check->vertindex[(k+2) % 3];

			g_stripverts[g_stripcount + 2] = m2;
			g_striptris[g_stripcount] = j;
			g_stripcount++;

			g_used[j] = 2;
			goto nexttri;
		}
	}
done:
	// clear the temp used flags
	for( j = starttri + 1; j < m_pAliasHeader->numtris; j++ )
	{
		if( g_used[j] == 2 )
			g_used[j] = 0;
	}

	return g_stripcount;
}

/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
static void BuildTris( void )
{
	int	len, bestlen, besttype = 0;
	int	bestverts[1024];
	int	besttris[1024];
	int	type, startv;
	int	i, j, k;
	float	s, t;

	//
	// build tristrips
	//
	memset( g_used, 0, sizeof( g_used ));
	g_numcommands = 0;
	g_numorder = 0;

	for( i = 0; i < m_pAliasHeader->numtris; i++ )
	{
		// pick an unused triangle and start the trifan
		if( g_used[i] ) continue;

		bestlen = 0;
		for( type = 0; type < 2; type++ )
		{
			for( startv = 0; startv < 3; startv++ )
			{
				if( type == 1 ) len = StripLength( i, startv );
				else len = FanLength( i, startv );

				if( len > bestlen )
				{
					besttype = type;
					bestlen = len;

					for( j = 0; j < bestlen + 2; j++ )
						bestverts[j] = g_stripverts[j];

					for( j = 0; j < bestlen; j++ )
						besttris[j] = g_striptris[j];
				}
			}
		}

		// mark the tris on the best strip as used
		for( j = 0; j < bestlen; j++ )
			g_used[besttris[j]] = 1;

		if( besttype == 1 )
			g_commands[g_numcommands++] = (bestlen + 2);
		else g_commands[g_numcommands++] = -(bestlen + 2);

		for( j = 0; j < bestlen + 2; j++ )
		{
			// emit a vertex into the reorder buffer
			k = bestverts[j];
			g_vertexorder[g_numorder++] = k;

			// emit s/t coords into the commands stream
			s = g_stverts[k].s;
			t = g_stverts[k].t;

			if( !g_triangles[besttris[0]].facesfront && g_stverts[k].onseam )
				s += m_pAliasHeader->skinwidth / 2;	// on back side
			s = (s + 0.5f) / m_pAliasHeader->skinwidth;
			t = (t + 0.5f) / m_pAliasHeader->skinheight;

			// Carmack use floats and Valve use shorts here...
			g_commands[g_numcommands++] = FloatAsInt( s );
			g_commands[g_numcommands++] = FloatAsInt( t );
		}
	}

	g_commands[g_numcommands++] = 0; // end of list marker
}

/*
================
GL_MakeAliasModelDisplayLists
================
*/
static void GL_MakeAliasModelDisplayLists( model_t *m )
{
	trivertex_t	*verts;
	int		i, j;

	BuildTris( );

	// save the data out
	m_pAliasHeader->poseverts = g_numorder;

	m_pAliasHeader->commands = Mem_Malloc( m->mempool, g_numcommands * 4 );
	memcpy( m_pAliasHeader->commands, g_commands, g_numcommands * 4 );

	m_pAliasHeader->posedata = Mem_Malloc( m->mempool, m_pAliasHeader->numposes * m_pAliasHeader->poseverts * sizeof( trivertex_t ));
	verts = m_pAliasHeader->posedata;

	for( i = 0; i < m_pAliasHeader->numposes; i++ )
	{
		for( j = 0; j < g_numorder; j++ )
			*verts++ = m_pAliasHeader->pposeverts[i][g_vertexorder[j]];
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/


/*
===============
Mod_CreateSkinData
===============
*/
static rgbdata_t *Mod_CreateSkinData( model_t *mod, const byte *data, int width, int height )
{
	static rgbdata_t	skin;
	char		name[MAX_QPATH];
	int		i;

	skin.width = width;
	skin.height = height;
	skin.depth = 1;
	skin.type = PF_INDEXED_24;
	skin.flags = IMAGE_HAS_COLOR|IMAGE_QUAKEPAL;
	skin.encode = DXT_ENCODE_DEFAULT;
	skin.numMips = 1;
	skin.buffer = (byte *)data;
	skin.palette = (byte *)tr.palette;
	skin.size = width * height;

	if( !gEngfuncs.Image_CustomPalette() )
	{
		for( i = 0; i < skin.width * skin.height; i++ )
		{
			if( data[i] > 224 && data[i] != 255 )
			{
				SetBits( skin.flags, IMAGE_HAS_LUMA );
				break;
			}
		}
	}

	COM_FileBase( mod->name, name, sizeof( name ));

	// for alias models only player can have remap textures
	if( mod != NULL && !Q_stricmp( name, "player" ))
	{
		texture_t	*tx = NULL;
		int	i, size;

		i = mod->numtextures;
		mod->textures = (texture_t **)Mem_Realloc( mod->mempool, mod->textures, ( i + 1 ) * sizeof( texture_t* ));
		size = width * height + 768;
		tx = Mem_Calloc( mod->mempool, sizeof( *tx ) + size );
		mod->textures[i] = tx;

		Q_strncpy( tx->name, "DM_Skin", sizeof( tx->name ));
		tx->anim_min = SHIRT_HUE_START; // topcolor start
		tx->anim_max = SHIRT_HUE_END; // topcolor end
		// bottomcolor start always equal is (topcolor end + 1)
		tx->anim_total = PANTS_HUE_END;// bottomcolor end

		tx->width = width;
		tx->height = height;

		// the pixels immediately follow the structures
		memcpy( (tx+1), data, width * height );
		memcpy( ((byte *)(tx+1)+(width * height)), skin.palette, 768 );
		mod->numtextures++;	// done
	}

	// make an copy
	return gEngfuncs.FS_CopyImage( &skin );
}

static const void *Mod_LoadSingleSkin( model_t *loadmodel, const daliasskintype_t *pskintype, int skinnum, int size )
{
	const byte *ptexture = (const byte *)&pskintype[1];
	rgbdata_t *pic;
	string name, lumaname, checkname;

	Q_snprintf( name, sizeof( name ), "%s:frame%i", loadmodel->name, skinnum );
	Q_snprintf( checkname, sizeof( checkname ), "%s_%i.tga", loadmodel->name, skinnum );
	if( !gEngfuncs.fsapi->FileExists( checkname, false ) || ( pic = gEngfuncs.FS_LoadImage( checkname, NULL, 0 )) == NULL )
		pic = Mod_CreateSkinData( loadmodel, ptexture, m_pAliasHeader->skinwidth, m_pAliasHeader->skinheight );

	m_pAliasHeader->gl_texturenum[skinnum][0] =
		m_pAliasHeader->gl_texturenum[skinnum][1] =
		m_pAliasHeader->gl_texturenum[skinnum][2] =
		m_pAliasHeader->gl_texturenum[skinnum][3] = GL_LoadTextureInternal( name, pic, 0 );
	gEngfuncs.FS_FreeImage( pic );

	if( FBitSet( R_GetTexture( m_pAliasHeader->gl_texturenum[skinnum][0] )->flags, TF_HAS_LUMA ))
	{
		Q_snprintf( lumaname, sizeof( lumaname ), "%s:luma%i", loadmodel->name, skinnum );

		pic = Mod_CreateSkinData( NULL,	ptexture, m_pAliasHeader->skinwidth, m_pAliasHeader->skinheight );
		m_pAliasHeader->fb_texturenum[skinnum][0] =
			m_pAliasHeader->fb_texturenum[skinnum][1] =
			m_pAliasHeader->fb_texturenum[skinnum][2] =
			m_pAliasHeader->fb_texturenum[skinnum][3] = GL_LoadTextureInternal( lumaname, pic, TF_MAKELUMA );
		gEngfuncs.FS_FreeImage( pic );
	}

	return ptexture + size;
}

static const void *Mod_LoadGroupSkin( model_t *loadmodel, const daliasskintype_t *pskintype, int skinnum, int size )
{
	const daliasskininterval_t *pinskinintervals;
	const daliasskingroup_t *pinskingroup;
	const byte *ptexture;
	rgbdata_t *pic;
	string name, lumaname;
	int i, j;

	// animating skin group.  yuck.
	pinskingroup = (const daliasskingroup_t *)&pskintype[1];
	pinskinintervals = (const daliasskininterval_t *)(&pinskingroup[1]);
	ptexture = (const byte *)&pinskinintervals[pinskingroup->numskins];

	for( i = 0; i < pinskingroup->numskins; i++ )
	{
		Q_snprintf( name, sizeof( name ), "%s_%i_%i", loadmodel->name, skinnum, i );
		pic = Mod_CreateSkinData( loadmodel, ptexture, m_pAliasHeader->skinwidth, m_pAliasHeader->skinheight );
		m_pAliasHeader->gl_texturenum[skinnum][i & 3] = GL_LoadTextureInternal( name, pic, 0 );
		gEngfuncs.FS_FreeImage( pic );

		if( FBitSet( R_GetTexture( m_pAliasHeader->gl_texturenum[skinnum][i & 3] )->flags, TF_HAS_LUMA ))
		{
			Q_snprintf( lumaname, sizeof( lumaname ), "%s_%i_%i_luma", loadmodel->name, skinnum, i );
			pic = Mod_CreateSkinData( NULL, ptexture, m_pAliasHeader->skinwidth, m_pAliasHeader->skinheight );
			m_pAliasHeader->fb_texturenum[skinnum][i & 3] = GL_LoadTextureInternal( lumaname, pic, TF_MAKELUMA );
			gEngfuncs.FS_FreeImage( pic );
		}

		ptexture += size;
	}

	for( j = i; i < 4; i++ )
	{
		m_pAliasHeader->gl_texturenum[skinnum][i & 3] = m_pAliasHeader->gl_texturenum[skinnum][i - j];
		m_pAliasHeader->fb_texturenum[skinnum][i & 3] = m_pAliasHeader->fb_texturenum[skinnum][i - j];
	}

	return ptexture;
}

/*
===============
Mod_LoadAllSkins
===============
*/
static const void *Mod_LoadAllSkins( model_t *mod, int numskins, const daliasskintype_t *pskintype )
{
	int	i, size;

	size = m_pAliasHeader->skinwidth * m_pAliasHeader->skinheight;

	// TODO: texture replacement support here
	for( i = 0; i < numskins; i++ )
	{
		if( pskintype->type == ALIAS_SKIN_SINGLE )
			pskintype = Mod_LoadSingleSkin( mod, pskintype, i, size );
		else
			pskintype = Mod_LoadGroupSkin( mod, pskintype, i, size );
	}

	return pskintype;
}

//=========================================================================

/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	const daliasskintype_t *pskintype;
	const dtriangle_t *pintriangles;
	const daliashdr_t *pinmodel;
	const stvert_t *pinstverts;

	if( loaded ) *loaded = false;
	pinmodel = (const daliashdr_t *)buffer;
	m_pAliasHeader = mod->cache.data;
	if( !m_pAliasHeader ) return;

	// load the skins
	pskintype = (const daliasskintype_t *)&pinmodel[1];
	pskintype = Mod_LoadAllSkins( mod, m_pAliasHeader->numskins, pskintype );

	// load base s and t vertices
	pinstverts = (const stvert_t *)pskintype;
	memset( g_stverts, 0, sizeof( g_stverts ));
	memcpy( g_stverts, pinstverts, sizeof( g_stverts[0] ) * m_pAliasHeader->numverts );

	// load triangle lists
	pintriangles = (const dtriangle_t *)&pinstverts[m_pAliasHeader->numverts];
	memset( g_triangles, 0, sizeof( g_triangles ));
	memcpy( g_triangles, pintriangles, sizeof( g_triangles[0] ) * m_pAliasHeader->numtris );

	// build the draw lists
	GL_MakeAliasModelDisplayLists( mod );

	m_pAliasHeader = NULL;

	if( loaded ) *loaded = true;	// done
}

void Mod_AliasUnloadTextures( void *data )
{
	aliashdr_t	*palias;
	int		i, j;

	palias = data;
	if( !palias ) return; // already freed

	for( i = 0; i < MAX_SKINS; i++ )
	{
		if( !palias->gl_texturenum[i][0] )
			break;

		for( j = 0; j < 4; j++ )
		{
			GL_FreeTexture( palias->gl_texturenum[i][j] );
			GL_FreeTexture( palias->fb_texturenum[i][j] );
		}
	}
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

/*
===============
R_AliasDynamicLight

similar to R_StudioDynamicLight
===============
*/
static void R_AliasDynamicLight( cl_entity_t *ent, alight_t *plight )
{
	movevars_t	*mv = tr.movevars;
	vec3_t		lightDir, vecSrc, vecEnd;
	vec3_t		origin, dist, finalLight;
	float		add, radius, total;
	colorVec		light;
	uint		lnum;

	if( !plight || !ent )
		return;

	if( !RI.drawWorld || r_fullbright->value || FBitSet( ent->curstate.effects, EF_FULLBRIGHT ))
	{
		plight->shadelight = 0;
		plight->ambientlight = 192;

		VectorSet( plight->plightvec, 0.0f, 0.0f, -1.0f );
		VectorSet( plight->color, 1.0f, 1.0f, 1.0f );
		return;
	}

	// determine plane to get lightvalues from: ceil or floor
	if( FBitSet( ent->curstate.effects, EF_INVLIGHT ))
		VectorSet( lightDir, 0.0f, 0.0f, 1.0f );
	else VectorSet( lightDir, 0.0f, 0.0f, -1.0f );

	VectorCopy( ent->origin, origin );

	VectorSet( vecSrc, origin[0], origin[1], origin[2] - lightDir[2] * 8.0f );
	light.r = light.g = light.b = light.a = 0;

	if(( mv->skycolor_r + mv->skycolor_g + mv->skycolor_b ) != 0 )
	{
		msurface_t	*psurf = NULL;
		pmtrace_t		trace;

		if( FBitSet( gp_host->features, ENGINE_WRITE_LARGE_COORD ))
		{
			vecEnd[0] = origin[0] - mv->skyvec_x * 65536.0f;
			vecEnd[1] = origin[1] - mv->skyvec_y * 65536.0f;
			vecEnd[2] = origin[2] - mv->skyvec_z * 65536.0f;
		}
		else
		{
			vecEnd[0] = origin[0] - mv->skyvec_x * 8192.0f;
			vecEnd[1] = origin[1] - mv->skyvec_y * 8192.0f;
			vecEnd[2] = origin[2] - mv->skyvec_z * 8192.0f;
		}

		trace = gEngfuncs.CL_TraceLine( vecSrc, vecEnd, PM_STUDIO_IGNORE );
		if( trace.ent > 0 ) psurf = gEngfuncs.EV_TraceSurface( trace.ent, vecSrc, vecEnd );
		else psurf = gEngfuncs.EV_TraceSurface( 0, vecSrc, vecEnd );

		if( psurf && FBitSet( psurf->flags, SURF_DRAWSKY ))
		{
			VectorSet( lightDir, mv->skyvec_x, mv->skyvec_y, mv->skyvec_z );

			light.r = mv->skycolor_r;
			light.g = mv->skycolor_g;
			light.b = mv->skycolor_b;
		}
	}

	if(( light.r + light.g + light.b ) == 0 )
	{
		colorVec	gcolor;
		float	grad[4];

		VectorScale( lightDir, 2048.0f, vecEnd );
		VectorAdd( vecEnd, vecSrc, vecEnd );

		light = R_LightVec( vecSrc, vecEnd, g_alias.lightspot, g_alias.lightvec );

		if( VectorIsNull( g_alias.lightvec ))
		{
			vecSrc[0] -= 16.0f;
			vecSrc[1] -= 16.0f;
			vecEnd[0] -= 16.0f;
			vecEnd[1] -= 16.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[0] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			vecSrc[0] += 32.0f;
			vecEnd[0] += 32.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[1] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			vecSrc[1] += 32.0f;
			vecEnd[1] += 32.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[2] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			vecSrc[0] -= 32.0f;
			vecEnd[0] -= 32.0f;

			gcolor = R_LightVec( vecSrc, vecEnd, NULL, NULL );
			grad[3] = ( gcolor.r + gcolor.g + gcolor.b ) / 768.0f;

			lightDir[0] = grad[0] - grad[1] - grad[2] + grad[3];
			lightDir[1] = grad[1] + grad[0] - grad[2] - grad[3];
			VectorNormalize( lightDir );
		}
		else
		{
			VectorCopy( g_alias.lightvec, lightDir );
		}
	}

	VectorSet( finalLight, light.r, light.g, light.b );
	ent->cvFloorColor = light;

	total = Q_max( Q_max( light.r, light.g ), light.b );
	if( total == 0.0f ) total = 1.0f;

	// scale lightdir by light intentsity
	VectorScale( lightDir, total, lightDir );

	for( lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		const dlight_t *dl = &tr.dlights[lnum];

		if( dl->die < g_alias.time || !r_dynamic->value )
			continue;

		VectorSubtract( origin, dl->origin, dist );

		radius = VectorLength( dist );
		add = dl->radius - radius;

		if( add > 0.0f )
		{
			total += add;

			if( radius > 1.0f )
				VectorScale( dist, ( add / radius ), dist );
			else VectorScale( dist, add, dist );

			VectorAdd( lightDir, dist, lightDir );

			finalLight[0] += dl->color.r * ( add / 256.0f );
			finalLight[1] += dl->color.g * ( add / 256.0f );
			finalLight[2] += dl->color.b * ( add / 256.0f );
		}
	}

	VectorScale( lightDir, 0.9f, lightDir );

	plight->shadelight = VectorLength( lightDir );
	plight->ambientlight = total - plight->shadelight;

	total = Q_max( Q_max( finalLight[0], finalLight[1] ), finalLight[2] );

	if( total > 0.0f )
	{
		plight->color[0] = finalLight[0] * ( 1.0f / total );
		plight->color[1] = finalLight[1] * ( 1.0f / total );
		plight->color[2] = finalLight[2] * ( 1.0f / total );
	}
	else VectorSet( plight->color, 1.0f, 1.0f, 1.0f );

	if( plight->ambientlight > 128 )
		plight->ambientlight = 128;

	if( plight->ambientlight + plight->shadelight > 255 )
		plight->shadelight = 255 - plight->ambientlight;

	VectorNormalize2( lightDir, plight->plightvec );
}

/*
===============
R_AliasSetupLighting

===============
*/
static void R_AliasSetupLighting( alight_t *plight )
{
	if( !m_pAliasHeader || !plight )
		return;

	g_alias.ambientlight = plight->ambientlight;
	g_alias.shadelight = plight->shadelight;
	VectorCopy( plight->plightvec, g_alias.lightvec );
	VectorCopy( plight->color, g_alias.lightcolor );

	// transform back to local space
	Matrix4x4_VectorIRotate( RI.objectMatrix, g_alias.lightvec, g_alias.lightvec_local );
	VectorNormalize( g_alias.lightvec_local );
}

/*
===============
R_AliasLighting

===============
*/
static void R_AliasLighting( float *lv, const vec3_t normal )
{
	float 	illum = g_alias.ambientlight;
	float	r, lightcos;

	lightcos = DotProduct( normal, g_alias.lightvec_local ); // -1 colinear, 1 opposite
	if( lightcos > 1.0f ) lightcos = 1.0f;

	illum += g_alias.shadelight;

	r = SHADE_LAMBERT;

	// do modified hemispherical lighting
	if( r <= 1.0f )
	{
		r += 1.0f;
		lightcos = (( r - 1.0f ) - lightcos) / r;
		if( lightcos > 0.0f )
			illum += g_alias.shadelight * lightcos;
	}
	else
	{
		lightcos = (lightcos + ( r - 1.0f )) / r;
		if( lightcos > 0.0f )
			illum -= g_alias.shadelight * lightcos;
	}

	illum = bound( 0.0f, illum, 255.0f );

	*lv = LightToTexGamma( illum * 4 ) / 1023.0f;
}

/*
===============
R_AliasSetRemapColors

===============
*/
static void R_AliasSetRemapColors( int newTop, int newBottom )
{
	if( gEngfuncs.CL_EntitySetRemapColors( RI.currententity, RI.currentmodel, newTop, newBottom ))
		m_fDoRemap = true;
}

/*
=============
GL_DrawAliasFrame
=============
*/
static void GL_DrawAliasFrame( aliashdr_t *paliashdr )
{
	float 		lv_tmp;
	trivertex_t	*verts0;
	trivertex_t	*verts1;
	vec3_t		vert, norm;
	int		*order;
	int		count;

	verts0 = verts1 = paliashdr->posedata;
	verts0 += g_alias.oldpose * paliashdr->poseverts;
	verts1 += g_alias.newpose * paliashdr->poseverts;
	order = paliashdr->commands;

	while( 1 )
	{
		// get the vertex count and primitive type
		count = *order++;
		if( !count ) break; // done

		if( count < 0 )
		{
			pglBegin( GL_TRIANGLE_FAN );
			count = -count;
		}
		else
		{
			pglBegin( GL_TRIANGLE_STRIP );
		}

		do
		{
			// texture coordinates come from the draw list
			if( GL_Support( GL_ARB_MULTITEXTURE ) && glState.activeTMU > 0 )
			{
				GL_MultiTexCoord2f( XASH_TEXTURE0, ((float *)order)[0], ((float *)order)[1] );
				GL_MultiTexCoord2f( XASH_TEXTURE1, ((float *)order)[0], ((float *)order)[1] );
			}
			else
			{
				pglTexCoord2f( ((float *)order)[0], ((float *)order)[1] );
			}
			order += 2;

			VectorLerp( m_bytenormals[verts0->lightnormalindex], g_alias.lerpfrac, m_bytenormals[verts1->lightnormalindex], norm );
			VectorNormalize( norm );
			R_AliasLighting( &lv_tmp, norm );
			pglColor4f( g_alias.lightcolor[0] * lv_tmp, g_alias.lightcolor[1] * lv_tmp, g_alias.lightcolor[2] * lv_tmp, tr.blend );
			VectorLerp( verts0->v, g_alias.lerpfrac, verts1->v, vert );
			pglVertex3fv( vert );
			verts0++, verts1++;
		} while( --count );

		pglEnd();
	}
}

/*
=============
GL_DrawAliasShadow
=============
*/
static void GL_DrawAliasShadow( aliashdr_t *paliashdr )
{
	trivertex_t	*verts0;
	trivertex_t	*verts1;
	float		vec_x, vec_y;
	vec3_t		av, point;
	int		*order;
	float		height;
	int		count;

	if( FBitSet( RI.currententity->curstate.effects, EF_NOSHADOW ))
		return;

	if( glState.stencilEnabled )
		pglEnable( GL_STENCIL_TEST );

	height = g_alias.lightspot[2] + 1.0f;
	vec_x = -g_alias.lightvec[0] * 8.0f;
	vec_y = -g_alias.lightvec[1] * 8.0f;

	r_stats.c_alias_polys += paliashdr->numtris;

	verts0 = verts1 = paliashdr->posedata;
	verts0 += g_alias.oldpose * paliashdr->poseverts;
	verts1 += g_alias.newpose * paliashdr->poseverts;
	order = paliashdr->commands;

	while( 1 )
	{
		// get the vertex count and primitive type
		count = *order++;
		if( !count ) break; // done

		if( count < 0 )
		{
			pglBegin( GL_TRIANGLE_FAN );
			count = -count;
		}
		else
		{
			pglBegin( GL_TRIANGLE_STRIP );
		}

		do
		{
			// texture coordinates come from the draw list
			// (skipped for shadows) pglTexCoord2fv ((float *)order);
			order += 2;

			// normals and vertexes come from the frame list
			VectorLerp( verts0->v, g_alias.lerpfrac, verts1->v, av );
			point[0] = av[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point[1] = av[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point[2] = av[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];
			Matrix3x4_VectorTransform( RI.objectMatrix, point, av );

			point[0] = av[0] - (vec_x * ( av[2] - g_alias.lightspot[2] ));
			point[1] = av[1] - (vec_y * ( av[2] - g_alias.lightspot[2] ));
			point[2] = g_alias.lightspot[2] + 1.0f;

			pglVertex3fv( point );
			verts0++, verts1++;

		} while( --count );

		pglEnd();
	}

	if( glState.stencilEnabled )
		pglDisable( GL_STENCIL_TEST );
}

/*
====================
R_AliasLerpMovement

====================
*/
static void R_AliasLerpMovement( cl_entity_t *e )
{
	float	f = 1.0f;

	// don't do it if the goalstarttime hasn't updated in a while.
	// NOTE: Because we need to interpolate multiplayer characters, the interpolation time limit
	// was increased to 1.0 s., which is 2x the max lag we are accounting for.
	if( g_alias.interpolate && ( g_alias.time < e->curstate.animtime + 1.0f ) && ( e->curstate.animtime != e->latched.prevanimtime ))
		f = ( g_alias.time - e->curstate.animtime ) / ( e->curstate.animtime - e->latched.prevanimtime );

	if( ENGINE_GET_PARM( PARM_PLAYING_DEMO ) == DEMO_QUAKE1 )
		f = f + 1.0f;

	g_alias.lerpfrac = bound( 0.0f, f, 1.0f );

	if( e->player || e->curstate.movetype != MOVETYPE_STEP )
		return; // monsters only

	// Con_Printf( "%4.2f %.2f %.2f\n", f, e->curstate.animtime, g_alias.time );
	VectorLerp( e->latched.prevorigin, f, e->curstate.origin, e->origin );

	if( !VectorCompareEpsilon( e->curstate.angles, e->latched.prevangles, ON_EPSILON ))
	{
		vec4_t	q, q1, q2;

		AngleQuaternion( e->curstate.angles, q1, false );
		AngleQuaternion( e->latched.prevangles, q2, false );
		QuaternionSlerp( q2, q1, f, q );
		QuaternionAngle( q, e->angles );
	}
	else VectorCopy( e->curstate.angles, e->angles );

	// NOTE: this completely over control about angles and don't broke interpolation
	if( FBitSet( e->model->flags, ALIAS_ROTATE ))
		e->angles[1] = anglemod( 100.0f * g_alias.time );
}

/*
=================
R_SetupAliasFrame

=================
*/
static void R_SetupAliasFrame( cl_entity_t *e, aliashdr_t *paliashdr )
{
	int	newpose, oldpose;
	int	newframe, oldframe;
	int	numposes, cycle;
	float	interval;

	oldframe = e->latched.prevframe;
	newframe = e->curstate.frame;

	if( newframe < 0 )
	{
		newframe = 0;
	}
	else if( newframe >= paliashdr->numframes )
	{
		if( newframe > paliashdr->numframes )
			gEngfuncs.Con_Reportf( S_WARN "%s: no such frame %d (%s)\n", __func__, newframe, e->model->name );
		newframe = paliashdr->numframes - 1;
	}

	if(( oldframe >= paliashdr->numframes ) || ( oldframe < 0 ))
		oldframe = newframe;

	numposes = paliashdr->frames[newframe].numposes;

	if( numposes > 1 )
	{
		oldpose = newpose = paliashdr->frames[newframe].firstpose;
		interval = 1.0f / paliashdr->frames[newframe].interval;
		cycle = (int)(g_alias.time * interval);
		oldpose += (cycle + 0) % numposes; // lerpframe from
		newpose += (cycle + 1) % numposes; // lerpframe to
		g_alias.lerpfrac = ( g_alias.time * interval );
		g_alias.lerpfrac -= (int)g_alias.lerpfrac;
	}
	else
	{
		oldpose = paliashdr->frames[oldframe].firstpose;
		newpose = paliashdr->frames[newframe].firstpose;
	}

	g_alias.oldpose = oldpose;
	g_alias.newpose = newpose;

	GL_DrawAliasFrame( paliashdr );
}

/*
===============
R_StudioDrawAbsBBox

===============
*/
static void R_AliasDrawAbsBBox( cl_entity_t *e, const vec3_t absmin, const vec3_t absmax )
{
	vec3_t	p[8];
	int	i;

	// looks ugly, skip
	if( r_drawentities->value != 5 || e == tr.viewent )
		return;

	// compute a full bounding box
	for( i = 0; i < 8; i++ )
	{
		p[i][0] = ( i & 1 ) ? absmin[0] : absmax[0];
		p[i][1] = ( i & 2 ) ? absmin[1] : absmax[1];
		p[i][2] = ( i & 4 ) ? absmin[2] : absmax[2];
	}

	GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	TriColor4f( 0.5f, 0.5f, 1.0f, 0.5f );
	TriRenderMode( kRenderTransAdd );
	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	TriBegin( TRI_QUADS );
	for( i = 0; i < 6; i++ )
	{
		TriBrightness( g_alias.shadelight / 255.0f );
		TriVertex3fv( p[boxpnt[i][0]] );
		TriVertex3fv( p[boxpnt[i][1]] );
		TriVertex3fv( p[boxpnt[i][2]] );
		TriVertex3fv( p[boxpnt[i][3]] );
	}
	TriEnd();

	TriRenderMode( kRenderNormal );
}

static void R_AliasDrawLightTrace( cl_entity_t *e )
{
	if( r_drawentities->value == 7 )
	{
		vec3_t	origin;

		pglDisable( GL_TEXTURE_2D );
		pglDisable( GL_DEPTH_TEST );

		pglBegin( GL_LINES );
		pglColor3f( 1, 0.5, 0 );
		pglVertex3fv( e->origin );
		pglVertex3fv( g_alias.lightspot );
		pglEnd();

		pglBegin( GL_LINES );
		pglColor3f( 0, 0.5, 1 );
		VectorMA( g_alias.lightspot, -64.0f, g_alias.lightvec, origin );
		pglVertex3fv( g_alias.lightspot );
		pglVertex3fv( origin );
		pglEnd();

		pglPointSize( 5.0f );
		pglColor3f( 1, 0, 0 );
		pglBegin( GL_POINTS );
		pglVertex3fv( g_alias.lightspot );
		pglEnd();
		pglPointSize( 1.0f );

		pglEnable( GL_DEPTH_TEST );
		pglEnable( GL_TEXTURE_2D );
	}
}

/*
================
R_AliasSetupTimings

init current time for a given model
================
*/
static void R_AliasSetupTimings( void )
{
	if( RI.drawWorld )
	{
		// synchronize with server time
		g_alias.time = gp_cl->time;
	}
	else
	{
		// menu stuff
		g_alias.time = gp_host->realtime;
	}

	m_fDoRemap = false;
}

/*
=================
R_DrawAliasModel

=================
*/
void R_DrawAliasModel( cl_entity_t *e )
{
	model_t		*clmodel;
	vec3_t		absmin, absmax;
	remap_info_t	*pinfo = NULL;
	int		anim, skin;
	alight_t		lighting;
	player_info_t	*playerinfo;
	vec3_t		dir, angles;

	clmodel = RI.currententity->model;

	VectorAdd( e->origin, clmodel->mins, absmin );
	VectorAdd( e->origin, clmodel->maxs, absmax );

	if( R_CullModel( e, absmin, absmax ))
		return;

	//
	// locate the proper data
	//
	m_pAliasHeader = (aliashdr_t *)gEngfuncs.Mod_Extradata( mod_alias, RI.currententity->model );
	if( !m_pAliasHeader ) return;

	// init time
	R_AliasSetupTimings();

	// angles will be modify below keep original
	VectorCopy( e->angles, angles );

	R_AliasLerpMovement( e );

	if( !FBitSet( gp_host->features, ENGINE_COMPENSATE_QUAKE_BUG ))
		e->angles[PITCH] = -e->angles[PITCH]; // stupid quake bug

	// don't rotate clients, only aim
	if( e->player ) e->angles[PITCH] = 0.0f;

	//
	// get lighting information
	//
	lighting.plightvec = dir;
	R_AliasDynamicLight( e, &lighting );

	r_stats.c_alias_polys += m_pAliasHeader->numtris;
	r_stats.c_alias_models_drawn++;

	//
	// draw all the triangles
	//

	R_RotateForEntity( e );

	// model and frame independant
	R_AliasSetupLighting( &lighting );
	GL_SetRenderMode( e->curstate.rendermode );

	// setup remapping only for players
	if( e->player && ( playerinfo = pfnPlayerInfo( e->curstate.number - 1 )) != NULL )
	{
		// get remap colors
		int topcolor = bound( 0, playerinfo->topcolor, 13 );
		int bottomcolor = bound( 0, playerinfo->bottomcolor, 13 );
		R_AliasSetRemapColors( topcolor, bottomcolor );
	}

	if( tr.fFlipViewModel )
	{
		pglTranslatef( m_pAliasHeader->scale_origin[0], -m_pAliasHeader->scale_origin[1], m_pAliasHeader->scale_origin[2] );
		pglScalef( m_pAliasHeader->scale[0], -m_pAliasHeader->scale[1], m_pAliasHeader->scale[2] );
	}
	else
	{
		pglTranslatef( m_pAliasHeader->scale_origin[0], m_pAliasHeader->scale_origin[1], m_pAliasHeader->scale_origin[2] );
		pglScalef( m_pAliasHeader->scale[0], m_pAliasHeader->scale[1], m_pAliasHeader->scale[2] );
	}

	anim = (int)(g_alias.time * 10) & 3;
	skin = bound( 0, RI.currententity->curstate.skin, m_pAliasHeader->numskins - 1 );
	if( m_fDoRemap ) pinfo = gEngfuncs.CL_GetRemapInfoForEntity( e );

	if( r_lightmap->value && !r_fullbright->value )
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	else if( pinfo != NULL && pinfo->textures[skin] != 0 )
		GL_Bind( XASH_TEXTURE0, pinfo->textures[skin] );	// FIXME: allow remapping for skingroups someday
	else
	{
		GL_Bind( XASH_TEXTURE0, m_pAliasHeader->gl_texturenum[skin][anim] );

		if( FBitSet( R_GetTexture( m_pAliasHeader->gl_texturenum[skin][anim] )->flags, TF_HAS_ALPHA ))
		{
			pglEnable( GL_ALPHA_TEST );
			pglAlphaFunc( GL_GREATER, 0.0f );
			tr.blend = 1.0f;
		}
	}

	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	if( GL_Support( GL_ARB_MULTITEXTURE ) && m_pAliasHeader->fb_texturenum[skin][anim] )
	{
		GL_Bind( XASH_TEXTURE1, m_pAliasHeader->fb_texturenum[skin][anim] );
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD );
	}

	pglShadeModel( GL_SMOOTH );
	R_SetupAliasFrame( e, m_pAliasHeader );

	if( GL_Support( GL_ARB_MULTITEXTURE ) && m_pAliasHeader->fb_texturenum[skin][anim] )
		GL_CleanUpTextureUnits( 1 );

	pglShadeModel( GL_FLAT );
	R_LoadIdentity();

	// get lerped origin
	VectorAdd( e->origin, clmodel->mins, absmin );
	VectorAdd( e->origin, clmodel->maxs, absmax );

	R_AliasDrawAbsBBox( e, absmin, absmax );
	R_AliasDrawLightTrace( e );

	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
	pglDisable( GL_ALPHA_TEST );

	if( r_shadows.value )
	{
		// need to compute transformation matrix
		Matrix4x4_CreateFromEntity( RI.objectMatrix, e->angles, e->origin, 1.0f );
		pglDisable( GL_TEXTURE_2D );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglEnable( GL_BLEND );
		pglColor4f( 0.0f, 0.0f, 0.0f, 0.5f );
		pglDepthFunc( GL_LESS );

		GL_DrawAliasShadow( m_pAliasHeader );

		pglDepthFunc( GL_LEQUAL );
		pglEnable( GL_TEXTURE_2D );
		pglDisable( GL_BLEND );
		pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		R_LoadIdentity();
	}

	// restore original angles
	VectorCopy( angles, e->angles );
}

//==================================================================================
