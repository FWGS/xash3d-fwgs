/*
cl_demo.c - demo record & playback
Copyright (C) 2007 Uncle Mike

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

#define dem_unknown		0	// unknown command
#define dem_norewind	1	// startup message
#define dem_read		2	// it's a normal network packet
#define dem_jumptime	3	// move the demostart time value forward by this amount
#define dem_userdata	4	// userdata from the client.dll
#define dem_usercmd		5	// read usercmd_t
#define dem_stop		6	// end of time
#define dem_lastcmd		dem_stop

#define DEMO_STARTUP	0	// this lump contains startup info needed to spawn into the server
#define DEMO_NORMAL		1	// this lump contains playback info of messages, etc., needed during playback.

// Demo flags
#define FDEMO_TITLE		0x01	// Show title
#define FDEMO_PLAY		0x04	// Playing cd track
#define FDEMO_FADE_IN_SLOW	0x08	// Fade in (slow)
#define FDEMO_FADE_IN_FAST	0x10	// Fade in (fast)
#define FDEMO_FADE_OUT_SLOW	0x20	// Fade out (slow)
#define FDEMO_FADE_OUT_FAST	0x40	// Fade out (fast)

#define IDEMOHEADER		(('M'<<24)+('E'<<16)+('D'<<8)+'I') // little-endian "IDEM"
#define DEMO_PROTOCOL	3

#define PROTOCOL_GOLDSRC_VERSION_DEMO (PROTOCOL_GOLDSRC_VERSION | (BIT( 7 ))) // should be 48, only to differentiate it from PROTOCOL_LEGACY_VERSION

const char *demo_cmd[dem_lastcmd+1] =
{
	"dem_unknown",
	"dem_norewind",
	"dem_read",
	"dem_jumptime",
	"dem_userdata",
	"dem_usercmd",
	"dem_stop",
};

#pragma pack( push, 1 )
typedef struct
{
	int		id;		// should be IDEM
	int		dem_protocol;	// should be DEMO_PROTOCOL
	int		net_protocol;	// should be PROTOCOL_VERSION
	double		host_fps;		// fps for demo playing
	char		mapname[64];	// name of map
	char		comment[64];	// comment for demo
	char		gamedir[64];	// name of game directory (FS_Gamedir())
	int		directory_offset;	// offset of Entry Directory.
} demoheader_t;
#pragma pack( pop )

typedef struct
{
	int		entrytype;	// DEMO_STARTUP or DEMO_NORMAL
	float		playback_time;	// time of track
	int		playback_frames;	// # of frames in track
	int		offset;		// file offset of track data
	int		length;		// length of track
	int		flags;		// FX-flags
	char		description[64];	// entry description
} demoentry_t;

typedef struct
{
	demoentry_t	*entries;		// track entry info
	int32_t		numentries;	// number of tracks
} demodirectory_t;

// add angles
typedef struct
{
	float		starttime;
	vec3_t		viewangles;
} demoangle_t;

// private demo states
struct
{
	demoheader_t	header;
	demoentry_t	*entry;
	demodirectory_t	directory;
	int		framecount;
	float		starttime;
	float		realstarttime;
	float		timestamp;
	float		lasttime;
	int		entryIndex;

	// interpolation stuff
	demoangle_t	cmds[ANGLE_BACKUP];
	int		angle_position;
} demo;

static qboolean CL_NextDemo( void );

static int CL_GetDemoNetProtocol( connprotocol_t proto )
{
	switch( proto )
	{
	case PROTO_CURRENT:
		return PROTOCOL_VERSION;
	case PROTO_LEGACY:
		return PROTOCOL_LEGACY_VERSION;
	case PROTO_QUAKE:
		return PROTOCOL_VERSION_QUAKE;
	case PROTO_GOLDSRC:
		return PROTOCOL_GOLDSRC_VERSION_DEMO;
	}

	return PROTOCOL_VERSION;
}

static connprotocol_t CL_GetProtocolFromDemo( int net_protocol )
{
	switch( net_protocol )
	{
	case PROTOCOL_VERSION:
		return PROTO_CURRENT;
	case PROTOCOL_LEGACY_VERSION:
		return PROTO_LEGACY;
	case PROTOCOL_VERSION_QUAKE:
		return PROTO_QUAKE;
	case PROTOCOL_GOLDSRC_VERSION_DEMO:
		return PROTO_GOLDSRC;
	}

	return PROTO_CURRENT;
}

/*
====================
CL_StartupDemoHeader

spooling demo header in case
we record a demo on this level
====================
*/
void CL_StartupDemoHeader( void )
{
	CL_CloseDemoHeader();

	cls.demoheader = FS_Open( "demoheader.tmp", "w+bm", true );

	if( !cls.demoheader )
	{
		Con_DPrintf( S_ERROR "couldn't open temporary header file.\n" );
		return;
	}

	Con_Printf( "Spooling demo header.\n" );
}

/*
====================
CL_CloseDemoHeader

close demoheader file on engine shutdown
====================
*/
void CL_CloseDemoHeader( void )
{
	if( !cls.demoheader )
		return;

	FS_Close( cls.demoheader );
}

/*
====================
CL_GetDemoRecordClock

write time while demo is recording
====================
*/
static float CL_GetDemoRecordClock( void )
{
	return cl.mtime[0];
}

/*
====================
CL_GetDemoPlaybackClock

overwrite host.realtime
====================
*/
static float CL_GetDemoPlaybackClock( void )
{
	return host.realtime + host.frametime;
}

/*
====================
CL_GetDemoFramerate

overwrite host.frametime
====================
*/
double CL_GetDemoFramerate( void )
{
	if( cls.timedemo )
		return 0.0;
	return bound( MIN_FPS, demo.header.host_fps, MAX_FPS_HARD );
}

/*
=================
CL_DemoAborted
=================
*/
static void CL_DemoAborted( void )
{
	if( cls.demofile )
		FS_Close( cls.demofile );
	cls.demoplayback = false;
	cls.changedemo = false;
	cls.timedemo = false;
	demo.framecount = 0;
	cls.demofile = NULL;
	cls.demonum = -1;

	Cvar_DirectSet( &v_dark, "0" );
}

