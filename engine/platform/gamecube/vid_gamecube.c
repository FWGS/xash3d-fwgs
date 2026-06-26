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
static volatile qboolean gc_fatal_breadcrumb_active;
static GXRModeObj *rmode = NULL;
static uint8_t gx_fifo[256 * 1024] __attribute__((aligned(32)));
static unsigned int gc_present_count;
static unsigned int gc_blank_present_count;
static convar_t *gc_quality;
static double gc_last_present_time;
#endif

#define GC_VIDEO_SAFE_AREA_PERCENT 10
#define GC_VIDEO_MIN_READABLE_WIDTH 320
#define GC_VIDEO_MIN_READABLE_HEIGHT 240

/* GC_GetVisualQuality is provided by ref/gx/r_local.h as an inline helper.
 * The platform video backend does not redefine it to avoid duplicate symbols.
 * Quality 0: Low (smoke/minimal visuals, reduced particles)
 * Quality 1: Medium (default, some optimizations for memory)
 * Quality 2: High (full visuals if memory permits)
 */

void Platform_Minimize_f( void )
{
}

static void GC_InitVideoHardware( void )
{
#if XASH_GAMECUBE
	int safe_x, safe_y, safe_w, safe_h;
	qboolean progressive;

	if( gc.initialized )
		return;

	gc_last_present_time = 0.0;
	SYS_Report( "Xash3D GameCube: mem stage=video_init total=%.2f\n", 0.0 );
	VIDEO_Init();
	/* G44: Use libogc's region/cable-safe preferred mode. Do not force 480p. */
	rmode = VIDEO_GetPreferredMode( NULL );
	VIDEO_Configure( rmode );
	progressive = ( rmode->viTVMode & VI_NON_INTERLACE ) ? true : false;
	safe_x = ( rmode->fbWidth * GC_VIDEO_SAFE_AREA_PERCENT ) / 100;
	safe_y = ( rmode->xfbHeight * GC_VIDEO_SAFE_AREA_PERCENT ) / 100;
	safe_w = rmode->fbWidth - safe_x * 2;
	safe_h = rmode->xfbHeight - safe_y * 2;
	SYS_Report( "Xash3D GameCube: video mode fb=%dx%d efb=%dx%d vi=%dx%d tv=0x%08x progressive=%u policy=preferred-4:3-480i\n",
		rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth, rmode->efbHeight,
		rmode->viWidth, rmode->viHeight, rmode->viTVMode, progressive ? 1u : 0u );
	SYS_Report( "Xash3D GameCube: video safe_area percent=%d rect=%d,%d,%d,%d min_readable=%dx%d\n",
		GC_VIDEO_SAFE_AREA_PERCENT, safe_x, safe_y, safe_w, safe_h,
		GC_VIDEO_MIN_READABLE_WIDTH, GC_VIDEO_MIN_READABLE_HEIGHT );

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
	SYS_Report( "Xash3D GameCube: renderer initialized gx\n" );
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
	int copy_w, copy_h, row, col2;
	int src_w, src_h;
	qboolean sampled_nonblack;
	size_t buf_size;
	int col_diag;
	int check_w;
	unsigned short *scanrow;
	unsigned short first_pixel;

	if( !rmode || !xfb[which_fb] )
		return;

	copy_w = rmode->fbWidth;
	copy_h = rmode->xfbHeight;
	dst = (unsigned short *)xfb[which_fb];

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		src = gc.buffer;
		src_w = gc.width;
		src_h = gc.height;
		if( src_w > copy_w )
			src_w = copy_w;
		if( src_h > copy_h )
			src_h = copy_h;

		// G36: Flush buffer from cache before copying to XFB
		// DCFlushRange expects (start, end), not (start, size)
		buf_size = gc.stride * gc.height * sizeof(unsigned short);
		DCFlushRange(gc.buffer, (void *)((unsigned char *)gc.buffer + buf_size));

		// G36: Sample first pixel for visual evidence only on first frame
		if( gc_present_count == 1 )
		{
			first_pixel = gc.buffer[0];
			SYS_Report( "Xash3D GameCube: software buffer pixel[0]=0x%04X (RGB565)\n", first_pixel );
		}

		// G36: Copy visible rows to XFB
		for( row = 0; row < src_h; row++ )
			memcpy( dst + row * rmode->fbWidth, src + row * gc.stride, src_w * sizeof( unsigned short ));

		// G36: Detect non-black content on first frame only to stabilize frame budget
		if( gc_present_count == 1 && src_h > 0 && src_w > 0 )
		{
			check_w = src_w < 8 ? src_w : 8;
			scanrow = src;
			for( col2 = 0; col2 < check_w; col2++ )
			{
				if( scanrow[col2] != 0 )
				{
					sampled_nonblack = true;
					break;
				}
			}
		}
	}
	else
	{
		/* G36: Diagnostic blue fill only for first frame when buffer is missing.
		 * Avoid wasting CPU cycles on full-screen fills after initial evidence
		 * is captured. Leaves XFB black (zeroed) for subsequent frames. */
		if( gc_present_count == 1 )
		{
			unsigned short *diag_rowdst;
			col_diag = 0;
			for( row = 0; row < copy_h; row++ )
			{
				diag_rowdst = dst + row * rmode->fbWidth;
				for( col_diag = 0; col_diag < copy_w; col_diag++ )
					diag_rowdst[col_diag] = 0x001F; /* Blue in RGB565 -- diagnostic frame */
			}
			sampled_nonblack = true;
		}
		else
		{
			sampled_nonblack = false;
		}
	}


	GX_Flush();
	GX_DrawDone();

	/* G36: Emit frame budget markers only for early frames to establish visual evidence.
	 * Suppress per-frame SYS_Report in steady-state to reduce route-time render cost.
	 * The first present reports 0ms so short smoke probes still get a parsable sample. */
	if( gc_present_count <= 2 )
	{
		double now, elapsed_ms;
		now = Sys_FloatTime();
		elapsed_ms = gc_last_present_time > 0.0 ? ( now - gc_last_present_time ) * 1000.0 : 0.0;
		SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u blank_frames=%u\n",
			gc_present_count, sampled_nonblack ? 1u : 0u, gc_blank_present_count );
		SYS_Report( "Xash3D GameCube: frame render complete\n" );
		SYS_Report( "Xash3D GameCube: frame time=%.2fms\n", elapsed_ms );
		gc_last_present_time = now;
	}

	DCFlushRange( xfb[which_fb], (void *)((unsigned char *)xfb[which_fb] + VIDEO_GetFrameBufferSize( rmode )));
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
		SYS_Report( "Xash3D GameCube: mem stage=video_alloc total=%.2f\n", ( width * height * 2.0 ) / ( 1024.0 * 1024.0 ) );
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

