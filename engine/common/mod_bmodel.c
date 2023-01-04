/*
mod_bmodel.c - loading & handling world and brushmodels
Copyright (C) 2016 Uncle Mike

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
#include "mod_local.h"
#include "sprite.h"
#include "xash3d_mathlib.h"
#include "alias.h"
#include "studio.h"
#include "wadfile.h"
#include "world.h"
#include "enginefeatures.h"
#include "client.h"
#include "server.h"			// LUMP_ error codes
#include "ref_common.h"
typedef struct wadlist_s
{
	char			wadnames[MAX_MAP_WADS][32];
	int			wadusage[MAX_MAP_WADS];
	int			count;
} wadlist_t;

typedef struct leaflist_s
{
	int			count;
	int			maxcount;
	qboolean			overflowed;
	int			*list;
	vec3_t			mins, maxs;
	int			topnode;		// for overflows where each leaf can't be stored individually
} leaflist_t;

typedef struct
{
	// generic lumps
	dmodel_t			*submodels;
	size_t			numsubmodels;

	dvertex_t			*vertexes;
	size_t			numvertexes;

	dplane_t			*planes;
	size_t			numplanes;

	union
	{
		dnode_t		*nodes;
		dnode32_t		*nodes32;
	};
	size_t			numnodes;

	union
	{
		dleaf_t		*leafs;
		dleaf32_t		*leafs32;
	};
	size_t			numleafs;

	union
	{
		dclipnode_t	*clipnodes;
		dclipnode32_t	*clipnodes32;
	};
	size_t			numclipnodes;

	dtexinfo_t		*texinfo;
	size_t			numtexinfo;

	union
	{
		dmarkface_t	*markfaces;
		dmarkface32_t	*markfaces32;
	};
	size_t			nummarkfaces;

	dsurfedge_t		*surfedges;
	size_t			numsurfedges;

	union
	{
		dedge_t		*edges;
		dedge32_t		*edges32;
	};
	size_t			numedges;

	union
	{
		dface_t		*surfaces;
		dface32_t		*surfaces32;
	};
	size_t			numsurfaces;

	dfaceinfo_t		*faceinfo;
	size_t			numfaceinfo;

	// array lumps
	byte			*visdata;
	size_t			visdatasize;

	byte			*lightdata;
	size_t			lightdatasize;

	byte			*deluxdata;
	size_t			deluxdatasize;

	byte			*shadowdata;
	size_t			shadowdatasize;

	byte			*entdata;
	size_t			entdatasize;

	// lumps that required personal handler
	dmiptexlump_t		*textures;
	size_t			texdatasize;

	// intermediate arrays (pointers will lost after loading, but keep the data)
	color24			*deluxedata_out;	// deluxemap data pointer
	byte			*shadowdata_out;	// occlusion data pointer
	dclipnode32_t		*clipnodes_out;	// temporary 32-bit array to hold clipnodes

	// misc stuff
	wadlist_t			wadlist;
	int			lightmap_samples;	// samples per lightmap (1 or 3)
	int			version;		// model version
	qboolean			isworld;
	qboolean			isbsp30ext;
} dbspmodel_t;

typedef struct
{
	const char	*lumpname;
	size_t		entrysize;
	size_t		maxcount;
	size_t		count;
} mlumpstat_t;

typedef struct
{
	char		name[64];		// just for debug

	// count errors and warnings
	int		numerrors;
	int		numwarnings;
} loadstat_t;

#define CHECK_OVERFLOW	BIT( 0 )		// if some of lumps will be overflowed this non fatal for us. But some lumps are critical. mark them
#define USE_EXTRAHEADER	BIT( 1 )

#define LUMP_SAVESTATS	BIT( 0 )
#define LUMP_TESTONLY	BIT( 1 )
#define LUMP_SILENT		BIT( 2 )
#define LUMP_BSP30EXT   BIT( 3 ) // extra marker for Mod_LoadLump

typedef struct
{
	int		lumpnumber;
	const size_t	mincount;
	const size_t	maxcount;
	const int		entrysize;
	const int		entrysize32;	// alternative (-1 by default)
	const char	*loadname;
	int		flags;
	const void	**dataptr;
	size_t		*count;
} mlumpinfo_t;

world_static_t		world;
static dbspmodel_t		srcmodel;
static loadstat_t		loadstat;
static model_t		*worldmodel;
static byte		g_visdata[(MAX_MAP_LEAFS+7)/8];	// intermediate buffer
static mlumpstat_t		worldstats[HEADER_LUMPS+EXTRA_LUMPS];
static mlumpinfo_t		srclumps[HEADER_LUMPS] =
{
{ LUMP_ENTITIES, 32, MAX_MAP_ENTSTRING, sizeof( byte ), -1, "entities", 0, (const void **)&srcmodel.entdata, &srcmodel.entdatasize },
{ LUMP_PLANES, 1, MAX_MAP_PLANES, sizeof( dplane_t ), -1, "planes", 0, (const void **)&srcmodel.planes, &srcmodel.numplanes },
{ LUMP_TEXTURES, 1, MAX_MAP_MIPTEX, sizeof( byte ), -1, "textures", 0, (const void **)&srcmodel.textures, &srcmodel.texdatasize },
{ LUMP_VERTEXES, 0, MAX_MAP_VERTS, sizeof( dvertex_t ), -1, "vertexes", 0, (const void **)&srcmodel.vertexes, &srcmodel.numvertexes },
{ LUMP_VISIBILITY, 0, MAX_MAP_VISIBILITY, sizeof( byte ), -1, "visibility", 0, (const void **)&srcmodel.visdata, &srcmodel.visdatasize },
{ LUMP_NODES, 1, MAX_MAP_NODES, sizeof( dnode_t ), sizeof( dnode32_t ), "nodes", CHECK_OVERFLOW, (const void **)&srcmodel.nodes, &srcmodel.numnodes },
{ LUMP_TEXINFO, 0, MAX_MAP_TEXINFO, sizeof( dtexinfo_t ), -1, "texinfo", CHECK_OVERFLOW, (const void **)&srcmodel.texinfo, &srcmodel.numtexinfo },
{ LUMP_FACES, 0, MAX_MAP_FACES, sizeof( dface_t ), sizeof( dface32_t ), "faces", CHECK_OVERFLOW, (const void **)&srcmodel.surfaces, &srcmodel.numsurfaces },
{ LUMP_LIGHTING, 0, MAX_MAP_LIGHTING, sizeof( byte ), -1, "lightmaps", 0, (const void **)&srcmodel.lightdata, &srcmodel.lightdatasize },
{ LUMP_CLIPNODES, 0, MAX_MAP_CLIPNODES, sizeof( dclipnode_t ), sizeof( dclipnode32_t ), "clipnodes", 0, (const void **)&srcmodel.clipnodes, &srcmodel.numclipnodes },
{ LUMP_LEAFS, 1, MAX_MAP_LEAFS, sizeof( dleaf_t ), sizeof( dleaf32_t ), "leafs", CHECK_OVERFLOW, (const void **)&srcmodel.leafs, &srcmodel.numleafs },
{ LUMP_MARKSURFACES, 0, MAX_MAP_MARKSURFACES, sizeof( dmarkface_t ), sizeof( dmarkface32_t ), "markfaces", 0, (const void **)&srcmodel.markfaces, &srcmodel.nummarkfaces },
{ LUMP_EDGES, 0, MAX_MAP_EDGES, sizeof( dedge_t ), sizeof( dedge32_t ), "edges", 0, (const void **)&srcmodel.edges, &srcmodel.numedges },
{ LUMP_SURFEDGES, 0, MAX_MAP_SURFEDGES, sizeof( dsurfedge_t ), -1, "surfedges", 0, (const void **)&srcmodel.surfedges, &srcmodel.numsurfedges },
{ LUMP_MODELS, 1, MAX_MAP_MODELS, sizeof( dmodel_t ), -1, "models", CHECK_OVERFLOW, (const void **)&srcmodel.submodels, &srcmodel.numsubmodels },
};

static mlumpinfo_t		extlumps[EXTRA_LUMPS] =
{
{ LUMP_LIGHTVECS, 0, MAX_MAP_LIGHTING, sizeof( byte ), -1, "deluxmaps", USE_EXTRAHEADER, (const void **)&srcmodel.deluxdata, &srcmodel.deluxdatasize },
{ LUMP_FACEINFO,  0, MAX_MAP_FACEINFO, sizeof( dfaceinfo_t ), -1, "faceinfos", CHECK_OVERFLOW|USE_EXTRAHEADER, (const void **)&srcmodel.faceinfo, &srcmodel.numfaceinfo },
{ LUMP_SHADOWMAP, 0, MAX_MAP_LIGHTING / 3, sizeof( byte ), -1, "shadowmap", USE_EXTRAHEADER, (const void **)&srcmodel.shadowdata, &srcmodel.shadowdatasize },
};

/*
===============================================================================

			MAP PROCESSING

===============================================================================
*/
/*
=================
Mod_LoadLump

generic loader
=================
*/
static void Mod_LoadLump( const byte *in, mlumpinfo_t *info, mlumpstat_t *stat, int flags )
{
	int	version = ((dheader_t *)in)->version;
	size_t	numelems, real_entrysize;
	char	msg1[32], msg2[32];
	dlump_t	*l = NULL;

	if( FBitSet( info->flags, USE_EXTRAHEADER ))
	{
		dextrahdr_t *header = (dextrahdr_t *)((byte *)in + sizeof( dheader_t ));
		if( header->id != IDEXTRAHEADER || header->version != EXTRA_VERSION )
			return;
		l = &header->lumps[info->lumpnumber];
	}
	else
	{
		dheader_t	*header = (dheader_t *)in;
		l = &header->lumps[info->lumpnumber];
	}

	// lump is unused by engine for some reasons ?
	if( !l || info->entrysize <= 0 || info->maxcount <= 0 )
		return;

	real_entrysize = info->entrysize; // default

	// analyze real entrysize
	if( version == QBSP2_VERSION && info->entrysize32 > 0 )
	{
		// always use alternate entrysize for BSP2
		real_entrysize = info->entrysize32;
	}
	else if( version == HLBSP_VERSION && FBitSet( flags, LUMP_BSP30EXT ) && info->lumpnumber == LUMP_CLIPNODES )
	{
		// if this map is bsp30ext, try to guess extended clipnodes
		if((( l->filelen % info->entrysize ) || ( l->filelen / info->entrysize32 ) >= MAX_MAP_CLIPNODES_HLBSP ))
		{
			real_entrysize = info->entrysize32;
		}
	}

	// bmodels not required the visibility
	if( !FBitSet( flags, LUMP_TESTONLY ) && !world.loading && info->lumpnumber == LUMP_VISIBILITY )
		SetBits( flags, LUMP_SILENT ); // shut up warning

	// fill the stats for world
	if( FBitSet( flags, LUMP_SAVESTATS ))
	{
		stat->lumpname = info->loadname;
		stat->entrysize = real_entrysize;
		stat->maxcount = info->maxcount;
		if( real_entrysize != 0 )
			stat->count = l->filelen / real_entrysize;
	}

	Q_strncpy( msg1, info->loadname, sizeof( msg1 ));
	Q_strncpy( msg2, info->loadname, sizeof( msg2 ));
	msg2[0] = Q_toupper( msg2[0] ); // first letter in cap

	// lump is not present
	if( l->filelen <= 0 )
	{
		// don't warn about extra lumps - it's optional
		if( !FBitSet( info->flags, USE_EXTRAHEADER ))
		{
			// some data array that may be optional
			if( real_entrysize == sizeof( byte ))
			{
				if( !FBitSet( flags, LUMP_SILENT ))
				{
					Con_DPrintf( S_WARN "map ^2%s^7 has no %s\n", loadstat.name, msg1 );
					loadstat.numwarnings++;
				}
			}
			else if( info->mincount > 0 )
			{
				// it has the mincount and the lump is completely missed!
				if( !FBitSet( flags, LUMP_SILENT ))
					Con_DPrintf( S_ERROR "map ^2%s^7 has no %s\n", loadstat.name, msg1 );
				loadstat.numerrors++;
			}
		}
		return;
	}

	if( l->filelen % real_entrysize )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "Mod_Load%s: Lump size %d was not a multiple of %lu bytes\n", msg2, l->filelen, real_entrysize );
		loadstat.numerrors++;
		return;
	}

	numelems = l->filelen / real_entrysize;

	if( numelems < info->mincount )
	{
		// it has the mincount and it's smaller than this limit
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "map ^2%s^7 has no %s\n", loadstat.name, msg1 );
		loadstat.numerrors++;
		return;
	}

	if( numelems > info->maxcount )
	{
		// it has the maxcount and it's overflowed
		if( FBitSet( info->flags, CHECK_OVERFLOW ))
		{
			if( !FBitSet( flags, LUMP_SILENT ))
				Con_DPrintf( S_ERROR "map ^2%s^7 has too many %s\n", loadstat.name, msg1 );
			loadstat.numerrors++;
			return;
		}
		else if( !FBitSet( flags, LUMP_SILENT ))
		{
			// just throw warning
			Con_DPrintf( S_WARN "map ^2%s^7 has too many %s\n", loadstat.name, msg1 );
			loadstat.numwarnings++;
		}
	}

	if( FBitSet( flags, LUMP_TESTONLY ))
		return; // don't fill the intermediate struct

	// all checks are passed, store pointers
	if( info->dataptr ) *info->dataptr = (void *)(in + l->fileofs);
	if( info->count ) *info->count = numelems;
}