/*
====================
CL_WriteDemoCmdHeader

Writes the demo command header and time-delta
====================
*/
static void CL_WriteDemoCmdHeader( byte cmd, file_t *file )
{
	float	dt;

	Assert( cmd >= 1 && cmd <= dem_lastcmd );
	if( !file ) return;

	// command
	FS_Write( file, &cmd, sizeof( byte ));

	// time offset
	dt = (float)(CL_GetDemoRecordClock() - demo.starttime);
	FS_Write( file, &dt, sizeof( float ));
}

/*
====================
CL_WriteDemoJumpTime

Update level time on a next level
====================
*/
void CL_WriteDemoJumpTime( void )
{
	if( cls.demowaiting || !cls.demofile )
		return;

	demo.starttime = CL_GetDemoRecordClock(); // setup the demo starttime

	// demo playback should read this as an incoming message.
	// write the client's realtime value out so we can synchronize the reads.
	CL_WriteDemoCmdHeader( dem_jumptime, cls.demofile );
}

/*
====================
CL_WriteDemoUserCmd

Writes the current user cmd
====================
*/
void CL_WriteDemoUserCmd( int cmdnumber )
{
	sizebuf_t	buf;
	word	bytes;
	byte	data[1024];

	if( !cls.demorecording || !cls.demofile )
		return;

	CL_WriteDemoCmdHeader( dem_usercmd, cls.demofile );

	FS_Write( cls.demofile, &cls.netchan.outgoing_sequence, sizeof( int ));
	FS_Write( cls.demofile, &cmdnumber, sizeof( int ));

	// write usercmd_t
	MSG_Init( &buf, "UserCmd", data, sizeof( data ));
	CL_WriteUsercmd( PROTO_CURRENT, &buf, -1, cmdnumber ); // always no delta, always in current protocol

	bytes = MSG_GetNumBytesWritten( &buf );

	FS_Write( cls.demofile, &bytes, sizeof( word ));
	FS_Write( cls.demofile, data, bytes );
}

/*
====================
CL_WriteDemoSequence

Save state of cls.netchan sequences
so that we can play the demo correctly.
====================
*/
static void CL_WriteDemoSequence( file_t *file )
{
	Assert( file != NULL );

	FS_Write( file, &cls.netchan.incoming_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.incoming_acknowledged, sizeof( int ));
	FS_Write( file, &cls.netchan.incoming_reliable_acknowledged, sizeof( int ));
	FS_Write( file, &cls.netchan.incoming_reliable_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.outgoing_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.reliable_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.last_reliable_sequence, sizeof( int ));
}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length
====================
*/
void CL_WriteDemoMessage( qboolean startup, int start, sizebuf_t *msg )
{
	file_t	*file = startup ? cls.demoheader : cls.demofile;
	int	swlen;
	byte	c;

	if( !file ) return;

	swlen = MSG_GetNumBytesWritten( msg ) - start;
	if( swlen <= 0 ) return;

	if( !startup ) demo.framecount++;

	// demo playback should read this as an incoming message.
	c = (cls.state != ca_active) ? dem_norewind : dem_read;

	CL_WriteDemoCmdHeader( c, file );
	CL_WriteDemoSequence( file );

	// write the length out.
	FS_Write( file, &swlen, sizeof( int ));

	// output the buffer. Skip the network packet stuff.
	FS_Write( file, MSG_GetData( msg ) + start, swlen );
}

/*
====================
CL_WriteDemoUserMessage

Dumps the user message (demoaction)
====================
*/
void GAME_EXPORT CL_WriteDemoUserMessage( int size, byte *buffer )
{
	if( !cls.demorecording || cls.demowaiting )
		return;

	if( !cls.demofile || !buffer || size <= 0 )
		return;

	CL_WriteDemoCmdHeader( dem_userdata, cls.demofile );

	// write the length out.
	FS_Write( cls.demofile, &size, sizeof( int ));

	// output the buffer.
	FS_Write( cls.demofile, buffer, size );
}

/*
====================
CL_WriteDemoHeader

Write demo header
====================
*/
static void CL_WriteDemoHeader( const char *name )
{
	double maxfps;
	int copysize;
	int savepos;
	int curpos;

	Con_Printf( "recording to %s.\n", name );
	cls.demofile = FS_Open( name, "wb", false );
	cls.demotime = 0.0;

	if( !cls.demofile )
	{
		Con_Printf( S_ERROR "couldn't open %s.\n", name );
		return;
	}

	cls.demorecording = true;
	cls.demowaiting = true;	// don't start saving messages until a non-delta compressed message is received

	maxfps = fps_override.value ? MAX_FPS_HARD : MAX_FPS_SOFT;

	memset( &demo.header, 0, sizeof( demo.header ));

	demo.header.id = IDEMOHEADER;
	demo.header.dem_protocol = DEMO_PROTOCOL;
	demo.header.net_protocol = CL_GetDemoNetProtocol( cls.legacymode );
	demo.header.host_fps = host_maxfps.value ? bound( MIN_FPS, host_maxfps.value, maxfps ) : maxfps;
	Q_strncpy( demo.header.mapname, clgame.mapname, sizeof( demo.header.mapname ));
	Q_strncpy( demo.header.comment, clgame.maptitle, sizeof( demo.header.comment ));
	Q_strncpy( demo.header.gamedir, FS_Gamedir(), sizeof( demo.header.gamedir ));

	// write header
	FS_Write( cls.demofile, &demo.header, sizeof( demo.header ));

	demo.directory.numentries = 2;
	demo.directory.entries = Mem_Calloc( cls.mempool, sizeof( demoentry_t ) * demo.directory.numentries );

	// DIRECTORY ENTRY # 0
	demo.entry = &demo.directory.entries[0];	// only one here.
	demo.entry->entrytype = DEMO_STARTUP;
	demo.entry->playback_time = 0.0f;		// startup takes 0 time.
	demo.entry->offset = FS_Tell( cls.demofile );	// position for this chunk.

	// finish off the startup info.
	CL_WriteDemoCmdHeader( dem_stop, cls.demoheader );
	FS_Flush( cls.demoheader );

	// now copy the stuff we cached from the server.
	copysize = savepos = FS_Tell( cls.demoheader );

	FS_Seek( cls.demoheader, 0, SEEK_SET );

	FS_FileCopy( cls.demofile, cls.demoheader, copysize );

	// jump back to end, in case we record another demo for this session.
	FS_Seek( cls.demoheader, savepos, SEEK_SET );

	demo.starttime = CL_GetDemoRecordClock();	// setup the demo starttime
	demo.realstarttime = demo.starttime;
	demo.framecount = 0;
	cls.td_startframe = host.framecount;
	cls.td_lastframe = -1;			// get a new message this frame

	// now move on to entry # 1, the first data chunk.
	curpos = FS_Tell( cls.demofile );
	demo.entry->length = curpos - demo.entry->offset;

	// now we are writing the first real lump.
	demo.entry = &demo.directory.entries[1]; // first real data lump
	demo.entry->entrytype = DEMO_NORMAL;
	demo.entry->playback_time = 0.0f; // startup takes 0 time.

	demo.entry->offset = FS_Tell( cls.demofile );

	// demo playback should read this as an incoming message.
	// write the client's realtime value out so we can synchronize the reads.
	CL_WriteDemoCmdHeader( dem_jumptime, cls.demofile );

	if( clgame.hInstance ) clgame.dllFuncs.pfnReset();

	Cbuf_InsertText( "fullupdate\n" );
	Cbuf_Execute();
}

