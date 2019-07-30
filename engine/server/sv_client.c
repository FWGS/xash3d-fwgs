/*
sv_client.c - client interactions
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
#include "const.h"
#include "server.h"
#include "net_encode.h"
#include "net_api.h"

const char *clc_strings[clc_lastmsg+1] =
{
	"clc_bad",
	"clc_nop",
	"clc_move",
	"clc_stringcmd",
	"clc_delta",
	"clc_resourcelist",
	"clc_unused6",
	"clc_fileconsistency",
	"clc_voicedata",
	"clc_cvarvalue",
	"clc_cvarvalue2",
};

typedef struct ucmd_s
{
	const char	*name;
	qboolean		(*func)( sv_client_t *cl );
} ucmd_t;

static int	g_userid = 1;

/*
=================
SV_GetChallenge

Returns a challenge number that can be used
in a subsequent client_connect command.
We do this to prevent denial of service attacks that
flood the server with invalid connection IPs.  With a
challenge, they must give a valid IP address.
=================
*/
void SV_GetChallenge( netadr_t from )
{
	int	i, oldest = 0;
	double	oldestTime;

	oldestTime = 0x7fffffff;

	// see if we already have a challenge for this ip
	for( i = 0; i < MAX_CHALLENGES; i++ )
	{
		if( !svs.challenges[i].connected && NET_CompareAdr( from, svs.challenges[i].adr ))
			break;

		if( svs.challenges[i].time < oldestTime )
		{
			oldestTime = svs.challenges[i].time;
			oldest = i;
		}
	}

	if( i == MAX_CHALLENGES )
	{
		// this is the first time this client has asked for a challenge
		svs.challenges[oldest].challenge = (COM_RandomLong( 0, 0xFFFF ) << 16) | COM_RandomLong( 0, 0xFFFF );
		svs.challenges[oldest].adr = from;
		svs.challenges[oldest].time = host.realtime;
		svs.challenges[oldest].connected = false;
		i = oldest;
	}

	// send it back
	Netchan_OutOfBandPrint( NS_SERVER, svs.challenges[i].adr, "challenge %i", svs.challenges[i].challenge );
}

int SV_GetFragmentSize( void *pcl, fragsize_t mode )
{
	sv_client_t *cl = (sv_client_t*)pcl;
	int	cl_frag_size;

	if( Netchan_IsLocal( &cl->netchan ))
		return FRAGMENT_LOCAL_SIZE;

	if( mode == FRAGSIZE_UNRELIABLE )
	{
		// allow setting unreliable limit with "setinfo cl_urmax"
		cl_frag_size = Q_atoi( Info_ValueForKey( cl->userinfo, "cl_urmax" ));
		if( cl_frag_size == 0 )
			return NET_MAX_MESSAGE;
		return bound( FRAGMENT_MAX_SIZE, cl_frag_size, NET_MAX_MESSAGE );
	}

	cl_frag_size = Q_atoi( Info_ValueForKey( cl->userinfo, "cl_dlmax" ));
	cl_frag_size = bound( FRAGMENT_MIN_SIZE, cl_frag_size, FRAGMENT_MAX_SIZE );

	if( mode != FRAGSIZE_FRAG )
	{
		if( cl->extensions & NET_EXT_SPLITSIZE )
			return cl_frag_size;
		else
			return 0; // original engine behaviour
	}

	// get in-game fragmentation size
	if( cl->state == cs_spawned )
	{
		// allow setting in-game fragsize with "setinfo cl_frmax"
		int frmax = Q_atoi( Info_ValueForKey( cl->userinfo, "cl_frmax" ));

		if( frmax < FRAGMENT_MIN_SIZE || frmax > FRAGMENT_MAX_SIZE )
			cl_frag_size = frmax;
		else
			cl_frag_size /= 2;// add window for unreliable
	}

	return cl_frag_size - HEADER_BYTES;
}

/*
================
SV_RejectConnection

Rejects connection request and sends back a message
================
*/
void SV_RejectConnection( netadr_t from, char *fmt, ... )
{
	char	text[1024];
	va_list	argptr;

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Con_Reportf( "%s connection refused. Reason: %s\n", NET_AdrToString( from ), text );
	Netchan_OutOfBandPrint( NS_SERVER, from, "errormsg\n^1Server was reject the connection:^7 %s", text );
	Netchan_OutOfBandPrint( NS_SERVER, from, "print\n^1Server was reject the connection:^7 %s", text );
	Netchan_OutOfBandPrint( NS_SERVER, from, "disconnect\n" );
}

/*
================
SV_FailDownload

for some reasons file can't be downloaded
tell the client about this problem
================
*/
void SV_FailDownload( sv_client_t *cl, const char *filename )
{
	if( !COM_CheckString( filename ))
		return;

	MSG_BeginServerCmd( &cl->netchan.message, svc_filetxferfailed );
	MSG_WriteString( &cl->netchan.message, filename );
}

/*
================
SV_CheckChallenge

Make sure connecting client is not spoofing
================
*/
int SV_CheckChallenge( netadr_t from, int challenge )
{
	int	i;

	// see if the challenge is valid
	// don't care if it is a local address.
	if( NET_IsLocalAddress( from ))
		return 1;

	for( i = 0; i < MAX_CHALLENGES; i++ )
	{
		if( NET_CompareAdr( from, svs.challenges[i].adr ))
		{
			if( challenge == svs.challenges[i].challenge )
				break; // valid challenge
#if 0
			// g-cont. this breaks multiple connections from single machine
			SV_RejectConnection( from, "bad challenge %i\n", challenge );
			return 0;
#endif
		}
	}

	if( i == MAX_CHALLENGES )
	{
		SV_RejectConnection( from, "no challenge for your address\n" );
		return 0;
	}
	svs.challenges[i].connected = true;

	return 1;
}

/*
================
SV_CheckIPRestrictions

Determine if client is outside appropriate address range
================
*/
int SV_CheckIPRestrictions( netadr_t from )
{
	if( sv_lan.value )
	{
		if( !NET_CompareClassBAdr( from, net_local ) && !NET_IsReservedAdr( from ))
			return 0;
	}
	return 1;
}

/*
================
SV_FindEmptySlot

Get slot # and set client_t pointer for player, if possible
We don't do this search on a "reconnect, we just reuse the slot
================
*/
int SV_FindEmptySlot( netadr_t from, int *pslot, sv_client_t **ppClient )
{
	sv_client_t	*cl;
	int		i;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state == cs_free )
		{
			*ppClient = cl;
			*pslot = i;
			return 1;
		}
	}

	SV_RejectConnection( from, "server is full\n" );
	return 0;
}

/*
==================
SV_ConnectClient

A connection request that did not come from the master
==================
*/
void SV_ConnectClient( netadr_t from )
{
	char		userinfo[MAX_INFO_STRING];
	char		protinfo[MAX_INFO_STRING];
	sv_client_t	*cl, *newcl = NULL;
	qboolean		reconnect = false;
	int		nClientSlot = 0;
	int		qport, version;
	int		i, count = 0;
	int		challenge;
	const char		*s;
	int extensions;

	if( Cmd_Argc() < 5 )
	{
		SV_RejectConnection( from, "insufficient connection info\n" );
		return;
	}

	version = Q_atoi( Cmd_Argv( 1 ));

	if( version != PROTOCOL_VERSION )
	{
		SV_RejectConnection( from, "unsupported protocol (%i should be %i)\n", version, PROTOCOL_VERSION );
		return;
	}

	challenge = Q_atoi( Cmd_Argv( 2 )); // get challenge

	// see if the challenge is valid (local clients don't need to challenge)
	if( !SV_CheckChallenge( from, challenge ))
		return;

	s = Cmd_Argv( 3 );	// protocol info

	if( !Info_IsValid( s ))
	{
		SV_RejectConnection( from, "invalid protinfo in connect command\n" );
		return;
	}

	Q_strncpy( protinfo, s, sizeof( protinfo ));

	if( !SV_ProcessUserAgent( from, protinfo ) )
	{
		return;
	}

	// extract qport from protocol info
	qport = Q_atoi( Info_ValueForKey( protinfo, "qport" ));

	s = Info_ValueForKey( protinfo, "uuid" );
	if( Q_strlen( s ) != 32 )
	{
		SV_RejectConnection( from, "invalid authentication certificate length\n" );
		return;
	}

	extensions = Q_atoi( Info_ValueForKey( protinfo, "ext" ) );

	// LAN servers restrict to class b IP addresses
	if( !SV_CheckIPRestrictions( from ))
	{
		SV_RejectConnection( from, "LAN servers are restricted to local clients (class C)\n" );
		return;
	}

	s = Cmd_Argv( 4 );	// user info

	if( Q_strlen( s ) > MAX_INFO_STRING || !Info_IsValid( s ))
	{
		SV_RejectConnection( from, "invalid userinfo in connect command\n" );
		return;
	}

	Q_strncpy( userinfo, s, sizeof( userinfo ));

	// check connection password (don't verify local client)
	if( !NET_IsLocalAddress( from ) && sv_password.string[0] && Q_stricmp( sv_password.string, Info_ValueForKey( userinfo, "password" )))
	{
		SV_RejectConnection( from, "invalid password\n" );
		return;
	}

	// if there is already a slot for this ip, reuse it
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state == cs_free || cl->state == cs_zombie )
			continue;

		if( NET_CompareBaseAdr( from, cl->netchan.remote_address ) && ( cl->netchan.qport == qport || from.port == cl->netchan.remote_address.port ))
		{
			reconnect = true;
			newcl = cl;
			break;
		}
	}

	// A reconnecting client will re-use the slot found above when checking for reconnection.
	// the slot will be wiped clean.
	if( !reconnect )
	{
		// connect the client if there are empty slots.
		if( !SV_FindEmptySlot( from, &nClientSlot, &newcl ))
			return;
	}
	else
	{
		Con_Reportf( S_NOTE "%s:reconnect\n", NET_AdrToString( from ));
	}

	// find a client slot
	ASSERT( newcl != NULL );

	// build a new connection
	// accept the new client
	if( Q_strncpy( newcl->useragent, Cmd_Argv( 6 ), MAX_INFO_STRING ) )
	{
		const char *id = Info_ValueForKey( newcl->useragent, "i" );

		if( *id )
		{
			//sscanf( id, "%llx", &newcl->WonID );
		}

		// Q_strncpy( cl->auth_id, id, sizeof( cl->auth_id ) );
	}

	sv.current_client = newcl;
	newcl->edict = EDICT_NUM( (newcl - svs.clients) + 1 );
	newcl->challenge = challenge; // save challenge for checksumming
	if( newcl->frames ) Mem_Free( newcl->frames );
	newcl->frames = (client_frame_t *)Z_Calloc( sizeof( client_frame_t ) * SV_UPDATE_BACKUP );
	newcl->userid = g_userid++;	// create unique userid
	newcl->state = cs_connected;
	newcl->extensions = extensions & (NET_EXT_SPLITSIZE);

	// reset viewentities (from previous level)
	memset( newcl->viewentity, 0, sizeof( newcl->viewentity ));
	newcl->num_viewents = 0;
	newcl->listeners = 0;

	// initailize netchan
	Netchan_Setup( NS_SERVER, &newcl->netchan, from, qport, newcl, SV_GetFragmentSize );
	MSG_Init( &newcl->datagram, "Datagram", newcl->datagram_buf, sizeof( newcl->datagram_buf )); // datagram buf

	Q_strncpy( newcl->hashedcdkey, Info_ValueForKey( protinfo, "uuid" ), 32 );
	newcl->hashedcdkey[32] = '\0';

	// build protinfo answer
	protinfo[0] = '\0';
	Info_SetValueForKey( protinfo, "ext", va( "%d",newcl->extensions ), sizeof( protinfo ) );

	// send the connect packet to the client
	Netchan_OutOfBandPrint( NS_SERVER, from, "client_connect %s", protinfo );

	newcl->upstate = us_inactive;
	newcl->connection_started = host.realtime;
	newcl->cl_updaterate = 0.05;	// 20 fps as default
	newcl->delta_sequence = -1;
	newcl->flags = 0;



	// reset any remaining events
	memset( &newcl->events, 0, sizeof( newcl->events ));

	// parse some info from the info strings (this can override cl_updaterate)
	Q_strncpy( newcl->userinfo, userinfo, sizeof( newcl->userinfo ));
	SV_UserinfoChanged( newcl );
	SV_ClearResourceLists( newcl );
