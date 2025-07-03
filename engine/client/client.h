/*
client.h - primary header for client
Copyright (C) 2009 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef CLIENT_H
#define CLIENT_H

#include "xash3d_types.h"
#include "xash3d_mathlib.h"
#include "cdll_int.h"
#include "menu_int.h"
#include "cl_entity.h"
#include "mod_local.h"
#include "pm_defs.h"
#include "pm_movevars.h"
#include "ref_params.h"
#include "render_api.h"
#include "cdll_exp.h"
#include "screenfade.h"
#include "protocol.h"
#include "netchan.h"
#include "net_api.h"
#include "world.h"
#include "ref_common.h"
#include "voice.h"

// client sprite types
#define SPR_CLIENT		0	// client sprite for temp-entities or user-textures
#define SPR_HUDSPRITE	1	// hud sprite
#define SPR_MAPSPRITE	2	// contain overview.bmp that diced into frames 128x128

//=============================================================================
typedef struct netbandwithgraph_s
{
	word		client;
	word		players;
	word		entities;		// entities bytes, except for players
	word		tentities;	// temp entities
	word		sound;
	word		event;
	word		usr;
	word		msgbytes;
	word		voicebytes;
} netbandwidthgraph_t;

typedef struct frame_s
{
	// received from server
	double		receivedtime;	// time message was received, or -1
	double		latency;
	double		time;		// server timestamp
	qboolean		valid;		// cleared if delta parsing was invalid
	qboolean		choked;

	clientdata_t	clientdata;	// local client private data
	entity_state_t	playerstate[MAX_CLIENTS];
	weapon_data_t	weapondata[MAX_LOCAL_WEAPONS];
	netbandwidthgraph_t graphdata;
	byte		flags[MAX_VISIBLE_PACKET_VIS_BYTES];
	int		num_entities;
	int		first_entity;	// into the circular cl_packet_entities[]
} frame_t;

typedef struct runcmd_s
{
	double		senttime;
	double		receivedtime;
	float		frame_lerp;

	usercmd_t		cmd;

	qboolean		processedfuncs;
	qboolean		heldback;
	int		sendsize;
} runcmd_t;

// add angles
typedef struct
{
	float		starttime;
	float		total;
} pred_viewangle_t;

#define ANGLE_BACKUP	16
#define ANGLE_MASK		(ANGLE_BACKUP - 1)

#define CL_UPDATE_MASK	(CL_UPDATE_BACKUP - 1)
#if XASH_LOW_MEMORY == 2
#define CL_UPDATE_BACKUP SINGLEPLAYER_BACKUP
#else
extern int CL_UPDATE_BACKUP;
#endif

#define SIGNONS		2		// signon messages to receive before connected
#define INVALID_HANDLE	0xFFFF		// for XashXT cache system

#define MIN_UPDATERATE	10.0f
#define MAX_UPDATERATE	102.0f

#define MAX_EX_INTERP	0.1f

#define CL_MIN_RESEND_TIME	1.5f		// mininum time gap (in seconds) before a subsequent connection request is sent.
#define CL_MAX_RESEND_TIME	20.0f		// max time.  The cvar cl_resend is bounded by these.

#define cl_serverframetime()	(cl.mtime[0] - cl.mtime[1])
#define cl_clientframetime()	(cl.time - cl.oldtime)

typedef struct
{
	// got from prediction system
	vec3_t		predicted_origins[CMD_BACKUP];
	vec3_t		prediction_error;
	vec3_t		lastorigin;
	int		lastground;

	// interp info
	float		interp_amount;

	// misc local info
	qboolean		repredicting;	// repredicting in progress
	qboolean		apply_effects;	// local player will not added but we should apply their effects: flashlight etc
	float		idealpitch;
	int		viewmodel;
	int		health;		// client health
	int		onground;
	int		light_level;
	int		waterlevel;
	int		usehull;
	qboolean	moving;
	int		pushmsec;
	int		weapons;
	float		maxspeed;
	float		scr_fov;

	// weapon predict stuff
	int		weaponsequence;
	float		weaponstarttime;
} cl_local_data_t;

typedef struct
{
	qboolean		bUsed;
	float		fTime;
	int		nBytesRemaining;
} downloadtime_t;

typedef struct
{
	qboolean		doneregistering;
	int		percent;
	qboolean		downloadrequested;
	downloadtime_t	rgStats[8];
	int		nCurStat;
	int		nTotalSize;
	int		nTotalToTransfer;
	int		nRemainingToTransfer;
	float		fLastStatusUpdate;
	qboolean		custom;
} incomingtransfer_t;

// the client_t structure is wiped completely
// at every server map change
typedef struct
{
	// ==== shared through RefAPI's ref_client_t ====
	double		time;			// this is the time value that the client
						// is rendering at.  always <= cls.realtime
						// a lerp point for other data
	double		oldtime;			// previous cl.time, time-oldtime is used
						// to decay light values and smooth step ups
	int		viewentity;

	// server state information
	int		playernum;
	int		maxclients;

	int		nummodels;
	model_t		*models[MAX_MODELS+1];		// precached models (plus sentinel slot)

	qboolean	paused;

	vec3_t		simorg;			// predicted origin
	// ==== shared through RefAPI's ref_client_t ===

	int		servercount;		// server identification for prespawns
	int		validsequence;		// this is the sequence number of the last good
						// world snapshot/update we got.  If this is 0, we can't
						// render a frame yet
	int		parsecount;		// server message counter
	int		parsecountmod;		// modulo with network window

	qboolean		video_prepped;		// false if on new level or new ref dll
	qboolean		audio_prepped;		// false if on new level or new snd dll

	int		delta_sequence;		// acknowledged sequence number

	double		mtime[2];			// the timestamp of the last two messages
	float		lerpFrac;

	int		last_command_ack;
	int		last_incoming_sequence;

	qboolean		background;		// not real game, just a background
	qboolean		first_frame;		// first rendering frame
	qboolean		proxy_redirect;		// spectator stuff
	qboolean		skip_interp;		// skip interpolation this frame

	uint		checksum;			// for catching cheater maps

	frame_t		frames[MULTIPLAYER_BACKUP];		// alloced on svc_serverdata
	runcmd_t		commands[MULTIPLAYER_BACKUP];		// each mesage will send several old cmds
	local_state_t	predicted_frames[MULTIPLAYER_BACKUP];	// local client state

	double		timedelta;		// floating delta between two updates

	char		serverinfo[MAX_SERVERINFO_STRING];
	player_info_t	players[MAX_CLIENTS];	// collected info about all other players include himself
	double		lastresourcecheck;
	qboolean		http_download;
	event_state_t	events;

	// predicting stuff but not only...
	cl_local_data_t	local;

	// player final info
	usercmd_t	cmd;			// cl.commands[outgoing_sequence].cmd
	vec3_t		viewangles;
	vec3_t		viewheight;
	vec3_t		punchangle;

	int		intermission;		// don't change view angle, full screen, et
	vec3_t		crosshairangle;

	pred_viewangle_t	predicted_angle[ANGLE_BACKUP];// accumulate angles from server
	int		angle_position;
	float		addangletotal;
	float		prevaddangletotal;

	// predicted velocity
	vec3_t		simvel;

	entity_state_t	instanced_baseline[MAX_CUSTOM_BASELINES];
	int		instanced_baseline_count;

	char		sound_precache[MAX_SOUNDS][MAX_QPATH];
	char		event_precache[MAX_EVENTS][MAX_QPATH];
	char		files_precache[MAX_CUSTOM][MAX_QPATH];
	lightstyle_t	lightstyles[MAX_LIGHTSTYLES];
	int		numfiles;

	consistency_t	consistency_list[MAX_MODELS];
	int		num_consistency;

	qboolean		need_force_consistency_response;
	resource_t	resourcesonhand;
	resource_t	resourcesneeded;
	resource_t	resourcelist[MAX_RESOURCES];
	int		num_resources;

	short		sound_index[MAX_SOUNDS];
	short		decal_index[MAX_DECALS];

	model_t		*worldmodel;			// pointer to world

	int lostpackets;					// count lost packets and show dialog in menu

	double frametime_remainder;

	uint worldmapCRC;
} client_t;

/*
==================================================================

the client_static_t structure is persistant through an arbitrary number
of server connections

==================================================================
*/
typedef enum
{
	scrshot_inactive,
	scrshot_normal,	// in-game screenshot
	scrshot_snapshot,	// in-game snapshot
	scrshot_plaque,  	// levelshot
	scrshot_savegame,	// saveshot
	scrshot_envshot,	// cubemap view
	scrshot_skyshot,	// skybox view
	scrshot_mapshot	// overview layer
} scrshot_t;

