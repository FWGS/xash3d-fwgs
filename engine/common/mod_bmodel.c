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
#if defined( HAVE_OPENMP )
#include <omp.h>
#endif // HAVE_OPENMP

#define MIPTEX_CUSTOM_PALETTE_SIZE_BYTES ( sizeof( int16_t ) + 768 )

typedef struct leaflist_s
{
	int      count;
	int      maxcount;
	qboolean overflowed;
	int      *list;
	vec3_t   mins, maxs;
	int      topnode; // for overflows where each leaf can't be stored individually
} leaflist_t;

typedef struct
{
	// generic lumps
	dmodel_t   *submodels;
	size_t     numsubmodels;

	dvertex_t  *vertexes;
	size_t     numvertexes;

	dplane_t   *planes;
	size_t     numplanes;

	union
	{
		dnode_t   *nodes;
		dnode32_t *nodes32;
	};
	size_t numnodes;

	union
	{
		dleaf_t   *leafs;
		dleaf32_t *leafs32;
	};
	size_t numleafs;

	union
	{
		dclipnode_t   *clipnodes;
		dclipnode32_t *clipnodes32;
	};
	size_t numclipnodes;

	dtexinfo_t *texinfo;
	size_t     numtexinfo;

	union
	{
		dmarkface_t   *markfaces;
		dmarkface32_t *markfaces32;
	};
	size_t nummarkfaces;

	dsurfedge_t *surfedges;
	size_t      numsurfedges;

	union
	{
		dedge_t   *edges;
		dedge32_t *edges32;
	};
	size_t numedges;

	union
	{
		dface_t   *surfaces;
		dface32_t *surfaces32;
	};
	size_t numsurfaces;

	dfaceinfo_t *faceinfo;
	size_t      numfaceinfo;

	// array lumps
	byte   *visdata;
	size_t visdatasize;

	byte   *lightdata;
	size_t lightdatasize;

	byte   *deluxdata;
	size_t deluxdatasize;

	byte   *shadowdata;
	size_t shadowdatasize;

	byte   *entdata;
	size_t entdatasize;

	byte   *rgblightdata;
	size_t rgblightdatasize;

	// lumps that required personal handler
	dmiptexlump_t *textures;
	size_t        texdatasize;

	// intermediate arrays (pointers will lost after loading, but keep the data)
	color24       *deluxedata_out; // deluxemap data pointer
	byte          *shadowdata_out; // occlusion data pointer
	dclipnode32_t *clipnodes_out;  // temporary 32-bit array to hold clipnodes

	// misc stuff
	int       lightmap_samples; // samples per lightmap (1 or 3)
	int       version;          // model version
	qboolean  isworld;
	qboolean  isbsp30ext;

} dbspmodel_t;

typedef struct
{
	const char *lumpname;
	size_t     entrysize;
	size_t     maxcount;
	size_t     count;
} mlumpstat_t;

typedef struct
{
	char name[64]; // just for debug

	// count errors and warnings
	int numerrors;
	int numwarnings;
} loadstat_t;

#define CHECK_OVERFLOW  BIT( 0 ) // if some of lumps will be overflowed this non fatal for us. But some lumps are critical. mark them

typedef enum
{
	LOADLUMP_STANDARD, // load lump from standard BSP header
	LOADLUMP_BSP30EXT, // ... from BSP30ext header
	LOADLUMP_BSPX,     // ... from BSPX data
} loadlump_source_t;

#define LUMP_SAVESTATS  BIT( 0 )
#define LUMP_TESTONLY   BIT( 1 )
#define LUMP_SILENT     BIT( 2 )
#define LUMP_BSP30EXT   BIT( 3 ) // extra marker for Mod_LoadLump
#define LUMP_BSPX       BIT( 4 )

typedef struct
{
	int          lumpnumber;

	// BSPX
	const char   lumpname[24];

	int          flags;
	const size_t mincount;
	const size_t maxcount;
	const int    entrysize;
	const int    entrysize32; // extended size (0 if not available)
	const char   *loadname;
	const void   **dataptr;
	size_t       *count;
} mlumpinfo_t;

world_static_t     world;
static dbspmodel_t srcmodel;
static loadstat_t  loadstat;
static model_t     *worldmodel;
static byte        g_visdata[(MAX_MAP_LEAFS+7)/8];	// intermediate buffer
static mlumpinfo_t srclumps[HEADER_LUMPS] =
{
	{
		.lumpnumber  = LUMP_ENTITIES,
		.mincount    = 32,
		.maxcount    = MAX_MAP_ENTSTRING,
		.entrysize   = sizeof( byte ),
		.loadname    = "entities",
		.dataptr     = (const void **)&srcmodel.entdata,
		.count       = &srcmodel.entdatasize,
	},
	{
		.lumpnumber  = LUMP_PLANES,
		.mincount    = 1,
		.maxcount    = MAX_MAP_PLANES,
		.entrysize   = sizeof( dplane_t ),
		.loadname    = "planes",
		.dataptr     = (const void **)&srcmodel.planes,
		.count       = &srcmodel.numplanes,
	},
	{
		.lumpnumber  = LUMP_TEXTURES,
		.mincount    = 1,
		.maxcount    = MAX_MAP_MIPTEX,
		.entrysize   = sizeof( byte ),
		.loadname    = "textures",
		.dataptr     = (const void **)&srcmodel.textures,
		.count       = &srcmodel.texdatasize,
	},
	{
		.lumpnumber  = LUMP_VERTEXES,
		.maxcount    = MAX_MAP_VERTS,
		.entrysize   = sizeof( dvertex_t ),
		.loadname    = "vertexes",
		.dataptr     = (const void **)&srcmodel.vertexes,
		.count       = &srcmodel.numvertexes,
	},
	{
		.lumpnumber  = LUMP_VISIBILITY,
		.maxcount    = MAX_MAP_VISIBILITY,
		.entrysize   = sizeof( byte ),
		.loadname    = "visibility",
		.dataptr     = (const void **)&srcmodel.visdata,
		.count       = &srcmodel.visdatasize,
	},
	{
		.lumpnumber  = LUMP_NODES,
		.mincount    = 1,
		.maxcount    = MAX_MAP_NODES,
		.entrysize   = sizeof( dnode_t ),
		.entrysize32 = sizeof( dnode32_t ),
		.loadname    = "nodes",
		.flags       = CHECK_OVERFLOW,
		.dataptr     = (const void **)&srcmodel.nodes,
		.count       = &srcmodel.numnodes,
	},
	{
		.lumpnumber  = LUMP_TEXINFO,
		.mincount    = 0,
		.maxcount    = MAX_MAP_TEXINFO,
		.entrysize   = sizeof( dtexinfo_t ),
		.loadname    = "texinfo",
		.flags       = CHECK_OVERFLOW,
		.dataptr     = (const void **)&srcmodel.texinfo,
		.count       = &srcmodel.numtexinfo,
	},
	{
		.lumpnumber  = LUMP_FACES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_FACES,
		.entrysize   = sizeof( dface_t ),
		.entrysize32 = sizeof( dface32_t ),
		.loadname    = "faces",
		.flags       = CHECK_OVERFLOW,
		.dataptr     = (const void **)&srcmodel.surfaces,
		.count       = &srcmodel.numsurfaces,
	},
	{
		.lumpnumber  = LUMP_LIGHTING,
		.mincount    = 0,
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "lightmaps",
		.flags       = 0,
		.dataptr     = (const void **)&srcmodel.lightdata,
		.count       = &srcmodel.lightdatasize,
	},
	{
		.lumpnumber  = LUMP_CLIPNODES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_CLIPNODES,
		.entrysize   = sizeof( dclipnode_t ),
		.entrysize32 = sizeof( dclipnode32_t ),
		.loadname    = "clipnodes",
		.flags       = 0,
		.dataptr     = (const void **)&srcmodel.clipnodes,
		.count       = &srcmodel.numclipnodes,
	},
	{
		.lumpnumber  = LUMP_LEAFS,
		.mincount    = 1,
		.maxcount    = MAX_MAP_LEAFS,
		.entrysize   = sizeof( dleaf_t ),
		.entrysize32 = sizeof( dleaf32_t ),
		.loadname    = "leafs",
		.flags       = CHECK_OVERFLOW,
		.dataptr     = (const void **)&srcmodel.leafs,
		.count       = &srcmodel.numleafs,
	},
	{
		.lumpnumber  = LUMP_MARKSURFACES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_MARKSURFACES,
		.entrysize   = sizeof( dmarkface_t ),
		.entrysize32 = sizeof( dmarkface32_t ),
		.loadname    = "markfaces",
		.flags       = 0,
		.dataptr     = (const void **)&srcmodel.markfaces,
		.count       = &srcmodel.nummarkfaces,
	},
	{
		.lumpnumber  = LUMP_EDGES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_EDGES,
		.entrysize   = sizeof( dedge_t ),
		.entrysize32 = sizeof( dedge32_t ),
		.loadname    = "edges",
		.flags       = 0,
		.dataptr     = (const void **)&srcmodel.edges,
		.count       = &srcmodel.numedges,
	},
	{
		.lumpnumber  = LUMP_SURFEDGES,
		.mincount    = 0,
		.maxcount    = MAX_MAP_SURFEDGES,
		.entrysize   = sizeof( dsurfedge_t ),
		.loadname    = "surfedges",
		.flags       = 0,
		.dataptr     = (const void **)&srcmodel.surfedges,
		.count       = &srcmodel.numsurfedges,
	},
	{
		.lumpnumber  = LUMP_MODELS,
		.mincount    = 1,
		.maxcount    = MAX_MAP_MODELS,
		.entrysize   = sizeof( dmodel_t ),
		.loadname    = "models",
		.flags       = CHECK_OVERFLOW,
		.dataptr     = (const void **)&srcmodel.submodels,
		.count       = &srcmodel.numsubmodels,
	},
};

static const mlumpinfo_t extlumps[EXTRA_LUMPS] =
{
	{
		.lumpnumber  = LUMP_LIGHTVECS,
		.mincount    = 0,
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "deluxmaps",
		.dataptr     = (const void **)&srcmodel.deluxdata,
		.count       = &srcmodel.deluxdatasize,
	},
	{
		.lumpnumber  = LUMP_FACEINFO,
		.mincount    = 0,
		.maxcount    = MAX_MAP_FACEINFO,
		.entrysize   = sizeof( dfaceinfo_t ),
		.loadname    = "faceinfos",
		.flags       = CHECK_OVERFLOW,
		.dataptr     = (const void **)&srcmodel.faceinfo,
		.count       = &srcmodel.numfaceinfo,
	},
	{
		.lumpnumber  = LUMP_SHADOWMAP,
		.mincount    = 0,
		.maxcount    = MAX_MAP_LIGHTING / 3,
		.entrysize   = sizeof( byte ),
		.loadname    = "shadowmap",
		.dataptr     = (const void **)&srcmodel.shadowdata,
		.count       = &srcmodel.shadowdatasize,
	},
};

static const mlumpinfo_t bspxlumps[] =
{
	{
		.lumpname    = "RGBLIGHTING",
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "rgblighting",
		.dataptr     = (const void **)&srcmodel.rgblightdata,
		.count       = &srcmodel.rgblightdatasize,
	},
	{
		.lumpname    = "LIGHTINGDIR",
		.maxcount    = MAX_MAP_LIGHTING,
		.entrysize   = sizeof( byte ),
		.loadname    = "lightingdir",
		.dataptr     = (const void **)&srcmodel.deluxdata,
		.count       = &srcmodel.deluxdatasize,
	},
};

static mlumpstat_t worldstats[HEADER_LUMPS + EXTRA_LUMPS + ARRAYSIZE( bspxlumps )];

#define BOX_CLIPNODES_INITIALIZER \
	{ \
		.planenum = 0, \
		.children = { CONTENTS_EMPTY, 1 }, \
	}, \
	{ \
		.planenum = 1, \
		.children = { 2, CONTENTS_EMPTY }, \
	}, \
	{ \
		.planenum = 2, \
		.children = { CONTENTS_EMPTY, 3 }, \
	}, \
	{ \
		.planenum = 3, \
		.children = { 4, CONTENTS_EMPTY }, \
	}, \
	{ \
		.planenum = 4, \
		.children = { CONTENTS_EMPTY, 5 }, \
	}, \
	{ \
		.planenum = 5, \
		.children = { CONTENTS_SOLID, CONTENTS_EMPTY }, \
	}, \

const mclipnode16_t box_clipnodes16[6] = { BOX_CLIPNODES_INITIALIZER };
const mclipnode32_t box_clipnodes32[6] = { BOX_CLIPNODES_INITIALIZER };

/*
===============================================================================

			Static helper functions

===============================================================================
*/

