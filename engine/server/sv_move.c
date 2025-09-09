/*
sv_move.c - monsters movement
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
#include "xash3d_mathlib.h"
#include "server.h"
#include "const.h"
#include "pm_defs.h"

#define MOVE_NORMAL		0	// normal move in the direction monster is facing
#define MOVE_STRAFE		1	// moves in direction specified, no matter which way monster is facing

/*
=============
SV_CheckBottom

Returns false if any part of the bottom of the entity is off an edge that
is not a staircase.

=============
*/
qboolean SV_CheckBottom( edict_t *ent, int iMode )
{
	vec3_t	mins, maxs, start, stop;
	float	mid, bottom;
	qboolean	monsterClip;
	trace_t	trace;
	int	x, y;

	monsterClip = FBitSet( ent->v.flags, FL_MONSTERCLIP ) ? true : false;
	VectorAdd( ent->v.origin, ent->v.mins, mins );
	VectorAdd( ent->v.origin, ent->v.maxs, maxs );

	// if all of the points under the corners are solid world, don't bother
	// with the tougher checks
	// the corners must be within 16 of the midpoint
	start[2] = mins[2] - 1.0f;

	for( x = 0; x <= 1; x++ )
	{
		for( y = 0; y <= 1; y++ )
		{
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];
			svs.groupmask = ent->v.groupinfo;

			if( SV_PointContents( start ) != CONTENTS_SOLID )
				goto realcheck;
		}
	}
	return true; // we got out easy
realcheck:
	// check it for real...
	start[2] = mins[2];

	if( !FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		start[2] += sv_stepsize.value;

	// the midpoint must be within 16 of the bottom
	start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5f;
	start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5f;
	stop[2] = start[2] - 2.0f * sv_stepsize.value;

	if( iMode == WALKMOVE_WORLDONLY )
		trace = SV_MoveNoEnts( start, vec3_origin, vec3_origin, stop, MOVE_NOMONSTERS, ent );
	else trace = SV_Move( start, vec3_origin, vec3_origin, stop, MOVE_NOMONSTERS, ent, monsterClip );

	if( trace.fraction == 1.0f )
		return false;

	mid = bottom = trace.endpos[2];

	// the corners must be within 16 of the midpoint
	for( x = 0; x <= 1; x++ )
	{
		for( y = 0; y <= 1; y++ )
		{
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];

			if( iMode == WALKMOVE_WORLDONLY )
				trace = SV_MoveNoEnts( start, vec3_origin, vec3_origin, stop, MOVE_NOMONSTERS, ent );
			else trace = SV_Move( start, vec3_origin, vec3_origin, stop, MOVE_NOMONSTERS, ent, monsterClip );

			if( trace.fraction != 1.0f && trace.endpos[2] > bottom )
				bottom = trace.endpos[2];
			if( trace.fraction == 1.0f || mid - trace.endpos[2] > sv_stepsize.value )
				return false;
		}
	}
	return true;
}

