/*
sv_main.c - server main loop
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
#include "net_encode.h"
#include "platform/platform.h"

#define HEARTBEAT_SECONDS	((sv_nat.value > 0.0f) ? 60.0f : 300.0f)  	// 1 or 5 minutes

// server cvars
CVAR_DEFINE_AUTO( sv_lan, "0", 0, "server is a lan server ( no heartbeat, no authentication, no non-class C addresses, 9999.0 rate, etc." );
CVAR_DEFINE_AUTO( sv_lan_rate, "20000.0", 0, "rate for lan server" );
CVAR_DEFINE_AUTO( sv_nat, "0", 0, "enable NAT bypass for this server" );
CVAR_DEFINE_AUTO( sv_aim, "1", FCVAR_ARCHIVE|FCVAR_SERVER, "auto aiming option" );
CVAR_DEFINE_AUTO( sv_unlag, "1", 0, "allow lag compensation on server-side" );
CVAR_DEFINE_AUTO( sv_maxunlag, "0.5", 0, "max latency value which can be interpolated (by default ping should not exceed 500 units)" );
CVAR_DEFINE_AUTO( sv_unlagpush, "0.0", 0, "interpolation bias for unlag time" );
CVAR_DEFINE_AUTO( sv_unlagsamples, "1", 0, "max samples to interpolate" );
CVAR_DEFINE_AUTO( rcon_password, "", FCVAR_PROTECTED | FCVAR_PRIVILEGED, "remote connect password" );
CVAR_DEFINE_AUTO( rcon_enable, "1", FCVAR_PROTECTED, "enable accepting remote commands on server" );
CVAR_DEFINE_AUTO( sv_filterban, "1", 0, "filter banned users" );
CVAR_DEFINE_AUTO( sv_cheats, "0", FCVAR_SERVER, "allow cheats on server" );
CVAR_DEFINE_AUTO( sv_instancedbaseline, "1", 0, "allow to use instanced baselines to saves network overhead" );
CVAR_DEFINE_AUTO( sv_contact, "", FCVAR_ARCHIVE|FCVAR_SERVER, "server techincal support contact address or web-page" );
CVAR_DEFINE_AUTO( sv_minupdaterate, "25.0", FCVAR_ARCHIVE, "minimal value for 'cl_updaterate' window" );
CVAR_DEFINE_AUTO( sv_maxupdaterate, "60.0", FCVAR_ARCHIVE, "maximal value for 'cl_updaterate' window" );
CVAR_DEFINE_AUTO( sv_minrate, "5000", FCVAR_SERVER, "min bandwidth rate allowed on server, 0 == unlimited" );
CVAR_DEFINE_AUTO( sv_maxrate, "50000", FCVAR_SERVER, "max bandwidth rate allowed on server, 0 == unlimited" );
CVAR_DEFINE_AUTO( sv_logrelay, "0", FCVAR_ARCHIVE, "allow log messages from remote machines to be logged on this server" );
CVAR_DEFINE_AUTO( sv_newunit, "0", 0, "clear level-saves from previous SP game chapter to help keep .sav file size as minimum" );
CVAR_DEFINE_AUTO( sv_clienttrace, "1", FCVAR_SERVER, "0 = big box(Quake), 0.5 = halfsize, 1 = normal (100%), otherwise it's a scaling factor" );
CVAR_DEFINE_AUTO( sv_timeout, "65", 0, "after this many seconds without a message from a client, the client is dropped" );
CVAR_DEFINE_AUTO( sv_failuretime, "0.5", 0, "after this long without a packet from client, don't send any more until client starts sending again" );
CVAR_DEFINE_AUTO( sv_password, "", FCVAR_SERVER|FCVAR_PROTECTED, "server password for entry into multiplayer games" );
CVAR_DEFINE_AUTO( sv_proxies, "1", FCVAR_SERVER, "maximum count of allowed proxies for HLTV spectating" );
CVAR_DEFINE_AUTO( sv_send_logos, "1", 0, "send custom decal logo to other players so they can view his too" );
CVAR_DEFINE_AUTO( sv_send_resources, "1", 0, "allow to download missed resources for players" );
CVAR_DEFINE_AUTO( sv_logbans, "0", 0, "print into the server log info about player bans" );
CVAR_DEFINE_AUTO( sv_allow_upload, "1", FCVAR_SERVER, "allow uploading custom resources on a server" );
CVAR_DEFINE_AUTO( sv_allow_download, "1", FCVAR_SERVER, "allow downloading custom resources to the client" );
CVAR_DEFINE_AUTO( sv_uploadmax, "0.5", FCVAR_SERVER, "max size to upload custom resources (500 kB as default)" );
CVAR_DEFINE_AUTO( sv_downloadurl, "", FCVAR_PROTECTED, "location from which clients can download missing files" );
CVAR_DEFINE( sv_consistency, "mp_consistency", "1", FCVAR_SERVER, "enbale consistency check in multiplayer" );
CVAR_DEFINE_AUTO( mp_logecho, "1", 0, "log multiplayer frags to server logfile" );
CVAR_DEFINE_AUTO( mp_logfile, "1", 0, "log multiplayer frags to console" );
CVAR_DEFINE_AUTO( sv_log_singleplayer, "0", FCVAR_ARCHIVE, "allows logging in singleplayer games" );
CVAR_DEFINE_AUTO( sv_log_onefile, "0", FCVAR_ARCHIVE, "logs server information to only one file" );
CVAR_DEFINE_AUTO( sv_trace_messages, "0", FCVAR_LATCH, "enable server usermessages tracing (good for developers)" );
CVAR_DEFINE_AUTO( sv_master_response_timeout, "4", FCVAR_ARCHIVE, "master server heartbeat response timeout in seconds" );
CVAR_DEFINE_AUTO( sv_autosave, "1", FCVAR_ARCHIVE|FCVAR_SERVER|FCVAR_PRIVILEGED, "enable autosaving" );

// game-related cvars
CVAR_DEFINE_AUTO( mapcyclefile, "mapcycle.txt", 0, "name of multiplayer map cycle configuration file" );
CVAR_DEFINE_AUTO( motdfile, "motd.txt", 0, "name of 'message of the day' file" );
CVAR_DEFINE_AUTO( logsdir, "logs", 0, "place to store multiplayer logs" );
CVAR_DEFINE_AUTO( bannedcfgfile, "banned.cfg", 0, "name of list of banned users" );
CVAR_DEFINE_AUTO( deathmatch, "0", 0, "deathmatch mode in multiplayer game" );
CVAR_DEFINE_AUTO( coop, "0", 0, "cooperative mode in multiplayer game" );
CVAR_DEFINE_AUTO( teamplay, "0", 0, "team mode in multiplayer game" );
CVAR_DEFINE_AUTO( skill, "1", 0, "skill level in singleplayer game" );
CVAR_DEFINE_AUTO( temp1, "0", 0, "temporary cvar that used by some mods" );
CVAR_DEFINE_AUTO( listipcfgfile, "listip.cfg", 0, "name of listip.cfg file" );
CVAR_DEFINE_AUTO( mapchangecfgfile, "", 0, "name of map change configuration file" );

// physic-related variables
CVAR_DEFINE_AUTO( sv_gravity, "800", FCVAR_MOVEVARS, "world gravity value" );
CVAR_DEFINE_AUTO( sv_stopspeed, "100", FCVAR_MOVEVARS, "how fast you come to a complete stop" );
CVAR_DEFINE_AUTO( sv_maxspeed, "320", FCVAR_MOVEVARS, "maximum speed a player can accelerate to when on ground" );
CVAR_DEFINE_AUTO( sv_spectatormaxspeed, "500", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "maximum speed a spectator can accelerate in air" );
CVAR_DEFINE_AUTO( sv_accelerate, "10", FCVAR_MOVEVARS, "rate at which a player accelerates to sv_maxspeed" );
CVAR_DEFINE_AUTO( sv_airaccelerate, "10", FCVAR_MOVEVARS, "rate at which a player accelerates to sv_maxspeed while in the air" );
CVAR_DEFINE_AUTO( sv_wateraccelerate, "10", FCVAR_MOVEVARS, "rate at which a player accelerates to sv_maxspeed while in the water" );
CVAR_DEFINE_AUTO( sv_friction, "4", FCVAR_MOVEVARS, "how fast you slow down" );
CVAR_DEFINE( sv_edgefriction, "edgefriction", "2", FCVAR_MOVEVARS, "how much you slow down when nearing a ledge you might fall off" );
CVAR_DEFINE_AUTO( sv_waterfriction, "1", FCVAR_MOVEVARS, "how fast you slow down in water" );
CVAR_DEFINE_AUTO( sv_bounce, "1", FCVAR_MOVEVARS, "bounce factor for entities with MOVETYPE_BOUNCE" );
CVAR_DEFINE_AUTO( sv_stepsize, "18", FCVAR_MOVEVARS, "how high you and NPS's can step up" );
CVAR_DEFINE_AUTO( sv_maxvelocity, "2000", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "max velocity for all things in the world" );
CVAR_DEFINE_AUTO( sv_zmax, "4096", FCVAR_MOVEVARS|FCVAR_SPONLY, "maximum viewable distance" );
CVAR_DEFINE_AUTO( sv_wateramp, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "world waveheight factor" );
CVAR_DEFINE( sv_footsteps, "mp_footsteps", "1", FCVAR_MOVEVARS, "world gravity value" );
CVAR_DEFINE_AUTO( sv_skyname, "desert", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "skybox name (can be dynamically changed in-game)" );
CVAR_DEFINE_AUTO( sv_rollangle, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED|FCVAR_ARCHIVE, "how much to tilt the view when strafing" );
CVAR_DEFINE_AUTO( sv_rollspeed, "200", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "how much strafing is necessary to tilt the view" );
CVAR_DEFINE_AUTO( sv_skycolor_r, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "skylight red component value" );
CVAR_DEFINE_AUTO( sv_skycolor_g, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "skylight green component value" );
CVAR_DEFINE_AUTO( sv_skycolor_b, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "skylight blue component value" );
CVAR_DEFINE_AUTO( sv_skyvec_x, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "skylight direction by x-axis" );
CVAR_DEFINE_AUTO( sv_skyvec_y, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "skylight direction by y-axis" );
CVAR_DEFINE_AUTO( sv_skyvec_z, "0", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "skylight direction by z-axis" );
CVAR_DEFINE_AUTO( sv_wateralpha, "1", FCVAR_MOVEVARS|FCVAR_UNLOGGED, "world surfaces water transparency factor. 1.0 - solid, 0.0 - fully transparent" );
CVAR_DEFINE_AUTO( sv_background_freeze, "1", FCVAR_ARCHIVE, "freeze player movement on background maps (e.g. to prevent falling)" );
CVAR_DEFINE_AUTO( showtriggers, "0", FCVAR_LATCH, "debug cvar shows triggers" );
CVAR_DEFINE_AUTO( sv_airmove, "1", FCVAR_SERVER, "obsolete, compatibility issues" );
CVAR_DEFINE_AUTO( sv_version, "", FCVAR_READ_ONLY, "engine version string" );
CVAR_DEFINE_AUTO( hostname, "", FCVAR_SERVER|FCVAR_PRINTABLEONLY, "name of current host" );
CVAR_DEFINE_AUTO( sv_fps, "0.0", FCVAR_SERVER, "server framerate" );

// gore-related cvars
CVAR_DEFINE_AUTO( violence_hblood, "1", 0, "draw human blood" );
CVAR_DEFINE_AUTO( violence_ablood, "1", 0, "draw alien blood" );
CVAR_DEFINE_AUTO( violence_hgibs, "1", 0, "show human gib entities" );
CVAR_DEFINE_AUTO( violence_agibs, "1", 0, "show alien gib entities" );

// voice chat
CVAR_DEFINE_AUTO( sv_voiceenable, "1", FCVAR_ARCHIVE|FCVAR_SERVER, "enable voice support" );
CVAR_DEFINE_AUTO( sv_voicequality, "3", FCVAR_ARCHIVE|FCVAR_SERVER, "voice chat quality level, from 0 to 5, higher is better" );

// enttools
CVAR_DEFINE_AUTO( sv_enttools_enable, "0", FCVAR_ARCHIVE|FCVAR_PROTECTED, "enable powerful and dangerous entity tools" );
CVAR_DEFINE_AUTO( sv_enttools_maxfire, "5", FCVAR_ARCHIVE|FCVAR_PROTECTED, "limit ent_fire actions count to prevent flooding" );

convar_t	*sv_novis;			// disable server culling entities by vis
convar_t	*sv_pausable;
convar_t	*timeout;				// seconds without any message
convar_t	*sv_lighting_modulate;
convar_t	*sv_maxclients;
convar_t	*sv_check_errors;
convar_t	*public_server;			// should heartbeats be sent
convar_t	*sv_reconnect_limit;		// minimum seconds between connect messages
convar_t	*sv_validate_changelevel;
convar_t	*sv_sendvelocity;
convar_t	*sv_hostmap;

convar_t	*sv_allow_noinputdevices;
convar_t	*sv_allow_touch;
convar_t	*sv_allow_mouse;
convar_t	*sv_allow_joystick;
convar_t	*sv_allow_vr;

static void Master_Heartbeat( void );

//============================================================================
/*
================
SV_HasActivePlayers

returns true if server have spawned players
================
*/
qboolean SV_HasActivePlayers( void )
{
	int	i;

	// server inactive
	if( !svs.clients ) return false;

	for( i = 0; i < svs.maxclients; i++ )
	{
		if( svs.clients[i].state == cs_spawned )
			return true;
	}
	return false;
}