// client screen state
typedef enum
{
	CL_LOADING = 1,	// draw loading progress-bar
	CL_ACTIVE,	// draw normal hud
	CL_PAUSED,	// pause when active
	CL_CHANGELEVEL,	// draw 'loading' during changelevel
} scrstate_t;

typedef struct
{
	char		name[32];
	int		number;	// svc_ number
	int		size;	// if size == -1, size come from first byte after svcnum
	pfnUserMsgHook	func;	// user-defined function
} cl_user_message_t;

typedef void (*pfnEventHook)( event_args_t *args );

typedef struct
{
	char		name[MAX_QPATH];
	word		index;	// event index
	pfnEventHook	func;	// user-defined function
} cl_user_event_t;

#define FONT_FIXED      0
#define FONT_VARIABLE   1

#define FONT_DRAW_HUD      BIT( 0 ) // pass to drawing function to apply hud_scale
#define FONT_DRAW_UTF8     BIT( 1 ) // call UtfProcessChar
#define FONT_DRAW_FORCECOL BIT( 2 ) // ignore colorcodes
#define FONT_DRAW_NORENDERMODE BIT( 3 ) // ignore font's default rendermode
#define FONT_DRAW_NOLF     BIT( 4 ) // ignore \n
#define FONT_DRAW_RESETCOLORONLF BIT( 5 ) // yet another flag to simulate consecutive Con_DrawString calls...

typedef struct
{
	int      hFontTexture;    // handle to texture
	wrect_t  fontRc[256];     // tex coords
	float    scale;           // scale factor
	byte     charWidths[256]; // scaled widths
	int      charHeight;      // scaled height
	int      type;            // fixed width font or variable
	convar_t *rendermode;     // user-defined default rendermode
	qboolean	valid;           // all rectangles are valid
} cl_font_t;

typedef struct scissor_state_s
{
	int x;
	int y;
	int width;
	int height;
	qboolean test;
} scissor_state_t;