#if 0
	memset( &newcl->resourcesneeded, 0, sizeof( resource_t ));
	memset( &newcl->resourcesonhand, 0, sizeof( resource_t ));
	newcl->resourcesneeded.pNext = newcl->resourcesneeded.pPrev = &newcl->resourcesneeded;
	newcl->resourcesonhand.pNext = newcl->resourcesonhand.pPrev = &newcl->resourcesonhand;
#endif
	newcl->next_messagetime = host.realtime + newcl->cl_updaterate;
	newcl->next_sendinfotime = 0.0;
	newcl->ignored_ents = 0;
	newcl->chokecount = 0;

	// reset stats
	newcl->next_checkpingtime = -1.0;
	newcl->packet_loss = 0.0f;

	// if this was the first client on the server, or the last client
	// the server can hold, send a heartbeat to the master.
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
		if( cl->state >= cs_connected ) count++;

	Log_Printf( "\"%s<%i><%i><>\" connected, address \"%s\"\n", newcl->name, newcl->userid, i, NET_AdrToString( newcl->netchan.remote_address ));

	if( count == 1 || count == svs.maxclients )
		svs.last_heartbeat = MAX_HEARTBEAT;
}

/*
==================
SV_FakeConnect

A connection request that came from the game module
==================
*/
edict_t *SV_FakeConnect( const char *netname )
{
	char		userinfo[MAX_INFO_STRING];
	int		i, count = 0;
	sv_client_t	*cl;

	if( !COM_CheckString( netname ))
		netname = "Bot";

	// find a client slot
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state == cs_free )
			break;
	}

	if( i == svs.maxclients )
		return NULL; // server is full

	userinfo[0] = '\0';

	// setup fake client params
	Info_SetValueForKey( userinfo, "name", netname, MAX_INFO_STRING );
	Info_SetValueForKey( userinfo, "model", "gordon", MAX_INFO_STRING );
	Info_SetValueForKey( userinfo, "topcolor", "1", MAX_INFO_STRING );
	Info_SetValueForKey( userinfo, "bottomcolor", "1", MAX_INFO_STRING );

	// build a new connection
	// accept the new client
	sv.current_client = cl;

	if( cl->frames ) Mem_Free( cl->frames );	// fakeclients doesn't have frames
	memset( cl, 0, sizeof( sv_client_t ));

	cl->edict = EDICT_NUM( (cl - svs.clients) + 1 );
	cl->userid = g_userid++;		// create unique userid
	SetBits( cl->flags, FCL_FAKECLIENT );

	// parse some info from the info strings
	Q_strncpy( cl->userinfo, userinfo, sizeof( cl->userinfo ));
	SV_UserinfoChanged( cl );
	SetBits( cl->flags, FCL_RESEND_USERINFO );
	cl->next_sendinfotime = 0.0;

	// if this was the first client on the server, or the last client
	// the server can hold, send a heartbeat to the master.
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
		if( cl->state >= cs_connected ) count++;
	cl = sv.current_client;

	Log_Printf( "\"%s<%i><%i><>\" connected, address \"local\"\n", cl->name, cl->userid, i );

	SetBits( cl->edict->v.flags, FL_CLIENT|FL_FAKECLIENT );	// mark it as fakeclient
	cl->connection_started = host.realtime;
	cl->state = cs_spawned;

	if( count == 1 || count == svs.maxclients )
		svs.last_heartbeat = MAX_HEARTBEAT;
	
	return cl->edict;
}

/*
=====================
SV_DropClient

Called when the player is totally leaving the server, either willingly
or unwillingly.  This is NOT called if the entire server is quiting
or crashing.
=====================
*/
void SV_DropClient( sv_client_t *cl, qboolean crash )
{
	int	i;
	
	if( cl->state == cs_zombie )
		return;	// already dropped

	if( !crash )
	{
		// add the disconnect
		if( !FBitSet( cl->flags, FCL_FAKECLIENT ) )
		{
			MSG_BeginServerCmd( &cl->netchan.message, svc_disconnect );
		}

		if( cl->edict && cl->state == cs_spawned )
		{
			svgame.dllFuncs.pfnClientDisconnect( cl->edict );
		}

		if( !FBitSet( cl->flags, FCL_FAKECLIENT ) )
		{
			Netchan_TransmitBits( &cl->netchan, 0, NULL );
		}
	}

	ClearBits( cl->flags, FCL_FAKECLIENT );
	ClearBits( cl->flags, FCL_HLTV_PROXY );
	cl->state = cs_zombie; // become free in a few seconds
	cl->name[0] = 0;

	if( cl->frames )
		Mem_Free( cl->frames ); // release delta
	cl->frames = NULL;

	if( NET_CompareBaseAdr( cl->netchan.remote_address, host.rd.address ))
		SV_EndRedirect();

	// throw away any residual garbage in the channel.
	Netchan_Clear( &cl->netchan );

	// clean client data on disconnect
	memset( cl->userinfo, 0, MAX_INFO_STRING );
	memset( cl->physinfo, 0, MAX_INFO_STRING );
	COM_ClearCustomizationList( &cl->customdata, false );

	// don't send to other clients
	cl->edict = NULL;

	// send notification to all other clients
	SV_FullClientUpdate( cl, &sv.reliable_datagram );

	// if this was the last client on the server, send a heartbeat
	// to the master so it is known the server is empty
	// send a heartbeat now so the master will get up to date info
	// if there is already a slot for this ip, reuse it
	for( i = 0; i < svs.maxclients; i++ )
	{
		if( svs.clients[i].state >= cs_connected )
			break;
	}

	if( i == svs.maxclients )
		svs.last_heartbeat = MAX_HEARTBEAT;
}

/*
==============================================================================

SVC COMMAND REDIRECT

==============================================================================
*/
void SV_BeginRedirect( netadr_t adr, int target, char *buffer, int buffersize, void (*flush))
{
	if( !target || !buffer || !buffersize || !flush )
		return;

	host.rd.target = target;
	host.rd.buffer = buffer;
	host.rd.buffersize = buffersize;
	host.rd.flush = flush;
	host.rd.address = adr;
	host.rd.buffer[0] = 0;
}

void SV_FlushRedirect( netadr_t adr, int dest, char *buf )
{
	if( sv.current_client && FBitSet( sv.current_client->flags, FCL_FAKECLIENT ))
		return;

	switch( dest )
	{
	case RD_PACKET:
		Netchan_OutOfBandPrint( NS_SERVER, adr, "print\n%s", buf );
		break;
	case RD_CLIENT:
		if( !sv.current_client ) return; // client not set
		MSG_BeginServerCmd( &sv.current_client->netchan.message, svc_print );
		MSG_WriteString( &sv.current_client->netchan.message, buf );
		break;
	case RD_NONE:
		Con_Printf( S_ERROR "SV_FlushRedirect: %s: invalid destination\n", NET_AdrToString( adr ));
		break;
	}
}

void SV_EndRedirect( void )
{
	if( host.rd.flush )
		host.rd.flush( host.rd.address, host.rd.target, host.rd.buffer );

	host.rd.target = 0;
	host.rd.buffer = NULL;
	host.rd.buffersize = 0;
	host.rd.flush = NULL;
}

