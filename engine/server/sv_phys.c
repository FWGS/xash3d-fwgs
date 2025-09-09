/*
sv_phys.c - server physic
Copyright (C) 2007 Uncle Mike

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
#include "server.h"
#include "const.h"
#include "library.h"
#include "triangleapi.h"
#include "ref_common.h"

typedef int (*PHYSICAPI)( int, server_physics_api_t*, physics_interface_t* );
#if !XASH_DEDICATED
extern triangleapi_t gTriApi;
#endif

/*
pushmove objects do not obey gravity, and do not interact with each other or trigger fields,
but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_BBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_BBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.
*/
#define MOVE_EPSILON	0.01f
#define MAX_CLIP_PLANES	5

static const vec3_t current_table[] =
{
{ 1,  0, 0 },
{ 0,  1, 0 },
{-1,  0, 0 },
{ 0, -1, 0 },
{ 0,  0, 1 },
{ 0,  0, -1}
};

/*
===============================================================================

Utility functions

===============================================================================
*/
/*
================
SV_CheckAllEnts
================
*/
static void SV_CheckAllEnts( void )
{
	static double	nextcheck;
	edict_t		*e;
	int		i;

	if( !sv_check_errors.value || sv.state != ss_active )
		return;

	if(( nextcheck - Sys_DoubleTime()) > 0.0 )
		return;

	// don't check entities every frame (but every 5 secs)
	nextcheck = Sys_DoubleTime() + 5.0;

	// check edicts errors
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		e = EDICT_NUM( i );

		if( e->free && e->pvPrivateData != NULL )
		{
			Con_Printf( S_ERROR "Freed entity %s (%i) has private data.\n", SV_ClassName( e ), i );
			continue;
		}

		if( !SV_IsValidEdict( e ))
			continue;

		if( !e->v.pContainingEntity || e->v.pContainingEntity != e )
		{
			Con_Printf( S_ERROR "Entity %s (%i) has invalid container, fixed.\n", SV_ClassName( e ), i );
			e->v.pContainingEntity = e;
			continue;
		}

		if( !e->pvPrivateData || !Mem_IsAllocatedExt( svgame.mempool, e->pvPrivateData ))
		{
			Con_Printf( S_ERROR "Entity %s (%i) trashed private data.\n", SV_ClassName( e ), i );
			e->pvPrivateData = NULL;
			continue;
		}

		SV_CheckVelocity( e );
	}
}

/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity( edict_t *ent )
{
	float	wishspd;
	float	maxspd;
	int	i;

	// bound velocity
	for( i = 0; i < 3; i++ )
	{
		if( IS_NAN( ent->v.velocity[i] ))
		{
			if( sv_check_errors.value )
				Con_Printf( "Got a NaN velocity on %s\n", STRING( ent->v.classname ));
			ent->v.velocity[i] = 0.0f;
		}

		if( IS_NAN( ent->v.origin[i] ))
		{
			if( sv_check_errors.value )
				Con_Printf( "Got a NaN origin on %s\n", STRING( ent->v.classname ));
			ent->v.origin[i] = 0.0f;
		}
	}

	wishspd = DotProduct( ent->v.velocity, ent->v.velocity );
	maxspd = sv_maxvelocity.value * sv_maxvelocity.value * 1.73f; // half-diagonal

	if( wishspd > maxspd )
	{
		wishspd = sqrt( wishspd );
		if( sv_check_errors.value )
			Con_Printf( "Got a velocity too high on %s ( %.2f > %.2f )\n", STRING( ent->v.classname ), wishspd, sqrt( maxspd ));
		wishspd = sv_maxvelocity.value / wishspd;
		VectorScale( ent->v.velocity, wishspd, ent->v.velocity );
	}
}

/*
================
SV_UpdateBaseVelocity
================
*/
void SV_UpdateBaseVelocity( edict_t *ent )
{
	if( ent->v.flags & FL_ONGROUND )
	{
		edict_t	*groundentity = ent->v.groundentity;

		if( SV_IsValidEdict( groundentity ))
		{
			// On conveyor belt that's moving?
			if( groundentity->v.flags & FL_CONVEYOR )
			{
				vec3_t	new_basevel;

				VectorScale( groundentity->v.movedir, groundentity->v.speed, new_basevel );
				if( ent->v.flags & FL_BASEVELOCITY )
					VectorAdd( new_basevel, ent->v.basevelocity, new_basevel );

				ent->v.flags |= FL_BASEVELOCITY;
				VectorCopy( new_basevel, ent->v.basevelocity );
			}
		}
	}
}

/*
============
SV_TestEntityPosition

returns true if the entity is in solid currently
============
*/
static qboolean SV_TestEntityPosition( edict_t *ent, edict_t *blocker )
{
	qboolean	monsterClip = FBitSet( ent->v.flags, FL_MONSTERCLIP ) ? true : false;
	trace_t	trace;

	if( FBitSet( ent->v.flags, FL_CLIENT|FL_FAKECLIENT ))
	{
		// to avoid falling through tracktrain update client mins\maxs here
		if( FBitSet( ent->v.flags, FL_DUCKING ))
			SV_SetMinMaxSize( ent, host.player_mins[1], host.player_maxs[1], true );
		else SV_SetMinMaxSize( ent, host.player_mins[0], host.player_maxs[0], true );
	}

	trace = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, MOVE_NORMAL, ent, monsterClip );

	if( SV_IsValidEdict( blocker ) && SV_IsValidEdict( trace.ent ))
	{
		if( trace.ent->v.movetype == MOVETYPE_PUSH || trace.ent == blocker )
			return trace.startsolid;
		return false;
	}

	return trace.startsolid;
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
static qboolean SV_RunThink( edict_t *ent )
{
	float	thinktime;

	if( !FBitSet( ent->v.flags, FL_KILLME ))
	{
		thinktime = ent->v.nextthink;
		if( thinktime <= 0.0f || thinktime > (sv.time + sv.frametime))
			return true;

		if( thinktime < sv.time )
			thinktime = sv.time;	// don't let things stay in the past.
						// it is possible to start that way
						// by a trigger with a local time.
		ent->v.nextthink = 0.0f;
		svgame.globals->time = thinktime;
		svgame.dllFuncs.pfnThink( ent );
	}

	if( FBitSet( ent->v.flags, FL_KILLME ))
		SV_FreeEdict( ent );

	return !ent->free;
}