/*
===================
SV_UpdateMovevars

check movevars for changes every frame
send updates to client if changed
===================
*/
void SV_UpdateMovevars( qboolean initialize )
{
	if( sv.state == ss_dead )
		return;

	if( !initialize && !host.movevars_changed )
		return;

	// check range
	if( sv_zmax.value < 256.0f ) Cvar_SetValue( "sv_zmax", 256.0f );

	// clamp it right
	if( FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ))
	{
		if( sv_zmax.value > 131070.0f )
			Cvar_SetValue( "sv_zmax", 131070.0f );
	}
	else
	{
		if( sv_zmax.value > 32767.0f )
			Cvar_SetValue( "sv_zmax", 32767.0f );
	}

	svgame.movevars.gravity = sv_gravity.value;
	svgame.movevars.stopspeed = sv_stopspeed.value;
	svgame.movevars.maxspeed = sv_maxspeed.value;
	svgame.movevars.spectatormaxspeed = sv_spectatormaxspeed.value;
	svgame.movevars.accelerate = sv_accelerate.value;
	svgame.movevars.airaccelerate = sv_airaccelerate.value;
	svgame.movevars.wateraccelerate = sv_wateraccelerate.value;
	svgame.movevars.friction = sv_friction.value;
	svgame.movevars.edgefriction = sv_edgefriction.value;
	svgame.movevars.waterfriction = sv_waterfriction.value;
	svgame.movevars.bounce = sv_bounce.value;
	svgame.movevars.stepsize = sv_stepsize.value;
	svgame.movevars.maxvelocity = sv_maxvelocity.value;
	svgame.movevars.zmax = sv_zmax.value;
	svgame.movevars.waveHeight = sv_wateramp.value;
	Q_strncpy( svgame.movevars.skyName, sv_skyname.string, sizeof( svgame.movevars.skyName ));
	svgame.movevars.footsteps = sv_footsteps.value;
	svgame.movevars.rollangle = sv_rollangle.value;
	svgame.movevars.rollspeed = sv_rollspeed.value;
	svgame.movevars.skycolor_r = sv_skycolor_r.value;
	svgame.movevars.skycolor_g = sv_skycolor_g.value;
	svgame.movevars.skycolor_b = sv_skycolor_b.value;
	svgame.movevars.skyvec_x = sv_skyvec_x.value;
	svgame.movevars.skyvec_y = sv_skyvec_y.value;
	svgame.movevars.skyvec_z = sv_skyvec_z.value;
	svgame.movevars.wateralpha = sv_wateralpha.value;
	svgame.movevars.features = host.features; // just in case. not really need
	svgame.movevars.entgravity = 1.0f;

	if( initialize ) return; // too early

	if( MSG_WriteDeltaMovevars( &sv.reliable_datagram, &svgame.oldmovevars, &svgame.movevars ))
		memcpy( &svgame.oldmovevars, &svgame.movevars, sizeof( movevars_t )); // oldstate changed

	host.movevars_changed = false;
}