/*
================
Mod_ArrayUsage
================
*/
static int Mod_ArrayUsage( const char *szItem, int items, int maxitems, int itemsize )
{
	float	percentage = maxitems ? (items * 100.0f / maxitems) : 0.0f;

	Con_Printf( "%-12s  %7i/%-7i  %8i/%-8i  (%4.1f%%) ", szItem, items, maxitems, items * itemsize, maxitems * itemsize, percentage );

	if( percentage > 99.99f )
		Con_Printf( "^1SIZE OVERFLOW!!!^7\n" );
	else if( percentage > 95.0f )
		Con_Printf( "^3SIZE DANGER!^7\n" );
	else if( percentage > 80.0f )
		Con_Printf( "^2VERY FULL!^7\n" );
	else Con_Printf( "\n" );

	return items * itemsize;
}

/*
================
Mod_GlobUsage
================
*/
static int Mod_GlobUsage( const char *szItem, int itemstorage, int maxstorage )
{
	float	percentage = maxstorage ? (itemstorage * 100.0f / maxstorage) : 0.0f;

	Con_Printf( "%-15s  %-12s  %8i/%-8i  (%4.1f%%) ", szItem, "[variable]", itemstorage, maxstorage, percentage );

	if( percentage > 99.99f )
		Con_Printf( "^1SIZE OVERFLOW!!!^7\n" );
	else if( percentage > 95.0f )
		Con_Printf( "^3SIZE DANGER!^7\n" );
	else if( percentage > 80.0f )
		Con_Printf( "^2VERY FULL!^7\n" );
	else Con_Printf( "\n" );

	return itemstorage;
}

/*
=============
Mod_PrintWorldStats_f

Dumps info about world
=============
*/
void Mod_PrintWorldStats_f( void )
{
	int	i, totalmemory = 0;
	model_t	*w = worldmodel;

	if( !w || !w->numsubmodels )
	{
		Con_Printf( "No map loaded\n" );
		return;
	}

	Con_Printf( "\n" );
	Con_Printf( "Object names  Objects/Maxobjs  Memory / Maxmem  Fullness\n" );
	Con_Printf( "------------  ---------------  ---------------  --------\n" );

	for( i = 0; i < ARRAYSIZE( worldstats ); i++ )
	{
		mlumpstat_t *stat = &worldstats[i];

		if( !stat->lumpname || !stat->maxcount || !stat->count )
			continue; // unused or lump is empty

		if( stat->entrysize == sizeof( byte ))
			totalmemory += Mod_GlobUsage( stat->lumpname, stat->count, stat->maxcount );
		else totalmemory += Mod_ArrayUsage( stat->lumpname, stat->count, stat->maxcount, stat->entrysize );
	}

	Con_Printf( "=== Total BSP file data space used: %s ===\n", Q_memprint( totalmemory ));
	Con_Printf( "World size ( %g %g %g ) units\n", world.size[0], world.size[1], world.size[2] );
	Con_Printf( "Supports transparency world water: %s\n", FBitSet( world.flags, FWORLD_WATERALPHA ) ? "Yes" : "No" );
	Con_Printf( "Lighting: %s\n", FBitSet( w->flags, MODEL_COLORED_LIGHTING ) ? "colored" : "monochrome" );
	Con_Printf( "World total leafs: %d\n", worldmodel->numleafs + 1 );
	Con_Printf( "original name: ^1%s\n", worldmodel->name );
	Con_Printf( "internal name: ^2%s\n", world.message[0] ? world.message : "none" );
	Con_Printf( "map compiler: ^3%s\n", world.compiler[0] ? world.compiler : "unknown" );
	Con_Printf( "map editor: ^2%s\n", world.generator[0] ? world.generator : "unknown" );
}

/*
===============================================================================

			COMMON ROUTINES

===============================================================================
*/
/*
===================
Mod_DecompressPVS
===================
*/
byte *Mod_DecompressPVS( const byte *in, int visbytes )
{
	byte	*out;
	int	c;

	out = g_visdata;

	if( !in )
	{
		// no vis info, so make all visible
		while( visbytes )
		{
			*out++ = 0xff;
			visbytes--;
		}
		return g_visdata;
	}

	do
	{
		if( *in )
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;

		while( c )
		{
			*out++ = 0;
			c--;
		}
	} while( out - g_visdata < visbytes );

	return g_visdata;
}

/*
==================
Mod_PointInLeaf

==================
*/
mleaf_t *Mod_PointInLeaf( const vec3_t p, mnode_t *node )
{
	Assert( node != NULL );

	while( 1 )
	{
		if( node->contents < 0 )
			return (mleaf_t *)node;
		node = node->children[PlaneDiff( p, node->plane ) <= 0];
	}

	// never reached
	return NULL;
}

/*
==================
Mod_GetPVSForPoint

Returns PVS data for a given point
NOTE: can return NULL
==================
*/
byte *Mod_GetPVSForPoint( const vec3_t p )
{
	mnode_t	*node;
	mleaf_t	*leaf = NULL;

	ASSERT( worldmodel != NULL );

	node = worldmodel->nodes;

	while( 1 )
	{
		if( node->contents < 0 )
		{
			leaf = (mleaf_t *)node;
			break; // we found a leaf
		}
		node = node->children[PlaneDiff( p, node->plane ) <= 0];
	}

	if( leaf && leaf->cluster >= 0 )
		return Mod_DecompressPVS( leaf->compressed_vis, world.visbytes );
	return NULL;
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

==================
*/
static void Mod_FatPVS_RecursiveBSPNode( const vec3_t org, float radius, byte *visbuffer, int visbytes, mnode_t *node )
{
	int	i;

	while( node->contents >= 0 )
	{
		float d = PlaneDiff( org, node->plane );

		if( d > radius )
			node = node->children[0];
		else if( d < -radius )
			node = node->children[1];
		else
		{
			// go down both sides
			Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, visbytes, node->children[0] );
			node = node->children[1];
		}
	}

	// if this leaf is in a cluster, accumulate the vis bits
	if(((mleaf_t *)node)->cluster >= 0 )
	{
		byte	*vis = Mod_DecompressPVS( ((mleaf_t *)node)->compressed_vis, world.visbytes );

		for( i = 0; i < visbytes; i++ )
			visbuffer[i] |= vis[i];
	}
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

Calculates a PVS that is the inclusive or of all leafs
within radius pixels of the given point.
==================
*/
int Mod_FatPVS( const vec3_t org, float radius, byte *visbuffer, int visbytes, qboolean merge, qboolean fullvis )
{
	int	bytes = world.visbytes;
	mleaf_t	*leaf = NULL;

	ASSERT( worldmodel != NULL );

	leaf = Mod_PointInLeaf( org, worldmodel->nodes );
	bytes = Q_min( bytes, visbytes );

	// enable full visibility for some reasons
	if( fullvis || !worldmodel->visdata || !leaf || leaf->cluster < 0 )
	{
		memset( visbuffer, 0xFF, bytes );
		return bytes;
	}

	if( !merge ) memset( visbuffer, 0x00, bytes );

	Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, bytes, worldmodel->nodes );

	return bytes;
}

/*
======================================================================

LEAF LISTING

======================================================================
*/
static void Mod_BoxLeafnums_r( leaflist_t *ll, mnode_t *node )
{
	int	sides;

	while( 1 )
	{
		if( node->contents == CONTENTS_SOLID )
			return;

		if( node->contents < 0 )
		{
			mleaf_t	*leaf = (mleaf_t *)node;

			// it's a leaf!
			if( ll->count >= ll->maxcount )
			{
				ll->overflowed = true;
				return;
			}

			ll->list[ll->count++] = leaf->cluster;
			return;
		}

		sides = BOX_ON_PLANE_SIDE( ll->mins, ll->maxs, node->plane );

		if( sides == 1 )
		{
			node = node->children[0];
		}
		else if( sides == 2 )
		{
			node = node->children[1];
		}
		else
		{
			// go down both
			if( ll->topnode == -1 )
				ll->topnode = node - worldmodel->nodes;
			Mod_BoxLeafnums_r( ll, node->children[0] );
			node = node->children[1];
		}
	}
}

/*
==================
Mod_BoxLeafnums
==================
*/
static int Mod_BoxLeafnums( const vec3_t mins, const vec3_t maxs, int *list, int listsize, int *topnode )
{
	leaflist_t	ll;

	if( !worldmodel ) return 0;

	VectorCopy( mins, ll.mins );
	VectorCopy( maxs, ll.maxs );

	ll.maxcount = listsize;
	ll.overflowed = false;
	ll.topnode = -1;
	ll.list = list;
	ll.count = 0;

	Mod_BoxLeafnums_r( &ll, worldmodel->nodes );

	if( topnode ) *topnode = ll.topnode;
	return ll.count;
}

/*
=============
Mod_BoxVisible

Returns true if any leaf in boxspace
is potentially visible
=============
*/
qboolean Mod_BoxVisible( const vec3_t mins, const vec3_t maxs, const byte *visbits )
{
	int	leafList[MAX_BOX_LEAFS];
	int	i, count;

	if( !visbits || !mins || !maxs )
		return true;

	count = Mod_BoxLeafnums( mins, maxs, leafList, MAX_BOX_LEAFS, NULL );

	for( i = 0; i < count; i++ )
	{
		if( CHECKVISBIT( visbits, leafList[i] ))
			return true;
	}
	return false;
}

/*
=============
Mod_HeadnodeVisible
=============
*/
qboolean Mod_HeadnodeVisible( mnode_t *node, const byte *visbits, int *lastleaf )
{
	if( !node || node->contents == CONTENTS_SOLID )
		return false;

	if( node->contents < 0 )
	{
		if( !CHECKVISBIT( visbits, ((mleaf_t *)node)->cluster ))
			return false;

		if( lastleaf )
			*lastleaf = ((mleaf_t *)node)->cluster;
		return true;
	}

	if( Mod_HeadnodeVisible( node->children[0], visbits, lastleaf ))
		return true;

	if( Mod_HeadnodeVisible( node->children[1], visbits, lastleaf ))
		return true;

	return false;
}

/*
==================
Mod_AmbientLevels

grab the ambient sound levels for current point
==================
*/
void Mod_AmbientLevels( const vec3_t p, byte *pvolumes )
{
	mleaf_t	*leaf;

	if( !worldmodel || !p || !pvolumes )
		return;

	leaf = Mod_PointInLeaf( p, worldmodel->nodes );
	*(int *)pvolumes = *(int *)leaf->ambient_sound_level;
}

/*
=================
Mod_FindModelOrigin

routine to detect bmodels with origin-brush
=================
*/
static void Mod_FindModelOrigin( const char *entities, const char *modelname, vec3_t origin )
{
	char	*pfile;
	string	keyname;
	char	token[2048];
	qboolean	model_found;
	qboolean	origin_found;

	if( !entities || !modelname || !*modelname )
		return;

	if( !origin || !VectorIsNull( origin ))
		return;

	pfile = (char *)entities;

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( token[0] != '{' )
			Host_Error( "Mod_FindModelOrigin: found %s when expecting {\n", token );

		model_found = origin_found = false;
		VectorClear( origin );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "Mod_FindModelOrigin: EOF without closing brace\n" );
			if( token[0] == '}' ) break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "Mod_FindModelOrigin: EOF without closing brace\n" );

			if( token[0] == '}' )
				Host_Error( "Mod_FindModelOrigin: closing brace without data\n" );

			if( !Q_stricmp( keyname, "model" ) && !Q_stricmp( modelname, token ))
				model_found = true;

			if( !Q_stricmp( keyname, "origin" ))
			{
				Q_atov( origin, token, 3 );
				origin_found = true;
			}
		}

		if( model_found ) break;
	}
}