/*
===============
SV_GetClientIDString

Returns a pointer to a static char for most likely only printing.
===============
*/
const char *SV_GetClientIDString( sv_client_t *cl )
{
	static char	result[MAX_QPATH];

	if( !cl ) return "";

	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
	{
		Q_strncpy( result, "ID_BOT", sizeof( result ));
	}
	else if( NET_IsLocalAddress( cl->netchan.remote_address ))
	{
		Q_strncpy( result, "ID_LOOPBACK", sizeof( result ));
	}
	else if( sv_lan.value )
	{
		Q_strncpy( result, "ID_LAN", sizeof( result ));
	}
	else
	{
		Q_snprintf( result, sizeof( result ), "ID_%s", MD5_Print( (byte *)cl->hashedcdkey ));
	}

	return result;
}

sv_client_t *SV_ClientById( int id )
{
	sv_client_t *cl;
	int i;

	ASSERT( id >= 0 );

	for( i = 0, cl = svs.clients; i < svgame.globals->maxClients; i++, cl++ )
	{
		if( !cl->state )
			continue;

		if( cl->userid == id )
			return cl;
	}

	return NULL;
}

sv_client_t *SV_ClientByName( const char *name )
{
	sv_client_t *cl;
	int i;

	ASSERT( name && *name );

	for( i = 0, cl = svs.clients; i < svgame.globals->maxClients; i++, cl++ )
	{
		if( !cl->state )
			continue;

		if( !Q_strcmp( cl->name, name ) )
			return cl;
	}

	return NULL;
}

/*
================
SV_TestBandWidth

================
*/
void SV_TestBandWidth( netadr_t from )
{
	int	version = Q_atoi( Cmd_Argv( 1 ));
	int	packetsize = Q_atoi( Cmd_Argv( 2 ));
	byte	send_buf[FRAGMENT_MAX_SIZE];
	dword	crcValue = 0;
	byte	*filepos;
	int	crcpos;
	file_t	*test;
	sizebuf_t	send;

	// don't waste time of protocol mismatched
	if( version != PROTOCOL_VERSION )
	{
		SV_RejectConnection( from, "unsupported protocol (%i should be %i)\n", version, PROTOCOL_VERSION );
		return;
	}

	test = FS_Open( "gfx.wad", "rb", false );

	if( FS_FileLength( test ) < sizeof( send_buf ))
	{
		// skip the test and just get challenge
		SV_GetChallenge( from );
		return;
	}

	// write the packet header
	MSG_Init( &send, "BandWidthPacket", send_buf, sizeof( send_buf ));
	MSG_WriteLong( &send, -1 );	// -1 sequence means out of band
	MSG_WriteString( &send, "testpacket" );
	crcpos = MSG_GetNumBytesWritten( &send );
	MSG_WriteLong( &send, 0 ); // reserve space for crc
	filepos = send.pData + MSG_GetNumBytesWritten( &send );
	packetsize = packetsize - MSG_GetNumBytesWritten( &send ); // adjust the packet size
	FS_Read( test, filepos, packetsize );
	FS_Close( test );

	CRC32_ProcessBuffer( &crcValue, filepos, packetsize );	// calc CRC
	MSG_SeekToBit( &send, packetsize << 3, SEEK_CUR );
	*(uint *)&send.pData[crcpos] = crcValue;

	// send the datagram
	NET_SendPacket( NS_SERVER, MSG_GetNumBytesWritten( &send ), MSG_GetData( &send ), from );
}

/*
================
SV_Ack

================
*/
void SV_Ack( netadr_t from )
{
	Con_Printf( "ping %s\n", NET_AdrToString( from ));
}

/*
================
SV_Info

Responds with short info for broadcast scans
The second parameter should be the current protocol version number.
================
*/
void SV_Info( netadr_t from )
{
	char	string[MAX_INFO_STRING];
	int	i, count = 0;
	int	version;

	// ignore in single player
	if( svs.maxclients == 1 || !svs.initialized )
		return;

	version = Q_atoi( Cmd_Argv( 1 ));
	string[0] = '\0';

	if( version != PROTOCOL_VERSION )
	{
		Q_snprintf( string, sizeof( string ), "%s: wrong version\n", hostname.string );
	}
	else
	{
		for( i = 0; i < svs.maxclients; i++ )
			if( svs.clients[i].state >= cs_connected )
				count++;

		Info_SetValueForKey( string, "host", hostname.string, MAX_INFO_STRING );
		Info_SetValueForKey( string, "map", sv.name, MAX_INFO_STRING );
		Info_SetValueForKey( string, "dm", va( "%i", (int)svgame.globals->deathmatch ), MAX_INFO_STRING );
		Info_SetValueForKey( string, "team", va( "%i", (int)svgame.globals->teamplay ), MAX_INFO_STRING );
		Info_SetValueForKey( string, "coop", va( "%i", (int)svgame.globals->coop ), MAX_INFO_STRING );
		Info_SetValueForKey( string, "numcl", va( "%i", count ), MAX_INFO_STRING );
		Info_SetValueForKey( string, "maxcl", va( "%i", svs.maxclients ), MAX_INFO_STRING );
		Info_SetValueForKey( string, "gamedir", GI->gamefolder, MAX_INFO_STRING );
	}

	Netchan_OutOfBandPrint( NS_SERVER, from, "info\n%s", string );
}

/*
================
SV_BuildNetAnswer

Responds with long info for local and broadcast requests
================
*/
void SV_BuildNetAnswer( netadr_t from )
{
	char	string[MAX_INFO_STRING];
	int	version, context, type;
	int	i, count = 0;

	// ignore in single player
	if( svs.maxclients == 1 || !svs.initialized )
		return;

	version = Q_atoi( Cmd_Argv( 1 ));
	context = Q_atoi( Cmd_Argv( 2 ));
	type = Q_atoi( Cmd_Argv( 3 ));

	if( version != PROTOCOL_VERSION )
	{
		// handle the unsupported protocol
		string[0] = '\0';
		Info_SetValueForKey( string, "neterror", "protocol", MAX_INFO_STRING );

		// send error unsupported protocol
		Netchan_OutOfBandPrint( NS_SERVER, from, "netinfo %i %i %s\n", context, type, string );
		return;
	}

	if( type == NETAPI_REQUEST_PING )
	{
		Netchan_OutOfBandPrint( NS_SERVER, from, "netinfo %i %i %s\n", context, type, "" );
	}
	else if( type == NETAPI_REQUEST_RULES )
	{
		// send serverinfo
		Netchan_OutOfBandPrint( NS_SERVER, from, "netinfo %i %i %s\n", context, type, svs.serverinfo );
	}
	else if( type == NETAPI_REQUEST_PLAYERS )
	{
		string[0] = '\0';

		for( i = 0; i < svs.maxclients; i++ )
		{
			if( svs.clients[i].state >= cs_connected )
			{
				edict_t *ed = svs.clients[i].edict;
				float time = host.realtime - svs.clients[i].connection_started;
				Q_strncat( string, va( "%c\\%s\\%i\\%f\\", count, svs.clients[i].name, (int)ed->v.frags, time ), sizeof( string )); 
				count++;
			}
		}

		// send playernames
		Netchan_OutOfBandPrint( NS_SERVER, from, "netinfo %i %i %s\n", context, type, string );
	}
	else if( type == NETAPI_REQUEST_DETAILS )
	{
		for( i = 0; i < svs.maxclients; i++ )
			if( svs.clients[i].state >= cs_connected )
				count++;

		string[0] = '\0';
		Info_SetValueForKey( string, "hostname", hostname.string, MAX_INFO_STRING );
		Info_SetValueForKey( string, "gamedir", GI->gamefolder, MAX_INFO_STRING );
		Info_SetValueForKey( string, "current", va( "%i", count ), MAX_INFO_STRING );
		Info_SetValueForKey( string, "max", va( "%i", svs.maxclients ), MAX_INFO_STRING );
		Info_SetValueForKey( string, "map", sv.name, MAX_INFO_STRING );

		// send serverinfo
		Netchan_OutOfBandPrint( NS_SERVER, from, "netinfo %i %i %s\n", context, type, string );
	}
	else
	{
		string[0] = '\0';
		Info_SetValueForKey( string, "neterror", "undefined", MAX_INFO_STRING );

		// send error undefined request type
		Netchan_OutOfBandPrint( NS_SERVER, from, "netinfo %i %i %s\n", context, type, string );
	}
}

/*
================
SV_Ping

Just responds with an acknowledgement
================
*/
void SV_Ping( netadr_t from )
{
	Netchan_OutOfBandPrint( NS_SERVER, from, "ack" );
}

/*
================
Rcon_Validate
================
*/
qboolean Rcon_Validate( void )
{
	if( !Q_strlen( rcon_password.string ))
		return false;
	if( Q_strcmp( Cmd_Argv( 1 ), rcon_password.string ))
		return false;
	return true;
}

/*
===============
SV_RemoteCommand

A client issued an rcon command.
Shift down the remaining args
Redirect all printfs
===============
*/
void SV_RemoteCommand( netadr_t from, sizebuf_t *msg )
{
	static char	outputbuf[2048];
	char		remaining[1024];
	int		i;

	Con_Printf( "Rcon from %s:\n%s\n", NET_AdrToString( from ), MSG_GetData( msg ) + 4 );
	Log_Printf( "Rcon: \"%s\" from \"%s\"\n", MSG_GetData( msg ) + 4, NET_AdrToString( from ));
	SV_BeginRedirect( from, RD_PACKET, outputbuf, sizeof( outputbuf ) - 16, SV_FlushRedirect );

	if( Rcon_Validate( ))
	{
		remaining[0] = 0;
		for( i = 2; i < Cmd_Argc(); i++ )
		{
			Q_strcat( remaining, Cmd_Argv( i ));
			Q_strcat( remaining, " " );
		}
		Cmd_ExecuteString( remaining );
	}
	else Con_Printf( S_ERROR "Bad rcon_password.\n" );

	SV_EndRedirect();
}