/*
=============
SV_PlayerRunThink

Runs thinking code if player time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
qboolean SV_PlayerRunThink( edict_t *ent, float frametime, double time )
{
	float	thinktime;

	if( svgame.physFuncs.SV_PlayerThink )
		return svgame.physFuncs.SV_PlayerThink( ent, frametime, time );

	if( !FBitSet( ent->v.flags, FL_KILLME|FL_DORMANT ))
	{
		thinktime = ent->v.nextthink;
		if( thinktime <= 0.0f || thinktime > (time + frametime))
			return true;

		if( thinktime < time )
			thinktime = time;	// don't let things stay in the past.
					// it is possible to start that way
					// by a trigger with a local time.

		ent->v.nextthink = 0.0f;
		svgame.globals->time = thinktime;
		svgame.dllFuncs.pfnThink( ent );
	}

	if( FBitSet( ent->v.flags, FL_KILLME ))
		ClearBits( ent->v.flags, FL_KILLME );

	return !ent->free;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
void SV_Impact( edict_t *e1, edict_t *e2, trace_t *trace )
{
	svgame.globals->time = sv.time;

	if(( e1->v.flags|e2->v.flags ) & FL_KILLME )
		return;

	if( e1->v.groupinfo && e2->v.groupinfo )
	{
		if( svs.groupop == GROUP_OP_AND && !FBitSet( e1->v.groupinfo, e2->v.groupinfo ))
			return;

		if( svs.groupop == GROUP_OP_NAND && FBitSet( e1->v.groupinfo, e2->v.groupinfo ))
			return;
	}

	if( e1->v.solid != SOLID_NOT )
	{
		SV_CopyTraceToGlobal( trace );
		svgame.dllFuncs.pfnTouch( e1, e2 );
	}

	if( e2->v.solid != SOLID_NOT )
	{
		SV_CopyTraceToGlobal( trace );
		svgame.dllFuncs.pfnTouch( e2, e1 );
	}
}

/*
=============
SV_AngularMove

may use friction for smooth stopping
=============
*/
static void SV_AngularMove( edict_t *ent, float frametime, float friction )
{
	float	adjustment;
	int	i;

	VectorMA( ent->v.angles, frametime, ent->v.avelocity, ent->v.angles );
	if( friction == 0.0f ) return;

	adjustment = frametime * (sv_stopspeed.value / 10.0f) * sv_friction.value * fabs( friction );

	for( i = 0; i < 3; i++ )
	{
		if( ent->v.avelocity[i] > 0.0f )
		{
			ent->v.avelocity[i] -= adjustment;
			if( ent->v.avelocity[i] < 0.0f )
				ent->v.avelocity[i] = 0.0f;
		}
		else
		{
			ent->v.avelocity[i] += adjustment;
			if( ent->v.avelocity[i] > 0.0f )
				ent->v.avelocity[i] = 0.0f;
		}
	}
}

/*
=============
SV_LinearMove

use friction for smooth stopping
=============
*/
static void SV_LinearMove( edict_t *ent, float frametime, float friction )
{
	int	i;
	float	adjustment;

	VectorMA( ent->v.origin, frametime, ent->v.velocity, ent->v.origin );
	if( friction == 0.0f ) return;

	adjustment = frametime * (sv_stopspeed.value / 10.0f) * sv_friction.value * fabs( friction );

	for( i = 0; i < 3; i++ )
	{
		if( ent->v.velocity[i] > 0.0f )
		{
			ent->v.velocity[i] -= adjustment;
			if( ent->v.velocity[i] < 0.0f )
				ent->v.velocity[i] = 0.0f;
		}
		else
		{
			ent->v.velocity[i] += adjustment;
			if( ent->v.velocity[i] > 0.0f )
				ent->v.velocity[i] = 0.0f;
		}
	}
}

/*
=============
SV_RecursiveWaterLevel

recursively recalculating the middle
=============
*/
static float SV_RecursiveWaterLevel( vec3_t origin, float out, float in, int count )
{
	vec3_t	point;
	float	offset;

	offset = ((out - in) * 0.5f) + in;
	if( ++count > 5 ) return offset;

	VectorSet( point, origin[0], origin[1], origin[2] + offset );

	if( SV_PointContents( point ) == CONTENTS_WATER )
		return SV_RecursiveWaterLevel( origin, out, offset, count );
	return SV_RecursiveWaterLevel( origin, offset, in, count );
}

/*
=============
SV_Submerged

determine how deep the entity is
=============
*/
static float SV_Submerged( edict_t *ent )
{
	float	start, bottom;
	vec3_t	point;
	vec3_t	center;

	VectorAverage( ent->v.absmin, ent->v.absmax, center );
	start = ent->v.absmin[2] - center[2];

	switch( ent->v.waterlevel )
	{
	case 1:
		bottom = SV_RecursiveWaterLevel( center, 0.0f, start, 0 );
		return bottom - start;
	case 3:
		VectorSet( point, center[0], center[1], ent->v.absmax[2] );
		svs.groupmask = ent->v.groupinfo;
		if( SV_PointContents( point ) == CONTENTS_WATER )
			return (ent->v.maxs[2] - ent->v.mins[2]);
		// intentionally fallthrough
	case 2:
		bottom = SV_RecursiveWaterLevel( center, ent->v.absmax[2] - center[2], 0.0f, 0 );
		return bottom - start;
	}

	return 0.0f;
}

/*
=============
SV_CheckWater
=============
*/
static qboolean SV_CheckWater( edict_t *ent )
{
	int	cont, truecont;
	vec3_t	point;

	point[0] = (ent->v.absmax[0] + ent->v.absmin[0]) * 0.5f;
	point[1] = (ent->v.absmax[1] + ent->v.absmin[1]) * 0.5f;
	point[2] = (ent->v.absmin[2] + 1.0f);

	ent->v.watertype = CONTENTS_EMPTY;
	svs.groupmask = ent->v.groupinfo;
	ent->v.waterlevel = 0;

	cont = SV_PointContents( point );

	if( cont <= CONTENTS_WATER && cont > CONTENTS_TRANSLUCENT )
	{
		svs.groupmask = ent->v.groupinfo;
		truecont = SV_TruePointContents( point );

		ent->v.watertype = cont;
		ent->v.waterlevel = 1;

		if( ent->v.absmin[2] != ent->v.absmax[2] )
		{
			point[2] = (ent->v.absmin[2] + ent->v.absmax[2]) * 0.5f;

			svs.groupmask = ent->v.groupinfo;
			cont = SV_PointContents( point );

			if( cont <= CONTENTS_WATER && cont > CONTENTS_TRANSLUCENT )
			{
				ent->v.waterlevel = 2;

				VectorAdd( point, ent->v.view_ofs, point );
				svs.groupmask = ent->v.groupinfo;
				cont = SV_PointContents( point );

				if( cont <= CONTENTS_WATER && cont > CONTENTS_TRANSLUCENT )
					ent->v.waterlevel = 3;
			}
		}
		else
		{
			// a point entity
			ent->v.waterlevel = 3;
		}

		// Quake2 feature. Probably never was used in Half-Life...
		if( truecont <= CONTENTS_CURRENT_0 && truecont >= CONTENTS_CURRENT_DOWN )
		{
			float speed = 150.0f * ent->v.waterlevel / 3.0f;
			const float *dir = current_table[CONTENTS_CURRENT_0 - truecont];

			VectorMA( ent->v.basevelocity, speed, dir, ent->v.basevelocity );
		}
	}

	return (ent->v.waterlevel > 1);
}

/*
=============
SV_CheckMover

test thing (applies the friction to pushables while standing on moving platform)
=============
*/
static qboolean SV_CheckMover( edict_t *ent )
{
	edict_t	*gnd = ent->v.groundentity;

	if( !SV_IsValidEdict( gnd ))
		return false;

	if( gnd->v.movetype != MOVETYPE_PUSH )
		return false;

	if( VectorIsNull( gnd->v.velocity ) && VectorIsNull( gnd->v.avelocity ))
		return false;

	return true;
}

/*
==================
SV_ClipVelocity

Slide off of the impacting object
==================
*/
static int SV_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce )
{
	float	backoff;
	float	change;
	int	i, blocked;

	blocked = 0;
	if( normal[2] > 0.0f ) blocked |= 1;	// floor
	if( !normal[2] ) blocked |= 2;	// step

	backoff = DotProduct( in, normal ) * overbounce;

	for( i = 0; i < 3; i++ )
	{
		change = normal[i] * backoff;
		out[i] = in[i] - change;

		if( out[i] > -1.0f && out[i] < 1.0f )
			out[i] = 0.0f;
	}

	return blocked;
}

