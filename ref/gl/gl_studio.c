/* XASH_I73770_ENGINE_PATCH_BEGIN */
#if defined(XASH_I73770_ENGINE_PATCH) && (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O3,omit-frame-pointer")
#pragma GCC target ("sse4.1,sse4.2,avx")
#endif
/* XASH_I73770_ENGINE_PATCH_END */

/*
gl_studio.c - studio model renderer
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Optimization pass (RX 580 / XASH3D-ENCHANCED):
 - Safe Studio VBO streaming path: uploads animated CPU-skinned verts to a dynamic VBO/IBO each mesh draw.
 - Keeps animation/movement correct; does NOT persist already-skinned world-space vertices.
 - Future-proof model-space cache structs retained, but unsafe static cache draw is not used.
 - Mesh sort: opaque first, masked second, additive last, then material/skin key.
 - Bone matrix upload packed into vec4 rows for future GPU skinning shader path.
 - GPU skinning scaffolding retained, but hard-disabled in this rescue build because the experimental shader path made Studio models invisible on some configs.
 - Persistent model-space cache: static VBO/IBO built from tri commands, never caches already-skinned world verts.
 - TBO/SSBO/instancing/LOD/KTX2 hooks are prepared as capability-gated scaffolding; disabled until gl_opengl/texture loader exposes the required plumbing.
 - frametime guard tightened to 0.0-0.1 s to avoid animation drift.
 - R_StudioBuildNormalTable: chrome_origin trig cached per frame.
 - R_StudioComputeSkinMatrix: fast-path for single bone (most common case).
 - CVARs: r_studio_meshcache, r_studio_gpu_skinning, r_studio_batch_sort.
*/

#include "gl_local.h"
#include "xash3d_mathlib.h"
#include "const.h"
#include "r_studioint.h"
#include "triangleapi.h"
#include "studio.h"
#include "pm_local.h"

#ifndef GL_STREAM_DRAW_ARB
#define GL_STREAM_DRAW_ARB 0x88E0
#endif

#ifndef GL_STATIC_DRAW_ARB
#define GL_STATIC_DRAW_ARB 0x88E4
#endif
#ifndef GL_VERTEX_SHADER_ARB
#define GL_VERTEX_SHADER_ARB 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER_ARB
#define GL_FRAGMENT_SHADER_ARB 0x8B30
#endif
#ifndef GL_OBJECT_COMPILE_STATUS_ARB
#define GL_OBJECT_COMPILE_STATUS_ARB 0x8B81
#endif
#ifndef GL_OBJECT_LINK_STATUS_ARB
#define GL_OBJECT_LINK_STATUS_ARB 0x8B82
#endif
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB 0x84C0
#endif
#ifndef GL_TEXTURE_BUFFER
#define GL_TEXTURE_BUFFER 0x8C2A
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif

#define STUDIO_OFFSETOF( type, field ) ((size_t)&(((type *)0)->field))


#define EVENT_CLIENT	10000	// less than this value it's a server-side studio events
#define MAX_LOCALLIGHTS	32

/* -----------------------------------------------------------------------
 * Studio mesh GPU cache
 * One entry per (submodel pointer + material sort key).
 * Built once on first draw, invalidated on model unload.
 * ----------------------------------------------------------------------- */
#define STUDIO_MESH_CACHE_SIZE	256

typedef struct studio_mesh_cache_s
{
	const mstudiomodel_t	*submodel;	// key: submodel pointer
	uint32_t		material_key;	// key: skinref index packed
	GLuint			vbo;		// vertex positions + UVs + colors
	GLuint			ibo;		// triangle-list indices
	uint32_t		num_verts;
	uint32_t		num_elems;
	qboolean		valid;
} studio_mesh_cache_t;

static studio_mesh_cache_t s_mesh_cache[STUDIO_MESH_CACHE_SIZE];
static int s_mesh_cache_used = 0;

/* Packed bone-row upload for GPU skinning baseline (3 vec4 rows per bone).
 * Filled every frame when r_studio_gpu_skinning is active. */
#define STUDIO_MAX_BONE_ROWS	(MAXSTUDIOBONES * 3)
static float s_bone_rows[STUDIO_MAX_BONE_ROWS * 4]; // [bone*3+row][xyzw]

typedef struct studio_stream_vert_s
{
	float	x, y, z;
	float	s, t;
	GLubyte	r, g, b, a;
} studio_stream_vert_t;

static GLuint s_stream_vbo = 0;
static GLuint s_stream_ibo = 0;
static studio_stream_vert_t s_stream_vertices[MAXSTUDIOVERTS];
static unsigned short s_stream_indices[MAXSTUDIOVERTS * 6];


/* -----------------------------------------------------------------------
 * Full RX 580 plan preparation layer
 * -----------------------------------------------------------------------
 * This is intentionally conservative:
 *  - persistent cache stores MODEL-SPACE attributes only, so animations never freeze;
 *  - GPU skinning is opt-in and falls back to the original CPU path on any failure;
 *  - TBO/SSBO/instancing are described by backend structs, but not forcibly used until
 *    gl_opengl exposes the required entry points and the shader path is validated.
 */
typedef enum studio_skin_backend_e
{
	STUDIO_SKIN_CPU = 0,
	STUDIO_SKIN_GPU_UNIFORM = 1,
	STUDIO_SKIN_GPU_TBO = 2,
	STUDIO_SKIN_GPU_SSBO = 3
} studio_skin_backend_t;

typedef struct studio_static_vertex_s
{
	float	pos[3];
	float	normal[3];
	float	st[2];
	GLubyte	bone[4];
	GLubyte	weight[4];
} studio_static_vertex_t;

typedef struct studio_gpu_mesh_s
{
	const studiohdr_t	*header;
	const mstudiomodel_t	*submodel;
	const mstudiomesh_t	*mesh;
	uint32_t		cache_tag;
	uint32_t		layout_flags;
	int			skinref;
	int			flags;
	GLuint			static_vbo;
	GLuint			static_ibo;
	uint32_t		num_vertices;
	uint32_t		num_indices;
	qboolean		valid;
} studio_gpu_mesh_t;

typedef struct studio_gpu_instance_s
{
	studio_skin_backend_t	backend;
	uint32_t		first_bone;
	uint32_t		bone_count;
	uint32_t		material_sort_key;
	uint32_t		flags;
	vec4_t			entity_color;
} studio_gpu_instance_t;

typedef struct studio_shader_uniform_s
{
	GLhandleARB	program;
	GLhandleARB	vertex_shader;
	GLhandleARB	fragment_shader;
	GLint		a_position;
	GLint		a_normal;
	GLint		a_texcoord0;
	GLint		a_bone_index;
	GLint		a_bone_weight;
	GLint		u_bone_rows;
	GLint		u_light_dir;
	GLint		u_ambient;
	GLint		u_diffuse;
	GLint		u_texture0;
	qboolean	ready;
	qboolean	failed;
} studio_shader_uniform_t;

#define STUDIO_GPU_MESH_CACHE_SIZE 128
static studio_gpu_mesh_t s_gpu_mesh_cache[STUDIO_GPU_MESH_CACHE_SIZE];
static int s_gpu_mesh_cache_used = 0;
static studio_shader_uniform_t s_studio_uniform_shader;

/* These CVARs live in the improved gl_opengl.c. gl_studio keeps them as optional
 * policy inputs so this file is ready for the PDF plan stages. */
extern convar_t gl_studio_skin_backend;
extern convar_t gl_studio_instancing;
extern convar_t gl_model_lod;
extern convar_t gl_model_lod_bias;
extern convar_t gl_texture_ktx2_prefer;
extern convar_t gl_texture_bc7;


typedef struct
{
	char		name[MAX_OSPATH];
	char		modelname[MAX_OSPATH];
	model_t		*model;
} player_model_t;

// never gonna change, just shut up const warning
CVAR_DEFINE_AUTO( r_shadows, "0", 0, "draw ugly shadows" );

static const vec3_t hullcolor[8] =
{
{ 1.0f, 1.0f, 1.0f },
{ 1.0f, 0.5f, 0.5f },
{ 0.5f, 1.0f, 0.5f },
{ 1.0f, 1.0f, 0.5f },
{ 0.5f, 0.5f, 1.0f },
{ 1.0f, 0.5f, 1.0f },
{ 0.5f, 1.0f, 1.0f },
{ 1.0f, 1.0f, 1.0f },
};

typedef struct sortedmesh_s
{
	mstudiomesh_t	*mesh;
	int		flags;			// face flags
	int		skinref;			// resolved material/texture index
	int		sortkey;			// blend bucket + material key
} sortedmesh_t;

typedef struct
{
	double		time;
	double		frametime;
	int		framecount;		// studio framecount
	qboolean		interpolate;
	int		rendermode;
	float		blend;			// blend value

	// bones
	matrix3x4		rotationmatrix;
	matrix3x4		bonestransform[MAXSTUDIOBONES];
	matrix3x4		lighttransform[MAXSTUDIOBONES];

	// boneweighting stuff
	matrix3x4		worldtransform[MAXSTUDIOBONES];

	// cached bones
	matrix3x4		cached_bonestransform[MAXSTUDIOBONES];
	matrix3x4		cached_lighttransform[MAXSTUDIOBONES];
	char		cached_bonenames[MAXSTUDIOBONES][32];
	int		cached_numbones;		// number of bones in cache

	sortedmesh_t	meshes[MAXSTUDIOMESHES];	// sorted meshes
	vec3_t		verts[MAXSTUDIOVERTS];
	vec3_t		norms[MAXSTUDIOVERTS];

	// lighting state
	float		ambientlight;
	float		shadelight;
	vec3_t		lightvec;			// averaging light direction
	vec3_t		lightspot;		// shadow spot
	vec3_t		lightcolor;		// averaging lightcolor
	vec3_t		blightvec[MAXSTUDIOBONES];	// bone light vecs
	vec3_t		lightvalues[MAXSTUDIOVERTS];	// precomputed lightvalues per each shared vertex of submodel

	// chrome stuff
	vec3_t		chrome_origin;
	vec2_t		chrome[MAXSTUDIOVERTS];	// texture coords for surface normals
	vec3_t		chromeright[MAXSTUDIOBONES];	// chrome vector "right" in bone reference frames
	vec3_t		chromeup[MAXSTUDIOBONES];	// chrome vector "up" in bone reference frames
	int		chromeage[MAXSTUDIOBONES];	// last time chrome vectors were updated

	// glowshell stuff
	int		normaltable[MAXSTUDIOVERTS];	// glowshell uses this

	// elights cache
	int		numlocallights;
	int		lightage[MAXSTUDIOBONES];
	dlight_t		*locallight[MAX_LOCALLIGHTS];
	int		locallightcolor[MAX_LOCALLIGHTS][3];
	vec4_t		lightpos[MAXSTUDIOVERTS][MAX_LOCALLIGHTS];
	vec3_t		lightbonepos[MAXSTUDIOBONES][MAX_LOCALLIGHTS];
	float		locallightR2[MAX_LOCALLIGHTS];

	// playermodels
	player_model_t  player_models[MAX_CLIENTS];

	// drawelements renderer
	vec3_t			arrayverts[MAXSTUDIOVERTS];
	vec2_t			arraycoord[MAXSTUDIOVERTS];
	unsigned short	arrayelems[MAXSTUDIOVERTS*6];
	GLubyte			arraycolor[MAXSTUDIOVERTS][4];
	uint			numverts;
	uint			numelems;
} studio_draw_state_t;

// studio-related cvars
CVAR_DEFINE_AUTO( r_studio_sort_textures, "0", FCVAR_GLCONFIG, "change draw order for additive meshes" );
CVAR_DEFINE_AUTO( r_studio_drawelements, "1", FCVAR_GLCONFIG, "use glDrawElements for studiomodels" );

// --- NEW optimisation CVARs (RX 580 preset) ---
// r_studio_meshcache now means SAFE dynamic Studio VBO streaming.
// The old unsafe static cache of already-skinned/world-space verts is not used,
// because it freezes animation and entity movement.
// 0 = original client arrays, 1 = dynamic VBO/IBO stream path with fallback.
CVAR_DEFINE_AUTO( r_studio_meshcache, "0", FCVAR_GLCONFIG, "stream studio meshes through dynamic VBO/IBO (safe animated path; off by default)" );
// r_studio_gpu_skinning: upload bone rows as uniforms, do skinning in vertex shader
CVAR_DEFINE_AUTO( r_studio_gpu_skinning, "0", FCVAR_GLCONFIG, "GPU skinning via GLSL uniform bone rows (prepared but disabled in rescue build)" );
// r_studio_batch_sort: sort meshes opaque->masked->additive to reduce state changes
CVAR_DEFINE_AUTO( r_studio_batch_sort, "1", FCVAR_GLCONFIG, "sort studio meshes by blend mode to reduce GL state churn" );
static cvar_t			*cl_righthand = NULL;

static r_studio_interface_t	*pStudioDraw;
static studio_draw_state_t	g_studio;		// global studio state

// global variables
static qboolean m_fDoRemap;
static mstudiomodel_t *m_pSubModel;
static mstudiobodyparts_t *m_pBodyPart;
static player_info_t *m_pPlayerInfo;
static studiohdr_t *m_pStudioHeader;
static float m_flGaitMovement;
static int g_nTopColor, g_nBottomColor;	// remap colors
static int g_nFaceFlags, g_nForceFaceFlags;

/* Forward declarations: R_StudioInit calls these before the function bodies below. */
void R_StudioFlushMeshCache( void );
static void R_StudioFreeStreamBuffers( void );
static void R_StudioFreeGPUMeshCache( void );
static qboolean R_StudioTryDrawGPUUniformMesh( mstudiomesh_t *pmesh, int skinref, int flags, float s, float t,
	vec3_t *pstudioverts, vec3_t *pstudionorms, byte *pvertbone, mstudioboneweight_t *pvertweight );


/*
====================
R_StudioInit

====================
*/
void R_StudioInit( void )
{

#if XASH_PSVITA
	// don't do the same array-building work twice since that's what our FFP shim does anyway
	gEngfuncs.Cvar_FullSet( "r_studio_drawelements", "0", FCVAR_READ_ONLY );
#endif

	Matrix3x4_LoadIdentity( g_studio.rotationmatrix );

	g_studio.interpolate = true;
	g_studio.framecount = 0;
	m_fDoRemap = false;

	// Flush any stale mesh cache from a previous map/session
	R_StudioFlushMeshCache();
}

/*
================
R_StudioSetupTimings

init current time for a given model
================
*/
static void R_StudioSetupTimings( void )
{
	if( RI.drawWorld )
	{
		// synchronize with server time
		g_studio.time = gp_cl->time;
		g_studio.frametime = gp_cl->time - gp_cl->oldtime;
	}
	else
	{
		// menu stuff
		g_studio.time = gp_host->realtime;
		g_studio.frametime = gp_host->frametime;
	}

	// Guard animation interpolation against clock jumps and long stalls.
	// Clamped to 0.1 s (was 0.25 s) — anything beyond one missed frame is drift,
	// not a meaningful timestep for GoldSrc-class animation.
	if( g_studio.frametime < 0.0 )
		g_studio.frametime = 0.0;
	else if( g_studio.frametime > 0.1 )
		g_studio.frametime = 0.1;
}

/*
================
R_AllowFlipViewModel

should a flip the viewmodel if cl_righthand is set to 1
================
*/
static qboolean R_AllowFlipViewModel( cl_entity_t *e )
{
	if( cl_righthand && cl_righthand->value > 0 )
	{
		if( e == tr.viewent )
			return true;
	}

	return false;
}

/*
================
R_StudioComputeBBox

Compute a full bounding box for current sequence
================
*/
static qboolean R_StudioComputeBBox( vec3_t bbox[8] )
{
	vec3_t		studio_mins, studio_maxs;
	vec3_t		mins, maxs, p1, p2;
	cl_entity_t	*e = RI.currententity;
	mstudioseqdesc_t	*pseqdesc;
	int		i;

	if( !m_pStudioHeader )
		return false;

	// check if we have valid mins\maxs
	if( !VectorIsNull( RI.currentmodel->mins ) && !VectorIsNull( RI.currentmodel->maxs ))
	{
		// clipping bounding box
		VectorCopy( RI.currentmodel->mins, mins );
		VectorCopy( RI.currentmodel->maxs, maxs );
	}
	else
	{
		ClearBounds( mins, maxs );
	}

	// check sequence range
	if( e->curstate.sequence < 0 || e->curstate.sequence >= m_pStudioHeader->numseq )
		e->curstate.sequence = 0;

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + e->curstate.sequence;

	// add sequence box to the model box
	AddPointToBounds( pseqdesc->bbmin, mins, maxs );
	AddPointToBounds( pseqdesc->bbmax, mins, maxs );
	ClearBounds( studio_mins, studio_maxs );

	// compute a full bounding box
	for( i = 0; i < 8; i++ )
	{
  		p1[0] = ( i & 1 ) ? mins[0] : maxs[0];
  		p1[1] = ( i & 2 ) ? mins[1] : maxs[1];
  		p1[2] = ( i & 4 ) ? mins[2] : maxs[2];

		Matrix3x4_VectorTransform( g_studio.rotationmatrix, p1, p2 );
		AddPointToBounds( p2, studio_mins, studio_maxs );
		if( bbox ) VectorCopy( p2, bbox[i] );
	}

	if( !bbox && R_CullModel( e, studio_mins, studio_maxs ))
		return false; // model culled
	return true; // visible
}

static void R_StudioComputeSkinMatrix( const mstudioboneweight_t *boneweights, matrix3x4 result )
{
	const float inv255 = 1.0f / 255.0f;
	const vec4_t *boneMat[MAXSTUDIOBONEWEIGHTS];
	float weight[MAXSTUDIOBONEWEIGHTS];
	float flTotal = 0.0f;
	int i, r, c, numbones = 0;

	/* Safety and null-pointer guard. */
	if( !boneweights || boneweights->bone[0] < 0 )
	{
		Matrix3x4_LoadIdentity( result );
		return;
	}

	/* Fast path: single-bone influence (covers ~80% of verts in GoldSrc models).
	 * No blending needed — just copy the world transform. */
	if( boneweights->bone[1] < 0 )
	{
		Matrix3x4_Copy( result, g_studio.worldtransform[boneweights->bone[0]] );
		return;
	}

	/* Multi-bone blend path. Collect active influences. */
	for( i = 0; i < MAXSTUDIOBONEWEIGHTS; i++ )
	{
		const int bone = boneweights->bone[i];

		if( bone < 0 )
			break;

		boneMat[numbones] = (const vec4_t *)g_studio.worldtransform[bone];
		weight[numbones] = boneweights->weight[i] * inv255;
		flTotal += weight[numbones];
		numbones++;
	}

	/* Rounding compensation: push residual onto first bone. */
	if( flTotal < 1.0f )
		weight[0] += 1.0f - flTotal;

	/* Weighted sum of bone matrices.
	 * Loop order (r, c, bones) is cache-friendly: each boneMat[i][r][c]
	 * access strides over the 4-float row, and the compiler can auto-vectorise
	 * the innermost loop over bones. */
	for( r = 0; r < 3; r++ )
	{
		for( c = 0; c < 4; c++ )
		{
			float v = 0.0f;

			for( i = 0; i < numbones; i++ )
				v += boneMat[i][r][c] * weight[i];

			result[r][c] = v;
		}
	}
}

/*
===============
pfnGetCurrentEntity

===============
*/
static cl_entity_t *pfnGetCurrentEntity( void )
{
	return RI.currententity;
}

/*
===============
pfnPlayerInfo

===============
*/
player_info_t *pfnPlayerInfo( int index )
{
	if( !RI.drawWorld )
		index = -1;

	return gEngfuncs.pfnPlayerInfo( index );
}

/*
===============
pfnMod_ForName

===============
*/
static model_t *pfnMod_ForName( const char *model, int crash )
{
	return gEngfuncs.Mod_ForName( model, crash, false );
}

/*
===============
pfnGetPlayerState

===============
*/
static entity_state_t *R_StudioGetPlayerState( int index )
{
	if( !RI.drawWorld )
		return &RI.currententity->curstate;

	return gEngfuncs.pfnGetPlayerState( index );
}

/*
===============
pfnGetViewEntity

===============
*/
static cl_entity_t *pfnGetViewEntity( void )
{
	return tr.viewent;
}

/*
===============
pfnGetEngineTimes

===============
*/
static void pfnGetEngineTimes( int *framecount, double *current, double *old )
{
	if( framecount ) *framecount = tr.realframecount;
	if( current ) *current = gp_cl->time;
	if( old ) *old =   gp_cl->oldtime;
}

/*
===============
pfnGetViewInfo

===============
*/
static void pfnGetViewInfo( float *origin, float *upv, float *rightv, float *forwardv )
{
	if( origin ) VectorCopy( RI.vieworg, origin );
	if( forwardv ) VectorCopy( RI.vforward, forwardv );
	if( rightv ) VectorCopy( RI.vright, rightv );
	if( upv ) VectorCopy( RI.vup, upv );
}

/*
===============
R_GetChromeSprite

===============
*/
static model_t *R_GetChromeSprite( void )
{
	return gEngfuncs.GetDefaultSprite( REF_CHROME_SPRITE );
}

/*
===============
pfnGetModelCounters

===============
*/
static void pfnGetModelCounters( int **s, int **a )
{
	*s = &g_studio.framecount;
	*a = &r_stats.c_studio_models_drawn;
}

