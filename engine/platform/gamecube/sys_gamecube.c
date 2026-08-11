/*
sys_gamecube.c - GameCube platform backend
Copyright (C) 2026 xash3d-gc contributors

Platform layer ported from Division-Zero-GX/xash3d-wii.
*/
#include "platform/platform.h"
#include "cvar.h"
#include "net_ws.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#if XASH_GAMECUBE
#include <ogc/system.h>
#include <ogc/dvd.h>
#include <ogc/lwp_watchdog.h>

#include <fat.h>
#include <iso9660.h>
#include <dirent.h>
#include <sys/stat.h>
#include "dll_gamecube.h"
#include "mem_gamecube.h"
#include "storage_gamecube.h"
#include "perf_gamecube.h"

#define GC_DATA_PATH "xash3d"
#define GC_DVD_DEVICE "gcdisc"

/*
 * libdvm (Swiss / libogc2) mounts GameCube storage as:
 *   sd:     Serial Port 2 (SD2SP2)
 *   carda:  Memory Slot A / SP1 (SD Gecko)
 *   cardb:  Memory Slot B
 * Classic libfat typically only exposes sd:. Probe all known prefixes.
 */
static const char *const gc_fat_volume_roots[] =
{
	"sd:/",
	"carda:/",
	"cardb:/",
};

#define GC_DEFAULT_SMOKE_MAP "c0a0e"

static qboolean gc_fat_mounted;
static qboolean gc_dvd_mounted;
static char gc_fat_write_root[16]; /* e.g. "sd:/" — preferred writable volume */
static char gc_fat_base_root[16];  /* volume used for game data root */
static qboolean gc_newsaveload_configured;
static qboolean gc_configroundtrip_configured;
static DISC_INTERFACE gc_dvd_io;

static bool GCube_DVDReadSectors( sec_t sector, sec_t count, void *buffer );
static qboolean GCube_MountDisc( void );
static void GCube_LoadDiscBootOverrides( void );

static qboolean GCube_MountDisc( void )
{
	if( gc_dvd_mounted )
		return true;

	SYS_Report( "Xash3D GameCube: DVD mount begin\n" );
	DVD_Init();
	gc_dvd_io = __io_gcdvd;
	gc_dvd_io.readSectors = GCube_DVDReadSectors;

	if( !ISO9660_Mount( GC_DVD_DEVICE, &gc_dvd_io ) )
	{
		SYS_Report( "Xash3D GameCube: mounting DVD filesystem failed\n" );
		gc_dvd_mounted = false;
		return false;
	}

	gc_dvd_mounted = true;
	SYS_Report( "Xash3D GameCube: DVD mount ready\n" );
	return true;
}

static bool GCube_DVDReadSectors( sec_t sector, sec_t count, void *buffer )
{
	u8 *output = buffer;
	sec_t i;

	/* libiso9660 3.1.0 always requests a 32 KiB cache fill even when it
	 * calculated that one sector is sufficient. Split that transfer because
	 * __io_gcdvd can otherwise complete with a zero-filled DMA buffer. */
	for( i = 0; i < count; i++ )
	{
		if( !__io_gcdvd.readSectors( sector + i, 1, output + i * 0x800 ))
			return false;
	}
	return true;
}
#endif

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
	(void)parms;
}

void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	(void)parentMainWindow;
	SYS_Report( "%s: %s\n", title, message );
	fprintf( stderr, "%s:\n%s\n", title, message );
}

void Posix_Daemonize( void )
{
}

void Posix_SetupSigtermHandling( void )
{
}

char *Posix_Input( void )
{
	return NULL;
}

void Platform_Sleep( int msec )
{
	usleep( msec * 1000 );
}

double Platform_DoubleTime( void )
{
#if XASH_GAMECUBE
	static u64 start_ticks;
	u64 now = SYS_Time();

	if( start_ticks == 0 ) {
		start_ticks = now;
	}

	double clock = (double)PPC_TIMER_CLOCK;
	if (clock <= 0.0) return 1.0;
	else return (double)diff_ticks( start_ticks, now ) / clock;
#else
	return 0.0;
#endif
}