/*
===============================================================================

	FLYING MOVEMENT CODE

===============================================================================
*/
/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
*steptrace - if not NULL, the trace results of any vertical wall hit will be stored
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
============
*/
static int SV_FlyMove( edict_t *ent, float time, trace_t *steptrace )
{
	int	i, j, numplanes, bumpcount, blocked;
	vec3_t	dir, end, planes[MAX_CLIP_PLANES];
	vec3_t	primal_velocity, original_velocity, new_velocity;
	float	d, time_left, allFraction;
	qboolean	monsterClip;
	trace_t	trace;

	blocked = 0;
	monsterClip = FBitSet( ent->v.flags, FL_MONSTERCLIP ) ? true : false;
	VectorCopy( ent->v.velocity, original_velocity );
	VectorCopy( ent->v.velocity, primal_velocity );
	VectorClear( new_velocity );
	numplanes = 0;

	allFraction = 0.0f;
	time_left = time;

	for( bumpcount = 0; bumpcount < MAX_CLIP_PLANES - 1; bumpcount++ )
	{
		if( VectorIsNull( ent->v.velocity ))
			break;

		VectorMA( ent->v.origin, time_left, ent->v.velocity, end );
		trace = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent, monsterClip );

		allFraction += trace.fraction;

		if( trace.allsolid )
		{
			// entity is trapped in another solid
			VectorClear( ent->v.velocity );
			return 4;
		}

		if( trace.fraction > 0.0f )
		{
			// actually covered some distance
			VectorCopy( trace.endpos, ent->v.origin );
			VectorCopy( ent->v.velocity, original_velocity );
			numplanes = 0;
		}

		if( trace.fraction == 1.0f )
			 break; // moved the entire distance

		if( !SV_IsValidEdict( trace.ent ))
			break; // g-cont. this should never happens

		if( trace.plane.normal[2] > 0.7f )
		{
			blocked |= 1; // floor

			if( trace.ent->v.solid == SOLID_BSP || trace.ent->v.solid == SOLID_SLIDEBOX ||
				trace.ent->v.movetype == MOVETYPE_PUSHSTEP || (trace.ent->v.flags & FL_CLIENT))
			{
				SetBits( ent->v.flags, FL_ONGROUND );
				ent->v.groundentity = trace.ent;
			}
		}

		if( trace.plane.normal[2] == 0.0f )
		{
			blocked |= 2; // step
			if( steptrace ) *steptrace = trace; // save for player extrafriction
		}

		// run the impact function
		SV_Impact( ent, trace.ent, &trace );

		// break if removed by the impact function
		if( ent->free ) break;

		time_left -= time_left * trace.fraction;

		// clipped to another plane
		if( numplanes >= MAX_CLIP_PLANES )
		{
			// this shouldn't really happen
			VectorClear( ent->v.velocity );
			break;
		}

		VectorCopy( trace.plane.normal, planes[numplanes] );
		numplanes++;

		// modify original_velocity so it parallels all of the clip planes
		for( i = 0; i < numplanes; i++ )
		{
			SV_ClipVelocity( original_velocity, planes[i], new_velocity, 1.0f );

			for( j = 0; j < numplanes; j++ )
			{
				if( j != i )
				{
					if( DotProduct( new_velocity, planes[j] ) < 0.0f )
						break; // not ok
				}
			}

			if( j == numplanes )
				break;
		}

		if( i != numplanes )
		{
			// go along this plane
			VectorCopy( new_velocity, ent->v.velocity );
		}
		else
		{
			// go along the crease
			if( numplanes != 2 )
			{
				VectorClear( ent->v.velocity );
				break;
			}

			CrossProduct( planes[0], planes[1], dir );
			d = DotProduct( dir, ent->v.velocity );
			VectorScale( dir, d, ent->v.velocity );
		}

		// if current velocity is against the original velocity,
		// stop dead to avoid tiny occilations in sloping corners
		if( DotProduct( ent->v.velocity, primal_velocity ) <= 0.0f )
		{
			VectorClear( ent->v.velocity );
			break;
		}
	}

	if( allFraction == 0.0f )
		VectorClear( ent->v.velocity );

	return blocked;
}

/*
============
SV_AddGravity

============
*/
static void SV_AddGravity( edict_t *ent )
{
	float	ent_gravity;

	if( ent->v.gravity )
		ent_gravity = ent->v.gravity;
	else ent_gravity = 1.0f;

	// add gravity incorrectly
	ent->v.velocity[2] -= ( ent_gravity * sv_gravity.value * sv.frametime );
	ent->v.velocity[2] += ( ent->v.basevelocity[2] * sv.frametime );
	ent->v.basevelocity[2] = 0.0f;

	// bound velocity
	SV_CheckVelocity( ent );
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/
/*
============
SV_AllowPushRotate

Allows to change entity yaw?
============
*/
static qboolean SV_AllowPushRotate( edict_t *ent )
{
	model_t	*mod;

	mod = SV_ModelHandle( ent->v.modelindex );

	if( !mod || mod->type != mod_brush )
		return true;

	if( !FBitSet( host.features, ENGINE_PHYSICS_PUSHER_EXT ))
		return false;

	if( FBitSet( mod->flags, MODEL_HAS_ORIGIN ))
		return true;

	return false;
}

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
static trace_t SV_PushEntity( edict_t *ent, const vec3_t lpush, const vec3_t apush, int *blocked, float flDamage )
{
	trace_t	trace;
	qboolean	monsterBlock;
	qboolean	monsterClip;
	int	type;
	vec3_t	end;

	monsterClip = FBitSet( ent->v.flags, FL_MONSTERCLIP ) ? true : false;
	VectorAdd( ent->v.origin, lpush, end );

	if( ent->v.movetype == MOVETYPE_FLYMISSILE )
		type = MOVE_MISSILE;
	else if( ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT )
		type = MOVE_NOMONSTERS; // only clip against bmodels
	else type = MOVE_NORMAL;

	trace = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, end, type, ent, monsterClip );

	if( trace.fraction != 0.0f )
	{
		VectorCopy( trace.endpos, ent->v.origin );

		if( sv.state == ss_active && apush[YAW] && ( ent->v.flags & FL_CLIENT ))
		{
			ent->v.avelocity[1] += apush[1];
			ent->v.fixangle = 2;
		}

		// don't rotate pushables!
		if( SV_AllowPushRotate( ent ))
			ent->v.angles[YAW] += trace.fraction * apush[YAW];
	}

	SV_LinkEdict( ent, true );

	if( ent->v.movetype == MOVETYPE_WALK || ent->v.movetype == MOVETYPE_STEP || ent->v.movetype == MOVETYPE_PUSHSTEP )
		monsterBlock = true;
	else monsterBlock = false;

	if( blocked )
	{
		// more accuracy blocking code
		if( monsterBlock )
			*blocked = !VectorCompareEpsilon( ent->v.origin, end, ON_EPSILON ); // can't move full distance
		else *blocked = true;
	}

	// so we can run impact function afterwards.
	if( SV_IsValidEdict( trace.ent ))
		SV_Impact( ent, trace.ent, &trace );

	return trace;
}

/*
============
SV_CanPushed

filter entities for push
============
*/
static qboolean SV_CanPushed( edict_t *ent )
{
	// filter movetypes to collide with
	switch( ent->v.movetype )
	{
	case MOVETYPE_NONE:
	case MOVETYPE_PUSH:
	case MOVETYPE_FOLLOW:
	case MOVETYPE_NOCLIP:
	case MOVETYPE_COMPOUND:
		return false;
	}
	return true;
}

