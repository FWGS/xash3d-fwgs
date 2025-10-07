/*
cl_main.c - client main loop
Copyright (C) 2009 Uncle Mike

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
#include "client.h"
#include "net_encode.h"
#include "cl_tent.h"
#include "input.h"
#include "kbutton.h"
#include "vgui_draw.h"
#include "library.h"
#include "vid_common.h"
#include "pm_local.h"
#include "sequence.h"

#define MAX_TOTAL_CMDS		32
#define MAX_CMD_BUFFER		8000
#define CONNECTION_PROBLEM_TIME	15.0	// 15 seconds
#define CL_CONNECTION_RETRIES		10
#define CL_TEST_RETRIES_NORESPONCE	3
#define CL_TEST_RETRIES		5

CVAR_DEFINE_AUTO( mp_decals, "300", FCVAR_ARCHIVE, "decals limit in multiplayer" );
CVAR_DEFINE_AUTO( dev_overview, "0", 0, "draw level in overview-mode" );
CVAR_DEFINE_AUTO( cl_resend, "6.0", 0, "time to resend connect" );
CVAR_DEFINE_AUTO( cl_allow_download, "1", FCVAR_ARCHIVE, "allow to downloading resources from the server" );
CVAR_DEFINE_AUTO( cl_allow_upload, "1", FCVAR_ARCHIVE, "allow to uploading resources to the server" );
CVAR_DEFINE_AUTO( cl_download_ingame, "1", FCVAR_ARCHIVE, "allow to downloading resources while client is active" );
CVAR_DEFINE_AUTO( cl_logofile, "lambda", FCVAR_ARCHIVE, "player logo name" );
CVAR_DEFINE_AUTO( cl_logocolor, "orange", FCVAR_ARCHIVE, "player logo color" );
CVAR_DEFINE_AUTO( cl_logoext, "bmp", FCVAR_ARCHIVE, "temporary cvar to tell engine which logo must be packed" );
CVAR_DEFINE_AUTO( cl_test_bandwidth, "1", FCVAR_ARCHIVE, "test network bandwith before connection" );
convar_t	*rcon_address;
convar_t	*cl_timeout;
convar_t	*cl_nopred;
convar_t	*cl_nodelta;
convar_t	*cl_crosshair;
convar_t	*cl_cmdbackup;
convar_t	*cl_showerror;
convar_t	*cl_bmodelinterp;
convar_t	*cl_draw_particles;
convar_t	*cl_draw_tracers;
convar_t	*cl_lightstyle_lerping;
convar_t	*cl_idealpitchscale;
convar_t	*cl_nosmooth;
convar_t	*cl_smoothtime;
convar_t	*cl_clockreset;
convar_t	*cl_fixtimerate;
convar_t	*hud_fontscale;
convar_t	*hud_scale;
convar_t	*cl_solid_players;
convar_t	*cl_draw_beams;
convar_t	*cl_updaterate;
convar_t	*cl_showevents;
convar_t	*cl_cmdrate;
convar_t	*cl_interp;
convar_t	*cl_nointerp;
convar_t	*cl_dlmax;
convar_t	*cl_upmax;

convar_t	*cl_lw;
convar_t	*cl_charset;
convar_t	*cl_trace_messages;
convar_t	*cl_nat;
convar_t	*hud_utf8;
convar_t	*ui_renderworld;

//
// userinfo
//
convar_t	*name;
convar_t	*model;
convar_t	*topcolor;
convar_t	*bottomcolor;
convar_t	*rate;

client_t		cl;
client_static_t	cls;
clgame_static_t	clgame;

void CL_InternetServers_f( void );

//======================================================================
int GAME_EXPORT CL_Active( void )
{
	return ( cls.state == ca_active );
}

qboolean CL_Initialized( void )
{
	return cls.initialized;
}

//======================================================================
qboolean CL_IsInGame( void )
{
	if( host.type == HOST_DEDICATED )
		return true; // always active for dedicated servers

	if( cl.background || CL_GetMaxClients() > 1 )
		return true; // always active for multiplayer or background map

	return ( cls.key_dest == key_game ); // active if not menu or console
}

qboolean CL_IsInMenu( void )
{
	return ( cls.key_dest == key_menu );
}

qboolean CL_IsInConsole( void )
{
	return ( cls.key_dest == key_console );
}

qboolean CL_IsIntermission( void )
{
	return cl.intermission;
}

qboolean CL_IsPlaybackDemo( void )
{
	return cls.demoplayback;
}

qboolean CL_IsRecordDemo( void )
{
	return cls.demorecording;
}

qboolean CL_IsTimeDemo( void )
{
	return cls.timedemo;
}

qboolean CL_DisableVisibility( void )
{
	return cls.envshot_disable_vis;
}

char *CL_Userinfo( void )
{
	return cls.userinfo;
}

int CL_IsDevOverviewMode( void )
{
	if( dev_overview.value > 0.0f )
	{
		if( host_developer.value || cls.spectator )
			return (int)dev_overview.value;
	}

	return 0;
}

/*
===============
CL_CheckClientState

finalize connection process and begin new frame
with new cls.state
===============
*/
void CL_CheckClientState( void )
{
	// first update is the pre-final signon stage
	if(( cls.state == ca_connected || cls.state == ca_validate ) && ( cls.signon == SIGNONS ))
	{
		cls.state = ca_active;
		cls.changelevel = false;		// changelevel is done
		cls.changedemo = false;		// changedemo is done
		cl.first_frame = true;		// first rendering frame

		SCR_MakeLevelShot();		// make levelshot if needs
		Cvar_SetValue( "scr_loading", 0.0f );	// reset progress bar
		Netchan_ReportFlow( &cls.netchan );

		Con_DPrintf( "client connected at %.2f sec\n", Sys_DoubleTime() - cls.timestart );
	}
}

