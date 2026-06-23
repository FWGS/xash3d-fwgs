/*
r_gcmap.c - GameCube smoke boot memory trims
*/
#include "r_local.h"

#if XASH_GAMECUBE
static qboolean gc_renderer_trimmed;

void R_GcmapTrimScreenBuffers( void );
void R_GcmapTrimSurfaceCache( void );
qboolean R_TryInitGcmapSurfaceCache( void );
qboolean R_AllocScreen( void );

void R_GcmapTrimForMapLoad( void )
{
	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ) || gc_renderer_trimmed )
		return;

	R_GcmapTrimSurfaceCache();
	R_GcmapTrimScreenBuffers();
	gc_renderer_trimmed = true;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer trim for map load\n" );
}

void R_GcmapRestoreAfterMapLoad( void )
{
	if( !gc_renderer_trimmed )
		return;

	R_InitCaches();
	R_AllocScreen();
	gc_renderer_trimmed = false;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer restore after map load\n" );
}

void R_GcmapMarkMapLoadComplete( void )
{
	gc_renderer_trimmed = false;
}

qboolean R_GcmapEnsureSurfaceCache( void )
{
	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ))
		return false;

	return R_TryInitGcmapSurfaceCache();
}
#endif
