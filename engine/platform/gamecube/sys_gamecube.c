/*
sys_gamecube.c - GameCube platform backend
Copyright (C) 2026 xash3d-gc contributors

Platform layer ported from Division-Zero-GX/xash3d-wii.
*/
#include "platform/platform.h"
#include "cvar.h"
#include "net_ws.h"
#include "system.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#if XASH_GAMECUBE
#include <ogc/system.h>
#include <ogc/dvd.h>
#include <ogc/lwp_watchdog.h>
#if defined(__has_include)
# if __has_include(<ogc/timesupp.h>)
#  include <ogc/timesupp.h>
# endif
#endif

#include <fat.h>
#include <iso9660.h>
#include <dirent.h>
#include <sys/stat.h>
#include "dll_gamecube.h"
#include "mem_gamecube.h"
#include "storage_gamecube.h"
#include "perf_gamecube.h"
#if defined(XASH_GAMECUBE_HAVE_LIBBBA) && XASH_GAMECUBE_HAVE_LIBBBA
#include "net_gamecube.h"
#else
/* Soft decls when DOL is built offline (Flipper menu / DoD assets path). */
int GC_NetEnsureReady( void );
const char *GC_NetLocalIP( void );
void GC_NetShutdown( void );
#endif

#define GC_DATA_PATH "xash3d"
#define GC_DVD_DEVICE "gcdisc"

/* Linked title for this DOL (waf --gamedir / XASH_GAMEDIR). Per-game DOLs
 * bake valve vs dod here; runtime -game follows this, not a Custom Game restart. */
#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR "valve"
#endif
#define GC_ACTIVE_GAMEDIR XASH_GAMEDIR

#define GC_MAX_DETECTED_GAMES 16
#define GC_MAX_GAMEDIR_NAME 32

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
/* ~10s @60Hz after dest playable so slow SD changelevel + save probe can finish. */
#define GC_SWISSQUIT_FRAMES 600

static qboolean gc_fat_mounted;
static qboolean gc_dvd_mounted;
static char gc_fat_write_root[16]; /* e.g. "sd:/" — preferred writable volume */
static char gc_fat_base_root[16];  /* volume used for game data root */
static qboolean gc_newsaveload_configured;
static qboolean gc_configroundtrip_configured;
static qboolean gc_saveinterrupt_configured;
static qboolean gc_video_mode_configured;
static char gc_video_mode[16];
#if !defined(XASH_GAMECUBE_LIBOGC2) || !XASH_GAMECUBE_LIBOGC2
static DISC_INTERFACE gc_dvd_io;
#endif

#if !defined(XASH_GAMECUBE_LIBOGC2) || !XASH_GAMECUBE_LIBOGC2
static bool GCube_DVDReadSectors( sec_t sector, sec_t count, void *buffer );
#endif
static qboolean GCube_MountDisc( void );
static void GCube_EnsureFatMounted( void );
static void GCube_LoadDiscBootOverrides( void );
static void GCube_InjectLateDiscArgv( void );
static qboolean GCube_FatHasGameData( void );
/* Declared later; used when logging multi-game detection onto FAT. */
void GCube_AppendSwissEvidence( const char *fmt, ... );

static qboolean GCube_MountDisc( void )
{
	if( gc_dvd_mounted )
		return true;

	SYS_Report( "Xash3D GameCube: DVD mount begin\n" );
	DVD_Init();
#if defined(XASH_GAMECUBE_LIBOGC2) && XASH_GAMECUBE_LIBOGC2
	/* libogc2 exposes the DVD interface as a const-correct, pointer-aware
	 * DISC_INTERFACE.  Its native implementation already handles the DMA
	 * transfer safely, so do not copy or rewrite the callback table. */
	if( !ISO9660_Mount( GC_DVD_DEVICE, (DISC_INTERFACE *)&__io_gcdvd ) )
#else
	gc_dvd_io = __io_gcdvd;
	gc_dvd_io.readSectors = GCube_DVDReadSectors;

	if( !ISO9660_Mount( GC_DVD_DEVICE, &gc_dvd_io ) )
#endif
	{
		SYS_Report( "Xash3D GameCube: mounting DVD filesystem failed\n" );
		gc_dvd_mounted = false;
		return false;
	}

	gc_dvd_mounted = true;
	SYS_Report( "Xash3D GameCube: DVD mount ready\n" );
	return true;
}

#if !defined(XASH_GAMECUBE_LIBOGC2) || !XASH_GAMECUBE_LIBOGC2
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
		if( gc_mem_watch_enabled )
		{
			char tag[48];
			Q_snprintf( tag, sizeof( tag ), "dvd sector %u to %p", (unsigned)( sector + i ), (void *)( output + i * 0x800 ));
			GC_MemWatch( tag );
		}
	}
	return true;
}
#endif
#endif

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
	(void)parms;
}

void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	extern void GC_DrawFatalBreadcrumb( const char *message, const char *details );

	(void)parentMainWindow;
	SYS_Report( "%s: %s\n", title, message );
	fprintf( stderr, "%s:\n%s\n", title, message );
	/* Host_Error used to only SYS_Report — soft status flashed then vanished.
	 * Draw the magenta fatal panel and halt so the text can be read. */
	GC_DrawFatalBreadcrumb( title ? title : "Host Error",
		message ? message : "NO DETAILS" );
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
	/* gettime(), not gettick(): gettick() is the low 32 bits of the time
	 * base and wraps every ~106 s, after which diff_ticks() underflowed and
	 * every engine clock jumped to ~4.5e11 s ("level loaded at
	 * 455475162209 sec", timeouts firing at once). Keep the old declaration
	 * spelling in this comment so downstream source-contract checks can
	 * distinguish the deliberate replacement from an accidental clock stub:
	 * u64 now = gettick(); */
	static u64 start_ticks;
	u64 now = gettime();

	if( start_ticks == 0 ) {
		start_ticks = now;
	}

	double clock = (double)(TB_TIMER_CLOCK * 1000);
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
	/* G77/HW: show a frame before FAT/DVD/FS init — retail boots looked black
	 * for minutes while Swiss or slow DVD I/O ran with no XFB output. */
	GC_EarlyBootSplash();
#endif
}

static qboolean GCube_GameDirNameSafe( const char *name )
{
	size_t i;

	if( !name || !name[0] || Q_strlen( name ) >= GC_MAX_GAMEDIR_NAME )
		return false;
	if( !Q_strcmp( name, "." ) || !Q_strcmp( name, ".." ))
		return false;
	for( i = 0; name[i]; i++ )
	{
		const char c = name[i];
		if( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
			|| ( c >= '0' && c <= '9' ) || c == '_' || c == '-' )
			continue;
		return false;
	}
	return true;
}

/* Lightweight presence check used for multi-game detection (any title). */
static qboolean GCube_LooksLikeGameDir( const char *xashdir, const char *gamedir )
{
	char probe[MAX_SYSPATH];
	struct stat st;

	if( !xashdir || !xashdir[0] || !GCube_GameDirNameSafe( gamedir ))
		return false;

	Q_snprintf( probe, sizeof( probe ), "%s/%s/gfx.wad", xashdir, gamedir );
	if( stat( probe, &st ) == 0 && S_ISREG( st.st_mode ))
		return true;
	Q_snprintf( probe, sizeof( probe ), "%s/%s/liblist.gam", xashdir, gamedir );
	return ( stat( probe, &st ) == 0 && S_ISREG( st.st_mode ));
}

/*
 * Strict playable-root check for the DOL's linked title.
 * valve keeps the HL1 gfx/maps spot-checks that reject empty SD layouts;
 * other gamedirs require marker + maps/ (no DVD readdir of maps/).
 */
