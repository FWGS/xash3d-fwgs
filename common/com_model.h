/*
com_model.h - cient model structures
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

#ifndef COM_MODEL_H
#define COM_MODEL_H

#include "xash3d_types.h"
#include "bspfile.h"	// we need some declarations from it

/*
==============================================================================

	ENGINE MODEL FORMAT
==============================================================================
*/
#define STUDIO_RENDER	1
#define STUDIO_EVENTS	2

#define ZISCALE		((float)0x8000)

#define MIPLEVELS		4
#define VERTEXSIZE		7
#define MAXLIGHTMAPS	4	// max light styles per face
#define MAXDYNLIGHTS	8	// maximum dynamic lights per one pixel
#define NUM_AMBIENTS	4		// automatic ambient sounds

// model types
typedef enum
{
	mod_bad = -1,
	mod_brush,
	mod_sprite,
	mod_alias,
	mod_studio
} modtype_t;

typedef struct mplane_s
{
	vec3_t		normal;
	float		dist;
	byte		type;		// for fast side tests
	byte		signbits;		// signx + (signy<<1) + (signz<<1)
	byte		pad[2];
} mplane_t;

typedef struct
{
	vec3_t		position;
} mvertex_t;

typedef struct
{
	int		planenum;
#ifdef SUPPORT_BSP2_FORMAT
	int		children[2];	// negative numbers are contents
#else
	short		children[2];	// negative numbers are contents
#endif
} mclipnode_t;

// size is matched but representation is not
typedef struct
{
#ifdef SUPPORT_BSP2_FORMAT
	unsigned int	v[2];
#else
	unsigned short	v[2];
	unsigned int	cachededgeoffset;
#endif
} medge_t;

typedef struct texture_s
{
	char		name[16];
	unsigned int	width, height;
	int		gl_texturenum;
	struct msurface_s	*texturechain;	// for gl_texsort drawing
	int		anim_total;	// total tenths in sequence ( 0 = no)
	int		anim_min, anim_max;	// time for this frame min <=time< max
	struct texture_s	*anim_next;	// in the animation sequence
	struct texture_s	*alternate_anims;	// bmodels in frame 1 use these
	unsigned short	fb_texturenum;	// auto-luma texturenum
	unsigned short	dt_texturenum;	// detail-texture binding
	unsigned int	unused[3];	// reserved
} texture_t;

typedef struct
{
	char		landname[16];	// name of decsription in mapname_land.txt
	unsigned short	texture_step;	// default is 16, pixels\luxels ratio
	unsigned short	max_extent;	// default is 16, subdivision step ((texture_step * max_extent) - texture_step)
	short		groupid;		// to determine equal landscapes from various groups, -1 - no group

	vec3_t		mins, maxs;	// terrain bounds (fill by user)

	intptr_t	reserved[32];	// just for future expansions or mod-makers
} mfaceinfo_t;

typedef struct
{
	mplane_t		*edges;
	int		numedges;
	vec3_t		origin;
	vec_t		radius;		// for culling tests
	int		contents;		// sky or solid
} mfacebevel_t;

typedef struct
{
	float		vecs[2][4];	// [s/t] unit vectors in world space.
					// [i][3] is the s/t offset relative to the origin.
					// s or t = dot( 3Dpoint, vecs[i] ) + vecs[i][3]
	mfaceinfo_t	*faceinfo;	// pointer to landscape info and lightmap resolution (may be NULL)
	texture_t		*texture;
	int		flags;		// sky or slime, no lightmap or 256 subdivision
} mtexinfo_t;

typedef struct glpoly_s
{
	struct glpoly_s	*next;
	struct glpoly_s	*chain;
	int		numverts;
	int		flags;          		// for SURF_UNDERWATER
	float		verts[4][VERTEXSIZE];	// variable sized (xyz s1t1 s2t2)
} glpoly_t;

typedef struct mnode_s
{
// common with leaf
	int		contents;		// 0, to differentiate from leafs
	int		visframe;		// node needs to be traversed if current

	float		minmaxs[6];	// for bounding box culling
	struct mnode_s	*parent;

// node specific
	mplane_t		*plane;
	struct mnode_s	*children[2];
#ifdef SUPPORT_BSP2_FORMAT
	int		firstsurface;
	int		numsurfaces;
#else
	unsigned short	firstsurface;
	unsigned short	numsurfaces;
#endif
} mnode_t;

typedef struct msurface_s	msurface_t;
typedef struct decal_s	decal_t;

// JAY: Compress this as much as possible
struct decal_s
{
	decal_t		*pnext;		// linked list for each surface
	msurface_t	*psurface;	// Surface id for persistence / unlinking
	float		dx;		// local texture coordinates
	float		dy;		//
	float		scale;		// Pixel scale
	short		texture;		// Decal texture
	short		flags;		// Decal flags  FDECAL_*
	short		entityIndex;	// Entity this is attached to
// Xash3D specific
	vec3_t		position;		// location of the decal center in world space.
	glpoly_t	*polys;		// precomputed decal vertices
	intptr_t	reserved[4];	// just for future expansions or mod-makers
};