/*
============
SV_CanBlock

allow entity to block pusher?
============
*/
static qboolean SV_CanBlock( edict_t *ent )
{
	if( ent->v.mins[0] == ent->v.maxs[0] )
		return false;

	if( ent->v.solid == SOLID_NOT || ent->v.solid == SOLID_TRIGGER )
	{
		// clear bounds for deadbody
		ent->v.mins[0] = ent->v.mins[1] = 0;
		VectorCopy( ent->v.mins, ent->v.maxs );
		return false;
	}

	return true;
}

/*
============
SV_PushMove

============
*/
static edict_t *SV_PushMove( edict_t *pusher, float movetime )
{
	int		i, e, block;
	int		num_moved, oldsolid;
	vec3_t		mins, maxs, lmove;
	sv_pushed_t	*p, *pushed_p;
	edict_t		*check;

	if( svgame.globals->changelevel || VectorIsNull( pusher->v.velocity ))
	{
		pusher->v.ltime += movetime;
		return NULL;
	}

	for( i = 0; i < 3; i++ )
	{
		lmove[i] = pusher->v.velocity[i] * movetime;
		mins[i] = pusher->v.absmin[i] + lmove[i];
		maxs[i] = pusher->v.absmax[i] + lmove[i];
	}

	pushed_p = svgame.pushed;

	// save the pusher's original position
	pushed_p->ent = pusher;
	VectorCopy( pusher->v.origin, pushed_p->origin );
	VectorCopy( pusher->v.angles, pushed_p->angles );
	pushed_p++;

	// move the pusher to it's final position
	SV_LinearMove( pusher, movetime, 0.0f );
	SV_LinkEdict( pusher, false );
	pusher->v.ltime += movetime;
	oldsolid = pusher->v.solid;

	// non-solid pushers can't push anything
	if( pusher->v.solid == SOLID_NOT )
		return NULL;

	// see if any solid entities are inside the final position
	num_moved = 0;

	for( e = 1; e < svgame.numEntities; e++ )
	{
		check = EDICT_NUM( e );
		if( !SV_IsValidEdict( check )) continue;

		// filter movetypes to collide with
		if( !SV_CanPushed( check ))
			continue;

		pusher->v.solid = SOLID_NOT;
		block = SV_TestEntityPosition( check, pusher );
		pusher->v.solid = oldsolid;
		if( block ) continue;

		// if the entity is standing on the pusher, it will definately be moved
		if( !( FBitSet( check->v.flags, FL_ONGROUND ) && check->v.groundentity == pusher ))
		{
			if( check->v.absmin[0] >= maxs[0]
			 || check->v.absmin[1] >= maxs[1]
			 || check->v.absmin[2] >= maxs[2]
			 || check->v.absmax[0] <= mins[0]
			 || check->v.absmax[1] <= mins[1]
			 || check->v.absmax[2] <= mins[2] )
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if( !SV_TestEntityPosition( check, NULL ))
				continue;
		}

		// remove the onground flag for non-players
		if( check->v.movetype != MOVETYPE_WALK )
			check->v.flags &= ~FL_ONGROUND;

		// save original position of contacted entity
		pushed_p->ent = check;
		VectorCopy( check->v.origin, pushed_p->origin );
		VectorCopy( check->v.angles, pushed_p->angles );
		pushed_p++;

		// try moving the contacted entity
		pusher->v.solid = SOLID_NOT;
		SV_PushEntity( check, lmove, vec3_origin, &block, pusher->v.dmg );
		pusher->v.solid = oldsolid;

		// if it is still inside the pusher, block
		if( SV_TestEntityPosition( check, NULL ) && block )
		{
			if( !SV_CanBlock( check ))
				continue;

			pusher->v.ltime -= movetime;

			// move back any entities we already moved
			// go backwards, so if the same entity was pushed
			// twice, it goes back to the original position
			for( p = pushed_p - 1; p >= svgame.pushed; p-- )
			{
				VectorCopy( p->origin, p->ent->v.origin );
				VectorCopy( p->angles, p->ent->v.angles );
				SV_LinkEdict( p->ent, (p->ent == check) ? true : false );
			}
			return check;
		}
	}

	return NULL;
}

/*
============
SV_PushRotate

============
*/
static edict_t *SV_PushRotate( edict_t *pusher, float movetime )
{
	int		i, e, block, oldsolid;
	matrix4x4		start_l, end_l;
	vec3_t		lmove, amove;
	sv_pushed_t	*p, *pushed_p;
	vec3_t		org, org2, temp;
	edict_t		*check;

	if( svgame.globals->changelevel || VectorIsNull( pusher->v.avelocity ))
	{
		pusher->v.ltime += movetime;
		return NULL;
	}

	for( i = 0; i < 3; i++ )
		amove[i] = pusher->v.avelocity[i] * movetime;

	// create pusher initial position
	Matrix4x4_CreateFromEntity( start_l, pusher->v.angles, pusher->v.origin, 1.0f );

	pushed_p = svgame.pushed;

	// save the pusher's original position
	pushed_p->ent = pusher;
	VectorCopy( pusher->v.origin, pushed_p->origin );
	VectorCopy( pusher->v.angles, pushed_p->angles );
	pushed_p++;

	// move the pusher to it's final position
	SV_AngularMove( pusher, movetime, pusher->v.friction );
	SV_LinkEdict( pusher, false );
	pusher->v.ltime += movetime;
	oldsolid = pusher->v.solid;

	// non-solid pushers can't push anything
	if( pusher->v.solid == SOLID_NOT )
		return NULL;

	// create pusher final position
	Matrix4x4_CreateFromEntity( end_l, pusher->v.angles, pusher->v.origin, 1.0f );

	// see if any solid entities are inside the final position
	for( e = 1; e < svgame.numEntities; e++ )
	{
		check = EDICT_NUM( e );
		if( !SV_IsValidEdict( check ))
			continue;

		// filter movetypes to collide with
		if( !SV_CanPushed( check ))
			continue;

		pusher->v.solid = SOLID_NOT;
		block = SV_TestEntityPosition( check, pusher );
		pusher->v.solid = oldsolid;
		if( block ) continue;

		// if the entity is standing on the pusher, it will definately be moved
		if( !(( check->v.flags & FL_ONGROUND ) && check->v.groundentity == pusher ))
		{
			if( check->v.absmin[0] >= pusher->v.absmax[0]
			|| check->v.absmin[1] >= pusher->v.absmax[1]
			|| check->v.absmin[2] >= pusher->v.absmax[2]
			|| check->v.absmax[0] <= pusher->v.absmin[0]
			|| check->v.absmax[1] <= pusher->v.absmin[1]
			|| check->v.absmax[2] <= pusher->v.absmin[2] )
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if( !SV_TestEntityPosition( check, NULL ))
				continue;
		}

		// save original position of contacted entity
		pushed_p->ent = check;
		VectorCopy( check->v.origin, pushed_p->origin );
		VectorCopy( check->v.angles, pushed_p->angles );
		pushed_p->fixangle = check->v.fixangle;
		pushed_p++;

		// calculate destination position
		if( check->v.movetype == MOVETYPE_PUSHSTEP || check->v.movetype == MOVETYPE_STEP )
			VectorAverage( check->v.absmin, check->v.absmax, org );
		else VectorCopy( check->v.origin, org );

		Matrix4x4_VectorITransform( start_l, org, temp );
		Matrix4x4_VectorTransform( end_l, temp, org2 );
		VectorSubtract( org2, org, lmove );

		// i can't clear FL_ONGROUND in all cases because many bad things may be happen
		if( check->v.movetype != MOVETYPE_WALK )
		{
			if( lmove[2] != 0.0f ) check->v.flags &= ~FL_ONGROUND;
			if( lmove[2] < 0.0f && !pusher->v.dmg )
				lmove[2] = 0.0f; // let's the free falling
		}

		// try moving the contacted entity
		pusher->v.solid = SOLID_NOT;
		SV_PushEntity( check, lmove, amove, &block, pusher->v.dmg );
		pusher->v.solid = oldsolid;

		// pushed entity blocked by wall
		if( block && check->v.movetype != MOVETYPE_WALK )
			check->v.flags &= ~FL_ONGROUND;

		// if it is still inside the pusher, block
		if( SV_TestEntityPosition( check, NULL ) && block )
		{
			if( !SV_CanBlock( check ))
				continue;

			pusher->v.ltime -= movetime;

			// move back any entities we already moved
			// go backwards, so if the same entity was pushed
			// twice, it goes back to the original position
			for( p = pushed_p - 1; p >= svgame.pushed; p-- )
			{
				VectorCopy( p->origin, p->ent->v.origin );
				VectorCopy( p->angles, p->ent->v.angles );
				SV_LinkEdict( p->ent, (p->ent == check) ? true : false );
				p->ent->v.fixangle = p->fixangle;
			}
			return check;
		}
	}

	return NULL;
}

