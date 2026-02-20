/*
server.h - primary header for server
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

#ifndef SERVER_H
#define SERVER_H

#include "xash3d_mathlib.h"
#include "edict.h"
#include "eiface.h"
#include "physint.h"	// physics interface
#include "mod_local.h"
#include "pm_defs.h"
#include "pm_movevars.h"
#include "entity_state.h"
#include "protocol.h"
#include "netchan.h"
#include "custom.h"
#include "world.h"

//=============================================================================

#define SV_UPDATE_MASK	(SV_UPDATE_BACKUP - 1)
#if XASH_LOW_MEMORY == 2
#define SV_UPDATE_BACKUP SINGLEPLAYER_BACKUP
#else
extern int SV_UPDATE_BACKUP;
#endif

// hostflags
#define SVF_SKIPLOCALHOST	BIT( 0 )
#define SVF_MERGE_VISIBILITY	BIT( 1 )	// we are do portal pass

// mapvalid flags
#define MAP_IS_EXIST        BIT( 0 )
#define MAP_HAS_LANDMARK    BIT( 2 )
#define MAP_INVALID_VERSION BIT( 3 )

#define SV_SPAWN_TIME	0.1

// group flags
#define GROUP_OP_AND	0
#define GROUP_OP_NAND	1

#ifdef NDEBUG
#define SV_IsValidEdict( e )	( e && !e->free )
#else
#define SV_IsValidEdict( e )	SV_CheckEdict( e, __FILE__, __LINE__ )
#endif
#define NUM_FOR_EDICT(e)	((int)((edict_t *)(e) - svgame.edicts))
#define EDICT_NUM( num )	SV_EdictNum( num )
#define STRING( offset )	SV_GetString( offset )
#define ALLOC_STRING(str)	SV_AllocString( str )
#define MAKE_STRING(str)	SV_MakeString( str )

#define MAX_PUSHED_ENTS	256
#define MAX_VIEWENTS	128
#define MAX_LOCALINFO_STRING	32768	// localinfo used on server and not sended to the clients

#define MAX_ENT_LEAFS( ext ) (( ext ) ? MAX_ENT_LEAFS_32 : MAX_ENT_LEAFS_16 )

#define FCL_RESEND_USERINFO	BIT( 0 )
#define FCL_RESEND_MOVEVARS	BIT( 1 )
#define FCL_SKIP_NET_MESSAGE	BIT( 2 )
#define FCL_SEND_NET_MESSAGE	BIT( 3 )
#define FCL_PREDICT_MOVEMENT	BIT( 4 )	// movement prediction is enabled
#define FCL_LOCAL_WEAPONS	BIT( 5 )	// weapon prediction is enabled
#define FCL_LAG_COMPENSATION	BIT( 6 )	// lag compensation is enabled
#define FCL_FAKECLIENT	BIT( 7 )	// this client is a fake player controlled by the game DLL
#define FCL_HLTV_PROXY	BIT( 8 )	// this is a proxy for a HLTV client (spectator)
#define FCL_SEND_RESOURCES	BIT( 9 )
#define FCL_FORCE_UNMODIFIED	BIT( 10 )

typedef enum
{
	ss_dead,		// no map loaded
	ss_loading,	// spawning level edicts
	ss_active		// actively running
} sv_state_t;

typedef enum
{
	cs_free = 0,	// can be reused for a new connection
	cs_zombie,	// client has been disconnected, but don't reuse connection for a couple seconds
	cs_connected,	// has been assigned to a sv_client_t, but not in game yet
	cs_spawning,	// put in game, but not spawned yet
	cs_spawned	// client is fully in game
} cl_state_t;

typedef enum
{
	us_inactive = 0,
	us_processing,
	us_complete,
} cl_upload_t;

// instanced baselines container
typedef struct
{
	const char	*classname;
	entity_state_t	baseline;
} sv_baseline_t;

typedef struct
{
	qboolean		active;
	qboolean		net_log;
	netadr_t		net_address;
	file_t		*file;
} server_log_t;

typedef struct server_s
{
	sv_state_t	state;		// precache commands are only valid during load

	qboolean		background;	// this is background map
	qboolean		loadgame;		// client begins should reuse existing entity
	double		time;		// sv.time += sv.frametime
	double		time_residual;	// unclamped
	float		frametime;	// 1.0 / sv_fps->value
	int		framecount;	// count physic frames
	struct sv_client_s	*current_client;	// current client who network message sending on

	int		hostflags;	// misc server flags: predicting etc
	CRC32_t		worldmapCRC;	// check crc for catch cheater maps
	int		progsCRC;		// this is used with feature ENGINE_QUAKE_COMPATIBLE

	char		name[MAX_QPATH];	// map name
	char		startspot[MAX_QPATH];

	double		lastchecktime;
	int		lastcheck;	// number of last checked client

	char		model_precache[MAX_MODELS][MAX_QPATH];
	char		sound_precache[MAX_SOUNDS][MAX_QPATH];
	char		files_precache[MAX_CUSTOM][MAX_QPATH];
	char		event_precache[MAX_EVENTS][MAX_QPATH];
	byte		model_precache_flags[MAX_MODELS];
	model_t		*models[MAX_MODELS];
	int		num_static_entities;

	// run local lightstyles to let SV_LightPoint grab the actual information
	lightstyle_t	lightstyles[MAX_LIGHTSTYLES];

	consistency_t	consistency_list[MAX_MODELS];
	resource_t	resources[MAX_RESOURCES];
	int		num_consistency;	// typically check model bounds on this
	int		num_resources;

	sv_baseline_t	instanced[MAX_CUSTOM_BASELINES];	// instanced baselines
	int		last_valid_baseline;// all the entities with number more than that was created in-game and doesn't have the baseline
	int		num_instanced;

	// unreliable data to send to clients.
	sizebuf_t		datagram;
	byte		datagram_buf[MAX_DATAGRAM];

	// reliable data to send to clients.
	sizebuf_t		reliable_datagram;	// copied to all clients at end of frame
	byte		reliable_datagram_buf[MAX_DATAGRAM];

	// the multicast buffer is used to send a message to a set of clients
	sizebuf_t		multicast;
	byte		multicast_buf[MAX_MULTICAST];

	sizebuf_t		signon;
	byte		signon_buf[MAX_INIT_MSG];	// need a get to maximum size

	sizebuf_t		spec_datagram;
	byte		spectator_buf[MAX_MULTICAST];

	model_t		*worldmodel;	// pointer to world

	qboolean		playersonly;
	qboolean		simulating;	// physics is running
	qboolean		paused;

	// statistics
	int		ignored_static_ents;
	int		ignored_world_decals;
	int		static_ents_overflow;
} server_t;

typedef struct
{
	double		senttime;
	float		ping_time;

	clientdata_t	clientdata;
	weapon_data_t	weapondata[MAX_LOCAL_WEAPONS];

	int  		num_entities;
	int  		first_entity;		// into the circular sv_packet_entities[]
} client_frame_t;

typedef struct sv_client_s
{
	cl_state_t  state;
	cl_upload_t upstate;    // uploading state
	char        name[32];   // extracted from userinfo, color string allowed
	uint        flags;      // client flags, some info
	uint        extensions; // protocol extensions

	char hashedcdkey[34];            // MD5 hash is 32 hex #'s, plus trailing 0
	char userinfo[MAX_INFO_STRING];  // name, etc (received from client)
	char physinfo[MAX_INFO_STRING];  // set on server (transmit to client)
	char useragent[MAX_INFO_STRING];

	byte ignorecmdtime_warned; // did we warn our server operator in the log for this batch of commands?
	uint listeners;   // which other clients does this guy's voice stream go to?

	int ignorecmdtime_warns; // how many times client time was faster than server during this session
	int userid;              // identifying number on server

	netchan_t netchan;
	sizebuf_t datagram; // the datagram is written to by sound calls, prints, temp ents, etc.
	byte      datagram_buf[MAX_DATAGRAM]; // it can be harmlessly overflowed.

	int chokecount;     // number of messages rate supressed
	int delta_sequence; // -1 = no compression.

	double next_messagetime;   // time when we should send next world state update
	double next_checkpingtime; // time to send all players pings to client
	double next_sendinfotime;  // time to send info about all players
	double next_messageinterval; // update rate, clamped
	double timebase;           // client timebase
	double connection_started;

	customization_t customdata;      // player customization linked list
	resource_t      resourcesonhand;
	resource_t      resourcesneeded; // <mapname.res> from client (server downloading)
	usercmd_t       lastcmd;         // for filling in big drops

	int    packet_loss;
	double connecttime;
	double cmdtime;
	double ignorecmdtime;
	float  latency;

	int     ignored_ents; // if visibility list is full we should know how many entities will be ignored
	edict_t *edict;       // EDICT_NUM(clientnum+1)
	edict_t *pViewEntity; // svc_setview member
	edict_t *viewentity[MAX_VIEWENTS]; // list of portal cameras in player PVS
	int     num_viewents; // num of portal cameras that can merge PVS

	int    userinfo_change_attempts;
	double fullupdate_next_calltime;
	double userinfo_next_changetime;
	double userinfo_penalty;

	double overflow_warn_time;

	client_frame_t *frames; // updates can be delta'd from here
	event_state_t  events;  // delta-updated events cycle
} sv_client_t;

/*
=============================================================================
 a client can leave the server in one of four ways:
 dropping properly by quiting or disconnecting
 timing out if no valid messages are received for sv_timeout.value seconds
 getting kicked off by the server operator
 a program error, like an overflowed reliable buffer
=============================================================================
*/
typedef struct
{
	char		name[32];	// in GoldSrc max name length is 12
	int		number;	// svc_ number
	int		size;	// if size == -1, size come from first byte after svcnum
} sv_user_message_t;