/*
===================
SV_CalcPing

recalc ping on current client
===================
*/
int SV_CalcPing( sv_client_t *cl )
{
	float		ping = 0;
	int		i, count;
	int		idx, back;
	client_frame_t	*frame;

	// bots don't have a real ping
	if( FBitSet( cl->flags, FCL_FAKECLIENT ) || !cl->frames )
		return 5;

	if( SV_UPDATE_BACKUP <= 31 )
	{
		back = SV_UPDATE_BACKUP / 2;
		if( back <= 0 ) return 0;
	}
	else back = 16;

	count = 0;

	for( i = 0; i < back; i++ )
	{
		idx = cl->netchan.incoming_acknowledged + ~i;
		frame = &cl->frames[idx & SV_UPDATE_MASK];

		if( frame->ping_time > 0.0f )
		{
			ping += frame->ping_time;
			count++;
		}
	}

	if( count > 0 )
		return (( ping / count ) * 1000.0f );
	return 0;
}

/*
===================
SV_EstablishTimeBase

Finangles latency and the like. 
===================
*/
void SV_EstablishTimeBase( sv_client_t *cl, usercmd_t *cmds, int dropped, int numbackup, int numcmds )
{
	double	runcmd_time = 0.0;
	int	i, cmdnum = dropped;

	if( dropped < 24 )
	{
		while( dropped > numbackup )
		{
			runcmd_time = (double)cl->lastcmd.msec / 1000.0;
			dropped--;
		}

		while( dropped > 0 )
		{
			cmdnum = dropped + numcmds - 1;
			runcmd_time += (double)cmds[cmdnum].msec / 1000.0;
			dropped--;
		}		
	}

	for( i = numcmds - 1; i >= 0; i-- )
		runcmd_time += cmds[i].msec / 1000.0;

	cl->timebase = sv.time + sv.frametime - runcmd_time;
}

/*
===================
SV_CalcClientTime

compute latency for client
===================
*/
float SV_CalcClientTime( sv_client_t *cl )
{
	float	minping, maxping;
	float	ping = 0.0f;
	int	i, count = 0;
	int	backtrack;

	backtrack = (int)sv_unlagsamples.value;
	if( backtrack < 1 ) backtrack = 1;

	if( backtrack >= (SV_UPDATE_BACKUP <= 16 ? SV_UPDATE_BACKUP : 16 ))
		backtrack = ( SV_UPDATE_BACKUP <= 16 ? SV_UPDATE_BACKUP : 16 );

	if( backtrack <= 0 )
		return 0.0f;

	for( i = 0; i < backtrack; i++ )
	{
		client_frame_t	*frame = &cl->frames[SV_UPDATE_MASK & (cl->netchan.incoming_acknowledged - i)];
		if( frame->ping_time <= 0.0f )
			continue;

		ping += frame->ping_time;
		count++;
	}

	if( !count ) return 0.0f;

	minping =  9999.0f;
	maxping = -9999.0f;
	ping /= count;
	
	for( i = 0; i < ( SV_UPDATE_BACKUP <= 4 ? SV_UPDATE_BACKUP : 4 ); i++ )
	{
		client_frame_t	*frame = &cl->frames[SV_UPDATE_MASK & (cl->netchan.incoming_acknowledged - i)];
		if( frame->ping_time <= 0.0f )
			continue;

		if( frame->ping_time < minping )
			minping = frame->ping_time;

		if( frame->ping_time > maxping )
			maxping = frame->ping_time;
	}

	if( maxping < minping || fabs( maxping - minping ) <= 0.2f )
		return ping;

	return 0.0f;
}

/*
===================
SV_FullClientUpdate

Writes all update values to a bitbuf
===================
*/
void SV_FullClientUpdate( sv_client_t *cl, sizebuf_t *msg )
{
	char		info[MAX_INFO_STRING];
	char		digest[16];
	MD5Context_t	ctx;
	int		i;	

	// process userinfo before updating
	SV_UserinfoChanged( cl );

	i = cl - svs.clients;

	MSG_BeginServerCmd( msg, svc_updateuserinfo );
	MSG_WriteUBitLong( msg, i, MAX_CLIENT_BITS );
	MSG_WriteLong( msg, cl->userid );

	if( cl->name[0] )
	{
		MSG_WriteOneBit( msg, 1 );

		Q_strncpy( info, cl->userinfo, sizeof( info ));

		// remove server passwords, etc.
		Info_RemovePrefixedKeys( info, '_' );
		MSG_WriteString( msg, info );

		MD5Init( &ctx );
		MD5Update( &ctx, (byte *)cl->hashedcdkey, sizeof( cl->hashedcdkey ));
		MD5Final( digest, &ctx );

		MSG_WriteBytes( msg, digest, sizeof( digest ));
	}
	else MSG_WriteOneBit( msg, 0 );
}

/*
===================
SV_RefreshUserinfo

===================
*/
void SV_RefreshUserinfo( void )
{
	sv_client_t	*cl;
	int		i;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state >= cs_connected )
			SetBits( cl->flags, FCL_RESEND_USERINFO );
	}
}

/*
===================
SV_FullUpdateMovevars

this is send all movevars values when client connected
otherwise see code SV_UpdateMovevars()
===================
*/
void SV_FullUpdateMovevars( sv_client_t *cl, sizebuf_t *msg )
{
	movevars_t	nullmovevars;

	memset( &nullmovevars, 0, sizeof( nullmovevars ));
	MSG_WriteDeltaMovevars( msg, &nullmovevars, &svgame.movevars );
}

/*
===================
SV_ShouldUpdatePing

determine should we recalculate
ping times now
===================
*/
qboolean SV_ShouldUpdatePing( sv_client_t *cl )
{
	if( FBitSet( cl->flags, FCL_HLTV_PROXY ))
	{
		if( host.realtime < cl->next_checkpingtime )
			return false;

		cl->next_checkpingtime = host.realtime + 2.0;
		return true;
	}

	// they are viewing the scoreboard.  Send them pings.
	return FBitSet( cl->lastcmd.buttons, IN_SCORE ) ? true : false;
}

/*
===================
SV_IsPlayerIndex

===================
*/
qboolean SV_IsPlayerIndex( int idx )
{
	if( idx > 0 && idx <= svs.maxclients )
		return true;
	return false;
}

/*
===================
SV_GetPlayerStats

This function and its static vars track some of the networking
conditions.  I haven't bothered to trace it beyond that, because
this fucntion sucks pretty badly.
===================
*/
void SV_GetPlayerStats( sv_client_t *cl, int *ping, int *packet_loss )
{
	static int	last_ping[MAX_CLIENTS];
	static int	last_loss[MAX_CLIENTS];
	int		i;

	i = cl - svs.clients;

	if( host.realtime >= cl->next_checkpingtime )
	{
		cl->next_checkpingtime = host.realtime + 2.0;
		last_ping[i] = SV_CalcPing( cl );
		last_loss[i] = cl->packet_loss;
	}

	if( ping ) *ping = last_ping[i];
	if( packet_loss ) *packet_loss = last_loss[i];
}

/*
===========
PutClientInServer

Called when a player connects to a server or respawns in
a deathmatch.
============
*/
void SV_PutClientInServer( sv_client_t *cl )
{
	static byte    	msg_buf[0x20200];	// MAX_INIT_MSG + some space
	edict_t		*ent = cl->edict;
	sizebuf_t		msg;

	MSG_Init( &msg, "Spawn", msg_buf, sizeof( msg_buf ));

	if( sv.loadgame )
	{
		// NOTE: we needs to setup angles on restore here
		if( ent->v.fixangle == 1 )
		{
			MSG_BeginServerCmd( &msg, svc_setangle );
			MSG_WriteVec3Angles( &msg, ent->v.angles );
			ent->v.fixangle = 0;
		}

		if( svgame.dllFuncs.pfnParmsChangeLevel )
		{
			SAVERESTOREDATA	levelData;
			string		name;
			int		i;

			memset( &levelData, 0, sizeof( levelData ));
			svgame.globals->pSaveData = &levelData;
			svgame.dllFuncs.pfnParmsChangeLevel();

			MSG_BeginServerCmd( &msg, svc_restore );
			Q_snprintf( name, sizeof( name ), "%s%s.HL2", DEFAULT_SAVE_DIRECTORY, sv.name );
			COM_FixSlashes( name );
			MSG_WriteString( &msg, name );
			MSG_WriteByte( &msg, levelData.connectionCount );

			for( i = 0; i < levelData.connectionCount; i++ )
				MSG_WriteString( &msg, levelData.levelList[i].mapName );

			svgame.globals->pSaveData = NULL;
		}

		// reset weaponanim
		MSG_BeginServerCmd( &msg, svc_weaponanim );
		MSG_WriteByte( &msg, 0 );
		MSG_WriteByte( &msg, 0 );

		sv.loadgame = false;
		sv.paused = false;
	}
	else
	{	
		if( Q_atoi( Info_ValueForKey( cl->userinfo, "hltv" )))
			SetBits( cl->flags, FCL_HLTV_PROXY );

		// need to realloc private data for client
		SV_InitEdict( ent );

		if( FBitSet( cl->flags, FCL_HLTV_PROXY ))
			SetBits( ent->v.flags, FL_PROXY );
		else ent->v.flags = 0;

		ent->v.netname = MAKE_STRING( cl->name );
		ent->v.colormap = NUM_FOR_EDICT( ent );	// ???

		// fisrt entering
		svgame.globals->time = sv.time;
		svgame.dllFuncs.pfnClientPutInServer( ent );

		if( sv.background )	// don't attack player in background mode
			SetBits( ent->v.flags, FL_GODMODE|FL_NOTARGET );

		cl->pViewEntity = NULL; // reset pViewEntity
	}

	if( svgame.globals->cdAudioTrack )
	{
		MSG_BeginServerCmd( &msg, svc_stufftext );
		MSG_WriteString( &msg, va( "cd loop %3d\n", svgame.globals->cdAudioTrack ));
		svgame.globals->cdAudioTrack = 0;
	}

#ifdef HACKS_RELATED_HLMODS
	// enable dev-mode to prevent crash cheat-protecting from Invasion mod
	if( FBitSet( ent->v.flags, FL_GODMODE|FL_NOTARGET ) && !Q_stricmp( GI->gamefolder, "invasion" ))
		SV_ExecuteClientCommand( cl, "test\n" );
#endif
	// refresh the userinfo and movevars
	// NOTE: because movevars can be changed during the connection process
	SetBits( cl->flags, FCL_RESEND_USERINFO|FCL_RESEND_MOVEVARS );

	// reset client times
	cl->connecttime = 0.0;
	cl->ignorecmdtime = 0.0;
	cl->cmdtime = 0.0;

	if( !FBitSet( cl->flags, FCL_FAKECLIENT ))
	{
		int	viewEnt;

		// NOTE: it's will be fragmented automatically in right ordering
		MSG_WriteBits( &msg, MSG_GetData( &sv.signon ), MSG_GetNumBitsWritten( &sv.signon ));

		if( cl->pViewEntity )
			viewEnt = NUM_FOR_EDICT( cl->pViewEntity );
		else viewEnt = NUM_FOR_EDICT( cl->edict );

		MSG_BeginServerCmd( &msg, svc_setview );
		MSG_WriteWord( &msg, viewEnt );

		MSG_BeginServerCmd( &msg, svc_signonnum );
		MSG_WriteByte( &msg, 1 );

		if( MSG_CheckOverflow( &msg ))
		{
			if( svs.maxclients == 1 )
				Host_Error( "spawn player: overflowed\n" );
			else SV_DropClient( cl, false );
		}
		else
		{
			// send initialization data
			Netchan_CreateFragments( &cl->netchan, &msg );
			Netchan_FragSend( &cl->netchan );
		}
	}
}