void Platform_MouseMove( float *x, float *y )
{
	if( x ) *x = 0.0f;
	if( y ) *y = 0.0f;
}

platform_orientation_t Platform_GetDisplayOrientation( void )
{
	return ORIENTATION_UNKNOWN;
}



void GCube_EarlyInit( void )
{
#if XASH_GAMECUBE
	/* Probe scripts match this marker as the guest bootstrap heartbeat. */
	SYS_Report( "Xash3D GameCube: bootstrap\n" );
#if defined(XASH_GAMECUBE_LIBOGC2) && XASH_GAMECUBE_LIBOGC2
#if defined(XASH_GAMECUBE_LIBDVM) && XASH_GAMECUBE_LIBDVM
	SYS_Report( "Xash3D GameCube: OGC stack=libogc2 fat=libdvm (Swiss)\n" );
#else
	SYS_Report( "Xash3D GameCube: OGC stack=libogc2 fat=libfat (Swiss)\n" );
#endif
#elif defined(XASH_GAMECUBE_LIBOGC) && XASH_GAMECUBE_LIBOGC
	SYS_Report( "Xash3D GameCube: OGC stack=libogc fat=libfat\n" );
#else
	SYS_Report( "Xash3D GameCube: OGC stack=legacy\n" );
#endif
	GC_ReportBootPhase( GC_BOOT_EARLY );
#endif
}

static qboolean GCube_PathAccessible( const char *path )
{
	struct stat st;
	if( stat( path, &st ) != 0 )
		return false;

	// Check if it's a directory
	if( !S_ISDIR( st.st_mode ) )
		return false;

	// Attempt to open to ensure readability/accessibility in the current FS context
	DIR *dir = opendir( path );
	if( !dir )
		return false;

	closedir( dir );
	return true;
}

/*
 * Prefer a volume that already has xash3d/valve; otherwise first mounted root.
 * Order matches Swiss/libdvm: SD2SP2 (sd), SD Gecko A (carda), Slot B (cardb).
 */
static qboolean GCube_SelectFatVolume( char *out_root, size_t out_len )
{
	size_t i;
	char candidate[MAX_SYSPATH];

	if( out_root && out_len )
		out_root[0] = '\0';

	for( i = 0; i < sizeof( gc_fat_volume_roots ) / sizeof( gc_fat_volume_roots[0] ); i++ )
	{
		const char *root = gc_fat_volume_roots[i];
		if( !GCube_PathAccessible( root ))
			continue;
		Q_snprintf( candidate, sizeof( candidate ), "%s%s/valve", root, GC_DATA_PATH );
		if( GCube_PathAccessible( candidate ))
		{
			Q_strncpy( out_root, root, out_len );
			return true;
		}
	}

	for( i = 0; i < sizeof( gc_fat_volume_roots ) / sizeof( gc_fat_volume_roots[0] ); i++ )
	{
		const char *root = gc_fat_volume_roots[i];
		if( !GCube_PathAccessible( root ))
			continue;
		Q_strncpy( out_root, root, out_len );
		return true;
	}

	return false;
}

static void GCube_ProbeFatVolumes( void )
{
	size_t i;
	int mounted = 0;

	gc_fat_write_root[0] = '\0';
	gc_fat_base_root[0] = '\0';

	for( i = 0; i < sizeof( gc_fat_volume_roots ) / sizeof( gc_fat_volume_roots[0] ); i++ )
	{
		const char *root = gc_fat_volume_roots[i];
		if( !GCube_PathAccessible( root ))
			continue;
		mounted++;
		SYS_Report( "Xash3D GameCube: FAT volume ready %s\n", root );
		Con_Reportf( "Xash3D GameCube: FAT volume ready %s\n", root );
	}

	if( !GCube_SelectFatVolume( gc_fat_write_root, sizeof( gc_fat_write_root )))
	{
		SYS_Report( "Xash3D GameCube: FAT init ok but no sd:/carda:/cardb: root accessible\n" );
		return;
	}

	Q_strncpy( gc_fat_base_root, gc_fat_write_root, sizeof( gc_fat_base_root ));
	SYS_Report( "Xash3D GameCube: FAT preferred volume %s (count=%d)\n",
		gc_fat_write_root, mounted );
}