typedef struct
{
	edict_t		*ent;
	vec3_t		origin;
	vec3_t		angles;
	int		fixangle;
} sv_pushed_t;

typedef struct
{
	qboolean		active;
	qboolean		moving;
	qboolean		firstframe;
	qboolean		nointerp;

	vec3_t		mins;
	vec3_t		maxs;

	vec3_t		curpos;
	vec3_t		oldpos;
	vec3_t		newpos;
	vec3_t		finalpos;
} sv_interp_t;

typedef struct
{
	// user messages stuff
	const char	*msg_name;		// just for debug
	sv_user_message_t	msg[MAX_USER_MESSAGES];	// user messages array
	int		msg_size_index;		// write message size at this pos in bitbuf
	int		msg_realsize;		// left in bytes
	int		msg_index;		// for debug messages
	int		msg_dest;			// msg destination ( MSG_ONE, MSG_ALL etc )
	int     msg_rewrite_index;
	int     msg_rewrite_pos;
	qboolean		msg_started;		// to avoid recursive included messages
	edict_t		*msg_ent;			// user message member entity
	vec3_t		msg_org;			// user message member origin
	qboolean	msg_trace;		// trace this message

	void		*hInstance;		// pointer to game.dll

	edict_t		*edicts;			// solid array of server entities
	int		numEntities;		// actual entities count

	movevars_t	movevars;			// movement variables curstate
	movevars_t	oldmovevars;		// movement variables oldstate
	playermove_t	*pmove;			// pmove state
	sv_interp_t	interp[MAX_CLIENTS];	// interpolate clients
	sv_pushed_t	pushed[MAX_PUSHED_ENTS];	// no reason to keep array for all edicts
						// 256 it should be enough for any game situation

	globalvars_t	*globals;			// server globals

	DLL_FUNCTIONS	dllFuncs;			// dll exported funcs
	NEW_DLL_FUNCTIONS	dllFuncs2;		// new dll exported funcs (may be NULL)
	physics_interface_t	physFuncs;		// physics interface functions (Xash3D extension)

	poolhandle_t mempool;			// server premamnent pool: edicts etc
	poolhandle_t stringspool;		// for engine strings
} svgame_static_t;

