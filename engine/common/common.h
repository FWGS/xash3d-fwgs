/*
common.h - definitions common between client and server
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

#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/*
===================================================================================================================================
Legend:

INTERNAL RESOURCE			- function contain hardcoded path to resource that engine required (optional in most cases)
OBSOLETE, UNUSED			- this function no longer used and leaved here for keep binary compatibility
TODO				- some functionality not impemented but planned
FIXME				- code doesn't working properly in some rare cases
HACKHACK				- unexpected behavior on some input params (or something like)
BUGBUG				- code doesn't working properly in most cases!
TESTTEST				- this code may be unstable and needs to be more tested
g-cont:				- notes from engine author
XASH SPECIFIC			- sort of hack that works only in Xash3D not in GoldSrc
===================================================================================================================================
*/

#include "port.h"

#include "backends.h"
#include "defaults.h"

#include <stdio.h>
#include <stdlib.h> // rand, adbs
#include <stdarg.h> // va

#if !XASH_WIN32
#include <stddef.h> // size_t
#else
#include <sys/types.h> // off_t
#endif

// configuration

//
// check if selected backend not allowed
//
#if XASH_TIMER == TIMER_NULL
	#error "Please select timer backend"
#endif

#if !XASH_DEDICATED
	#if XASH_VIDEO == VIDEO_NULL
		#error "Please select video backend"
	#endif
#endif

#ifndef XASH_SDL

#if XASH_TIMER == TIMER_SDL || XASH_VIDEO == VIDEO_SDL || XASH_SOUND == SOUND_SDL || XASH_INPUT == INPUT_SDL
#error "SDL backends without XASH_SDL not allowed"
#endif

#endif

#define HACKS_RELATED_HLMODS		// some HL-mods works differently under Xash and can't be fixed without some hacks at least at current time

enum dev_level_e
{
	DEV_NONE = 0,
	DEV_NORMAL,
	DEV_EXTENDED
};

typedef enum instance_e
{
	HOST_NORMAL,	// listen server, singleplayer
	HOST_DEDICATED,
} instance_t;

#ifdef XASH_DEDICATED
#define Host_IsDedicated() ( true )
#else
#define Host_IsDedicated() ( host.type == HOST_DEDICATED )
#endif

#include "system.h"
#include "com_model.h"
#include "com_strings.h"
#include "crtlib.h"
#define FSCALLBACK_OVERRIDE_OPEN
#define FSCALLBACK_OVERRIDE_LOADFILE
#define FSCALLBACK_OVERRIDE_MALLOC_LIKE
#include "fscallback.h"
#include "cvar.h"
#include "con_nprint.h"
#include "crclib.h"
#include "ref_api.h"

// PERFORMANCE INFO
#define MIN_FPS         20.0f    // host minimum fps value for maxfps.
#define MAX_FPS_SOFT    200.0f   // soft limit for maxfps.
#define MAX_FPS_HARD    1000.0f  // multiplayer hard limit for maxfps.
#define HOST_FPS		100.0f		// multiplayer games typical fps

#define MAX_FRAMETIME	0.25f
#define MIN_FRAMETIME	0.0001f
#define GAME_FPS		20.0f

#define MAX_CMD_TOKENS	80		// cmd tokens
#define MAX_ENTNUMBER	99999		// for server and client parsing
#define MAX_HEARTBEAT	-99999		// connection time
#define QCHAR_WIDTH		16		// font width

#define CIN_MAIN		0
#define CIN_LOGO		1

#if XASH_LOW_MEMORY == 0
#define MAX_DECALS		512	// touching TE_DECAL messages, etc
#define MAX_STATIC_ENTITIES	3096	// static entities that moved on the client when level is spawn
#elif XASH_LOW_MEMORY == 1
#define MAX_DECALS		512	// touching TE_DECAL messages, etc
#define MAX_STATIC_ENTITIES	128	// static entities that moved on the client when level is spawn
#elif XASH_LOW_MEMORY == 2
#define MAX_DECALS		256	// touching TE_DECAL messages, etc
#define MAX_STATIC_ENTITIES	32	// static entities that moved on the client when level is spawn
#endif

#define MAX_SERVERINFO_STRING 512  // server handles too many settings. expand to 1024?
#define MAX_PRINT_MSG         8192 // how many symbols can handle single call of Con_Printf or Con_DPrintf
#define MAX_TOKEN             2048 // parse token length
#define MAX_USERMSG_LENGTH    2048 // don't modify it's relies on a client-side definitions

#define GameState		(&host.game)

#define FORCE_DRAW_VERSION_TIME 5.0 // draw version for 5 seconds

