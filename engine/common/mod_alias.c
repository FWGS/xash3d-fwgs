/*
mod_alias.c - alias model loading
Copyright (C) 2010 Uncle Mike
Copyright (C) 2024 Alibek Omarov

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
#include "alias.h"
#if !XASH_DEDICATED
#include "ref_common.h"
#endif // XASH_DEDICATED
#include "mod_local.h"
#include "xash3d_mathlib.h"

static int g_posenum = 0;
static const trivertex_t *g_poseverts[MAXALIASFRAMES];

static qboolean Mod_AliasFits( const void *p, const byte *end, uint64_t count, uint64_t size )
{
	const uint64_t remaining = end - (const byte *)p;

	return size == 0 || count <= remaining / size;
}

static const void *Mod_LoadAliasFrame( const daliasframe_t *pdaliasframe, const byte *end, maliasframedesc_t *frame, const aliashdr_t *aliashdr )
{
	if( !Mod_AliasFits( pdaliasframe, end, 1, sizeof( *pdaliasframe ) + aliashdr->numverts * sizeof( trivertex_t )))
		return NULL;

	Q_strncpy( frame->name, pdaliasframe->name, sizeof( frame->name ));
	frame->firstpose = g_posenum;
	frame->numposes = 1;

	for( int i = 0; i < 3; i++ )
	{
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	const trivertex_t *pinframe = (const trivertex_t *)(pdaliasframe + 1);

	g_poseverts[g_posenum] = pinframe;
	g_posenum++;

	pinframe += aliashdr->numverts;

	return (void *)pinframe;
}

static const void *Mod_LoadAliasGroup( const daliasgroup_t *pingroup, const byte *end, maliasframedesc_t *frame, const aliashdr_t *aliashdr )
{
	if( !Mod_AliasFits( pingroup, end, 1, sizeof( *pingroup )))
		return NULL;

	daliasgroup_t group;
	memcpy( &group, pingroup, sizeof( group ));

	int numframes = group.numframes;

	if( numframes < 1 )
		return NULL;

	if( !Mod_AliasFits( pingroup + 1, end, numframes, sizeof( daliasinterval_t ) + sizeof( daliasframe_t ) + aliashdr->numverts * sizeof( trivertex_t )))
		return NULL;

	frame->firstpose = g_posenum;
	frame->numposes = numframes;

	for( int i = 0; i < 3; i++ )
	{
		frame->bboxmin.v[i] = group.bboxmin.v[i];
		frame->bboxmax.v[i] = group.bboxmax.v[i];
	}

	const daliasinterval_t *pin_intervals = (const daliasinterval_t *)(pingroup + 1);

	// all the intervals are always equal 0.1 so we don't care about them
	daliasinterval_t interval;
	memcpy( &interval, pin_intervals, sizeof( interval ));
	frame->interval = interval.interval;
	pin_intervals += numframes;
	const void *ptemp = (void *)pin_intervals;

	for( int i = 0; i < numframes; i++ )
	{
		g_poseverts[g_posenum] = (const trivertex_t *)((const daliasframe_t *)ptemp + 1);
		ptemp = g_poseverts[g_posenum] + aliashdr->numverts;
		g_posenum++;
	}

	return ptemp;
}

static void Mod_CalcAliasBounds( model_t *mod, const aliashdr_t *aliashdr )
{
	ClearBounds( mod->mins, mod->maxs );
	float	radius = 0.0f;

	// process verts
	for( int i = 0; i < aliashdr->numposes; i++ )
	{
		for( int j = 0; j < aliashdr->numverts; j++ )
		{
			vec3_t	v;
			for( int k = 0; k < 3; k++ )
				v[k] = g_poseverts[i][j].v[k] * aliashdr->scale[k] + aliashdr->scale_origin[k];

			AddPointToBounds( v, mod->mins, mod->maxs );
			float	dist = DotProduct( v, v );

			if( radius < dist )
				radius = dist;
		}
	}

	mod->radius = sqrt( radius );
}

static const void *Mod_LoadAllSkins( model_t *mod, int numskins, const daliasskintype_t *pskintype, const byte *end, const aliashdr_t *aliashdr )
{
	if(( numskins < 1 ) || ( numskins > MAX_SKINS ))
		Host_Error( "%s: Invalid # of skins: %d\n", __func__, numskins );

	if( aliashdr->skinwidth <= 0 || aliashdr->skinheight <= 0 )
		return NULL;

	const uint64_t size = (uint64_t)aliashdr->skinwidth * aliashdr->skinheight;

	// just skipping textures, renderer will take care of them later
	for( int i = 0; i < numskins; i++ )
	{
		if( !Mod_AliasFits( pskintype, end, 1, sizeof( *pskintype )))
			return NULL;

		// skin data can leave everything after it unaligned
		daliasskintype_t skintype;
		memcpy( &skintype, pskintype, sizeof( skintype ));

		if( skintype.type == ALIAS_SKIN_SINGLE )
		{
			const byte *ptexture = (const byte *)&pskintype[1];

			if( !Mod_AliasFits( ptexture, end, 1, size ))
				return NULL;

			pskintype = (const daliasskintype_t *)( ptexture + size );
		}
		else
		{
			const daliasskingroup_t *pinskingroup = (const daliasskingroup_t *)&pskintype[1];

			if( !Mod_AliasFits( pinskingroup, end, 1, sizeof( *pinskingroup )))
				return NULL;

			daliasskingroup_t group;
			memcpy( &group, pinskingroup, sizeof( group ));

			if( group.numskins < 1 )
				return NULL;

			const daliasskininterval_t *pinskinintervals = (const daliasskininterval_t *)&pinskingroup[1];

			if( !Mod_AliasFits( pinskinintervals, end, group.numskins, sizeof( *pinskinintervals ) + size ))
				return NULL;

			const byte *ptexture = (const byte *)( pinskinintervals + group.numskins );

			pskintype = (const daliasskintype_t *)( ptexture + group.numskins * size );
		}
	}

	return pskintype;
}

static const void *Mod_CheckAliasMesh( const stvert_t *pinstverts, const byte *end, const aliashdr_t *aliashdr )
{
	if( !Mod_AliasFits( pinstverts, end, aliashdr->numverts, sizeof( *pinstverts )))
		return NULL;

	const dtriangle_t *pintriangles = (const dtriangle_t *)&pinstverts[aliashdr->numverts];

	if( !Mod_AliasFits( pintriangles, end, aliashdr->numtris, sizeof( *pintriangles )))
		return NULL;

	for( int i = 0; i < aliashdr->numtris; i++ )
	{
		// skin data can leave the triangles unaligned
		dtriangle_t tri;
		memcpy( &tri, &pintriangles[i], sizeof( tri ));

		for( int j = 0; j < 3; j++ )
		{
			if( tri.vertindex[j] < 0 || tri.vertindex[j] >= aliashdr->numverts )
				return NULL;
		}
	}

	return &pintriangles[aliashdr->numtris];
}


/*
====================
Mod_LoadAliasModel

load alias model
====================
*/
void Mod_LoadAliasModel( model_t *mod, const void *buffer, size_t buffersize, qboolean *loaded )
{
	if( loaded ) *loaded = false;
	const daliashdr_t *pinmodel = (const daliashdr_t *)buffer;
	const byte *end = (const byte *)buffer + buffersize;

	if( buffersize < sizeof( *pinmodel ))
	{
		Con_DPrintf( S_ERROR "%s: %s is too short\n", __func__, mod->name );
		return;
	}

	if( pinmodel->version != ALIAS_VERSION )
	{
		Con_DPrintf( S_ERROR "%s has wrong version number (%i should be %i)\n", mod->name, pinmodel->version, ALIAS_VERSION );
		return;
	}

	if( pinmodel->numverts <= 0 || pinmodel->numtris <= 0 || pinmodel->numframes <= 0 )
		return; // how is it possible to make that?

	char poolname[MAX_VA_STRING];
	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );
	mod->mempool = Mem_AllocPool( poolname );

	size_t size = sizeof( aliashdr_t ) + (pinmodel->numframes - 1) * sizeof( maliasframedesc_t );
	aliashdr_t *m_pAliasHeader;
	mod->cache.data = m_pAliasHeader = Mem_Calloc( mod->mempool, size );

	// endian-adjust and copy the data, starting with the alias model header
	m_pAliasHeader->numverts = pinmodel->numverts;

	if( m_pAliasHeader->numverts > MAXALIASVERTS )
	{
		Con_DPrintf( S_ERROR "model %s has too many vertices\n", mod->name );
		return;
	}

	mod->flags = pinmodel->flags; // share effects flags
	m_pAliasHeader->boundingradius = pinmodel->boundingradius;
	m_pAliasHeader->numskins = pinmodel->numskins;
	m_pAliasHeader->skinwidth = pinmodel->skinwidth;
	m_pAliasHeader->skinheight = pinmodel->skinheight;
	m_pAliasHeader->numtris = pinmodel->numtris;
	mod->numframes = m_pAliasHeader->numframes = pinmodel->numframes;
	m_pAliasHeader->size = pinmodel->size;

	for( int i = 0; i < 3; i++ )
	{
		m_pAliasHeader->scale[i] = pinmodel->scale[i];
		m_pAliasHeader->scale_origin[i] = pinmodel->scale_origin[i];
		m_pAliasHeader->eyeposition[i] = pinmodel->eyeposition[i];
	}

	// load the skins
	const daliasskintype_t *pskintype = (const daliasskintype_t *)&pinmodel[1];
	pskintype = Mod_LoadAllSkins( mod, m_pAliasHeader->numskins, pskintype, end, m_pAliasHeader );
	// will be done at renderer side...

	// load base s and t vertices and triangle lists
	// will be done at renderer side...
	const daliasframetype_t *pframetype = NULL;
	if( pskintype )
		pframetype = Mod_CheckAliasMesh((const stvert_t *)pskintype, end, m_pAliasHeader );

	// load the frames
	m_pAliasHeader->pposeverts = g_poseverts; // store the pointer to be accessed by renderer
	g_posenum = 0;

	for( int i = 0; pframetype && i < m_pAliasHeader->numframes; i++ )
	{
		daliasframetype_t frametype;

		if( !Mod_AliasFits( pframetype, end, 1, sizeof( frametype )))
		{
			pframetype = NULL;
			break;
		}

		memcpy( &frametype, pframetype, sizeof( frametype ));

		if( frametype.type == ALIAS_SINGLE )
			pframetype = (const daliasframetype_t *)Mod_LoadAliasFrame((const daliasframe_t *)&pframetype[1], end, &m_pAliasHeader->frames[i], m_pAliasHeader );
		else pframetype = (const daliasframetype_t *)Mod_LoadAliasGroup(( const daliasgroup_t *)&pframetype[1], end, &m_pAliasHeader->frames[i], m_pAliasHeader );
	}

	if( !pframetype )
	{
		Con_DPrintf( S_ERROR "%s: %s is not a valid alias model\n", __func__, mod->name );
		return;
	}

	m_pAliasHeader->numposes = g_posenum;
	Mod_CalcAliasBounds( mod, m_pAliasHeader );
	mod->type = mod_alias;

	if( loaded ) *loaded = true;	// done
}