/*
=================
SV_CheckCmdTimes
=================
*/
void SV_CheckCmdTimes( void )
{
	sv_client_t	*cl;
	static double	lastreset = 0;
	float		diff;
	int		i;

	if( sv_fps.value != 0.0f )
	{
		if( sv_fps.value < MIN_FPS )
			Cvar_SetValue( "sv_fps", MIN_FPS );

		if( sv_fps.value > MAX_FPS )
			Cvar_SetValue( "sv_fps", MAX_FPS );
	}

	if( Host_IsLocalGame( ))
		return;

	if(( host.realtime - lastreset ) < 1.0 )
		return;

	lastreset = host.realtime;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state != cs_spawned )
			continue;

		if( cl->connecttime == 0.0 )
		{
			cl->connecttime = host.realtime;
		}

		diff = cl->connecttime + cl->cmdtime - host.realtime;

		if( diff > net_clockwindow->value )
		{
			cl->ignorecmdtime = net_clockwindow->value + host.realtime;
			cl->cmdtime = host.realtime - cl->connecttime;
		}
		else if( diff < -net_clockwindow->value )
		{
			cl->cmdtime = host.realtime - cl->connecttime;
		}
	}
}

/*
=================
SV_ProcessFile

process incoming file (customization)
=================
*/
void SV_ProcessFile( sv_client_t *cl, const char *filename )
{
	customization_t	*pList;
	resource_t	*resource;
	resource_t	*next;
	byte		md5[16];
	qboolean		bFound;
	qboolean		bError;

	if( filename[0] != '!' )
	{
		Con_Printf( "Ignoring non-customization file upload of %s\n", filename );
		return;
	}

	COM_HexConvert( filename + 4, 32, md5 );

	for( resource = cl->resourcesneeded.pNext; resource != &cl->resourcesneeded; resource = next )
	{
		next = resource->pNext;

		if( !memcmp( resource->rgucMD5_hash, md5, 16 ))
			break;
	}

	if( resource == &cl->resourcesneeded )
	{
		Con_Printf( "SV_ProcessFile:  Unrequested decal\n" );
		return;
	}

	if( resource->nDownloadSize != cl->netchan.tempbuffersize )
	{
		Con_Printf( "Downloaded %i bytes for purported %i byte file\n", cl->netchan.tempbuffersize, resource->nDownloadSize );
		return;
	}

	HPAK_AddLump( true, CUSTOM_RES_PATH, resource, cl->netchan.tempbuffer, NULL );
	ClearBits( resource->ucFlags, RES_WASMISSING );
	SV_MoveToOnHandList( cl, resource );

	bError = false;
	bFound = false;

	for( pList = cl->customdata.pNext; pList; pList = pList->pNext )
	{
		if( !memcmp( pList->resource.rgucMD5_hash, resource->rgucMD5_hash, 16 ))
		{
			bFound = true;
			break;
		}
	}

	if( !bFound )
	{
		if( !COM_CreateCustomization( &cl->customdata, resource, -1, FCUST_FROMHPAK|FCUST_WIPEDATA|FCUST_IGNOREINIT, NULL, NULL ))
			bError = true;
	}
	else
	{
		Con_DPrintf( "Duplicate resource received and ignored.\n" );
	}

	if( bError ) Con_Printf( S_ERROR "parsing custom decal from %s\n", cl->name );
}

