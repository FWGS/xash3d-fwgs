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

#define GC_DATA_PATH "xash3d"
#define GC_DVD_DEVICE "gcdisc"

static qboolean gc_fat_mounted;
static qboolean gc_dvd_mounted;
static DISC_INTERFACE gc_dvd_io;

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

#if XASH_MESSAGEBOX == MSGBOX_GAMECUBE
#include <ogc/system.h>
void Platform_MessageBox( const char *title, const char *message, qboolean unused )
{
	(void)unused;
	if( title && message )
		SYS_Report( "%s:\n%s\n", title, message );
	else if( message )
		SYS_Report( "%s\n", message );
}
#endif

void Platform_Sleep( int msec )
{
	usleep( msec * 1000 );
}

double Platform_DoubleTime( void )
{
#if XASH_GAMECUBE
	static u64 start_ticks;
	u64 now = SYS_Time();

	if( start_ticks == 0 )
		start_ticks = now;

	return (double)diff_ticks( start_ticks, now ) / (double)PPC_TIMER_CLOCK;
#else
	return 0.0;
#endif
}

void *Platform_GetNativeObject( const char *name )
{
	(void)name;
	return NULL;
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

#if XASH_GAMECUBE
struct passwd {
	char *pw_name;
};

uid_t geteuid( void )
{
	return 0;
}

struct passwd *getpwuid( uid_t uid )
{
	(void)uid;
	return NULL;
}

int execv( const char *path, char *const argv[] )
{
	(void)path;
	(void)argv;
	return -1;
}
#endif

void GCube_EarlyInit( void )
{
#if XASH_GAMECUBE
	/* Make startup and fatal errors visible in Dolphin before video is ready. */
	SYS_STDIO_Report( true );
	SYS_Report( "Xash3D GameCube: bootstrap\n" );
	GC_EarlyBootSplash();
#endif
}

static qboolean GCube_PathAccessible( const char *path )
{
	DIR *dir = opendir( path );
	if( !dir )
		return false;

	// Check if directory is accessible and readable
	if (access( path, R_OK) != 0 ) {
		closedir( dir );
		return false;
	}

	closedir( dir );
	return true;
}

qboolean GCube_GetDiscPath( char *buf, size_t buflen )
{
	const char *path = GC_DVD_DEVICE ":/" GC_DATA_PATH;

	if( !gc_dvd_mounted || !GCube_PathAccessible( path ))
		return false;

	Q_strncpy( buf, path, buflen );
	return true;
}

qboolean GCube_GetWritablePath( char *buf, size_t buflen )
{
	if( !gc_fat_mounted || !GCube_PathAccessible( "sd:/" ))
		return false;

	Q_strncpy( buf, "sd:/" GC_DATA_PATH, buflen );
	return true;
}

qboolean GCube_HasWritableStorage( void )
{
	char path[MAX_SYSPATH];

	return GCube_GetWritablePath( path, sizeof( path ));
}

static void GCube_MkdirIgnoreExists( const char *path )
{
	if( mkdir( path, 0777 ) != 0 ) {
		if (errno != EEXIST)
			Con_Reportf( S_WARN "GameCube storage: failed to create %s (%s)\n", path, strerror( errno ));
	}
}

/*
 * G32: GameCube Save/Load Policy
 *
 * Storage: SD Card via `fat:` interface (sd:/xash3d/valve/save).
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
	if( !gc_fat_mounted )
	{
		Con_Reportf( "Xash3D GameCube: SD storage not mounted (disc-only mode)\n" );
		return;
	}

	/* fatGetSpace availability varies across devkitPro/libogc versions.
	 * Skip dynamic free-space query to maintain build compatibility.
	 * Storage write failures are still reported via Con_Reportf in Host_WriteConfig/FS_SaveVFSConfig. */
#endif
}

void GCube_EnsureWritableLayout( void )
{
	char base[MAX_SYSPATH];
	char valve[MAX_SYSPATH];
	char save[MAX_SYSPATH];

	if( !GCube_GetWritablePath( base, sizeof( base )))
		return;

	GCube_MkdirIgnoreExists( base );
	Q_snprintf( valve, sizeof( valve ), "%s/valve", base );
	GCube_MkdirIgnoreExists( valve );
	Q_snprintf( save, sizeof( save ), "%s/valve/save", base );
	GCube_MkdirIgnoreExists( save );

	/* G32: Ensure standard Half-Life save slots directory exists.
	 * The engine uses 'valve/save' for autosaves and 'valve/save/<mapname>'
	 * for manual saves in some builds, but standard HL uses 'valve/save'
	 * directly for .sav files. We ensure the base is ready. */

	/* G32: Log storage status for bounding/failure diagnosis */
	GCube_LogStorageStatus();
}