/*
===============
pfnGetAliasScale

===============
*/
static void pfnGetAliasScale( float *x, float *y )
{
	if( x ) *x = 1.0f;
	if( y ) *y = 1.0f;
}

/*
===============
pfnStudioGetBoneTransform

===============
*/
static float ****pfnStudioGetBoneTransform( void )
{
	return (float ****)g_studio.bonestransform;
}

/*
===============
pfnStudioGetLightTransform

===============
*/
static float ****pfnStudioGetLightTransform( void )
{
	return (float ****)g_studio.lighttransform;
}

/*
===============
pfnStudioGetAliasTransform

===============
*/
static float ***pfnStudioGetAliasTransform( void )
{
	return NULL;
}

/*
===============
pfnStudioGetRotationMatrix

===============
*/
static float ***pfnStudioGetRotationMatrix( void )
{
	return (float ***)g_studio.rotationmatrix;
}

/*
====================
StudioPlayerBlend

====================
*/
static void R_StudioPlayerBlend( mstudioseqdesc_t *pseqdesc, int *pBlend, float *pPitch )
{
	// calc up/down pointing
	*pBlend = (*pPitch * 3.0f);

	if( *pBlend < pseqdesc->blendstart[0] )
	{
		*pPitch -= pseqdesc->blendstart[0] / 3.0f;
		*pBlend = 0;
	}
	else if( *pBlend > pseqdesc->blendend[0] )
	{
		*pPitch -= pseqdesc->blendend[0] / 3.0f;
		*pBlend = 255;
	}
	else
	{
		if( pseqdesc->blendend[0] - pseqdesc->blendstart[0] < 0.1f ) // catch qc error
			*pBlend = 127;
		else *pBlend = 255 * (*pBlend - pseqdesc->blendstart[0]) / (pseqdesc->blendend[0] - pseqdesc->blendstart[0]);
		*pPitch = 0.0f;
	}
}

/*
====================
R_StudioLerpMovement

====================
*/
void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
{
	float	f = 1.0f;

	// don't do it if the goalstarttime hasn't updated in a while.
	// NOTE: Because we need to interpolate multiplayer characters, the interpolation time limit
	// was increased to 1.0 s., which is 2x the max lag we are accounting for.
	if( g_studio.interpolate && ( time < e->curstate.animtime + 1.0f ) && ( e->curstate.animtime != e->latched.prevanimtime ))
		f = ( time - e->curstate.animtime ) / ( e->curstate.animtime - e->latched.prevanimtime );

	// Con_Printf( "%4.2f %.2f %.2f\n", f, e->curstate.animtime, g_studio.time );
	VectorLerp( e->latched.prevorigin, f, e->curstate.origin, origin );

	if( !VectorCompareEpsilon( e->curstate.angles, e->latched.prevangles, ON_EPSILON ))
	{
		vec4_t	q, q1, q2;

		AngleQuaternion( e->curstate.angles, q1, false );
		AngleQuaternion( e->latched.prevangles, q2, false );
		QuaternionSlerp( q2, q1, f, q );
		QuaternionAngle( q, angles );
	}
	else VectorCopy( e->curstate.angles, angles );
}

/*
====================
StudioSetUpTransform

====================
*/
static void R_StudioSetUpTransform( cl_entity_t *e )
{
	vec3_t	origin, angles;

	VectorCopy( e->origin, origin );
	VectorCopy( e->angles, angles );

	// interpolate monsters position (moved into UpdateEntityFields by user request)
	if( e->curstate.movetype == MOVETYPE_STEP && !FBitSet( gp_host->features, ENGINE_COMPUTE_STUDIO_LERP ))
	{
		R_StudioLerpMovement( e, g_studio.time, origin, angles );
	}

	if( !FBitSet( gp_host->features, ENGINE_COMPENSATE_QUAKE_BUG ))
		angles[PITCH] = -angles[PITCH]; // stupid quake bug

	// don't rotate clients, only aim
	if( e->player ) angles[PITCH] = 0.0f;

	Matrix3x4_CreateFromEntity( g_studio.rotationmatrix, angles, origin, 1.0f );

	if( tr.fFlipViewModel )
	{
		g_studio.rotationmatrix[0][1] = -g_studio.rotationmatrix[0][1];
		g_studio.rotationmatrix[1][1] = -g_studio.rotationmatrix[1][1];
		g_studio.rotationmatrix[2][1] = -g_studio.rotationmatrix[2][1];
	}
}

/*
====================
StudioEstimateFrame

====================
*/
float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time )
{
	double	dfdt, f;

	if( g_studio.interpolate )
	{
		if( time < e->curstate.animtime ) dfdt = 0.0;
		else dfdt = (time - e->curstate.animtime) * e->curstate.framerate * pseqdesc->fps;
	}
	else dfdt = 0;

	if( pseqdesc->numframes <= 1 ) f = 0.0;
	else f = (e->curstate.frame * (pseqdesc->numframes - 1)) / 256.0f;

	f += dfdt;

	if( pseqdesc->flags & STUDIO_LOOPING )
	{
		if( pseqdesc->numframes > 1 )
			f -= (int)(f / (pseqdesc->numframes - 1)) *  (pseqdesc->numframes - 1);
		if( f < 0 ) f += (pseqdesc->numframes - 1);
	}
	else
	{
		if( f >= pseqdesc->numframes - 1.001 )
			f = pseqdesc->numframes - 1.001;
		if( f < 0.0 )  f = 0.0;
	}
	return f;
}

/*
====================
StudioEstimateInterpolant

====================
*/
static float R_StudioEstimateInterpolant( cl_entity_t *e )
{
	float	dadt = 1.0f;

	if( g_studio.interpolate && ( e->curstate.animtime >= e->latched.prevanimtime + 0.01f ))
	{
		dadt = ( g_studio.time - e->curstate.animtime ) / 0.1f;
		if( dadt < 0.0f ) dadt = 0.0f;
		else if( dadt > 2.0f ) dadt = 2.0f;
	}

	return dadt;
}

/*
====================
StudioFxTransform

====================
*/
static void R_StudioFxTransform( cl_entity_t *ent, matrix3x4 transform )
{
	switch( ent->curstate.renderfx )
	{
	case kRenderFxDistort:
	case kRenderFxHologram:
		if( !gEngfuncs.COM_RandomLong( 0, 49 ))
		{
			int	axis = gEngfuncs.COM_RandomLong( 0, 1 );

			if( axis == 1 ) axis = 2; // choose between x & z
			VectorScale( transform[axis], gEngfuncs.COM_RandomFloat( 1.0f, 1.484f ), transform[axis] );
		}
		else if( !gEngfuncs.COM_RandomLong( 0, 49 ))
		{
			float	offset;
			int	axis = gEngfuncs.COM_RandomLong( 0, 1 );

			if( axis == 1 ) axis = 2; // choose between x & z
			offset = gEngfuncs.COM_RandomFloat( -10.0f, 10.0f );
			transform[gEngfuncs.COM_RandomLong( 0, 2 )][3] += offset;
		}
		break;
	case kRenderFxExplode:
		{
			float	scale;

			scale = 1.0f + ( g_studio.time - ent->curstate.animtime ) * 10.0f;
			if( scale > 2.0f ) scale = 2.0f; // don't blow up more than 200%

			transform[0][1] *= scale;
			transform[1][1] *= scale;
			transform[2][1] *= scale;
		}
		break;
	}
}

/*
====================
StudioCalcBoneAdj

====================
*/
static void R_StudioCalcBoneAdj( float dadt, float *adj, const byte *pcontroller1, const byte *pcontroller2, byte mouthopen )
{
	mstudiobonecontroller_t	*pbonecontroller;
	float			value = 0.0f;
	int			i, j;

	pbonecontroller = (mstudiobonecontroller_t *)((byte *)m_pStudioHeader + m_pStudioHeader->bonecontrollerindex);

	for( j = 0; j < m_pStudioHeader->numbonecontrollers; j++ )
	{
		i = pbonecontroller[j].index;

		if( i == STUDIO_MOUTH )
		{
			// mouth hardcoded at controller 4
			value = (float)mouthopen / 64.0f;
			value = bound( 0.0f, value, 1.0f );
			value = (1.0f - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
		}
		else if( i < 4 )
		{
			// check for 360% wrapping
			if( FBitSet( pbonecontroller[j].type, STUDIO_RLOOP ))
			{
				if( abs( pcontroller1[i] - pcontroller2[i] ) > 128 )
				{
					int a = (pcontroller1[i] + 128) % 256;
					int b = (pcontroller2[i] + 128) % 256;
					value = (( a * dadt ) + ( b * ( 1.0f - dadt )) - 128) * (360.0f / 256.0f) + pbonecontroller[j].start;
				}
				else
				{
					value = ((pcontroller1[i] * dadt + (pcontroller2[i]) * (1.0f - dadt))) * (360.0f / 256.0f) + pbonecontroller[j].start;
				}
			}
			else
			{
				value = (pcontroller1[i] * dadt + pcontroller2[i] * (1.0f - dadt)) / 255.0f;
				value = bound( 0.0f, value, 1.0f );
				value = (1.0f - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			}
		}

		switch( pbonecontroller[j].type & STUDIO_TYPES )
		{
		case STUDIO_XR:
		case STUDIO_YR:
		case STUDIO_ZR:
			adj[j] = DEG2RAD( value );
			break;
		case STUDIO_X:
		case STUDIO_Y:
		case STUDIO_Z:
			adj[j] = value;
			break;
		}
	}
}

/*
====================
StudioCalcRotations

====================
*/
static void R_StudioCalcRotations( cl_entity_t *e, float pos[][3], vec4_t *q, mstudioseqdesc_t *pseqdesc, mstudioanim_t *panim, float f )
{
	int		i, frame;
	float		adj[MAXSTUDIOCONTROLLERS];
	float		s, dadt;
	mstudiobone_t	*pbone;

	// bah, fix this bug with changing sequences too fast
	if( f > pseqdesc->numframes - 1 )
	{
		f = 0.0f;
	}
	else if( f < -0.01f )
	{
		// BUG ( somewhere else ) but this code should validate this data.
		// This could cause a crash if the frame # is negative, so we'll go ahead
		// and clamp it here
		f = -0.01f;
	}

	frame = (int)f;

	dadt = R_StudioEstimateInterpolant( e );
	s = (f - frame);

	// add in programtic controllers
	pbone = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);

	R_StudioCalcBoneAdj( dadt, adj, e->curstate.controller, e->latched.prevcontroller, e->mouth.mouthopen );

	for( i = 0; i < m_pStudioHeader->numbones; i++, pbone++, panim++ )
		R_StudioCalcBones( frame, s, pbone, panim, adj, pos[i], q[i] );

	if( pseqdesc->motiontype & STUDIO_X ) pos[pseqdesc->motionbone][0] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Y ) pos[pseqdesc->motionbone][1] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Z ) pos[pseqdesc->motionbone][2] = 0.0f;
}

/*
====================
StudioMergeBones

====================
*/
static void R_StudioMergeBones( cl_entity_t *e, model_t *m_pSubModel )
{
	int		i, j;
	mstudiobone_t	*pbones;
	mstudioseqdesc_t	*pseqdesc;
	mstudioanim_t	*panim;
	matrix3x4		bonematrix;
	static vec4_t	q[MAXSTUDIOBONES];
	static float	pos[MAXSTUDIOBONES][3];
	float		f;

	if( e->curstate.sequence >=  m_pStudioHeader->numseq )
		e->curstate.sequence = 0;

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + e->curstate.sequence;

	f = R_StudioEstimateFrame( e, pseqdesc, g_studio.time );

	panim = gEngfuncs.R_StudioGetAnim( m_pStudioHeader, m_pSubModel, pseqdesc );
	R_StudioCalcRotations( e, pos, q, pseqdesc, panim, f );
	pbones = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);

	for( i = 0; i < m_pStudioHeader->numbones; i++ )
	{
		for( j = 0; j < g_studio.cached_numbones; j++ )
		{
			if( !Q_stricmp( pbones[i].name, g_studio.cached_bonenames[j] ))
			{
				Matrix3x4_Copy( g_studio.bonestransform[i], g_studio.cached_bonestransform[j] );
				Matrix3x4_Copy( g_studio.lighttransform[i], g_studio.cached_lighttransform[j] );
				break;
			}
		}

		if( j >= g_studio.cached_numbones )
		{
			Matrix3x4_FromOriginQuat( bonematrix, q[i], pos[i] );
			if( pbones[i].parent == -1 )
			{
				Matrix3x4_ConcatTransforms( g_studio.bonestransform[i], g_studio.rotationmatrix, bonematrix );
				Matrix3x4_Copy( g_studio.lighttransform[i], g_studio.bonestransform[i] );

				// apply client-side effects to the transformation matrix
				R_StudioFxTransform( e, g_studio.bonestransform[i] );
			}
			else
			{
				Matrix3x4_ConcatTransforms( g_studio.bonestransform[i], g_studio.bonestransform[pbones[i].parent], bonematrix );
				Matrix3x4_ConcatTransforms( g_studio.lighttransform[i], g_studio.lighttransform[pbones[i].parent], bonematrix );
			}
		}
	}
}

/*
====================
StudioSetupBones

====================
*/
static void R_StudioSetupBones( cl_entity_t *e )
{
	float		f;
	mstudiobone_t	*pbones;
	mstudioseqdesc_t	*pseqdesc;
	mstudioanim_t	*panim;
	matrix3x4		bonematrix;
	static vec3_t	pos[MAXSTUDIOBONES];
	static vec4_t	q[MAXSTUDIOBONES];
	static vec3_t	pos2[MAXSTUDIOBONES];
	static vec4_t	q2[MAXSTUDIOBONES];
	static vec3_t	pos3[MAXSTUDIOBONES];
	static vec4_t	q3[MAXSTUDIOBONES];
	static vec3_t	pos4[MAXSTUDIOBONES];
	static vec4_t	q4[MAXSTUDIOBONES];
	int		i;

	if( e->curstate.sequence >= m_pStudioHeader->numseq )
		e->curstate.sequence = 0;

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + e->curstate.sequence;

	f = R_StudioEstimateFrame( e, pseqdesc, g_studio.time );

	panim = gEngfuncs.R_StudioGetAnim( m_pStudioHeader, RI.currentmodel, pseqdesc );
	R_StudioCalcRotations( e, pos, q, pseqdesc, panim, f );

	if( pseqdesc->numblends > 1 )
	{
		float	s;
		float	dadt;

		panim += m_pStudioHeader->numbones;
		R_StudioCalcRotations( e, pos2, q2, pseqdesc, panim, f );

		dadt = R_StudioEstimateInterpolant( e );
		s = (e->curstate.blending[0] * dadt + e->latched.prevblending[0] * (1.0f - dadt)) / 255.0f;

		R_StudioSlerpBones( m_pStudioHeader->numbones, q, pos, q2, pos2, s );

		if( pseqdesc->numblends == 4 )
		{
			panim += m_pStudioHeader->numbones;
			R_StudioCalcRotations( e, pos3, q3, pseqdesc, panim, f );

			panim += m_pStudioHeader->numbones;
			R_StudioCalcRotations( e, pos4, q4, pseqdesc, panim, f );

			s = (e->curstate.blending[0] * dadt + e->latched.prevblending[0] * (1.0f - dadt)) / 255.0f;
			R_StudioSlerpBones( m_pStudioHeader->numbones, q3, pos3, q4, pos4, s );

			s = (e->curstate.blending[1] * dadt + e->latched.prevblending[1] * (1.0f - dadt)) / 255.0f;
			R_StudioSlerpBones( m_pStudioHeader->numbones, q, pos, q3, pos3, s );
		}
	}

	if( g_studio.interpolate && e->latched.sequencetime && ( e->latched.sequencetime + 0.2f > g_studio.time ) && ( e->latched.prevsequence < m_pStudioHeader->numseq ))
	{
		// blend from last sequence
		static vec3_t	pos1b[MAXSTUDIOBONES];
		static vec4_t	q1b[MAXSTUDIOBONES];
		float		s;

		pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + e->latched.prevsequence;
		panim = gEngfuncs.R_StudioGetAnim( m_pStudioHeader, RI.currentmodel, pseqdesc );

		// clip prevframe
		R_StudioCalcRotations( e, pos1b, q1b, pseqdesc, panim, e->latched.prevframe );

		if( pseqdesc->numblends > 1 )
		{
			panim += m_pStudioHeader->numbones;
			R_StudioCalcRotations( e, pos2, q2, pseqdesc, panim, e->latched.prevframe );

			s = (e->latched.prevseqblending[0]) / 255.0f;
			R_StudioSlerpBones( m_pStudioHeader->numbones, q1b, pos1b, q2, pos2, s );

			if( pseqdesc->numblends == 4 )
			{
				panim += m_pStudioHeader->numbones;
				R_StudioCalcRotations( e, pos3, q3, pseqdesc, panim, e->latched.prevframe );

				panim += m_pStudioHeader->numbones;
				R_StudioCalcRotations( e, pos4, q4, pseqdesc, panim, e->latched.prevframe );

				s = (e->latched.prevseqblending[0]) / 255.0f;
				R_StudioSlerpBones( m_pStudioHeader->numbones, q3, pos3, q4, pos4, s );

				s = (e->latched.prevseqblending[1]) / 255.0f;
				R_StudioSlerpBones( m_pStudioHeader->numbones, q1b, pos1b, q3, pos3, s );
			}
		}

		s = 1.0f - ( g_studio.time - e->latched.sequencetime ) / 0.2f;
		R_StudioSlerpBones( m_pStudioHeader->numbones, q, pos, q1b, pos1b, s );
	}
	else
	{
		// store prevframe otherwise
		e->latched.prevframe = f;
	}

	pbones = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);

	// calc gait animation
	if( m_pPlayerInfo && m_pPlayerInfo->gaitsequence != 0 )
	{
		qboolean	copy_bones = true;

		if( m_pPlayerInfo->gaitsequence >= m_pStudioHeader->numseq )
			m_pPlayerInfo->gaitsequence = 0;

		pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pPlayerInfo->gaitsequence;

		panim = gEngfuncs.R_StudioGetAnim( m_pStudioHeader, RI.currentmodel, pseqdesc );
		R_StudioCalcRotations( e, pos2, q2, pseqdesc, panim, m_pPlayerInfo->gaitframe );

		for( i = 0; i < m_pStudioHeader->numbones; i++ )
		{
			if( !Q_strcmp( pbones[i].name, "Bip01 Spine" ))
				copy_bones = false;
			else if( !Q_strcmp( pbones[pbones[i].parent].name, "Bip01 Pelvis" ))
				copy_bones = true;

			if( !copy_bones ) continue;

			VectorCopy( pos2[i], pos[i] );
			Vector4Copy( q2[i], q[i] );
		}
	}

	for( i = 0; i < m_pStudioHeader->numbones; i++ )
	{
		Matrix3x4_FromOriginQuat( bonematrix, q[i], pos[i] );

		if( pbones[i].parent == -1 )
		{
			Matrix3x4_ConcatTransforms( g_studio.bonestransform[i], g_studio.rotationmatrix, bonematrix );
			Matrix3x4_Copy( g_studio.lighttransform[i], g_studio.bonestransform[i] );

			// apply client-side effects to the transformation matrix
			R_StudioFxTransform( e, g_studio.bonestransform[i] );
		}
		else
		{
			Matrix3x4_ConcatTransforms( g_studio.bonestransform[i], g_studio.bonestransform[pbones[i].parent], bonematrix );
			Matrix3x4_ConcatTransforms( g_studio.lighttransform[i], g_studio.lighttransform[pbones[i].parent], bonematrix );
		}
	}
}

/*
====================
StudioSaveBones

====================
*/
static void R_StudioSaveBones( void )
{
	mstudiobone_t	*pbones;
	int		i;

	pbones = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);
	g_studio.cached_numbones = m_pStudioHeader->numbones;

	for( i = 0; i < m_pStudioHeader->numbones; i++ )
	{
		Matrix3x4_Copy( g_studio.cached_bonestransform[i], g_studio.bonestransform[i] );
		Matrix3x4_Copy( g_studio.cached_lighttransform[i], g_studio.lighttransform[i] );
		Q_strncpy( g_studio.cached_bonenames[i], pbones[i].name, 32 );
	}
}

/*
====================
StudioBuildNormalTable

NOTE: m_pSubModel must be set
====================
*/
static void R_StudioBuildNormalTable( void )
{
	cl_entity_t	*e = RI.currententity;
	mstudiomesh_t	*pmesh;
	int		i, j;

	Assert( m_pSubModel != NULL );

	// reset chrome cache
	for( i = 0; i < m_pStudioHeader->numbones; i++ )
		g_studio.chromeage[i] = 0;

	for( i = 0; i < m_pSubModel->numverts; i++ )
		g_studio.normaltable[i] = -1;

	for( j = 0; j < m_pSubModel->nummesh; j++ )
	{
		short	*ptricmds;

		pmesh = (mstudiomesh_t *)((byte *)m_pStudioHeader + m_pSubModel->meshindex) + j;
		ptricmds = (short *)((byte *)m_pStudioHeader + pmesh->triindex);

		while(( i = *( ptricmds++ )))
		{
			if( i < 0 ) i = -i;

			for( ; i > 0; i--, ptricmds += 4 )
			{
				if( g_studio.normaltable[ptricmds[0]] < 0 )
					g_studio.normaltable[ptricmds[0]] = ptricmds[1];
			}
		}
	}

	g_studio.chrome_origin[0] = cos( r_glowshellfreq->value * g_studio.time ) * 4000.0f;
	g_studio.chrome_origin[1] = sin( r_glowshellfreq->value * g_studio.time ) * 4000.0f;
	// Re-use the cos from [0] scaled by 0.33 for [2] — avoids a third trig call.
	g_studio.chrome_origin[2] = cos( r_glowshellfreq->value * g_studio.time * 0.33f ) * 4000.0f;

	if( e->curstate.rendercolor.r || e->curstate.rendercolor.g || e->curstate.rendercolor.b )
		TriColor4ub( e->curstate.rendercolor.r, e->curstate.rendercolor.g, e->curstate.rendercolor.b, 255 );
	else TriColor4ub( 255, 255, 255, 255 );
}