/*
=================
SV_ReadPackets
=================
*/
void SV_ReadPackets( void )
{
	sv_client_t	*cl;
	int		i, qport;
	size_t		curSize;

	while( NET_GetPacket( NS_SERVER, &net_from, net_message_buffer, &curSize ))
	{
		MSG_Init( &net_message, "ClientPacket", net_message_buffer, curSize );

		// check for connectionless packet (0xffffffff) first
		if( MSG_GetMaxBytes( &net_message ) >= 4 && *(int *)net_message.pData == -1 )
		{
			if( !svs.initialized )
			{
				char	*args;
				const char *c;

				MSG_Clear( &net_message  );
				MSG_ReadLong( &net_message  );// skip the -1 marker

				args = MSG_ReadStringLine( &net_message  );
				Cmd_TokenizeString( args );
				c = Cmd_Argv( 0 );

				if( !Q_strcmp( c, "rcon" ))
					SV_RemoteCommand( net_from, &net_message );
			}
			else SV_ConnectionlessPacket( net_from, &net_message );

			continue;
		}

		// read the qport out of the message so we can fix up
		// stupid address translating routers
		MSG_Clear( &net_message );
		MSG_ReadLong( &net_message );	// sequence number
		MSG_ReadLong( &net_message );	// sequence number
		qport = (int)MSG_ReadShort( &net_message ) & 0xffff;

		// check for packets from connected clients
		for( i = 0, sv.current_client = svs.clients; i < svs.maxclients; i++, sv.current_client++ )
		{
			cl = sv.current_client;

			if( cl->state == cs_free || FBitSet( cl->flags, FCL_FAKECLIENT ))
				continue;

			if( !NET_CompareBaseAdr( net_from, cl->netchan.remote_address ))
				continue;

			if( cl->netchan.qport != qport )
				continue;

			if( cl->netchan.remote_address.port != net_from.port )
				cl->netchan.remote_address.port = net_from.port;

			if( Netchan_Process( &cl->netchan, &net_message ))
			{
				if(( svs.maxclients == 1 && !host_limitlocal->value ) || ( cl->state != cs_spawned ))
					SetBits( cl->flags, FCL_SEND_NET_MESSAGE ); // reply at end of frame

				// this is a valid, sequenced packet, so process it
				if( cl->frames != NULL && cl->state != cs_zombie )
				{
					SV_ExecuteClientMessage( cl, &net_message );
					svgame.globals->frametime = sv.frametime;
					svgame.globals->time = sv.time;
				}
			}

			// fragmentation/reassembly sending takes priority over all game messages, want this in the future?
			if( Netchan_IncomingReady( &cl->netchan ))
			{
				if( Netchan_CopyNormalFragments( &cl->netchan, &net_message, &curSize ))
				{
					MSG_Init( &net_message, "ClientPacket", net_message_buffer, curSize );

					if(( svs.maxclients == 1 && !host_limitlocal->value ) || ( cl->state != cs_spawned ))
						SetBits( cl->flags, FCL_SEND_NET_MESSAGE ); // reply at end of frame

					// this is a valid, sequenced packet, so process it
					if( cl->frames != NULL && cl->state != cs_zombie )
					{
						SV_ExecuteClientMessage( cl, &net_message );
						svgame.globals->frametime = sv.frametime;
						svgame.globals->time = sv.time;
					}
				}

				if( Netchan_CopyFileFragments( &cl->netchan, &net_message ))
				{
					SV_ProcessFile( cl, cl->netchan.incomingfilename );
				}
			}
			break;
		}

		if( i != svs.maxclients )
			continue;
	}

	sv.current_client = NULL;
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->value
seconds, drop the conneciton.  Server frames are used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the sv_client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
void SV_CheckTimeouts( void )
{
	sv_client_t	*cl;
	double		droppoint;
	int		i, numclients = 0;

	droppoint = host.realtime - timeout->value;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state >= cs_connected )
		{
			if( cl->edict && !FBitSet( cl->edict->v.flags, FL_SPECTATOR|FL_FAKECLIENT ))
				numclients++;
		}

		// fake clients do not timeout
		if( FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		// FIXME: get rid of the zombie state
		if( cl->state == cs_zombie )
		{
			cl->state = cs_free; // can now be reused
			continue;
		}

		if(( cl->state == cs_connected || cl->state == cs_spawned ) && cl->netchan.last_received < droppoint )
		{
			if( !NET_IsLocalAddress( cl->netchan.remote_address ))
			{
				SV_BroadcastPrintf( NULL, "%s timed out\n", cl->name );
				SV_DropClient( cl, false );
				cl->state = cs_free; // don't bother with zombie state
			}
		}
	}

	if( svs.maxclients > 1 && sv.paused && !numclients )
	{
		// nobody left, unpause the server
		SV_TogglePause( "Pause released since no players are left." );
	}
}