int CL_GetFragmentSize( void *unused, fragsize_t mode )
{
	if( mode == FRAGSIZE_SPLIT )
		return 0;

	if( mode == FRAGSIZE_UNRELIABLE )
		return NET_MAX_MESSAGE;

	if( Netchan_IsLocal( &cls.netchan ))
		return FRAGMENT_LOCAL_SIZE;

	return cl_upmax->value;
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply( void )
{
	// g-cont. my favorite message :-)
	Con_Reportf( "CL_SignonReply: %i\n", cls.signon );

	switch( cls.signon )
	{
	case 1:
		CL_ServerCommand( true, "begin" );
		if( host_developer.value >= DEV_EXTENDED )
			Mem_PrintStats();
		break;
	case 2:
		SCR_EndLoadingPlaque();
		if( cl.proxy_redirect && !cls.spectator )
			CL_Disconnect();
		cl.proxy_redirect = false;

		if( cls.demoplayback )
			Sequence_OnLevelLoad( clgame.mapname );
		break;
	}
}

/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
static float CL_LerpPoint( void )
{
	double f = cl_serverframetime();
	double frac;

	if( f == 0.0 || cls.timedemo )
	{
		double fgap = cl_clientframetime();
		cl.time = cl.mtime[0];

		// maybe don't need for Xash demos
		if( cls.demoplayback )
			cl.oldtime = cl.mtime[0] - fgap;

		return 1.0f;
	}

	if( cl_interp->value <= 0.001 )
		return 1.0f;

	frac = ( cl.time - cl.mtime[0] ) / cl_interp->value;

	return frac;
}

/*
===============
CL_DriftInterpolationAmount

Drift interpolation value (this is used for server unlag system)
===============
*/
int CL_DriftInterpolationAmount( int goal )
{
	float	fgoal, maxmove, diff;
	int	msec;

	fgoal = (float)goal / 1000.0f;

	if( fgoal != cl.local.interp_amount )
	{
		maxmove = host.frametime * 0.05;
		diff = fgoal - cl.local.interp_amount;
		diff = bound( -maxmove, diff, maxmove );
		cl.local.interp_amount += diff;
	}

	msec = cl.local.interp_amount * 1000.0f;
	msec = bound( 0, msec, 100 );

	return msec;
}

/*
===============
CL_ComputeClientInterpolationAmount

Validate interpolation cvars, calc interpolation window
===============
*/
void CL_ComputeClientInterpolationAmount( usercmd_t *cmd )
{
	const float epsilon = 0.001f; // to avoid float invalid comparision
	float min_interp;
	float max_interp = MAX_EX_INTERP;
	float interpolation_time;

	if( cl_updaterate->value < MIN_UPDATERATE )
	{
		Con_Printf( "cl_updaterate minimum is %f, resetting to default (20)\n", MIN_UPDATERATE );
		Cvar_Reset( "cl_updaterate" );
	}

	if( cl_updaterate->value > MAX_UPDATERATE )
	{
		Con_Printf( "cl_updaterate clamped at maximum (%f)\n", MAX_UPDATERATE );
		Cvar_SetValue( "cl_updaterate", MAX_UPDATERATE );
	}

	if( cls.spectator )
		max_interp = 0.2f;

	min_interp = 1.0f / cl_updaterate->value;
	interpolation_time = cl_interp->value * 1000.0;

	if( (cl_interp->value + epsilon) < min_interp )
	{
		Con_Printf( "ex_interp forced up to %.1f msec\n", min_interp * 1000.f );
		Cvar_SetValue( "ex_interp", min_interp );
	}
	else if( (cl_interp->value - epsilon) > max_interp )
	{
		Con_Printf( "ex_interp forced down to %.1f msec\n", max_interp * 1000.f );
		Cvar_SetValue( "ex_interp", max_interp );
	}

	interpolation_time = bound( min_interp, interpolation_time, max_interp );
	cmd->lerp_msec = CL_DriftInterpolationAmount( interpolation_time * 1000 );
}

/*
=================
CL_ComputePacketLoss

=================
*/
void CL_ComputePacketLoss( void )
{
	int	i, frm;
	frame_t	*frame;
	int	count = 0;
	int	lost = 0;

	if( host.realtime < cls.packet_loss_recalc_time )
		return;

	// recalc every second
	cls.packet_loss_recalc_time = host.realtime + 1.0;

	// compuate packet loss
	for( i = cls.netchan.incoming_sequence - CL_UPDATE_BACKUP + 1; i <= cls.netchan.incoming_sequence; i++ )
	{
		frm = i;
		frame = &cl.frames[frm & CL_UPDATE_MASK];

		if( frame->receivedtime == -1.0 )
			lost++;
		count++;
	}

	if( count <= 0 ) cls.packet_loss = 0.0f;
	else cls.packet_loss = ( 100.0f * (float)lost ) / (float)count;
}

/*
=================
CL_UpdateFrameLerp

=================
*/
void CL_UpdateFrameLerp( void )
{
	if( cls.state != ca_active || !cl.validsequence )
		return;

	// compute last interpolation amount
	cl.lerpFrac = CL_LerpPoint();

	cl.commands[(cls.netchan.outgoing_sequence - 1) & CL_UPDATE_MASK].frame_lerp = cl.lerpFrac;
}

void CL_FindInterpolatedAddAngle( float t, float *frac, pred_viewangle_t **prev, pred_viewangle_t **next )
{
	int	i, i0, i1, imod;
	float	at;

	imod = cl.angle_position - 1;
	i0 = (imod + 1) & ANGLE_MASK;
	i1 = (imod + 0) & ANGLE_MASK;

	if( cl.predicted_angle[i0].starttime >= t )
	{
		for( i = 0; i < ANGLE_BACKUP - 2; i++ )
		{
			at = cl.predicted_angle[imod & ANGLE_MASK].starttime;
			if( at == 0.0f ) break;

			if( at < t )
			{
				i0 = (imod + 1) & ANGLE_MASK;
				i1 = (imod + 0) & ANGLE_MASK;
				break;
			}
			imod--;
		}
	}

	*next = &cl.predicted_angle[i0];
	*prev = &cl.predicted_angle[i1];

	// avoid division by zero (probably this should never happens)
	if((*prev)->starttime == (*next)->starttime )
	{
		*prev = *next;
		*frac = 0.0f;
		return;
	}

	// time spans the two entries
	*frac = ( t - (*prev)->starttime ) / ((*next)->starttime - (*prev)->starttime );
	*frac = bound( 0.0f, *frac, 1.0f );
}

void CL_ApplyAddAngle( void )
{
	pred_viewangle_t	*prev = NULL, *next = NULL;
	float		addangletotal = 0.0f;
	float		amove, frac = 0.0f;

	CL_FindInterpolatedAddAngle( cl.time, &frac, &prev, &next );

	if( prev && next )
		addangletotal = prev->total + frac * ( next->total - prev->total );
	else addangletotal = cl.prevaddangletotal;

	amove = addangletotal - cl.prevaddangletotal;

	// update input angles
	cl.viewangles[YAW] += amove;

	// remember last total
	cl.prevaddangletotal = addangletotal;
}


/*
=======================================================================

CLIENT MOVEMENT COMMUNICATION

=======================================================================
*/
/*
===============
CL_ProcessShowTexturesCmds

navigate around texture atlas
===============
*/
qboolean CL_ProcessShowTexturesCmds( usercmd_t *cmd )
{
	static int	oldbuttons;
	int		changed;
	int		pressed, released;

	if( !gl_showtextures->value || CL_IsDevOverviewMode( ))
		return false;

	changed = (oldbuttons ^ cmd->buttons);
	pressed =  changed & cmd->buttons;
	released = changed & (~cmd->buttons);

	if( released & ( IN_RIGHT|IN_MOVERIGHT ))
		Cvar_SetValue( "r_showtextures", gl_showtextures->value + 1 );
	if( released & ( IN_LEFT|IN_MOVELEFT ))
		Cvar_SetValue( "r_showtextures", Q_max( 1, gl_showtextures->value - 1 ));
	oldbuttons = cmd->buttons;

	return true;
}

/*
===============
CL_ProcessOverviewCmds

Transform user movement into overview adjust
===============
*/
qboolean CL_ProcessOverviewCmds( usercmd_t *cmd )
{
	ref_overview_t	*ov = &clgame.overView;
	int		sign = 1;
	float		size = world.size[!ov->rotated] / world.size[ov->rotated];
	float		step = (2.0f / size) * host.realframetime;
	float		step2 = step * 100.0f * (2.0f / ov->flZoom);

	if( !CL_IsDevOverviewMode() || gl_showtextures->value )
		return false;

	if( ov->flZoom < 0.0f ) sign = -1;

	if( cmd->upmove > 0.0f ) ov->zNear += step;
	else if( cmd->upmove < 0.0f ) ov->zNear -= step;

	if( cmd->buttons & IN_JUMP ) ov->zFar += step;
	else if( cmd->buttons & IN_DUCK ) ov->zFar -= step;

	if( cmd->buttons & IN_FORWARD ) ov->origin[ov->rotated] -= sign * step2;
	else if( cmd->buttons & IN_BACK ) ov->origin[ov->rotated] += sign * step2;

	if( ov->rotated )
	{
		if( cmd->buttons & ( IN_RIGHT|IN_MOVERIGHT ))
			ov->origin[0] -= sign * step2;
		else if( cmd->buttons & ( IN_LEFT|IN_MOVELEFT ))
			ov->origin[0] += sign * step2;
	}
	else
	{
		if( cmd->buttons & ( IN_RIGHT|IN_MOVERIGHT ))
			ov->origin[1] += sign * step2;
		else if( cmd->buttons & ( IN_LEFT|IN_MOVELEFT ))
			ov->origin[1] -= sign * step2;
	}

	if( cmd->buttons & IN_ATTACK ) ov->flZoom += step;
	else if( cmd->buttons & IN_ATTACK2 ) ov->flZoom -= step;

	if( ov->flZoom == 0.0f ) ov->flZoom = 0.0001f; // to prevent disivion by zero

	return true;
}

/*
=================
CL_UpdateClientData

tell the client.dll about player origin, angles, fov, etc
=================
*/
void CL_UpdateClientData( void )
{
	client_data_t	cdat;

	if( cls.state != ca_active )
		return;

	memset( &cdat, 0, sizeof( cdat ) );

	VectorCopy( cl.viewangles, cdat.viewangles );
	VectorCopy( clgame.entities[cl.viewentity].origin, cdat.origin );
	cdat.iWeaponBits = cl.local.weapons;
	cdat.fov = cl.local.scr_fov;

	if( clgame.dllFuncs.pfnUpdateClientData( &cdat, cl.time ))
	{
		// grab changes if successful
		VectorCopy( cdat.viewangles, cl.viewangles );
		cl.local.scr_fov = cdat.fov;
	}
}

/*
=================
CL_CreateCmd
=================
*/
void CL_CreateCmd( void )
{
	usercmd_t	nullcmd, *cmd;
	runcmd_t		*pcmd;
	vec3_t		angles;
	qboolean		active;
	int		input_override;
	int		i, ms;

	if( cls.state < ca_connected || cls.state == ca_cinematic )
		return;

	// store viewangles in case it's will be freeze
	VectorCopy( cl.viewangles, angles );
	ms = bound( 1, host.frametime * 1000, 255 );
	input_override = 0;

	CL_SetSolidEntities();
	CL_PushPMStates();
	CL_SetSolidPlayers( cl.playernum );

	// message we are constructing.
	i = cls.netchan.outgoing_sequence & CL_UPDATE_MASK;
	pcmd = &cl.commands[i];
	pcmd->processedfuncs = false;

	if( !cls.demoplayback )
	{
		pcmd->senttime = host.realtime;
		memset( &pcmd->cmd, 0, sizeof( pcmd->cmd ));
		pcmd->receivedtime = -1.0;
		pcmd->heldback = false;
		pcmd->sendsize = 0;
		cmd = &pcmd->cmd;
	}
	else
	{
		memset( &nullcmd, 0, sizeof( nullcmd ));
		cmd = &nullcmd;
	}

	active = (( cls.signon == SIGNONS ) && !cl.paused && !cls.demoplayback );
	Platform_PreCreateMove();
	clgame.dllFuncs.CL_CreateMove( host.frametime, cmd, active );
	IN_EngineAppendMove( host.frametime, cmd, active  );

	CL_PopPMStates();

	if( !cls.demoplayback )
	{
		CL_ComputeClientInterpolationAmount( &pcmd->cmd );
		pcmd->cmd.lightlevel = cl.local.light_level;
		pcmd->cmd.msec = ms;
	}

	input_override |= CL_ProcessOverviewCmds( &pcmd->cmd );
	input_override |= CL_ProcessShowTexturesCmds( &pcmd->cmd );

	if(( cl.background && !cls.demoplayback ) || input_override || cls.changelevel )
	{
		VectorCopy( angles, pcmd->cmd.viewangles );
		VectorCopy( angles, cl.viewangles );
		if( !cl.background ) pcmd->cmd.msec = 0;
	}

	// demo always have commands so don't overwrite them
	if( !cls.demoplayback ) cl.cmd = &pcmd->cmd;

	// predict all unacknowledged movements
	CL_PredictMovement( false );
}

void CL_WriteUsercmd( sizebuf_t *msg, int from, int to )
{
	usercmd_t	nullcmd;
	usercmd_t	*f, *t;

	Assert( from == -1 || ( from >= 0 && from < MULTIPLAYER_BACKUP ));
	Assert( to >= 0 && to < MULTIPLAYER_BACKUP );

	if( from == -1 )
	{
		memset( &nullcmd, 0, sizeof( nullcmd ));
		f = &nullcmd;
	}
	else
	{
		f = &cl.commands[from].cmd;
	}

	t = &cl.commands[to].cmd;

	// write it into the buffer
	MSG_WriteDeltaUsercmd( msg, f, t );
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds
===================
*/
void CL_WritePacket( void )
{
	sizebuf_t		buf;
	qboolean		send_command = false;
	byte		data[MAX_CMD_BUFFER];
	int		i, from, to, key, size;
	int		numbackup = 2;
	int		numcmds;
	int		newcmds;
	int		cmdnumber;

	// don't send anything if playing back a demo
	if( cls.demoplayback || cls.state < ca_connected || cls.state == ca_cinematic )
		return;

	CL_ComputePacketLoss ();

	memset( data, 0, sizeof( data ));
	MSG_Init( &buf, "ClientData", data, sizeof( data ));

	// Determine number of backup commands to send along
	numbackup = bound( 0, cl_cmdbackup->value, cls.legacymode ? MAX_LEGACY_BACKUP_CMDS : MAX_BACKUP_COMMANDS );
	if( cls.state == ca_connected ) numbackup = 0;

	// clamp cmdrate
	if( cl_cmdrate->value < 10.0f )
	{
		Cvar_SetValue( "cl_cmdrate", 10.0f );
	}
	else if( cl_cmdrate->value > 100.0f )
	{
		Cvar_SetValue( "cl_cmdrate", 100.0f );
	}

	// Check to see if we can actually send this command

	// In single player, send commands as fast as possible
	// Otherwise, only send when ready and when not choking bandwidth
	if( cl.maxclients == 1 || ( NET_IsLocalAddress( cls.netchan.remote_address ) && !host_limitlocal->value ))
		send_command = true;

	if(( host.realtime >= cls.nextcmdtime ) && Netchan_CanPacket( &cls.netchan, true ))
		send_command = true;

	if( cl.send_reply )
	{
		cl.send_reply = false;
		send_command = true;
	}

	// spectator is not sending cmds to server
	if( cls.spectator && cls.state == ca_active && cl.delta_sequence == cl.validsequence )
	{
		if( !( cls.demorecording && cls.demowaiting ) && cls.nextcmdtime + 1.0f > host.realtime )
			return;
	}

	if(( cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged ) >= CL_UPDATE_MASK )
	{
		if(( host.realtime - cls.netchan.last_received ) > CONNECTION_PROBLEM_TIME )
		{
			Con_NPrintf( 1, "^3Warning:^1 Connection Problem^7\n" );
			Con_NPrintf( 2, "^1Auto-disconnect in %.1f seconds^7", cl_timeout->value - ( host.realtime - cls.netchan.last_received ));
			cl.validsequence = 0;
		}
	}

	if( cl_nodelta->value )
		cl.validsequence = 0;

	if( send_command )
	{
		int	outgoing_sequence;

		if( cl_cmdrate->value > 0 ) // clamped between 10 and 100 fps
			cls.nextcmdtime = host.realtime + bound( 0.1f, ( 1.0f / cl_cmdrate->value ), 0.01f );
		else cls.nextcmdtime = host.realtime; // always able to send right away

		if( cls.lastoutgoingcommand == -1 )
		{
			outgoing_sequence = cls.netchan.outgoing_sequence;
			cls.lastoutgoingcommand = cls.netchan.outgoing_sequence;
		}
		else outgoing_sequence = cls.lastoutgoingcommand + 1;

		// begin a client move command
		MSG_BeginClientCmd( &buf, clc_move );

		// save the position for a checksum byte
		key = MSG_GetRealBytesWritten( &buf );
		MSG_WriteByte( &buf, 0 );

		// write packet lossage percentation
		MSG_WriteByte( &buf, cls.packet_loss );

		// say how many backups we'll be sending
		MSG_WriteByte( &buf, numbackup );

		// how many real commands have queued up
		newcmds = ( cls.netchan.outgoing_sequence - cls.lastoutgoingcommand );

		// put an upper/lower bound on this
		newcmds = bound( 0, newcmds, cls.legacymode ? MAX_LEGACY_TOTAL_CMDS: MAX_TOTAL_CMDS );
		if( cls.state == ca_connected ) newcmds = 0;

		MSG_WriteByte( &buf, newcmds );

		numcmds = newcmds + numbackup;

		from = -1;

		for( i = numcmds - 1; i >= 0; i-- )
		{
			cmdnumber = ( cls.netchan.outgoing_sequence - i ) & CL_UPDATE_MASK;

			to = cmdnumber;
			CL_WriteUsercmd( &buf, from, to );
			from = to;

			if( MSG_CheckOverflow( &buf ))
				Host_Error( "CL_WritePacket: overflowed command buffer (%i bytes)\n", MAX_CMD_BUFFER );
		}

		// calculate a checksum over the move commands
		size = MSG_GetRealBytesWritten( &buf ) - key - 1;
		buf.pData[key] = CRC32_BlockSequence( buf.pData + key + 1, size, cls.netchan.outgoing_sequence );

		// message we are constructing.
		i = cls.netchan.outgoing_sequence & CL_UPDATE_MASK;

		// determine if we need to ask for a new set of delta's.
		if( cl.validsequence && (cls.state == ca_active) && !( cls.demorecording && cls.demowaiting ))
		{
			cl.delta_sequence = cl.validsequence;

			MSG_BeginClientCmd( &buf, clc_delta );
			MSG_WriteByte( &buf, cl.validsequence & 0xFF );
		}
		else
		{
			// request delta compression of entities
			cl.delta_sequence = -1;
		}

		if( MSG_CheckOverflow( &buf ))
			Host_Error( "CL_WritePacket: overflowed command buffer (%i bytes)\n", MAX_CMD_BUFFER );

		// remember outgoing command that we are sending
		cls.lastoutgoingcommand = cls.netchan.outgoing_sequence;

		// update size counter for netgraph
		cl.commands[cls.netchan.outgoing_sequence & CL_UPDATE_MASK].sendsize = MSG_GetNumBytesWritten( &buf );
		cl.commands[cls.netchan.outgoing_sequence & CL_UPDATE_MASK].heldback = false;

		// send voice data to the server
		CL_AddVoiceToDatagram();

		// composite the rest of the datagram..
		if( MSG_GetNumBitsWritten( &cls.datagram ) <= MSG_GetNumBitsLeft( &buf ))
			MSG_WriteBits( &buf, MSG_GetData( &cls.datagram ), MSG_GetNumBitsWritten( &cls.datagram ));
		MSG_Clear( &cls.datagram );

		// deliver the message (or update reliable)
		Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
	}
	else
	{
		// mark command as held back so we'll send it next time
		cl.commands[cls.netchan.outgoing_sequence & CL_UPDATE_MASK].heldback = true;

		// increment sequence number so we can detect that we've held back packets.
		cls.netchan.outgoing_sequence++;
	}

	if( cls.demorecording && numbackup > 0 )
	{
		// Back up one because we've incremented outgoing_sequence each frame by 1 unit
		cmdnumber = ( cls.netchan.outgoing_sequence - 1 ) & CL_UPDATE_MASK;
		CL_WriteDemoUserCmd( cmdnumber );
	}

	// update download/upload slider.
	Netchan_UpdateProgress( &cls.netchan );
}

/*
=================
CL_SendCommand

Called every frame to builds and sends a command packet to the server.
=================
*/
void CL_SendCommand( void )
{
	// we create commands even if a demo is playing,
	CL_CreateCmd();

	// clc_move, userinfo etc
	CL_WritePacket();
}

/*
==================
CL_BeginUpload_f
==================
*/
void CL_BeginUpload_f( void )
{
	const char		*name;
	resource_t	custResource;
	byte		*buf = NULL;
	int		size = 0;
	byte		md5[16];

	name = Cmd_Argv( 1 );

	if( !COM_CheckString( name ))
		return;

	if( !cl_allow_upload.value )
		return;

	if( Q_strlen( name ) != 36 || Q_strnicmp( name, "!MD5", 4 ))
	{
		Con_Printf( "Ingoring upload of non-customization\n" );
		return;
	}

	memset( &custResource, 0, sizeof( custResource ));
	COM_HexConvert( name + 4, 32, md5 );

	if( HPAK_ResourceForHash( CUSTOM_RES_PATH, md5, &custResource ))
	{
		if( memcmp( md5, custResource.rgucMD5_hash, 16 ))
		{
			Con_Reportf( "Bogus data retrieved from %s, attempting to delete entry\n", CUSTOM_RES_PATH );
			HPAK_RemoveLump( CUSTOM_RES_PATH, &custResource );
			return;
		}

		if( HPAK_GetDataPointer( CUSTOM_RES_PATH, &custResource, &buf, &size ))
		{
			byte		md5[16];
			MD5Context_t	ctx;

			memset( &ctx, 0, sizeof( ctx ));
			MD5Init( &ctx );
			MD5Update( &ctx, buf, size );
			MD5Final( md5, &ctx );

			if( memcmp( custResource.rgucMD5_hash, md5, 16 ))
			{
				Con_Reportf( "HPAK_AddLump called with bogus lump, md5 mismatch\n" );
				Con_Reportf( "Purported:  %s\n", MD5_Print( custResource.rgucMD5_hash ) );
				Con_Reportf( "Actual   :  %s\n", MD5_Print( md5 ) );
				Con_Reportf( "Removing conflicting lump\n" );
				HPAK_RemoveLump( CUSTOM_RES_PATH, &custResource );
				return;
			}
		}
	}

	if( buf && size > 0 )
	{
		Netchan_CreateFileFragmentsFromBuffer( &cls.netchan, name, buf, size );
		Netchan_FragSend( &cls.netchan );
		Mem_Free( buf );
	}
}

/*
==================
CL_Quit_f
==================
*/
void CL_Quit_f( void )
{
	CL_Disconnect();
	Sys_Quit();
}

/*
================
CL_Drop

Called after an Host_Error was thrown
================
*/
void CL_Drop( void )
{
	if( !cls.initialized )
		return;
	CL_Disconnect();
}

/*
=======================
CL_SendConnectPacket

We have gotten a challenge from the server, so try and
connect.
======================
*/
void CL_SendConnectPacket( void )
{
	char	protinfo[MAX_INFO_STRING];
	const char	*qport;
	const char	*key;
	netadr_t	adr;

	if( !NET_StringToAdr( cls.servername, &adr ))
	{
		Con_Printf( "CL_SendConnectPacket: bad server address\n");
		cls.connect_time = 0;
		return;
	}

	if( adr.port == 0 ) adr.port = MSG_BigShort( PORT_SERVER );
	qport = Cvar_VariableString( "net_qport" );
	key = ID_GetMD5();

	memset( protinfo, 0, sizeof( protinfo ));

	if( adr.type == NA_LOOPBACK )
	{
		IN_LockInputDevices( false );
	}
	else
	{
		int input_devices;

		input_devices = IN_CollectInputDevices();
		IN_LockInputDevices( true );

		Cvar_SetCheatState();
		Cvar_FullSet( "sv_cheats", "0", FCVAR_READ_ONLY | FCVAR_SERVER );

		Info_SetValueForKeyf( protinfo, "d", sizeof( protinfo ),  "%d", input_devices );
		Info_SetValueForKey( protinfo, "v", XASH_VERSION, sizeof( protinfo ) );
		Info_SetValueForKeyf( protinfo, "b", sizeof( protinfo ), "%d", Q_buildnum( ));
		Info_SetValueForKey( protinfo, "o", Q_buildos(), sizeof( protinfo ) );
		Info_SetValueForKey( protinfo, "a", Q_buildarch(), sizeof( protinfo ) );
	}

	if( cls.legacymode )
	{
		// set related userinfo keys
		if( cl_dlmax->value >= 40000 || cl_dlmax->value < 100 )
			Info_SetValueForKey( cls.userinfo, "cl_maxpacket", "1400", sizeof( cls.userinfo ) );
		else
			Info_SetValueForKey( cls.userinfo, "cl_maxpacket", cl_dlmax->string, sizeof( cls.userinfo ) );

		if( !*Info_ValueForKey( cls.userinfo,"cl_maxpayload") )
			Info_SetValueForKey( cls.userinfo, "cl_maxpayload", "1000", sizeof( cls.userinfo ) );

		Info_SetValueForKey( protinfo, "i", key, sizeof( protinfo ) );

		Netchan_OutOfBandPrint( NS_CLIENT, adr, "connect %i %i %i \"%s\" %d \"%s\"\n",
			PROTOCOL_LEGACY_VERSION, Q_atoi( qport ), cls.challenge, cls.userinfo, NET_LEGACY_EXT_SPLIT, protinfo );
		Con_Printf( "Trying to connect by legacy protocol\n" );
	}
	else
	{
		int extensions = NET_EXT_SPLITSIZE;

		if( cl_dlmax->value > FRAGMENT_MAX_SIZE  || cl_dlmax->value < FRAGMENT_MIN_SIZE )
			Cvar_SetValue( "cl_dlmax", FRAGMENT_DEFAULT_SIZE );

		Info_RemoveKey( cls.userinfo, "cl_maxpacket" );
		Info_RemoveKey( cls.userinfo, "cl_maxpayload" );

		Info_SetValueForKey( protinfo, "uuid", key, sizeof( protinfo ));
		Info_SetValueForKey( protinfo, "qport", qport, sizeof( protinfo ));
		Info_SetValueForKeyf( protinfo, "ext", sizeof( protinfo ), "%d", extensions);

		Netchan_OutOfBandPrint( NS_CLIENT, adr, "connect %i %i \"%s\" \"%s\"\n", PROTOCOL_VERSION, cls.challenge, protinfo, cls.userinfo );
		Con_Printf( "Trying to connect by modern protocol\n" );
	}

	cls.timestart = Sys_DoubleTime();
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend( void )
{
	netadr_t adr;
	net_gai_state_t res;
	qboolean bandwidthTest;

	if( cls.internetservers_wait )
		CL_InternetServers_f();

	// if the local server is running and we aren't then connect
	if( cls.state == ca_disconnected && SV_Active( ))
	{
		cls.signon = 0;
		cls.state = ca_connecting;
		Q_strncpy( cls.servername, "localhost", sizeof( cls.servername ));
		cls.serveradr.type = NA_LOOPBACK;

		// we don't need a challenge on the localhost
		CL_SendConnectPacket();
		return;
	}

	// resend if we haven't gotten a reply yet
	if( cls.demoplayback || cls.state != ca_connecting )
		return;

	if( cl_resend.value < CL_MIN_RESEND_TIME )
		Cvar_SetValue( "cl_resend", CL_MIN_RESEND_TIME );
	else if( cl_resend.value > CL_MAX_RESEND_TIME )
		Cvar_SetValue( "cl_resend", CL_MAX_RESEND_TIME );

	if(( host.realtime - cls.connect_time ) < cl_resend.value )
		return;

	res = NET_StringToAdrNB( cls.servername, &adr );

	if( res == NET_EAI_NONAME )
	{
		CL_Disconnect();
		return;
	}

	if( res == NET_EAI_AGAIN )
	{
		cls.connect_time = MAX_HEARTBEAT;
		return;
	}

	// only retry so many times before failure.
	if( cls.connect_retry >= CL_CONNECTION_RETRIES )
	{
		Con_DPrintf( S_ERROR "CL_CheckForResend: couldn't connected\n" );
		CL_Disconnect();
		return;
	}

	if( adr.port == 0 ) adr.port = MSG_BigShort( PORT_SERVER );

	if( cls.connect_retry == CL_TEST_RETRIES_NORESPONCE )
	{
		// too many fails use default connection method
		Con_Printf( "hi-speed connection is failed, use default method\n" );
		Netchan_OutOfBandPrint( NS_CLIENT, adr, "getchallenge\n" );
		Cvar_SetValue( "cl_dlmax", FRAGMENT_MIN_SIZE );
		cls.connect_time = host.realtime;
		cls.connect_retry++;
		return;
	}

	bandwidthTest = !cls.legacymode && cl_test_bandwidth.value;
	cls.serveradr = adr;
	cls.max_fragment_size = Q_min( FRAGMENT_MAX_SIZE, cls.max_fragment_size / (cls.connect_retry + 1));
	cls.connect_time = host.realtime; // for retransmit requests
	cls.connect_retry++;

	if( bandwidthTest )
		Con_Printf( "Connecting to %s... [retry #%i, max fragment size %i]\n", cls.servername, cls.connect_retry, cls.max_fragment_size );
	else
		Con_Printf( "Connecting to %s... [retry #%i]\n", cls.servername, cls.connect_retry );

	if( bandwidthTest )
		Netchan_OutOfBandPrint( NS_CLIENT, adr, "bandwidth %i %i\n", PROTOCOL_VERSION, cls.max_fragment_size );
	else
		Netchan_OutOfBandPrint( NS_CLIENT, adr, "getchallenge\n" );
}

resource_t *CL_AddResource( resourcetype_t type, const char *name, int size, qboolean bFatalIfMissing, int index )
{
	resource_t	*r = &cl.resourcelist[cl.num_resources];

	if( cl.num_resources >= MAX_RESOURCES )
		Host_Error( "Too many resources on client\n" );
	cl.num_resources++;

	Q_strncpy( r->szFileName, name, sizeof( r->szFileName ));
	r->ucFlags |= bFatalIfMissing ? RES_FATALIFMISSING : 0;
	r->nDownloadSize = size;
	r->nIndex = index;
	r->type = type;

	return r;
}

void CL_CreateResourceList( void )
{
	char szFileName[MAX_OSPATH];
	byte		rgucMD5_hash[16];
	resource_t	*pNewResource;
	int		nSize;
	file_t		*fp;

	HPAK_FlushHostQueue();
	cl.num_resources = 0;
	memset( rgucMD5_hash, 0, sizeof( rgucMD5_hash ));

	// sanitize cvar value
	if( Q_strcmp( cl_logoext.string, "bmp" ) &&
		 Q_strcmp( cl_logoext.string, "png" ))
		Cvar_DirectSet( &cl_logoext, "bmp" );

	Q_snprintf( szFileName, sizeof( szFileName ),
		"logos/remapped.%s", cl_logoext.string );
	fp = FS_Open( szFileName, "rb", true );

	if( !fp )
		return;

	MD5_HashFile( rgucMD5_hash, szFileName, NULL );
	nSize = FS_FileLength( fp );

	if( nSize != 0 )
	{
		pNewResource = CL_AddResource( t_decal, szFileName, nSize, false, 0 );

		if( pNewResource )
		{
			SetBits( pNewResource->ucFlags, RES_CUSTOM );
			memcpy( pNewResource->rgucMD5_hash, rgucMD5_hash, 16 );
			HPAK_AddLump( false, CUSTOM_RES_PATH, pNewResource, NULL, fp );
		}
	}

	FS_Close( fp );
}

/*
================
CL_Connect_f

================
*/
void CL_Connect_f( void )
{
	string	server;
	qboolean legacyconnect = false;

	// hidden hint to connect by using legacy protocol
	if( Cmd_Argc() == 3 )
	{
		legacyconnect = !Q_strcmp( Cmd_Argv( 2 ), "legacy" );
	}
	else if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "connect <server>\n" );
		return;
	}

	Q_strncpy( server, Cmd_Argv( 1 ), sizeof( server ));

	// if running a local server, kill it and reissue
	if( SV_Active( )) Host_ShutdownServer();
	NET_Config( true, !CVAR_TO_BOOL( cl_nat )); // allow remote

	Con_Printf( "server %s\n", server );
	CL_Disconnect();

	// TESTTEST: a see console during connection
	UI_SetActiveMenu( false );
	Key_SetKeyDest( key_console );

	cls.state = ca_connecting;
	cls.legacymode = legacyconnect;
	Q_strncpy( cls.servername, server, sizeof( cls.servername ));
	cls.connect_time = MAX_HEARTBEAT; // CL_CheckForResend() will fire immediately
	cls.max_fragment_size = FRAGMENT_MAX_SIZE; // guess a we can establish connection with maximum fragment size
	cls.connect_retry = 0;
	cls.spectator = false;
	cls.signon = 0;
}

/*
=====================
CL_Rcon_f

Send the rest of the command line over as
an unconnected command.
=====================
*/
void CL_Rcon_f( void )
{
	char	message[1024];
	netadr_t	to;
	string command;
	int	i;

	if( !COM_CheckString( rcon_password.string ))
	{
		Con_Printf( "You must set 'rcon_password' before issuing an rcon command.\n" );
		return;
	}

	message[0] = (char)255;
	message[1] = (char)255;
	message[2] = (char)255;
	message[3] = (char)255;
	message[4] = 0;

	NET_Config( true, false );	// allow remote

	Q_strncat( message, "rcon ", sizeof( message ));
	Q_strncat( message, rcon_password.string, sizeof( message ));
	Q_strncat( message, " ", sizeof( message ) );

	for( i = 1; i < Cmd_Argc(); i++ )
	{
		Cmd_Escape( command, Cmd_Argv( i ), sizeof( command ));
		Q_strncat( message, command, sizeof( message ));
		Q_strncat( message, " ", sizeof( message ));
	}

	if( cls.state >= ca_connected )
	{
		to = cls.netchan.remote_address;
	}
	else
	{
		if( !COM_CheckString( rcon_address->string ))
		{
			Con_Printf( "You must either be connected or set the 'rcon_address' cvar to issue rcon commands\n" );
			return;
		}

		NET_StringToAdr( rcon_address->string, &to );
		if( to.port == 0 ) to.port = MSG_BigShort( PORT_SERVER );
	}

	NET_SendPacket( NS_CLIENT, Q_strlen( message ) + 1, message, to );
}


/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState( void )
{
	int	i;

	CL_ClearResourceLists();

	for( i = 0; i < MAX_CLIENTS; i++ )
		COM_ClearCustomizationList( &cl.players[i].customdata, false );

	S_StopAllSounds ( true );
	CL_ClearEffects ();
	CL_FreeEdicts ();

	PM_ClearPhysEnts( clgame.pmove );
	NetAPI_CancelAllRequests();

	// wipe the entire cl structure
	memset( &cl, 0, sizeof( cl ));
	MSG_Clear( &cls.netchan.message );
	memset( &clgame.fade, 0, sizeof( clgame.fade ));
	memset( &clgame.shake, 0, sizeof( clgame.shake ));
	clgame.mapname[0] = '\0';
	Cvar_FullSet( "cl_background", "0", FCVAR_READ_ONLY );
	cl.maxclients = 1; // allow to drawing player in menu
	cl.mtime[0] = cl.mtime[1] = 1.0f; // because level starts from 1.0f second
	cls.signon = 0;

	cl.resourcesneeded.pNext = cl.resourcesneeded.pPrev = &cl.resourcesneeded;
	cl.resourcesonhand.pNext = cl.resourcesonhand.pPrev = &cl.resourcesonhand;

	CL_CreateResourceList();
	CL_ClearSpriteTextures();	// now all hud sprites are invalid

	cl.local.interp_amount = 0.1f;
	cl.local.scr_fov = 90.0f;

	Cvar_SetValue( "scr_download", -1.0f );
	Cvar_SetValue( "scr_loading", 0.0f );
	host.allow_console = host.allow_console_init;
	HTTP_ClearCustomServers();
}

/*
=====================
CL_SendDisconnectMessage

Sends a disconnect message to the server
=====================
*/
void CL_SendDisconnectMessage( void )
{
	sizebuf_t	buf;
	byte	data[32];

	if( cls.state == ca_disconnected ) return;

	MSG_Init( &buf, "LastMessage", data, sizeof( data ));
	MSG_BeginClientCmd( &buf, clc_stringcmd );
	MSG_WriteString( &buf, "disconnect" );

	if( !cls.netchan.remote_address.type )
		cls.netchan.remote_address.type = NA_LOOPBACK;

	// make sure message will be delivered
	Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
	Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
	Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
}

int CL_GetSplitSize( void )
{
	int splitsize;

	if( Host_IsDedicated() )
		return 0;

	if( !(cls.extensions & NET_EXT_SPLITSIZE) )
		return 1400;

	splitsize = cl_dlmax->value;

	if( splitsize < FRAGMENT_MIN_SIZE || splitsize > FRAGMENT_MAX_SIZE )
		Cvar_SetValue( "cl_dlmax", FRAGMENT_DEFAULT_SIZE );

	return cl_dlmax->value;
}

/*
=====================
CL_Reconnect

build a request to reconnect client
=====================
*/
void CL_Reconnect( qboolean setup_netchan )
{
	if( setup_netchan )
	{
		Netchan_Setup( NS_CLIENT, &cls.netchan, net_from, Cvar_VariableInteger( "net_qport" ), NULL, CL_GetFragmentSize );

		if( cls.legacymode )
		{
			unsigned int extensions = Q_atoi( Cmd_Argv( 1 ) );

			if( extensions & NET_LEGACY_EXT_SPLIT )
			{
				// only enable incoming split for legacy mode
				cls.netchan.split = true;
				Con_Reportf( "^2NET_EXT_SPLIT enabled^7 (packet sizes is %d/%d)\n", (int)cl_dlmax->value, 65536 );
			}
		}
		else
		{
			cls.extensions = Q_atoi( Info_ValueForKey( Cmd_Argv( 1 ), "ext" ));

			if( cls.extensions & NET_EXT_SPLITSIZE )
			{
				Con_Reportf( "^2NET_EXT_SPLITSIZE enabled^7 (packet size is %d)\n", (int)cl_dlmax->value );
			}
		}

	}
	else
	{
		// clear channel and stuff
		Netchan_Clear( &cls.netchan );
		MSG_Clear( &cls.netchan.message );
	}

	cls.demonum = cls.movienum = -1;	// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;

	CL_ServerCommand( true, "new" );

	cl.validsequence = 0;		// haven't gotten a valid frame update yet
	cl.delta_sequence = -1;		// we'll request a full delta from the baseline
	cls.lastoutgoingcommand = -1;		// we don't have a backed up cmd history yet
	cls.nextcmdtime = host.realtime;	// we can send a cmd right away
	cl.last_command_ack = -1;

	CL_StartupDemoHeader ();
}

/*
=====================
CL_Disconnect

Goes from a connected state to full screen console state
Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( void )
{
	cls.legacymode = false;

	if( cls.state == ca_disconnected )
		return;

	cls.connect_time = 0;
	cls.changedemo = false;
	cls.max_fragment_size = FRAGMENT_MAX_SIZE; // reset fragment size
	Voice_Disconnect();
	CL_Stop_f();

	// send a disconnect message to the server
	CL_SendDisconnectMessage();
	CL_ClearState ();

	S_StopBackgroundTrack ();
	SCR_EndLoadingPlaque (); // get rid of loading plaque

	// clear the network channel, too.
	Netchan_Clear( &cls.netchan );

	IN_LockInputDevices( false ); // unlock input devices

	cls.state = ca_disconnected;
	memset( &cls.serveradr, 0, sizeof( cls.serveradr ) );
	cls.set_lastdemo = false;
	cls.connect_retry = 0;
	cls.signon = 0;

	// back to menu in non-developer mode
	if( host_developer.value || CL_IsInMenu( ))
		return;

	UI_SetActiveMenu( true );
}

void CL_Disconnect_f( void )
{
	if( Host_IsLocalClient( ))
		Host_EndGame( true, "disconnected from server\n" );
	else CL_Disconnect();
}

void CL_Crashed( void )
{
	// already freed
	if( host.status == HOST_CRASHED ) return;
	if( host.type != HOST_NORMAL ) return;
	if( !cls.initialized ) return;

	host.status = HOST_CRASHED;

	CL_Stop_f(); // stop any demos

	// send a disconnect message to the server
	CL_SendDisconnectMessage();

	Host_WriteOpenGLConfig();
	Host_WriteConfig();	// write config
}

/*
=================
CL_LocalServers_f
=================
*/
void CL_LocalServers_f( void )
{
	netadr_t	adr;

	Con_Printf( "Scanning for servers on the local network area...\n" );
	NET_Config( true, true ); // allow remote

	// send a broadcast packet
	adr.type = NA_BROADCAST;
	adr.port = MSG_BigShort( PORT_SERVER );
	Netchan_OutOfBandPrint( NS_CLIENT, adr, "info %i", PROTOCOL_VERSION );

	adr.type = NA_MULTICAST_IP6;
	Netchan_OutOfBandPrint( NS_CLIENT, adr, "info %i", PROTOCOL_VERSION );
}

/*
=================
CL_BuildMasterServerScanRequest
=================
*/
size_t CL_BuildMasterServerScanRequest( char *buf, size_t size, qboolean nat )
{
	size_t remaining;
	char *info;

	if( unlikely( size < sizeof( MS_SCAN_REQUEST )))
		return 0;

	Q_strncpy( buf, MS_SCAN_REQUEST, size );

	info = buf + sizeof( MS_SCAN_REQUEST ) - 1;
	remaining = size - sizeof( MS_SCAN_REQUEST );

	info[0] = 0;

	Info_SetValueForKey( info, "gamedir", GI->gamefolder, remaining );
	Info_SetValueForKey( info, "clver", XASH_VERSION, remaining ); // let master know about client version
	Info_SetValueForKey( info, "nat", nat ? "1" : "0", remaining );

	return sizeof( MS_SCAN_REQUEST ) + Q_strlen( info );
}

/*
=================
CL_InternetServers_f
=================
*/
void CL_InternetServers_f( void )
{
	char	fullquery[512];
	size_t len;
	qboolean nat = cl_nat->value != 0.0f;

	len = CL_BuildMasterServerScanRequest( fullquery, sizeof( fullquery ), nat );

	Con_Printf( "Scanning for servers on the internet area...\n" );

	NET_Config( true, true ); // allow remote

	cls.internetservers_wait = NET_SendToMasters( NS_CLIENT, len, fullquery );
	cls.internetservers_pending = true;

	if( !cls.internetservers_wait )
	{
		// now we clearing the vgui request
		if( clgame.master_request != NULL )
			memset( clgame.master_request, 0, sizeof( net_request_t ));
		clgame.request_type = NET_REQUEST_GAMEUI;
	}
}

/*
=================
CL_Reconnect_f

The server is changing levels
=================
*/
void CL_Reconnect_f( void )
{
	if( cls.state == ca_disconnected )
		return;

	S_StopAllSounds ( true );

	if( cls.state == ca_connected )
	{
		CL_Reconnect( false );
		return;
	}

	if( COM_CheckString( cls.servername ))
	{
		qboolean legacy = cls.legacymode;

		if( cls.state >= ca_connected )
			CL_Disconnect();

		cls.connect_time = MAX_HEARTBEAT;	// fire immediately
		cls.demonum = cls.movienum = -1;	// not in the demo loop now
		cls.state = ca_connecting;
		cls.signon = 0;
		cls.legacymode = legacy; // don't change protocol

		Con_Printf( "reconnecting...\n" );
	}
}

/*
=================
CL_FixupColorStringsForInfoString

all the keys and values must be ends with ^7
=================
*/
void CL_FixupColorStringsForInfoString( const char *in, char *out )
{
	qboolean	hasPrefix = false;
	qboolean	endOfKeyVal = false;
	int	color = 7;
	int	count = 0;

	if( *in == '\\' )
	{
		*out++ = *in++;
		count++;
	}

	while( *in && count < MAX_INFO_STRING )
	{
		if( IsColorString( in ))
			color = ColorIndex( *(in+1));

		// color the not reset while end of key (or value) was found!
		if( *in == '\\' && color != 7 )
		{
			if( IsColorString( out - 2 ))
			{
				*(out - 1) = '7';
			}
			else
			{
				*out++ = '^';
				*out++ = '7';
				count += 2;
			}
			color = 7;
		}

		*out++ = *in++;
		count++;
	}

	// check the remaining value
	if( color != 7 )
	{
		// if the ends with another color rewrite it
		if( IsColorString( out - 2 ))
		{
			*(out - 1) = '7';
		}
		else
		{
			*out++ = '^';
			*out++ = '7';
			count += 2;
		}
	}

	*out = '\0';
}

/*
=================
CL_ParseStatusMessage

Handle a reply from a info
=================
*/
void CL_ParseStatusMessage( netadr_t from, sizebuf_t *msg )
{
	static char	infostring[MAX_INFO_STRING+8];
	char		*s = MSG_ReadString( msg );
	int i;
	const char *magic = ": wrong version\n";
	size_t len = Q_strlen( s ), magiclen = Q_strlen( magic );

	if( len >= magiclen && !Q_strcmp( s + len - magiclen, magic ))
	{
		Netchan_OutOfBandPrint( NS_CLIENT, from, "info %i", PROTOCOL_LEGACY_VERSION );
		return;
	}

	if( !Info_IsValid( s ))
	{
		Con_Printf( "^1Server^7: %s, invalid infostring\n", NET_AdrToString( from ));
		return;
	}

	CL_FixupColorStringsForInfoString( s, infostring );

	if( !COM_CheckString( Info_ValueForKey( infostring, "gamedir" )))
	{
		Con_Printf( "^1Server^7: %s, Info: %s\n", NET_AdrToString( from ), infostring );
		return; // unsupported proto
	}

	if( !COM_CheckString( Info_ValueForKey( infostring, "p" )))
	{
		Info_SetValueForKey( infostring, "legacy", "1", sizeof( infostring ) );
		Con_Printf( "^3Server^7: %s, Game: %s\n", NET_AdrToString( from ), Info_ValueForKey( infostring, "gamedir" ));
	}
	else
	{
		// more info about servers
		Con_Printf( "^2Server^7: %s, Game: %s\n", NET_AdrToString( from ), Info_ValueForKey( infostring, "gamedir" ));
	}

	UI_AddServerToList( from, infostring );
}

/*
=================
CL_ParseNETInfoMessage

Handle a reply from a netinfo
=================
*/
void CL_ParseNETInfoMessage( netadr_t from, sizebuf_t *msg, const char *s )
{
	net_request_t	*nr;
	static char	infostring[MAX_INFO_STRING+8];
	int		i, context, type;
	int		errorBits = 0;
	const char		*val;

	context = Q_atoi( Cmd_Argv( 1 ));
	type = Q_atoi( Cmd_Argv( 2 ));
	while( *s != '\\' ) s++; // fetching infostring

	// check for errors
	val = Info_ValueForKey( s, "neterror" );

	if( !Q_stricmp( val, "protocol" ))
		SetBits( errorBits, NET_ERROR_PROTO_UNSUPPORTED );
	else if( !Q_stricmp( val, "undefined" ))
		SetBits( errorBits, NET_ERROR_UNDEFINED );

	CL_FixupColorStringsForInfoString( s, infostring );

	// find a request with specified context
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		nr = &clgame.net_requests[i];

		if( nr->resp.context == context && nr->resp.type == type )
		{
			// setup the answer
			nr->resp.response = infostring;
			nr->resp.remote_address = from;
			nr->resp.error = NET_SUCCESS;
			nr->resp.ping = host.realtime - nr->timesend;

			if( nr->timeout <= host.realtime )
				SetBits( nr->resp.error, NET_ERROR_TIMEOUT );
			SetBits( nr->resp.error, errorBits ); // misc error bits

			nr->pfnFunc( &nr->resp );

			if( !FBitSet( nr->flags, FNETAPI_MULTIPLE_RESPONSE ))
				memset( nr, 0, sizeof( *nr )); // done
			return;
		}
	}
}