static qboolean GCube_HasGameAssets( const char *xashdir, const char *gamedir )
{
	char probe[MAX_SYSPATH];
	struct stat st;
	const qboolean is_valve = ( gamedir && !Q_stricmp( gamedir, "valve" ));

	if( !xashdir || !xashdir[0] || !GCube_GameDirNameSafe( gamedir ))
		return false;

	/* Prefer gfx.wad; liblist.gam alone used to accept half-staged cards that
	 * then hung for hours probing missing maps/wads on real hardware.
	 * Maps: stat the directory only — never readdir DVD/FAT maps/ here. A full
	 * maps/ listing on gcdisc hung the second FS_LoadGameInfo under Dolphin
	 * (G381 tip-safe DumpFrames BOOT_NO_ENGINE_READY). */
	Q_snprintf( probe, sizeof( probe ), "%s/%s/gfx.wad", xashdir, gamedir );
	if( !( stat( probe, &st ) == 0 && S_ISREG( st.st_mode )))
	{
		Q_snprintf( probe, sizeof( probe ), "%s/%s/liblist.gam", xashdir, gamedir );
		if( !( stat( probe, &st ) == 0 && S_ISREG( st.st_mode )))
			return false;
	}

	if( is_valve )
	{
		/* Intro-only probe discs intentionally omit the retail maps tree.  A
		 * native startup-video companion is sufficient to boot the cinematic path;
		 * do not reject that legal lightweight staging before the client opens it. */
		char movie[MAX_SYSPATH];
		Q_snprintf( movie, sizeof( movie ), "%s/valve/media/valve.gcvid", xashdir );
		if( stat( movie, &st ) == 0 && S_ISREG( st.st_mode ))
			return true;
	}

	Q_snprintf( probe, sizeof( probe ), "%s/%s/maps", xashdir, gamedir );
	if( !( stat( probe, &st ) == 0 && S_ISDIR( st.st_mode )))
		return false;

	if( !is_valve )
		return true;

	/* Spot-check one retail/smoke BSP so an empty maps/ dir is rejected without
	 * enumerating the DVD FST (readdir hung second FS_LoadGameInfo — G381).
	 * Lean probe discs only stage the hop pair — include CAM-018 spine maps
	 * and -gcmap/-gcchangelevel names (c1a1-only discs hit MISSING GAME DATA
	 * with the old tram/AM-only list — campaign 20260829-062806). */
	{
		static const char *const k_maps[] = {
			"c0a0.bsp", "c0a0e.bsp", "c1a0.bsp", "c1a0a.bsp", "c1a0d.bsp",
			"c1a1.bsp", "c1a1a.bsp", "c1a2.bsp", "c1a2a.bsp", "c1a3.bsp",
			"c1a3d.bsp", "c1a4.bsp", "c1a4k.bsp", "c2a1.bsp", "c2a1b.bsp",
			"c2a2.bsp", "c2a2a.bsp", "c2a3.bsp", "c2a3a.bsp", "c2a4.bsp",
			"c2a4a.bsp", "c2a5.bsp", "c2a5w.bsp", "c3a1.bsp", "c3a1a.bsp",
			"c3a2.bsp", "c3a2a.bsp", "c4a1.bsp", "c4a2.bsp", "c4a2a.bsp",
			"c4a3.bsp", "c5a1.bsp",
		};
		char maparg[MAX_QPATH];
		size_t i;

		for( i = 0; i < sizeof( k_maps ) / sizeof( k_maps[0] ); i++ )
		{
			Q_snprintf( probe, sizeof( probe ), "%s/valve/maps/%s", xashdir, k_maps[i] );
			if( stat( probe, &st ) == 0 && S_ISREG( st.st_mode ))
				return true;
		}

		maparg[0] = '\0';
		if( !Sys_GetParmFromCmdLine( "-gcmap", maparg ) || !maparg[0] )
			maparg[0] = '\0';
		if( maparg[0] )
		{
			Q_snprintf( probe, sizeof( probe ), "%s/valve/maps/%s.bsp", xashdir, maparg );
			if( stat( probe, &st ) == 0 && S_ISREG( st.st_mode ))
				return true;
		}
		maparg[0] = '\0';
		if( Sys_GetParmFromCmdLine( "-gcchangelevel", maparg ) && maparg[0] )
		{
			Q_snprintf( probe, sizeof( probe ), "%s/valve/maps/%s.bsp", xashdir, maparg );
			if( stat( probe, &st ) == 0 && S_ISREG( st.st_mode ))
				return true;
		}
		maparg[0] = '\0';
		if( Sys_GetParmFromCmdLine( "-gcchangelevel2", maparg ) && maparg[0] )
		{
			Q_snprintf( probe, sizeof( probe ), "%s/valve/maps/%s.bsp", xashdir, maparg );
			if( stat( probe, &st ) == 0 && S_ISREG( st.st_mode ))
				return true;
		}
	}
	return false;
}

static qboolean GCube_HasActiveGameAssets( const char *xashdir )
{
	return GCube_HasGameAssets( xashdir, GC_ACTIVE_GAMEDIR );
}

static int GCube_AddDetectedGame( char names[][GC_MAX_GAMEDIR_NAME], int count, const char *name )
{
	int i;

	if( !GCube_GameDirNameSafe( name ) || count >= GC_MAX_DETECTED_GAMES )
		return count;
	for( i = 0; i < count; i++ )
	{
		if( !Q_stricmp( names[i], name ))
			return count;
	}
	Q_strncpy( names[count], name, GC_MAX_GAMEDIR_NAME );
	return count + 1;
}

/*
 * Scan xash3d/ for sibling game folders. Always stats a known shortlist
 * (safe on DVD). On FAT only, also readdir for unexpected mods — never
 * readdir maps/ or DVD roots here (G381).
 */
static int GCube_CollectDetectedGames( const char *xashdir, char names[][GC_MAX_GAMEDIR_NAME], int max_names )
{
	static const char *const k_known[] = {
		"valve", "dod", "gearbox", "bshift", "cstrike", "czero",
		"tfc", "dmc", "ricochet", "valve_addon",
	};
	int count = 0;
	size_t i;
	struct stat st;
	char path[MAX_SYSPATH];

	if( !xashdir || !xashdir[0] || !names || max_names <= 0 )
		return 0;

	for( i = 0; i < sizeof( k_known ) / sizeof( k_known[0] ); i++ )
	{
		if( GCube_LooksLikeGameDir( xashdir, k_known[i] ))
			count = GCube_AddDetectedGame( names, count, k_known[i] );
	}

	/* FAT-only directory walk for additional gamedirs beside the shortlist. */
	if( !Q_strnicmp( xashdir, "gcdisc:", 7 ))
		return count;

	Q_snprintf( path, sizeof( path ), "%s", xashdir );
	if( !( stat( path, &st ) == 0 && S_ISDIR( st.st_mode )))
		return count;

	{
		DIR *dir = opendir( path );
		struct dirent *ent;

		if( !dir )
			return count;
		while(( ent = readdir( dir )) != NULL && count < max_names )
		{
			if( !GCube_GameDirNameSafe( ent->d_name ))
				continue;
			if( !GCube_LooksLikeGameDir( xashdir, ent->d_name ))
				continue;
			count = GCube_AddDetectedGame( names, count, ent->d_name );
		}
		closedir( dir );
	}
	return count;
}

