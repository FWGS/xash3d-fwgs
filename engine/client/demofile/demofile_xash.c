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
#include "demofile.h"

#define dem_unknown  0          // unknown command
#define dem_norewind 1          // startup message
#define dem_read     2          // it's a normal network packet
#define dem_jumptime 3          // move the demostart time value forward by this amount
#define dem_userdata 4          // userdata from the client.dll
#define dem_usercmd  5          // read usercmd_t
#define dem_stop     6          // end of time
#define dem_lastcmd  dem_stop

#define DEMO_STARTUP 0          // this lump contains startup info needed to spawn into the server
#define DEMO_NORMAL  1          // this lump contains playback info of messages, etc., needed during playback.

// Demo flags
#define FDEMO_TITLE         0x01        // Show title
#define FDEMO_PLAY          0x04        // Playing cd track
#define FDEMO_FADE_IN_SLOW  0x08        // Fade in (slow)
#define FDEMO_FADE_IN_FAST  0x10        // Fade in (fast)
#define FDEMO_FADE_OUT_SLOW 0x20        // Fade out (slow)
#define FDEMO_FADE_OUT_FAST 0x40        // Fade out (fast)

#define IDEMOHEADER   (( 'M' << 24 ) + ( 'E' << 16 ) + ( 'D' << 8 ) + 'I' ) // little-endian "IDEM"
#define DEMO_PROTOCOL 3

#define PROTOCOL_GOLDSRC_VERSION_DEMO ( PROTOCOL_GOLDSRC_VERSION | ( BIT( 7 ))) // should be 48, only to differentiate it from PROTOCOL_LEGACY_VERSION

#pragma pack( push, 1 )
typedef struct
{
	int    id;               // should be IDEM
	int    dem_protocol;     // should be DEMO_PROTOCOL
	int    net_protocol;     // should be PROTOCOL_VERSION
	double host_fps;         // fps for demo playing
	char   mapname[64];      // name of map
	char   comment[64];      // comment for demo
	char   gamedir[64];      // name of game directory (FS_Gamedir())
	int    directory_offset; // offset of Entry Directory.
} demoheader_t;
#pragma pack( pop )

typedef struct
{
	int   entrytype;       // DEMO_STARTUP or DEMO_NORMAL
	float playback_time;   // time of track
	int   playback_frames; // # of frames in track
	int   offset;          // file offset of track data
	int   length;          // length of track
	int   flags;           // FX-flags
	char  description[64]; // entry description
} demoentry_t;

typedef struct
{
	demoentry_t *entries;   // track entry info
	int32_t     numentries; // number of tracks
} demodirectory_t;

// private demo states
struct
{
	demoheader_t    header;
	demoentry_t     *entry;
	demodirectory_t directory;
	int   framecount;
	float starttime;
	float realstarttime;
	float timestamp;
	float lasttime;
	int   entryIndex;

	// interpolation stuff
	demoangle_t cmds[ANGLE_BACKUP];
	int angle_position;
} demo;

static qboolean CL_NextDemo( void );

static void CL_DemoFindInterpolatedViewAngles( float t, float *frac, demoangle_t **prev, demoangle_t **next )
{
	int   i, i0, i1, imod;
	float at;

	if( cls.timedemo )
		return;

	imod = demo.angle_position - 1;
	i0 = ( imod + 1 ) & ANGLE_MASK;
	i1 = ( imod + 0 ) & ANGLE_MASK;

	if( demo.cmds[i0].starttime >= t )
	{
		for( i = 0; i < ANGLE_BACKUP - 2; i++ )
		{
			at = demo.cmds[imod & ANGLE_MASK].starttime;
			if( at == 0.0f )
				break;

			if( at < t )
			{
				i0 = ( imod + 1 ) & ANGLE_MASK;
				i1 = ( imod + 0 ) & ANGLE_MASK;
				break;
			}
			imod--;
		}
	}

	*next = &demo.cmds[i0];
	*prev = &demo.cmds[i1];

	// avoid division by zero (probably this should never happens)
	if(( *prev )->starttime == ( *next )->starttime )
	{
		*prev = *next;
		*frac = 0.0f;
		return;
	}

	// time spans the two entries
	*frac = ( t - ( *prev )->starttime ) / (( *next )->starttime - ( *prev )->starttime );
	*frac = bound( 0.0f, *frac, 1.0f );
}