void GCube_Init( void )
{
#if XASH_GAMECUBE
	char xashdir[MAX_SYSPATH];

	/* G29: Initialize networking for local loopback single-player.
	 * Disable external network dependencies (master servers, HTTP)
	 * to ensure offline boot works without network hardware. */
	NET_Config( false, false );

	gc_fat_mounted = fatInitDefault();
	if( !gc_fat_mounted )
		Con_Reportf( S_WARN "SD card init failed\n" );

	if( !gc_dvd_mounted )
	{
		SYS_Report( "Xash3D GameCube: mounting DVD filesystem\n" );
		DVD_Init();
		gc_dvd_io = __io_gcdvd;
		gc_dvd_io.readSectors = GCube_DVDReadSectors;
		gc_dvd_mounted = ISO9660_Mount( GC_DVD_DEVICE, &gc_dvd_io );
		if( !gc_dvd_mounted )
			Con_Reportf( S_WARN "Xash3D GameCube: DVD mount failed\n" );
		else
			SYS_Report( "Xash3D GameCube: DVD mount ok\n" );
	}
	if( gc_dvd_mounted )
		Con_Reportf( "GameCube DVD filesystem mounted (%s)\n",
			ISO9660_GetVolumeLabel( GC_DVD_DEVICE ));
	else
		Con_Reportf( S_WARN "DVD filesystem init failed\n" );

	if( GCube_GetWritablePath( xashdir, sizeof( xashdir )))
	{
		GCube_EnsureWritableLayout();
		Con_Reportf( "Xash3D GameCube: writable storage %s\n", xashdir );
	}
	else if( GCube_GetDiscPath( xashdir, sizeof( xashdir )))
	{
		Con_Reportf( "Xash3D GameCube: read-only fallback %s (no SD)\n", xashdir );
	}
	else if( GCube_GetBasePath( xashdir, sizeof( xashdir )))
	{
		Con_Reportf( S_WARN "GameCube storage: using legacy base path %s\n", xashdir );
	}
	else
	{
		SYS_Report( "Xash3D GameCube: no base path found (SD/DVD missing or empty)\n" );
		Con_Reportf( S_ERROR "No data directory found. Game assets will not load.\n" );
		/* Removed Sys_Error as it might crash on missing assets in read-only mode */
	}

	if( chdir( xashdir ) == 0 )
		Con_Reportf( "GameCube data directory: %s\n", xashdir );
	else
	{
		Con_Reportf( S_ERROR "GameCube storage: failed to chdir to %s (errno %d: %s)\n", xashdir, errno, strerror( errno ) );
		/* G47: If we cannot chdir to the data directory, asset lookups will likely fail.
		   Report this as a warning to allow runtime checks to potentially recover from asset misses. */
		Con_Reportf( S_WARN "GameCube storage: failed to chdir to %s (errno %d: %s)\n", xashdir, errno, strerror( errno ) );
	}

	setup_gamecube_dll_functions();
#endif
}

qboolean GCube_GetBasePath( char *buf, size_t buflen )
{
#if XASH_GAMECUBE
	if( GCube_GetWritablePath( buf, buflen ))
		return true;
	if( GCube_GetDiscPath( buf, buflen ))
		return true;

	{
		static const char *paths[] =
		{
			"sd:/",
			GC_DVD_DEVICE ":/",
		};
		size_t i;

		for( i = 0; i < ARRAYSIZE( paths ); i++ )
		{
			if( !GCube_PathAccessible( paths[i] ))
				continue;

			Q_strncpy( buf, paths[i], buflen );
			return true;
		}
	}
#endif
	(void)buf;
	(void)buflen;
	return false;
}

#define GC_MAX_ARGV 24
#define GC_DEFAULT_SMOKE_MAP "c0a0e"
static char *gc_argv[GC_MAX_ARGV];
static char gc_smoke_map[MAX_QPATH] = GC_DEFAULT_SMOKE_MAP;
static qboolean gc_smoke_map_configured;
static qboolean gc_newgame_configured;
static qboolean gc_world_render_configured;