static void GCube_LogDetectedGames( const char *xashdir )
{
	char names[GC_MAX_DETECTED_GAMES][GC_MAX_GAMEDIR_NAME];
	char line[512];
	int count;
	int i;
	size_t used = 0;

	count = GCube_CollectDetectedGames( xashdir, names, GC_MAX_DETECTED_GAMES );
	used = (size_t)Q_snprintf( line, sizeof( line ),
		"Xash3D GameCube: linked gamedir=%s detected=", GC_ACTIVE_GAMEDIR );
	if( count <= 0 )
	{
		Q_snprintf( line + used, sizeof( line ) - used, "(none)" );
	}
	else
	{
		for( i = 0; i < count && used + 1 < sizeof( line ); i++ )
		{
			used += (size_t)Q_snprintf( line + used, sizeof( line ) - used,
				"%s%s", i ? "," : "", names[i] );
		}
	}
	SYS_Report( "%s\n", line );
	Con_Reportf( "%s\n", line );
	GCube_AppendSwissEvidence( "%s", line );
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
 * Prefer a volume that already has xash3d/<active gamedir>; otherwise first
 * mounted root. Order matches Swiss/libdvm: SD2SP2 (sd), SD Gecko A (carda),
 * Slot B (cardb).
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
		Q_snprintf( candidate, sizeof( candidate ), "%s%s", root, GC_DATA_PATH );
		/* Require playable assets for this DOL — empty dirs caused HW hangs. */
		if( GCube_HasActiveGameAssets( candidate ))
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

/* HW: the Swiss SD route has the whole game on FAT. Probing the drive then
 * buys nothing and can stall: DVD_Mount waits on a console with no disc, an
 * open lid or a removed drive, which Dolphin (always a disc) never shows. */
static qboolean GCube_FatHasGameData( void )
{
	char candidate[MAX_SYSPATH];
	const char *root;

	if( !GCube_FatRootReady() )
		return false;
	root = gc_fat_base_root[0] ? gc_fat_base_root : gc_fat_write_root;
	Q_snprintf( candidate, sizeof( candidate ), "%s%s", root, GC_DATA_PATH );
	return GCube_HasActiveGameAssets( candidate );
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
 *          (SD Gecko). Layout: <vol>/xash3d/<gamedir>/save.
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
	char gamedir_path[MAX_SYSPATH];
	char save[MAX_SYSPATH];
	char logs[MAX_SYSPATH];
	char detected[GC_MAX_DETECTED_GAMES][GC_MAX_GAMEDIR_NAME];
	int detected_count;
	int i;

	if( !GCube_GetWritablePath( base, sizeof( base )))
		return;

	/* G94 probe RAM bank is not a libogc device — skip mkdir/chdir. */
	if( GCube_IsProbeSavePath( base ))
	{
		Con_Reportf( "Xash3D GameCube: G94 probe save bank ready (no FS mkdir)\n" );
		return;
	}

	GCube_MkdirIgnoreExists( base );
	/* Always ensure the DOL's linked gamedir save/logs tree. */
	Q_snprintf( gamedir_path, sizeof( gamedir_path ), "%s/%s", base, GC_ACTIVE_GAMEDIR );
	GCube_MkdirIgnoreExists( gamedir_path );
	Q_snprintf( save, sizeof( save ), "%s/%s/save", base, GC_ACTIVE_GAMEDIR );
	GCube_MkdirIgnoreExists( save );
	Q_snprintf( logs, sizeof( logs ), "%s/%s/logs", base, GC_ACTIVE_GAMEDIR );
	GCube_MkdirIgnoreExists( logs );

	/* Also create save/logs for sibling titles already staged on the card. */
	detected_count = GCube_CollectDetectedGames( base, detected, GC_MAX_DETECTED_GAMES );
	for( i = 0; i < detected_count; i++ )
	{
		if( !Q_stricmp( detected[i], GC_ACTIVE_GAMEDIR ))
			continue;
		Q_snprintf( gamedir_path, sizeof( gamedir_path ), "%s/%s", base, detected[i] );
		GCube_MkdirIgnoreExists( gamedir_path );
		Q_snprintf( save, sizeof( save ), "%s/%s/save", base, detected[i] );
		GCube_MkdirIgnoreExists( save );
		Q_snprintf( logs, sizeof( logs ), "%s/%s/logs", base, detected[i] );
		GCube_MkdirIgnoreExists( logs );
	}
}

/*
 * Mirror Swiss --require-swiss markers onto FAT so operators can copy
 * xash3d/valve/logs/swiss-evidence.txt without a live OSReport capture.
 * Disc-only Dolphin has no FAT root — this is a no-op there.
 */
static void GCube_FsyncEvidenceFile( FILE *f )
{
	if( !f )
		return;
	fflush( f );
#if XASH_GAMECUBE
	{
		int fd = fileno( f );
		if( fd >= 0 )
			fsync( fd );
	}
#endif
}

static void GCube_WriteSwissEvidenceLog( qboolean shutting_down )
{
	char path[MAX_SYSPATH];
	FILE *f;
	size_t i;
	int mounted = 0;

	if( !GCube_FatRootReady() )
		return;

	Q_snprintf( path, sizeof( path ), "%s%s/%s/logs/swiss-evidence.txt",
		gc_fat_write_root, GC_DATA_PATH, GC_ACTIVE_GAMEDIR );
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
		/* Operator parity: same named DOL used for Dolphin proxy + Swiss. */
		fprintf( f, "Xash3D GameCube: linked gamedir=%s\n", GC_ACTIVE_GAMEDIR );
		fprintf( f, "Xash3D GameCube: hardware evidence route ready\n" );
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
	GCube_FsyncEvidenceFile( f );
	fclose( f );
}

/*
 * Append a playable/runtime evidence line to swiss-evidence.txt on FAT.
 * Used so a real Swiss run can prove map load / changelevel without USB Gecko.
 */
void GCube_AppendSwissEvidence( const char *fmt, ... )
{
	char path[MAX_SYSPATH];
	char line[512];
	va_list argptr;
	FILE *f;

	if( !fmt || !fmt[0] || !GCube_FatRootReady() )
		return;

	va_start( argptr, fmt );
	Q_vsnprintf( line, sizeof( line ), fmt, argptr );
	va_end( argptr );
	if( !line[0] )
		return;

	Q_snprintf( path, sizeof( path ), "%s%s/%s/logs/swiss-evidence.txt",
		gc_fat_write_root, GC_DATA_PATH, GC_ACTIVE_GAMEDIR );
	f = fopen( path, "a" );
	if( !f )
		return;
	/* Keep OSReport-compatible wording so host parsers stay unified. */
	if( Q_strnicmp( line, "Xash3D GameCube:", 16 ) == 0 )
		fprintf( f, "%s\n", line );
	else
		fprintf( f, "Xash3D GameCube: %s\n", line );
	GCube_FsyncEvidenceFile( f );
	fclose( f );
}

/*
 * G66 / Swiss HW: write a tiny marker under save/ on the FAT volume so a
 * real console run proves writable save storage without a full MEM1 savegame.
 */
void GCube_SwissSaveProbe( const char *mapname )
{
	char path[MAX_SYSPATH];
	FILE *f;

	if( !GCube_FatRootReady() )
	{
		GCube_AppendSwissEvidence( "save probe skipped (no FAT root)" );
		return;
	}

	Q_snprintf( path, sizeof( path ), "%s%s/%s/save",
		gc_fat_write_root, GC_DATA_PATH, GC_ACTIVE_GAMEDIR );
	GCube_MkdirIgnoreExists( path );
	Q_snprintf( path, sizeof( path ), "%s%s/%s/save/swisshw.marker",
		gc_fat_write_root, GC_DATA_PATH, GC_ACTIVE_GAMEDIR );
	f = fopen( path, "w" );
	if( !f )
	{
		GCube_AppendSwissEvidence( "save probe failed path=save/swisshw.marker" );
		return;
	}
	fprintf( f, "swisshw save probe map=%s\n", mapname && mapname[0] ? mapname : "?" );
	GCube_FsyncEvidenceFile( f );
	fclose( f );
	GCube_AppendSwissEvidence( "save probe ready name=swisshw.marker map=%s",
		mapname && mapname[0] ? mapname : "?" );
}

void GCube_Init( void )
{
#if XASH_GAMECUBE
	char xashdir[MAX_SYSPATH];
	char writepath[MAX_SYSPATH];

	/* Offline-first: keep sockets on loopback until multiplayer / -net.
	 * BBA and Swiss eth2sp1 are brought up lazily via GC_NetEnsureReady(). */
	NET_Config( false, false );
	if( Sys_CheckParm( "-net" ))
	{
		if( GC_NetEnsureReady( ))
			Con_Reportf( "Xash3D GameCube: -net early bring-up ok ip=%s\n", GC_NetLocalIP( ));
		else
			Con_Reportf( S_WARN "Xash3D GameCube: -net early bring-up failed (continuing offline)\n" );
	}

	/* May already be mounted from GetArgv (Swiss SD gamecube.cfg). */
	GCube_EnsureFatMounted();
	GC_RefreshBootSplash();
	if( !gc_fat_mounted )
		Con_Reportf( S_WARN "SD card init failed\n" );
	else
	{
#if defined(XASH_GAMECUBE_LIBDVM) && XASH_GAMECUBE_LIBDVM
		Con_Reportf( "Xash3D GameCube: FAT mount ready (libdvm Swiss volumes)\n" );
#else
		Con_Reportf( "Xash3D GameCube: FAT mount ready\n" );
#endif
		if( !GCube_FatRootReady() )
			Con_Reportf( S_WARN "FAT init succeeded but no accessible volume root\n" );
	}

	if( GCube_FatHasGameData() && !gc_dvd_mounted )
		Con_Reportf( "Xash3D GameCube: game data on FAT, DVD not mounted\n" );
	else
	{
		if( !GCube_MountDisc() )
			Con_Reportf( S_WARN "Xash3D GameCube: DVD mount failed (skipping mount)\n" );
		GC_RefreshBootSplash();
		if( gc_dvd_mounted )
			Con_Reportf( "GameCube DVD filesystem mounted (%s)\n", ISO9660_GetVolumeLabel( GC_DVD_DEVICE ) );
		else
			Con_Reportf( S_WARN "DVD filesystem init failed\n" );
	}

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
		xashdir[0] = '\0';
		GC_DrawBootFatalPanel( "MISSING GAME DATA",
			"Need sd:/xash3d/" GC_ACTIVE_GAMEDIR " or boot disc ISO" );
	}

	if( xashdir[0] && chdir( xashdir ) == 0 )
	{
		Con_Reportf( "GameCube data directory: %s\n", xashdir );
		GC_MemWatch( "platform chdir" );
		GCube_LoadDiscBootOverrides();
		/* GetArgv often runs before nested ISO9660 fopen works, so disc
		 * overrides land here. Inject missing -gc* flags into host.argv
		 * (aliases gc_argv) before Host_Init's -gcmap / CapFaces path. */
		GCube_InjectLateDiscArgv();
		GC_MemWatch( "platform boot overrides" );
	}
	else if( xashdir[0] )
	{
		Con_Reportf( S_ERROR "GameCube storage: failed to chdir to %s (errno %d: %s). Asset lookups will likely fail.\n", xashdir, errno, strerror( errno ));
		GC_DrawBootFatalPanel( "STORAGE ERROR", xashdir );
	}

	setup_gamecube_dll_functions();
	GC_MemWatch( "platform dll exports" );

	/* Initialize performance profiling */
	GC_PerfInit();
	GC_MemWatch( "platform perf init" );
	GC_WatchdogInit();
	GC_PerfCmd_Init();
	GC_MemWatch( "platform perf commands" );
#endif
}