static void DEM_XASH_DemoInterpolateAngles( void )
{
	demoangle_t *prev = NULL, *next = NULL;
	float frac = 0.0f;
	float curtime;

	if( cls.demoplayback == DEMO_QUAKE1 )
	{
		// manually select next & prev states
		next = &demo.cmds[( demo.angle_position - 0 ) & ANGLE_MASK];
		prev = &demo.cmds[( demo.angle_position - 1 ) & ANGLE_MASK];
		if( cl.skip_interp )
			*prev = *next;             // camera was teleported
		frac = cl.lerpFrac;
	}
	else
	{
		curtime = ( CL_GetDemoPlaybackClock() - demo.starttime ) - host.frametime;
		if( curtime > demo.timestamp )
			curtime = demo.timestamp; // don't run too far

		CL_DemoFindInterpolatedViewAngles( curtime, &frac, &prev, &next );
	}

	if( prev && next )
	{
		vec4_t q, q1, q2;

		AngleQuaternion( next->viewangles, q1, false );
		AngleQuaternion( prev->viewangles, q2, false );
		QuaternionSlerp( q2, q1, frac, q );
		QuaternionAngle( q, cl.viewangles );
	}
	else
		VectorCopy( cl.cmd.viewangles, cl.viewangles );
}

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
CL_WriteDemoCmdHeader