static mip_t *Mod_GetMipTexForTexture( dbspmodel_t *bmod, int i )
{
	if( i < 0 || i >= bmod->textures->nummiptex )
		return NULL;

	if( bmod->textures->dataofs[i] == -1 )
		return NULL;

	return (mip_t *)((byte *)bmod->textures + bmod->textures->dataofs[i] );
}

// Returns index of WAD that texture was found in, or -1 if not found.
static int Mod_LoadTextureFromWadList( wadlist_t *list, const char *name, rgbdata_t **pic, char *texpath, size_t texpathlen )
{
	int i;

	if( !list || COM_StringEmptyOrNULL( name ))
		return -1;

	// check wads in reverse order
	for( i = list->count - 1; i >= 0; i-- )
	{
		searchpath_t *sp = NULL;

		while(( sp = g_fsapi.GetArchiveByName( list->wadnames[i], sp )))
		{
			fs_offset_t len;
			byte *buf;
			char file[MAX_VA_STRING];
			int pack_ind;

			Q_snprintf( file, sizeof( file ), "%s.mip", name );
			pack_ind = g_fsapi.FindFileInArchive( sp, file, NULL, 0 );

			if( pack_ind < 0 )
				continue;

			if( texpath != NULL )
				Q_snprintf( texpath, texpathlen, "%s/%s.mip", list->wadnames[i], name );

			if( pic == NULL )
				return i; // dedicated server don't want to load the textures (why?)

			if( !( buf = g_fsapi.LoadFileFromArchive( sp, file, pack_ind, &len, false )))
			{
				*pic = NULL;
				return i; // corrupted file, don't ignore it
			}

			// tell imagelib to directly load this texture to save time
			Q_snprintf( file, sizeof( file ), "#%s/%s.mip", list->wadnames[i], name );
			*pic = FS_LoadImage( file, buf, len );
			Mem_Free( buf );
			return i; // if file is corrupted, it's fine, we want to tell the user about it
		}
	}

	return -1;
}

static fs_offset_t Mod_CalculateMipTexSize( const mip_t *mt, qboolean palette )
{
	if( !mt )
		return 0;

	return sizeof( *mt ) + (( mt->width * mt->height * 85 ) >> 6 ) +
		( palette ? MIPTEX_CUSTOM_PALETTE_SIZE_BYTES : 0 );
}

static qboolean Mod_CalcMipTexUsesCustomPalette( model_t *mod, dbspmodel_t *bmod, int textureIndex )
{
	int nextTextureIndex = 0;
	mip_t *mipTex;
	fs_offset_t size, remainingBytes;

	mipTex = Mod_GetMipTexForTexture( bmod, textureIndex );

	if( !mipTex || mipTex->offsets[0] <= 0 )
		return false;

	// Calculate the size assuming we are not using a custom palette.
	size = Mod_CalculateMipTexSize( mipTex, false );

	// Compute next data offset to determine allocated miptex space
	for( nextTextureIndex = textureIndex + 1; nextTextureIndex < mod->numtextures; nextTextureIndex++ )
	{
		int nextOffset = bmod->textures->dataofs[nextTextureIndex];

		if( nextOffset != -1 )
		{
			remainingBytes = nextOffset - ( bmod->textures->dataofs[textureIndex] + size );
			return remainingBytes >= MIPTEX_CUSTOM_PALETTE_SIZE_BYTES;
		}
	}

	// There was no other miptex after this one.
	// See if there is enough space between the end and our offset.
	remainingBytes = bmod->texdatasize - ( bmod->textures->dataofs[textureIndex] + size );
	return remainingBytes >= MIPTEX_CUSTOM_PALETTE_SIZE_BYTES;
}

static qboolean Mod_NameImpliesTextureIsAnimated( texture_t *tex )
{
	if( !tex )
		return false;

	// Not an animated texture name
	if( tex->name[0] != '-' && tex->name[0] != '+' )
		return false;

	// Name implies texture is animated - check second character is valid.
	if( !( tex->name[1] >= '0' && tex->name[1] <= '9' ) &&
		!( tex->name[1] >= 'a' && tex->name[1] <= 'j' ))
	{
		Con_Printf( S_ERROR "%s: animating texture \"%s\" has invalid name\n", __func__, tex->name );
		return false;
	}

	return true;
}

static void Mod_CreateDefaultTexture( model_t *mod, texture_t **texture )
{
	texture_t *tex;

	// Pointer must be valid, and value pointed to must be null.
	if( !texture || *texture != NULL )
		return;

	*texture = tex = Mem_Calloc( mod->mempool, sizeof( *tex ));
	Q_strncpy( tex->name, REF_DEFAULT_TEXTURE, sizeof( tex->name ));

#if !XASH_DEDICATED
	if( !Host_IsDedicated( ))
	{
		tex->gl_texturenum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );
		tex->width = 16;
		tex->height = 16;
	}
#endif // XASH_DEDICATED
}

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
static void Mod_LoadLump( const void *in, const mlumpinfo_t *info, mlumpstat_t *stat, int flags, loadlump_source_t source, const void *bspx_data )
{
	int     version = ((const dheader_t *)in)->version, i;
	size_t  numelems, real_entrysize;
	dlump_t l = { 0 };

	switch( source )
	{
	case LOADLUMP_STANDARD:
	{
		const dheader_t *header = in;
		l = header->lumps[info->lumpnumber];
		break;
	}
	case LOADLUMP_BSP30EXT:
	{
		const dextrahdr_t *header = (const dextrahdr_t *)((const byte *)in + sizeof( dheader_t ));
		if( header->id != IDEXTRAHEADER || header->version != EXTRA_VERSION )
			return;
		l = header->lumps[info->lumpnumber];
		break;
	}
	case LOADLUMP_BSPX:
	{
		if( !bspx_data )
			return;

		const dbspx_hdr_t *header = bspx_data;

		if( header->id != IDBSPXHEADER )
			return;

		for( i = 0; i < header->numlumps; i++ )
		{
			if( !Q_strcmp( info->lumpname, header->lumps[i].lumpname ))
			{
				l.fileofs = header->lumps[i].fileofs;
				l.filelen = header->lumps[i].filelen;
				break;
			}
		}

		if( i == header->numlumps )
			return;
	}
	}

	// lump is unused by engine for some reasons ?
	if( !l.fileofs || info->entrysize <= 0 || info->maxcount <= 0 )
		return;

	real_entrysize = info->entrysize; // default

	// analyze real entrysize
	switch( version )
	{
	case HLBSP_VERSION:
		if( FBitSet( flags, LUMP_BSP30EXT ) && info->lumpnumber == LUMP_CLIPNODES )
		{
			// if this map is bsp30ext, try to guess extended clipnodes
			if((( l.filelen % info->entrysize ) || ( l.filelen / info->entrysize32 ) >= MAX_MAP_CLIPNODES_HLBSP ))
			{
				real_entrysize = info->entrysize32;
			}
		}
		break;
	case QBSP2_VERSION:
		if( info->entrysize32 > 0 )
		{
			// always use alternate entrysize for BSP2
			real_entrysize = info->entrysize32;
		}
		break;
	default:
		break;
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
			stat->count = l.filelen / real_entrysize;
	}

	// lump is not present
	if( l.filelen <= 0 )
	{
		// don't warn about extra lumps - it's optional
		if( source == LOADLUMP_STANDARD )
		{
			// some data array that may be optional
			if( real_entrysize == sizeof( byte ))
			{
				if( !FBitSet( flags, LUMP_SILENT ))
				{
					Con_DPrintf( S_WARN "map ^2%s^7 has no %s\n", loadstat.name, info->loadname );
					loadstat.numwarnings++;
				}
			}
			else if( info->mincount > 0 )
			{
				// it has the mincount and the lump is completely missed!
				if( !FBitSet( flags, LUMP_SILENT ))
					Con_DPrintf( S_ERROR "map ^2%s^7 has no %s\n", loadstat.name, info->loadname );
				loadstat.numerrors++;
			}
		}
		return;
	}

	if( l.filelen % real_entrysize )
	{
		if( !FBitSet( flags, LUMP_SILENT ))
		{
			Con_DPrintf( S_ERROR "Mod_Load%c%s: Lump size %d was not a multiple of %zu bytes\n", toupper( info->loadname[0] ), &info->loadname[1], l.filelen, real_entrysize );
		}
		loadstat.numerrors++;
		return;
	}

	numelems = l.filelen / real_entrysize;

	if( numelems < info->mincount )
	{
		// it has the mincount and it's smaller than this limit
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_DPrintf( S_ERROR "map ^2%s^7 has no %s\n", loadstat.name, info->loadname );
		loadstat.numerrors++;
		return;
	}

	if( numelems > info->maxcount )
	{
		// it has the maxcount and it's overflowed
		if( FBitSet( info->flags, CHECK_OVERFLOW ))
		{
			if( !FBitSet( flags, LUMP_SILENT ))
				Con_DPrintf( S_ERROR "map ^2%s^7 has too many %s\n", loadstat.name, info->loadname );
			loadstat.numerrors++;
			return;
		}
		else if( !FBitSet( flags, LUMP_SILENT ))
		{
			// just throw warning
			Con_DPrintf( S_WARN "map ^2%s^7 has too many %s\n", loadstat.name, info->loadname );
			loadstat.numwarnings++;
		}
	}

	if( FBitSet( flags, LUMP_TESTONLY ))
		return; // don't fill the intermediate struct

	// all checks are passed, store pointers
	if( info->dataptr )
		*info->dataptr = (void *)((byte *)in + l.fileofs);

	if( info->count )
		*info->count = numelems;
}