#ifdef _DEBUG
void DBG_AssertFunction( qboolean fExpr, const char* szExpr, const char* szFile, int szLine, const char* szMessage );
#define Assert( f )		DBG_AssertFunction( f, #f, __FILE__, __LINE__, NULL )
#else
#define Assert( f )
#endif

extern convar_t	gl_vsync;
extern convar_t	scr_loading;
extern convar_t	scr_download;
extern convar_t	cmd_scripting;
extern convar_t	host_allow_materials;
extern convar_t	host_developer;
extern convar_t	host_limitlocal;
extern convar_t	host_maxfps;
extern convar_t	fps_override;
extern convar_t	sys_timescale;
extern convar_t	cl_filterstuffcmd;
extern convar_t	rcon_password;
extern convar_t	hpk_custom_file;
extern convar_t	con_gamemaps;

#define Mod_AllowMaterials() ( host_allow_materials.value != 0.0f && !FBitSet( host.features, ENGINE_DISABLE_HDTEXTURES ))

/*
==============================================================

HOST INTERFACE

==============================================================
*/
/*
========================================================================

GAMEINFO stuff

internal shared gameinfo structure (readonly for engine parts)
========================================================================
*/
typedef enum host_status_e
{
	HOST_INIT = 0,	// initalize operations
	HOST_FRAME,	// host running
	HOST_SHUTDOWN,	// shutdown operations
	HOST_ERR_FATAL,	// sys error
	HOST_SLEEP,	// sleeped by different reason, e.g. minimize window
	HOST_NOFOCUS,	// same as HOST_FRAME, but disable mouse
	HOST_CRASHED	// an exception handler called
} host_status_t;

typedef enum host_state_e
{
	STATE_RUNFRAME = 0,
	STATE_LOAD_LEVEL,
	STATE_LOAD_GAME,
	STATE_CHANGELEVEL,
	STATE_GAME_SHUTDOWN,
} host_state_t;

typedef struct game_status_e
{
	host_state_t	curstate;
	host_state_t	nextstate;
	char		levelName[MAX_QPATH];
	char		landmarkName[MAX_QPATH];
	qboolean		backgroundMap;
	qboolean		loadGame;
	qboolean		newGame;		// unload the server.dll before start a new map
} game_status_t;

typedef enum keydest_e
{
	key_console = 0,
	key_game,
	key_menu,
	key_message
} keydest_t;

typedef enum rdtype_e
{
	RD_NONE = 0,
	RD_CLIENT,
	RD_PACKET
} rdtype_t;

#include "net_ws.h"

// console field
typedef struct field_e
{
	string		buffer;
	int		cursor;
	int		scroll;
	int		widthInChars;
} field_t;

typedef struct host_redirect_s
{
	rdtype_t target;
	char     *buffer;
	size_t   buffersize;
	netadr_t address;
	void     (*flush)( netadr_t adr, rdtype_t target, char *buffer );
	int      lines;
} host_redirect_t;

typedef struct soundlist_e
{
	char		name[MAX_QPATH];
	short		entnum;
	vec3_t		origin;
	float		volume;
	float		attenuation;
	qboolean		looping;
	byte		channel;
	byte		pitch;
	byte		wordIndex;	// current playing word in sentence
	double		samplePos;
	double		forcedEnd;
} soundlist_t;

typedef enum bugcomp_e
{
	// reverts fix for pfnPEntityOfEntIndex for bug compatibility with GoldSrc
	BUGCOMP_PENTITYOFENTINDEX_FLAG = BIT( 0 ),

	// rewrites mod's attempts to write GoldSrc-specific messages into Xash protocol
	// (new wrappers are added by request)
	BUGCOMP_MESSAGE_REWRITE_FACILITY_FLAG = BIT( 1 ),

	// makes sound with no attenuation spatialized, like in GoldSrc
	BUGCOMP_SPATIALIZE_SOUND_WITH_ATTN_NONE = BIT( 2 ),

	// returns full path to the game directory in server's pfnGetGameDir call
	BUGCOMP_GET_GAME_DIR_FULL_PATH = BIT( 3 ),
} bugcomp_t;