typedef struct
{
	qboolean		initialized;		// sv_init has completed
	double		timestart;		// just for profiling

	int		maxclients;		// server max clients

	int		groupmask;
	int		groupop;

	server_log_t	log;

	char		serverinfo[MAX_SERVERINFO_STRING];
	char		localinfo[MAX_LOCALINFO_STRING];

	int		spawncount;		// incremented each server start
						// used to check late spawns
	sv_client_t	*clients;			// [svs.maxclients]
	int		num_client_entities;	// svs.maxclients*UPDATE_BACKUP*MAX_PACKET_ENTITIES
	int		next_client_entities;	// next client_entity to use
	entity_state_t	*packet_entities;		// [num_client_entities]
	entity_state_t	*baselines;		// [GI->max_edicts]
	entity_state_t	*static_entities;		// [MAX_STATIC_ENTITIES];

	uint32_t  challenge_salt[16]; // pregenerated random numbers for generating challenged based on IP's MD5 address

	sizebuf_t testpacket;         // pregenerataed testpacket, only needs CRC32 patching
	byte      *testpacket_buf;    // check for NULL if testpacket is available
	byte      *testpacket_crcpos; // pointer to write pregenerated crc (unaligned!!!)
	uint32_t  *testpacket_crcs;   // checksums lookup table
	int       testpacket_filepos; // file position (need to calculate lookup table pos)
	int       testpacket_filelen; // file and lookup table length
} server_static_t;

