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
#include "demofile/demofile.h"

// Demo flags
#define FDEMO_TITLE		0x01	// Show title
#define FDEMO_PLAY		0x04	// Playing cd track
#define FDEMO_FADE_IN_SLOW	0x08	// Fade in (slow)
#define FDEMO_FADE_IN_FAST	0x10	// Fade in (fast)
#define FDEMO_FADE_OUT_SLOW	0x20	// Fade out (slow)
#define FDEMO_FADE_OUT_FAST	0x40	// Fade out (fast)

static qboolean CL_NextDemo( void );

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
	DEM_ResetHandler();
}

/*
====================
CL_GetDemoRecordClock

write time while demo is recording
====================
*/
float CL_GetDemoRecordClock( void )
{
	return cl.mtime[0];
}

/*
====================
CL_GetDemoPlaybackClock

overwrite host.realtime
====================
*/
float CL_GetDemoPlaybackClock( void )
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

	return bound( MIN_FPS, DEM_GetHostFPS(), MAX_FPS_HARD);
}

/*
=================
CL_DemoAborted
=================
*/
void CL_DemoAborted( void )
{
	if( cls.demofile )
		FS_Close( cls.demofile );
	cls.demoplayback = false;
	cls.changedemo = false;
	cls.timedemo = false;
	DEM_ResetHandler();
	cls.demofile = NULL;
	cls.demonum = -1;

	Cvar_DirectSet( &v_dark, "0" );
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
	int	frames;

	if( !cls.demorecording ) return;

	DEM_StopRecord(cls.demofile);	

	FS_Close( cls.demofile );
	DEM_ResetHandler();
	cls.demofile = NULL;
	cls.demorecording = false;
	cls.demoname[0] = '\0';
	cls.td_lastframe = host.framecount;
	gameui.globals->demoname[0] = '\0';
	
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
=================
CL_DemoStartPlayback
=================
*/
void CL_DemoStartPlayback( int mode )
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

	CL_SetupNetchanForProtocol( cls.legacymode );

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
CL_DemoReadMessage

reads demo data and write it to client
=================
*/
qboolean CL_DemoReadMessage( byte *buffer, size_t *length )
{
	return DEM_DemoReadMessage(buffer, length);
}


/*
==============
CL_DemoInterpolateAngles

We can predict or inpolate player movement with standed client code
but viewangles interpolate here
==============
*/


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

	DEM_StopPlayback(cls.demofile);
	FS_Close( cls.demofile );
	DEM_ResetHandler();
	cls.demoplayback = false;
	cls.demofile = NULL;

	cls.olddemonum = Q_max( -1, cls.demonum - 1 );
	cls.td_lastframe = host.framecount;

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
CL_WriteDemoHeader

Write demo header
====================
*/
static void CL_WriteDemoHeader(const char* name)
{
	double maxfps;
	int copysize;
	int savepos;
	int curpos;

	Con_Printf("recording to %s.\n", name);
	cls.demofile = FS_Open(name, "wb", false);
	cls.demotime = 0.0;

	if (!cls.demofile)
	{
		Con_Printf(S_ERROR "couldn't open %s.\n", name);
		return;
	}

	DEM_StartRecord(cls.demofile);

	cls.demorecording = true;
	cls.demowaiting = true;	// don't start saving messages until a non-delta compressed message is received

	if (clgame.hInstance) clgame.dllFuncs.pfnReset();

	Cbuf_InsertText("fullupdate\n");
	Cbuf_Execute();
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

	cls.forcetrack = 0;

	if (!DEM_StartPlayback(cls.demofile))
	{
		CL_DemoAborted();
		return;
	}
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

	DEM_ListDemo(f);
	FS_Close( f );
	DEM_ResetHandler();
}

int GAME_EXPORT CL_GetDemoComment(const char* demoname, char* comment)
{
	return 0;
}

void CL_WriteDemoUserCmd(int cmdnumber)
{
	DEM_WriteDemoUserCmd(cmdnumber);
}

void CL_WriteDemoMessage(qboolean startup, int start, sizebuf_t* msg)
{
	DEM_WriteNetPacket(startup, start, msg);
}

void CL_WriteDemoUserMessage(int size, byte* buffer)
{
	DEM_WriteDemoUserMessage(size, buffer);
}

void CL_WriteDemoAnim(int anim, int body)
{
	DEM_WriteAnim(anim, body);
}

void CL_WriteDemoClientData(client_data_t* cdata)
{
	DEM_WriteClientData(cdata);
}

void CL_WriteDemoSound(int channel, const char* sample, float vol, float attenuation, int flags, int pitch)
{
	DEM_WriteSound(channel, sample, vol, attenuation, flags, pitch);
}
void CL_WriteDemoStringCmd(const char* cmd)
{
	DEM_WriteStringCmd(cmd);
}

void CL_WriteDemoEvent(int flags, int idx, float delay, event_args_t* pargs)
{
	DEM_WriteEvent(flags, idx, delay, pargs);
}

void CL_DemoInterpolateAngles(void)
{
	DEM_DemoInterpolateAngles();
}