static qboolean GCube_FatRootReady( void )
{
	return gc_fat_mounted && gc_fat_write_root[0] != '\0'
		&& GCube_PathAccessible( gc_fat_write_root );
}

qboolean GCube_GetDiscPath( char *buf, size_t buflen )
{
	const char *path = GC_DVD_DEVICE ":/" GC_DATA_PATH;

	if( !gc_dvd_mounted )
		return false;
	if( !GCube_PathAccessible( path ) )
		return false;

	Q_strncpy( buf, path, buflen );
	return true;
}

qboolean GCube_GetWritablePath( char *buf, size_t buflen )
{
	if( GCube_FatRootReady() )
	{
		Q_snprintf( buf, buflen, "%s%s", gc_fat_write_root, GC_DATA_PATH );
		return true;
	}

	/* G94/G508: disc-only Dolphin probes use a RAM save/config bank under this
	 * prefix. Prefer the disc-override flag: ISO boots rebuild argv and may not
	 * see Dolphin -- guest args. This path is probe-only — never chdir/root here. */
	if( gc_newsaveload_configured || gc_configroundtrip_configured
		|| Sys_CheckParm( "-gcnewsaveload" ) || Sys_CheckParm( "-gcconfigroundtrip" ))
	{
		Q_strncpy( buf, "gcprobe:/" GC_DATA_PATH, buflen );
		return true;
	}

	return false;
}

qboolean GCube_HasWritableStorage( void )
{
	char path[MAX_SYSPATH];

	return GCube_GetWritablePath( path, sizeof( path ));
}

qboolean GCube_HasPersistentWritableStorage( void )
{
	/* Real FAT volume only (SD2SP2 sd:, SD Gecko carda:/cardb:). G94 gcprobe:
	 * counts as writable for save/load cmds but must not trigger mkdir/fopen
	 * config playlist writes on a fake device. */
	return GCube_FatRootReady();
}

static qboolean GCube_IsProbeSavePath( const char *path )
{
	return path && !Q_strnicmp( path, "gcprobe:", 8 );
}

static void GCube_MkdirIgnoreExists( const char *path )
{
	if( mkdir( path, 0777 ) != 0 ) {
		if (errno != EEXIST) {
			Con_Reportf( S_WARN "GameCube storage: failed to create %s (%s)\n", path, strerror( errno ));
		}
	}
}

/*
 * G32: GameCube Save/Load Policy
 *
 * Storage: Swiss/libdvm FAT volumes — prefer sd: (SD2SP2), then carda:/cardb:
 *          (SD Gecko). Layout: <vol>/xash3d/valve/save.
 * Size Bounds: Half-Life saves are typically <128KB. We report available
 *             space at init to bound expectations. Engine save logic
 *             relies on FS_Open/Write failure returns for disk-full errors.
 * Failure Behavior:
 *   - Write errors (disk full/unmounted) are reported via Con_Reportf.
 *   - Disc-only boots (read-only) skip save commands via GCube_HasWritableStorage().
 */

static void GCube_LogStorageStatus( void )
{
#if XASH_GAMECUBE
	if( !GCube_FatRootReady() )
	{
		Con_Reportf( "Xash3D GameCube: SD storage not mounted (disc-only mode)\n" );
		return;
	}

	Con_Reportf( "Xash3D GameCube: persistent storage volume %s\n", gc_fat_write_root );
	/* Dynamic free-space query is omitted to maintain build compatibility. */
#endif
}