/*
=================
CL_ProcessNetRequests

check for timeouts
=================
*/
void CL_ProcessNetRequests( void )
{
	net_request_t	*nr;
	int		i;

	// find a request with specified context
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		nr = &clgame.net_requests[i];
		if( !nr->pfnFunc ) continue;	// not used

		if( nr->timeout <= host.realtime )
		{
			// setup the answer
			SetBits( nr->resp.error, NET_ERROR_TIMEOUT );
			nr->resp.ping = host.realtime - nr->timesend;

			nr->pfnFunc( &nr->resp );
			memset( nr, 0, sizeof( *nr )); // done
		}
	}
}

//===================================================================
/*
===============
CL_SetupOverviewParams

Get initial overview values
===============
*/
void CL_SetupOverviewParams( void )
{
	ref_overview_t	*ov = &clgame.overView;
	float		mapAspect, screenAspect, aspect;

	ov->rotated = ( world.size[1] <= world.size[0] ) ? true : false;

	// calculate nearest aspect
	mapAspect = world.size[!ov->rotated] / world.size[ov->rotated];
	screenAspect = (float)refState.width / (float)refState.height;
	aspect = Q_max( mapAspect, screenAspect );

	ov->zNear = world.maxs[2];
	ov->zFar = world.mins[2];
	ov->flZoom = ( 8192.0f / world.size[ov->rotated] ) / aspect;

	VectorAverage( world.mins, world.maxs, ov->origin );

	memset( &cls.spectator_state, 0, sizeof( cls.spectator_state ));

	if( cls.spectator )
	{
		cls.spectator_state.playerstate.friction = 1;
		cls.spectator_state.playerstate.gravity = 1;
		cls.spectator_state.playerstate.number = cl.playernum + 1;
		cls.spectator_state.playerstate.usehull = 1;
		cls.spectator_state.playerstate.movetype = MOVETYPE_NOCLIP;
		cls.spectator_state.client.maxspeed = clgame.movevars.spectatormaxspeed;
	}
}