typedef struct
{
	// scissor test
	scissor_state_t scissor;

	// temp handle
	const model_t	*pSprite;			// pointer to current SpriteTexture

	int		renderMode;		// override kRenderMode from TriAPI
	TRICULLSTYLE	cullMode;			// override CULL FACE from TriAPI

	// holds text color
	rgba_t		textColor;
	rgba_t		spriteColor;
	vec4_t		triRGBA;

	// crosshair members
	const model_t	*pCrosshair;
	wrect_t		rcCrosshair;
	rgba_t		rgbaCrosshair;
} client_draw_t;

typedef struct cl_predicted_player_s
{
	int		movetype;
	int		solid;
	int		usehull;
	qboolean		active;
	vec3_t		origin;		// interpolated origin
	vec3_t		angles;
} predicted_player_t;

typedef struct
{
	// scissor test
	scissor_state_t scissor;

	int		gl_texturenum;	// this is a real texnum

	// holds text color
	rgba_t		textColor;
} gameui_draw_t;

typedef struct
{
	char		szListName[MAX_QPATH];
	client_sprite_t	*pList;
	int		count;
} cached_spritelist_t;

typedef struct
{
	// centerprint stuff
	float		time;
	int		y, lines;
	char		message[2048];
	int		totalWidth;
	int		totalHeight;
} center_print_t;

typedef struct
{
	float		time;
	float		duration;
	float		amplitude;
	float		frequency;
	float		next_shake;
	vec3_t		offset;
	float		angle;
	vec3_t		applied_offset;
	float		applied_angle;
} screen_shake_t;

typedef struct
{
	net_response_t		resp;
	net_api_response_func_t	pfnFunc;
	double			timeout;
	double			timesend;	// time when request was sended
	int			flags;	// FNETAPI_MULTIPLE_RESPONSE etc
} net_request_t;

// new versions of client dlls have a single export with all callbacks
typedef void (*CL_EXPORT_FUNCS)( void *pv );

typedef struct
{
	void		*hInstance;		// pointer to client.dll
	cldll_func_t	dllFuncs;			// dll exported funcs
	render_interface_t	drawFuncs;		// custom renderer support
	poolhandle_t      mempool;			// client edicts pool
	string		mapname;			// map name
	string		maptitle;			// display map title
	string		itemspath;		// path to items description for auto-complete func

	cl_entity_t	*entities;		// dynamically allocated entity array
	cl_entity_t	*static_entities;		// dynamically allocated static entity array
	remap_info_t	**remap_info;		// store local copy of all remap textures for each entity

	int		maxEntities;
	int		maxRemapInfos;		// maxEntities + cl.viewEnt; also used for catch entcount
	int		numStatics;		// actual static entity count
	int		maxModels;

	// movement values from server
	movevars_t	movevars;
	movevars_t	oldmovevars;
	playermove_t	*pmove;			// pmove state

	qboolean		pushed;			// used by PM_Push\Pop state
	int		oldviscount;		// used by PM_Push\Pop state
	int		oldphyscount;		// used by PM_Push\Pop state

	cl_user_message_t	msg[MAX_USER_MESSAGES];	// keep static to avoid fragment memory
	cl_user_event_t	*events[MAX_EVENTS];

	string		cdtracks[MAX_CDTRACKS];	// 32 cd-tracks read from cdaudio.txt

	model_t		sprites[MAX_CLIENT_SPRITES];	// hud&client spritetexturesz
	int		viewport[4];		// viewport sizes

	client_draw_t	ds;			// draw2d stuff (hud, weaponmenu etc)
	screenfade_t	fade;			// screen fade
	screen_shake_t	shake;			// screen shake
	center_print_t	centerPrint;		// centerprint variables
	SCREENINFO	scrInfo;			// actual screen info
	ref_overview_t	overView;			// overView params
	color24		palette[256];		// palette used for particle colors

	cached_spritelist_t	sprlist[MAX_CLIENT_SPRITES];	// client list sprites

	client_textmessage_t *titles;			// title messages, not network messages
	int		numTitles;

	net_request_t	net_requests[MAX_REQUESTS];	// no reason to keep more

	cl_entity_t	viewent;			// viewmodel

#if XASH_WIN32
	qboolean client_dll_uses_sdl;
#endif
} clgame_static_t;

typedef struct
{
	void		*hInstance;		// pointer to client.dll
	UI_FUNCTIONS	dllFuncs;			// dll exported funcs
	UI_EXTENDED_FUNCTIONS dllFuncs2;	// fwgs extension
	poolhandle_t      mempool;			// client edicts pool

	cl_entity_t	playermodel;		// uiPlayerSetup drawing model
	player_info_t	playerinfo;		// local playerinfo

	gameui_draw_t	ds;			// draw2d stuff (menu images)
	gameinfo2_t		gameInfo;	// current gameInfo
	gameinfo2_t		*modsInfo;	// simplified gameInfo for MainUI, allocated by demand
	GAMEINFO		**oldModsInfo;	// simplified gameInfo for older MainUI, allocated by demand

	ui_globalvars_t	*globals;

	qboolean		drawLogo;			// set to TRUE if logo.avi missed or corrupted
	int		logo_xres;
	int		logo_yres;
	float		logo_length;

	qboolean use_extended_api;
} gameui_static_t;