/*
===========
SV_UpdateClientView

Resend the client viewentity (used for demos)
============
*/
void SV_UpdateClientView( sv_client_t *cl )
{
	int	viewEnt;

	if( cl->pViewEntity )
		viewEnt = NUM_FOR_EDICT( cl->pViewEntity );
	else viewEnt = NUM_FOR_EDICT( cl->edict );

	MSG_BeginServerCmd( &cl->netchan.message, svc_setview );
	MSG_WriteWord( &cl->netchan.message, viewEnt );
}

/*
==================
SV_TogglePause
==================
*/
void SV_TogglePause( const char *msg )
{
	if( sv.background ) return;

	sv.paused ^= 1;

	if( COM_CheckString( msg ))
		SV_BroadcastPrintf( NULL, "%s", msg );

	// send notification to all clients
	MSG_BeginServerCmd( &sv.reliable_datagram, svc_setpause );
	MSG_WriteOneBit( &sv.reliable_datagram, sv.paused );
}

/*
================
SV_SendReconnect

Tell all the clients that the server is changing levels
================
*/
void SV_BuildReconnect( sizebuf_t *msg )
{
	MSG_BeginServerCmd( msg, svc_stufftext );
	MSG_WriteString( msg, "reconnect\n" );
}

/*
==================
SV_WriteDeltaDescriptionToClient

send delta communication encoding
==================
*/
void SV_WriteDeltaDescriptionToClient( sizebuf_t *msg )
{
	int	tableIndex;
	int	fieldIndex;

	for( tableIndex = 0; tableIndex < Delta_NumTables(); tableIndex++ )
	{
		delta_info_t	*dt = Delta_FindStructByIndex( tableIndex );

		for( fieldIndex = 0; fieldIndex < dt->numFields; fieldIndex++ )
			Delta_WriteTableField( msg, tableIndex, &dt->pFields[fieldIndex] );
	}
}

/*
================
SV_SendServerdata

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void SV_SendServerdata( sizebuf_t *msg, sv_client_t *cl )
{
	string	message;
	int	i;

	// Only send this message to developer console, or multiplayer clients.
	if(( host_developer.value ) || ( svs.maxclients > 1 ))
	{
		MSG_BeginServerCmd( msg, svc_print );
		Q_snprintf( message, sizeof( message ), "\n^3BUILD %d SERVER (%i CRC)\nServer #%i\n", Q_buildnum(), sv.progsCRC, svs.spawncount );
		MSG_WriteString( msg, message );
	}

	// send the serverdata
	MSG_BeginServerCmd( msg, svc_serverdata );
	MSG_WriteLong( msg, PROTOCOL_VERSION );
	MSG_WriteLong( msg, svs.spawncount );
	MSG_WriteLong( msg, sv.worldmapCRC );
	MSG_WriteByte( msg, cl - svs.clients );
	MSG_WriteByte( msg, svs.maxclients );
	MSG_WriteWord( msg, GI->max_edicts );
	MSG_WriteWord( msg, MAX_MODELS );
	MSG_WriteString( msg, sv.name );
	MSG_WriteString( msg, STRING( svgame.edicts->v.message )); // Map Message
	MSG_WriteOneBit( msg, sv.background ); // tell client about background map
	MSG_WriteString( msg, GI->gamefolder );
	MSG_WriteLong( msg, host.features );

	// send the player hulls
	for( i = 0; i < MAX_MAP_HULLS * 3; i++ )
	{
		MSG_WriteChar( msg, host.player_mins[i/3][i%3] );
		MSG_WriteChar( msg, host.player_maxs[i/3][i%3] );
	}

	// send delta-encoding
	SV_WriteDeltaDescriptionToClient( msg );

	// now client know delta and can reading encoded messages
	SV_FullUpdateMovevars( cl, msg );

	// send the user messages registration
	for( i = 1; i < MAX_USER_MESSAGES && svgame.msg[i].name[0]; i++ )
		SV_SendUserReg( msg, &svgame.msg[i] );

	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		if( !sv.lightstyles[i].pattern[0] )
			continue;	// unused style

		MSG_BeginServerCmd( msg, svc_lightstyle );
		MSG_WriteByte( msg, i ); // stylenum
		MSG_WriteString( msg, sv.lightstyles[i].pattern );
		MSG_WriteFloat( msg, sv.lightstyles[i].time );
	}
}

/*
============================================================

CLIENT COMMAND EXECUTION

============================================================
*/
/*
================
SV_New_f

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
static qboolean SV_New_f( sv_client_t *cl )
{
	byte		msg_buf[MAX_INIT_MSG];
	char		szRejectReason[128];
	char		szAddress[128];
	char		szName[32];
	sv_client_t	*cur;
	sizebuf_t		msg;
	int		i;

	MSG_Init( &msg, "New", msg_buf, sizeof( msg_buf ));

	if( cl->state != cs_connected )
		return false;

	// send the serverdata
	SV_SendServerdata( &msg, cl );

	// if the client was connected, tell the game .dll to disconnect him/her.
	if(( cl->state == cs_spawned ) && cl->edict )
		svgame.dllFuncs.pfnClientDisconnect( cl->edict );

	Q_snprintf( szName, sizeof( szName ), "%s", cl->name );
	Q_snprintf( szAddress, sizeof( szAddress ), "%s", NET_AdrToString( cl->netchan.remote_address ));
	Q_snprintf( szRejectReason, sizeof( szRejectReason ), "Connection rejected by game\n");

	// Allow the game dll to reject this client.
	if( !svgame.dllFuncs.pfnClientConnect( cl->edict, szName, szAddress, szRejectReason ))
	{
		// reject the connection and drop the client.
		SV_RejectConnection( cl->netchan.remote_address, "%s\n", szRejectReason );
		SV_DropClient( cl, false );
		return true;
	}

	// server info string
	MSG_BeginServerCmd( &msg, svc_stufftext );
	MSG_WriteString( &msg, va( "fullserverinfo \"%s\"\n", SV_Serverinfo( )));

	// collect the info about all the players and send to me
	for( i = 0, cur = svs.clients; i < svs.maxclients; i++, cur++ )
	{
		if( !cur->edict || cur->state != cs_spawned )
			continue;	// not in game yet
		SV_FullClientUpdate( cur, &msg );
	}

	// g-cont. why this is there?
	memset( &cl->lastcmd, 0, sizeof( cl->lastcmd ));

	Netchan_CreateFragments( &cl->netchan, &msg );
	Netchan_FragSend( &cl->netchan );

	return true;
}

/*
=================
SV_Disconnect_f

The client is going to disconnect, so remove the connection immediately
=================
*/
static qboolean SV_Disconnect_f( sv_client_t *cl )
{
	SV_DropClient( cl, false );	
	return true;
}

/*
==================
SV_ShowServerinfo_f

Dumps the serverinfo info string
==================
*/
static qboolean SV_ShowServerinfo_f( sv_client_t *cl )
{
	Info_Print( svs.serverinfo );
	return true;
}

/*
==================
SV_Pause_f
==================
*/
static qboolean SV_Pause_f( sv_client_t *cl )
{
	string	message;

	if( UI_CreditsActive( ))
		return true;

	if( !sv_pausable->value )
	{
		SV_ClientPrintf( cl, "Pause not allowed.\n" );
		return true;
	}

	if( FBitSet( cl->flags, FCL_HLTV_PROXY ))
	{
		SV_ClientPrintf( cl, "Spectators can not pause.\n" );
		return true;
	}

	if( !sv.paused ) Q_snprintf( message, MAX_STRING, "^2%s^7 paused the game\n", cl->name );
	else Q_snprintf( message, MAX_STRING, "^2%s^7 unpaused the game\n", cl->name );

	SV_TogglePause( message );

	return true;
}