typedef struct mleaf_s
{
// common with node
	int		contents;
	int		visframe;		// node needs to be traversed if current

	float		minmaxs[6];	// for bounding box culling

	struct mnode_s	*parent;
// leaf specific
	byte		*compressed_vis;
	struct efrag_s	*efrags;

	msurface_t	**firstmarksurface;
	int		nummarksurfaces;
	int		cluster;		// helper to acess to uncompressed visdata
	byte		ambient_sound_level[NUM_AMBIENTS];

} mleaf_t;

// surface extradata
typedef struct mextrasurf_s
{
	vec3_t		mins, maxs;
	vec3_t		origin;		// surface origin
	struct msurface_s	*surf;		// upcast to surface

	// extended light info
	int		dlight_s, dlight_t;	// gl lightmap coordinates for dynamic lightmaps

	short		lightmapmins[2];	// lightmatrix
	short		lightextents[2];
	float		lmvecs[2][4];

	color24		*deluxemap;	// note: this is the actual deluxemap data for this surface
	byte		*shadowmap;	// note: occlusion map for this surface
// begin userdata
	struct msurface_s	*lightmapchain;	// lightmapped polys
	struct mextrasurf_s	*detailchain;	// for detail textures drawing
	mfacebevel_t	*bevel;		// for exact face traceline
	struct mextrasurf_s	*lumachain;	// draw fullbrights
	struct cl_entity_s	*parent;		// upcast to owner entity

	int		mirrortexturenum;	// gl texnum
	float		mirrormatrix[4][4];

	struct grasshdr_s	*grass;		// grass that linked by this surface
	unsigned short	grasscount;	// number of bushes per polygon (used to determine total VBO size)
	unsigned short	numverts;		// world->vertexes[]
	int		firstvertex;	// fisrt look up in tr.tbn_vectors[], then acess to world->vertexes[]

	intptr_t	reserved[32];	// just for future expansions or mod-makers
} mextrasurf_t;

struct msurface_s
{
	int		visframe;		// should be drawn when node is crossed

	mplane_t		*plane;		// pointer to shared plane
	int		flags;		// see SURF_ #defines

	int		firstedge;	// look up in model->surfedges[], negative numbers
	int		numedges;		// are backwards edges

	short		texturemins[2];
	short		extents[2];

	int		light_s, light_t;	// gl lightmap coordinates

	glpoly_t		*polys;		// multiple if warped
	struct msurface_s	*texturechain;

	mtexinfo_t	*texinfo;

	// lighting info
	int		dlightframe;	// last frame the surface was checked by an animated light
	int		dlightbits;	// dynamically generated. Indicates if the surface illumination
					// is modified by an animated light.

	int		lightmaptexturenum;
	byte		styles[MAXLIGHTMAPS];
	int		cached_light[MAXLIGHTMAPS];	// values currently used in lightmap
	mextrasurf_t	*info;		// pointer to surface extradata (was cached_dlight)

	color24		*samples;		// note: this is the actual lightmap data for this surface
	decal_t		*pdecals;
};

typedef struct hull_s
{
	mclipnode_t	*clipnodes;
	mplane_t		*planes;
	int		firstclipnode;
	int		lastclipnode;
	vec3_t		clip_mins;
	vec3_t		clip_maxs;
} hull_t;

#ifndef CACHE_USER
#define CACHE_USER
typedef struct cache_user_s
{
	void		*data;		// extradata
} cache_user_t;
#endif

typedef struct model_s
{
	char		name[64];		// model name
	qboolean		needload;		// bmodels and sprites don't cache normally

	// shared modelinfo
	modtype_t		type;		// model type
	int		numframes;	// sprite's framecount
	poolhandle_t mempool;		// private mempool (was synctype)
	int		flags;		// hl compatibility

//
// volume occupied by the model
//
	vec3_t		mins, maxs;	// bounding box at angles '0 0 0'
	float		radius;

	// brush model
	int		firstmodelsurface;
	int		nummodelsurfaces;

	int		numsubmodels;
	dmodel_t		*submodels;	// or studio animations

	int		numplanes;
	mplane_t		*planes;

	int		numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int		numvertexes;
	mvertex_t		*vertexes;

	int		numedges;
	medge_t		*edges;

	int		numnodes;
	mnode_t		*nodes;

	int		numtexinfo;
	mtexinfo_t	*texinfo;

	int		numsurfaces;
	msurface_t	*surfaces;

	int		numsurfedges;
	int		*surfedges;

	int		numclipnodes;
	mclipnode_t	*clipnodes;

	int		nummarksurfaces;
	msurface_t	**marksurfaces;

	hull_t		hulls[MAX_MAP_HULLS];

	int		numtextures;
	texture_t		**textures;

	byte		*visdata;

	color24		*lightdata;
	char		*entities;
//
// additional model data
//
	cache_user_t	cache;		// only access through Mod_Extradata
} model_t;