/*
================
SV_Physics_Pusher

================
*/
static void SV_Physics_Pusher( edict_t *ent )
{
	float	oldtime, oldtime2;
	float	thinktime, movetime;
	edict_t	*pBlocker;
	int	i;

	pBlocker = NULL;
	oldtime = ent->v.ltime;
	thinktime = ent->v.nextthink;

	if( thinktime < oldtime + sv.frametime )
	{
		movetime = thinktime - oldtime;
		if( movetime < 0.0f ) movetime = 0.0f;
	}
	else movetime = sv.frametime;

	if( movetime )
	{
		if( !VectorIsNull( ent->v.avelocity ))
		{
			if( !VectorIsNull( ent->v.velocity ))
			{
				pBlocker = SV_PushRotate( ent, movetime );

				if( !pBlocker )
				{
					oldtime2 = ent->v.ltime;

					// reset the local time to what it was before we rotated
					ent->v.ltime = oldtime;
					pBlocker = SV_PushMove( ent, movetime );
					if( ent->v.ltime < oldtime2 )
						ent->v.ltime = oldtime2;
				}
			}
			else
			{
				pBlocker = SV_PushRotate( ent, movetime );
			}
		}
		else
		{
			pBlocker = SV_PushMove( ent, movetime );
		}
	}

	// if the pusher has a "blocked" function, call it
	// otherwise, just stay in place until the obstacle is gone
	if( pBlocker ) svgame.dllFuncs.pfnBlocked( ent, pBlocker );

	for( i = 0; i < 3; i++ )
	{
		if( ent->v.angles[i] < -3600.0f || ent->v.angles[i] > 3600.0f )
			ent->v.angles[i] = fmod( ent->v.angles[i], 3600.0f );
	}

	if( thinktime > oldtime && (( ent->v.flags & FL_ALWAYSTHINK ) || thinktime <= ent->v.ltime ))
	{
		ent->v.nextthink = 0.0f;
		svgame.globals->time = sv.time;
		svgame.dllFuncs.pfnThink( ent );
	}
}

//============================================================================
/*
=============
SV_Physics_Follow

just copy angles and origin of parent
=============
*/
static void SV_Physics_Follow( edict_t *ent )
{
	edict_t	*parent;

	// regular thinking
	if( !SV_RunThink( ent )) return;

	parent = ent->v.aiment;

	if( !SV_IsValidEdict( parent ))
	{
		ent->v.movetype = MOVETYPE_NONE;
		return;
	}

	VectorAdd( parent->v.origin, ent->v.v_angle, ent->v.origin );
	VectorCopy( parent->v.angles, ent->v.angles );

	SV_LinkEdict( ent, true );
}

/*
=============
SV_Physics_Compound

a glue two entities together
=============
*/
static void SV_Physics_Compound( edict_t *ent )
{
	edict_t	*parent;

	// regular thinking
	if( !SV_RunThink( ent )) return;

	parent = ent->v.aiment;

	if( !SV_IsValidEdict( parent ))
	{
		ent->v.movetype = MOVETYPE_NONE;
		return;
	}

	if( ent->v.solid != SOLID_TRIGGER )
		ent->v.solid = SOLID_NOT;

	switch( parent->v.movetype )
	{
	case MOVETYPE_PUSH:
	case MOVETYPE_PUSHSTEP:
		break;
	default: return;
	}

	// not initialized ?
	if( ent->v.ltime == 0.0f )
	{
		VectorCopy( parent->v.origin, ent->v.oldorigin );
		VectorCopy( parent->v.angles, ent->v.avelocity );
		ent->v.ltime = sv.frametime;
		return;
	}

	if( !VectorCompare( parent->v.origin, ent->v.oldorigin ) || !VectorCompare( parent->v.angles, ent->v.avelocity ))
	{
		matrix4x4	start_l, end_l, temp_l, child;

		// create parent old position
		Matrix4x4_CreateFromEntity( temp_l, ent->v.avelocity, ent->v.oldorigin, 1.0f );
		Matrix4x4_Invert_Simple( start_l, temp_l );

		// create parent actual position
		Matrix4x4_CreateFromEntity( end_l, parent->v.angles, parent->v.origin, 1.0f );

		// stupid quake bug!!!
		if( !( host.features & ENGINE_COMPENSATE_QUAKE_BUG ))
			ent->v.angles[PITCH] = -ent->v.angles[PITCH];

		// create child actual position
		Matrix4x4_CreateFromEntity( child, ent->v.angles, ent->v.origin, 1.0f );

		// transform child from start to end
		Matrix4x4_ConcatTransforms( temp_l, start_l, child );
		Matrix4x4_ConcatTransforms( child, end_l, temp_l );

		// create child final position
		Matrix4x4_ConvertToEntity( child, ent->v.angles, ent->v.origin );

		// stupid quake bug!!!
		if( !( host.features & ENGINE_COMPENSATE_QUAKE_BUG ))
			ent->v.angles[PITCH] = -ent->v.angles[PITCH];
	}

	// notsolid ents never touch triggers
	SV_LinkEdict( ent, (ent->v.solid == SOLID_NOT) ? false : true );

	// shuffle states
	VectorCopy( parent->v.origin, ent->v.oldorigin );
	VectorCopy( parent->v.angles, ent->v.avelocity );
}