/*
=================
SV_UserinfoChanged

Pull specific info from a newly changed userinfo string
into a more C freindly form.
=================
*/
void SV_UserinfoChanged( sv_client_t *cl )
{
	int		i, dupc = 1;
	edict_t		*ent = cl->edict;
	string		name1, name2;	
	sv_client_t	*current;
	const char		*val;

	if( !COM_CheckString( cl->userinfo ))
		return;

	val = Info_ValueForKey( cl->userinfo, "name" );
	Q_strncpy( name2, val, sizeof( name2 ));
	COM_TrimSpace( name2, name1 );

	if( !Q_stricmp( name1, "console" ))
	{
		Info_SetValueForKey( cl->userinfo, "name", "unnamed", MAX_INFO_STRING );
		val = Info_ValueForKey( cl->userinfo, "name" );
	}
	else if( Q_strcmp( name1, val ))
	{
		Info_SetValueForKey( cl->userinfo, "name", name1, MAX_INFO_STRING );
		val = Info_ValueForKey( cl->userinfo, "name" );
	}

	if( !Q_strlen( name1 ))
	{
		Info_SetValueForKey( cl->userinfo, "name", "unnamed", MAX_INFO_STRING );
		val = Info_ValueForKey( cl->userinfo, "name" );
		Q_strncpy( name2, "unnamed", sizeof( name2 ));
		Q_strncpy( name1, "unnamed", sizeof( name1 ));
	}

	// check to see if another user by the same name exists
	while( 1 )
	{
		for( i = 0, current = svs.clients; i < svs.maxclients; i++, current++ )
		{
			if( current == cl || current->state != cs_spawned )
				continue;

			if( !Q_stricmp( current->name, val ))
				break;
		}

		if( i != svs.maxclients )
		{
			// dup name
			Q_snprintf( name2, sizeof( name2 ), "%s (%u)", name1, dupc++ );
			Info_SetValueForKey( cl->userinfo, "name", name2, MAX_INFO_STRING );
			val = Info_ValueForKey( cl->userinfo, "name" );
			Q_strncpy( cl->name, name2, sizeof( cl->name ));
		}
		else
		{
			if( dupc == 1 ) // unchanged
				Q_strncpy( cl->name, name1, sizeof( cl->name ));
			break;
		}
	}

	// rate command
	val = Info_ValueForKey( cl->userinfo, "rate" );
	if( Q_strlen( val ))
		cl->netchan.rate = bound( MIN_RATE, Q_atoi( val ), MAX_RATE );
	else cl->netchan.rate = DEFAULT_RATE;

	// movement prediction	
	if( Q_atoi( Info_ValueForKey( cl->userinfo, "cl_nopred" )))
		ClearBits( cl->flags, FCL_PREDICT_MOVEMENT );
	else SetBits( cl->flags, FCL_PREDICT_MOVEMENT );

	// lag compensation
	if( Q_atoi( Info_ValueForKey( cl->userinfo, "cl_lc" )))
		SetBits( cl->flags, FCL_LAG_COMPENSATION );
	else ClearBits( cl->flags, FCL_LAG_COMPENSATION );

	// weapon perdiction
	if( Q_atoi( Info_ValueForKey( cl->userinfo, "cl_lw" )))
		SetBits( cl->flags, FCL_LOCAL_WEAPONS );
	else ClearBits( cl->flags, FCL_LOCAL_WEAPONS );

	val = Info_ValueForKey( cl->userinfo, "cl_updaterate" );

	if( Q_strlen( val ))
	{
		if( Q_atoi( val ) != 0 )
		{
			int i = bound( 10, Q_atoi( val ), 300 );
			cl->cl_updaterate = 1.0 / i;
		}
		else cl->cl_updaterate = 0.0;
	}

	// call prog code to allow overrides
	svgame.dllFuncs.pfnClientUserInfoChanged( cl->edict, cl->userinfo );

	val = Info_ValueForKey( cl->userinfo, "name" );
	Q_strncpy( cl->name, val, sizeof( cl->name ));
	ent->v.netname = MAKE_STRING( cl->name );
}

/*
==================
SV_UpdateUserinfo_f
==================
*/
static qboolean SV_UpdateUserinfo_f( sv_client_t *cl )
{
	Q_strncpy( cl->userinfo, Cmd_Argv( 1 ), sizeof( cl->userinfo ));

	if( cl->state >= cs_connected )
		SetBits( cl->flags, FCL_RESEND_USERINFO ); // needs for update client info
	return true;
}

/*
==================
SV_SetInfo_f
==================
*/
static qboolean SV_SetInfo_f( sv_client_t *cl )
{
	Info_SetValueForKey( cl->userinfo, Cmd_Argv( 1 ), Cmd_Argv( 2 ), MAX_INFO_STRING );

	if( cl->state >= cs_connected )
		SetBits( cl->flags, FCL_RESEND_USERINFO ); // needs for update client info
	return true;
}

/*
==================
SV_Noclip_f
==================
*/
static qboolean SV_Noclip_f( sv_client_t *cl )
{
	edict_t	*pEntity = cl->edict;

	if( !Cvar_VariableInteger( "sv_cheats" ) || sv.background )
		return true;

	if( pEntity->v.movetype != MOVETYPE_NOCLIP )
	{
		SV_ClientPrintf( cl, "noclip ON\n" );
		pEntity->v.movetype = MOVETYPE_NOCLIP;
	}
	else
	{
		SV_ClientPrintf( cl, "noclip OFF\n" );
		pEntity->v.movetype =  MOVETYPE_WALK;
	}

	return true;
}

/*
==================
SV_Godmode_f
==================
*/
static qboolean SV_Godmode_f( sv_client_t *cl )
{
	edict_t	*pEntity = cl->edict;

	if( !Cvar_VariableInteger( "sv_cheats" ) || sv.background )
		return true;

	pEntity->v.flags = pEntity->v.flags ^ FL_GODMODE;
	if( pEntity->v.takedamage == DAMAGE_AIM )
		pEntity->v.takedamage = DAMAGE_NO;
	else pEntity->v.takedamage = DAMAGE_AIM;

	if( !FBitSet( pEntity->v.flags, FL_GODMODE ))
		SV_ClientPrintf( cl, "godmode OFF\n" );
	else SV_ClientPrintf( cl, "godmode ON\n" );

	return true;
}

/*
==================
SV_Notarget_f
==================
*/
static qboolean SV_Notarget_f( sv_client_t *cl )
{
	edict_t	*pEntity = cl->edict;

	if( !Cvar_VariableInteger( "sv_cheats" ) || sv.background )
		return true;

	pEntity->v.flags = pEntity->v.flags ^ FL_NOTARGET;

	if( !FBitSet( pEntity->v.flags, FL_NOTARGET ))
		SV_ClientPrintf( cl, "notarget OFF\n" );
	else SV_ClientPrintf( cl, "notarget ON\n" );

	return true;
}

/*
==================
SV_Kill_f
==================
*/
static qboolean SV_Kill_f( sv_client_t *cl )
{
	if( !SV_IsValidEdict( cl->edict ))
		return true;

	if( cl->edict->v.health <= 0.0f )
	{
		SV_ClientPrintf( cl, "Can't suicide - already dead!\n");
		return true;
	}

	svgame.dllFuncs.pfnClientKill( cl->edict );

	return true;
}

/*
==================
SV_SendRes_f
==================
*/
static qboolean SV_SendRes_f( sv_client_t *cl )
{
	byte	buffer[MAX_INIT_MSG];
	sizebuf_t	msg;

	if( cl->state != cs_connected )
		return false;

	MSG_Init( &msg, "SendResources", buffer, sizeof( buffer ));

	if( svs.maxclients > 1 && FBitSet( cl->flags, FCL_SEND_RESOURCES ))
		return true;

	SetBits( cl->flags, FCL_SEND_RESOURCES );
	SV_SendResources( cl, &msg );

	Netchan_CreateFragments( &cl->netchan, &msg );
	Netchan_FragSend( &cl->netchan );

	return true;
}

/*
==================
SV_DownloadFile_f
==================
*/
static qboolean SV_DownloadFile_f( sv_client_t *cl )
{
	const char	*name;

	if( Cmd_Argc() < 2 )
		return true;

	name = Cmd_Argv( 1 );

	if( !COM_CheckString( name ))
		return true;

	if( !COM_IsSafeFileToDownload( name ) || !sv_allow_download.value )
	{
		SV_FailDownload( cl, name );
		return true;
	}

	// g-cont. now we supports hot precache
	if( name[0] != '!' )
	{
		if( sv_send_resources.value )
		{
			int i;

			// security: allow download only precached resources
			for( i = 0; i < sv.num_resources; i++ )
			{
				const char *cmpname = name;

				if( sv.resources[i].type == t_sound )
					cmpname += sizeof( DEFAULT_SOUNDPATH ) - 1; // cut "sound/" off

				if( !Q_strncmp( sv.resources[i].szFileName, cmpname, 64 ) )
					break;
			}

			if( i == sv.num_resources )
			{
				SV_FailDownload( cl, name );
				return true;
			}

			// also check the model textures
			if( !Q_stricmp( COM_FileExtension( name ), "mdl" ))
			{
				if( FS_FileExists( Mod_StudioTexName( name ), false ) > 0 )
					Netchan_CreateFileFragments( &cl->netchan, Mod_StudioTexName( name ));
			}

			if( Netchan_CreateFileFragments( &cl->netchan, name ))
			{
				Netchan_FragSend( &cl->netchan );
				return true;
			}
		}

		SV_FailDownload( cl, name );
		return true;
	}

	if( Q_strlen( name ) == 36 && !Q_strnicmp( name, "!MD5", 4 ) && sv_send_logos.value )
	{
		resource_t	custResource;
		byte		md5[32];
		byte		*pbuf;
		int		size;

		memset( &custResource, 0, sizeof( custResource ) );
		COM_HexConvert( name + 4, 32, md5 );

		if( HPAK_ResourceForHash( CUSTOM_RES_PATH, md5, &custResource ))
		{
			if( HPAK_GetDataPointer( CUSTOM_RES_PATH, &custResource, &pbuf, &size ))
			{
				if( size )
				{
					Netchan_CreateFileFragmentsFromBuffer( &cl->netchan, name, pbuf, size );
					Netchan_FragSend( &cl->netchan );
					Mem_Free( pbuf );
				}
			}
		}
	}
	else
	{
		SV_FailDownload( cl, name );
	}

	return true;
}