typedef struct host_parm_s
{
	// ==== shared through RefAPI's ref_host_t
	double realtime;    // host.curtime
	double frametime;   // time between engine frames
	uint   features;    // custom features that enables by mod-maker request
	// ==== shared through RefAPI's ref_host_t

	host_status_t status;           // global host state
	game_status_t game;             // game manager
	instance_t    type;             // running at
	poolhandle_t  mempool;          // static mempool for misc allocations
	poolhandle_t  imagepool;        // imagelib mempool
	poolhandle_t  soundpool;        // soundlib mempool
	string        downloadfile;     // filename to be downloading
	int           downloadcount;    // how many files remain to downloading
	char          deferred_cmd[128];// deferred commands

	host_redirect_t rd; // remote console

	void   *hWnd;          // main window

	// command line parms
	char **argv;
	int	 argc;

	uint     framecount;     // global framecount
	uint     errorframe;     // to prevent multiple host error
	uint32_t bugcomp; // bug compatibility level, for very "special" games
	double   realframetime;  // for some system events, e.g. console animations
	double   starttime;      // measure time to first frame
	double   pureframetime;  // count of sleeps can be inserted between frames
	double   force_draw_version_time;

	char   draw_decals[MAX_DECALS][MAX_QPATH]; // list of unique decal indexes
	vec3_t player_mins[MAX_MAP_HULLS];         // 4 hulls allowed
	vec3_t player_maxs[MAX_MAP_HULLS];         // 4 hulls allowed

	// for CL_{Push,Pop}TraceBounds
	vec3_t player_mins_backup[MAX_MAP_HULLS];
	vec3_t player_maxs_backup[MAX_MAP_HULLS];
	qboolean trace_bounds_pushed;

	qboolean allow_console;       // allow console in dev-mode or multiplayer game
	qboolean allow_console_init;  // initial value to allow the console
	qboolean key_overstrike;      // key overstrike mode
	qboolean stuffcmds_pending;   // should execute stuff commands
	qboolean allow_cheats;        // this host will allow cheating
	qboolean change_game;         // initialize when game is changed
	qboolean mouse_visible;       // vgui override cursor control (never change outside Platform_SetCursorType!)
	qboolean shutdown_issued;     // engine is shutting down
	qboolean apply_game_config;   // when true apply only to game cvars and ignore all other commands
	qboolean apply_opengl_config; // when true apply only to opengl cvars and ignore all other commands
	qboolean config_executed;     // a bit who indicated was config.cfg already executed e.g. from valve.rc
#if XASH_DLL_LOADER
	qboolean enabledll;
#endif
	qboolean textmode;

	// some settings were changed and needs to global update
	qboolean userinfo_changed;
	qboolean movevars_changed;
	qboolean renderinfo_changed;

	// for IN_MouseMove() easy access
	int      window_center_x;
	int      window_center_y;
	string   gamedll;
	string   clientlib;
	string   menulib;
} host_parm_t;

extern host_parm_t	host;

#define CMD_SERVERDLL   BIT( 0 ) // added by server.dll
#define CMD_CLIENTDLL   BIT( 1 ) // added by client.dll
#define CMD_GAMEUIDLL   BIT( 2 ) // added by GameUI.dll
#define CMD_PRIVILEGED  BIT( 3 ) // only available in privileged mode
#define CMD_FILTERABLE  BIT( 4 ) // filtered in unprivileged mode if cl_filterstuffcmd is 1
#define CMD_REFDLL      BIT( 5 ) // added by ref.dll
#define CMD_OVERRIDABLE BIT( 6 ) // can be removed by DLLs if name matches

typedef void (*xcommand_t)( void );

//
// zone.c
//
void Memory_Init( void );
void _Mem_Free( void *data, const char *filename, int fileline );
void *_Mem_Realloc( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline )
	ALLOC_CHECK( 3 ) WARN_UNUSED_RESULT;
void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
	ALLOC_CHECK( 2 ) MALLOC_LIKE( _Mem_Free, 1 ) WARN_UNUSED_RESULT;
poolhandle_t _Mem_AllocPool( const char *name, const char *filename, int fileline )
	WARN_UNUSED_RESULT;
void _Mem_FreePool( poolhandle_t *poolptr, const char *filename, int fileline );
void _Mem_EmptyPool( poolhandle_t poolptr, const char *filename, int fileline );
void _Mem_Check( const char *filename, int fileline );
qboolean Mem_IsAllocatedExt( poolhandle_t poolptr, void *data );
void Mem_PrintList( size_t minallocationsize );
void Mem_PrintStats( void );

#define Mem_Malloc( pool, size ) _Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) _Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) _Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) _Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) _Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) _Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) _Mem_EmptyPool( pool, __FILE__, __LINE__ )
#define Mem_IsAllocated( mem ) Mem_IsAllocatedExt( NULL, mem )
#define Mem_Check() _Mem_Check( __FILE__, __LINE__ )

//
// filesystem_engine.c
//
void FS_Init( const char *basedir );
void FS_Shutdown( void );
void *FS_GetNativeObject( const char *obj );
int FS_Close( file_t *file );
search_t *FS_Search( const char *pattern, int caseinsensitive, int gamedironly )
	MALLOC_LIKE( _Mem_Free, 1 ) WARN_UNUSED_RESULT;