typedef struct
{
	connstate_t	state;
	qboolean		initialized;
	qboolean		changelevel;		// during changelevel
	qboolean		changedemo;		// during changedemo
	double		timestart;		// just for profiling

	// screen rendering information
	float		disable_screen;		// showing loading plaque between levels
						// or changing rendering dlls
						// if time gets > 30 seconds ahead, break it
	qboolean		draw_changelevel;		// draw changelevel image 'Loading...'

	keydest_t		key_dest;

	poolhandle_t      mempool;			// client premamnent pool: edicts etc

	int		signon;			// 0 to SIGNONS, for the signon sequence.

	// connection information
	char		servername[MAX_QPATH];	// name of server from original connect
	double		connect_time;		// for connection retransmits
	int		max_fragment_size;		// we needs to test a real network bandwidth
	int		connect_retry;		// how many times we send a connect packet to the server
	qboolean		spectator;		// not a real player, just spectator

	local_state_t	spectator_state;		// init as client startup

	char		userinfo[MAX_INFO_STRING];
	char		physinfo[MAX_INFO_STRING];	// read-only

	sizebuf_t		datagram;			// unreliable stuff. gets sent in CL_Move about cl_cmdrate times per second.
	byte		datagram_buf[MAX_DATAGRAM];

	netchan_t		netchan;

	float		packet_loss;
	double		packet_loss_recalc_time;
	int		starting_count;		// message num readed bits

	float		nextcmdtime;		// when can we send the next command packet?
	int		lastoutgoingcommand;	// sequence number of last outgoing command
	int		lastupdate_sequence;	// prediction stuff

	int		td_lastframe;		// to meter out one message a frame
	int		td_startframe;		// host_framecount at start
	double		td_starttime;		// realtime at second frame of timedemo
	int		forcetrack;		// -1 = use normal cd track

	// game images
	int		pauseIcon;		// draw 'paused' when game in-pause
	int		tileImage;		// for draw any areas not covered by the refresh
	int		loadingBar;		// 'loading' progress bar
	cl_font_t		creditsFont;		// shared creditsfont

	float		latency;			// rolling average of frame latencey (receivedtime - senttime) values.

	int		num_client_entities;	// cl.maxclients * CL_UPDATE_BACKUP * MAX_PACKET_ENTITIES
	int		next_client_entities;	// next client_entity to use
	entity_state_t	*packet_entities;		// [num_client_entities]

	predicted_player_t	predicted_players[MAX_CLIENTS];
	double		correction_time;

	scrshot_t		scrshot_request;		// request for screen shot
	scrshot_t		scrshot_action;		// in-action
	const float	*envshot_vieworg;		// envshot position
	int		envshot_viewsize;		// override cvar
	qboolean		envshot_disable_vis;	// disable VIS on server while makes an envshots
	string		shotname;

	// download info
	incomingtransfer_t	dl;

	// demo loop control
	int		demonum;			// -1 = don't play demos
	int		olddemonum;		// restore playing
	char		demos[MAX_DEMOS][MAX_QPATH];	// when not playing
	qboolean		demos_pending;

	// movie playlist
	int		movienum;
	string		movies[MAX_MOVIES];

	// demo recording info must be here, so it isn't clearing on level change
	qboolean		demorecording;
	int			demoplayback;
	qboolean		demowaiting;		// don't record until a non-delta message is received
	qboolean		timedemo;
	string		demoname;			// for demo looping
	double		demotime;			// recording time
	qboolean		set_lastdemo;		// store name of last played demo into the cvar

	file_t		*demofile;
	file_t		*demoheader;		// contain demo startup info in case we record a demo on this level
	qboolean internetservers_wait;	// internetservers is waiting for dns request
	qboolean internetservers_pending; // if true, clean master server pings
	uint32_t internetservers_key;       // compare key to validate master server reply
	char     internetservers_query[512]; // cached query
	uint32_t internetservers_query_len;

	// multiprotocol support
	connprotocol_t legacymode;
	int extensions;

	netadr_t serveradr;

	// do we accept utf8 as input
	qboolean accept_utf8;

	// server's build number (might be zero)
	int build_num;
	uint8_t steamid[8];
} client_static_t;

#ifdef __cplusplus
extern "C" {
#endif

extern client_t		cl;
extern client_static_t	cls;
extern clgame_static_t	clgame;
extern gameui_static_t	gameui;

#ifdef __cplusplus
}
#endif

//
// cvars
//
extern convar_t	showpause;
extern convar_t	mp_decals;
extern convar_t	cl_logomaxdim;
extern convar_t	cl_allow_download;
extern convar_t	cl_download_ingame;
extern convar_t	cl_nopred;
extern convar_t	cl_timeout;
extern convar_t	cl_interp;
extern convar_t cl_nointerp;
extern convar_t	cl_showerror;
extern convar_t	cl_nosmooth;
extern convar_t	cl_smoothtime;
extern convar_t	cl_crosshair;
extern convar_t	cl_testlights;
extern convar_t	cl_cmdrate;
extern convar_t	cl_updaterate;
extern convar_t	cl_solid_players;
extern convar_t	cl_idealpitchscale;
extern convar_t	cl_allow_levelshots;
extern convar_t	cl_draw_particles;
extern convar_t	cl_draw_tracers;
extern convar_t	cl_levelshot_name;
extern convar_t	cl_draw_beams;
extern convar_t	cl_clockreset;
extern convar_t	hud_fontscale;
extern convar_t hud_fontrender;
extern convar_t	hud_scale;
extern convar_t hud_scale_minimal_width;
extern convar_t	r_showtextures;
extern convar_t	cl_bmodelinterp;
extern convar_t	cl_lw;		// local weapons
extern convar_t	cl_charset;
extern convar_t	cl_trace_consistency;
extern convar_t	cl_trace_stufftext;
extern convar_t	cl_trace_messages;
extern convar_t	cl_trace_events;
extern convar_t	hud_utf8;
extern convar_t	cl_showevents;
extern convar_t	scr_centertime;
extern convar_t	scr_viewsize;
extern convar_t	scr_loading;
extern convar_t	v_dark;	// start from dark
extern convar_t	net_graph;
extern convar_t	rate;
extern convar_t	m_ignore;
extern convar_t	r_showtree;
extern convar_t	ui_renderworld;
extern convar_t cl_fixmodelinterpolationartifacts;