/*
================
Mod_ArrayUsage
================
*/
static int Mod_ArrayUsage( const char *szItem, int items, int maxitems, int itemsize )
{
	float	percentage = maxitems ? (items * 100.0f / maxitems) : 0.0f;

	Con_Printf( "%-12s  %7i/%-7i  %8i/%-8i  (%4.1f%%) %s\n",
		szItem, items, maxitems, items * itemsize, maxitems * itemsize, percentage,
		percentage > 99.99f ? "^1SIZE OVERFLOW!!!^7" :
		percentage > 95.0f ? "^3SIZE DANGER!^7" :
		percentage > 80.0f ? "^2VERY FULL!^7" :
		"" );

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

	Con_Printf( "%-15s  %-12s  %8i/%-8i  (%4.1f%%) %s\n",
		szItem, "[variable]", itemstorage, maxstorage, percentage,
		percentage > 99.99f ? "^1SIZE OVERFLOW!!!^7" :
		percentage > 95.0f ? "^3SIZE DANGER!^7" :
		percentage > 80.0f ? "^2VERY FULL!^7" :
		"" );

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
		else
			totalmemory += Mod_ArrayUsage( stat->lumpname, stat->count, stat->maxcount, stat->entrysize );
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
static void Mod_DecompressPVS( byte *const out, const byte *in, size_t visbytes )
{
	byte *dst = out;

	if( !in ) // no visinfo, make all visible
	{
		memset( out, 0xFF, visbytes );
		return;
	}

	while( dst < out + visbytes )
	{
		if( *in ) // uncompressed
		{
			*dst++ = *in++;
		}
		else // zero repeated `c` times
		{
			size_t c = in[1];
			if( c > out + visbytes - dst )
				c = out + visbytes - dst;

			memset( dst, 0, c );
			in += 2;
			dst += c;
		}
	}
}

static size_t Mod_CompressPVS( byte *const out, const byte *in, size_t inbytes )
{
	size_t i;
	byte *dst = out;

	for( i = 0; i < inbytes; i++ )
	{
		size_t j = i + 1, rep = 1;

		*dst++ = in[i];

		// only compress zeros
		if( in[i] )
			continue;

		for( ; j < inbytes && rep != 255; j++, rep++ )
		{
			if( in[j] )
				break;
		}

		*dst++ = rep;
		i = j - 1;
	}

	return dst - out;
}

/*
==================
Mod_PointInLeaf

==================
*/
mleaf_t *Mod_PointInLeaf( const vec3_t p, mnode_t *node, model_t *mod )
{
	Assert( node != NULL );

	while( 1 )
	{
		if( node->contents < 0 )
			return (mleaf_t *)node;
		node = node_child( node, PlaneDiff( p, node->plane ) <= 0, mod );
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
	mleaf_t	*leaf;

	ASSERT( worldmodel != NULL );

	leaf = Mod_PointInLeaf( p, worldmodel->nodes, worldmodel );

	if( leaf && leaf->cluster >= 0 )
	{
		Mod_DecompressPVS( g_visdata, leaf->compressed_vis, world.visbytes );
		return g_visdata;
	}

	return NULL;
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

==================
*/
static void Mod_FatPVS_RecursiveBSPNode( const vec3_t org, float radius, byte *visbuffer, int visbytes, mnode_t *node, qboolean phs )
{
	while( node->contents >= 0 )
	{
		float d = PlaneDiff( org, node->plane );

		if( d > radius )
			node = node_child( node, 0, worldmodel );
		else if( d < -radius )
			node = node_child( node, 1, worldmodel );
		else
		{
			// go down both sides
			Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, visbytes, node_child( node, 0, worldmodel ), phs );
			node = node_child( node, 1, worldmodel );
		}
	}

	// if this leaf is in a cluster, accumulate the vis bits
	if(((mleaf_t *)node)->cluster >= 0 )
	{
		if( phs )
		{
			int i = ((mleaf_t *)node)->cluster + 1;
			Mod_DecompressPVS( g_visdata, &world.compressed_phs[world.phsofs[i]], world.visbytes );
		}
		else
		{
			Mod_DecompressPVS( g_visdata, ((mleaf_t *)node)->compressed_vis, world.visbytes );
		}

		Q_memor( visbuffer, g_visdata, visbytes );
	}
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

Calculates a PVS that is the inclusive or of all leafs
within radius pixels of the given point.
==================
*/
int Mod_FatPVS( const vec3_t org, float radius, byte *visbuffer, int visbytes, qboolean merge, qboolean fullvis, qboolean phs )
{
	int	bytes = world.visbytes;
	mleaf_t	*leaf = NULL;

	ASSERT( worldmodel != NULL );

	leaf = Mod_PointInLeaf( org, worldmodel->nodes, worldmodel );
	bytes = Q_min( bytes, visbytes );

	// enable full visibility for some reasons
	if( fullvis || !worldmodel->visdata || !leaf || leaf->cluster < 0 )
	{
		memset( visbuffer, 0xFF, bytes );
		return bytes;
	}

	// requested PHS but we don't have PHS for some reason
	// enable full visibility
	if( phs && !( world.compressed_phs && world.phsofs ))
	{
		memset( visbuffer, 0xFF, bytes );
		return bytes;
	}

	if( !merge ) memset( visbuffer, 0x00, bytes );

	Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, bytes, worldmodel->nodes, phs );

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
			node = node_child( node, 0, worldmodel );
		else if( sides == 2 )
			node = node_child( node, 1, worldmodel );
		else
		{
			// go down both
			if( ll->topnode == -1 )
				ll->topnode = node - worldmodel->nodes;
			Mod_BoxLeafnums_r( ll, node_child( node, 0, worldmodel ));
			node = node_child( node, 1, worldmodel );
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

	if( !entities || COM_StringEmptyOrNULL( modelname ))
		return;

	if( !origin || !VectorIsNull( origin ))
		return;

	pfile = (char *)entities;

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( token[0] != '{' )
			Host_Error( "%s: found %s when expecting {\n", __func__, token );

		model_found = origin_found = false;
		VectorClear( origin );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "%s: EOF without closing brace\n", __func__ );
			if( token[0] == '}' ) break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "%s: EOF without closing brace\n", __func__ );

			if( token[0] == '}' )
				Host_Error( "%s: closing brace without data\n", __func__ );

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
static qboolean Mod_CheckWaterAlphaSupport( model_t *mod, dbspmodel_t *bmod )
{
	mleaf_t *leaf;
	int     i, j;

	if( bmod->visdatasize <= 0 )
		return true;

	// check all liquid leafs to see if they can see into empty leafs, if any
	// can we can assume this map supports r_wateralpha
	for( i = 0, leaf = mod->leafs; i < mod->numleafs; i++, leaf++ )
	{
		if(( leaf->contents == CONTENTS_WATER || leaf->contents == CONTENTS_SLIME ) && leaf->cluster >= 0 )
		{
			Mod_DecompressPVS( g_visdata, leaf->compressed_vis, world.visbytes );

			for( j = 0; j < mod->numleafs; j++ )
			{
				if( CHECKVISBIT( g_visdata, mod->leafs[j].cluster ) && mod->leafs[j].contents == CONTENTS_EMPTY )
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
int Mod_SampleSizeForFace( const msurface_t *surf )
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
static mvertex_t *Mod_GetVertexByNumber( model_t *mod, int surfedge, const dbspmodel_t *bmod )
{
	int	lindex = mod->surfedges[surfedge];

	if( bmod->version == QBSP2_VERSION )
	{
		if( lindex > 0 )
		{
			medge32_t *edge = &mod->edges32[lindex];
			return &mod->vertexes[edge->v[0]];
		}
		else
		{
			medge32_t *edge = &mod->edges32[-lindex];
			return &mod->vertexes[edge->v[1]];
		}
	}
	else
	{
		if( lindex > 0 )
		{
			medge16_t *edge = &mod->edges16[lindex];
			return &mod->vertexes[edge->v[0]];
		}
		else
		{
			medge16_t *edge = &mod->edges16[-lindex];
			return &mod->vertexes[edge->v[1]];
		}
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
static void Mod_CalcSurfaceExtents( model_t *mod, msurface_t *surf, const dbspmodel_t *bmod )
{
	// this place is VERY critical to precision
	// keep it as float, don't use double, because it causes issues with lightmap
	float		mins[2], maxs[2], val;
	float		lmmins[2], lmmaxs[2];
	int		bmins[2], bmaxs[2];
	int		i, j, e, sample_size;
	mextrasurf_t	*info = surf->info;
	mtexinfo_t	*tex;
	mvertex_t		*v;

	sample_size = Mod_SampleSizeForFace( surf );
	tex = surf->texinfo;

	Mod_LightMatrixFromTexMatrix( tex, info->lmvecs );

	mins[0] = lmmins[0] = mins[1] = lmmins[1] = 999999;
	maxs[0] = lmmaxs[0] = maxs[1] = lmmaxs[1] =-999999;

	for( i = 0; i < surf->numedges; i++ )
	{
		e = mod->surfedges[surf->firstedge + i];

		if( e >= mod->numedges || e <= -mod->numedges )
			Host_Error( "%s: bad edge\n", __func__ );

		if( bmod->version == QBSP2_VERSION )
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges32[e].v[0]];
			else v = &mod->vertexes[mod->edges32[-e].v[1]];
		}
		else
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges16[e].v[0]];
			else v = &mod->vertexes[mod->edges16[-e].v[1]];
		}

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
static void Mod_CalcSurfaceBounds( model_t *mod, msurface_t *surf, const dbspmodel_t *bmod )
{
	int	i, e;
	mvertex_t	*v;

	ClearBounds( surf->info->mins, surf->info->maxs );

	for( i = 0; i < surf->numedges; i++ )
	{
		e = mod->surfedges[surf->firstedge + i];

		if( e >= mod->numedges || e <= -mod->numedges )
			Host_Error( "%s: bad edge\n", __func__ );

		if( bmod->version == QBSP2_VERSION )
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges32[e].v[0]];
			else v = &mod->vertexes[mod->edges32[-e].v[1]];
		}
		else
		{
			if( e >= 0 ) v = &mod->vertexes[mod->edges16[e].v[0]];
			else v = &mod->vertexes[mod->edges16[-e].v[1]];
		}
		AddPointToBounds( v->position, surf->info->mins, surf->info->maxs );
	}

	VectorAverage( surf->info->mins, surf->info->maxs, surf->info->origin );
}

/*
=================
Mod_CreateFaceBevels
=================
*/
static void Mod_CreateFaceBevels( model_t *mod, msurface_t *surf, const dbspmodel_t *bmod )
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
	facebevel = (byte *)Mem_Calloc( mod->mempool, size );
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

		v0 = Mod_GetVertexByNumber( mod, surf->firstedge + i, bmod );
		v1 = Mod_GetVertexByNumber( mod, surf->firstedge + (i + 1) % surf->numedges, bmod );
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
		v0 = Mod_GetVertexByNumber( mod, surf->firstedge + i, bmod );
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
static void Mod_SetParent( model_t *mod, mnode_t *node, mnode_t *parent )
{
	node->parent = parent;

	if( node->contents < 0 )
		return; // it's leaf

	Mod_SetParent( mod, node_child( node, 0, mod ), node );
	Mod_SetParent( mod, node_child( node, 1, mod ), node );
}

/*
==================
CountClipNodes_r
==================
*/
static void CountClipNodes16_r( mclipnode16_t *src, hull_t *hull, int nodenum )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == MAX_MAP_CLIPNODES_HLBSP )
		Host_Error( "%s: MAX_MAP_CLIPNODES_HLBSP limit exceeded\n", __func__ );
	hull->lastclipnode++;

	CountClipNodes16_r( src, hull, src[nodenum].children[0] );
	CountClipNodes16_r( src, hull, src[nodenum].children[1] );
}

static void CountClipNodes32_r( mclipnode32_t *src, hull_t *hull, int nodenum )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == MAX_MAP_CLIPNODES_BSP2 )
		Host_Error( "%s: MAX_MAP_CLIPNODES_BSP2 limit exceeded\n", __func__ );
	hull->lastclipnode++;

	CountClipNodes32_r( src, hull, src[nodenum].children[0] );
	CountClipNodes32_r( src, hull, src[nodenum].children[1] );
}

static void CountDClipNodes_r( dclipnode32_t *src, hull_t *hull, int nodenum, const int max_clipnodes )
{
	// leaf?
	if( nodenum < 0 ) return;

	if( hull->lastclipnode == max_clipnodes )
		Host_Error( "%s: MAX_MAP_CLIPNODES (%d) limit exceeded\n", __func__, max_clipnodes );
	hull->lastclipnode++;

	CountDClipNodes_r( src, hull, src[nodenum].children[0], max_clipnodes );
	CountDClipNodes_r( src, hull, src[nodenum].children[1], max_clipnodes );
}

/*
==================
RemapClipNodes_r
==================
*/
static int RemapClipNodes_r( dbspmodel_t *bmod, dclipnode32_t *srcnodes, hull_t *hull, int nodenum )
{
	dclipnode32_t *src;
	int		i, c;

	// leaf?
	if( nodenum < 0 )
		return nodenum;

	// emit a clipnode
	if( bmod->version == QBSP2_VERSION )
	{
		if( hull->lastclipnode == MAX_MAP_CLIPNODES_BSP2 )
			Host_Error( "%s: MAX_MAP_CLIPNODES_BSP2 limit exceeded\n", __func__ );
	}
	else
	{
		if( hull->lastclipnode == MAX_MAP_CLIPNODES_HLBSP )
			Host_Error( "%s: MAX_MAP_CLIPNODES_HLBSP limit exceeded\n", __func__ );
	}

	src = srcnodes + nodenum;

	c = hull->lastclipnode;
	hull->lastclipnode++;

	if( bmod->version == QBSP2_VERSION )
	{
		mclipnode32_t *out = &hull->clipnodes32[c];
		out->planenum = src->planenum;
		for( i = 0; i < 2; i++ )
			out->children[i] = RemapClipNodes_r( bmod, srcnodes, hull, src->children[i] );
	}
	else
	{
		mclipnode16_t *out = &hull->clipnodes16[c];
		out->planenum = src->planenum;
		for( i = 0; i < 2; i++ )
			out->children[i] = RemapClipNodes_r( bmod, srcnodes, hull, src->children[i] );
	}

	return c;
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0( model_t *mod, const dbspmodel_t *bmod )
{
	hull_t *hull = &mod->hulls[0];
	int i;

	hull->firstclipnode = 0;
	hull->lastclipnode = mod->numnodes - 1;
	hull->planes = mod->planes;

	if( bmod->version == QBSP2_VERSION )
	{
		mclipnode32_t *out;
		mnode_t *in = mod->nodes;

		hull->clipnodes32 = out = Mem_Malloc( mod->mempool, mod->numnodes * sizeof( *hull->clipnodes32 ));

		for( i = 0; i < mod->numnodes; i++, out++, in++ )
		{
			int j;

			out->planenum = in->plane - mod->planes;

			for( j = 0; j < 2; j++ )
			{
				mnode_t *child = node_child( in, j, mod );

				if( child->contents < 0 )
					out->children[j] = child->contents;
				else
					out->children[j] = child - mod->nodes;
			}
		}
	}
	else
	{
		mclipnode16_t *out;
		mnode_t *in = mod->nodes;

		hull->clipnodes16 = out = Mem_Malloc( mod->mempool, mod->numnodes * sizeof( *hull->clipnodes16 ));

		for( i = 0; i < mod->numnodes; i++, out++, in++ )
		{
			int j;

			out->planenum = in->plane - mod->planes;

			for( j = 0; j < 2; j++ )
			{
				mnode_t *child = node_child( in, j, mod );

				if( child->contents < 0 )
					out->children[j] = child->contents;
				else
					out->children[j] = child - mod->nodes;
			}
		}
	}

}

/*
=================
Mod_SetupHull
=================
*/
static void Mod_SetupHull( dbspmodel_t *bmod, model_t *mod, int headnode, int hullnum, model_t *world )
{
	hull_t *hull = &mod->hulls[hullnum];

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
		Host_Error( "%s: bad hull number %i\n", __func__, hullnum );
		break;
	}

	if( VectorIsNull( hull->clip_mins ) && VectorIsNull( hull->clip_maxs ))
		return;	// no hull specified

	// assume no hull
	hull->firstclipnode = hull->lastclipnode = 0;
	hull->planes = NULL; // hull is missed

	if( headnode >= mod->numclipnodes )
		return;	// ZHLT weird empty hulls

	// bsp30ext allows for extended total amount of clipnodes, but the limit is still 16-bit per submodel
	// therefore we need to remap them
	// take a simpler route if we don't need clipnodes remapping
	if( !bmod->isbsp30ext )
	{
		hull->planes = mod->planes;

		// some map "optimizers" (you know who you are!) put -1 here
		// ... and it's purposefully? encode CONTENTS_EMPTY sometimes
		// but might cause out of bounds reads
		hull->firstclipnode = headnode;
		hull->lastclipnode = mod->numclipnodes - 1;

		// only allocate clipnodes array for the base model, only for first hull
		if( mod == world && hullnum == 1 )
		{
			int i;

			if( bmod->version == QBSP2_VERSION )
			{
				hull->clipnodes32 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes32 ) * mod->numclipnodes );

				for( i = 0; i < mod->numclipnodes; i++ )
				{
					hull->clipnodes32[i].planenum = bmod->clipnodes_out[i].planenum;
					hull->clipnodes32[i].children[0] = bmod->clipnodes_out[i].children[0];
					hull->clipnodes32[i].children[1] = bmod->clipnodes_out[i].children[1];
				}
			}
			else
			{
				hull->clipnodes16 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes16 ) * mod->numclipnodes );

				for( i = 0; i < mod->numclipnodes; i++ )
				{
					hull->clipnodes16[i].planenum = bmod->clipnodes_out[i].planenum;
					hull->clipnodes16[i].children[0] = bmod->clipnodes_out[i].children[0];
					hull->clipnodes16[i].children[1] = bmod->clipnodes_out[i].children[1];
				}
			}
		}
		else
		{
			if( bmod->version == QBSP2_VERSION )
				hull->clipnodes32 = world->hulls[1].clipnodes32;
			else
				hull->clipnodes16 = world->hulls[1].clipnodes16;
		}

		return;
	}

	if(( headnode == -1 ) || ( hullnum != 1 && headnode == 0 ))
		return; // hull missed

	// fit array to real count
	if( bmod->version == QBSP2_VERSION )
	{
		CountDClipNodes_r( bmod->clipnodes_out, hull, headnode, MAX_MAP_CLIPNODES_BSP2 );
		hull->clipnodes32 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes32 ) * hull->lastclipnode );
	}
	else
	{
		CountDClipNodes_r( bmod->clipnodes_out, hull, headnode, MAX_MAP_CLIPNODES_HLBSP );
		hull->clipnodes16 = Mem_Malloc( world->mempool, sizeof( *hull->clipnodes16 ) * hull->lastclipnode );
	}

	hull->planes = mod->planes; // share planes
	hull->lastclipnode = 0; // restart counting

	RemapClipNodes_r( bmod, bmod->clipnodes_out, hull, headnode ); // remap clipnodes to 16-bit indexes
}