file_t *FS_Open( const char *filepath, const char *mode, qboolean gamedironly )
	MALLOC_LIKE( FS_Close, 1 ) WARN_UNUSED_RESULT;
byte *FS_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
	MALLOC_LIKE( _Mem_Free, 1 ) WARN_UNUSED_RESULT;
byte *FS_LoadDirectFile( const char *path, fs_offset_t *filesizeptr )
	MALLOC_LIKE( _Mem_Free, 1 ) WARN_UNUSED_RESULT;
void FS_Rescan_f( void );
void FS_LoadGameInfo( void );
void FS_SaveVFSConfig( void );

//
// cmd.c
//
typedef struct cmd_s cmd_t;

static inline int GAME_EXPORT Cmd_Argc( void )
{
	extern int cmd_argc;
	return cmd_argc;
}

static inline const char *GAME_EXPORT RETURNS_NONNULL Cmd_Argv( int arg )
{
	extern int cmd_argc;
	extern char *cmd_argv[MAX_CMD_TOKENS];

	if((uint)arg >= cmd_argc )
		return "";
	return cmd_argv[arg];
}

static inline const char *GAME_EXPORT RETURNS_NONNULL Cmd_Args( void )
{
	extern const char *cmd_args;

	return cmd_args;
}

void Cbuf_Clear( void );
void Cbuf_AddText( const char *text );
void Cbuf_AddTextf( const char *text, ... ) FORMAT_CHECK( 1 );
void Cbuf_AddFilteredText( const char *text );
void Cbuf_InsertText( const char *text );
void Cbuf_InsertTextLen( const char *text, size_t len, size_t requested_len );
void Cbuf_ExecStuffCmds( void );
void Cbuf_Execute (void);
qboolean Cmd_CurrentCommandIsPrivileged( void );
void Cmd_Init( void );
void Cmd_Shutdown( void );
void Cmd_Unlink( int group );
int Cmd_AddCommandEx( const char *cmd_name, xcommand_t function, const char *cmd_desc, int flags, const char *funcname );

static inline int Cmd_AddCommand( const char *cmd_name, xcommand_t function, const char *cmd_desc )
{
	return Cmd_AddCommandEx( cmd_name, function, cmd_desc, 0, __func__ );
}

static inline int Cmd_AddRestrictedCommand( const char *cmd_name, xcommand_t function, const char *cmd_desc )
{
	return Cmd_AddCommandEx( cmd_name, function, cmd_desc, CMD_PRIVILEGED, __func__ );
}

static inline int Cmd_AddCommandWithFlags( const char *cmd_name, xcommand_t function, const char *cmd_desc, int flags )
{
	return Cmd_AddCommandEx( cmd_name, function, cmd_desc, flags, __func__ );
}

void Cmd_RemoveCommand( const char *cmd_name );
cmd_t *Cmd_Exists( const char *cmd_name );
void Cmd_LookupCmds( void *buffer, void *ptr, setpair_t callback );
int Cmd_ListMaps( search_t *t , char *lastmapname, size_t len );
void Cmd_TokenizeString( const char *text );
void Cmd_ExecuteString( const char *text );
void Cmd_ForwardToServer( void );
void Cmd_Escape( char *newCommand, const char *oldCommand, int len );


//
// imagelib
//
#include "com_image.h"

void Image_Setup( void );
void Image_Init( void );
void Image_Shutdown( void );
void Image_AddCmdFlags( uint flags );
void FS_FreeImage( rgbdata_t *pack );
rgbdata_t *FS_LoadImage( const char *filename, const byte *buffer, size_t size ) MALLOC_LIKE( FS_FreeImage, 1 ) WARN_UNUSED_RESULT;
qboolean FS_SaveImage( const char *filename, rgbdata_t *pix );
rgbdata_t *FS_CopyImage( rgbdata_t *in ) MALLOC_LIKE( FS_FreeImage, 1 ) WARN_UNUSED_RESULT;
extern const bpc_desc_t PFDesc[];	// image get pixelformat
qboolean Image_Process( rgbdata_t **pix, int width, int height, uint flags, float reserved );
void Image_PaletteHueReplace( byte *palSrc, int newHue, int start, int end, int pal_size );
void Image_SetForceFlags( uint flags );	// set image force flags on loading
qboolean Image_CustomPalette( void );
void Image_ClearForceFlags( void );
void Image_SetMDLPointer( byte *p );
void Image_CheckPaletteQ1( void );

