/*
vid_gamecube.c - GameCube video backend (software buffer + GX display)
Copyright (C) 2026 xash3d-gc contributors

Ported from Division-Zero-GX/xash3d-wii with libogc GX output for GameCube.
*/
#include "platform/platform.h"

#if XASH_VIDEO == VIDEO_GX

#include "client.h"
#include "ref_common.h"
#include "vid_common.h"
#include <stdlib.h>
#include <string.h>

#if XASH_GAMECUBE
#include <ogc/gx.h>
#include <ogc/video.h>
#include <ogc/system.h>
#include <ogc/cache.h>
#endif

typedef struct gc_video_s
{
	qboolean initialized;
	int width;
	int height;
	int stride;
	uint bpp;
	unsigned short *buffer;
} gc_video_t;

static gc_video_t gc;
#if XASH_GAMECUBE
static void *xfb[2] = { NULL, NULL };
static int which_fb = 0;
static GXRModeObj *rmode = NULL;
static uint8_t gx_fifo[256 * 1024] __attribute__((aligned(32)));
static unsigned int gc_present_count;
static unsigned int gc_blank_present_count;
static convar_t *gc_quality;
#endif

/*
================
GC_GetVisualQuality

Returns the current GameCube visual quality mode.
0: Low (smoke/minimal visuals, no lightmaps, reduced particles)
1: Medium (default, some optimizations for memory)
2: High (full visuals if memory permits)
================
*/
int GC_GetVisualQuality( void )
{
#if XASH_GAMECUBE
	if( gc_quality )
		return (int)gc_quality->value;
#endif
	return 1;
}

void Platform_Minimize_f( void )
{
}

static void GC_InitVideoHardware( void )
{
#if XASH_GAMECUBE
	if( gc.initialized )
		return;

	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode( NULL );
	VIDEO_Configure( rmode );

	xfb[0] = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
	xfb[1] = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
	VIDEO_SetNextFramebuffer( xfb[which_fb] );
	VIDEO_SetBlack( false );
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if( rmode->viTVMode & VI_NON_INTERLACE )
		VIDEO_WaitVSync();

	GX_Init( gx_fifo, sizeof( gx_fifo ));
	GX_SetDispCopyGamma( GX_GM_1_0 );
	GX_SetCopyFilter( rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter );
	GX_SetFieldMode( rmode->field_rendering, (( rmode->viHeight == 2 * rmode->xfbHeight ) ? GX_ENABLE : GX_DISABLE ));

	/* Software renderer outputs 16-bit RGB565; force matching display format */
	GX_SetPixelFmt( GX_PF_RGB565_Z16, GX_ZC_LINEAR );

	gc.initialized = true;
#endif
}

static void GC_ShutdownVideoHardware( void )
{
#if XASH_GAMECUBE
	if( !gc.initialized )
		return;

	GX_AbortFrame();
	VIDEO_SetBlack( true );
	VIDEO_Flush();
	gc.initialized = false;
#endif
}

static void GC_PresentBuffer( void )
{
#if XASH_GAMECUBE
	unsigned short *src;
	unsigned short *dst;
	int copy_w, copy_h, row;
	qboolean sampled_nonblack = false;

	if( !rmode || !xfb[which_fb] )
		return;

	copy_w = rmode->fbWidth;
	copy_h = rmode->xfbHeight;
	dst = (unsigned short *)xfb[which_fb];

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		src = gc.buffer;
		int src_w = gc.width;
		int src_h = gc.height;
		if( src_w > copy_w )
			src_w = copy_w;
		if( src_h > copy_h )
			src_h = copy_h;

		for( row = 0; row < src_h; row++ )
			memcpy( dst + row * rmode->fbWidth, src + row * gc.stride, src_w * sizeof( unsigned short ));

		for( row = 0; row < src_h && !sampled_nonblack; row += 8 )
		{
			int col;
			for( col = 0; col < src_w; col += 8 )
			{
				if( src[row * gc.stride + col] != 0 )
				{
					sampled_nonblack = true;
					break;
				}
			}
		}
	}
	else
	{
		/* Diagnostic: clear to a visible color when no software buffer is ready */
		for( row = 0; row < copy_h; row++ )
		{
			unsigned short *rowdst = dst + row * rmode->fbWidth;
			int col;
			for( col = 0; col < copy_w; col++ )
				rowdst[col] = 0x001F; /* Blue in RGB565 -- diagnostic frame */
		}
		sampled_nonblack = true;
	}

	gc_present_count++;
	if( !sampled_nonblack )
	{
		int mark_w = copy_w < 32 ? copy_w : 32;
		int mark_h = copy_h < 32 ? copy_h : 32;
		gc_blank_present_count++;

		// Draw a highly visible diagnostic marker (Red/Green checker)
		// to prove XFB is being updated even if renderer output is black.
		for( row = 0; row < mark_h; row++ )
		{
			int col;
			unsigned short *rowdst = dst + row * rmode->fbWidth;
			for( col = 0; col < mark_w; col++ )
			{
				// Red (0xF800) and Green (0x07E0) checker
				rowdst[col] = (( row ^ col ) & 8 ) ? 0xF800 : 0x07E0;
			}
		}
		
		// Report diagnostic marker visibility
		if( gc_blank_present_count == 1 || ( gc_blank_present_count % 60 == 0 ))
		{
			SYS_Report( "Xash3D GameCube: DIAGNOSTIC MARKER VISIBLE (frame %u, blank streak %u). Check top-left 32x32.\n",
				gc_present_count, gc_blank_present_count );
		}
	}

	if( gc_present_count <= 8 )
	{
		SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u blank_frames=%u\n",
			gc_present_count, sampled_nonblack ? 1u : 0u, gc_blank_present_count );
	}

	DCFlushRange( xfb[which_fb], VIDEO_GetFrameBufferSize( rmode ));
	VIDEO_SetNextFramebuffer( xfb[which_fb] );
	VIDEO_Flush();
	VIDEO_WaitVSync();
	which_fb ^= 1;
