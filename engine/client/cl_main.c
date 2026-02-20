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
#include "multi_emulator.h"

#define MAX_CMD_BUFFER        8000
#define CL_CONNECTION_TIMEOUT 15.0f
#define CL_CONNECTION_RETRIES 5
#define CL_TEST_RETRIES       5

CVAR_DEFINE_AUTO( showpause, "1", 0, "show pause logo when paused" );
CVAR_DEFINE_AUTO( mp_decals, "300", FCVAR_ARCHIVE, "decals limit in multiplayer" );
static CVAR_DEFINE_AUTO( dev_overview, "0", 0, "draw level in overview-mode" );
static CVAR_DEFINE_AUTO( cl_resend, "6.0", 0, "time to resend connect" );
CVAR_DEFINE( cl_allow_download, "cl_allowdownload", "1", FCVAR_ARCHIVE, "allow to downloading resources from the server" );
static CVAR_DEFINE( cl_allow_upload, "cl_allowupload", "1", FCVAR_ARCHIVE, "allow to uploading resources to the server" );
CVAR_DEFINE_AUTO( cl_download_ingame, "1", FCVAR_ARCHIVE, "allow to downloading resources while client is active" );
static CVAR_DEFINE_AUTO( cl_logofile, "lambda", FCVAR_ARCHIVE, "player logo name" );
static CVAR_DEFINE_AUTO( cl_logocolor, "orange", FCVAR_ARCHIVE, "player logo color" );
static CVAR_DEFINE_AUTO( cl_logoext, "bmp", FCVAR_ARCHIVE, "temporary cvar to tell engine which logo must be packed" );
CVAR_DEFINE_AUTO( cl_logomaxdim, "96", FCVAR_ARCHIVE, "maximum decal dimension" );
static CVAR_DEFINE_AUTO( cl_test_bandwidth, "1", FCVAR_ARCHIVE, "test network bandwith before connection" );

CVAR_DEFINE( cl_draw_particles, "r_drawparticles", "1", FCVAR_CHEAT, "render particles" );
CVAR_DEFINE( cl_draw_tracers, "r_drawtracers", "1", FCVAR_CHEAT, "render tracers" );
CVAR_DEFINE( cl_draw_beams, "r_drawbeams", "1", FCVAR_CHEAT, "render beams" );

static CVAR_DEFINE_AUTO( rcon_address, "", FCVAR_PRIVILEGED, "remote control address" );
CVAR_DEFINE_AUTO( cl_timeout, "60", 0, "connect timeout (in-seconds)" );
CVAR_DEFINE_AUTO( cl_nopred, "0", FCVAR_ARCHIVE|FCVAR_USERINFO, "disable client movement prediction" );
static CVAR_DEFINE_AUTO( cl_nodelta, "0", 0, "disable delta-compression for server messages" );
CVAR_DEFINE( cl_crosshair, "crosshair", "1", FCVAR_ARCHIVE, "show weapon chrosshair" );
static CVAR_DEFINE_AUTO( cl_cmdbackup, "10", FCVAR_ARCHIVE, "how many additional history commands are sent" );
CVAR_DEFINE_AUTO( cl_showerror, "0", FCVAR_ARCHIVE, "show prediction error" );
CVAR_DEFINE_AUTO( cl_bmodelinterp, "1", FCVAR_ARCHIVE, "enable bmodel interpolation" );
static CVAR_DEFINE_AUTO( cl_lightstyle_lerping, "0", FCVAR_ARCHIVE, "enables animated light lerping (perfomance option)" );
CVAR_DEFINE_AUTO( cl_idealpitchscale, "0.8", 0, "how much to look up/down slopes and stairs when not using freelook" );
CVAR_DEFINE_AUTO( cl_nosmooth, "0", FCVAR_ARCHIVE, "disable smooth up stair climbing" );
CVAR_DEFINE_AUTO( cl_smoothtime, "0.1", FCVAR_ARCHIVE, "time to smooth up" );
CVAR_DEFINE_AUTO( cl_clockreset, "0.1", FCVAR_ARCHIVE, "frametime delta maximum value before reset" );
static CVAR_DEFINE_AUTO( cl_fixtimerate, "7.5", FCVAR_ARCHIVE, "time in msec to client clock adjusting" );
CVAR_DEFINE_AUTO( hud_fontscale, "1.0", FCVAR_ARCHIVE|FCVAR_LATCH, "scale hud font texture" );
CVAR_DEFINE_AUTO( hud_fontrender, "0", FCVAR_ARCHIVE, "hud font render mode (0: additive, 1: holes, 2: trans)" );
CVAR_DEFINE_AUTO( hud_scale, "0", FCVAR_ARCHIVE|FCVAR_LATCH, "scale hud at current resolution" );
CVAR_DEFINE_AUTO( hud_scale_minimal_width, "640", FCVAR_ARCHIVE|FCVAR_LATCH, "if hud_scale results in a HUD virtual screen smaller than this value, it won't be applied" );
CVAR_DEFINE_AUTO( cl_solid_players, "1", 0, "Make all players not solid (can't traceline them)" );
CVAR_DEFINE_AUTO( cl_updaterate, "20", FCVAR_USERINFO|FCVAR_ARCHIVE, "refresh rate of server messages" );
CVAR_DEFINE_AUTO( cl_showevents, "0", FCVAR_ARCHIVE, "show events playback" );
CVAR_DEFINE_AUTO( cl_cmdrate, "30", FCVAR_ARCHIVE, "Max number of command packets sent to server per second" );
CVAR_DEFINE( cl_interp, "ex_interp", "0.1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "Interpolate object positions starting this many seconds in past" );
CVAR_DEFINE_AUTO( cl_nointerp, "0", 0, "disable interpolation of entities and players" );
static CVAR_DEFINE_AUTO( cl_dlmax, "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "max allowed outcoming fragment size" );
static CVAR_DEFINE_AUTO( cl_upmax, "508", FCVAR_ARCHIVE, "max allowed incoming fragment size" );

CVAR_DEFINE_AUTO( cl_lw, "1", FCVAR_ARCHIVE|FCVAR_USERINFO, "enable client weapon predicting" );
CVAR_DEFINE_AUTO( cl_charset, "utf-8", FCVAR_ARCHIVE, "1-byte charset to use (iconv style)" );
CVAR_DEFINE_AUTO( cl_trace_consistency, "0", 0, "enable consistency info tracing (good for developers)" );
CVAR_DEFINE_AUTO( cl_trace_stufftext, "0", 0, "enable stufftext (server-to-client console commands) tracing (good for developers)" );
CVAR_DEFINE_AUTO( cl_trace_messages, "0", FCVAR_CHEAT, "enable message names tracing (good for developers)" );
CVAR_DEFINE_AUTO( cl_trace_events, "0", FCVAR_CHEAT, "enable events tracing (good for developers)" );
static CVAR_DEFINE_AUTO( cl_nat, "0", 0, "show servers running under NAT" );
CVAR_DEFINE_AUTO( hud_utf8, "0", FCVAR_ARCHIVE, "Use utf-8 encoding for hud text" );
CVAR_DEFINE_AUTO( ui_renderworld, "0", FCVAR_ARCHIVE, "render world when UI is visible" );
static CVAR_DEFINE_AUTO( cl_maxframetime, "0", 0, "set deadline timer for client rendering to catch freezes" );
CVAR_DEFINE_AUTO( cl_fixmodelinterpolationartifacts, "1", 0, "try to fix up models interpolation on a moving platforms (monsters on trains for example)" );

//
// userinfo
//
static char username[32];
static CVAR_DEFINE_AUTO( name, username, FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_PRINTABLEONLY|FCVAR_FILTERABLE, "player name" );
static CVAR_DEFINE_AUTO( model, "", FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_FILTERABLE, "player model ('player' is a singleplayer model)" );
static CVAR_DEFINE_AUTO( topcolor, "0", FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_FILTERABLE, "player top color" );
static CVAR_DEFINE_AUTO( bottomcolor, "0", FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_FILTERABLE, "player bottom color" );
CVAR_DEFINE_AUTO( rate, "25000", FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_FILTERABLE, "player network rate" );

static CVAR_DEFINE_AUTO( cl_ticket_generator, "revemu2013", FCVAR_ARCHIVE, "you wouldn't steal a car" );
static CVAR_DEFINE_AUTO( cl_advertise_engine_in_name, "1", FCVAR_ARCHIVE|FCVAR_PRIVILEGED, "add [Xash3D] to the nickname when connecting to GoldSrc servers" );
static CVAR_DEFINE_AUTO( cl_log_outofband, "0", FCVAR_ARCHIVE, "log out of band messages, can be useful for server admins and for engine debugging" );
static CVAR_DEFINE_AUTO( cl_autorecord, "0", 0, "automatically start recording a demo after joining the server" );

client_t		cl;
client_static_t	cls;
clgame_static_t	clgame;

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

	if( cl.background || cl.maxclients > 1 )
		return true; // always active for multiplayer or background map

	return ( cls.key_dest == key_game ); // active if not menu or console
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

connprotocol_t CL_Protocol( void )
{
	return cls.legacymode;
}

void CL_SetCheatState( qboolean multiplayer, qboolean allow_cheats )
{
	uint flags;

	if( NET_NetadrType( &cls.netchan.remote_address ) == NA_LOOPBACK )
		return;

	if( cls.demoplayback )
		flags = FCVAR_SERVER;
	else
		flags = FCVAR_SERVER | FCVAR_READ_ONLY;

	if( allow_cheats )
	{
		Cvar_FullSet( "sv_cheats", "1", flags );
	}
	else
	{
		Cvar_FullSet( "sv_cheats", "0", flags );
		Cvar_SetCheatState();
	}
}

/*
===============
CL_CheckClientState

finalize connection process and begin new frame
with new cls.state
===============
*/
static void CL_CheckClientState( void )
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

		if( cls.legacymode == PROTO_GOLDSRC )
		{
			CL_ServerCommand(true, "specmode 4\n");
			CL_ServerCommand(true, "specmode 4\n");
			CL_ServerCommand(true, "unpause\n");
			CL_ServerCommand(true, "unpause\n");
			CL_ServerCommand(true, "unpause\n");
			CL_ServerCommand(true, "unpause\n");
			CL_ServerCommand(true, "specmode 4\n");
			CL_ServerCommand(true, "specmode 4\n");
		}

		Con_DPrintf( "client connected at %.2f sec\n", Platform_DoubleTime() - cls.timestart );

		if( cl_autorecord.value && !cls.demoplayback )
		{
			if( cls.demorecording )
				CL_Stop_f();

			Cbuf_AddTextf( "record %s_%s\n", Q_timestamp( TIME_FILENAME ), clgame.mapname );
		}
	}
}

static int CL_GetGoldSrcFragmentSize( void *unused, fragsize_t mode )
{
	switch( mode )
	{
	case FRAGSIZE_SPLIT:
		return 1200; // MAX_RELIABLE_PAYLOAD
	case FRAGSIZE_UNRELIABLE:
		return 1400; // MAX_ROUTABLE_PACKET
	default:
		if( cls.state == ca_active )
		{
			// GoldSrc's default value is 512
			// let's help users to not shoot themselves in the foot,
			// assuming nobody wants to make this value lower
			return bound( 512, cl_dlmax.value, 1024 );
		}
		return 128;
	}
}

