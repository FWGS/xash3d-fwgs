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

enum
{
	DEV_NONE = 0,
	DEV_NORMAL,
	DEV_EXTENDED
};

enum
{
	D_INFO = 1,	// "-dev 1", shows various system messages
	D_WARN,		// "-dev 2", shows not critical system warnings
	D_ERROR,		// "-dev 3", shows critical warnings
	D_REPORT,		// "-dev 4", special case for game reports
	D_NOTE		// "-dev 5", show system notifications for engine developers
};

typedef enum
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
#include "cvar.h"
#include "con_nprint.h"
#include "crclib.h"
#include "ref_api.h"
#include "fscallback.h"

// PERFORMANCE INFO
#define MIN_FPS         20.0f		// host minimum fps value for maxfps.
#define MAX_FPS         200.0f		// upper limit for maxfps.
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
extern convar_t	cl_allow_levelshots;
extern convar_t	host_developer;
extern convar_t	host_limitlocal;
extern convar_t	host_maxfps;
extern convar_t	sys_timescale;
extern convar_t	cl_filterstuffcmd;
extern convar_t	rcon_password;

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
typedef struct sysinfo_s
{
	string		exeName;		// exe.filename
	string		rcName;		// .rc script name
	string		basedirName;	// name of base directory
	string		gamedll;
	string		clientlib;
} sysinfo_t;

typedef enum
{
	HOST_INIT = 0,	// initalize operations
	HOST_FRAME,	// host running
	HOST_SHUTDOWN,	// shutdown operations
	HOST_ERR_FATAL,	// sys error
	HOST_SLEEP,	// sleeped by different reason, e.g. minimize window
	HOST_NOFOCUS,	// same as HOST_FRAME, but disable mouse
	HOST_CRASHED	// an exception handler called
} host_status_t;

typedef enum
{
	STATE_RUNFRAME = 0,
	STATE_LOAD_LEVEL,
	STATE_LOAD_GAME,
	STATE_CHANGELEVEL,
	STATE_GAME_SHUTDOWN,
} host_state_t;

typedef struct
{
	host_state_t	curstate;
	host_state_t	nextstate;
	char		levelName[MAX_QPATH];
	char		landmarkName[MAX_QPATH];
	qboolean		backgroundMap;
	qboolean		loadGame;
	qboolean		newGame;		// unload the server.dll before start a new map
} game_status_t;

typedef enum
{
	key_console = 0,
	key_game,
	key_menu,
	key_message
} keydest_t;

typedef enum
{
	RD_NONE = 0,
	RD_CLIENT,
	RD_PACKET
} rdtype_t;

#include "net_ws.h"

// console field
typedef struct
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

typedef struct
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

typedef struct
{
	char		model[MAX_QPATH];	// curstate.modelindex = SV_ModelIndex
	vec3_t		tentOffset;	// if attached, client origin + tentOffset = tent origin.
	short		clientIndex;
	float		fadeSpeed;
	float		bounceFactor;
	byte		hitSound;
	qboolean		high_priority;
	float		x, y, z;
	int		flags;
	float		time;

	// base state
	vec3_t		velocity;		// baseline.origin
	vec3_t		avelocity;	// baseline.angles
	int		fadeamount;	// baseline.renderamt
	float		sparklife;	// baseline.framerate
	float		thinkTime;	// baseline.scale

	// current state
	vec3_t		origin;		// entity.origin
	vec3_t		angles;		// entity.angles
	float		renderamt;	// curstate.renderamt
	color24		rendercolor;	// curstate.rendercolor
	int		rendermode;	// curstate.rendermode
	int		renderfx;		// curstate.renderfx
	float		framerate;	// curstate.framerate
	float		frame;		// curstate.frame
	byte		body;		// curstate.body
	byte		skin;		// curstate.skin
	float		scale;		// curstate.scale
} tentlist_t;

typedef enum bugcomp_e
{
	// default mode, we assume that user dlls are not relying on engine bugs
	BUGCOMP_OFF,

	// GoldSrc mode, user dlls are relying on GoldSrc specific bugs
	// but fixing them may break regular Xash games
	BUGCOMP_GOLDSRC,
} bugcomp_t;