/*
================
SV_PrepWorldFrame

This has to be done before the world logic, because
player processing happens outside RunWorldFrame
================
*/
void SV_PrepWorldFrame( void )
{
	edict_t	*ent;
	int	i;

	for( i = 1; i < svgame.numEntities; i++ )
	{
		ent = EDICT_NUM( i );
		if( ent->free ) continue;

		ClearBits( ent->v.effects, EF_MUZZLEFLASH|EF_NOINTERP );
	}

	if( svgame.physFuncs.pfnPrepWorldFrame != NULL )
		svgame.physFuncs.pfnPrepWorldFrame();
}

/*
=================
SV_IsSimulating
=================
*/
qboolean SV_IsSimulating( void )
{
	if( sv.background && SV_Active() && CL_Active())
	{
		if( CL_IsInConsole( ))
			return false;
		return true; // force simulating for background map
	}

	if( Host_IsDedicated() )
		return true; // always active for dedicated servers

	if( !SV_HasActivePlayers( ))
		return false;

	// allow to freeze everything in singleplayer
	if( svs.maxclients <= 1 && sv.playersonly )
		return false;

	if( !sv.paused && CL_IsInGame( ))
		return true;

	return false;
}

/*
=================
SV_RunGameFrame
=================
*/
qboolean SV_RunGameFrame( void )
{
	sv.simulating = SV_IsSimulating();

	if( !sv.simulating )
		return true;

	if( sv_fps.value != 0.0f )
	{
		double		fps = (1.0 / (double)( sv_fps.value - 0.01f )); // FP issues
		int		numFrames = 0;

		while( sv.time_residual >= fps )
		{
			sv.frametime = fps;

			SV_Physics();

			sv.time_residual -= fps;
			sv.time += fps;
			numFrames++;
		}

		return (numFrames != 0);
	}
	else
	{
		SV_Physics();
		sv.time += sv.frametime;
		return true;
	}
}

/*
==================
Host_ServerFrame

==================
*/
void Host_ServerFrame( void )
{
	// if server is not active, do nothing
	if( !svs.initialized ) return;

	if( sv_fps.value != 0.0f && ( sv.simulating || sv.state != ss_active ))
		sv.time_residual += host.frametime;

	if( sv_fps.value == 0.0f )
		sv.frametime = host.frametime;
	svgame.globals->frametime = sv.frametime;

	// check clients timewindow
	SV_CheckCmdTimes ();

	// read packets from clients
	SV_ReadPackets ();

	// refresh physic movevars on the client side
	SV_UpdateMovevars ( false );

	// request missing resources for clients
	SV_RequestMissingResources();

	// check timeouts
	SV_CheckTimeouts ();

	// let everything in the world think and move
	if( !SV_RunGameFrame ()) return;

	// send messages back to the clients that had packets read this frame
	SV_SendClientMessages ();

	// clear edict flags for next frame
	SV_PrepWorldFrame ();

	// update dedicated server status line in console
	Platform_UpdateStatusLine ();

	// send a heartbeat to the master if needed
	Master_Heartbeat ();
}

/*
==================
Host_SetServerState
==================
*/
void Host_SetServerState( int state )
{
	Cvar_FullSet( "host_serverstate", va( "%i", state ), FCVAR_READ_ONLY );
	sv.state = state;
}

//============================================================================

/*
=================
Master_Add
=================
*/
static void Master_Add( void )
{
	sizebuf_t msg;
	char buf[16];
	uint challenge;

	NET_Config( true, false ); // allow remote

	svs.heartbeat_challenge = challenge = COM_RandomLong( 0, INT_MAX );

	MSG_Init( &msg, "Master Join", buf, sizeof( buf ));
	MSG_WriteBytes( &msg, "q\xFF", 2 );
	MSG_WriteDword( &msg, challenge );

	if( NET_SendToMasters( NS_SERVER, MSG_GetNumBytesWritten( &msg ), MSG_GetBuf( &msg )))
		svs.last_heartbeat = MAX_HEARTBEAT;
}