/*
=================
CL_StopRecord

finish recording demo
=================
*/
static void CL_StopRecord( void )
{
	int	i, curpos;
	float	stoptime;
	int	frames;

	if( !cls.demorecording ) return;

	// demo playback should read this as an incoming message.
	CL_WriteDemoCmdHeader( dem_stop, cls.demofile );

	stoptime = CL_GetDemoRecordClock();
	if( clgame.hInstance ) clgame.dllFuncs.pfnReset();

	curpos = FS_Tell( cls.demofile );
	demo.entry->length = curpos - demo.entry->offset;
	demo.entry->playback_time = stoptime - demo.realstarttime;
	demo.entry->playback_frames = demo.framecount;

	//  Now write out the directory and free it and touch up the demo header.
	FS_Write( cls.demofile, &demo.directory.numentries, sizeof( int ));

	for( i = 0; i < demo.directory.numentries; i++ )
		FS_Write( cls.demofile, &demo.directory.entries[i], sizeof( demoentry_t ));

	Mem_Free( demo.directory.entries );
	demo.directory.numentries = 0;

	demo.header.directory_offset = curpos;
	FS_Seek( cls.demofile, 0, SEEK_SET );
	FS_Write( cls.demofile, &demo.header, sizeof( demo.header ));

	FS_Close( cls.demofile );
	cls.demofile = NULL;
	cls.demorecording = false;
	cls.demoname[0] = '\0';
	cls.td_lastframe = host.framecount;
	gameui.globals->demoname[0] = '\0';
	demo.header.host_fps = 0.0;

	frames = cls.td_lastframe - cls.td_startframe;
	Con_Printf( "Completed demo\nRecording time: %02d:%02d, frames %i\n", (int)(cls.demotime / 60.0f), (int)fmod(cls.demotime, 60.0f), frames );
	cls.demotime = 0.0;
}

/*
=================
CL_DrawDemoRecording
=================
*/
void CL_DrawDemoRecording( void )
{
	char	string[64];
	rgba_t	color = { 255, 255, 255, 255 };
	int	pos;
	int	len;

	if(!( host_developer.value && cls.demorecording ))
		return;

	pos = FS_Tell( cls.demofile );
	Q_snprintf( string, sizeof( string ), "^1RECORDING:^7 %s: %s time: %02d:%02d", cls.demoname,
		Q_memprint( pos ), (int)(cls.demotime / 60.0f ), (int)fmod( cls.demotime, 60.0f ));

	Con_DrawStringLen( string, &len, NULL );
	Con_DrawString(( refState.width - len ) >> 1, refState.height >> 4, string, color );
}

/*
=======================================================================

CLIENT SIDE DEMO PLAYBACK

=======================================================================
*/
/*
=================
CL_ReadDemoCmdHeader

read the demo command
=================
*/
static qboolean CL_ReadDemoCmdHeader( byte *cmd, float *dt )
{
	// read the command
	// HACKHACK: skip NOPs
	do
	{
		FS_Read( cls.demofile, cmd, sizeof( byte ));
	} while( *cmd == dem_unknown );

	if( *cmd > dem_lastcmd )
	{
		Con_Printf( S_ERROR "Demo cmd %d > %d, file offset = %d\n", *cmd, dem_lastcmd, (int)FS_Tell( cls.demofile ));
		CL_DemoCompleted();
		return false;
	}

	// read the timestamp
	FS_Read( cls.demofile, dt, sizeof( float ));

	return true;
}

/*
=================
CL_ReadDemoUserCmd

read the demo usercmd for predicting
and smooth movement during playback the demo
=================
*/
static void CL_ReadDemoUserCmd( qboolean discard )
{
	byte	data[1024];
	int	cmdnumber;
	int	outgoing_sequence;
	runcmd_t	*pcmd;
	word	bytes;

	FS_Read( cls.demofile, &outgoing_sequence, sizeof( int ));
	FS_Read( cls.demofile, &cmdnumber, sizeof( int ));
	FS_Read( cls.demofile, &bytes, sizeof( short ));

	if( bytes >= sizeof( data ))
	{
		Con_Printf( S_ERROR "%s: too large dem_usercmd (size %u seq %i)\n", __func__, bytes, outgoing_sequence );
		CL_DemoAborted();
		return;
	}

	FS_Read( cls.demofile, data, bytes );

	if( !discard )
	{
		const usercmd_t nullcmd = { 0 };
		sizebuf_t		buf;
		demoangle_t	*a;

		MSG_Init( &buf, "UserCmd", data, sizeof( data ));

		// a1ba: I have no proper explanation why
		cmdnumber++;

		pcmd = &cl.commands[cmdnumber & CL_UPDATE_MASK];
		pcmd->processedfuncs = false;
		pcmd->senttime = 0.0f;
		pcmd->receivedtime = 0.1f;
		pcmd->frame_lerp = 0.1f;
		pcmd->heldback = false;
		pcmd->sendsize = 1;

		// always delta'ing from null
		MSG_ReadDeltaUsercmd( &buf, &nullcmd, &pcmd->cmd );

		// make sure what interp info contain angles from different frames
		// or lerping will stop working
		if( demo.lasttime != demo.timestamp )
		{
			// select entry into circular buffer
			demo.angle_position = (demo.angle_position + 1) & ANGLE_MASK;
			a = &demo.cmds[demo.angle_position];

			// record update
			a->starttime = demo.timestamp;
			VectorCopy( pcmd->cmd.viewangles, a->viewangles );
			demo.lasttime = demo.timestamp;
		}

		// NOTE: we need to have the current outgoing sequence correct
		// so we can do prediction correctly during playback
		cls.netchan.outgoing_sequence = outgoing_sequence;

		// save last usercmd
		cl.cmd = pcmd->cmd;
	}
}