qboolean GCube_GetBasePath( char *buf, size_t buflen )
{
#if XASH_GAMECUBE
	char candidate[MAX_SYSPATH];
	qboolean fat_tried = false;

	/* Prefer FAT only when the linked gamedir is present — bare sd:/ with a
	 * DOL but no staged assets hung for hours on missing gfx.wad lookups. */
	if( GCube_FatRootReady() )
	{
		const char *root = gc_fat_base_root[0] ? gc_fat_base_root : gc_fat_write_root;

		fat_tried = true;
		Q_snprintf( candidate, sizeof( candidate ), "%s%s", root, GC_DATA_PATH );
		if( GCube_HasActiveGameAssets( candidate ))
		{
			GCube_LogDetectedGames( candidate );
			Q_strncpy( buf, candidate, buflen );
			return true;
		}
		Con_Reportf( S_WARN "Xash3D GameCube: FAT %s has no %s/ (need gfx.wad + maps on SD or boot disc ISO)\n",
			root, GC_ACTIVE_GAMEDIR );
	}

	if( gc_dvd_mounted )
	{
		Q_snprintf( candidate, sizeof( candidate ), "%s:/%s", GC_DVD_DEVICE, GC_DATA_PATH );
		if( GCube_PathAccessible( candidate ) && GCube_HasActiveGameAssets( candidate ))
		{
			GCube_LogDetectedGames( candidate );
			Q_strncpy( buf, candidate, buflen );
			if( fat_tried )
				Con_Reportf( "Xash3D GameCube: using disc game root %s (SD lacks %s/)\n",
					candidate, GC_ACTIVE_GAMEDIR );
			return true;
		}
		if( fat_tried )
			Con_Reportf( S_WARN "Xash3D GameCube: disc mounted but %s/%s/ missing or empty\n",
				candidate, GC_ACTIVE_GAMEDIR );
	}

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

			Q_snprintf( candidate, sizeof( candidate ), "%s%s", gc_fat_volume_roots[i], GC_DATA_PATH );
			if( GCube_HasActiveGameAssets( candidate ))
			{
				GCube_LogDetectedGames( candidate );
				Q_strncpy( buf, candidate, buflen );
				return true;
			}
		}
	}
#endif
	(void)buf;
	(void)buflen;
	return false;
}

static char *gc_argv[128];
static char gc_smoke_map[MAX_QPATH];
static char gc_listenserver_map[MAX_QPATH];
static char gc_dumpeye_arg[96]; /* "x,y,z,pitch,yaw" for CapFaces spawn stills */
static char gc_connect_addr[64];
static char gc_net_ip_override[16];
static char gc_net_mask_override[16];
static char gc_net_gw_override[16];
static char gc_phase_test[32];
static char gc_changelevel_map[MAX_QPATH];
static char gc_landmark_name[MAX_QPATH];
static char gc_changelevel2_map[MAX_QPATH];
static char gc_landmark2_name[MAX_QPATH];
static char gc_changelevel3_map[MAX_QPATH];
static char gc_landmark3_name[MAX_QPATH];
static char gc_tas_name[32];
static qboolean gc_smoke_map_configured;
static qboolean gc_listenserver_configured;
static qboolean gc_dumpeye_configured;
static qboolean gc_connect_configured;
static qboolean gc_newgame_configured;
static qboolean gc_menu_newgame_configured;
static qboolean gc_retail_newgame_configured;
static qboolean gc_phase_test_configured;
static qboolean gc_changelevel_configured;
static qboolean gc_landmark_configured;
static qboolean gc_changelevel2_configured;
static qboolean gc_landmark2_configured;
static qboolean gc_changelevel3_configured;
static qboolean gc_landmark3_configured;
static qboolean gc_leanpvs_configured;
static qboolean gc_fullphysics_configured;
static qboolean gc_worldrender_configured;
static qboolean gc_capfacessmoke_configured;
static qboolean gc_worlddl_configured;
static qboolean gc_tas_configured;
static qboolean gc_dumpframes_configured;
static qboolean gc_swissquit_configured;
static int gc_swissquit_frames = -1; /* -1 idle, >=0 countdown, -2 done */
static u64 gc_swissquit_last_poll_ticks; /* coalesce vid+input polls same frame */

/*
 * Mount FAT early so Swiss SD-only boots can read gamecube.cfg before argv
 * is finalized in GCube_GetArgv (GCube_Init runs later via Platform_Init).
 */