/*
================
Master_Heartbeat

Send a message to the master every few minutes to
let it know we are alive, and log information
================
*/
static void Master_Heartbeat( void )
{
	if(( !public_server->value && !sv_nat.value ) || svs.maxclients == 1 )
		return; // only public servers send heartbeats

	// check for time wraparound
	if( svs.last_heartbeat > host.realtime )
		svs.last_heartbeat = host.realtime;

	if(( host.realtime - svs.last_heartbeat ) < HEARTBEAT_SECONDS )
		return; // not time to send yet

	svs.last_heartbeat = host.realtime;

	Master_Add();
}

/*
=================
Master_Shutdown

Informs all masters that this server is going down
=================
*/
static void Master_Shutdown( void )
{
	NET_Config( true, false ); // allow remote
	while( NET_SendToMasters( NS_SERVER, 2, "\x62\x0A" ));
}

/*
=================
SV_AddToMaster

A server info answer to master server.
Master will validate challenge and this server to public list
=================
*/
void SV_AddToMaster( netadr_t from, sizebuf_t *msg )
{
	uint	challenge, challenge2;
	char	s[MAX_INFO_STRING] = "0\n"; // skip 2 bytes of header
	int	clients, bots;
	const int len = sizeof( s );

	if( !NET_IsMasterAdr( from ))
	{
		Con_Printf( S_WARN "unexpected master server info query packet from %s\n", NET_AdrToString( from ));
		return;
	}

	if( svs.last_heartbeat + sv_master_response_timeout.value < host.realtime )
	{
		Con_Printf( S_WARN "unexpected master server info query packet (too late? try increasing sv_master_response_timeout value)\n");
		return;
	}

	challenge = MSG_ReadDword( msg );
	challenge2 = MSG_ReadDword( msg );

	if( challenge2 != svs.heartbeat_challenge )
	{
		Con_Printf( S_WARN "unexpected master server info query packet (wrong challenge!)\n" );
		return;
	}

	SV_GetPlayerCount( &clients, &bots );
	Info_SetValueForKeyf( s, "protocol", len, "%d", PROTOCOL_VERSION ); // protocol version
	Info_SetValueForKeyf( s, "challenge", len, "%u", challenge ); // challenge number
	Info_SetValueForKeyf( s, "players", len, "%d", clients ); // current player number, without bots
	Info_SetValueForKeyf( s, "max", len, "%d", svs.maxclients ); // max_players
	Info_SetValueForKeyf( s, "bots", len, "%d", bots ); // bot count
	Info_SetValueForKey( s, "gamedir", GI->gamefolder, len ); // gamedir
	Info_SetValueForKey( s, "map", sv.name, len ); // current map
	Info_SetValueForKey( s, "type", (Host_IsDedicated()) ? "d" : "l", len ); // dedicated or local
	Info_SetValueForKey( s, "password", "0", len ); // is password set
	Info_SetValueForKey( s, "os", "w", len ); // Windows
	Info_SetValueForKey( s, "secure", "0", len ); // server anti-cheat
	Info_SetValueForKey( s, "lan", "0", len ); // LAN servers doesn't send info to master
	Info_SetValueForKey( s, "version", XASH_VERSION, len ); // server region. 255 -- all regions
	Info_SetValueForKey( s, "region", "255", len ); // server region. 255 -- all regions
	Info_SetValueForKey( s, "product", GI->gamefolder, len ); // product? Where is the difference with gamedir?
	Info_SetValueForKey( s, "nat", sv_nat.string, len ); // Server running under NAT, use reverse connection

	NET_SendPacket( NS_SERVER, Q_strlen( s ), s, from );
}

/*
====================
SV_ProcessUserAgent

send error message and return false on wrong input devices
====================
*/
qboolean SV_ProcessUserAgent( netadr_t from, const char *useragent )
{
	const char *input_devices_str = Info_ValueForKey( useragent, "d" );
	const char *id = Info_ValueForKey( useragent, "uuid" );

	if( !sv_allow_noinputdevices->value && ( !input_devices_str || !input_devices_str[0] ) )
	{
		SV_RejectConnection( from, "This server does not allow\nconnect without input devices list.\nPlease update your engine.\n" );
		return false;
	}

	if( input_devices_str )
	{
		int input_devices = Q_atoi( input_devices_str );

		if( !sv_allow_touch->value && ( input_devices & INPUT_DEVICE_TOUCH ) )
		{
			SV_RejectConnection( from, "This server does not allow touch\nDisable it (touch_enable 0)\nto play on this server\n" );
			return false;
		}
		if( !sv_allow_mouse->value && ( input_devices & INPUT_DEVICE_MOUSE) )
		{
			SV_RejectConnection( from, "This server does not allow mouse\nDisable it(m_ignore 1)\nto play on this server\n" );
			return false;
		}
		if( !sv_allow_joystick->value && ( input_devices & INPUT_DEVICE_JOYSTICK) )
		{
			SV_RejectConnection( from, "This server does not allow joystick\nDisable it(joy_enable 0)\nto play on this server\n" );
			return false;
		}
		if( !sv_allow_vr->value && ( input_devices & INPUT_DEVICE_VR) )
		{
			SV_RejectConnection( from, "This server does not allow VR\n" );
			return false;
		}
	}

	if( id )
	{
		qboolean banned = SV_CheckID( id );

		if( banned )
		{
			SV_RejectConnection( from, "You are banned!\n" );
			return false;
		}
	}

	return true;
}

//============================================================================