//=============================================================================

extern server_static_t svs RENAME_SYMBOL( "svs_" ); // persistant server info
extern server_t        sv RENAME_SYMBOL( "sv_" );   // local server
extern svgame_static_t svgame;                      // persistant game info
extern areanode_t      sv_areanodes[];              // AABB dynamic tree

extern convar_t		mp_logecho;
extern convar_t		mp_logfile;
extern convar_t		sv_log_onefile;
extern convar_t		sv_log_singleplayer;
extern convar_t		sv_unlag;
extern convar_t		sv_maxunlag;
extern convar_t		sv_unlagpush;
extern convar_t		sv_unlagsamples;
extern convar_t		rcon_enable;
extern convar_t		sv_instancedbaseline;
extern convar_t		sv_background_freeze;
extern convar_t		sv_minupdaterate;
extern convar_t		sv_maxupdaterate;
extern convar_t		sv_minrate;
extern convar_t		sv_maxrate;
extern convar_t		sv_downloadurl;
extern convar_t		sv_newunit;
extern convar_t		sv_clienttrace;
extern convar_t		sv_failuretime;
extern convar_t		sv_send_resources;
extern convar_t		sv_send_logos;
extern convar_t		sv_allow_upload;
extern convar_t		sv_allow_download;
extern convar_t		sv_friction;
extern convar_t		sv_gravity;
extern convar_t		sv_stopspeed;
extern convar_t		sv_wateralpha;
extern convar_t		sv_wateramp;
extern convar_t		sv_voiceenable;
extern convar_t		sv_voicequality;
extern convar_t		sv_maxvelocity;
extern convar_t		sv_stepsize;
extern convar_t		sv_skyname;
extern convar_t		sv_skycolor_r;
extern convar_t		sv_skycolor_g;
extern convar_t		sv_skycolor_b;
extern convar_t		sv_skyvec_x;
extern convar_t		sv_skyvec_y;
extern convar_t		sv_skyvec_z;
extern convar_t		sv_consistency;
extern convar_t		sv_password;
extern convar_t		sv_uploadmax;
extern convar_t		sv_trace_messages;
extern convar_t		sv_enttools_enable;
extern convar_t		sv_enttools_maxfire;
extern convar_t		sv_autosave;
extern convar_t		deathmatch;
extern convar_t		hostname;
extern convar_t		skill;
extern convar_t		coop;
extern convar_t		sv_cheats;
extern convar_t		public_server;
extern convar_t		sv_nat;
extern convar_t		sv_speedhack_kick;
extern convar_t		sv_pausable;		// allows pause in multiplayer
extern convar_t		sv_check_errors;
extern convar_t		sv_lighting_modulate;
extern convar_t		sv_novis;
extern convar_t		sv_hostmap;
extern convar_t		sv_validate_changelevel;
extern convar_t		sv_maxclients;
extern convar_t		sv_userinfo_enable_penalty;
extern convar_t		sv_userinfo_penalty_time;
extern convar_t		sv_userinfo_penalty_multiplier;
extern convar_t		sv_userinfo_penalty_attempts;
extern convar_t		sv_fullupdate_penalty_time;
extern convar_t		sv_log_outofband;
extern convar_t		sv_allow_autoaim;
extern convar_t		sv_aim;
extern convar_t		sv_allow_testpacket;
extern convar_t		sv_expose_player_list;