static int CL_GetFragmentSize( void *unused, fragsize_t mode )
{
	switch( mode )
	{
	case FRAGSIZE_SPLIT:
		return 0;
	case FRAGSIZE_UNRELIABLE:
		return NET_MAX_MESSAGE;
	default:
		if( Netchan_IsLocal( &cls.netchan ))
			return FRAGMENT_LOCAL_SIZE;
		return cl_upmax.value;
	}
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply( connprotocol_t proto )
{
	// g-cont. my favorite message :-)
	Con_Reportf( "%s: %i\n", __func__, cls.signon );

	switch( cls.signon )
	{
	case 1:
		CL_ServerCommand( true, proto == PROTO_GOLDSRC ? "sendents" : "begin" );
		if( host_developer.value >= DEV_EXTENDED )
			Mem_PrintStats();
		break;
	case 2:
		SCR_EndLoadingPlaque();
		if( cl.proxy_redirect && !cls.spectator )
			CL_Disconnect();
		cl.proxy_redirect = false;
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

	if( cl_interp.value <= 0.001 )
		return 1.0f;

	frac = ( cl.time - cl.mtime[0] ) / cl_interp.value;

	return frac;
}

/*
===============
CL_DriftInterpolationAmount

Drift interpolation value (this is used for server unlag system)
===============
*/
static int CL_DriftInterpolationAmount( int goal )
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
static void CL_ComputeClientInterpolationAmount( usercmd_t *cmd )
{
	const float epsilon = 0.001f; // to avoid float invalid comparision
	float min_interp;
	float max_interp = MAX_EX_INTERP;
	float interpolation_time;

	if( cl_updaterate.value < MIN_UPDATERATE )
	{
		Con_Printf( "cl_updaterate minimum is %f, resetting to default (20)\n", MIN_UPDATERATE );
		Cvar_Reset( "cl_updaterate" );
	}

	if( cl_updaterate.value > MAX_UPDATERATE )
	{
		Con_Printf( "cl_updaterate clamped at maximum (%f)\n", MAX_UPDATERATE );
		Cvar_SetValue( "cl_updaterate", MAX_UPDATERATE );
	}

	if( cls.spectator )
		max_interp = 0.2f;

	min_interp = 1.0f / cl_updaterate.value;
	interpolation_time = cl_interp.value * 1000.0;

	if( (cl_interp.value + epsilon) < min_interp )
	{
		Con_Printf( "ex_interp forced up to %.1f msec\n", min_interp * 1000.f );
		Cvar_SetValue( "ex_interp", min_interp );
	}
	else if( (cl_interp.value - epsilon) > max_interp )
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
static void CL_ComputePacketLoss( void )
{
	int i, lost = 0;

	if( host.realtime < cls.packet_loss_recalc_time )
		return;

	cls.packet_loss_recalc_time = host.realtime + 1.0;

	for( i = cls.netchan.incoming_sequence - CL_UPDATE_BACKUP + 1; i <= cls.netchan.incoming_sequence; i++ )
	{
		if( cl.frames[i & CL_UPDATE_MASK].receivedtime == -1.0 )
			lost++;
	}

	cls.packet_loss = lost * 100.0f / (float)CL_UPDATE_BACKUP;
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

static void CL_FindInterpolatedAddAngle( float t, float *frac, pred_viewangle_t **prev, pred_viewangle_t **next )
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

static void CL_ApplyAddAngle( void )
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
static qboolean CL_ProcessShowTexturesCmds( usercmd_t *cmd )
{
	static int	oldbuttons;
	int		changed;
	int		released;

	if( !r_showtextures.value || CL_IsDevOverviewMode( ))
		return false;

	changed = (oldbuttons ^ cmd->buttons);
	released = changed & (~cmd->buttons);

	if( released & ( IN_RIGHT|IN_MOVERIGHT ))
		Cvar_SetValue( "r_showtextures", r_showtextures.value + 1 );
	if( released & ( IN_LEFT|IN_MOVELEFT ))
		Cvar_SetValue( "r_showtextures", Q_max( 1, r_showtextures.value - 1 ));
	oldbuttons = cmd->buttons;

	return true;
}

/*
===============
CL_ProcessOverviewCmds

Transform user movement into overview adjust
===============
*/
static qboolean CL_ProcessOverviewCmds( usercmd_t *cmd )
{
	ref_overview_t	*ov = &clgame.overView;
	int		sign = 1;
	float		size = world.size[!ov->rotated] / world.size[ov->rotated];
	float		step = (2.0f / size) * host.realframetime;
	float		step2 = step * 100.0f * (2.0f / ov->flZoom);

	if( !CL_IsDevOverviewMode() || r_showtextures.value )
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
static void CL_UpdateClientData( void )
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
static void CL_CreateCmd( void )
{
	usercmd_t nullcmd = { 0 }, *cmd;
	runcmd_t  *pcmd;
	qboolean  active;
	double    accurate_ms;
	vec3_t    angles;
	int       input_override;
	int       i, ms;

	if( cls.state <= ca_connected || cls.state == ca_cinematic )
		return;

	// store viewangles in case it's will be freeze
	VectorCopy( cl.viewangles, angles );
	input_override = 0;

	// fix rounding error and framerate depending player move
	accurate_ms = host.frametime * 1000;
	ms = (int)accurate_ms;
	cl.frametime_remainder += accurate_ms - ms; // accumulate rounding error each frame

	// add a ms if error accumulates enough
	if( cl.frametime_remainder >= 1.0 )
	{
		int ms2 = (int)cl.frametime_remainder;

		ms += ms2;
		cl.frametime_remainder -= ms2;
	}

	// ms can't be negative, rely on error accumulation only if FPS > 1000
	ms = Q_min( ms, 255 );

	CL_SetSolidEntities();
	CL_PushPMStates();
	CL_SetSolidPlayers( cl.playernum );

	// message we are constructing.
	i = cls.netchan.outgoing_sequence & CL_UPDATE_MASK;
	pcmd = &cl.commands[i];

	if( !cls.demoplayback )
	{
		pcmd->processedfuncs = false;
		pcmd->senttime = host.realtime;
		memset( &pcmd->cmd, 0, sizeof( pcmd->cmd ));
		pcmd->receivedtime = -1.0;
		pcmd->heldback = false;
		pcmd->sendsize = 0;
		cmd = &pcmd->cmd;
	}
	else
	{
		cmd = &nullcmd;
	}

	active = (( cls.signon == SIGNONS ) && !cl.paused && !cls.demoplayback );
	Platform_PreCreateMove();
	clgame.dllFuncs.CL_CreateMove( host.frametime, cmd, active );
	IN_EngineAppendMove( host.frametime, cmd, active );

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
	if( !cls.demoplayback ) cl.cmd = pcmd->cmd;

	// predict all unacknowledged movements
	CL_PredictMovement( false );
}

void CL_WriteUsercmd( connprotocol_t proto, sizebuf_t *msg, int from, int to )
{
	const usercmd_t nullcmd = { 0 };
	const usercmd_t	*f;
	usercmd_t *t;

	Assert( from == -1 || ( from >= 0 && from < MULTIPLAYER_BACKUP ));
	Assert( to >= 0 && to < MULTIPLAYER_BACKUP );

	f = from == -1 ? &nullcmd : &cl.commands[from].cmd;
	t = &cl.commands[to].cmd;

	// write it into the buffer
	if( proto == PROTO_GOLDSRC )
	{
		MSG_StartBitWriting( msg );
		Delta_WriteGSFields( msg, DT_USERCMD_T, f, t, 0.0f );
		MSG_EndBitWriting( msg );
	}
	else MSG_WriteDeltaUsercmd( msg, f, t );
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds
===================
*/
static void CL_WritePacket( void )
{
	sizebuf_t buf;
	byte data[MAX_CMD_BUFFER] = { 0 };
	runcmd_t *pcmd;
	int numbackup, maxbackup, maxcmds;
	const connprotocol_t proto = cls.legacymode;

	// FIXME: on Xash protocol we don't send move commands until ca_active
	// to prevent outgoing_command outrun incoming_acknowledged
	// which is fatal for some buggy mods like TFC
	//
	// ... but GoldSrc don't have (real) ca_validate state, so we consider
	// ca_validate the same as ca_active, otherwise we don't pass validation
	// of server-side mods like ReAuthCheck
	const connstate_t min_state = proto == PROTO_GOLDSRC ? ca_validate : ca_active;

	// don't send anything if playing back a demo
	if( cls.demoplayback || cls.state < ca_connected || cls.state == ca_cinematic )
		return;

	if( cls.state < min_state )
	{
		Netchan_TransmitBits( &cls.netchan, 0, "" );
		return;
	}
	// cls.state can only be ca_validate or ca_active from here

	CL_ComputePacketLoss( );

	MSG_Init( &buf, "ClientData", data, sizeof( data ));

	switch( proto )
	{
	case PROTO_GOLDSRC:
		maxbackup = MAX_GOLDSRC_BACKUP_CMDS;
		maxcmds = MAX_GOLDSRC_TOTAL_CMDS;
		break;
	default:
		maxbackup = MAX_BACKUP_COMMANDS;
		maxcmds = MAX_TOTAL_CMDS;
		break;
	}

	numbackup = bound( 0, cl_cmdbackup.value, maxbackup );

	// allow extended usercmd limit
	if( proto == PROTO_GOLDSRC && cls.build_num >= 5971 )
		maxcmds = MAX_GOLDSRC_EXTENDED_TOTAL_CMDS - numbackup;

	// clamp cmdrate
	if( cl_cmdrate.value < 10.0f )
		Cvar_DirectSet( &cl_cmdrate, "10" );
	else if( cl_cmdrate.value > 100.0f )
		Cvar_DirectSet( &cl_cmdrate, "100" );

	// are we hltv spectator?
	if( cls.spectator && cl.delta_sequence == cl.validsequence && ( !cls.demorecording || !cls.demowaiting ) && cls.nextcmdtime + 1.0f > host.realtime )
		return;

	// can send this command?
	pcmd = &cl.commands[cls.netchan.outgoing_sequence & CL_UPDATE_MASK];

	if( cl.maxclients == 1 || ( NET_IsLocalAddress( cls.netchan.remote_address ) && !host_limitlocal.value ) || ( host.realtime >= cls.nextcmdtime && Netchan_CanPacket( &cls.netchan, true )))
		pcmd->heldback = false;
	else pcmd->heldback = true;

	// immediately add it to the demo, regardless if we send the message or not
	if( cls.demorecording )
		CL_WriteDemoUserCmd( cls.netchan.outgoing_sequence & CL_UPDATE_MASK );

	if( !pcmd->heldback )
	{
		int newcmds, numcmds;
		int from, i, key;
		int packet_loss = bound( 0, (int)cls.packet_loss, 100 );

		cls.nextcmdtime = host.realtime + ( 1.0f / cl_cmdrate.value );

		if( cls.lastoutgoingcommand < 0 )
			cls.lastoutgoingcommand = cls.netchan.outgoing_sequence;

		newcmds = cls.netchan.outgoing_sequence - cls.lastoutgoingcommand;
		newcmds = bound( 0, newcmds, maxcmds );
		numcmds = newcmds + numbackup;

		// goldsrc starts writing clc_move earlier but it doesn't make sense if it's not going to be sent
		MSG_BeginClientCmd( &buf, clc_move );

		if( proto == PROTO_GOLDSRC )
			MSG_WriteByte( &buf, 0 ); // command length

		key = MSG_GetRealBytesWritten( &buf );
		MSG_WriteByte( &buf, 0 );

		if( proto == PROTO_GOLDSRC && voice_loopback.value )
			SetBits( packet_loss, BIT( 7 ) ); // set 7-th bit to tell server that we want voice loopback

		MSG_WriteByte( &buf, packet_loss );
		MSG_WriteByte( &buf, numbackup );
		MSG_WriteByte( &buf, newcmds );

		for( from = -1, i = numcmds - 1; i >= 0; i-- )
		{
			int to = ( cls.netchan.outgoing_sequence - i ) & CL_UPDATE_MASK;

			CL_WriteUsercmd( proto, &buf, from, to );
			from = to;
		}

		// finalize message
		if( proto == PROTO_GOLDSRC )
		{
			int size = MSG_GetRealBytesWritten( &buf ) - key - 1;

			buf.pData[key - 1] = Q_min( size, 255 );
			buf.pData[key] = CRC32_BlockSequence( &buf.pData[key + 1], size, cls.netchan.outgoing_sequence );
			COM_Munge( &buf.pData[key + 1], Q_min( size, 255 ), cls.netchan.outgoing_sequence );
		}
		else if( !Host_IsLocalClient( ))
		{
			int size = MSG_GetRealBytesWritten( &buf ) - key - 1;
			buf.pData[key] = CRC32_BlockSequence( &buf.pData[key + 1], size, cls.netchan.outgoing_sequence );
		}

		// check if we're timing out
		if( cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged >= CL_UPDATE_MASK && host.realtime - cls.netchan.last_received >= CL_CONNECTION_TIMEOUT )
		{
			Con_NPrintf( 1, "^3Warning:^1 Connection Problem^7\n" );
			Con_NPrintf( 2, "^1Auto-disconnect in %.1f seconds^7", cl_timeout.value - ( host.realtime - cls.netchan.last_received ));
			cl.validsequence = 0;
		}

		if( cl_nodelta.value )
			cl.validsequence = 0;

		if( cl.validsequence && ( !cls.demorecording || !cls.demowaiting ))
		{
			cl.delta_sequence = cl.validsequence;
			MSG_BeginClientCmd( &buf, clc_delta );
			MSG_WriteByte( &buf, cl.validsequence & 0xff );
		}
		else cl.delta_sequence = -1;

		// command finished, remember last sent sequence id
		cls.lastoutgoingcommand = cls.netchan.outgoing_sequence;
		pcmd->sendsize = MSG_GetNumBytesWritten( &buf );

		CL_AddVoiceToDatagram();

		// now add unreliable, if there is enough space
		if( MSG_GetNumBitsWritten( &cls.datagram ) <= MSG_GetNumBitsLeft( &buf ))
			MSG_WriteBits( &buf, MSG_GetData( &cls.datagram ), MSG_GetNumBitsWritten( &cls.datagram ));
		MSG_Clear( &cls.datagram );

		Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
	}
	else
	{
		cls.netchan.outgoing_sequence++;
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
static void CL_SendCommand( void )
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
static void CL_BeginUpload_f( void )
{
	const char		*name;
	resource_t	custResource;
	byte		*buf = NULL;
	int		size = 0;
	byte		md5[16];

	name = Cmd_Argv( 1 );

	if( COM_StringEmptyOrNULL( name ))
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

	if( HPAK_ResourceForHash( hpk_custom_file.string, md5, &custResource ))
	{
		if( memcmp( md5, custResource.rgucMD5_hash, 16 ))
		{
			Con_Reportf( "Bogus data retrieved from %s, attempting to delete entry\n", hpk_custom_file.string );
			HPAK_RemoveLump( hpk_custom_file.string, &custResource );
			return;
		}

		if( HPAK_GetDataPointer( hpk_custom_file.string, &custResource, &buf, &size ))
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
				HPAK_RemoveLump( hpk_custom_file.string, &custResource );
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
	Sys_Quit( "command" );
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

static void CL_GetCDKey( char *protinfo, size_t protinfosize )
{
	byte hash[16] = { 0 };
	MD5Context_t ctx = { 0 };
	char key[64];
	int keylength;

	keylength = Q_snprintf( key, sizeof( key ), "%u", COM_RandomLong( 0, 0x7ffffffe ));

	MD5Init( &ctx );
	MD5Update( &ctx, key, keylength );
	MD5Final( hash, &ctx );

	Q_strnlwr( MD5_Print( hash ), key, sizeof( key ));

	Info_SetValueForKey( protinfo, "cdkey", key, protinfosize );
}

static void CL_WriteSteamTicket( sizebuf_t *send )
{
	string key;
	netadr_t adr = { .type = NA_LOOPBACK, }; // goldsrc servers don't get unique key as xashid isn't sent raw to them
	uint32_t crc;
	char buf[768] = { 0 }; // setti and steamemu return 768
	int i = sizeof( buf );

	if( !Q_strcmp( cl_ticket_generator.string, "null" ))
	{
		MSG_WriteBytes( send, buf, 512 ); // specifically 512 bytes of zeros
		return;
	}

	ID_GetMD5ForAddress( key, adr, sizeof( key ));
	CRC32_Init( &crc );
	CRC32_ProcessBuffer( &crc, key, Q_strlen( key ));
	crc = CRC32_Final( crc );
	i = GenerateRevEmu2013( buf, key, crc );
	MSG_WriteBytes( send, buf, i );

	// RevEmu2013: pTicket[1] = revHash (low), pTicket[5] = 0x01100001 (high)
	*(uint32_t*)cls.steamid = LittleLong( ((uint32_t*)buf)[1] );
	*(uint32_t*)(cls.steamid + 4) = LittleLong( ((uint32_t*)buf)[5] );
}

void CL_SendGoldSrcConnectPacket( netadr_t adr, int challenge, const void *ticket, size_t ticketlen )
{
	const char *name;
	sizebuf_t send;
	byte send_buf[2048];
	char protinfo[MAX_INFO_STRING];

	protinfo[0] = 0;

	Info_SetValueForKey( protinfo, "prot", "3", sizeof( protinfo )); // steam auth type
	Info_SetValueForKeyf( protinfo, "unique", sizeof( protinfo ), "%i", 0xffffffff );
	Info_SetValueForKey( protinfo, "raw", "steam", sizeof( protinfo ));
	CL_GetCDKey( protinfo, sizeof( protinfo ));
	name = Info_ValueForKey( cls.userinfo, "name" );
	if( cl_advertise_engine_in_name.value && Q_strnicmp( name, "[Xash3D]", 8 ))
		Info_SetValueForKeyf( cls.userinfo, "name", sizeof( cls.userinfo ), "[Xash3D]%s", name );

	MSG_Init( &send, "GoldSrcConnect", send_buf, sizeof( send_buf ));
	MSG_WriteLong( &send, NET_HEADER_OUTOFBANDPACKET );
	MSG_WriteStringf( &send, C2S_CONNECT" %i %i \"%s\" \"%s\"\n",
		PROTOCOL_GOLDSRC_VERSION, challenge, protinfo, cls.userinfo );
	MSG_SeekToBit( &send, -8, SEEK_CUR ); // rewrite null terminator
	if( ticket == NULL )
		CL_WriteSteamTicket( &send );
	else
		MSG_WriteBytes( &send, ticket, ticketlen );

	if( MSG_CheckOverflow( &send ))
		Con_Printf( S_ERROR "%s: %s overflow!\n", __func__, MSG_GetName( &send ) );

	NET_SendPacket( NS_CLIENT, MSG_GetNumBytesWritten( &send ), MSG_GetData( &send ), adr );
	Con_Printf( "Trying to connect with GoldSrc 48 protocol\n" );
}

/*
=======================
CL_SendConnectPacket

We have gotten a challenge from the server, so try and
connect.
======================
*/
static void CL_SendConnectPacket( connprotocol_t proto, int challenge )
{
	char protinfo[MAX_INFO_STRING];
	netadr_t adr = { 0 };
	int input_devices;
	netadrtype_t adrtype;

	protinfo[0] = 0;

	if( !NET_StringToAdr( cls.servername, &adr ))
	{
		Con_Printf( "%s: bad server address\n", __func__ );
		cls.connect_time = 0;
		return;
	}

	adrtype = NET_NetadrType( &adr );

	if( adr.port == 0 ) adr.port = MSG_BigShort( PORT_SERVER );

	input_devices = IN_CollectInputDevices();
	IN_LockInputDevices( adrtype != NA_LOOPBACK ? true : false );

	// GoldSrc doesn't need useragent string
	if( adrtype != NA_LOOPBACK && proto != PROTO_GOLDSRC )
	{
		Info_SetValueForKeyf( protinfo, "d", sizeof( protinfo ),  "%d", input_devices );
		Info_SetValueForKey( protinfo, "v", XASH_VERSION, sizeof( protinfo ) );
		Info_SetValueForKeyf( protinfo, "b", sizeof( protinfo ), "%d", Q_buildnum( ));
		Info_SetValueForKey( protinfo, "o", Q_buildos(), sizeof( protinfo ) );
		Info_SetValueForKey( protinfo, "a", Q_buildarch(), sizeof( protinfo ) );
	}

	cls.broker_wait = false;

	if( proto == PROTO_GOLDSRC )
	{
		// if the cl_ticket_generator is set to "steam" we need to get ticket
		// from steam broker, which is asynchronous process by nature
		if( !Q_stricmp( cl_ticket_generator.string, "steam" ))
		{
			if( SteamBroker_InitiateGameConnection( adr, challenge ))
			{
				// we are waiting for the broker response...
				cls.broker_wait = true;
				cls.timestart = Platform_DoubleTime();
				return;
			}
		}

		CL_SendGoldSrcConnectPacket( adr, challenge, NULL, 0 );
	}
	else
	{
		const char *qport = Cvar_VariableString( "net_qport" );
		int extensions = NET_EXT_SPLITSIZE;
		string key;

		ID_GetMD5ForAddress( key, adr, sizeof( key ));

		// reset nickname from cvar value
		Info_SetValueForKey( cls.userinfo, "name", name.string, sizeof( cls.userinfo ));

		if( cl_dlmax.value > FRAGMENT_MAX_SIZE || cl_dlmax.value < FRAGMENT_MIN_SIZE )
			Cvar_DirectSetValue( &cl_dlmax, FRAGMENT_DEFAULT_SIZE );

		Info_SetValueForKey( protinfo, "uuid", key, sizeof( protinfo ));
		Info_SetValueForKey( protinfo, "qport", qport, sizeof( protinfo ));
		Info_SetValueForKeyf( protinfo, "ext", sizeof( protinfo ), "%d", extensions );

		Netchan_OutOfBandPrint( NS_CLIENT, adr, C2S_CONNECT" %i %i \"%s\" \"%s\"\n", PROTOCOL_VERSION, challenge, protinfo, cls.userinfo );
		Con_Printf( "Trying to connect with modern protocol\n" );
	}

	cls.timestart = Platform_DoubleTime();
}

/*
=================
CL_GetTestFragmentSize

Returns bandwidth test fragment size
=================
*/
static int CL_GetTestFragmentSize( void )
{
	// const int fragmentSizes[CL_TEST_RETRIES] = { 64000, 32000, 10666, 5200, 1400 };

	// it turns out, even if we pass the bandwidth test, it doesn't mean we can use such large fragments
	// as a temporary solution, use smaller fragment sizes
	const int fragmentSizes[CL_TEST_RETRIES] = { 1400, 1200, 1000, 800, 508 };

	if( cls.bandwidth_test.retry >= 0 && cls.bandwidth_test.retry < CL_TEST_RETRIES )
		return bound( FRAGMENT_MIN_SIZE, fragmentSizes[cls.bandwidth_test.retry], FRAGMENT_MAX_SIZE );
	else
		return FRAGMENT_MIN_SIZE;
}

static void CL_SendBandwidthTest( netadr_t to, qboolean start )
{
	if( start )
	{
		cls.bandwidth_test.started = true;
		cls.bandwidth_test.retry = 0;
	}
	else cls.bandwidth_test.retry++;

	cls.max_fragment_size = CL_GetTestFragmentSize();
	Con_Printf( "Connecting to %s... (retry #%i, test #%i)\n",
	    cls.servername, cls.connect_retry, cls.bandwidth_test.retry );

	Netchan_OutOfBandPrint( NS_CLIENT, to, C2S_BANDWIDTHTEST " %i %i %i\n",
	    PROTOCOL_VERSION, cls.max_fragment_size, cls.bandwidth_test.challenge );
}

static void CL_SendGetChallenge( netadr_t to )
{
	// always send GoldSrc-styled getchallenge message
	// Xash servers will ignore it but for GoldSrc it will help
	// in auto-detection
	Netchan_OutOfBandPrint( NS_CLIENT, to, C2S_GETCHALLENGE" steam\n" );
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
static void CL_CheckForResend( void )
{
	netadr_t adr;
	net_gai_state_t res;
	float resend_time;

	if( cls.internetservers_wait )
	{
		cls.internetservers_wait = NET_MasterQuery(
			cls.internetservers_key,
			cls.internetservers_nat,
			cls.internetservers_customfilter
		);
	}

	// if the local server is running and we aren't then connect
	if( cls.state == ca_disconnected && SV_Active( ))
	{
		cls.signon = 0;
		cls.state = ca_connecting;
		Q_strncpy( cls.servername, "localhost", sizeof( cls.servername ));
		NET_NetadrSetType( &cls.serveradr, NA_LOOPBACK );
		cls.legacymode = PROTO_CURRENT;

		// we don't need a challenge on the localhost
		CL_SendConnectPacket( PROTO_CURRENT, 0 );
		return;
	}

	// resend if we haven't gotten a reply yet
	if( cls.demoplayback || cls.state != ca_connecting )
		return;

	if( cl_resend.value < CL_MIN_RESEND_TIME )
		Cvar_DirectSetValue( &cl_resend, CL_MIN_RESEND_TIME );
	else if( cl_resend.value > CL_MAX_RESEND_TIME )
		Cvar_DirectSetValue( &cl_resend, CL_MAX_RESEND_TIME );

	if( cls.bandwidth_test.started && !cls.bandwidth_test.passed && !cls.bandwidth_test.failed )
		resend_time = 2.0f;
	else
		resend_time = cl_resend.value;

	if(( host.realtime - cls.connect_time ) < resend_time )
		return;

	res = NET_StringToAdrNB( cls.servername, &adr, false );

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

	if( adr.port == 0 ) adr.port = MSG_BigShort( PORT_SERVER );

	if( cls.bandwidth_test.started )
	{
		if( cls.bandwidth_test.retry >= CL_TEST_RETRIES )
		{
			Con_DPrintf( S_ERROR "%s: couldn't connect\n", __func__ );
			CL_Disconnect();
			return;
		}

		// retry counter incremented during send
	}
	else
	{
		// only retry so many times before failure.
		if( cls.connect_retry >= CL_CONNECTION_RETRIES )
		{
			Con_DPrintf( S_ERROR "%s: couldn't connect\n", __func__ );
			CL_Disconnect();
			return;
		}

		cls.connect_retry++;
	}

	cls.serveradr = adr;
	cls.connect_time = host.realtime; // for retransmit requests

	if( cls.bandwidth_test.started )
	{
		// a1ba: what should we do if the test has been failed?
		// server might intentionally not implement the test,
		// but tell us that test is allowed
		// in this case, just send connect packet and hope for the best
		if( cls.bandwidth_test.passed || cls.bandwidth_test.failed )
			CL_SendConnectPacket( cls.legacymode, cls.bandwidth_test.challenge );
		else
			CL_SendBandwidthTest( adr, false );
	}
	else if( !cls.broker_wait )
	{
		Con_Printf( "Connecting to %s... (retry #%i)\n", cls.servername, cls.connect_retry );
		CL_SendGetChallenge( adr );
	}
}

static resource_t *CL_AddResource( resourcetype_t type, const char *name, int size, qboolean bFatalIfMissing, int index )
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

static void CL_CreateResourceList( void )
{
	char szFileName[MAX_OSPATH];
	byte rgucMD5_hash[16] = { 0 };
	resource_t	*pNewResource;
	int		nSize;
	file_t		*fp;

	HPAK_FlushHostQueue();
	cl.num_resources = 0;
	memset( rgucMD5_hash, 0, sizeof( rgucMD5_hash ));

	// sanitize cvar value
	if( Q_strcmp( cl_logoext.string, "bmp" ) && Q_strcmp( cl_logoext.string, "png" ))
		Cvar_DirectSet( &cl_logoext, "bmp" );

	Q_snprintf( szFileName, sizeof( szFileName ), "logos/remapped.%s", cl_logoext.string );
	if( cls.legacymode == PROTO_GOLDSRC )
	{
		CL_ConvertImageToWAD3( szFileName );
		Q_strncpy( szFileName, "tempdecal.wad", sizeof( szFileName ));
	}
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
			HPAK_AddLump( false, hpk_custom_file.string, pNewResource, NULL, fp );
		}
	}

	FS_Close( fp );
}

static qboolean CL_StringToProtocol( const char *s, connprotocol_t *proto )
{
	if( !Q_stricmp( s, "current" ) || !Q_strcmp( s, "49" ))
	{
		*proto = PROTO_CURRENT;
		return true;
	}

	if( !Q_stricmp( s, "goldsrc" ) || !Q_stricmp( s, "gs" ))
	{
		*proto = PROTO_GOLDSRC;
		return true;
	}

	// quake protocol only used for demos
	Con_Printf( "Unknown protocol. Supported are: 49 (current), gs (goldsrc)\n" );
	return false;
}

/*
================
CL_Connect_f

================
*/
static void CL_Connect_f( void )
{
	string	server;
	connprotocol_t proto = PROTO_CURRENT;

	// hint to connect by using legacy protocol
	if( Cmd_Argc() == 3 && !CL_StringToProtocol( Cmd_Argv( 2 ), &proto ) && Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "connect <server> [protocol]\n" );
		return;
	}

	Q_strncpy( server, Cmd_Argv( 1 ), sizeof( server ));

	// if running a local server, kill it and reissue
	if( SV_Active( ))
		SV_Shutdown( "Server was killed due to connection to remote server\n" );
	NET_Config( true, !cl_nat.value ); // allow remote

	Con_Printf( "server %s\n", server );
	CL_Disconnect();

	// TESTTEST: a see console during connection
	UI_SetActiveMenu( false );
	Key_SetKeyDest( key_console );

	cls.state = ca_connecting;
	cls.legacymode = proto;
	Q_strncpy( cls.servername, server, sizeof( cls.servername ));
	cls.connect_time = MAX_HEARTBEAT; // CL_CheckForResend() will fire immediately
	cls.max_fragment_size = FRAGMENT_MAX_SIZE; // guess a we can establish connection with maximum fragment size
	cls.connect_retry = 0;
	memset( &cls.bandwidth_test, 0, sizeof( cls.bandwidth_test ));
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
static void CL_Rcon_f( void )
{
	char message[1024];
	sizebuf_t msg;
	netadr_t to;
	int	i;

	if( COM_StringEmptyOrNULL( rcon_password.string ))
	{
		Con_Printf( "You must set 'rcon_password' before issuing an rcon command.\n" );
		return;
	}

	NET_Config( true, false );	// allow remote

	if( cls.state >= ca_connected )
	{
		to = cls.netchan.remote_address;
	}
	else
	{
		if( COM_StringEmptyOrNULL( rcon_address.string ))
		{
			Con_Printf( "You must either be connected or set the 'rcon_address' cvar to issue rcon commands\n" );
			return;
		}

		NET_StringToAdr( rcon_address.string, &to );
		if( to.port == 0 )
			to.port = MSG_BigShort( PORT_SERVER );
	}

	MSG_Init( &msg, "RconMessage", message, sizeof( message ));
	MSG_WriteLong( &msg, -1 );
	MSG_WriteStringf( &msg, C2S_RCON" %s ", rcon_password.string );
	MSG_SeekToBit( &msg, -8, SEEK_CUR );

	for( i = 1; i < Cmd_Argc(); i++ )
	{
		string command;

		Cmd_Escape( command, Cmd_Argv( i ), sizeof( command ));
		MSG_WriteString( &msg, command );
		MSG_SeekToBit( &msg, -8, SEEK_CUR );
		MSG_WriteChar( &msg, ' ' );
	}
	MSG_WriteByte( &msg, 0 );

	NET_SendPacket( NS_CLIENT, MSG_GetNumBytesWritten( &msg ), MSG_GetData( &msg ), to );
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
	Cvar_DirectFullSet( &cl_background, "0", FCVAR_READ_ONLY );
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
static void CL_SendDisconnectMessage( connprotocol_t proto )
{
	sizebuf_t	buf;
	byte	data[32];

	if( cls.state == ca_disconnected ) return;

	MSG_Init( &buf, "LastMessage", data, sizeof( data ));
	MSG_BeginClientCmd( &buf, clc_stringcmd );
	if( proto == PROTO_GOLDSRC )
		MSG_WriteString( &buf, "dropclient\n" );
	else MSG_WriteString( &buf, "disconnect" );

	if( NET_NetadrType( &cls.netchan.remote_address ) == NA_UNDEFINED )
		NET_NetadrSetType( &cls.netchan.remote_address, NA_LOOPBACK );

	// make sure message will be delivered
	Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
	Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
	Netchan_TransmitBits( &cls.netchan, MSG_GetNumBitsWritten( &buf ), MSG_GetData( &buf ));
}

int CL_GetSplitSize( void )
{
	int splitsize = (int)cl_dlmax.value;

	if( !FBitSet( cls.extensions, NET_EXT_SPLITSIZE ))
		return 1400;

	if(( splitsize < FRAGMENT_MIN_SIZE ) || ( splitsize > FRAGMENT_MAX_SIZE ))
	{
		Cvar_SetValue( "cl_dlmax", FRAGMENT_DEFAULT_SIZE );
		return FRAGMENT_DEFAULT_SIZE;
	}

	return (int)cl_dlmax.value;
}

void CL_SetupNetchanForProtocol( connprotocol_t proto )
{
	int (*pfnBlockSize)( void *, fragsize_t ) = CL_GetFragmentSize;
	uint flags = 0;

	switch( proto )
	{
	case PROTO_GOLDSRC:
		SetBits( flags, NETCHAN_USE_MUNGE | NETCHAN_USE_BZIP2 | NETCHAN_GOLDSRC );
		pfnBlockSize = CL_GetGoldSrcFragmentSize;
		break;
	default:
		if( !Host_IsLocalClient( ))
			SetBits( flags, NETCHAN_USE_LZSS );

		cls.extensions = Q_atoi( Info_ValueForKey( Cmd_Argv( 1 ), "ext" ));

		if( FBitSet( cls.extensions, NET_EXT_SPLITSIZE ))
			Con_Reportf( "^2NET_EXT_SPLITSIZE enabled^7 (packet size is %d)\n", (int)cl_dlmax.value );
		break;
	}

	Netchan_Setup( NS_CLIENT, &cls.netchan, net_from, Cvar_VariableInteger( "net_qport" ), NULL, pfnBlockSize, flags );
}

/*
=====================
CL_Reconnect

build a request to reconnect client
=====================
*/
static void CL_Reconnect( qboolean setup_netchan )
{
	if( setup_netchan )
	{
		CL_SetupNetchanForProtocol( cls.legacymode );
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
	if( cls.state == ca_disconnected )
		return;

	cls.connect_time = 0;
	cls.changedemo = false;
	cls.max_fragment_size = FRAGMENT_MAX_SIZE; // reset fragment size
	Voice_Disconnect();
	CL_Stop_f();

	// send a disconnect message to the server
	CL_SendDisconnectMessage( cls.legacymode );
	SteamBroker_TerminateGameConnection();
	CL_ClearState ();

	S_StopBackgroundTrack ();
	SCR_EndLoadingPlaque (); // get rid of loading plaque

	// clear the network channel, too.
	Netchan_Clear( &cls.netchan );

	IN_LockInputDevices( false ); // unlock input devices

	cls.state = ca_disconnected;
	memset( &cls.serveradr, 0, sizeof( cls.serveradr ));
	cls.set_lastdemo = false;
	cls.connect_retry = 0;
	memset( &cls.bandwidth_test, 0, sizeof( cls.bandwidth_test ));
	cls.signon = 0;
	cls.legacymode = PROTO_CURRENT;

	// back to menu in non-developer mode
	if( host_developer.value || cls.key_dest == key_menu )
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
	CL_SendDisconnectMessage( cls.legacymode );

	Host_WriteOpenGLConfig();
	Host_WriteConfig();	// write config
}

/*
=================
CL_LocalServers_f
=================
*/
static void CL_LocalServers_f( void )
{
	Con_Printf( "Scanning for servers on the local network area...\n" );
	NET_Config( true, true ); // allow remote

	for( int i = 0; i < 10; i++ )
	{
		netadr_t adr =
		{
			.port = MSG_BigShort( PORT_SERVER + i ),
		};

		NET_NetadrSetType( &adr, NA_BROADCAST );
		Netchan_OutOfBandPrint( NS_CLIENT, adr, A2A_INFO" %i", PROTOCOL_VERSION );

		NET_NetadrSetType( &adr, NA_MULTICAST_IP6 );
		Netchan_OutOfBandPrint( NS_CLIENT, adr, A2A_INFO" %i", PROTOCOL_VERSION );
	}
}

/*
=================
CL_InternetServers_f
=================
*/
static void CL_InternetServers_f( void )
{
	if( Cmd_Argc( ) > 2 || ( Cmd_Argc( ) == 2 && !Info_IsValid( Cmd_Argv( 1 ))))
	{
		Con_Printf( S_USAGE "internetservers [filter]\n" );
		return;
	}

	Con_Printf( "Scanning for servers on the internet area...\n" );

	NET_Config( true, true ); // allow remote

	cls.internetservers_nat = cl_nat.value != 0.0f;
	cls.internetservers_pending = true;
	cls.internetservers_key = COM_RandomLong( 0, 0xFFFFFFFF );
	Q_strncpy( cls.internetservers_customfilter, Cmd_Argv( 1 ), sizeof( cls.internetservers_customfilter ));

	cls.internetservers_wait = NET_MasterQuery(
		cls.internetservers_key,
		cls.internetservers_nat,
		cls.internetservers_customfilter
	);
}

static void CL_QueryServer( netadr_t adr, connprotocol_t proto )
{
	switch( proto )
	{
	case PROTO_GOLDSRC:
		Netchan_OutOfBand( NS_CLIENT, adr, sizeof( A2S_GOLDSRC_INFO ), A2S_GOLDSRC_INFO ); // includes null terminator!
		break;
	case PROTO_CURRENT:
		Netchan_OutOfBandPrint( NS_CLIENT, adr, A2A_INFO" %i", PROTOCOL_VERSION );
		break;
	}
}

static void CL_QueryServer_f( void )
{
	netadr_t adr;
	connprotocol_t proto;

	if( Cmd_Argc( ) != 3 )
	{
		Con_Printf( S_USAGE "ui_queryserver <adr> <protocol>\n" );
		return;
	}

	NET_Config( true, false );

	if( !NET_StringToAdr( Cmd_Argv( 1 ), &adr ))
	{
		Con_Printf( S_ERROR "%s: can't parse %s", __func__, Cmd_Argv( 1 ));
		return;
	}

	if( adr.port == 0 )
		adr.port = PORT_SERVER;

	if( !CL_StringToProtocol( Cmd_Argv( 2 ), &proto ))
		return;

	CL_QueryServer( adr, proto );
}

/*
=================
CL_Reconnect_f

The server is changing levels
=================
*/
static void CL_Reconnect_f( void )
{
	if( cls.state == ca_disconnected )
		return;

	S_StopAllSounds ( true );

	if( cls.state == ca_connected )
	{
		CL_Reconnect( false );
		return;
	}

	if( !COM_StringEmptyOrNULL( cls.servername ))
	{
		connprotocol_t proto = cls.legacymode;

		if( cls.state >= ca_connected )
			CL_Disconnect();

		cls.connect_time = MAX_HEARTBEAT;	// fire immediately
		cls.demonum = cls.movienum = -1;	// not in the demo loop now
		cls.state = ca_connecting;
		cls.signon = 0;
		cls.legacymode = proto; // don't change protocol

		Con_Printf( "reconnecting...\n" );
	}
}

/*
=================
CL_Retry_f

retry connection to last server
=================
*/
static void CL_Retry_f( void )
{
	if( COM_StringEmptyOrNULL( cls.servername ))
	{
		Con_Printf( "Can't retry, no previous connection.\n" );
		return;
	}

	// can't retry when running a server
	if( SV_Active( ))
	{
		Con_Printf( "Can't retry when running a server.\n" );
		return;
	}

	NET_Config( true, !cl_nat.value ); // allow remote

	Con_Printf( "Commencing connection retry to %s\n", cls.servername );
	CL_Disconnect();

	UI_SetActiveMenu( false );
	Key_SetKeyDest( key_console );

	cls.state = ca_connecting;
	cls.connect_time = MAX_HEARTBEAT; // CL_CheckForResend() will fire immediately
	cls.max_fragment_size = FRAGMENT_MAX_SIZE;
	cls.connect_retry = 0;
	memset( &cls.bandwidth_test, 0, sizeof( cls.bandwidth_test ));
	cls.spectator = false;
	cls.signon = 0;
}

/*
=================
CL_FixupColorStringsForInfoString

all the keys and values must be ends with ^7
=================
*/
static void CL_FixupColorStringsForInfoString( const char *in, char *out, size_t len )
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

	while( *in && count < len )
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
static void CL_ParseStatusMessage( netadr_t from, sizebuf_t *msg )
{
	static char	infostring[512+8];
	char		*s = MSG_ReadString( msg );
	int numcl, maxcl, i;

	if( !Info_IsValid( s ))
		return;

	CL_FixupColorStringsForInfoString( s, infostring, sizeof( infostring ));

	if( COM_StringEmptyOrNULL( Info_ValueForKey( infostring, "gamedir" )))
		return; // unsupported proto

	if( COM_StringEmptyOrNULL( Info_ValueForKey( infostring, "host" )))
		return;

	if( COM_StringEmptyOrNULL( Info_ValueForKey( infostring, "map" )))
		return;

	// don't let servers pretend they're something else
	if( !COM_StringEmptyOrNULL( Info_ValueForKey( infostring, "gs" )))
		return;

	maxcl = Q_atoi( Info_ValueForKey( infostring, "maxcl" ));
	numcl = Q_atoi( Info_ValueForKey( infostring, "numcl" ));
	i = Q_atoi( Info_ValueForKey( infostring, "p" ));

	// sanity check
	if( maxcl > MAX_CLIENTS || numcl > MAX_CLIENTS || numcl > maxcl || i != PROTOCOL_VERSION )
		return;

	UI_AddServerToList( from, infostring );
}

static void CL_ParseGoldSrcStatusMessage( netadr_t from, sizebuf_t *msg )
{
	static char	s[512+8];
	int p, numcl, maxcl, password, remaining, bots;
	string host, map, gamedir, version;
	char *replace;

	// set to beginning but skip header
	MSG_SeekToBit( msg, (sizeof( uint32_t ) + sizeof( uint8_t )) << 3, SEEK_SET );

	p = MSG_ReadByte( msg );
	Q_strncpy( host, MSG_ReadString( msg ), sizeof( host ));
	Q_strncpy( map, MSG_ReadString( msg ), sizeof( map ));
	Q_strncpy( gamedir, MSG_ReadString( msg ), sizeof( gamedir ));
	MSG_ReadString( msg ); // game description
	MSG_ReadShort( msg ); // app id
	numcl = MSG_ReadByte( msg );
	maxcl = MSG_ReadByte( msg );
	bots = MSG_ReadByte( msg ); // bots count
	MSG_ReadByte( msg ); // dedicated
	MSG_ReadByte( msg ); // operating system
	password = MSG_ReadByte( msg );
	Q_strncpy( version, MSG_ReadString( msg ), sizeof( version ));

	// sanity check
	if( maxcl > MAX_CLIENTS || numcl > MAX_CLIENTS || bots > MAX_CLIENTS || numcl > maxcl || bots > maxcl )
		return;

	if( MSG_CheckOverflow( msg ))
	{
		Con_Printf( "%s: malfored info packet from %s\n", __func__, NET_AdrToString( from ));
		return;
	}

	// now construct infostring for mainui
	Info_SetValueForKeyf( s, "p", sizeof( s ), "%i", p );
	Info_SetValueForKey( s, "gs", "1", sizeof( s )); // we only support GoldSrc here, Xash never should reply with this message
	Info_SetValueForKey( s, "map", map, sizeof( s ));
	Info_SetValueForKey( s, "dm", "0", sizeof( s )); // obsolete keys
	Info_SetValueForKey( s, "team", "0", sizeof( s ));
	Info_SetValueForKey( s, "coop", "0", sizeof( s ));
	Info_SetValueForKeyf( s, "numcl", sizeof( s ), "%i", numcl );
	Info_SetValueForKeyf( s, "maxcl", sizeof( s ), "%i", maxcl );
	Info_SetValueForKey( s, "gamedir", gamedir, sizeof( s ));
	Info_SetValueForKey( s, "password", password ? "1" : "0", sizeof( s ));

	// write host last so we can try to cut off too long hostnames
	// TODO: value size limit for infostrings
	remaining = sizeof( s ) - Q_strlen( s ) - sizeof( "\\host\\" ) - 1;
	if( remaining < 0 )
	{
		// should never happen?
		Con_Printf( S_ERROR "%s: infostring overflow!\n", __func__ );
		return;
	}

	while(( replace = Q_strpbrk( host, "\\\"" )))
	{
		*replace = ' '; // find a better replacement?
	}

	Info_SetValueForKey( s, "host", host, sizeof( s ));

	UI_AddServerToList( from, s );
}

/*
=================
CL_ParseNETInfoMessage

Handle a reply from a netinfo
=================
*/
static void CL_ParseNETInfoMessage( netadr_t from, const char *s )
{
	net_request_t	*nr = NULL;
	static char	infostring[MAX_PRINT_MSG];
	int		i, context, type;
	int		errorBits = 0;
	const char		*val;
	size_t slen;

	context = Q_atoi( Cmd_Argv( 1 ));
	type = Q_atoi( Cmd_Argv( 2 ));

	// find request with specified context and type
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		if( clgame.net_requests[i].resp.context == context && clgame.net_requests[i].resp.type == type )
		{
			nr = &clgame.net_requests[i];
			break;
		}
	}

	// not found, ignore
	if( nr == NULL )
		return;

	// find the payload
	s = Q_strchr( s, ' ' ); // skip netinfo
	if( !s )
		return;

	s = Q_strchr( s + 1, ' ' ); // skip challenge
	if( !s )
		return;

	s = Q_strchr( s + 1, ' ' ); // skip type
	if( s )
		s++; // skip final whitespace
	else if( type != NETAPI_REQUEST_PING ) // ping have no payload, and that's ok
		return;

	if( s )
	{
		if( s[0] == '\\' )
		{
			// check for errors
			val = Info_ValueForKey( s, "neterror" );

			if( !Q_stricmp( val, "protocol" ))
				SetBits( errorBits, NET_ERROR_PROTO_UNSUPPORTED );
			else if( !Q_stricmp( val, "undefined" ))
				SetBits( errorBits, NET_ERROR_UNDEFINED );
			else if( !Q_stricmp( val, "forbidden" ))
				SetBits( errorBits, NET_ERROR_FORBIDDEN );

			CL_FixupColorStringsForInfoString( s, infostring, sizeof( infostring ));
		}
		else
		{
			Q_strncpy( infostring, s, sizeof( infostring ));
		}
	}
	else
	{
		infostring[0] = 0;
	}

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
}

/*
=================
CL_ProcessNetRequests

check for timeouts
=================
*/
static void CL_ProcessNetRequests( void )
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

static void CL_HandleTestPacket( netadr_t from, sizebuf_t *msg )
{
	byte	recv_buf[NET_MAX_FRAGMENT];
	dword	crcValue;
	int	realsize;
	dword	crcValue2 = 0;

	// this message only used during connection
	// it doesn't make sense after client_connect
	if( cls.state != ca_connecting )
		return;

	if( !CL_IsFromConnectingServer( from ))
		return;

	crcValue = MSG_ReadLong( msg );
	realsize = MSG_GetMaxBytes( msg ) - MSG_GetNumBytesRead( msg );

	if( cls.max_fragment_size != MSG_GetMaxBytes( msg ))
	{
		if( cls.bandwidth_test.retry >= CL_TEST_RETRIES )
		{
			// too many fails use default connection method
			Con_Printf( "hi-speed connection is failed, use default method\n" );
			Cvar_SetValue( "cl_dlmax", FRAGMENT_DEFAULT_SIZE );
			cls.connect_time = MAX_HEARTBEAT;
			cls.bandwidth_test.failed = true;
			return;
		}

		return; // just wait for a next response
	}

	// reading test buffer
	MSG_ReadBytes( msg, recv_buf, realsize );

	// procssing the CRC
	CRC32_ProcessBuffer( &crcValue2, recv_buf, realsize );

	if( crcValue == crcValue2 )
	{
		// packet was sucessfully delivered, adjust the fragment size
		Con_DPrintf( "CRC %x is matched, get challenge, fragment size %d\n", crcValue, cls.max_fragment_size );
		Cvar_SetValue( "cl_dlmax", cls.max_fragment_size );
		cls.connect_time = MAX_HEARTBEAT;
		cls.bandwidth_test.passed = true;
	}
	else
	{
		if( cls.bandwidth_test.retry >= CL_TEST_RETRIES )
		{
			// too many fails use default connection method
			Con_Printf( "hi-speed connection is failed, use default method\n" );
			Cvar_SetValue( "cl_dlmax", FRAGMENT_MIN_SIZE );
			cls.connect_time = host.realtime;
			cls.bandwidth_test.failed = true;
			return;
		}

		Msg( "got testpacket, CRC mismatched 0x%08x should be 0x%08x, trying next fragment size %d\n", crcValue2, crcValue, cls.max_fragment_size >> 1 );
	}
}

static void CL_ClientConnect( connprotocol_t proto, const char *c, netadr_t from )
{
	if( !CL_IsFromConnectingServer( from ))
		return;

	if( cls.state == ca_connected )
	{
		Con_DPrintf( S_ERROR "dup connect received. ignored\n");
		return;
	}

	if( proto == PROTO_GOLDSRC )
	{
		if( Q_strcmp( c, S2C_GOLDSRC_CONNECTION ))
		{
			Con_DPrintf( S_ERROR "GoldSrc client connect expected but wasn't received, ignored\n");
			CL_Disconnect_f();
			return;
		}

		cls.build_num = Q_atoi( Cmd_Argv( 4 ));
		cls.allow_cheats = false; // set by svc_goldsrc_sendextrainfo
	}
	else
	{
		if( Q_strcmp( c, S2C_CONNECTION ))
		{
			Con_DPrintf( S_ERROR "Xash3D client connect expected but wasn't received, ignored\n");
			CL_Disconnect_f();
			return;
		}

		cls.build_num = 0; // not used in Xash3D protocols
		cls.allow_cheats = Q_atoi( Info_ValueForKey( Cmd_Argv( 1 ), "cheats" ));
	}

	CL_Reconnect( true );
	UI_SetActiveMenu( cl.background );
}

static void CL_Print( const char *c, const char *args, netadr_t from, sizebuf_t *msg )
{
	const char *s;

	s = c[0] == A2C_GOLDSRC_PRINT ? args + 1 : MSG_ReadString( msg );

	if( COM_StringEmpty( s ))
		return;

	Con_Printf( "Remote message from %s:\n", NET_AdrToString( from ));
	Con_Printf( "%s%c", s, s[Q_strlen( s ) - 1] != '\n' ? '\n' : '\0' );
}

static void CL_Challenge( const char *c, netadr_t from )
{
	if( cls.state != ca_connecting )
		return;

	if( !CL_IsFromConnectingServer( from ))
		return;

	// try to autodetect protocol by challenge response
	if( !Q_strcmp( c, S2C_GOLDSRC_CHALLENGE ))
	{
		cls.legacymode = PROTO_GOLDSRC;

		cls.steam_auth = Q_atoi( Cmd_Argv( 2 )) == 3;

		if( Cmd_Argc( ) == 5 && cls.steam_auth )
		{
			// arg 2 auth protocol, we only support steam
			// arg 3 if steam is server's steam id
			// arg 4 if steam is server's VAC status
			cls.server_steamid = strtoull( Cmd_Argv( 3 ), NULL, 10 );
			cls.vac2_secure = Q_atoi( Cmd_Argv( 4 ));
		}
	}

	cls.bandwidth_test.challenge = Q_atoi( Cmd_Argv( 1 ));

	if( cls.legacymode == PROTO_CURRENT && cl_test_bandwidth.value && !cls.bandwidth_test.passed )
	{
		// when connecting to old server or server that has bandwidth test disabled
		// it might be more preferrable to have some sane fragment size
		if( !Q_atoi( Cmd_Argv( 2 )))
		{
			Cvar_SetValue( "cl_dlmax", FRAGMENT_DEFAULT_SIZE );
			CL_SendConnectPacket( cls.legacymode, cls.bandwidth_test.challenge );
		}
		else
		{
			CL_SendBandwidthTest( from, true );
		}
	}
	else
	{
		// challenge from the server we are connecting to
		CL_SendConnectPacket( cls.legacymode, cls.bandwidth_test.challenge );
	}
}

static void CL_ErrorMsg( const char *c, const char *args, netadr_t from, sizebuf_t *msg )
{
	char formatted_msg[MAX_VA_STRING];

	if( !CL_IsFromConnectingServer( from ))
		return;

	if( msg != NULL && !Q_strcmp( c, S2C_ERRORMSG ))
	{
		const char *s = MSG_ReadString( msg );
		Q_snprintf( formatted_msg, sizeof( formatted_msg ), "^3Server message^7\n%s", s );
	}
	else if( c[0] == S2C_GOLDSRC_REJECT )
	{
		Q_snprintf( formatted_msg, sizeof( formatted_msg ), "^3Server message^7\n%s", args + 1 );
	}
	else if( c[0] == S2C_GOLDSRC_REJECT_BADPASSWORD )
	{
		if( !Q_strnicmp( &c[1], "BADPASSWORD", 11 ))
			Q_snprintf( formatted_msg, sizeof( formatted_msg ), "^3Server message^7\n%s", args + 12 );
		else
			Q_snprintf( formatted_msg, sizeof( formatted_msg ), "^3Server message^7\n%s", args + 1 );
	}

	// in case we're in console or it's classic mainui which doesn't support messageboxes
	if( !UI_IsVisible() || !UI_ShowMessageBox( formatted_msg ))
		Msg( "%s\n", formatted_msg );

	// don't disconnect, errormsg is a FWGS extension and
	// always followed by disconnect message
}

static void CL_Reject( const char *c, const char *args, netadr_t from )
{
	// this message only used during connection
	// it doesn't make sense after client_connect
	if( cls.state != ca_connecting )
		return;

	if( !CL_IsFromConnectingServer( from ))
		return;

	CL_ErrorMsg( c, args, from, NULL );

	// a disconnect message from the server, which will happen if the server
	// dropped the connection but it is still getting packets from us
	CL_Disconnect_f();
}

static void CL_ServerList( netadr_t from, sizebuf_t *msg )
{
	connprotocol_t proto;

	if( !NET_IsMasterAdr( from, &proto ))
	{
		Con_Printf( S_WARN "unexpected server list packet from %s\n", NET_AdrToString( from ));
		return;
	}

	// check the extra header
	if( proto == PROTO_CURRENT )
	{
		if( MSG_ReadByte( msg ) == 0x7f )
		{
			uint32_t key = MSG_ReadDword( msg );

			if( cls.internetservers_key != key )
			{
				Con_Printf( S_WARN "unexpected server list packet from %s (invalid key)\n", NET_AdrToString( from ));
			return;
			}

			MSG_ReadByte( msg ); // reserved byte
		}
		else
		{
			Con_Printf( S_WARN "invalid server list packet from %s (missing extra header)\n", NET_AdrToString( from ));
			return;
		}
	}

	// serverlist got from masterserver
	while( MSG_GetNumBitsLeft( msg ) > 8 )
	{
		uint8_t addr[16];
		netadr_t servadr = { 0 };

		if( NET_NetadrType( &from ) == NA_IP6 ) // IPv6 master server only sends IPv6 addresses
		{
			MSG_ReadBytes( msg, addr, sizeof( addr ));
			NET_IP6BytesToNetadr( &servadr, addr );
			NET_NetadrSetType( &servadr, NA_IP6 );
		}
		else
		{
			MSG_ReadBytes( msg, servadr.ip, sizeof( servadr.ip ));	// 4 bytes for IP
			NET_NetadrSetType( &servadr, NA_IP );
		}
		servadr.port = MSG_ReadShort( msg );			// 2 bytes for Port

		// list is ends here
		if( !servadr.port )
			break;

		NET_Config( true, false ); // allow remote
		CL_QueryServer( servadr, proto );
	}

	if( cls.internetservers_pending )
	{
		UI_ResetPing();
		cls.internetservers_pending = false;
	}
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
static void CL_ConnectionlessPacket( netadr_t from, sizebuf_t *msg )
{
	char *args;
	const char *c;

	MSG_Clear( msg );
	MSG_ReadLong( msg ); // skip the -1

	args = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( args );
	c = Cmd_Argv( 0 );

	if( cl_log_outofband.value )
		Con_Reportf( "%s: %s : %s\n", __func__, NET_AdrToString( from ), c );

	// server connection
	if( !Q_strcmp( c, "sb_connect" ))
	{
		SteamBroker_HandlePacket( from, msg );
	}
	if( !Q_strcmp( c, S2C_GOLDSRC_CONNECTION ) || !Q_strcmp( c, S2C_CONNECTION ))
	{
		CL_ClientConnect( cls.legacymode, c, from );
	}
	else if( !Q_strcmp( c, A2A_INFO ))
	{
		CL_ParseStatusMessage( from, msg ); // server responding to a status broadcast
	}
	else if( c[0] == S2A_GOLDSRC_INFO )
	{
		CL_ParseGoldSrcStatusMessage( from, msg );
	}
	else if( !Q_strcmp( c, A2A_NETINFO ))
	{
		CL_ParseNETInfoMessage( from, args ); // server responding to a status broadcast
	}
	else if( c[0] == A2C_GOLDSRC_PRINT || !Q_strcmp( c, A2C_PRINT ))
	{
		CL_Print( c, args, from, msg );
	}
	else if( !Q_strcmp( c, S2C_BANDWIDTHTEST ))
	{
		CL_HandleTestPacket( from, msg );
	}
	else if( !Q_strcmp( c, A2A_PING ))
	{
		Netchan_OutOfBandPrint( NS_CLIENT, from, A2A_ACK );
	}
	else if( !Q_strcmp( c, A2A_GOLDSRC_PING ))
	{
		Netchan_OutOfBandPrint( NS_CLIENT, from, A2A_GOLDSRC_ACK );
	}
	else if( !Q_strcmp( c, A2A_ACK ) || !Q_strcmp( c, A2A_GOLDSRC_ACK ))
	{
		// no-op
	}
	else if( !Q_strcmp( c, S2C_CHALLENGE ) || !Q_strcmp( c, S2C_GOLDSRC_CHALLENGE ))
	{
		CL_Challenge( c, from );
	}
	else if( !Q_strcmp( c, S2C_REJECT ) || c[0] == S2C_GOLDSRC_REJECT || c[0] == S2C_GOLDSRC_REJECT_BADPASSWORD )
	{
		CL_Reject( c, args, from );
	}
	else if( !Q_strcmp( c, S2C_ERRORMSG ))
	{
		CL_ErrorMsg( c, args, from, msg );
	}
	else if( !Q_strcmp( c, M2A_SERVERSLIST ))
	{
		CL_ServerList( from, msg );
	}
	else
	{
		char buf[MAX_SYSPATH];
		int len = sizeof( buf );

		if( clgame.dllFuncs.pfnConnectionlessPacket( &from, args, buf, &len ))
		{
			// user out of band message (must be handled in SV_ConnectionlessPacket)
			if( len > 0 )
				Netchan_OutOfBand( NS_SERVER, from, len, (byte *)buf );
		}
		else if( cl_log_outofband.value )
		{
			Con_DPrintf( S_ERROR "bad connectionless packet from %s:\n%s\n", NET_AdrToString( from ), args );
		}
	}
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
static qboolean CL_GetMessage( byte *data, size_t *length )
{
	if( cls.demoplayback )
		return CL_DemoReadMessage( data, length );

	return NET_GetPacket( NS_CLIENT, &net_from, data, length );
}

static void CL_ParseNetMessage( sizebuf_t *msg, void (*parsefn)( sizebuf_t * ))
{
	cls.starting_count = MSG_GetNumBytesRead( msg ); // updates each frame
	CL_Parse_Debug( true ); // begin parsing

	parsefn( msg );

	cl.frames[cl.parsecountmod].graphdata.msgbytes += MSG_GetNumBytesRead( msg ) - cls.starting_count;
	CL_Parse_Debug( false ); // done

	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	if( !cls.demoplayback )
	{
		if( cls.state != ca_active )
			CL_WriteDemoMessage( true, cls.starting_count, msg );

		if( cls.demorecording && !cls.demowaiting )
			CL_WriteDemoMessage( false, cls.starting_count, msg );
	}
}


/*
=================
CL_ReadNetMessage
=================
*/
static void CL_ReadNetMessage( void )
{
	size_t	curSize;
	void (*parsefn)( sizebuf_t *msg );

	switch( cls.legacymode )
	{
	case PROTO_QUAKE:
		parsefn = CL_ParseQuakeMessage;
		break;
	case PROTO_GOLDSRC:
		parsefn = CL_ParseGoldSrcServerMessage;
		break;
	default:
		parsefn = CL_ParseServerMessage;
		break;
	}

	while( CL_GetMessage( net_message_buffer, &curSize ))
	{
		MSG_Init( &net_message, "ServerData", net_message_buffer, curSize );

		// check for connectionless packet (0xffffffff) first
		if( MSG_GetMaxBytes( &net_message ) >= 4 && *(int *)net_message.pData == -1 )
		{
			CL_ConnectionlessPacket( net_from, &net_message );
			continue;
		}

		// can't be a valid sequenced packet
		if( cls.state < ca_connected ) continue;

		if( !cls.demoplayback )
		{
			if( MSG_GetMaxBytes( &net_message ) < 8 )
			{
				Con_Printf( S_WARN "%s: %s:runt packet\n", __func__, NET_AdrToString( net_from ));
				continue;
			}

			// packet from server
			if( !NET_CompareAdr( net_from, cls.netchan.remote_address ))
			{
				Con_DPrintf( S_ERROR "%s: %s:sequenced packet without connection\n", __func__, NET_AdrToString( net_from ));
				continue;
			}

			if( !Netchan_Process( &cls.netchan, &net_message ))
				continue;	// wasn't accepted for some reason
		}

		if( cls.state == ca_active )
		{
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].valid = false;
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].choked = false;
		}
		else
		{
			CL_ResetFrame( &cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK] );
		}

		CL_ParseNetMessage( &net_message, parsefn );
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
			CL_ParseNetMessage( &net_message, parsefn );
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
static void CL_ReadPackets( void )
{
	// decide the simulation time
	cl.oldtime = cl.time;

	if( !cl.paused )
		cl.time += host.frametime;

	// demo time
	if( cls.demorecording && !cls.demowaiting )
		cls.demotime += host.frametime;

	CL_ReadNetMessage();

	CL_ApplyAddAngle();

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
	if( host.frametime > 5.0f ) cls.netchan.last_received = Platform_DoubleTime();

	// check timeout
	if( cls.state >= ca_connected && cls.state != ca_cinematic && !cls.demoplayback )
	{
		if( host.realtime - cls.netchan.last_received > cl_timeout.value )
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
static const char *CL_CleanFileName( const char *filename )
{
	if( !COM_StringEmptyOrNULL( filename ) && filename[0] == '!' )
		return "customization";

	return filename;
}


/*
====================
CL_RegisterCustomization

register custom resource for player
====================
*/
static void CL_RegisterCustomization( resource_t *resource )
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
	resource_t	*p;

	if( !COM_StringEmptyOrNULL( filename ) && successfully_received )
	{
		if( filename[0] != '!' )
			Con_Printf( "processing %s\n", filename );

		if( !Q_strnicmp( filename, DEFAULT_DOWNLOADED_DIRECTORY, sizeof( DEFAULT_DOWNLOADED_DIRECTORY ) - 1 ))
		{
			// skip "downloaded/" part to avoid mismatch with needed resources list
			filename += sizeof( DEFAULT_DOWNLOADED_DIRECTORY ) - 1;
		}
	}
	else if( !successfully_received )
	{
		Con_Printf( S_ERROR "server failed to transmit file '%s'\n", CL_CleanFileName( filename ));
	}

	for( p = cl.resourcesneeded.pNext; p != &cl.resourcesneeded; p = p->pNext )
	{
		if( !Q_strnicmp( filename, "!MD5", 4 ))
		{
			COM_HexConvert( filename + 4, 32, rgucMD5_hash );

			if( !memcmp( p->rgucMD5_hash, rgucMD5_hash, 16 ))
				break;
		}
		else if( p->type == t_sound )
		{
			const char *pfilename = filename;

			if( !Q_strnicmp( filename, DEFAULT_SOUNDPATH, sound_len ))
				pfilename += sound_len;

			if( !Q_stricmp( p->szFileName, pfilename ))
				break;
		}
		else
		{
			if( !Q_stricmp( p->szFileName, filename ))
					break;
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
						HPAK_AddLump( true, hpk_custom_file.string, p, cls.netchan.tempbuffer, NULL );
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
				CL_RegisterResources( &msg, cls.legacymode );

			if( MSG_GetNumBytesWritten( &msg ) > 0 )
			{
				Netchan_CreateFragments( &cls.netchan, &msg );
				Netchan_FragSend( &cls.netchan );
			}
		}

		if( cls.netchan.tempbuffer )
		{
			Con_Printf( "Received a decal %s, but didn't find it in resources needed list!\n", filename );
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

/*
===============
CL_UpdateInfo

tell server about changed userinfo
===============
*/
void CL_UpdateInfo( const char *key, const char *value )
{
	switch( cls.legacymode )
	{
	case PROTO_GOLDSRC:
		if( cl_advertise_engine_in_name.value && !Q_stricmp( key, "name" ) && Q_strnicmp( value, "[Xash3D]", 8 ))
		{
			CL_ServerCommand( true, "setinfo \"%s\" \"[Xash3D]%s\"\n", key, value );
			break;
		}
		// intentional fallthrough
	default:
		CL_ServerCommand( true, "setinfo \"%s\" \"%s\"\n", key, value );
		break;
	}
}

//=============================================================================
/*
==============
CL_SetInfo_f
==============
*/
static void CL_SetInfo_f( void )
{
	convar_t	*var;

	if( Cmd_Argc() == 1 )
	{
		Con_Printf( "User info settings:\n" );
		Info_Print( cls.userinfo );
		Con_Printf( "Total %zu symbols\n", Q_strlen( cls.userinfo ));
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
	else if( Info_SetValueForKey( cls.userinfo, Cmd_Argv( 1 ), Cmd_Argv( 2 ), sizeof( cls.userinfo )))
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
static void CL_Physinfo_f( void )
{
	Con_Printf( "Phys info settings:\n" );
	Info_Print( cls.physinfo );
	Con_Printf( "Total %zu symbols\n", Q_strlen( cls.physinfo ));
}

static qboolean CL_ShouldRescanFilesystem( void )
{
	resource_t *res;
	qboolean retval = false;

	for( res = cl.resourcesonhand.pNext; res && res != &cl.resourcesonhand; res = res->pNext )
	{
		if( res->type == t_generic )
		{
			const char *ext = COM_FileExtension( res->szFileName );

			if( !g_fsapi.IsArchiveExtensionSupported( ext, IAES_ONLY_REAL_ARCHIVES ))
				continue;

			if( FBitSet( res->ucExtraFlags, RES_EXTRA_ARCHIVE_CHECKED ))
				continue;

			SetBits( res->ucExtraFlags, RES_EXTRA_ARCHIVE_CHECKED );
			retval = true;
		}
	}

	return retval;
}

qboolean CL_PrecacheResources( void )
{
	resource_t	*pRes;

	// if we downloaded new WAD files or any other archives they must be added to searchpath
	if( CL_ShouldRescanFilesystem( ))
		FS_Rescan_f();

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
			if( pRes->nIndex >= 0 && pRes->nIndex < ARRAYSIZE( cl.sound_precache ) && pRes->nIndex < ARRAYSIZE( cl.sound_index ))
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
			if( pRes->nIndex >= 0 && pRes->nIndex < ARRAYSIZE( cl.models ))
			{
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
			}
			break;
		case t_decal:
			if( !FBitSet( pRes->ucFlags, RES_CUSTOM ) && pRes->nIndex >= 0 && pRes->nIndex < ARRAYSIZE( host.draw_decals ))
				Q_strncpy( host.draw_decals[pRes->nIndex], pRes->szFileName, sizeof( host.draw_decals[0] ));
			break;
		case t_generic:
			if( pRes->nIndex >= 0 && pRes->nIndex < ARRAYSIZE( cl.files_precache ))
			{
				Q_strncpy( cl.files_precache[pRes->nIndex], pRes->szFileName, sizeof( cl.files_precache[0] ));
				cl.numfiles = Q_max( cl.numfiles, pRes->nIndex + 1 );
			}
			break;
		case t_eventscript:
			if( pRes->nIndex >= 0 && pRes->nIndex < ARRAYSIZE( cl.event_precache ))
			{
				Q_strncpy( cl.event_precache[pRes->nIndex], pRes->szFileName, sizeof( cl.event_precache[0] ));
				CL_SetEventIndex( cl.event_precache[pRes->nIndex], pRes->nIndex );
			}
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
static void CL_FullServerinfo_f( void )
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
static void CL_Escape_f( void )
{
	if( cls.key_dest == key_menu )
		return;

	// the final credits is running
	if( UI_CreditsActive( )) return;

	if( cls.state == ca_cinematic )
		SCR_NextMovie(); // jump to next movie
	else UI_SetActiveMenu( true );
}

static void CL_ListMessages_f( void )
{
	int i;

	Con_Printf( "num size name\n" );
	for( i = 0; i < MAX_USER_MESSAGES; i++ )
	{
		if( COM_StringEmpty( clgame.msg[i].name ))
			break;

		Con_Printf( "%3d\t%3d\t%s\n", clgame.msg[i].number, clgame.msg[i].size, clgame.msg[i].name );
	}

	Con_Printf( "Total %i messages\n", i );
}

/*
=================
CL_InitLocal
=================
*/
static void CL_InitLocal( void )
{
	cls.state = ca_disconnected;
	cls.signon = 0;
	memset( &cls.serveradr, 0, sizeof( cls.serveradr ) );

	cl.resourcesneeded.pNext = cl.resourcesneeded.pPrev = &cl.resourcesneeded;
	cl.resourcesonhand.pNext = cl.resourcesonhand.pPrev = &cl.resourcesonhand;

	Cvar_RegisterVariable( &cl_ticket_generator );
	Cvar_RegisterVariable( &cl_advertise_engine_in_name );
	Cvar_RegisterVariable( &cl_log_outofband );
	Cvar_RegisterVariable( &cl_autorecord );

	Cvar_RegisterVariable( &showpause );
	Cvar_RegisterVariable( &mp_decals );
	Cvar_RegisterVariable( &dev_overview );
	Cvar_RegisterVariable( &cl_resend );
	Cvar_RegisterVariable( &cl_allow_upload );
	Cvar_RegisterVariable( &cl_allow_download );
	Cvar_RegisterVariable( &cl_download_ingame );
	Cvar_RegisterVariable( &cl_logofile );
	Cvar_RegisterVariable( &cl_logocolor );
	Cvar_RegisterVariable( &cl_logoext );
	Cvar_RegisterVariable( &cl_logomaxdim );
	Cvar_RegisterVariable( &cl_test_bandwidth );

	Voice_RegisterCvars();
	VGui_RegisterCvars();

	// register our variables
	Cvar_RegisterVariable( &cl_crosshair );
	Cvar_RegisterVariable( &cl_nodelta );
	Cvar_RegisterVariable( &cl_idealpitchscale );
	Cvar_RegisterVariable( &cl_solid_players );
	Cvar_RegisterVariable( &cl_interp );
	Cvar_RegisterVariable( &cl_timeout );
	Cvar_RegisterVariable( &cl_charset );
	Cvar_RegisterVariable( &hud_utf8 );

	Cvar_RegisterVariable( &rcon_address );

	Cvar_RegisterVariable( &cl_trace_consistency );
	Cvar_RegisterVariable( &cl_trace_stufftext );
	Cvar_RegisterVariable( &cl_trace_messages );
	Cvar_RegisterVariable( &cl_trace_events );

	// userinfo
	Cvar_RegisterVariable( &cl_nopred );
	Q_strncpy( username, Sys_GetCurrentUser(), sizeof( username ));	// initialize before registering variable
	Cvar_RegisterVariable( &name );
	Cvar_Get( "ui_username", username, FCVAR_READ_ONLY|FCVAR_PRIVILEGED, "default user name" );
	Cvar_RegisterVariable( &model );
	Cvar_RegisterVariable( &cl_updaterate );
	Cvar_RegisterVariable( &cl_dlmax );
	Cvar_RegisterVariable( &cl_upmax );
	Cvar_RegisterVariable( &cl_nat );
	Cvar_RegisterVariable( &rate );
	Cvar_RegisterVariable( &topcolor );
	Cvar_RegisterVariable( &bottomcolor );
	Cvar_RegisterVariable( &cl_lw );
	Cvar_Get( "cl_lc", "1", FCVAR_ARCHIVE|FCVAR_USERINFO, "enable lag compensation" );
	Cvar_Get( "password", "", FCVAR_USERINFO, "server password" );
	Cvar_Get( "team", "", FCVAR_USERINFO, "player team" );
	Cvar_Get( "skin", "", FCVAR_USERINFO, "player skin" );

	Cvar_RegisterVariable( &cl_nosmooth );
	Cvar_RegisterVariable( &cl_nointerp );
	Cvar_RegisterVariable( &cl_smoothtime );
	Cvar_RegisterVariable( &cl_cmdbackup );
	Cvar_RegisterVariable( &cl_cmdrate );
	Cvar_RegisterVariable( &cl_draw_particles );
	Cvar_RegisterVariable( &cl_draw_tracers );
	Cvar_RegisterVariable( &cl_draw_beams );
	Cvar_RegisterVariable( &cl_lightstyle_lerping );
	Cvar_RegisterVariable( &cl_showerror );
	Cvar_RegisterVariable( &cl_bmodelinterp );
	Cvar_RegisterVariable( &cl_clockreset );
	Cvar_RegisterVariable( &cl_fixtimerate );
	Cvar_RegisterVariable( &hud_fontscale );
	Cvar_RegisterVariable( &hud_fontrender );
	Cvar_RegisterVariable( &hud_scale );
	Cvar_RegisterVariable( &hud_scale_minimal_width );
	Cvar_RegisterVariable( &cl_showevents );
	Cvar_Get( "lastdemo", "", FCVAR_ARCHIVE, "last played demo" );
	Cvar_RegisterVariable( &ui_renderworld );
	Cvar_RegisterVariable( &cl_maxframetime );
	Cvar_RegisterVariable( &cl_fixmodelinterpolationartifacts );

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
	Cmd_AddRestrictedCommand( "localservers", CL_LocalServers_f, "collect info about local servers" );
	Cmd_AddRestrictedCommand( "internetservers", CL_InternetServers_f, "collect info about internet servers" );
	Cmd_AddRestrictedCommand( "ui_queryserver", CL_QueryServer_f, "query server info from console" );
	Cmd_AddCommand ("cd", CL_PlayCDTrack_f, "Play cd-track (not real cd-player of course)" );
	Cmd_AddCommand ("mp3", CL_PlayCDTrack_f, "Play mp3-track (based on virtual cd-player)" );
	Cmd_AddCommand ("waveplaylen", CL_WavePlayLen_f, "Get approximate length of wave file");

	Cmd_AddRestrictedCommand ("setinfo", CL_SetInfo_f, "examine or change the userinfo string (alias of userinfo)" );
	Cmd_AddRestrictedCommand ("userinfo", CL_SetInfo_f, "examine or change the userinfo string (alias of setinfo)" );
	Cmd_AddCommand ("physinfo", CL_Physinfo_f, "print current client physinfo" );
	Cmd_AddCommand ("disconnect", CL_Disconnect_f, "disconnect from server" );
	Cmd_AddRestrictedCommand( "record", CL_Record_f, "record a demo" );
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f, "play a demo" );
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f, "demo benchmark" );
	Cmd_AddRestrictedCommand( "killdemo", CL_DeleteDemo_f, "delete a specified demo file" );
	Cmd_AddCommand ("startdemos", CL_StartDemos_f, "start playing back the selected demos sequentially" );
	Cmd_AddCommand ("demos", CL_Demos_f, "restart looping demos defined by the last startdemos command" );
	Cmd_AddCommand ("movie", CL_PlayVideo_f, "play a movie" );
	Cmd_AddCommand ("stop", CL_Stop_f, "stop playing or recording a demo" );
	Cmd_AddCommand( "listdemo", CL_ListDemo_f, "list demo entries" );
	Cmd_AddCommand ("info", NULL, "collect info about local servers with specified protocol" );
	Cmd_AddCommand ("escape", CL_Escape_f, "escape from game to menu" );
	Cmd_AddCommand ("togglemenu", CL_Escape_f, "toggle between game and menu" );
	Cmd_AddCommand ("pointfile", CL_ReadPointFile_f, "show leaks on a map (if present of course)" );
	Cmd_AddCommand ("linefile", CL_ReadLineFile_f, "show leaks on a map (if present of course)" );
	Cmd_AddCommand ("fullserverinfo", CL_FullServerinfo_f, "sent by server when serverinfo changes" );
	Cmd_AddCommand ("upload", CL_BeginUpload_f, "uploading file to the server" );

	Cmd_AddRestrictedCommand( "replaybufferdat", CL_ReplayBufferDat_f, "development and debugging tool" );

	Cmd_AddRestrictedCommand ("quit", CL_Quit_f, "quit from game" );
	Cmd_AddRestrictedCommand ("exit", CL_Quit_f, "quit from game" );

	Cmd_AddCommand ("screenshot", CL_GenericShot_f, "takes a screenshot of the next rendered frame" );
	Cmd_AddCommand ("snapshot", CL_GenericShot_f, "takes a snapshot of the next rendered frame" );
	Cmd_AddCommand ("envshot", CL_GenericShot_f, "takes a six-sides cubemap shot with specified name" );
	Cmd_AddCommand ("skyshot", CL_GenericShot_f, "takes a six-sides envmap (skybox) shot with specified name" );
	Cmd_AddCommand ("levelshot", CL_LevelShot_f, "same as \"screenshot\", used for create plaque images" );
	Cmd_AddCommand ("saveshot", CL_GenericShot_f, "used for create save previews with LoadGame menu" );

	Cmd_AddCommand ("connect", CL_Connect_f, "connect to a server by hostname" );
	Cmd_AddCommand ("reconnect", CL_Reconnect_f, "reconnect to current level" );
	Cmd_AddCommand ("retry", CL_Retry_f, "retry connection to last server" );

	Cmd_AddCommand ("rcon", CL_Rcon_f, "sends a command to the server console (rcon_password and rcon_address required)" );

	Cmd_AddCommand( "richpresence_gamemode", Cmd_Null_f, "compatibility command, does nothing" );
	Cmd_AddCommand( "richpresence_update", Cmd_Null_f, "compatibility command, does nothing" );

	Cmd_AddCommand( "cl_list_messages", CL_ListMessages_f, "list registered user messages" );
}

//============================================================================
/*
==================
CL_AdjustClock

slowly adjuct client clock
to smooth lag effect
==================
*/
static void CL_AdjustClock( void )
{
	if( cl.timedelta == 0.0f || !cl_fixtimerate.value )
		return;

	if( cl_fixtimerate.value < 0.0f )
		Cvar_SetValue( "cl_fixtimerate", 7.5f );

	if( fabs( cl.timedelta ) >= 0.001f )
	{
		double msec, adjust;
		double sign;

		msec = ( cl.timedelta * 1000.0 );
		sign = ( msec < 0 ) ? 1.0 : -1.0;
		msec = Q_min( cl_fixtimerate.value, fabs( msec ));
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
	if( cls.key_dest == key_game && cls.state == ca_active && !Con_Visible() )
		Platform_SetTimer( cl_maxframetime.value );

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
	Voice_Init( VOICE_DEFAULT_CODEC, 3, true ); // init voice (do not open the device)

	// unreliable buffer. unsed for unreliable commands and voice stream
	MSG_Init( &cls.datagram, "cls.datagram", cls.datagram_buf, sizeof( cls.datagram_buf ));

	COM_GetCommonLibraryPath( LIBRARY_CLIENT, libpath, sizeof( libpath ));

	if( !CL_LoadProgs( libpath ))
		Host_Error( "can't initialize %s: %s\n", libpath, COM_GetLibraryError( ));

	ID_Init();
	SteamBroker_Init();

	cls.build_num = 0;
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
	Con_Printf( "%s()\n", __func__ );

	if( host.status != HOST_CRASHED && cls.initialized )
	{
		Host_WriteOpenGLConfig ();
		Host_WriteVideoConfig ();
		Touch_WriteConfig();
	}

	Joy_Shutdown ();
	CL_CloseDemoHeader ();
	IN_Shutdown ();
	Mobile_Shutdown ();
	SCR_Shutdown ();
	CL_UnloadProgs ();
	SteamBroker_Shutdown();
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