typedef struct host_parm_s
{
	HINSTANCE			hInst;
	HANDLE				hMutex;

	host_status_t	status;		// global host state
	game_status_t	game;		// game manager
	uint		type;		// running at
	jmp_buf		abortframe;	// abort current frame
	dword		errorframe;	// to prevent multiple host error
	poolhandle_t mempool;		// static mempool for misc allocations
	string		finalmsg;		// server shutdown final message
	string		downloadfile;	// filename to be downloading
	int		downloadcount;	// how many files remain to downloading
	char		deferred_cmd[128];	// deferred commands
	host_redirect_t	rd;		// remote console

	// command line parms
	int		argc;
	char	**argv;

	double		realtime;		// host.curtime
	double		frametime;	// time between engine frames
	double		realframetime;	// for some system events, e.g. console animations

	uint		framecount;	// global framecount

	// list of unique decal indexes
	char		draw_decals[MAX_DECALS][MAX_QPATH];

	vec3_t		player_mins[MAX_MAP_HULLS];	// 4 hulls allowed
	vec3_t		player_maxs[MAX_MAP_HULLS];	// 4 hulls allowed

	void*			hWnd;		// main window
	qboolean		allow_console;	// allow console in dev-mode or multiplayer game
	qboolean		allow_console_init;	// initial value to allow the console
	qboolean		key_overstrike;	// key overstrike mode
	qboolean		stuffcmds_pending;	// should execute stuff commands
	qboolean		allow_cheats;	// this host will allow cheating
	qboolean		con_showalways;	// show console always (developer and dedicated)
	qboolean		change_game;	// initialize when game is changed
	qboolean		mouse_visible;	// vgui override cursor control (never change outside Platform_SetCursorType!)
	qboolean		shutdown_issued;	// engine is shutting down
	double			force_draw_version_time;
	qboolean		apply_game_config;	// when true apply only to game cvars and ignore all other commands
	qboolean		apply_opengl_config;// when true apply only to opengl cvars and ignore all other commands
	qboolean		config_executed;	// a bit who indicated was config.cfg already executed e.g. from valve.rc
	qboolean		crashed;		// set to true if crashed
	qboolean		daemonized;
	qboolean		enabledll;
	qboolean		textmode;

	// some settings were changed and needs to global update
	qboolean		userinfo_changed;
	qboolean		movevars_changed;
	qboolean		renderinfo_changed;

	char		rootdir[MAX_OSPATH];	// member root directory
	char		rodir[MAX_OSPATH];		// readonly root
	char		gamefolder[MAX_QPATH];	// it's a default gamefolder
	poolhandle_t imagepool;	// imagelib mempool
	poolhandle_t soundpool;	// soundlib mempool

	uint		features;		// custom features that enables by mod-maker request

	// for IN_MouseMove() easy access
	int		window_center_x;
	int		window_center_y;

	struct decallist_s	*decalList;	// used for keep decals, when renderer is restarted or changed
	int		numdecals;

	// bug compatibility level, for very "special" games
	bugcomp_t bugcomp;

	// measure time to first frame
	double starttime;

	// count of sleeps can be inserted between frames
	double pureframetime;
} host_parm_t;

extern host_parm_t	host;
extern sysinfo_t	SI;

#define CMD_SERVERDLL	BIT( 0 )		// added by server.dll
#define CMD_CLIENTDLL	BIT( 1 )		// added by client.dll
#define CMD_GAMEUIDLL	BIT( 2 )		// added by GameUI.dll
#define CMD_PRIVILEGED	BIT( 3 )		// only available in privileged mode
#define CMD_FILTERABLE  BIT( 4 )		// filtered in unprivileged mode if cl_filterstuffcmd is 1
#define CMD_REFDLL	BIT( 5 )		// added by ref.dll

typedef void (*xcommand_t)( void );

//
// filesystem_engine.c
//
qboolean FS_LoadProgs( void );
void FS_Init( void );
void FS_Shutdown( void );

