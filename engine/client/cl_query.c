/*
cl_query.c - GoldSrc server query subsystem
Copyright (C) 2026 rootcreep

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#include "common.h"
#include "client.h"
#include "cl_query.h"

#define SPLIT_PART_SIZE	( MAX_PRINT_MSG / 16 )

#define MAX_GOLDSRC_CACHE	256
#define GOLDSRC_CACHE_TTL	300.0f
#define MAX_KV_SIZE			128

typedef struct
{
	netadr_t	adr;
	float		expire;
} gs_cache_entry_t;

static server_query_t	cl_queries[MAX_SERVER_QUERIES];
static gs_cache_entry_t	cl_gs_cache[MAX_GOLDSRC_CACHE];

static const char *CL_QueryTypeName( query_type_t type )
{
	switch( type )
	{
	case SQ_INFO: return "INFO";
	case SQ_PLAYERS: return "PLAYERS";
	case SQ_RULES: return "RULES";
	case SQ_PING: return "PING";
	}
	return "?";
}

void CL_QueryLog( const char *fmt, ... )
{
	va_list va;
	char msg[MAX_VA_STRING];

	if( !cl_log_outofband.value )
		return;

	va_start( va, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, va );
	va_end( va );

	Con_DPrintf( "[Query] %s\n", msg );
}

void CL_QueryMarkGoldSrcAddress( netadr_t adr )
{
	int i, oldest = 0;
	float oldest_expire = cl_gs_cache[0].expire;

	for( i = 0; i < MAX_GOLDSRC_CACHE; i++ )
	{
		if( cl_gs_cache[i].expire != 0.0f && NET_CompareAdr( cl_gs_cache[i].adr, adr ))
		{
			cl_gs_cache[i].expire = host.realtime + GOLDSRC_CACHE_TTL;
			return;
		}
	}

	for( i = 0; i < MAX_GOLDSRC_CACHE; i++ )
	{
		if( cl_gs_cache[i].expire == 0.0f || cl_gs_cache[i].expire <= host.realtime )
		{
			cl_gs_cache[i].adr = adr;
			cl_gs_cache[i].expire = host.realtime + GOLDSRC_CACHE_TTL;
			return;
		}

		if( cl_gs_cache[i].expire < oldest_expire )
		{
			oldest_expire = cl_gs_cache[i].expire;
			oldest = i;
		}
	}

	cl_gs_cache[oldest].adr = adr;
	cl_gs_cache[oldest].expire = host.realtime + GOLDSRC_CACHE_TTL;
}

qboolean CL_IsGoldSrcAddress( netadr_t adr )
{
	int i;

	for( i = 0; i < MAX_GOLDSRC_CACHE; i++ )
	{
		if( cl_gs_cache[i].expire > host.realtime && NET_CompareAdr( cl_gs_cache[i].adr, adr ))
			return true;
	}

	return false;
}

static server_query_t *CL_QueryAlloc( void )
{
	server_query_t *query, *oldest = NULL;
	int i;

	for( i = 0; i < MAX_SERVER_QUERIES; i++ )
	{
		query = &cl_queries[i];
		if( !query->active )
			return query;
		if( query->consumer == QC_BROWSER_INFO )
			oldest = query;
	}

	return oldest;
}

static server_query_t *CL_QueryFindByAddress( netadr_t adr, query_type_t type )
{
	int i;

	for( i = 0; i < MAX_SERVER_QUERIES; i++ )
	{
		server_query_t *query = &cl_queries[i];

		if( !query->active )
			continue;

		if( query->type != type )
			continue;

		if( NET_CompareAdr( query->adr, adr ))
			return query;
	}

	return NULL;
}

static server_query_t *CL_QueryFindChallengeTarget( netadr_t from )
{
	int i;

	for( i = 0; i < MAX_SERVER_QUERIES; i++ )
	{
		server_query_t *query = &cl_queries[i];

		if( !query->active || query->type == SQ_PING )
			continue;

		if( !NET_CompareAdr( query->adr, from ))
			continue;

		if( query->challenge != QUERY_CHALLENGE_NONE )
			continue;

		return query;
	}

	return NULL;
}

static qboolean CL_QueryTypeForResponse( byte response_type, query_type_t *type )
{
	switch( response_type )
	{
	case S2A_GOLDSRC_INFO:
	case S2A_GOLDSRC_LEGACY_INFO:
		*type = SQ_INFO;
		return true;
	case S2A_GOLDSRC_PLAYERS:
		*type = SQ_PLAYERS;
		return true;
	case S2A_GOLDSRC_RULES:
		*type = SQ_RULES;
		return true;
	case A2A_GOLDSRC_ACK_CHAR:
		*type = SQ_PING;
		return true;
	default:
		return false;
	}
}

static server_query_t *CL_QueryFindSplitTarget( netadr_t from, int request_id )
{
	server_query_t *fallback = NULL;
	int i;

	for( i = 0; i < MAX_SERVER_QUERIES; i++ )
	{
		server_query_t *query = &cl_queries[i];

		if( !query->active || !NET_CompareAdr( query->adr, from ))
			continue;

		if( query->split_request_id == request_id )
			return query;

		if( !fallback && query->type == SQ_RULES )
			fallback = query;
	}

	if( fallback )
		return fallback;

	return CL_QueryFindByAddress( from, SQ_PLAYERS );
}

static server_query_t *CL_QueryFindForPacket( netadr_t from, const byte *data, size_t size )
{
	query_type_t type;
	byte response_type;

	if( size < 5 )
		return NULL;

	response_type = data[4];

	if( response_type == S2C_GOLDSRC_CHALLENGE_CHAR )
		return CL_QueryFindChallengeTarget( from );

	if( CL_QueryTypeForResponse( response_type, &type ))
		return CL_QueryFindByAddress( from, type );

	return NULL;
}

static void CL_QueryResetSplit( server_query_t *query )
{
	query->split_request_id = -1;
	query->split_total = 0;
	query->split_received = 0;
	query->split_size = 0;
	memset( query->split_seen, 0, sizeof( query->split_seen ));
	memset( query->split_part_len, 0, sizeof( query->split_part_len ));
}

static void CL_QueryFree( server_query_t *query )
{
	memset( query, 0, sizeof( *query ));
}

static void CL_QueryCompleteNetAPI( server_query_t *query, const char *response )
{
	net_request_t *nr;
	static char infostring[MAX_PRINT_MSG];

	if( query->net_request_index < 0 || query->net_request_index >= MAX_REQUESTS )
	{
		CL_QueryFree( query );
		return;
	}

	nr = &clgame.net_requests[query->net_request_index];
	if( !nr->pfnFunc )
	{
		CL_QueryFree( query );
		return;
	}

	if( response )
	{
		Q_strncpy( infostring, response, sizeof( infostring ));
		nr->resp.response = infostring;
	}
	else
	{
		infostring[0] = 0;
		nr->resp.response = infostring;
	}

	nr->resp.remote_address = query->adr;
	nr->resp.error = NET_SUCCESS;
	nr->resp.ping = host.realtime - nr->timesend;

	if( nr->timeout <= host.realtime )
		SetBits( nr->resp.error, NET_ERROR_TIMEOUT );

	CL_QueryLog( "Protocol=48 %s completed (ping=%.0fms)", CL_QueryTypeName( query->type ), nr->resp.ping * 1000.0 );

	nr->pfnFunc( &nr->resp );

	if( !FBitSet( nr->flags, FNETAPI_MULTIPLE_RESPONSE ))
		memset( nr, 0, sizeof( *nr ));

	CL_QueryFree( query );
}

static void CL_QueryFailNetAPI( server_query_t *query )
{
	net_request_t *nr;

	if( query->net_request_index < 0 || query->net_request_index >= MAX_REQUESTS )
	{
		CL_QueryFree( query );
		return;
	}

	nr = &clgame.net_requests[query->net_request_index];
	if( !nr->pfnFunc )
	{
		CL_QueryFree( query );
		return;
	}

	nr->resp.response = NULL;
	nr->resp.remote_address = query->adr;
	nr->resp.error = NET_ERROR_TIMEOUT;
	nr->resp.ping = host.realtime - nr->timesend;

	CL_QueryLog( "Protocol=48 %s failed (retries=%d)", CL_QueryTypeName( query->type ), query->retries );

	nr->pfnFunc( &nr->resp );
	memset( nr, 0, sizeof( *nr ));
	CL_QueryFree( query );
}

static void CL_QuerySend( server_query_t *query )
{
	byte buf[MAX_PRINT_MSG];
	sizebuf_t msg;
	int challenge = query->challenge;

	MSG_Init( &msg, "GoldSrcQuery", buf, sizeof( buf ));

	if( query->type == SQ_PING )
	{
		Netchan_OutOfBandPrint( NS_CLIENT, query->adr, A2A_GOLDSRC_PING );
		CL_QueryLog( "Protocol=48 PING" );
		return;
	}

	if( query->type == SQ_INFO )
	{
		MSG_WriteString( &msg, A2S_GOLDSRC_INFO );
	}
	else
	{
		MSG_WriteByte( &msg, query->type == SQ_PLAYERS ? A2S_GOLDSRC_PLAYERS : A2S_GOLDSRC_RULES );
	}

	if( challenge == QUERY_CHALLENGE_NONE )
		MSG_WriteLong( &msg, -1 );
	else
		MSG_WriteLong( &msg, challenge );

	Netchan_OutOfBand( NS_CLIENT, query->adr, MSG_GetNumBytesWritten( &msg ), MSG_GetData( &msg ));

	CL_QueryLog( "Protocol=48 %s challenge=%d", CL_QueryTypeName( query->type ), challenge );
}

static int CL_QueryExtractChallenge( const byte *data, size_t size )
{
	if( size < 9 )
		return QUERY_CHALLENGE_NONE;

	if( *(const int32_t *)data != -1 )
		return QUERY_CHALLENGE_NONE;

	if( data[4] != 'A' )
		return QUERY_CHALLENGE_NONE;

	return *(const int32_t *)( data + 5 );
}

static byte CL_QueryExpectedResponse( query_type_t type )
{
	switch( type )
	{
	case SQ_INFO: return S2A_GOLDSRC_INFO;
	case SQ_PLAYERS: return S2A_GOLDSRC_PLAYERS;
	case SQ_RULES: return S2A_GOLDSRC_RULES;
	default: return 0;
	}
}

static void CL_QueryHandleChallenge( server_query_t *query, const byte *data, size_t size )
{
	int challenge = CL_QueryExtractChallenge( data, size );

	if( challenge == QUERY_CHALLENGE_NONE )
	{
		if( query->consumer == QC_NETAPI )
			CL_QueryFailNetAPI( query );
		else CL_QueryFree( query );
		return;
	}

	query->challenge = challenge;
	CL_QueryLog( "received challenge=%d, resending %s", challenge, CL_QueryTypeName( query->type ));
	CL_QuerySend( query );
}

static qboolean CL_QueryParseInfoToInfostring( sizebuf_t *msg, char *out, size_t outsize )
{
	int p, numcl, maxcl, bots, password;
	string host, map, gamedir;

	MSG_SeekToBit( msg, ( sizeof( uint32_t ) + sizeof( uint8_t )) << 3, SEEK_SET );

	p = MSG_ReadByte( msg );
	Q_strncpy( host, MSG_ReadString( msg ), sizeof( host ));
	Q_strncpy( map, MSG_ReadString( msg ), sizeof( map ));
	Q_strncpy( gamedir, MSG_ReadString( msg ), sizeof( gamedir ));
	MSG_ReadString( msg );
	MSG_ReadShort( msg );
	numcl = MSG_ReadByte( msg );
	maxcl = MSG_ReadByte( msg );
	bots = MSG_ReadByte( msg );
	MSG_ReadByte( msg );
	MSG_ReadByte( msg );
	password = MSG_ReadByte( msg );
	MSG_ReadString( msg );

	if( maxcl > MAX_CLIENTS || numcl > MAX_CLIENTS || bots > MAX_CLIENTS || numcl > maxcl )
		return false;

	if( MSG_CheckOverflow( msg ))
		return false;

	out[0] = 0;
	Info_SetValueForKeyf( out, "p", outsize, "%i", p );
	Info_SetValueForKey( out, "gs", "1", outsize );
	Info_SetValueForKey( out, "host", host, outsize );
	Info_SetValueForKey( out, "map", map, outsize );
	Info_SetValueForKey( out, "gamedir", gamedir, outsize );
	Info_SetValueForKeyf( out, "numcl", outsize, "%i", numcl );
	Info_SetValueForKeyf( out, "maxcl", outsize, "%i", maxcl );
	Info_SetValueForKeyf( out, "password", outsize, "%i", password ? 1 : 0 );

	return true;
}

static qboolean CL_QueryParsePlayersToInfostring( sizebuf_t *msg, char *out, size_t outsize )
{
	int i, count;
	char temp[64];

	MSG_SeekToBit( msg, ( sizeof( uint32_t ) + sizeof( uint8_t )) << 3, SEEK_SET );

	count = MSG_ReadByte( msg );
	if( count < 0 || count > MAX_CLIENTS )
		return false;

	out[0] = 0;
	Info_SetValueForKeyf( out, "players", outsize, "%i", count );

	for( i = 0; i < count; i++ )
	{
		int index = MSG_ReadByte( msg );
		const char *name = MSG_ReadString( msg );
		int frags = MSG_ReadLong( msg );
		float time = MSG_ReadFloat( msg );

		Q_snprintf( temp, sizeof( temp ), "p%iname", index );
		Info_SetValueForKey( out, temp, name, outsize );
		Q_snprintf( temp, sizeof( temp ), "p%ifrags", index );
		Info_SetValueForKeyf( out, temp, outsize, "%i", frags );
		Q_snprintf( temp, sizeof( temp ), "p%itime", index );
		Info_SetValueForKeyf( out, temp, outsize, "%f", time );
	}

	if( MSG_CheckOverflow( msg ))
		return false;

	CL_QueryLog( "parsed %d players", count );
	return true;
}

static qboolean CL_QueryParseRulesToInfostring( sizebuf_t *msg, char *out, size_t outsize )
{
	int i, count;
	char key[MAX_KV_SIZE], val[MAX_KV_SIZE];

	MSG_SeekToBit( msg, ( sizeof( uint32_t ) + sizeof( uint8_t )) << 3, SEEK_SET );

	count = MSG_ReadShort( msg );
	if( count < 0 || count > 256 )
		return false;

	out[0] = 0;
	Info_SetValueForKeyf( out, "rules", outsize, "%i", count );

	for( i = 0; i < count; i++ )
	{
		Q_strncpy( key, MSG_ReadString( msg ), sizeof( key ));
		Q_strncpy( val, MSG_ReadString( msg ), sizeof( val ));

		Info_SetValueForKey( out, key, val, outsize );
	}

	if( MSG_CheckOverflow( msg ))
		return false;

	CL_QueryLog( "parsed %d rules", count );
	return true;
}

static void CL_QueryProcessPayload( server_query_t *query, const byte *data, size_t size )
{
	sizebuf_t msg;
	static char infostring[MAX_PRINT_MSG];
	byte response_type;

	if( size < 5 )
	{
		if( query->consumer == QC_NETAPI )
			CL_QueryFailNetAPI( query );
		else CL_QueryFree( query );
		return;
	}

	if( *(const int32_t *)data != -1 )
	{
		if( query->consumer == QC_NETAPI )
			CL_QueryFailNetAPI( query );
		else CL_QueryFree( query );
		return;
	}

	response_type = data[4];

	if( response_type == S2C_GOLDSRC_CHALLENGE_CHAR )
	{
		CL_QueryHandleChallenge( query, data, size );
		return;
	}

	if( query->type == SQ_PING )
	{
		if( response_type == A2A_GOLDSRC_ACK_CHAR )
		{
			if( query->consumer == QC_NETAPI )
				CL_QueryCompleteNetAPI( query, "" );
			else CL_QueryFree( query );
		}
		return;
	}

	MSG_Init( &msg, "QueryResponse", (byte *)data, size );

	if( response_type == S2A_GOLDSRC_LEGACY_INFO && query->type == SQ_INFO )
	{
		CL_QueryLog( "received packet type=m (legacy INFO)" );
		if( query->consumer == QC_BROWSER_INFO )
		{
			CL_ParseGoldSrcStatusMessage( query->adr, &msg, true );
			CL_QueryFree( query );
		}
		else if( query->consumer == QC_NETAPI && CL_QueryParseInfoToInfostring( &msg, infostring, sizeof( infostring )))
			CL_QueryCompleteNetAPI( query, infostring );
		else if( query->consumer == QC_NETAPI )
			CL_QueryFailNetAPI( query );
		return;
	}

	if( response_type != CL_QueryExpectedResponse( query->type ))
	{
		CL_QueryLog( "unexpected packet type=%c for %s", response_type, CL_QueryTypeName( query->type ));
		if( query->consumer == QC_NETAPI )
			CL_QueryFailNetAPI( query );
		else CL_QueryFree( query );
		return;
	}

	CL_QueryLog( "received packet type=%c", response_type );

	switch( query->type )
	{
	case SQ_INFO:
		if( query->consumer == QC_BROWSER_INFO )
		{
			CL_ParseGoldSrcStatusMessage( query->adr, &msg, false );
			CL_QueryFree( query );
		}
		else if( CL_QueryParseInfoToInfostring( &msg, infostring, sizeof( infostring )))
			CL_QueryCompleteNetAPI( query, infostring );
		else CL_QueryFailNetAPI( query );
		break;
	case SQ_PLAYERS:
		if( CL_QueryParsePlayersToInfostring( &msg, infostring, sizeof( infostring )))
			CL_QueryCompleteNetAPI( query, infostring );
		else CL_QueryFailNetAPI( query );
		break;
	case SQ_RULES:
		if( CL_QueryParseRulesToInfostring( &msg, infostring, sizeof( infostring )))
			CL_QueryCompleteNetAPI( query, infostring );
		else CL_QueryFailNetAPI( query );
		break;
	default:
		if( query->consumer == QC_NETAPI )
			CL_QueryFailNetAPI( query );
		else CL_QueryFree( query );
		break;
	}
}

static qboolean CL_QueryReassembleSplit( server_query_t *query, const byte *data, size_t size )
{
	int request_id, packet_id, packet_count, packet_number, payload_offset, payload_size;
	const byte *payload;
	int i;

	if( size < 10 )
		return false;

	if( *(const int32_t *)data != NET_HEADER_SPLITPACKET )
		return false;

	request_id = *(const int32_t *)( data + 4 );
	packet_id = data[8];
	packet_count = packet_id & 0x0F;
	packet_number = ( packet_id >> 4 ) & 0x0F;

	if( packet_count <= 0 || packet_count > 16 || packet_number >= packet_count )
	{
		CL_QueryLog( "malformed split packet (%d/%d)", packet_number + 1, packet_count );
		return false;
	}

	payload_offset = 9;
	payload_size = size - payload_offset;
	payload = data + payload_offset;

	if( payload_size > SPLIT_PART_SIZE )
	{
		CL_QueryLog( "split fragment too large (%d > %d)", payload_size, SPLIT_PART_SIZE );
		return false;
	}

	if( query->split_request_id != request_id )
	{
		CL_QueryResetSplit( query );
		query->split_request_id = request_id;
		query->split_total = packet_count;
	}

	if( query->split_seen[packet_number] )
		return false;

	query->split_seen[packet_number] = true;
	query->split_received++;
	query->split_part_len[packet_number] = payload_size;
	memcpy( query->split_buffer + packet_number * SPLIT_PART_SIZE, payload, payload_size );

	CL_QueryLog( "received split packet %d/%d", packet_number + 1, packet_count );

	if( query->split_received < query->split_total )
		return false;

	query->split_size = 0;
	for( i = 0; i < query->split_total; i++ )
	{
		if( query->split_size + query->split_part_len[i] > sizeof( query->split_buffer ))
		{
			CL_QueryLog( "split reassembly overflow" );
			CL_QueryResetSplit( query );
			return false;
		}

		if( i > 0 )
		{
			memmove( query->split_buffer + query->split_size,
				query->split_buffer + i * SPLIT_PART_SIZE,
				query->split_part_len[i] );
		}

		query->split_size += query->split_part_len[i];
	}

	CL_QueryLog( "reassembled %s response", CL_QueryTypeName( query->type ));
	CL_QueryProcessPayload( query, query->split_buffer, query->split_size );
	CL_QueryResetSplit( query );

	return true;
}

qboolean CL_QueryHandleSplitPacket( netadr_t from, const byte *data, size_t size )
{
	int request_id;
	server_query_t *query;

	if( size < 9 )
		return false;

	request_id = *(const int32_t *)( data + 4 );
	query = CL_QueryFindSplitTarget( from, request_id );

	if( !query )
		return false;

	return CL_QueryReassembleSplit( query, data, size );
}

qboolean CL_QueryHandlePacket( netadr_t from, const byte *data, size_t size )
{
	server_query_t *query;

	if( size < 4 )
		return false;

	if( *(const int32_t *)data == NET_HEADER_SPLITPACKET )
		return CL_QueryHandleSplitPacket( from, data, size );

	if( *(const int32_t *)data != -1 )
		return false;

	query = CL_QueryFindForPacket( from, data, size );
	if( !query )
		return false;

	CL_QueryProcessPayload( query, data, size );
	return true;
}

qboolean CL_QueryStartNetAPI( net_request_t *nr, int request_index )
{
	server_query_t *query;
	query_type_t type;

	switch( nr->resp.type )
	{
	case NETAPI_REQUEST_PING: type = SQ_PING; break;
	case NETAPI_REQUEST_PLAYERS: type = SQ_PLAYERS; break;
	case NETAPI_REQUEST_RULES: type = SQ_RULES; break;
	case NETAPI_REQUEST_DETAILS: type = SQ_INFO; break;
	default: return false;
	}

	query = CL_QueryAlloc();
	if( !query )
		return false;

	query->active = true;
	query->adr = nr->resp.remote_address;
	query->type = type;
	query->consumer = QC_NETAPI;
	query->challenge = QUERY_CHALLENGE_NONE;
	query->net_request_index = request_index;
	CL_QueryResetSplit( query );

	CL_QueryMarkGoldSrcAddress( nr->resp.remote_address );
	CL_QuerySend( query );
	return true;
}

qboolean CL_QueryStartBrowserInfo( netadr_t adr )
{
	server_query_t *query;

	query = CL_QueryFindByAddress( adr, SQ_INFO );
	if( query )
		return true;

	query = CL_QueryAlloc();
	if( !query )
		return false;

	query->active = true;
	query->adr = adr;
	query->type = SQ_INFO;
	query->consumer = QC_BROWSER_INFO;
	query->challenge = QUERY_CHALLENGE_NONE;
	query->net_request_index = -1;
	CL_QueryResetSplit( query );

	CL_QueryMarkGoldSrcAddress( adr );
	CL_QuerySend( query );
	return true;
}

void CL_QueryCancelByContext( int context )
{
	int i;

	for( i = 0; i < MAX_SERVER_QUERIES; i++ )
	{
		server_query_t *query = &cl_queries[i];
		net_request_t *nr;

		if( !query->active || query->consumer != QC_NETAPI )
			continue;

		if( query->net_request_index < 0 || query->net_request_index >= MAX_REQUESTS )
			continue;

		nr = &clgame.net_requests[query->net_request_index];
		if( nr->resp.context != context )
			continue;

		CL_QueryFree( query );
		break;
	}
}

void CL_QueryCancelAll( void )
{
	memset( cl_queries, 0, sizeof( cl_queries ));
}

void CL_QueryFrame( void )
{
	int i;

	for( i = 0; i < MAX_SERVER_QUERIES; i++ )
	{
		server_query_t *query = &cl_queries[i];
		net_request_t *nr;

		if( !query->active )
			continue;

		if( query->consumer != QC_NETAPI )
			continue;

		if( query->net_request_index < 0 || query->net_request_index >= MAX_REQUESTS )
			continue;

		nr = &clgame.net_requests[query->net_request_index];
		if( !nr->pfnFunc )
		{
			CL_QueryFree( query );
			continue;
		}

		if( nr->timeout <= host.realtime )
		{
			if( query->retries < MAX_QUERY_RETRIES )
			{
				query->retries++;
				query->challenge = QUERY_CHALLENGE_NONE;
				CL_QueryLog( "timeout, retry %d/%d", query->retries, MAX_QUERY_RETRIES );
				CL_QuerySend( query );
				nr->timeout = host.realtime + ( nr->timeout - nr->timesend );
				nr->timesend = host.realtime;
			}
			else
			{
				CL_QueryFailNetAPI( query );
			}
		}
	}
}

void CL_QueryInit( void )
{
	memset( cl_queries, 0, sizeof( cl_queries ));
	memset( cl_gs_cache, 0, sizeof( cl_gs_cache ));
}

void CL_QueryShutdown( void )
{
	CL_QueryCancelAll();
}