/*
==================
SV_Spawn_f
==================
*/
static qboolean SV_Spawn_f( sv_client_t *cl )
{
	if( cl->state != cs_connected )
		return false;

	// handle the case of a level changing while a client was connecting
	if( Q_atoi( Cmd_Argv( 1 )) != svs.spawncount )
	{
		SV_New_f( cl );
		return true;
	}

	SV_PutClientInServer( cl );

	// if we are paused, tell the clients
	if( sv.paused )
	{
		MSG_BeginServerCmd( &sv.reliable_datagram, svc_setpause );
		MSG_WriteByte( &sv.reliable_datagram, sv.paused );
		SV_ClientPrintf( cl, "Server is paused.\n" );
	}
	return true;
}

/*
==================
SV_Begin_f
==================
*/
static qboolean SV_Begin_f( sv_client_t *cl )
{
	if( cl->state != cs_connected )
		return false;

	// now client is spawned
	cl->state = cs_spawned;
	return true;
}

/*
==================
SV_SendBuildInfo_f
==================
*/
static qboolean SV_SendBuildInfo_f( sv_client_t *cl )
{
	SV_ClientPrintf( cl, "Server running %s %s (build %i-%s, %s-%s)\n",
		XASH_ENGINE_NAME, XASH_VERSION, Q_buildnum(), Q_buildcommit(), Q_buildos(), Q_buildarch() );
	return true;
}


ucmd_t ucmds[] =
{
{ "new", SV_New_f },
{ "god", SV_Godmode_f },
{ "kill", SV_Kill_f },
{ "begin", SV_Begin_f },
{ "spawn", SV_Spawn_f },
{ "pause", SV_Pause_f },
{ "noclip", SV_Noclip_f },
{ "log", SV_ServerLog_f },
{ "setinfo", SV_SetInfo_f },
{ "sendres", SV_SendRes_f },
{ "notarget", SV_Notarget_f },
{ "info", SV_ShowServerinfo_f },
{ "dlfile", SV_DownloadFile_f },
{ "disconnect", SV_Disconnect_f },
{ "userinfo", SV_UpdateUserinfo_f },
{ "_sv_build_info", SV_SendBuildInfo_f },
{ NULL, NULL }
};

/*
==================
SV_ExecuteUserCommand
==================
*/
void SV_ExecuteClientCommand( sv_client_t *cl, char *s )
{
	ucmd_t	*u;

	Cmd_TokenizeString( s );

	for( u = ucmds; u->name; u++ )
	{
		if( !Q_strcmp( Cmd_Argv( 0 ), u->name ))
		{
			if( !u->func( cl ))
				Con_Printf( "'%s' is not valid from the console\n", u->name );
			else Con_Reportf( "ucmd->%s()\n", u->name );
			break;
		}
	}

	if( !u->name && sv.state == ss_active )
	{
		// custom client commands
		svgame.dllFuncs.pfnClientCommand( cl->edict );

		if( !Q_strcmp( Cmd_Argv( 0 ), "fullupdate" ))
		{
			// resend the ambient sounds for demo recording
			SV_RestartAmbientSounds();
			// resend all the decals for demo recording
			SV_RestartDecals();
			// resend all the static ents for demo recording
			SV_RestartStaticEnts();
			// resend the viewentity
			SV_UpdateClientView( cl );
		}
	}
}

/*
==================
SV_TSourceEngineQuery
==================
*/
void SV_TSourceEngineQuery( netadr_t from )
{
	// A2S_INFO
	char	answer[1024] = "";
	int	count = 0, bots = 0;
	int	index;
	sizebuf_t	buf;

	if( svs.clients )
	{
		for( index = 0; index < svs.maxclients; index++ )
		{
			if( svs.clients[index].state >= cs_connected )
			{
				if( FBitSet( svs.clients[index].flags, FCL_FAKECLIENT ))
					bots++;
				else count++;
			}
		}
	}

	MSG_Init( &buf, "TSourceEngineQuery", answer, sizeof( answer ));

	MSG_WriteByte( &buf, 'm' );
	MSG_WriteString( &buf, NET_AdrToString( net_local ));
	MSG_WriteString( &buf, hostname.string );
	MSG_WriteString( &buf, sv.name );
	MSG_WriteString( &buf, GI->gamefolder );
	MSG_WriteString( &buf, GI->title );
	MSG_WriteByte( &buf, count );
	MSG_WriteByte( &buf, svs.maxclients );
	MSG_WriteByte( &buf, PROTOCOL_VERSION );
	MSG_WriteByte( &buf, Host_IsDedicated() ? 'D' : 'L' );
	MSG_WriteByte( &buf, 'W' );

	if( Q_stricmp( GI->gamefolder, "valve" ))
	{
		MSG_WriteByte( &buf, 1 ); // mod
		MSG_WriteString( &buf, GI->game_url );
		MSG_WriteString( &buf, GI->update_url );
		MSG_WriteByte( &buf, 0 );
		MSG_WriteLong( &buf, (int)GI->version );
		MSG_WriteLong( &buf, GI->size );

		if( GI->gamemode == 2 )
			MSG_WriteByte( &buf, 1 ); // multiplayer_only
		else MSG_WriteByte( &buf, 0 );

		if( Q_strstr( GI->game_dll, "hl." ))
			MSG_WriteByte( &buf, 0 ); // Half-Life DLL
		else MSG_WriteByte( &buf, 1 ); // Own DLL
	}
	else MSG_WriteByte( &buf, 0 ); // Half-Life

	MSG_WriteByte( &buf, GI->secure ); // unsecure
	MSG_WriteByte( &buf, bots );

	NET_SendPacket( NS_SERVER, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ), from );
}

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
void SV_ConnectionlessPacket( netadr_t from, sizebuf_t *msg )
{
	char	*args;
	const char	*pcmd;
	char buf[MAX_SYSPATH];
	int	len = sizeof( buf );

	// prevent flooding from banned address
	if( SV_CheckIP( &from ) )
		return;

	MSG_Clear( msg );
	MSG_ReadLong( msg );// skip the -1 marker

	args = MSG_ReadStringLine( msg );
	Cmd_TokenizeString( args );

	pcmd = Cmd_Argv( 0 );
	Con_Reportf( "SV_ConnectionlessPacket: %s : %s\n", NET_AdrToString( from ), pcmd );

	if( !Q_strcmp( pcmd, "ping" )) SV_Ping( from );
	else if( !Q_strcmp( pcmd, "ack" )) SV_Ack( from );
	else if( !Q_strcmp( pcmd, "info" )) SV_Info( from );
	else if( !Q_strcmp( pcmd, "bandwidth" )) SV_TestBandWidth( from );
	else if( !Q_strcmp( pcmd, "getchallenge" )) SV_GetChallenge( from );
	else if( !Q_strcmp( pcmd, "connect" )) SV_ConnectClient( from );
	else if( !Q_strcmp( pcmd, "rcon" )) SV_RemoteCommand( from, msg );
	else if( !Q_strcmp( pcmd, "netinfo" )) SV_BuildNetAnswer( from );
	else if( !Q_strcmp( pcmd, "s" )) SV_AddToMaster( from, msg );
	else if( !Q_strcmp( pcmd, "T" "Source" )) SV_TSourceEngineQuery( from );
	else if( !Q_strcmp( pcmd, "i" )) NET_SendPacket( NS_SERVER, 5, "\xFF\xFF\xFF\xFFj", from ); // A2A_PING
	else if( svgame.dllFuncs.pfnConnectionlessPacket( &from, args, buf, &len ))
	{
		// user out of band message (must be handled in CL_ConnectionlessPacket)
		if( len > 0 ) Netchan_OutOfBand( NS_SERVER, from, len, (byte*)buf );
	}
	else Con_DPrintf( S_ERROR "bad connectionless packet from %s:\n%s\n", NET_AdrToString( from ), args );
}