/*
==================
Mod_CheckWaterAlphaSupport

converted maps potential may don't
support water transparency
==================
*/
static qboolean Mod_CheckWaterAlphaSupport( dbspmodel_t *bmod )
{
	mleaf_t		*leaf;
	int		i, j;
	const byte	*pvs;

	if( bmod->visdatasize <= 0 )
		return true;

	// check all liquid leafs to see if they can see into empty leafs, if any
	// can we can assume this map supports r_wateralpha
	for( i = 0, leaf = loadmodel->leafs; i < loadmodel->numleafs; i++, leaf++ )
	{
		if(( leaf->contents == CONTENTS_WATER || leaf->contents == CONTENTS_SLIME ) && leaf->cluster >= 0 )
		{
			pvs = Mod_DecompressPVS( leaf->compressed_vis, world.visbytes );

			for( j = 0; j < loadmodel->numleafs; j++ )
			{
				if( CHECKVISBIT( pvs, loadmodel->leafs[j].cluster ) && loadmodel->leafs[j].contents == CONTENTS_EMPTY )
					return true;
			}
		}
	}

	return false;
}

/*
==================
Mod_SampleSizeForFace

return the current lightmap resolution per face
==================
*/
int Mod_SampleSizeForFace( msurface_t *surf )
{
	if( !surf || !surf->texinfo )
		return LM_SAMPLE_SIZE;

	// world luxels has more priority
	if( FBitSet( surf->texinfo->flags, TEX_WORLD_LUXELS ))
		return 1;

	if( FBitSet( surf->texinfo->flags, TEX_EXTRA_LIGHTMAP ))
		return LM_SAMPLE_EXTRASIZE;

	if( surf->texinfo->faceinfo )
		return surf->texinfo->faceinfo->texture_step;

	return LM_SAMPLE_SIZE;
}

/*
==================
Mod_GetFaceContents

determine face contents by name
==================
*/
static int Mod_GetFaceContents( const char *name )
{
	if( !Q_strnicmp( name, "SKY", 3 ))
		return CONTENTS_SKY;

	if( name[0] == '!' || name[0] == '*' )
	{
		if( !Q_strnicmp( name + 1, "lava", 4 ))
			return CONTENTS_LAVA;
		else if( !Q_strnicmp( name + 1, "slime", 5 ))
			return CONTENTS_SLIME;
		return CONTENTS_WATER; // otherwise it's water
	}

	if( !Q_strnicmp( name, "water", 5 ))
		return CONTENTS_WATER;

	return CONTENTS_SOLID;
}

/*
==================
Mod_GetFaceContents

determine face contents by name
==================
*/
static mvertex_t *Mod_GetVertexByNumber( model_t *mod, int surfedge )
{
	int	lindex;
	medge_t	*edge;

	lindex = mod->surfedges[surfedge];

	if( lindex > 0 )
	{
		edge = &mod->edges[lindex];
		return &mod->vertexes[edge->v[0]];
	}
	else
	{
		edge = &mod->edges[-lindex];
		return &mod->vertexes[edge->v[1]];
	}
}

/*
==================
Mod_MakeNormalAxial

remove jitter from near-axial normals
==================
*/
static void Mod_MakeNormalAxial( vec3_t normal )
{
	int	i, type;

	for( type = 0; type < 3; type++ )
	{
		if( fabs( normal[type] ) > 0.9999f )
			break;
	}

	// make positive and pure axial
	for( i = 0; i < 3 && type != 3; i++ )
	{
		if( i == type )
			normal[i] = 1.0f;
		else normal[i] = 0.0f;
	}
}

/*
==================
Mod_LightMatrixFromTexMatrix

compute lightmap matrix based on texture matrix
==================
*/
static void Mod_LightMatrixFromTexMatrix( const mtexinfo_t *tx, float lmvecs[2][4] )
{
	float	lmscale = LM_SAMPLE_SIZE;
	int	i, j;

	// this is can't be possible but who knews
	if( FBitSet( tx->flags, TEX_EXTRA_LIGHTMAP ))
		lmscale = LM_SAMPLE_EXTRASIZE;

	if( tx->faceinfo )
		lmscale = tx->faceinfo->texture_step;

	// copy texmatrix into lightmap matrix fisrt
	for( i = 0; i < 2; i++ )
	{
		for( j = 0; j < 4; j++ )
		{
			lmvecs[i][j] = tx->vecs[i][j];
		}
	}

	if( !FBitSet( tx->flags, TEX_WORLD_LUXELS ))
		return; // just use texmatrix

	VectorNormalize( lmvecs[0] );
	VectorNormalize( lmvecs[1] );

	if( FBitSet( tx->flags, TEX_AXIAL_LUXELS ))
	{
		Mod_MakeNormalAxial( lmvecs[0] );
		Mod_MakeNormalAxial( lmvecs[1] );
	}

	// put the lighting origin at center the of poly
	VectorScale( lmvecs[0], (1.0f / lmscale), lmvecs[0] );
	VectorScale( lmvecs[1], -(1.0f / lmscale), lmvecs[1] );

	lmvecs[0][3] = lmscale * 0.5f;
	lmvecs[1][3] = -lmscale * 0.5f;
}

/*
=================
Mod_CalcSurfaceExtents

Fills in surf->texturemins[] and surf->extents[]
=================
*/
static void Mod_CalcSurfaceExtents( msurface_t *surf )
{
	// this place is VERY critical to precision
	// keep it as float, don't use double, because it causes issues with lightmap
	float		mins[2], maxs[2], val;
	float		lmmins[2], lmmaxs[2];
	int		bmins[2], bmaxs[2];
	int		i, j, e, sample_size;
	mextrasurf_t	*info = surf->info;
	int		facenum = surf - loadmodel->surfaces;
	mtexinfo_t	*tex;
	mvertex_t		*v;

	sample_size = Mod_SampleSizeForFace( surf );
	tex = surf->texinfo;

	Mod_LightMatrixFromTexMatrix( tex, info->lmvecs );

	mins[0] = lmmins[0] = mins[1] = lmmins[1] = 999999;
	maxs[0] = lmmaxs[0] = maxs[1] = lmmaxs[1] =-999999;

	for( i = 0; i < surf->numedges; i++ )
	{
		e = loadmodel->surfedges[surf->firstedge + i];

		if( e >= loadmodel->numedges || e <= -loadmodel->numedges )
			Host_Error( "Mod_CalcSurfaceExtents: bad edge\n" );

		if( e >= 0 ) v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for( j = 0; j < 2; j++ )
		{
			val = DotProductPrecise( v->position, surf->texinfo->vecs[j] ) + surf->texinfo->vecs[j][3];
			mins[j] = Q_min( val, mins[j] );
			maxs[j] = Q_max( val, maxs[j] );
		}

		for( j = 0; j < 2; j++ )
		{
			val = DotProductPrecise( v->position, info->lmvecs[j] ) + info->lmvecs[j][3];
			lmmins[j] = Q_min( val, lmmins[j] );
			lmmaxs[j] = Q_max( val, lmmaxs[j] );
		}
	}

	for( i = 0; i < 2; i++ )
	{
		bmins[i] = floor( mins[i] / sample_size );
		bmaxs[i] = ceil( maxs[i] / sample_size );

		surf->texturemins[i] = bmins[i] * sample_size;
		surf->extents[i] = (bmaxs[i] - bmins[i]) * sample_size;

		if( FBitSet( tex->flags, TEX_WORLD_LUXELS ))
		{
			lmmins[i] = floor( lmmins[i] );
			lmmaxs[i] = ceil( lmmaxs[i] );

			info->lightmapmins[i] = lmmins[i];
			info->lightextents[i] = (lmmaxs[i] - lmmins[i]);
		}
		else
		{
			// just copy texturemins
			info->lightmapmins[i] = surf->texturemins[i];
			info->lightextents[i] = surf->extents[i];
		}

#if !XASH_DEDICATED && 0 // REFTODO:
		if( !FBitSet( tex->flags, TEX_SPECIAL ) && ( surf->extents[i] > 16384 ) && ( tr.block_size == BLOCK_SIZE_DEFAULT ))
			Con_Reportf( S_ERROR "Bad surface extents %i\n", surf->extents[i] );
#endif // XASH_DEDICATED
	}
}

/*
=================
Mod_CalcSurfaceBounds

fills in surf->mins and surf->maxs
=================
*/
static void Mod_CalcSurfaceBounds( msurface_t *surf )
{
	int	i, e;
	mvertex_t	*v;

	ClearBounds( surf->info->mins, surf->info->maxs );

	for( i = 0; i < surf->numedges; i++ )
	{
		e = loadmodel->surfedges[surf->firstedge + i];

		if( e >= loadmodel->numedges || e <= -loadmodel->numedges )
			Host_Error( "Mod_CalcSurfaceBounds: bad edge\n" );

		if( e >= 0 ) v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		AddPointToBounds( v->position, surf->info->mins, surf->info->maxs );
	}

	VectorAverage( surf->info->mins, surf->info->maxs, surf->info->origin );
}

/*
=================
Mod_CreateFaceBevels
=================
*/
static void Mod_CreateFaceBevels( msurface_t *surf )
{
	vec3_t		delta, edgevec;
	byte		*facebevel;
	vec3_t		faceNormal;
	mvertex_t		*v0, *v1;
	int		contents;
	int		i, size;
	vec_t		radius;
	mfacebevel_t	*fb;

	if( surf->texinfo && surf->texinfo->texture )
		contents = Mod_GetFaceContents( surf->texinfo->texture->name );
	else contents = CONTENTS_SOLID;

	size = sizeof( mfacebevel_t ) + surf->numedges * sizeof( mplane_t );
	facebevel = (byte *)Mem_Calloc( loadmodel->mempool, size );
	fb = (mfacebevel_t *)facebevel;
	facebevel += sizeof( mfacebevel_t );
	fb->edges = (mplane_t *)facebevel;
	fb->numedges = surf->numedges;
	fb->contents = contents;
	surf->info->bevel = fb;

	if( FBitSet( surf->flags, SURF_PLANEBACK ))
		VectorNegate( surf->plane->normal, faceNormal );
	else VectorCopy( surf->plane->normal, faceNormal );

	// compute face origin and plane edges
	for( i = 0; i < surf->numedges; i++ )
	{
		mplane_t	*dest = &fb->edges[i];

		v0 = Mod_GetVertexByNumber( loadmodel, surf->firstedge + i );
		v1 = Mod_GetVertexByNumber( loadmodel, surf->firstedge + (i + 1) % surf->numedges );
		VectorSubtract( v1->position, v0->position, edgevec );
		CrossProduct( faceNormal, edgevec, dest->normal );
		VectorNormalize( dest->normal );
		dest->dist = DotProduct( dest->normal, v0->position );
		dest->type = PlaneTypeForNormal( dest->normal );
		VectorAdd( fb->origin, v0->position, fb->origin );
	}

	VectorScale( fb->origin, 1.0f / surf->numedges, fb->origin );

	// compute face radius
	for( i = 0; i < surf->numedges; i++ )
	{
		v0 = Mod_GetVertexByNumber( loadmodel, surf->firstedge + i );
		VectorSubtract( v0->position, fb->origin, delta );
		radius = DotProduct( delta, delta );
		fb->radius = Q_max( radius, fb->radius );
	}
}

/*
=================
Mod_SetParent
=================
*/
static void Mod_SetParent( mnode_t *node, mnode_t *parent )
{
	node->parent = parent;

	if( node->contents < 0 ) return; // it's leaf
	Mod_SetParent( node->children[0], node );
	Mod_SetParent( node->children[1], node );
}

/*
==================
CountClipNodes_r
==================
*/
static void CountClipNodes_r( mclipnode_t *src, hull_t *hull, int nodenum )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == MAX_MAP_CLIPNODES )
		Host_Error( "MAX_MAP_CLIPNODES limit exceeded\n" );
	hull->lastclipnode++;

	CountClipNodes_r( src, hull, src[nodenum].children[0] );
	CountClipNodes_r( src, hull, src[nodenum].children[1] );
}