//=============================================================================

void CL_SetLightstyle( int style, const char* s, float f );
void CL_DecayLights( void );
dlight_t *CL_GetDynamicLight( int number );
dlight_t *CL_GetEntityLight( int number );

//=================================================

//
// cl_cmds.c
//
void CL_Quit_f( void );
void CL_GenericShot_f( void );
void CL_PlayCDTrack_f( void );
void CL_LevelShot_f( void );
void CL_SetSky_f( void );
void SCR_Viewpos_f( void );
void CL_WavePlayLen_f( void );

//
// cl_custom.c
//
qboolean CL_CheckFile( sizebuf_t *msg, resource_t *pResource );
void CL_AddToResourceList( resource_t *pResource, resource_t *pList );
void CL_RemoveFromResourceList( resource_t *pResource );
void CL_MoveToOnHandList( resource_t *pResource );
void CL_ClearResourceLists( void );

//
// cl_debug.c
//
void CL_ReplayBufferDat_f( void );
void CL_Parse_Debug( qboolean enable );
void CL_Parse_RecordCommand( int cmd, int startoffset );
void CL_ResetFrame( frame_t *frame );

//
// cl_efx.c
//
void CL_Particle( const vec3_t org, int color, float life, int zpos, int zvel );

//
// cl_main.c
//
void CL_Init( void );
void CL_Disconnect_f( void );
void CL_ProcessFile( qboolean successfully_received, const char *filename );
void CL_WriteUsercmd( connprotocol_t proto, sizebuf_t *msg, int from, int to );
void CL_SetupNetchanForProtocol( connprotocol_t proto );
qboolean CL_PrecacheResources( void );
void CL_SetupOverviewParams( void );
void CL_UpdateFrameLerp( void );
int CL_IsDevOverviewMode( void );
void CL_SignonReply( connprotocol_t proto );
void CL_ClearState( void );

//
// cl_demo.c
//
void CL_StartupDemoHeader( void );
void CL_DrawDemoRecording( void );
void CL_WriteDemoUserCmd( int cmdnumber );
void CL_WriteDemoMessage( qboolean startup, int start, sizebuf_t *msg );
void CL_WriteDemoUserMessage( int size, byte *buffer );
qboolean CL_DemoReadMessage( byte *buffer, size_t *length );
void CL_DemoInterpolateAngles( void );
void CL_CheckStartupDemos( void );
void CL_WriteDemoJumpTime( void );
void CL_CloseDemoHeader( void );
void CL_DemoCompleted( void );
void CL_PlayDemo_f( void );
void CL_TimeDemo_f( void );
void CL_StartDemos_f( void );
void CL_Demos_f( void );
void CL_DeleteDemo_f( void );
void CL_Record_f( void );
void CL_Stop_f( void );
void CL_ListDemo_f( void );
int CL_GetDemoComment( const char *demoname, char *comment );

//
// cl_events.c
//
void CL_ParseEvent( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseReliableEvent( sizebuf_t *msg, connprotocol_t proto );
void CL_SetEventIndex( const char *szEvName, int ev_index );
void CL_PlaybackEvent( int flags, const edict_t *pInvoker, word eventindex, float delay, float *origin,
	float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 );
void CL_RegisterEvent( int lastnum, const char *szEvName, pfnEventHook func );
void CL_ResetEvent( event_info_t *ei );
word CL_EventIndex( const char *name );
void CL_FireEvents( void );

//
// cl_font.c
//
qboolean CL_FixedFont( cl_font_t *font );
qboolean Con_LoadFixedWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags );
qboolean Con_LoadVariableWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags );
void CL_FreeFont( cl_font_t *font );
void CL_SetFontRendermode( cl_font_t *font );
int CL_DrawCharacter( float x, float y, int number, const rgba_t color, cl_font_t *font, int flags );
int CL_DrawString( float x, float y, const char *s, const rgba_t color, cl_font_t *font, int flags );
void CL_DrawCharacterLen( cl_font_t *font, int number, int *width, int *height );
void CL_DrawStringLen( cl_font_t *font, const char *s, int *width, int *height, int flags );
int CL_DrawStringf( cl_font_t *font, float x, float y, const rgba_t color, int flags, const char *fmt, ... ) FORMAT_CHECK( 6 );


