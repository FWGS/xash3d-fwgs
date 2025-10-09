/*
demo_gs.c - Goldsrc demo format compatibility for Xash3D
Copyright (C) 2025 Garey

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "demofile.h"

#define GS_DEMO_SIGNATURE_SIZE 8
#define GS_DEMO_HEADER_SIZE    544
#define GS_MIN_DIR_ENTRY_COUNT 1
#define GS_MAX_DIR_ENTRY_COUNT 1024

#define GS_DEMO_STARTUP 0 // this lump contains startup info needed to spawn into the server
#define GS_DEMO_NORMAL  1 // this lump contains playback info of messages, etc., needed during playback.

#define GS_CMD_DEMO_START      2
#define GS_CMD_CONSOLE_COMMAND 3
#define GS_CMD_CLIENT_DATA     4
#define GS_CMD_NEXT_SECTION    5
#define GS_CMD_EVENT           6
#define GS_CMD_WEAPON_ANIM     7
#define GS_CMD_SOUND           8
#define GS_CMD_DEMO_BUFFER     9

struct demo_header_s
{
	char    filestamp[8];
	int     demo_protocol;
	int     net_protocol;
	char    map_name[260];
	char    dll_dir[260];
	CRC32_t map_crc;
	int     directory_offset;
};

typedef struct demo_header_s dem_header_t;

typedef struct
{
	int   entrytype;
	char  description[64];
	int   flags;
	int   cd_track;
	float cd_tracktime;
	int   frame_count;
	int   offset;
	int   file_length;
} dem_entry_t;

typedef struct
{
	dem_entry_t *entries;   // track entry info
	int32_t     numentries; // number of tracks
} dem_directory_t;


// private demo states
static struct
{
	dem_header_t    header;
	dem_entry_t     *entry;
	dem_directory_t directory;
	int    framecount;
	float  starttime;
	float  realstarttime;
	float  timestamp;
	float  lasttime;
	int    entryIndex;
	double fps;
	size_t size;
} demo;

typedef struct demo_anim_s demo_anim_t;

static void DEM_GS_WriteDemoCmdHeader( byte cmd, file_t *file )
{
	float dt;
	int   frame;

	Assert( cmd >= 1 && cmd <= dem_lastcmd );
	if( !file )
		return;

	// command
	FS_Write( file, &cmd, sizeof( byte ));

	// time offset
	dt = (float)( CL_GetDemoRecordClock() - demo.starttime );
	FS_Write( file, &dt, sizeof( float ));

	// current frame
	frame = demo.framecount;
	FS_Write( file, &frame, sizeof( int ));
}

static qboolean DEM_GS_ReadDemoCmdHeader( byte *cmd, float *dt, int *frame_number )
{
	FS_Read( cls.demofile, cmd, sizeof( byte ));

	// read the timestamp
	FS_Read( cls.demofile, dt, sizeof( float ));

	// read frmae number
	FS_Read( cls.demofile, frame_number, sizeof( int ));
	return true;
}

static void DEM_GS_ReadClientData( void )
{
	client_data_t cdat;
	file_t *file = cls.demofile;
	float  time;
	if( !file )
		return;

	FS_Read( file, &cdat, sizeof( client_data_t ));

	if( clgame.dllFuncs.pfnUpdateClientData )
	{
		if( clgame.dllFuncs.pfnUpdateClientData( &cdat, cl.time ))
		{
			if( !cls.spectator )
			{
				VectorCopy( cdat.viewangles, cl.viewangles );
				cl.local.scr_fov = cdat.fov;
			}
		}
	}
}

static void DEM_GS_WriteClientData( client_data_t *cdata )
{
	file_t *file = cls.demofile;
	if( !file )
		return;

	DEM_GS_WriteDemoCmdHeader( GS_CMD_CLIENT_DATA, file );
	FS_Write( file, cdata->origin, sizeof( float[3] ));
	FS_Write( file, cdata->viewangles, sizeof( float[3] ));
	FS_Write( file, &cdata->iWeaponBits, sizeof( int ));
	FS_Write( file, &cdata->fov, sizeof( float ));
}

static void DEM_GS_WriteAnim( int anim, int body )
{
	file_t *file = cls.demofile;
	if( !file )
		return;

	DEM_GS_WriteDemoCmdHeader( GS_CMD_WEAPON_ANIM, file );
	FS_Write( file, &anim, sizeof( int ));
	FS_Write( file, &body, sizeof( int ));
}

static void DEM_GS_ReadAnim( void )
{
	int    anim, body;
	file_t *file = cls.demofile;
	if( !file )
		return;

	FS_Read( file, &anim, sizeof( int ));
	FS_Read( file, &body, sizeof( int ));

	CL_WeaponAnim( anim, body );
}

static void DEM_GS_WriteEvent( int flags, int idx, float delay, event_args_t *pargs )
{
	file_t *file = cls.demofile;
	if( !file )
		return;

	DEM_GS_WriteDemoCmdHeader( GS_CMD_EVENT, file );
	FS_Write( file, &flags, sizeof( int ));
	FS_Write( file, &idx, sizeof( int ));
	FS_Write( file, &delay, sizeof( float ));
	FS_Write( file, pargs, sizeof( event_args_t ));
}

static void DEM_GS_WriteNetPacket( qboolean startup, int start, sizebuf_t *msg )
{
	file_t *file = startup ? cls.demoheader : cls.demofile;
	int    swlen;
	byte   c;

	if( !file )
		return;

	swlen = MSG_GetNumBytesWritten( msg ) - start;
	if( swlen <= 0 )
		return;

	ref_params_t *rp = V_RefParams();
	DEM_GS_WriteDemoCmdHeader( startup ? 0 : 1, file );
	// Write timestamp
	float dt = (float)( CL_GetDemoRecordClock() - demo.starttime );
	FS_Write( file, &dt, sizeof( float ));
	// Write rp
	FS_Write( file, rp, offsetof( ref_params_t, cmd ));
	int nothing = 0;
	// Write cmd and movevars pointers as zeroed '4-byte' int
	FS_Write( file, &nothing, sizeof( int ));
	FS_Write( file, &nothing, sizeof( int ));

	FS_Write( file, rp->viewport, sizeof( int[4] ));
	FS_Write( file, &rp->nextView, sizeof( int ));
	FS_Write( file, &rp->onlyClientDraw, sizeof( int ));

	if( !rp->movevars )
		rp->movevars = &clgame.movevars;
	if( !rp->cmd )
		rp->cmd = &cl.cmd;

	// Write actual cmd and movevars
	FS_Write( file, rp->cmd, sizeof( usercmd_t ));
	FS_Write( file, rp->movevars, offsetof( movevars_t, features ));

	// TODO: Idk is this right coordinates
	FS_Write( file, &cl.local.lastorigin, sizeof( vec3_t ));

	FS_Write( file, &cl.local.viewmodel, sizeof( int ));

	// Write net sequences
	FS_Write( file, &cls.netchan.incoming_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.incoming_acknowledged, sizeof( int ));
	FS_Write( file, &cls.netchan.incoming_reliable_acknowledged, sizeof( int ));
	FS_Write( file, &cls.netchan.incoming_reliable_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.outgoing_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.reliable_sequence, sizeof( int ));
	FS_Write( file, &cls.netchan.last_reliable_sequence, sizeof( int ));

	// Write the length out.
	FS_Write( file, &swlen, sizeof( int ));

	// Output the buffer. Skip the network packet stuff.
	FS_Write( file, MSG_GetData( msg ) + start, swlen );
}

static void DEM_GS_ReadNetPacket( byte *buffer, size_t *length )
{
	float  timestamp;

	file_t *file = cls.demofile;
	if( !file )
		return;

	FS_Read( file, &timestamp, sizeof( int ));
	ref_params_t *rp = V_RefParams();
	if( !rp->movevars )
		rp->movevars = &clgame.movevars;
	if( !rp->cmd )
		rp->cmd = &cl.cmd;
	FS_Read( file, rp, offsetof( ref_params_t, cmd ));

	if( rp->frametime )
	{
		demo.fps = 1.0 / rp->frametime;
	}

	// Skip usercmd and movevars pointers
	FS_Seek( file, 8, SEEK_CUR );

	FS_Read( file, rp->viewport, sizeof( int[4] ));
	FS_Read( file, &rp->nextView, sizeof( int ));
	FS_Read( file, &rp->onlyClientDraw, sizeof( int ));


	FS_Read( file, rp->cmd, sizeof( usercmd_t ));
	FS_Read( file, rp->movevars, offsetof( movevars_t, features ));

	FS_Read( file, &cl.local.lastorigin, sizeof( vec3_t ));

	FS_Read( file, &cl.local.viewmodel, sizeof( int ));

	FS_Read( file, &cls.netchan.incoming_sequence, sizeof( int ));
	FS_Read( file, &cls.netchan.incoming_acknowledged, sizeof( int ));
	FS_Read( file, &cls.netchan.incoming_reliable_acknowledged, sizeof( int ));
	FS_Read( file, &cls.netchan.incoming_reliable_sequence, sizeof( int ));
	FS_Read( file, &cls.netchan.outgoing_sequence, sizeof( int ));
	FS_Read( file, &cls.netchan.reliable_sequence, sizeof( int ));
	FS_Read( file, &cls.netchan.last_reliable_sequence, sizeof( int ));

	int msglen = 0;

	*length = 0; // assume we fail
	FS_Read( cls.demofile, &msglen, sizeof( int ));

	if( msglen < 0 )
	{
		Con_Reportf( S_ERROR "Demo message length < 0\n" );
		CL_DemoCompleted();
		return;
	}

	if( msglen > MAX_INIT_MSG )
	{
		Con_Reportf( S_ERROR "Demo message %i > %i\n", msglen, MAX_INIT_MSG );
		CL_DemoCompleted();
	}

	if( msglen > 0 )
	{
		if( FS_Read( cls.demofile, buffer, msglen ) != msglen )
		{
			Con_Reportf( S_ERROR "Error reading demo message data\n" );
			CL_DemoCompleted();
		}
	}

	cls.netchan.last_received = host.realtime;
	cls.netchan.total_received += msglen;
	*length = msglen;

	if( cls.state != ca_active )
		Cbuf_Execute();

	return;
}

static void DEM_GS_WriteSound( int channel, const char *sample, float vol, float attenuation, int flags, int pitch )
{
	size_t len;
	file_t *file = cls.demofile;
	if( !file )
		return;

	DEM_GS_WriteDemoCmdHeader( GS_CMD_SOUND, file );

	FS_Write( file, &channel, sizeof( int ));
	len = Q_strlen( sample );
	FS_Write( file, &len, sizeof( int ));
	FS_Write( file, sample, len );
	FS_Write( file, &vol, sizeof( float ));
	FS_Write( file, &attenuation, sizeof( float ));
	FS_Write( file, &flags, sizeof( int ));
	FS_Write( file, &pitch, sizeof( int ));
}

static void DEM_GS_DemoPlaySound( int chan, char *sample, float attn, float volume, int flags, int pitch )
{
	S_StartSound( NULL, clgame.pmove->player_index + 1, chan, S_RegisterSound( sample ), volume, attn, pitch, flags );
}

static void DEM_GS_ReadSound( void )
{
	int    channel, flags, pitch;
	float  vol, attenuation;
	char   sample[256];
	int    len;
	file_t *file = cls.demofile;
	if( !file )
		return;

	FS_Read( file, &channel, sizeof( int ));
	FS_Read( file, &len, sizeof( int ));
	if( len >= 255 )
	{
		len = 255;
	}
	FS_Read( file, sample, len );
	sample[len] = 0;
	FS_Read( file, &attenuation, sizeof( float ));
	FS_Read( file, &vol, sizeof( float ));
	FS_Read( file, &flags, sizeof( int ));
	FS_Read( file, &pitch, sizeof( int ));

	DEM_GS_DemoPlaySound( channel, sample, attenuation, vol, flags, pitch );

}

static void DEM_GS_WriteStringCMD( const char *cmdname )
{
	char   command[64];
	size_t len;
	file_t *file = cls.demofile;
	if( !file )
		return;

	DEM_GS_WriteDemoCmdHeader( GS_CMD_CONSOLE_COMMAND, file );
	memset( command, 0, 64 );
	Q_strncpy( command, cmdname, 63 );
	command[63] = 0;
	FS_Write( file, cmdname, 64 );
}

static void DEM_GS_ReadHeader( file_t *file )
{
	FS_Seek( file, GS_DEMO_SIGNATURE_SIZE, SEEK_SET );

	FS_Read( file, &demo.header.demo_protocol, sizeof( int ));
	FS_Read( file, &demo.header.net_protocol, sizeof( int ));

	FS_Read( file, &demo.header.map_name, sizeof( demo.header.map_name ));
	FS_Read( file, demo.header.dll_dir, sizeof( demo.header.dll_dir ));

	FS_Read( file, &demo.header.map_crc, sizeof( int ));
	FS_Read( file, &demo.header.directory_offset, sizeof( int ));

	demo.fps = MAX_FPS_HARD;
}

static void DEM_GS_ReadDirectory( file_t *file )
{
	int i, file_mark, dir_entries_count;

	if( demo.header.directory_offset < 0
	    || ( demo.size - 4u ) < demo.header.directory_offset )
	{
		Con_Printf( "Malformed directory offset in demofile.\n" );
		return;
	}

	file_mark = FS_Tell( file );
	FS_Seek( file, demo.header.directory_offset, SEEK_SET );

	dir_entries_count;
	FS_Read( file, &dir_entries_count, sizeof( int ));

	if( dir_entries_count < GS_MIN_DIR_ENTRY_COUNT
	    || dir_entries_count > GS_MAX_DIR_ENTRY_COUNT
	    || ((demo.size - ( dir_entries_count * sizeof( dem_entry_t ))) < FS_Tell( file )))
	{
		// Case for bogus demo (seems like client crashed or somehow doesn't writted directories entries)
		// But in most cases we can still playback this demo.
		dir_entries_count = 1;
		demo.header.directory_offset = 0;
		demo.directory.numentries = dir_entries_count;
		demo.directory.entries = Mem_Malloc( cls.mempool, sizeof( *demo.directory.entries ) * demo.directory.numentries );
		demo.directory.entries[0].offset = file_mark;
		return;
	}

	demo.directory.numentries = dir_entries_count;

	demo.directory.entries = Mem_Malloc( cls.mempool, sizeof( *demo.directory.entries ) * demo.directory.numentries );

	for( i = 0; i < demo.directory.numentries; i++ )
	{
		dem_entry_t *entry = &demo.directory.entries[i];
		if( FS_Read( cls.demofile, entry, sizeof( *entry )) != sizeof( *entry ))
		{
			Con_Printf( S_ERROR "demo entry %i corrupted", i );
			// CL_DemoAborted();
			return;
		}
	}
	demo.entryIndex = 0;
	demo.entry = &demo.directory.entries[demo.entryIndex];

	FS_Seek( cls.demofile, demo.entry->offset, SEEK_SET );
}

static qboolean DEM_GS_CanHandle( file_t *file )
{
	FS_Seek( file, 0, SEEK_END );

	demo.size = FS_Tell( file );

	if(demo.size < GS_DEMO_HEADER_SIZE )
	{
		Con_Printf( "Invalid demo file (the size is too small)." );
		return false;
	}

	FS_Seek( file, 0, SEEK_SET );

	char signature[GS_DEMO_SIGNATURE_SIZE];
	FS_Read( file, signature, GS_DEMO_SIGNATURE_SIZE );
	if( Q_strcmp( signature, "HLDEMO" ))
	{
		return false;
	}
	return true;
}

static qboolean DEM_GS_ReadDemo( file_t *file )
{
	if( !DEM_GS_CanHandle( file ))
		return false;

	DEM_GS_ReadHeader( file );
	DEM_GS_ReadDirectory( file );

	return true;
}

static void DEM_GS_ReadStringCMD( void )
{
	char cmd[64];
	FS_Read( cls.demofile, cmd, 64 );
	cmd[63] = 0;

	// TODO: Validate CMD
	Cbuf_AddFilteredText( cmd );
	Cbuf_AddFilteredText( "\n" );
}

static void DEM_GS_DemoMoveToNextSection( void )
{
	if( ++demo.entryIndex >= demo.directory.numentries )
	{
		// done
		CL_DemoCompleted();
		return;
	}

	// switch to next section, we got a dem_stop
	demo.entry = &demo.directory.entries[demo.entryIndex];

	// ready to continue reading, reset clock.
	FS_Seek( cls.demofile, demo.entry->offset, SEEK_SET );

	// time is now relative to this chunk's clock.
	demo.starttime = CL_GetDemoPlaybackClock();
	demo.framecount = 0;
}

static void DEM_GS_ReadEvent( void )
{
	int    flags, idx, delay;
	event_args_t args;
	file_t *file = cls.demofile;
	if( !file )
		return;

	FS_Read( file, &flags, sizeof( int ));
	FS_Read( file, &idx, sizeof( int ));
	FS_Read( file, &delay, sizeof( int ));
	FS_Read( file, &args, sizeof( event_args_t ));

	CL_QueueEvent( flags, idx, delay, &args );
}

static void DEM_GS_ReadClientDLLData( void )
{
	static byte buffer[0x8000];
	int len;
	file_t      *file = cls.demofile;
	if( !file )
		return;
	FS_Read( file, &len, sizeof( int ));
	if( len >= 0x8000 )
	{
		len = 0x8000;
	}
	if( !clgame.dllFuncs.pfnDemo_ReadBuffer )
	{
		FS_Seek( file, len, SEEK_CUR );
		return;
	}

	FS_Read( file, buffer, len );
	clgame.dllFuncs.pfnDemo_ReadBuffer( len, buffer );
}

static qboolean DEM_GS_ReadDemoMessage( byte *buffer, size_t *length )
{
	size_t     curpos = 0;
	float      fElapsedTime = 0.0f;
	qboolean   swallowmessages = true;
	static int tdlastdemoframe = 0;
	byte       cmd;

	if ( !cls.demofile )
	{
		CL_DemoCompleted();
		return false;
	}

	if ( (!cl.background && ( cl.paused || cls.key_dest != key_game )) || cls.key_dest == key_console )
	{
		demo.starttime += host.frametime;
		return false; // paused
	}

	do
	{
		qboolean bSkipMessage = false;
		int      frame_num;

		if( !cls.demofile )
			break;

		curpos = FS_Tell( cls.demofile );

		if( !DEM_GS_ReadDemoCmdHeader( &cmd, &demo.timestamp, &frame_num ))
			return false;

		fElapsedTime = CL_GetDemoPlaybackClock() - demo.starttime;

		if( !cls.timedemo )
			bSkipMessage = ( demo.timestamp > fElapsedTime );

		// changelevel issues
		if( demo.framecount <= 2 && ( fElapsedTime - demo.timestamp) > host.frametime )
			demo.starttime = CL_GetDemoPlaybackClock();

		// not ready for a message yet, put it back on the file.
		if( cmd != GS_CMD_DEMO_START && cmd != GS_CMD_NEXT_SECTION && bSkipMessage )
		{
			// never skip first message
			if( demo.framecount != 0 )
			{
				FS_Seek( cls.demofile, curpos, SEEK_SET );
				return false; // not time yet.
			}
		}

		switch( cmd )
		{
		case GS_CMD_DEMO_START:
			break;
		case GS_CMD_CONSOLE_COMMAND:
			DEM_GS_ReadStringCMD();
			break;
		case GS_CMD_CLIENT_DATA:
			DEM_GS_ReadClientData();
			break;
		case GS_CMD_NEXT_SECTION:
			DEM_GS_DemoMoveToNextSection();
			break;
		case GS_CMD_EVENT:
			DEM_GS_ReadEvent();
			break;
		case GS_CMD_WEAPON_ANIM:
			DEM_GS_ReadAnim();
			break;
		case GS_CMD_SOUND:
			DEM_GS_ReadSound();
			break;
		case GS_CMD_DEMO_BUFFER:
			DEM_GS_ReadClientDLLData();
			break;
		default:
			swallowmessages = false;
			DEM_GS_ReadNetPacket( buffer, length );
			break;
		}

	}
	while( swallowmessages );


	// If we are playing back a timedemo, and we've already passed on a
	//  frame update for this host_frame tag, then we'll just skip this message.
	if ( cls.timedemo && ( tdlastdemoframe == host.framecount ))
	{
		FS_Seek(cls.demofile, curpos, SEEK_SET );
		return false;
	}
	tdlastdemoframe = host.framecount;

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

	return true;
}

static qboolean DEM_GS_StartRecord( file_t *file )
{
	double      maxfps;
	int         copysize;
	int         savepos;
	int         curpos;

	static char hl_signature[] = { 'H', 'L', 'D', 'E', 'M', 'O', '\0', '\0' };
	cls.demorecording = true;
	cls.demowaiting = true;

	maxfps = fps_override.value ? MAX_FPS_HARD : MAX_FPS_SOFT;

	memset( &demo.header, 0, sizeof( demo.header ));

	memcpy( demo.header.filestamp, hl_signature, sizeof( hl_signature ));

	demo.fps = host_maxfps.value ? bound( MIN_FPS, host_maxfps.value, maxfps ) : maxfps;


	demo.header.demo_protocol = 5;

	if( cls.legacymode == PROTO_CURRENT )
		demo.header.net_protocol = 49;
	else
		demo.header.net_protocol = 48;

	demo.header.directory_offset = 0;

	Q_strncpy( demo.header.map_name, clgame.mapname, sizeof( demo.header.map_name ));
	Q_strncpy( demo.header.dll_dir, FS_Gamedir(), sizeof( demo.header.dll_dir ));

	FS_Write( file, &demo.header, sizeof( demo.header ));

	memset( &demo.directory, 0, sizeof( demo.directory ));
	demo.directory.numentries = 2;
	demo.directory.entries = Mem_Calloc( cls.mempool, sizeof( dem_entry_t ) * demo.directory.numentries );

	demo.entry = &demo.directory.entries[0]; // only one here.
	Q_strncpy( demo.entry->description, "LOADING", sizeof( demo.entry->description ));
	demo.entry->entrytype = GS_DEMO_STARTUP;
	demo.entry->flags = 0;
	demo.entry->cd_track = -1;
	demo.entry->cd_tracktime = 0.0;
	cls.demowaiting = true;
	demo.entry->offset = FS_Tell( file );
	cls.demorecording = true;

	DEM_GS_WriteDemoCmdHeader( 5, cls.demoheader );
	FS_Flush( cls.demoheader );

	copysize = savepos = FS_Tell( cls.demoheader );
	FS_Seek( cls.demoheader, 0, SEEK_SET );

	FS_FileCopy( cls.demofile, cls.demoheader, copysize );

	FS_Seek( cls.demoheader, savepos, SEEK_SET );


	demo.starttime = CL_GetDemoRecordClock(); // setup the demo starttime
	demo.realstarttime = demo.starttime;
	demo.framecount = 0;
	cls.td_startframe = host.framecount;
	cls.td_lastframe = -1; // get a new message this frame

	// now move on to entry # 1, the first data chunk.
	curpos = FS_Tell( cls.demofile );
	demo.entry->file_length = curpos - demo.entry->offset;

	// now we are writing the first real lump.
	demo.entry = &demo.directory.entries[1]; // first real data lump
	Q_strncpy( demo.entry->description, "PLAYBACK", sizeof( demo.entry->description ));
	demo.entry->entrytype = GS_DEMO_NORMAL;
	demo.entry->cd_track = -1;
	demo.entry->cd_tracktime = 0.0;

	demo.entry->offset = FS_Tell( cls.demofile );

	DEM_GS_WriteDemoCmdHeader( 2, cls.demofile );

	if( clgame.hInstance )
		clgame.dllFuncs.pfnReset();

	Cbuf_InsertText( "fullupdate\n" );
	Cbuf_Execute();

	return true;
}

static qboolean DEM_GS_StopRecord( file_t *file )
{
	int   i, curpos;
	float stoptime;
	int   frames;

	if( !cls.demorecording )
	{
		return false;
	}

	// demo playback should read this as an incoming message.
	DEM_GS_WriteDemoCmdHeader( GS_CMD_NEXT_SECTION, file );

	stoptime = CL_GetDemoRecordClock();
	if( clgame.hInstance )
		clgame.dllFuncs.pfnReset();

	curpos = FS_Tell( file );
	demo.entry->file_length = curpos - demo.entry->offset;

	//  Now write out the directory and free it and touch up the demo header.
	FS_Write( file, &demo.directory.numentries, sizeof( int ));

	for( i = 0; i < demo.directory.numentries; i++ )
		FS_Write( file, &demo.directory.entries[i], sizeof( dem_entry_t ));

	Mem_Free( demo.directory.entries );
	demo.directory.numentries = 0;

	demo.header.directory_offset = curpos;
	FS_Seek( file, 0, SEEK_SET );
	FS_Write( file, &demo.header, sizeof( demo.header ));

	return true;

}

static qboolean DEM_GS_StartPlayback( file_t *file )
{
	if( !DEM_GS_ReadDemo( file ))
	{
		return false;
	}

	CL_DemoStartPlayback( DEMO_GOLDSRC );

	if( demo.header.net_protocol == 49 )
		cls.legacymode = PROTO_CURRENT;
	else
		cls.legacymode = PROTO_GOLDSRC;

	return true;
}

static void DEM_GS_ResetHandler( void )
{
	demo.framecount = 0;
	demo.starttime = 0;
}

static qboolean DEM_GS_StopPlayback( file_t *file )
{
	DEM_GS_ResetHandler();
	return true;
}

static void DEM_GS_WriteDemoUserMessage( int size, byte *buffer )
{
	if( !cls.demorecording || cls.demowaiting )
		return;

	if( !cls.demofile || !buffer || size <= 0 )
		return;

	DEM_GS_WriteDemoCmdHeader( GS_CMD_DEMO_BUFFER, cls.demofile );

	// write the length out.
	FS_Write( cls.demofile, &size, sizeof( int ));

	// output the buffer.
	FS_Write( cls.demofile, buffer, size );
}

static double DEM_GS_GetHostFPS( void )
{
	return demo.fps;
}

static demo_handler_t GS_DemoHandler = {
	"goldsource",
	{
		DEM_GS_StartRecord,
		DEM_GS_StopRecord,
		DEM_GS_StartPlayback,
		DEM_GS_StopPlayback,
		NULL, // todo ListDemo
		DEM_GS_CanHandle,
		DEM_GS_ResetHandler,
		DEM_GS_WriteAnim,
		DEM_GS_WriteClientData,
		DEM_GS_WriteEvent,
		DEM_GS_WriteNetPacket,
		DEM_GS_WriteSound,
		DEM_GS_WriteStringCMD,
		NULL,
		DEM_GS_WriteDemoUserMessage,
		DEM_GS_ReadDemoMessage,
		DEM_GS_GetHostFPS,
		NULL
	},
	NULL
};

void DEM_GS_InitHandler( void )
{
	DEM_RegisterHandler( &GS_DemoHandler );
}