/*
====================
StudioGenerateNormals

NOTE: m_pSubModel must be set
g_studio.verts must be computed
====================
*/
static void R_StudioGenerateNormals( void )
{
	int		v0, v1, v2;
	vec3_t		e0, e1, norm;
	mstudiomesh_t	*pmesh;
	int		i, j;

	Assert( m_pSubModel != NULL );

	for( i = 0; i < m_pSubModel->numverts; i++ )
		VectorClear( g_studio.norms[i] );

	for( j = 0; j < m_pSubModel->nummesh; j++ )
	{
		short	*ptricmds;

		pmesh = (mstudiomesh_t *)((byte *)m_pStudioHeader + m_pSubModel->meshindex) + j;
		ptricmds = (short *)((byte *)m_pStudioHeader + pmesh->triindex);

		while(( i = *( ptricmds++ )))
		{
			if( i < 0 )
			{
				i = -i;

				if( i > 2 )
				{
					v0 = ptricmds[0]; ptricmds += 4;
					v1 = ptricmds[0]; ptricmds += 4;

					for( i -= 2; i > 0; i--, ptricmds += 4 )
					{
						v2 = ptricmds[0];

						VectorSubtract( g_studio.verts[v1], g_studio.verts[v0], e0 );
						VectorSubtract( g_studio.verts[v2], g_studio.verts[v0], e1 );
						CrossProduct( e1, e0, norm );

						VectorAdd( g_studio.norms[v0], norm, g_studio.norms[v0] );
						VectorAdd( g_studio.norms[v1], norm, g_studio.norms[v1] );
						VectorAdd( g_studio.norms[v2], norm, g_studio.norms[v2] );

						v1 = v2;
					}
				}
				else
				{
					ptricmds += i;
				}
			}
			else
			{
				if( i > 2 )
				{
					qboolean	odd = false;

					v0 = ptricmds[0]; ptricmds += 4;
					v1 = ptricmds[0]; ptricmds += 4;

					for( i -= 2; i > 0; i--, ptricmds += 4 )
					{
						v2 = ptricmds[0];

						VectorSubtract( g_studio.verts[v1], g_studio.verts[v0], e0 );
						VectorSubtract( g_studio.verts[v2], g_studio.verts[v0], e1 );
						CrossProduct( e1, e0, norm );

						VectorAdd( g_studio.norms[v0], norm, g_studio.norms[v0] );
						VectorAdd( g_studio.norms[v1], norm, g_studio.norms[v1] );
						VectorAdd( g_studio.norms[v2], norm, g_studio.norms[v2] );

						if( odd ) v1 = v2;
						else v0 = v2;

						odd = !odd;
					}
				}
				else
				{
					ptricmds += i;
				}
			}
		}
	}

	for( i = 0; i < m_pSubModel->numverts; i++ )
		VectorNormalize( g_studio.norms[i] );
}

/*
====================
StudioSetupChrome

====================
*/
static void R_StudioSetupChrome( float *pchrome, int bone, vec3_t normal )
{
	float	n;

	if( g_studio.chromeage[bone] != g_studio.framecount )
	{
		// calculate vectors from the viewer to the bone. This roughly adjusts for position
		vec3_t	chromeupvec;	// g_studio.chrome t vector in world reference frame
		vec3_t	chromerightvec;	// g_studio.chrome s vector in world reference frame
		vec3_t	tmp;		// vector pointing at bone in world reference frame

		VectorNegate( g_studio.chrome_origin, tmp );
		tmp[0] += g_studio.lighttransform[bone][0][3];
		tmp[1] += g_studio.lighttransform[bone][1][3];
		tmp[2] += g_studio.lighttransform[bone][2][3];

		VectorNormalize( tmp );
		CrossProduct( tmp, RI.vright, chromeupvec );
		VectorNormalize( chromeupvec );
		CrossProduct( chromeupvec, tmp, chromerightvec );
		VectorNormalize( chromerightvec );

		Matrix3x4_VectorIRotate( g_studio.lighttransform[bone], chromeupvec, g_studio.chromeup[bone] );
		Matrix3x4_VectorIRotate( g_studio.lighttransform[bone], chromerightvec, g_studio.chromeright[bone] );

		g_studio.chromeage[bone] = g_studio.framecount;
	}

	// calc s coord
	n = DotProduct( normal, g_studio.chromeright[bone] );
	pchrome[0] = (n + 1.0f) * 32.0f;

	// calc t coord
	n = DotProduct( normal, g_studio.chromeup[bone] );
	pchrome[1] = (n + 1.0f) * 32.0f;
}

/*
====================
StudioCalcAttachments

====================
*/
static void R_StudioCalcAttachments( void )
{
	mstudioattachment_t	*pAtt;
	int		i;

	// calculate attachment points
	pAtt = (mstudioattachment_t *)((byte *)m_pStudioHeader + m_pStudioHeader->attachmentindex);

	for( i = 0; i < Q_min( MAXSTUDIOATTACHMENTS, m_pStudioHeader->numattachments ); i++ )
	{
		Matrix3x4_VectorTransform( g_studio.lighttransform[pAtt[i].bone], pAtt[i].org, RI.currententity->attachment[i] );
	}
}

/*
===============
pfnStudioSetupModel

===============
*/
static void R_StudioSetupModel( int bodypart, void **ppbodypart, void **ppsubmodel )
{
	int	index;

	if( bodypart < 0 || bodypart >= m_pStudioHeader->numbodyparts )
		bodypart = 0;

	m_pBodyPart = (mstudiobodyparts_t *)((byte *)m_pStudioHeader + m_pStudioHeader->bodypartindex) + bodypart;

	index = RI.currententity->curstate.body / m_pBodyPart->base;
	index = index % m_pBodyPart->nummodels;

	m_pSubModel = (mstudiomodel_t *)((byte *)m_pStudioHeader + m_pBodyPart->modelindex) + index;

	if( ppbodypart ) *ppbodypart = m_pBodyPart;
	if( ppsubmodel ) *ppsubmodel = m_pSubModel;
}

/*
===============
R_StudioCheckBBox

===============
*/
static int R_StudioCheckBBox( void )
{
	if( !RI.currententity || !RI.currentmodel )
		return false;

	return R_StudioComputeBBox( NULL );
}