void SV_WaterMove( edict_t *ent )
{
	float	drownlevel;
	int	waterlevel;
	int	watertype;
	int	flags;

	if( ent->v.movetype == MOVETYPE_NOCLIP )
	{
		ent->v.air_finished = sv.time + 12.0f;
		return;
	}

	// no watermove for monsters but pushables
	if(( ent->v.flags & FL_MONSTER ) && ent->v.health <= 0.0f )
		return;

	drownlevel = (ent->v.deadflag == DEAD_NO) ? 3.0 : 1.0;
	waterlevel = ent->v.waterlevel;
	watertype = ent->v.watertype;
	flags = ent->v.flags;

	if( !( flags & ( FL_IMMUNE_WATER|FL_GODMODE )))
	{
		if((( flags & FL_SWIM ) && waterlevel > drownlevel ) || waterlevel <= drownlevel )
		{
			if( ent->v.air_finished > sv.time && ent->v.pain_finished > sv.time )
			{
				ent->v.dmg += 2;

				if( ent->v.dmg < 15 )
					ent->v.dmg = 10; // quake1 original code
				ent->v.pain_finished = sv.time + 1.0f;
			}
		}
		else
		{
			ent->v.air_finished = sv.time + 12.0f;
			ent->v.dmg = 2;
		}
	}

	if( !waterlevel )
	{
		if( flags & FL_INWATER )
		{
			// leave the water.
			const char *snd = SoundList_GetRandom( EntityWaterExit );
			if( snd )
				SV_StartSound( ent, CHAN_BODY, snd, 1.0f, ATTN_NORM, 0, 100 );

			ent->v.flags = flags & ~FL_INWATER;
		}

		ent->v.air_finished = sv.time + 12.0f;
		return;
	}

	if( watertype == CONTENTS_LAVA )
	{
		if((!( flags & ( FL_IMMUNE_LAVA|FL_GODMODE ))) && ent->v.dmgtime < sv.time )
		{
			if( ent->v.radsuit_finished < sv.time )
				ent->v.dmgtime = sv.time + 0.2f;
			else ent->v.dmgtime = sv.time + 1.0f;
		}
	}
	else if( watertype == CONTENTS_SLIME )
	{
		if((!( flags & ( FL_IMMUNE_SLIME|FL_GODMODE ))) && ent->v.dmgtime < sv.time )
		{
			if( ent->v.radsuit_finished < sv.time )
				ent->v.dmgtime = sv.time + 1.0;
			// otherwise radsuit is fully protect entity from slime
		}
	}

	if( !( flags & FL_INWATER ))
	{
		if( watertype == CONTENTS_WATER )
		{
			// entering the water
			const char *snd = SoundList_GetRandom( EntityWaterEnter );
			if( snd )
				SV_StartSound( ent, CHAN_BODY, snd, 1.0f, ATTN_NORM, 0, 100 );
		}

		ent->v.flags = flags | FL_INWATER;
		ent->v.dmgtime = 0.0f;
	}

	if( !( flags & FL_WATERJUMP ))
	{
		VectorMA( ent->v.velocity, ( ent->v.waterlevel * -0.8f * sv.frametime ), ent->v.velocity, ent->v.velocity );
	}
}

/*
=============
SV_VecToYaw

converts dir to yaw
=============
*/
float SV_VecToYaw( const vec3_t src )
{
	float	yaw;

	if( !src ) return 0.0f;

	if( src[1] == 0.0f && src[0] == 0.0f )
	{
		yaw = 0.0f;
	}
	else
	{
		yaw = (int)( atan2( src[1], src[0] ) * 180.0 / M_PI );
		if( yaw < 0 ) yaw += 360.0f;
	}
	return yaw;
}

//============================================================================

