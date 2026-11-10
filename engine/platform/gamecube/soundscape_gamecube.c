/*
 * CapFaces Lost Coast soundscape (Source env_soundscape → GoldSrc/ASND).
 *
 * Shoreline bed streams via S_StartBackgroundTrack (WAV too large for SFX budget).
 * Tip-safe sound/sc_*.wav oneshots ride the gameplay SFX budget for birds/wind.
 */
#include "platform/platform.h"

#if XASH_GAMECUBE

#include "common.h"
#include "client.h"
#include "sound.h"

#include <ogc/system.h> /* SYS_Report */

extern void S_StartBackgroundTrack( const char *intro, const char *loop, int position, qboolean fullpath );
extern void S_GCAudioRestartVoice( void );
extern void S_GCLogAudioTelemetry( const char *tag );
extern void SND_UpdateSound( void );
extern void FS_ClearFindMissCache( void );

/* Flat media/ paths — deep sound/ambient/ trees break CapFaces GC FST mount. */
#define GC_SC_SHORE_BED		"media/sc_shore.wav"
#define GC_SC_DISTANT_BED	"media/sc_distant.wav"
#define GC_SC_NAME		"coast_lostcoast.general_shoreline"

static qboolean gc_sc_armed;
static qboolean gc_sc_started;
static qboolean gc_sc_ready_logged;
static double   gc_sc_next_oneshot;
static int      gc_sc_oneshot_i;
static int      gc_sc_tick;

/* media/ oneshots — streamed as short background cuts (no sound/ SFX tree). */
static const char *const gc_sc_oneshots[] = {
	"media/sc_bird1.wav",
	"media/sc_bird2.wav",
	"media/sc_bird3.wav",
	"media/sc_wind1.wav",
	"media/sc_wind2.wav",
};

static void GC_CapFacesSoundscapeEnsureMusic( void )
{
	convar_t *vol = Cvar_FindVar( "MP3Volume" );
	if( vol && vol->value < 0.35f )
		Cvar_SetValue( "MP3Volume", 0.7f );
}

static void GC_CapFacesSoundscapeStartBed( const char *path )
{
	int i;

	/* Beds live in soundscape.pk3 — do not gate on FS_FileExists (ZIP miss). */
	FS_ClearFindMissCache();
	S_StartBackgroundTrack( path, path, 0, true );

	/* Kick ASND voice so streamed PCM reaches the DMA ring. */
	if( snd.initialized && (int)( snd.paintedtime - snd.soundtime ) > 64 )
		snd.paintedtime = snd.soundtime;
	for( i = 0; i < 6; i++ )
		SND_UpdateSound();
	S_GCAudioRestartVoice();
	for( i = 0; i < 6; i++ )
		SND_UpdateSound();
}

void GC_CapFacesSoundscapeArm( void )
{
	const char *bed;

	if( !Sys_CheckParm( "-gccapfacessmoke" ))
		return;
	gc_sc_armed = true;

	/* Log first — OpenStream from soundscape.pk3 can stall; prove arming. */
	if( !gc_sc_started )
	{
		bed = GC_SC_SHORE_BED;
		gc_sc_started = true;
		gc_sc_next_oneshot = host.realtime + 4.0;
		gc_sc_oneshot_i = 0;
		SYS_Report( "Xash3D GameCube: SOUNDSCAPE_START %s bed=%s\n", GC_SC_NAME, bed );
		Con_Reportf( "Xash3D GameCube: SOUNDSCAPE_START %s bed=%s\n", GC_SC_NAME, bed );
		GC_CapFacesSoundscapeEnsureMusic();
		GC_CapFacesSoundscapeStartBed( bed );
		S_GCLogAudioTelemetry( "soundscape-start" );
	}
	if( !gc_sc_ready_logged )
	{
		gc_sc_ready_logged = true;
		SYS_Report( "Xash3D GameCube: SOUNDSCAPE_READY %s\n", GC_SC_NAME );
		Con_Reportf( "Xash3D GameCube: SOUNDSCAPE_READY %s\n", GC_SC_NAME );
	}
}

void GC_CapFacesSoundscapeFrame( void )
{
	int i;
	int nshots;

	if( !gc_sc_armed || !Sys_CheckParm( "-gccapfacessmoke" ))
		return;
	if( !gc_sc_started )
		GC_CapFacesSoundscapeArm();
	if( cls.state != ca_active )
		return;

	nshots = (int)( sizeof( gc_sc_oneshots ) / sizeof( gc_sc_oneshots[0] ));
	if( nshots > 0 && host.realtime >= gc_sc_next_oneshot )
	{
		const char *name = gc_sc_oneshots[gc_sc_oneshot_i % nshots];
		gc_sc_oneshot_i++;
		/* Short media clip, then restore the shoreline bed loop. */
		FS_ClearFindMissCache();
		S_StartBackgroundTrack( name, GC_SC_SHORE_BED, 0, true );
		for( i = 0; i < 4; i++ )
			SND_UpdateSound();
		Con_Reportf( "Xash3D GameCube: SOUNDSCAPE_ONESHOT %s\n", name );
		SYS_Report( "Xash3D GameCube: SOUNDSCAPE_ONESHOT %s\n", name );
		gc_sc_next_oneshot = host.realtime + 6.0 + (double)( gc_sc_oneshot_i & 3 );
	}

	gc_sc_tick++;
	if( ( gc_sc_tick % 64 ) == 0 )
		S_GCLogAudioTelemetry( "soundscape-tick" );
}

#else

void GC_CapFacesSoundscapeArm( void )
{
}

void GC_CapFacesSoundscapeFrame( void )
{
}

#endif