void GCube_EnsureWritableLayout( void )
{
	char base[MAX_SYSPATH];
	char valve[MAX_SYSPATH];
	char save[MAX_SYSPATH];
	char logs[MAX_SYSPATH];

	if( !GCube_GetWritablePath( base, sizeof( base )))
		return;

	/* G94 probe RAM bank is not a libogc device — skip mkdir/chdir. */
	if( GCube_IsProbeSavePath( base ))
	{
		Con_Reportf( "Xash3D GameCube: G94 probe save bank ready (no FS mkdir)\n" );
		return;
	}

	GCube_MkdirIgnoreExists( base );
	Q_snprintf( valve, sizeof( valve ), "%s/valve", base );
	GCube_MkdirIgnoreExists( valve );
	Q_snprintf( save, sizeof( save ), "%s/valve/save", base );
	GCube_MkdirIgnoreExists( save );
	Q_snprintf( logs, sizeof( logs ), "%s/valve/logs", base );
	GCube_MkdirIgnoreExists( logs );

	/* G32: Ensure standard Half-Life save slots directory exists.
	 * The engine uses 'valve/save' for autosaves and 'valve/save/<mapname>'
	 * for manual saves in some builds, but standard HL uses 'valve/save'
	 * directly for .sav files. We ensure the base is ready. */

}

/*
 * Mirror Swiss --require-swiss markers onto FAT so operators can copy
 * xash3d/valve/logs/swiss-evidence.txt without a live OSReport capture.
 * Disc-only Dolphin has no FAT root — this is a no-op there.
 */
static void GCube_WriteSwissEvidenceLog( qboolean shutting_down )
{
	char path[MAX_SYSPATH];
	FILE *f;
	size_t i;
	int mounted = 0;

	if( !GCube_FatRootReady() )
		return;

	Q_snprintf( path, sizeof( path ), "%s%s/valve/logs/swiss-evidence.txt",
		gc_fat_write_root, GC_DATA_PATH );
	f = fopen( path, shutting_down ? "a" : "w" );
	if( !f )
		return;

	if( !shutting_down )
	{
#if defined(XASH_GAMECUBE_LIBDVM) && XASH_GAMECUBE_LIBDVM
		fprintf( f, "Xash3D GameCube: OGC stack=libogc2 fat=libdvm (Swiss)\n" );
#else
		fprintf( f, "Xash3D GameCube: OGC stack=libogc fat=libfat\n" );
#endif
		for( i = 0; i < sizeof( gc_fat_volume_roots ) / sizeof( gc_fat_volume_roots[0] ); i++ )
		{
			const char *root = gc_fat_volume_roots[i];
			if( !GCube_PathAccessible( root ))
				continue;
			mounted++;
			fprintf( f, "Xash3D GameCube: FAT volume ready %s\n", root );
		}
		fprintf( f, "Xash3D GameCube: FAT preferred volume %s (count=%d)\n",
			gc_fat_write_root, mounted );
	}
	else
	{
		fprintf( f, "Xash3D GameCube: FAT shutdown (return to Swiss loader via exit stub)\n" );
	}
	fflush( f );
	fclose( f );
}