/*
========================================================================

internal sound format

typically expanded to wav buffer
========================================================================
*/
typedef enum sndformat_e
{
	WF_UNKNOWN = 0,
	WF_PCMDATA,
	WF_MPGDATA,
	WF_VORBISDATA,
	WF_OPUSDATA,
	WF_TOTALCOUNT,	// must be last
} sndformat_t;

// wavdata output flags
typedef enum sndFlags_e
{
	// wavdata->flags
	SOUND_LOOPED	= BIT( 0 ),	// this is looped sound (contain cue markers)
	SOUND_STREAM	= BIT( 1 ),	// this is a streaminfo, not a real sound

	// Sound_Process manipulation flags
	SOUND_RESAMPLE	= BIT( 12 ),	// resample sound to specified rate
} sndFlags_t;

typedef struct wavdata_s
{
	size_t  size;      // for bounds checking
	uint    loopStart; // offset at this point sound will be looping while playing more than only once
	uint    samples;   // total samplecount in wav
	uint    type;      // compression type
	uint    flags;     // misc sound flags
	word    rate;      // num samples per second (e.g. 11025 - 11 khz)
	byte    width;     // resolution - bum bits divided by 8 (8 bit is 1, 16 bit is 2)
	byte    channels;  // num channels (1 - mono, 2 - stereo)
	byte    buffer[];  // sound buffer
} wavdata_t;

//
// soundlib
//
typedef struct stream_s stream_t;
void Sound_Init( void );
void Sound_Shutdown( void );
void FS_FreeSound( wavdata_t *pack );
void FS_FreeStream( stream_t *stream );
wavdata_t *FS_LoadSound( const char *filename, const byte *buffer, size_t size ) MALLOC_LIKE( FS_FreeSound, 1 ) WARN_UNUSED_RESULT;
stream_t *FS_OpenStream( const char *filename ) MALLOC_LIKE( FS_FreeStream, 1 ) WARN_UNUSED_RESULT;
int FS_ReadStream( stream_t *stream, int bytes, void *buffer );
int FS_SetStreamPos( stream_t *stream, int newpos );
int FS_GetStreamPos( stream_t *stream );
qboolean Sound_Process( wavdata_t **wav, int rate, int width, int channels, uint flags );
uint Sound_GetApproxWavePlayLen( const char *filepath );
qboolean Sound_SupportedFileFormat( const char *fileext );

//
// host.c
//
typedef void( *pfnChangeGame )( const char *progname );

qboolean Host_IsQuakeCompatible( void );
void Host_ShutdownWithReason( const char *reason );
int EXPORT Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
void Host_EndGame( qboolean abort, const char *message, ... ) FORMAT_CHECK( 2 );
void Host_AbortCurrentFrame( void ) NORETURN;
void Host_WriteServerConfig( const char *name );
void Host_WriteOpenGLConfig( void );
void Host_WriteVideoConfig( void );
void Host_WriteConfig( void );
void Host_Error( const char *error, ... ) FORMAT_CHECK( 1 );
void Host_ValidateEngineFeatures( uint32_t mask, uint32_t features );
void Host_Frame( double time );
void Host_Credits( void );
void Host_ExitInMain( void );

//
// host_state.c
//
void COM_InitHostState( void );
void COM_NewGame( char const *pMapName );
void COM_LoadLevel( char const *pMapName, qboolean background );
void COM_LoadGame( char const *pSaveFileName );
void COM_ChangeLevel( char const *pNewLevel, char const *pLandmarkName, qboolean background );
void COM_Frame( double time );

/*
==============================================================

CLIENT / SERVER SYSTEMS

==============================================================
*/
#if !XASH_DEDICATED
void CL_Init( void );
void CL_Shutdown( void );
void Host_ClientBegin( void );
void Host_ClientFrame( void );
int CL_Active( void );
#else
static inline void CL_Init( void ) { }
static inline void CL_Shutdown( void ) { }
static inline void Host_ClientBegin( void ) { Cbuf_Execute(); }
static inline void Host_ClientFrame( void ) { }
static inline int CL_Active( void ) { return 0; }
#endif

void SV_Init( void );
void SV_Shutdown( const char *finalmsg );
void SV_ShutdownFilter( void );
void Host_ServerFrame( void );
qboolean SV_Active( void );