/*
=================
CL_IsFromConnectingServer

Used for connectionless packets, when netchan may not be ready.
=================
*/
static qboolean CL_IsFromConnectingServer( netadr_t from )
{
	return NET_IsLocalAddress( from ) ||
		NET_CompareAdr( cls.serveradr, from );
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
void CL_ConnectionlessPacket( netadr_t from, sizebuf_t *msg )
{
	char	*args;
	const char	*c;
	char	buf[MAX_SYSPATH];
	int	len = sizeof( buf );
	int	dataoffset = 0;
	netadr_t	servadr;

	MSG_Clear( msg );
	MSG_ReadLong( msg ); // skip the -1

	args = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( args );
	c = Cmd_Argv( 0 );

	Con_Reportf( "CL_ConnectionlessPacket: %s : %s\n", NET_AdrToString( from ), c );

	// server connection
	if( !Q_strcmp( c, "client_connect" ))
	{
		if( !CL_IsFromConnectingServer( from ))
			return;

		if( cls.state == ca_connected )
		{
			Con_DPrintf( S_ERROR "dup connect received. ignored\n");
			return;
		}

		CL_Reconnect( true );
		UI_SetActiveMenu( cl.background );
	}
	else if( !Q_strcmp( c, "info" ))
	{
		// server responding to a status broadcast
		CL_ParseStatusMessage( from, msg );
	}
	else if( !Q_strcmp( c, "netinfo" ))
	{
		// server responding to a status broadcast
		CL_ParseNETInfoMessage( from, msg, args );
	}
	else if( !Q_strcmp( c, "cmd" ))
	{
		// remote command from gui front end
		if( !NET_IsLocalAddress( from ))
		{
			Con_Printf( "Command packet from remote host. Ignored.\n" );
			return;
		}

#if XASH_SDL == 2
		SDL_ShowWindow( host.hWnd );
#endif
		args = MSG_ReadString( msg );
		Cbuf_AddText( args );
		Cbuf_AddText( "\n" );
	}
	else if( !Q_strcmp( c, "print" ))
	{
		// print command from somewhere
		Con_Printf( "%s", MSG_ReadString( msg ));
	}
	else if( !Q_strcmp( c, "testpacket" ))
	{
		byte	recv_buf[NET_MAX_FRAGMENT];
		dword	crcValue;
		int	realsize;
		dword	crcValue2 = 0;

		if( !CL_IsFromConnectingServer( from ))
			return;

		crcValue = MSG_ReadLong( msg );
		realsize = MSG_GetMaxBytes( msg ) - MSG_GetNumBytesRead( msg );

		if( cls.max_fragment_size != MSG_GetMaxBytes( msg ))
		{
			if( cls.connect_retry >= CL_TEST_RETRIES )
			{
				// too many fails use default connection method
				Con_Printf( "hi-speed connection is failed, use default method\n" );
				Netchan_OutOfBandPrint( NS_CLIENT, from, "getchallenge\n" );
				Cvar_SetValue( "cl_dlmax", FRAGMENT_DEFAULT_SIZE );
				cls.connect_time = host.realtime;
				return;
			}

			// if we waiting more than cl_timeout or packet was trashed
			cls.connect_time = MAX_HEARTBEAT;
			return; // just wait for a next responce
		}

		// reading test buffer
		MSG_ReadBytes( msg, recv_buf, realsize );

		// procssing the CRC
		CRC32_ProcessBuffer( &crcValue2, recv_buf, realsize );

		if( crcValue == crcValue2 )
		{
			// packet was sucessfully delivered, adjust the fragment size and get challenge

			Con_DPrintf( "CRC %x is matched, get challenge, fragment size %d\n", crcValue, cls.max_fragment_size );
			Netchan_OutOfBandPrint( NS_CLIENT, from, "getchallenge\n" );
			Cvar_SetValue( "cl_dlmax", cls.max_fragment_size );
			cls.connect_time = host.realtime;
		}
		else
		{
			if( cls.connect_retry >= CL_TEST_RETRIES )
			{
				// too many fails use default connection method
				Con_Printf( "hi-speed connection is failed, use default method\n" );
				Netchan_OutOfBandPrint( NS_CLIENT, from, "getchallenge\n" );
				Cvar_SetValue( "cl_dlmax", FRAGMENT_MIN_SIZE );
				cls.connect_time = host.realtime;
				return;
			}

			Msg( "got testpacket, CRC mismatched 0x%08x should be 0x%08x, trying next fragment size %d\n", crcValue2, crcValue, cls.max_fragment_size >> 1 );

			// trying the next size of packet
			cls.connect_time = MAX_HEARTBEAT;
		}
	}
	else if( !Q_strcmp( c, "ping" ))
	{
		// ping from somewhere
		Netchan_OutOfBandPrint( NS_CLIENT, from, "ack" );
	}
	else if( !Q_strcmp( c, "challenge" ))
	{
		if( !CL_IsFromConnectingServer( from ))
			return;

		// challenge from the server we are connecting to
		cls.challenge = Q_atoi( Cmd_Argv( 1 ));
		CL_SendConnectPacket();
		return;
	}
	else if( !Q_strcmp( c, "echo" ))
	{
		if( !CL_IsFromConnectingServer( from ))
			return;

		// echo request from server
		Netchan_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv( 1 ));
	}
	else if( !Q_strcmp( c, "disconnect" ))
	{
		if( !CL_IsFromConnectingServer( from ))
			return;

		// a disconnect message from the server, which will happen if the server
		// dropped the connection but it is still getting packets from us
		CL_Disconnect_f();

		if( NET_CompareAdr( from, cls.legacyserver ))
		{
			Cbuf_AddTextf( "connect %s legacy\n", NET_AdrToString( from ));
			memset( &cls.legacyserver, 0, sizeof( cls.legacyserver ));
		}
	}
	else if( !Q_strcmp( c, "errormsg" ))
	{
		if( !CL_IsFromConnectingServer( from ))
			return;

		args = MSG_ReadString( msg );

		if( !Q_strcmp( args, "Server uses protocol version 48.\n" ))
		{
			cls.legacyserver = from;
		}
		else
		{
			if( UI_IsVisible() )
				UI_ShowMessageBox( va("^3Server message^7\n%s", args ) );
			Msg( "%s", args );
		}
	}
	else if( !Q_strcmp( c, "updatemsg" ))
	{
		// got an update message from master server
		// show update dialog from menu
		netadr_t adr;
		qboolean preferStore = true;

		if( !Q_strcmp( Cmd_Argv( 1 ), "nostore" ) )
			preferStore = false;

		// trust only hardcoded master server
		if( NET_StringToAdr( MASTERSERVER_ADR, &adr ) )
		{
			if( NET_CompareAdr( from, adr ))
			{
				UI_ShowUpdateDialog( preferStore );
			}
		}
		else
		{
			// in case we don't have master anymore
			UI_ShowUpdateDialog( preferStore );
		}
	}
	else if( !Q_strcmp( c, "f" ))
	{
		if( !NET_IsMasterAdr( from ))
		{
			Con_Printf( S_WARN "unexpected server list packet from %s\n", NET_AdrToString( from ));
			return;
		}

		// serverlist got from masterserver
		while( MSG_GetNumBitsLeft( msg ) > 8 )
		{
			MSG_ReadBytes( msg, servadr.ip, sizeof( servadr.ip ));	// 4 bytes for IP
			servadr.port = MSG_ReadShort( msg );			// 2 bytes for Port
			servadr.type = NA_IP;

			// list is ends here
			if( !servadr.port )
			{
				if( clgame.request_type == NET_REQUEST_CLIENT && clgame.master_request != NULL )
				{
					net_request_t	*nr = clgame.master_request;
					net_adrlist_t	*list, **prev;

					// setup the answer
					nr->resp.remote_address = from;
					nr->resp.error = NET_SUCCESS;
					nr->resp.ping = host.realtime - nr->timesend;

					if( nr->timeout <= host.realtime )
						SetBits( nr->resp.error, NET_ERROR_TIMEOUT );

					Con_Printf( "serverlist call: %s\n", NET_AdrToString( from ));
					nr->pfnFunc( &nr->resp );

					// throw the list, now it will be stored in user area
					prev = (net_adrlist_t**)&nr->resp.response;

					while( 1 )
					{
						list = *prev;
						if( !list ) break;

						// throw out any variables the game created
						*prev = list->next;
						Mem_Free( list );
					}
					memset( nr, 0, sizeof( *nr )); // done
					clgame.request_type = NET_REQUEST_CANCEL;
					clgame.master_request = NULL;
				}
				break;
			}

			if( clgame.request_type == NET_REQUEST_CLIENT && clgame.master_request != NULL )
			{
				net_request_t	*nr = clgame.master_request;
				net_adrlist_t	*list;

				// adding addresses into list
				list = Z_Malloc( sizeof( *list ));
				list->remote_address = servadr;
				list->next = nr->resp.response;
				nr->resp.response = list;
			}
			else if( clgame.request_type == NET_REQUEST_GAMEUI )
			{
				NET_Config( true, false ); // allow remote
				Netchan_OutOfBandPrint( NS_CLIENT, servadr, "info %i", PROTOCOL_VERSION );
			}
		}

		if( cls.internetservers_pending )
		{
			UI_ResetPing();
			cls.internetservers_pending = false;
		}
	}
	else if( clgame.dllFuncs.pfnConnectionlessPacket( &from, args, buf, &len ))
	{
		// user out of band message (must be handled in CL_ConnectionlessPacket)
		if( len > 0 ) Netchan_OutOfBand( NS_SERVER, from, len, (byte *)buf );
	}
	else Con_DPrintf( S_ERROR "bad connectionless packet from %s:\n%s\n", NET_AdrToString( from ), args );
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int CL_GetMessage( byte *data, size_t *length )
{
	if( cls.demoplayback )
	{
		if( CL_DemoReadMessage( data, length ))
			return true;
		return false;
	}

	if( NET_GetPacket( NS_CLIENT, &net_from, data, length ))
		return true;
	return false;
}

/*
=================
CL_ReadNetMessage
=================
*/
void CL_ReadNetMessage( void )
{
	size_t	curSize;

	while( CL_GetMessage( net_message_buffer, &curSize ))
	{
		if( cls.legacymode && *((int *)&net_message_buffer) == 0xFFFFFFFE )
			// Will rewrite existing packet by merged
			if( !NetSplit_GetLong( &cls.netchan.netsplit, &net_from, net_message_buffer, &curSize ) )
				continue;

		MSG_Init( &net_message, "ServerData", net_message_buffer, curSize );

		// check for connectionless packet (0xffffffff) first
		if( MSG_GetMaxBytes( &net_message ) >= 4 && *(int *)net_message.pData == -1 )
		{
			CL_ConnectionlessPacket( net_from, &net_message );
			continue;
		}

		// can't be a valid sequenced packet
		if( cls.state < ca_connected ) continue;

		if( !cls.demoplayback && MSG_GetMaxBytes( &net_message ) < 8 )
		{
			Con_Printf( S_WARN "CL_ReadPackets: %s:runt packet\n", NET_AdrToString( net_from ));
			continue;
		}

		// packet from server
		if( !cls.demoplayback && !NET_CompareAdr( net_from, cls.netchan.remote_address ))
		{
			Con_DPrintf( S_ERROR "CL_ReadPackets: %s:sequenced packet without connection\n", NET_AdrToString( net_from ));
			continue;
		}

		if( !cls.demoplayback && !Netchan_Process( &cls.netchan, &net_message ))
			continue;	// wasn't accepted for some reason

		// run special handler for quake demos
		if( cls.demoplayback == DEMO_QUAKE1 )
			CL_ParseQuakeMessage( &net_message, true );
		else if( cls.legacymode ) CL_ParseLegacyServerMessage( &net_message, true );
		else CL_ParseServerMessage( &net_message, true );
		cl.send_reply = true;
	}

	// build list of all solid entities per next frame (exclude clients)
	CL_SetSolidEntities();

	// check for fragmentation/reassembly related packets.
	if( cls.state != ca_disconnected && Netchan_IncomingReady( &cls.netchan ))
	{
		// process the incoming buffer(s)
		if( Netchan_CopyNormalFragments( &cls.netchan, &net_message, &curSize ))
		{
			MSG_Init( &net_message, "ServerData", net_message_buffer, curSize );
			CL_ParseServerMessage( &net_message, false );
		}

		if( Netchan_CopyFileFragments( &cls.netchan, &net_message ))
		{
			// remove from resource request stuff.
			CL_ProcessFile( true, cls.netchan.incomingfilename );
		}
	}

	Netchan_UpdateProgress( &cls.netchan );

	// check requests for time-expire
	CL_ProcessNetRequests();
}

/*
=================
CL_ReadPackets

Updates the local time and reads/handles messages
on client net connection.
=================
*/
void CL_ReadPackets( void )
{
	// decide the simulation time
	cl.oldtime = cl.time;

	if( cls.demoplayback != DEMO_XASH3D && !cl.paused )
		cl.time += host.frametime;

	// demo time
	if( cls.demorecording && !cls.demowaiting )
		cls.demotime += host.frametime;

	CL_ReadNetMessage();

	CL_ApplyAddAngle();
#if 0
	// keep cheat cvars are unchanged
	if( cl.maxclients > 1 && cls.state == ca_active && !host_developer.value )
		Cvar_SetCheatState();
#endif
	// hot precache and downloading resources
	if( cls.signon == SIGNONS && cl.lastresourcecheck < host.realtime )
	{
		double checktime = Host_IsLocalGame() ? 0.1 : 1.0;

		if( !cls.dl.custom && cl.resourcesneeded.pNext != &cl.resourcesneeded )
		{
			// check resource for downloading and precache
			CL_EstimateNeededResources();
			CL_BatchResourceRequest( false );
			cls.dl.doneregistering = false;
			cls.dl.custom = true;
		}

		cl.lastresourcecheck = host.realtime + checktime;
	}

	// singleplayer never has connection timeout
	if( NET_IsLocalAddress( cls.netchan.remote_address ))
		return;

	// if in the debugger last frame, don't timeout
	if( host.frametime > 5.0f ) cls.netchan.last_received = Sys_DoubleTime();

	// check timeout
	if( cls.state >= ca_connected && cls.state != ca_cinematic && !cls.demoplayback )
	{
		if( host.realtime - cls.netchan.last_received > cl_timeout->value )
		{
			Con_Printf( "\nServer connection timed out.\n" );
			CL_Disconnect();
			return;
		}
	}

}

/*
====================
CL_CleanFileName

Replace the displayed name for some resources
====================
*/
const char *CL_CleanFileName( const char *filename )
{
	if( COM_CheckString( filename ) && filename[0] == '!' )
		return "customization";

	return filename;
}


/*
====================
CL_RegisterCustomization

register custom resource for player
====================
*/
void CL_RegisterCustomization( resource_t *resource )
{
	qboolean		bFound = false;
	customization_t	*pList;

	for( pList = cl.players[resource->playernum].customdata.pNext; pList; pList = pList->pNext )
	{
		if( !memcmp( pList->resource.rgucMD5_hash, resource->rgucMD5_hash, 16 ))
		{
			bFound = true;
			break;
		}
	}

	if( !bFound )
	{
		player_info_t	*player =  &cl.players[resource->playernum];

		if( !COM_CreateCustomization( &player->customdata, resource, resource->playernum, FCUST_FROMHPAK, NULL, NULL ))
			Con_Printf( "Unable to create custom decal for player %i\n", resource->playernum );
	}
	else
	{
		Con_DPrintf( "Duplicate resource received and ignored.\n" );
	}
}

/*
====================
CL_ProcessFile

A file has been received via the fragmentation/reassembly layer, put it in the right spot and
 see if we have finished downloading files.
====================
*/
void CL_ProcessFile( qboolean successfully_received, const char *filename )
{
	int		sound_len = sizeof( DEFAULT_SOUNDPATH ) - 1;
	byte		rgucMD5_hash[16];
	const char	*pfilename;
	resource_t	*p;

	if( COM_CheckString( filename ) && successfully_received )
	{
		if( filename[0] != '!' )
			Con_Printf( "processing %s\n", filename );

		if( !Q_strnicmp( filename, "downloaded/", 11 ))
		{
			// skip "downloaded/" part to avoid mismatch with needed resources list
			filename += 11; 
		}
	}
	else if( !successfully_received )
	{
		Con_Printf( S_ERROR "server failed to transmit file '%s'\n", CL_CleanFileName( filename ));
	}

	if( cls.legacymode )
	{
		if( host.downloadcount > 0 )
			host.downloadcount--;
		if( !host.downloadcount )
		{
			MSG_WriteByte( &cls.netchan.message, clc_stringcmd );
			MSG_WriteString( &cls.netchan.message, "continueloading" );
		}
		return;
	}

	pfilename = filename;

	if( !Q_strnicmp( filename, DEFAULT_SOUNDPATH, sound_len ))
		pfilename += sound_len;

	for( p = cl.resourcesneeded.pNext; p != &cl.resourcesneeded; p = p->pNext )
	{
		if( !Q_strnicmp( filename, "!MD5", 4 ))
		{
			COM_HexConvert( filename + 4, 32, rgucMD5_hash );

			if( !memcmp( p->rgucMD5_hash, rgucMD5_hash, 16 ))
				break;
		}
		else
		{
			if( p->type == t_generic )
			{
				if( !Q_stricmp( p->szFileName, filename ))
					break;
			}
			else
			{
				if( !Q_stricmp( p->szFileName, pfilename ))
					break;
			}
		}
	}

	if( p != &cl.resourcesneeded )
	{
		if( successfully_received )
			ClearBits( p->ucFlags, RES_WASMISSING );

		if( filename[0] == '!' )
		{
			if( cls.netchan.tempbuffer )
			{
				if( p->nDownloadSize == cls.netchan.tempbuffersize )
				{
					if( p->ucFlags & RES_CUSTOM )
					{
						HPAK_AddLump( true, CUSTOM_RES_PATH, p, cls.netchan.tempbuffer, NULL );
						CL_RegisterCustomization( p );
					}
				}
				else
				{
					Con_Printf( "Downloaded %i bytes for purported %i byte file, ignoring download\n",
					cls.netchan.tempbuffersize, p->nDownloadSize );
				}

				if( cls.netchan.tempbuffer )
					Mem_Free( cls.netchan.tempbuffer );
			}

			cls.netchan.tempbuffersize = 0;
			cls.netchan.tempbuffer = NULL;
		}

		// moving to 'onhandle' list even if file was missed
		CL_MoveToOnHandList( p );
	}

	if( cls.state != ca_disconnected )
	{
		host.downloadcount = 0;

		for( p = cl.resourcesneeded.pNext; p != &cl.resourcesneeded; p = p->pNext )
			host.downloadcount++;

		if( cl.resourcesneeded.pNext == &cl.resourcesneeded )
		{
			byte	msg_buf[MAX_INIT_MSG];
			sizebuf_t msg;

			MSG_Init( &msg, "Resource Registration", msg_buf, sizeof( msg_buf ));

			if( CL_PrecacheResources( ))
				CL_RegisterResources( &msg );

			if( MSG_GetNumBytesWritten( &msg ) > 0 )
			{
				Netchan_CreateFragments( &cls.netchan, &msg );
				Netchan_FragSend( &cls.netchan );
			}
		}

		if( cls.netchan.tempbuffer )
		{
			Con_Printf( "Received a decal %s, but didn't find it in resources needed list!\n", pfilename );
			Mem_Free( cls.netchan.tempbuffer );
		}

		cls.netchan.tempbuffer = NULL;
		cls.netchan.tempbuffersize = 0;
	}
}

/*
====================
CL_ServerCommand

send command to a server
====================
*/
void CL_ServerCommand( qboolean reliable, const char *fmt, ... )
{
	char		string[MAX_SYSPATH];
	va_list		argptr;

	if( cls.state < ca_connecting )
		return;

	va_start( argptr, fmt );
	Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	if( reliable )
	{
		MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
		MSG_WriteString( &cls.netchan.message, string );
	}
	else
	{
		MSG_BeginClientCmd( &cls.datagram, clc_stringcmd );
		MSG_WriteString( &cls.datagram, string );
	}
}

//=============================================================================
/*
==============
CL_SetInfo_f
==============
*/
void CL_SetInfo_f( void )
{
	convar_t	*var;

	if( Cmd_Argc() == 1 )
	{
		Con_Printf( "User info settings:\n" );
		Info_Print( cls.userinfo );
		Con_Printf( "Total %i symbols\n", Q_strlen( cls.userinfo ));
		return;
	}

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "setinfo [ <key> <value> ]\n" );
		return;
	}

	// NOTE: some userinfo comed from cvars, e.g. cl_lw but we can call "setinfo cl_lw 1"
	// without real cvar changing. So we need to lookup for cvar first to make sure what
	// our key is not linked with console variable
	var = Cvar_FindVar( Cmd_Argv( 1 ));

	// make sure what cvar is existed and really part of userinfo
	if( var && FBitSet( var->flags, FCVAR_USERINFO ))
	{
		Cvar_DirectSet( var, Cmd_Argv( 2 ));
	}
	else if( Info_SetValueForKey( cls.userinfo, Cmd_Argv( 1 ), Cmd_Argv( 2 ), MAX_INFO_STRING ))
	{
		// send update only on successfully changed userinfo
		Cmd_ForwardToServer ();
	}
}

