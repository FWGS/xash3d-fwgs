/*
sys_gamecube.c - GameCube platform backend
Copyright (C) 2026 xash3d-gc contributors

Platform layer ported from Division-Zero-GX/xash3d-wii.
*/
#include "platform/platform.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#if XASH_GAMECUBE
#include <ogc/system.h>
#include <ogc/dvd.h>
#include <fat.h>
#include <iso9660.h>
#include <dirent.h>
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
void Platform_MessageBox( const char *title, const char *message, qboolean unused )
{
	(void)unused;
	fprintf( stderr, "%s:\n%s\n", title, message );
}
#endif

void Platform_Sleep( int msec )
{
	usleep( msec * 1000 );
}

double Platform_DoubleTime( void )
{
#if XASH_GAMECUBE
	return (double)SYS_Time() / 1000.0;
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
#endif
}

static qboolean GCube_PathAccessible( const char *path )
{
	DIR *dir = opendir( path );
	if( !dir )
		return false;

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
	if( mkdir( path, 0777 ) != 0 && errno != EEXIST )
		Con_Reportf( S_WARN "GameCube storage: failed to create %s (%s)\n", path, strerror( errno ));
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
}

void GCube_Init( void )
{
#if XASH_GAMECUBE
	char xashdir[MAX_SYSPATH];

	gc_fat_mounted = fatInitDefault();
	if( !gc_fat_mounted )
		Con_Reportf( S_WARN "SD card init failed\n" );

	DVD_Init();
	gc_dvd_io = __io_gcdvd;
	gc_dvd_io.readSectors = GCube_DVDReadSectors;
	gc_dvd_mounted = ISO9660_Mount( GC_DVD_DEVICE, &gc_dvd_io );
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
		Con_Reportf( S_WARN "No data directory found. Game assets will not load.\n" );
		setup_gamecube_dll_functions();
		GC_MemSample( "filesystem" );
		return;
	}

	if( chdir( xashdir ) == 0 )
		Con_Reportf( "GameCube data directory: %s\n", xashdir );
	else
		Con_Reportf( S_WARN "Failed to chdir to %s\n", xashdir );

	setup_gamecube_dll_functions();
	GC_MemSample( "filesystem" );
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

int GCube_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	int fake_argc = 0;

	if( in_argc > 1 )
	{
		*out_argv = in_argv;
		return in_argc;
	}

	gc_argv[fake_argc++] = "xash";
	gc_argv[fake_argc++] = "-dev";
	gc_argv[fake_argc++] = "2";
	gc_argv[fake_argc++] = "-log";
	gc_argv[fake_argc++] = "-toconsole";
	gc_argv[fake_argc++] = "-nointro";
	gc_argv[fake_argc++] = "-gcnolightmaps";
	gc_argv[fake_argc++] = "+r_fullbright";
	gc_argv[fake_argc++] = "1";
	gc_argv[fake_argc++] = "-gcnobevels";
	gc_argv[fake_argc++] = "-gcnodeltareinit";
	gc_argv[fake_argc++] = "-width";
	gc_argv[fake_argc++] = "320";
	gc_argv[fake_argc++] = "-height";
	gc_argv[fake_argc++] = "240";
	gc_argv[fake_argc++] = "-gcmap";
	gc_argv[fake_argc++] = gc_smoke_map;

	*out_argv = gc_argv;
	return fake_argc;
}

void GCube_Shutdown( void )
{
#if XASH_GAMECUBE
	if( gc_dvd_mounted )
		ISO9660_Unmount( GC_DVD_DEVICE );
	if( gc_fat_mounted )
		fatUnmount( "sd" );
	gc_dvd_mounted = false;
	gc_fat_mounted = false;
#endif
}