/*
==================
SV_ParseClientMove

The message usually contains all the movement commands 
that were in the last three packets, so that the information
in dropped packets can be recovered.

On very fast clients, there may be multiple usercmd packed into
each of the backup packets.
==================
*/
static void SV_ParseClientMove( sv_client_t *cl, sizebuf_t *msg )
{
	client_frame_t	*frame;
	int		key, size, checksum1, checksum2;
	int		i, numbackup, totalcmds, numcmds;
	usercmd_t		nullcmd, *to, *from;
	usercmd_t		cmds[CMD_BACKUP];
	float		packet_loss;
	edict_t		*player;
	model_t		*model;

	player = cl->edict;

	frame = &cl->frames[cl->netchan.incoming_acknowledged & SV_UPDATE_MASK];
	memset( &nullcmd, 0, sizeof( usercmd_t ));
	memset( cmds, 0, sizeof( cmds ));

	key = MSG_GetRealBytesRead( msg );
	checksum1 = MSG_ReadByte( msg );
	packet_loss = MSG_ReadByte( msg );

	numbackup = MSG_ReadByte( msg );
	numcmds = MSG_ReadByte( msg );

	totalcmds = numcmds + numbackup;
	net_drop -= (numcmds - 1);

	if( totalcmds < 0 || totalcmds >= CMD_MASK )
	{
		Con_Reportf( S_ERROR "SV_ParseClientMove: %s sending too many commands %i\n", cl->name, totalcmds );
		SV_DropClient( cl, false );
		return;
	}

	from = &nullcmd;	// first cmd are starting from null-compressed usercmd_t

	for( i = totalcmds - 1; i >= 0; i-- )
	{
		to = &cmds[i];
		MSG_ReadDeltaUsercmd( msg, from, to );
		from = to; // get new baseline
	}

	if( cl->state != cs_spawned )
		return;

	// if the checksum fails, ignore the rest of the packet
	size = MSG_GetRealBytesRead( msg ) - key - 1;
	checksum2 = CRC32_BlockSequence( msg->pData + key + 1, size, cl->netchan.incoming_sequence );

	if( checksum2 != checksum1 )
	{
		Con_Reportf( S_ERROR "SV_UserMove: failed command checksum for %s (%d != %d)\n", cl->name, checksum2, checksum1 );
		return;
	}

	cl->packet_loss = packet_loss;

	// freeze player for some reasons if loadgame was executed
	if( GameState->loadGame )
		return;

	// check for pause or frozen
	if( sv.paused || !CL_IsInGame() || SV_PlayerIsFrozen( player ))
	{
		for( i = 0; i < numcmds; i++ )
		{
			cmds[i].msec = 0;
			cmds[i].forwardmove = 0;
			cmds[i].sidemove = 0;
			cmds[i].upmove = 0;
			cmds[i].buttons = 0;

			if( SV_PlayerIsFrozen( player ))
				cmds[i].impulse = 0;

			VectorCopy( cmds[i].viewangles, player->v.v_angle );
		}
		net_drop = 0;
	}
	else
	{
		if( !player->v.fixangle )
			VectorCopy( cmds[0].viewangles, player->v.v_angle );
	}

	SV_EstablishTimeBase( cl, cmds, net_drop, numbackup, numcmds );

	if( net_drop < 24 )
	{
		while( net_drop > numbackup )
		{
			SV_RunCmd( cl, &cl->lastcmd, 0 );
			net_drop--;
		}

		while( net_drop > 0 )
		{
			i = numcmds + net_drop - 1;
			SV_RunCmd( cl, &cmds[i], cl->netchan.incoming_sequence - i );
			net_drop--;
		}
	}

	for( i = numcmds - 1; i >= 0; i-- )
	{
		SV_RunCmd( cl, &cmds[i], cl->netchan.incoming_sequence - i );
	}

	cl->lastcmd = cmds[0];

	// adjust latency time by 1/2 last client frame since
	// the message probably arrived 1/2 through client's frame loop
	frame->ping_time -= ( cl->lastcmd.msec * 0.5f ) / 1000.0f;
	frame->ping_time = Q_max( 0.0f, frame->ping_time );
	model = SV_ModelHandle( player->v.modelindex );

	if( model && model->type == mod_studio )
	{
		// g-cont. yes we using svgame.globals->time instead of sv.time
		if( player->v.animtime > svgame.globals->time + sv.frametime )
			player->v.animtime = svgame.globals->time + sv.frametime;
	}
}

/*
===================
SV_ParseResourceList

Parse resource list
===================
*/
void SV_ParseResourceList( sv_client_t *cl, sizebuf_t *msg )
{
	int		totalsize;
	resource_t	*resource;
	int		i, total;
	resourceinfo_t	ri;

	total = MSG_ReadShort( msg );

	SV_ClearResourceList( &cl->resourcesneeded );
	SV_ClearResourceList( &cl->resourcesonhand );

	for( i = 0; i < total; i++ )
	{
		resource = Z_Calloc( sizeof( resource_t ) );
		Q_strncpy( resource->szFileName, MSG_ReadString( msg ), sizeof( resource->szFileName ));
		resource->type = MSG_ReadByte( msg );
		resource->nIndex = MSG_ReadShort( msg );
		resource->nDownloadSize = MSG_ReadLong( msg );
		resource->ucFlags = MSG_ReadByte( msg );
		resource->pNext = NULL;
		resource->pPrev = NULL;
		ClearBits( resource->ucFlags, RES_WASMISSING );

		if( FBitSet( resource->ucFlags, RES_CUSTOM ))
			MSG_ReadBytes( msg, resource->rgucMD5_hash, 16 );

		if( resource->type > t_world || resource->nDownloadSize > 1024 * 1024 * 1024 )
		{
			SV_ClearResourceList( &cl->resourcesneeded );
			SV_ClearResourceList( &cl->resourcesonhand );
			return;
		}
		SV_AddToResourceList( resource, &cl->resourcesneeded );
	}

	totalsize = COM_SizeofResourceList( &cl->resourcesneeded, &ri );

	if( totalsize != 0 && sv_allow_upload.value )
	{
		Con_DPrintf( "Verifying and uploading resources...\n" );

		if( totalsize != 0 )
		{
			Con_DPrintf( "Custom resources total %.2fK\n", totalsize / 1024.0 );

			if ( ri.info[t_model].size != 0 )
				Con_DPrintf( "  Models:  %.2fK\n", ri.info[t_model].size / 1024.0 );

			if ( ri.info[t_sound].size != 0 )
				Con_DPrintf( "  Sounds:  %.2fK\n", ri.info[t_sound].size / 1024.0 );

			if ( ri.info[t_decal].size != 0 )
				Con_DPrintf( "  Decals:  %.2fK\n", ri.info[t_decal].size / 1024.0 );

			if ( ri.info[t_skin].size != 0 )
				Con_DPrintf( "  Skins :  %.2fK\n", ri.info[t_skin].size / 1024.0 );

			if ( ri.info[t_generic].size != 0 )
				Con_DPrintf( "  Generic :  %.2fK\n", ri.info[t_generic].size / 1024.0 );

			if ( ri.info[t_eventscript].size != 0 )
				Con_DPrintf( "  Events  :  %.2fK\n", ri.info[t_eventscript].size / 1024.0 );

			Con_DPrintf( "----------------------\n" );
		}

		totalsize = SV_EstimateNeededResources( cl );

		if( totalsize > sv_uploadmax.value * 1024 * 1024 )
		{
			SV_ClearResourceList( &cl->resourcesneeded );
			SV_ClearResourceList( &cl->resourcesonhand );
			return;
		}
		Con_DPrintf( "resources to request: %s\n", Q_memprint( totalsize ));
	}

	cl->upstate = us_processing;
	SV_BatchUploadRequest( cl );
}

/*
===================
SV_ParseCvarValue

Parse a requested value from client cvar 
===================
*/
void SV_ParseCvarValue( sv_client_t *cl, sizebuf_t *msg )
{
	const char *value = MSG_ReadString( msg );

	if( svgame.dllFuncs2.pfnCvarValue != NULL )
		svgame.dllFuncs2.pfnCvarValue( cl->edict, value );
	Con_Reportf( "Cvar query response: name:%s, value:%s\n", cl->name, value );
}

/*
===================
SV_ParseCvarValue2

Parse a requested value from client cvar 
===================
*/
void SV_ParseCvarValue2( sv_client_t *cl, sizebuf_t *msg )
{
	string	name, value;
	int	requestID = MSG_ReadLong( msg );

	Q_strcpy( name, MSG_ReadString( msg ));
	Q_strcpy( value, MSG_ReadString( msg ));

	if( svgame.dllFuncs2.pfnCvarValue2 != NULL )
		svgame.dllFuncs2.pfnCvarValue2( cl->edict, requestID, name, value );
	Con_Reportf( "Cvar query response: name:%s, request ID %d, cvar:%s, value:%s\n", cl->name, requestID, name, value );
}

/*
===================
SV_ExecuteClientMessage

Parse a client packet
===================
*/
void SV_ExecuteClientMessage( sv_client_t *cl, sizebuf_t *msg )
{
	qboolean		move_issued = false;
	client_frame_t	*frame;
	int		c;

	ASSERT( cl->frames != NULL );

	// calc ping time
	frame = &cl->frames[cl->netchan.incoming_acknowledged & SV_UPDATE_MASK];

	// ping time doesn't factor in message interval, either
	frame->ping_time = host.realtime - frame->senttime - cl->cl_updaterate;

	// on first frame ( no senttime ) don't skew ping
	if( frame->senttime == 0.0f ) frame->ping_time = 0.0f;

	// don't skew ping based on signon stuff either
	if(( host.realtime - cl->connection_started ) < 2.0f && ( frame->ping_time > 0.0 ))
		frame->ping_time = 0.0f;

	cl->latency = SV_CalcClientTime( cl );
	cl->delta_sequence = -1; // no delta unless requested
				
	// read optional clientCommand strings
	while( cl->state != cs_zombie )
	{
		if( MSG_CheckOverflow( msg ))
		{
			Con_DPrintf( S_ERROR "incoming overflow for %s\n", cl->name );
			SV_DropClient( cl, false );
			return;
		}

		// end of message
		if( MSG_GetNumBitsLeft( msg ) < 8 )
			break;

		c = MSG_ReadClientCmd( msg );

		switch( c )
		{
		case clc_nop:
			break;
		case clc_delta:
			cl->delta_sequence = MSG_ReadByte( msg );
			break;
		case clc_move:
			if( move_issued ) return; // someone is trying to cheat...
			move_issued = true;
			SV_ParseClientMove( cl, msg );
			break;
		case clc_stringcmd:	
			SV_ExecuteClientCommand( cl, MSG_ReadString( msg ));
			if( cl->state == cs_zombie )
				return; // disconnect command
			break;
		case clc_resourcelist:
			SV_ParseResourceList( cl, msg );
			break;
		case clc_fileconsistency:
			SV_ParseConsistencyResponse( cl, msg );
			break;
		case clc_requestcvarvalue:
			SV_ParseCvarValue( cl, msg );
			break;
		case clc_requestcvarvalue2:
			SV_ParseCvarValue2( cl, msg );
			break;
		default:
			Con_DPrintf( S_ERROR "%s: clc_bad\n", cl->name );
			SV_DropClient( cl, false );
			return;
		}
	}
 }