qboolean SV_MoveStep( edict_t *ent, vec3_t move, qboolean relink )
{
	int	i;
	trace_t	trace;
	vec3_t	oldorg, neworg, end;
	qboolean	monsterClip;
	edict_t	*enemy;
	float	dz;

	VectorCopy( ent->v.origin, oldorg );
	VectorAdd( ent->v.origin, move, neworg );
	monsterClip = FBitSet( ent->v.flags, FL_MONSTERCLIP ) ? true : false;

	// well, try it.  Flying and swimming monsters are easiest.
	if( FBitSet( ent->v.flags, FL_SWIM|FL_FLY ))
	{
		// try one move with vertical motion, then one without
		for( i = 0; i < 2; i++ )
		{
			VectorAdd( ent->v.origin, move, neworg );

			enemy = ent->v.enemy;
			if( i == 0 && enemy != NULL )
			{
				dz = ent->v.origin[2] - enemy->v.origin[2];

				if( dz > 40.0f ) neworg[2] -= 8.0f;
				else if( dz < 30.0f ) neworg[2] += 8.0f;
			}

			trace = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, neworg, MOVE_NORMAL, ent, monsterClip );

			if( trace.fraction == 1.0f )
			{
				svs.groupmask = ent->v.groupinfo;

				// that move takes us out of the water.
				// apparently though, it's okay to travel into solids, lava, sky, etc :)
				if( FBitSet( ent->v.flags, FL_SWIM ) && SV_PointContents( trace.endpos ) == CONTENTS_EMPTY )
					return 0;

				VectorCopy( trace.endpos, ent->v.origin );
				if( relink ) SV_LinkEdict( ent, true );

				return 1;
			}
			else
			{
				if( !SV_IsValidEdict( enemy ))
					break;
			}
		}
		return 0;
	}
	else
	{
		dz = sv_stepsize.value;
		neworg[2] += dz;
		VectorCopy( neworg, end );
		end[2] -= dz * 2.0f;

		trace = SV_Move( neworg, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent, monsterClip );
		if( trace.allsolid )
			return 0;

		if( trace.startsolid != 0 )
		{
			neworg[2] -= dz;
			trace = SV_Move( neworg, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent, monsterClip );

			if( trace.allsolid != 0 || trace.startsolid != 0 )
				return 0;
		}

		if( trace.fraction == 1.0f )
		{
			if( FBitSet( ent->v.flags, FL_PARTIALGROUND ))
			{
				VectorAdd( ent->v.origin, move, ent->v.origin );
				if( relink ) SV_LinkEdict( ent, true );
				ClearBits( ent->v.flags, FL_ONGROUND );
				return 1;
			}
			return 0;
		}
		else
		{
			VectorCopy( trace.endpos, ent->v.origin );

			if( SV_CheckBottom( ent, WALKMOVE_NORMAL ) == 0 )
			{
				if( FBitSet( ent->v.flags, FL_PARTIALGROUND ))
				{
					if( relink ) SV_LinkEdict( ent, true );
					return 1;
				}

				VectorCopy( oldorg, ent->v.origin );
				return 0;
			}
			else
			{
				ClearBits( ent->v.flags, FL_PARTIALGROUND );
				ent->v.groundentity = trace.ent;
				if( relink ) SV_LinkEdict( ent, true );

				return 1;
			}
		}
	}
}

qboolean SV_MoveTest( edict_t *ent, vec3_t move, qboolean relink )
{
	float	temp;
	vec3_t	oldorg, neworg, end;
	trace_t	trace;

	VectorCopy( ent->v.origin, oldorg );
	VectorAdd( ent->v.origin, move, neworg );

	temp = sv_stepsize.value;

	neworg[2] += temp;
	VectorCopy( neworg, end );
	end[2] -= temp * 2.0f;

	trace = SV_MoveNoEnts( neworg, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent );

	if( trace.allsolid != 0 )
		return 0;

	if( trace.startsolid != 0 )
	{
		neworg[2] -= temp;
		trace = SV_MoveNoEnts( neworg, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent );

		if( trace.allsolid != 0 || trace.startsolid != 0 )
			return 0;
	}

	if( trace.fraction == 1.0f )
	{
		if( ent->v.flags & FL_PARTIALGROUND )
		{
			VectorAdd( ent->v.origin, move, ent->v.origin );
			if( relink ) SV_LinkEdict( ent, true );
			ent->v.flags &= ~FL_ONGROUND;
			return 1;
		}
		return 0;
	}
	else
	{
		VectorCopy( trace.endpos, ent->v.origin );

		if( SV_CheckBottom( ent, WALKMOVE_WORLDONLY ) == 0 )
		{
			if( ent->v.flags & FL_PARTIALGROUND )
			{
				if( relink ) SV_LinkEdict( ent, true );
				return 1;
			}

			VectorCopy( oldorg, ent->v.origin );
			return 0;
		}
		else
		{
			ent->v.flags &= ~FL_PARTIALGROUND;
			ent->v.groundentity = trace.ent;
			if( relink ) SV_LinkEdict( ent, true );

			return 1;
		}
	}
}

static qboolean SV_StepDirection( edict_t *ent, float yaw, float dist )
{
	int	ret;
	float	cSin, cCos;
	vec3_t	move;

	yaw = yaw * M_PI2 / 360.0f;
	SinCos( yaw, &cSin, &cCos );
	VectorSet( move, cCos * dist, cSin * dist, 0.0f );

	ret = SV_MoveStep( ent, move, false );
	SV_LinkEdict( ent, true );

	return ret;
}