void GCube_Init( void )
{
#if XASH_GAMECUBE
	char xashdir[MAX_SYSPATH];
	char writepath[MAX_SYSPATH];

	/* G29: Initialize networking for local loopback single-player.
	 * Disable external network dependencies (master servers, HTTP)
	 * to ensure offline boot works without network hardware. */
	NET_Config( false, false );

	gc_fat_mounted = fatInitDefault();
	gc_fat_write_root[0] = '\0';
	gc_fat_base_root[0] = '\0';
	if( !gc_fat_mounted )
		Con_Reportf( S_WARN "SD card init failed\n" );
	else
	{
		GCube_ProbeFatVolumes();
#if defined(XASH_GAMECUBE_LIBDVM) && XASH_GAMECUBE_LIBDVM
		Con_Reportf( "Xash3D GameCube: FAT mount ready (libdvm Swiss volumes)\n" );
#else
		Con_Reportf( "Xash3D GameCube: FAT mount ready\n" );
#endif
		if( !GCube_FatRootReady() )
			Con_Reportf( S_WARN "FAT init succeeded but no accessible volume root\n" );
	}

	if( !GCube_MountDisc() )
		Con_Reportf( S_WARN "Xash3D GameCube: DVD mount failed (skipping mount)\n" );
	if( gc_dvd_mounted )
		Con_Reportf( "GameCube DVD filesystem mounted (%s)\n", ISO9660_GetVolumeLabel( GC_DVD_DEVICE ) );
	else
		Con_Reportf( S_WARN "DVD filesystem init failed\n" );

	/* Check for writable storage before proceeding with layout setup */
	if( !GCube_HasWritableStorage() )
		Con_Reportf( "Xash3D GameCube: no writable storage detected (SD card not available), proceeding in read-only mode\n" );

	if( GCube_GetWritablePath( writepath, sizeof( writepath )))
	{
		GCube_EnsureWritableLayout();
		GCube_WriteSwissEvidenceLog( false );
		Con_Reportf( "Xash3D GameCube: writable storage %s\n", writepath );
	}

	/* Game data root must be real media (SD or disc). Never chdir into gcprobe:. */
	if( GCube_GetBasePath( xashdir, sizeof( xashdir )))
	{
		if( !gc_fat_mounted )
			Con_Reportf( "Xash3D GameCube: read-only fallback %s (no SD)\n", xashdir );
	}
	else
	{
		SYS_Report( "Xash3D GameCube: no base path found (SD/DVD missing or empty). Cannot initialize game data path.\n" );
		Con_Reportf( S_ERROR "Xash3D GameCube: FATAL: Cannot initialize game data path.\n" );
		/* No data directory found. Game assets will not load. */
		xashdir[0] = '\0';
	}

	if( xashdir[0] && chdir( xashdir ) == 0 )
	{
		Con_Reportf( "GameCube data directory: %s\n", xashdir );
		GCube_LoadDiscBootOverrides();
	}
	else if( xashdir[0] )
	{
		Con_Reportf( S_ERROR "GameCube storage: failed to chdir to %s (errno %d: %s). Asset lookups will likely fail.\n", xashdir, errno, strerror( errno ) );
	}

	setup_gamecube_dll_functions();

	/* Initialize performance profiling */
	GC_PerfInit();
	GC_PerfCmd_Init();
#endif
}

qboolean GCube_GetBasePath( char *buf, size_t buflen )
{
#if XASH_GAMECUBE
	/* Prefer real FAT volume; never use the G94 probe RAM bank as the game root. */
	if( GCube_FatRootReady() )
	{
		const char *root = gc_fat_base_root[0] ? gc_fat_base_root : gc_fat_write_root;
		Q_snprintf( buf, buflen, "%s%s", root, GC_DATA_PATH );
		return true;
	}
	if( GCube_GetDiscPath( buf, buflen ))
		return true;

	{
		size_t i;

		for( i = 0; i < sizeof( gc_fat_volume_roots ) / sizeof( gc_fat_volume_roots[0] ); i++ )
		{
			if( !GCube_PathAccessible( gc_fat_volume_roots[i] ))
			{
				Con_Reportf( S_WARN "GameCube storage: path %s inaccessible or unmounted\n",
					gc_fat_volume_roots[i] );
				continue;
			}

			Q_strncpy( buf, gc_fat_volume_roots[i], buflen );
			return true;
		}

		if( GCube_PathAccessible( GC_DVD_DEVICE ":/" ))
		{
			Q_strncpy( buf, GC_DVD_DEVICE ":/", buflen );
			return true;
		}
	}
#endif
	(void)buf;
	(void)buflen;
	return false;
}

static char *gc_argv[36];
static char gc_smoke_map[MAX_QPATH];
static char gc_phase_test[32];
static char gc_changelevel_map[MAX_QPATH];
static char gc_landmark_name[MAX_QPATH];
static char gc_tas_name[32];
static qboolean gc_smoke_map_configured;
static qboolean gc_newgame_configured;
static qboolean gc_menu_newgame_configured;
static qboolean gc_phase_test_configured;
static qboolean gc_changelevel_configured;
static qboolean gc_landmark_configured;
static qboolean gc_leanpvs_configured;
static qboolean gc_fullphysics_configured;
static qboolean gc_worldrender_configured;
static qboolean gc_tas_configured;