/*
=============
SV_PhysicsNoclip

A moving object that doesn't obey physics
=============
*/
static void SV_Physics_Noclip( edict_t *ent )
{
	// regular thinking
	if( !SV_RunThink( ent )) return;

	SV_CheckWater( ent );

	VectorMA( ent->v.origin, sv.frametime, ent->v.velocity,  ent->v.origin );
	VectorMA( ent->v.angles, sv.frametime, ent->v.avelocity, ent->v.angles );

	// noclip ents never touch triggers
	SV_LinkEdict( ent, false );
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/
/*
=============
SV_CheckWaterTransition

=============
*/
static void SV_CheckWaterTransition( edict_t *ent )
{
	vec3_t	point;
	int	cont;

	point[0] = (ent->v.absmax[0] + ent->v.absmin[0]) * 0.5f;
	point[1] = (ent->v.absmax[1] + ent->v.absmin[1]) * 0.5f;
	point[2] = (ent->v.absmin[2] + 1.0f);

	svs.groupmask = ent->v.groupinfo;
	cont = SV_PointContents( point );

	if( !ent->v.watertype )
	{
		// just spawned here
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		return;
	}

	if( cont <= CONTENTS_WATER && cont > CONTENTS_TRANSLUCENT )
	{
		if( ent->v.watertype == CONTENTS_EMPTY )
		{
			// just crossed into water
			const char *snd = SoundList_GetRandom( PlayerWaterEnter );
			if( snd )
				SV_StartSound( ent, CHAN_AUTO, snd, 1.0f, ATTN_NORM, 0, 100 );
			ent->v.velocity[2] *= 0.5f;
		}

		ent->v.watertype = cont;
		ent->v.waterlevel = 1;

		if( ent->v.absmin[2] != ent->v.absmax[2] )
		{
			point[2] = (ent->v.absmin[2] + ent->v.absmax[2]) * 0.5f;
			svs.groupmask = ent->v.groupinfo;
			cont = SV_PointContents( point );

			if( cont <= CONTENTS_WATER && cont > CONTENTS_TRANSLUCENT )
			{
				ent->v.waterlevel = 2;
				VectorAdd( point, ent->v.view_ofs, point );
				svs.groupmask = ent->v.groupinfo;
				cont = SV_PointContents( point );
				if( cont <= CONTENTS_WATER && cont > CONTENTS_TRANSLUCENT )
					ent->v.waterlevel = 3;
			}
		}
		else
		{
			// point entity
			ent->v.waterlevel = 3;
		}
	}
	else
	{
		if( ent->v.watertype != CONTENTS_EMPTY )
		{
			// just crossed into water
			const char *snd = SoundList_GetRandom( PlayerWaterExit );
			if( snd )
				SV_StartSound( ent, CHAN_AUTO, snd, 1.0f, ATTN_NORM, 0, 100 );
		}
		ent->v.watertype = CONTENTS_EMPTY;
		ent->v.waterlevel = 0;
	}
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
static void SV_Physics_Toss( edict_t *ent )
{
	trace_t	trace;
	vec3_t	move;
	float	backoff;
	edict_t	*ground;

	SV_CheckWater( ent );

	// regular thinking
	if( !SV_RunThink( ent )) return;

	ground = ent->v.groundentity;

	if( ent->v.velocity[2] > 0 )
		ClearBits( ent->v.flags, FL_ONGROUND );

	if( !SV_IsValidEdict( ground ) || FBitSet( ground->v.flags, FL_MONSTER|FL_CLIENT ))
		ClearBits( ent->v.flags, FL_ONGROUND );

	// if on ground and not moving, return.
	if( FBitSet( ent->v.flags, FL_ONGROUND ) && VectorIsNull( ent->v.velocity ))
	{
		VectorClear( ent->v.avelocity );

		if( VectorIsNull( ent->v.basevelocity ))
			return;	// at rest
	}

	SV_CheckVelocity( ent );

	// add gravity
	switch( ent->v.movetype )
	{
	case MOVETYPE_FLY:
	case MOVETYPE_FLYMISSILE:
	case MOVETYPE_BOUNCEMISSILE:
		break;
	default:
		SV_AddGravity( ent );
		break;
	}

	// move angles (with friction)
	switch( ent->v.movetype )
	{
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
		SV_AngularMove( ent, sv.frametime, ent->v.friction );
		break;
	default:
		SV_AngularMove( ent, sv.frametime, 0.0f );
		break;
	}

	// move origin
	// Base velocity is not properly accounted for since this entity will move again
	// after the bounce without taking it into account
	VectorAdd( ent->v.velocity, ent->v.basevelocity, ent->v.velocity );

	SV_CheckVelocity( ent );
	VectorScale( ent->v.velocity, sv.frametime, move );

	VectorSubtract( ent->v.velocity, ent->v.basevelocity, ent->v.velocity );

	trace = SV_PushEntity( ent, move, vec3_origin, NULL, 0.0f );
	if( ent->free ) return;

	SV_CheckVelocity( ent );

	if( trace.allsolid )
	{
		// entity is trapped in another solid
		VectorClear( ent->v.avelocity );
		VectorClear( ent->v.velocity );
		return;
	}

	if( trace.fraction == 1.0f )
	{
		SV_CheckWaterTransition( ent );
		return;
	}

	if( ent->v.movetype == MOVETYPE_BOUNCE )
		backoff = 2.0f - ent->v.friction;
	else if( ent->v.movetype == MOVETYPE_BOUNCEMISSILE )
		backoff = 2.0f;
	else backoff = 1.0f;

	SV_ClipVelocity( ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff );

	// stop if on ground
	if( trace.plane.normal[2] > 0.7f )
	{
		float	vel;

		VectorAdd( ent->v.velocity, ent->v.basevelocity, move );
		vel = DotProduct( move, move );

		if( ent->v.velocity[2] < sv_gravity.value * sv.frametime )
		{
			// we're rolling on the ground, add static friction.
			ent->v.groundentity = trace.ent;
			ent->v.flags |= FL_ONGROUND;
			ent->v.velocity[2] = 0.0f;
		}

		if( vel < 900.0f || ( ent->v.movetype != MOVETYPE_BOUNCE && ent->v.movetype != MOVETYPE_BOUNCEMISSILE ))
		{
			ent->v.flags |= FL_ONGROUND;
			ent->v.groundentity = trace.ent;
			VectorClear( ent->v.avelocity );
			VectorClear( ent->v.velocity );
		}
		else
		{
			VectorScale( ent->v.velocity, (1.0f - trace.fraction) * sv.frametime * 0.9f, move );
			VectorMA( move, (1.0f - trace.fraction) * sv.frametime * 0.9f, ent->v.basevelocity, move );
			trace = SV_PushEntity( ent, move, vec3_origin, NULL, 0.0f );
			if( ent->free ) return;
		}
	}

	// check for in water
	SV_CheckWaterTransition( ent );
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/
/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
static void SV_Physics_Step( edict_t *ent )
{
	qboolean	inwater;
	qboolean	wasonground;
	qboolean	wasonmover;
	vec3_t	mins, maxs;
	vec3_t	point;
	trace_t	trace;
	int	x, y;

	SV_WaterMove( ent );
	SV_CheckVelocity( ent );

	wasonground = (ent->v.flags & FL_ONGROUND);
	wasonmover = SV_CheckMover( ent );
	inwater = SV_CheckWater( ent );

	if( FBitSet( ent->v.flags, FL_FLOAT ) && ent->v.waterlevel > 0 )
	{
		float buoyancy = SV_Submerged( ent ) * ent->v.skin * sv.frametime;

		SV_AddGravity( ent );
		ent->v.velocity[2] += buoyancy;
	}

	if( !wasonground )
	{
		if( !FBitSet( ent->v.flags, FL_FLY ))
		{
			if( !FBitSet( ent->v.flags, FL_SWIM ) || ( ent->v.waterlevel <= 0 ))
			{
				if( !inwater )
					SV_AddGravity( ent );
			}
		}
	}

	if( !VectorIsNull( ent->v.velocity ) || !VectorIsNull( ent->v.basevelocity ))
	{
		ent->v.flags &= ~FL_ONGROUND;

		if(( wasonground || wasonmover ) && ( ent->v.health > 0 || SV_CheckBottom( ent, MOVE_NORMAL )))
		{
			float	*vel = ent->v.velocity;
			float	control, speed, newspeed;
			float	friction;

			speed = sqrt(( vel[0] * vel[0] ) + ( vel[1] * vel[1] ));	// DotProduct2D

			if( speed )
			{
				friction = sv_friction.value * ent->v.friction;	// factor
				ent->v.friction = 1.0f; // g-cont. ???
				if( wasonmover ) friction *= 0.5f; // add a little friction

				control = (speed < sv_stopspeed.value) ? sv_stopspeed.value : speed;
				newspeed = speed - (sv.frametime * control * friction);
				if( newspeed < 0 ) newspeed = 0;
				newspeed /= speed;

				vel[0] = vel[0] * newspeed;
				vel[1] = vel[1] * newspeed;
			}
		}

		VectorAdd( ent->v.velocity, ent->v.basevelocity, ent->v.velocity );
		SV_CheckVelocity( ent );

		SV_FlyMove( ent, sv.frametime, NULL );
		if( ent->free ) return;

		SV_CheckVelocity( ent );
		VectorSubtract( ent->v.velocity, ent->v.basevelocity, ent->v.velocity );
		SV_CheckVelocity( ent );

		VectorAdd( ent->v.origin, ent->v.mins, mins );
		VectorAdd( ent->v.origin, ent->v.maxs, maxs );

		point[2] = mins[2] - 1.0f;

		for( x = 0; x <= 1; x++ )
		{
			if( FBitSet( ent->v.flags, FL_ONGROUND ))
				break;

			for( y = 0; y <= 1; y++ )
			{
				point[0] = x ? maxs[0] : mins[0];
				point[1] = y ? maxs[1] : mins[1];

				trace = SV_Move( point, vec3_origin, vec3_origin, point, MOVE_NORMAL, ent, false );

				if( trace.startsolid )
				{
					SetBits( ent->v.flags, FL_ONGROUND );
					ent->v.groundentity = trace.ent;
					ent->v.friction = 1.0f;
					break;
				}
			}
		}

		SV_LinkEdict( ent, true );
	}
	else
	{
		if( svgame.globals->force_retouch != 0 )
		{
			qboolean monsterClip = FBitSet( ent->v.flags, FL_MONSTERCLIP ) ? true : false;
			trace = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, MOVE_NORMAL, ent, monsterClip );

			// hentacle impact code
			if(( trace.fraction < 1.0f || trace.startsolid ) && SV_IsValidEdict( trace.ent ))
			{
				SV_Impact( ent, trace.ent, &trace );
				if( ent->free ) return;
			}
		}
	}

	if( !SV_RunThink( ent )) return;
	SV_CheckWaterTransition( ent );

}

/*
=============
SV_PhysicsNone

Non moving objects can only think
=============
*/
static void SV_Physics_None( edict_t *ent )
{
	SV_RunThink( ent );
}

//============================================================================
static void SV_Physics_Entity( edict_t *ent )
{
	// user dll can override movement type (Xash3D extension)
	if( svgame.physFuncs.SV_PhysicsEntity && svgame.physFuncs.SV_PhysicsEntity( ent ))
		return; // overrided

	SV_UpdateBaseVelocity( ent );

	if( !FBitSet( ent->v.flags, FL_BASEVELOCITY ) && !VectorIsNull( ent->v.basevelocity ))
	{
		// Apply momentum (add in half of the previous frame of velocity first)
		VectorMA( ent->v.velocity, 1.0f + (sv.frametime * 0.5f), ent->v.basevelocity, ent->v.velocity );
		VectorClear( ent->v.basevelocity );
	}

	ent->v.flags &= ~FL_BASEVELOCITY;

	if( svgame.globals->force_retouch != 0.0f )
	{
		// force retouch even for stationary
		SV_LinkEdict( ent, true );
	}

	switch( ent->v.movetype )
	{
	case MOVETYPE_NONE:
		SV_Physics_None( ent );
		break;
	case MOVETYPE_NOCLIP:
		SV_Physics_Noclip( ent );
		break;
	case MOVETYPE_FOLLOW:
		SV_Physics_Follow( ent );
		break;
	case MOVETYPE_COMPOUND:
		SV_Physics_Compound( ent );
		break;
	case MOVETYPE_STEP:
	case MOVETYPE_PUSHSTEP:
		SV_Physics_Step( ent );
		break;
	case MOVETYPE_FLY:
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_FLYMISSILE:
	case MOVETYPE_BOUNCEMISSILE:
		SV_Physics_Toss( ent );
		break;
	case MOVETYPE_PUSH:
		SV_Physics_Pusher( ent );
		break;
	case MOVETYPE_WALK:
		Host_Error( "%s: bad movetype %i\n", __func__, ent->v.movetype );
		break;
	}

	// g-cont. don't alow free entities during loading because
	// this produce a corrupted baselines
	if( sv.state == ss_active && FBitSet( ent->v.flags, FL_KILLME ))
		SV_FreeEdict( ent );
}

static void SV_RunLightStyles( void )
{
	int	i;

	// run lightstyles animation
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		lightstyle_t *ls = &sv.lightstyles[i];
		int ofs;

		ls->time += sv.frametime;
		ofs = (ls->time * 10);

		if( ls->length == 0 )
			ls->value = 1.0f; // disable this light
		else if( ls->length == 1 )
			ls->value = ls->map[0] / 12.0f;
		else
			ls->value = ls->map[ofs % ls->length] / 12.0f;
	}
}

/*
================
SV_Physics

================
*/
void SV_Physics( void )
{
	edict_t	*ent;
	int    	i;

	SV_CheckAllEnts ();

	svgame.globals->time = sv.time;

	// let the progs know that a new frame has started
	svgame.dllFuncs.pfnStartFrame();

	// treat each object in turn
	for( i = 0; i < svgame.numEntities; i++ )
	{
		ent = EDICT_NUM( i );

		if( !SV_IsValidEdict( ent ))
			continue;

		if( i > 0 && i <= svs.maxclients )
			continue;

		SV_Physics_Entity( ent );
	}

	if( svgame.globals->force_retouch != 0.0f )
		svgame.globals->force_retouch--;

	if( svgame.physFuncs.SV_EndFrame != NULL )
		svgame.physFuncs.SV_EndFrame();

	// animate lightstyles (used for GetEntityIllum)
	SV_RunLightStyles ();

	// increase framecount
	sv.framecount++;

#if 0 // figure out why this causes memory corruption
	// decrement svgame.numEntities if the highest number entities died
	for( ; ( ent = EDICT_NUM( svgame.numEntities - 1 )) && ent->free; svgame.numEntities-- );
#endif
}

/*
================
SV_GetServerTime

Inplementation for new physics interface
================
*/
static double GAME_EXPORT SV_GetServerTime( void )
{
	return sv.time;
}

/*
================
SV_GetFrameTime

Inplementation for new physics interface
================
*/
static double GAME_EXPORT SV_GetFrameTime( void )
{
	return sv.frametime;
}

/*
================
SV_GetHeadNode

Inplementation for new physics interface
================
*/
static areanode_t *GAME_EXPORT SV_GetHeadNode( void )
{
	return sv_areanodes;
}

/*
================
SV_ServerState

Inplementation for new physics interface
================
*/
static int GAME_EXPORT SV_ServerState( void )
{
	return sv.state;
}

/*
================
SV_DrawDebugTriangles

Called from renderer for debug purposes
================
*/
void SV_DrawDebugTriangles( void )
{
	if( host.type != HOST_NORMAL )
		return;

	if( svgame.physFuncs.DrawNormalTriangles != NULL )
	{
		// draw solid overlay
		svgame.physFuncs.DrawNormalTriangles ();
	}

	if( svgame.physFuncs.DrawDebugTriangles != NULL )
	{
#if 0
		// debug draws only
		pglDisable( GL_BLEND );
		pglDepthMask( GL_FALSE );
		pglDisable( GL_TEXTURE_2D );
#endif
		// draw wireframe overlay
		svgame.physFuncs.DrawDebugTriangles ();
#if 0
		pglEnable( GL_TEXTURE_2D );
		pglDepthMask( GL_TRUE );
		pglEnable( GL_BLEND );
#endif
	}
}

/*
================
SV_DrawOrthoTriangles

Called from renderer for debug purposes
================
*/
void SV_DrawOrthoTriangles( void )
{
	if( host.type != HOST_NORMAL )
		return;

	if( svgame.physFuncs.DrawOrthoTriangles != NULL )
	{
		// draw solid overlay
		svgame.physFuncs.DrawOrthoTriangles ();
	}
}

/*
==================
SV_GetLightStyle

needs to get correct working SV_LightPoint
==================
*/
static const char *GAME_EXPORT SV_GetLightStyle( int style )
{
	if( style < 0 ) style = 0;
	if( style >= MAX_LIGHTSTYLES )
		Host_Error( "%s: style: %i >= %d", __func__, style, MAX_LIGHTSTYLES );

	return sv.lightstyles[style].pattern;
}

static void GAME_EXPORT SV_UpdateFogSettings( unsigned int packed_fog )
{
	svgame.movevars.fog_settings = packed_fog;
	host.movevars_changed = true; // force to transmit
}

/*
=========
pfnGetFilesList

=========
*/
static char **GAME_EXPORT pfnGetFilesList( const char *pattern, int *numFiles, int gamedironly )
{
	static search_t	*t = NULL;

	if( t ) Mem_Free( t ); // release prev search

	t = FS_Search( pattern, true, gamedironly );

	if( !t )
	{
		if( numFiles ) *numFiles = 0;
		return NULL;
	}

	if( numFiles ) *numFiles = t->numfilenames;
	return t->filenames;
}

static void *GAME_EXPORT pfnMem_Alloc( size_t cb, const char *filename, const int fileline )
{
	return _Mem_Alloc( svgame.mempool, cb, true, filename, fileline );
}

static void GAME_EXPORT pfnMem_Free( void *mem, const char *filename, const int fileline )
{
	if( !mem ) return;
	_Mem_Free( mem, filename, fileline );
}

/*
=============
pfnPointContents

=============
*/
static int GAME_EXPORT pfnPointContents( const float *pos, int groupmask )
{
	int	oldmask, cont;

	if( !pos ) return CONTENTS_NONE;
	oldmask = svs.groupmask;

	svs.groupmask = groupmask;
	cont = SV_PointContents( pos );
	svs.groupmask = oldmask; // restore old mask

	return cont;
}

static trace_t GAME_EXPORT SV_MoveNormal( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, int type, edict_t *e )
{
	return SV_Move( start, mins, maxs, end, type, e, false );
}

/*
=============
pfnWriteBytes

=============
*/
static void GAME_EXPORT pfnWriteBytes( const byte *bytes, int count )
{
	MSG_WriteBytes( &sv.multicast, bytes, count );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %i )\n", __func__, count );
	svgame.msg_realsize += count;
}

