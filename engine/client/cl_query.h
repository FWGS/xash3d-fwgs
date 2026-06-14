/*
cl_query.h - GoldSrc server query subsystem
Copyright (C) 2026 rootcreep

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#ifndef CL_QUERY_H
#define CL_QUERY_H

typedef enum
{
	SQ_INFO,
	SQ_PLAYERS,
	SQ_RULES,
	SQ_PING,
} query_type_t;

typedef enum
{
	QC_NETAPI,
	QC_BROWSER_INFO,
} query_consumer_t;

#define MAX_SERVER_QUERIES		128
#define MAX_QUERY_RETRIES		3
#define QUERY_CHALLENGE_NONE	-1

typedef struct server_query_s
{
	qboolean		active;
	netadr_t		adr;
	query_type_t		type;
	query_consumer_t	consumer;

	int		challenge;
	int		retries;

	int		net_request_index;

	int		split_request_id;
	int		split_total;
	int		split_received;
	qboolean		split_seen[16];
	size_t		split_part_len[16];
	byte		split_buffer[MAX_PRINT_MSG];
	size_t		split_size;
} server_query_t;

void CL_QueryInit( void );
void CL_QueryShutdown( void );
void CL_QueryFrame( void );

qboolean CL_QueryStartNetAPI( net_request_t *nr, int request_index );
qboolean CL_QueryStartBrowserInfo( netadr_t adr );

qboolean CL_QueryHandlePacket( netadr_t from, const byte *data, size_t size );
qboolean CL_QueryHandleSplitPacket( netadr_t from, const byte *data, size_t size );

void CL_QueryCancelByContext( int context );
void CL_QueryCancelAll( void );

void CL_QueryMarkGoldSrcAddress( netadr_t adr );
qboolean CL_IsGoldSrcAddress( netadr_t adr );
void CL_QueryLog( const char *fmt, ... ) FORMAT_CHECK( 1 );

#endif // CL_QUERY_H