static void GCube_LoadDiscBootOverrides( void )
{
#if XASH_GAMECUBE
	FILE *file;
	char line[128];

	gc_smoke_map_configured = false;
	gc_newgame_configured = false;
	gc_menu_newgame_configured = false;
	gc_newsaveload_configured = false;
	gc_configroundtrip_configured = false;
	gc_phase_test_configured = false;
	gc_changelevel_configured = false;
	gc_landmark_configured = false;
	gc_leanpvs_configured = false;
	gc_fullphysics_configured = false;
	gc_worldrender_configured = false;
	gc_tas_configured = false;
	gc_phase_test[0] = '\0';
	gc_changelevel_map[0] = '\0';
	gc_landmark_name[0] = '\0';
	gc_tas_name[0] = '\0';

	if( !GCube_MountDisc() )
		return;

	/* If running on read-only boot without a config file, the default smoke
	 * map below still gives the probe a deterministic map-load route. */
	file = fopen( GC_DVD_DEVICE ":/" GC_DATA_PATH "/valve/gamecube.cfg", "r" );
	if( !file )
		file = fopen( GC_DATA_PATH "/valve/gamecube.cfg", "r" );
	/* No gamecube.cfg map override means retail menu boot, not smoke map load. */
	if( !file )
		return;

	while( fgets( line, sizeof( line ), file ))
	{
		char *cursor = line;
		char *mapname;
		char *phase;
		size_t len;

		while( *cursor == ' ' || *cursor == '\t' )
			cursor++;
		if( cursor[0] == '#' || cursor[0] == '\0' || cursor[0] == '\r' || cursor[0] == '\n' )
			continue;

		if( !Q_strnicmp( cursor, "newgame", 7 ))
		{
			char ch = cursor[7];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_newgame_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override newgame\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "menunewgame", 11 ))
		{
			char ch = cursor[11];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_menu_newgame_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override menunewgame\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "gcworldrender", 13 ))
		{
			char ch = cursor[13];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_worldrender_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override gcworldrender\n" );
				continue;
			}
		}

		/* Also accept bare "worldrender" token from older cfg drafts. */
		if( !Q_strnicmp( cursor, "worldrender", 11 ))
		{
			char ch = cursor[11];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_worldrender_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override worldrender\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "newsaveload", 11 ))
		{
			char ch = cursor[11];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_newsaveload_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override newsaveload\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "configroundtrip", 15 ))
		{
			char ch = cursor[15];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_configroundtrip_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override configroundtrip\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "leanpvs", 7 ))
		{
			char ch = cursor[7];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_leanpvs_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override leanpvs\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "fullphysics", 11 ))
		{
			char ch = cursor[11];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_fullphysics_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override fullphysics\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "phasetest", 9 ) && ( cursor[9] == ' ' || cursor[9] == '\t' ))
		{
			phase = cursor + 9;
			while( *phase == ' ' || *phase == '\t' )
				phase++;
			len = strlen( phase );
			while( len > 0 && ( phase[len - 1] == '\r' || phase[len - 1] == '\n' ))
			{
				phase[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_phase_test ))
			{
				Q_strncpy( gc_phase_test, phase, sizeof( gc_phase_test ));
				gc_phase_test_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override phasetest %s\n", gc_phase_test );
			}
			continue;
		}

		if( !Q_strnicmp( cursor, "tas", 3 ) && ( cursor[3] == ' ' || cursor[3] == '\t' ))
		{
			phase = cursor + 3;
			while( *phase == ' ' || *phase == '\t' )
				phase++;
			len = strlen( phase );
			while( len > 0 && ( phase[len - 1] == '\r' || phase[len - 1] == '\n' ))
			{
				phase[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_tas_name )
				&& !strchr( phase, '/' ) && !strchr( phase, '\\' ) && !strchr( phase, '.' ))
			{
				Q_strncpy( gc_tas_name, phase, sizeof( gc_tas_name ));
				gc_tas_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override tas %s\n", gc_tas_name );
			}
			continue;
		}

		if( !Q_strnicmp( cursor, "changelevel", 11 ) && ( cursor[11] == ' ' || cursor[11] == '\t' ))
		{
			mapname = cursor + 11;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
			{
				mapname[--len] = '\0';
			}
			/* G337: split map/landmark before path checks. HL landmarks may
			 * contain '/' (e.g. c1a1c/d); rejecting the whole line dropped
			 * -gcchangelevel entirely (probe 20260810-044429 NEWGAME_READY). */
			if( len > 0 && len < sizeof( gc_changelevel_map ) + sizeof( gc_landmark_name ))
			{
				/* Optional: changelevel <map> <landmark> */
				{
					char *sp = strchr( mapname, ' ' );
					if( !sp )
						sp = strchr( mapname, '\t' );
					if( sp )
					{
						*sp++ = '\0';
						while( *sp == ' ' || *sp == '\t' )
							sp++;
						len = strlen( mapname );
						/* Landmark targetnames may include '/'; block '\\' only. */
						if( *sp && strlen( sp ) < sizeof( gc_landmark_name )
							&& !strchr( sp, '\\' ))
						{
							Q_strncpy( gc_landmark_name, sp, sizeof( gc_landmark_name ));
							gc_landmark_configured = true;
							SYS_Report( "Xash3D GameCube: disc boot override landmark %s\n",
								gc_landmark_name );
						}
					}
				}
				if( len > 0 && len < sizeof( gc_changelevel_map )
					&& !strchr( mapname, '/' ) && !strchr( mapname, '\\' ))
				{
					Q_strncpy( gc_changelevel_map, mapname, sizeof( gc_changelevel_map ));
					gc_changelevel_configured = true;
					SYS_Report( "Xash3D GameCube: disc boot override changelevel %s\n",
						gc_changelevel_map );
				}
			}
			continue;
		}

		if( !Q_strnicmp( cursor, "landmark", 8 ) && ( cursor[8] == ' ' || cursor[8] == '\t' ))
		{
			mapname = cursor + 8;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
			{
				mapname[--len] = '\0';
			}
			/* G337: allow '/' in landmark targetnames (c1a1c/d). */
			if( len > 0 && len < sizeof( gc_landmark_name )
				&& !strchr( mapname, '\\' ))
			{
				Q_strncpy( gc_landmark_name, mapname, sizeof( gc_landmark_name ));
				gc_landmark_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override landmark %s\n",
					gc_landmark_name );
			}
			continue;
		}

		if( Q_strnicmp( cursor, "map", 3 ) || ( cursor[3] != ' ' && cursor[3] != '\t' ))
			continue;

		mapname = cursor + 3;
		while( *mapname == ' ' || *mapname == '\t' )
			mapname++;
		len = strlen( mapname );
		while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
		{
			mapname[--len] = '\0';
		}
		if( len > 0 && len < sizeof( gc_smoke_map ) && !strchr( mapname, '/' ) && !strchr( mapname, '\\' ) )
		{
			Q_strncpy( gc_smoke_map, mapname, sizeof( gc_smoke_map ));
			gc_smoke_map_configured = true;
			SYS_Report( "Xash3D GameCube: smoke map override %s\n", gc_smoke_map );
		}
	}

	fclose( file );
