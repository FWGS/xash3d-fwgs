/*
sv_query.c - Source-engine like server querying
Copyright (C) 2023 jeefo

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

#define SOURCE_QUERY_INFO 'T'
#define SOURCE_QUERY_DETAILS 'I'

#define SOURCE_QUERY_RULES 'V'
#define SOURCE_QUERY_RULES_RESPONSE 'E'

#define SOURCE_QUERY_PLAYERS 'U'
#define SOURCE_QUERY_PLAYERS_RESPONSE 'D'

#define SOURCE_QUERY_PING 'i'
#define SOURCE_QUERY_ACK 'j'

#define SOURCE_QUERY_CONNECTIONLESS -1

/*
==================
SV_SourceQuery_Details
==================
*/
void SV_SourceQuery_Details( netadr_t from )
{
	sizebuf_t buf;
	char answer[2048] = "";
	char version[128] = "";

	int i, bot_count = 0, client_count = 0;
	int is_private = 0;

	if ( svs.clients )
	{
		for ( i = 0; i < svs.maxclients; i++ )
		{
			if ( svs.clients[i].state >= cs_connected )
			{
				if ( svs.clients[i].edict->v.flags & FL_FAKECLIENT )
					bot_count++;
				else
					client_count++;
			}
		}
	}
	is_private = ( sv_password.string[0] && ( Q_stricmp( sv_password.string, "none" ) || !Q_strlen( sv_password.string ) ) ? 1 : 0 );

    MSG_Init( &buf, "TSourceEngineQuery", answer, sizeof( answer ));

	MSG_WriteLong( &buf, SOURCE_QUERY_CONNECTIONLESS );
	MSG_WriteByte( &buf, SOURCE_QUERY_DETAILS );
	MSG_WriteByte( &buf, PROTOCOL_VERSION );

	MSG_WriteString( &buf, hostname.string );
	MSG_WriteString( &buf, sv.name );
	MSG_WriteString( &buf, GI->gamefolder );
	MSG_WriteString( &buf, svgame.dllFuncs.pfnGetGameDescription( ) );

	MSG_WriteShort( &buf, 0 );
	MSG_WriteByte( &buf, client_count );
	MSG_WriteByte( &buf, svs.maxclients );
	MSG_WriteByte( &buf, bot_count );

	MSG_WriteByte( &buf, Host_IsDedicated( ) ? 'd' : 'l' );
#if defined( _WIN32 )
	MSG_WriteByte( &buf, 'w' );
#elif defined( __APPLE__ )
	MSG_WriteByte( &buf, 'm' );
#else
	MSG_WriteByte( &buf, 'l' );
#endif
	MSG_WriteByte( &buf, is_private );
	MSG_WriteByte( &buf, GI->secure );
	MSG_WriteString( &buf, XASH_VERSION );

	NET_SendPacket( NS_SERVER, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ), from );
}

/*
==================
SV_SourceQuery_Rules
==================
*/
void SV_SourceQuery_Rules( netadr_t from )
{
	sizebuf_t buf;
	char answer[1024 * 8] = "";

	cvar_t *cvar;
	int cvar_count = 0;

	for ( cvar = Cvar_GetList( ); cvar; cvar = cvar->next )
	{
		if ( cvar->flags & FCVAR_SERVER )
			cvar_count++;
	}
	if ( cvar_count <= 0 )
		return;

	MSG_Init( &buf, "TSourceEngineQueryRules", answer, sizeof( answer ) );

	MSG_WriteLong( &buf, SOURCE_QUERY_CONNECTIONLESS );
	MSG_WriteByte( &buf, SOURCE_QUERY_RULES_RESPONSE );
	MSG_WriteShort( &buf, cvar_count );

	for ( cvar = Cvar_GetList( ); cvar; cvar = cvar->next )
	{
		if ( !( cvar->flags & FCVAR_SERVER ) )
			continue;

		MSG_WriteString( &buf, cvar->name );

		if ( cvar->flags & FCVAR_PROTECTED )
			MSG_WriteString( &buf, ( Q_strlen( cvar->string ) > 0 && Q_stricmp( cvar->string, "none" ) ) ? "1" : "0" );
		else
			MSG_WriteString( &buf, cvar->string );
	}
	NET_SendPacket( NS_SERVER, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ), from );
}

/*
==================
SV_SourceQuery_Players
==================
*/
void SV_SourceQuery_Players( netadr_t from )
{
	sizebuf_t buf;
	char answer[1024 * 8] = "";

	int i, client_count = 0;

	if ( svs.clients )
	{
		for ( i = 0; i < svs.maxclients; i++ )
		{
			if ( svs.clients[i].state >= cs_connected )
				client_count++;
		}
	}
	if ( client_count <= 0 )
		return;

	MSG_Init( &buf, "TSourceEngineQueryPlayers", answer, sizeof( answer ) );

	MSG_WriteLong( &buf, SOURCE_QUERY_CONNECTIONLESS );
	MSG_WriteByte( &buf, SOURCE_QUERY_PLAYERS_RESPONSE );
	MSG_WriteByte( &buf, client_count );

	for ( i = 0; i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];

		if ( cl->state < cs_connected )
			continue;

		MSG_WriteByte( &buf, i );
		MSG_WriteString( &buf, cl->name );
		MSG_WriteLong( &buf, cl->edict->v.frags );
		MSG_WriteFloat( &buf, ( cl->edict->v.flags &  FL_FAKECLIENT ) ? -1.0 : ( host.realtime - cl->connecttime ) );
	}
	NET_SendPacket( NS_SERVER, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ), from );
}

/*
==================
SV_SourceQuery_HandleConnnectionlessPacket
==================
*/
qboolean SV_SourceQuery_HandleConnnectionlessPacket( const char *c, netadr_t from )
{
	int request = c[0];

	switch ( request )
	{
	case SOURCE_QUERY_INFO:
		SV_SourceQuery_Details( from );
		return true;

	case SOURCE_QUERY_PING:
		Netchan_OutOfBandPrint( NS_SERVER, from, "%c00000000000000", SOURCE_QUERY_ACK );
		return true;

	case SOURCE_QUERY_ACK:
		return true;

	case SOURCE_QUERY_RULES:
		SV_SourceQuery_Rules( from );
		return true;

	case SOURCE_QUERY_PLAYERS:
		SV_SourceQuery_Players( from );
		return true;

	default:
		return false;
	}
	return false;
}