/*
===============
R_StudioDynamicLight

===============
*/
static void R_StudioDynamicLight( cl_entity_t *ent, alight_t *plight )
{
	movevars_t	*mv = tr.movevars;
	vec3_t		lightDir, vecSrc, vecEnd;
	vec3_t		origin, dist, finalLight;
	float		add, radius, total;
	colorVec		light;
	uint		lnum;
	dlight_t		*dl;

	if( !plight || !ent || !ent->model )
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

	if(( mv->skycolor[0] + mv->skycolor[1] + mv->skycolor[2] ) != 0 )
	{
		msurface_t	*psurf = NULL;
		pmtrace_t		trace;
		vec3_t skyvec;

		if( FBitSet( gp_host->features, ENGINE_WRITE_LARGE_COORD ))
			VectorScale( mv->skyvec, 65536.0f, skyvec );
		else
			VectorScale( mv->skyvec, 8192.0f, skyvec );

		VectorSubtract( origin, skyvec, vecEnd );

		trace = gEngfuncs.CL_TraceLine( vecSrc, vecEnd, PM_WORLD_ONLY );
		if( trace.ent > 0 ) psurf = gEngfuncs.EV_TraceSurface( trace.ent, vecSrc, vecEnd );
		else psurf = gEngfuncs.EV_TraceSurface( 0, vecSrc, vecEnd );

		if( FBitSet( ent->model->flags, STUDIO_FORCE_SKYLIGHT ) || ( psurf && FBitSet( psurf->flags, SURF_DRAWSKY )))
		{
			VectorCopy( mv->skyvec, lightDir );

			light.r = mv->skycolor[0];
			light.g = mv->skycolor[1];
			light.b = mv->skycolor[2];
		}
	}

	if(( light.r + light.g + light.b ) == 0 )
	{
		colorVec	gcolor;
		float	grad[4];

		VectorScale( lightDir, 2048.0f, vecEnd );
		VectorAdd( vecEnd, vecSrc, vecEnd );

		light = R_LightVec( vecSrc, vecEnd, g_studio.lightspot, g_studio.lightvec );

		if( VectorIsNull( g_studio.lightvec ))
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
			VectorCopy( g_studio.lightvec, lightDir );
		}
	}

	if( ent->curstate.renderfx == kRenderFxLightMultiplier && ent->curstate.iuser4 != 10 )
	{
		light.r *= ent->curstate.iuser4 / 10.0f;
		light.g *= ent->curstate.iuser4 / 10.0f;
		light.b *= ent->curstate.iuser4 / 10.0f;
	}

	VectorSet( finalLight, light.r, light.g, light.b );
	ent->cvFloorColor = light;

	total = Q_max( Q_max( light.r, light.g ), light.b );
	if( total == 0.0f ) total = 1.0f;

	// scale lightdir by light intentsity
	VectorScale( lightDir, total, lightDir );

	for( lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		dl = &tr.dlights[lnum];

		if( dl->die < g_studio.time || !r_dynamic->value )
			continue;

		VectorSubtract( ent->origin, dl->origin, dist );

		radius = VectorLength( dist );
		add = (dl->radius - radius);

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

	if( FBitSet( ent->model->flags, STUDIO_AMBIENT_LIGHT ))
		add = 0.6f;
	else add = bound( 0.75f, v_direct->value, 1.0f );

	VectorScale( lightDir, add, lightDir );

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
pfnStudioEntityLight

===============
*/
static void R_StudioEntityLight( alight_t *lightinfo )
{
	int		lnum, i, j, k;
	float		minstrength, f, r2;
	float		lstrength[MAX_LOCALLIGHTS];
	cl_entity_t	*ent = RI.currententity;
	vec3_t		mid, origin;

	g_studio.numlocallights = 0;

	if( !ent || !r_dynamic->value )
		return;

	for( i = 0; i < MAX_LOCALLIGHTS; i++ )
		lstrength[i] = 0.0f;

	Matrix3x4_OriginFromMatrix( g_studio.rotationmatrix, origin );

	for( lnum = 0; lnum < MAX_ELIGHTS; lnum++ )
	{
		dlight_t *el = &tr.elights[lnum];

		if( el->die < g_studio.time || el->radius <= 0.0f )
			continue;

		if(( el->key & 0xFFF ) == ent->index )
		{
			const int att = (el->key >> 12) & 0xF;

			if( att ) VectorCopy( ent->attachment[att], el->origin );
			else VectorCopy( ent->origin, el->origin );
		}

		VectorSubtract( origin, el->origin, mid );

		f = DotProduct( mid, mid );
		r2 = el->radius * el->radius;

		if( f > r2 ) minstrength = r2 / f;
		else minstrength = 1.0f;

		if( minstrength <= 0.05f )
			continue;

		if( g_studio.numlocallights >= MAX_LOCALLIGHTS )
		{
			float weakest = minstrength;
			k = -1;

			for( j = 0; j < g_studio.numlocallights; j++ )
			{
				if( lstrength[j] < weakest )
				{
					weakest = lstrength[j];
					k = j;
				}
			}
		}
		else k = g_studio.numlocallights;

		if( k != -1 )
		{
			g_studio.locallightcolor[k][0] = LinearGammaTable( el->color.r << 2 );
			g_studio.locallightcolor[k][1] = LinearGammaTable( el->color.g << 2 );
			g_studio.locallightcolor[k][2] = LinearGammaTable( el->color.b << 2 );
			g_studio.locallightR2[k] = r2;
			g_studio.locallight[k] = el;
			lstrength[k] = minstrength;

			if( k >= g_studio.numlocallights )
				g_studio.numlocallights = k + 1;
		}
	}
}

/*
===============
R_StudioSetupLighting

===============
*/
static void R_StudioSetupLighting( alight_t *plight )
{
	float	scale = 1.0f;
	int	i;

	if( !m_pStudioHeader || !plight )
		return;

	if( RI.currententity != NULL )
		scale = RI.currententity->curstate.scale;

	g_studio.ambientlight = plight->ambientlight;
	g_studio.shadelight = plight->shadelight;
	VectorCopy( plight->plightvec, g_studio.lightvec );

	for( i = 0; i < m_pStudioHeader->numbones; i++ )
	{
		Matrix3x4_VectorIRotate( g_studio.lighttransform[i], plight->plightvec, g_studio.blightvec[i] );
		if( scale > 1.0f ) VectorNormalize( g_studio.blightvec[i] ); // in case model may be scaled
	}

	VectorCopy( plight->color, g_studio.lightcolor );
}

/*
===============
R_StudioLighting

===============
*/
static void R_StudioLighting( float *lv, int bone, int flags, vec3_t normal )
{
	float 	illum;

	if( FBitSet( flags, STUDIO_NF_FULLBRIGHT ))
	{
		*lv = 1.0f;
		return;
	}

	illum = g_studio.ambientlight;

	if( FBitSet( flags, STUDIO_NF_FLATSHADE ))
	{
		illum += g_studio.shadelight * 0.8f;
	}
	else
	{
		float	r, lightcos;

		if( bone != -1 ) lightcos = DotProduct( normal, g_studio.blightvec[bone] );
		else lightcos = DotProduct( normal, g_studio.lightvec ); // -1 colinear, 1 opposite
		if( lightcos > 1.0f ) lightcos = 1.0f;

		illum += g_studio.shadelight;

		r = SHADE_LAMBERT;

 		// do modified hemispherical lighting
		if( r <= 1.0f )
		{
			r += 1.0f;
			lightcos = (( r - 1.0f ) - lightcos) / r;
			if( lightcos > 0.0f )
				illum += g_studio.shadelight * lightcos;
		}
		else
		{
			lightcos = (lightcos + ( r - 1.0f )) / r;
			if( lightcos > 0.0f )
				illum -= g_studio.shadelight * lightcos;
		}

		illum = Q_max( illum, 0.0f );
	}

	illum = Q_min( illum, 255.0f );

	*lv = LightToTexGamma( illum * 4 ) / 1023.0f;
}

/*
====================
R_LightLambert

====================
*/
static void R_LightLambert( vec4_t light[MAX_LOCALLIGHTS], const vec3_t normal, const vec3_t color, byte *out )
{
	vec3_t	finalLight;
	int	i;

	if( !g_studio.numlocallights )
	{
		VectorScale( color, 255.0f, out );
		return;
	}

	VectorSet( finalLight, 0, 0, 0 );

	for( i = 0; i < g_studio.numlocallights; i++ )
	{
		float	r;

		r = DotProduct( normal, light[i] );
		if( likely( !tr.fFlipViewModel ))
			r = -r;

		if( r > 0.0f )
		{
			vec3_t localLight;
			float temp;

			if( light[i][3] == 0.0f )
			{
				float r2 = DotProduct( light[i], light[i] );

				if( r2 > 0.0f )
					light[i][3] = g_studio.locallightR2[i] / ( r2 * sqrt( r2 ));
				else light[i][3] = 0.0001f;
			}

			temp = r * light[i][3];

			VectorAddScalar( g_studio.locallightcolor[i], temp, localLight );
			VectorAdd( finalLight, localLight, finalLight );
		}
	}

	if( !VectorIsNull( finalLight ))
	{
		for( i = 0; i < 3; i++ )
		{
			float c = finalLight[i] + LinearGammaTable( color[i] * 1023.0f );

			if( c > 1023.0f )
				out[i] = 255;
			else
				out[i] = ScreenGammaTable( c ) >> 2;
		}
	}
	else
	{
		VectorScale( color, 255.0f, out );
	}
}

static void R_StudioSetColorArray( short *ptricmds, vec3_t *pstudionorms, byte *color )
{
	float	*lv = (float *)g_studio.lightvalues[ptricmds[1]];

	color[3] = tr.blend * 255;
	R_LightLambert( g_studio.lightpos[ptricmds[0]], pstudionorms[ptricmds[1]], lv, color );
}

static void R_StudioSetColorBegin( short *ptricmds, vec3_t *pstudionorms )
{
	rgba_t color;

	R_StudioSetColorArray( ptricmds, pstudionorms, color );
	pglColor4ubv( color );
}

/*
====================
R_LightStrength

====================
*/
static void R_LightStrength( int bone, const vec3_t localpos, vec4_t light[MAX_LOCALLIGHTS] )
{
	int	i;

	if( !g_studio.numlocallights )
		return;

	if( g_studio.lightage[bone] != g_studio.framecount )
	{
		for( i = 0; i < g_studio.numlocallights; i++ )
		{
			const dlight_t *el = g_studio.locallight[i];
			Matrix3x4_VectorITransform( g_studio.lighttransform[bone], el->origin, g_studio.lightbonepos[bone][i] );
		}

		g_studio.lightage[bone] = g_studio.framecount;
	}

	for( i = 0; i < g_studio.numlocallights; i++ )
	{
		VectorSubtract( localpos, g_studio.lightbonepos[bone][i], light[i] );
		light[i][3] = 0.0f;
	}
}

/*
===============
R_StudioSetupSkin

===============
*/
static void R_StudioSetupSkin( studiohdr_t *ptexturehdr, int index )
{
	mstudiotexture_t	*ptexture = NULL;

	if( FBitSet( g_nForceFaceFlags, STUDIO_NF_CHROME ))
		return;

	if( ptexturehdr == NULL )
		return;

	// NOTE: user may ignore to call StudioRemapColors and remap_info will be unavailable
	if( m_fDoRemap ) ptexture = gEngfuncs.CL_GetRemapInfoForEntity( RI.currententity )->ptexture;
	if( !ptexture ) ptexture = (mstudiotexture_t *)((byte *)ptexturehdr + ptexturehdr->textureindex); // fallback

	if( r_lightmap->value && !r_fullbright->value )
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	else GL_Bind( XASH_TEXTURE0, ptexture[index].index );
}

/*
===============
R_StudioGetTexture

Doesn't changes studio global state at all
===============
*/
mstudiotexture_t *R_StudioGetTexture( cl_entity_t *e )
{
	mstudiotexture_t	*ptexture;
	studiohdr_t	*phdr, *thdr;

	if(( phdr = gEngfuncs.Mod_Extradata( mod_studio, e->model )) == NULL )
		return NULL;

	thdr = m_pStudioHeader;
	if( !thdr ) return NULL;

	if( m_fDoRemap ) ptexture = gEngfuncs.CL_GetRemapInfoForEntity( e )->ptexture;
	else ptexture = (mstudiotexture_t *)((byte *)thdr + thdr->textureindex);

	return ptexture;
}

static void R_StudioSetRenderamt( int iRenderamt )
{
	if( !RI.currententity ) return;

	RI.currententity->curstate.renderamt = iRenderamt;
	tr.blend = CL_FxBlend( RI.currententity ) / 255.0f;
}

/*
===============
R_StudioSetCullState

sets true for enable backculling (for left-hand viewmodel)
===============
*/
static void R_StudioSetCullState( int iCull )
{
	// This function intentionally does nothing
}

/*
===============
R_StudioRenderShadow

just a prefab for render shadow
===============
*/
static void R_StudioRenderShadow( int iSprite, float *p1, float *p2, float *p3, float *p4 )
{
	if( !p1 || !p2 || !p3 || !p4 )
		return;

	if( TriSpriteTexture( CL_ModelHandle( iSprite ), 0 ))
	{
		TriRenderMode( kRenderTransAlpha );
		TriColor4f( 0.0f, 0.0f, 0.0f, 1.0f );

		pglBegin( GL_QUADS );
			pglTexCoord2f( 0.0f, 0.0f );
			pglVertex3fv( p1 );
			pglTexCoord2f( 0.0f, 1.0f );
			pglVertex3fv( p2 );
			pglTexCoord2f( 1.0f, 1.0f );
			pglVertex3fv( p3 );
			pglTexCoord2f( 1.0f, 0.0f );
			pglVertex3fv( p4 );
		pglEnd();

		TriRenderMode( kRenderNormal );
	}
}

/*
===============
R_StudioMeshCompare

Three-way sort to batch GL state changes:
  opaque (0) → masked (1) → additive (2)
Opaque meshes first minimises depth-mask flips and alpha-test toggling.
*/
static int R_StudioMeshCompare( const void *a, const void *b )
{
	const sortedmesh_t *ma = (const sortedmesh_t *)a;
	const sortedmesh_t *mb = (const sortedmesh_t *)b;

	if( ma->sortkey < mb->sortkey )
		return -1;
	if( ma->sortkey > mb->sortkey )
		return 1;

	return 0;
}

static int R_StudioMeshSortKey( int flags, int skinref )
{
	int bucket = 0;

	if( FBitSet( flags, STUDIO_NF_ADDITIVE ))
		bucket = 2;
	else if( FBitSet( flags, STUDIO_NF_MASKED ))
		bucket = 1;

	return ( bucket << 24 ) | ( skinref & 0x00FFFFFF );
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
static void R_StudioDrawNormalMesh( short *ptricmds, vec3_t *pstudionorms, float s, float t )
{
	int	i;

	while(( i = *( ptricmds++ )))
	{
		if( i < 0 )
		{
			pglBegin( GL_TRIANGLE_FAN );
			i = -i;
		}
		else pglBegin( GL_TRIANGLE_STRIP );

		for( ; i > 0; i--, ptricmds += 4 )
		{
			R_StudioSetColorBegin( ptricmds, pstudionorms );

			pglTexCoord2f( ptricmds[2] * s, ptricmds[3] * t );
			pglVertex3fv( g_studio.verts[ptricmds[0]] );
		}

		pglEnd();
	}
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
static void R_StudioDrawFloatMesh( short *ptricmds, vec3_t *pstudionorms )
{
	int	i;

	while(( i = *( ptricmds++ )))
	{
		if( i < 0 )
		{
			pglBegin( GL_TRIANGLE_FAN );
			i = -i;
		}
		else pglBegin( GL_TRIANGLE_STRIP );

		for( ; i > 0; i--, ptricmds += 4 )
		{
			R_StudioSetColorBegin( ptricmds, pstudionorms );
			pglTexCoord2f( HalfToFloat( ptricmds[2] ), HalfToFloat( ptricmds[3] ));
			pglVertex3fv( g_studio.verts[ptricmds[0]] );
		}

		pglEnd();
	}
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
static void R_StudioDrawChromeMesh( short *ptricmds, vec3_t *pstudionorms, float s, float t, float scale )
{
	float	*lv, *av;
	int	i, idx;
	qboolean	glowShell = (scale > 0.0f) ? true : false;
	vec3_t	vert;

	while(( i = *( ptricmds++ )))
	{
		if( i < 0 )
		{
			pglBegin( GL_TRIANGLE_FAN );
			i = -i;
		}
		else pglBegin( GL_TRIANGLE_STRIP );

		for( ; i > 0; i--, ptricmds += 4 )
		{
			if( glowShell )
			{
				color24 *clr = &RI.currententity->curstate.rendercolor;

				idx = g_studio.normaltable[ptricmds[0]];
				av = g_studio.verts[ptricmds[0]];
				lv = g_studio.norms[ptricmds[0]];
				VectorMA( av, scale, lv, vert );
				pglColor4ub( clr->r, clr->g, clr->b, 255 );
				pglTexCoord2f( g_studio.chrome[idx][0] * s, g_studio.chrome[idx][1] * t );
				pglVertex3fv( vert );
			}
			else
			{
				idx = ptricmds[1];
				lv = (float *)g_studio.lightvalues[ptricmds[1]];
				R_StudioSetColorBegin( ptricmds, pstudionorms );
				pglTexCoord2f( g_studio.chrome[idx][0] * s, g_studio.chrome[idx][1] * t );
				pglVertex3fv( g_studio.verts[ptricmds[0]] );
			}
		}

		pglEnd();
	}
}


static inline qboolean R_StudioArrayHasRoom( uint addverts, uint addelems )
{
	return ( g_studio.numverts + addverts <= MAXSTUDIOVERTS ) &&
		( g_studio.numelems + addelems <= MAXSTUDIOVERTS * 6 );
}

static qboolean R_StudioBuildIndices( qboolean tri_strip, int *vertexState )
{
	const int oldState = *vertexState;
	const int newState = oldState + 1;

	if( !R_StudioArrayHasRoom( 1, oldState < 3 ? 1 : 3 ))
		return false;

	*vertexState = newState;

	// build in indices
	if( oldState < 3 )
	{
		g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts;
	}
	else if( tri_strip )
	{
		// flip triangles between clockwise and counter clockwise
		if( newState & 1 )
		{
			// draw triangle [n-2 n-1 n]
			g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts - 2;
			g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts - 1;
			g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts;
		}
		else
		{
			// draw triangle [n-1 n-2 n]
			g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts - 1;
			g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts - 2;
			g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts;
		}
	}
	else
	{
		// draw triangle fan [0 n-1 n]
		g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts - ( newState - 1 );
		g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts - 1;
		g_studio.arrayelems[g_studio.numelems++] = g_studio.numverts;
	}

	return true;
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
static void R_StudioBuildArrayNormalMesh( short *ptricmds, vec3_t *pstudionorms, float s, float t )
{
	int	i;

	while(( i = *( ptricmds++ )))
	{
		int vertexState = 0;
		qboolean tri_strip = true;

		if( i < 0 )
		{
			tri_strip = false;
			i = -i;
		}

		for( ; i > 0; i--, ptricmds += 4 )
		{
			GLubyte *cl;
			cl = g_studio.arraycolor[g_studio.numverts];

			if( !R_StudioBuildIndices( tri_strip, &vertexState ))
				return;

			R_StudioSetColorArray( ptricmds, pstudionorms, cl );

			g_studio.arraycoord[g_studio.numverts][0] = ptricmds[2] * s;
			g_studio.arraycoord[g_studio.numverts][1] = ptricmds[3] * t;

			VectorCopy( g_studio.verts[ptricmds[0]], g_studio.arrayverts[g_studio.numverts] );
			g_studio.numverts++;
		}
	}
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
static void R_StudioBuildArrayFloatMesh( short *ptricmds, vec3_t *pstudionorms )
{
	int	i;

	while(( i = *( ptricmds++ )))
	{
		int vertexState = 0;
		qboolean tri_strip = true;

		if( i < 0 )
		{
			tri_strip = false;
			i = -i;
		}

		for( ; i > 0; i--, ptricmds += 4 )
		{
			GLubyte *cl;
			cl = g_studio.arraycolor[g_studio.numverts];

			if( !R_StudioBuildIndices( tri_strip, &vertexState ))
				return;

			R_StudioSetColorArray( ptricmds, pstudionorms, cl );

			g_studio.arraycoord[g_studio.numverts][0] = HalfToFloat( ptricmds[2] );
			g_studio.arraycoord[g_studio.numverts][1] = HalfToFloat( ptricmds[3] );

			VectorCopy( g_studio.verts[ptricmds[0]], g_studio.arrayverts[g_studio.numverts] );
			g_studio.numverts++;
		}
	}
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
static void R_StudioBuildArrayChromeMesh( short *ptricmds, vec3_t *pstudionorms, float s, float t, float scale )
{
	float	*lv, *av;
	int	i, idx;
	qboolean	glowShell = (scale > 0.0f) ? true : false;
	vec3_t	vert;

	while(( i = *( ptricmds++ )))
	{
		int vertexState = 0;
		qboolean tri_strip = true;

		if( i < 0 )
		{
			tri_strip = false;
			i = -i;
		}

		for( ; i > 0; i--, ptricmds += 4 )
		{
			GLubyte *cl;
			cl = g_studio.arraycolor[g_studio.numverts];

			if( !R_StudioBuildIndices( tri_strip, &vertexState ))
				return;

			if( glowShell )
			{
				idx = g_studio.normaltable[ptricmds[0]];
				av = g_studio.verts[ptricmds[0]];
				lv = g_studio.norms[ptricmds[0]];

				cl[0] = RI.currententity->curstate.rendercolor.r;
				cl[1] = RI.currententity->curstate.rendercolor.g;
				cl[2] = RI.currententity->curstate.rendercolor.b;
				cl[3] = 255;

				VectorMA( av, scale, lv, vert );
				VectorCopy( vert, g_studio.arrayverts[g_studio.numverts] );
			}
			else
			{
				idx = ptricmds[1];
				R_StudioSetColorArray( ptricmds, pstudionorms, cl );

				VectorCopy( g_studio.verts[ptricmds[0]], g_studio.arrayverts[g_studio.numverts] );
			}

			g_studio.arraycoord[g_studio.numverts][0] = g_studio.chrome[idx][0] * s;
			g_studio.arraycoord[g_studio.numverts][1] = g_studio.chrome[idx][1] * t;

			g_studio.numverts++;
		}
	}
}

/* -----------------------------------------------------------------------
 * Mesh cache helpers
 * ----------------------------------------------------------------------- */

/*
 * R_StudioMeshCacheFind
 * Returns an existing cache entry for (submodel, material_key), or NULL.
 */
static studio_mesh_cache_t *R_StudioMeshCacheFind( const mstudiomodel_t *sub, uint32_t mkey )
{
	int i;
	for( i = 0; i < s_mesh_cache_used; i++ )
	{
		if( s_mesh_cache[i].valid &&
		    s_mesh_cache[i].submodel == sub &&
		    s_mesh_cache[i].material_key == mkey )
			return &s_mesh_cache[i];
	}
	return NULL;
}

/*
 * R_StudioMeshCacheAlloc
 * Allocates a new cache slot. Evicts oldest entry (LRU-lite) when full.
 */
static studio_mesh_cache_t *R_StudioMeshCacheAlloc( const mstudiomodel_t *sub, uint32_t mkey )
{
	studio_mesh_cache_t *slot;

	if( s_mesh_cache_used < STUDIO_MESH_CACHE_SIZE )
	{
		slot = &s_mesh_cache[s_mesh_cache_used++];
	}
	else
	{
		// Evict slot 0, shift ring (simple FIFO eviction)
		slot = &s_mesh_cache[0];
		if( slot->vbo ) pglDeleteBuffersARB( 1, &slot->vbo );
		if( slot->ibo ) pglDeleteBuffersARB( 1, &slot->ibo );
		memmove( &s_mesh_cache[0], &s_mesh_cache[1],
		         (STUDIO_MESH_CACHE_SIZE - 1) * sizeof( studio_mesh_cache_t ));
		slot = &s_mesh_cache[STUDIO_MESH_CACHE_SIZE - 1];
	}

	memset( slot, 0, sizeof( *slot ));
	slot->submodel      = sub;
	slot->material_key  = mkey;
	return slot;
}

/*
 * R_StudioFlushMeshCache
 * Called on map change / model unload to free all GPU buffers.
 */
void R_StudioFlushMeshCache( void )
{
	int i;
	for( i = 0; i < s_mesh_cache_used; i++ )
	{
		if( s_mesh_cache[i].vbo ) pglDeleteBuffersARB( 1, &s_mesh_cache[i].vbo );
		if( s_mesh_cache[i].ibo ) pglDeleteBuffersARB( 1, &s_mesh_cache[i].ibo );
	}
	memset( s_mesh_cache, 0, sizeof( s_mesh_cache ));
	s_mesh_cache_used = 0;

	R_StudioFreeStreamBuffers();
	R_StudioFreeGPUMeshCache();
}

/*
 * R_StudioBuildMeshCache
 * Converts the current arrayverts/arraycoord/arraycolor/arrayelems into a
 * persistent VBO+IBO and stores it in the cache.
 * Only called when r_studio_meshcache is enabled and GL_ARB_VERTEX_BUFFER_OBJECT_EXT
 * is available.
 */
static studio_mesh_cache_t *R_StudioBuildMeshCache( const mstudiomodel_t *sub,
                                                     uint32_t mkey,
                                                     uint startverts, uint startelems,
                                                     uint numverts, uint numelems )
{
	studio_mesh_cache_t *slot;

	if( !GL_Support( GL_ARB_VERTEX_BUFFER_OBJECT_EXT ))
		return NULL;

	if( numelems == 0 || numverts == 0 )
		return NULL;

	slot = R_StudioMeshCacheAlloc( sub, mkey );

	// Build an interleaved vertex buffer: [pos(12) + uv(8) + color(4)] = 24 bytes
	{
		typedef struct { float x, y, z, s, t; GLubyte r, g, b, a; } studiovert_t;
		studiovert_t *buf = (studiovert_t *)Mem_Malloc( r_temppool, numverts * sizeof( studiovert_t ));
		uint i;

		for( i = 0; i < numverts; i++ )
		{
			buf[i].x = g_studio.arrayverts[startverts + i][0];
			buf[i].y = g_studio.arrayverts[startverts + i][1];
			buf[i].z = g_studio.arrayverts[startverts + i][2];
			buf[i].s = g_studio.arraycoord[startverts + i][0];
			buf[i].t = g_studio.arraycoord[startverts + i][1];
			buf[i].r = g_studio.arraycolor[startverts + i][0];
			buf[i].g = g_studio.arraycolor[startverts + i][1];
			buf[i].b = g_studio.arraycolor[startverts + i][2];
			buf[i].a = g_studio.arraycolor[startverts + i][3];
		}

		pglGenBuffersARB( 1, &slot->vbo );
		pglBindBufferARB( GL_ARRAY_BUFFER_ARB, slot->vbo );
		pglBufferDataARB( GL_ARRAY_BUFFER_ARB, numverts * sizeof( studiovert_t ), buf, GL_STATIC_DRAW_ARB );
		pglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		Mem_Free( buf );
	}

	// IBO
	{
		pglGenBuffersARB( 1, &slot->ibo );
		pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, slot->ibo );
		pglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB,
		                  numelems * sizeof( unsigned short ),
		                  &g_studio.arrayelems[startelems], GL_STATIC_DRAW_ARB );
		pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
	}

	slot->num_verts = numverts;
	slot->num_elems = numelems;
	slot->valid     = true;
	return slot;
}

/*
 * R_StudioDrawCachedMesh
 * Draw a mesh from VBO/IBO.  Stride layout matches R_StudioBuildMeshCache.
 */
static void R_StudioDrawCachedMesh( const studio_mesh_cache_t *slot )
{
	// stride: float3 pos + float2 uv + ubyte4 color = 24 bytes
	const GLsizei stride = (GLsizei)(3 * sizeof(float) + 2 * sizeof(float) + 4 * sizeof(GLubyte));

	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, slot->vbo );
	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, slot->ibo );

	pglEnableClientState( GL_VERTEX_ARRAY );
	pglVertexPointer( 3, GL_FLOAT, stride, (void*)0 );

	pglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	pglTexCoordPointer( 2, GL_FLOAT, stride, (void*)(3 * sizeof(float)) );

	if( !( g_nForceFaceFlags & STUDIO_NF_CHROME ))
	{
		pglEnableClientState( GL_COLOR_ARRAY );
		pglColorPointer( 4, GL_UNSIGNED_BYTE, stride,
		                 (void*)(3 * sizeof(float) + 2 * sizeof(float)) );
	}

	if( pglDrawRangeElements )
		pglDrawRangeElements( GL_TRIANGLES, 0, slot->num_verts - 1,
		                      slot->num_elems, GL_UNSIGNED_SHORT, (void*)0 );
	else
		pglDrawElements( GL_TRIANGLES, slot->num_elems, GL_UNSIGNED_SHORT, (void*)0 );

	pglDisableClientState( GL_VERTEX_ARRAY );
	pglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	if( !( g_nForceFaceFlags & STUDIO_NF_CHROME ))
		pglDisableClientState( GL_COLOR_ARRAY );

	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
}

/*
 * R_StudioFreeStreamBuffers
 * Frees safe dynamic VBO/IBO stream buffers used for animated Studio meshes.
 */
static void R_StudioFreeStreamBuffers( void )
{
	if( s_stream_vbo )
	{
		pglDeleteBuffersARB( 1, &s_stream_vbo );
		s_stream_vbo = 0;
	}

	if( s_stream_ibo )
	{
		pglDeleteBuffersARB( 1, &s_stream_ibo );
		s_stream_ibo = 0;
	}
}

static qboolean R_StudioEnsureStreamBuffers( void )
{
	if( !GL_Support( GL_ARB_VERTEX_BUFFER_OBJECT_EXT ))
		return false;

	if( !pglGenBuffersARB || !pglBindBufferARB || !pglBufferDataARB )
		return false;

	if( !s_stream_vbo )
		pglGenBuffersARB( 1, &s_stream_vbo );
	if( !s_stream_ibo )
		pglGenBuffersARB( 1, &s_stream_ibo );

	return ( s_stream_vbo != 0 && s_stream_ibo != 0 ) ? true : false;
}

/*
 * R_StudioDrawStreamVBO
 * Safe RX 580 path: stream the current animated/skinned mesh to VBO/IBO.
 * This is intentionally dynamic. It does not cache world-space vertices across frames,
 * so movement and animation remain correct while still exercising the driver VBO path.
 */
static qboolean R_StudioDrawStreamVBO( uint startverts, uint startelems )
{
	uint i;
	const uint numverts = g_studio.numverts - startverts;
	const uint numelems = g_studio.numelems - startelems;
	const GLsizei stride = (GLsizei)sizeof( studio_stream_vert_t );

	if( !r_studio_meshcache.value )
		return false;
	if( !numverts || !numelems )
		return false;
	if( startverts >= g_studio.numverts || startelems >= g_studio.numelems )
		return false;
	if( numverts > MAXSTUDIOVERTS || numelems > MAXSTUDIOVERTS * 6 )
		return false;
	if( !R_StudioEnsureStreamBuffers() )
		return false;

	for( i = 0; i < numverts; i++ )
	{
		const uint src = startverts + i;
		studio_stream_vert_t *dst = &s_stream_vertices[i];

		dst->x = g_studio.arrayverts[src][0];
		dst->y = g_studio.arrayverts[src][1];
		dst->z = g_studio.arrayverts[src][2];
		dst->s = g_studio.arraycoord[src][0];
		dst->t = g_studio.arraycoord[src][1];
		dst->r = g_studio.arraycolor[src][0];
		dst->g = g_studio.arraycolor[src][1];
		dst->b = g_studio.arraycolor[src][2];
		dst->a = g_studio.arraycolor[src][3];
	}

	for( i = 0; i < numelems; i++ )
	{
		const unsigned short idx = g_studio.arrayelems[startelems + i];

		if( idx < startverts || idx >= g_studio.numverts )
			return false;

		s_stream_indices[i] = (unsigned short)( idx - startverts );
	}

	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, s_stream_vbo );
	pglBufferDataARB( GL_ARRAY_BUFFER_ARB,
		(GLsizeiptrARB)( numverts * sizeof( studio_stream_vert_t )),
		s_stream_vertices, GL_STREAM_DRAW_ARB );

	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, s_stream_ibo );
	pglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB,
		(GLsizeiptrARB)( numelems * sizeof( unsigned short )),
		s_stream_indices, GL_STREAM_DRAW_ARB );

	pglEnableClientState( GL_VERTEX_ARRAY );
	pglVertexPointer( 3, GL_FLOAT, stride, (void*)0 );

	pglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	pglTexCoordPointer( 2, GL_FLOAT, stride, (void*)( 3 * sizeof( float )) );

	if( !( g_nForceFaceFlags & STUDIO_NF_CHROME ) )
	{
		pglEnableClientState( GL_COLOR_ARRAY );
		pglColorPointer( 4, GL_UNSIGNED_BYTE, stride,
			(void*)( 3 * sizeof( float ) + 2 * sizeof( float )) );
	}

#if !defined XASH_NANOGL || defined XASH_WES && XASH_EMSCRIPTEN
	if( pglDrawRangeElements )
		pglDrawRangeElements( GL_TRIANGLES, 0, numverts - 1,
			numelems, GL_UNSIGNED_SHORT, (void*)0 );
	else
#endif
		pglDrawElements( GL_TRIANGLES, numelems, GL_UNSIGNED_SHORT, (void*)0 );

	pglDisableClientState( GL_VERTEX_ARRAY );
	pglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	if( !( g_nForceFaceFlags & STUDIO_NF_CHROME ) )
		pglDisableClientState( GL_COLOR_ARRAY );

	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
	return true;
}

/*
 * R_StudioPackBoneRows
 * Pack g_studio.worldtransform into flat float vec4-rows for GPU skinning.
 * Layout: s_bone_rows[bone*3+row][0..3] = row-vector of 3x4 matrix.
 */
static void R_StudioPackBoneRows( int numbones )
{
	int b, r;
	for( b = 0; b < numbones; b++ )
	{
		for( r = 0; r < 3; r++ )
		{
			float *dst = s_bone_rows + (b * 3 + r) * 4;
			dst[0] = g_studio.worldtransform[b][r][0];
			dst[1] = g_studio.worldtransform[b][r][1];
			dst[2] = g_studio.worldtransform[b][r][2];
			dst[3] = g_studio.worldtransform[b][r][3];
		}
	}
}

/* -----------------------------------------------------------------------
 * Persistent model-space mesh cache + experimental GLSL uniform skinning
 * ----------------------------------------------------------------------- */
static void R_StudioFreeUniformShader( void )
{
	if( s_studio_uniform_shader.program )
	{
		if( pglUseProgramObjectARB ) pglUseProgramObjectARB( 0 );
		if( pglDeleteObjectARB ) pglDeleteObjectARB( s_studio_uniform_shader.program );
	}
	if( s_studio_uniform_shader.vertex_shader && pglDeleteObjectARB )
		pglDeleteObjectARB( s_studio_uniform_shader.vertex_shader );
	if( s_studio_uniform_shader.fragment_shader && pglDeleteObjectARB )
		pglDeleteObjectARB( s_studio_uniform_shader.fragment_shader );
	memset( &s_studio_uniform_shader, 0, sizeof( s_studio_uniform_shader ));
}