//===========================================================
//
// sv_main.c
//
void SV_FinalMessage( const char *message, qboolean reconnect );
void SV_KickPlayer( sv_client_t *cl, const char *fmt, ... ) FORMAT_CHECK( 2 );
void SV_DropClient( sv_client_t *cl, qboolean crash ) RENAME_SYMBOL( "SV_DropClient_" );
void SV_UpdateMovevars( qboolean initialize );
int SV_ModelIndex( const char *name );
int SV_SoundIndex( const char *name );
int SV_EventIndex( const char *name );
int SV_GenericIndex( const char *name );
void SV_InitOperatorCommands( void );
void SV_KillOperatorCommands( void );
void SV_RemoteCommand( netadr_t from, sizebuf_t *msg );
void SV_SendResource( resource_t *pResource, sizebuf_t *msg );
void SV_AddToMaster( netadr_t from, sizebuf_t *msg );
qboolean SV_ProcessUserAgent( netadr_t from, const char *useragent );

//
// sv_init.c
//
qboolean SV_InitGame( void );
void SV_ActivateServer( int runPhysics );
qboolean SV_SpawnServer( const char *server, const char *startspot, qboolean background );
void SV_DeactivateServer( void );
void SV_FreeTestPacket( void );

/*
================
SV_ModelHandle

get model by handle
================
*/
static inline model_t *GAME_EXPORT SV_ModelHandle( int modelindex )
{
	if( unlikely( modelindex < 0 || modelindex >= MAX_MODELS ))
		return NULL;
	return sv.models[modelindex];
}


//
// sv_phys.c
//
void SV_Physics( void );
qboolean SV_InitPhysicsAPI( void );
void SV_CheckVelocity( edict_t *ent );
qboolean SV_PlayerRunThink( edict_t *ent, float frametime, double time );
void SV_Impact( edict_t *e1, edict_t *e2, trace_t *trace );
void SV_FreeOldEntities( void );

//
// sv_move.c
//
qboolean SV_MoveStep( edict_t *ent, vec3_t move, qboolean relink );
qboolean SV_MoveTest( edict_t *ent, vec3_t move, qboolean relink );
void SV_MoveToOrigin( edict_t *ed, const vec3_t goal, float dist, int iMode );
qboolean SV_CheckBottom( edict_t *ent, int iMode );
float SV_VecToYaw( const vec3_t src );
void SV_WaterMove( edict_t *ent );

//
// sv_send.c
//
void SV_SendClientMessages( void );
void SV_ClientPrintf( sv_client_t *cl, const char *fmt, ... ) FORMAT_CHECK( 2 );