//
// cl_game.c
//
void CL_UnloadProgs( void );
qboolean CL_LoadProgs( const char *name );
void CL_LinkUserMessage( char *pszName, const int svc_num, int iSize );
void CL_DrawHUD( int state );
void CL_InitEdicts( int maxclients );
void CL_FreeEdicts( void );
void CL_ClearWorld( void );
void CL_DrawCenterPrint( void );
void CL_ClearSpriteTextures( void );
void CL_CenterPrint( const char *text, float y );
void CL_TextMessageParse( byte *pMemFile, int fileSize );
client_textmessage_t *CL_TextMessageGet( const char *pName );
void NetAPI_CancelAllRequests( void );
model_t *CL_LoadClientSprite( const char *filename );
model_t *CL_LoadModel( const char *modelname, int *index );
HSPRITE pfnSPR_LoadExt( const char *szPicName, uint texFlags );
void SPR_AdjustSize( float *x, float *y, float *w, float *h );
int CL_GetScreenInfo( SCREENINFO *pscrinfo );
pmtrace_t *PM_CL_TraceLine( float *start, float *end, int flags, int usehull, int ignore_pe );
const char *PM_CL_TraceTexture( int ground, float *vstart, float *vend );
int PM_CL_PointContents( const float *p, int *truecontents );
physent_t *pfnGetPhysent( int idx );
struct msurface_s *pfnTraceSurface( int ground, float *vstart, float *vend );
void CL_EnableScissor( scissor_state_t *scissor, int x, int y, int width, int height );
void CL_DisableScissor( scissor_state_t *scissor );
qboolean CL_Scissor( const scissor_state_t *scissor, float *x, float *y, float *width, float *height, float *u0, float *v0, float *u1, float *v1 );

static inline cl_entity_t *CL_EDICT_NUM( int index )
{
	if( !clgame.entities ) // not in game yet
	{
		Host_Error( "%s: clgame.entities is NULL\n", __func__ );
		return NULL;
	}

	if( index < 0 || index >= clgame.maxEntities )
	{
		Host_Error( "%s: bad number %i\n", __func__, index );
		return NULL;
	}

	return clgame.entities + index;
}

static inline cl_entity_t *CL_GetEntityByIndex( int index )
{
	if( !clgame.entities ) // not in game yet
		return NULL;

	if( index < 0 || index >= clgame.maxEntities )
		return NULL;

	return clgame.entities + index;
}

static inline model_t *CL_ModelHandle( int modelindex )
{
	return modelindex >= 0 && modelindex < MAX_MODELS ? cl.models[modelindex] : NULL;
}

static inline qboolean CL_IsThirdPerson( void )
{
	return clgame.dllFuncs.CL_IsThirdPerson();
}

static inline cl_entity_t *CL_GetLocalPlayer( void )
{
	cl_entity_t	*player = CL_GetEntityByIndex( cl.playernum + 1 );

	// HACKHACK: GoldSrc doesn't do this, but some mods actually check it for null pointer
	// this is a lesser evil than changing semantics of HUD_VidInit and call it after entities are allocated
	if( !player )
		Con_Printf( S_WARN "%s: client entities are not initialized yet! Returning NULL...\n", __func__ );

	return player;
}