static void R_StudioFreeGPUMeshCache( void )
{
	int i;
	for( i = 0; i < s_gpu_mesh_cache_used; i++ )
	{
		if( s_gpu_mesh_cache[i].static_vbo ) pglDeleteBuffersARB( 1, &s_gpu_mesh_cache[i].static_vbo );
		if( s_gpu_mesh_cache[i].static_ibo ) pglDeleteBuffersARB( 1, &s_gpu_mesh_cache[i].static_ibo );
	}
	memset( s_gpu_mesh_cache, 0, sizeof( s_gpu_mesh_cache ));
	s_gpu_mesh_cache_used = 0;
	R_StudioFreeUniformShader();
}

static uint32_t R_StudioMakeCacheTag( const studiohdr_t *hdr, const mstudiomodel_t *sub, const mstudiomesh_t *mesh, int skinref, int flags )
{
	uintptr_t h = (uintptr_t)hdr;
	uintptr_t s = (uintptr_t)sub;
	uintptr_t m = (uintptr_t)mesh;
	uint32_t tag = (uint32_t)((h >> 4) ^ (s >> 7) ^ (m >> 10));
	tag ^= (uint32_t)( skinref * 1315423911u );
	tag ^= (uint32_t)( flags * 2654435761u );
	return tag;
}

static studio_gpu_mesh_t *R_StudioFindGPUMesh( const studiohdr_t *hdr, const mstudiomodel_t *sub, const mstudiomesh_t *mesh, int skinref, int flags )
{
	uint32_t tag = R_StudioMakeCacheTag( hdr, sub, mesh, skinref, flags );
	int i;
	for( i = 0; i < s_gpu_mesh_cache_used; i++ )
	{
		studio_gpu_mesh_t *c = &s_gpu_mesh_cache[i];
		if( c->valid && c->header == hdr && c->submodel == sub && c->mesh == mesh && c->skinref == skinref && c->flags == flags && c->cache_tag == tag )
			return c;
	}
	return NULL;
}

static studio_gpu_mesh_t *R_StudioAllocGPUMesh( const studiohdr_t *hdr, const mstudiomodel_t *sub, const mstudiomesh_t *mesh, int skinref, int flags )
{
	studio_gpu_mesh_t *slot;
	if( s_gpu_mesh_cache_used < STUDIO_GPU_MESH_CACHE_SIZE )
		slot = &s_gpu_mesh_cache[s_gpu_mesh_cache_used++];
	else
	{
		slot = &s_gpu_mesh_cache[0];
		if( slot->static_vbo ) pglDeleteBuffersARB( 1, &slot->static_vbo );
		if( slot->static_ibo ) pglDeleteBuffersARB( 1, &slot->static_ibo );
		memmove( &s_gpu_mesh_cache[0], &s_gpu_mesh_cache[1], ( STUDIO_GPU_MESH_CACHE_SIZE - 1 ) * sizeof( studio_gpu_mesh_t ));
		slot = &s_gpu_mesh_cache[STUDIO_GPU_MESH_CACHE_SIZE - 1];
	}
	memset( slot, 0, sizeof( *slot ));
	slot->header = hdr;
	slot->submodel = sub;
	slot->mesh = mesh;
	slot->skinref = skinref;
	slot->flags = flags;
	slot->cache_tag = R_StudioMakeCacheTag( hdr, sub, mesh, skinref, flags );
	return slot;
}

static qboolean R_StudioStaticIndexAppend( qboolean tri_strip, int *vertexState, uint curvert, unsigned short *indices, uint *numelems, uint maxelems )
{
	const int oldState = *vertexState;
	const int newState = oldState + 1;
	if( oldState < 3 )
	{
		if( *numelems + 1 > maxelems ) return false;
		indices[(*numelems)++] = (unsigned short)curvert;
	}
	else if( tri_strip )
	{
		if( *numelems + 3 > maxelems ) return false;
		if( newState & 1 )
		{
			indices[(*numelems)++] = (unsigned short)( curvert - 2 );
			indices[(*numelems)++] = (unsigned short)( curvert - 1 );
			indices[(*numelems)++] = (unsigned short)curvert;
		}
		else
		{
			indices[(*numelems)++] = (unsigned short)( curvert - 1 );
			indices[(*numelems)++] = (unsigned short)( curvert - 2 );
			indices[(*numelems)++] = (unsigned short)curvert;
		}
	}
	else
	{
		if( *numelems + 3 > maxelems ) return false;
		indices[(*numelems)++] = (unsigned short)( curvert - ( newState - 1 ));
		indices[(*numelems)++] = (unsigned short)( curvert - 1 );
		indices[(*numelems)++] = (unsigned short)curvert;
	}
	*vertexState = newState;
	return true;
}

static void R_StudioFillBoneWeight( studio_static_vertex_t *dst, int vindex, byte *pvertbone, mstudioboneweight_t *pvertweight )
{
	int i;
	memset( dst->bone, 0, sizeof( dst->bone ));
	memset( dst->weight, 0, sizeof( dst->weight ));

	if( pvertweight )
	{
		for( i = 0; i < 4; i++ )   /* MAXSTUDIOBONEWEIGHTS ja e 4 */
		{
			if( pvertweight[vindex].bone[i] < 0 )
				break;
			dst->bone[i] = (GLubyte)pvertweight[vindex].bone[i];
			dst->weight[i] = (GLubyte)pvertweight[vindex].weight[i];
		}
		if( dst->weight[0] == 0 && dst->weight[1] == 0 && dst->weight[2] == 0 && dst->weight[3] == 0 )
			dst->weight[0] = 255;
	}
	else
	{
		dst->bone[0] = pvertbone ? (GLubyte)pvertbone[vindex] : 0;
		dst->weight[0] = 255;
	}
}

static studio_gpu_mesh_t *R_StudioBuildModelSpaceMesh( mstudiomesh_t *pmesh, int skinref, int flags, float s, float t,
	vec3_t *pstudioverts, vec3_t *pstudionorms, byte *pvertbone, mstudioboneweight_t *pvertweight )
{
	short *ptricmds;
	studio_static_vertex_t *verts;
	unsigned short *indices;
	studio_gpu_mesh_t *slot;
	uint numverts = 0, numelems = 0;
	uint maxverts = 0, maxelems = 0;
	int i;

	if( !GL_Support( GL_ARB_VERTEX_BUFFER_OBJECT_EXT ) || !pglGenBuffersARB || !pglBindBufferARB || !pglBufferDataARB )
		return NULL;

	ptricmds = (short *)((byte *)m_pStudioHeader + pmesh->triindex);
	while(( i = *( ptricmds++ )))
	{
		if( i < 0 ) i = -i;
		maxverts += i;
		if( i >= 3 ) maxelems += 3 + ( i - 3 ) * 3;
		ptricmds += i * 4;
	}

	if( maxverts == 0 || maxelems == 0 || maxverts > MAXSTUDIOVERTS || maxelems > MAXSTUDIOVERTS * 6 )
		return NULL;

	verts = (studio_static_vertex_t *)Mem_Malloc( r_temppool, maxverts * sizeof( studio_static_vertex_t ));
	indices = (unsigned short *)Mem_Malloc( r_temppool, maxelems * sizeof( unsigned short ));
	if( !verts || !indices )
	{
		if( verts ) Mem_Free( verts );
		if( indices ) Mem_Free( indices );
		return NULL;
	}

	ptricmds = (short *)((byte *)m_pStudioHeader + pmesh->triindex);
	while(( i = *( ptricmds++ )))
	{
		int vertexState = 0;
		qboolean tri_strip = true;
		if( i < 0 )
		{
			tri_strip = false;
			i = -i;
		}

		for( ; i > 0; i--, ptricmds += 4 )
		{
			int vindex = ptricmds[0];
			int nindex = ptricmds[1];
			studio_static_vertex_t *dst;

			if( numverts >= maxverts ) break;
			if( !R_StudioStaticIndexAppend( tri_strip, &vertexState, numverts, indices, &numelems, maxelems )) break;

			dst = &verts[numverts];
			VectorCopy( pstudioverts[vindex], dst->pos );
			VectorCopy( pstudionorms[nindex], dst->normal );
			if( FBitSet( flags, STUDIO_NF_UV_COORDS ))
			{
				dst->st[0] = HalfToFloat( ptricmds[2] );
				dst->st[1] = HalfToFloat( ptricmds[3] );
			}
			else
			{
				dst->st[0] = ptricmds[2] * s;
				dst->st[1] = ptricmds[3] * t;
			}
			R_StudioFillBoneWeight( dst, vindex, pvertbone, pvertweight );
			numverts++;
		}
	}

	if( numverts == 0 || numelems == 0 )
	{
		Mem_Free( verts );
		Mem_Free( indices );
		return NULL;
	}

	slot = R_StudioAllocGPUMesh( m_pStudioHeader, m_pSubModel, pmesh, skinref, flags );
	pglGenBuffersARB( 1, &slot->static_vbo );
	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, slot->static_vbo );
	pglBufferDataARB( GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)( numverts * sizeof( studio_static_vertex_t )), verts, GL_STATIC_DRAW_ARB );
	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );

	pglGenBuffersARB( 1, &slot->static_ibo );
	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, slot->static_ibo );
	pglBufferDataARB( GL_ELEMENT_ARRAY_BUFFER_ARB, (GLsizeiptrARB)( numelems * sizeof( unsigned short )), indices, GL_STATIC_DRAW_ARB );
	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );

	slot->num_vertices = numverts;
	slot->num_indices = numelems;
	slot->layout_flags = 0;
	slot->valid = true;

	Mem_Free( verts );
	Mem_Free( indices );
	return slot;
}

static studio_gpu_mesh_t *R_StudioGetModelSpaceMesh( mstudiomesh_t *pmesh, int skinref, int flags, float s, float t,
	vec3_t *pstudioverts, vec3_t *pstudionorms, byte *pvertbone, mstudioboneweight_t *pvertweight )
{
	studio_gpu_mesh_t *slot = R_StudioFindGPUMesh( m_pStudioHeader, m_pSubModel, pmesh, skinref, flags );
	if( slot ) return slot;
	return R_StudioBuildModelSpaceMesh( pmesh, skinref, flags, s, t, pstudioverts, pstudionorms, pvertbone, pvertweight );
}

static const char *studio_gpu_vs =
"#version 110\n"
"attribute vec3 a_Position;\n"
"attribute vec3 a_Normal;\n"
"attribute vec2 a_TexCoord0;\n"
"attribute vec4 a_BoneIndex;\n"
"attribute vec4 a_BoneWeight;\n"
"uniform vec4 u_BoneRows[384];\n"
"uniform vec3 u_LightDir;\n"
"uniform vec3 u_Ambient;\n"
"uniform vec3 u_Diffuse;\n"
"varying vec2 v_TexCoord;\n"
"varying vec4 v_Color;\n"
"vec3 bonePos(int bone, vec4 p){ vec4 r0=u_BoneRows[bone*3+0]; vec4 r1=u_BoneRows[bone*3+1]; vec4 r2=u_BoneRows[bone*3+2]; return vec3(dot(r0,p),dot(r1,p),dot(r2,p)); }\n"
"vec3 boneDir(int bone, vec3 n){ vec4 r0=u_BoneRows[bone*3+0]; vec4 r1=u_BoneRows[bone*3+1]; vec4 r2=u_BoneRows[bone*3+2]; return vec3(dot(r0.xyz,n),dot(r1.xyz,n),dot(r2.xyz,n)); }\n"
"void main(void){\n"
" vec4 p=vec4(a_Position,1.0);\n"
" int b0=int(a_BoneIndex.x); int b1=int(a_BoneIndex.y); int b2=int(a_BoneIndex.z); int b3=int(a_BoneIndex.w);\n"
" vec3 sp=bonePos(b0,p)*a_BoneWeight.x + bonePos(b1,p)*a_BoneWeight.y + bonePos(b2,p)*a_BoneWeight.z + bonePos(b3,p)*a_BoneWeight.w;\n"
" vec3 sn=boneDir(b0,a_Normal)*a_BoneWeight.x + boneDir(b1,a_Normal)*a_BoneWeight.y + boneDir(b2,a_Normal)*a_BoneWeight.z + boneDir(b3,a_Normal)*a_BoneWeight.w;\n"
" sn=normalize(sn); float ndl=max(dot(sn, normalize(u_LightDir)), 0.0);\n"
" v_Color=vec4(u_Ambient + u_Diffuse * ndl, 1.0); v_TexCoord=a_TexCoord0;\n"
" gl_Position=gl_ModelViewProjectionMatrix * vec4(sp,1.0);\n"
"}\n";

static const char *studio_gpu_fs =
"#version 110\n"
"uniform sampler2D u_Texture0;\n"
"varying vec2 v_TexCoord;\n"
"varying vec4 v_Color;\n"
"void main(void){ gl_FragColor = texture2D(u_Texture0, v_TexCoord) * v_Color; }\n";

static GLhandleARB R_StudioCompileShaderObject( GLenum type, const char *source )
{
	GLhandleARB obj;
	GLint ok = 0;
	if( !pglCreateShaderObjectARB || !pglShaderSourceARB || !pglCompileShaderARB || !pglGetObjectParameterivARB ) return 0;
	obj = pglCreateShaderObjectARB( type );
	if( !obj ) return 0;
	pglShaderSourceARB( obj, 1, &source, NULL );
	pglCompileShaderARB( obj );
	pglGetObjectParameterivARB( obj, GL_OBJECT_COMPILE_STATUS_ARB, &ok );
	if( !ok ) { if( pglDeleteObjectARB ) pglDeleteObjectARB( obj ); return 0; }
	return obj;
}

static qboolean R_StudioEnsureUniformShader( void )
{
	GLint ok = 0;
	if( s_studio_uniform_shader.ready ) return true;
	if( s_studio_uniform_shader.failed ) return false;
	if( !GL_Support( GL_SHADER_OBJECTS_EXT ) || !GL_Support( GL_SHADER_GLSL100_EXT )) { s_studio_uniform_shader.failed = true; return false; }
	if( !pglCreateProgramObjectARB || !pglAttachObjectARB || !pglLinkProgramARB || !pglUseProgramObjectARB ) { s_studio_uniform_shader.failed = true; return false; }
	s_studio_uniform_shader.vertex_shader = R_StudioCompileShaderObject( GL_VERTEX_SHADER_ARB, studio_gpu_vs );
	s_studio_uniform_shader.fragment_shader = R_StudioCompileShaderObject( GL_FRAGMENT_SHADER_ARB, studio_gpu_fs );
	if( !s_studio_uniform_shader.vertex_shader || !s_studio_uniform_shader.fragment_shader ) { R_StudioFreeUniformShader(); s_studio_uniform_shader.failed = true; return false; }
	s_studio_uniform_shader.program = pglCreateProgramObjectARB();
	pglAttachObjectARB( s_studio_uniform_shader.program, s_studio_uniform_shader.vertex_shader );
	pglAttachObjectARB( s_studio_uniform_shader.program, s_studio_uniform_shader.fragment_shader );
	if( pglBindAttribLocationARB )
	{
		pglBindAttribLocationARB( s_studio_uniform_shader.program, 0, "a_Position" );
		pglBindAttribLocationARB( s_studio_uniform_shader.program, 1, "a_Normal" );
		pglBindAttribLocationARB( s_studio_uniform_shader.program, 2, "a_TexCoord0" );
		pglBindAttribLocationARB( s_studio_uniform_shader.program, 3, "a_BoneIndex" );
		pglBindAttribLocationARB( s_studio_uniform_shader.program, 4, "a_BoneWeight" );
	}
	pglLinkProgramARB( s_studio_uniform_shader.program );
	pglGetObjectParameterivARB( s_studio_uniform_shader.program, GL_OBJECT_LINK_STATUS_ARB, &ok );
	if( !ok ) { R_StudioFreeUniformShader(); s_studio_uniform_shader.failed = true; return false; }
	s_studio_uniform_shader.a_position = pglGetAttribLocationARB ? pglGetAttribLocationARB( s_studio_uniform_shader.program, "a_Position" ) : 0;
	s_studio_uniform_shader.a_normal = pglGetAttribLocationARB ? pglGetAttribLocationARB( s_studio_uniform_shader.program, "a_Normal" ) : 1;
	s_studio_uniform_shader.a_texcoord0 = pglGetAttribLocationARB ? pglGetAttribLocationARB( s_studio_uniform_shader.program, "a_TexCoord0" ) : 2;
	s_studio_uniform_shader.a_bone_index = pglGetAttribLocationARB ? pglGetAttribLocationARB( s_studio_uniform_shader.program, "a_BoneIndex" ) : 3;
	s_studio_uniform_shader.a_bone_weight = pglGetAttribLocationARB ? pglGetAttribLocationARB( s_studio_uniform_shader.program, "a_BoneWeight" ) : 4;
	s_studio_uniform_shader.u_bone_rows = pglGetUniformLocationARB ? pglGetUniformLocationARB( s_studio_uniform_shader.program, "u_BoneRows" ) : -1;
	s_studio_uniform_shader.u_light_dir = pglGetUniformLocationARB ? pglGetUniformLocationARB( s_studio_uniform_shader.program, "u_LightDir" ) : -1;
	s_studio_uniform_shader.u_ambient = pglGetUniformLocationARB ? pglGetUniformLocationARB( s_studio_uniform_shader.program, "u_Ambient" ) : -1;
	s_studio_uniform_shader.u_diffuse = pglGetUniformLocationARB ? pglGetUniformLocationARB( s_studio_uniform_shader.program, "u_Diffuse" ) : -1;
	s_studio_uniform_shader.u_texture0 = pglGetUniformLocationARB ? pglGetUniformLocationARB( s_studio_uniform_shader.program, "u_Texture0" ) : -1;
	s_studio_uniform_shader.ready = true;
	return true;
}

static studio_skin_backend_t R_StudioSelectSkinBackend( void )
{
	int requested = 1;
	if( !r_studio_gpu_skinning.value ) return STUDIO_SKIN_CPU;
	requested = (int)gl_studio_skin_backend.value;
	if( requested <= 0 ) requested = STUDIO_SKIN_GPU_UNIFORM;
	if( requested == STUDIO_SKIN_GPU_TBO || requested == STUDIO_SKIN_GPU_SSBO ) requested = STUDIO_SKIN_GPU_UNIFORM;
	return (studio_skin_backend_t)requested;
}

static qboolean R_StudioShouldUseGPUUniformPath( int flags )
{
	(void)flags;

	/* Rescue/stability build:
	 * keep the GLSL/TBO/SSBO preparation code in the file, but never dispatch it yet.
	 * The previous fullprep version could report success from the shader path and skip
	 * the proven CPU Studio path, causing players/viewmodels/world Studio props to vanish.
	 * Re-enable this only after the shader path is split into a separate tested file.
	 */
	return false;
}

static qboolean R_StudioTryDrawGPUUniformMesh( mstudiomesh_t *pmesh, int skinref, int flags, float s, float t,
	vec3_t *pstudioverts, vec3_t *pstudionorms, byte *pvertbone, mstudioboneweight_t *pvertweight )
{
	studio_gpu_mesh_t *mesh;
	const GLsizei stride = (GLsizei)sizeof( studio_static_vertex_t );
	vec3_t lightdir, ambient, diffuse;
	if( !R_StudioShouldUseGPUUniformPath( flags )) return false;
	mesh = R_StudioGetModelSpaceMesh( pmesh, skinref, flags, s, t, pstudioverts, pstudionorms, pvertbone, pvertweight );
	if( !mesh || !mesh->valid || !mesh->static_vbo || !mesh->static_ibo ) return false;
	VectorCopy( g_studio.lightvec, lightdir );
	if( VectorIsNull( lightdir )) VectorSet( lightdir, 0.0f, 0.0f, 1.0f );
	VectorNormalize( lightdir );
	VectorScale( g_studio.lightcolor, Q_max( 0.05f, g_studio.ambientlight ), ambient );
	VectorScale( g_studio.lightcolor, Q_max( 0.25f, g_studio.shadelight ), diffuse );
	pglUseProgramObjectARB( s_studio_uniform_shader.program );
	if( s_studio_uniform_shader.u_texture0 >= 0 ) pglUniform1iARB( s_studio_uniform_shader.u_texture0, 0 );
	if( s_studio_uniform_shader.u_light_dir >= 0 ) pglUniform3fvARB( s_studio_uniform_shader.u_light_dir, 1, lightdir );
	if( s_studio_uniform_shader.u_ambient >= 0 ) pglUniform3fvARB( s_studio_uniform_shader.u_ambient, 1, ambient );
	if( s_studio_uniform_shader.u_diffuse >= 0 ) pglUniform3fvARB( s_studio_uniform_shader.u_diffuse, 1, diffuse );
	if( s_studio_uniform_shader.u_bone_rows >= 0 ) pglUniform4fvARB( s_studio_uniform_shader.u_bone_rows, m_pStudioHeader->numbones * 3, s_bone_rows );
	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, mesh->static_vbo );
	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, mesh->static_ibo );
	pglEnableVertexAttribArrayARB( s_studio_uniform_shader.a_position );
	pglVertexAttribPointerARB( s_studio_uniform_shader.a_position, 3, GL_FLOAT, GL_FALSE, stride, (void *)STUDIO_OFFSETOF( studio_static_vertex_t, pos ));
	pglEnableVertexAttribArrayARB( s_studio_uniform_shader.a_normal );
	pglVertexAttribPointerARB( s_studio_uniform_shader.a_normal, 3, GL_FLOAT, GL_FALSE, stride, (void *)STUDIO_OFFSETOF( studio_static_vertex_t, normal ));
	pglEnableVertexAttribArrayARB( s_studio_uniform_shader.a_texcoord0 );
	pglVertexAttribPointerARB( s_studio_uniform_shader.a_texcoord0, 2, GL_FLOAT, GL_FALSE, stride, (void *)STUDIO_OFFSETOF( studio_static_vertex_t, st ));
	pglEnableVertexAttribArrayARB( s_studio_uniform_shader.a_bone_index );
	pglVertexAttribPointerARB( s_studio_uniform_shader.a_bone_index, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, (void *)STUDIO_OFFSETOF( studio_static_vertex_t, bone ));
	pglEnableVertexAttribArrayARB( s_studio_uniform_shader.a_bone_weight );
	pglVertexAttribPointerARB( s_studio_uniform_shader.a_bone_weight, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)STUDIO_OFFSETOF( studio_static_vertex_t, weight ));