//
// sv_client.c
//
void SV_RefreshUserinfo( void );
void SV_TogglePause( const char *msg );
qboolean SV_ShouldUpdatePing( sv_client_t *cl );
const char *SV_GetClientIDString( sv_client_t *cl );
sv_client_t *SV_ClientById( int id );
sv_client_t *SV_ClientByName( const char *name );
void SV_FullClientUpdate( sv_client_t *cl, sizebuf_t *msg );
void SV_FullUpdateMovevars( sv_client_t *cl, sizebuf_t *msg );
void SV_GetPlayerStats( sv_client_t *cl, int *ping, int *packet_loss );
void SV_SendServerdata( sizebuf_t *msg, sv_client_t *cl );
void SV_ExecuteClientMessage( sv_client_t *cl, sizebuf_t *msg );
void SV_ConnectionlessPacket( netadr_t from, sizebuf_t *msg );
edict_t *SV_FakeConnect( const char *netname );
void SV_BuildReconnect( sizebuf_t *msg );
int SV_CalcPing( const sv_client_t *cl );
void SV_UpdateServerInfo( void );
void SV_EndRedirect( host_redirect_t *rd );
void SV_RejectConnection( netadr_t from, const char *fmt, ... ) FORMAT_CHECK( 2 );
void SV_GetPlayerCount( int *clients, int *bots );

static inline qboolean SV_HavePassword( void )
{
	if( !COM_StringEmpty( sv_password.string ) && Q_stricmp( sv_password.string, "none" ))
		return true;

	return false;
}

static inline qboolean SV_IsPlayerIndex( int idx )
{
	return idx > 0 && idx <= svs.maxclients ? true : false;
}

//
// sv_cmds.c
//
void SV_InitHostCommands( void );

//
// sv_custom.c
//
void SV_AddToResourceList( resource_t *pResource, resource_t *pList );
void SV_MoveToOnHandList( sv_client_t *cl, resource_t *pResource );
void SV_RemoveFromResourceList( resource_t *pResource );
void SV_ParseConsistencyResponse( sv_client_t *cl, sizebuf_t *msg );
int SV_EstimateNeededResources( sv_client_t *cl );
void SV_ClearResourceList( resource_t *pList );
void SV_BatchUploadRequest( sv_client_t *cl );
void SV_SendResources( sv_client_t *cl, sizebuf_t *msg );
void SV_ClearResourceLists( sv_client_t *cl );
void SV_TransferConsistencyInfo( void );
void SV_RequestMissingResources( void );

//
// sv_filter.c
//
void SV_InitFilter( void );
qboolean SV_CheckIP( netadr_t *adr );
qboolean SV_CheckID( const char *id );

//
// sv_frame.c
//
void SV_InactivateClients( void );
int SV_FindBestBaseline( int index, entity_state_t **baseline, entity_state_t *to, client_frame_t *frame, qboolean player );
void SV_SkipUpdates( void );

//
// sv_game.c
//
qboolean SV_LoadProgs( const char *name );
void SV_UnloadProgs( void );
void SV_FreeEdicts( void );
edict_t *SV_AllocEdict( void );
void SV_FreeEdict( edict_t *pEdict );
void SV_InitEdict( edict_t *pEdict );
const char *SV_ClassName( const edict_t *e );
void SV_CopyTraceToGlobal( trace_t *trace );
qboolean SV_CheckEdict( const edict_t *e, const char *file, const int line );
void SV_SetMinMaxSize( edict_t *e, const float *min, const float *max, qboolean relink );
void SV_PlaybackEventFull( int flags, const edict_t *pInvoker, word eventindex, float delay, float *origin,
	float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 );