static const byte *GAME_EXPORT pfnLoadImagePixels( const char *filename, int *width, int *height )
{
	rgbdata_t	*pic = FS_LoadImage( filename, NULL, 0 );
	byte	*buffer;

	if( !pic ) return NULL;

	buffer = Mem_Malloc( svgame.mempool, pic->size );
	if( buffer ) memcpy( buffer, pic->buffer, pic->size );
	if( width ) *width = pic->width;
	if( height ) *height = pic->height;
	FS_FreeImage( pic );

	return buffer;
}

static const char *GAME_EXPORT pfnGetModelName( int modelindex )
{
	if( modelindex < 0 || modelindex >= MAX_MODELS )
		return NULL;
	return sv.model_precache[modelindex];
}

static const byte *GAME_EXPORT GL_TextureData( unsigned int texnum )
{
#if !XASH_DEDICATED
	return Host_IsDedicated() ? NULL : ref.dllFuncs.GL_TextureData( texnum );
#else // XASH_DEDICATED
	return NULL;
#endif // XASH_DEDICATED
}

static server_physics_api_t gPhysicsAPI =
{
	SV_LinkEdict,
	SV_GetServerTime,
	SV_GetFrameTime,
	(void*)SV_ModelHandle,
	SV_GetHeadNode,
	SV_ServerState,
	Host_Error,
#if !XASH_DEDICATED
	&gTriApi,	// ouch!
	pfnDrawConsoleString,
	pfnDrawSetTextColor,
	pfnDrawConsoleStringLen,
#else
	NULL,		// ouch! ouch!
	NULL,		// ouch! ouch!
	NULL,		// ouch! ouch!
	NULL,		// ouch! ouch!
#endif
	Con_NPrintf,
	Con_NXPrintf,
	SV_GetLightStyle,
	SV_UpdateFogSettings,
	pfnGetFilesList,
	SV_TraceSurface,
	GL_TextureData,
	pfnMem_Alloc,
	pfnMem_Free,
	pfnPointContents,
	SV_MoveNormal,
	SV_MoveNoEnts,
	(void*)SV_BoxInPVS,
	pfnWriteBytes,
	Mod_CheckLump,
	Mod_ReadLump,
	Mod_SaveLump,
	COM_SaveFile,
	pfnLoadImagePixels,
	pfnGetModelName,
	Sys_GetNativeObject
};

