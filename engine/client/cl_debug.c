/*
cl_debug.c - server message debugging
Copyright (C) 2018 Uncle Mike

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

#define MSG_COUNT		32		// last 32 messages parsed
#define MSG_MASK		(MSG_COUNT - 1)

typedef struct
{
	int	command;
	int	starting_offset;
	int	frame_number;
} oldcmd_t;

typedef struct
{
	oldcmd_t	oldcmd[MSG_COUNT];
	int	currentcmd;
	qboolean	parsing;
} msg_debug_t;

static msg_debug_t	cls_message_debug;

const char *CL_MsgInfo( int cmd )
{
	static string	sz;

	Q_strcpy( sz, "???" );

	if( cmd >= 0 && cmd <= svc_lastmsg )
	{
		// get engine message name
		Q_strncpy( sz, svc_strings[cmd], sizeof( sz ));
	}
	else if( cmd > svc_lastmsg && cmd <= ( svc_lastmsg + MAX_USER_MESSAGES ))
	{
		int	i;

		for( i = 0; i < MAX_USER_MESSAGES; i++ )
		{
			if( clgame.msg[i].number == cmd )
			{
				Q_strncpy( sz, clgame.msg[i].name, sizeof( sz ));
				break;
			}
		}
	}
	return sz;
}

/*
=====================
CL_Parse_Debug

enable message debugging
=====================
*/
void CL_Parse_Debug( qboolean enable )
{
	cls_message_debug.parsing = enable;
}

/*
=====================
CL_Parse_RecordCommand

record new message params into debug buffer
=====================
*/
void CL_Parse_RecordCommand( int cmd, int startoffset )
{
	int	slot;

	if( cmd == svc_nop ) return;

	slot = ( cls_message_debug.currentcmd++ & MSG_MASK );
	cls_message_debug.oldcmd[slot].command = cmd;
	cls_message_debug.oldcmd[slot].starting_offset = startoffset;
	cls_message_debug.oldcmd[slot].frame_number = host.framecount;
}

/*
=====================
CL_ResetFrame
=====================
*/
void CL_ResetFrame( frame_t *frame )
{
	memset( &frame->graphdata, 0, sizeof( netbandwidthgraph_t ));
	frame->receivedtime = host.realtime;
	frame->valid = true;
	frame->choked = false;
	frame->latency = 0.0;
	frame->time = cl.mtime[0];
}

/*
=====================
CL_WriteErrorMessage

write net_message into buffer.dat for debugging
=====================
*/
static void CL_WriteErrorMessage( int current_count, sizebuf_t *msg )
{
	const char	*buffer_file = "buffer.dat";
	file_t		*fp;

	fp = FS_Open( buffer_file, "wb", false );
	if( !fp ) return;

	FS_Write( fp, &cls.starting_count, sizeof( int ));
	FS_Write( fp, &current_count, sizeof( int ));
	FS_Write( fp, MSG_GetData( msg ), MSG_GetMaxBytes( msg ));
	FS_Close( fp );

	Con_Printf( "Wrote erroneous message to %s\n", buffer_file );
}

/*
=====================
CL_WriteMessageHistory

list last 32 messages for debugging net troubleshooting
=====================
*/
void CL_WriteMessageHistory( void )
{
	oldcmd_t	*old, *failcommand;
	sizebuf_t	*msg = &net_message;
	int	i, thecmd;

	if( !cls.initialized || cls.state == ca_disconnected )
		return;

	if( !cls_message_debug.parsing )
		return;

	Con_Printf( "Last %i messages parsed.\n", MSG_COUNT );

	// finish here
	thecmd = cls_message_debug.currentcmd - 1;
	thecmd -= ( MSG_COUNT - 1 );	// back up to here

	for( i = 0; i < MSG_COUNT - 1; i++ )
	{
		thecmd &= MSG_MASK;
		old = &cls_message_debug.oldcmd[thecmd];
		Con_Printf( "%i %04i %s\n", old->frame_number, old->starting_offset, CL_MsgInfo( old->command ));
		thecmd++;
	}

	failcommand = &cls_message_debug.oldcmd[thecmd];
	Con_Printf( "BAD:  %3i:%s\n", MSG_GetNumBytesRead( msg ) - 1, CL_MsgInfo( failcommand->command ));
	if( host_developer.value >= DEV_EXTENDED )
		CL_WriteErrorMessage( MSG_GetNumBytesRead( msg ) - 1, msg );
	cls_message_debug.parsing = false;
}