/*
=================
CL_ReadDemoSequence

read netchan sequences
=================
*/
static void CL_ReadDemoSequence( qboolean discard )
{
	int	incoming_sequence;
	int	incoming_acknowledged;
	int	incoming_reliable_acknowledged;
	int	incoming_reliable_sequence;
	int	outgoing_sequence;
	int	reliable_sequence;
	int	last_reliable_sequence;

	FS_Read( cls.demofile, &incoming_sequence, sizeof( int ));
	FS_Read( cls.demofile, &incoming_acknowledged, sizeof( int ));
	FS_Read( cls.demofile, &incoming_reliable_acknowledged, sizeof( int ));
	FS_Read( cls.demofile, &incoming_reliable_sequence, sizeof( int ));
	FS_Read( cls.demofile, &outgoing_sequence, sizeof( int ));
	FS_Read( cls.demofile, &reliable_sequence, sizeof( int ));
	FS_Read( cls.demofile, &last_reliable_sequence, sizeof( int ));

	if( discard ) return;

	cls.netchan.incoming_sequence	= incoming_sequence;
	cls.netchan.incoming_acknowledged = incoming_acknowledged;
	cls.netchan.incoming_reliable_acknowledged = incoming_reliable_acknowledged;
	cls.netchan.incoming_reliable_sequence = incoming_reliable_sequence;
	cls.netchan.outgoing_sequence	= outgoing_sequence;
	cls.netchan.reliable_sequence	= reliable_sequence;
	cls.netchan.last_reliable_sequence = last_reliable_sequence;
}

/*
=================
CL_DemoStartPlayback
=================
*/
static void CL_DemoStartPlayback( int mode )
{
	if( cls.changedemo )
	{
		int maxclients = cl.maxclients;

		S_StopAllSounds( true );
		SCR_BeginLoadingPlaque( false );

		CL_ClearState( );
		CL_InitEdicts( maxclients ); // re-arrange edicts
	}
	else
	{
		// NOTE: at this point demo is still valid
		CL_Disconnect();
		SV_Shutdown( "Server was killed due to demo playback start\n" );

		Con_FastClose();
		UI_SetActiveMenu( false );
	}

	cls.demoplayback = mode;
	cls.state = ca_connected;
	cl.background = (cls.demonum != -1) ? true : false;
	cls.spectator = false;
	cls.signon = 0;

	demo.starttime = CL_GetDemoPlaybackClock(); // for determining whether to read another message

	CL_SetupNetchanForProtocol( cls.legacymode );

	memset( demo.cmds, 0, sizeof( demo.cmds ));
	demo.angle_position = 1;
	demo.framecount = 0;
	cls.lastoutgoingcommand = -1;
 	cls.nextcmdtime = host.realtime;
	cl.last_command_ack = -1;
}

/*
=================
CL_DemoCompleted
=================
*/
void CL_DemoCompleted( void )
{
	if( cls.demonum != -1 )
		cls.changedemo = true;

	CL_StopPlayback();

	if( !CL_NextDemo() && !cls.changedemo )
		UI_SetActiveMenu( true );

	Cvar_DirectSet( &v_dark, "0" );
}

/*
=================
CL_DemoMoveToNextSection

returns true on success, false on failure
g-cont. probably captain obvious mode is ON
=================
*/
static qboolean CL_DemoMoveToNextSection( void )
{
	if( ++demo.entryIndex >= demo.directory.numentries )
	{
		// done
		CL_DemoCompleted();
		return false;
	}

	// switch to next section, we got a dem_stop
	demo.entry = &demo.directory.entries[demo.entryIndex];

	// ready to continue reading, reset clock.
	FS_Seek( cls.demofile, demo.entry->offset, SEEK_SET );

	// time is now relative to this chunk's clock.
	demo.starttime = CL_GetDemoPlaybackClock();
	demo.framecount = 0;

	return true;
}

static qboolean CL_ReadRawNetworkData( byte *buffer, size_t *length )
{
	int	msglen = 0;

	Assert( buffer != NULL );
	Assert( length != NULL );

	*length = 0; // assume we fail
	FS_Read( cls.demofile, &msglen, sizeof( int ));

	if( msglen < 0 )
	{
		Con_Reportf( S_ERROR "Demo message length < 0\n" );
		CL_DemoCompleted();
		return false;
	}

	if( msglen > MAX_INIT_MSG )
	{
		Con_Reportf( S_ERROR "Demo message %i > %i\n", msglen, MAX_INIT_MSG );
		CL_DemoCompleted();
		return false;
	}

	if( msglen > 0 )
	{
		if( FS_Read( cls.demofile, buffer, msglen ) != msglen )
		{
			Con_Reportf( S_ERROR "Error reading demo message data\n" );
			CL_DemoCompleted();
			return false;
		}
	}

	cls.netchan.last_received = host.realtime;
	cls.netchan.total_received += msglen;
	*length = msglen;

	if( cls.state != ca_active )
		Cbuf_Execute();

	return true;
}

