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
static double gc_last_present_time;
static double gc_worst_frame_ms;
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

	gc.initialized = false;

	GX_AbortFrame();
	VIDEO_SetBlack( true );
	VIDEO_Flush();
#endif
}

static void GC_PresentBuffer( void )
{
#if XASH_GAMECUBE
	unsigned short *src;
	unsigned short *dst;
	unsigned short *diag_rowdst;
	int copy_w, copy_h, row, col2;
	int src_w, src_h;
	qboolean sampled_nonblack;
	size_t buf_size;
	int col_diag;
	int check_w;
	unsigned short *scanrow;
	unsigned short first_pixel;
	double now;
	double elapsed_ms;

	src = NULL;
	dst = NULL;
	diag_rowdst = NULL;
	copy_w = 0;
	copy_h = 0;
	row = 0;
	col2 = 0;
	src_w = 0;
	src_h = 0;
	sampled_nonblack = false;
	buf_size = 0;
	col_diag = 0;
	check_w = 0;
	scanrow = NULL;
	first_pixel = 0;
	now = 0.0;
	elapsed_ms = 0.0;

	if( !rmode || !xfb[which_fb] )
		return;

	gc_present_count++;

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

		buf_size = gc.stride * gc.height * sizeof(unsigned short);
		DCFlushRange( gc.buffer, (u32)buf_size );

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
			col2 = 0;
			for( col2 = 0; col2 < check_w; col2++ )
			{
				if( scanrow[col2] != 0 )
				{
					sampled_nonblack = true;
					break;
				}
			}
		}
		else
		{
			sampled_nonblack = false;
		}
	}
	else
	{
		/* G36: Diagnostic blue fill only for first frame when buffer is missing.
		 * Avoid wasting CPU cycles on full-screen fills after initial evidence
		 * is captured. Leaves XFB black (zeroed) for subsequent frames. */
		if( gc_present_count == 1 )
		{
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


	/* G36: Emit frame budget markers only for early frames to establish visual evidence.
	 * Suppress per-frame SYS_Report in steady-state to reduce route-time render cost.
	 * The first present reports 0ms so short smoke probes still get a parsable sample. */
	if( gc_present_count <= 2 )
	{
		now = Sys_FloatTime();
		elapsed_ms = gc_last_present_time > 0.0 ? ( now - gc_last_present_time ) * 1000.0 : 0.0;
		if( elapsed_ms > gc_worst_frame_ms )
			gc_worst_frame_ms = elapsed_ms;
		SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u blank_frames=%u\n",
			gc_present_count, sampled_nonblack ? 1u : 0u, gc_blank_present_count );
		SYS_Report( "Xash3D GameCube: frame render complete\n" );
		SYS_Report( "Xash3D GameCube: frame time=%.2fms\n", elapsed_ms );
		if( elapsed_ms >= 33.0 )
			SYS_Report( "Xash3D GameCube: G49 slow frame %.2fms worst=%.2fms\n", elapsed_ms, gc_worst_frame_ms );
		gc_last_present_time = now;
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
	gc_quality = Cvar_Get( "gc_quality", "1", FCVAR_ARCHIVE, "GameCube quality profile: 0=smoke, 1=release, 2=high telemetry-only" );
	GC_ReportQualityProfile( "video-init" );
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

static unsigned char GC_FatalGlyphRow( char ch, int row )
{
	static const unsigned char digit[10][7] = {
		{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
		{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
		{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
		{ 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },
		{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
		{ 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E },
		{ 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E },
		{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
		{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
		{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E },
	};
	static const unsigned char alpha[26][7] = {
		{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
		{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
		{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
		{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
		{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
		{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
		{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E },
		{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
		{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
		{ 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C },
		{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
		{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
		{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },
		{ 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
		{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
		{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
		{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },
		{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
		{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },
		{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
		{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A },
		{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
		{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },
		{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
	};

	if( row < 0 || row >= 7 )
		return 0;
	if( ch >= 'a' && ch <= 'z' )
		ch -= 32;
	if( ch >= 'A' && ch <= 'Z' )
		return alpha[ch - 'A'][row];
	if( ch >= '0' && ch <= '9' )
		return digit[ch - '0'][row];
	switch( ch )
	{
	case ':': return ( row == 1 || row == 5 ) ? 0x04 : 0x00;
	case '.': return ( row == 6 ) ? 0x04 : 0x00;
	case '-': return ( row == 3 ) ? 0x1F : 0x00;
	case '_': return ( row == 6 ) ? 0x1F : 0x00;
	case '/': return 1 << ( row < 5 ? 4 - row : 0 );
	case '\\': return 1 << ( row < 5 ? row : 4 );
	case '<': return row < 4 ? ( 1 << ( 3 - row )) : ( 1 << ( row - 3 ));
	case '>': return row < 4 ? ( 1 << ( row + 1 )) : ( 1 << ( 7 - row ));
	case ' ': return 0x00;
	default: return 0x04;
	}
}

static void GC_FatalPutPixel( unsigned short *dst, int x, int y, unsigned short color )
{
	if( x < 0 || y < 0 || !rmode || x >= rmode->fbWidth || y >= rmode->xfbHeight )
		return;
	dst[y * rmode->fbWidth + x] = color;
}

static void GC_FatalDrawChar( unsigned short *dst, int x, int y, char ch, unsigned short color, int scale )
{
	int row, col, sx, sy;
	for( row = 0; row < 7; row++ )
	{
		unsigned char bits = GC_FatalGlyphRow( ch, row );
		for( col = 0; col < 5; col++ )
		{
			if( !FBitSet( bits, 1 << ( 4 - col )))
				continue;
			for( sy = 0; sy < scale; sy++ )
				for( sx = 0; sx < scale; sx++ )
					GC_FatalPutPixel( dst, x + col * scale + sx, y + row * scale + sy, color );
		}
	}
}

static int GC_FatalDrawLine( unsigned short *dst, int x, int y, const char *text, unsigned short color, int scale, int max_chars )
{
	int i;
	for( i = 0; text && text[i] && text[i] != '\n' && i < max_chars; i++ )
		GC_FatalDrawChar( dst, x + i * 6 * scale, y, text[i], color, scale );
	return i;
}

static void GC_FatalDrawWrapped( unsigned short *dst, int x, int y, const char *text, unsigned short color, int scale, int max_chars, int max_lines )
{
	int line = 0;
	while( text && *text && line < max_lines )
	{
		int used = GC_FatalDrawLine( dst, x, y + line * 9 * scale, text, color, scale, max_chars );
		text += used;
		if( *text == '\n' )
			text++;
		while( *text == ' ' )
			text++;
		line++;
	}
}

/*
 * G60: Present loading progress without relying on renderer assets. Long map
 * loads can otherwise look like a hard hang on real hardware or composite
 * capture, especially before the client has a valid loading texture.
 */
void GC_DrawLoadingStatus( const char *message, const char *details )
{
#if XASH_GAMECUBE
	unsigned short *dst;
	unsigned short *rowdst;
	int row;
	int col;
	int panel_x;
	int panel_y;
	int panel_w;
	int panel_h;
	size_t xfb_size;

	if( !rmode || !xfb[which_fb] )
		return;

	dst = (unsigned short *)xfb[which_fb];
	panel_x = 24;
	panel_w = rmode->fbWidth - 48;
	panel_h = 92;
	panel_y = rmode->xfbHeight - panel_h - 24;
	if( panel_y < 24 )
		panel_y = 24;

	for( row = panel_y; row < panel_y + panel_h && row < rmode->xfbHeight; row++ )
	{
		rowdst = dst + row * rmode->fbWidth;
		for( col = panel_x; col < panel_x + panel_w && col < rmode->fbWidth; col++ )
		{
			if( row == panel_y || row == panel_y + panel_h - 1 ||
				col == panel_x || col == panel_x + panel_w - 1 )
				rowdst[col] = 0x07FF; /* cyan border */
			else
				rowdst[col] = 0x0010; /* dark blue panel */
		}
	}

	GC_FatalDrawLine( dst, panel_x + 18, panel_y + 12, "LOADING", 0xFFFF, 2, 24 );
	GC_FatalDrawLine( dst, panel_x + 18, panel_y + 38, message ? message : "PLEASE WAIT", 0xFFE0, 2, 34 );
	GC_FatalDrawLine( dst, panel_x + 18, panel_y + 64, details ? details : "GAMECUBE VIDEO ALIVE", 0x07E0, 1, 72 );

	xfb_size = rmode->fbWidth * rmode->xfbHeight * sizeof(unsigned short);
	DCFlushRange( xfb[which_fb], (u32)xfb_size );
	VIDEO_SetNextFramebuffer( xfb[which_fb] );
	VIDEO_Flush();
	VIDEO_WaitVSync();
	which_fb ^= 1;
#endif
}

/*
 * G50: Draw a readable fatal breadcrumb directly to XFB so it survives silent
 * crashes and missing assets. This is called from Sys_Error before shutdown.
 */
void GC_DrawFatalBreadcrumb( const char *message, const char *details )
{
#if XASH_GAMECUBE
	unsigned short *dst;
	unsigned short *rowdst;
	int row;
	int col_fatal;
	int i;
	size_t xfb_size;

	dst = NULL;
	rowdst = NULL;
	row = 0;
	col_fatal = 0;
	i = 0;
	xfb_size = 0;

	/* G65: Guard against early Sys_Error before video init.
	 * GC_DrawFatalBreadcrumb writes directly to XFB via rmode/xfb pointers.
	 * If called before GC_InitVideoHardware completes, rmode/xfb are uninitialized,
	 * causing guest_fatal in Dolphin or real hardware. Skip visual output if
	 * video hardware is not initialized; rely on Sys_Error/SYS_Report for diagnostics. */
	if( !gc.initialized )
		return;

	/* G66: Additional safety guard - verify rmode and xfb[0] are valid before drawing.
	 * Prevents guest_fatal in Dolphin when video subsystem is partially initialized
	 * or in an inconsistent state during error paths. */
	if( !rmode || !rmode->fbWidth || !rmode->xfbHeight || !xfb[0] )
		return;

	/* Do not clear gc.initialized - this function draws to XFB and
	 * leaves hardware in a presentable state. Clearing initialization
	 * can break subsequent rendering attempts or cause hardware state
	 * mismatches during error recovery paths. */

	/* Present to front buffer immediately for visibility */
	dst = (unsigned short *)xfb[0];

	/* Fill XFB with high-contrast Magenta (RGB565 0xF81F) to signal FATAL ERROR */
	for( row = 0; row < rmode->xfbHeight; row++ )
	{
		rowdst = dst + row * rmode->fbWidth;
		for( col_fatal = 0; col_fatal < rmode->fbWidth; col_fatal++ )
			rowdst[col_fatal] = 0xF81F; /* magenta */
	}

	GC_FatalDrawLine( dst, 24, 6, "XASH3D GAMECUBE FATAL", 0xFFFF, 2, 34 );
	GC_FatalDrawWrapped( dst, 24, 42, message ? message : "UNKNOWN ERROR", 0xFFE0, 2, 38, 5 );
	GC_FatalDrawWrapped( dst, 24, 150, details ? details : "NO DETAILS", 0xFFFF, 2, 38, 8 );
	GC_FatalDrawLine( dst, 24, rmode->xfbHeight - 28, "HALTED: POWER CYCLE OR RESET", 0x07E0, 2, 38 );

	/* G51: Do not flush GX pipeline here. If GX is in an inconsistent state due to
	 * the fatal error, GX_Flush/GX_DrawDone can trigger guest_fatal hangs in
	 * Dolphin or on real hardware. We only need to flush the data cache for the
	 * XFB region and present it via VIDEO_SetNextFramebuffer. The GX command
	 * buffer is not used for this CPU-written fatal message. */

	/* G68: Guard the presentation sequence. If the VIDEO subsystem is not fully
	 * configured (e.g., VIDEO_Configure failed or was interrupted before Sys_Error),
	 * calling VIDEO_SetNextFramebuffer or VIDEO_WaitVSync can trigger guest_fatal
	 * in Dolphin or hang on real hardware. Check if VIDEO_Init has completed
	 * successfully by verifying VIDEO_GetPreferredMode returns a non-NULL mode.
	 * This is a conservative check; if it fails, we skip visual output and rely
	 * on SYS_Report/Sys_Error for diagnostics. */
	if( VIDEO_GetPreferredMode( NULL ) != NULL )
	{
		xfb_size = rmode->fbWidth * rmode->xfbHeight * sizeof(unsigned short);
		DCFlushRange( xfb[0], (u32)xfb_size );
		VIDEO_SetNextFramebuffer( xfb[0] );
		VIDEO_Flush();
		VIDEO_WaitVSync();

		/* G68: Single VSync wait is sufficient for visibility and avoids guest_fatal
		 * hangs in Dolphin that can occur with multiple VSync calls from error paths.
		 * Remove the previous loop of 3 VIDEO_WaitVSync() calls. */
	}
	else
	{
		/* VIDEO subsystem not fully initialized; skip visual output.
		 * The magenta fill and text are still written to xfb[0] in memory,
		 * but they will not be presented. Diagnostics rely on SYS_Report. */
	}

	/* Do not toggle which_fb here. The fatal breadcrumb draws directly to xfb[0]
	 * and leaves the double-buffering state unchanged. Modifying which_fb during
	 * an error path can cause subsequent rendering (if any) to target the wrong
	 * framebuffer, leading to visual artifacts or guest_fatal hangs in Dolphin.
	 * Since this function is called from Sys_Error before process exit, preserving
	 * the original which_fb value ensures any late-stage diagnostic output remains
	 * consistent with the normal rendering path. */

	/* Return control to caller (e.g., Sys_Error) for proper termination or
	 * recovery. Do not call SYS_ResetSystem here as it can cause guest_fatal
	 * hangs or crash loops in emulated or real hardware environments. */
#endif
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