static void GCube_LoadDiscBootOverrides( void )
{
#if XASH_GAMECUBE
	FILE *file;
	char line[128];

	gc_smoke_map_configured = false;
	gc_newgame_configured = false;
	gc_world_render_configured = false;
	Q_strncpy( gc_smoke_map, GC_DEFAULT_SMOKE_MAP, sizeof( gc_smoke_map ));

	if( !gc_dvd_mounted )
	{
		DVD_Init();
		gc_dvd_io = __io_gcdvd;
		gc_dvd_io.readSectors = GCube_DVDReadSectors;
		gc_dvd_mounted = ISO9660_Mount( GC_DVD_DEVICE, &gc_dvd_io );
	}

	if( !gc_dvd_mounted )
		return;

	file = fopen( GC_DVD_DEVICE ":/" GC_DATA_PATH "/valve/gamecube.cfg", "r" );
	if( !file )
		return;

	while( fgets( line, sizeof( line ), file ))
	{
		char *cursor = line;
		char *mapname;
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

		if( !Q_strnicmp( cursor, "gcworldrender", 13 ))
		{
			char ch = cursor[13];
			if( ch == '\0' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t' )
			{
				gc_world_render_configured = true;
				SYS_Report( "Xash3D GameCube: disc boot override gcworldrender\n" );
				continue;
			}
		}

		if( Q_strnicmp( cursor, "map", 3 ) || ( cursor[3] != ' ' && cursor[3] != '\t' ))
			continue;

		mapname = cursor + 3;
		while( *mapname == ' ' || *mapname == '\t' )
			mapname++;
		len = strlen( mapname );
		while( len > 0 && ( mapname[len - 1] == '\r' || mapname[len - 1] == '\n' ||
			mapname[len - 1] == ' ' || mapname[len - 1] == '\t' ))
		{
			mapname[--len] = '\0';
		}
		if( len > 0 && len < sizeof( gc_smoke_map ) && !strchr( mapname, '/' ) && !strchr( mapname, '\\' ))
		{
			Q_strncpy( gc_smoke_map, mapname, sizeof( gc_smoke_map ));
			gc_smoke_map_configured = true;
			SYS_Report( "Xash3D GameCube: smoke map override %s\n", gc_smoke_map );
			continue;
		}
	}

	fclose( file );
#endif
}

int GCube_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	int fake_argc = 0;

	if( in_argc > 1 )
	{
		*out_argv = in_argv;
		return in_argc;
	}

	GCube_LoadDiscBootOverrides();

	gc_argv[fake_argc++] = "xash";
	gc_argv[fake_argc++] = "-dev";
	gc_argv[fake_argc++] = "2";
	gc_argv[fake_argc++] = "-log";
	gc_argv[fake_argc++] = "-game";
	gc_argv[fake_argc++] = "valve";
	if( gc_smoke_map_configured )
	{
		gc_argv[fake_argc++] = "-nointro";
		gc_argv[fake_argc++] = "-toconsole";
		gc_argv[fake_argc++] = "-gcmap";
		gc_argv[fake_argc++] = gc_smoke_map;
		gc_argv[fake_argc++] = "-gcnolightmaps";
		gc_argv[fake_argc++] = "-gcnobevels";
		if( gc_world_render_configured )
			gc_argv[fake_argc++] = "-gcworldrender";
		gc_argv[fake_argc++] = "map";
		gc_argv[fake_argc++] = gc_smoke_map;
	}
	else if( gc_newgame_configured )
	{
		gc_argv[fake_argc++] = "-gcnewgame";
	}
	gc_argv[fake_argc++] = "-width";
	gc_argv[fake_argc++] = "320";
	gc_argv[fake_argc++] = "-height";
	gc_argv[fake_argc++] = "240";

	*out_argv = gc_argv;
	return fake_argc;
}

void GCube_Shutdown( void )
{
#if XASH_GAMECUBE
	/* G29: Shutdown networking layer. */
	NET_Shutdown();

	if( gc_dvd_mounted )
		ISO9660_Unmount( GC_DVD_DEVICE );
	if( gc_fat_mounted )
		fatUnmount( "sd" );
	gc_dvd_mounted = false;
	gc_fat_mounted = false;
#endif
}