/*
=================
CL_DemoReadMessageQuake

reads demo data and write it to client
=================
*/
static qboolean CL_DemoReadMessageQuake( byte *buffer, size_t *length )
{
	vec3_t		viewangles;
	int		msglen = 0;
	demoangle_t	*a;

	*length = 0; // assume we fail

	// decide if it is time to grab the next message
	if( cls.signon == SIGNONS )	// allways grab until fully connected
	{
		if( cls.timedemo )
		{
			if( host.framecount == cls.td_lastframe )
				return false; // already read this frame's message

			cls.td_lastframe = host.framecount;

			// if this is the second frame, grab the real td_starttime
			// so the bogus time on the first frame doesn't count
			if( host.framecount == cls.td_startframe + 1 )
				cls.td_starttime = host.realtime;
		}
		else if( cl.time <= cl.mtime[0] )
		{
			// don't need another message yet
			return false;
		}
	}

	// get the next message
	FS_Read( cls.demofile, &msglen, sizeof( int ));
	FS_Read( cls.demofile, &viewangles[0], sizeof( float ));
	FS_Read( cls.demofile, &viewangles[1], sizeof( float ));
	FS_Read( cls.demofile, &viewangles[2], sizeof( float ));
	cls.netchan.incoming_sequence++;
	demo.timestamp = cl.mtime[0];
	cl.skip_interp = false;

	// make sure what interp info contain angles from different frames
	// or lerping will stop working
	if( demo.lasttime != demo.timestamp )
	{
		// select entry into circular buffer
		demo.angle_position = (demo.angle_position + 1) & ANGLE_MASK;
		a = &demo.cmds[demo.angle_position];

		// record update
		a->starttime = demo.timestamp;
		VectorCopy( viewangles, a->viewangles );
		demo.lasttime = demo.timestamp;
	}

	if( msglen < 0 )
	{
		Con_Reportf( S_ERROR "Demo message length < 0\n" );
		CL_DemoCompleted();
		return false;
	}

	if( msglen > MAX_INIT_MSG )
	{
		Con_Reportf( S_ERROR "Demo message %i > %i\n", msglen, MAX_INIT_MSG );
		CL_DemoCompleted();
		return false;
	}

	if( msglen > 0 )
	{
		if( FS_Read( cls.demofile, buffer, msglen ) != msglen )
		{
			Con_Reportf( S_ERROR "Error reading demo message data\n" );
			CL_DemoCompleted();
			return false;
		}
	}

	cls.netchan.last_received = host.realtime;
	cls.netchan.total_received += msglen;
	*length = msglen;

	if( cls.state != ca_active )
		Cbuf_Execute();
	return true;
}

/*
=================
CL_DemoReadMessage

reads demo data and write it to client
=================
*/
qboolean CL_DemoReadMessage( byte *buffer, size_t *length )
{
	size_t		curpos = 0, lastpos = 0;
	float		fElapsedTime = 0.0f;
	qboolean		swallowmessages = true;
	static int	tdlastdemoframe = 0;
	byte		*userbuf = NULL;
	size_t		size = 0;
	byte		cmd;

	if( !cls.demofile )
	{
		CL_DemoCompleted();
		return false;
	}

	if(( !cl.background && ( cl.paused || cls.key_dest != key_game )) || cls.key_dest == key_console )
	{
		demo.starttime += host.frametime;
		return false; // paused
	}

	if( cls.demoplayback == DEMO_QUAKE1 )
		return CL_DemoReadMessageQuake( buffer, length );

	do
	{
		qboolean	bSkipMessage = false;

		if( !cls.demofile ) break;
		curpos = FS_Tell( cls.demofile );

		if( !CL_ReadDemoCmdHeader( &cmd, &demo.timestamp ))
			return false;

		fElapsedTime = CL_GetDemoPlaybackClock() - demo.starttime;
		if( !cls.timedemo ) bSkipMessage = ((demo.timestamp - cl_serverframetime()) >= fElapsedTime) ? true : false;
		if( cls.changelevel ) demo.framecount = 1;

		// changelevel issues
		if( demo.framecount <= 2 && ( fElapsedTime - demo.timestamp ) > host.frametime )
			demo.starttime = CL_GetDemoPlaybackClock();

		// not ready for a message yet, put it back on the file.
		if( cmd != dem_norewind && cmd != dem_stop && bSkipMessage )
		{
			// never skip first message
			if( demo.framecount != 0 )
			{
				FS_Seek( cls.demofile, curpos, SEEK_SET );
				return false; // not time yet.
			}
		}

		// we already have the usercmd_t for this frame
		// don't read next usercmd_t so predicting will work properly
		if( cmd == dem_usercmd && lastpos != 0 && demo.framecount != 0 )
		{
			FS_Seek( cls.demofile, lastpos, SEEK_SET );
			return false; // not time yet.
		}

		// COMMAND HANDLERS
		switch( cmd )
		{
		case dem_jumptime:
			demo.starttime = CL_GetDemoPlaybackClock();
			return false; // time is changed, skip frame
		case dem_stop:
			CL_DemoMoveToNextSection();
			return false; // header is ended, skip frame
		case dem_userdata:
			FS_Read( cls.demofile, &size, sizeof( int ));
			userbuf = Mem_Malloc( cls.mempool, size );
			FS_Read( cls.demofile, userbuf, size );

			if( clgame.hInstance )
				clgame.dllFuncs.pfnDemo_ReadBuffer( size, userbuf );
			Mem_Free( userbuf );
			userbuf = NULL;
			break;
		case dem_usercmd:
			CL_ReadDemoUserCmd( false );
			lastpos = FS_Tell( cls.demofile );
			break;
		default:
			swallowmessages = false;
			break;
		}
	} while( swallowmessages );

	// If we are playing back a timedemo, and we've already passed on a
	//  frame update for this host_frame tag, then we'll just skip this message.
	if( cls.timedemo && ( tdlastdemoframe == host.framecount ))
	{
		FS_Seek( cls.demofile, FS_Tell ( cls.demofile ) - 5, SEEK_SET );
		return false;
	}

	tdlastdemoframe = host.framecount;

	if( !cls.demofile )
		return false;

	// if not on "LOADING" section, check a few things
	if( demo.entryIndex )
	{
		// We are now on the second frame of a new section,
		// if so, reset start time (unless in a timedemo)
		if( demo.framecount == 1 && !cls.timedemo )
		{
			// cheat by moving the relative start time forward.
			demo.starttime = CL_GetDemoPlaybackClock();
		}
	}

	demo.framecount++;
	CL_ReadDemoSequence( false );

	return CL_ReadRawNetworkData( buffer, length );
}

static void CL_DemoFindInterpolatedViewAngles( float t, float *frac, demoangle_t **prev, demoangle_t **next )
{
	int	i, i0, i1, imod;
	float	at;

	if( cls.timedemo ) return;

	imod = demo.angle_position - 1;
	i0 = (imod + 1) & ANGLE_MASK;
	i1 = (imod + 0) & ANGLE_MASK;

	if( demo.cmds[i0].starttime >= t )
	{
		for( i = 0; i < ANGLE_BACKUP - 2; i++ )
		{
			at = demo.cmds[imod & ANGLE_MASK].starttime;
			if( at == 0.0f ) break;

			if( at < t )
			{
				i0 = (imod + 1) & ANGLE_MASK;
				i1 = (imod + 0) & ANGLE_MASK;
				break;
			}
			imod--;
		}
	}

	*next = &demo.cmds[i0];
	*prev = &demo.cmds[i1];

	// avoid division by zero (probably this should never happens)
	if((*prev)->starttime == (*next)->starttime )
	{
		*prev = *next;
		*frac = 0.0f;
		return;
	}

	// time spans the two entries
	*frac = ( t - (*prev)->starttime ) / ((*next)->starttime - (*prev)->starttime );
	*frac = bound( 0.0f, *frac, 1.0f );
}