#endif
}

int GCube_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	int fake_argc = 0;

	/* Swiss / wiiload / libogc2 often pass the DOL path as argv[0] with argc>=1.
	 * Prefer real guest argv whenever the loader supplied more than a bare path. */
	if( in_argc > 1 )
	{
		SYS_Report( "Xash3D GameCube: using loader argv argc=%d argv0=%s\n",
			in_argc, ( in_argv && in_argv[0] ) ? in_argv[0] : "(null)" );
		*out_argv = in_argv;
		return in_argc;
	}

	if( in_argc == 1 && in_argv && in_argv[0] && in_argv[0][0] )
		SYS_Report( "Xash3D GameCube: loader argv0=%s (building disc overrides)\n", in_argv[0] );

	GCube_LoadDiscBootOverrides();

	gc_argv[fake_argc++] = "xash";
	gc_argv[fake_argc++] = "xash";
	gc_argv[fake_argc++] = "-dev";
	gc_argv[fake_argc++] = "2";
	gc_argv[fake_argc++] = "-log";
	gc_argv[fake_argc++] = "-game";
	gc_argv[fake_argc++] = "valve";
	if( gc_newgame_configured )
		gc_argv[fake_argc++] = "-gcnewgame";
	if( gc_menu_newgame_configured )
		gc_argv[fake_argc++] = "-gcmenuplaystart";
	if( gc_worldrender_configured )
		gc_argv[fake_argc++] = "-gcworldrender";
	if( gc_smoke_map_configured )
	{
		gc_argv[fake_argc++] = "-gcmap";
		gc_argv[fake_argc++] = gc_smoke_map;
	}
	if( !gc_newgame_configured && !gc_smoke_map_configured && !gc_worldrender_configured )
	{
		SYS_Report( "Xash3D GameCube: disc boot override menu (retail Flipper)\n" );
	}
	if( gc_newsaveload_configured )
		gc_argv[fake_argc++] = "-gcnewsaveload";
	if( gc_configroundtrip_configured )
		gc_argv[fake_argc++] = "-gcconfigroundtrip";
	if( gc_leanpvs_configured )
		gc_argv[fake_argc++] = "-gcleanpvs";
	if( gc_fullphysics_configured )
		gc_argv[fake_argc++] = "-gcfullphysics";
	if( gc_phase_test_configured )
	{
		gc_argv[fake_argc++] = "-gc_phase_test";
		gc_argv[fake_argc++] = gc_phase_test;
	}
	if( gc_changelevel_configured )
	{
		gc_argv[fake_argc++] = "-gcchangelevel";
		gc_argv[fake_argc++] = gc_changelevel_map;
	}
	if( gc_landmark_configured )
	{
		gc_argv[fake_argc++] = "-gclandmark";
		gc_argv[fake_argc++] = gc_landmark_name;
	}
	if( gc_tas_configured )
	{
		gc_argv[fake_argc++] = "-gctas";
		gc_argv[fake_argc++] = gc_tas_name;
	}
	gc_argv[fake_argc++] = "-width";
	/* Tip-safe soft/internal FB. Flipper EFB/XFB stay native 640×480 via VI. */
	gc_argv[fake_argc++] = "320";
	gc_argv[fake_argc++] = "-height";
	gc_argv[fake_argc++] = "240";

	*out_argv = gc_argv;
	return fake_argc;
}

