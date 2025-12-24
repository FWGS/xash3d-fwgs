/*
masterlist.c - multi-master list
Copyright (C) 2018 mittorn

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
#include "netchan.h"
#include "server.h"

typedef struct master_s
{
	struct master_s *next;
	qboolean sent; // TODO: get rid of this internal state
	qboolean save;
	qboolean v6only;
	qboolean gs;
	string address;
	netadr_t adr; // temporary, rewritten after each send

	uint heartbeat_challenge;
	double last_heartbeat;

	double resolve_time;
} master_t;

static struct masterlist_s
{
	master_t *head, *tail;
	qboolean modified;
} ml;

static CVAR_DEFINE_AUTO( sv_verbose_heartbeats, "0", 0, "print every heartbeat to console" );

#define HEARTBEAT_SECONDS      ((sv_nat.value > 0.0f) ? 60.0f : 300.0f) // 1 or 5 minutes
#define RESOLVE_EXPIRE_SECONDS (60.0f) // 1 minute to expire

static size_t NET_BuildMasterServerScanRequest( char *buf, size_t size, uint32_t key, qboolean nat, const char *filter, connprotocol_t proto )
{
	size_t remaining;
	char *info, temp[32];

	// TODO: pagination and region
	Q_strncpy( buf, A2M_SCAN_REQUEST, size );

	info = buf + sizeof( A2M_SCAN_REQUEST ) - 1;
	remaining = size - sizeof( A2M_SCAN_REQUEST );

	Q_strncpy( info, filter, remaining );

#ifndef XASH_ALL_SERVERS
	Info_SetValueForKey( info, "gamedir", GI->gamefolder, remaining );
#endif // XASH_ALL_SERVERS

	if( proto != PROTO_GOLDSRC )
	{
		// let master know about client version
		Info_SetValueForKey( info, "clver", XASH_VERSION, remaining );
		Info_SetValueForKey( info, "nat", nat ? "1" : "0", remaining );
		Info_SetValueForKey( info, "commit", g_buildcommit, remaining );
		Info_SetValueForKey( info, "branch", g_buildbranch, remaining );
		Info_SetValueForKey( info, "os", Q_buildos(), remaining );
		Info_SetValueForKey( info, "arch", Q_buildarch(), remaining );

		Q_snprintf( temp, sizeof( temp ), "%d", Q_buildnum() );
		Info_SetValueForKey( info, "buildnum", temp, remaining );

		Q_snprintf( temp, sizeof( temp ), "%x", key );
		Info_SetValueForKey( info, "key", temp, remaining );
	}

	return sizeof( A2M_SCAN_REQUEST ) + Q_strlen( info );
}

/*
========================
NET_GetMasterHostByName
========================
*/
static net_gai_state_t NET_GetMasterHostByName( master_t *m )
{
	net_gai_state_t res;

	if( host.realtime > m->resolve_time )
		m->adr.type = 0;

	if( m->adr.type )
		return NET_EAI_OK;

	res = NET_StringToAdrNB( m->address, &m->adr, m->v6only );

	if( res == NET_EAI_OK )
	{
		m->resolve_time = host.realtime + RESOLVE_EXPIRE_SECONDS;
		return res;
	}

	m->adr.type = 0;
	if( res == NET_EAI_NONAME )
		Con_Reportf( "Can't resolve adr: %s\n", m->address );

	return res;
}

static void NET_ClearSendState( void )
{
	// reset sent state
	for( master_t *master = ml.head; master; master = master->next )
		master->sent = false;
}

/*
========================
NET_SendToMasters

Send request to all masterservers list
return true if would block
========================
*/
static qboolean NET_SendToMasters( netsrc_t sock, size_t len, const void *data, connprotocol_t proto )
{
	master_t *master;
	qboolean wait = false;

	for( master = ml.head; master; master = master->next )
	{
		if( master->gs )
		{
			if( proto != PROTO_GOLDSRC )
				continue;
		}
		else
		{
			if( proto != PROTO_CURRENT )
				continue;
		}

		if( master->sent )
			continue;

		switch( NET_GetMasterHostByName( master ))
		{
		case NET_EAI_AGAIN:
			master->sent = false;
			wait = true;
			break;
		case NET_EAI_NONAME:
			master->sent = true;
			break;
		case NET_EAI_OK:
			master->sent = true;
			NET_SendPacket( sock, len, data, master->adr );
			break;
		}
	}

	return wait;
}