#else
	(void)0;
#endif
}

qboolean R_Init_Video( ref_graphic_apis_t type )
{
	if( type != REF_GX && type != REF_SOFTWARE )
		return false;

#if XASH_GAMECUBE
	gc_quality = Cvar_Get( "gc_quality", "1", FCVAR_ARCHIVE, "GameCube visual quality mode: 0=low/smoke, 1=medium, 2=high" );
#endif

	GC_InitVideoHardware();

#if XASH_GAMECUBE
	{
		uint stride, bpp, r, g, b;
		int width = refState.width > 0 ? refState.width : ( rmode ? rmode->fbWidth : DEFAULT_MODE_WIDTH );
		int height = refState.height > 0 ? refState.height : ( rmode ? rmode->efbHeight : DEFAULT_MODE_HEIGHT );

		if( !SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
		{
			SYS_Report( "GX video: failed to allocate software buffer %dx%d\n", width, height );
			return false;
		}
		SYS_Report( "GX video: software buffer %dx%d allocated (stride %u, bpp %u)\n",
		          width, height, stride, bpp );
		GC_PresentBuffer();
	}
#endif

	host.renderinfo_changed = false;
	return true;
}

void R_Free_Video( void )
{
	if( gc.buffer )
	{
		free( gc.buffer );
		gc.buffer = NULL;
	}

	GC_ShutdownVideoHardware();

	if( ref.dllFuncs.GL_ClearExtensions )
		ref.dllFuncs.GL_ClearExtensions();
}

void GL_SwapBuffers( void )
{
	GC_PresentBuffer();
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	if( gc.buffer )
		free( gc.buffer );

	gc.width = width;
	gc.height = height;
	gc.stride = width;
	gc.bpp = 2;
	gc.buffer = calloc( width * height, sizeof( unsigned short ));

	if( !gc.buffer )
		return false;

	*stride = gc.stride;
	*bpp = gc.bpp;
	*r = 0xF800;
	*g = 0x07E0;
	*b = 0x001F;
	return true;
}

void *SW_LockBuffer( void )
{
	return gc.buffer;
}

void SW_UnlockBuffer( void )
{
}

qboolean VID_SetMode( void )
{
	R_ChangeDisplaySettings( 0, 0, WINDOW_MODE_FULLSCREEN );
	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
#if XASH_GAMECUBE
	if( rmode )
	{
		if( refState.width > 0 )
			width = refState.width;
		else
			width = rmode->fbWidth;
		if( refState.height > 0 )
			height = refState.height;
		else
			height = rmode->efbHeight;
	}
#endif

	if( !width ) width = DEFAULT_MODE_WIDTH;
	if( !height ) height = DEFAULT_MODE_HEIGHT;

	(void)window_mode;

#if XASH_GAMECUBE
	{
		uint stride, bpp, r, g, b;
		if( !SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
			return rserr_unknown;
		GC_PresentBuffer();
	}
#endif

	R_SaveVideoMode( width, height, width, height, false );
	return rserr_ok;
}

int GL_SetAttribute( int attr, int val )
{
	(void)attr;
	(void)val;
	return 0;
}

int GL_GetAttribute( int attr, int *val )
{
	(void)attr;
	if( val ) *val = 0;
	return 0;
}

int R_MaxVideoModes( void )
{
	return 0;
}

vidmode_t *R_GetVideoMode( int num )
{
	(void)num;
	return NULL;
}

void *GL_GetProcAddress( const char *name )
{
	(void)name;
	return NULL;
}

void GC_TrimVideoMemoryForMapLoad( void )
{
	if( gc.buffer )
	{
		free( gc.buffer );
		gc.buffer = NULL;
	}
}

void GC_RestoreVideoMemoryAfterMapLoad( void )
{
	uint stride, bpp, r, g, b;

	if( gc.buffer || gc.width <= 0 || gc.height <= 0 )
		return;

	SW_CreateBuffer( gc.width, gc.height, &stride, &bpp, &r, &g, &b );
}

void GL_UpdateSwapInterval( void )
{
}

void VID_Info_f( void )
{
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	(void)handle;
	(void)type;
	return REF_WINDOW_TYPE_NULL;
}

#endif /* XASH_VIDEO == VIDEO_GX */