//
// cmd.c
//
void Cbuf_Init( void );
void Cbuf_Clear( void );
void Cbuf_AddText( const char *text );
void Cbuf_AddTextf( const char *text, ... ) _format( 1 );
void Cbuf_AddFilteredText( const char *text );
void Cbuf_InsertText( const char *text );
void Cbuf_ExecStuffCmds( void );
void Cbuf_Execute (void);
qboolean Cmd_CurrentCommandIsPrivileged( void );
int Cmd_Argc( void );
const char *Cmd_Args( void );
const char *Cmd_Argv( int arg );
void Cmd_Init( void );
void Cmd_Unlink( int group );
void Cmd_AddCommand( const char *cmd_name, xcommand_t function, const char *cmd_desc );
void Cmd_AddRestrictedCommand( const char *cmd_name, xcommand_t function, const char *cmd_desc );
void Cmd_AddServerCommand( const char *cmd_name, xcommand_t function );
int Cmd_AddClientCommand( const char *cmd_name, xcommand_t function );
int Cmd_AddGameUICommand( const char *cmd_name, xcommand_t function );
int Cmd_AddRefCommand( const char *cmd_name, xcommand_t function, const char *description );
void Cmd_RemoveCommand( const char *cmd_name );
qboolean Cmd_Exists( const char *cmd_name );
void Cmd_LookupCmds( void *buffer, void *ptr, setpair_t callback );
int Cmd_ListMaps( search_t *t , char *lastmapname, size_t len );
qboolean Cmd_GetMapList( const char *s, char *completedname, int length );
qboolean Cmd_GetDemoList( const char *s, char *completedname, int length );
qboolean Cmd_GetMovieList( const char *s, char *completedname, int length );
void Cmd_TokenizeString( const char *text );
void Cmd_ExecuteString( const char *text );
void Cmd_ForwardToServer( void );
void Cmd_Escape( char *newCommand, const char *oldCommand, int len );

//
// zone.c
//
void Memory_Init( void );
void *_Mem_Realloc( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline );
void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline );
poolhandle_t _Mem_AllocPool( const char *name, const char *filename, int fileline );
void _Mem_FreePool( poolhandle_t *poolptr, const char *filename, int fileline );
void _Mem_EmptyPool( poolhandle_t poolptr, const char *filename, int fileline );
void _Mem_Free( void *data, const char *filename, int fileline );
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
// imagelib
//
#include "com_image.h"

void Image_Setup( void );
void Image_Init( void );
void Image_Shutdown( void );
void Image_AddCmdFlags( uint flags );
rgbdata_t *FS_LoadImage( const char *filename, const byte *buffer, size_t size );
qboolean FS_SaveImage( const char *filename, rgbdata_t *pix );
rgbdata_t *FS_CopyImage( rgbdata_t *in );
void FS_FreeImage( rgbdata_t *pack );
extern const bpc_desc_t PFDesc[];	// image get pixelformat
qboolean Image_Process( rgbdata_t **pix, int width, int height, uint flags, float reserved );
void Image_PaletteHueReplace( byte *palSrc, int newHue, int start, int end, int pal_size );
void Image_PaletteTranslate( byte *palSrc, int top, int bottom, int pal_size );
void Image_SetForceFlags( uint flags );	// set image force flags on loading
size_t Image_DXTGetLinearSize( int type, int width, int height, int depth );
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
typedef enum
{
	WF_UNKNOWN = 0,
	WF_PCMDATA,
	WF_MPGDATA,
	WF_TOTALCOUNT,	// must be last
} sndformat_t;

// soundlib global settings
typedef enum
{
	SL_USE_LERPING	= BIT(0),		// lerping sounds during resample
	SL_KEEP_8BIT	= BIT(1),		// don't expand 8bit sounds automatically up to 16 bit
	SL_ALLOW_OVERWRITE	= BIT(2),		// allow to overwrite stored sounds
} slFlags_t;

// wavdata output flags
typedef enum
{
	// wavdata->flags
	SOUND_LOOPED	= BIT( 0 ),	// this is looped sound (contain cue markers)
	SOUND_STREAM	= BIT( 1 ),	// this is a streaminfo, not a real sound

	// Sound_Process manipulation flags
	SOUND_RESAMPLE	= BIT(12),	// resample sound to specified rate
	SOUND_CONVERT16BIT	= BIT(13),	// change sound resolution from 8 bit to 16
} sndFlags_t;

typedef struct
{
	word	rate;		// num samples per second (e.g. 11025 - 11 khz)
	byte	width;		// resolution - bum bits divided by 8 (8 bit is 1, 16 bit is 2)
	byte	channels;		// num channels (1 - mono, 2 - stereo)
	int	loopStart;	// offset at this point sound will be looping while playing more than only once
	int	samples;		// total samplecount in wav
	uint	type;		// compression type
	uint	flags;		// misc sound flags
	byte	*buffer;		// sound buffer
	size_t	size;		// for bounds checking
} wavdata_t;

