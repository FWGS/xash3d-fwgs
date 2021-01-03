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
*/

#include "r_local.h"
#include "xash3d_mathlib.h"
#include "const.h"
#include "r_studioint.h"
#include "triangleapi.h"
#include "studio.h"
#include "pm_local.h"
//#include "cl_tent.h"
//#include "client.h"
#include "pmtrace.h"

#define EVENT_CLIENT	5000	// less than this value it's a server-side studio events
#define MAX_LOCALLIGHTS	4

typedef struct
{
	char		name[MAX_OSPATH];
	char		modelname[MAX_OSPATH];
	model_t		*model;
} player_model_t;

cvar_t *r_glowshellfreq;

cvar_t r_shadows = { "r_shadows", "0", 0 };

static vec3_t hullcolor[8] =
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
} sortedmesh_t;

#if XASH_LOW_MEMORY
	#undef MAXSTUDIOVERTS
	#define MAXSTUDIOVERTS 1024
#endif

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
	color24		locallightcolor[MAX_LOCALLIGHTS];
	vec4_t		lightpos[MAXSTUDIOVERTS][MAX_LOCALLIGHTS];
	vec3_t		lightbonepos[MAXSTUDIOBONES][MAX_LOCALLIGHTS];
	float		locallightR2[MAX_LOCALLIGHTS];

	// playermodels
	player_model_t  player_models[MAX_CLIENTS];
} studio_draw_state_t;

// studio-related cvars
static cvar_t			*r_studio_sort_textures;
static cvar_t			*r_drawviewmodel;
cvar_t			*cl_righthand = NULL;
static cvar_t			*cl_himodels;
static cvar_t			*r_studio_drawelements;

static r_studio_interface_t	*pStudioDraw;
static studio_draw_state_t	g_studio;		// global studio state

// global variables
static qboolean		m_fDoRemap;
mstudiomodel_t		*m_pSubModel;
mstudiobodyparts_t		*m_pBodyPart;
player_info_t		*m_pPlayerInfo;
studiohdr_t		*m_pStudioHeader;
float			m_flGaitMovement;
int			g_iBackFaceCull;
int			g_nTopColor, g_nBottomColor;	// remap colors
int			g_nFaceFlags, g_nForceFaceFlags;

/*
====================
R_StudioInit

====================
*/
void R_StudioInit( void )
{
	cl_himodels = gEngfuncs.Cvar_Get( "cl_himodels", "1", FCVAR_ARCHIVE, "draw high-resolution player models in multiplayer" );
	r_studio_sort_textures = gEngfuncs.Cvar_Get( "r_studio_sort_textures", "0", FCVAR_ARCHIVE, "change draw order for additive meshes" );
	r_drawviewmodel = gEngfuncs.Cvar_Get( "r_drawviewmodel", "1", 0, "draw firstperson weapon model" );
	r_studio_drawelements = gEngfuncs.Cvar_Get( "r_studio_drawelements", "1", FCVAR_ARCHIVE, "use glDrawElements for studiomodels" );

	Matrix3x4_LoadIdentity( g_studio.rotationmatrix );
	r_glowshellfreq = gEngfuncs.Cvar_Get( "r_glowshellfreq", "2.2", 0, "glowing shell frequency update" );

	// g-cont. cvar disabled by Valve
//	gEngfuncs.Cvar_RegisterVariable( &r_shadows );

	g_studio.interpolate = true;
	g_studio.framecount = 0;
	m_fDoRemap = false;
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
		g_studio.time = gpGlobals->time;
		g_studio.frametime = gpGlobals->time -   gpGlobals->oldtime;
	}
	else
	{
		// menu stuff
		g_studio.time = gpGlobals->realtime;
		g_studio.frametime = gpGlobals->frametime;
	}
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
		if( e == gEngfuncs.GetViewModel() )
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
	if( !VectorCompare( vec3_origin, RI.currentmodel->mins ))
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