/*
==============================================================

	SHARED ENGFUNCS

==============================================================
*/
char *COM_MemFgets( byte *pMemFile, int fileSize, int *filePos, char *pBuffer, int bufferSize );
void COM_HexConvert( const char *pszInput, int nInputLength, byte *pOutput );
byte COM_Nibble( char c );
int COM_SaveFile( const char *filename, const void *data, int len );
byte *COM_LoadFileForMe( const char *filename, int *pLength ) MALLOC_LIKE( free, 1 );
qboolean COM_IsSafeFileToDownload( const char *filename );
cvar_t *pfnCVarGetPointer( const char *szVarName );
int pfnDrawConsoleString( int x, int y, char *string );
void pfnDrawSetTextColor( float r, float g, float b );
void pfnDrawConsoleStringLen( const char *pText, int *length, int *height );
void *Cache_Check( poolhandle_t mempool, struct cache_user_s *c );
void COM_TrimSpace( const char *source, char *dest );
void pfnGetModelBounds( model_t *mod, float *mins, float *maxs );
int COM_CheckParm( char *parm, char **ppnext );
int pfnGetModelType( model_t *mod );
int pfnIsMapValid( char *filename );
void Con_Reportf( const char *szFmt, ... ) FORMAT_CHECK( 1 );
void Con_DPrintf( const char *fmt, ... ) FORMAT_CHECK( 1 );
void Con_Printf( const char *szFmt, ... ) FORMAT_CHECK( 1 );
int pfnNumberOfEntities( void );
int pfnIsInGame( void );
float pfnTime( void );
#define copystring( s ) _copystring( host.mempool, s, __FILE__, __LINE__ )
#define copystringpool( pool, s ) _copystring( pool, s, __FILE__, __LINE__ )
#define SV_CopyString( s ) _copystring( svgame.stringspool, s, __FILE__, __LINE__ )
#define freestring( s ) if( s != NULL ) { Mem_Free( s ); s = NULL; }
char *_copystring( poolhandle_t mempool, const char *s, const char *filename, int fileline );

// CS:CS engfuncs (stubs)
void *pfnSequenceGet( const char *fileName, const char *entryName );
void *pfnSequencePickSentence( const char *groupName, int pickMethod, int *picked );
int pfnIsCareerMatch( void );

// Decay engfuncs (stubs)
void pfnConstructTutorMessageDecayBuffer( int *buffer, int buflen );
void pfnProcessTutorMessageDecayBuffer( int *buffer, int bufferLength );
void pfnResetTutorMessageDecayData( void );


/*
==============================================================

	MISC COMMON FUNCTIONS

==============================================================
*/
#define Z_Malloc( size )		Mem_Malloc( host.mempool, size )
#define Z_Calloc( size )		Mem_Calloc( host.mempool, size )
#define Z_Realloc( ptr, size )	Mem_Realloc( host.mempool, ptr, size )
#define Z_Free( ptr )		if( ptr != NULL ) Mem_Free( ptr )

//
// con_utils.c
//
void Con_CompleteCommand( field_t *field );
void Cmd_AutoComplete( char *complete_string );
void Cmd_AutoCompleteClear( void );
void Host_InitializeConfig( file_t *f, const char *config, const char *description );
void Host_FinalizeConfig( file_t *f, const char *config );

//
// custom.c
//
void COM_ClearCustomizationList( customization_t *pHead, qboolean bCleanDecals );
qboolean COM_CreateCustomization( customization_t *pHead, resource_t *pRes, int playernum, int flags, customization_t **pCust, int *nLumps );
int COM_SizeofResourceList( resource_t *pList, resourceinfo_t *ri );

//
// cfgscript.c
//
int CSCR_LoadDefaultCVars( const char *scriptfilename );
int CSCR_WriteGameCVars( file_t *cfg, const char *scriptfilename );

//
// hpak.c
//
const char *COM_ResourceTypeFromIndex( int index );
void HPAK_Init( void );
qboolean HPAK_GetDataPointer( const char *filename, struct resource_s *pRes, byte **buffer, int *size );
qboolean HPAK_ResourceForHash( const char *filename, byte *hash, struct resource_s *pRes );
void HPAK_AddLump( qboolean queue, const char *filename, struct resource_s *pRes, byte *data, file_t *f );
void HPAK_RemoveLump( const char *name, resource_t *resource );
void HPAK_CheckIntegrity( const char *filename );
void HPAK_CheckSize( const char *filename );
void HPAK_FlushHostQueue( void );

#include "avi/avi.h"

//
// input.c
//

#define INPUT_DEVICE_MOUSE (1<<0)
#define INPUT_DEVICE_TOUCH (1<<1)
#define INPUT_DEVICE_JOYSTICK (1<<2)
#define INPUT_DEVICE_VR (1<<3)


typedef enum connprotocol_e
{
	PROTO_CURRENT = 0, // Xash3D 49
	PROTO_LEGACY, // Xash3D 48
	PROTO_QUAKE, // Quake 15
	PROTO_GOLDSRC, // GoldSrc 48
} connprotocol_t;

// shared calls
struct physent_s;
struct sv_client_s;
typedef struct sizebuf_s sizebuf_t;
int SV_GetMaxClients( void );

