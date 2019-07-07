/*
sv_filter.c - server ID/IP filter
Copyright (C) 2017 a1batross

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


typedef struct ipfilter_s
{
	float time;
	float endTime; // -1 for permanent ban
	struct ipfilter_s *next;
	uint mask;
	uint ip;
} ipfilter_t;

static ipfilter_t *ipfilter = NULL;


// TODO: Is IP filter really needed?
// TODO: Make it IPv6 compatible, for future expansion

typedef struct cidfilter_s
{
	float endTime;
	struct cidfilter_s *next;
	string id;
} cidfilter_t;

static cidfilter_t *cidfilter = NULL;

static void SV_RemoveID( const char *id )
{
	cidfilter_t *filter, *prevfilter = NULL;

	for( filter = cidfilter; filter; filter = filter->next )
	{
		if( Q_strcmp( filter->id, id ) )
		{
			prevfilter = filter;
			continue;
		}

		if( filter == cidfilter )
		{
			cidfilter = cidfilter->next;
			Mem_Free( filter );
			return;
		}

		if( prevfilter )
		prevfilter->next = filter->next;
		Mem_Free( filter );
		return;
	}
}

static void SV_RemoveIP( uint ip, uint mask )
{
	ipfilter_t *filter, *prevfilter = NULL;

	for( filter = ipfilter; filter; filter = filter->next )
	{
		if( filter->ip != ip || mask != filter->mask )
		{
			prevfilter = filter;
			continue;
		}

		if( filter == ipfilter )
		{
			ipfilter = ipfilter->next;
			Mem_Free( filter );
			return;
		}

		if( prevfilter )
			prevfilter->next = filter->next;
		Mem_Free( filter );
		return;
	}
}

qboolean SV_CheckID( const char *id )
{
	qboolean ret = false;
	cidfilter_t *filter;

	for( filter = cidfilter; filter; filter = filter->next )
	{
		int len1 = Q_strlen( id ), len2 = Q_strlen( filter->id );
		int len = min( len1, len2 );

		while( filter->endTime && host.realtime > filter->endTime )
		{
			char *fid = filter->id;
			filter = filter->next;
			SV_RemoveID( fid );
			if( !filter )
				return false;
		}

		if( !Q_strncmp( id, filter->id, len ) )
		{
			ret = true;
			break;
		}
	}

	return ret;
}

qboolean SV_CheckIP( netadr_t *addr )
{
	uint ip = addr->ip[0] << 24 | addr->ip[1] << 16 | addr->ip[2] << 8 | addr->ip[3];
	qboolean ret = false;
	ipfilter_t *filter;

	for( filter = ipfilter; filter; filter = filter->next )
	{
		while( filter->endTime && host.realtime > filter->endTime )
		{
			uint rip = filter->ip;
			uint rmask = filter->mask;
			SV_RemoveIP( rip, rmask );
			filter = filter->next;
			if( !filter )
				return false;
		}

		if( (ip & filter->mask) == (filter->ip & filter->mask) )
		{
			ret = true;
			break;
		}
	}

	return ret;
}

static void SV_BanID_f( void )
{
	float time = Q_atof( Cmd_Argv( 1 ) );
	const char *id = Cmd_Argv( 2 );
	sv_client_t *cl = NULL;
	cidfilter_t *filter;

	if( time )
		time = host.realtime + time * 60.0f;

	if( !id[0] )
	{
		Con_Reportf( "Usage: banid <minutes> <#userid or unique id>\n0 minutes for permanent ban\n" );
		return;
	}

	if( !Q_strnicmp( id, "STEAM_", 6 ) || !Q_strnicmp( id, "VALVE_", 6 ) )
		id += 6;
	if( !Q_strnicmp( id, "XASH_", 5 ) )
		id += 5;

	if( svs.clients )
	{
		if( id[0] == '#' )
			cl = SV_ClientById( Q_atoi( id + 1 ) );

		if( !cl )
		{
			int i;
			sv_client_t *cl1;
			int len = Q_strlen( id );

			for( i = 0, cl1 = svs.clients; i < sv_maxclients->value; i++, cl1++ )
			{
				if( !Q_strncmp( id, Info_ValueForKey( cl1->useragent, "i" ), len ) )
				{
					cl = cl1;
					break;
				}
			}
		}

		if( !cl )
		{
			Con_DPrintf( S_WARN "banid: no such player\n" );
		}
		else
			id = Info_ValueForKey( cl->useragent, "i" );

		if( !id[0] )
		{
			Con_DPrintf( S_ERROR "Could not ban, not implemented yet\n" );
			return;
		}
	}

	if( !id[0] || id[0] == '#' )
	{
		Con_DPrintf( S_ERROR "banid: bad id\n" );
		return;
	}

	SV_RemoveID( id );

	filter = Mem_Malloc( host.mempool, sizeof( cidfilter_t ) );
	filter->endTime = time;
	filter->next = cidfilter;
	Q_strncpy( filter->id, id, sizeof( filter->id ) );
	cidfilter = filter;

	if( cl && !Q_stricmp( Cmd_Argv( Cmd_Argc() - 1 ), "kick" ) )
		Cbuf_AddText( va( "kick #%d \"Kicked and banned\"\n", cl->userid ) );
}

static void SV_ListID_f( void )
{
	cidfilter_t *filter;

	Con_Reportf( "id ban list\n" );
	Con_Reportf( "-----------\n" );

	for( filter = cidfilter; filter; filter = filter->next )
	{
		if( filter->endTime && host.realtime > filter->endTime )
			continue; // no negative time

		if( filter->endTime )
			Con_Reportf( "%s expries in %f minutes\n", filter->id, ( filter->endTime - host.realtime ) / 60.0f );
		else
			Con_Reportf( "%s permanent\n", filter->id );
	}
}

static void SV_RemoveID_f( void )
{
	const char *id = Cmd_Argv( 1 );

	if( id[0] == '#' && svs.clients )
	{
		int num = Q_atoi( id + 1 );

		if( num >= sv_maxclients->value || num < 0 )
			return;

		id = Info_ValueForKey( svs.clients[num].useragent, "i" );
	}

	if( !id[0] )
	{
		Con_Reportf("Usage: removeid <#slotnumber or uniqueid>\n");
		return;
	}

	SV_RemoveID( id );
}

static void SV_WriteID_f( void )
{
	file_t *f = FS_Open( Cvar_VariableString( "bannedcfgfile" ), "w", false );
	cidfilter_t *filter;

	if( !f )
	{
		Con_DPrintf( S_ERROR "Could not write %s\n", Cvar_VariableString( "bannedcfgfile" ) );
		return;
	}

	FS_Printf( f, "//=======================================================================\n" );
	FS_Printf( f, "//\t\tCopyright Flying With Gauss Team %s ©\n", Q_timestamp( TIME_YEAR_ONLY ));
	FS_Printf( f, "//\t\t    %s - archive of id blacklist\n", Cvar_VariableString( "bannedcfgfile" ) );
	FS_Printf( f, "//=======================================================================\n" );

	for( filter = cidfilter; filter; filter = filter->next )
		if( !filter->endTime ) // only permanent
			FS_Printf( f, "banid 0 %s\n", filter->id );

	FS_Close( f );
}

static qboolean StringToIP( const char *str, const char *maskstr, uint *outip, uint *outmask ) 
{
	byte ip[4] = {0};
	byte mask[4] = {0};
	int i = 0;

	if( *str > '9' || *str < '0' )
		return false;

	do
	{
		while( *str <= '9' && *str >= '0' )
		{
			ip[i] *=10;
			ip[i] += *str - '0';
			str++;
		}
		mask[i] = 255;
		i++;
		if( *str != '.' ) break;
		str++;
	} while( i < 4 );

	i = 0;

	if( !maskstr ||  *maskstr > '9' || *maskstr < '0' )
		goto end;

	do
	{
		byte mask1 = 0;
		while( *maskstr <= '9' && *maskstr >= '0' )
		{
			mask1 *=10;
			mask1 += *maskstr - '0';
			maskstr++;
		}
		mask[i] &= mask1;
		i++;
		if( *maskstr != '.' ) break;
		maskstr++;
	} while( i < 4 );

end:
	*outip = ip[0] << 24 | ip[1] << 16 | ip[2] << 8 | ip[3];
	if( outmask )
		*outmask = mask[0] << 24 | mask[1] << 16 | mask[2] << 8 | mask[3];

	return true;
}

#define IPARGS(ip) (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF
static void SV_AddIP_f( void )
{
	float time = Q_atof( Cmd_Argv( 1 ) );
	const char *ipstr = Cmd_Argv( 2 );
	const char *maskstr = Cmd_Argv( 3 );
	uint ip, mask;
	ipfilter_t *filter;

	if( time )
		time = host.realtime + time * 60.0f;

	if( !StringToIP( ipstr, maskstr, &ip, &mask ) )
	{
		Con_Reportf( "Usage: addip <minutes> <ip> [mask]\n0 minutes for permanent ban\n");
		return;
	}

	SV_RemoveIP( ip, mask );

	filter = Mem_Malloc( host.mempool, sizeof( ipfilter_t ) );
	filter->endTime = time;
	filter->ip = ip;
	filter->mask = mask;
	filter->next = ipfilter;

	ipfilter = filter;
}

static void SV_ListIP_f( void )
{
	ipfilter_t *filter;

	Con_Reportf( "ip ban list\n" );
	Con_Reportf( "-----------\n" );

	for( filter = ipfilter; filter; filter = filter->next )
	{
		if( filter->endTime && host.realtime > filter->endTime )
			continue; // no negative time

		if( filter->endTime )
			Con_Reportf( "%d.%d.%d.%d %d.%d.%d.%d expries in %f minutes\n", IPARGS( filter->ip ), IPARGS( filter->mask ), ( filter->endTime - host.realtime ) / 60.0f );
		else
			Con_Reportf( "%d.%d.%d.%d %d.%d.%d.%d permanent\n", IPARGS( filter->ip ), IPARGS( filter->mask ) );
	}
}

static void SV_RemoveIP_f( void )
{
	uint ip, mask;

	if( !StringToIP( Cmd_Argv(1), Cmd_Argv(2), &ip, &mask ) )
	{
		Con_Reportf( "Usage: removeip <ip> [mask]\n" );
		return;
	}

	SV_RemoveIP( ip, mask );
}

static void SV_WriteIP_f( void )
{
	file_t *f = FS_Open( Cvar_VariableString( "listipcfgfile" ), "w", false );
	ipfilter_t *filter;

	if( !f )
	{
		Con_DPrintf( S_ERROR "Could not write %s\n", Cvar_VariableString( "listipcfgfile" ) );
		return;
	}

	FS_Printf( f, "//=======================================================================\n" );
	FS_Printf( f, "//\t\tCopyright Flying With Gauss Team %s ©\n", Q_timestamp( TIME_YEAR_ONLY ));
	FS_Printf( f, "//\t\t    %s - archive of IP blacklist\n", Cvar_VariableString( "listipcfgfile" ) );
	FS_Printf( f, "//=======================================================================\n" );

	for( filter = ipfilter; filter; filter = filter->next )
		if( !filter->endTime ) // only permanent
			FS_Printf( f, "addip 0 %d.%d.%d.%d %d.%d.%d.%d\n", IPARGS(filter->ip), IPARGS(filter->mask) );

	FS_Close( f );
}

void SV_InitFilter( void )
{
	Cmd_AddCommand( "banid", SV_BanID_f, "ban player by ID" );
	Cmd_AddCommand( "listid", SV_ListID_f, "list banned players" );
	Cmd_AddCommand( "removeid", SV_RemoveID_f, "remove player from banned list" );
	Cmd_AddCommand( "writeid", SV_WriteID_f, "write banned.cfg" );
	Cmd_AddCommand( "addip", SV_AddIP_f, "add entry to IP filter" );
	Cmd_AddCommand( "listip", SV_ListIP_f, "list current IP filter" );
	Cmd_AddCommand( "removeip", SV_RemoveIP_f, "remove IP filter" );
	Cmd_AddCommand( "writeip", SV_WriteIP_f, "write listip.cfg" );
}

void SV_ShutdownFilter( void )
{
	ipfilter_t *ipList, *ipNext;
	cidfilter_t *cidList, *cidNext;

	// should be called manually because banned.cfg is not executed by engine
	//SV_WriteIP_f();
	//SV_WriteID_f();

	for( ipList = ipfilter; ipList; ipList = ipNext )
	{
		ipNext = ipList->next;
		Mem_Free( ipList );
	}

	for( cidList = cidfilter; cidList; cidList = cidNext )
	{
		cidNext = cidList->next;
		Mem_Free( cidList );
	}

	cidfilter = NULL;
	ipfilter = NULL;
}
