/*
cl_parse.c - parse a message received from the server (GoldSrc 48 protocol)
Copyright (C) 2008 Uncle Mike

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
#include "particledef.h"
#include "cl_tent.h"
#include "shake.h"
#include "hltv.h"
#include "input.h"
#include "server.h"

static void CL_ParseExtraInfo( sizebuf_t *msg )
{
	string clientfallback;

	Q_strncpy( clientfallback, MSG_ReadString( msg ), sizeof( clientfallback ));
	Cvar_FullSet( "sv_cheats", MSG_ReadByte( msg ) ? "1" : "0", FCVAR_READ_ONLY | FCVAR_SERVER );
}

static void CL_ParseNewMovevars( sizebuf_t *msg )
{
	Delta_InitClient(); // finalize client delta's

	clgame.movevars.gravity           = MSG_ReadFloat( msg );
	clgame.movevars.stopspeed         = MSG_ReadFloat( msg );
	clgame.movevars.maxspeed          = MSG_ReadFloat( msg );
	clgame.movevars.spectatormaxspeed = MSG_ReadFloat( msg );
	clgame.movevars.accelerate        = MSG_ReadFloat( msg );
	clgame.movevars.airaccelerate     = MSG_ReadFloat( msg );
	clgame.movevars.wateraccelerate   = MSG_ReadFloat( msg );
	clgame.movevars.friction          = MSG_ReadFloat( msg );
	clgame.movevars.edgefriction      = MSG_ReadFloat( msg );
	clgame.movevars.waterfriction     = MSG_ReadFloat( msg );
	clgame.movevars.entgravity        = MSG_ReadFloat( msg );
	clgame.movevars.bounce            = MSG_ReadFloat( msg );
	clgame.movevars.stepsize          = MSG_ReadFloat( msg );
	clgame.movevars.maxvelocity       = MSG_ReadFloat( msg );
	clgame.movevars.zmax              = MSG_ReadFloat( msg );
	clgame.movevars.waveHeight        = MSG_ReadFloat( msg );
	clgame.movevars.footsteps         = MSG_ReadByte( msg );
	clgame.movevars.rollangle         = MSG_ReadFloat( msg );
	clgame.movevars.rollspeed         = MSG_ReadFloat( msg );
	clgame.movevars.skycolor_r        = MSG_ReadFloat( msg );
	clgame.movevars.skycolor_g        = MSG_ReadFloat( msg );
	clgame.movevars.skycolor_b        = MSG_ReadFloat( msg );
	clgame.movevars.skyvec_x          = MSG_ReadFloat( msg );
	clgame.movevars.skyvec_y          = MSG_ReadFloat( msg );
	clgame.movevars.skyvec_z          = MSG_ReadFloat( msg );

	Q_strncpy( clgame.movevars.skyName, MSG_ReadString( msg ), sizeof( clgame.movevars.skyName ));

	// water alpha is not allowed
	if( !FBitSet( world.flags, FWORLD_WATERALPHA ))
		clgame.movevars.wateralpha = 1.0f;

	// update sky if changed
	if( Q_strcmp( clgame.oldmovevars.skyName, clgame.movevars.skyName ) && cl.video_prepped )
		ref.dllFuncs.R_SetupSky( clgame.movevars.skyName );

	memcpy( &clgame.oldmovevars, &clgame.movevars, sizeof( movevars_t ));
	clgame.entities->curstate.scale = clgame.movevars.waveHeight;

	// keep features an actual!
	clgame.oldmovevars.features = clgame.movevars.features = host.features;
}

static void CL_ParseNewUserMsg( sizebuf_t *msg )
{
	int svc_num, size;
	char s[16];

	svc_num = MSG_ReadByte( msg );
	size = MSG_ReadByte( msg );
	MSG_ReadBytes( msg, s, sizeof( s ));

	s[15] = 0;
	if( size == 255 )
		size = -1;

	CL_LinkUserMessage( s, svc_num, size );
}

typedef struct delta_header_t
{
	qboolean remove : 1;
	qboolean custom : 1;
	qboolean instanced : 1;
	uint instanced_baseline_index : 6;
	uint offset : 6;

	int entnum;
} delta_header_t;

static void CL_ParseDeltaHeader( sizebuf_t *msg, qboolean delta, int oldnum, struct delta_header_t *hdr )
{
	hdr->remove = hdr->custom = hdr->instanced = false;
	hdr->instanced_baseline_index = hdr->offset = 0;
	hdr->entnum = oldnum;

	if( !delta )
	{
		// if we have one bit set, then it's a next entity in line
		// if we have next bit NON set, then it's a one of next 64 entities
		// if not, it's a new entity
		if( MSG_ReadOneBit( msg ))
			hdr->entnum++;
		else if( MSG_ReadOneBit( msg ) == 0 )
			hdr->entnum += MSG_ReadUBitLong( msg, 6 );
		else
			hdr->entnum += MSG_ReadUBitLong( msg, MAX_GOLDSRC_ENTITY_BITS );
	}
	else
	{
		// does this packet encode entity deletion?
		hdr->remove = MSG_ReadOneBit( msg );

		// same logic as above
		if( MSG_ReadOneBit( msg ) == 0 )
			hdr->entnum += MSG_ReadUBitLong( msg, 6 );
		else hdr->entnum = MSG_ReadUBitLong( msg, MAX_GOLDSRC_ENTITY_BITS );
	}

	// if we are not removing this entity
	if( !hdr->remove )
	{
		hdr->custom = MSG_ReadOneBit( msg );

		hdr->instanced = false;
		hdr->instanced_baseline_index = 0;

		// do we got instanced baselines in svc_spawnbaselines?
		if( cl.instanced_baseline_count )
		{
			hdr->instanced = MSG_ReadOneBit( msg );
			if( hdr->instanced )
				hdr->instanced_baseline_index = MSG_ReadUBitLong( msg, 6 );
		}

		hdr->offset = 0;
		if( !delta && !hdr->instanced )
		{
			if( MSG_ReadOneBit( msg ))
				hdr->offset = MSG_ReadUBitLong( msg, 6 );
		}
	}
}

static int CL_GetEntityDelta( const struct delta_header_t *hdr )
{
	if( hdr->custom )
		return DT_CUSTOM_ENTITY_STATE_T;

	if( CL_IsPlayerIndex( hdr->entnum ))
		return DT_ENTITY_STATE_PLAYER_T;

	return DT_ENTITY_STATE_T;
}

static void CL_FlushEntityPacketGS( frame_t *frame, sizebuf_t *msg )
{
	frame->valid = false;
	cl.validsequence = 0; // can't render a frame

	// read it all but ignore it
	while( 1 )
	{
		int oldnum = 0;
		entity_state_t from = { 0 }, to;
		delta_header_t hdr;

		if( MSG_ReadWord( msg ) != 0 )
		{
			MSG_SeekToBit( msg, -16, SEEK_CUR );

			CL_ParseDeltaHeader( msg, false, oldnum, &hdr );
		}
		else break;

		if( MSG_CheckOverflow( msg ))
			Host_Error( "%s: overflow\n", __func__ );

		if( hdr.remove )
			continue;

		Delta_ReadGSFields( msg, CL_GetEntityDelta( &hdr ), &from, &to, cl.mtime[0] );
	}

	MSG_EndBitWriting( msg );
}

static void CL_DeltaEntityGS( delta_header_t *hdr, sizebuf_t *msg, frame_t *frame, int newnum, entity_state_t *from, qboolean has_update )
{
	cl_entity_t	*ent;
	entity_state_t	*to;
	int pack = frame->num_entities;

	// alloc next slot to store update
	to = &cls.packet_entities[cls.next_client_entities % cls.num_client_entities];

	if(( newnum < 0 ) || ( newnum >= clgame.maxEntities ))
	{
		Con_DPrintf( S_ERROR "CL_DeltaEntity: invalid newnum: %d\n", newnum );
		Host_Error( "%s: bad delta entity number: %i", __func__, newnum );
		return;
	}

	ent = CL_EDICT_NUM( newnum );
	ent->index = newnum; // enumerate entity index
	if( !from )
	{
		if( hdr->instanced )
		{
			from = &cl.instanced_baseline[hdr->instanced_baseline_index];
		}
		else if( hdr->offset != 0 )
		{
			from = &cls.packet_entities[(cls.next_client_entities - hdr->offset) % cls.num_client_entities];
		}
		else
		{
			from = &ent->baseline;
		}
	}

	to->entityType = hdr->custom ? ENTITY_BEAM : ENTITY_NORMAL;
	to->number = newnum;

	if( has_update )
		Delta_ReadGSFields( msg, CL_GetEntityDelta( hdr ), from, to, cl.mtime[0] );
	else memcpy( to, from, sizeof( entity_state_t ));

	if( hdr->remove )
	{
		memset( to, 0, sizeof( *to ));
		to->number = -1;

		CL_KillDeadBeams( ent ); // release dead beams

		// this is for reference
		Con_DPrintf( "Entity %i was removed from server\n", newnum );
		return;
	}

	if( !from )
	{
		// interpolation must be reset
		SETVISBIT( frame->flags, pack );

		// release beams from previous entity
		CL_KillDeadBeams( ent );
	}

	// add entity to packet
	cls.next_client_entities++;
	frame->num_entities++;
}

static int CL_ParsePacketEntitiesGS( sizebuf_t *msg, qboolean delta )
{
	frame_t *frame, *oldframe;
	int oldindex, newnum, oldnum, numbase = 0;
	entity_state_t *oldent;
	int count;
	int playerbytes = 0;

	// save first uncompressed packet as timestamp
	if( cls.changelevel && !delta && cls.demorecording )
		CL_WriteDemoJumpTime();

	count = MSG_ReadWord( msg );

	frame = &cl.frames[cl.parsecountmod];
	memset( frame->flags, 0, sizeof( frame->flags ));
	frame->first_entity = cls.next_client_entities;
	frame->num_entities = 0;
	frame->valid = true;

	MSG_StartBitWriting( msg );

	if( delta )
	{
		uint oldpacket = MSG_ReadByte( msg );
		oldframe = &cl.frames[oldpacket & CL_UPDATE_MASK];

		if( !CL_ValidateDeltaPacket( oldpacket, oldframe ))
		{
			CL_FlushEntityPacketGS( frame, msg );
			return playerbytes;
		}
	}
	else
	{
		oldframe = NULL;
		cl.send_reply = true;
		cls.demowaiting = false;
	}

	cl.validsequence = cls.netchan.incoming_sequence;

	oldent = NULL;
	oldindex = 0;
	oldnum = CL_UpdateOldEntNum( oldindex, oldframe, &oldent );

	// read it all but ignore it
	while( 1 )
	{
		qboolean player;
		delta_header_t hdr;
		int val = MSG_ReadWord( msg );

		if( val )
		{
			MSG_SeekToBit( msg, -16, SEEK_CUR );
			CL_ParseDeltaHeader( msg, delta, numbase, &hdr );
			numbase = hdr.entnum;
		}
		else break;

		if( MSG_CheckOverflow( msg ))
			Host_Error( "%s: overflow\n", __func__ );

		newnum = hdr.entnum;
		player = CL_IsPlayerIndex( newnum );

		while( oldnum < newnum )
		{
			// one or more entities from the old packet are unchanged
			CL_DeltaEntityGS( &hdr, msg, frame, oldnum, oldent, false );
			oldnum = CL_UpdateOldEntNum( ++oldindex, oldframe, &oldent );
		}

		if( oldnum == newnum )
		{
			int bufstart = MSG_GetNumBytesRead( msg );
			CL_DeltaEntityGS( &hdr, msg, frame, newnum, oldent, !hdr.remove );
			if( player ) playerbytes += MSG_GetNumBytesRead( msg ) - bufstart;
			oldnum = CL_UpdateOldEntNum( ++oldindex, oldframe, &oldent );
		}
		else if( oldnum > newnum )
		{
			// delta from baseline ?
			int bufstart = MSG_GetNumBytesRead( msg );
			CL_DeltaEntityGS( &hdr, msg, frame, newnum, NULL, !hdr.remove );
			if( player ) playerbytes += MSG_GetNumBytesRead( msg ) - bufstart;
		}
	}

	if( MSG_CheckOverflow( msg ))
		Host_Error( "%s: overflow\n", __func__ );

	// any remaining entities in the old frame are copied over
	while( oldnum != MAX_ENTNUMBER )
	{
		// one or more entities from the old packet are unchanged
		delta_header_t hdr =
		{
			.custom = FBitSet( oldent->entityType, ENTITY_BEAM ),
			.entnum = oldnum,
		};
		CL_DeltaEntityGS( &hdr, msg, frame, oldnum, oldent, false );
		oldnum = CL_UpdateOldEntNum( ++oldindex, oldframe, &oldent );
	}

	MSG_EndBitWriting( msg );

	if( frame->num_entities != count )
		Con_Reportf( S_WARN "CL_Parse%sPacketEntitiesGS: (%i should be %i)\n", delta ? "Delta" : "", frame->num_entities, count );

	if( !frame->valid )
		return playerbytes;

	CL_ProcessPacket( frame );
	CL_SetSolidEntities();

	// first update is the final signon stage where we actually receive an entity (i.e., the world at least)
	if( cls.signon == ( SIGNONS - 1 ))
	{
		// we are done with signon sequence.
		cls.signon = SIGNONS;

		// Clear loading plaque.
		CL_SignonReply ();
	}

	return playerbytes;
}

static float MSG_ReadGSBitCoord( sizebuf_t *sb )
{
	float value = 0;
	int ival, fval;

	ival = MSG_ReadOneBit( sb );
	fval = MSG_ReadOneBit( sb );

	if( ival || fval )
	{
		int sign = MSG_ReadOneBit( sb );

		if( ival )
			ival = MSG_ReadUBitLong( sb, 12 );
		if( fval )
			fval = MSG_ReadUBitLong( sb, 3 );

		value = (float)( fval / 8.0 + ival );
		if( sign )
			value = -value;
	}

	return value;
}

static void MSG_ReadGSBitVec3Coord( sizebuf_t *sb, vec3_t fa )
{
	qboolean x, y, z;

	VectorClear( fa );

	x = MSG_ReadOneBit( sb );
	y = MSG_ReadOneBit( sb );
	z = MSG_ReadOneBit( sb );
	if( x )
		fa[0] = MSG_ReadGSBitCoord( sb );
	if( y )
		fa[1] = MSG_ReadGSBitCoord( sb );
	if( z )
		fa[2] = MSG_ReadGSBitCoord( sb );
}

static void CL_ParseSoundPacketGS( sizebuf_t *msg )
{
	vec3_t	pos;
	int 	chan, sound;
	float 	volume, attn;
	int	flags, pitch, entnum;
	sound_t	handle = 0;

	MSG_StartBitWriting( msg );

	flags = MSG_ReadUBitLong( msg, 9 );

	if( FBitSet( flags, SND_VOLUME ))
		volume = (float)MSG_ReadByte( msg ) / 255.0f;
	else volume = VOL_NORM;

	if( FBitSet( flags, SND_ATTENUATION ))
		attn = (float)MSG_ReadByte( msg ) / 64.0f;
	else attn = ATTN_NONE;

	chan = MSG_ReadUBitLong( msg, 3 );
	entnum = MSG_ReadUBitLong( msg, MAX_GOLDSRC_ENTITY_BITS );
	sound = MSG_ReadUBitLong( msg, FBitSet( flags, SND_SEQUENCE ) ? 16 : 8 );
	MSG_ReadGSBitVec3Coord( msg, pos );

	if( FBitSet( flags, SND_PITCH ))
		pitch = MSG_ReadByte( msg );
	else pitch = PITCH_NORM;

	MSG_EndBitWriting( msg );

	if( FBitSet( flags, SND_SENTENCE ))
	{
		char	sentenceName[32];

		if( FBitSet( flags, SND_SEQUENCE ))
			Q_snprintf( sentenceName, sizeof( sentenceName ), "!#%i", sound + MAX_SOUNDS_NONSENTENCE );
		else Q_snprintf( sentenceName, sizeof( sentenceName ), "!%i", sound );

		handle = S_RegisterSound( sentenceName );
	}
	else handle = cl.sound_index[sound];	// see precached sound

	if( !cl.audio_prepped )
		return; // too early

	// g-cont. sound and ambient sound have only difference with channel
	if( chan == CHAN_STATIC )
	{
		S_AmbientSound( pos, entnum, handle, volume, attn, pitch, flags );
	}
	else
	{
		S_StartSound( pos, entnum, chan, handle, volume, attn, pitch, flags );
	}
}

/*
=====================================================================

ACTION MESSAGES

=====================================================================
*/
/*
=====================
CL_ParseGoldSrcServerMessage

dispatch messages
=====================
*/
void CL_ParseGoldSrcServerMessage( sizebuf_t *msg, qboolean normal_message )
{
	size_t		bufStart, playerbytes;
	int		cmd, param1, param2;
	int		old_background;
	const char	*s;

	cls.starting_count = MSG_GetNumBytesRead( msg );	// updates each frame
	CL_Parse_Debug( true );			// begin parsing

	if( normal_message )
	{
		// assume no entity/player update this packet
		if( cls.state == ca_active )
		{
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].valid = false;
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].choked = false;
		}
		else
		{
			CL_ResetFrame( &cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK] );
		}
	}

	// parse the message
	while( 1 )
	{
		if( MSG_CheckOverflow( msg ))
		{
			Host_Error( "CL_ParseServerMessage: overflow!\n" );
			return;
		}

		// mark start position
		bufStart = MSG_GetNumBytesRead( msg );

		// end of message (align bits)
		if( MSG_GetNumBitsLeft( msg ) < 8 )
			break;

		cmd = MSG_ReadServerCmd( msg );

		// record command for debugging spew on parse problem
		CL_Parse_RecordCommand( cmd, bufStart );

		// other commands
		switch( cmd )
		{
		case svc_bad:
			Host_Error( "svc_bad\n" );
			break;
		case svc_nop:
			// this does nothing
			break;
		case svc_disconnect:
			CL_Drop ();
			Host_AbortCurrentFrame ();
			break;
		case svc_event:
			MSG_StartBitWriting( msg );
			CL_ParseEvent( msg );
			MSG_EndBitWriting( msg );
			cl.frames[cl.parsecountmod].graphdata.event += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_setview:
			CL_ParseViewEntity( msg );
			break;
		case svc_sound:
			CL_ParseSoundPacketGS( msg );
			cl.frames[cl.parsecountmod].graphdata.sound += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_time:
			CL_ParseServerTime( msg );
			break;
		case svc_print:
			Con_Printf( "%s", MSG_ReadString( msg ));
			break;
		case svc_stufftext:
			s = MSG_ReadString( msg );
#ifdef HACKS_RELATED_HLMODS
			// disable Cry Of Fear antisave protection
			if( !Q_strnicmp( s, "disconnect", 10 ) && cls.signon != SIGNONS )
				break; // too early
#endif
			Cbuf_AddFilteredText( s );
			break;
		case svc_setangle:
			CL_ParseSetAngle( msg );
			break;
		case svc_goldsrc_serverinfo:
			Cbuf_Execute(); // make sure any stuffed commands are done
			CL_ParseServerData( msg, PROTOCOL_GOLDSRC_VERSION );
			Delta_InitMeta();
			break;
		case svc_lightstyle:
			CL_ParseLightStyle( msg );
			break;
		case svc_updateuserinfo:
			CL_UpdateUserinfo( msg, PROTOCOL_GOLDSRC_VERSION );
			break;
		case svc_goldsrc_deltadescription:
			Delta_ParseTableField_GS( msg );
			break;
		case svc_clientdata:
			MSG_StartBitWriting( msg );
			CL_ParseClientData( msg );
			MSG_EndBitWriting( msg );
			cl.frames[cl.parsecountmod].graphdata.client += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_pings:
			MSG_StartBitWriting( msg );
			CL_UpdateUserPings( msg );
			MSG_EndBitWriting( msg );
			break;
		case svc_particle:
			CL_ParseParticles( msg );
			break;
		case svc_spawnstatic:
			CL_ParseStaticEntity( msg );
			break;
		case svc_event_reliable:
			MSG_StartBitWriting( msg );
			CL_ParseReliableEvent( msg );
			MSG_EndBitWriting( msg );
			cl.frames[cl.parsecountmod].graphdata.event += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_spawnbaseline:
			CL_ParseBaseline( msg, PROTOCOL_GOLDSRC_VERSION );
			break;
		case svc_temp_entity:
			CL_ParseTempEntity( msg );
			cl.frames[cl.parsecountmod].graphdata.tentities += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_setpause:
			cl.paused = ( MSG_ReadOneBit( msg ) != 0 );
			break;
		case svc_signonnum:
			CL_ParseSignon( msg );
			break;
		case svc_centerprint:
			CL_CenterPrint( MSG_ReadString( msg ), 0.25f );
			break;
		case svc_intermission:
			cl.intermission = 1;
			break;
		case svc_finale:
			CL_ParseFinaleCutscene( msg, 2 );
			break;
		case svc_cdtrack:
			param1 = MSG_ReadByte( msg );
			param1 = bound( 1, param1, MAX_CDTRACKS ); // tracknum
			param2 = MSG_ReadByte( msg );
			param2 = bound( 1, param2, MAX_CDTRACKS ); // loopnum
			S_StartBackgroundTrack( clgame.cdtracks[param1-1], clgame.cdtracks[param2-1], 0, false );
			break;
		case svc_restore:
			CL_ParseRestore( msg );
			break;
		case svc_cutscene:
			CL_ParseFinaleCutscene( msg, 3 );
			break;
		case svc_weaponanim:
			param1 = MSG_ReadByte( msg );	// iAnim
			param2 = MSG_ReadByte( msg );	// body
			CL_WeaponAnim( param1, param2 );
			break;
		case svc_roomtype:
			param1 = MSG_ReadShort( msg );
			Cvar_SetValue( "room_type", param1 );
			break;
		case svc_addangle:
			CL_ParseAddAngle( msg );
			break;
		case svc_goldsrc_newusermsg:
			CL_ParseNewUserMsg( msg );
			break;
		case svc_packetentities:
			playerbytes = CL_ParsePacketEntitiesGS( msg, false );
			cl.frames[cl.parsecountmod].graphdata.players += playerbytes;
			cl.frames[cl.parsecountmod].graphdata.entities += MSG_GetNumBytesRead( msg ) - bufStart - playerbytes;
			break;
		case svc_deltapacketentities:
			playerbytes = CL_ParsePacketEntitiesGS( msg, true );
			cl.frames[cl.parsecountmod].graphdata.players += playerbytes;
			cl.frames[cl.parsecountmod].graphdata.entities += MSG_GetNumBytesRead( msg ) - bufStart - playerbytes;
			break;
		case svc_choke:
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].choked = true;
			cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].receivedtime = -2.0;
			break;
		case svc_resourcelist:
			MSG_StartBitWriting( msg );
			CL_ParseResourceList( msg );
			MSG_EndBitWriting( msg );
			break;
		case svc_goldsrc_newmovevars:
			CL_ParseNewMovevars( msg );
			break;
		case svc_resourcerequest:
			CL_ParseResourceRequest( msg );
			break;
		case svc_customization:
			CL_ParseCustomization( msg );
			break;
		case svc_crosshairangle:
			CL_ParseCrosshairAngle( msg );
			break;
		case svc_soundfade:
			CL_ParseSoundFade( msg );
			break;
		case svc_filetxferfailed:
			CL_ParseFileTransferFailed( msg );
			break;
		case svc_hltv:
			CL_ParseHLTV( msg );
			break;
		case svc_director:
			CL_ParseDirector( msg );
			break;
		case svc_voiceinit:
			CL_ParseVoiceInit( msg );
			break;
		case svc_voicedata:
			CL_ParseVoiceData( msg );
			cl.frames[cl.parsecountmod].graphdata.voicebytes += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_resourcelocation:
			CL_ParseResLocation( msg );
			break;
		case svc_goldsrc_sendextrainfo:
			CL_ParseExtraInfo( msg );
			break;
		case svc_querycvarvalue:
			CL_ParseCvarValue( msg, false );
			break;
		case svc_querycvarvalue2:
			CL_ParseCvarValue( msg, true );
			break;
		case svc_exec:
			CL_ParseExec( msg );
			break;
		default:
			CL_ParseUserMessage( msg, cmd );
			cl.frames[cl.parsecountmod].graphdata.usr += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		}
	}

	cl.frames[cl.parsecountmod].graphdata.msgbytes += MSG_GetNumBytesRead( msg ) - cls.starting_count;
	CL_Parse_Debug( false ); // done

	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	if( !cls.demoplayback )
	{
		if( cls.demorecording && !cls.demowaiting )
		{
			CL_WriteDemoMessage( false, cls.starting_count, msg );
		}
		else if( cls.state != ca_active )
		{
			CL_WriteDemoMessage( true, cls.starting_count, msg );
		}
	}
}