#if !defined XASH_NANOGL || defined XASH_WES && XASH_EMSCRIPTEN
	if( pglDrawRangeElements ) pglDrawRangeElements( GL_TRIANGLES, 0, mesh->num_vertices - 1, mesh->num_indices, GL_UNSIGNED_SHORT, (void *)0 );
	else
#endif
		pglDrawElements( GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_SHORT, (void *)0 );
	pglDisableVertexAttribArrayARB( s_studio_uniform_shader.a_position );
	pglDisableVertexAttribArrayARB( s_studio_uniform_shader.a_normal );
	pglDisableVertexAttribArrayARB( s_studio_uniform_shader.a_texcoord0 );
	pglDisableVertexAttribArrayARB( s_studio_uniform_shader.a_bone_index );
	pglDisableVertexAttribArrayARB( s_studio_uniform_shader.a_bone_weight );
	pglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
	pglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
	pglUseProgramObjectARB( 0 );
	return true;
}

static void R_StudioDrawArrays( uint startverts, uint startelems )
{
	const uint count = g_studio.numelems - startelems;
	const uint endverts = g_studio.numverts ? g_studio.numverts - 1 : 0;

	if( !count || startverts >= g_studio.numverts )
		return;

	/* Ensure the fixed-function/client-array Studio path is not affected by a
	 * previously bound experimental shader. This is harmless when shaders are absent.
	 */
	if( pglUseProgramObjectARB )
		pglUseProgramObjectARB( 0 );

	pglEnableClientState( GL_VERTEX_ARRAY );
	pglVertexPointer( 3, GL_FLOAT, 12, g_studio.arrayverts );

	pglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	pglTexCoordPointer( 2, GL_FLOAT, 0, g_studio.arraycoord );

	if( !( g_nForceFaceFlags & STUDIO_NF_CHROME ) )
	{
		pglEnableClientState( GL_COLOR_ARRAY );
		pglColorPointer( 4, GL_UNSIGNED_BYTE, 0, g_studio.arraycolor );
	}

#if !defined XASH_NANOGL || defined XASH_WES && XASH_EMSCRIPTEN // WebGL need to know array sizes
	if( pglDrawRangeElements )
		pglDrawRangeElements( GL_TRIANGLES, startverts, endverts,
			count, GL_UNSIGNED_SHORT, &g_studio.arrayelems[startelems] );
	else
#endif
		pglDrawElements( GL_TRIANGLES, count, GL_UNSIGNED_SHORT, &g_studio.arrayelems[startelems] );
	pglDisableClientState( GL_VERTEX_ARRAY );
	pglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	if( !( g_nForceFaceFlags & STUDIO_NF_CHROME ) )
		pglDisableClientState( GL_COLOR_ARRAY );
}

/*
===============
R_StudioDrawPoints

===============
*/
static void R_StudioDrawPoints( void )
{
	int		i, j, k, m_skinnum;
	float		shellscale = 0.0f;
	qboolean		need_sort = false;
	byte		*pvertbone;
	byte		*pnormbone;
	mstudioboneweight_t	*pvertweight = NULL;
	mstudioboneweight_t	*pnormweight = NULL;
	vec3_t		*pstudioverts;
	vec3_t		*pstudionorms;
	mstudiotexture_t	*ptexture;
	mstudiomesh_t	*pmesh;
	short		*pskinref;
	float		lv_tmp;

	if( !m_pStudioHeader || !m_pSubModel ) return;

	g_studio.numverts = g_studio.numelems = 0;

	m_skinnum = RI.currententity->curstate.skin;
	if( m_skinnum < 0 ) m_skinnum = 0;
	ptexture = (mstudiotexture_t *)((byte *)m_pStudioHeader + m_pStudioHeader->textureindex);
	pvertbone = ((byte *)m_pStudioHeader + m_pSubModel->vertinfoindex);
	pnormbone = ((byte *)m_pStudioHeader + m_pSubModel->norminfoindex);

	pmesh = (mstudiomesh_t *)((byte *)m_pStudioHeader + m_pSubModel->meshindex);
	pstudioverts = (vec3_t *)((byte *)m_pStudioHeader + m_pSubModel->vertindex);
	pstudionorms = (vec3_t *)((byte *)m_pStudioHeader + m_pSubModel->normindex);

	pskinref = (short *)((byte *)m_pStudioHeader + m_pStudioHeader->skinindex);
	if( m_skinnum > 0 && m_skinnum < m_pStudioHeader->numskinfamilies )
		pskinref += (m_skinnum * m_pStudioHeader->numskinref);

	if( FBitSet( m_pStudioHeader->flags, STUDIO_HAS_BONEWEIGHTS ) && m_pSubModel->blendvertinfoindex != 0 && m_pSubModel->blendnorminfoindex != 0 )
	{
		matrix3x4		skinMat;

		pvertweight = (mstudioboneweight_t *)((byte *)m_pStudioHeader + m_pSubModel->blendvertinfoindex);
		pnormweight = (mstudioboneweight_t *)((byte *)m_pStudioHeader + m_pSubModel->blendnorminfoindex);

		for( i = 0; i < m_pSubModel->numverts; i++ )
		{
			R_StudioComputeSkinMatrix( &pvertweight[i], skinMat );
			Matrix3x4_VectorTransform( skinMat, pstudioverts[i], g_studio.verts[i] );
			R_LightStrength( pvertbone[i], pstudioverts[i], g_studio.lightpos[i] );
		}

		for( i = 0; i < m_pSubModel->numnorms; i++ )
		{
			R_StudioComputeSkinMatrix( &pnormweight[i], skinMat );
			Matrix3x4_VectorRotate( skinMat, pstudionorms[i], g_studio.norms[i] );
		}
	}
	else
	{
		for( i = 0; i < m_pSubModel->numverts; i++ )
		{
			Matrix3x4_VectorTransform( g_studio.bonestransform[pvertbone[i]], pstudioverts[i], g_studio.verts[i] );
			R_LightStrength( pvertbone[i], pstudioverts[i], g_studio.lightpos[i] );
		}
	}

	// generate shared normals for properly scaling glowing shell
	if( RI.currententity->curstate.renderfx == kRenderFxGlowShell )
	{
		float factor = (1.0f / 128.0f);
		shellscale = Q_max( factor, RI.currententity->curstate.renderamt * factor );
		R_StudioBuildNormalTable();
		R_StudioGenerateNormals();
	}

	for( j = k = 0; j < m_pSubModel->nummesh; j++ )
	{
		g_nFaceFlags = ptexture[pskinref[pmesh[j].skinref]].flags | g_nForceFaceFlags;

		// fill in sortedmesh info
		g_studio.meshes[j].flags = g_nFaceFlags;
		g_studio.meshes[j].mesh = &pmesh[j];
		g_studio.meshes[j].skinref = pskinref[pmesh[j].skinref];
		g_studio.meshes[j].sortkey = R_StudioMeshSortKey( g_nFaceFlags, g_studio.meshes[j].skinref );

		if( FBitSet( g_nFaceFlags, STUDIO_NF_MASKED|STUDIO_NF_ADDITIVE ))
			need_sort = true;

		if( RI.currententity->curstate.rendermode == kRenderTransAdd )
		{
			for( i = 0; i < pmesh[j].numnorms; i++, k++, pstudionorms++, pnormbone++ )
			{
				if( FBitSet( g_nFaceFlags, STUDIO_NF_CHROME ))
					R_StudioSetupChrome( g_studio.chrome[k], *pnormbone, (float *)pstudionorms );
				VectorSet( g_studio.lightvalues[k], tr.blend, tr.blend, tr.blend );
			}
		}
		else
		{
			for( i = 0; i < pmesh[j].numnorms; i++, k++, pstudionorms++, pnormbone++ )
			{
				if( FBitSet( m_pStudioHeader->flags, STUDIO_HAS_BONEWEIGHTS ))
					R_StudioLighting( &lv_tmp, -1, g_nFaceFlags, g_studio.norms[k] );
				else R_StudioLighting( &lv_tmp, *pnormbone, g_nFaceFlags, (float *)pstudionorms );

				if( FBitSet( g_nFaceFlags, STUDIO_NF_CHROME ))
					R_StudioSetupChrome( g_studio.chrome[k], *pnormbone, (float *)pstudionorms );
				VectorScale( g_studio.lightcolor, lv_tmp, g_studio.lightvalues[k] );
			}
		}
	}

	if(( r_studio_sort_textures.value && need_sort ) || ( r_studio_batch_sort.value && m_pSubModel->nummesh > 1 ))
	{
		// resort meshes to reduce GL state churn:
		// opaque -> masked -> additive, then by resolved material/skin reference.
		qsort( g_studio.meshes, m_pSubModel->nummesh, sizeof( sortedmesh_t ), R_StudioMeshCompare );
	}

	// Pack bone rows for GPU skinning path (no-op cost if shader not active yet)
	if( r_studio_gpu_skinning.value && m_pStudioHeader )
		R_StudioPackBoneRows( m_pStudioHeader->numbones );

	// NOTE: rewind normals at start
	pstudionorms = (vec3_t *)((byte *)m_pStudioHeader + m_pSubModel->normindex);

	// backface culling for left-handed weapons
	if( R_AllowFlipViewModel( RI.currententity ))
	{
		tr.fFlipViewModel = true;
		GL_Cull( GL_NONE );
	}
	else
	{
		tr.fFlipViewModel = false;
		GL_Cull( GL_FRONT );
	}

	for( j = 0; j < m_pSubModel->nummesh; j++ )
	{
		float	oldblend = tr.blend;
		uint startArrayVerts = g_studio.numverts;
		uint startArrayElems = g_studio.numelems;
		short	*ptricmds;
		float	s, t;

		pmesh = g_studio.meshes[j].mesh;
		ptricmds = (short *)((byte *)m_pStudioHeader + pmesh->triindex);

		g_nFaceFlags = ptexture[pskinref[pmesh->skinref]].flags | g_nForceFaceFlags;

		s = 1.0f / (float)ptexture[pskinref[pmesh->skinref]].width;
		t = 1.0f / (float)ptexture[pskinref[pmesh->skinref]].height;

		if( FBitSet( g_nFaceFlags, STUDIO_NF_MASKED ))
		{
			pglEnable( GL_ALPHA_TEST );
			pglAlphaFunc( GL_GREATER, 0.5f );
			pglDepthMask( GL_TRUE );
			if( R_ModelOpaque( RI.currententity->curstate.rendermode ))
				tr.blend = 1.0f;
		}
		else if( FBitSet( g_nFaceFlags, STUDIO_NF_ADDITIVE ))
		{
			if( R_ModelOpaque( RI.currententity->curstate.rendermode ))
			{
				pglBlendFunc( GL_ONE, GL_ONE );
				pglDepthMask( GL_FALSE );
				pglEnable( GL_BLEND );
				R_AllowFog( false );
			}
			else pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
		}

		R_StudioSetupSkin( m_pStudioHeader, pskinref[pmesh->skinref] );

		if( r_studio_drawelements.value )
		{
			qboolean drawnByGpuSkinning = false;

			/* First try the real model-space GPU skinning path. It uses persistent
			 * static VBO/IBO data and bone uniform rows, so it does not cache animated
			 * world-space vertices and therefore cannot freeze animations. It is guarded
			 * by r_studio_gpu_skinning and falls back to the legacy CPU path on failure. */
			if( 0 && r_studio_gpu_skinning.value && shellscale <= 0.0f )
				drawnByGpuSkinning = R_StudioTryDrawGPUUniformMesh( pmesh, pskinref[pmesh->skinref], g_nFaceFlags, s, t,
					pstudioverts, pstudionorms, pvertbone, pvertweight );

			if( !drawnByGpuSkinning )
			{
				// startArrayVerts/startArrayElems mark where THIS mesh begins in the arrays
				if( FBitSet( g_nFaceFlags, STUDIO_NF_CHROME ))
					R_StudioBuildArrayChromeMesh( ptricmds, pstudionorms, s, t, shellscale );
				else if( FBitSet( g_nFaceFlags, STUDIO_NF_UV_COORDS ))
					R_StudioBuildArrayFloatMesh( ptricmds, pstudionorms );
				else R_StudioBuildArrayNormalMesh( ptricmds, pstudionorms, s, t );

				/* Safe RX 580 path: stream animated/skinned arrays to VBO/IBO for this mesh.
				 * Unlike the old static cache experiment, this does not persist world-space
				 * vertices across frames, so animation and movement stay correct.
				 * If anything is unsupported or invalid, fall back to original client arrays.
				 */
				if( !R_StudioDrawStreamVBO( startArrayVerts, startArrayElems ))
					R_StudioDrawArrays( startArrayVerts, startArrayElems );
			}
		}
		else
		{
			if( FBitSet( g_nFaceFlags, STUDIO_NF_CHROME ))
				R_StudioDrawChromeMesh( ptricmds, pstudionorms, s, t, shellscale );
			else if( FBitSet( g_nFaceFlags, STUDIO_NF_UV_COORDS ))
				R_StudioDrawFloatMesh( ptricmds, pstudionorms );
			else R_StudioDrawNormalMesh( ptricmds, pstudionorms, s, t );
		}

		if( FBitSet( g_nFaceFlags, STUDIO_NF_MASKED ))
		{
			pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
			pglDisable( GL_ALPHA_TEST );
		}
		else if( FBitSet( g_nFaceFlags, STUDIO_NF_ADDITIVE ) && R_ModelOpaque( RI.currententity->curstate.rendermode ))
		{
			pglDepthMask( GL_TRUE );
			pglDisable( GL_BLEND );
			R_AllowFog( true );
		}

		r_stats.c_studio_polys += pmesh->numtris;
		tr.blend = oldblend;
	}
}

/*
===============
R_StudioDrawHulls

===============
*/
static void R_StudioDrawHulls( void )
{
	float	alpha, lv;
	int	i, j;

	if( r_drawentities->value == 4 )
		alpha = 0.5f;
	else alpha = 1.0f;

	GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	for( i = 0; i < m_pStudioHeader->numhitboxes; i++ )
	{
		mstudiobbox_t	*pbbox = (mstudiobbox_t *)((byte *)m_pStudioHeader + m_pStudioHeader->hitboxindex);
		vec3_t		tmp, p[8];

		for( j = 0; j < 8; j++ )
		{
			tmp[0] = (j & 1) ? pbbox[i].bbmin[0] : pbbox[i].bbmax[0];
			tmp[1] = (j & 2) ? pbbox[i].bbmin[1] : pbbox[i].bbmax[1];
			tmp[2] = (j & 4) ? pbbox[i].bbmin[2] : pbbox[i].bbmax[2];

			Matrix3x4_VectorTransform( g_studio.bonestransform[pbbox[i].bone], tmp, p[j] );
		}

		j = (pbbox[i].group % 8);

		TriBegin( TRI_QUADS );
		TriColor4f( hullcolor[j][0], hullcolor[j][1], hullcolor[j][2], alpha );

		for( j = 0; j < 6; j++ )
		{
			VectorClear( tmp );
			tmp[j % 3] = (j < 3) ? 1.0f : -1.0f;
			R_StudioLighting( &lv, pbbox[i].bone, 0, tmp );

			TriBrightness( lv );
			TriVertex3fv( p[boxpnt[j][0]] );
			TriVertex3fv( p[boxpnt[j][1]] );
			TriVertex3fv( p[boxpnt[j][2]] );
			TriVertex3fv( p[boxpnt[j][3]] );
		}
		TriEnd();
	}
}

/*
===============
R_StudioDrawAbsBBox

===============
*/
static void R_StudioDrawAbsBBox( void )
{
	vec3_t	p[8], tmp;
	float	lv;
	int	i;

	// looks ugly, skip
	if( RI.currententity == tr.viewent )
		return;

	if( !R_StudioComputeBBox( p ))
		return;

	GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	TriColor4f( 0.5f, 0.5f, 1.0f, 0.5f );
	TriRenderMode( kRenderTransAdd );

	TriBegin( TRI_QUADS );
	for( i = 0; i < 6; i++ )
	{
		VectorClear( tmp );
		tmp[i % 3] = (i < 3) ? 1.0f : -1.0f;
		R_StudioLighting( &lv, -1, 0, tmp );

		TriBrightness( lv );
		TriVertex3fv( p[boxpnt[i][0]] );
		TriVertex3fv( p[boxpnt[i][1]] );
		TriVertex3fv( p[boxpnt[i][2]] );
		TriVertex3fv( p[boxpnt[i][3]] );
	}
	TriEnd();
	TriRenderMode( kRenderNormal );
}