/*
==================
CountClipNodes32_r
==================
*/
static void CountClipNodes32_r( dclipnode32_t *src, hull_t *hull, int nodenum )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == MAX_MAP_CLIPNODES )
		Host_Error( "MAX_MAP_CLIPNODES limit exceeded\n" );
	hull->lastclipnode++;

	CountClipNodes32_r( src, hull, src[nodenum].children[0] );
	CountClipNodes32_r( src, hull, src[nodenum].children[1] );
}

/*
==================
RemapClipNodes_r
==================
*/
static int RemapClipNodes_r( dclipnode32_t *srcnodes, hull_t *hull, int nodenum )
{
	dclipnode32_t	*src;
	mclipnode_t	*out;
	int		i, c;

	// leaf?
	if( nodenum < 0 )
		return nodenum;

	// emit a clipnode
	if( hull->lastclipnode == MAX_MAP_CLIPNODES )
		Host_Error( "MAX_MAP_CLIPNODES limit exceeded\n" );
	src = srcnodes + nodenum;

	c = hull->lastclipnode;
	out = &hull->clipnodes[c];
	hull->lastclipnode++;

	out->planenum = src->planenum;

	for( i = 0; i < 2; i++ )
		out->children[i] = RemapClipNodes_r( srcnodes, hull, src->children[i] );

	return c;
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0( void )
{
	mnode_t		*in, *child;
	mclipnode_t	*out;
	hull_t		*hull;
	int		i, j;

	hull = &loadmodel->hulls[0];
	hull->clipnodes = out = Mem_Malloc( loadmodel->mempool, loadmodel->numnodes * sizeof( *out ));
	in = loadmodel->nodes;

	hull->firstclipnode = 0;
	hull->lastclipnode = loadmodel->numnodes - 1;
	hull->planes = loadmodel->planes;

	for( i = 0; i < loadmodel->numnodes; i++, out++, in++ )
	{
		out->planenum = in->plane - loadmodel->planes;

		for( j = 0; j < 2; j++ )
		{
			child = in->children[j];

			if( child->contents < 0 )
				out->children[j] = child->contents;
			else out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_SetupHull
=================
*/
static void Mod_SetupHull( dbspmodel_t *bmod, model_t *mod, poolhandle_t mempool, int headnode, int hullnum )
{
	hull_t	*hull = &mod->hulls[hullnum];
	int	count;

	// assume no hull
	hull->firstclipnode = hull->lastclipnode = 0;
	hull->planes = NULL; // hull is missed

	if(( headnode == -1 ) || ( hullnum != 1 && headnode == 0 ))
		return; // hull missed

	if( headnode >= mod->numclipnodes )
		return;	// ZHLT weird empty hulls

	switch( hullnum )
	{
	case 1:
		VectorCopy( host.player_mins[0], hull->clip_mins ); // copy human hull
		VectorCopy( host.player_maxs[0], hull->clip_maxs );
		break;
	case 2:
		VectorCopy( host.player_mins[3], hull->clip_mins ); // copy large hull
		VectorCopy( host.player_maxs[3], hull->clip_maxs );
		break;
	case 3:
		VectorCopy( host.player_mins[1], hull->clip_mins ); // copy head hull
		VectorCopy( host.player_maxs[1], hull->clip_maxs );
		break;
	default:
		Host_Error( "Mod_SetupHull: bad hull number %i\n", hullnum );
		break;
	}

	if( VectorIsNull( hull->clip_mins ) && VectorIsNull( hull->clip_maxs ))
		return;	// no hull specified

	CountClipNodes32_r( bmod->clipnodes_out, hull, headnode );
	count = hull->lastclipnode;

	// fit array to real count
	hull->clipnodes = (mclipnode_t *)Mem_Malloc( mempool, sizeof( mclipnode_t ) * hull->lastclipnode );
	hull->planes = mod->planes; // share planes
	hull->lastclipnode = 0; // restart counting

	// remap clipnodes to 16-bit indexes
	RemapClipNodes_r( bmod->clipnodes_out, hull, headnode );
}

/*
=================
Mod_LoadColoredLighting
=================
*/
static qboolean Mod_LoadColoredLighting( dbspmodel_t *bmod )
{
	char	modelname[64];
	char	path[64];
	int	iCompare;
	fs_offset_t	litdatasize;
	byte	*in;

	COM_FileBase( loadmodel->name, modelname );
	Q_snprintf( path, sizeof( path ), "maps/%s.lit", modelname );

	// make sure what deluxemap is actual
	if( !COM_CompareFileTime( path, loadmodel->name, &iCompare ))
		return false;

	if( iCompare < 0 ) // this may happens if level-designer used -onlyents key for hlcsg
		Con_Printf( S_WARN "%s probably is out of date\n", path );

	in = FS_LoadFile( path, &litdatasize, false );

	Assert( in != NULL );

	if( *(uint *)in != IDDELUXEMAPHEADER || *((uint *)in + 1) != DELUXEMAP_VERSION )
	{
		Mem_Free( in );
		return false;
	}

	// skip header bytes
	litdatasize -= 8;

	if( litdatasize != ( bmod->lightdatasize * 3 ))
	{
		Con_Printf( S_ERROR "%s has mismatched size (%li should be %lu)\n", path, litdatasize, bmod->lightdatasize * 3 );
		Mem_Free( in );
		return false;
	}

	loadmodel->lightdata = Mem_Malloc( loadmodel->mempool, litdatasize );
	memcpy( loadmodel->lightdata, in + 8, litdatasize );
	SetBits( loadmodel->flags, MODEL_COLORED_LIGHTING );
	bmod->lightdatasize = litdatasize;
	Mem_Free( in );

	return true;
}

/*
=================
Mod_LoadDeluxemap
=================
*/
static void Mod_LoadDeluxemap( dbspmodel_t *bmod )
{
	char	modelname[64];
	fs_offset_t	deluxdatasize;
	char	path[64];
	int	iCompare;
	byte	*in;

	if( !FBitSet( host.features, ENGINE_LOAD_DELUXEDATA ))
		return;

	COM_FileBase( loadmodel->name, modelname );
	Q_snprintf( path, sizeof( path ), "maps/%s.dlit", modelname );

	// make sure what deluxemap is actual
	if( !COM_CompareFileTime( path, loadmodel->name, &iCompare ))
		return;

	if( iCompare < 0 ) // this may happens if level-designer used -onlyents key for hlcsg
		Con_Printf( S_WARN "%s probably is out of date\n", path );

	in = FS_LoadFile( path, &deluxdatasize, false );

	Assert( in != NULL );

	if( *(uint *)in != IDDELUXEMAPHEADER || *((uint *)in + 1) != DELUXEMAP_VERSION )
	{
		Mem_Free( in );
		return;
	}

	// skip header bytes
	deluxdatasize -= 8;

	if( deluxdatasize != bmod->lightdatasize )
	{
		Con_Reportf( S_ERROR "%s has mismatched size (%li should be %lu)\n", path, deluxdatasize, bmod->lightdatasize );
		Mem_Free( in );
		return;
	}

	bmod->deluxedata_out = Mem_Malloc( loadmodel->mempool, deluxdatasize );
	memcpy( bmod->deluxedata_out, in + 8, deluxdatasize );
	bmod->deluxdatasize = deluxdatasize;
	Mem_Free( in );
}

/*
=================
Mod_SetupSubmodels

duplicate the basic information
for embedded submodels
=================
*/
static void Mod_SetupSubmodels( dbspmodel_t *bmod )
{
	qboolean	colored = false;
	poolhandle_t mempool;
	char	*ents;
	model_t	*mod;
	dmodel_t 	*bm;
	int	i, j;

	ents = loadmodel->entities;
	mempool = loadmodel->mempool;
	if( FBitSet( loadmodel->flags, MODEL_COLORED_LIGHTING ))
		colored = true;
	mod = loadmodel;

	loadmodel->numframes = 2;	// regular and alternate animation

	// set up the submodels
	for( i = 0; i < mod->numsubmodels; i++ )
	{
		bm = &mod->submodels[i];

		// hull 0 is just shared across all bmodels
		mod->hulls[0].firstclipnode = bm->headnode[0];
		mod->hulls[0].lastclipnode = bm->headnode[0]; // need to be real count

		// counting a real number of clipnodes per each submodel
		CountClipNodes_r( mod->hulls[0].clipnodes, &mod->hulls[0], bm->headnode[0] );

		// but hulls1-3 is build individually for a each given submodel
		for( j = 1; j < MAX_MAP_HULLS; j++ )
			Mod_SetupHull( bmod, mod, mempool, bm->headnode[j], j );

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy( bm->mins, mod->mins );
		VectorCopy( bm->maxs, mod->maxs );

		mod->radius = RadiusFromBounds( mod->mins, mod->maxs );
		mod->numleafs = bm->visleafs;
		mod->flags = 0;

		// this bit will be shared between all the submodels include worldmodel
		if( colored ) SetBits( mod->flags, MODEL_COLORED_LIGHTING );

		if( i != 0 )
		{
			Mod_FindModelOrigin( ents, va( "*%i", i ), bm->origin );

			// mark models that have origin brushes
			if( !VectorIsNull( bm->origin ))
				SetBits( mod->flags, MODEL_HAS_ORIGIN );
#ifdef HACKS_RELATED_HLMODS
			// c2a1 doesn't have origin brush it's just placed at center of the level
			if( !Q_stricmp( loadmodel->name, "maps/c2a1.bsp" ) && ( i == 11 ))
				SetBits( mod->flags, MODEL_HAS_ORIGIN );
#endif
		}

		// sets the model flags
		for( j = 0; i != 0 && j < mod->nummodelsurfaces; j++ )
		{
			msurface_t *surf = mod->surfaces + mod->firstmodelsurface + j;

			if( FBitSet( surf->flags, SURF_CONVEYOR ))
				SetBits( mod->flags, MODEL_CONVEYOR );

			if( FBitSet( surf->flags, SURF_TRANSPARENT ))
				SetBits( mod->flags, MODEL_TRANSPARENT );

			if( FBitSet( surf->flags, SURF_DRAWTURB ))
				SetBits( mod->flags, MODEL_LIQUID );
		}

		if( i < mod->numsubmodels - 1 )
		{
			char	name[8];

			// duplicate the basic information
			Q_snprintf( name, sizeof( name ), "*%i", i + 1 );
			loadmodel = Mod_FindName( name, true );
			*loadmodel = *mod;
			Q_strncpy( loadmodel->name, name, sizeof( loadmodel->name ));
			loadmodel->mempool = 0;
			mod = loadmodel;
		}
	}

	if( bmod->clipnodes_out != NULL )
		Mem_Free( bmod->clipnodes_out );
}

/*
===============================================================================

			MAP LOADING

===============================================================================
*/
/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels( dbspmodel_t *bmod )
{
	dmodel_t	*in, *out;
	int	oldmaxfaces;
	int	i, j;

	// allocate extradata for each dmodel_t
	out = Mem_Malloc( loadmodel->mempool, bmod->numsubmodels * sizeof( *out ));

	loadmodel->numsubmodels = bmod->numsubmodels;
	loadmodel->submodels = out;
	in = bmod->submodels;

	if( bmod->isworld )
		refState.max_surfaces = 0;
	oldmaxfaces = refState.max_surfaces;

	for( i = 0; i < bmod->numsubmodels; i++, in++, out++ )
	{
		for( j = 0; j < 3; j++ )
		{
			// reset empty bounds to prevent error
			if( in->mins[j] == 999999.0f )
				in->mins[j] = 0.0f;
			if( in->maxs[j] == -999999.0f)
				in->maxs[j] = 0.0f;

			// spread the mins / maxs by a unit
			out->mins[j] = in->mins[j] - 1.0f;
			out->maxs[j] = in->maxs[j] + 1.0f;
			out->origin[j] = in->origin[j];
		}

		for( j = 0; j < MAX_MAP_HULLS; j++ )
			out->headnode[j] = in->headnode[j];

		out->visleafs = in->visleafs;
		out->firstface = in->firstface;
		out->numfaces = in->numfaces;

		if( i == 0 && bmod->isworld )
			continue; // skip the world to save mem
		oldmaxfaces = Q_max( oldmaxfaces, out->numfaces );
	}

	// these array used to sort translucent faces in bmodels
	if( oldmaxfaces > refState.max_surfaces )
	{
		refState.draw_surfaces = (sortedface_t *)Z_Realloc( refState.draw_surfaces, oldmaxfaces * sizeof( sortedface_t ));
		refState.max_surfaces = oldmaxfaces;
	}
}

/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities( dbspmodel_t *bmod )
{
	byte	*entpatch = NULL;
	char	token[MAX_TOKEN];
	char	wadstring[MAX_TOKEN];
	string	keyname;
	char	*pfile;

	if( bmod->isworld )
	{
		char	entfilename[MAX_QPATH];
		fs_offset_t	entpatchsize;
		size_t	ft1, ft2;

		// world is check for entfile too
		Q_strncpy( entfilename, loadmodel->name, sizeof( entfilename ));
		COM_ReplaceExtension( entfilename, ".ent" );

		// make sure what entity patch is never than bsp
		ft1 = FS_FileTime( loadmodel->name, false );
		ft2 = FS_FileTime( entfilename, true );

		if( ft2 != -1 )
		{
			if( ft1 > ft2 )
			{
				Con_Printf( S_WARN "Entity patch is older than bsp. Ignored.\n" );
			}
			else if(( entpatch = FS_LoadFile( entfilename, &entpatchsize, true )) != NULL )
			{
				Con_Printf( "^2Read entity patch:^7 %s\n", entfilename );
				bmod->entdatasize = entpatchsize;
				bmod->entdata = entpatch;
			}
		}
	}

	// make sure what we really has terminator
	loadmodel->entities = Mem_Calloc( loadmodel->mempool, bmod->entdatasize + 1 );
	memcpy( loadmodel->entities, bmod->entdata, bmod->entdatasize ); // moving to private model pool
	if( entpatch ) Mem_Free( entpatch ); // release entpatch if present
	if( !bmod->isworld ) return;

	pfile = (char *)loadmodel->entities;
	world.generator[0] = '\0';
	world.compiler[0] = '\0';
	world.message[0] = '\0';
	bmod->wadlist.count = 0;

	// parse all the wads for loading textures in right ordering
	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( token[0] != '{' )
			Host_Error( "Mod_LoadEntities: found %s when expecting {\n", token );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "Mod_LoadEntities: EOF without closing brace\n" );
			if( token[0] == '}' ) break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "Mod_LoadEntities: EOF without closing brace\n" );

			if( token[0] == '}' )
				Host_Error( "Mod_LoadEntities: closing brace without data\n" );

			if( !Q_stricmp( keyname, "wad" ))
			{
				char	*pszWadFile;

				Q_strncpy( wadstring, token, MAX_TOKEN - 2 );
				wadstring[MAX_TOKEN - 2] = 0;

				if( !Q_strchr( wadstring, ';' ))
					Q_strcat( wadstring, ";" );

				// parse wad pathes
				for( pszWadFile = strtok( wadstring, ";" ); pszWadFile != NULL; pszWadFile = strtok( NULL, ";" ))
				{
					COM_FixSlashes( pszWadFile );
					COM_FileBase( pszWadFile, token );

					// make sure what wad is really exist
					if( FS_FileExists( va( "%s.wad", token ), false ))
					{
						int num = bmod->wadlist.count++;
						Q_strncpy( bmod->wadlist.wadnames[num], token, sizeof( bmod->wadlist.wadnames[0] ));
						bmod->wadlist.wadusage[num] = 0;
					}

					if( bmod->wadlist.count >= MAX_MAP_WADS )
						break; // too many wads...
				}
			}
			else if( !Q_stricmp( keyname, "message" ))
				Q_strncpy( world.message, token, sizeof( world.message ));
			else if( !Q_stricmp( keyname, "compiler" ) || !Q_stricmp( keyname, "_compiler" ))
				Q_strncpy( world.compiler, token, sizeof( world.compiler ));
			else if( !Q_stricmp( keyname, "generator" ) || !Q_stricmp( keyname, "_generator" ))
				Q_strncpy( world.generator, token, sizeof( world.generator ));
		}
		return;	// all done
	}
}