/*
==============
CL_DemoInterpolateAngles

We can predict or inpolate player movement with standed client code
but viewangles interpolate here
==============
*/
void CL_DemoInterpolateAngles( void )
{
	demoangle_t	*prev = NULL, *next = NULL;
	float		frac = 0.0f;
	float		curtime;

	if( cls.demoplayback == DEMO_QUAKE1 )
	{
		// manually select next & prev states
		next = &demo.cmds[(demo.angle_position - 0) & ANGLE_MASK];
		prev = &demo.cmds[(demo.angle_position - 1) & ANGLE_MASK];
		if( cl.skip_interp ) *prev = *next; // camera was teleported
		frac = cl.lerpFrac;
	}
	else
	{
		curtime = (CL_GetDemoPlaybackClock() - demo.starttime) - host.frametime;
		if( curtime > demo.timestamp )
			curtime = demo.timestamp; // don't run too far

		CL_DemoFindInterpolatedViewAngles( curtime, &frac, &prev, &next );
	}

	if( prev && next )
	{
		vec4_t	q, q1, q2;

		AngleQuaternion( next->viewangles, q1, false );
		AngleQuaternion( prev->viewangles, q2, false );
		QuaternionSlerp( q2, q1, frac, q );
		QuaternionAngle( q, cl.viewangles );
	}
	else VectorCopy( cl.cmd.viewangles, cl.viewangles );
}

/*
==============
CL_FinishTimeDemo

show stats
==============
*/
static void CL_FinishTimeDemo( void )
{
	qboolean temp = host.allow_console;
	int	frames;
	double	time;

	cls.timedemo = false;

	// the first frame didn't count
	frames = (host.framecount - cls.td_startframe) - 1;
	time = host.realtime - cls.td_starttime;
	if( !time ) time = 1.0;

	host.allow_console = true;
	Con_Printf( "timedemo result: %i frames %5.3f seconds %5.3f fps\n", frames, time, frames / time );
	host.allow_console = temp;

	if( Sys_CheckParm( "-timedemo" ))
		CL_Quit_f();
}

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void CL_StopPlayback( void )
{
	if( !cls.demoplayback ) return;

	// release demofile
	FS_Close( cls.demofile );
	cls.demoplayback = false;
	demo.framecount = 0;
	cls.demofile = NULL;

	cls.olddemonum = Q_max( -1, cls.demonum - 1 );
	if( demo.directory.entries != NULL )
		Mem_Free( demo.directory.entries );
	cls.td_lastframe = host.framecount;
	demo.directory.numentries = 0;
	demo.directory.entries = NULL;
	demo.header.host_fps = 0.0;
	demo.entry = NULL;

	cls.demoname[0] = '\0';	// clear demoname too
	gameui.globals->demoname[0] = '\0';

	if( cls.timedemo )
		CL_FinishTimeDemo();

	if( cls.changedemo )
	{
		S_StopAllSounds( true );
		S_StopBackgroundTrack();
	}
	else
	{
		// let game known about demo state
		Cvar_FullSet( "cl_background", "0", FCVAR_READ_ONLY );
		cls.state = ca_disconnected;
		memset( &cls.serveradr, 0, sizeof( cls.serveradr ) );
		cls.set_lastdemo = false;
		S_StopBackgroundTrack();
		cls.connect_time = 0;
		cls.demonum = -1;
		cls.signon = 0;

		// and finally clear the state
		CL_ClearState ();
	}
}

/*
==================
CL_GetDemoComment
==================
*/
int GAME_EXPORT CL_GetDemoComment( const char *demoname, char *comment )
{
	file_t		*demfile;
	demoheader_t	demohdr;
	demodirectory_t	directory;
	demoentry_t	entry;
	float		playtime = 0.0f;
	int		i;

	if( !comment ) return false;

	demfile = FS_Open( demoname, "rb", false );
	if( !demfile )
	{
		comment[0] = '\0';
		return false;
	}

	// read in the m_DemoHeader
	FS_Read( demfile, &demohdr, sizeof( demoheader_t ));

	if( demohdr.id != IDEMOHEADER )
	{
		FS_Close( demfile );
		Q_strncpy( comment, "<corrupted>", MAX_STRING );
		return false;
	}

	if(( demohdr.net_protocol != PROTOCOL_VERSION &&
		demohdr.net_protocol != PROTOCOL_LEGACY_VERSION ) ||
		demohdr.dem_protocol != DEMO_PROTOCOL )
	{
		FS_Close( demfile );
		Q_strncpy( comment, "<invalid protocol>", MAX_STRING );
		return false;
	}

	// now read in the directory structure.
	FS_Seek( demfile, demohdr.directory_offset, SEEK_SET );
	FS_Read( demfile, &directory.numentries, sizeof( int ));

	if( directory.numentries < 1 || directory.numentries > 1024 )
	{
		FS_Close( demfile );
		Q_strncpy( comment, "<corrupted>", MAX_STRING );
		return false;
	}

	for( i = 0; i < directory.numentries; i++ )
	{
		FS_Read( demfile, &entry, sizeof( demoentry_t ));
		playtime += entry.playback_time;
	}

	// split comment to sections
	Q_strncpy( comment, demohdr.mapname, CS_SIZE );
	Q_strncpy( comment + CS_SIZE, demohdr.comment, CS_SIZE );
	Q_snprintf( comment + CS_SIZE * 2, CS_TIME, "%g sec", playtime );

	// all done
	FS_Close( demfile );

	return true;
}

/*
==================
CL_NextDemo

Called when a demo finishes
==================
*/
static qboolean CL_NextDemo( void )
{
	char	str[MAX_QPATH];

	if( cls.demonum == -1 )
		return false; // don't play demos
	S_StopAllSounds( true );

	if( !cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS )
	{
		cls.demonum = 0;
		if( !cls.demos[cls.demonum][0] )
		{
			Con_Printf( "no demos listed with startdemos\n" );
			cls.demonum = -1;
			return false;
		}
	}

	Q_snprintf( str, MAX_STRING, "playdemo %s\n", cls.demos[cls.demonum] );
	Cbuf_InsertText( str );
	cls.demonum++;

	return true;
}