int SV_BuildSoundMsg( sizebuf_t *msg, edict_t *ent, int chan, const char *sample, int vol, float attn, int flags, int pitch, const vec3_t pos );
qboolean SV_BoxInPVS( const vec3_t org, const vec3_t absmin, const vec3_t absmax );
void SV_QueueChangeLevel( const char *level, const char *landname );
void SV_WriteEntityPatch( const char *filename );
void SV_SpawnEntities( const char *mapname );
edict_t* SV_CreateNamedEntity( edict_t *ent, string_t className );
string_t SV_AllocString( const char *szValue );
string_t SV_MakeString( const char *szValue );
const char *SV_GetString( string_t iString );
void SV_SetStringArrayMode( qboolean dynamic );
void SV_EmptyStringPool( qboolean clear_stats );
void SV_PrintStr64Stats_f( void );
sv_client_t *SV_ClientFromEdict( const edict_t *pEdict, qboolean spawned_only );
uint SV_MapIsValid( const char *filename, const char *landmark_name );
void SV_StartSound( edict_t *ent, int chan, const char *sample, float vol, float attn, int flags, int pitch );
edict_t *SV_FindGlobalEntity( string_t classname, string_t globalname );
qboolean SV_CreateStaticEntity( struct sizebuf_s *msg, int index );
void SV_SendUserReg( sizebuf_t *msg, sv_user_message_t *user );
int pfnIndexOfEdict( const edict_t *pEdict );
void SV_UpdateBaseVelocity( edict_t *ent );
void SV_RestartAmbientSounds( void );
void SV_RestartDecals( void );
void SV_RestartStaticEnts( void );
int pfnDropToFloor( edict_t* e );
void SV_SetModel( edict_t *ent, const char *name );
int pfnDecalIndex( const char *m );
void SV_CreateDecal( sizebuf_t *msg, const float *origin, int decalIndex, int entityIndex, int modelIndex, int flags, float scale );
qboolean SV_RestoreCustomDecal( struct decallist_s *entry, edict_t *pEdict, qboolean adjacent );

static inline edict_t *SV_EdictNum( int n )
{
	if( likely( n >= 0 && n < GI->max_edicts ))
		return &svgame.edicts[n];
	return NULL;
}

//
// sv_log.c
//
void Log_Close( void );
void Log_Open( void );
void Log_PrintServerVars( void );
void SV_ServerLog_f( void );
void SV_SetLogAddress_f( void );

//
// sv_save.c
//
qboolean SV_SaveGame( const char *pName );
qboolean SV_LoadGame( const char *pName );
int SV_LoadGameState( char const *level );
void SV_ChangeLevel( qboolean loadfromsavedgame, const char *mapname, const char *start, qboolean background );
const char *SV_GetLatestSave( void );
void SV_InitSaveRestore( void );
void SV_ClearGameState( void );

//
// sv_pmove.c
//
void SV_InitClientMove( void );
void SV_RunCmd( sv_client_t *cl, usercmd_t *ucmd, int random_seed );

//
// sv_world.c
//
void SV_ClearWorld( void );
void SV_UnlinkEdict( edict_t *ent );
void SV_ClipMoveToEntity( edict_t *ent, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, trace_t *trace );
void SV_CustomClipMoveToEntity( edict_t *ent, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, trace_t *trace );
trace_t SV_Move( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, int type, edict_t *e, qboolean monsterclip );
trace_t SV_MoveNoEnts( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, int type, edict_t *e );
const char *SV_TraceTexture( edict_t *ent, const vec3_t start, const vec3_t end );
msurface_t *SV_TraceSurface( edict_t *ent, const vec3_t start, const vec3_t end );
trace_t SV_MoveToss( edict_t *tossent, edict_t *ignore );
void SV_LinkEdict( edict_t *ent, qboolean touch_triggers );
int SV_TruePointContents( const vec3_t p );
int SV_PointContents( const vec3_t p );
void SV_SetLightStyle( int style, const char* s, float f );
int SV_LightForEntity( edict_t *pEdict );

//
// sv_query.c
//
void SV_SourceQuery_HandleConnnectionlessPacket( const char *c, netadr_t from );

#endif//SERVER_H