/*
===============
R_StudioDrawBones

===============
*/
static void R_StudioDrawBones( void )
{
	mstudiobone_t	*pbones = (mstudiobone_t *) ((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);
	vec3_t		point;
	int		i;

	pglDisable( GL_TEXTURE_2D );

	for( i = 0; i < m_pStudioHeader->numbones; i++ )
	{
		if( pbones[i].parent >= 0 )
		{
			pglPointSize( 3.0f );
			pglColor3f( 1, 0.7f, 0 );
			pglBegin( GL_LINES );

			Matrix3x4_OriginFromMatrix( g_studio.bonestransform[pbones[i].parent], point );
			pglVertex3fv( point );
			Matrix3x4_OriginFromMatrix( g_studio.bonestransform[i], point );
			pglVertex3fv( point );

			pglEnd();

			pglColor3f( 0, 0, 0.8f );
			pglBegin( GL_POINTS );
			if( pbones[pbones[i].parent].parent != -1 )
			{
				Matrix3x4_OriginFromMatrix( g_studio.bonestransform[pbones[i].parent], point );
				pglVertex3fv( point );
			}
			Matrix3x4_OriginFromMatrix( g_studio.bonestransform[i], point );
			pglVertex3fv( point );
			pglEnd();
		}
		else
		{
			// draw parent bone node
			pglPointSize( 5.0f );
			pglColor3f( 0.8f, 0, 0 );
			pglBegin( GL_POINTS );
			Matrix3x4_OriginFromMatrix( g_studio.bonestransform[i], point );
			pglVertex3fv( point );
			pglEnd();
		}
	}

	pglPointSize( 1.0f );
	pglEnable( GL_TEXTURE_2D );
}

static void R_StudioDrawAttachments( void )
{
	int	i;

	pglDisable( GL_TEXTURE_2D );
	pglDisable( GL_DEPTH_TEST );

	for( i = 0; i < m_pStudioHeader->numattachments; i++ )
	{
		mstudioattachment_t	*pattachments;
		vec3_t		v[4];

		pattachments = (mstudioattachment_t *)((byte *)m_pStudioHeader + m_pStudioHeader->attachmentindex);
		Matrix3x4_VectorTransform( g_studio.bonestransform[pattachments[i].bone], pattachments[i].org, v[0] );
		Matrix3x4_VectorTransform( g_studio.bonestransform[pattachments[i].bone], pattachments[i].vectors[0], v[1] );
		Matrix3x4_VectorTransform( g_studio.bonestransform[pattachments[i].bone], pattachments[i].vectors[1], v[2] );
		Matrix3x4_VectorTransform( g_studio.bonestransform[pattachments[i].bone], pattachments[i].vectors[2], v[3] );

		pglBegin( GL_LINES );
		pglColor3f( 1, 0, 0 );
		pglVertex3fv( v[0] );
		pglColor3f( 1, 1, 1 );
		pglVertex3fv (v[1] );
		pglColor3f( 1, 0, 0 );
		pglVertex3fv (v[0] );
		pglColor3f( 1, 1, 1 );
		pglVertex3fv (v[2] );
		pglColor3f( 1, 0, 0 );
		pglVertex3fv (v[0] );
		pglColor3f( 1, 1, 1 );
		pglVertex3fv( v[3] );
		pglEnd();

		pglPointSize( 5.0f );
		pglColor3f( 0, 1, 0 );
		pglBegin( GL_POINTS );
		pglVertex3fv( v[0] );
		pglEnd();
		pglPointSize( 1.0f );
	}

	pglEnable( GL_TEXTURE_2D );
	pglEnable( GL_DEPTH_TEST );
}

/*
===============
R_StudioSetRemapColors

===============
*/
static void R_StudioSetRemapColors( int newTop, int newBottom )
{
	if( gEngfuncs.CL_EntitySetRemapColors( RI.currententity, RI.currentmodel, newTop, newBottom ))
		m_fDoRemap = true;
}

void R_StudioResetPlayerModels( void )
{
	memset( g_studio.player_models, 0, sizeof( g_studio.player_models ));
}

/*
===============
R_StudioSetupPlayerModel

===============
*/
static model_t *R_StudioSetupPlayerModel( int index )
{
	player_info_t  *info;
	player_model_t *state;

	if( index < 0 || index >= gp_cl->maxclients )
		return NULL;

	info = gEngfuncs.pfnPlayerInfo( index );
	if( !info )
		return NULL;

	state = &g_studio.player_models[index];

	// g-cont: force for "dev-mode", non-local games and menu preview
	if(( gpGlobals->developer || !ENGINE_GET_PARM( PARM_LOCAL_GAME ) || !RI.drawWorld ) && info->model[0] )
	{
		if( Q_strcmp( state->name, info->model ))
		{
			Q_strncpy( state->name, info->model, sizeof( state->name ));
			state->name[sizeof( state->name ) - 1] = 0;

			Q_snprintf( state->modelname, sizeof( state->modelname ), "models/player/%s/%s.mdl", info->model, info->model );

			if( gEngfuncs.fsapi->FileExists( state->modelname, false ))
				state->model = gEngfuncs.Mod_ForName( state->modelname, false, true );
			else
				state->model = NULL;

			/* HUD export calls this with no current entity. Do not dereference it. */
			if( !state->model && RI.currententity )
				state->model = RI.currententity->model;
		}
	}
	else
	{
		if( !RI.currententity )
			return NULL;
		if( state->model != RI.currententity->model )
			state->model = RI.currententity->model;
		state->name[0] = 0;
	}

	return state->model;
}

/*
================
R_GetEntityRenderMode

check for texture flags
================
*/
int R_GetEntityRenderMode( cl_entity_t *ent )
{
	int              i, opaque, trans;
	mstudiotexture_t *ptexture;
	cl_entity_t      *oldent;
	model_t          *model = NULL;
	studiohdr_t      *phdr;

	oldent = RI.currententity;
	RI.currententity = ent;

	if( ent->player ) // check it for real playermodel
		model = R_StudioSetupPlayerModel( ent->curstate.number - 1 );

	if( !model )
		model = ent->model;

	RI.currententity = oldent;

	if(( phdr = gEngfuncs.Mod_Extradata( mod_studio, model )) == NULL )
	{
		if( R_ModelOpaque( ent->curstate.rendermode ))
		{
			// forcing to choose right sorting type
			if(( model && model->type == mod_brush ) && FBitSet( model->flags, MODEL_TRANSPARENT ))
				return kRenderTransAlpha;
		}
		return ent->curstate.rendermode;
	}
	ptexture = (mstudiotexture_t *)((byte *)phdr + phdr->textureindex);

	for( opaque = trans = i = 0; i < phdr->numtextures; i++, ptexture++ )
	{
		// ignore chrome & additive it's just a specular-like effect
		if( FBitSet( ptexture->flags, STUDIO_NF_ADDITIVE ) && !FBitSet( ptexture->flags, STUDIO_NF_CHROME ))
			trans++;
		else opaque++;
	}

	// if model is more additive than opaque
	if( trans > opaque )
		return kRenderTransAdd;
	return ent->curstate.rendermode;
}

/*
===============
R_StudioClientEvents

===============
*/
static void R_StudioClientEvents( void )
{
	mstudioseqdesc_t	*pseqdesc;
	mstudioevent_t	*pevent;
	cl_entity_t	*e = RI.currententity;
	int		i, sequence;
	float		end, start;

	if( g_studio.frametime == 0.0 )
		return; // gamepaused

	// fill attachments with interpolated origin
	if( m_pStudioHeader->numattachments <= 0 )
	{
		Matrix3x4_OriginFromMatrix( g_studio.rotationmatrix, e->attachment[0] );
		Matrix3x4_OriginFromMatrix( g_studio.rotationmatrix, e->attachment[1] );
		Matrix3x4_OriginFromMatrix( g_studio.rotationmatrix, e->attachment[2] );
		Matrix3x4_OriginFromMatrix( g_studio.rotationmatrix, e->attachment[3] );
	}

	if( FBitSet( e->curstate.effects, EF_MUZZLEFLASH ))
	{
		dlight_t	*el = gEngfuncs.CL_AllocElight( 0 );

		ClearBits( e->curstate.effects, EF_MUZZLEFLASH );
		VectorCopy( e->attachment[0], el->origin );
		el->die = gp_cl->time + 0.05f;
		el->color.r = 255;
		el->color.g = 192;
		el->color.b = 64;
		el->decay = 320;
		el->radius = 24;
	}

	sequence = bound( 0, e->curstate.sequence, m_pStudioHeader->numseq - 1 );
	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + sequence;

	// no events for this animation
	if( pseqdesc->numevents == 0 )
		return;

	end = R_StudioEstimateFrame( e, pseqdesc, g_studio.time );
	start = end - e->curstate.framerate * gp_host->frametime * pseqdesc->fps;
	pevent = (mstudioevent_t *)((byte *)m_pStudioHeader + pseqdesc->eventindex);

	if( e->latched.sequencetime == e->curstate.animtime )
	{
		if( !FBitSet( pseqdesc->flags, STUDIO_LOOPING ))
			start = -0.01f;
	}

	for( i = 0; i < pseqdesc->numevents; i++ )
	{
		// ignore all non-client-side events
		if( pevent[i].event < EVENT_CLIENT )
			continue;

		if( (float)pevent[i].frame > start && pevent[i].frame <= end )
			gEngfuncs.pfnStudioEvent( &pevent[i], e );
	}
}

/*
===============
R_StudioGetForceFaceFlags

===============
*/
static int R_StudioGetForceFaceFlags( void )
{
	return g_nForceFaceFlags;
}

/*
===============
R_StudioSetForceFaceFlags

===============
*/
static void R_StudioSetForceFaceFlags( int flags )
{
	g_nForceFaceFlags = flags;
}

/*
===============
pfnStudioSetHeader

===============
*/
static void R_StudioSetHeader( studiohdr_t *pheader )
{
	m_pStudioHeader = pheader;
	m_fDoRemap = false;
}

/*
===============
R_StudioSetRenderModel

===============
*/
static void R_StudioSetRenderModel( model_t *model )
{
	RI.currentmodel = model;
}

/*
===============
R_StudioSetupRenderer

===============
*/
static void R_StudioSetupRenderer( int rendermode )
{
	studiohdr_t	*phdr = m_pStudioHeader;
	int		i;

	if( rendermode > kRenderTransAdd ) rendermode = 0;
	g_studio.rendermode = bound( 0, rendermode, kRenderTransAdd );

	if( g_studio.rendermode == kRenderTransAdd || g_studio.rendermode == kRenderGlow )
		R_AllowFog( false );

	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglDisable( GL_ALPHA_TEST );
	pglShadeModel( GL_SMOOTH );

	// a point to setup local to world transform for boneweighted models
	if( phdr && FBitSet( phdr->flags, STUDIO_HAS_BONEINFO ))
	{
		// NOTE: extended boneinfo goes immediately after bones
		mstudioboneinfo_t *boneinfo = (mstudioboneinfo_t *)((byte *)phdr + phdr->boneindex + phdr->numbones * sizeof( mstudiobone_t ));

		for( i = 0; i < phdr->numbones; i++ )
			Matrix3x4_ConcatTransforms( g_studio.worldtransform[i], g_studio.bonestransform[i], boneinfo[i].poseToBone );
	}
}

/*
===============
R_StudioRestoreRenderer

===============
*/
static void R_StudioRestoreRenderer( void )
{
	if( g_studio.rendermode != kRenderNormal )
		pglDisable( GL_BLEND );

	if( g_studio.rendermode == kRenderTransAdd || g_studio.rendermode == kRenderGlow )
		R_AllowFog( true );

	pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	pglShadeModel( GL_FLAT );
	m_fDoRemap = false;
}

/*
===============
R_StudioSetChromeOrigin

===============
*/
static void R_StudioSetChromeOrigin( void )
{
	VectorCopy( RI.vieworg, g_studio.chrome_origin );
}

/*
===============
pfnIsHardware

Xash3D is always works in hardware mode
===============
*/
static int pfnIsHardware( void )
{
	return 1;	// 0 is Software, 1 is OpenGL, 2 is Direct3D
}

/*
===============
R_StudioDrawPointsShadow

===============
*/
static void R_StudioDrawPointsShadow( void )
{
	float		*av, height;
	float		vec_x, vec_y;
	mstudiomesh_t	*pmesh;
	vec3_t		point;
	int		i, k;

	if( FBitSet( RI.currententity->curstate.effects, EF_NOSHADOW ))
		return;

	if( glState.stencilEnabled )
		pglEnable( GL_STENCIL_TEST );

	height = g_studio.lightspot[2] + 1.0f;
	vec_x = -g_studio.lightvec[0] * 8.0f;
	vec_y = -g_studio.lightvec[1] * 8.0f;

	for( k = 0; k < m_pSubModel->nummesh; k++ )
	{
		short	*ptricmds;

		pmesh = (mstudiomesh_t *)((byte *)m_pStudioHeader + m_pSubModel->meshindex) + k;
		ptricmds = (short *)((byte *)m_pStudioHeader + pmesh->triindex);

		r_stats.c_studio_polys += pmesh->numtris;

		while(( i = *( ptricmds++ )))
		{
			if( i < 0 )
			{
				pglBegin( GL_TRIANGLE_FAN );
				i = -i;
			}
			else
			{
				pglBegin( GL_TRIANGLE_STRIP );
			}


			for( ; i > 0; i--, ptricmds += 4 )
			{
				av = g_studio.verts[ptricmds[0]];
				point[0] = av[0] - (vec_x * ( av[2] - g_studio.lightspot[2] ));
				point[1] = av[1] - (vec_y * ( av[2] - g_studio.lightspot[2] ));
				point[2] = g_studio.lightspot[2] + 1.0f;

				pglVertex3fv( point );
			}

			pglEnd();
		}
	}

	if( glState.stencilEnabled )
		pglDisable( GL_STENCIL_TEST );
}

/*
===============
GL_StudioSetRenderMode

set rendermode for studiomodel
===============
*/
static void GL_StudioSetRenderMode( int rendermode )
{
	switch( rendermode )
	{
	case kRenderNormal:
		break;
	case kRenderTransColor:
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglEnable( GL_BLEND );
		break;
	case kRenderTransAdd:
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglColor4f( tr.blend, tr.blend, tr.blend, 1.0f );
		pglBlendFunc( GL_ONE, GL_ONE );
		pglDepthMask( GL_FALSE );
		pglEnable( GL_BLEND );
		break;
	default:
		pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglColor4f( 1.0f, 1.0f, 1.0f, tr.blend );
		pglDepthMask( GL_TRUE );
		pglEnable( GL_BLEND );
		break;
	}
}

/*
===============
GL_StudioDrawShadow

g-cont: don't modify this code it's 100% matched with
original GoldSrc code and used in some mods to enable
studio shadows with some asm tricks
===============
*/
static void GL_StudioDrawShadow( void )
{
	pglDepthMask( GL_TRUE );

	if( r_shadows.value && g_studio.rendermode != kRenderTransAdd && !FBitSet( RI.currentmodel->flags, STUDIO_AMBIENT_LIGHT ))
	{
		float	color = 1.0f - (tr.blend * 0.5f);

		pglDisable( GL_TEXTURE_2D );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		pglEnable( GL_BLEND );
		pglColor4f( 0.0f, 0.0f, 0.0f, 1.0f - color );

		pglDepthFunc( GL_LESS );
		R_StudioDrawPointsShadow();
		pglDepthFunc( GL_LEQUAL );

		pglEnable( GL_TEXTURE_2D );
		pglDisable( GL_BLEND );
		pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		pglShadeModel( GL_SMOOTH );
	}
}

/*
====================
StudioRenderFinal

====================
*/
static void R_StudioRenderFinal( void )
{
	int	i, rendermode;

	rendermode = R_StudioGetForceFaceFlags() ? kRenderTransAdd : RI.currententity->curstate.rendermode;
	R_StudioSetupRenderer( rendermode );

	if( r_drawentities->value == 2 )
	{
		R_StudioDrawBones();
	}
	else if( r_drawentities->value == 3 )
	{
		R_StudioDrawHulls();
	}
	else
	{
		for( i = 0; i < m_pStudioHeader->numbodyparts; i++ )
		{
			R_StudioSetupModel( i, (void**)&m_pBodyPart, (void**)&m_pSubModel );

			GL_StudioSetRenderMode( rendermode );
			R_StudioDrawPoints();
			GL_StudioDrawShadow();
		}
	}

	if( r_drawentities->value == 4 )
	{
		TriRenderMode( kRenderTransAdd );
		R_StudioDrawHulls( );
		TriRenderMode( kRenderNormal );
	}

	if( r_drawentities->value == 5 )
	{
		R_StudioDrawAbsBBox( );
	}

	if( r_drawentities->value == 6 )
	{
		R_StudioDrawAttachments();
	}

	if( r_drawentities->value == 7 )
	{
		vec3_t	origin;

		pglDisable( GL_TEXTURE_2D );
		pglDisable( GL_DEPTH_TEST );

		Matrix3x4_OriginFromMatrix( g_studio.rotationmatrix, origin );

		pglBegin( GL_LINES );
		pglColor3f( 1, 0.5, 0 );
		pglVertex3fv( origin );
		pglVertex3fv( g_studio.lightspot );
		pglEnd();

		pglBegin( GL_LINES );
		pglColor3f( 0, 0.5, 1 );
		VectorMA( g_studio.lightspot, -64.0f, g_studio.lightvec, origin );
		pglVertex3fv( g_studio.lightspot );
		pglVertex3fv( origin );
		pglEnd();

		pglPointSize( 5.0f );
		pglColor3f( 1, 0, 0 );
		pglBegin( GL_POINTS );
		pglVertex3fv( g_studio.lightspot );
		pglEnd();
		pglPointSize( 1.0f );

		pglEnable( GL_DEPTH_TEST );
		pglEnable( GL_TEXTURE_2D );
	}

	R_StudioRestoreRenderer();
}

/*
====================
StudioRenderModel

====================
*/
static void R_StudioRenderModel( void )
{
	R_StudioSetChromeOrigin();
	R_StudioSetForceFaceFlags( 0 );

	if( RI.currententity->curstate.renderfx == kRenderFxGlowShell )
	{
		RI.currententity->curstate.renderfx = kRenderFxNone;

		R_StudioRenderFinal( );

		R_StudioSetForceFaceFlags( STUDIO_NF_CHROME );
		TriSpriteTexture( R_GetChromeSprite(), 0 );
		RI.currententity->curstate.renderfx = kRenderFxGlowShell;

		R_StudioRenderFinal( );
	}
	else
	{
		R_StudioRenderFinal( );
	}
}

/*
====================
StudioEstimateGait

====================
*/
static void R_StudioEstimateGait( entity_state_t *pplayer )
{
	vec3_t	est_velocity;
	float	dt;

	dt = bound( 0.0f, g_studio.frametime, 1.0f );

	if( dt == 0.0f || m_pPlayerInfo->renderframe == tr.realframecount )
	{
		m_flGaitMovement = 0;
		return;
	}

	VectorSubtract( RI.currententity->origin, m_pPlayerInfo->prevgaitorigin, est_velocity );
	VectorCopy( RI.currententity->origin, m_pPlayerInfo->prevgaitorigin );
	m_flGaitMovement = VectorLength( est_velocity );

	if( dt <= 0.0f || m_flGaitMovement / dt < 5.0f )
	{
		m_flGaitMovement = 0.0f;
		est_velocity[0] = 0.0f;
		est_velocity[1] = 0.0f;
	}

	if( est_velocity[1] == 0.0f && est_velocity[0] == 0.0f )
	{
		float	flYawDiff = RI.currententity->angles[YAW] - m_pPlayerInfo->gaityaw;

		flYawDiff = flYawDiff - (int)(flYawDiff / 360) * 360;
		if( flYawDiff > 180.0f ) flYawDiff -= 360.0f;
		if( flYawDiff < -180.0f ) flYawDiff += 360.0f;

		if( dt < 0.25f )
			flYawDiff *= dt * 4.0f;
		else flYawDiff *= dt;

		m_pPlayerInfo->gaityaw += flYawDiff;
		m_pPlayerInfo->gaityaw = m_pPlayerInfo->gaityaw - (int)(m_pPlayerInfo->gaityaw / 360) * 360;

		m_flGaitMovement = 0.0f;
	}
	else
	{
		m_pPlayerInfo->gaityaw = ( atan2( est_velocity[1], est_velocity[0] ) * 180 / M_PI_F );
		if( m_pPlayerInfo->gaityaw > 180.0f ) m_pPlayerInfo->gaityaw = 180.0f;
		if( m_pPlayerInfo->gaityaw < -180.0f ) m_pPlayerInfo->gaityaw = -180.0f;
	}

}

/*
====================
StudioProcessGait

====================
*/
static void R_StudioProcessGait( entity_state_t *pplayer )
{
	mstudioseqdesc_t	*pseqdesc;
	int		iBlend;
	float		dt, flYaw; // view direction relative to movement

	if( RI.currententity->curstate.sequence >= m_pStudioHeader->numseq )
		RI.currententity->curstate.sequence = 0;

	dt = bound( 0.0f, g_studio.frametime, 1.0f );

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + RI.currententity->curstate.sequence;

	R_StudioPlayerBlend( pseqdesc, &iBlend, &RI.currententity->angles[PITCH] );

	RI.currententity->latched.prevangles[PITCH] = RI.currententity->angles[PITCH];
	RI.currententity->curstate.blending[0] = iBlend;
	RI.currententity->latched.prevblending[0] = RI.currententity->curstate.blending[0];
	RI.currententity->latched.prevseqblending[0] = RI.currententity->curstate.blending[0];
	R_StudioEstimateGait( pplayer );

	// calc side to side turning
	flYaw = RI.currententity->angles[YAW] - m_pPlayerInfo->gaityaw;
	flYaw = flYaw - (int)(flYaw / 360) * 360;
	if( flYaw < -180.0f ) flYaw = flYaw + 360.0f;
	if( flYaw > 180.0f ) flYaw = flYaw - 360.0f;

	if( flYaw > 120.0f )
	{
		m_pPlayerInfo->gaityaw = m_pPlayerInfo->gaityaw - 180.0f;
		m_flGaitMovement = -m_flGaitMovement;
		flYaw = flYaw - 180.0f;
	}
	else if( flYaw < -120.0f )
	{
		m_pPlayerInfo->gaityaw = m_pPlayerInfo->gaityaw + 180.0f;
		m_flGaitMovement = -m_flGaitMovement;
		flYaw = flYaw + 180.0f;
	}

	// adjust torso
	RI.currententity->curstate.controller[0] = ((flYaw / 4.0f) + 30.0f) / (60.0f / 255.0f);
	RI.currententity->curstate.controller[1] = ((flYaw / 4.0f) + 30.0f) / (60.0f / 255.0f);
	RI.currententity->curstate.controller[2] = ((flYaw / 4.0f) + 30.0f) / (60.0f / 255.0f);
	RI.currententity->curstate.controller[3] = ((flYaw / 4.0f) + 30.0f) / (60.0f / 255.0f);
	RI.currententity->latched.prevcontroller[0] = RI.currententity->curstate.controller[0];
	RI.currententity->latched.prevcontroller[1] = RI.currententity->curstate.controller[1];
	RI.currententity->latched.prevcontroller[2] = RI.currententity->curstate.controller[2];
	RI.currententity->latched.prevcontroller[3] = RI.currententity->curstate.controller[3];

	RI.currententity->angles[YAW] = m_pPlayerInfo->gaityaw;
	if( RI.currententity->angles[YAW] < -0 ) RI.currententity->angles[YAW] += 360.0f;
	RI.currententity->latched.prevangles[YAW] = RI.currententity->angles[YAW];

	if( pplayer->gaitsequence >= m_pStudioHeader->numseq )
		pplayer->gaitsequence = 0;

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + pplayer->gaitsequence;

	// calc gait frame
	if( pseqdesc->linearmovement[0] > 0 )
		m_pPlayerInfo->gaitframe += (m_flGaitMovement / pseqdesc->linearmovement[0]) * pseqdesc->numframes;
	else m_pPlayerInfo->gaitframe += pseqdesc->fps * dt;

	// do modulo
	m_pPlayerInfo->gaitframe = m_pPlayerInfo->gaitframe - (int)(m_pPlayerInfo->gaitframe / pseqdesc->numframes) * pseqdesc->numframes;
	if( m_pPlayerInfo->gaitframe < 0 ) m_pPlayerInfo->gaitframe += pseqdesc->numframes;
}

/*
===============
R_StudioDrawPlayer

===============
*/
static int R_StudioDrawPlayer( int flags, entity_state_t *pplayer )
{
	int	m_nPlayerIndex;
	alight_t	lighting;
	vec3_t	dir;

	m_nPlayerIndex = pplayer->number - 1;

	if( m_nPlayerIndex < 0 || m_nPlayerIndex >= gp_cl->maxclients )
		return 0;

	RI.currentmodel = R_StudioSetupPlayerModel( m_nPlayerIndex );
	if( RI.currentmodel == NULL )
		return 0;

	R_StudioSetHeader((studiohdr_t *)gEngfuncs.Mod_Extradata( mod_studio, RI.currentmodel ));

	if( pplayer->gaitsequence )
	{
		vec3_t orig_angles;

		m_pPlayerInfo = pfnPlayerInfo( m_nPlayerIndex );
		VectorCopy( RI.currententity->angles, orig_angles );

		R_StudioProcessGait( pplayer );

		m_pPlayerInfo->gaitsequence = pplayer->gaitsequence;
		m_pPlayerInfo = NULL;

		R_StudioSetUpTransform( RI.currententity );
		VectorCopy( orig_angles, RI.currententity->angles );
	}
	else
	{
		RI.currententity->curstate.controller[0] = 127;
		RI.currententity->curstate.controller[1] = 127;
		RI.currententity->curstate.controller[2] = 127;
		RI.currententity->curstate.controller[3] = 127;
		RI.currententity->latched.prevcontroller[0] = RI.currententity->curstate.controller[0];
		RI.currententity->latched.prevcontroller[1] = RI.currententity->curstate.controller[1];
		RI.currententity->latched.prevcontroller[2] = RI.currententity->curstate.controller[2];
		RI.currententity->latched.prevcontroller[3] = RI.currententity->curstate.controller[3];

		m_pPlayerInfo = pfnPlayerInfo( m_nPlayerIndex );
		m_pPlayerInfo->gaitsequence = 0;

		R_StudioSetUpTransform( RI.currententity );
	}

	if( flags & STUDIO_RENDER )
	{
		// see if the bounding box lets us trivially reject, also sets
		if( !R_StudioCheckBBox( ))
			return 0;

		r_stats.c_studio_models_drawn++;
		g_studio.framecount++; // render data cache cookie

		if( m_pStudioHeader->numbodyparts == 0 )
			return 1;
	}

	m_pPlayerInfo = pfnPlayerInfo( m_nPlayerIndex );
	R_StudioSetupBones( RI.currententity );
	R_StudioSaveBones( );

	m_pPlayerInfo->renderframe = tr.realframecount;
	m_pPlayerInfo = NULL;

	if( flags & STUDIO_EVENTS )
	{
		R_StudioCalcAttachments( );
		R_StudioClientEvents( );

		// copy attachments into global entity array
		if( RI.currententity->index > 0 )
		{
			cl_entity_t *ent = CL_GetEntityByIndex( RI.currententity->index );
			memcpy( ent->attachment, RI.currententity->attachment, sizeof( vec3_t ) * 4 );
		}
	}

	if( flags & STUDIO_RENDER )
	{
		// change body if it's a menu entity
		if( cl_himodels->value && ( RI.currentmodel != RI.currententity->model || !RI.drawWorld ))
		{
			// show highest resolution multiplayer model
			RI.currententity->curstate.body = 255;
		}

		if( !( !gpGlobals->developer && gp_cl->maxclients == 1 ) && ( RI.currentmodel == RI.currententity->model ))
			RI.currententity->curstate.body = 1; // force helmet

		lighting.plightvec = dir;
		R_StudioDynamicLight( RI.currententity, &lighting );

		R_StudioEntityLight( &lighting );

		// model and frame independant
		R_StudioSetupLighting( &lighting );

		m_pPlayerInfo = pfnPlayerInfo( m_nPlayerIndex );

		// get remap colors
		g_nTopColor = m_pPlayerInfo->topcolor;
		g_nBottomColor = m_pPlayerInfo->bottomcolor;

		if( g_nTopColor < 0 ) g_nTopColor = 0;
		if( g_nTopColor > 360 ) g_nTopColor = 360;
		if( g_nBottomColor < 0 ) g_nBottomColor = 0;
		if( g_nBottomColor > 360 ) g_nBottomColor = 360;

		R_StudioSetRemapColors( g_nTopColor, g_nBottomColor );

		R_StudioRenderModel( );
		m_pPlayerInfo = NULL;

		if( pplayer->weaponmodel )
		{
			cl_entity_t	saveent = *RI.currententity;
			model_t		*pweaponmodel = CL_ModelHandle( pplayer->weaponmodel );

			m_pStudioHeader = (studiohdr_t *)gEngfuncs.Mod_Extradata( mod_studio, pweaponmodel );

			R_StudioMergeBones( RI.currententity, pweaponmodel );
			R_StudioSetupLighting( &lighting );
			R_StudioRenderModel( );
			R_StudioCalcAttachments( );

			*RI.currententity = saveent;
		}
	}

	return 1;
}

/*
===============
R_StudioDrawModel

===============
*/
static int R_StudioDrawModel( int flags )
{
	alight_t	lighting;
	vec3_t	dir;

	if( RI.currententity->curstate.renderfx == kRenderFxDeadPlayer )
	{
		entity_state_t	deadplayer;
		int		result;

		if( RI.currententity->curstate.renderamt <= 0 ||
			RI.currententity->curstate.renderamt > gp_cl->maxclients )
			return 0;

		// get copy of player
		deadplayer = *R_StudioGetPlayerState( RI.currententity->curstate.renderamt - 1 );

		// clear weapon, movement state
		deadplayer.number = RI.currententity->curstate.renderamt;
		deadplayer.weaponmodel = 0;
		deadplayer.gaitsequence = 0;

		deadplayer.movetype = MOVETYPE_NONE;
		VectorCopy( RI.currententity->curstate.angles, deadplayer.angles );
		VectorCopy( RI.currententity->curstate.origin, deadplayer.origin );

		g_studio.interpolate = false;
		result = R_StudioDrawPlayer( flags, &deadplayer ); // draw as though it were a player
		g_studio.interpolate = true;

		return result;
	}

	R_StudioSetHeader((studiohdr_t *)gEngfuncs.Mod_Extradata( mod_studio, RI.currentmodel ));

	R_StudioSetUpTransform( RI.currententity );

	if( flags & STUDIO_RENDER )
	{
		// see if the bounding box lets us trivially reject, also sets
		if( !R_StudioCheckBBox( ))
			return 0;

		r_stats.c_studio_models_drawn++;
		g_studio.framecount++; // render data cache cookie

		if( m_pStudioHeader->numbodyparts == 0 )
			return 1;
	}

	if( RI.currententity->curstate.movetype == MOVETYPE_FOLLOW )
		R_StudioMergeBones( RI.currententity, RI.currentmodel );
	else R_StudioSetupBones( RI.currententity );

	R_StudioSaveBones();

	if( flags & STUDIO_EVENTS )
	{
		R_StudioCalcAttachments( );
		R_StudioClientEvents( );

		// copy attachments into global entity array
		if( RI.currententity->index > 0 )
		{
			cl_entity_t *ent = CL_GetEntityByIndex( RI.currententity->index );
			memcpy( ent->attachment, RI.currententity->attachment, sizeof( vec3_t ) * 4 );
		}
	}

	if( flags & STUDIO_RENDER )
	{
		lighting.plightvec = dir;
		R_StudioDynamicLight( RI.currententity, &lighting );

		R_StudioEntityLight( &lighting );

		// model and frame independant
		R_StudioSetupLighting( &lighting );

		// get remap colors
		g_nTopColor = RI.currententity->curstate.colormap & 0xFF;
		g_nBottomColor = (RI.currententity->curstate.colormap & 0xFF00) >> 8;

		R_StudioSetRemapColors( g_nTopColor, g_nBottomColor );

		R_StudioRenderModel();
	}

	return 1;
}

/*
=================
R_StudioDrawModelInternal
=================
*/
static void R_StudioDrawModelInternal( cl_entity_t *e, int flags )
{
	if( !RI.drawWorld )
	{
		if( e->player )
			R_StudioDrawPlayer( flags, &e->curstate );
		else R_StudioDrawModel( flags );
	}
	else
	{
		// select the properly method
		if( e->player )
			pStudioDraw->StudioDrawPlayer( flags, R_StudioGetPlayerState( e->index - 1 ));
		else pStudioDraw->StudioDrawModel( flags );
	}
}

static cl_entity_t *R_FindParentEntity( cl_entity_t *e, cl_entity_t **entities, uint num_entities )
{
	uint i;

	for( i = 0; i < num_entities; i++ )
	{
		if( entities[i]->index == e->curstate.aiment )
			return entities[i];
	}

	return NULL;
}

/*
=================
R_DrawStudioModel
=================
*/
void R_DrawStudioModel( cl_entity_t *e )
{
	if( FBitSet( RI.params, RP_ENVVIEW ))
		return;

	R_StudioSetupTimings();

	if( e->player )
	{
		R_StudioDrawModelInternal( e, STUDIO_RENDER|STUDIO_EVENTS );
	}
	else if( e->curstate.movetype == MOVETYPE_FOLLOW )
	{
		cl_entity_t *parent = CL_GetEntityByIndex( e->curstate.aiment );

		if( !parent || !parent->model || parent->model->type != mod_studio )
			return;

		parent = R_FindParentEntity( e, tr.draw_list->solid_entities, tr.draw_list->num_solid_entities );

		if( !parent )
			parent = R_FindParentEntity( e, tr.draw_list->trans_entities, tr.draw_list->num_trans_entities );

		if( parent )
		{
			RI.currententity = parent;
			R_StudioDrawModelInternal( RI.currententity, 0 );
			VectorCopy( RI.currententity->curstate.origin, e->curstate.origin );
			VectorCopy( RI.currententity->origin, e->origin );
			RI.currententity = e;

			R_StudioDrawModelInternal( e, STUDIO_RENDER|STUDIO_EVENTS );
		}
	}
	else
	{
		R_StudioDrawModelInternal( e, STUDIO_RENDER|STUDIO_EVENTS );
	}
}

/*
=================
R_RunViewmodelEvents
=================
*/
void R_RunViewmodelEvents( void )
{
	int	i;
	vec3_t simorg;

	if( r_drawviewmodel->value == 0 )
		return;

	if( ENGINE_GET_PARM( PARM_THIRDPERSON ))
		return;

	// ignore in thirdperson, camera view or client is died
	if( !RP_NORMALPASS() || ENGINE_GET_PARM( PARM_LOCAL_HEALTH ) <= 0 || !CL_IsViewEntityLocalPlayer())
		return;

	RI.currententity = tr.viewent;

	if( !RI.currententity->model || RI.currententity->model->type != mod_studio )
		return;

	R_StudioSetupTimings();

	VectorCopy( gp_cl->simorg, simorg );
	for( i = 0; i < 4; i++ )
		VectorCopy( simorg, RI.currententity->attachment[i] );
	RI.currentmodel = RI.currententity->model;

	R_StudioDrawModelInternal( RI.currententity, STUDIO_EVENTS );
}

/*
=================
R_GatherPlayerLight
=================
*/
void R_GatherPlayerLight( void )
{
	cl_entity_t	*view = tr.viewent;
	colorVec		c;

	c = R_LightPoint( view->origin );
	gEngfuncs.SetLocalLightLevel( ( c.r + c.g + c.b ) / 3 );
}

/*
=================
R_DrawViewModel
=================
*/
void R_DrawViewModel( void )
{
	cl_entity_t	*view = tr.viewent;

	R_GatherPlayerLight();

	if( r_drawviewmodel->value == 0 )
		return;

	if( ENGINE_GET_PARM( PARM_THIRDPERSON ))
		return;

	// ignore in thirdperson, camera view or client is died
	if( !RP_NORMALPASS() || ENGINE_GET_PARM( PARM_LOCAL_HEALTH ) <= 0 || !CL_IsViewEntityLocalPlayer())
		return;

	tr.blend = CL_FxBlend( view ) / 255.0f;
	if( !R_ModelOpaque( view->curstate.rendermode ) && tr.blend <= 0.0f )
		return; // invisible ?

	RI.currententity = view;

	if( !RI.currententity->model )
		return;

	// adjust the depth range to prevent view model from poking into walls
	pglDepthRange( gldepthmin, gldepthmin + 0.3f * ( gldepthmax - gldepthmin ));
	RI.currentmodel = RI.currententity->model;

	switch( RI.currententity->model->type )
	{
	case mod_alias:
		R_DrawAliasModel( RI.currententity );
		break;
	case mod_studio:
		R_StudioSetupTimings();
		R_StudioDrawModelInternal( RI.currententity, STUDIO_RENDER );
		break;
	}

	// restore depth range
	pglDepthRange( gldepthmin, gldepthmax );
}

/*
====================
R_StudioLoadTexture

load model texture with unique name
====================
*/
static void R_StudioLoadTexture( model_t *mod, studiohdr_t *phdr, mstudiotexture_t *ptexture )
{
	size_t		size;
	int		flags = 0;
	char		texname[128], name[128], mdlname[128];
	texture_t		*tx = NULL;
	qboolean load_external = false;

	if( ptexture->flags & STUDIO_NF_NORMALMAP )
		flags |= (TF_NORMALMAP);

	// store some textures for remapping
	if( !Q_strnicmp( ptexture->name, "DM_Base", 7 ) || !Q_strnicmp( ptexture->name, "remap", 5 ))
	{
		int	i, size;
		char	val[6];
		byte	*pixels;

		i = mod->numtextures;
		mod->textures = (texture_t **)Mem_Realloc( mod->mempool, mod->textures, ( i + 1 ) * sizeof( texture_t* ));
		size = ptexture->width * ptexture->height + 768;
		tx = Mem_Calloc( mod->mempool, sizeof( *tx ) + size );
		mod->textures[i] = tx;

		// store ranges into anim_min, anim_max etc
		if( !Q_strnicmp( ptexture->name, "DM_Base", 7 ))
		{
			Q_strncpy( tx->name, "DM_Base", sizeof( tx->name ));
			tx->anim_min = PLATE_HUE_START; // topcolor start
			tx->anim_max = PLATE_HUE_END; // topcolor end
			// bottomcolor start always equal is (topcolor end + 1)
			tx->anim_total = SUIT_HUE_END;// bottomcolor end
		}
		else
		{
			Q_strncpy( tx->name, "DM_User", sizeof( tx->name )); // custom remapped
			Q_strncpy( val, ptexture->name + 7, 4 );
			tx->anim_min = bound( 0, Q_atoi( val ), 255 ); // topcolor start
			Q_strncpy( val, ptexture->name + 11, 4 );
			tx->anim_max = bound( 0, Q_atoi( val ), 255 ); // topcolor end
			// bottomcolor start always equal is (topcolor end + 1)
			Q_strncpy( val, ptexture->name + 15, 4 );
			tx->anim_total = bound( 0, Q_atoi( val ), 255 ); // bottomcolor end
		}

		tx->width = ptexture->width;
		tx->height = ptexture->height;

		// the pixels immediately follow the structures
		pixels = (byte *)phdr + ptexture->index;
		memcpy( tx+1, pixels, size );

		ptexture->flags |= STUDIO_NF_COLORMAP;	// yes, this is colormap image
		flags |= TF_FORCE_COLOR;

		mod->numtextures++;	// done
	}

	Q_strncpy( mdlname, mod->name, sizeof( mdlname ));
	COM_FileBase( ptexture->name, name, sizeof( name ));
	COM_StripExtension( mdlname );

	if( FBitSet( ptexture->flags, STUDIO_NF_NOMIPS ))
		SetBits( flags, TF_NOMIPMAP );

	if( FBitSet( gp_host->features, ENGINE_IMPROVED_LINETRACE ) && FBitSet( ptexture->flags, STUDIO_NF_MASKED ))
		flags |= TF_KEEP_SOURCE; // Paranoia2 texture alpha-tracing

	// NOTE: colormaps must have the palette for properly work. Ignore them
	if( Mod_AllowMaterials( ) && !FBitSet( ptexture->flags, STUDIO_NF_COLORMAP ))
	{
		if( R_SearchForTextureReplacement( texname, sizeof( texname ), mdlname, "materials/%s/%s.tga", mdlname, name ))
		{
			int gl_texturenum = GL_LoadTexture( texname, NULL, 0, flags );
			R_TextureReplacementReport( mdlname, gl_texturenum, texname );
			if(( load_external = gl_texturenum != 0 ))
				ptexture->index = gl_texturenum;
		}
	}

	if( !load_external )
	{
		// NOTE: replace index with pointer to start of imagebuffer, ImageLib expected it
		gEngfuncs.Image_SetMDLPointer((byte *)phdr + ptexture->index);
		size = sizeof( mstudiotexture_t ) + ptexture->width * ptexture->height + 768;

		// build the texname
		Q_snprintf( texname, sizeof( texname ), "#%s/%s.mdl", mdlname, name );
		ptexture->index = GL_LoadTexture( texname, (byte *)ptexture, size, flags );
	}

	if( !ptexture->index )
	{
		ptexture->index = tr.defaultTexture;
	}
	else if( tx )
	{
		// duplicate texnum for easy acess
		tx->gl_texturenum = ptexture->index;
	}
}

/*
=================
Mod_StudioLoadTextures
=================
*/
void Mod_StudioLoadTextures( model_t *mod, void *data )
{
	studiohdr_t	*phdr = (studiohdr_t *)data;
	mstudiotexture_t	*ptexture;
	int		i;

	if( !phdr )
		return;

	ptexture = (mstudiotexture_t *)(((byte *)phdr) + phdr->textureindex);
	if( phdr->textureindex > 0 )
	{
		for( i = 0; i < phdr->numtextures; i++ )
			R_StudioLoadTexture( mod, phdr, &ptexture[i] );
	}
}

/*
=================
Mod_StudioUnloadTextures
=================
*/
void Mod_StudioUnloadTextures( void *data )
{
	studiohdr_t	*phdr = (studiohdr_t *)data;
	mstudiotexture_t	*ptexture;
	int		i;

	if( !phdr )
		return;

	ptexture = (mstudiotexture_t *)(((byte *)phdr) + phdr->textureindex);

	// release all textures
	for( i = 0; i < phdr->numtextures; i++ )
	{
		if( ptexture[i].index == tr.defaultTexture )
			continue;
		GL_FreeTexture( ptexture[i].index );
	}
}

static model_t *pfnModelHandle( int modelindex )
{
	if( modelindex < 0 || modelindex >= MAX_MODELS )
		return NULL;
	return CL_ModelHandle( modelindex );
}

static void *pfnMod_CacheCheck( struct cache_user_s *c )
{
	return gEngfuncs.Mod_CacheCheck( c );
}

static void *pfnMod_StudioExtradata( model_t *mod )
{
	return gEngfuncs.Mod_Extradata( mod_studio, mod );
}

static void pfnMod_LoadCacheFile( const char *path, struct cache_user_s *cu )
{
	gEngfuncs.Mod_LoadCacheFile( path, cu );
}

static cvar_t *pfnGetCvarPointer( const char *name )
{
	return (cvar_t*)gEngfuncs.pfnGetCvarPointer( name );
}

static void *pfnMod_Calloc( int number, size_t size )
{
	return gEngfuncs.Mod_Calloc( number, size );
}

static engine_studio_api_t gStudioAPI =
{
	pfnMod_Calloc,
	pfnMod_CacheCheck,
	pfnMod_LoadCacheFile,
	pfnMod_ForName,
	pfnMod_StudioExtradata,
	pfnModelHandle,
	pfnGetCurrentEntity,
	pfnPlayerInfo,
	R_StudioGetPlayerState,
	pfnGetViewEntity,
	pfnGetEngineTimes,
	pfnGetCvarPointer,
	pfnGetViewInfo,
	R_GetChromeSprite,
	pfnGetModelCounters,
	pfnGetAliasScale,
	pfnStudioGetBoneTransform,
	pfnStudioGetLightTransform,
	pfnStudioGetAliasTransform,
	pfnStudioGetRotationMatrix,
	R_StudioSetupModel,
	R_StudioCheckBBox,
	R_StudioDynamicLight,
	R_StudioEntityLight,
	R_StudioSetupLighting,
	R_StudioDrawPoints,
	R_StudioDrawHulls,
	R_StudioDrawAbsBBox,
	R_StudioDrawBones,
	(void*)R_StudioSetupSkin,
	R_StudioSetRemapColors,
	R_StudioSetupPlayerModel,
	R_StudioClientEvents,
	R_StudioGetForceFaceFlags,
	R_StudioSetForceFaceFlags,
	(void*)R_StudioSetHeader,
	R_StudioSetRenderModel,
	R_StudioSetupRenderer,
	R_StudioRestoreRenderer,
	R_StudioSetChromeOrigin,
	pfnIsHardware,
	GL_StudioDrawShadow,
	GL_StudioSetRenderMode,
	R_StudioSetRenderamt,
	R_StudioSetCullState,
	R_StudioRenderShadow,
};

static r_studio_interface_t gStudioDraw =
{
	STUDIO_INTERFACE_VERSION,
	R_StudioDrawModel,
	R_StudioDrawPlayer,
};

/*
===============
CL_InitStudioAPI

Initialize client studio
===============
*/
void CL_InitStudioAPI( void )
{
	pStudioDraw = &gStudioDraw;

	// trying to grab them from client.dll
	cl_righthand = gEngfuncs.pfnGetCvarPointer( "cl_righthand" );

	// Xash will be used internal StudioModelRenderer
	if( gEngfuncs.pfnGetStudioModelInterface( STUDIO_INTERFACE_VERSION, &pStudioDraw, &gStudioAPI ))
		return;

	// NOTE: we always return true even if game interface was not correct
	// because we need Draw our StudioModels
	// just restore pointer to builtin function
	pStudioDraw = &gStudioDraw;
}