/*
==================
CL_CheckStartupDemos

queue demos loop after movie playing
==================
*/
void CL_CheckStartupDemos( void )
{
	if( !cls.demos_pending )
		return; // no demos in loop

	if( cls.movienum != -1 )
		return; // wait until movies finished

	if( GameState->nextstate != STATE_RUNFRAME || cls.demoplayback )
	{
		// commandline override
		cls.demos_pending = false;
		cls.demonum = -1;
		return;
	}

	// run demos loop in background mode
	Cvar_DirectSet( &v_dark, "1" );
	cls.demos_pending = false;
	cls.demonum = 0;
	CL_NextDemo ();
}

/*
==================
CL_DemoGetName
==================
*/
static void CL_DemoGetName( int lastnum, char *filename, size_t size )
{
	if( lastnum < 0 || lastnum > 9999 )
	{
		// bound
		Q_strncpy( filename, "demo9999", size );
		return;
	}

	Q_snprintf( filename, size, "demo%04d", lastnum );
}

/*
====================
CL_Record_f

record <demoname>
Begins recording a demo from the current position
====================
*/
void CL_Record_f( void )
{
	string		demoname, demopath;
	const char	*name;
	int		n;

	if( Cmd_Argc() == 1 )
	{
		name = "new";
	}
	else if( Cmd_Argc() == 2 )
	{
		name = Cmd_Argv( 1 );
	}
	else
	{
		Con_Printf( S_USAGE "record <demoname>\n" );
		return;
	}

	if( cls.demorecording )
	{
		Con_Printf( "Already recording.\n");
		return;
	}

	if( cls.demoplayback )
	{
		Con_Printf( "Can't record during demo playback.\n");
		return;
	}

	if( !cls.demoheader || cls.state != ca_active )
	{
		Con_Printf( "You must be in a level to record.\n");
		return;
	}

	if( !Q_stricmp( name, "new" ))
	{
		// scan for a free filename
		for( n = 0; n < 10000; n++ )
		{
			CL_DemoGetName( n, demoname, sizeof( demoname ));
			Q_snprintf( demopath, sizeof( demopath ), "%s.dem", demoname );

			if( !FS_FileExists( demopath, true ))
				break;
		}

		if( n == 10000 )
		{
			Con_Printf( S_ERROR "no free slots for demo recording\n" );
			return;
		}
	}
	else Q_strncpy( demoname, name, sizeof( demoname ));

	// open the demo file
	Q_snprintf( demopath, sizeof( demopath ), "%s.dem", demoname );

	// make sure that old demo is removed
	if( FS_FileExists( demopath, false ))
		FS_Delete( demopath );

	Q_strncpy( cls.demoname, demoname, sizeof( cls.demoname ));
	Q_strncpy( gameui.globals->demoname, demoname, sizeof( gameui.globals->demoname ));

	CL_WriteDemoHeader( demopath );
}

static qboolean CL_ParseDemoHeader( const char *callee, const char *filename, file_t *f, demoheader_t *hdr, int32_t *numentries )
{
	if( FS_Read( f, hdr, sizeof( *hdr )) != sizeof( *hdr ) || hdr->id != IDEMOHEADER )
	{
		Con_Printf( S_ERROR "%s: %s is not in supported format or not a demo file\n", callee, filename );
		return false;
	}

	// force null terminate strings
	hdr->mapname[sizeof( hdr->mapname ) - 1] = 0;
	hdr->comment[sizeof( hdr->comment ) - 1] = 0;
	hdr->gamedir[sizeof( hdr->gamedir ) - 1] = 0;

	if( hdr->dem_protocol != DEMO_PROTOCOL )
	{
		Con_Printf( S_ERROR "%s: demo protocol outdated (%i should be %i)\n",
			callee, hdr->net_protocol, DEMO_PROTOCOL );
		return false;
	}

	if( hdr->net_protocol != PROTOCOL_VERSION && hdr->net_protocol != PROTOCOL_LEGACY_VERSION && hdr->net_protocol != PROTOCOL_GOLDSRC_VERSION_DEMO )
	{
		Con_Printf( S_ERROR "%s: net protocol outdated (%i should be %i or %i)\n",
			callee, hdr->net_protocol, PROTOCOL_VERSION, PROTOCOL_LEGACY_VERSION );
		return false;
	}

	if( FS_Seek( f, hdr->directory_offset, SEEK_SET ) < 0
		|| FS_Read( f, numentries, sizeof( *numentries )) != sizeof( *numentries ))
	{
		Con_Printf( S_ERROR "%s: can't find directory offset in %s, demo file corrupted\n",
			callee, filename );
		return false;
	}

	if(( *numentries < 1 ) || ( *numentries > 1024 ))
	{
		Con_Printf( S_ERROR "%s: demo have bogus # of directory entries: %i\n",
			callee, *numentries );
		return false;
	}

	return true;
}

