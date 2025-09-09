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
	string address;
	netadr_t adr; // temporary, rewritten after each send

	uint heartbeat_challenge;
	double last_heartbeat;
} master_t;

static struct masterlist_s
{
	master_t *list;
	qboolean modified;
} ml;

static CVAR_DEFINE_AUTO( sv_verbose_heartbeats, "0", 0, "print every heartbeat to console" );

#define HEARTBEAT_SECONDS	((sv_nat.value > 0.0f) ? 60.0f : 300.0f)  	// 1 or 5 minutes

/*
========================
NET_GetMasterHostByName
========================
*/
static net_gai_state_t NET_GetMasterHostByName( master_t *m )
{
	net_gai_state_t res = NET_StringToAdrNB( m->address, &m->adr, m->v6only );

	if( res == NET_EAI_OK )
		return res;

	m->adr.type = 0;
	if( res == NET_EAI_NONAME )
		Con_Reportf( "Can't resolve adr: %s\n", m->address );

	return res;
}

/*
========================
NET_SendToMasters

Send request to all masterservers list
return true if would block
========================
*/
qboolean NET_SendToMasters( netsrc_t sock, size_t len, const void *data )
{
	master_t *list;
	qboolean wait = false;

	for( list = ml.list; list; list = list->next )
	{
		if( list->sent )
			continue;

		switch( NET_GetMasterHostByName( list ))
		{
		case NET_EAI_AGAIN:
			list->sent = false;
			wait = true;
			break;
		case NET_EAI_NONAME:
			list->sent = true;
			break;
		case NET_EAI_OK:
			list->sent = true;
			NET_SendPacket( sock, len, data, list->adr );
			break;
		}
	}

	if( !wait )
	{
		// reset sent state
		for( list = ml.list; list; list = list->next )
			list->sent = false;
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
	MSG_WriteBytes( &msg, "q\xFF", 2 );
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

	for( m = ml.list; m; m = m->next )
		m->last_heartbeat = MAX_HEARTBEAT;
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

	for( m = ml.list; m; m = m->next )
	{
		if( host.realtime - m->last_heartbeat < HEARTBEAT_SECONDS )
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
	while( NET_SendToMasters( NS_SERVER, 2, "\x62\x0A" ));
}


/*
========================
NET_GetMasterFromAdr

========================
*/
static master_t *NET_GetMasterFromAdr( netadr_t adr )
{
	master_t *master;

	for( master = ml.list; master; master = master->next )
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
qboolean NET_IsMasterAdr( netadr_t adr )
{
	return NET_GetMasterFromAdr( adr ) != NULL;
}

/*
========================
NET_AddMaster

Add master to the list
========================
*/
static void NET_AddMaster( const char *addr, qboolean save, qboolean v6only )
{
	master_t *master, *last;

	for( last = ml.list; last && last->next; last = last->next )
	{
		if( !Q_strcmp( last->address, addr ) ) // already exists
			return;
	}

	master = Mem_Malloc( host.mempool, sizeof( *master ) );
	Q_strncpy( master->address, addr, sizeof( master->address ));
	master->sent = false;
	master->save = save;
	master->v6only = v6only;
	master->next = NULL;
	master->adr.type = 0;

	// link in
	if( last )
		last->next = master;
	else
		ml.list = master;
}

static void NET_AddMaster_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Msg( S_USAGE "addmaster <address>\n");
		return;
	}

	NET_AddMaster( Cmd_Argv( 1 ), true, false ); // save them into config
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
	while( ml.list )
	{
		master_t *prev = ml.list;
		ml.list = ml.list->next;
		Mem_Free( prev );
	}
}

/*
========================
NET_ListMasters_f

Display current master linked list
========================
*/
static void NET_ListMasters_f( void )
{
	master_t *list;
	int i;

	Con_Printf( "Master servers:\n" );

	for( i = 1, list = ml.list; list; i++, list = list->next )
	{
		Con_Printf( "%d\t%s", i, list->address );
		if( list->adr.type != 0 )
			Con_Printf( "\t%s\n", NET_AdrToString( list->adr ));
		else Con_Printf( "\n" );
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

	afile = FS_LoadFile( "xashcomm.lst", NULL, true );

	if( !afile ) // file doesn't exist yet
	{
		Con_Reportf( "Cannot load xashcomm.lst\n" );
		return;
	}

	pfile = (char*)afile;

	// format: master <addr>\n
	while( ( pfile = COM_ParseFile( pfile, token, sizeof( token ) ) ) )
	{
		if( !Q_strcmp( token, "master" )) // load addr
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ) );
			NET_AddMaster( token, true, false );
		}
		else if( !Q_strcmp( token, "master6" ))
		{
			pfile = COM_ParseFile( pfile, token, sizeof( token ) );
			NET_AddMaster( token, true, true );
		}
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

	for( m = ml.list; m; m = m->next )
	{
		if( m->save )
			FS_Printf( f, "%s %s\n", m->v6only ? "master6" : "master", m->address );
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
		NET_AddMaster( "mentality.rip:27010", false, false );
		NET_AddMaster( "ms2.mentality.rip:27010", false, false );
		NET_AddMaster( "ms3.mentality.rip:27010", false, false );
	}

	{ // IPv6-only
		NET_AddMaster( "aaaa.mentality.rip:27010", false, true );
		NET_AddMaster( "aaaa.ms2.mentality.rip:27010", false, true );
	}

	{ // testing servers, might be offline
		NET_AddMaster( "mentality.rip:27011", false, false );
		NET_AddMaster( "aaaa.mentality.rip:27011", false, true );
	}

	NET_LoadMasters( );
}