/*
==============
CL_Physinfo_f
==============
*/
void CL_Physinfo_f( void )
{
	Con_Printf( "Phys info settings:\n" );
	Info_Print( cls.physinfo );
	Con_Printf( "Total %i symbols\n", Q_strlen( cls.physinfo ));
}

qboolean CL_PrecacheResources( void )
{
	resource_t	*pRes;

	// NOTE: world need to be loaded as first model
	for( pRes = cl.resourcesonhand.pNext; pRes && pRes != &cl.resourcesonhand; pRes = pRes->pNext )
	{
		if( FBitSet( pRes->ucFlags, RES_PRECACHED ))
			continue;

		if( pRes->type != t_model || pRes->nIndex != WORLD_INDEX )
			continue;

		cl.models[pRes->nIndex] = Mod_LoadWorld( pRes->szFileName, true );
		SetBits( pRes->ucFlags, RES_PRECACHED );
		cl.nummodels = 1;
		break;
	}

	// then we set up all the world submodels
	for( pRes = cl.resourcesonhand.pNext; pRes && pRes != &cl.resourcesonhand; pRes = pRes->pNext )
	{
		if( FBitSet( pRes->ucFlags, RES_PRECACHED ))
			continue;

		if( pRes->type == t_model && pRes->szFileName[0] == '*' )
		{
			cl.models[pRes->nIndex] = Mod_ForName( pRes->szFileName, false, false );
			cl.nummodels = Q_max( cl.nummodels, pRes->nIndex + 1 );
			SetBits( pRes->ucFlags, RES_PRECACHED );

			if( cl.models[pRes->nIndex] == NULL )
			{
				Con_Printf( S_ERROR "submodel %s not found\n", pRes->szFileName );

				if( FBitSet( pRes->ucFlags, RES_FATALIFMISSING ))
				{
					CL_Disconnect_f();
					return false;
				}
			}
		}
	}

	if( cls.state != ca_active )
		S_BeginRegistration();

	// precache all the remaining resources where order is doesn't matter
	for( pRes = cl.resourcesonhand.pNext; pRes && pRes != &cl.resourcesonhand; pRes = pRes->pNext )
	{
		if( FBitSet( pRes->ucFlags, RES_PRECACHED ))
			continue;

		switch( pRes->type )
		{
		case t_sound:
			if( pRes->nIndex != -1 )
			{
				if( FBitSet( pRes->ucFlags, RES_WASMISSING ))
				{
					Con_Printf( S_ERROR "Could not load sound " DEFAULT_SOUNDPATH "%s\n", pRes->szFileName );
					cl.sound_precache[pRes->nIndex][0] = 0;
					cl.sound_index[pRes->nIndex] = 0;
				}
				else
				{
					Q_strncpy( cl.sound_precache[pRes->nIndex], pRes->szFileName, sizeof( cl.sound_precache[0] ));
					cl.sound_index[pRes->nIndex] = S_RegisterSound( pRes->szFileName );

					if( !cl.sound_index[pRes->nIndex] )
					{
						if( FBitSet( pRes->ucFlags, RES_FATALIFMISSING ))
						{
							S_EndRegistration();
							CL_Disconnect_f();
							return false;
						}
					}
				}
			}
			else
			{
				// client sounds
				S_RegisterSound( pRes->szFileName );
			}
			break;
		case t_skin:
			break;
		case t_model:
			cl.nummodels = Q_max( cl.nummodels, pRes->nIndex + 1 );
			if( pRes->szFileName[0] != '*' )
			{
				if( pRes->nIndex != -1 )
				{
					cl.models[pRes->nIndex] = Mod_ForName( pRes->szFileName, false, true );

					if( cl.models[pRes->nIndex] == NULL )
					{
						if( FBitSet( pRes->ucFlags, RES_FATALIFMISSING ))
						{
							S_EndRegistration();
							CL_Disconnect_f();
							return false;
						}
					}
				}
				else
				{
					CL_LoadClientSprite( pRes->szFileName );
				}
			}
			break;
		case t_decal:
			if( !FBitSet( pRes->ucFlags, RES_CUSTOM ))
				Q_strncpy( host.draw_decals[pRes->nIndex], pRes->szFileName, sizeof( host.draw_decals[0] ));
			break;
		case t_generic:
			Q_strncpy( cl.files_precache[pRes->nIndex], pRes->szFileName, sizeof( cl.files_precache[0] ));
			cl.numfiles = Q_max( cl.numfiles, pRes->nIndex + 1 );
			break;
		case t_eventscript:
			Q_strncpy( cl.event_precache[pRes->nIndex], pRes->szFileName, sizeof( cl.event_precache[0] ));
			CL_SetEventIndex( cl.event_precache[pRes->nIndex], pRes->nIndex );
			break;
		default:
			break;
		}

		SetBits( pRes->ucFlags, RES_PRECACHED );
	}

	// make sure modelcount is in-range
	cl.nummodels = bound( 0, cl.nummodels, MAX_MODELS );
	cl.numfiles = bound( 0, cl.numfiles, MAX_CUSTOM );

	if( cls.state != ca_active )
		S_EndRegistration();

	return true;
}