/*
===============
SV_InitPhysicsAPI

Initialize server external physics
===============
*/
qboolean SV_InitPhysicsAPI( void )
{
	static PHYSICAPI	pPhysIface;

	pPhysIface = (PHYSICAPI)COM_GetProcAddress( svgame.hInstance, "Server_GetPhysicsInterface" );
	if( pPhysIface )
	{
		if( pPhysIface( SV_PHYSICS_INTERFACE_VERSION, &gPhysicsAPI, &svgame.physFuncs ))
		{
			Con_Reportf( "%s: ^2initailized extended PhysicAPI ^7ver. %i\n", __func__, SV_PHYSICS_INTERFACE_VERSION );

			if( svgame.physFuncs.SV_CheckFeatures != NULL )
			{
				// grab common engine features (it will be shared across the network)
				Host_ValidateEngineFeatures( ENGINE_FEATURES_MASK, svgame.physFuncs.SV_CheckFeatures( ));
			}
			return true;
		}

		// make sure what physic functions is cleared
		memset( &svgame.physFuncs, 0, sizeof( svgame.physFuncs ));
		Host_ValidateEngineFeatures( ENGINE_FEATURES_MASK, 0 );
		return false; // just tell user about problems
	}

	// physic interface is missed
	Host_ValidateEngineFeatures( ENGINE_FEATURES_MASK, 0 );
	return true;
}