/*
===============
SV_Init

Only called at startup, not for each game
===============
*/
void SV_Init( void )
{
	string	versionString;

	SV_InitHostCommands();

	Cvar_Getf( "protocol", FCVAR_READ_ONLY, "displays server protocol version", "%i", PROTOCOL_VERSION );
	Cvar_Get( "suitvolume", "0.25", FCVAR_ARCHIVE, "HEV suit volume" );
	Cvar_Get( "sv_background", "0", FCVAR_READ_ONLY, "indicate what background map is running" );
	Cvar_Get( "gamedir", GI->gamefolder, FCVAR_READ_ONLY, "game folder" );
	Cvar_Get( "sv_alltalk", "1", 0, "allow to talking for all players (legacy, unused)" );
	Cvar_Get( "sv_allow_PhysX", "1", FCVAR_ARCHIVE, "allow XashXT to usage PhysX engine" );			// XashXT cvar
	Cvar_Get( "sv_precache_meshes", "1", FCVAR_ARCHIVE, "cache SOLID_CUSTOM meshes before level loading" );	// Paranoia 2 cvar
	Cvar_Get( "servercfgfile", "server.cfg", 0, "name of dedicated server configuration file" );
	Cvar_Get( "lservercfgfile", "listenserver.cfg", 0, "name of listen server configuration file" );

	Cvar_RegisterVariable( &sv_zmax );
	Cvar_RegisterVariable( &sv_wateramp );
	Cvar_RegisterVariable( &sv_skycolor_r );
	Cvar_RegisterVariable( &sv_skycolor_g );
	Cvar_RegisterVariable( &sv_skycolor_b );
	Cvar_RegisterVariable( &sv_skyvec_x );
	Cvar_RegisterVariable( &sv_skyvec_y );
	Cvar_RegisterVariable( &sv_skyvec_z );
	Cvar_RegisterVariable( &sv_skyname );
	Cvar_RegisterVariable( &sv_footsteps );
	Cvar_RegisterVariable( &sv_wateralpha );
	Cvar_RegisterVariable( &sv_minupdaterate );
	Cvar_RegisterVariable( &sv_maxupdaterate );
	Cvar_RegisterVariable( &sv_minrate );
	Cvar_RegisterVariable( &sv_maxrate );
	Cvar_RegisterVariable( &sv_cheats );
	Cvar_RegisterVariable( &sv_airmove );
	Cvar_RegisterVariable( &sv_fps );
	Cvar_RegisterVariable( &showtriggers );
	Cvar_RegisterVariable( &sv_aim );
	Cvar_RegisterVariable( &deathmatch );
	Cvar_RegisterVariable( &coop );
	Cvar_RegisterVariable( &teamplay );
	Cvar_RegisterVariable( &skill );
	Cvar_RegisterVariable( &temp1 );

	Cvar_RegisterVariable( &rcon_password );
	Cvar_RegisterVariable( &rcon_enable );
	Cvar_RegisterVariable( &sv_stepsize );
	Cvar_RegisterVariable( &sv_newunit );
	Cvar_RegisterVariable( &hostname );
	timeout = Cvar_Get( "timeout", "125", FCVAR_SERVER, "connection timeout" );
	sv_pausable = Cvar_Get( "pausable", "1", FCVAR_SERVER, "allow players to pause or not" );
	sv_validate_changelevel = Cvar_Get( "sv_validate_changelevel", "0", 0, "test change level for level-designer errors" );
	Cvar_RegisterVariable( &sv_clienttrace );
	Cvar_RegisterVariable( &sv_bounce );
	Cvar_RegisterVariable( &sv_spectatormaxspeed );
	Cvar_RegisterVariable( &sv_waterfriction );
	Cvar_RegisterVariable( &sv_wateraccelerate );
	Cvar_RegisterVariable( &sv_rollangle );
	Cvar_RegisterVariable( &sv_rollspeed );
	Cvar_RegisterVariable( &sv_airaccelerate );
	Cvar_RegisterVariable( &sv_maxvelocity );
	Cvar_RegisterVariable( &sv_gravity );
	Cvar_RegisterVariable( &sv_maxspeed );
	Cvar_RegisterVariable( &sv_accelerate );
	Cvar_RegisterVariable( &sv_friction );
	Cvar_RegisterVariable( &sv_edgefriction );
	Cvar_RegisterVariable( &sv_stopspeed );
	sv_maxclients = Cvar_Get( "maxplayers", "1", FCVAR_LATCH, "server max capacity" );
	sv_check_errors = Cvar_Get( "sv_check_errors", "0", FCVAR_ARCHIVE, "check edicts for errors" );
	public_server = Cvar_Get ("public", "0", 0, "change server type from private to public" );
	sv_lighting_modulate = Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	sv_reconnect_limit = Cvar_Get ("sv_reconnect_limit", "3", FCVAR_ARCHIVE, "max reconnect attempts" );
	Cvar_RegisterVariable( &sv_failuretime );
	Cvar_RegisterVariable( &sv_unlag );
	Cvar_RegisterVariable( &sv_maxunlag );
	Cvar_RegisterVariable( &sv_unlagpush );
	Cvar_RegisterVariable( &sv_unlagsamples );
	Cvar_RegisterVariable( &sv_allow_upload );
	Cvar_RegisterVariable( &sv_allow_download );
	Cvar_RegisterVariable( &sv_send_logos );
	Cvar_RegisterVariable( &sv_send_resources );
	Cvar_RegisterVariable( &sv_uploadmax );
	Cvar_RegisterVariable( &sv_version );
	Cvar_RegisterVariable( &sv_instancedbaseline );
	Cvar_RegisterVariable( &sv_consistency );
	Cvar_RegisterVariable( &sv_downloadurl );
	sv_novis = Cvar_Get( "sv_novis", "0", 0, "force to ignore server visibility" );
	sv_hostmap = Cvar_Get( "hostmap", GI->startmap, 0, "keep name of last entered map" );
	Cvar_RegisterVariable( &sv_password );
	Cvar_RegisterVariable( &sv_lan );
	Cvar_RegisterVariable( &sv_nat );
	Cvar_RegisterVariable( &violence_ablood );
	Cvar_RegisterVariable( &violence_hblood );
	Cvar_RegisterVariable( &violence_agibs );
	Cvar_RegisterVariable( &violence_hgibs );
	Cvar_RegisterVariable( &mp_logecho );
	Cvar_RegisterVariable( &mp_logfile );
	Cvar_RegisterVariable( &sv_log_onefile );
	Cvar_RegisterVariable( &sv_log_singleplayer );
	Cvar_RegisterVariable( &sv_master_response_timeout );

	Cvar_RegisterVariable( &sv_background_freeze );
	Cvar_RegisterVariable( &sv_autosave );

	Cvar_RegisterVariable( &mapcyclefile );
	Cvar_RegisterVariable( &motdfile );
	Cvar_RegisterVariable( &logsdir );
	Cvar_RegisterVariable( &bannedcfgfile );
	Cvar_RegisterVariable( &listipcfgfile );
	Cvar_RegisterVariable( &mapchangecfgfile );

	Cvar_RegisterVariable( &sv_voiceenable );
	Cvar_RegisterVariable( &sv_voicequality );
	Cvar_RegisterVariable( &sv_trace_messages );
	Cvar_RegisterVariable( &sv_enttools_enable );
	Cvar_RegisterVariable( &sv_enttools_maxfire );

	sv_allow_joystick = Cvar_Get( "sv_allow_joystick", "1", FCVAR_ARCHIVE, "allow connect with joystick enabled" );
	sv_allow_mouse = Cvar_Get( "sv_allow_mouse", "1", FCVAR_ARCHIVE, "allow connect with mouse" );
	sv_allow_touch = Cvar_Get( "sv_allow_touch", "1", FCVAR_ARCHIVE, "allow connect with touch controls" );
	sv_allow_vr = Cvar_Get( "sv_allow_vr", "1", FCVAR_ARCHIVE, "allow connect from vr version" );
	sv_allow_noinputdevices = Cvar_Get( "sv_allow_noinputdevices", "1", FCVAR_ARCHIVE, "allow connect from old versions without useragent" );

	// when we in developer-mode automatically turn cheats on
	if( host_developer.value ) Cvar_SetValue( "sv_cheats", 1.0f );

	MSG_Init( &net_message, "NetMessage", net_message_buffer, sizeof( net_message_buffer ));

	Q_snprintf( versionString, sizeof( versionString ), XASH_ENGINE_NAME ": " XASH_VERSION "-%s(%s-%s),%i,%i",
		Q_buildcommit(), Q_buildos(), Q_buildarch(), PROTOCOL_VERSION, Q_buildnum() );

	Cvar_FullSet( "sv_version", versionString, FCVAR_READ_ONLY );

	SV_InitFilter();
	SV_ClearGameState ();	// delete all temporary *.hl files
	SV_InitGame();
}

