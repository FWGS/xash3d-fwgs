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

/*
==================
SV_SourceQuery_Details
==================
*/
static void SV_SourceQuery_Details( netadr_t from )
{
	sizebuf_t buf;
	char answer[2048];
	int bot_count, client_count;

	SV_GetPlayerCount( &client_count, &bot_count );
	client_count += bot_count; // bots are counted as players in this reply

	MSG_Init( &buf, "TSourceEngineQuery", answer, sizeof( answer ));

	MSG_WriteDword( &buf, 0xFFFFFFFFU );
	MSG_WriteByte( &buf, S2A_GOLDSRC_INFO );
	MSG_WriteByte( &buf, PROTOCOL_VERSION );

	MSG_WriteString( &buf, hostname.string );
	MSG_WriteString( &buf, sv.name );
	MSG_WriteString( &buf, GI->gamefolder );
	MSG_WriteString( &buf, svgame.dllFuncs.pfnGetGameDescription( ));

	MSG_WriteShort( &buf, 0 );
	MSG_WriteByte( &buf, client_count );
	MSG_WriteByte( &buf, svs.maxclients );
	MSG_WriteByte( &buf, bot_count );

	MSG_WriteByte( &buf, Host_IsDedicated( ) ? 'd' : 'l' );
#if XASH_WIN32
	MSG_WriteByte( &buf, 'w' );
#elif XASH_APPLE
	MSG_WriteByte( &buf, 'm' );
#else
	MSG_WriteByte( &buf, 'l' );
#endif

	if( SV_HavePassword( ))
		MSG_WriteByte( &buf, 1 );
	else MSG_WriteByte( &buf, 0 );
	MSG_WriteByte( &buf, GI->secure );
	MSG_WriteString( &buf, XASH_VERSION );

	NET_SendPacket( NS_SERVER, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ), from );
}

/*
==================
SV_SourceQuery_Rules
==================
*/
static void SV_SourceQuery_Rules( netadr_t from )
{
	const cvar_t *cvar;
	sizebuf_t buf;
	char answer[MAX_PRINT_MSG - 4];
	int pos;
	uint cvar_count = 0;

	MSG_Init( &buf, "TSourceEngineQueryRules", answer, sizeof( answer ));

	MSG_WriteDword( &buf, 0xFFFFFFFFU );
	MSG_WriteByte( &buf, S2A_GOLDSRC_RULES );

	pos = MSG_GetNumBitsWritten( &buf );
	MSG_WriteShort( &buf, 0 );

	for( cvar = Cvar_GetList( ); cvar; cvar = cvar->next )
	{
		if( !FBitSet( cvar->flags, FCVAR_SERVER ))
			continue;

		MSG_WriteString( &buf, cvar->name );

		if( FBitSet( cvar->flags, FCVAR_PROTECTED ))
		{
			if( COM_CheckStringEmpty( cvar->string ) && Q_stricmp( cvar->string, "none" ))
				MSG_WriteString( &buf, "1" );
			else MSG_WriteString( &buf, "0" );
		}
		else MSG_WriteString( &buf, cvar->string );

		cvar_count++;
	}

	if( cvar_count != 0 )
	{
		int total = MSG_GetNumBytesWritten( &buf );

		MSG_SeekToBit( &buf, pos, SEEK_SET );
		MSG_WriteShort( &buf, cvar_count );

		NET_SendPacket( NS_SERVER, total, MSG_GetData( &buf ), from );
	}
}

/*
==================
SV_SourceQuery_Players
==================
*/
static void SV_SourceQuery_Players( netadr_t from )
{
	sizebuf_t buf;
	char answer[MAX_PRINT_MSG - 4];
	int i, count = 0;
	int pos;

	// respect players privacy
	if( !sv_expose_player_list.value || SV_HavePassword( ))
		return;

	MSG_Init( &buf, "TSourceEngineQueryPlayers", answer, sizeof( answer ));

	MSG_WriteDword( &buf, 0xFFFFFFFFU );
	MSG_WriteByte( &buf, S2A_GOLDSRC_PLAYERS );

	pos = MSG_GetNumBitsWritten( &buf );
	MSG_WriteByte( &buf, 0 );

	for( i = 0; i < svs.maxclients; i++ )
	{
		const sv_client_t *cl = &svs.clients[i];

		if( cl->state < cs_connected )
			continue;

		MSG_WriteByte( &buf, count );
		MSG_WriteString( &buf, cl->name );
		MSG_WriteLong( &buf, cl->edict->v.frags );
		if( FBitSet( cl->flags, FCL_FAKECLIENT ))
			MSG_WriteFloat( &buf, -1.0f );
		else MSG_WriteFloat( &buf, host.realtime - cl->connection_started );

		count++;
	}

	if( count != 0 )
	{
		int total = MSG_GetNumBytesWritten( &buf );

		MSG_SeekToBit( &buf, pos, SEEK_SET );
		MSG_WriteByte( &buf, count );

		NET_SendPacket( NS_SERVER, total, MSG_GetData( &buf ), from );
	}
}

/*
==================
SV_SourceQuery_HandleConnnectionlessPacket
==================
*/
void SV_SourceQuery_HandleConnnectionlessPacket( const char *c, netadr_t from )
{
	if( !Q_strcmp( c, A2S_GOLDSRC_INFO ))
	{
		SV_SourceQuery_Details( from );
	}
	else switch( c[0] )
	{
	case A2S_GOLDSRC_RULES:
		SV_SourceQuery_Rules( from );
		break;
	case A2S_GOLDSRC_PLAYERS:
		SV_SourceQuery_Players( from );
		break;
	}
}