/*
====================
CL_PlayDemo_f

playdemo <demoname>
====================
*/
void CL_PlayDemo_f( void )
{
	char	filename[MAX_QPATH];
	char	demoname[MAX_QPATH];
	int	i, ident;

	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "%s <demoname>\n", Cmd_Argv( 0 ));
		return;
	}

	if( cls.demoplayback )
		CL_StopPlayback();

	if( cls.demorecording )
	{
		Con_Printf( "Can't playback during demo record.\n");
		return;
	}

	Q_strncpy( demoname, Cmd_Argv( 1 ), sizeof( demoname ));
	COM_StripExtension( demoname );
	Q_snprintf( filename, sizeof( filename ), "%s.dem", demoname );

	// hidden parameter
	if( Cmd_Argc() > 2 )
		cls.set_lastdemo = Q_atoi( Cmd_Argv( 2 ));

	// member last demo
	if( cls.set_lastdemo )
		Cvar_Set( "lastdemo", demoname );

	if( !FS_FileExists( filename, true ))
	{
		Con_Printf( S_ERROR "couldn't open %s\n", filename );
		CL_DemoAborted();
		return;
	}

	cls.demofile = FS_Open( filename, "rb", true );
	Q_strncpy( cls.demoname, demoname, sizeof( cls.demoname ));
	Q_strncpy( gameui.globals->demoname, demoname, sizeof( gameui.globals->demoname ));

	FS_Read( cls.demofile, &ident, sizeof( int ));
	FS_Seek( cls.demofile, 0, SEEK_SET ); // rewind back to start
	cls.forcetrack = 0;

	// check for quake demos
	if( ident != IDEMOHEADER )
	{
		int	c, neg = false;

		demo.header.host_fps = host_maxfps.value;

		while(( c = FS_Getc( cls.demofile )) != '\n' )
		{
			if( c == '-' ) neg = true;
			else cls.forcetrack = cls.forcetrack * 10 + (c - '0');
		}

		if( neg ) cls.forcetrack = -cls.forcetrack;
		CL_DemoStartPlayback( DEMO_QUAKE1 );
		cls.legacymode = PROTO_QUAKE;
		return; // quake demo is started
	}

	// read in the demo header
	if( !CL_ParseDemoHeader( Cmd_Argv( 0 ), filename, cls.demofile, &demo.header, &demo.directory.numentries ))
	{
		CL_DemoAborted();
		return;
	}

	// allocate demo entries
	demo.directory.entries = Mem_Malloc( cls.mempool, sizeof( *demo.directory.entries ) * demo.directory.numentries );

	for( i = 0; i < demo.directory.numentries; i++ )
	{
		demoentry_t *entry = &demo.directory.entries[i];

		if( FS_Read( cls.demofile, entry, sizeof( *entry )) != sizeof( *entry ))
		{
			Con_Printf( S_ERROR "%s: demo entry %i of %s corrupted", Cmd_Argv( 0 ), i, filename );
			CL_DemoAborted();
			return;
		}

		entry->description[sizeof( entry->description ) - 1] = 0;
	}

	demo.entryIndex = 0;
	demo.entry = &demo.directory.entries[demo.entryIndex];

	FS_Seek( cls.demofile, demo.entry->offset, SEEK_SET );

	CL_DemoStartPlayback( DEMO_XASH3D );

	// must be after DemoStartPlayback, as CL_Disconnect_f resets the protocol
	cls.legacymode = CL_GetProtocolFromDemo( demo.header.net_protocol );

	// g-cont. is this need?
	Q_strncpy( cls.servername, demoname, sizeof( cls.servername ));

	// begin a playback demo
}

/*
====================
CL_TimeDemo_f

timedemo <demoname>
====================
*/
void CL_TimeDemo_f( void )
{
	CL_PlayDemo_f ();

	// cls.td_starttime will be grabbed at the second frame of the demo, so
	// all the loading time doesn't get counted
	cls.timedemo = true;
	cls.td_starttime = host.realtime;
	cls.td_startframe = host.framecount;
	cls.td_lastframe = -1;		// get a new message this frame
}

/*
==================
CL_StartDemos_f
==================
*/
void CL_StartDemos_f( void )
{
	int	i, c;

	if( cls.key_dest != key_menu )
	{
		Con_Printf( "'startdemos' is not valid from the console\n" );
		return;
	}

	c = Cmd_Argc() - 1;
	if( c > MAX_DEMOS )
	{
		Con_DPrintf( S_WARN "%s: max %i demos in demoloop\n", __func__, MAX_DEMOS );
		c = MAX_DEMOS;
	}

	Con_Printf( "%i demo%s in loop\n", c, (c > 1) ? "s" : "" );

	for( i = 1; i < c + 1; i++ )
		Q_strncpy( cls.demos[i-1], Cmd_Argv( i ), sizeof( cls.demos[0] ));
	cls.demos_pending = true;
}

/*
==================
CL_Demos_f

Return to looping demos
==================
*/
void CL_Demos_f( void )
{
	if( cls.key_dest != key_menu )
	{
		Con_Printf( "'demos' is not valid from the console\n" );
		return;
	}

	// demos loop are not running
	if( cls.olddemonum == -1 )
		return;

	cls.demonum = cls.olddemonum;

	// run demos loop in background mode
	if( !SV_Active() && !cls.demoplayback )
		CL_NextDemo ();
}


/*
====================
CL_Stop_f

stop any client activity
====================
*/
void CL_Stop_f( void )
{
	// stop all
	CL_StopRecord();
	CL_StopPlayback();
	SCR_StopCinematic();

	// stop background track that was runned from the console
	if( !SV_Active( ))
	{
		S_StopBackgroundTrack();
	}
}

void CL_ListDemo_f( void )
{
	demoheader_t hdr;
	int32_t num_entries;
	file_t *f;
	char filename[MAX_QPATH];
	char demoname[MAX_QPATH];
	int i;

	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "%s <demoname>\n", Cmd_Argv( 0 ));
		return;
	}

	Q_strncpy( demoname, Cmd_Argv( 1 ), sizeof( demoname ));
	COM_StripExtension( demoname );
	Q_snprintf( filename, sizeof( filename ), "%s.dem", demoname );

	f = FS_Open( filename, "rb", true );
	if( !f )
	{
		Con_Printf( S_ERROR "couldn't open %s\n", filename );
		return;
	}

	if( !CL_ParseDemoHeader( Cmd_Argv( 0 ), filename, f, &hdr, &num_entries ))
	{
		FS_Close( f );
		return;
	}

	Con_Printf( "Demo contents for %s:\n"
		"\tProtocol: %i net/%i demo\n"
		"\tFPS: %g\n"
		"\tMap: %s\n"
		"\tComment: %s\n"
		"\tGame: %s\n",
		filename, hdr.net_protocol, hdr.dem_protocol, hdr.host_fps, hdr.mapname,
		hdr.comment, hdr.gamedir );

	for( i = 0; i < num_entries; i++ )
	{
		demoentry_t entry;

		Con_Printf( "Demo entry #%i:\n", i );

		if( FS_Read( f, &entry, sizeof( entry )) != sizeof( entry ))
		{
			Con_Printf( S_ERROR "can't read demo entry\n" );
			FS_Close( f );
			return;
		}

		entry.description[sizeof( entry.description ) - 1] = 0;

		if( entry.entrytype == DEMO_STARTUP )
		{
			// startup entries don't have anything useful
			Con_Printf( "\tEntry type: " S_YELLOW "startup" S_DEFAULT "\n" );
		}
		else
		{
			Con_Printf( "\tEntry type: " S_GREEN "normal" S_DEFAULT " (%i)\n"
				"\tEntry playback time/frames: %.2f seconds/%i frames\n"
				"\tEntry flags: 0x%x\n"
				"\tEntry description: %s\n",
				entry.entrytype, entry.playback_time, entry.playback_frames,
				entry.flags, entry.description );
		}
	}

	FS_Close( f );
}