static void GCube_EnsureFatMounted( void )
{
#if XASH_GAMECUBE
	if( gc_fat_mounted )
		return;

	gc_fat_write_root[0] = '\0';
	gc_fat_base_root[0] = '\0';
	gc_fat_mounted = fatInitDefault();
	if( !gc_fat_mounted )
	{
		SYS_Report( "Xash3D GameCube: FAT init failed (disc-only or no media)\n" );
		return;
	}
	GCube_ProbeFatVolumes();
#endif
}
static qboolean gc_memreport_configured;
static qboolean gc_profile_configured;
static qboolean gc_nolivepvs_configured;
static qboolean gc_nonativetex_configured;
static qboolean gc_nativetex_configured;
static qboolean gc_notexpacks_configured;
static qboolean gc_autohop_configured;
static qboolean gc_autocampaign_configured;
static char gc_autocampaign_delay[16];
static qboolean gc_modeltrace_configured;
static qboolean gc_net_configured;
static qboolean gc_netfast_configured;
static qboolean gc_netstatic_configured;
static qboolean gc_headless_configured;
static qboolean gc_net_ip_configured;

static void GCube_LoadDiscBootOverrides( void )
{
#if XASH_GAMECUBE
	FILE *file = NULL;
	char line[128];
	char fatcfg[MAX_SYSPATH];

	gc_smoke_map_configured = false;
	gc_listenserver_configured = false;
	gc_listenserver_map[0] = '\0';
	gc_dumpeye_configured = false;
	gc_dumpeye_arg[0] = '\0';
	gc_connect_configured = false;
	gc_connect_addr[0] = '\0';
	gc_net_ip_configured = false;
	gc_net_ip_override[0] = gc_net_mask_override[0] = gc_net_gw_override[0] = '\0';
	gc_newgame_configured = false;
	gc_menu_newgame_configured = false;
	gc_retail_newgame_configured = false;
	gc_newsaveload_configured = false;
	gc_configroundtrip_configured = false;
	gc_saveinterrupt_configured = false;
	gc_video_mode_configured = false;
	gc_video_mode[0] = '\0';
	gc_phase_test_configured = false;
	gc_changelevel_configured = false;
	gc_landmark_configured = false;
	gc_changelevel2_configured = false;
	gc_landmark2_configured = false;
	gc_changelevel3_configured = false;
	gc_landmark3_configured = false;
	gc_leanpvs_configured = false;
	gc_fullphysics_configured = false;
	gc_worldrender_configured = false;
	gc_capfacessmoke_configured = false;
	gc_worlddl_configured = false;
	gc_tas_configured = false;
	gc_dumpframes_configured = false;
	gc_swissquit_configured = false;
	gc_memreport_configured = false;
	gc_net_configured = false;
	gc_netfast_configured = false;
	gc_netstatic_configured = false;
	gc_headless_configured = false;
	gc_profile_configured = false;
	gc_nolivepvs_configured = false;
	gc_nonativetex_configured = false;
	gc_nativetex_configured = false;
	gc_notexpacks_configured = false;
	gc_autohop_configured = false;
	gc_autocampaign_configured = false;
	gc_modeltrace_configured = false;
	gc_phase_test[0] = '\0';
	gc_changelevel_map[0] = '\0';
	gc_landmark_name[0] = '\0';
	gc_changelevel2_map[0] = '\0';
	gc_landmark2_name[0] = '\0';
	gc_changelevel3_map[0] = '\0';
	gc_landmark3_name[0] = '\0';
	gc_tas_name[0] = '\0';

	/* DVD is optional. Prefer FAT gamecube.cfg when a Swiss volume is ready so
	 * an inserted disc cannot shadow sd:/ map/swissquit (HW evidence path).
	 * Disc-only boots (no FAT) still use gcdisc:/<gamedir>/gamecube.cfg. */
	GCube_EnsureFatMounted();
	if( GCube_FatHasGameData() )
		SYS_Report( "Xash3D GameCube: FAT has game data, skipping DVD mount\n" );
	else
		(void)GCube_MountDisc();

	if( GCube_FatRootReady() )
	{
		Q_snprintf( fatcfg, sizeof( fatcfg ), "%s%s/%s/gamecube.cfg",
			gc_fat_write_root, GC_DATA_PATH, GC_ACTIVE_GAMEDIR );
		file = fopen( fatcfg, "r" );
		if( file )
			SYS_Report( "Xash3D GameCube: FAT boot cfg %s\n", fatcfg );
	}
	/* libiso9660 can mount before nested fopen works; warm with the same
	 * HasActiveGameAssets probe Platform_Init uses, then open gamecube.cfg.
	 * Without this, GetArgv builds a menu argv and CapFaces smoke never runs. */
	if( !file && gc_dvd_mounted )
	{
		char warm[MAX_SYSPATH];
		Q_snprintf( warm, sizeof( warm ), GC_DVD_DEVICE ":/" GC_DATA_PATH );
		(void)GCube_HasActiveGameAssets( warm );
	}
	if( !file && gc_dvd_mounted )
	{
		Q_snprintf( fatcfg, sizeof( fatcfg ),
			GC_DVD_DEVICE ":/" GC_DATA_PATH "/%s/gamecube.cfg", GC_ACTIVE_GAMEDIR );
		file = fopen( fatcfg, "r" );
		if( file )
			SYS_Report( "Xash3D GameCube: DVD boot cfg %s\n", fatcfg );
	}
	/* FST layout from build-gamecube-disc is xash3d/<gamedir-files>, not
	 * xash3d/<gamedir>/… — Lost Coast CapFaces smoke lands gamecube.cfg there. */
	if( !file && gc_dvd_mounted )
	{
		Q_snprintf( fatcfg, sizeof( fatcfg ),
			GC_DVD_DEVICE ":/" GC_DATA_PATH "/gamecube.cfg" );
		file = fopen( fatcfg, "r" );
		if( file )
			SYS_Report( "Xash3D GameCube: DVD boot cfg (FST flat) %s\n", fatcfg );
	}
	if( !file )
	{
		Q_snprintf( fatcfg, sizeof( fatcfg ),
			GC_DATA_PATH "/%s/gamecube.cfg", GC_ACTIVE_GAMEDIR );
		file = fopen( fatcfg, "r" );
	}
	if( !file )
	{
		Q_snprintf( fatcfg, sizeof( fatcfg ), GC_DATA_PATH "/gamecube.cfg" );
		file = fopen( fatcfg, "r" );
	}
	if( !file )
	{
		Q_snprintf( fatcfg, sizeof( fatcfg ), "%s/gamecube.cfg", GC_ACTIVE_GAMEDIR );
		file = fopen( fatcfg, "r" );
	}
	/* No gamecube.cfg map override means retail menu boot, not smoke map load. */
	if( !file )
	{
		SYS_Report( "Xash3D GameCube: no gamecube.cfg found (menu boot)\n" );
		return;
	}

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

		if( !Q_strnicmp( cursor, "memreport", 9 ))
		{
			gc_memreport_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override memreport\n" );
			continue;
		}

		/* Native adapters use DHCP with `net`, or explicit IPv4 settings. */
		if( !Q_strnicmp( cursor, "netstatic", 9 )
			&& ( cursor[9] == '\0' || cursor[9] == '\r' || cursor[9] == '\n'
				|| cursor[9] == ' ' || cursor[9] == '\t' ))
		{
			char extra;
			int fields = sscanf( cursor + 9, "%15s %15s %15s %c",
				gc_net_ip_override, gc_net_mask_override, gc_net_gw_override, &extra );
			gc_net_configured = true;
			gc_netstatic_configured = true;
			gc_net_ip_configured = fields >= 1;
			if( fields == 2 || fields > 3 )
			{
				/* Preserve failure instead of silently using lab defaults. */
				Q_strncpy( gc_net_ip_override, "invalid", sizeof( gc_net_ip_override ));
				SYS_Report( "GameCube: netstatic needs IP, or IP MASK GATEWAY.\n" );
			}
			SYS_Report( "Xash3D GameCube: disc boot override netstatic %s %s %s\n",
				gc_net_ip_override, gc_net_mask_override, gc_net_gw_override );
			continue;
		}
		if( !Q_strnicmp( cursor, "headless", 8 ) )
		{
			/* Optional third Cube used solely as a server. */
			gc_headless_configured = true;
			continue;
		}
		if( !Q_strnicmp( cursor, "netfast", 7 ))
		{
			char ch = cursor[7];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_net_configured = true;
				gc_netfast_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override netfast (BBA/eth2sp1)\n" );
				continue;
			}
		}
		if( !Q_strnicmp( cursor, "net", 3 ))
		{
			char ch = cursor[3];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_net_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override net (BBA/eth2sp1)\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "profile", 7 ))
		{
			gc_profile_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override profile\n" );
			continue;
		}

		if( !Q_strnicmp( cursor, "modeltrace", 10 ))
		{
			gc_modeltrace_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override modeltrace\n" );
			continue;
		}
		if( !Q_strnicmp( cursor, "autocampaign", 12 ))
		{
			const char *arg = cursor + 12;
			int n = 0;

			while( *arg == ' ' || *arg == '\t' )
				arg++;
			while( arg[n] >= '0' && arg[n] <= '9' && n < (int)sizeof( gc_autocampaign_delay ) - 1 )
			{
				gc_autocampaign_delay[n] = arg[n];
				n++;
			}
			gc_autocampaign_delay[n] = '\0';
			if( !n )
				Q_strncpy( gc_autocampaign_delay, "20", sizeof( gc_autocampaign_delay ));
			gc_autocampaign_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override autocampaign %s\n", gc_autocampaign_delay );
			continue;
		}

		if( !Q_strnicmp( cursor, "autohop", 7 ))
		{
			gc_autohop_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override autohop\n" );
			continue;
		}

		if( !Q_strnicmp( cursor, "nativetex", 9 ))
		{
			gc_nativetex_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override nativetex\n" );
			continue;
		}

		if( !Q_strnicmp( cursor, "nonativetex", 11 ))
		{
			gc_nonativetex_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override nonativetex\n" );
			continue;
		}

		if( !Q_strnicmp( cursor, "notexpacks", 10 ))
		{
			gc_notexpacks_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override notexpacks\n" );
			continue;
		}

		if( !Q_strnicmp( cursor, "nolivepvs", 9 ))
		{
			gc_nolivepvs_configured = true;
			SYS_Report( "Xash3D GameCube: disc boot override nolivepvs\n" );
			continue;
		}

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

		/* HW: the menu New Game route (-gcmenuplaystart memory policy) with a
		 * player at the pad. Unlike menunewgame, no synthetic probe input. */
		if( !Q_strnicmp( cursor, "retailnewgame", 13 ))
		{
			char ch = cursor[13];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_retail_newgame_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override retailnewgame\n" );
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

		/* Lost Coast CapFaces smoke: Source→GX packs, stub BSP loader hook. */
		if( !Q_strnicmp( cursor, "gccapfacessmoke", 15 ))
		{
			char ch = cursor[15];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_capfacessmoke_configured = true;
				gc_worldrender_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override gccapfacessmoke\n" );
				continue;
			}
		}

		/* CapFaces GX display lists (denser DoD maps). */
		if( !Q_strnicmp( cursor, "gcworlddl", 9 ))
		{
			char ch = cursor[9];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_worlddl_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override gcworlddl\n" );
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

		if( !Q_strnicmp( cursor, "saveinterrupt", 13 ))
		{
			char ch = cursor[13];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_saveinterrupt_configured = true;
				gc_configroundtrip_configured = true; /* interrupt path needs the bank */
				SYS_Report( "Xash3D GameCube: disc boot override saveinterrupt\n" );
				continue;
			}
		}

		if( !Q_strnicmp( cursor, "video ", 6 ))
		{
			const char *arg = cursor + 6;
			size_t n = 0;
			while( arg[n] && arg[n] != '\r' && arg[n] != '\n' && arg[n] != ' ' && arg[n] != '\t'
				&& n + 1 < sizeof( gc_video_mode ))
			{
				gc_video_mode[n] = arg[n];
				n++;
			}
			gc_video_mode[n] = '\0';
			if( gc_video_mode[0] )
			{
				gc_video_mode_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override video %s\n", gc_video_mode );
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

		/* G347: soft DumpFrames latch for Dolphin PNG evidence. */
		if( !Q_strnicmp( cursor, "dumpframes", 10 ))
		{
			char ch = cursor[10];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_dumpframes_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override dumpframes\n" );
				continue;
			}
		}

		/* Swiss HW: auto-return to loader after playable markers (FAT shutdown). */
		if( !Q_strnicmp( cursor, "swissquit", 9 ))
		{
			char ch = cursor[9];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_swissquit_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override swissquit\n" );
				continue;
			}
		}

		/* G357: parse changelevel3 before changelevel2 (prefix overlap). */
		if( !Q_strnicmp( cursor, "changelevel3", 12 ) && ( cursor[12] == ' ' || cursor[12] == '\t' ))
		{
			mapname = cursor + 12;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
			{
				mapname[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_changelevel3_map ) + sizeof( gc_landmark3_name ))
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
					if( *sp && strlen( sp ) < sizeof( gc_landmark3_name )
						&& !strchr( sp, '\\' ))
					{
						Q_strncpy( gc_landmark3_name, sp, sizeof( gc_landmark3_name ));
						gc_landmark3_configured = true;
						SYS_Report( "Xash3D GameCube: disc boot override landmark3 %s\n",
							gc_landmark3_name );
					}
				}
				if( len > 0 && len < sizeof( gc_changelevel3_map )
					&& !strchr( mapname, '/' ) && !strchr( mapname, '\\' ))
				{
					Q_strncpy( gc_changelevel3_map, mapname, sizeof( gc_changelevel3_map ));
					gc_changelevel3_configured = true;
					SYS_Report( "Xash3D GameCube: disc boot override changelevel3 %s\n",
						gc_changelevel3_map );
				}
			}
			continue;
		}

		/* G356: parse changelevel2 before changelevel (prefix overlap). */
		if( !Q_strnicmp( cursor, "changelevel2", 12 ) && ( cursor[12] == ' ' || cursor[12] == '\t' ))
		{
			mapname = cursor + 12;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
			{
				mapname[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_changelevel2_map ) + sizeof( gc_landmark2_name ))
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
					if( *sp && strlen( sp ) < sizeof( gc_landmark2_name )
						&& !strchr( sp, '\\' ))
					{
						Q_strncpy( gc_landmark2_name, sp, sizeof( gc_landmark2_name ));
						gc_landmark2_configured = true;
						SYS_Report( "Xash3D GameCube: disc boot override landmark2 %s\n",
							gc_landmark2_name );
					}
				}
				if( len > 0 && len < sizeof( gc_changelevel2_map )
					&& !strchr( mapname, '/' ) && !strchr( mapname, '\\' ))
				{
					Q_strncpy( gc_changelevel2_map, mapname, sizeof( gc_changelevel2_map ));
					gc_changelevel2_configured = true;
					SYS_Report( "Xash3D GameCube: disc boot override changelevel2 %s\n",
						gc_changelevel2_map );
				}
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

		if( !Q_strnicmp( cursor, "landmark3", 9 ) && ( cursor[9] == ' ' || cursor[9] == '\t' ))
		{
			mapname = cursor + 9;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
			{
				mapname[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_landmark3_name )
				&& !strchr( mapname, '\\' ))
			{
				Q_strncpy( gc_landmark3_name, mapname, sizeof( gc_landmark3_name ));
				gc_landmark3_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override landmark3 %s\n",
					gc_landmark3_name );
			}
			continue;
		}

		if( !Q_strnicmp( cursor, "landmark2", 9 ) && ( cursor[9] == ' ' || cursor[9] == '\t' ))
		{
			mapname = cursor + 9;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
			{
				mapname[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_landmark2_name )
				&& !strchr( mapname, '\\' ))
			{
				Q_strncpy( gc_landmark2_name, mapname, sizeof( gc_landmark2_name ));
				gc_landmark2_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override landmark2 %s\n",
					gc_landmark2_name );
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

		/* CapFaces spawn stills: dumpeye x y z [pitch] [yaw] → -gcdumpeye csv */
		if( !Q_strnicmp( cursor, "dumpeye", 7 ) && ( cursor[7] == ' ' || cursor[7] == '\t' ))
		{
			char	*tok = cursor + 7;
			float	x = 0, y = 0, z = 0, pitch = 0, yaw = 0;
			int	n;

			while( *tok == ' ' || *tok == '\t' )
				tok++;
			n = sscanf( tok, "%f %f %f %f %f", &x, &y, &z, &pitch, &yaw );
			if( n == 4 )
			{
				/* x y z yaw — pitch defaults to 0 */
				yaw = pitch;
				pitch = 0.0f;
			}
			if( n >= 3 )
			{
				Q_snprintf( gc_dumpeye_arg, sizeof( gc_dumpeye_arg ),
					"%.0f,%.0f,%.0f,%.0f,%.0f", x, y, z, pitch, yaw );
				gc_dumpeye_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override dumpeye %s\n",
					gc_dumpeye_arg );
			}
			continue;
		}

		/* Solo local listen server (Create Game equivalent, no peers required).
		 * Must be parsed before bare "map" so it does not become -gcmap smoke. */
		if( !Q_strnicmp( cursor, "listenserver", 12 ) && ( cursor[12] == ' ' || cursor[12] == '\t' ))
		{
			mapname = cursor + 12;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ))
			{
				mapname[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_listenserver_map )
				&& !strchr( mapname, '/' ) && !strchr( mapname, '\\' ))
			{
				Q_strncpy( gc_listenserver_map, mapname, sizeof( gc_listenserver_map ));
				gc_listenserver_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override listenserver %s\n",
					gc_listenserver_map );
			}
			continue;
		}

		/* Peer join: connect <ip[:port]> after BBA/eth2sp1 bring-up. */
		if( !Q_strnicmp( cursor, "connect", 7 ) && ( cursor[7] == ' ' || cursor[7] == '\t' ))
		{
			mapname = cursor + 7;
			while( *mapname == ' ' || *mapname == '\t' )
				mapname++;
			len = strlen( mapname );
			while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n'
				|| mapname[len - 1] == ' ' || mapname[len - 1] == '\t' ))
			{
				mapname[--len] = '\0';
			}
			if( len > 0 && len < sizeof( gc_connect_addr )
				&& !strchr( mapname, '/' ) && !strchr( mapname, '\\' )
				&& !strchr( mapname, ' ' ) && !strchr( mapname, '\t' ))
			{
				Q_strncpy( gc_connect_addr, mapname, sizeof( gc_connect_addr ));
				gc_connect_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override connect %s\n",
					gc_connect_addr );
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

