/*
r_gcmap.c - GameCube smoke boot memory trims
*/
#include "r_local.h"

#if XASH_GAMECUBE
static qboolean gc_renderer_trimmed;
static short gc_gcmap_static_zbuffer[160 * 128];
static pixel_t gc_gcmap_static_viewbuffer[160 * 128];

void R_GcmapTrimScreenBuffers( void );
void R_GcmapTrimSurfaceCache( void );
qboolean R_TryInitGcmapSurfaceCache( void );
qboolean R_AllocScreen( void );

qboolean R_GcmapOwnsStaticScreenBuffers( void )
{
	return d_pzbuffer == gc_gcmap_static_zbuffer || vid.buffer == gc_gcmap_static_viewbuffer;
}

qboolean R_GcmapOwnsStaticZBuffer( void )
{
	return d_pzbuffer == gc_gcmap_static_zbuffer;
}

qboolean R_GcmapOwnsStaticViewBuffer( void )
{
	return vid.buffer == gc_gcmap_static_viewbuffer;
}

void R_GcmapReleaseDynamicScreenBuffers( void )
{
	if( d_pzbuffer )
	{
		if( d_pzbuffer != gc_gcmap_static_zbuffer )
			free( d_pzbuffer );
		d_pzbuffer = NULL;
	}

	if( vid.buffer )
	{
		if( vid.buffer != gc_gcmap_static_viewbuffer )
			free( vid.buffer );
		vid.buffer = NULL;
	}
}

qboolean R_GcmapGetViewport( int *width, int *height )
{
	if( !vid.width || !vid.height )
		return false;

	if( width )
		*width = vid.width;
	if( height )
		*height = vid.height;
	return true;
}

void R_GcmapTrimForMapLoad( void )
{
	if( gc_renderer_trimmed )
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

	if( gEngfuncs.Sys_CheckParm( "-gcmap" ))
	{
		if( !R_TryInitGcmapSurfaceCache() )
			gEngfuncs.Con_Reportf( "Xash3D GameCube: surface cache restore deferred after map load\n" );
		gEngfuncs.Con_Reportf( "Xash3D GameCube: renderer screen restore deferred for gcmap smoke route\n" );
	}
	else
	{
		R_InitCaches();
		R_AllocScreen();
	}

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

qboolean R_GcmapPrepareWorldRender( void )
{
	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ))
		return false;

	if( vid.buffer && d_pzbuffer && vid.width > 0 && vid.height > 0 )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap world render reusing retained screen buffers\n" );
		return true;
	}

	if( R_GcmapAllocMinimalScreen() )
		return true;

	return R_AllocScreen();
}

qboolean R_GcmapAllocMinimalScreen( void )
{
	int w, h;
	size_t pixels;
	qboolean use_static_screen = false;

	if( !gEngfuncs.Sys_CheckParm( "-gcmap" ))
		return false;

	if( vid.buffer && d_pzbuffer && vid.width > 0 && vid.height > 0 )
		return true;

	w = gpGlobals->width > 0 ? gpGlobals->width : 320;
	h = gpGlobals->height > 0 ? gpGlobals->height : 240;
	if( gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
	{
		/* Probe route: keep the world pass visible but tighter than the
		 * generic half-res smoke path so software BSP fill fits GameCube CPU. */
		w = 128;
		h = 96;
		use_static_screen = ( w <= 160 && h <= 128 ) ? true : false;
	}
	if( w < 128 )
		w = 128;
	if( h < 96 )
		h = 96;
	else if( !gEngfuncs.Sys_CheckParm( "-gcworldrender" ) && h < 128 )
		h = 128;

	vid.width = w;
	vid.height = h;
	vid.rowbytes = w;
	pixels = (size_t)w * (size_t)h;

	if( gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
	{
		gpGlobals->width = w;
		gpGlobals->height = h;
	}

	R_GcmapReleaseDynamicScreenBuffers();

	if( use_static_screen )
	{
		d_pzbuffer = gc_gcmap_static_zbuffer;
		vid.buffer = gc_gcmap_static_viewbuffer;
		memset( d_pzbuffer, 0xff, pixels * sizeof( d_pzbuffer[0] ));
		memset( vid.buffer, 0, pixels * sizeof( vid.buffer[0] ));
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen ready %dx%d (static)\n", w, h );
		return true;
	}

	d_pzbuffer = malloc( pixels * sizeof( d_pzbuffer[0] ));
	if( !d_pzbuffer )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen missing z buffer\n" );
		return false;
	}

	vid.buffer = malloc( pixels * sizeof( pixel_t ));
	if( !vid.buffer )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen missing colormap buffer\n" );
		free( d_pzbuffer );
		d_pzbuffer = NULL;
		return false;
	}

	gEngfuncs.Con_Reportf( "Xash3D GameCube: gcmap minimal screen ready %dx%d\n", w, h );
	return true;
}
#endif