void R_StudioComputeSkinMatrix( mstudioboneweight_t *boneweights, matrix3x4 result )
{
	float	flWeight0, flWeight1, flWeight2, flWeight3;
	int	i, numbones = 0;
	float	flTotal;

	for( i = 0; i < MAXSTUDIOBONEWEIGHTS; i++ )
	{
		if( boneweights->bone[i] != -1 )
			numbones++;
	}

	if( numbones == 4 )
	{
		vec4_t *boneMat0 = (vec4_t *)g_studio.worldtransform[boneweights->bone[0]];
		vec4_t *boneMat1 = (vec4_t *)g_studio.worldtransform[boneweights->bone[1]];
		vec4_t *boneMat2 = (vec4_t *)g_studio.worldtransform[boneweights->bone[2]];
		vec4_t *boneMat3 = (vec4_t *)g_studio.worldtransform[boneweights->bone[3]];
		flWeight0 = boneweights->weight[0] / 255.0f;
		flWeight1 = boneweights->weight[1] / 255.0f;
		flWeight2 = boneweights->weight[2] / 255.0f;
		flWeight3 = boneweights->weight[3] / 255.0f;
		flTotal = flWeight0 + flWeight1 + flWeight2 + flWeight3;

		if( flTotal < 1.0f ) flWeight0 += 1.0f - flTotal;	// compensate rounding error

		result[0][0] = boneMat0[0][0] * flWeight0 + boneMat1[0][0] * flWeight1 + boneMat2[0][0] * flWeight2 + boneMat3[0][0] * flWeight3;
		result[0][1] = boneMat0[0][1] * flWeight0 + boneMat1[0][1] * flWeight1 + boneMat2[0][1] * flWeight2 + boneMat3[0][1] * flWeight3;
		result[0][2] = boneMat0[0][2] * flWeight0 + boneMat1[0][2] * flWeight1 + boneMat2[0][2] * flWeight2 + boneMat3[0][2] * flWeight3;
		result[0][3] = boneMat0[0][3] * flWeight0 + boneMat1[0][3] * flWeight1 + boneMat2[0][3] * flWeight2 + boneMat3[0][3] * flWeight3;
		result[1][0] = boneMat0[1][0] * flWeight0 + boneMat1[1][0] * flWeight1 + boneMat2[1][0] * flWeight2 + boneMat3[1][0] * flWeight3;
		result[1][1] = boneMat0[1][1] * flWeight0 + boneMat1[1][1] * flWeight1 + boneMat2[1][1] * flWeight2 + boneMat3[1][1] * flWeight3;
		result[1][2] = boneMat0[1][2] * flWeight0 + boneMat1[1][2] * flWeight1 + boneMat2[1][2] * flWeight2 + boneMat3[1][2] * flWeight3;
		result[1][3] = boneMat0[1][3] * flWeight0 + boneMat1[1][3] * flWeight1 + boneMat2[1][3] * flWeight2 + boneMat3[1][3] * flWeight3;
		result[2][0] = boneMat0[2][0] * flWeight0 + boneMat1[2][0] * flWeight1 + boneMat2[2][0] * flWeight2 + boneMat3[2][0] * flWeight3;
		result[2][1] = boneMat0[2][1] * flWeight0 + boneMat1[2][1] * flWeight1 + boneMat2[2][1] * flWeight2 + boneMat3[2][1] * flWeight3;
		result[2][2] = boneMat0[2][2] * flWeight0 + boneMat1[2][2] * flWeight1 + boneMat2[2][2] * flWeight2 + boneMat3[2][2] * flWeight3;
		result[2][3] = boneMat0[2][3] * flWeight0 + boneMat1[2][3] * flWeight1 + boneMat2[2][3] * flWeight2 + boneMat3[2][3] * flWeight3;
	}
	else if( numbones == 3 )
	{
		vec4_t *boneMat0 = (vec4_t *)g_studio.worldtransform[boneweights->bone[0]];
		vec4_t *boneMat1 = (vec4_t *)g_studio.worldtransform[boneweights->bone[1]];
		vec4_t *boneMat2 = (vec4_t *)g_studio.worldtransform[boneweights->bone[2]];
		flWeight0 = boneweights->weight[0] / 255.0f;
		flWeight1 = boneweights->weight[1] / 255.0f;
		flWeight2 = boneweights->weight[2] / 255.0f;
		flTotal = flWeight0 + flWeight1 + flWeight2;

		if( flTotal < 1.0f ) flWeight0 += 1.0f - flTotal;	// compensate rounding error

		result[0][0] = boneMat0[0][0] * flWeight0 + boneMat1[0][0] * flWeight1 + boneMat2[0][0] * flWeight2;
		result[0][1] = boneMat0[0][1] * flWeight0 + boneMat1[0][1] * flWeight1 + boneMat2[0][1] * flWeight2;
		result[0][2] = boneMat0[0][2] * flWeight0 + boneMat1[0][2] * flWeight1 + boneMat2[0][2] * flWeight2;
		result[0][3] = boneMat0[0][3] * flWeight0 + boneMat1[0][3] * flWeight1 + boneMat2[0][3] * flWeight2;
		result[1][0] = boneMat0[1][0] * flWeight0 + boneMat1[1][0] * flWeight1 + boneMat2[1][0] * flWeight2;
		result[1][1] = boneMat0[1][1] * flWeight0 + boneMat1[1][1] * flWeight1 + boneMat2[1][1] * flWeight2;
		result[1][2] = boneMat0[1][2] * flWeight0 + boneMat1[1][2] * flWeight1 + boneMat2[1][2] * flWeight2;
		result[1][3] = boneMat0[1][3] * flWeight0 + boneMat1[1][3] * flWeight1 + boneMat2[1][3] * flWeight2;
		result[2][0] = boneMat0[2][0] * flWeight0 + boneMat1[2][0] * flWeight1 + boneMat2[2][0] * flWeight2;
		result[2][1] = boneMat0[2][1] * flWeight0 + boneMat1[2][1] * flWeight1 + boneMat2[2][1] * flWeight2;
		result[2][2] = boneMat0[2][2] * flWeight0 + boneMat1[2][2] * flWeight1 + boneMat2[2][2] * flWeight2;
		result[2][3] = boneMat0[2][3] * flWeight0 + boneMat1[2][3] * flWeight1 + boneMat2[2][3] * flWeight2;
	}
	else if( numbones == 2 )
	{
		vec4_t *boneMat0 = (vec4_t *)g_studio.worldtransform[boneweights->bone[0]];
		vec4_t *boneMat1 = (vec4_t *)g_studio.worldtransform[boneweights->bone[1]];
		flWeight0 = boneweights->weight[0] / 255.0f;
		flWeight1 = boneweights->weight[1] / 255.0f;
		flTotal = flWeight0 + flWeight1;

		if( flTotal < 1.0f ) flWeight0 += 1.0f - flTotal;	// compensate rounding error

		result[0][0] = boneMat0[0][0] * flWeight0 + boneMat1[0][0] * flWeight1;
		result[0][1] = boneMat0[0][1] * flWeight0 + boneMat1[0][1] * flWeight1;
		result[0][2] = boneMat0[0][2] * flWeight0 + boneMat1[0][2] * flWeight1;
		result[0][3] = boneMat0[0][3] * flWeight0 + boneMat1[0][3] * flWeight1;
		result[1][0] = boneMat0[1][0] * flWeight0 + boneMat1[1][0] * flWeight1;
		result[1][1] = boneMat0[1][1] * flWeight0 + boneMat1[1][1] * flWeight1;
		result[1][2] = boneMat0[1][2] * flWeight0 + boneMat1[1][2] * flWeight1;
		result[1][3] = boneMat0[1][3] * flWeight0 + boneMat1[1][3] * flWeight1;
		result[2][0] = boneMat0[2][0] * flWeight0 + boneMat1[2][0] * flWeight1;
		result[2][1] = boneMat0[2][1] * flWeight0 + boneMat1[2][1] * flWeight1;
		result[2][2] = boneMat0[2][2] * flWeight0 + boneMat1[2][2] * flWeight1;
		result[2][3] = boneMat0[2][3] * flWeight0 + boneMat1[2][3] * flWeight1;
	}
	else
	{
		Matrix3x4_Copy( result, g_studio.worldtransform[boneweights->bone[0]] );
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
entity_state_t *R_StudioGetPlayerState( int index )
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
	return gEngfuncs.GetViewModel();
}

/*
===============
pfnGetEngineTimes

===============
*/
static void pfnGetEngineTimes( int *framecount, double *current, double *old )
{
	if( framecount ) *framecount = tr.realframecount;
	if( current ) *current = gpGlobals->time;
	if( old ) *old =   gpGlobals->oldtime;
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
void R_StudioPlayerBlend( mstudioseqdesc_t *pseqdesc, int *pBlend, float *pPitch )
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
void GAME_EXPORT R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
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
void R_StudioSetUpTransform( cl_entity_t *e )
{
	vec3_t	origin, angles;

	VectorCopy( e->origin, origin );
	VectorCopy( e->angles, angles );

	// interpolate monsters position (moved into UpdateEntityFields by user request)
	if( e->curstate.movetype == MOVETYPE_STEP && !FBitSet( ENGINE_GET_PARM( PARM_FEATURES ), ENGINE_COMPUTE_STUDIO_LERP ))
	{
		R_StudioLerpMovement( e, g_studio.time, origin, angles );
	}

	if( !FBitSet( ENGINE_GET_PARM( PARM_FEATURES ), ENGINE_COMPENSATE_QUAKE_BUG ))
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
float GAME_EXPORT R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc )
{
	double	dfdt, f;

	if( g_studio.interpolate )
	{
		if( g_studio.time < e->curstate.animtime ) dfdt = 0.0;
		else dfdt = (g_studio.time - e->curstate.animtime) * e->curstate.framerate * pseqdesc->fps;
	}
	else dfdt = 0;

	if( pseqdesc->numframes <= 1 ) f = 0.0;
	else f = (e->curstate.frame * (pseqdesc->numframes - 1)) / 256.0;

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
float R_StudioEstimateInterpolant( cl_entity_t *e )
{
	float	dadt = 1.0f;

	if( g_studio.interpolate && ( e->curstate.animtime >= e->latched.prevanimtime + 0.01f ))
	{
		dadt = ( g_studio.time - e->curstate.animtime ) / 0.1f;
		if( dadt > 2.0f ) dadt = 2.0f;
	}

	return dadt;
}

/*
====================
CL_GetSequenceDuration

====================
*/
float CL_GetSequenceDuration( cl_entity_t *ent, int sequence )
{
	studiohdr_t	*pstudiohdr;
	mstudioseqdesc_t	*pseqdesc;

	if( ent->model != NULL && ent->model->type == mod_studio )
	{
		pstudiohdr = (studiohdr_t *)gEngfuncs.Mod_Extradata( mod_studio, ent->model );

		if( pstudiohdr )
		{
			sequence = bound( 0, sequence, pstudiohdr->numseq - 1 );
			pseqdesc = (mstudioseqdesc_t *)((byte *)pstudiohdr + pstudiohdr->seqindex) + sequence;

			if( pseqdesc->numframes > 1 && pseqdesc->fps > 0 )
				return (float)pseqdesc->numframes / (float)pseqdesc->fps;
		}
	}

	return 0.1f;
}


/*
====================
StudioFxTransform

====================
*/
void R_StudioFxTransform( cl_entity_t *ent, matrix3x4 transform )
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
void R_StudioCalcBoneAdj( float dadt, float *adj, const byte *pcontroller1, const byte *pcontroller2, byte mouthopen )
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
void R_StudioCalcRotations( cl_entity_t *e, float pos[][3], vec4_t *q, mstudioseqdesc_t *pseqdesc, mstudioanim_t *panim, float f )
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
	{
		gEngfuncs.R_StudioCalcBoneQuaternion( frame, s, pbone, panim, adj, q[i] );
		gEngfuncs.R_StudioCalcBonePosition( frame, s, pbone, panim, adj, pos[i] );
	}

	if( pseqdesc->motiontype & STUDIO_X ) pos[pseqdesc->motionbone][0] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Y ) pos[pseqdesc->motionbone][1] = 0.0f;
	if( pseqdesc->motiontype & STUDIO_Z ) pos[pseqdesc->motionbone][2] = 0.0f;
}

/*
====================
StudioMergeBones

====================
*/
void R_StudioMergeBones( cl_entity_t *e, model_t *m_pSubModel )
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

	f = R_StudioEstimateFrame( e, pseqdesc );

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
void R_StudioSetupBones( cl_entity_t *e )
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

	f = R_StudioEstimateFrame( e, pseqdesc );

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

		gEngfuncs.R_StudioSlerpBones( m_pStudioHeader->numbones, q, pos, q2, pos2, s );

		if( pseqdesc->numblends == 4 )
		{
			panim += m_pStudioHeader->numbones;
			R_StudioCalcRotations( e, pos3, q3, pseqdesc, panim, f );

			panim += m_pStudioHeader->numbones;
			R_StudioCalcRotations( e, pos4, q4, pseqdesc, panim, f );

			s = (e->curstate.blending[0] * dadt + e->latched.prevblending[0] * (1.0f - dadt)) / 255.0f;
			gEngfuncs.R_StudioSlerpBones( m_pStudioHeader->numbones, q3, pos3, q4, pos4, s );

			s = (e->curstate.blending[1] * dadt + e->latched.prevblending[1] * (1.0f - dadt)) / 255.0f;
			gEngfuncs.R_StudioSlerpBones( m_pStudioHeader->numbones, q, pos, q3, pos3, s );
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
			gEngfuncs.R_StudioSlerpBones( m_pStudioHeader->numbones, q1b, pos1b, q2, pos2, s );

			if( pseqdesc->numblends == 4 )
			{
				panim += m_pStudioHeader->numbones;
				R_StudioCalcRotations( e, pos3, q3, pseqdesc, panim, e->latched.prevframe );

				panim += m_pStudioHeader->numbones;
				R_StudioCalcRotations( e, pos4, q4, pseqdesc, panim, e->latched.prevframe );

				s = (e->latched.prevseqblending[0]) / 255.0f;
				gEngfuncs.R_StudioSlerpBones( m_pStudioHeader->numbones, q3, pos3, q4, pos4, s );

				s = (e->latched.prevseqblending[1]) / 255.0f;
				gEngfuncs.R_StudioSlerpBones( m_pStudioHeader->numbones, q1b, pos1b, q3, pos3, s );
			}
		}

		s = 1.0f - ( g_studio.time - e->latched.sequencetime ) / 0.2f;
		gEngfuncs.R_StudioSlerpBones( m_pStudioHeader->numbones, q, pos, q1b, pos1b, s );
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
void R_StudioBuildNormalTable( void )
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
void R_StudioGenerateNormals( void )
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
void R_StudioSetupChrome( float *pchrome, int bone, vec3_t normal )
{
	float	n;

	if( g_studio.chromeage[bone] != g_studio.framecount )
	{
		// calculate vectors from the viewer to the bone. This roughly adjusts for position
		vec3_t	chromeupvec;	// g_studio.chrome t vector in world reference frame
		vec3_t	chromerightvec;	// g_studio.chrome s vector in world reference frame
		vec3_t	tmp;		// vector pointing at bone in world reference frame

		VectorNegate( g_studio.chrome_origin, tmp );
		tmp[0] += g_studio.bonestransform[bone][0][3];
		tmp[1] += g_studio.bonestransform[bone][1][3];
		tmp[2] += g_studio.bonestransform[bone][2][3];

		VectorNormalize( tmp );
		CrossProduct( tmp, RI.vright, chromeupvec );
		VectorNormalize( chromeupvec );
		CrossProduct( tmp, chromeupvec, chromerightvec );
		VectorNormalize( chromerightvec );

		Matrix3x4_VectorIRotate( g_studio.bonestransform[bone], chromeupvec, g_studio.chromeup[bone] );
		Matrix3x4_VectorIRotate( g_studio.bonestransform[bone], chromerightvec, g_studio.chromeright[bone] );

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
	vec3_t		forward, bonepos;
	vec3_t		localOrg, localAng;
	int		i;

	// calculate attachment points
	pAtt = (mstudioattachment_t *)((byte *)m_pStudioHeader + m_pStudioHeader->attachmentindex);

	for( i = 0; i < Q_min( MAXSTUDIOATTACHMENTS, m_pStudioHeader->numattachments ); i++ )
	{
		Matrix3x4_VectorTransform( g_studio.lighttransform[pAtt[i].bone], pAtt[i].org, RI.currententity->attachment[i] );
		VectorSubtract( RI.currententity->attachment[i], RI.currententity->origin, localOrg );
		Matrix3x4_OriginFromMatrix( g_studio.lighttransform[pAtt[i].bone], bonepos );
		VectorSubtract( localOrg, bonepos, forward );	// make forward
		VectorNormalizeFast( forward );
		VectorAngles( forward, localAng );
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

	if( bodypart > m_pStudioHeader->numbodyparts )
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
void R_StudioDynamicLight( cl_entity_t *ent, alight_t *plight )
{
	movevars_t	*mv = gEngfuncs.pfnGetMoveVars();
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

	if(( mv->skycolor_r + mv->skycolor_g + mv->skycolor_b ) != 0 )
	{
		msurface_t	*psurf = NULL;
		pmtrace_t		trace;

		if( FBitSet( ENGINE_GET_PARM( PARM_FEATURES ), ENGINE_WRITE_LARGE_COORD ))
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

		trace = gEngfuncs.CL_TraceLine( vecSrc, vecEnd, PM_WORLD_ONLY );
		if( trace.ent > 0 ) psurf = gEngfuncs.EV_TraceSurface( trace.ent, vecSrc, vecEnd );
		else psurf = gEngfuncs.EV_TraceSurface( 0, vecSrc, vecEnd );

		if( FBitSet( ent->model->flags, STUDIO_FORCE_SKYLIGHT ) || ( psurf && FBitSet( psurf->flags, SURF_DRAWSKY )))
		{
			VectorSet( lightDir, mv->skyvec_x, mv->skyvec_y, mv->skyvec_z );

			light.r = gEngfuncs.LightToTexGamma( bound( 0, mv->skycolor_r, 255 ));
			light.g = gEngfuncs.LightToTexGamma( bound( 0, mv->skycolor_g, 255 ));
			light.b = gEngfuncs.LightToTexGamma( bound( 0, mv->skycolor_b, 255 ));
		}
	}

	if(( light.r + light.g + light.b ) < 16 ) // TESTTEST
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

	VectorSet( finalLight, light.r, light.g, light.b );
	ent->cvFloorColor = light;

	total = Q_max( Q_max( light.r, light.g ), light.b );
	if( total == 0.0f ) total = 1.0f;

	// scale lightdir by light intentsity
	VectorScale( lightDir, total, lightDir );

	for( lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		dl = gEngfuncs.GetDynamicLight( lnum );

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

			finalLight[0] += gEngfuncs.LightToTexGamma( dl->color.r ) * ( add / 256.0f ) * 2.0f;
			finalLight[1] += gEngfuncs.LightToTexGamma( dl->color.g ) * ( add / 256.0f ) * 2.0f;
			finalLight[2] += gEngfuncs.LightToTexGamma( dl->color.b ) * ( add / 256.0f ) * 2.0f;
		}
	}

	if( FBitSet( ent->model->flags, STUDIO_AMBIENT_LIGHT ))
		add = 0.6f;
	else add = 0.9f;

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
void R_StudioEntityLight( alight_t *lightinfo )
{
	int		lnum, i, j, k;
	float		minstrength, dist2, f, r2;
	float		lstrength[MAX_LOCALLIGHTS];
	cl_entity_t	*ent = RI.currententity;
	vec3_t		mid, origin, pos;
	dlight_t		*el;

	g_studio.numlocallights = 0;

	if( !ent || !r_dynamic->value )
		return;

	for( i = 0; i < MAX_LOCALLIGHTS; i++ )
		lstrength[i] = 0;

	Matrix3x4_OriginFromMatrix( g_studio.rotationmatrix, origin );
	dist2 = 1000000.0f;
	k = 0;

	for( lnum = 0; lnum < MAX_ELIGHTS; lnum++ )
	{
		el = gEngfuncs.GetEntityLight( lnum );

		if( el->die < g_studio.time || el->radius <= 0.0f )
			continue;

		if(( el->key & 0xFFF ) == ent->index )
		{
			int	att = (el->key >> 12) & 0xF;

			if( att ) VectorCopy( ent->attachment[att], el->origin );
			else VectorCopy( ent->origin, el->origin );
		}

		VectorCopy( el->origin, pos );
		VectorSubtract( origin, el->origin, mid );

		f = DotProduct( mid, mid );
		r2 = el->radius * el->radius;

		if( f > r2 ) minstrength = r2 / f;
		else minstrength = 1.0f;

		if( minstrength > 0.05f )
		{
			if( g_studio.numlocallights >= MAX_LOCALLIGHTS )
			{
				for( j = 0, k = -1; j < g_studio.numlocallights; j++ )
				{
					if( lstrength[j] < dist2 && lstrength[j] < minstrength )
					{
						dist2 = lstrength[j];
						k = j;
					}
				}
			}
			else k = g_studio.numlocallights;

			if( k != -1 )
			{
				g_studio.locallightcolor[k].r = gEngfuncs.LightToTexGamma( el->color.r );
				g_studio.locallightcolor[k].g = gEngfuncs.LightToTexGamma( el->color.g );
				g_studio.locallightcolor[k].b = gEngfuncs.LightToTexGamma( el->color.b );
				g_studio.locallightR2[k] = r2;
				g_studio.locallight[k] = el;
				lstrength[k] = minstrength;

				if( k >= g_studio.numlocallights )
					g_studio.numlocallights = k + 1;
			}
		}
	}
}

/*
===============
R_StudioSetupLighting

===============
*/
void R_StudioSetupLighting( alight_t *plight )
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
void R_StudioLighting( float *lv, int bone, int flags, vec3_t normal )
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
	*lv = illum * (1.0f / 255.0f);
}

/*
====================
R_LightLambert

====================
*/
void R_LightLambert( vec4_t light[MAX_LOCALLIGHTS], vec3_t normal, vec3_t color, byte *out )
{
	vec3_t	finalLight;
	vec3_t	localLight;
	int	i;

	VectorCopy( color, finalLight );

	for( i = 0; i < g_studio.numlocallights; i++ )
	{
		float	r, r2;

		if( tr.fFlipViewModel )
			r = DotProduct( normal, light[i] );
		else r = -DotProduct( normal, light[i] );

		if( r > 0.0f )
		{
			if( light[i][3] == 0.0f )
			{
				r2 = DotProduct( light[i], light[i] );

				if( r2 > 0.0f )
					light[i][3] = g_studio.locallightR2[i] / ( r2 * sqrt( r2 ));
				else light[i][3] = 0.0001f;
			}

			localLight[0] = Q_min( g_studio.locallightcolor[i].r * r * light[i][3], 255.0f );
			localLight[1] = Q_min( g_studio.locallightcolor[i].g * r * light[i][3], 255.0f );
			localLight[2] = Q_min( g_studio.locallightcolor[i].b * r * light[i][3], 255.0f );
			VectorScale( localLight, ( 1.0f / 255.0f ), localLight );

			finalLight[0] = Q_min( finalLight[0] + localLight[0], 1.0f );
			finalLight[1] = Q_min( finalLight[1] + localLight[1], 1.0f );
			finalLight[2] = Q_min( finalLight[2] + localLight[2], 1.0f );
		}
	}

	out[0] = finalLight[0] * 255;
	out[1] = finalLight[1] * 255;
	out[2] = finalLight[2] * 255;
}

static void R_StudioSetColorBegin(short *ptricmds, vec3_t *pstudionorms )
{
	float	*lv = (float *)g_studio.lightvalues[ptricmds[1]];
	rgba_t color;

	if( g_studio.numlocallights )
	{
		color[3] = tr.blend * 255;
		R_LightLambert( g_studio.lightpos[ptricmds[0]], pstudionorms[ptricmds[1]], lv, color );
		//pglColor4ubv( color );
		TriColor4ub(color[0], color[1], color[2], color[3]);
	}
	else
	{
		if( RI.currententity->curstate.rendermode == kRenderTransColor )
		{
			color[3] = tr.blend * 255;
			VectorCopy( (byte*)&RI.currententity->curstate.rendercolor, color );
			//pglColor4ubv( color );
			TriColor4ub(color[0], color[1], color[2], color[3]);
		}
		else _TriColor4f( lv[0], lv[1], lv[2], tr.blend );
	}
}


/*
====================
R_LightStrength

====================
*/
void R_LightStrength( int bone, vec3_t localpos, vec4_t light[MAX_LOCALLIGHTS] )
{
	int	i;

	if( g_studio.lightage[bone] != g_studio.framecount )
	{
		for( i = 0; i < g_studio.numlocallights; i++ )
		{
			dlight_t *el = g_studio.locallight[i];
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
	{
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture);
		return;
	}

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
mstudiotexture_t * GAME_EXPORT R_StudioGetTexture( cl_entity_t *e )
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

void R_StudioSetRenderamt( int iRenderamt )
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
void R_StudioSetCullState( int iCull )
{
	g_iBackFaceCull = iCull;
}

/*
===============
R_StudioRenderShadow

just a prefab for render shadow
===============
*/
void R_StudioRenderShadow( int iSprite, float *p1, float *p2, float *p3, float *p4 )
{
	if( !p1 || !p2 || !p3 || !p4 )
		return;

	if( TriSpriteTexture( gEngfuncs.pfnGetModelByIndex( iSprite ), 0 ))
	{
		TriRenderMode( kRenderTransAlpha );
		_TriColor4f( 0.0f, 0.0f, 0.0f, 1.0f );

		TriBegin( TRI_QUADS );
			TriTexCoord2f( 0.0f, 0.0f );
			TriVertex3fv( p1 );
			TriTexCoord2f( 0.0f, 1.0f );
			TriVertex3fv( p2 );
			TriTexCoord2f( 1.0f, 1.0f );
			TriVertex3fv( p3 );
			TriTexCoord2f( 1.0f, 0.0f );
			TriVertex3fv( p4 );
		TriEnd();

		TriRenderMode( kRenderNormal );
	}
}

/*
===============
R_StudioMeshCompare

Sorting opaque entities by model type
===============
*/
static int R_StudioMeshCompare( const sortedmesh_t *a, const sortedmesh_t *b )
{
	if( FBitSet( a->flags, STUDIO_NF_ADDITIVE ))
		return 1;

	if( FBitSet( a->flags, STUDIO_NF_MASKED ))
		return -1;

	return 0;
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
_inline void R_StudioDrawNormalMesh( short *ptricmds, vec3_t *pstudionorms, float s, float t )
{
	float	*lv;
	int	i;

	while(( i = *( ptricmds++ )))
	{
		if( i < 0 )
		{
			TriBegin( TRI_TRIANGLE_FAN );
			i = -i;
		}
		else TriBegin( TRI_TRIANGLE_STRIP );

		for( ; i > 0; i--, ptricmds += 4 )
		{
			R_StudioSetColorBegin( ptricmds, pstudionorms );

			TriTexCoord2f( ptricmds[2] * s, ptricmds[3] * t );
			TriVertex3fv( g_studio.verts[ptricmds[0]] );
		}

		TriEnd();
	}
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
_inline void R_StudioDrawFloatMesh( short *ptricmds, vec3_t *pstudionorms )
{
	float	*lv;
	int	i;

	while(( i = *( ptricmds++ )))
	{
		if( i < 0 )
		{
			TriBegin( TRI_TRIANGLE_FAN );
			i = -i;
		}
		else TriBegin( TRI_TRIANGLE_STRIP );

		for( ; i > 0; i--, ptricmds += 4 )
		{
			R_StudioSetColorBegin( ptricmds, pstudionorms );
			TriTexCoord2f( HalfToFloat( ptricmds[2] ), HalfToFloat( ptricmds[3] ));
			TriVertex3fv( g_studio.verts[ptricmds[0]] );
		}

		TriEnd();
	}
}

/*
===============
R_StudioDrawNormalMesh

generic path
===============
*/
_inline void R_StudioDrawChromeMesh( short *ptricmds, vec3_t *pstudionorms, float s, float t, float scale )
{
	float	*lv, *av;
	int	i, idx;
	qboolean	glowShell = (scale > 0.0f) ? true : false;
	vec3_t	vert;

	while(( i = *( ptricmds++ )))
	{
		if( i < 0 )
		{
			TriBegin( TRI_TRIANGLE_FAN );
			i = -i;
		}
		else TriBegin( TRI_TRIANGLE_STRIP );

		for( ; i > 0; i--, ptricmds += 4 )
		{
			if( glowShell )
			{
				color24 *clr = &RI.currententity->curstate.rendercolor;

				idx = g_studio.normaltable[ptricmds[0]];
				av = g_studio.verts[ptricmds[0]];
				lv = g_studio.norms[ptricmds[0]];
				VectorMA( av, scale, lv, vert );
				TriColor4ub( clr->r, clr->g, clr->b, 255 );
				TriTexCoord2f( g_studio.chrome[idx][0] * s, g_studio.chrome[idx][1] * t );
				TriVertex3fv( vert );
			}
			else
			{
				idx = ptricmds[1];
				lv = (float *)g_studio.lightvalues[ptricmds[1]];
				R_StudioSetColorBegin( ptricmds, pstudionorms );
				TriTexCoord2f( g_studio.chrome[idx][0] * s, g_studio.chrome[idx][1] * t );
				TriVertex3fv( g_studio.verts[ptricmds[0]] );
			}
		}

		TriEnd();
	}
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
	vec3_t		*pstudioverts;
	vec3_t		*pstudionorms;
	mstudiotexture_t	*ptexture;
	mstudiomesh_t	*pmesh;
	short		*pskinref;
	float		lv_tmp;

	if( !m_pStudioHeader ) return;

	// safety bounding the skinnum
	m_skinnum = bound( 0, RI.currententity->curstate.skin, ( m_pStudioHeader->numskinfamilies - 1 ));
	ptexture = (mstudiotexture_t *)((byte *)m_pStudioHeader + m_pStudioHeader->textureindex);
	pvertbone = ((byte *)m_pStudioHeader + m_pSubModel->vertinfoindex);
	pnormbone = ((byte *)m_pStudioHeader + m_pSubModel->norminfoindex);

	pmesh = (mstudiomesh_t *)((byte *)m_pStudioHeader + m_pSubModel->meshindex);
	pstudioverts = (vec3_t *)((byte *)m_pStudioHeader + m_pSubModel->vertindex);
	pstudionorms = (vec3_t *)((byte *)m_pStudioHeader + m_pSubModel->normindex);

	pskinref = (short *)((byte *)m_pStudioHeader + m_pStudioHeader->skinindex);
	if( m_skinnum != 0 ) pskinref += (m_skinnum * m_pStudioHeader->numskinref);

	if( FBitSet( m_pStudioHeader->flags, STUDIO_HAS_BONEWEIGHTS ) && m_pSubModel->blendvertinfoindex != 0 && m_pSubModel->blendnorminfoindex != 0 )
	{
		mstudioboneweight_t	*pvertweight = (mstudioboneweight_t *)((byte *)m_pStudioHeader + m_pSubModel->blendvertinfoindex);
		mstudioboneweight_t	*pnormweight = (mstudioboneweight_t *)((byte *)m_pStudioHeader + m_pSubModel->blendnorminfoindex);
		matrix3x4		skinMat;

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

	if( r_studio_sort_textures->value && need_sort )
	{
		// resort opaque and translucent meshes draw order
		qsort( g_studio.meshes, m_pSubModel->nummesh, sizeof( sortedmesh_t ), (void*)R_StudioMeshCompare );
	}

	// NOTE: rewind normals at start
	pstudionorms = (vec3_t *)((byte *)m_pStudioHeader + m_pSubModel->normindex);

	for( j = 0; j < m_pSubModel->nummesh; j++ )
	{
		float	oldblend = tr.blend;
		short	*ptricmds;
		float	s, t;

		pmesh = g_studio.meshes[j].mesh;
		ptricmds = (short *)((byte *)m_pStudioHeader + pmesh->triindex);

		g_nFaceFlags = ptexture[pskinref[pmesh->skinref]].flags | g_nForceFaceFlags;

		s = 1.0f / (float)ptexture[pskinref[pmesh->skinref]].width;
		t = 1.0f / (float)ptexture[pskinref[pmesh->skinref]].height;

		if( FBitSet( g_nFaceFlags, STUDIO_NF_MASKED ))
		{
			//pglEnable( GL_ALPHA_TEST );
			//pglAlphaFunc( GL_GREATER, 0.5f );
			//pglDepthMask( GL_TRUE );
			if( R_ModelOpaque( RI.currententity->curstate.rendermode ))
				tr.blend = 1.0f;
		}
		else if( FBitSet( g_nFaceFlags, STUDIO_NF_ADDITIVE ))
		{
			if( R_ModelOpaque( RI.currententity->curstate.rendermode ))
			{
				//pglBlendFunc( GL_ONE, GL_ONE );
				//pglDepthMask( GL_FALSE );
				//pglEnable( GL_BLEND );
				R_AllowFog( false );
			}
			//else pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
		}

		R_StudioSetupSkin( m_pStudioHeader, pskinref[pmesh->skinref] );

		{
			if( FBitSet( g_nFaceFlags, STUDIO_NF_CHROME ))
				R_StudioDrawChromeMesh( ptricmds, pstudionorms, s, t, shellscale );
			else if( FBitSet( g_nFaceFlags, STUDIO_NF_UV_COORDS ))
				R_StudioDrawFloatMesh( ptricmds, pstudionorms );
			else R_StudioDrawNormalMesh( ptricmds, pstudionorms, s, t );
		}

		if( FBitSet( g_nFaceFlags, STUDIO_NF_MASKED ))
		{
			//pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
			//pglDisable( GL_ALPHA_TEST );
		}
		else if( FBitSet( g_nFaceFlags, STUDIO_NF_ADDITIVE ) && R_ModelOpaque( RI.currententity->curstate.rendermode ))
		{
			//pglDepthMask( GL_TRUE );
			//pglDisable( GL_BLEND );
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
#if 0
	float	alpha, lv;
	int	i, j;

	if( r_drawentities->value == 4 )
		alpha = 0.5f;
	else alpha = 1.0f;

	GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	//pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

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
		_TriColor4f( hullcolor[j][0], hullcolor[j][1], hullcolor[j][2], alpha );

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
#endif
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
	if( RI.currententity == gEngfuncs.GetViewModel() )
		return;

	if( !R_StudioComputeBBox( p ))
		return;

	GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
	_TriColor4f( 0.5f, 0.5f, 1.0f, 0.5f );
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
#if 0
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
#endif
}

static void R_StudioDrawAttachments( void )
{
	int	i;
#if 0
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
#endif
}

/*
===============
R_StudioSetRemapColors

===============
*/
static void R_StudioSetRemapColors( int newTop, int newBottom )
{
	gEngfuncs.CL_AllocRemapInfo( RI.currententity, newTop, newBottom );

	if( gEngfuncs.CL_GetRemapInfoForEntity( RI.currententity ))
	{
		gEngfuncs.CL_UpdateRemapInfo( RI.currententity, newTop, newBottom );
		m_fDoRemap = true;
	}
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
	player_info_t	*info = gEngfuncs.pfnPlayerInfo( index );
	player_model_t	*state;

	state = &g_studio.player_models[index];

	// g-cont: force for "dev-mode", non-local games and menu preview
	if(( gpGlobals->developer || !ENGINE_GET_PARM( PARM_LOCAL_GAME ) || !RI.drawWorld ) && info->model[0] )
	{
		if( Q_strcmp( state->name, info->model ))
		{
			Q_strncpy( state->name, info->model, sizeof( state->name ));
			state->name[sizeof( state->name ) - 1] = 0;

			Q_snprintf( state->modelname, sizeof( state->modelname ), "models/player/%s/%s.mdl", info->model, info->model );

			if( gEngfuncs.FS_FileExists( state->modelname, false ))
				state->model = gEngfuncs.Mod_ForName( state->modelname, false, true );
			else state->model = NULL;

			if( !state->model )
				state->model = RI.currententity->model;
		}
	}
	else
	{
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
	int		i, opaque, trans;
	mstudiotexture_t	*ptexture;
	cl_entity_t	*oldent;
	model_t		*model;
	studiohdr_t	*phdr;

	oldent = RI.currententity;
	RI.currententity = ent;

	if( ent->player ) // check it for real playermodel
		model = R_StudioSetupPlayerModel( ent->curstate.number - 1 );
	else model = ent->model;

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
		el->die = gpGlobals->time + 0.05f;
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

	end = R_StudioEstimateFrame( e, pseqdesc );
	start = end - e->curstate.framerate * gpGlobals->frametime * pseqdesc->fps;
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
int R_StudioGetForceFaceFlags( void )
{
	return g_nForceFaceFlags;
}

/*
===============
R_StudioSetForceFaceFlags

===============
*/
void R_StudioSetForceFaceFlags( int flags )
{
	g_nForceFaceFlags = flags;
}

/*
===============
pfnStudioSetHeader

===============
*/
void R_StudioSetHeader( studiohdr_t *pheader )
{
	m_pStudioHeader = pheader;
	m_fDoRemap = false;
}

/*
===============
R_StudioSetRenderModel

===============
*/
void R_StudioSetRenderModel( model_t *model )
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

	//pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	//pglDisable( GL_ALPHA_TEST );
	//pglShadeModel( GL_SMOOTH );

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
	//if( g_studio.rendermode != kRenderNormal )
		//pglDisable( GL_BLEND );

	//pglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	//pglShadeModel( GL_FLAT );
	m_fDoRemap = false;
}

/*
===============
R_StudioSetChromeOrigin

===============
*/
void R_StudioSetChromeOrigin( void )
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

	//if( glState.stencilEnabled )
		//pglEnable( GL_STENCIL_TEST );

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
				TriBegin( TRI_TRIANGLE_FAN );
				i = -i;
			}
			else
			{
				TriBegin( TRI_TRIANGLE_STRIP );
			}


			for( ; i > 0; i--, ptricmds += 4 )
			{
				av = g_studio.verts[ptricmds[0]];
				point[0] = av[0] - (vec_x * ( av[2] - g_studio.lightspot[2] ));
				point[1] = av[1] - (vec_y * ( av[2] - g_studio.lightspot[2] ));
				point[2] = g_studio.lightspot[2] + 1.0f;

				TriVertex3fv( point );
			}

			TriEnd();
		}
	}

	//if( glState.stencilEnabled )
		//pglDisable( GL_STENCIL_TEST );
}

/*
===============
GL_StudioSetRenderMode

set rendermode for studiomodel
===============
*/
void GL_StudioSetRenderMode( int rendermode )
{
	GL_SetRenderMode( rendermode );
#if 0
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
#endif
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
#if 0
	pglDepthMask( GL_TRUE );

	if( r_shadows.value && g_studio.rendermode != kRenderTransAdd && !FBitSet( RI.currentmodel->flags, STUDIO_AMBIENT_LIGHT ))
	{
		float	color = 1.0 - (tr.blend * 0.5);

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
#endif
}

/*
====================
StudioRenderFinal

====================
*/
void R_StudioRenderFinal( void )
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
#if 0
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
#endif
}

/*
====================
StudioRenderModel

====================
*/
void R_StudioRenderModel( void )
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
void R_StudioEstimateGait( entity_state_t *pplayer )
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
void R_StudioProcessGait( entity_state_t *pplayer )
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

	if( m_nPlayerIndex < 0 || m_nPlayerIndex >= ENGINE_GET_PARM( PARM_MAX_CLIENTS ) )
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
			cl_entity_t *ent = gEngfuncs.GetEntityByIndex( RI.currententity->index );
			memcpy( ent->attachment, RI.currententity->attachment, sizeof( vec3_t ) * 4 );
		}
	}

	if( flags & STUDIO_RENDER )
	{
		if( cl_himodels->value && RI.currentmodel != RI.currententity->model  )
		{
			// show highest resolution multiplayer model
			RI.currententity->curstate.body = 255;
		}

		if( !( !gpGlobals->developer && ENGINE_GET_PARM( PARM_MAX_CLIENTS ) == 1 ) && ( RI.currentmodel == RI.currententity->model ))
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
			model_t		*pweaponmodel = gEngfuncs.pfnGetModelByIndex( pplayer->weaponmodel );

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
			RI.currententity->curstate.renderamt > ENGINE_GET_PARM( PARM_MAX_CLIENTS ) )
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
			cl_entity_t *ent = gEngfuncs.GetEntityByIndex( RI.currententity->index );
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
void R_StudioDrawModelInternal( cl_entity_t *e, int flags )
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
	else
	{
		if( e->curstate.movetype == MOVETYPE_FOLLOW && e->curstate.aiment > 0 )
		{
			cl_entity_t *parent = gEngfuncs.GetEntityByIndex( e->curstate.aiment );

			if( parent && parent->model && parent->model->type == mod_studio )
			{
				RI.currententity = parent;
				R_StudioDrawModelInternal( RI.currententity, 0 );
				VectorCopy( parent->curstate.origin, e->curstate.origin );
				VectorCopy( parent->origin, e->origin );
				RI.currententity = e;
			}
		}

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

	RI.currententity = gEngfuncs.GetViewModel();

	if( !RI.currententity->model || RI.currententity->model->type != mod_studio )
		return;

	R_StudioSetupTimings();

	gEngfuncs.GetPredictedOrigin( simorg );
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
	cl_entity_t	*view = gEngfuncs.GetViewModel();
	colorVec		c;

	tr.ignore_lightgamma = true;
	c = R_LightPoint( view->origin );
	tr.ignore_lightgamma = false;
	gEngfuncs.SetLocalLightLevel( ( c.r + c.g + c.b ) / 3 );
}

/*
=================
R_DrawViewModel
=================
*/
void R_DrawViewModel( void )
{
	cl_entity_t	*view = gEngfuncs.GetViewModel();

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
	//pglDepthRange( gldepthmin, gldepthmin + 0.3f * ( gldepthmax - gldepthmin ));
	s_ziscale = (float)0x8000 * (float)0x10000 * 3.0f;
	RI.currentmodel = RI.currententity->model;

	// backface culling for left-handed weapons
	if( R_AllowFlipViewModel( RI.currententity ) || g_iBackFaceCull )
	{
		tr.fFlipViewModel = true;
		//pglFrontFace( GL_CW );
	}

	switch( RI.currententity->model->type )
	{
	case mod_alias:
	//	R_DrawAliasModel( RI.currententity );
		break;
	case mod_studio:
		R_StudioSetupTimings();
		R_StudioDrawModelInternal( RI.currententity, STUDIO_RENDER );
		break;
	}

	// restore depth range
	//pglDepthRange( gldepthmin, gldepthmax );
	s_ziscale = (float)0x8000 * (float)0x10000;

	// backface culling for left-handed weapons
	if( R_AllowFlipViewModel( RI.currententity ) || g_iBackFaceCull )
	{
		tr.fFlipViewModel = false;
		//pglFrontFace( GL_CCW );
	}
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
	COM_FileBase( ptexture->name, name );
	COM_StripExtension( mdlname );

	if( FBitSet( ptexture->flags, STUDIO_NF_NOMIPS ))
		SetBits( flags, TF_NOMIPMAP );

	// NOTE: replace index with pointer to start of imagebuffer, ImageLib expected it
	//ptexture->index = (int)((byte *)phdr) + ptexture->index;
	gEngfuncs.Image_SetMDLPointer((byte *)phdr + ptexture->index);
	size = sizeof( mstudiotexture_t ) + ptexture->width * ptexture->height + 768;

	if( FBitSet( ENGINE_GET_PARM( PARM_FEATURES ), ENGINE_LOAD_DELUXEDATA ) && FBitSet( ptexture->flags, STUDIO_NF_MASKED ))
		flags |= TF_KEEP_SOURCE; // Paranoia2 texture alpha-tracing

	// build the texname
	Q_snprintf( texname, sizeof( texname ), "#%s/%s.mdl", mdlname, name );
	ptexture->index = GL_LoadTexture( texname, (byte *)ptexture, size, flags );

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
void GAME_EXPORT Mod_StudioLoadTextures( model_t *mod, void *data )
{
	studiohdr_t	*phdr = (studiohdr_t *)data;
	mstudiotexture_t	*ptexture;
	int		i;

	if( !phdr )
		return;

	ptexture = (mstudiotexture_t *)(((byte *)phdr) + phdr->textureindex);
	if( phdr->textureindex > 0 && phdr->numtextures <= MAXSTUDIOSKINS )
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
	return gEngfuncs.pfnGetModelByIndex( modelindex );
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
	return (cvar_t*)gEngfuncs.pfnGetCvarPointer( name, 0 );
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
void GAME_EXPORT CL_InitStudioAPI( void )
{
	pStudioDraw = &gStudioDraw;

	// trying to grab them from client.dll
	cl_righthand = gEngfuncs.pfnGetCvarPointer( "cl_righthand", 0 );

	if( cl_righthand == NULL )
		cl_righthand = gEngfuncs.Cvar_Get( "cl_righthand", "0", FCVAR_ARCHIVE, "flip viewmodel (left to right)" );

	// Xash will be used internal StudioModelRenderer
	if( gEngfuncs.pfnGetStudioModelInterface( STUDIO_INTERFACE_VERSION, &pStudioDraw, &gStudioAPI ))
		return;

	// NOTE: we always return true even if game interface was not correct
	// because we need Draw our StudioModels
	// just restore pointer to builtin function
	pStudioDraw = &gStudioDraw;
}