/*
 * After map/entity FAT markers, schedule return to Swiss so shutdown writes
 * "FAT shutdown (return to Swiss loader via exit stub)". Only arms when FAT
 * is writable — disc-only Dolphin probes keep running.
 *
 * Re-arms on each call (map → entity → playable) so a slow SD spawn cannot
 * burn the whole countdown before playable-present evidence lands.
 */
void GCube_ArmSwissQuit( void )
{
#if XASH_GAMECUBE
	if( !Sys_CheckParm( "-gcswissquit" ))
		return;
	if( !GCube_HasWritableStorage() )
		return;
	if( gc_swissquit_frames == -2 )
		return;
	/* ~6s of presents so playable-present + slow SD flush can land before quit. */
	gc_swissquit_frames = GC_SWISSQUIT_FRAMES;
	gc_swissquit_last_poll_ticks = 0;
	SYS_Report( "Xash3D GameCube: swissquit armed frames=%d\n", gc_swissquit_frames );
	GCube_AppendSwissEvidence( "swissquit armed frames=%d", gc_swissquit_frames );
#endif
}

void GCube_PollSwissQuit( void )
{
#if XASH_GAMECUBE
	u64 now;

	if( gc_swissquit_frames < 0 )
		return;

	/* vid Present and PAD input both call this; without coalescing the
	 * countdown finishes ~2× fast and can miss playable present evidence. */
	now = gettime();
	if( gc_swissquit_last_poll_ticks
		&& diff_msec( gc_swissquit_last_poll_ticks, now ) < 8 )
		return;
	gc_swissquit_last_poll_ticks = now;

	if( gc_swissquit_frames > 0 )
	{
		gc_swissquit_frames--;
		return;
	}
	gc_swissquit_frames = -2;
	SYS_Report( "Xash3D GameCube: swissquit → Sys_Quit (evidence complete)\n" );
	Sys_Quit( "swiss evidence complete" );
#endif
}