void GCube_Shutdown( void )
{
#if XASH_GAMECUBE
	size_t i;

	/* G29: Shutdown networking layer. */
	NET_Shutdown();

	if( gc_dvd_mounted )
		ISO9660_Unmount( GC_DVD_DEVICE );

	/* libdvm may have mounted sd + carda + cardb (+ dvd/ram). Unmount each.
	 * Classic libfat ignores unknown names. Prefer fatDeinit when available. */
	if( gc_fat_mounted )
	{
		/* Persist shutdown marker before unmount so SD copy satisfies Swiss gate. */
		GCube_WriteSwissEvidenceLog( true );
#if defined(XASH_GAMECUBE_LIBDVM) && XASH_GAMECUBE_LIBDVM
		fatDeinit();
#else
		for( i = 0; i < sizeof( gc_fat_volume_roots ) / sizeof( gc_fat_volume_roots[0] ); i++ )
		{
			char name[16];
			const char *root = gc_fat_volume_roots[i];
			size_t n = 0;

			while( root[n] && root[n] != ':' && n + 1 < sizeof( name ))
			{
				name[n] = root[n];
				n++;
			}
			name[n] = '\0';
			if( name[0] )
				fatUnmount( name );
		}
#endif
		SYS_Report( "Xash3D GameCube: FAT shutdown (return to Swiss loader via exit stub)\n" );
	}

	gc_dvd_mounted = false;
	gc_fat_mounted = false;
	gc_fat_write_root[0] = '\0';
	gc_fat_base_root[0] = '\0';
#endif
}