/*
========================
NET_AnnounceToMaster

========================
*/
static void NET_AnnounceToMaster( master_t *m )
{
	sizebuf_t msg;
	char buf[16];

	m->heartbeat_challenge = COM_RandomLong( 0, INT_MAX );

	MSG_Init( &msg, "Master Join", buf, sizeof( buf ));
	MSG_WriteBytes( &msg, S2M_HEARTBEAT, 2 );
	MSG_WriteDword( &msg, m->heartbeat_challenge );

	NET_SendPacket( NS_SERVER, MSG_GetNumBytesWritten( &msg ), MSG_GetData( &msg ), m->adr );

	if( sv_verbose_heartbeats.value )
	{
		Con_Printf( S_NOTE "sent heartbeat to %s (%s, 0x%x)\n",
			m->address, NET_AdrToString( m->adr ), m->heartbeat_challenge );
	}
}

/*
========================
NET_AnnounceToMaster

========================
*/
void NET_MasterClear( void )
{
	master_t *m;

	for( m = ml.head; m; m = m->next )
		m->last_heartbeat = MAX_HEARTBEAT;
}

/*
=================
NET_MasterQuery
=================
*/
qboolean NET_MasterQuery( uint32_t key, qboolean nat, const char *filter )
{
	char buf[512];
	size_t len;
	qboolean wait = false;

	len = NET_BuildMasterServerScanRequest( buf, sizeof( buf ), key, nat, filter, PROTO_CURRENT );
	wait = NET_SendToMasters( NS_CLIENT, len, buf, PROTO_CURRENT );

	// goldsrc don't have nat traversal extensions
	if( !nat )
	{
		len = NET_BuildMasterServerScanRequest( buf, sizeof( buf ), 0, false, filter, PROTO_GOLDSRC );
		wait |= NET_SendToMasters( NS_CLIENT, len, buf, PROTO_GOLDSRC );
	}

	if( !wait )
		NET_ClearSendState();

	return wait;
}

/*
========================
NET_MasterHeartbeat

========================
*/
void NET_MasterHeartbeat( void )
{
	master_t *m;

	if(( !public_server.value && !sv_nat.value ) || svs.maxclients == 1 )
		return; // only public servers send heartbeats

	for( m = ml.head; m; m = m->next )
	{
		if( host.realtime - m->last_heartbeat < HEARTBEAT_SECONDS )
			continue;

		if( m->gs )
			continue;

		switch( NET_GetMasterHostByName( m ))
		{
		case NET_EAI_AGAIN:
			m->last_heartbeat = MAX_HEARTBEAT; // retry on next frame
			if( sv_verbose_heartbeats.value )
				Con_Printf( S_NOTE "delay heartbeat to next frame until %s resolves\n", m->address );

			break;
		case NET_EAI_NONAME:
			m->last_heartbeat = host.realtime; // try to resolve again on next heartbeat
			break;
		case NET_EAI_OK:
			m->last_heartbeat = host.realtime;
			NET_AnnounceToMaster( m );
			break;
		}
	}
}

/*
=================
NET_MasterShutdown

Informs all masters that this server is going down
(ignored by master servers in current implementation)
=================
*/
void NET_MasterShutdown( void )
{
	NET_Config( true, false ); // allow remote
	while( NET_SendToMasters( NS_SERVER, 2, S2M_SHUTDOWN, PROTO_CURRENT ));
	NET_ClearSendState();
}


/*
========================
NET_GetMasterFromAdr

========================
*/
static master_t *NET_GetMasterFromAdr( netadr_t adr )
{
	master_t *master;

	for( master = ml.head; master; master = master->next )
	{
		if( NET_CompareAdr( adr, master->adr ))
			return master;
	}

	return NULL;
}


/*
========================
NET_GetMaster
========================
*/
qboolean NET_GetMaster( netadr_t from, uint *challenge, double *last_heartbeat )
{
	master_t *m;

	m = NET_GetMasterFromAdr( from );

	if( m )
	{
		*challenge = m->heartbeat_challenge;
		*last_heartbeat = m->last_heartbeat;
	}

	return m != NULL;
}

/*
========================
NET_IsMasterAdr

========================
*/
qboolean NET_IsMasterAdr( netadr_t adr, connprotocol_t *proto )
{
	master_t *master = NET_GetMasterFromAdr( adr );

	if( !master )
		return false;

	if( proto )
		*proto = master->gs ? PROTO_GOLDSRC : PROTO_CURRENT;

	return true;
}

/*
========================
NET_AddMaster

Add master to the list
========================
*/
static master_t *NET_AddMaster( const char *addr )
{
	master_t *master;

	for( master = ml.head; master; master = master->next )
	{
		if( !Q_stricmp( master->address, addr )) // already exists
			return master;
	}

	master = Mem_Calloc( host.mempool, sizeof( *master ));
	Q_strncpy( master->address, addr, sizeof( master->address ));

	if( ml.tail )
	{
		ml.tail->next = master;
		ml.tail = master;
	}
	else
	{
		ml.head = ml.tail = master;
	}

	return master;
}