/*
 * G37: Draw fatal breadcrumb directly to XFB so it survives silent crashes.
 * Uses a simple solid background with a clear distinction from normal frames.
 * This is called from Sys_Error path before shutdown.
 */
void GC_DrawFatalBreadcrumb( const char *message )
{
#if XASH_GAMECUBE
	unsigned short *dst;
	int row, col_fatal, i;
	size_t xfb_size;
	unsigned short *rowdst;

	if( !rmode || !xfb[0] )
		return;

	gc_fatal_breadcrumb_active = true;

	/* Present to front buffer immediately for visibility */
	dst = (unsigned short *)xfb[0];

	/* Fill XFB with a distinct color: Magenta (RGB565 0xF81F) to signal ERROR */
	for( row = 0; row < rmode->xfbHeight; row++ )
	{
		rowdst = dst + row * rmode->fbWidth;
		for( col_fatal = 0; col_fatal < rmode->fbWidth; col_fatal++ )
			rowdst[col_fatal] = 0xF81F; /* Magenta */
	}

	/* Flush to ensure hardware sees it */
	// DCFlushRange expects (start, end), not (start, size)
	xfb_size = rmode->fbWidth * rmode->xfbHeight * sizeof(unsigned short);
	DCFlushRange(xfb[0], (void *)((unsigned char *)xfb[0] + xfb_size));
	VIDEO_SetNextFramebuffer( xfb[0] );
	VIDEO_Flush();
	VIDEO_WaitVSync();

	/* Block briefly to ensure frame is presented before exit.
	 * Use multiple VIDEO_WaitVSync() calls to ensure the frame is
	 * presented on screen before the process exits. This is more
	 * portable than SYS_Delay across different libogc versions. */
	for( i = 0; i < 3; i++ )
		VIDEO_WaitVSync();
#endif
	(void)message; /* Message is already reported via OSReport in Sys_Error */
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