/*
==================
SV_FinalMessage

Used by SV_Shutdown to send a final message to all
connected clients before the server goes down.  The messages are sent immediately,
not just stuck on the outgoing message list, because the server is going
to totally exit after returning from this function.
==================
*/
void SV_FinalMessage( const char *message, qboolean reconnect )
{
	byte		msg_buf[1024];
	sv_client_t	*cl;
	sizebuf_t		msg;
	int		i;

	MSG_Init( &msg, "FinalMessage", msg_buf, sizeof( msg_buf ));

	if( COM_CheckString( message ))
	{
		MSG_BeginServerCmd( &msg, svc_print );
		MSG_WriteString( &msg, message );
	}

	if( reconnect )
	{
		if( svs.maxclients <= 1 )
		{
			MSG_BeginServerCmd( &msg, svc_changing );
			MSG_WriteOneBit( &msg, GameState->loadGame );
		}
		else SV_BuildReconnect( &msg );
	}
	else
	{
		MSG_BeginServerCmd( &msg, svc_disconnect );
	}

	// send it twice
	// stagger the packets to crutch operating system limited buffers
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
		if( cl->state >= cs_connected && !FBitSet( cl->flags, FCL_FAKECLIENT ))
			Netchan_TransmitBits( &cl->netchan, MSG_GetNumBitsWritten( &msg ), MSG_GetData( &msg ));

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
		if( cl->state >= cs_connected && !FBitSet( cl->flags, FCL_FAKECLIENT ))
			Netchan_TransmitBits( &cl->netchan, MSG_GetNumBitsWritten( &msg ), MSG_GetData( &msg ));
}

/*
================
SV_FreeClients

release server clients
================
*/
void SV_FreeClients( void )
{
	if( svs.maxclients != 0 )
	{
		// free server static data
		if( svs.clients )
		{
			Z_Free( svs.clients );
			svs.clients = NULL;
		}

		if( svs.packet_entities )
		{
			Z_Free( svs.packet_entities );
			svs.packet_entities = NULL;
			svs.num_client_entities = 0;
			svs.next_client_entities = 0;
		}
	}
}

/*
================
SV_Shutdown

Called when each game quits,
before Sys_Quit or Sys_Error
================
*/
void SV_Shutdown( const char *finalmsg )
{
	// already freed
	if( !SV_Initialized( ))
	{
		// drop the client if want to load a new map
		if( CL_IsPlaybackDemo( ))
			CL_Drop();

		SV_UnloadProgs ();
		return;
	}

	if( COM_CheckString( finalmsg ))
		Con_Printf( "%s", finalmsg );

	// rcon will be disconnected
	SV_EndRedirect();

	if( svs.clients )
		SV_FinalMessage( finalmsg, false );

	if( public_server->value && svs.maxclients != 1 )
		Master_Shutdown();

	NET_Config( false, false );
	SV_UnloadProgs ();
	CL_Drop();

	// free current level
	memset( &sv, 0, sizeof( sv ));

	SV_FreeClients();
	svs.maxclients = 0;

	// release all models
	Mod_FreeAll();

	HPAK_FlushHostQueue();
	Log_Printf( "Server shutdown\n" );
	Log_Close();

	svs.initialized = false;
}
