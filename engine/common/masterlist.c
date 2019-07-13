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

typedef struct master_s
{
	struct master_s *next;
	qboolean sent;
	qboolean save;
	string address;
} master_t;

struct masterlist_s
{
	master_t *list;
	qboolean modified;
} ml;

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
		netadr_t adr;
		int res;

		if( list->sent )
			continue;

		res = NET_StringToAdrNB( list->address, &adr );

		if( !res )
		{
			Con_Reportf( "Can't resolve adr: %s\n", list->address );
			list->sent = true;
			continue;
		}

		if( res == 2 )
		{
			list->sent = false;
			wait = true;
			continue;
		}

		list->sent = true;

		NET_SendPacket( sock, len, data, adr );
	}

	if( !wait )
	{
		list = ml.list;

		while( list )
		{
			list->sent = false;
			list = list->next;
		}
	}

	return wait;
}

/*
========================
NET_AddMaster

Add master to the list
========================
*/
static void NET_AddMaster( const char *addr, qboolean save )
{
	master_t *master, *last;

	for( last = ml.list; last && last->next; last = last->next )
	{
		if( !Q_strcmp( last->address, addr ) ) // already exists
			return;
	}

	master = Mem_Malloc( host.mempool, sizeof( master_t ) );
	Q_strncpy( master->address, addr, MAX_STRING );
	master->sent = false;
	master->save = save;
	master->next = NULL;

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

	NET_AddMaster( Cmd_Argv( 1 ), true ); // save them into config
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

	Msg( "Master servers\n=============\n" );


	for( i = 1, list = ml.list; list; i++, list = list->next )
	{
		Msg( "%d\t%s\n", i, list->address );
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
	while( ( pfile = COM_ParseFile( pfile, token ) ) )
	{
		if( !Q_strcmp( token, "master" ) ) // load addr
		{
			pfile = COM_ParseFile( pfile, token );

			NET_AddMaster( token, true );
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
	{
		Con_Reportf( "Master server list not changed\n" );
		return;
	}

	f = FS_Open( "xashcomm.lst", "w", true );

	if( !f )
	{
		Con_Reportf( S_ERROR  "Couldn't write xashcomm.lst\n" );
		return;
	}

	for( m = ml.list; m; m = m->next )
	{
		if( m->save )
			FS_Printf( f, "master %s\n", m->address );
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

	// keep main master always there
	NET_AddMaster( MASTERSERVER_ADR, false );
	NET_AddMaster( MASTERSERVER_ADR2, false );
	NET_LoadMasters( );
}