//
// soundlib
//
void Sound_Init( void );
void Sound_Shutdown( void );
wavdata_t *FS_LoadSound( const char *filename, const byte *buffer, size_t size );
void FS_FreeSound( wavdata_t *pack );
stream_t *FS_OpenStream( const char *filename );
wavdata_t *FS_StreamInfo( stream_t *stream );
int FS_ReadStream( stream_t *stream, int bytes, void *buffer );
int FS_SetStreamPos( stream_t *stream, int newpos );
int FS_GetStreamPos( stream_t *stream );
void FS_FreeStream( stream_t *stream );
qboolean Sound_Process( wavdata_t **wav, int rate, int width, uint flags );
uint Sound_GetApproxWavePlayLen( const char *filepath );
qboolean Sound_SupportedFileFormat( const char *fileext );

//
// host.c
//
typedef void( *pfnChangeGame )( const char *progname );

qboolean Host_IsQuakeCompatible( void );
void EXPORT Host_Shutdown( void );
int EXPORT Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
int Host_CompareFileTime( int ft1, int ft2 );
void Host_NewInstance( const char *name, const char *finalmsg );
void Host_EndGame( qboolean abort, const char *message, ... ) _format( 2 );
void Host_AbortCurrentFrame( void ) NORETURN;
void Host_WriteServerConfig( const char *name );
void Host_WriteOpenGLConfig( void );
void Host_WriteVideoConfig( void );
void Host_WriteConfig( void );
qboolean Host_IsLocalGame( void );
qboolean Host_IsLocalClient( void );
void Host_ShutdownServer( void );
void Host_Error( const char *error, ... ) _format( 1 );
void Host_PrintEngineFeatures( void );
void Host_Frame( float time );
void Host_InitDecals( void );
void Host_Credits( void );

//
// host_state.c
//
void COM_InitHostState( void );
void COM_NewGame( char const *pMapName );
void COM_LoadLevel( char const *pMapName, qboolean background );
void COM_LoadGame( char const *pSaveFileName );
void COM_ChangeLevel( char const *pNewLevel, char const *pLandmarkName, qboolean background );
void COM_Frame( float time );

/*
==============================================================

CLIENT / SERVER SYSTEMS

==============================================================
*/
void CL_Init( void );
void CL_Shutdown( void );
void Host_ClientBegin( void );
void Host_ClientFrame( void );
int CL_Active( void );

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
void pfnCvar_RegisterServerVariable( cvar_t *variable );
void pfnCvar_RegisterEngineVariable( cvar_t *variable );
cvar_t *pfnCvar_RegisterClientVariable( const char *szName, const char *szValue, int flags );
cvar_t *pfnCvar_RegisterGameUIVariable( const char *szName, const char *szValue, int flags );
char *COM_MemFgets( byte *pMemFile, int fileSize, int *filePos, char *pBuffer, int bufferSize );
void COM_HexConvert( const char *pszInput, int nInputLength, byte *pOutput );
int COM_SaveFile( const char *filename, const void *data, int len );
byte* COM_LoadFileForMe( const char *filename, int *pLength );
qboolean COM_IsSafeFileToDownload( const char *filename );
const char *COM_GetResourceTypeName( resourcetype_t restype );
cvar_t *pfnCVarGetPointer( const char *szVarName );
int pfnDrawConsoleString( int x, int y, char *string );
void pfnDrawSetTextColor( float r, float g, float b );
void pfnDrawConsoleStringLen( const char *pText, int *length, int *height );
void *Cache_Check( poolhandle_t mempool, struct cache_user_s *c );
void COM_TrimSpace( const char *source, char *dest );
void pfnGetModelBounds( model_t *mod, float *mins, float *maxs );
void pfnCVarDirectSet( cvar_t *var, const char *szValue );
int COM_CheckParm( char *parm, char **ppnext );
void pfnGetGameDir( char *szGetGameDir );
int pfnDecalIndex( const char *m );
int pfnGetModelType( model_t *mod );
int pfnIsMapValid( char *filename );
void Con_Reportf( const char *szFmt, ... ) _format( 1 );
void Con_DPrintf( const char *fmt, ... ) _format( 1 );
void Con_Printf( const char *szFmt, ... ) _format( 1 );
int pfnNumberOfEntities( void );
int pfnIsInGame( void );
float pfnTime( void );
#define copystring( s ) _copystring( host.mempool, s, __FILE__, __LINE__ )
#define SV_CopyString( s ) _copystring( svgame.stringspool, s, __FILE__, __LINE__ )
#define freestring( s ) if( s != NULL ) { Mem_Free( s ); s = NULL; }
char *_copystring( poolhandle_t mempool, const char *s, const char *filename, int fileline );