#if !XASH_DEDICATED
qboolean CL_Initialized( void );
qboolean CL_IsInGame( void );
qboolean CL_IsInConsole( void );
qboolean CL_IsIntermission( void );
qboolean CL_DisableVisibility( void );
qboolean CL_IsRecordDemo( void );
qboolean CL_IsPlaybackDemo( void );
qboolean UI_CreditsActive( void );
int CL_GetMaxClients( void );
#else
static inline qboolean CL_Initialized( void ) { return false; }
static inline qboolean CL_IsInGame( void ) { return true; } // always true for dedicated
static inline qboolean CL_IsInConsole( void ) { return false; }
static inline qboolean CL_IsIntermission( void ) { return false; }
static inline qboolean CL_DisableVisibility( void ) { return false; }
static inline qboolean CL_IsRecordDemo( void ) { return false; }
static inline qboolean CL_IsPlaybackDemo( void ) { return false; }
static inline qboolean UI_CreditsActive( void ) { return false; }
static inline int CL_GetMaxClients( void ) { return SV_GetMaxClients(); }
#endif

char *CL_Userinfo( void );
void CL_CharEvent( int key );
byte *COM_LoadFile( const char *filename, int usehunk, int *pLength ) MALLOC_LIKE( free, 1 );
struct cmd_s *Cmd_GetFirstFunctionHandle( void );
struct cmd_s *Cmd_GetNextFunctionHandle( struct cmd_s *cmd );
struct cmdalias_s *Cmd_AliasGetList( void );
const char *Cmd_GetName( struct cmd_s *cmd );
void Log_Printf( const char *fmt, ... ) FORMAT_CHECK( 1 );
void SV_BroadcastCommand( const char *fmt, ... ) FORMAT_CHECK( 1 );
void SV_BroadcastPrintf( struct sv_client_s *ignore, const char *fmt, ... ) FORMAT_CHECK( 2 );
void CL_ClearStaticEntities( void );
qboolean S_StreamGetCurrentState( char *currentTrack, size_t currentTrackSize, char *loopTrack, size_t loopTrackSize, int *position );
void CL_ServerCommand( qboolean reliable, const char *fmt, ... ) FORMAT_CHECK( 2 );
void CL_UpdateInfo( const char *key, const char *value );
void CL_HudMessage( const char *pMessage );
const char *CL_MsgInfo( int cmd );
void SV_DrawDebugTriangles( void );
void SV_DrawOrthoTriangles( void );
double CL_GetDemoFramerate( void );
void CL_StopPlayback( void );
qboolean SV_Initialized( void );
void CL_ProcessFile( qboolean successfully_received, const char *filename );
int SV_GetSaveComment( const char *savename, char *comment );
void SV_ClipPMoveToEntity( struct physent_s *pe, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, struct pmtrace_s *tr );
void CL_ClipPMoveToEntity( struct physent_s *pe, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, struct pmtrace_s *tr );
void SV_SysError( const char *error_string );
void SV_ShutdownGame( void );
void SV_ExecLoadLevel( void );
void SV_ExecLoadGame( void );
void SV_ExecChangeLevel( void );
void CL_WriteMessageHistory( void );
void CL_Disconnect( void );
void CL_ClearEdicts( void );
void CL_Crashed( void );
char *SV_Serverinfo( void );
void CL_Drop( void );
void Con_Init( void );
void SCR_Init( void );
void SCR_UpdateScreen( void );
void SCR_BeginLoadingPlaque( qboolean is_background );
void SCR_CheckStartupVids( void );
void SCR_Shutdown( void );
void Con_Print( const char *txt );
void Con_NPrintf( int idx, const char *fmt, ... ) FORMAT_CHECK( 2 );
void Con_NXPrintf( con_nprint_t *info, const char *fmt, ... ) FORMAT_CHECK( 2 );
void UI_NPrintf( int idx, const char *fmt, ... ) FORMAT_CHECK( 2 );
void UI_NXPrintf( con_nprint_t *info, const char *fmt, ... ) FORMAT_CHECK( 2 );
const char *Info_ValueForKey( const char *s, const char *key ) RETURNS_NONNULL NONNULL;
void Info_RemovePrefixedKeys( char *start, char prefix );
qboolean Info_RemoveKey( char *s, const char *key );
qboolean Info_SetValueForKey( char *s, const char *key, const char *value, int maxsize );
qboolean Info_SetValueForKeyf( char *s, const char *key, int maxsize, const char *format, ... ) FORMAT_CHECK( 4 );
qboolean Info_SetValueForStarKey( char *s, const char *key, const char *value, int maxsize );
qboolean Info_IsValid( const char *s );
void Info_WriteVars( file_t *f );
void Info_Print( const char *s );
int Cmd_CheckMapsList( int fRefresh );
void COM_SetRandomSeed( int lSeed );
int COM_RandomLong( int lMin, int lMax );
float COM_RandomFloat( float fMin, float fMax );
qboolean LZSS_IsCompressed( const byte *source, size_t input_len );
uint LZSS_GetActualSize( const byte *source, size_t input_len );
byte *LZSS_Compress( byte *pInput, int inputLength, uint *pOutputSize );
uint LZSS_Decompress( const byte *pInput, byte *pOutput, size_t input_len, size_t output_len );
void GL_FreeImage( const char *name );
void VID_InitDefaultResolution( void );
void VID_Init( void );
void UI_SetActiveMenu( qboolean fActive );
void UI_ShowConnectionWarning( void );
void Cmd_Null_f( void );
void Rcon_Print( host_redirect_t *rd, const char *pMsg );
qboolean COM_ParseVector( char **pfile, float *v, size_t size );
int COM_FileSize( const char *filename );
void COM_FreeFile( void *buffer );
int pfnCompareFileTime( const char *path1, const char *path2, int *retval );
char *va( const char *format, ... ) FORMAT_CHECK( 1 ) RETURNS_NONNULL;
qboolean CRC32_MapFile( dword *crcvalue, const char *filename, qboolean multiplayer );