Writes the demo command header and time-delta
====================
*/
static void CL_WriteDemoCmdHeader( byte cmd, file_t *file )
{
	float dt;

	Assert( cmd >= 1 && cmd <= dem_lastcmd );
	if( !file )
		return;

	// command
	FS_Write( file, &cmd, sizeof( byte ));

	// time offset
	dt = (float)( CL_GetDemoRecordClock() - demo.starttime );
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
DEM_Xash_WriteDemoUserCmd

Writes the current user cmd
====================
*/
static void DEM_Xash_WriteDemoUserCmd( int cmdnumber )
{
	sizebuf_t buf;
	word      bytes;
	byte      data[1024];

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
DEM_Xash_WriteDemoMessage

Dumps the current net message, prefixed by the length
====================
*/
static void DEM_Xash_WriteDemoMessage( qboolean startup, int start, sizebuf_t *msg )
{
	file_t *file = startup ? cls.demoheader : cls.demofile;
	int    swlen;
	byte   c;

	if( !file )
		return;

	swlen = MSG_GetNumBytesWritten( msg ) - start;
	if( swlen <= 0 )
		return;

	if( !startup )
		demo.framecount++;

	// demo playback should read this as an incoming message.
	c = ( cls.state != ca_active ) ? dem_norewind : dem_read;

	CL_WriteDemoCmdHeader( c, file );
	CL_WriteDemoSequence( file );

	// write the length out.
	FS_Write( file, &swlen, sizeof( int ));

	// output the buffer. Skip the network packet stuff.
	FS_Write( file, MSG_GetData( msg ) + start, swlen );
}

/*
====================
DEM_Xash_WriteDemoUserMessage

Dumps the user message (demoaction)
====================
*/
static void DEM_Xash_WriteDemoUserMessage( int size, byte *buffer )
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
	}
	while( *cmd == dem_unknown );

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
	byte     data[1024];
	int      cmdnumber;
	int      outgoing_sequence;
	runcmd_t *pcmd;
	word     bytes;

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
		sizebuf_t buf;
		demoangle_t     *a;

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
			demo.angle_position = ( demo.angle_position + 1 ) & ANGLE_MASK;
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
	int incoming_sequence;
	int incoming_acknowledged;
	int incoming_reliable_acknowledged;
	int incoming_reliable_sequence;
	int outgoing_sequence;
	int reliable_sequence;
	int last_reliable_sequence;

	FS_Read( cls.demofile, &incoming_sequence, sizeof( int ));
	FS_Read( cls.demofile, &incoming_acknowledged, sizeof( int ));
	FS_Read( cls.demofile, &incoming_reliable_acknowledged, sizeof( int ));
	FS_Read( cls.demofile, &incoming_reliable_sequence, sizeof( int ));
	FS_Read( cls.demofile, &outgoing_sequence, sizeof( int ));
	FS_Read( cls.demofile, &reliable_sequence, sizeof( int ));
	FS_Read( cls.demofile, &last_reliable_sequence, sizeof( int ));

	if( discard )
		return;

	cls.netchan.incoming_sequence = incoming_sequence;
	cls.netchan.incoming_acknowledged = incoming_acknowledged;
	cls.netchan.incoming_reliable_acknowledged = incoming_reliable_acknowledged;
	cls.netchan.incoming_reliable_sequence = incoming_reliable_sequence;
	cls.netchan.outgoing_sequence = outgoing_sequence;
	cls.netchan.reliable_sequence = reliable_sequence;
	cls.netchan.last_reliable_sequence = last_reliable_sequence;
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
	int msglen = 0;

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
	vec3_t viewangles;
	int    msglen = 0;
	demoangle_t *a;

	*length = 0; // assume we fail

	// decide if it is time to grab the next message
	if( cls.signon == SIGNONS ) // allways grab until fully connected
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
		demo.angle_position = ( demo.angle_position + 1 ) & ANGLE_MASK;
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

static qboolean DEM_Xash_CanHandle( file_t *file )
{
	int i, ident;

	if( !file )
	{
		return false;
	}

	return true;
}

static qboolean DEM_Xash_StartPlayback( file_t *file )
{
	int i, ident;

	if( !file )
	{
		return false;
	}

	FS_Read( cls.demofile, &ident, sizeof( int ));
	FS_Seek( cls.demofile, 0, SEEK_SET ); // rewind back to start
	cls.forcetrack = 0;

	demo.angle_position = 1;
	demo.framecount = 0;
	demo.starttime = CL_GetDemoPlaybackClock();
	memset( demo.cmds, 0, sizeof( ANGLE_BACKUP * sizeof( usercmd_t )));

	// check for quake demos
	if( ident != IDEMOHEADER )
	{
		int c, neg = false;

		demo.header.host_fps = host_maxfps.value;

		while(( c = FS_Getc( cls.demofile )) != '\n' )
		{
			if( c == '-' )
				neg = true;
			else
				cls.forcetrack = cls.forcetrack * 10 + ( c - '0' );
		}

		if( neg )
			cls.forcetrack = -cls.forcetrack;
		CL_DemoStartPlayback( DEMO_QUAKE1 );
		cls.legacymode = PROTO_QUAKE;
		return; // quake demo is started
	}

	// read in the demo header
	if( !CL_ParseDemoHeader( Cmd_Argv( 0 ), cls.demoname, cls.demofile, &demo.header, &demo.directory.numentries ))
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
			Con_Printf( S_ERROR "%s: demo entry %i of %s corrupted", Cmd_Argv( 0 ), i, cls.demoname );
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
	Q_strncpy( cls.servername, cls.demoname, sizeof( cls.servername ));

	// begin a playback demo
	return true;
}

static qboolean DEM_Xash_ListDemo( file_t *f )
{
	demoheader_t hdr;
	int32_t      num_entries;
	int i;

	if( !CL_ParseDemoHeader( Cmd_Argv( 0 ), cls.demoname, f, &hdr, &num_entries ))
	{
		return false;
	}

	Con_Printf( "Demo contents for %s:\n"
		    "\tProtocol: %i net/%i demo\n"
		    "\tFPS: %g\n"
		    "\tMap: %s\n"
		    "\tComment: %s\n"
		    "\tGame: %s\n",
		    cls.demoname, hdr.net_protocol, hdr.dem_protocol, hdr.host_fps, hdr.mapname,
		    hdr.comment, hdr.gamedir );

	for( i = 0; i < demo.directory.numentries; i++ )
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

	return true;
}

static qboolean DEM_Xash_StopRecord( file_t *file )
{
	int   i, curpos;
	float stoptime;
	int   frames;

	if( !cls.demorecording )
	{
		return false;
	}

	// demo playback should read this as an incoming message.
	CL_WriteDemoCmdHeader( dem_stop, file );

	stoptime = CL_GetDemoRecordClock();
	if( clgame.hInstance )
		clgame.dllFuncs.pfnReset();

	curpos = FS_Tell( file );
	demo.entry->length = curpos - demo.entry->offset;
	demo.entry->playback_time = stoptime - demo.realstarttime;
	demo.entry->playback_frames = demo.framecount;

	//  Now write out the directory and free it and touch up the demo header.
	FS_Write( file, &demo.directory.numentries, sizeof( int ));

	for( i = 0; i < demo.directory.numentries; i++ )
		FS_Write( file, &demo.directory.entries[i], sizeof( demoentry_t ));

	Mem_Free( demo.directory.entries );
	demo.directory.numentries = 0;

	demo.header.directory_offset = curpos;
	FS_Seek( file, 0, SEEK_SET );
	FS_Write( file, &demo.header, sizeof( demo.header ));

	demo.header.host_fps = 0;
	return true;
}

static qboolean DEM_Xash_StartRecord( file_t *file )
{
	double maxfps;
	int    copysize;
	int    savepos;
	int    curpos;

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
	demo.entry = &demo.directory.entries[0]; // only one here.
	demo.entry->entrytype = DEMO_STARTUP;
	demo.entry->playback_time = 0.0f;             // startup takes 0 time.
	demo.entry->offset = FS_Tell( cls.demofile ); // position for this chunk.

	// finish off the startup info.
	CL_WriteDemoCmdHeader( dem_stop, cls.demoheader );
	FS_Flush( cls.demoheader );

	// now copy the stuff we cached from the server.
	copysize = savepos = FS_Tell( cls.demoheader );

	FS_Seek( cls.demoheader, 0, SEEK_SET );

	FS_FileCopy( cls.demofile, cls.demoheader, copysize );

	// jump back to end, in case we record another demo for this session.
	FS_Seek( cls.demoheader, savepos, SEEK_SET );

	demo.starttime = CL_GetDemoRecordClock(); // setup the demo starttime
	demo.realstarttime = demo.starttime;
	demo.framecount = 0;
	cls.td_startframe = host.framecount;
	cls.td_lastframe = -1; // get a new message this frame

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

	if( clgame.hInstance )
		clgame.dllFuncs.pfnReset();

	Cbuf_InsertText( "fullupdate\n" );
	Cbuf_Execute();

	return true;
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
=================
DEM_Xash_DemoReadMessage

reads demo data and write it to client
=================
*/
static qboolean DEM_Xash_DemoReadMessage( byte *buffer, size_t *length )
{
	size_t     curpos = 0, lastpos = 0;
	float      fElapsedTime = 0.0f;
	qboolean   swallowmessages = true;
	static int tdlastdemoframe = 0;
	byte       *userbuf = NULL;
	size_t     size = 0;
	byte       cmd;

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
		qboolean bSkipMessage = false;

		if( !cls.demofile )
			break;
		curpos = FS_Tell( cls.demofile );

		if( !CL_ReadDemoCmdHeader( &cmd, &demo.timestamp ))
			return false;

		fElapsedTime = CL_GetDemoPlaybackClock() - demo.starttime;
		if( !cls.timedemo )
			bSkipMessage = (( demo.timestamp - cl_serverframetime()) >= fElapsedTime ) ? true : false;
		if( cls.changelevel )
			demo.framecount = 1;

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
	}
	while( swallowmessages );

	// If we are playing back a timedemo, and we've already passed on a
	//  frame update for this host_frame tag, then we'll just skip this message.
	if( cls.timedemo && ( tdlastdemoframe == host.framecount ))
	{
		FS_Seek( cls.demofile, FS_Tell( cls.demofile ) - 5, SEEK_SET );
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

static double DEM_Xash_GetHostFPS( void )
{
	return demo.header.host_fps;
}

static void DEM_Xash_ResetHandler( void )
{
	demo.framecount = 0;
	demo.angle_position = 1;
}

static qboolean DEM_Xash_StopPlayback( file_t *file )
{
	DEM_Xash_ResetHandler();
	return true;
}

static const demo_handler_t Xash_DemoHandler = {
	"xash3d",
	{
		DEM_Xash_StartRecord,
		DEM_Xash_StopRecord,
		DEM_Xash_StartPlayback,
		DEM_Xash_StopPlayback,
		DEM_Xash_ListDemo,
		DEM_Xash_CanHandle,
		DEM_Xash_ResetHandler,
		NULL,
		NULL,
		NULL,
		DEM_Xash_WriteDemoMessage,
		NULL,
		NULL,
		DEM_Xash_WriteDemoUserCmd,
		DEM_Xash_WriteDemoUserMessage,
		DEM_Xash_DemoReadMessage,
		DEM_Xash_GetHostFPS,
		DEM_XASH_DemoInterpolateAngles
	},
	NULL
};

void DEM_Xash_InitHandler( void )
{
	DEM_RegisterHandler( &Xash_DemoHandler );
}