static qboolean Mod_LoadLitfile( model_t *mod, const char *ext, size_t expected_size, color24 **out, size_t *outsize )
{
	char        modelname[64], path[64];
	int         iCompare;
	fs_offset_t datasize;
	file_t      *f;
	uint        hdr[2];

	COM_FileBase( mod->name, modelname, sizeof( modelname ));
	Q_snprintf( path, sizeof( path ), "maps/%s.%s", modelname, ext );

	if( !pfnCompareFileTime( path, mod->name, &iCompare ))
		return false;

	if( iCompare < 0 ) // this may happens if level-designer used -onlyents key for hlcsg
		Con_Printf( S_WARN "%s probably is out of date\n", path );

	f = FS_Open( path, "rb", false );

	if( !f )
	{
		Con_Printf( S_ERROR "couldn't load %s\n", path );
		return false;
	}

	datasize = FS_FileLength( f );

	// skip header bytes
	datasize -= 8;

	if( datasize != expected_size )
	{
		Con_Printf( S_ERROR "%s has mismatched size (%li should be %zu)\n", path, (long)datasize, expected_size );
		goto cleanup_and_error;
	}

	if( FS_Read( f, hdr, sizeof( hdr )) != sizeof( hdr ))
	{
		Con_Printf( S_ERROR "failed reading header from %s\n", path );
		goto cleanup_and_error;
	}

	if( LittleLong( hdr[0] ) != IDDELUXEMAPHEADER )
	{
		Con_Printf( S_ERROR "%s is corrupted\n", path );
		goto cleanup_and_error;
	}

	if( LittleLong( hdr[1] ) != DELUXEMAP_VERSION )
	{
		Con_Printf( S_ERROR "has %s mismatched version (%u should be %u)\n", path, LittleLong( hdr[1] ), DELUXEMAP_VERSION );
		goto cleanup_and_error;
	}

	*out = Mem_Malloc( mod->mempool, datasize );
	*outsize = datasize;

	FS_Read( f, *out, datasize );
	FS_Close( f );
	return true;

cleanup_and_error:
	FS_Close( f );
	return false;
}