//
// cl_parse.c
//
void CL_ParseSetAngle( sizebuf_t *msg );
void CL_ParseServerData( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseLightStyle( sizebuf_t *msg, connprotocol_t proto );
void CL_UpdateUserinfo( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseResource( sizebuf_t *msg );
void CL_ParseClientData( sizebuf_t *msg, connprotocol_t proto );
void CL_UpdateUserPings( sizebuf_t *msg );
void CL_ParseParticles( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseRestoreSoundPacket( sizebuf_t *msg );
void CL_ParseBaseline( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseSignon( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseRestore( sizebuf_t *msg );
void CL_ParseStaticDecal( sizebuf_t *msg );
void CL_ParseAddAngle( sizebuf_t *msg );
void CL_RegisterUserMessage( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseResourceList( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseMovevars( sizebuf_t *msg );
void CL_ParseResourceRequest( sizebuf_t *msg );
void CL_ParseCustomization( sizebuf_t *msg );
void CL_ParseCrosshairAngle( sizebuf_t *msg );
void CL_ParseSoundFade( sizebuf_t *msg );
void CL_ParseFileTransferFailed( sizebuf_t *msg );
void CL_ParseHLTV( sizebuf_t *msg );
void CL_ParseDirector( sizebuf_t *msg );
void CL_ParseVoiceInit( sizebuf_t *msg );
void CL_ParseVoiceData( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseResLocation( sizebuf_t *msg );
void CL_ParseCvarValue( sizebuf_t *msg, const qboolean ext, const connprotocol_t proto );
void CL_ParseServerMessage( sizebuf_t *msg );
qboolean CL_ParseCommonDLLMessage( sizebuf_t *msg, connprotocol_t proto, int svc_num, int startoffset );
void CL_ParseTempEntity( sizebuf_t *msg, connprotocol_t proto );
qboolean CL_DispatchUserMessage( const char *pszName, int iSize, void *pbuf );
qboolean CL_RequestMissingResources( void );
void CL_RegisterResources( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseViewEntity( sizebuf_t *msg );
void CL_ParseServerTime( sizebuf_t *msg, connprotocol_t proto );
void CL_ParseUserMessage( sizebuf_t *msg, int svc_num, connprotocol_t proto );
void CL_ParseFinaleCutscene( sizebuf_t *msg, int level );
void CL_ParseTextMessage( sizebuf_t *msg );
void CL_ParseExec( sizebuf_t *msg );
void CL_BatchResourceRequest( qboolean initialize );
int CL_EstimateNeededResources( void );

//
// cl_parse_48.c
//
void CL_ParseLegacyServerMessage( sizebuf_t *msg );
void CL_LegacyPrecache_f( void );

//
// cl_parse_gs.c
//
void CL_ParseGoldSrcServerMessage( sizebuf_t *msg );

//
// cl_scrn.c
//
void SCR_VidInit( void );
void SCR_TileClear( void );
void SCR_DirtyScreen( void );
void SCR_EndLoadingPlaque( void );
int SCR_LoadPauseIcon( void );
void SCR_RegisterTextures( void );
void SCR_LoadCreditsFont( void );
void SCR_MakeScreenShot( void );
void SCR_MakeLevelShot( void );
void SCR_NetSpeeds( void );
void SCR_RSpeeds( void );
void SCR_DrawFPS( int height );
void SCR_DrawPos( void );
void SCR_DrawEnts( void );
void SCR_DrawUserCmd( void );

//
// cl_netgraph.c
//
void CL_InitNetgraph( void );
void SCR_DrawNetGraph( void );

//
// cl_view.c
//

void V_Init (void);
void V_Shutdown( void );
qboolean V_PreRender( void );
void V_PostRender( void );
void V_RenderView( void );

//
// cl_pmove.c
//
void CL_SetSolidEntities( void );
void CL_SetSolidPlayers( int playernum );
void CL_InitClientMove( void );
void CL_PredictMovement( qboolean repredicting );
void CL_CheckPredictionError( void );
int CL_WaterEntity( const float *rgflPos );
cl_entity_t *CL_GetWaterEntity( const float *rgflPos );
pmtrace_t *CL_VisTraceLine( vec3_t start, vec3_t end, int flags );
pmtrace_t CL_TraceLine( vec3_t start, vec3_t end, int flags );
void CL_MoveSpectatorCamera( void );
void CL_SetLastUpdate( void );
void CL_RedoPrediction( void );
void CL_PushPMStates( void );
void CL_PopPMStates( void );
void CL_SetUpPlayerPrediction( int dopred, int bIncludeLocalClient );
void CL_SetIdealPitch( void );

//
// cl_qparse.c
//
void CL_ParseQuakeMessage( sizebuf_t *msg );

//
// cl_frame.c
//
struct channel_s;
struct rawchan_s;
qboolean CL_ValidateDeltaPacket( uint oldpacket, frame_t *oldframe );
int CL_UpdateOldEntNum( int oldindex, frame_t *oldframe, entity_state_t **oldent );
int CL_ParsePacketEntities( sizebuf_t *msg, qboolean delta, connprotocol_t proto );
qboolean CL_AddVisibleEntity( cl_entity_t *ent, int entityType );
void CL_ResetLatchedVars( cl_entity_t *ent, qboolean full_reset );
qboolean CL_GetEntitySpatialization( struct channel_s *ch );
qboolean CL_GetMovieSpatialization( struct rawchan_s *ch );
void CL_ComputePlayerOrigin( cl_entity_t *clent );
void CL_ProcessPacket( frame_t *frame );
void CL_MoveThirdpersonCamera( void );
void CL_EmitEntities( void );

static inline qboolean CL_IsPlayerIndex( int idx )
{
	return idx >= 1 && idx <= cl.maxclients ? true : false;
}

//
// cl_remap.c
//
remap_info_t *CL_GetRemapInfoForEntity( cl_entity_t *e );
qboolean CL_EntitySetRemapColors( cl_entity_t *e, model_t *mod, int top, int bottom );
void CL_ClearAllRemaps( void );

//
// cl_render.c
//
qboolean R_InitRenderAPI( void );
intptr_t CL_RenderGetParm( const int parm, const int arg, const qboolean checkRef );
lightstyle_t *CL_GetLightStyle( int number );
int R_FatPVS( const vec3_t org, float radius, byte *visbuffer, qboolean merge, qboolean fullvis );
const ref_overview_t *GL_GetOverviewParms( void );

//
// cl_efrag.c
//
void R_StoreEfrags( efrag_t **ppefrag, int framecount );
void R_AddEfrags( cl_entity_t *ent );

//
// cl_tent.c
//
struct particle_s;
void CL_WeaponAnim( int iAnim, int body );
void CL_ClearEffects( void );
void CL_ClearEfrags( void );
void CL_TestLights( void );
void CL_FireCustomDecal( int textureIndex, int entityIndex, int modelIndex, float *pos, int flags, float scale );
void CL_DecalShoot( int textureIndex, int entityIndex, int modelIndex, float *pos, int flags );
void R_FreeDeadParticles( struct particle_s **ppparticles );
void CL_AddClientResource( const char *filename, int type );
void CL_AddClientResources( void );
void CL_InitParticles( void );
void CL_ClearParticles( void );
void CL_FreeParticles( void );
void CL_InitTempEnts( void );
void CL_FreeTempEnts( void );
void CL_TempEntUpdate( void );
void CL_InitViewBeams( void );
void CL_ClearViewBeams( void );
void CL_FreeViewBeams( void );
cl_entity_t *R_BeamGetEntity( int index );
void CL_KillDeadBeams( cl_entity_t *pDeadEntity );
void CL_ParseViewBeam( sizebuf_t *msg, int beamType );
void CL_LoadClientSprites( void );
void CL_ReadPointFile_f( void );
void CL_DrawEFX( float time, qboolean fTrans );
void CL_ThinkParticle( double frametime, particle_t *p );
void CL_ReadLineFile_f( void );

//
// console.c
//
extern convar_t con_fontsize;
int Con_Visible( void );
qboolean Con_FixedFont( void );
void Con_VidInit( void );
void Con_Shutdown( void );
void Con_ToggleConsole_f( void );
void Con_ClearNotify( void );
void Con_DrawDebug( void );
void Con_RunConsole( void );
void Con_DrawConsole( void );
void Con_DrawVersion( void );
int Con_UtfProcessChar( int in );
int Con_UtfProcessCharForce( int in );
int Con_UtfMoveLeft( char *str, int pos );
int Con_UtfMoveRight( char *str, int pos, int length );
void Con_DefaultColor( int r, int g, int b, qboolean gameui );
cl_font_t *Con_GetCurFont( void );
cl_font_t *Con_GetFont( int num );
int Con_DrawString( int x, int y, const char *string, const rgba_t setColor ); // legacy, use cl_font.c
void GAME_EXPORT Con_DrawStringLen( const char *pText, int *length, int *height ); // legacy, use cl_font.c
void Con_CharEvent( int key );
void Key_Console( int key );
void Key_Message( int key );
void Con_FastClose( void );
void Con_Bottom( void );
void Con_PageDown( int lines );
void Con_PageUp( int lines );

//
// s_main.c
//
typedef int sound_t;
void S_StartBackgroundTrack( const char *intro, const char *loop, int position, qboolean fullpath );
void S_StopBackgroundTrack( void );
void S_StreamSetPause( int pause );
void S_StartStreaming( void );
void S_StopStreaming( void );
void S_BeginRegistration( void );
sound_t S_RegisterSound( const char *sample );
void S_EndRegistration( void );
void S_RestoreSound( const vec3_t pos, int ent, int chan, sound_t handle, float fvol, float attn, int pitch, int flags, double sample, double end, int wordIndex );
void S_StartSound( const vec3_t pos, int ent, int chan, sound_t sfx, float vol, float attn, int pitch, int flags );
void S_AmbientSound( const vec3_t pos, int ent, sound_t handle, float fvol, float attn, int pitch, int flags );
void S_FadeClientVolume( float fadePercent, float fadeOutSeconds, float holdTime, float fadeInSeconds );
void S_FadeMusicVolume( float fadePercent );
void S_StartLocalSound( const char *name, float volume, qboolean reliable );
void SND_UpdateSound( void );
void S_ExtraUpdate( void );

//
// cl_gameui.c
//
void UI_UnloadProgs( void );
qboolean UI_LoadProgs( void );
void UI_UpdateMenu( float realtime );
void UI_KeyEvent( int key, qboolean down );
void UI_MouseMove( int x, int y );
void UI_SetActiveMenu( qboolean fActive );
void UI_AddServerToList( netadr_t adr, const char *info );
void UI_GetCursorPos( int *pos_x, int *pos_y );
void UI_SetCursorPos( int pos_x, int pos_y );
void UI_ShowCursor( qboolean show );
qboolean UI_CreditsActive( void );
void UI_CharEvent( int key );
qboolean UI_MouseInRect( void );
qboolean UI_IsVisible( void );
void UI_ResetPing( void );
void UI_ShowUpdateDialog( qboolean preferStore );
qboolean UI_ShowMessageBox( const char *text );
void UI_AddTouchButtonToList( const char *name, const char *texture, const char *command, unsigned char *color, int flags );
void UI_ConnectionProgress_Disconnect( void );
void UI_ConnectionProgress_Download( const char *pszFileName, const char *pszServerName, const char *pszServerPath, int iCurrent, int iTotal, const char *comment );
void UI_ConnectionProgress_DownloadEnd( void );
void UI_ConnectionProgress_Precache( void );
void UI_ConnectionProgress_Connect( const char *server );
void UI_ConnectionProgress_ChangeLevel( void );
void UI_ConnectionProgress_ParseServerInfo( const char *server );

//
// cl_mobile.c
//
qboolean Mobile_Init( void );
void Mobile_Shutdown( void );

//
// cl_securedstub.c
//
void CL_GetSecuredClientAPI( CL_EXPORT_FUNCS F );

//
// cl_video.c
//
void SCR_InitCinematic( void );
void SCR_FreeCinematic( void );
qboolean SCR_PlayCinematic( const char *name );
qboolean SCR_DrawCinematic( void );
qboolean SCR_NextMovie( void );
void SCR_RunCinematic( void );
void SCR_StopCinematic( void );
void CL_PlayVideo_f( void );


//
// keys.c
//
int Key_IsDown( int keynum );
void Key_Event( int key, int down );
void Key_Init( void );
void Key_WriteBindings( file_t *f );
const char *Key_GetBinding( int keynum );
void Key_SetBinding( int keynum, const char *binding );
const char *Key_LookupBinding( const char *pBinding );
void Key_ClearStates( void );
const char *Key_KeynumToString( int keynum );
void Key_EnumCmds_f( void );
void Key_SetKeyDest( int key_dest );
void Key_EnableTextInput( qboolean enable, qboolean force );
int Key_ToUpper( int key );
void OSK_Draw( void );

//
// identification.c
//
void ID_Init( void );
const char *ID_GetMD5( void );

extern rgba_t g_color_table[8];
extern triangleapi_t gTriApi;
extern net_api_t gNetApi;

#endif//CLIENT_H