// CS:CS engfuncs (stubs)
void *pfnSequenceGet( const char *fileName, const char *entryName );
void *pfnSequencePickSentence( const char *groupName, int pickMethod, int *picked );
int pfnIsCareerMatch( void );

// Decay engfuncs (stubs)
int pfnGetTimesTutorMessageShown( int mid );
void pfnRegisterTutorMessageShown( int mid );
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
qboolean Cmd_AutocompleteName( const char *source, int arg, char *buffer, size_t bufsize );
void Con_CompleteCommand( field_t *field );
void Cmd_AutoComplete( char *complete_string );
void Cmd_AutoCompleteClear( void );

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

// shared calls
struct physent_s;
struct sv_client_s;
typedef struct sizebuf_s sizebuf_t;
qboolean CL_IsInGame( void );
qboolean CL_IsInMenu( void );
qboolean CL_IsInConsole( void );
qboolean CL_IsThirdPerson( void );
qboolean CL_IsIntermission( void );
qboolean CL_Initialized( void );
char *CL_Userinfo( void );
void CL_LegacyUpdateInfo( void );
void CL_CharEvent( int key );
qboolean CL_DisableVisibility( void );
byte *COM_LoadFile( const char *filename, int usehunk, int *pLength );
int CL_GetDemoComment( const char *demoname, char *comment );
void COM_AddAppDirectoryToSearchPath( const char *pszBaseDir, const char *appName );
int COM_ExpandFilename( const char *fileName, char *nameOutBuffer, int nameOutBufferSize );
struct cmd_s *Cmd_GetFirstFunctionHandle( void );
struct cmd_s *Cmd_GetNextFunctionHandle( struct cmd_s *cmd );
struct cmdalias_s *Cmd_AliasGetList( void );
const char *Cmd_GetName( struct cmd_s *cmd );
void SV_StartSound( edict_t *ent, int chan, const char *sample, float vol, float attn, int flags, int pitch );
void SV_StartMusic( const char *curtrack, const char *looptrack, int position );
void SV_CreateDecal( sizebuf_t *msg, const float *origin, int decalIndex, int entityIndex, int modelIndex, int flags, float scale );
void Log_Printf( const char *fmt, ... ) _format( 1 );
void SV_BroadcastCommand( const char *fmt, ... ) _format( 1 );
qboolean SV_RestoreCustomDecal( struct decallist_s *entry, edict_t *pEdict, qboolean adjacent );
void SV_BroadcastPrintf( struct sv_client_s *ignore, const char *fmt, ... ) _format( 2 );
int R_CreateDecalList( struct decallist_s *pList );
void R_ClearAllDecals( void );
void CL_ClearStaticEntities( void );
qboolean S_StreamGetCurrentState( char *currentTrack, char *loopTrack, int *position );
struct cl_entity_s *CL_GetEntityByIndex( int index );
void CL_ServerCommand( qboolean reliable, const char *fmt, ... ) _format( 2 );
void CL_HudMessage( const char *pMessage );
const char *CL_MsgInfo( int cmd );
void SV_DrawDebugTriangles( void );
void SV_DrawOrthoTriangles( void );
double CL_GetDemoFramerate( void );
qboolean UI_CreditsActive( void );
void CL_StopPlayback( void );
void CL_ExtraUpdate( void );
int CL_GetMaxClients( void );
int SV_GetMaxClients( void );
qboolean CL_IsRecordDemo( void );
qboolean CL_IsTimeDemo( void );
qboolean CL_IsPlaybackDemo( void );
qboolean SV_Initialized( void );
qboolean CL_LoadProgs( const char *name );
void CL_ProcessFile( qboolean successfully_received, const char *filename );
int SV_GetSaveComment( const char *savename, char *comment );
qboolean SV_NewGame( const char *mapName, qboolean loadGame );
void SV_ClipPMoveToEntity( struct physent_s *pe, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, struct pmtrace_s *tr );
void CL_ClipPMoveToEntity( struct physent_s *pe, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, struct pmtrace_s *tr );
void SV_SysError( const char *error_string );
void SV_ShutdownGame( void );
void SV_ExecLoadLevel( void );
void SV_ExecLoadGame( void );
void SV_ExecChangeLevel( void );
void SV_InitGameProgs( void );
void SV_FreeGameProgs( void );
void CL_WriteMessageHistory( void );
void CL_SendCmd( void );
void CL_Disconnect( void );
void CL_ClearEdicts( void );
void CL_Crashed( void );
qboolean CL_NextDemo( void );
char *SV_Serverinfo( void );
void CL_Drop( void );
void Con_Init( void );
void SCR_Init( void );
void SCR_UpdateScreen( void );
void SCR_BeginLoadingPlaque( qboolean is_background );
void SCR_CheckStartupVids( void );
int SCR_GetAudioChunk( char *rawdata, int length );
wavdata_t *SCR_GetMovieInfo( void );
void SCR_Shutdown( void );
void Con_Print( const char *txt );
void Con_NPrintf( int idx, const char *fmt, ... ) _format( 2 );
void Con_NXPrintf( con_nprint_t *info, const char *fmt, ... ) _format( 2 );
void UI_NPrintf( int idx, const char *fmt, ... ) _format( 2 );
void UI_NXPrintf( con_nprint_t *info, const char *fmt, ... ) _format( 2 );
const char *Info_ValueForKey( const char *s, const char *key );
void Info_RemovePrefixedKeys( char *start, char prefix );
qboolean Info_RemoveKey( char *s, const char *key );
qboolean Info_SetValueForKey( char *s, const char *key, const char *value, int maxsize );
qboolean Info_SetValueForKeyf( char *s, const char *key, int maxsize, const char *format, ... ) _format( 4 );
qboolean Info_SetValueForStarKey( char *s, const char *key, const char *value, int maxsize );
qboolean Info_IsValid( const char *s );
void Info_WriteVars( file_t *f );
void Info_Print( const char *s );
void Cmd_WriteVariables( file_t *f );
int Cmd_CheckMapsList( int fRefresh );
void COM_SetRandomSeed( int lSeed );
int COM_RandomLong( int lMin, int lMax );
float COM_RandomFloat( float fMin, float fMax );
qboolean LZSS_IsCompressed( const byte *source );
uint LZSS_GetActualSize( const byte *source );
byte *LZSS_Compress( byte *pInput, int inputLength, uint *pOutputSize );
uint LZSS_Decompress( const byte *pInput, byte *pOutput );
void GL_FreeImage( const char *name );
void VID_InitDefaultResolution( void );
void VID_Init( void );
void UI_SetActiveMenu( qboolean fActive );
void UI_ShowConnectionWarning( void );
void Cmd_Null_f( void );
void Rcon_Print( const char *pMsg );
qboolean COM_ParseVector( char **pfile, float *v, size_t size );
void COM_NormalizeAngles( vec3_t angles );
int COM_FileSize( const char *filename );
void COM_FreeFile( void *buffer );
int COM_CompareFileTime( const char *filename1, const char *filename2, int *iCompare );
char *va( const char *format, ... ) _format( 1 );

// soundlib shared exports
qboolean S_Init( void );
void S_Shutdown( void );
void S_StopSound( int entnum, int channel, const char *soundname );
int S_GetCurrentStaticSounds( soundlist_t *pout, int size );
void S_StopBackgroundTrack( void );
void S_StopAllSounds( qboolean ambient );

// gamma routines
void BuildGammaTable( float gamma, float brightness );
byte LightToTexGamma( byte b );

//
// identification.c
//
void ID_Init( void );
const char *ID_GetMD5( void );
void GAME_EXPORT ID_SetCustomClientID( const char *id );

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

#ifdef REF_DLL
#error "common.h in ref_dll"
#endif

#ifdef __cplusplus
}
#endif
#endif//COMMON_H