/*
=================
Mod_SetupSubmodels

duplicate the basic information
for embedded submodels
=================
*/
static void Mod_SetupSubmodels( model_t *mod, dbspmodel_t *bmod )
{
	const qboolean colored = FBitSet( mod->flags, MODEL_COLORED_LIGHTING ) ? true : false;
	const qboolean qbsp2 = FBitSet( mod->flags, MODEL_QBSP2 ) ? true : false;
	const char *name = mod->name;
	model_t *world = mod; // submodels might want to share hulls
	int	i;

	mod->numframes = 2;	// regular and alternate animation

	// set up the submodels
	for( i = 0; i < mod->numsubmodels; i++ )
	{
		dmodel_t *bm = &mod->submodels[i];
		int j;

		// hull 0 is just shared across all bmodels
		mod->hulls[0].firstclipnode = bm->headnode[0];
		mod->hulls[0].lastclipnode = bm->headnode[0]; // need to be real count

		// counting a real number of clipnodes per each submodel
		if( bmod->version == QBSP2_VERSION )
			CountClipNodes32_r( mod->hulls[0].clipnodes32, &mod->hulls[0], bm->headnode[0] );
		else
			CountClipNodes16_r( mod->hulls[0].clipnodes16, &mod->hulls[0], bm->headnode[0] );

		// but hulls1-3 is build individually for a each given submodel
		for( j = 1; j < MAX_MAP_HULLS; j++ )
			Mod_SetupHull( bmod, mod, bm->headnode[j], j, world );

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy( bm->mins, mod->mins );
		VectorCopy( bm->maxs, mod->maxs );

		mod->radius = RadiusFromBounds( mod->mins, mod->maxs );
		mod->numleafs = bm->visleafs;
		mod->flags = 0;

		// this bit will be shared between all the submodels include worldmodel
		if( colored ) SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		if( qbsp2 ) SetBits( mod->flags, MODEL_QBSP2 );

		if( i != 0 )
		{
			char temp[MAX_VA_STRING];

			Q_snprintf( temp, sizeof( temp ), "*%i", i );
			Mod_FindModelOrigin( world->entities, temp, bm->origin );

			// mark models that have origin brushes
			if( !VectorIsNull( bm->origin ))
				SetBits( mod->flags, MODEL_HAS_ORIGIN );
#ifdef HACKS_RELATED_HLMODS
			// c2a1 doesn't have origin brush it's just placed at center of the level
			if( !Q_stricmp( name, "maps/c2a1.bsp" ) && ( i == 11 ))
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
			model_t *submod;

			// duplicate the basic information
			Q_snprintf( name, sizeof( name ), "*%i", i + 1 );
			submod = Mod_FindName( name, true );
			*submod = *mod;
			Q_strncpy( submod->name, name, sizeof( submod->name ));
			submod->mempool = 0;
			mod = submod;
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
static void Mod_LoadSubmodels( model_t *mod, dbspmodel_t *bmod )
{
	dmodel_t	*in, *out;
	int	oldmaxfaces;
	int	i, j;

	// allocate extradata for each dmodel_t
	out = Mem_Malloc( mod->mempool, bmod->numsubmodels * sizeof( *out ));

	mod->numsubmodels = bmod->numsubmodels;
	mod->submodels = out;
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

static int Mod_LoadEntities_splitstr_handler( char *prev, char *next, void *userdata )
{
	const char *wad;
	wadlist_t *wadlist = userdata;

	*next = '\0';

	if( COM_StringEmpty( prev ))
		return 0;

	COM_FixSlashes( prev );
	wad = COM_FileWithoutPath( prev );

	if( Q_stricmp( COM_FileExtension( wad ), "wad" ))
		return 0;

	// make sure that wad is really exist
	if( FS_FileExists( wad, false ))
	{
		int num = wadlist->count++;
		Q_strncpy( wadlist->wadnames[num], wad, sizeof( wadlist->wadnames[0] ));
		wadlist->wadusage[num] = 0;
	}

	if( wadlist->count >= ARRAYSIZE( wadlist->wadnames ))
		return 1;

	return 0;
}

/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities( model_t *mod, dbspmodel_t *bmod )
{
	byte   *entpatch = NULL;
	char   token[MAX_TOKEN];
	string keyname;
	char   *pfile;

	if( bmod->isworld )
	{
		char        entfilename[MAX_QPATH];
		fs_offset_t	entpatchsize;
		int         ft1, ft2;

		// if world check for entfile too
		Q_strncpy( entfilename, mod->name, sizeof( entfilename ));
		COM_ReplaceExtension( entfilename, ".ent", sizeof( entfilename ));

		// make sure that entity patch is never than bsp
		ft1 = FS_FileTime( mod->name, false );
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

	// make sure that we really have null terminator
	mod->entities = Mem_Malloc( mod->mempool, bmod->entdatasize + 1 );
	memcpy( mod->entities, bmod->entdata, bmod->entdatasize ); // moving to private model pool
	mod->entities[bmod->entdatasize] = 0;

	if( entpatch )
	{
		Mem_Free( entpatch ); // release entpatch if present
		entpatch = NULL;
	}

	if( !bmod->isworld )
		return;

	pfile = (char *)mod->entities;
	world.generator[0] = '\0';
	world.compiler[0] = '\0';
	world.message[0] = '\0';
	world.wadlist.count = 0;

	// parse all the wads for loading textures in right ordering
	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( token[0] != '{' )
			Host_Error( "%s: found %s when expecting {\n", __func__, token );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "%s: EOF without closing brace\n", __func__ );

			if( token[0] == '}' )
				break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value
			if(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) == NULL )
				Host_Error( "%s: EOF without closing brace\n", __func__ );

			if( token[0] == '}' )
				Host_Error( "%s: closing brace without data\n", __func__ );

			if( !Q_stricmp( keyname, "wad" ))
				Q_splitstr( token, ';', &world.wadlist, Mod_LoadEntities_splitstr_handler );
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
static void Mod_LoadPlanes( model_t *mod, dbspmodel_t *bmod )
{
	dplane_t	*in;
	mplane_t	*out;
	int	i, j;

	in = bmod->planes;
	mod->planes = out = Mem_Malloc( mod->mempool, bmod->numplanes * sizeof( *out ));
	mod->numplanes = bmod->numplanes;

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
static void Mod_LoadVertexes( model_t *mod, dbspmodel_t *bmod )
{
	dvertex_t	*in;
	mvertex_t	*out;
	int	i;

	in = bmod->vertexes;
	out = mod->vertexes = Mem_Malloc( mod->mempool, bmod->numvertexes * sizeof( mvertex_t ));
	mod->numvertexes = bmod->numvertexes;

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
static void Mod_LoadEdges( model_t *mod, dbspmodel_t *bmod )
{
	int	i;

	mod->numedges = bmod->numedges;

	if( bmod->version == QBSP2_VERSION )
	{
		dedge32_t *in = bmod->edges32;
		medge32_t *out;
		mod->edges32 = out = Mem_Malloc( mod->mempool, bmod->numedges * sizeof( *out ));

		for( i = 0; i < bmod->numedges; i++, in++, out++ )
		{
			out->v[0] = in->v[0];
			out->v[1] = in->v[1];
		}
	}
	else
	{
		dedge_t	*in = bmod->edges;
		medge16_t *out;
		mod->edges16 = out = Mem_Malloc( mod->mempool, bmod->numedges * sizeof( *out ));

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
static void Mod_LoadSurfEdges( model_t *mod, dbspmodel_t *bmod )
{
	mod->surfedges = Mem_Malloc( mod->mempool, bmod->numsurfedges * sizeof( dsurfedge_t ));
	memcpy( mod->surfedges, bmod->surfedges, bmod->numsurfedges * sizeof( dsurfedge_t ));
	mod->numsurfedges = bmod->numsurfedges;
}

/*
=================
Mod_LoadMarkSurfaces
=================
*/
static void Mod_LoadMarkSurfaces( model_t *mod, dbspmodel_t *bmod )
{
	msurface_t	**out;
	int		i;

	mod->marksurfaces = out = Mem_Malloc( mod->mempool, bmod->nummarkfaces * sizeof( *out ));
	mod->nummarksurfaces = bmod->nummarkfaces;

	if( bmod->version == QBSP2_VERSION )
	{
		const dmarkface32_t *in = bmod->markfaces32;

		for( i = 0; i < bmod->nummarkfaces; i++ )
		{
			if( in[i] < 0 || in[i] >= mod->numsurfaces )
				Host_Error( "%s: bad surface number %i at %i (max %i) in '%s'\n", __func__, in[i], i, mod->numsurfaces, mod->name );
			out[i] = mod->surfaces + in[i];
		}
	}
	else
	{
		const dmarkface_t *in = bmod->markfaces;

		for( i = 0; i < bmod->nummarkfaces; i++ )
		{
			// NOTE: some of the buggy compilers have written a broken BSP file
			// with marksurface pointing at negative surface, for example darkf6.bsp
			// and darkf26.bsp in darkfuture mod. GoldSrc straight up writes
			// invalid pointer to a surface. Try to fix up these cases...
			if( mod->numsurfaces <= INT16_MAX && (int16_t)in[i] < 0 )
			{
				Con_Printf( S_WARN "%s: fixing up bad surface number %i at %i (max %i) in '%s'\n", __func__, in[i], i, mod->numsurfaces, mod->name );
				out[i] = mod->surfaces;
				continue;
			}

			if( in[i] < 0 || in[i] >= mod->numsurfaces )
				Host_Error( "%s: bad surface number %i at %i (max %i) in '%s'\n", __func__, in[i], i, mod->numsurfaces, mod->name );
			out[i] = mod->surfaces + in[i];
		}
	}
}

static qboolean Mod_LooksLikeWaterTexture( const char *name )
{
	if(( name[0] == '*' && Q_stricmp( name, REF_DEFAULT_TEXTURE )) || name[0] == '!' )
		return true;

	if( !Host_IsQuakeCompatible( ))
	{
		if( !Q_strncmp( name, "water", 5 ) || !Q_strnicmp( name, "laser", 5 ))
			return true;
	}

	return false;
}

static void Mod_TextureReplacementReport( const char *modelname, const char *texname, const char *type, int gl_texturenum, const char *foundpath )
{
	if( host_allow_materials.value != 2.0f )
		return;

	if( gl_texturenum > 0 ) // found and loaded successfully
		Con_Printf( "Looking for %s:%s%s tex replacement..." S_GREEN "OK (%s)\n", modelname, texname, type, foundpath );
	else if( gl_texturenum < 0 ) // not found
		Con_Printf( "Looking for %s:%s%s tex replacement..." S_YELLOW "MISS (%s)\n", modelname, texname, type, foundpath );
	else // found but not loaded
		Con_Printf( "Looking for %s:%s%s tex replacement..." S_RED "FAIL (%s)\n", modelname, texname, type, foundpath );
}

static qboolean Mod_SearchForTextureReplacement( char *out, size_t size, const char *modelname, const char *texname, const char *type )
{
	const char *subdirs[] = { modelname, "common" };
	int i;

	for( i = 0; i < ARRAYSIZE( subdirs ); i++ )
	{
		if( Q_snprintf( out, size, "materials/%s/%s%s.tga", subdirs[i], texname, type ) < 0 )
			continue; // truncated name

		if( g_fsapi.FileExists( out, false ))
			return true; // found, load it
	}

	Mod_TextureReplacementReport( modelname, texname, type, -1, "not found" );

	return false;
}

static void Mod_InitSkyClouds( model_t *mod, const mip_t *mt, texture_t *tx, qboolean custom_palette )
{
#if !XASH_DEDICATED
	rgbdata_t	r_temp, *r_sky;
	uint	*trans, *rgba;
	uint	transpix;
	int	r, g, b;
	int	i, j, p;
	string	texname;
	int solidskyTexture = 0, alphaskyTexture = 0;

	if( !ref.initialized )
		return;

	if( Mod_AllowMaterials( ))
	{
		rgbdata_t *pic;

		if( Mod_SearchForTextureReplacement( texname, sizeof( texname ), mod->name, mt->name, "_solid" ))
		{
			pic = FS_LoadImage( texname, NULL, 0 );
			if( pic )
			{
				// need to do rename texture to properly cleanup these textures on reload
				solidskyTexture = GL_LoadTextureInternal( "solid_sky", pic, TF_NOMIPMAP );
				Mod_TextureReplacementReport( mod->name, mt->name, "_solid", solidskyTexture, texname );
				FS_FreeImage( pic );
			}
		}

		if( Mod_SearchForTextureReplacement( texname, sizeof( texname ), mod->name, mt->name, "_alpha" ))
		{
			pic = FS_LoadImage( texname, NULL, 0 );
			if( pic )
			{
				alphaskyTexture = GL_LoadTextureInternal( "alpha_sky", pic, TF_NOMIPMAP );
				Mod_TextureReplacementReport( mod->name, mt->name, "_alpha", alphaskyTexture, texname );
				FS_FreeImage( pic );
			}
		}

		if( !solidskyTexture || !alphaskyTexture )
		{
			ref.dllFuncs.GL_FreeTexture( solidskyTexture );
			ref.dllFuncs.GL_FreeTexture( alphaskyTexture );
		}
		else goto done; // replacements found, notify the renderer and exit
	}

	Q_snprintf( texname, sizeof( texname ), "%s%s.mip", ( mt->offsets[0] > 0 ) ? "#" : "", tx->name );

	if( mt->offsets[0] > 0 )
	{
		size_t size = sizeof( mip_t ) + (( mt->width * mt->height * 85 ) >> 6 );

		if( custom_palette )
			size += sizeof( short ) + 768;

		r_sky = FS_LoadImage( texname, (byte *)mt, size );
	}
	else
	{
		// okay loading it from wad
		r_sky = FS_LoadImage( texname, NULL, 0 );
	}

	if( !r_sky || !r_sky->palette || r_sky->type != PF_INDEXED_32 || r_sky->height == 0 )
	{
		Con_Printf( S_ERROR "%s: unable to load sky texture %s\n", __func__, tx->name );

		if( r_sky )
			FS_FreeImage( r_sky );

		return;
	}

	// make an average value for the back to avoid
	// a fringe on the top level
	trans = Mem_Malloc( host.mempool, r_sky->height * r_sky->height * sizeof( *trans ));
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
	solidskyTexture = GL_LoadTextureInternal( "solid_sky", &r_temp, TF_NOMIPMAP );

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
	alphaskyTexture = GL_LoadTextureInternal( "alpha_sky", &r_temp, TF_NOMIPMAP );

	// clean up
	FS_FreeImage( r_sky );
	Mem_Free( trans );

	if( !solidskyTexture || !alphaskyTexture )
	{
		ref.dllFuncs.GL_FreeTexture( solidskyTexture );
		ref.dllFuncs.GL_FreeTexture( alphaskyTexture );
		return;
	}

done:
	// notify the renderer
	ref.dllFuncs.R_SetSkyCloudsTextures( solidskyTexture, alphaskyTexture );

	if( solidskyTexture && alphaskyTexture )
		SetBits( world.flags, FWORLD_SKYSPHERE );
#endif // !XASH_DEDICATED
}

static void Mod_LoadTextureData( model_t *mod, dbspmodel_t *bmod, int textureIndex )
{
	uint32_t txFlags = 0;
	char texpath[MAX_VA_STRING];
	char safemtname[16]; // only for external textures
	qboolean load_external = false;

	// don't load texture data on dedicated server, as there is no renderer.
	// but count the wadusage for automatic precache
	texture_t *texture = mod->textures[textureIndex];
	const mip_t *mipTex = Mod_GetMipTexForTexture( bmod, textureIndex );
	const qboolean usesCustomPalette = Mod_CalcMipTexUsesCustomPalette( mod, bmod, textureIndex );
	const qboolean iswater = Mod_LooksLikeWaterTexture( mipTex->name );
	const uint texture_force_flags = r_allow_wad3_luma.value ? IL_ALLOW_WAD3_LUMA : 0;

	// check for multi-layered sky texture (quake1 specific)
	if( bmod->isworld && Q_strncmp( mipTex->name, "sky", 3 ) == 0 && ( mipTex->width / mipTex->height ) == 2 )
	{
		Mod_InitSkyClouds( mod, mipTex, texture, usesCustomPalette ); // load quake sky
		return;
	}

	// FIXME: for ENGINE_IMPROVED_LINETRACE we need to load textures on server too
	// but there is no facility for this yet
	if( FBitSet( host.features, ENGINE_IMPROVED_LINETRACE ) && mipTex->name[0] == '{' )
		SetBits( txFlags, TF_KEEP_SOURCE ); // Paranoia2 texture alpha-tracing

	// check if this is water to keep the source texture and expand it to RGBA (so ripple effect works)
	if( iswater )
		SetBits( txFlags, TF_KEEP_SOURCE | TF_EXPAND_SOURCE );

	// Texture loading order:
	// 1. HQ from disk
	// 2. From WAD
	// 3. Internal from map

	texture->gl_texturenum = 0;
	Q_strncpy( safemtname, mipTex->name, sizeof( safemtname ));
	if( safemtname[0] == '*' )
		safemtname[0] = '!'; // replace unexpected symbol

	if( Mod_AllowMaterials( ))
	{
#if !XASH_DEDICATED
		if( Mod_SearchForTextureReplacement( texpath, sizeof( texpath ), mod->name, safemtname, "" ))
		{
			texture->gl_texturenum = ref.dllFuncs.GL_LoadTexture( texpath, NULL, 0, txFlags );
			load_external = texture->gl_texturenum != 0;
			Mod_TextureReplacementReport( mod->name, safemtname, "", texture->gl_texturenum, texpath );
		}
#endif // !XASH_DEDICATED
	}

	// Try WAD texture (force while r_wadtextures is 1)
	if( !texture->gl_texturenum && (( r_wadtextures.value && world.wadlist.count > 0 ) || mipTex->offsets[0] <= 0 ))
	{
		rgbdata_t *pic = NULL;
		int wadIndex = Mod_LoadTextureFromWadList( &world.wadlist, mipTex->name, Host_IsDedicated() ? NULL : &pic, texpath, sizeof( texpath ));

		if( wadIndex >= 0 )
		{
#if !XASH_DEDICATED
			if( !Host_IsDedicated( ) && pic != NULL )
			{
				Image_SetForceFlags( texture_force_flags );
				texture->gl_texturenum = ref.dllFuncs.GL_LoadTextureFromBuffer( texpath, pic, txFlags, false );
				FS_FreeImage( pic );
			}
#endif // !XASH_DEDICATED
			world.wadlist.wadusage[wadIndex]++;
		}
	}

#if !XASH_DEDICATED
	if( Host_IsDedicated( ))
		return;

	// WAD failed, so use internal texture (if present)
	if( mipTex->offsets[0] > 0 && texture->gl_texturenum == 0 )
	{
		char texName[64];
		const size_t size = Mod_CalculateMipTexSize( mipTex, usesCustomPalette );

		Q_snprintf( texName, sizeof( texName ), "#%s:%s.mip", loadstat.name, mipTex->name );
		Image_SetForceFlags( texture_force_flags );
		texture->gl_texturenum = ref.dllFuncs.GL_LoadTexture( texName, (byte *)mipTex, size, txFlags );
	}

	// If texture is completely missed:
	if( texture->gl_texturenum == 0 )
	{
		Con_DPrintf( S_ERROR "Unable to find %s.mip\n", mipTex->name );
		texture->gl_texturenum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );
	}

	texture->fb_texturenum = 0;

	// Check for luma texture
	// a1ba: ignore for water because fb_texturenum will be used to store ripple texture
	if( iswater )
		return;

	if( load_external ) // external textures will not have TF_HAS_LUMA flag because it set only from WAD images loader
	{
		if( Mod_SearchForTextureReplacement( texpath, sizeof( texpath ), mod->name, safemtname, "_luma" ))
		{
			texture->fb_texturenum = ref.dllFuncs.GL_LoadTexture( texpath, NULL, 0, TF_MAKELUMA );
			Mod_TextureReplacementReport( mod->name, safemtname, "_luma", texture->fb_texturenum, texpath );
		}
	}

	if( FBitSet( REF_GET_PARM( PARM_TEX_FLAGS, texture->gl_texturenum ), TF_HAS_LUMA ) && !texture->fb_texturenum )
	{
		char texName[64];

		Q_snprintf( texName, sizeof( texName ), "#%s:%s_luma.mip", loadstat.name, mipTex->name );

		Image_SetForceFlags( texture_force_flags );

		if( mipTex->offsets[0] > 0 )
		{
			const size_t size = Mod_CalculateMipTexSize( mipTex, usesCustomPalette );
			texture->fb_texturenum = ref.dllFuncs.GL_LoadTexture( texName, (byte *)mipTex, size, TF_MAKELUMA );
		}
		else
		{
			int wadIndex;
			rgbdata_t *pic = NULL;

			// NOTE: We can't load the _luma texture from the WAD as normal because it
			// doesn't exist there. The original texture is already loaded, but cannot be modified.
			// Instead, load the original texture again and convert it to luma.
			wadIndex = Mod_LoadTextureFromWadList( &world.wadlist, texture->name, &pic, NULL, 0 );

			if( wadIndex >= 0 && pic != NULL )
			{
				// OK, loading it from wad or hi-res(??) version
				texture->fb_texturenum = ref.dllFuncs.GL_LoadTextureFromBuffer( texName, pic, TF_MAKELUMA, false );
				FS_FreeImage( pic );
				world.wadlist.wadusage[wadIndex]++;
			}
		}
	}
#endif // !XASH_DEDICATED
}

static void Mod_LoadTexture( model_t *mod, dbspmodel_t *bmod, int textureIndex )
{
	texture_t *texture;
	mip_t *mipTex;

	if( textureIndex < 0 || textureIndex >= mod->numtextures )
		return;

	mipTex = Mod_GetMipTexForTexture( bmod, textureIndex );

	if( !mipTex )
	{
		// No data for this texture.
		// Create default texture (some mods require this).
		Mod_CreateDefaultTexture( mod, &mod->textures[textureIndex] );
		return;
	}

	if( mipTex->name[0] == '\0' )
		Q_snprintf( mipTex->name, sizeof( mipTex->name ), "miptex_%i", textureIndex );

	texture = (texture_t *)Mem_Calloc( mod->mempool, sizeof( *texture ));
	mod->textures[textureIndex] = texture;

	// Ensure texture name is lowercase.
	Q_strnlwr( mipTex->name, texture->name, sizeof( texture->name ));

	texture->width = mipTex->width;
	texture->height = mipTex->height;

	Mod_LoadTextureData( mod, bmod, textureIndex );
}

static void Mod_LoadAllTextures( model_t *mod, dbspmodel_t *bmod )
{
	int i;

	for( i = 0; i < mod->numtextures; i++ )
		Mod_LoadTexture( mod, bmod, i );
}

static void Mod_SequenceAnimatedTexture( model_t *mod, int baseTextureIndex )
{
	texture_t *anims[10];
	texture_t *altanims[10];
	texture_t *baseTexture;
	int max = 0;
	int altmax = 0;
	int candidateIndex;

	if( baseTextureIndex < 0 || baseTextureIndex >= mod->numtextures )
		return;

	baseTexture = mod->textures[baseTextureIndex];

	if( !Mod_NameImpliesTextureIsAnimated( baseTexture ))
		return;

	// Already sequenced
	if( baseTexture->anim_next )
		return;

	// find the number of frames in the animation
	memset( anims, 0, sizeof( anims ));
	memset( altanims, 0, sizeof( altanims ));

	if( baseTexture->name[1] >= '0' && baseTexture->name[1] <= '9' )
	{
		// This texture is a standard animation frame.
		int frameIndex = (int)baseTexture->name[1] - (int)'0';

		anims[frameIndex] = baseTexture;
		max = frameIndex + 1;
	}
	else
	{
		// This texture is an alternate animation frame.
		int frameIndex = (int)baseTexture->name[1] - (int)'a';

		altanims[frameIndex] = baseTexture;
		altmax = frameIndex + 1;
	}

	// Now search the rest of the textures to find all other frames.
	for( candidateIndex = baseTextureIndex + 1; candidateIndex < mod->numtextures; candidateIndex++ )
	{
		texture_t *altTexture = mod->textures[candidateIndex];

		if( !Mod_NameImpliesTextureIsAnimated( altTexture ))
			continue;

		// This texture is animated, but is it part of the same group as
		// the original texture we encountered? Check that the rest of
		// the name matches the original (both will be valid for at least
		// string index 2).
		if( Q_strcmp( altTexture->name + 2, baseTexture->name + 2 ) != 0 )
			continue;

		if( altTexture->name[1] >= '0' && altTexture->name[1] <= '9' )
		{
			// This texture is a standard frame.
			int frameIndex = (int)altTexture->name[1] - (int)'0';
			anims[frameIndex] = altTexture;

			if( frameIndex >= max )
				max = frameIndex + 1;
		}
		else
		{
			// This texture is an alternate frame.
			int frameIndex = (int)altTexture->name[1] - (int)'a';
			altanims[frameIndex] = altTexture;

			if( frameIndex >= altmax )
				altmax = frameIndex + 1;
		}
	}

	// Link all standard animated frames together.
	for( candidateIndex = 0; candidateIndex < max; candidateIndex++ )
	{
		texture_t *tex = anims[candidateIndex];

		if( !tex )
		{
			Con_Printf( S_ERROR "%s: missing frame %i of animated texture \"%s\"\n",
				__func__,
				candidateIndex,
				baseTexture->name );

			baseTexture->anim_total = 0;
			break;
		}

		tex->anim_total = max * ANIM_CYCLE;
		tex->anim_min = candidateIndex * ANIM_CYCLE;
		tex->anim_max = ( candidateIndex + 1 ) * ANIM_CYCLE;
		tex->anim_next = anims[( candidateIndex + 1 ) % max];

		if( altmax > 0 )
			tex->alternate_anims = altanims[0];
	}

	// Link all alternate animated frames together.
	for( candidateIndex = 0; candidateIndex < altmax; candidateIndex++ )
	{
		texture_t *tex = altanims[candidateIndex];

		if( !tex )
		{
			Con_Printf( S_ERROR "%s: missing alternate frame %i of animated texture \"%s\"\n",
				__func__,
				candidateIndex,
				baseTexture->name );

			baseTexture->anim_total = 0;
			break;
		}

		tex->anim_total = altmax * ANIM_CYCLE;
		tex->anim_min = candidateIndex * ANIM_CYCLE;
		tex->anim_max = ( candidateIndex + 1 ) * ANIM_CYCLE;
		tex->anim_next = altanims[( candidateIndex + 1 ) % altmax];

		if( max > 0 )
			tex->alternate_anims = anims[0];
	}
}

static void Mod_SequenceAllAnimatedTextures( model_t *mod )
{
	int i;

	for( i = 0; i < mod->numtextures; i++ )
		Mod_SequenceAnimatedTexture( mod, i );
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures( model_t *mod, dbspmodel_t *bmod )
{
	dmiptexlump_t *lump;

#if !XASH_DEDICATED
	// release old sky layers first
	if( !Host_IsDedicated() && bmod->isworld )
	{
		ref.dllFuncs.GL_FreeTexture( R_GetBuiltinTexture( "alpha_sky" ));
		ref.dllFuncs.GL_FreeTexture( R_GetBuiltinTexture( "solid_sky" ));
	}
#endif

	lump = bmod->textures;

	if( bmod->texdatasize < 1 || !lump || lump->nummiptex < 1 )
	{
		// no textures
		mod->textures = NULL;
		return;
	}

	mod->textures = (texture_t **)Mem_Calloc( mod->mempool, lump->nummiptex * sizeof( texture_t * ));
	mod->numtextures = lump->nummiptex;

	Mod_LoadAllTextures( mod, bmod );
	Mod_SequenceAllAnimatedTextures( mod );
}

/*
=================
Mod_LoadTexInfo
=================
*/
static void Mod_LoadTexInfo( model_t *mod, dbspmodel_t *bmod )
{
	mfaceinfo_t	*fout, *faceinfo;
	int		i, j, k, miptex;
	dfaceinfo_t	*fin;
	mtexinfo_t	*out;
	dtexinfo_t	*in;

	// trying to load faceinfo
	faceinfo = fout = Mem_Calloc( mod->mempool, bmod->numfaceinfo * sizeof( *fout ));
	fin = bmod->faceinfo;

	for( i = 0; i < bmod->numfaceinfo; i++, fin++, fout++ )
	{
		Q_strncpy( fout->landname, fin->landname, sizeof( fout->landname ));
		fout->texture_step = fin->texture_step;
		fout->max_extent = fin->max_extent;
		fout->groupid = fin->groupid;
	}

	mod->texinfo = out = Mem_Calloc( mod->mempool, bmod->numtexinfo * sizeof( *out ));
	mod->numtexinfo = bmod->numtexinfo;
	in = bmod->texinfo;

	for( i = 0; i < bmod->numtexinfo; i++, in++, out++ )
	{
		for( j = 0; j < 2; j++ )
			for( k = 0; k < 4; k++ )
				out->vecs[j][k] = in->vecs[j][k];

		miptex = in->miptex;
		if( miptex < 0 || miptex >= mod->numtextures )
			miptex = 0; // this is possible?
		out->texture = mod->textures[miptex];
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
static void Mod_LoadSurfaces( model_t *mod, dbspmodel_t *bmod )
{
	int          test_lightsize = -1;
	int          next_lightofs = -1;
	int          prev_lightofs = -1;
	int          i, j, lightofs;
	mextrasurf_t *info;
	msurface_t   *out;

	mod->surfaces = out = Mem_Calloc( mod->mempool, bmod->numsurfaces * sizeof( msurface_t ));
	info = Mem_Calloc( mod->mempool, bmod->numsurfaces * sizeof( mextrasurf_t ));
	mod->numsurfaces = bmod->numsurfaces;

	// predict samplecount based on bspversion
	if( bmod->version == Q1BSP_VERSION || bmod->version == QBSP2_VERSION )
		bmod->lightmap_samples = 1;
	else
		bmod->lightmap_samples = 3;

	for( i = 0; i < bmod->numsurfaces; i++, out++, info++ )
	{
		texture_t	*tex;

		// setup crosslinks between two parts of msurface_t
		out->info = info;
		info->surf = out;

		if( bmod->version == QBSP2_VERSION )
		{
			dface32_t	*in = &bmod->surfaces32[i];

			if(( in->firstedge + in->numedges ) > mod->numsurfedges )
				continue;	// corrupted level?
			out->firstedge = in->firstedge;
			out->numedges = in->numedges;
			if( in->side ) SetBits( out->flags, SURF_PLANEBACK );
			out->plane = mod->planes + in->planenum;
			out->texinfo = mod->texinfo + in->texinfo;

			for( j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in->styles[j];
			lightofs = in->lightofs;
		}
		else
		{
			dface_t	*in = &bmod->surfaces[i];

			if(( in->firstedge + in->numedges ) > mod->numsurfedges )
			{
				Con_Reportf( S_ERROR "bad surface %i from %zu\n", i, bmod->numsurfaces );
				continue;
			}

			out->firstedge = in->firstedge;
			out->numedges = in->numedges;
			if( in->side ) SetBits( out->flags, SURF_PLANEBACK );
			out->plane = mod->planes + in->planenum;
			out->texinfo = mod->texinfo + in->texinfo;

			for( j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in->styles[j];
			lightofs = in->lightofs;
		}

		tex = out->texinfo->texture;

		if( !Q_strncmp( tex->name, "sky", 3 ))
			SetBits( out->flags, SURF_DRAWSKY );

		if( Mod_LooksLikeWaterTexture( tex->name ))
			SetBits( out->flags, SURF_DRAWTURB );

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

		Mod_CalcSurfaceBounds( mod, out, bmod );
		Mod_CalcSurfaceExtents( mod, out, bmod );
		Mod_CreateFaceBevels( mod, out, bmod );

		// grab the second sample to detect colored lighting
		if( test_lightsize > 0 && lightofs != -1 )
		{
			if( lightofs > prev_lightofs && lightofs < next_lightofs )
				next_lightofs = lightofs;
		}

		// grab the first sample to determine lightmap size
		if( lightofs != -1 && test_lightsize == -1 )
		{
			int sample_size = Mod_SampleSizeForFace( out );
			int smax = (info->lightextents[0] / sample_size) + 1;
			int tmax = (info->lightextents[1] / sample_size) + 1;
			int lightstyles = 0;

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
			ref.dllFuncs.GL_SubdivideSurface( mod, out ); // cut up polygon for warps
#endif
	}

	// now we have enough data to trying determine samplecount per lightmap pixel
	if( test_lightsize > 0 && prev_lightofs != -1 && next_lightofs != -1 && next_lightofs != 99999999 )
	{
		float samples = (float)(next_lightofs - prev_lightofs) / (float)test_lightsize;

		if( samples != (int)samples )
		{
			test_lightsize = (test_lightsize + 3) & ~3; // align datasize and try again
			samples = (float)(next_lightofs - prev_lightofs) / (float)test_lightsize;
		}

		if( samples == 1 || samples == 3 )
		{
			if( bmod->lightmap_samples != (int)samples )
				Con_DPrintf( S_WARN "detected light sample count: %g\n", samples );
			bmod->lightmap_samples = (int)samples;
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
static void Mod_LoadNodes( model_t *mod, dbspmodel_t *bmod )
{
	mnode_t	*out;
	int	i, j, p;

	mod->nodes = out = (mnode_t *)Mem_Calloc( mod->mempool, bmod->numnodes * sizeof( *out ));
	mod->numnodes = bmod->numnodes;

	for( i = 0; i < mod->numnodes; i++, out++ )
	{
		if( bmod->version == QBSP2_VERSION )
		{
			dnode32_t	*in = &bmod->nodes32[i];

			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in->mins[j];
				out->minmaxs[j+3] = in->maxs[j];
			}

#if !XASH_64BIT
			if( in->firstface >= BIT( 24 ))
			{
				Host_Error( "%s: face index limit exceeded on node %i\n", __func__, i );
				return;
			}

			if( in->numfaces >= BIT( 24 ))
			{
				Host_Error( "%s: face count limit exceeded on node %i\n", __func__, i );
				return;
			}
#endif

			p = in->planenum;
			out->plane = mod->planes + p;
			out->firstsurface_0 = in->firstface & 0xFFFF;
			out->numsurfaces_0  = in->numfaces  & 0xFFFF;

			out->firstsurface_1 = in->firstface >> 16;
			out->numsurfaces_1  = in->numfaces >> 16;

			for( j = 0; j < 2; j++ )
			{
				p = in->children[j];
#if XASH_64BIT
				if( p >= 0 ) out->children_[j] = mod->nodes + p;
				else out->children_[j] = (mnode_t *)(mod->leafs + ( -1 - p ));
#else
				if( j == 0 )
				{
					if( p >= 0 )
					{
						out->child_0_leaf = 0;
						out->child_0_off  = p;
					}
					else
					{
						out->child_0_leaf = 1;
						out->child_0_off = -1 - p;
					}
				}
				else
				{
					if( p >= 0 )
					{
						out->child_1_leaf = 0;
						out->child_1_off  = p;
					}
					else
					{
						out->child_1_leaf = 1;
						out->child_1_off = -1 - p;
					}
				}
#endif
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
			out->plane = mod->planes + p;
			out->firstsurface_0 = in->firstface;
			out->numsurfaces_0 = in->numfaces;

			for( j = 0; j < 2; j++ )
			{
				p = in->children[j];
				if( p >= 0 ) out->children_[j] = mod->nodes + p;
				else out->children_[j] = (mnode_t *)(mod->leafs + ( -1 - p ));
			}
		}
	}

	// sets nodes and leafs
	Mod_SetParent( mod, mod->nodes, NULL );
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs( model_t *mod, dbspmodel_t *bmod )
{
	mleaf_t	*out;
	int	i, j, p;
	int	visclusters = 0;

	mod->leafs = out = (mleaf_t *)Mem_Calloc( mod->mempool, bmod->numleafs * sizeof( *out ));
	mod->numleafs = bmod->numleafs;

	if( bmod->isworld )
	{
		visclusters = mod->submodels[0].visleafs;
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

			out->firstmarksurface = mod->marksurfaces + in->firstmarksurface;
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

			out->firstmarksurface = mod->marksurfaces + in->firstmarksurface;
			out->nummarksurfaces = in->nummarksurfaces;
		}

		if( bmod->isworld )
		{
			out->cluster = ( i - 1 ); // solid leaf 0 has no visdata

			if( out->cluster >= visclusters )
				out->cluster = -1;

			// ignore visofs errors on leaf 0 (solid)
			if( p >= 0 && out->cluster >= 0 && mod->visdata )
			{
				if( p < bmod->visdatasize )
					out->compressed_vis = mod->visdata + p;
				else Con_Reportf( S_WARN "Mod_LoadLeafs: invalid visofs for leaf #%i\n", i );
			}
		}
		else out->cluster = -1; // no visclusters on bmodels

		if( p == -1 ) out->compressed_vis = NULL;
		else out->compressed_vis = mod->visdata + p;

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

	if( bmod->isworld && mod->leafs[0].contents != CONTENTS_SOLID )
		Host_Error( "%s: Map %s has leaf 0 is not CONTENTS_SOLID\n", __func__, mod->name );

	// do some final things for world
	if( bmod->isworld && Mod_CheckWaterAlphaSupport( mod, bmod ))
		SetBits( world.flags, FWORLD_WATERALPHA );
}

/*
===========
Mod_CalcPHS

To be called while loading world for multiplayer game server
===========
*/
static void Mod_CalcPHS( model_t *mod )
{
	const qboolean vis_stats = host_developer.value >= DEV_EXTENDED;
	const size_t rowbytes = ALIGN( world.visbytes, 4 ); // force align rows by 32-bit boundary
	const size_t count = mod->numleafs + 1; // same as mod->submodels[0].visleafs + 1
	double t1;
	double t2;
	size_t total_compressed_size = 0;
	size_t hcount = 0;
	size_t vcount = 0;
	int i;
	byte *uncompressed_pvs;
	byte *uncompressed_phs;

	if( !mod->visdata )
		return;

#if defined( HAVE_OPENMP )
	Con_Reportf( "Building PHS in %d threads...\n", omp_get_max_threads( ));
#else
	Con_Reportf( "Building PHS...\n" );
#endif

	uncompressed_pvs = Mem_Calloc( mod->mempool, rowbytes * count * 2 );
	uncompressed_phs = &uncompressed_pvs[rowbytes * count];

	world.phsofs = Mem_Calloc( mod->mempool, sizeof( size_t ) * count );
	world.compressed_phs = NULL;

	t1 = Platform_DoubleTime();

#pragma omp parallel
	{
		// uncompress pvs first
#pragma omp for schedule( static, 256 ) // there might be thousands of leafs, split by 256
		for( i = 0; i < count; i++ )
			Mod_DecompressPVS( &uncompressed_pvs[rowbytes * i], mod->leafs[i].compressed_vis, world.visbytes );

		// now create phs
#pragma omp for schedule( static, 256 ) reduction( + : vcount, hcount )
		for( i = 0; i < count; i++ )
		{
			const byte *scan = &uncompressed_pvs[rowbytes * i];
			byte *dst = &uncompressed_phs[rowbytes * i]; // rowbytes, not rowwords!
			size_t j;

			memcpy( dst, scan, rowbytes );

			for( j = 0; j < rowbytes; j++ )
			{
				size_t k;
				uint bitbyte = scan[j];

				if( bitbyte == 0 )
					continue;

				for( k = 0; k < 8; k++ )
				{
					size_t index;

					if( !FBitSet( bitbyte, BIT( k )))
						continue;

					// OR this pvs row into the phs
					// +1 because pvs is 1 based
					index = (( j * 8 ) + k + 1 );
					if( index >= count )
						continue;

					Q_memor( dst, &uncompressed_pvs[rowbytes * index], rowbytes );
				}
			}

			if( vis_stats && i != 0 )
			{
				size_t j;

				for( j = 0; j < count; j++ )
				{
					if( CHECKVISBIT( scan, j ))
						vcount++;

					if( CHECKVISBIT( dst, j ))
						hcount++;
				}
			}
		}
	}

	// since I can't predict at which spot compressed array
	// should be put, this loop is single threaded
	for( i = 0; i < count; i++ )
	{
		const byte *src = &uncompressed_phs[rowbytes * i];
		byte temp_compressed_row[(MAX_MAP_LEAFS+1)/4]; // compression for this row might be ineffective
		size_t compressed_size;

		compressed_size = Mod_CompressPVS( temp_compressed_row, src, rowbytes );

		world.compressed_phs = Mem_Realloc( mod->mempool, world.compressed_phs, total_compressed_size + compressed_size );
		memcpy( &world.compressed_phs[total_compressed_size], temp_compressed_row, compressed_size );
		world.phsofs[i]	= total_compressed_size;

		total_compressed_size += compressed_size;
	}

	t2 = Platform_DoubleTime();

	if( vis_stats )
		Con_Reportf( "Average leaves visible / audible / total: %zu / %zu / %zu\n", vcount / count, hcount / count, count );
	Con_Reportf( "Uncompressed PHS size: %s\n", Q_memprint( rowbytes * count ));
	Con_Reportf( "Compressed PHS size: %s\n", Q_memprint( total_compressed_size + sizeof( *world.phsofs ) * count ));
	Con_Reportf( "PHS building time: %.2f ms\n", ( t2 - t1 ) * 1000.0f );

	// TODO: rewrite this into a unit test
	// NOTE: how to get GoldSrc fat PHS and PVS data
	// start a multiplayer server with some op4_bootcamp (for example)
	// attach to process with GDB:
	// (gdb) p gPAS[0]
	// $0 = (byte *) ...
	// (gdb) p gPAS[gPVSRowBytes * (cl.worldmodel->numleafs + 1)]
	// $1 = (byte *) ...
	// (gdb) dump binary memory op4_bootcamp_gs.phs $0 $1
	// (gdb) p gPVS[0]
	// $2 = (byte *) ...
	// (gdb) p gPVS[gPVSRowBytes * (cl.worldmodel->numleafs + 1)]
	// $3 = (byte *) ...
	// (gdb) dump binary memory op4_bootcamp_gs.pvs $0 $1
	//
	// NOTE: as of writing, uncompressed PVS and PHS data do match! hooray!
	//
	// FS_WriteFile( "op4_bootcamp.pvs", uncompressed_pvs, rowbytes * count );
	// FS_WriteFile( "op4_bootcamp.phs", uncompressed_phs, rowbytes * count );

	// release uncompressed data
	Mem_Free( uncompressed_pvs );

	// TODO: cache the PHS somewhere, it might take a long time on giant maps
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes( model_t *mod, dbspmodel_t *bmod )
{
	dclipnode32_t	*out;
	int		i;

	bmod->clipnodes_out = out = (dclipnode32_t *)Mem_Malloc( mod->mempool, bmod->numclipnodes * sizeof( *out ));

	if(( bmod->version == QBSP2_VERSION ) || ( bmod->version == HLBSP_VERSION && bmod->isbsp30ext && bmod->numclipnodes >= MAX_MAP_CLIPNODES_HLBSP ))
	{
		dclipnode32_t *in = bmod->clipnodes32;

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

			// aguirRe QBSP 'broken' clipnodes
			if( out->children[0] >= bmod->numclipnodes )
				out->children[0] -= 65536;
			if( out->children[1] >= bmod->numclipnodes )
				out->children[1] -= 65536;
		}
	}

	// FIXME: fill mod->clipnodes?
	mod->numclipnodes = bmod->numclipnodes;
}

/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility( model_t *mod, dbspmodel_t *bmod )
{
	mod->visdata = Mem_Malloc( mod->mempool, bmod->visdatasize );
	memcpy( mod->visdata, bmod->visdata, bmod->visdatasize );
}

/*
=================
Mod_LoadLightVecs
=================
*/
static void Mod_LoadLightVecs( model_t *mod, dbspmodel_t *bmod )
{
	if( bmod->deluxdatasize != bmod->lightdatasize )
	{
		if( bmod->deluxdatasize > 0 )
			Con_Printf( S_ERROR "%s: has mismatched size (%zu should be %zu)\n", __func__, bmod->deluxdatasize, bmod->lightdatasize );
		else
			Mod_LoadLitfile( mod, "dlit", bmod->lightdatasize, &bmod->deluxedata_out, &bmod->deluxdatasize ); // old method
		return;
	}

	bmod->deluxedata_out = Mem_Malloc( mod->mempool, bmod->deluxdatasize );
	memcpy( bmod->deluxedata_out, bmod->deluxdata, bmod->deluxdatasize );
}

/*
=================
Mod_LoadShadowmap
=================
*/
static void Mod_LoadShadowmap( model_t *mod, dbspmodel_t *bmod )
{
	if( bmod->shadowdatasize != ( bmod->lightdatasize / 3 ))
	{
		if( bmod->shadowdatasize > 0 )
			Con_Printf( S_ERROR "%s: has mismatched size (%zu should be %zu)\n", __func__, bmod->shadowdatasize, bmod->lightdatasize / 3 );
		return;
	}

	bmod->shadowdata_out = Mem_Malloc( mod->mempool, bmod->shadowdatasize );
	memcpy( bmod->shadowdata_out, bmod->shadowdata, bmod->shadowdatasize );
}

/*
=================
Mod_LoadLighting
=================
*/
static void Mod_LoadLighting( model_t *mod, dbspmodel_t *bmod )
{
	int i;

	if( !bmod->lightdatasize )
		return;

	switch( bmod->lightmap_samples )
	{
	case 1:
		if( bmod->rgblightdata && bmod->rgblightdatasize > 0 && bmod->rgblightdatasize == bmod->lightdatasize * 3 )
		{
			bmod->lightdatasize = bmod->rgblightdatasize;
			mod->lightdata = Mem_Malloc( mod->mempool, bmod->rgblightdatasize );
			memcpy( mod->lightdata, bmod->rgblightdata, bmod->rgblightdatasize );
			SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		}
		else if( Mod_LoadLitfile( mod, "lit", bmod->lightdatasize * 3, &mod->lightdata, &bmod->lightdatasize ))
		{
			SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		}
		else
		{
			mod->lightdata = (color24 *)Mem_Malloc( mod->mempool, bmod->lightdatasize * sizeof( color24 ));

			// expand the white lighting data
			for( i = 0; i < bmod->lightdatasize; i++ )
				mod->lightdata[i].r = mod->lightdata[i].g = mod->lightdata[i].b = bmod->lightdata[i];
		}
		break;
	case 3:	// load colored lighting
		mod->lightdata = Mem_Malloc( mod->mempool, bmod->lightdatasize );
		memcpy( mod->lightdata, bmod->lightdata, bmod->lightdatasize );
		SetBits( mod->flags, MODEL_COLORED_LIGHTING );
		break;
	default:
		Host_Error( "%s: bad lightmap sample count %i\n", __func__, bmod->lightmap_samples );
		break;
	}

	Con_Reportf( "lighting: %s\n", FBitSet( mod->flags, MODEL_COLORED_LIGHTING ) ? "colored" : "monochrome" );

	// not supposed to be load ?
	if( FBitSet( host.features, ENGINE_LOAD_DELUXEDATA ))
	{
		Mod_LoadLightVecs( mod, bmod );
		Mod_LoadShadowmap( mod, bmod );

		if( bmod->isworld && bmod->deluxdatasize )
			SetBits( world.flags, FWORLD_HAS_DELUXEMAP );
	}

	// setup lightdata pointers
	if( !mod->lightdata )
		return;

	for( i = 0; i < mod->numsurfaces; i++ )
	{
		int lightofs;

		if( bmod->version == QBSP2_VERSION )
			lightofs = bmod->surfaces32[i].lightofs;
		else
			lightofs = bmod->surfaces[i].lightofs;

		if( lightofs != -1 )
		{
			int offset = lightofs / bmod->lightmap_samples;

			// NOTE: we divide offset by three because lighting and deluxemap keep their pointers
			// into three-bytes structs and shadowmap just monochrome
			mod->surfaces[i].samples = mod->lightdata + offset;

			// if deluxemap is present setup it too
			if( bmod->deluxedata_out )
				mod->surfaces[i].info->deluxemap = bmod->deluxedata_out + offset;

			// will be used by mods
			if( bmod->shadowdata_out )
				mod->surfaces[i].info->shadowmap = bmod->shadowdata_out + offset;
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
Mod_FindEndOfBSPFile

scans all lumps to find the factual end of file
=================
*/
static fs_offset_t Mod_FindEndOfBSPFile( const byte *mod_base, size_t bufferlen )
{
	const dheader_t *header = (const dheader_t *)mod_base;
	const dextrahdr_t *ext_header = (const dextrahdr_t *)( mod_base + sizeof( *header ));
	fs_offset_t max_offset = sizeof( *header );

	// find the maximum offset
	for( int i = 0; i < ARRAYSIZE( header->lumps ); i++ )
	{
		fs_offset_t offset = header->lumps[i].fileofs + header->lumps[i].filelen;

		if( max_offset < offset )
			max_offset = offset;
	}

	// to be able to combine BSPX data with BSP30ext, check the extended header too
	if( header->version == HLBSP_VERSION && ext_header->id == IDEXTRAHEADER && ext_header->version == EXTRA_VERSION )
	{
		for( int i = 0; i < ARRAYSIZE( ext_header->lumps ); i++ )
		{
			fs_offset_t offset = ext_header->lumps[i].fileofs + ext_header->lumps[i].filelen;

			if( max_offset < offset )
				max_offset = offset;
		}
	}

	return max_offset;
}

/*
=================
Mod_FindBSPX

find BSPX header position, returns -1 on error
=================
*/
static fs_offset_t Mod_FindBSPX( const byte *mod_base, size_t bufferlen )
{
	fs_offset_t max_offset = Mod_FindEndOfBSPFile( mod_base, bufferlen );
	const dbspx_hdr_t *bspx_header;

	max_offset = ALIGN( max_offset, 4 ); // force 32-bit boundary

	if( max_offset > bufferlen )
		return -1;

	bspx_header = (const dbspx_hdr_t *)( mod_base + max_offset );
	if( bspx_header->id != LittleLong( IDBSPXHEADER ))
		return -1;

	Con_DPrintf( "Found valid BSPX signature at %lld\n", (long long)max_offset );
	return max_offset;
}

/*
=================
Mod_LoadBmodelLumps

loading and processing bmodel
=================
*/
static qboolean Mod_LoadBmodelLumps( model_t *mod, byte *mod_base, size_t bufferlen, qboolean isworld )
{
	dheader_t   *header = (dheader_t *)mod_base;
	int         *extident = (int *)(mod_base + sizeof( dheader_t ));
	dbspmodel_t *bmod = &srcmodel;
	char        wadvalue[2048];
	size_t      len = 0;
	int         i, stat_index = 0, ret, flags = 0;
	qboolean    wadlist_warn = false;
	fs_offset_t bspx_header_offset;

	// always reset the intermediate struct
	memset( bmod, 0, sizeof( *bmod ));
	memset( &loadstat, 0, sizeof( loadstat ));

	Q_strncpy( loadstat.name, mod->name, sizeof( loadstat.name ));
	wadvalue[0] = '\0';

	// restore default lump numbers
	srclumps[0].lumpnumber = LUMP_ENTITIES;
	srclumps[1].lumpnumber = LUMP_PLANES;

	switch( header->version )
	{
	case HLBSP_VERSION:
		if( *extident == IDEXTRAHEADER )
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
		}
		break;
	case Q1BSP_VERSION:
	case QBSP2_VERSION:
		if( header->version == QBSP2_VERSION )
			SetBits( mod->flags, MODEL_QBSP2 );
		break;
	default:
		Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", mod->name, header->version, HLBSP_VERSION );
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
	bspx_header_offset = Mod_FindBSPX( mod_base, bufferlen );

	// loading base lumps
	for( i = 0; i < ARRAYSIZE( srclumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[stat_index], flags, LOADLUMP_STANDARD, NULL );

	// loading extralumps
	for( i = 0; i < ARRAYSIZE( extlumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSP30EXT, NULL );

	// loading bspx lumps
	for( i = 0; i < ARRAYSIZE( bspxlumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &bspxlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSPX, mod_base + bspx_header_offset );

	if( !bmod->isworld ) // a1ba: why world excluded here?
	{
		if( loadstat.numerrors )
		{
			Con_DPrintf( "%s: %i error(s), %i warning(s)\n", __func__, loadstat.numerrors, loadstat.numwarnings );
			return false; // there were errors, we can't load this map
		}

		if( loadstat.numwarnings )
			Con_DPrintf( "%s: %i warning(s)\n", __func__, loadstat.numwarnings );
	}

	// load into heap
	Mod_LoadEntities( mod, bmod );
	Mod_LoadPlanes( mod, bmod );
	Mod_LoadSubmodels( mod, bmod );
	Mod_LoadVertexes( mod, bmod );
	Mod_LoadEdges( mod, bmod );
	Mod_LoadSurfEdges( mod, bmod );
	Mod_LoadTextures( mod, bmod );
	Mod_LoadVisibility( mod, bmod );
	Mod_LoadTexInfo( mod, bmod );
	Mod_LoadSurfaces( mod, bmod );
	Mod_LoadLighting( mod, bmod );
	Mod_LoadMarkSurfaces( mod, bmod );
	Mod_LoadLeafs( mod, bmod );
	Mod_LoadNodes( mod, bmod );
	Mod_LoadClipnodes( mod, bmod );

	// preform some post-initalization
	Mod_MakeHull0( mod, bmod );
	Mod_SetupSubmodels( mod, bmod );

	if( isworld )
	{
		world.version = bmod->version;
#if !XASH_DEDICATED
		world.deluxedata = bmod->deluxedata_out;	// deluxemap data pointer
		world.shadowdata = bmod->shadowdata_out;	// occlusion data pointer
#endif // XASH_DEDICATED

		if( SV_Active() && svs.maxclients > 1 )
			Mod_CalcPHS( mod );
	}

	for( i = 0; i < world.wadlist.count; i++ )
	{
		if( !world.wadlist.wadusage[i] )
			continue;

		if( !wadlist_warn )
		{
			ret = Q_snprintf( &wadvalue[len], sizeof( wadvalue ), "%s; ", world.wadlist.wadnames[i] );
			if( ret == -1 )
			{
				Con_DPrintf( S_WARN "Too many wad files for output!\n" );
				wadlist_warn = true;
			}
			len += ret;
		}
	}

	if( !COM_StringEmptyOrNULL( wadvalue ))
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
qboolean Mod_TestBmodelLumps( file_t *f, const char *name, byte *mod_base, size_t buffersize, qboolean silent, dlump_t *entities )
{
	dheader_t   *header = (dheader_t *)mod_base;
	int         *extident = (int *)( mod_base + sizeof( dheader_t ));
	int         i, flags = LUMP_TESTONLY, stat_index = 0;

	// always reset the intermediate struct
	memset( &loadstat, 0, sizeof( loadstat_t ));

	// store the name to correct show errors and warnings
	Q_strncpy( loadstat.name, name, sizeof( loadstat.name ));
	if( silent )
		SetBits( flags, LUMP_SILENT );

	if( buffersize < sizeof( *header ))
		return false;

	// restore default lump numbers
	srclumps[0].lumpnumber = LUMP_ENTITIES;
	srclumps[1].lumpnumber = LUMP_PLANES;

	switch( header->version )
	{
	case HLBSP_VERSION:
		if( buffersize > sizeof( *header ) + sizeof( dextrahdr_t ) && *extident == IDEXTRAHEADER )
		{
			SetBits( flags, LUMP_BSP30EXT );
		}
		else
		{
			// only relevant for half-life maps
			int ret = Mod_LumpLooksLikeEntitiesFile( f, &header->lumps[LUMP_ENTITIES], flags, "entities" );
			if( ret < 0 )
				return false;

			if( !ret )
			{
				ret = Mod_LumpLooksLikeEntitiesFile( f, &header->lumps[LUMP_PLANES], flags, "planes" );
				if( ret < 0 )
					return false;

				if( ret )
				{
					// blue-shift swapped lumps
					srclumps[0].lumpnumber = LUMP_PLANES;
					srclumps[1].lumpnumber = LUMP_ENTITIES;
				}
			}
		}
		break;
	case Q1BSP_VERSION:
	case QBSP2_VERSION:
		break;
	default:
		// don't early out: let me analyze errors
		if( !FBitSet( flags, LUMP_SILENT ))
			Con_Printf( S_ERROR "%s has wrong version number (%i should be %i)\n", name, header->version, HLBSP_VERSION );
		loadstat.numerrors++;
		break;
	}

	// get entities lump to caller
	*entities = header->lumps[srclumps[0].lumpnumber];

	// loading base lumps
	for( i = 0; i < ARRAYSIZE( srclumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[stat_index], flags, LOADLUMP_STANDARD, NULL );

	// loading extralumps
	for( i = 0; i < ARRAYSIZE( extlumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSP30EXT, NULL );

	// FIXME: BSPX testing

	if( !FBitSet( flags, LUMP_SILENT ))
	{
		if( loadstat.numerrors )
			Con_Printf( "%s: %i error(s), %i warning(s)\n", __func__, loadstat.numerrors, loadstat.numwarnings );
		else if( loadstat.numwarnings )
			Con_Printf( "%s: %i warning(s)\n", __func__, loadstat.numwarnings );
	}

	return loadstat.numerrors ? false : true;
}

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel( model_t *mod, void *buffer, size_t buffersize, qboolean *loaded )
{
	char poolname[MAX_VA_STRING];

	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );

	if( loaded ) *loaded = false;

	mod->mempool = Mem_AllocPool( poolname );
	mod->type = mod_brush;

	// loading all the lumps into heap
	if( !Mod_LoadBmodelLumps( mod, buffer, buffersize, world.loading ))
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
int GAME_EXPORT Mod_CheckLump( const char *filename, const int lump, int *lumpsize )
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
int GAME_EXPORT Mod_ReadLump( const char *filename, const int lump, void **lumpdata, int *lumpsize )
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
int GAME_EXPORT Mod_SaveLump( const char *filename, const int lump, void *lumpdata, int lumpsize )
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