static inline void COM_NormalizeAngles( vec3_t angles )
{
	int i;

	for( i = 0; i < 3; i++ )
	{
		if( angles[i] > 180.0f )
			angles[i] -= 360.0f;
		else if( angles[i] < -180.0f )
			angles[i] += 360.0f;
	}
}

#if !XASH_DEDICATED
connprotocol_t CL_Protocol( void );
#else
static inline connprotocol_t CL_Protocol( void )
{
	return PROTO_CURRENT;
}
#endif

static inline qboolean Host_IsLocalGame( void )
{
	if( SV_Active( ))
		return SV_GetMaxClients() == 1 ? true : false;
	return CL_GetMaxClients() == 1 ? true : false;
}

static inline qboolean Host_IsLocalClient( void )
{
	return CL_Initialized( ) && SV_Initialized( ) ? true : false;
}

// soundlib shared exports
qboolean S_Init( void );
void S_Shutdown( void );
void S_StopSound( int entnum, int channel, const char *soundname );
int S_GetCurrentStaticSounds( soundlist_t *pout, int size );
void S_StopBackgroundTrack( void );
void S_StopAllSounds( qboolean ambient );

// gamma routines
byte LightToTexGamma( byte b );
byte TextureToGamma( byte );
uint ScreenGammaTable( uint );
uint LinearGammaTable( uint );
void V_Init( void );
void V_CheckGamma( void );
void V_CheckGammaEnd( void );
intptr_t V_GetGammaPtr( int parm );

//
// masterlist.c
//
void NET_InitMasters( void );
void NET_SaveMasters( void );
qboolean NET_SendToMasters( netsrc_t sock, size_t len, const void *data );
qboolean NET_IsMasterAdr( netadr_t adr );
void NET_MasterHeartbeat( void );
void NET_MasterClear( void );
void NET_MasterShutdown( void );
qboolean NET_GetMaster( netadr_t from, uint *challenge, double *last_heartbeat );

//
// munge.c
//
void COM_Munge( byte *data, size_t len, int seq );
void COM_UnMunge( byte *data, size_t len, int seq );
void COM_Munge2( byte *data, size_t len, int seq );
void COM_UnMunge2( byte *data, size_t len, int seq );
void COM_Munge3( byte *data, size_t len, int seq );
void COM_UnMunge3( byte *data, size_t len, int seq );

//
// sounds.c
//
typedef enum soundlst_group_e
{
	BouncePlayerShell = 0,
	BounceWeaponShell,
	BounceConcrete,
	BounceGlass,
	BounceMetal,
	BounceFlesh,
	BounceWood,
	Ricochet,
	Explode,
	PlayerWaterEnter,
	PlayerWaterExit,
	EntityWaterEnter,
	EntityWaterExit,

	SoundList_Groups // must be last
} soundlst_group_t;

int SoundList_Count( soundlst_group_t group );
const char *SoundList_GetRandom( soundlst_group_t group );
const char *SoundList_Get( soundlst_group_t group, int idx );
void SoundList_Init( void );
void SoundList_Shutdown( void );

#ifdef REF_DLL
#error "common.h in ref_dll"
#endif

#ifdef __cplusplus
}
#endif
#endif//COMMON_H