static qboolean SV_FlyDirection( edict_t *ent, vec3_t move )
{
	int	ret;

	ret = SV_MoveStep( ent, move, false );
	SV_LinkEdict( ent, true );

	return ret;
}

static void SV_NewChaseDir( edict_t *actor, vec3_t destination, float dist )
{
	float	deltax, deltay;
	float	tempdir, olddir, turnaround;
	vec3_t	d;

	olddir = anglemod(((int)( actor->v.ideal_yaw / 45.0f )) * 45.0f );
	turnaround = anglemod( olddir - 180.0f );

	deltax = destination[0] - actor->v.origin[0];
	deltay = destination[1] - actor->v.origin[1];

	if( deltax > 10.0f )
		d[1] = 0.0f;
	else if( deltax < -10.0f )
		d[1] = 180.0f;
	else d[1] = -1;

	if( deltay < -10.0f )
		d[2] = 270.0f;
	else if( deltay > 10.0f )
		d[2] = 90.0f;
	else d[2] = -1.0f;

	// try direct route
	if( d[1] != -1.0f && d[2] != -1.0f )
	{
		if( d[1] == 0.0f )
			tempdir = ( d[2] == 90.0f ) ? 45.0f : 315.0f;
		else tempdir = ( d[2] == 90.0f ) ? 135.0f : 215.0f;

		if( tempdir != turnaround && SV_StepDirection( actor, tempdir, dist ))
			return;
	}

	// try other directions
	if( COM_RandomLong( 0, 1 ) != 0 || fabs( deltay ) > fabs( deltax ))
	{
		tempdir = d[1];
		d[1] = d[2];
		d[2] = tempdir;
	}

	if( d[1] != -1.0f && d[1] != turnaround && SV_StepDirection( actor, d[1], dist ))
		return;

	if( d[2] != -1.0f && d[2] != turnaround && SV_StepDirection( actor, d[2], dist ))
		return;

	// there is no direct path to the player, so pick another direction
	if( olddir != -1.0f && SV_StepDirection( actor, olddir, dist ))
		return;

	// fine, just run somewhere.
	if( COM_RandomLong( 0, 1 ) != 1 )
	{
		for( tempdir = 0; tempdir <= 315.0f; tempdir += 45.0f )
		{
			if( tempdir != turnaround && SV_StepDirection( actor, tempdir, dist ))
				return;
		}
	}
	else
	{
		for( tempdir = 315.0f; tempdir >= 0.0f; tempdir -= 45.0f )
		{
			if( tempdir != turnaround && SV_StepDirection( actor, tempdir, dist ))
				return;
		}
	}

	// we tried. run backwards. that ought to work...
	if( turnaround != -1.0f && SV_StepDirection( actor, turnaround, dist ))
		return;

	// well, we're stuck somehow.
	actor->v.ideal_yaw = olddir;

	// if a bridge was pulled out from underneath a monster, it may not have
	// a valid standing position at all.
	if( !SV_CheckBottom( actor, WALKMOVE_NORMAL ))
	{
		actor->v.flags |= FL_PARTIALGROUND;
	}
}

void SV_MoveToOrigin( edict_t *ent, const vec3_t pflGoal, float dist, int iMoveType )
{
	vec3_t	vecDist;

	VectorCopy( pflGoal, vecDist );

	if( ent->v.flags & ( FL_FLY|FL_SWIM|FL_ONGROUND ))
	{
		if( iMoveType == MOVE_NORMAL )
		{
			if( !SV_StepDirection( ent, ent->v.ideal_yaw, dist ))
			{
				SV_NewChaseDir( ent, vecDist, dist );
			}
		}
		else
		{
			vecDist[0] -= ent->v.origin[0];
			vecDist[1] -= ent->v.origin[1];

			if( ent->v.flags & ( FL_FLY|FL_SWIM ))
				vecDist[2] -= ent->v.origin[2];
			else vecDist[2] = 0.0f;

			VectorNormalize( vecDist );
			VectorScale( vecDist, dist, vecDist );
			SV_FlyDirection( ent, vecDist );
		}
	}
}