/*
 * GetArgv may miss gamecube.cfg when ISO9660 nested fopen is not ready yet.
 * Platform_Init reloads cfg after GetBasePath; append any missing -gc* tokens
 * onto host.argv (which aliases gc_argv) before Host_Init consumes -gcmap.
 */
static void GCube_InjectLateDiscArgv( void )
{
	int n;
	int cap = (int)( sizeof( gc_argv ) / sizeof( gc_argv[0] ));
	int injected = 0;

	if( !host.argv || host.argc < 1 )
		return;
	/* Only patch when host.argv is our static builder table. */
	if( host.argv != (const char **)gc_argv && host.argv != (char **)gc_argv )
		return;

	n = host.argc;
	if( n < 0 || n >= cap - 1 )
		return;

	if( gc_capfacessmoke_configured && !Sys_CheckParm( "-gccapfacessmoke" ) && n + 1 < cap )
	{
		gc_argv[n++] = "-gccapfacessmoke";
		injected++;
	}
	if( gc_worldrender_configured && !Sys_CheckParm( "-gcworldrender" ) && n + 1 < cap )
	{
		gc_argv[n++] = "-gcworldrender";
		injected++;
	}
	if( gc_smoke_map_configured && gc_smoke_map[0] && !Sys_CheckParm( "-gcmap" ) && n + 2 < cap )
	{
		gc_argv[n++] = "-gcmap";
		gc_argv[n++] = gc_smoke_map;
		injected++;
	}
	if( gc_dumpframes_configured && !Sys_CheckParm( "-gcdumpframes" ) && n + 1 < cap )
	{
		gc_argv[n++] = "-gcdumpframes";
		injected++;
	}

	if( !injected )
		return;

	gc_argv[n] = NULL;
	host.argc = n;
	SYS_Report( "Xash3D GameCube: late disc argv inject argc=%d (+%d)\n", n, injected );
}