typedef struct alight_s
{
	int		ambientlight;	// clip at 128
	int		shadelight;	// clip at 192 - ambientlight
	vec3_t		color;
	float		*plightvec;
} alight_t;

typedef struct auxvert_s
{
	float		fv[3];		// viewspace x, y
} auxvert_t;

#define MAX_SCOREBOARDNAME	32
#define MAX_INFO_STRING	256

#include "custom.h"

typedef struct player_info_s
{
	int		userid;			// User id on server
	char		userinfo[MAX_INFO_STRING];	// User info string
	char		name[MAX_SCOREBOARDNAME];	// Name (extracted from userinfo)
	int		spectator;		// Spectator or not, unused (frags for quake demo playback)

	int		ping;
	int		packet_loss;

	// skin information
	char		model[64];
	int		topcolor;
	int		bottomcolor;

	// last frame rendered
	int		renderframe;

	// Gait frame estimation
	int		gaitsequence;
	float		gaitframe;
	float		gaityaw;
	vec3_t		prevgaitorigin;

	customization_t	customdata;

	// hashed cd key
	char		hashedcdkey[16];
} player_info_t;

//
// sprite representation in memory
//
typedef enum { SPR_SINGLE = 0, SPR_GROUP, SPR_ANGLED } spriteframetype_t;

typedef struct mspriteframe_s
{
	int		width;
	int		height;
	float		up, down, left, right;
	int		gl_texturenum;
} mspriteframe_t;

typedef struct
{
	int		numframes;
	float		*intervals;
	mspriteframe_t	*frames[1];
} mspritegroup_t;

typedef struct
{
	spriteframetype_t	type;
	mspriteframe_t	*frameptr;
} mspriteframedesc_t;

typedef struct
{
	short		type;
	short		texFormat;
	int		maxwidth;
	int		maxheight;
	int		numframes;
	int		radius;
	int		facecull;
	int		synctype;
	mspriteframedesc_t	frames[1];
} msprite_t;

/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/
#define MAXALIASVERTS	2048
#define MAXALIASFRAMES	256
#define MAXALIASTRIS	4096
#define MAX_SKINS		32

// This mirrors trivert_t in trilib.h, is present so Quake knows how to
// load this data
typedef struct
{
	byte		v[3];
	byte		lightnormalindex;
} trivertex_t;

typedef struct
{
	int		firstpose;
	int		numposes;
	trivertex_t	bboxmin;
	trivertex_t	bboxmax;
	float		interval;
	char		name[16];
} maliasframedesc_t;

typedef struct
{
	int		ident;
	int		version;
	vec3_t		scale;
	vec3_t		scale_origin;
	float		boundingradius;
	vec3_t		eyeposition;
	int		numskins;
	int		skinwidth;
	int		skinheight;
	int		numverts;
	int		numtris;
	int		numframes;
	int		synctype;
	int		flags;
	float		size;

	int		reserved[8];		// VBO offsets

	int		numposes;
	int		poseverts;
	trivertex_t	*posedata;	// numposes * poseverts trivert_t
	int		*commands;	// gl command list with embedded s/t
	unsigned short	gl_texturenum[MAX_SKINS][4];
	unsigned short	fb_texturenum[MAX_SKINS][4];
	unsigned short	gl_reserved0[MAX_SKINS][4];	// detail tex
	unsigned short	gl_reserved1[MAX_SKINS][4];	// normalmap
	unsigned short	gl_reserved2[MAX_SKINS][4];	// glossmap

	maliasframedesc_t	frames[1];	// variable sized
} aliashdr_t;



// remapping info
#define SUIT_HUE_START		192
#define SUIT_HUE_END		223
#define PLATE_HUE_START		160
#define PLATE_HUE_END		191

#define SHIRT_HUE_START		16
#define SHIRT_HUE_END		32
#define PANTS_HUE_START		96
#define PANTS_HUE_END		112


// 1/32 epsilon to keep floating point happy
#define DIST_EPSILON		(1.0f / 32.0f)
#define FRAC_EPSILON		(1.0f / 1024.0f)
#define BACKFACE_EPSILON		0.01f
#define MAX_BOX_LEAFS		256
#define ANIM_CYCLE			2
#define MOD_FRAMES			20



#define MAX_DEMOS		32
#define MAX_MOVIES		8
#define MAX_CDTRACKS	32
#define MAX_CLIENT_SPRITES	512	// SpriteTextures (0-256 hud, 256-512 client)
#define MAX_EFRAGS		8192	// Arcane Dimensions required
#define MAX_REQUESTS	64


#endif//COM_MODEL_H