/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes( dbspmodel_t *bmod )
{
	dplane_t	*in;
	mplane_t	*out;
	int	i, j;

	in = bmod->planes;
	loadmodel->planes = out = Mem_Malloc( loadmodel->mempool, bmod->numplanes * sizeof( *out ));
	loadmodel->numplanes = bmod->numplanes;

	for( i = 0; i < bmod->numplanes; i++, in++, out++ )
	{
		out->signbits = 0;
		for( j = 0; j < 3; j++ )
		{
			out->normal[j] = in->normal[j];

			if( out->normal[j] < 0.0f )
				SetBits( out->signbits, BIT( j ));
		}

		if( VectorLength( out->normal ) < 0.5f )
			Con_Printf( S_ERROR "bad normal for plane #%i\n", i );

		out->dist = in->dist;
		out->type = in->type;
	}
}

/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes( dbspmodel_t *bmod )
{
	dvertex_t	*in;
	mvertex_t	*out;
	int	i;

	in = bmod->vertexes;
	out = loadmodel->vertexes = Mem_Malloc( loadmodel->mempool, bmod->numvertexes * sizeof( mvertex_t ));
	loadmodel->numvertexes = bmod->numvertexes;

	if( bmod->isworld ) ClearBounds( world.mins, world.maxs );

	for( i = 0; i < bmod->numvertexes; i++, in++, out++ )
	{
		if( bmod->isworld )
			AddPointToBounds( in->point, world.mins, world.maxs );
		VectorCopy( in->point, out->position );
	}

	if( !bmod->isworld ) return;

	VectorSubtract( world.maxs, world.mins, world.size );

	for( i = 0; i < 3; i++ )
	{
		// spread the mins / maxs by a pixel
		world.mins[i] -= 1.0f;
		world.maxs[i] += 1.0f;
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges( dbspmodel_t *bmod )
{
	medge_t	*out;
	int	i;

	loadmodel->edges = out = Mem_Malloc( loadmodel->mempool, bmod->numedges * sizeof( medge_t ));
	loadmodel->numedges = bmod->numedges;

	if( bmod->version == QBSP2_VERSION )
	{
		dedge32_t	*in = (dedge32_t *)bmod->edges32;

		for( i = 0; i < bmod->numedges; i++, in++, out++ )
		{
			out->v[0] = in->v[0];
			out->v[1] = in->v[1];
		}
	}
	else
	{
		dedge_t	*in = (dedge_t *)bmod->edges;

		for( i = 0; i < bmod->numedges; i++, in++, out++ )
		{
			out->v[0] = (word)in->v[0];
			out->v[1] = (word)in->v[1];
		}
	}
}

/*
=================
Mod_LoadSurfEdges
=================
*/
static void Mod_LoadSurfEdges( dbspmodel_t *bmod )
{
	loadmodel->surfedges = Mem_Malloc( loadmodel->mempool, bmod->numsurfedges * sizeof( dsurfedge_t ));
	memcpy( loadmodel->surfedges, bmod->surfedges, bmod->numsurfedges * sizeof( dsurfedge_t ));
	loadmodel->numsurfedges = bmod->numsurfedges;
}

/*
=================
Mod_LoadMarkSurfaces
=================
*/
static void Mod_LoadMarkSurfaces( dbspmodel_t *bmod )
{
	msurface_t	**out;
	int		i;

	loadmodel->marksurfaces = out = Mem_Malloc( loadmodel->mempool, bmod->nummarkfaces * sizeof( *out ));
	loadmodel->nummarksurfaces = bmod->nummarkfaces;

	if( bmod->version == QBSP2_VERSION )
	{
		dmarkface32_t	*in = bmod->markfaces32;

		for( i = 0; i < bmod->nummarkfaces; i++, in++ )
		{
			if( *in < 0 || *in >= loadmodel->numsurfaces )
				Host_Error( "Mod_LoadMarkFaces: bad surface number in '%s'\n", loadmodel->name );
			out[i] = loadmodel->surfaces + *in;
		}
	}
	else
	{
		dmarkface_t	*in = bmod->markfaces;

		for( i = 0; i < bmod->nummarkfaces; i++, in++ )
		{
			if( *in < 0 || *in >= loadmodel->numsurfaces )
				Host_Error( "Mod_LoadMarkFaces: bad surface number in '%s'\n", loadmodel->name );
			out[i] = loadmodel->surfaces + *in;
		}
	}
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures( dbspmodel_t *bmod )
{
	dmiptexlump_t	*in;
	texture_t		*tx, *tx2;
	texture_t		*anims[10];
	texture_t		*altanims[10];
	int		num, max, altmax;
	qboolean		custom_palette;
	char		texname[64];
	mip_t		*mt;
	int 		i, j;

	if( bmod->isworld )
	{
#if !XASH_DEDICATED
		// release old sky layers first
		if( !Host_IsDedicated() )
		{
			ref.dllFuncs.GL_FreeTexture( R_GetBuiltinTexture( REF_ALPHASKY_TEXTURE ));
			ref.dllFuncs.GL_FreeTexture( R_GetBuiltinTexture( REF_SOLIDSKY_TEXTURE ));
		}
#endif
	}

	if( !bmod->texdatasize )
	{
		// no textures
		loadmodel->textures = NULL;
		return;
	}

	in = bmod->textures;
	loadmodel->textures = (texture_t **)Mem_Calloc( loadmodel->mempool, in->nummiptex * sizeof( texture_t* ));
	loadmodel->numtextures = in->nummiptex;

	for( i = 0; i < loadmodel->numtextures; i++ )
	{
		int	txFlags = 0;

		if( in->dataofs[i] == -1 )
		{
			// create default texture (some mods requires this)
			tx = Mem_Calloc( loadmodel->mempool, sizeof( *tx ));
			loadmodel->textures[i] = tx;

			Q_strncpy( tx->name, "*default", sizeof( tx->name ));
#if !XASH_DEDICATED
			if( !Host_IsDedicated() )
			{
				tx->gl_texturenum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );
				tx->width = tx->height = 16;
			}
#endif
			continue; // missed
		}

		mt = (mip_t *)((byte *)in + in->dataofs[i] );

		if( !mt->name[0] )
			Q_snprintf( mt->name, sizeof( mt->name ), "miptex_%i", i );
		tx = Mem_Calloc( loadmodel->mempool, sizeof( *tx ));
		loadmodel->textures[i] = tx;

		// convert to lowercase
		Q_strncpy( tx->name, mt->name, sizeof( tx->name ));
		Q_strnlwr( tx->name, tx->name, sizeof( tx->name ));
		custom_palette = false;

		tx->width = mt->width;
		tx->height = mt->height;

		if( FBitSet( host.features, ENGINE_IMPROVED_LINETRACE ) && mt->name[0] == '{' )
			SetBits( txFlags, TF_KEEP_SOURCE ); // Paranoia2 texture alpha-tracing

		if( mt->offsets[0] > 0 )
		{
			int	size = (int)sizeof( mip_t ) + ((mt->width * mt->height * 85)>>6);
			int	next_dataofs = 0, remaining;

			// compute next dataofset to determine allocated miptex space
			for( j = i + 1; j < loadmodel->numtextures; j++ )
			{
				next_dataofs = in->dataofs[j];
				if( next_dataofs != -1 ) break;
			}

			if( j == loadmodel->numtextures )
				next_dataofs = bmod->texdatasize;

			// NOTE: imagelib detect miptex version by size
			// 770 additional bytes is indicated custom palette
			remaining = next_dataofs - (in->dataofs[i] + size);
			if( remaining >= 770 ) custom_palette = true;
		}

#if !XASH_DEDICATED
		if( !Host_IsDedicated() )
		{
			// check for multi-layered sky texture (quake1 specific)
			if( bmod->isworld && !Q_strncmp( mt->name, "sky", 3 ) && (( mt->width / mt->height ) == 2 ) )
			{
				ref.dllFuncs.R_InitSkyClouds( mt, tx, custom_palette ); // load quake sky

				if( R_GetBuiltinTexture( REF_SOLIDSKY_TEXTURE ) &&
					R_GetBuiltinTexture( REF_ALPHASKY_TEXTURE ) )
					SetBits( world.flags, FWORLD_SKYSPHERE );
				continue;
			}

			// texture loading order:
			// 1. from wad
			// 2. internal from map

			// trying wad texture (force while r_wadtextures is 1)
			if(( r_wadtextures->value && bmod->wadlist.count > 0 ) || ( mt->offsets[0] <= 0 ))
			{
				Q_snprintf( texname, sizeof( texname ), "%s.mip", mt->name );

				// check wads in reverse order
				for( j = bmod->wadlist.count - 1; j >= 0; j-- )
				{
					char	*texpath = va( "%s.wad/%s", bmod->wadlist.wadnames[j], texname );

					if( FS_FileExists( texpath, false ))
					{
						tx->gl_texturenum = ref.dllFuncs.GL_LoadTexture( texpath, NULL, 0, txFlags );
						bmod->wadlist.wadusage[j]++; // this wad are really used
						break;
					}
				}
			}

			// wad failed, so use internal texture (if present)
			if( mt->offsets[0] > 0 && !tx->gl_texturenum )
			{
				// NOTE: imagelib detect miptex version by size
				// 770 additional bytes is indicated custom palette
				int	size = (int)sizeof( mip_t ) + ((mt->width * mt->height * 85)>>6);

				if( custom_palette ) size += sizeof( short ) + 768;
				Q_snprintf( texname, sizeof( texname ), "#%s:%s.mip", loadstat.name, mt->name );
				tx->gl_texturenum = ref.dllFuncs.GL_LoadTexture( texname, (byte *)mt, size, txFlags );
			}

			// if texture is completely missed
			if( !tx->gl_texturenum )
			{
				Con_DPrintf( S_ERROR "unable to find %s.mip\n", mt->name );
				tx->gl_texturenum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );
			}

			// check for luma texture
			if( FBitSet( REF_GET_PARM( PARM_TEX_FLAGS, tx->gl_texturenum ), TF_HAS_LUMA ))
			{
				Q_snprintf( texname, sizeof( texname ), "#%s:%s_luma.mip", loadstat.name, mt->name );

				if( mt->offsets[0] > 0 )
				{
					// NOTE: imagelib detect miptex version by size
					// 770 additional bytes is indicated custom palette
					int	size = (int)sizeof( mip_t ) + ((mt->width * mt->height * 85)>>6);

					if( custom_palette ) size += sizeof( short ) + 768;
					tx->fb_texturenum = ref.dllFuncs.GL_LoadTexture( texname, (byte *)mt, size, TF_MAKELUMA );
				}
				else
				{
					fs_offset_t srcSize = 0;
					byte *src = NULL;

					// NOTE: we can't loading it from wad as normal because _luma texture doesn't exist
					// and not be loaded. But original texture is already loaded and can't be modified
					// So load original texture manually and convert it to luma

					// check wads in reverse order
					for( j = bmod->wadlist.count - 1; j >= 0; j-- )
					{
						char	*texpath = va( "%s.wad/%s.mip", bmod->wadlist.wadnames[j], tx->name );

						if( FS_FileExists( texpath, false ))
						{
							src = FS_LoadFile( texpath, &srcSize, false );
							bmod->wadlist.wadusage[j]++; // this wad are really used
							break;
						}
					}

					// okay, loading it from wad or hi-res version
					tx->fb_texturenum = ref.dllFuncs.GL_LoadTexture( texname, src, srcSize, TF_MAKELUMA );
					if( src ) Mem_Free( src );
				}
			}
		}
#endif
	}

	// sequence the animations and detail textures
	for( i = 0; i < loadmodel->numtextures; i++ )
	{
		tx = loadmodel->textures[i];

		if( !tx || ( tx->name[0] != '-' && tx->name[0] != '+' ))
			continue;

		if( tx->anim_next )
			continue;	// already sequenced

		// find the number of frames in the animation
		memset( anims, 0, sizeof( anims ));
		memset( altanims, 0, sizeof( altanims ));

		max = tx->name[1];
		altmax = 0;

		if( max >= '0' && max <= '9' )
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}
		else if( max >= 'a' && max <= 'j' )
		{
			altmax = max - 'a';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else Con_Printf( S_ERROR "Mod_LoadTextures: bad animating texture %s\n", tx->name );

		for( j = i + 1; j < loadmodel->numtextures; j++ )
		{
			tx2 = loadmodel->textures[j];

			if( !tx2 || ( tx2->name[0] != '-' && tx2->name[0] != '+' ))
				continue;

			if( Q_strcmp( tx2->name + 2, tx->name + 2 ))
				continue;

			num = tx2->name[1];

			if( num >= '0' && num <= '9' )
			{
				num -= '0';
				anims[num] = tx2;
				if( num + 1 > max )
					max = num + 1;
			}
			else if( num >= 'a' && num <= 'j' )
			{
				num = num - 'a';
				altanims[num] = tx2;
				if( num + 1 > altmax )
					altmax = num + 1;
			}
			else Con_Printf( S_ERROR "Mod_LoadTextures: bad animating texture %s\n", tx->name );
		}

		// link them all together
		for( j = 0; j < max; j++ )
		{
			tx2 = anims[j];

			if( !tx2 )
			{
				Con_Printf( S_ERROR "Mod_LoadTextures: missing frame %i of %s\n", j, tx->name );
				tx->anim_total = 0;
				break;
			}

			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = anims[(j + 1) % max];
			if( altmax ) tx2->alternate_anims = altanims[0];
		}

		for( j = 0; j < altmax; j++ )
		{
			tx2 = altanims[j];

			if( !tx2 )
			{
				Con_Printf( S_ERROR "Mod_LoadTextures: missing frame %i of %s\n", j, tx->name );
				tx->anim_total = 0;
				break;
			}

			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[(j + 1) % altmax];
			if( max ) tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadTexInfo
=================
*/
static void Mod_LoadTexInfo( dbspmodel_t *bmod )
{
	mfaceinfo_t	*fout, *faceinfo;
	int		i, j, k, miptex;
	dfaceinfo_t	*fin;
	mtexinfo_t	*out;
	dtexinfo_t	*in;

	// trying to load faceinfo
	faceinfo = fout = Mem_Calloc( loadmodel->mempool, bmod->numfaceinfo * sizeof( *fout ));
	fin = bmod->faceinfo;

	for( i = 0; i < bmod->numfaceinfo; i++, fin++, fout++ )
	{
		Q_strncpy( fout->landname, fin->landname, sizeof( fout->landname ));
		fout->texture_step = fin->texture_step;
		fout->max_extent = fin->max_extent;
		fout->groupid = fin->groupid;
	}

	loadmodel->texinfo = out = Mem_Calloc( loadmodel->mempool, bmod->numtexinfo * sizeof( *out ));
	loadmodel->numtexinfo = bmod->numtexinfo;
	in = bmod->texinfo;

	for( i = 0; i < bmod->numtexinfo; i++, in++, out++ )
	{
		for( j = 0; j < 2; j++ )
			for( k = 0; k < 4; k++ )
				out->vecs[j][k] = in->vecs[j][k];

		miptex = in->miptex;
		if( miptex < 0 || miptex > loadmodel->numtextures )
			miptex = 0; // this is possible?
		out->texture = loadmodel->textures[miptex];
		out->flags = in->flags;

		// make sure what faceinfo is really exist
		if( faceinfo != NULL && in->faceinfo != -1 && in->faceinfo < bmod->numfaceinfo )
			out->faceinfo = &faceinfo[in->faceinfo];
	}
}

/*
=================
Mod_LoadSurfaces
=================
*/
static void Mod_LoadSurfaces( dbspmodel_t *bmod )
{
	int		test_lightsize = -1;
	int		next_lightofs = -1;
	int		prev_lightofs = -1;
	int		i, j, lightofs;
	mextrasurf_t	*info;
	msurface_t	*out;

	loadmodel->surfaces = out = Mem_Calloc( loadmodel->mempool, bmod->numsurfaces * sizeof( msurface_t ));
	info = Mem_Calloc( loadmodel->mempool, bmod->numsurfaces * sizeof( mextrasurf_t ));
	loadmodel->numsurfaces = bmod->numsurfaces;

	// predict samplecount based on bspversion
	if( bmod->version == Q1BSP_VERSION || bmod->version == QBSP2_VERSION )
		bmod->lightmap_samples = 1;
	else bmod->lightmap_samples = 3;

	for( i = 0; i < bmod->numsurfaces; i++, out++, info++ )
	{
		texture_t	*tex;

		// setup crosslinks between two parts of msurface_t
		out->info = info;
		info->surf = out;

		if( bmod->version == QBSP2_VERSION )
		{
			dface32_t	*in = &bmod->surfaces32[i];

			if(( in->firstedge + in->numedges ) > loadmodel->numsurfedges )
				continue;	// corrupted level?
			out->firstedge = in->firstedge;
			out->numedges = in->numedges;
			if( in->side ) SetBits( out->flags, SURF_PLANEBACK );
			out->plane = loadmodel->planes + in->planenum;
			out->texinfo = loadmodel->texinfo + in->texinfo;

			for( j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in->styles[j];
			lightofs = in->lightofs;
		}
		else
		{
			dface_t	*in = &bmod->surfaces[i];

			if(( in->firstedge + in->numedges ) > loadmodel->numsurfedges )
			{
				Con_Reportf( S_ERROR "bad surface %i from %lu\n", i, bmod->numsurfaces );
				continue;
			}

			out->firstedge = in->firstedge;
			out->numedges = in->numedges;
			if( in->side ) SetBits( out->flags, SURF_PLANEBACK );
			out->plane = loadmodel->planes + in->planenum;
			out->texinfo = loadmodel->texinfo + in->texinfo;

			for( j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in->styles[j];
			lightofs = in->lightofs;
		}

		tex = out->texinfo->texture;

		if( !Q_strncmp( tex->name, "sky", 3 ))
			SetBits( out->flags, SURF_DRAWSKY );

		if(( tex->name[0] == '*' && Q_stricmp( tex->name, "*default" )) || tex->name[0] == '!' )
			SetBits( out->flags, SURF_DRAWTURB );

		if( !Host_IsQuakeCompatible( ))
		{
			if( !Q_strncmp( tex->name, "water", 5 ) || !Q_strnicmp( tex->name, "laser", 5 ))
				SetBits( out->flags, SURF_DRAWTURB );
		}

		if( !Q_strncmp( tex->name, "scroll", 6 ))
			SetBits( out->flags, SURF_CONVEYOR );

		if( FBitSet( out->texinfo->flags, TEX_SCROLL ))
			SetBits( out->flags, SURF_CONVEYOR );

		// g-cont. added a combined conveyor-transparent
		if( !Q_strncmp( tex->name, "{scroll", 7 ))
			SetBits( out->flags, SURF_CONVEYOR|SURF_TRANSPARENT );

		if( tex->name[0] == '{' )
			SetBits( out->flags, SURF_TRANSPARENT );

		if( FBitSet( out->texinfo->flags, TEX_SPECIAL ))
			SetBits( out->flags, SURF_DRAWTILED );

		Mod_CalcSurfaceBounds( out );
		Mod_CalcSurfaceExtents( out );
		Mod_CreateFaceBevels( out );

		// grab the second sample to detect colored lighting
		if( test_lightsize > 0 && lightofs != -1 )
		{
			if( lightofs > prev_lightofs && lightofs < next_lightofs )
				next_lightofs = lightofs;
		}

		// grab the first sample to determine lightmap size
		if( lightofs != -1 && test_lightsize == -1 )
		{
			int	sample_size = Mod_SampleSizeForFace( out );
			int	smax = (info->lightextents[0] / sample_size) + 1;
			int	tmax = (info->lightextents[1] / sample_size) + 1;
			int	lightstyles = 0;

			test_lightsize = smax * tmax;
			// count styles to right compute test_lightsize
			for( j = 0; j < MAXLIGHTMAPS && out->styles[j] != 255; j++ )
				lightstyles++;

			test_lightsize *= lightstyles;
			prev_lightofs = lightofs;
			next_lightofs = 99999999;
		}

#if !XASH_DEDICATED // TODO: Do we need subdivide on server?
		if( FBitSet( out->flags, SURF_DRAWTURB ) && !Host_IsDedicated() )
			ref.dllFuncs.GL_SubdivideSurface( out ); // cut up polygon for warps
#endif
	}

	// now we have enough data to trying determine samplecount per lightmap pixel
	if( test_lightsize > 0 && prev_lightofs != -1 && next_lightofs != -1 && next_lightofs != 99999999 )
	{
		float	samples = (float)(next_lightofs - prev_lightofs) / (float)test_lightsize;

		if( samples != (int)samples )
		{
			test_lightsize = (test_lightsize + 3) & ~3; // align datasize and try again
			samples = (float)(next_lightofs - prev_lightofs) / (float)test_lightsize;
		}

		if( samples == 1 || samples == 3 )
		{
			bmod->lightmap_samples = (int)samples;
			Con_Reportf( "lighting: %s\n", (bmod->lightmap_samples == 1) ? "monochrome" : "colored" );
			bmod->lightmap_samples = Q_max( bmod->lightmap_samples, 1 ); // avoid division by zero
		}
		else Con_DPrintf( S_WARN "lighting invalid samplecount: %g, defaulting to %i\n", samples, bmod->lightmap_samples );
	}
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes( dbspmodel_t *bmod )
{
	mnode_t	*out;
	int	i, j, p;

	loadmodel->nodes = out = (mnode_t *)Mem_Calloc( loadmodel->mempool, bmod->numnodes * sizeof( *out ));
	loadmodel->numnodes = bmod->numnodes;

	for( i = 0; i < loadmodel->numnodes; i++, out++ )
	{
		if( bmod->version == QBSP2_VERSION )
		{
			dnode32_t	*in = &bmod->nodes32[i];

			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

			p = in->planenum;
			out->plane = loadmodel->planes + p;
			out->firstsurface = in->firstface;
			out->numsurfaces = in->numfaces;

			for( j = 0; j < 2; j++ )
			{
				p = in->children[j];
				if( p >= 0 ) out->children[j] = loadmodel->nodes + p;
				else out->children[j] = (mnode_t *)(loadmodel->leafs + ( -1 - p ));
			}
		}
		else
		{
			dnode_t	*in = &bmod->nodes[i];

			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

			p = in->planenum;
			out->plane = loadmodel->planes + p;
			out->firstsurface = in->firstface;
			out->numsurfaces = in->numfaces;

			for( j = 0; j < 2; j++ )
			{
				p = in->children[j];
				if( p >= 0 ) out->children[j] = loadmodel->nodes + p;
				else out->children[j] = (mnode_t *)(loadmodel->leafs + ( -1 - p ));
			}
		}
	}

	// sets nodes and leafs
	Mod_SetParent( loadmodel->nodes, NULL );
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs( dbspmodel_t *bmod )
{
	mleaf_t	*out;
	int	i, j, p;
	int	visclusters = 0;

	loadmodel->leafs = out = (mleaf_t *)Mem_Calloc( loadmodel->mempool, bmod->numleafs * sizeof( *out ));
	loadmodel->numleafs = bmod->numleafs;

	if( bmod->isworld )
	{
		visclusters = loadmodel->submodels[0].visleafs;
		world.visbytes = (visclusters + 7) >> 3;
		world.fatbytes = (visclusters + 31) >> 3;
		refState.visbytes = world.visbytes;
	}

	for( i = 0; i < bmod->numleafs; i++, out++ )
	{
		if( bmod->version == QBSP2_VERSION )
		{
			dleaf32_t	*in = &bmod->leafs32[i];

			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

			out->contents = in->contents;
			p = in->visofs;

			for( j = 0; j < 4; j++ )
				out->ambient_sound_level[j] = in->ambient_level[j];

			out->firstmarksurface = loadmodel->marksurfaces + in->firstmarksurface;
			out->nummarksurfaces = in->nummarksurfaces;
		}
		else
		{
			dleaf_t	*in = &bmod->leafs[i];

			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

			out->contents = in->contents;
			p = in->visofs;

			for( j = 0; j < 4; j++ )
				out->ambient_sound_level[j] = in->ambient_level[j];

			out->firstmarksurface = loadmodel->marksurfaces + in->firstmarksurface;
			out->nummarksurfaces = in->nummarksurfaces;
		}

		if( bmod->isworld )
		{
			out->cluster = ( i - 1 ); // solid leaf 0 has no visdata

			if( out->cluster >= visclusters )
				out->cluster = -1;

			// ignore visofs errors on leaf 0 (solid)
			if( p >= 0 && out->cluster >= 0 && loadmodel->visdata )
			{
				if( p < bmod->visdatasize )
					out->compressed_vis = loadmodel->visdata + p;
				else Con_Reportf( S_WARN "Mod_LoadLeafs: invalid visofs for leaf #%i\n", i );
			}
		}
		else out->cluster = -1; // no visclusters on bmodels

		if( p == -1 ) out->compressed_vis = NULL;
		else out->compressed_vis = loadmodel->visdata + p;

		// gl underwater warp
		if( out->contents != CONTENTS_EMPTY )
		{
			for( j = 0; j < out->nummarksurfaces; j++ )
			{
				// mark underwater surfaces
				SetBits( out->firstmarksurface[j]->flags, SURF_UNDERWATER );
			}
		}
	}

	if( bmod->isworld && loadmodel->leafs[0].contents != CONTENTS_SOLID )
		Host_Error( "Mod_LoadLeafs: Map %s has leaf 0 is not CONTENTS_SOLID\n", loadmodel->name );

	// do some final things for world
	if( bmod->isworld && Mod_CheckWaterAlphaSupport( bmod ))
		SetBits( world.flags, FWORLD_WATERALPHA );
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes( dbspmodel_t *bmod )
{
	dclipnode32_t	*out;
	int		i;

	bmod->clipnodes_out = out = (dclipnode32_t *)Mem_Malloc( loadmodel->mempool, bmod->numclipnodes * sizeof( *out ));

	if(( bmod->version == QBSP2_VERSION ) || ( bmod->version == HLBSP_VERSION && bmod->isbsp30ext && bmod->numclipnodes >= MAX_MAP_CLIPNODES_HLBSP ))
	{
		dclipnode32_t	*in = bmod->clipnodes32;

		for( i = 0; i < bmod->numclipnodes; i++, out++, in++ )
		{
			out->planenum = in->planenum;
			out->children[0] = in->children[0];
			out->children[1] = in->children[1];
		}
	}
	else
	{
		dclipnode_t	*in = bmod->clipnodes;

		for( i = 0; i < bmod->numclipnodes; i++, out++, in++ )
		{
			out->planenum = in->planenum;

			out->children[0] = (unsigned short)in->children[0];
			out->children[1] = (unsigned short)in->children[1];

			// Arguire QBSP 'broken' clipnodes
			if( out->children[0] >= bmod->numclipnodes )
				out->children[0] -= 65536;
			if( out->children[1] >= bmod->numclipnodes )
				out->children[1] -= 65536;
		}
	}

	// FIXME: fill loadmodel->clipnodes?
	loadmodel->numclipnodes = bmod->numclipnodes;
}

/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility( dbspmodel_t *bmod )
{
	loadmodel->visdata = Mem_Malloc( loadmodel->mempool, bmod->visdatasize );
	memcpy( loadmodel->visdata, bmod->visdata, bmod->visdatasize );
}

/*
=================
Mod_LoadLightVecs
=================
*/
static void Mod_LoadLightVecs( dbspmodel_t *bmod )
{
	if( bmod->deluxdatasize != bmod->lightdatasize )
	{
		if( bmod->deluxdatasize > 0 )
			Con_Printf( S_ERROR "Mod_LoadLightVecs: has mismatched size (%lu should be %i)\n", bmod->deluxdatasize, bmod->lightdatasize );
		else Mod_LoadDeluxemap( bmod ); // old method
		return;
	}

	bmod->deluxedata_out = Mem_Malloc( loadmodel->mempool, bmod->deluxdatasize );
	memcpy( bmod->deluxedata_out, bmod->deluxdata, bmod->deluxdatasize );
}

/*
=================
Mod_LoadShadowmap
=================
*/
static void Mod_LoadShadowmap( dbspmodel_t *bmod )
{
	if( bmod->shadowdatasize != ( bmod->lightdatasize / 3 ))
	{
		if( bmod->shadowdatasize > 0 )
			Con_Printf( S_ERROR "Mod_LoadShadowmap: has mismatched size (%i should be %lu)\n", bmod->shadowdatasize, bmod->lightdatasize / 3 );
		return;
	}

	bmod->shadowdata_out = Mem_Malloc( loadmodel->mempool, bmod->shadowdatasize );
	memcpy( bmod->shadowdata_out, bmod->shadowdata, bmod->shadowdatasize );
}

/*
=================
Mod_LoadLighting
=================
*/
static void Mod_LoadLighting( dbspmodel_t *bmod )
{
	int		i, lightofs;
	msurface_t	*surf;
	color24		*out;
	byte		*in;

	if( !bmod->lightdatasize )
		return;

	switch( bmod->lightmap_samples )
	{
	case 1:
		if( !Mod_LoadColoredLighting( bmod ))
		{
			loadmodel->lightdata = out = (color24 *)Mem_Malloc( loadmodel->mempool, bmod->lightdatasize * sizeof( color24 ));
			in = bmod->lightdata;

			// expand the white lighting data
			for( i = 0; i < bmod->lightdatasize; i++, out++ )
				out->r = out->g = out->b = *in++;
		}
		break;
	case 3:	// load colored lighting
		loadmodel->lightdata = Mem_Malloc( loadmodel->mempool, bmod->lightdatasize );
		memcpy( loadmodel->lightdata, bmod->lightdata, bmod->lightdatasize );
		SetBits( loadmodel->flags, MODEL_COLORED_LIGHTING );
		break;
	default:
		Host_Error( "Mod_LoadLighting: bad lightmap sample count %i\n", bmod->lightmap_samples );
		break;
	}

	// not supposed to be load ?
	if( FBitSet( host.features, ENGINE_LOAD_DELUXEDATA ))
	{
		Mod_LoadLightVecs( bmod );
		Mod_LoadShadowmap( bmod );

		if( bmod->isworld && bmod->deluxdatasize )
			SetBits( world.flags, FWORLD_HAS_DELUXEMAP );
	}

	surf = loadmodel->surfaces;

	// setup lightdata pointers
	for( i = 0; i < loadmodel->numsurfaces; i++, surf++ )
	{
		if( bmod->version == QBSP2_VERSION )
			lightofs = bmod->surfaces32[i].lightofs;
		else lightofs = bmod->surfaces[i].lightofs;

		if( loadmodel->lightdata && lightofs != -1 )
		{
			int	offset = (lightofs / bmod->lightmap_samples);

			// NOTE: we divide offset by three because lighting and deluxemap keep their pointers
			// into three-bytes structs and shadowmap just monochrome
			surf->samples = loadmodel->lightdata + offset;

			// if deluxemap is present setup it too
			if( bmod->deluxedata_out )
				surf->info->deluxemap = bmod->deluxedata_out + offset;

			// will be used by mods
			if( bmod->shadowdata_out )
				surf->info->shadowmap = bmod->shadowdata_out + offset;
		}
	}
}

/*
=================
Mod_LumpLooksLikeEntities

=================
*/
static int Mod_LumpLooksLikeEntities( const char *lump, const size_t lumplen )
{
	// look for "classname" string
	return Q_memmem( lump, lumplen, "\"classname\"", sizeof( "\"classname\"" ) - 1 ) != NULL ? 1 : 0;
}

/*
=================
Mod_LoadBmodelLumps

loading and processing bmodel
=================
*/
qboolean Mod_LoadBmodelLumps( const byte *mod_base, qboolean isworld )
{
	const dheader_t *header = (const dheader_t *)mod_base;
	const dextrahdr_t	*extrahdr = (const dextrahdr_t *)(mod_base + sizeof( dheader_t ));
	dbspmodel_t	*bmod = &srcmodel;
	model_t		*mod = loadmodel;
	char		wadvalue[2048];
	size_t		len = 0;
	int		i, ret, flags = 0;

	// always reset the intermediate struct
	memset( bmod, 0, sizeof( dbspmodel_t ));
	memset( &loadstat, 0, sizeof( loadstat_t ));

	Q_strncpy( loadstat.name, loadmodel->name, sizeof( loadstat.name ));
	wadvalue[0] = '\0';

#ifndef SUPPORT_BSP2_FORMAT
	if( header->version == QBSP2_VERSION )
	{
		Con_Printf( S_ERROR DEFAULT_BSP_BUILD_ERROR, loadmodel->name );
		return false;
	}
#endif
	switch( header->version )
	{
	case HLBSP_VERSION:
		if( extrahdr->id == IDEXTRAHEADER )
		{
			SetBits( flags, LUMP_BSP30EXT );
		}
		// only relevant for half-life maps
		else if( !Mod_LumpLooksLikeEntities( mod_base + header->lumps[LUMP_ENTITIES].fileofs, header->lumps[LUMP_ENTITIES].filelen ) &&
			 Mod_LumpLooksLikeEntities( mod_base + header->lumps[LUMP_PLANES].fileofs, header->lumps[LUMP_PLANES].filelen ))
		{
			// blue-shift swapped lumps
			srclumps[0].lumpnumber = LUMP_PLANES;
			srclumps[1].lumpnumber = LUMP_ENTITIES;
			break;
		}
		// intended fallthrough
	case Q1BSP_VERSION:
	case QBSP2_VERSION:
		// everything else
		srclumps[0].lumpnumber = LUMP_ENTITIES;
		srclumps[1].lumpnumber = LUMP_PLANES;
		break;
	default:
		Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", loadmodel->name, header->version, HLBSP_VERSION );
		loadstat.numerrors++;
		return false;
	}

	bmod->version = header->version;	// share up global
	if( isworld )
	{
		world.flags = 0;	// clear world settings
		SetBits( flags, LUMP_SAVESTATS|LUMP_SILENT );
	}
	bmod->isworld = isworld;
	bmod->isbsp30ext = FBitSet( flags, LUMP_BSP30EXT );

	// loading base lumps
	for( i = 0; i < ARRAYSIZE( srclumps ); i++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[i], flags );

	// loading extralumps
	for( i = 0; i < ARRAYSIZE( extlumps ); i++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[ARRAYSIZE( srclumps ) + i], flags );

	if( !bmod->isworld && loadstat.numerrors )
	{
		Con_DPrintf( "Mod_Load%s: %i error(s), %i warning(s)\n", isworld ? "World" : "Brush", loadstat.numerrors, loadstat.numwarnings );
		return false; // there were errors, we can't load this map
	}
	else if( !bmod->isworld && loadstat.numwarnings )
		Con_DPrintf( "Mod_Load%s: %i warning(s)\n", isworld ? "World" : "Brush", loadstat.numwarnings );

	// load into heap
	Mod_LoadEntities( bmod );
	Mod_LoadPlanes( bmod );
	Mod_LoadSubmodels( bmod );
	Mod_LoadVertexes( bmod );
	Mod_LoadEdges( bmod );
	Mod_LoadSurfEdges( bmod );
	Mod_LoadTextures( bmod );
	Mod_LoadVisibility( bmod );
	Mod_LoadTexInfo( bmod );
	Mod_LoadSurfaces( bmod );
	Mod_LoadLighting( bmod );
	Mod_LoadMarkSurfaces( bmod );
	Mod_LoadLeafs( bmod );
	Mod_LoadNodes( bmod );
	Mod_LoadClipnodes( bmod );

	// preform some post-initalization
	Mod_MakeHull0 ();
	Mod_SetupSubmodels( bmod );

	if( isworld )
	{
		loadmodel = mod;		// restore pointer to world
#if !XASH_DEDICATED
		Mod_InitDebugHulls();	// FIXME: build hulls for separate bmodels (shells, medkits etc)
		world.deluxedata = bmod->deluxedata_out;	// deluxemap data pointer
		world.shadowdata = bmod->shadowdata_out;	// occlusion data pointer
#endif // XASH_DEDICATED
	}

	for( i = 0; i < bmod->wadlist.count; i++ )
	{
		if( !bmod->wadlist.wadusage[i] )
			continue;
		ret = Q_snprintf( &wadvalue[len], sizeof( wadvalue ), "%s.wad; ", bmod->wadlist.wadnames[i] );
		if( ret == -1 )
		{
			Con_DPrintf( S_WARN "Too many wad files for output!\n" );
			break;
		}
		len += ret;
	}

	if( COM_CheckString( wadvalue ))
	{
		wadvalue[Q_strlen( wadvalue ) - 2] = '\0'; // kill the last semicolon
		Con_Reportf( "Wad files required to run the map: \"%s\"\n", wadvalue );
	}

	return true;
}

static int Mod_LumpLooksLikeEntitiesFile( file_t *f, const dlump_t *l, int flags, const char *msg )
{
	char *buf;
	int ret;

	if( FS_Seek( f, l->fileofs, SEEK_SET ) < 0 )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "map ^2%s^7 %s lump past end of file\n", loadstat.name, msg );
		return -1;
	}

	buf = Z_Malloc( l->filelen + 1 );
	if( FS_Read( f, buf, l->filelen ) != l->filelen )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "can't read %s lump of map ^2%s^7", msg, loadstat.name );
		Z_Free( buf );
		return -1;
	}

	ret = Mod_LumpLooksLikeEntities( buf, l->filelen );

	Z_Free( buf );
	return ret;
}

/*
=================
Mod_TestBmodelLumps

check for possible errors
return real entities lump (for bshift swapped lumps)
=================
*/
qboolean Mod_TestBmodelLumps( file_t *f, const char *name, const byte *mod_base, qboolean silent, dlump_t *entities )
{
	const dheader_t	*header = (const dheader_t *)mod_base;
	const dextrahdr_t *extrahdr = (const dextrahdr_t *)( mod_base + sizeof( dheader_t ));
	int	i, flags = LUMP_TESTONLY;

	// always reset the intermediate struct
	memset( &loadstat, 0, sizeof( loadstat_t ));

	// store the name to correct show errors and warnings
	Q_strncpy( loadstat.name, name, sizeof( loadstat.name ));
	if( silent )
		SetBits( flags, LUMP_SILENT );

#ifndef SUPPORT_BSP2_FORMAT
	if( header->version == QBSP2_VERSION )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_Printf( S_ERROR DEFAULT_BSP_BUILD_ERROR, name );
		return false;
	}
#endif

	switch( header->version )
	{
	case HLBSP_VERSION:
		if( extrahdr->id == IDEXTRAHEADER )
		{
			SetBits( flags, LUMP_BSP30EXT );
		}
		else
		{
			// only relevant for half-life maps
			int ret = Mod_LumpLooksLikeEntitiesFile( f, &header->lumps[LUMP_ENTITIES], flags, "entities" );
			if( ret < 0 ) return false;
			if( !ret )
			{
				ret = Mod_LumpLooksLikeEntitiesFile( f, &header->lumps[LUMP_PLANES], flags, "planes" );
				if( ret < 0 ) return false;
				if( ret )
				{
					// blue-shift swapped lumps
					*entities = header->lumps[LUMP_PLANES];

					srclumps[0].lumpnumber = LUMP_PLANES;
					srclumps[1].lumpnumber = LUMP_ENTITIES;
					break;
				}
			}
		}
		// intended fallthrough
	case Q1BSP_VERSION:
	case QBSP2_VERSION:
		// everything else
		*entities = header->lumps[LUMP_ENTITIES];

		srclumps[0].lumpnumber = LUMP_ENTITIES;
		srclumps[1].lumpnumber = LUMP_PLANES;
		break;
	default:
		// don't early out: let me analyze errors
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", name, header->version, HLBSP_VERSION );
		loadstat.numerrors++;
		break;
	}

	// loading base lumps
	for( i = 0; i < ARRAYSIZE( srclumps ); i++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[i], flags );

	// loading extralumps
	for( i = 0; i < ARRAYSIZE( extlumps ); i++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[ARRAYSIZE( srclumps ) + i], flags );

	if( loadstat.numerrors )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_Printf( "Mod_LoadWorld: %i error(s), %i warning(s)\n", loadstat.numerrors, loadstat.numwarnings );
		return false; // there were errors, we can't load this map
	}
	else if( loadstat.numwarnings )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_Printf( "Mod_LoadWorld: %i warning(s)\n", loadstat.numwarnings );
	}

	return true;
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	if( loaded ) *loaded = false;

	loadmodel->mempool = Mem_AllocPool( va( "^2%s^7", loadmodel->name ));
	loadmodel->type = mod_brush;

	// loading all the lumps into heap
	if( !Mod_LoadBmodelLumps( buffer, world.loading ))
		return; // there were errors

	if( world.loading ) worldmodel = mod;

	if( loaded ) *loaded = true;	// all done
}

/*
==================
Mod_CheckLump

check lump for existing
==================
*/
int Mod_CheckLump( const char *filename, const int lump, int *lumpsize )
{
	file_t		*f = FS_Open( filename, "rb", false );
	byte		buffer[sizeof( dheader_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );
	dextrahdr_t	*extrahdr;
	dheader_t		*header;

	if( !f ) return LUMP_LOAD_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_HEADER;
	}

	header = (dheader_t *)buffer;

	if( header->version != HLBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_VERSION;
	}

	extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_LOAD_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen <= 0 )
	{
		FS_Close( f );
		return LUMP_LOAD_NOT_EXIST;
	}

	if( lumpsize )
		*lumpsize = extrahdr->lumps[lump].filelen;

	FS_Close( f );

	return LUMP_LOAD_OK;
}

/*
==================
Mod_ReadLump

reading random lump by user request
==================
*/
int Mod_ReadLump( const char *filename, const int lump, void **lumpdata, int *lumpsize )
{
	file_t		*f = FS_Open( filename, "rb", false );
	byte		buffer[sizeof( dheader_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );
	dextrahdr_t	*extrahdr;
	dheader_t		*header;
	byte		*data;
	int		length;

	if( !f ) return LUMP_LOAD_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_HEADER;
	}

	header = (dheader_t *)buffer;

	if( header->version != HLBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_VERSION;
	}

	extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_LOAD_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen <= 0 )
	{
		FS_Close( f );
		return LUMP_LOAD_NOT_EXIST;
	}

	data = malloc( extrahdr->lumps[lump].filelen + 1 );
	length = extrahdr->lumps[lump].filelen;

	if( !data )
	{
		FS_Close( f );
		return LUMP_LOAD_MEM_FAILED;
	}

	FS_Seek( f, extrahdr->lumps[lump].fileofs, SEEK_SET );

	if( FS_Read( f, data, length ) != length )
	{
		free( data );
		FS_Close( f );
		return LUMP_LOAD_CORRUPTED;
	}

	data[length] = 0; // write term
	FS_Close( f );

	if( lumpsize )
		*lumpsize = length;
	*lumpdata = data;

	return LUMP_LOAD_OK;
}