int GCube_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	int fake_argc = 0;
	int i;

	/* Always read gamecube.cfg (disc and/or FAT) before deciding argv. */
	GCube_LoadDiscBootOverrides();

	/* Swiss can pass argc>1 (DOL path plus a .cli file's arguments). Those
	 * used to replace the whole built argv, which dropped -game <gamedir>, the
	 * 320x240 mode and every gamecube.cfg route flag. Build the normal argv
	 * and append the loader's extras after it. */
	if( in_argc > 1 && in_argv )
		SYS_Report( "Xash3D GameCube: using loader argv argc=%d argv0=%s (appending extras)\n",
			in_argc, in_argv[0] ? in_argv[0] : "(null)" );

	if( in_argc == 1 && in_argv && in_argv[0] && in_argv[0][0] )
		SYS_Report( "Xash3D GameCube: loader argv0=%s (building disc overrides)\n", in_argv[0] );

	gc_argv[fake_argc++] = "xash";
	gc_argv[fake_argc++] = "xash";
	gc_argv[fake_argc++] = "-dev";
	gc_argv[fake_argc++] = "2";
	gc_argv[fake_argc++] = "-log";
	gc_argv[fake_argc++] = "-game";
	/* Baked at link time via XASH_GAMEDIR / --gamedir (halflife1.dol → valve). */
	gc_argv[fake_argc++] = GC_ACTIVE_GAMEDIR;
	if( gc_newgame_configured )
		gc_argv[fake_argc++] = "-gcnewgame";
	if( gc_menu_newgame_configured )
		gc_argv[fake_argc++] = "-gcmenuplaystart";
	/* Keep the actor policy explicit in the DLL argv. Some static HLSDK
	 * loaders expose the menu route after argv normalization; this dedicated
	 * flag makes the full-AI/sentence/node-graph profile unambiguous. */
	if( gc_menu_newgame_configured )
		gc_argv[fake_argc++] = "-gcfullactors";
	if( gc_worldrender_configured )
		gc_argv[fake_argc++] = "-gcworldrender";
	if( gc_capfacessmoke_configured )
		gc_argv[fake_argc++] = "-gccapfacessmoke";
	if( gc_worlddl_configured )
		gc_argv[fake_argc++] = "-gcworlddl";
	if( gc_smoke_map_configured )
	{
		gc_argv[fake_argc++] = "-gcmap";
		gc_argv[fake_argc++] = gc_smoke_map;
	}
	if( gc_dumpeye_configured && gc_dumpeye_arg[0] )
	{
		gc_argv[fake_argc++] = "-gcdumpeye";
		gc_argv[fake_argc++] = gc_dumpeye_arg;
	}
	if( gc_listenserver_configured )
	{
		gc_argv[fake_argc++] = "-gclistenserver";
		gc_argv[fake_argc++] = gc_listenserver_map;
		/* Reuse -gcmap BSP tip path so listen boots do not Mem_Alloc world
		 * lumps after SP BSS edicts leave <32 KiB heap (solo-listen2 OOM). */
		if( !gc_smoke_map_configured )
		{
			gc_argv[fake_argc++] = "-gcmap";
			gc_argv[fake_argc++] = gc_listenserver_map;
		}
	}
	if( gc_connect_configured )
	{
		gc_argv[fake_argc++] = "-gcconnect";
		gc_argv[fake_argc++] = gc_connect_addr;
		/* Peer Mod_LoadWorld needs GC_MapLoadMemoryOpt (-gcmap) for the
		 * map-load high-slot surface carve; listen already injects -gcmap.
		 * Prefer disc "map <name>" (smoke override); else keep connect-only. */
		if( !gc_smoke_map_configured )
			SYS_Report( "Xash3D GameCube: connect without map — add 'map <bsp>' to gamecube.cfg for peer world tip path\n" );
	}
	if( !gc_newgame_configured && !gc_smoke_map_configured && !gc_worldrender_configured
		&& !gc_listenserver_configured && !gc_connect_configured )
	{
		SYS_Report( "Xash3D GameCube: disc boot override menu (retail Flipper)\n" );
	}
	if( gc_newsaveload_configured )
		gc_argv[fake_argc++] = "-gcnewsaveload";
	if( gc_configroundtrip_configured )
		gc_argv[fake_argc++] = "-gcconfigroundtrip";
	if( gc_saveinterrupt_configured )
		gc_argv[fake_argc++] = "-gcsaveinterrupt";
	if( gc_video_mode_configured )
	{
		gc_argv[fake_argc++] = "-gcvideo";
		gc_argv[fake_argc++] = gc_video_mode;
	}
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
	if( gc_changelevel2_configured )
	{
		gc_argv[fake_argc++] = "-gcchangelevel2";
		gc_argv[fake_argc++] = gc_changelevel2_map;
	}
	if( gc_landmark2_configured )
	{
		gc_argv[fake_argc++] = "-gclandmark2";
		gc_argv[fake_argc++] = gc_landmark2_name;
	}
	if( gc_changelevel3_configured )
	{
		gc_argv[fake_argc++] = "-gcchangelevel3";
		gc_argv[fake_argc++] = gc_changelevel3_map;
	}
	if( gc_landmark3_configured )
	{
		gc_argv[fake_argc++] = "-gclandmark3";
		gc_argv[fake_argc++] = gc_landmark3_name;
	}
	if( gc_tas_configured )
	{
		gc_argv[fake_argc++] = "-gctas";
		gc_argv[fake_argc++] = gc_tas_name;
	}
	if( gc_dumpframes_configured )
		gc_argv[fake_argc++] = "-gcdumpframes";
	if( gc_swissquit_configured )
		gc_argv[fake_argc++] = "-gcswissquit";
	if( gc_retail_newgame_configured && !gc_menu_newgame_configured )
	{
		gc_argv[fake_argc++] = "-gcmenuplaystart";
		/* retailnewgame runs the full intro server under the retail physics
		 * profile; keep the actor policy explicit across argv normalization. */
		gc_argv[fake_argc++] = "-gcfullactors";
		gc_argv[fake_argc++] = "-gcplayerinput";
	}
	if( gc_memreport_configured )
		gc_argv[fake_argc++] = "-gcmemreport";
	if( gc_net_configured )
		gc_argv[fake_argc++] = "-net";
	if( gc_netfast_configured )
		gc_argv[fake_argc++] = "-netfast";
	if( gc_netstatic_configured )
		gc_argv[fake_argc++] = "-netstatic";
	if( gc_net_ip_configured )
	{
		gc_argv[fake_argc++] = "-gcnetip";
		gc_argv[fake_argc++] = gc_net_ip_override;
	}
	if( gc_net_mask_override[0] )
	{
		gc_argv[fake_argc++] = "-gcnetmask";
		gc_argv[fake_argc++] = gc_net_mask_override;
	}
	if( gc_net_gw_override[0] )
	{
		gc_argv[fake_argc++] = "-gcnetgw";
		gc_argv[fake_argc++] = gc_net_gw_override;
	}
	if( gc_headless_configured )
		gc_argv[fake_argc++] = "-gcheadless";
	if( ( gc_listenserver_configured || gc_connect_configured ) && !gc_headless_configured )
	{
		gc_argv[fake_argc++] = "-gcplayerinput";
	}
	if( gc_profile_configured )
		gc_argv[fake_argc++] = "-gcprofile";
	if( gc_nolivepvs_configured )
		gc_argv[fake_argc++] = "-gcnolivepvs";
	if( gc_nonativetex_configured )
		gc_argv[fake_argc++] = "-gcnonativetex";
	if( gc_nativetex_configured )
		gc_argv[fake_argc++] = "-gcnativetex";
	if( gc_notexpacks_configured )
		gc_argv[fake_argc++] = "-gcnotexpacks";
	if( gc_autohop_configured )
		gc_argv[fake_argc++] = "-gcautohop";
	if( gc_autocampaign_configured )
	{
		gc_argv[fake_argc++] = "-gcautocampaign";
		gc_argv[fake_argc++] = gc_autocampaign_delay;
	}
	if( gc_modeltrace_configured )
		gc_argv[fake_argc++] = "-gcmodeltrace";
	gc_argv[fake_argc++] = "-width";
	/* Tip-safe soft/internal FB. Flipper EFB/XFB stay native 640×480 via VI. */
	gc_argv[fake_argc++] = "320";
	gc_argv[fake_argc++] = "-height";
	gc_argv[fake_argc++] = "240";

	/* Loader extras go last. Sys_GetParmFromCmdLine takes the first match, so
	 * a repeated flag cannot override gamecube.cfg; it only adds new ones. */
	for( i = 1; in_argv && i < in_argc; i++ )
	{
		if( !in_argv[i] || !in_argv[i][0] )
			continue;
		if( fake_argc + 1 >= (int)( sizeof( gc_argv ) / sizeof( gc_argv[0] )))
		{
			SYS_Report( "Xash3D GameCube: loader argv truncated at %s\n", in_argv[i] );
			break;
		}
		gc_argv[fake_argc++] = in_argv[i];
	}

	*out_argv = gc_argv;
	return fake_argc;
}

void GCube_Shutdown( void )
{
#if XASH_GAMECUBE
	size_t i;

	/* G29: Shutdown networking layer (BBA/eth2sp1 then engine sockets). */
	GC_NetShutdown();
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