/*
==================
CL_FullServerinfo_f

Sent by server when serverinfo changes
==================
*/
void CL_FullServerinfo_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "fullserverinfo <complete info string>\n" );
		return;
	}

	Q_strncpy( cl.serverinfo, Cmd_Argv( 1 ), sizeof( cl.serverinfo ));
}

/*
=================
CL_Escape_f

Escape to menu from game
=================
*/
void CL_Escape_f( void )
{
	if( cls.key_dest == key_menu )
		return;

	// the final credits is running
	if( UI_CreditsActive( )) return;

	if( cls.state == ca_cinematic )
		SCR_NextMovie(); // jump to next movie
	else UI_SetActiveMenu( true );
}

/*
=================
CL_InitLocal
=================
*/
void CL_InitLocal( void )
{
	cls.state = ca_disconnected;
	cls.signon = 0;
	memset( &cls.serveradr, 0, sizeof( cls.serveradr ) );

	cl.resourcesneeded.pNext = cl.resourcesneeded.pPrev = &cl.resourcesneeded;
	cl.resourcesonhand.pNext = cl.resourcesonhand.pPrev = &cl.resourcesonhand;

	Cvar_RegisterVariable( &mp_decals );
	Cvar_RegisterVariable( &dev_overview );
	Cvar_RegisterVariable( &cl_resend );
	Cvar_RegisterVariable( &cl_allow_upload );
	Cvar_RegisterVariable( &cl_allow_download );
	Cvar_RegisterVariable( &cl_download_ingame );
	Cvar_RegisterVariable( &cl_logofile );
	Cvar_RegisterVariable( &cl_logocolor );
	Cvar_RegisterVariable( &cl_logoext );
	Cvar_RegisterVariable( &cl_test_bandwidth );

	Voice_RegisterCvars();
	VGui_RegisterCvars();

	// register our variables
	cl_crosshair = Cvar_Get( "crosshair", "1", FCVAR_ARCHIVE, "show weapon chrosshair" );
	cl_nodelta = Cvar_Get ("cl_nodelta", "0", 0, "disable delta-compression for server messages" );
	cl_idealpitchscale = Cvar_Get( "cl_idealpitchscale", "0.8", 0, "how much to look up/down slopes and stairs when not using freelook" );
	cl_solid_players = Cvar_Get( "cl_solid_players", "1", 0, "Make all players not solid (can't traceline them)" );
	cl_interp = Cvar_Get( "ex_interp", "0.1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "Interpolate object positions starting this many seconds in past" );
	cl_timeout = Cvar_Get( "cl_timeout", "60", 0, "connect timeout (in-seconds)" );
	cl_charset = Cvar_Get( "cl_charset", "utf-8", FCVAR_ARCHIVE, "1-byte charset to use (iconv style)" );
	hud_utf8 = Cvar_Get( "hud_utf8", "0", FCVAR_ARCHIVE, "Use utf-8 encoding for hud text" );

	rcon_address = Cvar_Get( "rcon_address", "", FCVAR_PRIVILEGED, "remote control address" );

	cl_trace_messages = Cvar_Get( "cl_trace_messages", "0", FCVAR_ARCHIVE|FCVAR_CHEAT, "enable message names tracing (good for developers)");

	// userinfo
	cl_nopred = Cvar_Get( "cl_nopred", "0", FCVAR_ARCHIVE|FCVAR_USERINFO, "disable client movement prediction" );
	name = Cvar_Get( "name", Sys_GetCurrentUser(), FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_PRINTABLEONLY, "player name" );
	model = Cvar_Get( "model", "", FCVAR_USERINFO|FCVAR_ARCHIVE, "player model ('player' is a singleplayer model)" );
	cl_updaterate = Cvar_Get( "cl_updaterate", "20", FCVAR_USERINFO|FCVAR_ARCHIVE, "refresh rate of server messages" );
	cl_dlmax = Cvar_Get( "cl_dlmax", "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "max allowed outcoming fragment size" );
	cl_upmax = Cvar_Get( "cl_upmax", "1200", FCVAR_ARCHIVE, "max allowed incoming fragment size" );
	cl_nat = Cvar_Get( "cl_nat", "0", 0, "show servers running under NAT" );
	rate = Cvar_Get( "rate", "3500", FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_FILTERABLE, "player network rate" );
	topcolor = Cvar_Get( "topcolor", "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "player top color" );
	bottomcolor = Cvar_Get( "bottomcolor", "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "player bottom color" );
	cl_lw = Cvar_Get( "cl_lw", "1", FCVAR_ARCHIVE|FCVAR_USERINFO, "enable client weapon predicting" );
	Cvar_Get( "cl_lc", "1", FCVAR_ARCHIVE|FCVAR_USERINFO, "enable lag compensation" );
	Cvar_Get( "password", "", FCVAR_USERINFO, "server password" );
	Cvar_Get( "team", "", FCVAR_USERINFO, "player team" );
	Cvar_Get( "skin", "", FCVAR_USERINFO, "player skin" );

	cl_nosmooth = Cvar_Get( "cl_nosmooth", "0", FCVAR_ARCHIVE, "disable smooth up stair climbing" );
	cl_nointerp = Cvar_Get( "cl_nointerp", "0", FCVAR_CLIENTDLL, "disable interpolation of entities and players" );
	cl_smoothtime = Cvar_Get( "cl_smoothtime", "0.1", FCVAR_ARCHIVE, "time to smooth up" );
	cl_cmdbackup = Cvar_Get( "cl_cmdbackup", "10", FCVAR_ARCHIVE, "how many additional history commands are sent" );
	cl_cmdrate = Cvar_Get( "cl_cmdrate", "60", FCVAR_ARCHIVE, "Max number of command packets sent to server per second" );
	cl_draw_particles = Cvar_Get( "r_drawparticles", "1", FCVAR_CHEAT, "render particles" );
	cl_draw_tracers = Cvar_Get( "r_drawtracers", "1", FCVAR_CHEAT, "render tracers" );
	cl_draw_beams = Cvar_Get( "r_drawbeams", "1", FCVAR_CHEAT, "render beams" );
	cl_lightstyle_lerping = Cvar_Get( "cl_lightstyle_lerping", "0", FCVAR_ARCHIVE, "enables animated light lerping (perfomance option)" );
	cl_showerror = Cvar_Get( "cl_showerror", "0", FCVAR_ARCHIVE, "show prediction error" );
	cl_bmodelinterp = Cvar_Get( "cl_bmodelinterp", "1", FCVAR_ARCHIVE, "enable bmodel interpolation" );
	cl_clockreset = Cvar_Get( "cl_clockreset", "0.1", FCVAR_ARCHIVE, "frametime delta maximum value before reset" );
	cl_fixtimerate = Cvar_Get( "cl_fixtimerate", "7.5", FCVAR_ARCHIVE, "time in msec to client clock adjusting" );
	hud_fontscale = Cvar_Get( "hud_fontscale", "1.0", FCVAR_ARCHIVE|FCVAR_LATCH, "scale hud font texture" );
	hud_scale = Cvar_Get( "hud_scale", "0", FCVAR_ARCHIVE|FCVAR_LATCH, "scale hud at current resolution" );
	Cvar_Get( "cl_background", "0", FCVAR_READ_ONLY, "indicate what background map is running" );
	cl_showevents = Cvar_Get( "cl_showevents", "0", FCVAR_ARCHIVE, "show events playback" );
	Cvar_Get( "lastdemo", "", FCVAR_ARCHIVE, "last played demo" );
	ui_renderworld = Cvar_Get( "ui_renderworld", "0", FCVAR_ARCHIVE, "render world when UI is visible" );

	// these two added to shut up CS 1.5 about 'unknown' commands
	Cvar_Get( "lightgamma", "1", FCVAR_ARCHIVE, "ambient lighting level (legacy, unused)" );
	Cvar_Get( "direct", "1", FCVAR_ARCHIVE, "direct lighting level (legacy, unused)" );

	// server commands
	Cmd_AddCommand ("noclip", NULL, "enable or disable no clipping mode" );
	Cmd_AddCommand ("notarget", NULL, "notarget mode (monsters do not see you)" );
	Cmd_AddCommand ("fullupdate", NULL, "re-init HUD on start demo recording" );
	Cmd_AddCommand ("give", NULL, "give specified item or weapon" );
	Cmd_AddCommand ("drop", NULL, "drop current/specified item or weapon" );
	Cmd_AddCommand ("gametitle", NULL, "show game logo" );
	Cmd_AddRestrictedCommand ("kill", NULL, "die instantly" );
	Cmd_AddCommand ("god", NULL, "enable godmode" );
	Cmd_AddCommand ("fov", NULL, "set client field of view" );

	Cmd_AddRestrictedCommand ("ent_list", NULL, "list entities on server" );
	Cmd_AddRestrictedCommand ("ent_fire", NULL, "fire entity command (be careful)" );
	Cmd_AddRestrictedCommand ("ent_info", NULL, "dump entity information" );
	Cmd_AddRestrictedCommand ("ent_create", NULL, "create entity with specified values (be careful)" );
	Cmd_AddRestrictedCommand ("ent_getvars", NULL, "put parameters of specified entities to client's' ent_last_* cvars" );

	// register our commands
	Cmd_AddCommand ("pause", NULL, "pause the game (if the server allows pausing)" );
	Cmd_AddCommand ("localservers", CL_LocalServers_f, "collect info about local servers" );
	Cmd_AddCommand ("internetservers", CL_InternetServers_f, "collect info about internet servers" );
	Cmd_AddCommand ("cd", CL_PlayCDTrack_f, "Play cd-track (not real cd-player of course)" );
	Cmd_AddCommand ("mp3", CL_PlayCDTrack_f, "Play mp3-track (based on virtual cd-player)" );
	Cmd_AddCommand ("waveplaylen", CL_WavePlayLen_f, "Get approximate length of wave file");

	Cmd_AddRestrictedCommand ("setinfo", CL_SetInfo_f, "examine or change the userinfo string (alias of userinfo)" );
	Cmd_AddRestrictedCommand ("userinfo", CL_SetInfo_f, "examine or change the userinfo string (alias of setinfo)" );
	Cmd_AddCommand ("physinfo", CL_Physinfo_f, "print current client physinfo" );
	Cmd_AddCommand ("disconnect", CL_Disconnect_f, "disconnect from server" );
	Cmd_AddCommand ("record", CL_Record_f, "record a demo" );
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f, "play a demo" );
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f, "demo benchmark" );
	Cmd_AddCommand ("killdemo", CL_DeleteDemo_f, "delete a specified demo file" );
	Cmd_AddCommand ("startdemos", CL_StartDemos_f, "start playing back the selected demos sequentially" );
	Cmd_AddCommand ("demos", CL_Demos_f, "restart looping demos defined by the last startdemos command" );
	Cmd_AddCommand ("movie", CL_PlayVideo_f, "play a movie" );
	Cmd_AddCommand ("stop", CL_Stop_f, "stop playing or recording a demo" );
	Cmd_AddCommand ("info", NULL, "collect info about local servers with specified protocol" );
	Cmd_AddCommand ("escape", CL_Escape_f, "escape from game to menu" );
	Cmd_AddCommand ("togglemenu", CL_Escape_f, "toggle between game and menu" );
	Cmd_AddCommand ("pointfile", CL_ReadPointFile_f, "show leaks on a map (if present of course)" );
	Cmd_AddCommand ("linefile", CL_ReadLineFile_f, "show leaks on a map (if present of course)" );
	Cmd_AddCommand ("fullserverinfo", CL_FullServerinfo_f, "sent by server when serverinfo changes" );
	Cmd_AddCommand ("upload", CL_BeginUpload_f, "uploading file to the server" );

	Cmd_AddRestrictedCommand ("quit", CL_Quit_f, "quit from game" );
	Cmd_AddRestrictedCommand ("exit", CL_Quit_f, "quit from game" );

	Cmd_AddCommand ("screenshot", CL_ScreenShot_f, "takes a screenshot of the next rendered frame" );
	Cmd_AddCommand ("snapshot", CL_SnapShot_f, "takes a snapshot of the next rendered frame" );
	Cmd_AddCommand ("envshot", CL_EnvShot_f, "takes a six-sides cubemap shot with specified name" );
	Cmd_AddCommand ("skyshot", CL_SkyShot_f, "takes a six-sides envmap (skybox) shot with specified name" );
	Cmd_AddCommand ("levelshot", CL_LevelShot_f, "same as \"screenshot\", used for create plaque images" );
	Cmd_AddCommand ("saveshot", CL_SaveShot_f, "used for create save previews with LoadGame menu" );

	Cmd_AddCommand ("connect", CL_Connect_f, "connect to a server by hostname" );
	Cmd_AddCommand ("reconnect", CL_Reconnect_f, "reconnect to current level" );

	Cmd_AddCommand ("rcon", CL_Rcon_f, "sends a command to the server console (rcon_password and rcon_address required)" );
	Cmd_AddCommand ("precache", CL_LegacyPrecache_f, "legacy server compatibility" );

}

//============================================================================
/*
==================
CL_AdjustClock

slowly adjuct client clock
to smooth lag effect
==================
*/
void CL_AdjustClock( void )
{
	if( cl.timedelta == 0.0f || !cl_fixtimerate->value )
		return;

	if( cl_fixtimerate->value < 0.0f )
		Cvar_SetValue( "cl_fixtimerate", 7.5f );

	if( fabs( cl.timedelta ) >= 0.001f )
	{
		double msec, adjust;
		double sign;

		msec = ( cl.timedelta * 1000.0 );
		sign = ( msec < 0 ) ? 1.0 : -1.0;
		msec = Q_min( cl_fixtimerate->value, fabs( msec ));
		adjust = sign * ( msec / 1000.0 );

		if( fabs( adjust ) < fabs( cl.timedelta ))
		{
			cl.timedelta += adjust;
			cl.time += adjust;
		}

		if( cl.oldtime > cl.time )
			cl.oldtime = cl.time;
	}
}

/*
==================
Host_ClientBegin

==================
*/
void Host_ClientBegin( void )
{
	// exec console commands
	Cbuf_Execute ();

	// if client is not active, do nothing
	if( !cls.initialized ) return;

	// finalize connection process if needs
	CL_CheckClientState();

	// tell the client.dll about client data
	CL_UpdateClientData();

	// if running the server locally, make intentions now
	if( SV_Active( )) CL_SendCommand ();
}

/*
==================
Host_ClientFrame

==================
*/
void Host_ClientFrame( void )
{
	// if client is not active, do nothing
	if( !cls.initialized ) return;

	// if running the server remotely, send intentions now after
	// the incoming messages have been read
	if( !SV_Active( )) CL_SendCommand ();

	clgame.dllFuncs.pfnFrame( host.frametime );

	// remember last received framenum
	CL_SetLastUpdate ();

	// read updates from server
	CL_ReadPackets ();

	// do prediction again in case we got
	// a new portion updates from server
	CL_RedoPrediction ();

	// update voice
	Voice_Idle( host.frametime );

	// emit visible entities
	CL_EmitEntities ();

	// in case we lost connection
	CL_CheckForResend ();

	// procssing resources on handle
	while( CL_RequestMissingResources( ));

	// handle thirdperson camera
	CL_MoveThirdpersonCamera();

	// handle spectator movement
	CL_MoveSpectatorCamera();

	// catch changes video settings
	VID_CheckChanges();

	// update the screen
	SCR_UpdateScreen ();

	// update audio
	SND_UpdateSound ();

	// play avi-files
	SCR_RunCinematic ();

	// adjust client time
	CL_AdjustClock ();
}

//============================================================================

/*
====================
CL_Init
====================
*/
void CL_Init( void )
{
	string libpath;

	if( host.type == HOST_DEDICATED )
		return; // nothing running on the client

	CL_InitLocal();

	VID_Init();	// init video
	S_Init();	// init sound
	Voice_Init( VOICE_DEFAULT_CODEC, 3 ); // init voice
	Sequence_Init();

	// unreliable buffer. unsed for unreliable commands and voice stream
	MSG_Init( &cls.datagram, "cls.datagram", cls.datagram_buf, sizeof( cls.datagram_buf ));

	// IN_TouchInit();

	COM_GetCommonLibraryPath( LIBRARY_CLIENT, libpath, sizeof( libpath ));

	if( !CL_LoadProgs( libpath ) )
		Host_Error( "can't initialize %s: %s\n", libpath, COM_GetLibraryError() );

	cls.initialized = true;
	cl.maxclients = 1; // allow to drawing player in menu
	cls.olddemonum = -1;
	cls.demonum = -1;
}

/*
===============
CL_Shutdown

===============
*/
void CL_Shutdown( void )
{
	Con_Printf( "CL_Shutdown()\n" );

	if( !host.crashed && cls.initialized )
	{
		Host_WriteOpenGLConfig ();
		Host_WriteVideoConfig ();
		Touch_WriteConfig();
	}

	// IN_TouchShutdown ();
	Joy_Shutdown ();
	CL_CloseDemoHeader ();
	IN_Shutdown ();
	Mobile_Shutdown ();
	SCR_Shutdown ();
	CL_UnloadProgs ();
	cls.initialized = false;

	// for client-side VGUI support we use other order
	if( FI && FI->GameInfo && !FI->GameInfo->internal_vgui_support )
		VGui_Shutdown();

	if( g_fsapi.Delete )
		g_fsapi.Delete( "demoheader.tmp" ); // remove tmp file
	SCR_FreeCinematic (); // release AVI's *after* client.dll because custom renderer may use them
	S_Shutdown ();
	R_Shutdown ();

	Con_Shutdown ();

}