/*
==================
Mod_SaveLump

writing lump by user request
only empty lumps is allows
==================
*/
int Mod_SaveLump( const char *filename, const int lump, void *lumpdata, int lumpsize )
{
	byte		buffer[sizeof( dheader_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );
	int		result, dummy = lumpsize;
	dextrahdr_t	*extrahdr;
	dheader_t		*header;
	file_t		*f;

	if( !lumpdata || lumpsize <= 0 )
		return LUMP_SAVE_NO_DATA;

	// make sure what .bsp is placed into gamedir and not in pak
	if( !FS_GetDiskPath( filename, true ))
		return LUMP_SAVE_COULDNT_OPEN;

	// first we should sure what we allow to rewrite this .bsp
	result = Mod_CheckLump( filename, lump, &dummy );

	if( result != LUMP_LOAD_NOT_EXIST )
		return result;

	f = FS_Open( filename, "e+b", true );

	if( !f ) return LUMP_SAVE_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_SAVE_BAD_HEADER;
	}

	header = (dheader_t *)buffer;

	// these checks below are redundant
	if( header->version != HLBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_SAVE_BAD_VERSION;
	}

	extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_SAVE_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_SAVE_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen != 0 )
	{
		FS_Close( f );
		return LUMP_SAVE_ALREADY_EXIST;
	}

	FS_Seek( f, 0, SEEK_END );

	// will be saved later
	extrahdr->lumps[lump].fileofs = FS_Tell( f );
	extrahdr->lumps[lump].filelen = lumpsize;

	if( FS_Write( f, lumpdata, lumpsize ) != lumpsize )
	{
		FS_Close( f );
		return LUMP_SAVE_CORRUPTED;
	}

	// update the header
	FS_Seek( f, sizeof( dheader_t ), SEEK_SET );

	if( FS_Write( f, extrahdr, sizeof( dextrahdr_t )) != sizeof( dextrahdr_t ))
	{
		FS_Close( f );
		return LUMP_SAVE_CORRUPTED;
	}

	FS_Close( f );
	return LUMP_SAVE_OK;
}