static void NET_AddMaster_f( void )
{
	master_t *master;

	if( Cmd_Argc() != 2 )
	{
		Msg( S_USAGE "addmaster <address> [gs]\n");
		return;
	}

	master = NET_AddMaster( Cmd_Argv( 1 ));
	master->save = true;

	if( Q_stricmp( Cmd_Argv( 2 ), "gs" ))
		master->gs = true;

	ml.modified = true; // save config
}

/*
========================
NET_ClearMasters

Clear master list
========================
*/
static void NET_ClearMasters_f( void )
{
	while( ml.head )
	{
		master_t *head = ml.head;
		ml.head = ml.head->next;
		Mem_Free( head );
	}

	ml.tail = NULL;
}

/*
========================
NET_ListMasters_f

Display current master linked list
========================
*/
static void NET_ListMasters_f( void )
{
	master_t *master;
	int i;

	Con_Printf( "Master servers:\n" );

	for( i = 1, master = ml.head; master; i++, master = master->next )
	{
		Con_Printf( "%d\t%s", i, master->address );
		if( master->adr.type != 0 )
			Con_Printf( "\t%s", NET_AdrToString( master->adr ));

		if( master->gs )
			Con_Printf( " GoldSrc" );

		if( master->v6only )
			Con_Printf( " IPv6-only");

		Con_Printf( "\n" );
	}
}

/*
========================
NET_LoadMasters

Load master server list from xashcomm.lst
========================
*/
static void NET_LoadMasters( void )
{
	byte *afile;
	char *pfile;
	char token[MAX_TOKEN];

	afile = FS_LoadFile( "xashcomm.lst", NULL, false );

	if( !afile ) // file doesn't exist yet
	{
		Con_Reportf( "Cannot load xashcomm.lst\n" );
		return;
	}

	pfile = (char*)afile;

	// format: master <addr>\n
	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))))
	{
		master_t *master = NULL;

		//if( !Q_strcmp( token, "clear" ))
		//	NET_ClearMasters_f();

		if( !Q_strcmp( token, "master" )) // load addr
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			master = NET_AddMaster( token );
		}
		else if( !Q_strcmp( token, "master6" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			master = NET_AddMaster( token );
			master->v6only = true;
		}
		else if( !Q_strcmp( token, "mastergs" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ));
			master = NET_AddMaster( token );
			master->gs = true;
		}

		if( master )
			master->save = true;
	}

	Mem_Free( afile );

	ml.modified = false;
}

/*
========================
NET_SaveMasters

Save master server list to xashcomm.lst, except for default
========================
*/
void NET_SaveMasters( void )
{
	file_t *f;
	master_t *m;

	if( !ml.modified )
		return;

	f = FS_Open( "xashcomm.lst", "w", true );

	if( !f )
	{
		Con_Reportf( S_ERROR "Couldn't write xashcomm.lst\n" );
		return;
	}

	for( m = ml.head; m; m = m->next )
	{
		const char *key;

		if( !m->save )
			continue;

		if( m->v6only )
			key = "master6";
		else if( m->gs )
			key = "mastergs";
		else
			key = "master";

		FS_Printf( f, "%s %s\n", key, m->address );
	}

	FS_Close( f );
}

/*
========================
NET_InitMasters

Initialize master server list
========================
*/
void NET_InitMasters( void )
{
	Cmd_AddRestrictedCommand( "addmaster", NET_AddMaster_f, "add address to masterserver list" );
	Cmd_AddRestrictedCommand( "clearmasters", NET_ClearMasters_f, "clear masterserver list" );
	Cmd_AddCommand( "listmasters", NET_ListMasters_f, "list masterservers" );

	Cvar_RegisterVariable( &sv_verbose_heartbeats );

	{ // IPv4-only
		NET_AddMaster( "mentality.rip:27010" );
		NET_AddMaster( "ms2.mentality.rip:27010" );
		NET_AddMaster( "ms3.mentality.rip:27010" );
	}

	{ // IPv6-only
		NET_AddMaster( "aaaa.mentality.rip:27010" )->v6only = true;
		NET_AddMaster( "aaaa.ms2.mentality.rip:27010" )->v6only = true;
	}

	{ // testing servers, might be offline
		NET_AddMaster( "mentality.rip:27011" );
		NET_AddMaster( "aaaa.mentality.rip:27011" )->v6only = true;
	}

	NET_LoadMasters();
}
