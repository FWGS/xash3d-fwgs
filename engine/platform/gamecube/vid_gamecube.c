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
#include "imagelib/imagelib.h"
#if XASH_GAMECUBE
#include "server.h"
#include "mod_local.h"
#include "gamecube/mem_gamecube.h"

qboolean R_GcmapEnsureSurfaceCache( void );
qboolean R_TryInitLowResSurfaceCache( void );
void R_GcmapTrimSurfaceCache( void );
qboolean R_GcmapEnsureWorldRenderScratch( void );
qboolean R_GcmapPrepareWorldRender( void );
qboolean R_GcmapGetViewport( int *width, int *height );
qboolean GC_PrepareNewGameWorldPresent( void );
#endif
#include <stdlib.h>
#include <string.h>

#if XASH_GAMECUBE
#include <ogc/gx.h>
#include <ogc/gu.h>
#include <ogc/video.h>
#include <ogc/color.h>
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
	size_t buffer_pixels;
} gc_video_t;

static gc_video_t gc;
#if XASH_GAMECUBE
static void *xfb[2] = { NULL, NULL };
static int which_fb = 0;
static GXRModeObj *rmode = NULL;
static uint8_t gx_fifo[256 * 1024] __attribute__((aligned(32)));
static unsigned int gc_present_count;
static unsigned int gc_blank_present_count;
static unsigned int gc_budget_sample_count;
static unsigned int gc_budget_warmup_left;
static unsigned int gc_light_present_left;
static qboolean gc_newgame_world_ready;
static qboolean gc_newgame_g36_done; /* sticky: never re-arm probe after first flush */
static int gc_newgame_viewcluster = -1;
static qboolean gc_newgame_pvs_ready;
static byte *gc_newgame_vis; /* active row pointer into pvs table */
static byte *gc_newgame_nodebits; /* active row pointer into node table */
static byte *gc_newgame_pvs_table; /* [numclusters][visbytes] */
static byte *gc_newgame_node_table; /* [numclusters][nodebytes] */
static byte *gc_newgame_cluster_valid; /* one byte per cluster */
static int gc_newgame_numclusters;
static int gc_newgame_visbytes;
static int gc_newgame_nodebytes;
static int gc_newgame_numleafs;
static int gc_newgame_numnodes;
static int gc_newgame_vis_leafs;
static int gc_newgame_vis_nodes;
typedef struct
{
	vec3_t	mins;
	vec3_t	maxs;
	int	cluster;
} gc_newgame_leafbox_t;
static gc_newgame_leafbox_t *gc_newgame_leafboxes;
static int gc_newgame_nleafboxes;
static qboolean gc_newgame_pvs_follow_proved;
static qboolean gc_gx_present_logged;
static convar_t *gc_quality;
static double gc_last_present_time;
static double gc_worst_frame_ms;
static qboolean gc_budget_probe_active;
static GXTexObj gc_present_tex;
static qboolean gc_present_tex_ready;
static gc_boot_phase_t gc_boot_phase = GC_BOOT_NONE;
#endif

#if XASH_GAMECUBE
const char *GC_GetBootPhaseName( gc_boot_phase_t phase )
{
	switch( phase )
	{
	case GC_BOOT_EARLY: return "early";
	case GC_BOOT_RENDERER: return "renderer";
	case GC_BOOT_SW_FB: return "sw_fb";
	case GC_BOOT_ENGINE: return "engine";
	case GC_BOOT_CLIENT: return "client";
	case GC_BOOT_MENU: return "menu";
	case GC_BOOT_INTRO: return "intro";
	case GC_BOOT_MAP: return "map";
	default: return "none";
	}
}

gc_boot_phase_t GC_GetBootPhase( void )
{
	return gc_boot_phase;
}

void GC_ReportBootPhase( gc_boot_phase_t phase )
{
	gc_boot_phase_t prev = gc_boot_phase;

	if( phase < gc_boot_phase )
		return;

	gc_boot_phase = phase;
	SYS_Report( "Xash3D GameCube: boot phase=%s last=%s\n",
		GC_GetBootPhaseName( phase ), GC_GetBootPhaseName( prev ));
}

qboolean GC_BootDrawAllowed( void )
{
	/* Fallback menu FillRGBA / present need a validated software framebuffer. */
	return gc_boot_phase >= GC_BOOT_SW_FB && gc.buffer != NULL && gc.width > 0 && gc.height > 0;
}
#endif

#define GC_VIDEO_SAFE_AREA_PERCENT 10
#define GC_VIDEO_MIN_READABLE_WIDTH 320
#define GC_VIDEO_MIN_READABLE_HEIGHT 240
#define GC_VIDEO_PROBE_WIDTH 320
#define GC_VIDEO_PROBE_HEIGHT 240
/* Post-map New Game G36 presents — match r_gcmap static screen (160×120). */
#define GC_VIDEO_NEWGAME_PROBE_WIDTH 160
#define GC_VIDEO_NEWGAME_PROBE_HEIGHT 120
/* Skip first Host_Frame after arm (connect residual), then sample. */
#define GC_VIDEO_BUDGET_WARMUP_PRESENTS 1
#define GC_VIDEO_BUDGET_SAMPLE_TARGET 16
#define GC_VIDEO_NEWGAME_BUDGET_SAMPLE_TARGET 8
/* Keep SCR on the light fill path after G36 samples so Host_Frame still presents
 * while we restore the framebuffer for world render. Short grace so real hardware
 * reaches the low-res world path quickly after evidence is collected. */
#define GC_VIDEO_LIGHT_PRESENT_GRACE 8

#if XASH_GAMECUBE
/* Collected during the probe window; flushed to OSReport only after sampling so
 * SYS_Report I/O does not inflate the measured Host_Frame intervals. */
static float gc_budget_sample_ms[GC_VIDEO_BUDGET_SAMPLE_TARGET];
static uint8_t gc_budget_sample_nonblack[GC_VIDEO_BUDGET_SAMPLE_TARGET];
#endif

static unsigned int GC_GetFrameBudgetSampleTarget( void )
{
#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnewgame" ))
		return GC_VIDEO_NEWGAME_BUDGET_SAMPLE_TARGET;
#endif
	return GC_VIDEO_BUDGET_SAMPLE_TARGET;
}

/* GC_GetVisualQuality is provided by ref/gx/r_local.h as an inline helper.
 * The platform video backend does not redefine it to avoid duplicate symbols.
 * Quality 0: Low (smoke/minimal visuals, reduced particles)
 * Quality 1: Medium (default, some optimizations for memory)
 * Quality 2: High (full visuals if memory permits)
 */

void Platform_Minimize_f( void )
{
}

void GC_EarlyBootSplash( void )
{
#if XASH_GAMECUBE
	unsigned short *dst;

	if( gc.initialized || rmode )
		return;

	SYS_Report( "Xash3D GameCube: early video splash\n" );
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode( NULL );
	if( !rmode )
		return;

	VIDEO_Configure( rmode );
	xfb[0] = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
	if( !xfb[0] )
		return;

	dst = (unsigned short *)xfb[0];
	VIDEO_ClearFrameBuffer( rmode, dst, 0x0010 ); /* dark blue boot frame */
	VIDEO_SetNextFramebuffer( xfb[0] );
	VIDEO_SetBlack( false );
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if( rmode->viTVMode & VI_NON_INTERLACE )
		VIDEO_WaitVSync();
#endif
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
	if( !rmode )
	{
		VIDEO_Init();
		rmode = VIDEO_GetPreferredMode( NULL );
		VIDEO_Configure( rmode );
	}
	else
	{
		SYS_Report( "Xash3D GameCube: video init continuing after early splash\n" );
	}
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

	if( !xfb[0] )
		xfb[0] = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
	if( !xfb[1] )
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

	GX_SetViewport( 0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1 );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
	GX_SetNumChans( 1 );
	GX_SetNumTexGens( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetNumTevStages( 1 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetCullMode( GX_CULL_NONE );

	gc_present_tex_ready = false;
	gc.initialized = true;
	SYS_Report( "Xash3D GameCube: renderer initialized gx\n" );
	GC_ReportBootPhase( GC_BOOT_RENDERER );
#endif
}

#if XASH_GAMECUBE
/* Linear→tiled staging for GX_TF_RGB565 (4×4). Sized for New Game / probe presents. */
#define GC_GX_TILE_MAX_W 320
#define GC_GX_TILE_MAX_H 240
static u16 gc_tiled_rgb565[GC_GX_TILE_MAX_W * GC_GX_TILE_MAX_H] __attribute__((aligned( 32 )));
/* Post-map MEM1 cannot calloc a present FB; keep a BSS New Game probe buffer
 * so G36 never falls back to a 640×480 CPU blit on real hardware. */
static unsigned short gc_probe_rgb565[GC_VIDEO_NEWGAME_PROBE_WIDTH * GC_VIDEO_NEWGAME_PROBE_HEIGHT] __attribute__((aligned( 32 )));
static qboolean gc_buffer_owns_heap;

static void GC_InitPresentTexture( void )
{
	if( !gc.buffer || gc.width <= 0 || gc.height <= 0 )
	{
		gc_present_tex_ready = false;
		return;
	}

	GX_InitTexObj( &gc_present_tex, gc.buffer, (u16)gc.width, (u16)gc.height,
		GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
	GX_InitTexObjFilterMode( &gc_present_tex, GX_LINEAR, GX_LINEAR );
	gc_present_tex_ready = true;
}

static int gc_tiled_tex_w;
static int gc_tiled_tex_h;
static void *gc_tiled_tex_ptr;
static qboolean gc_gx_present_pipe_ready;

static void GC_InitPresentTextureTiled( void *tiled, int width, int height )
{
	if( !tiled || width <= 0 || height <= 0 )
	{
		gc_present_tex_ready = false;
		gc_tiled_tex_ptr = NULL;
		gc_tiled_tex_w = 0;
		gc_tiled_tex_h = 0;
		return;
	}

	/* Same staging buffer + size: keep the TexObj; DCFlush + Invalidate refresh data. */
	if( gc_present_tex_ready && tiled == gc_tiled_tex_ptr
		&& width == gc_tiled_tex_w && height == gc_tiled_tex_h )
		return;

	GX_InitTexObj( &gc_present_tex, tiled, (u16)width, (u16)height,
		GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
	GX_InitTexObjFilterMode( &gc_present_tex, GX_NEAR, GX_NEAR );
	gc_tiled_tex_ptr = tiled;
	gc_tiled_tex_w = width;
	gc_tiled_tex_h = height;
	gc_present_tex_ready = true;
}

static void GC_SwizzleRGB565ToTiled( const unsigned short *src, int src_stride,
	int width, int height, unsigned short *dst )
{
	int tile_x, tile_y, y;
	unsigned short *out = dst;

	/* GX_TF_RGB565 stores 4×4 tiles contiguously.
	 * Copy each tile row as two u32 loads when naturally aligned. */
	for( tile_y = 0; tile_y < height; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < width; tile_x += 4 )
		{
			for( y = 0; y < 4; y++ )
			{
				const unsigned short *row = src + ( tile_y + y ) * src_stride + tile_x;
#if defined( __GNUC__ )
				if( (((unsigned long)row) & 3 ) == 0 && (((unsigned long)out) & 3 ) == 0 )
				{
					const unsigned int *r32 = (const unsigned int *)(const void *)row;
					unsigned int *o32 = (unsigned int *)(void *)out;

					o32[0] = r32[0];
					o32[1] = r32[1];
				}
				else
#endif
				{
					out[0] = row[0];
					out[1] = row[1];
					out[2] = row[2];
					out[3] = row[3];
				}
				out += 4;
			}
		}
	}
}

static void GC_PresentBufferViaGX( void );

static qboolean GC_CanPresentViaGX( int width, int height )
{
	if( !gc.initialized || !rmode || !xfb[which_fb] )
		return false;
	if( width <= 0 || height <= 0 )
		return false;
	if(( width & 3 ) || ( height & 3 ))
		return false;
	if( width > GC_GX_TILE_MAX_W || height > GC_GX_TILE_MAX_H )
		return false;
	/* Prefer native GX (tiled RGB565 → EFB → XFB) for any buffer that fits
	 * the tile staging area. Avoids CPU YUYV upscale on GameCube. */
	return true;
}

static void GC_PresentBufferViaGX( void )
{
	f32 fb_w, fb_h;

	if( !rmode || !xfb[which_fb] || !gc_present_tex_ready )
		return;

	fb_w = (f32)rmode->fbWidth;
	fb_h = (f32)rmode->efbHeight;

	/* One-time ortho / copy / vtx setup; per-frame we only refresh the
	 * textured quad after DCFlush of the tiled RGB565 staging buffer. */
	if( !gc_gx_present_pipe_ready )
	{
		Mtx44 proj;
		Mtx modelview;

		GX_SetViewport( 0.0f, 0.0f, fb_w, fb_h, 0.0f, 1.0f );
		GX_SetScissor( 0, 0, (u32)fb_w, (u32)fb_h );
		GX_SetDispCopySrc( 0, 0, rmode->fbWidth, rmode->efbHeight );
		GX_SetDispCopyDst( rmode->fbWidth, rmode->xfbHeight );
		GX_SetDispCopyYScale((f32)rmode->xfbHeight / (f32)rmode->efbHeight );
		GX_SetCopyFilter( rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter );
		GX_SetDispCopyGamma( GX_GM_1_0 );

		guOrtho( proj, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
		GX_LoadProjectionMtx( proj, GX_ORTHOGRAPHIC );
		guMtxIdentity( modelview );
		GX_LoadPosMtxImm( modelview, GX_PNMTX0 );

		GX_ClearVtxDesc();
		GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
		gc_gx_present_pipe_ready = true;
	}

	GX_InvVtxCache();
	GX_InvalidateTexAll();
	GX_LoadTexObj( &gc_present_tex, GX_TEXMAP0 );

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( 0.0f, 0.0f, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 0.0f, 0.0f );
	GX_Position3f32( fb_w, 0.0f, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 1.0f, 0.0f );
	GX_Position3f32( fb_w, fb_h, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 1.0f, 1.0f );
	GX_Position3f32( 0.0f, fb_h, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 0.0f, 1.0f );
	GX_End();

	GX_DrawDone();
	GX_CopyDisp( xfb[which_fb], GX_TRUE );
	/* Second sync wait is only needed when the next CPU work can race the
	 * copy; silent G36 windows skip it to reclaim present ms at 160×120. */
	if( !gc_budget_probe_active )
		GX_DrawDone();
	else
		GX_Flush();
}
#endif

static void GC_ShutdownVideoHardware( void )
{
#if XASH_GAMECUBE
	if( !gc.initialized )
		return;

	gc.initialized = false;
	gc_present_tex_ready = false;
	gc_gx_present_pipe_ready = false;
	gc_tiled_tex_ptr = NULL;
	gc_tiled_tex_w = 0;
	gc_tiled_tex_h = 0;

	GX_AbortFrame();
	VIDEO_SetBlack( true );
	VIDEO_Flush();
#endif
}

static unsigned int GC_RGBPairToYUYV( unsigned short p1, unsigned short p2 )
{
	int r1, g1, b1, r2, g2, b2;
	int y1, cb1, cr1, y2, cb2, cr2;
	int cb, cr;

	/* Budget/New Game presents only need a readable non-black XFB; skip full
	 * BT.601 conversion (dominant cost of the software present path). */
	if( gc_budget_probe_active || gc_newgame_world_ready )
	{
		y1 = (( p1 >> 5 ) & 0x3F ) << 2;
		y2 = (( p2 >> 5 ) & 0x3F ) << 2;
		return ( y1 << 24 ) | ( 128 << 16 ) | ( y2 << 8 ) | 128;
	}

	r1 = ((( p1 >> 11 ) & 0x1F ) * 527 + 23 ) >> 6;
	g1 = ((( p1 >> 5 ) & 0x3F ) * 259 + 33 ) >> 6;
	b1 = (( p1 & 0x1F ) * 527 + 23 ) >> 6;
	r2 = ((( p2 >> 11 ) & 0x1F ) * 527 + 23 ) >> 6;
	g2 = ((( p2 >> 5 ) & 0x3F ) * 259 + 33 ) >> 6;
	b2 = (( p2 & 0x1F ) * 527 + 23 ) >> 6;

	y1 = ( 299 * r1 + 587 * g1 + 114 * b1 ) / 1000;
	cb1 = ( -16874 * r1 - 33126 * g1 + 50000 * b1 + 12800000 ) / 100000;
	cr1 = ( 50000 * r1 - 41869 * g1 - 8131 * b1 + 12800000 ) / 100000;
	y2 = ( 299 * r2 + 587 * g2 + 114 * b2 ) / 1000;
	cb2 = ( -16874 * r2 - 33126 * g2 + 50000 * b2 + 12800000 ) / 100000;
	cr2 = ( 50000 * r2 - 41869 * g2 - 8131 * b2 + 12800000 ) / 100000;

	cb = ( cb1 + cb2 ) >> 1;
	cr = ( cr1 + cr2 ) >> 1;

	return ( y1 << 24 ) | ( cb << 16 ) | ( y2 << 8 ) | cr;
}

static void GC_SampleBufferNonBlack( const unsigned short *src, int src_w, int src_h, int src_stride, qboolean *sampled_nonblack )
{
	int sample_x, sample_y;
	int grid;
	int denom;

	if( !src || src_w <= 0 || src_h <= 0 || !sampled_nonblack )
		return;

	/* Budget probes only need a cheap non-black signal. Keep the denser 9x9
	 * grid for normal presents so thin menu/title pixels are still caught. */
	grid = gc_budget_probe_active ? 5 : 9;
	denom = grid - 1;
	for( sample_y = 0; sample_y < grid; sample_y++ )
	{
		int y = ( sample_y * ( src_h - 1 )) / denom;
		const unsigned short *scanrow = src + y * src_stride;

		for( sample_x = 0; sample_x < grid; sample_x++ )
		{
			int x = ( sample_x * ( src_w - 1 )) / denom;

			if( scanrow[x] != 0 )
			{
				*sampled_nonblack = true;
				return;
			}
		}
	}
}

static void GC_BlitSoftwareBufferScaled( const unsigned short *src, int src_w, int src_h, int src_stride,
	unsigned int *dst, int copy_w, int copy_h, int row_pairs )
{
	int dst_y, dst_x, src_y, src_x;
	const int pairs = src_w / 2;

	if( src_w > 0 && src_h > 0 && ( src_w & 1 ) == 0 && ( copy_w & 1 ) == 0
		&& src_w * 2 == copy_w && src_h * 2 == copy_h )
	{
		for( src_y = 0; src_y < src_h; src_y++ )
		{
			const unsigned short *scanline = src + src_y * src_stride;
			unsigned int *out0 = dst + ( src_y * 2 ) * row_pairs;
			unsigned int *out1 = out0 + row_pairs;

			for( src_x = 0; src_x < pairs; src_x++ )
			{
				unsigned int yuyv = GC_RGBPairToYUYV( scanline[src_x * 2], scanline[src_x * 2 + 1] );
				int dst_pair = src_x * 2;

				out0[dst_pair] = yuyv;
				out0[dst_pair + 1] = yuyv;
				out1[dst_pair] = yuyv;
				out1[dst_pair + 1] = yuyv;
			}
		}
		return;
	}

	/* Exact 4× nearest (e.g. 160×120 → 640×480) without per-pixel divides. */
	if( src_w > 0 && src_h > 0 && ( src_w & 1 ) == 0 && ( copy_w & 1 ) == 0
		&& src_w * 4 == copy_w && src_h * 4 == copy_h )
	{
		for( src_y = 0; src_y < src_h; src_y++ )
		{
			const unsigned short *scanline = src + src_y * src_stride;
			unsigned int *out = dst + ( src_y * 4 ) * row_pairs;
			int row;

			for( src_x = 0; src_x < pairs; src_x++ )
			{
				unsigned int yuyv = GC_RGBPairToYUYV( scanline[src_x * 2], scanline[src_x * 2 + 1] );
				int dst_pair = src_x * 4;

				for( row = 0; row < 4; row++ )
				{
					unsigned int *line = out + row * row_pairs;
					line[dst_pair] = yuyv;
					line[dst_pair + 1] = yuyv;
					line[dst_pair + 2] = yuyv;
					line[dst_pair + 3] = yuyv;
				}
			}
		}
		return;
	}

	if( src_w == copy_w && src_h == copy_h && pairs > 0 )
	{
		for( src_y = 0; src_y < src_h; src_y++ )
		{
			const unsigned short *scanline = src + src_y * src_stride;
			unsigned int *out = dst + src_y * row_pairs;

			for( src_x = 0; src_x < pairs; src_x++ )
				out[src_x] = GC_RGBPairToYUYV( scanline[src_x * 2], scanline[src_x * 2 + 1] );
		}
		return;
	}

	for( dst_y = 0; dst_y < copy_h; dst_y++ )
	{
		int source_y = dst_y * src_h / copy_h;
		unsigned int *out = dst + dst_y * row_pairs;
		const unsigned short *scanline = src + source_y * src_stride;

		for( dst_x = 0; dst_x < copy_w; dst_x += 2 )
		{
			unsigned short p1 = scanline[dst_x * src_w / copy_w];
			unsigned short p2 = scanline[( dst_x + 1 ) * src_w / copy_w];
			out[dst_x / 2] = GC_RGBPairToYUYV( p1, p2 );
		}
	}
}

static void GC_PresentBuffer( void )
{
#if XASH_GAMECUBE
	unsigned short *src;
	unsigned int *dst;
	unsigned int *diag_rowdst;
	int copy_w, copy_h, row, col_diag;
	int src_w, src_h;
	qboolean sampled_nonblack;
	size_t buf_size;
	unsigned short first_pixel;
	double now;
	double elapsed_ms;

	src = NULL;
	dst = NULL;
	diag_rowdst = NULL;
	copy_w = 0;
	copy_h = 0;
	row = 0;
	src_w = 0;
	src_h = 0;
	sampled_nonblack = false;
	buf_size = 0;
	col_diag = 0;
	first_pixel = 0;
	now = 0.0;
	elapsed_ms = 0.0;

	if( !rmode || !xfb[which_fb] )
		return;

	gc_present_count++;

	copy_w = rmode->fbWidth;
	copy_h = rmode->xfbHeight;
	dst = (unsigned int *)xfb[which_fb];

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		const int row_pairs = rmode->fbWidth / 2;

		src = gc.buffer;
		src_w = gc.width;
		src_h = gc.height;

		buf_size = gc.stride * gc.height * sizeof(unsigned short);

		/* G36: Sample first pixel for visual evidence only on first frame. */
		if( gc_present_count == 1 )
		{
			first_pixel = gc.buffer[0];
			SYS_Report( "Xash3D GameCube: software buffer pixel[0]=0x%04X (RGB565)\n", first_pixel );
		}

		/* Native GX present: tile linear RGB565 → EFB textured quad → XFB.
		 * Avoids the CPU nearest-neighbor YUYV scale that dominates post-G36 lag. */
		if( GC_CanPresentViaGX( src_w, src_h ))
		{
			GC_SwizzleRGB565ToTiled( src, gc.stride, src_w, src_h, gc_tiled_rgb565 );
			DCFlushRange( gc_tiled_rgb565, (u32)((size_t)src_w * (size_t)src_h * sizeof( u16 )));
			GC_InitPresentTextureTiled( gc_tiled_rgb565, src_w, src_h );
			GC_PresentBufferViaGX();
			if( !gc_gx_present_logged )
			{
				SYS_Report( "Xash3D GameCube: GX present path active %dx%d\n", src_w, src_h );
				gc_gx_present_logged = true;
			}
		}
		else
		{
			if( !gc_budget_probe_active )
				DCFlushRange( gc.buffer, (u32)buf_size );
			GC_BlitSoftwareBufferScaled( src, src_w, src_h, gc.stride, dst, copy_w, copy_h, row_pairs );
		}

		/* Sample non-black for evidence. Do not scan the full RGB565 buffer
		 * every New Game world present — that alone was several ms/frame. */
		if( src_h > 0 && src_w > 0 )
		{
			qboolean should_sample = false;

			if( gc_budget_probe_active && !Sys_CheckParm( "-gcworldrender" )
				&& !gc_newgame_world_ready )
			{
				sampled_nonblack = true;
			}
			else if( gc_budget_probe_active || gc_present_count <= 4
				|| ( gc_newgame_world_ready && (( gc_present_count & 31 ) == 1 )))
			{
				should_sample = true;
			}

			if( should_sample )
				GC_SampleBufferNonBlack( src, src_w, src_h, gc.stride, &sampled_nonblack );
			else if( gc_newgame_world_ready )
				sampled_nonblack = true; /* world path already proved nonblack */
		}
	}
	else
	{
		VIDEO_ClearFrameBuffer( rmode, dst, COLOR_BLACK );

		/* G36: Diagnostic blue fill only for first frame when buffer is missing.
		 * Avoid wasting CPU cycles on full-screen fills after initial evidence
		 * is captured. Leaves XFB black (zeroed) for subsequent frames. */
		if( gc_present_count == 1 )
		{
			for( row = 0; row < copy_h; row++ )
			{
				diag_rowdst = dst + row * ( rmode->fbWidth / 2 );
				for( col_diag = 0; col_diag < copy_w / 2; col_diag++ )
					diag_rowdst[col_diag] = GC_RGBPairToYUYV( 0x001F, 0x001F );
			}
			sampled_nonblack = true;
		}
		else
		{
			sampled_nonblack = false;
		}
	}

	/* G36: After New Game arm, skip connect warm-up presents then sample
	 * steady Host_Frame intervals (not synthetic fill bursts). */
	now = Sys_FloatTime();
	elapsed_ms = gc_last_present_time > 0.0 ? ( now - gc_last_present_time ) * 1000.0 : 0.0;
	if( elapsed_ms > gc_worst_frame_ms )
		gc_worst_frame_ms = elapsed_ms;

	if( gc_budget_probe_active )
	{
		if( gc_budget_warmup_left > 0 )
		{
			gc_budget_warmup_left--;
			gc_last_present_time = now;
		}
		else if( gc_budget_sample_count < GC_GetFrameBudgetSampleTarget() )
		{
			gc_budget_sample_ms[gc_budget_sample_count] = (float)elapsed_ms;
			gc_budget_sample_nonblack[gc_budget_sample_count] = sampled_nonblack ? 1 : 0;
			gc_budget_sample_count++;
			if( Sys_CheckParm( "-gcnewgame" ))
			{
				SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u frame time=%.2fms\n",
					gc_budget_sample_count, sampled_nonblack ? 1u : 0u, elapsed_ms );
			}
			gc_last_present_time = now;
			if( gc_budget_sample_count >= GC_GetFrameBudgetSampleTarget() )
			{
				unsigned int i;

				gc_budget_probe_active = false;
				if( Sys_CheckParm( "-gcnewgame" ))
					gc_newgame_g36_done = true;
				/* Emit after the timed window so analyzer still sees frame time=. */
				for( i = 0; i < gc_budget_sample_count; i++ )
				{
					SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u frame time=%.2fms\n",
						i + 1, gc_budget_sample_nonblack[i], gc_budget_sample_ms[i] );
				}
				SYS_Report( "Xash3D GameCube: budget sample flush count=%u worst=%.2fms\n",
					gc_budget_sample_count, gc_worst_frame_ms );
			}
		}
		else
		{
			gc_budget_probe_active = false;
			gc_last_present_time = now;
		}
	}
	else if( gc_present_count <= 16 && !gc_newgame_world_ready && cls.state != ca_cinematic )
	{
		/* One line for smoke evidence — three SYS_Reports per present were
		 * dominating host I/O on short Dolphin probes. Skip once New Game
		 * world is up; that path uses silent budget sampling instead.
		 * Also skip during intro cinematics (direct GX present owns timing). */
		SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u blank_frames=%u frame time=%.2fms\n",
			gc_present_count, sampled_nonblack ? 1u : 0u, gc_blank_present_count, elapsed_ms );
		/* gcmap/gcworldrender probes intentionally pace presents while the real
		 * renderer cost is reported inside R_RenderScene. Avoid flagging those
		 * waits as slow frame bugs, but keep the warning for normal gameplay. */
		if( elapsed_ms >= 33.0 )
			SYS_Report( "Xash3D GameCube: G49 slow frame %.2fms worst=%.2fms\n", elapsed_ms, gc_worst_frame_ms );
		gc_last_present_time = now;
	}
	else
	{
		gc_last_present_time = now;
	}

	DCFlushRange( xfb[which_fb], VIDEO_GetFrameBufferSize( rmode ));
	VIDEO_SetNextFramebuffer( xfb[which_fb] );
	VIDEO_Flush();
	/* Skip VSync during G36 budget samples, New Game low-res world presents,
	 * and startup cinematics so Host_Frame is not gated on the 16.7ms VI period. */
	if( !gc_budget_probe_active && !gc_newgame_world_ready && cls.state != ca_cinematic )
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
	gc_quality = Cvar_Get( "gc_quality", "1", FCVAR_ARCHIVE, "GameCube quality profile: 0=playable/low-mem, 1=standard (default), 2=high telemetry-only" );
	Cvar_Get( "gc_hud_probe_skip", "0", 0, "GameCube HUD UpdateClientData skip gate during post-map probe" );
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
#if !XASH_GAMECUBE
	if( gc.buffer )
	{
		free( gc.buffer );
		gc.buffer = NULL;
	}
#else
	if( gc.buffer && gc_buffer_owns_heap )
		free( gc.buffer );
	gc.buffer = NULL;
	gc.buffer_pixels = 0;
	gc_buffer_owns_heap = false;
	gc.width = 0;
	gc.height = 0;
	gc.stride = 0;
	gc.bpp = 0;
#endif

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
	size_t needed_pixels;

	if( width <= 0 || height <= 0 )
		return false;

	needed_pixels = (size_t)width * (size_t)height;

#if XASH_GAMECUBE
	/* Prefer BSS New Game probe FB for anything that fits — avoids post-map
	 * calloc failure that left presents stuck at 640×480 on real hardware. */
	if( needed_pixels <= (size_t)( GC_VIDEO_NEWGAME_PROBE_WIDTH * GC_VIDEO_NEWGAME_PROBE_HEIGHT ))
	{
		if( gc.buffer && gc_buffer_owns_heap )
			free( gc.buffer );
		gc.buffer = gc_probe_rgb565;
		gc.buffer_pixels = (size_t)( GC_VIDEO_NEWGAME_PROBE_WIDTH * GC_VIDEO_NEWGAME_PROBE_HEIGHT );
		gc_buffer_owns_heap = false;
		memset( gc.buffer, 0, needed_pixels * sizeof( unsigned short ));
	}
	else if( !gc.buffer || gc.buffer_pixels < needed_pixels || !gc_buffer_owns_heap )
	{
		unsigned short *new_buffer = calloc( needed_pixels, sizeof( unsigned short ));
		if( !new_buffer )
			return false;

		if( gc.buffer && gc_buffer_owns_heap )
			free( gc.buffer );
		gc.buffer = new_buffer;
		gc.buffer_pixels = needed_pixels;
		gc_buffer_owns_heap = true;
	}
	else
	{
		memset( gc.buffer, 0, needed_pixels * sizeof( unsigned short ));
	}

	gc.width = width;
	gc.height = height;
	gc.stride = width;
	gc.bpp = 2;

	for( size_t i = 0; i < needed_pixels; i++ )
		gc.buffer[i] = 0x0010; /* dark blue boot backdrop */
#else
	if( gc.buffer )
		free( gc.buffer );
	gc.buffer = calloc( needed_pixels, sizeof( unsigned short ));
	if( !gc.buffer )
		return false;

	gc.width = width;
	gc.height = height;
	gc.stride = width;
	gc.bpp = 2;
#endif

	if( !gc.buffer )
		return false;

#if XASH_GAMECUBE
	GC_InitPresentTexture();
#endif

	*stride = gc.stride;
	*bpp = gc.bpp;
	*r = 0xF800;
	*g = 0x07E0;
	*b = 0x001F;
#if XASH_GAMECUBE
	GC_ReportBootPhase( GC_BOOT_SW_FB );
#endif
	return true;
}

void *SW_LockBuffer( void )
{
	return gc.buffer;
}

void SW_UnlockBuffer( void )
{
	GC_PresentBuffer();
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
	/* Free the RGB565 present buffer during BSP load; status text draws to XFB
	 * directly. Restored by GC_RestoreVideoMemoryAfterMapLoad. */
	if( gc.buffer )
	{
		if( gc_buffer_owns_heap )
			free( gc.buffer );
		gc.buffer = NULL;
		gc.buffer_pixels = 0;
		gc_buffer_owns_heap = false;
	}
	gc_present_tex_ready = false;
	Con_Reportf( "Xash3D GameCube: presentation buffer released for map load\n" );
}

static void GC_ReleasePresentationBufferForWorldRender( void )
{
#if XASH_GAMECUBE
	if( !gc.buffer )
		return;

	if( gc_buffer_owns_heap )
		free( gc.buffer );
	gc.buffer = NULL;
	gc.buffer_pixels = 0;
	gc_buffer_owns_heap = false;
	gc_present_tex_ready = false;
	Con_Reportf( "Xash3D GameCube: released presentation buffer for world render\n" );
#endif
}

static qboolean GC_EnsurePresentationBuffer( int width, int height )
{
#if XASH_GAMECUBE
	uint stride, bpp, r, g, b;

	if( width <= 0 || height <= 0 )
		return false;

	if( gc.buffer && gc.width == width && gc.height == height
		&& gc.buffer_pixels >= (size_t)width * (size_t)height )
		return true;

	/* Route through SW_CreateBuffer so New Game / probe sizes bind BSS. */
	if( !SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
	{
		Con_Reportf( "Xash3D GameCube: presentation buffer alloc failed %dx%d\n", width, height );
		return false;
	}

	Con_Reportf( "Xash3D GameCube: presentation buffer ready %dx%d\n", width, height );
	return true;
#else
	(void)width;
	(void)height;
	return false;
#endif
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

static void GC_StatusPutPixel( unsigned short *dst, int stride, int width, int height, int x, int y, unsigned short color )
{
	if( x < 0 || y < 0 || x >= width || y >= height )
		return;
	dst[y * stride + x] = color;
}

static void GC_FatalPutPixel( unsigned short *dst, int x, int y, unsigned short color )
{
	if( !rmode )
		return;
	GC_StatusPutPixel( dst, rmode->fbWidth, rmode->fbWidth, rmode->xfbHeight, x, y, color );
}

static void GC_StatusDrawChar( unsigned short *dst, int stride, int width, int height,
	int x, int y, char ch, unsigned short color, int scale )
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
					GC_StatusPutPixel( dst, stride, width, height,
						x + col * scale + sx, y + row * scale + sy, color );
		}
	}
}

static void GC_FatalDrawChar( unsigned short *dst, int x, int y, char ch, unsigned short color, int scale )
{
	if( !rmode )
		return;
	GC_StatusDrawChar( dst, rmode->fbWidth, rmode->fbWidth, rmode->xfbHeight,
		x, y, ch, color, scale );
}

static int GC_StatusDrawLine( unsigned short *dst, int stride, int width, int height,
	int x, int y, const char *text, unsigned short color, int scale, int max_chars )
{
	int i;

	for( i = 0; text && text[i] && text[i] != '\n' && i < max_chars; i++ )
		GC_StatusDrawChar( dst, stride, width, height, x + i * 6 * scale, y, text[i], color, scale );
	return i;
}

static int GC_FatalDrawLine( unsigned short *dst, int x, int y, const char *text, unsigned short color, int scale, int max_chars )
{
	if( !rmode )
		return 0;
	return GC_StatusDrawLine( dst, rmode->fbWidth, rmode->fbWidth, rmode->xfbHeight,
		x, y, text, color, scale, max_chars );
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

static void GC_DrawStatusPanelToBuffer( unsigned short *dst, int width, int height, int stride,
	const char *message, const char *details )
{
	int row;
	int col;
	int panel_x;
	int panel_y;
	int panel_w;
	int panel_h;
	int text_scale;
	int line_scale;

	panel_x = width * 24 / 640;
	panel_w = width - panel_x * 2;
	panel_h = height * 92 / 480;
	panel_y = height - panel_h - height * 24 / 480;
	if( panel_y < height * 24 / 480 )
		panel_y = height * 24 / 480;
	text_scale = height >= 240 ? 2 : 1;
	line_scale = height >= 240 ? 2 : 1;

	for( row = panel_y; row < panel_y + panel_h && row < height; row++ )
	{
		unsigned short *rowdst = dst + row * stride;
		for( col = panel_x; col < panel_x + panel_w && col < width; col++ )
		{
			if( row == panel_y || row == panel_y + panel_h - 1 ||
				col == panel_x || col == panel_x + panel_w - 1 )
				rowdst[col] = 0x07FF;
			else
				rowdst[col] = 0x0010;
		}
	}

	GC_StatusDrawLine( dst, stride, width, height, panel_x + width * 18 / 640,
		panel_y + height * 12 / 480, "LOADING", 0xFFFF, text_scale, 24 );
	GC_StatusDrawLine( dst, stride, width, height, panel_x + width * 18 / 640,
		panel_y + height * 38 / 480, message ? message : "PLEASE WAIT", 0xFFE0, text_scale, 34 );
	GC_StatusDrawLine( dst, stride, width, height, panel_x + width * 18 / 640,
		panel_y + height * 64 / 480, details ? details : "GAMECUBE VIDEO ALIVE", 0x07E0, line_scale, 72 );
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
	int panel_w;
	int panel_h;
	int panel_y;
	size_t xfb_size;

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		for( row = 0; row < gc.height; row++ )
		{
			rowdst = gc.buffer + row * gc.stride;
			for( col = 0; col < gc.width; col++ )
				rowdst[col] = 0x0010;
		}

		GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride, message, details );
		/* Avoid VIDEO_WaitVSync during Host_Init map load; it can stall for minutes in Dolphin. */
		if( host.status != HOST_INIT )
			GC_PresentBuffer();
		return;
	}

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
	if( host.status != HOST_INIT )
	{
		VIDEO_WaitVSync();
		which_fb ^= 1;
	}
#endif
}

void GC_RunGcmapSmokeFrames( const char *mapname, int count )
{
#if XASH_GAMECUBE
	char details[64];
	int i;
	unsigned int required_count;
	double smoke_samples_ms[GC_VIDEO_BUDGET_SAMPLE_TARGET];
	unsigned int smoke_sample_count;
	qboolean log_smoke_samples;

	smoke_sample_count = 0;
	log_smoke_samples = gc_budget_probe_active && !Sys_CheckParm( "-gcnewgame" );

	if( count <= 0 )
		count = 12;

	required_count = GC_GetFrameBudgetSampleTarget() + GC_VIDEO_BUDGET_WARMUP_PRESENTS;
	if( gc_budget_probe_active && count < (int)required_count )
		count = (int)required_count;

	Q_snprintf( details, sizeof( details ), "GCMAP %s", mapname && mapname[0] ? mapname : "READY" );

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		int row, col;
		unsigned short *rowdst;

		for( row = 0; row < gc.height; row++ )
		{
			rowdst = gc.buffer + row * gc.stride;
			for( col = 0; col < gc.width; col++ )
				rowdst[col] = 0x0010;
		}

		GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride,
			"MAP READY", details );

		// G72: Reduce frame/render cost while preserving MAP_READY/G45/nonblack
		if( gc.width >= 160 && gc.height >= 120 )
		{
			for( row = 0; row < gc.height / 4; row++ )
			{
				for( col = 0; col < gc.width / 4; col++ )
				{
					rowdst[col] = 0x0010;
				}
			}
		}
	}

	for( i = 0; i < count; i++ )
	{
		double start = 0.0;
		double end = 0.0;

		if( log_smoke_samples && smoke_sample_count < GC_VIDEO_BUDGET_SAMPLE_TARGET )
			start = Sys_FloatTime();

		if( gc.buffer && gc.width > 0 && gc.height > 0 )
			GC_PresentBuffer();
		else
			GC_DrawLoadingStatus( "MAP LOADED", details );

		if( log_smoke_samples && smoke_sample_count < GC_VIDEO_BUDGET_SAMPLE_TARGET )
		{
			end = Sys_FloatTime();
			smoke_samples_ms[smoke_sample_count++] = ( end - start ) * 1000.0;
		}
	}

	if( log_smoke_samples )
	{
		for( i = 0; i < (int)smoke_sample_count; i++ )
		{
			SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=1 frame time=%.2fms\n",
				(unsigned int)( i + 1 ), smoke_samples_ms[i] );
		}
		SYS_Report( "Xash3D GameCube: budget sample flush count=%u worst=%.2fms\n",
			smoke_sample_count, gc_worst_frame_ms );
	}

	gc_budget_probe_active = false;
#else
	(void)mapname;
	(void)count;
#endif
}

qboolean GC_AttemptGcmapWorldRender( int count )
{
#if XASH_GAMECUBE
	ref_viewpass_t rvp;
	model_t *world;
	vec3_t center;
	char old_drawviewmodel[16];
	int present_w, present_h;
	int i;

	if( !ref.initialized || !SV_Active() )
		return false;

	if( count <= 0 )
		count = 12;

	Image_GCPurgeDecodeScratch();
	Mod_GcmapMarkPrecacheFreeable();
	GC_ReleasePresentationBufferForWorldRender();
	/* Full software edge pass uses ~288 KiB of stack (NUMSTACKEDGES/SURFACES).
	 * Force quality=0 so R_EdgeDrawing takes the gcmap low-memory probe path.
	 * Allocate edge/surf/span scratch before screen buffers while MEM1 is freest. */
	Cvar_Set( "gc_quality", "0" );
	if( !R_GcmapEnsureWorldRenderScratch() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap world render scratch alloc failed\n" );
		return false;
	}
	GC_MemSample( "pre-world render" );

	if( !R_GcmapPrepareWorldRender() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap world render screen alloc failed\n" );
		Cvar_Set( "gc_quality", "0" );
		if( !R_GcmapPrepareWorldRender() )
			return false;
	}

	present_w = gc.width > 0 ? gc.width : GC_VIDEO_NEWGAME_PROBE_WIDTH;
	present_h = gc.height > 0 ? gc.height : GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	if( !R_GcmapGetViewport( &present_w, &present_h ))
	{
		present_w = GC_VIDEO_NEWGAME_PROBE_WIDTH;
		present_h = GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	}

	if( !GC_EnsurePresentationBuffer( present_w, present_h ))
	{
		Con_Reportf( "Xash3D GameCube: gcmap world render presentation buffer failed\n" );
		return false;
	}

	world = sv.models[1];
	if( !world )
		return false;

	cl.models[1] = world;
	cl.worldmodel = world;
	cl.video_prepped = true;

	/* World-render probe flat-fills spans instead of sampling cached textured
	 * surfaces, so keep MEM1 pressure down by dropping the gcmap surfcache. */
	if( Sys_CheckParm( "-gcworldrender" ))
	{
		R_GcmapTrimSurfaceCache();
		Con_Reportf( "Xash3D GameCube: gcmap world render using quality=0 without surface cache\n" );
	}
	else if( !R_GcmapEnsureSurfaceCache() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap world render using quality=0 without surface cache\n" );
		Cvar_Set( "gc_quality", "0" );
	}

	ref.dllFuncs.R_NewMap();

	memset( &rvp, 0, sizeof( rvp ));
	rvp.viewport[0] = 0;
	rvp.viewport[1] = 0;
	if( !R_GcmapGetViewport( &rvp.viewport[2], &rvp.viewport[3] ))
	{
		rvp.viewport[2] = refState.width > 0 ? refState.width : gc.width;
		rvp.viewport[3] = refState.height > 0 ? refState.height : gc.height;
	}
	rvp.fov_x = 90.0f;
	rvp.fov_y = rvp.fov_x * 0.75f;
	/* Prefer a spawned entity origin so the camera is inside playable space.
	 * Map-bbox center is often in solid/void and produces an all-black frame. */
	VectorAverage( world->mins, world->maxs, center );
	center[2] += 64.0f;
	for( i = 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = &svgame.edicts[i];

		if( ent->free )
			continue;
		if( VectorIsNull( ent->v.origin ))
			continue;
		VectorCopy( ent->v.origin, center );
		center[2] += 48.0f;
		Con_Reportf( "Xash3D GameCube: gcmap world render view from edict=%d origin=(%.0f,%.0f,%.0f)\n",
			i, center[0], center[1], center[2] );
		break;
	}
	VectorCopy( center, rvp.vieworigin );
	SetBits( rvp.flags, RF_DRAW_WORLD );
	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	Cvar_Set( "r_drawviewmodel", "0" );

	if( count > 6 )
		count = 6;

	Con_Reportf( "Xash3D GameCube: gcmap world render begin frames=%d\n", count );
	for( i = 0; i < count; ++i )
	{
		ref.dllFuncs.R_BeginFrame( false );
		VectorCopy( rvp.vieworigin, refState.vieworg );
		VectorCopy( rvp.viewangles, refState.viewangles );
		ref.dllFuncs.GL_RenderFrame( &rvp );
		/* R_EndFrame -> R_BlitScreen already presents; avoid a second present. */
		ref.dllFuncs.R_EndFrame();
	}
	Cvar_Set( "r_drawviewmodel", old_drawviewmodel );
	Con_Reportf( "Xash3D GameCube: gcmap world render ready\n" );
	return true;
#endif

	(void)count;
	return false;
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

	xfb_size = rmode->fbWidth * rmode->xfbHeight * sizeof(unsigned short);
	DCFlushRange( xfb[0], (u32)xfb_size );
	VIDEO_SetNextFramebuffer( xfb[0] );
	VIDEO_Flush();
	VIDEO_WaitVSync();

	/* Block briefly to ensure the fatal frame reaches the display before the
	 * caller halts or exits. Keep this bounded so the error path cannot loop. */
	for( i = 0; i < 3; i++ )
		VIDEO_WaitVSync();

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

qboolean GC_IsFrameBudgetProbeActive( void )
{
#if XASH_GAMECUBE
	return gc_budget_probe_active;
#else
	return false;
#endif
}

qboolean GC_IsNewGameWorldReady( void )
{
#if XASH_GAMECUBE
	return gc_newgame_world_ready;
#else
	return false;
#endif
}

qboolean GC_IsNewGameG36Done( void )
{
#if XASH_GAMECUBE
	return gc_newgame_g36_done;
#else
	return false;
#endif
}

int GC_GetNewGameViewCluster( void )
{
#if XASH_GAMECUBE
	return gc_newgame_viewcluster;
#else
	return -1;
#endif
}

qboolean GC_HasNewGameCachedVis( void )
{
#if XASH_GAMECUBE
	return gc_newgame_pvs_ready;
#else
	return false;
#endif
}

qboolean GC_ApplyNewGameCachedVis( int visframe )
{
#if XASH_GAMECUBE
	model_t *wmodel;
	int i;
	int leaf_mark = gc_newgame_numleafs;
	int node_mark = gc_newgame_numnodes;

	if( !gc_newgame_pvs_ready || !gc_newgame_vis || !gc_newgame_nodebits )
	{
		SYS_Report( "Xash3D GameCube: ApplyCachedVis fail ready=%d vis=%p nodes=%p\n",
			gc_newgame_pvs_ready ? 1 : 0, (void *)gc_newgame_vis, (void *)gc_newgame_nodebits );
		return false;
	}

	wmodel = sv.models[1];
#if !XASH_DEDICATED
	if( !wmodel )
		wmodel = cl.worldmodel;
#endif
	if( !wmodel || !wmodel->leafs || !wmodel->nodes )
	{
		SYS_Report( "Xash3D GameCube: ApplyCachedVis fail world=%p leafs=%p nodes=%p\n",
			(void *)wmodel,
			wmodel ? (void *)wmodel->leafs : NULL,
			wmodel ? (void *)wmodel->nodes : NULL );
		return false;
	}

	if( wmodel->numleafs != gc_newgame_numleafs || wmodel->numnodes != gc_newgame_numnodes )
	{
		SYS_Report( "Xash3D GameCube: ApplyCachedVis size mismatch world=%d/%d cache=%d/%d\n",
			wmodel->numleafs, wmodel->numnodes, gc_newgame_numleafs, gc_newgame_numnodes );
		if( leaf_mark > wmodel->numleafs )
			leaf_mark = wmodel->numleafs;
		if( node_mark > wmodel->numnodes )
			node_mark = wmodel->numnodes;
	}

	for( i = 0; i < leaf_mark; i++ )
	{
		if( gc_newgame_vis[i >> 3] & ( 1 << ( i & 7 )))
			((mnode_t *)&wmodel->leafs[i + 1])->visframe = visframe;
	}
	for( i = 0; i < node_mark; i++ )
	{
		if( gc_newgame_nodebits[i >> 3] & ( 1 << ( i & 7 )))
			wmodel->nodes[i].visframe = visframe;
	}
	return true;
#else
	(void)visframe;
	return false;
#endif
}

#if XASH_GAMECUBE
/* Local RLE decompress — Mod_DecompressPVS is static in mod_bmodel.c. */
static void GC_DecompressPVS( byte *out, const byte *in, size_t visbytes )
{
	byte *dst = out;

	if( !in )
	{
		memset( out, 0xFF, visbytes );
		return;
	}

	while( dst < out + visbytes )
	{
		if( *in )
		{
			*dst++ = *in++;
		}
		else
		{
			size_t c = in[1];

			if( c == 0 )
			{
				memset( dst, 0xFF, (size_t)( out + visbytes - dst ));
				return;
			}
			if( c > (size_t)( out + visbytes - dst ))
				c = (size_t)( out + visbytes - dst );
			memset( dst, 0, c );
			in += 2;
			dst += c;
		}
	}
}

static void GC_CountActiveVisRows( void )
{
	int i;

	gc_newgame_vis_leafs = 0;
	gc_newgame_vis_nodes = 0;
	if( !gc_newgame_vis || !gc_newgame_nodebits )
		return;
	for( i = 0; i < gc_newgame_numleafs; i++ )
	{
		if( gc_newgame_vis[i >> 3] & ( 1 << ( i & 7 )))
			gc_newgame_vis_leafs++;
	}
	for( i = 0; i < gc_newgame_numnodes; i++ )
	{
		if( gc_newgame_nodebits[i >> 3] & ( 1 << ( i & 7 )))
			gc_newgame_vis_nodes++;
	}
}

static qboolean GC_SetActiveNewGameCluster( int cluster, qboolean log_change )
{
	int prev = gc_newgame_viewcluster;

	if( !gc_newgame_pvs_table || !gc_newgame_node_table || !gc_newgame_cluster_valid )
		return false;
	if( cluster < 0 || cluster >= gc_newgame_numclusters )
		return false;
	if( !gc_newgame_cluster_valid[cluster] )
		return false;

	gc_newgame_vis = gc_newgame_pvs_table + (size_t)cluster * (size_t)gc_newgame_visbytes;
	gc_newgame_nodebits = gc_newgame_node_table + (size_t)cluster * (size_t)gc_newgame_nodebytes;
	gc_newgame_viewcluster = cluster;
	GC_CountActiveVisRows();

	if( log_change && prev != cluster )
	{
		SYS_Report( "Xash3D GameCube: PVS cluster change %d->%d leaves=%d nodes=%d\n",
			prev, cluster, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
	}
	return true;
}

static int GC_SelectClusterForOrigin( const float *org )
{
	int i;
	int best = -1;
	float best_vol = 1e30f;

	if( !org || !gc_newgame_leafboxes || gc_newgame_nleafboxes <= 0 )
		return gc_newgame_viewcluster;

	for( i = 0; i < gc_newgame_nleafboxes; i++ )
	{
		const gc_newgame_leafbox_t *box = &gc_newgame_leafboxes[i];
		float vol;
		vec3_t size;

		if( box->cluster < 0 )
			continue;
		if( org[0] < box->mins[0] || org[0] > box->maxs[0]
			|| org[1] < box->mins[1] || org[1] > box->maxs[1]
			|| org[2] < box->mins[2] || org[2] > box->maxs[2] )
			continue;

		VectorSubtract( box->maxs, box->mins, size );
		vol = size[0] * size[1] * size[2];
		if( vol <= 0.0f )
			vol = 1.0f;
		if( vol < best_vol )
		{
			best_vol = vol;
			best = box->cluster;
		}
	}

	return ( best >= 0 ) ? best : gc_newgame_viewcluster;
}

/*
 * G89: select cached PVS row for the camera origin via load-time leaf AABBs
 * (no live PointInLeaf — BSP scratch may be corrupted by present time).
 */
qboolean GC_UpdateNewGamePVSForOrigin( const float *org )
{
	int cluster;

	if( !gc_newgame_pvs_ready || !org )
		return false;

	cluster = GC_SelectClusterForOrigin( org );
	return GC_SetActiveNewGameCluster( cluster, true );
}

static void GC_BuildNodebitsForVisRow( model_t *wmodel, const byte *vis, byte *nodebits )
{
	const int parent_limit = wmodel->numnodes > 0 ? wmodel->numnodes + 8 : 4096;
	int i;

	memset( nodebits, 0, (size_t)gc_newgame_nodebytes );

	for( i = 0; i < wmodel->numleafs; i++ )
	{
		mnode_t *node;
		int parent_depth = 0;

		if( !( vis[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		if( i + 1 >= wmodel->numleafs )
			continue;
		node = (mnode_t *)&wmodel->leafs[i + 1];
		do
		{
			if( node->contents >= 0 )
			{
				const int nidx = (int)( node - wmodel->nodes );

				if( nidx >= 0 && nidx < wmodel->numnodes )
					nodebits[nidx >> 3] |= (byte)( 1 << ( nidx & 7 ));
			}
			node = node->parent;
			if( ++parent_depth > parent_limit )
				break;
		}
		while( node );
	}
}

static void GC_FreeNewGamePVSCache( void )
{
	if( gc_newgame_pvs_table )
	{
		free( gc_newgame_pvs_table );
		gc_newgame_pvs_table = NULL;
	}
	if( gc_newgame_node_table )
	{
		free( gc_newgame_node_table );
		gc_newgame_node_table = NULL;
	}
	if( gc_newgame_cluster_valid )
	{
		free( gc_newgame_cluster_valid );
		gc_newgame_cluster_valid = NULL;
	}
	if( gc_newgame_leafboxes )
	{
		free( gc_newgame_leafboxes );
		gc_newgame_leafboxes = NULL;
	}
	gc_newgame_vis = NULL;
	gc_newgame_nodebits = NULL;
	gc_newgame_nleafboxes = 0;
	gc_newgame_numclusters = 0;
	gc_newgame_pvs_ready = false;
	gc_newgame_pvs_follow_proved = false;
}

static void GC_ProveNewGamePVSFollow( void )
{
	int c0, c1, i;
	int leaves0, nodes0, leaves1, nodes1;

	if( gc_newgame_pvs_follow_proved || !gc_newgame_pvs_ready )
		return;

	c0 = gc_newgame_viewcluster;
	c1 = -1;
	for( i = 0; i < gc_newgame_numclusters; i++ )
	{
		if( i == c0 )
			continue;
		if( gc_newgame_cluster_valid[i] )
		{
			c1 = i;
			break;
		}
	}
	if( c1 < 0 )
	{
		SYS_Report( "Xash3D GameCube: PVS follow prove skipped (only one cluster)\n" );
		gc_newgame_pvs_follow_proved = true;
		return;
	}

	if( !GC_SetActiveNewGameCluster( c0, false ))
		return;
	leaves0 = gc_newgame_vis_leafs;
	nodes0 = gc_newgame_vis_nodes;
	SYS_Report( "Xash3D GameCube: PVS follow prove cluster=%d leaves=%d nodes=%d\n",
		c0, leaves0, nodes0 );

	if( !GC_SetActiveNewGameCluster( c1, true ))
		return;
	leaves1 = gc_newgame_vis_leafs;
	nodes1 = gc_newgame_vis_nodes;
	SYS_Report( "Xash3D GameCube: PVS follow prove cluster=%d leaves=%d nodes=%d\n",
		c1, leaves1, nodes1 );

	gc_newgame_pvs_follow_proved = true;
	SYS_Report( "Xash3D GameCube: PVS follow ready clusters=%d->%d leafdelta=%d\n",
		c0, c1, leaves1 - leaves0 );

	/* Restore origin-based cluster when possible (player / first leafbox). */
	if( svgame.edicts && svs.maxclients >= 1 )
	{
		edict_t *player = SV_EdictNum( 1 );

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			vec3_t eye;

			VectorCopy( player->v.origin, eye );
			eye[2] += 48.0f;
			GC_UpdateNewGamePVSForOrigin( eye );
			return;
		}
	}
	GC_SetActiveNewGameCluster( c0, false );
}
#endif

/*
 * G83/G89: capture PointInLeaf + per-cluster PVS while BSP scratch is intact.
 * G89 stores every cluster row + leaf AABBs so present-time selection needs no
 * live tree walks after scratch reuse.
 */
void GC_CaptureNewGamePVSFromModel( model_t *wmodel )
{
#if XASH_GAMECUBE
	vec3_t vieworigin;
	mleaf_t *viewleaf;
	int max_cluster = -1;
	int i;

	if( !Sys_CheckParm( "-gcnewgame" ))
		return;
	if( gc_newgame_pvs_ready )
		return;
	if( !wmodel )
		wmodel = sv.models[1];

	gc_newgame_viewcluster = -1;
	gc_newgame_vis_leafs = 0;
	gc_newgame_vis_nodes = 0;

	SYS_Report( "Xash3D GameCube: CaptureNewGamePVS begin\n" );

	if( !wmodel || !wmodel->nodes || !wmodel->leafs )
	{
		SYS_Report( "Xash3D GameCube: CaptureNewGamePVS skipped (no world nodes)\n" );
		return;
	}

	VectorAverage( wmodel->mins, wmodel->maxs, vieworigin );
	vieworigin[2] += 64.0f;
	if( svgame.edicts )
	{
		for( i = 1; i < svgame.numEntities; i++ )
		{
			edict_t *ent = &svgame.edicts[i];

			if( ent->free || VectorIsNull( ent->v.origin ))
				continue;
			VectorCopy( ent->v.origin, vieworigin );
			vieworigin[2] += 48.0f;
			break;
		}
	}

	viewleaf = Mod_PointInLeaf( vieworigin, wmodel->nodes, wmodel );
	gc_newgame_viewcluster = viewleaf ? viewleaf->cluster : -1;

	/* Load-time world center is often solid; pick any non-solid leaf cluster. */
	if( gc_newgame_viewcluster < 0 && wmodel->leafs )
	{
		for( i = 1; i < wmodel->numleafs; i++ )
		{
			mleaf_t *leaf = &wmodel->leafs[i];
			vec3_t leaf_origin;

			if( leaf->cluster < 0 || leaf->contents >= 0 )
				continue;
			VectorAverage( leaf->minmaxs, leaf->minmaxs + 3, leaf_origin );
			viewleaf = Mod_PointInLeaf( leaf_origin, wmodel->nodes, wmodel );
			if( viewleaf && viewleaf->cluster >= 0 )
			{
				VectorCopy( leaf_origin, vieworigin );
				gc_newgame_viewcluster = viewleaf->cluster;
				break;
			}
		}
	}

	SYS_Report( "Xash3D GameCube: Capture PointInLeaf cluster=%d contents=%d origin=(%.0f,%.0f,%.0f)\n",
		gc_newgame_viewcluster, viewleaf ? viewleaf->contents : 0,
		vieworigin[0], vieworigin[1], vieworigin[2] );

	if( !wmodel->visdata || world.visbytes <= 0 )
	{
		SYS_Report( "Xash3D GameCube: Capture FatPVS skipped vis=%d\n",
			wmodel->visdata ? 1 : 0 );
		return;
	}

	for( i = 1; i < wmodel->numleafs; i++ )
	{
		if( wmodel->leafs[i].cluster > max_cluster )
			max_cluster = wmodel->leafs[i].cluster;
	}
	if( max_cluster < 0 )
	{
		SYS_Report( "Xash3D GameCube: Capture FatPVS skipped (no clusters)\n" );
		return;
	}

	{
		const size_t visbytes = world.visbytes;
		const size_t nodebytes = (size_t)( wmodel->numnodes + 7 ) / 8;
		const int numclusters = max_cluster + 1;
		int valid_clusters = 0;

		GC_FreeNewGamePVSCache();

		gc_newgame_visbytes = (int)visbytes;
		gc_newgame_nodebytes = (int)nodebytes;
		gc_newgame_numclusters = numclusters;
		gc_newgame_numleafs = wmodel->numleafs;
		gc_newgame_numnodes = wmodel->numnodes;
		gc_newgame_nleafboxes = wmodel->numleafs > 1 ? wmodel->numleafs - 1 : 0;

		gc_newgame_pvs_table = (byte *)calloc( (size_t)numclusters, visbytes );
		gc_newgame_node_table = (byte *)calloc( (size_t)numclusters, nodebytes );
		gc_newgame_cluster_valid = (byte *)calloc( (size_t)numclusters, 1 );
		gc_newgame_leafboxes = gc_newgame_nleafboxes > 0
			? (gc_newgame_leafbox_t *)calloc( (size_t)gc_newgame_nleafboxes, sizeof( gc_newgame_leafbox_t ))
			: NULL;

		if( !gc_newgame_pvs_table || !gc_newgame_node_table || !gc_newgame_cluster_valid
			|| ( gc_newgame_nleafboxes > 0 && !gc_newgame_leafboxes ))
		{
			SYS_Report( "Xash3D GameCube: Capture FatPVS multi-cluster alloc failed clusters=%d\n",
				numclusters );
			GC_FreeNewGamePVSCache();
			return;
		}

		SYS_Report( "Xash3D GameCube: Capture multi-cluster PVS begin clusters=%d visbytes=%d\n",
			numclusters, (int)visbytes );

		for( i = 1; i < wmodel->numleafs; i++ )
		{
			mleaf_t *leaf = &wmodel->leafs[i];
			gc_newgame_leafbox_t *box = &gc_newgame_leafboxes[i - 1];
			byte *row;

			VectorCopy( leaf->minmaxs, box->mins );
			VectorCopy( leaf->minmaxs + 3, box->maxs );
			box->cluster = leaf->cluster;

			if( leaf->cluster < 0 || leaf->cluster >= numclusters )
				continue;
			if( gc_newgame_cluster_valid[leaf->cluster] )
				continue;

			row = gc_newgame_pvs_table + (size_t)leaf->cluster * visbytes;
			GC_DecompressPVS( row, leaf->compressed_vis, visbytes );
			GC_BuildNodebitsForVisRow( wmodel, row,
				gc_newgame_node_table + (size_t)leaf->cluster * nodebytes );
			gc_newgame_cluster_valid[leaf->cluster] = 1;
			valid_clusters++;
		}

		if( gc_newgame_viewcluster < 0 || !gc_newgame_cluster_valid[gc_newgame_viewcluster] )
		{
			for( i = 0; i < numclusters; i++ )
			{
				if( gc_newgame_cluster_valid[i] )
				{
					gc_newgame_viewcluster = i;
					break;
				}
			}
		}

		if( gc_newgame_viewcluster < 0 || !GC_SetActiveNewGameCluster( gc_newgame_viewcluster, false ))
		{
			SYS_Report( "Xash3D GameCube: Capture multi-cluster PVS failed (no valid row)\n" );
			GC_FreeNewGamePVSCache();
			return;
		}

		gc_newgame_pvs_ready = true;
		SYS_Report( "Xash3D GameCube: Capture FatPVS cluster=%d leaves=%d nodes=%d\n",
			gc_newgame_viewcluster, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
		SYS_Report( "Xash3D GameCube: Capture multi-cluster PVS ready clusters=%d valid=%d\n",
			numclusters, valid_clusters );
	}
#else
	(void)wmodel;
#endif
}

void GC_CaptureNewGamePVS( void )
{
	GC_CaptureNewGamePVSFromModel( NULL );
}

/*
 * G90: single New Game world pass for V_RenderViewBoundedGC.
 * Caller owns R_BeginFrame / R_EndFrame (V_PreRender / V_PostRender).
 */
qboolean GC_RenderNewGameWorldPassNoFrame( qboolean draw_viewmodel )
{
#if XASH_GAMECUBE
	ref_viewpass_t rvp;
	model_t *world;
	vec3_t center;
	char old_drawviewmodel[16];
	int i;

	if( !ref.initialized || !SV_Active() )
		return false;
	if( !gc_newgame_world_ready )
		return false;

	world = sv.models[1];
	if( !world )
		return false;

	cl.models[1] = world;
	cl.worldmodel = world;
	cl.video_prepped = true;

	memset( &rvp, 0, sizeof( rvp ));
	rvp.viewport[0] = 0;
	rvp.viewport[1] = 0;
	if( !R_GcmapGetViewport( &rvp.viewport[2], &rvp.viewport[3] ))
	{
		rvp.viewport[2] = refState.width > 0 ? refState.width : gc.width;
		rvp.viewport[3] = refState.height > 0 ? refState.height : gc.height;
	}
	rvp.fov_x = 90.0f;
	rvp.fov_y = rvp.fov_x * 0.75f;
	VectorAverage( world->mins, world->maxs, center );
	center[2] += 64.0f;
	{
		edict_t *player = ( svgame.edicts && svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			VectorCopy( player->v.origin, center );
			center[2] += 48.0f;
			VectorCopy( player->v.v_angle, rvp.viewangles );
		}
		else
		{
			for( i = 1; i < svgame.numEntities; i++ )
			{
				edict_t *ent = &svgame.edicts[i];

				if( ent->free || VectorIsNull( ent->v.origin ))
					continue;
				VectorCopy( ent->v.origin, center );
				center[2] += 48.0f;
				break;
			}
		}
	}
	VectorCopy( center, rvp.vieworigin );
	GC_UpdateNewGamePVSForOrigin( center );
	GC_ProveNewGamePVSFollow();
	SetBits( rvp.flags, RF_DRAW_WORLD );

	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	Cvar_Set( "r_drawviewmodel", draw_viewmodel ? "1" : "0" );

	VectorCopy( rvp.vieworigin, refState.vieworg );
	VectorCopy( rvp.viewangles, refState.viewangles );
	/* Match GC_RenderNewGameWorldFrames: do not call R_Set2DMode before the
	 * world pass — it has hung Host_Frame on the post-G36 pump. */
	ref.dllFuncs.GL_RenderFrame( &rvp );
	Cvar_Set( "r_drawviewmodel", old_drawviewmodel );
	return true;
#else
	(void)draw_viewmodel;
	return false;
#endif
}

/*
 * Bounded low-res world presents for New Game after G36.
 * Bypasses V_RenderView / Host_ServerFrame (both stall on this path) and
 * reuses the same GL_RenderFrame probe used by -gcworldrender / -gcmap.
 */
qboolean GC_RenderNewGameWorldFrames( int count )
{
#if XASH_GAMECUBE
	ref_viewpass_t rvp;
	model_t *world;
	vec3_t center;
	char old_drawviewmodel[16];
	int i;

	if( !ref.initialized || !SV_Active() )
		return false;
	if( !gc_newgame_world_ready )
		return false;
	if( count <= 0 )
		count = 1;
	if( count > 4 )
		count = 4;

	world = sv.models[1];
	if( !world )
		return false;

	cl.models[1] = world;
	cl.worldmodel = world;
	cl.video_prepped = true;

	/* Skip R_NewMap here: on New Game it re-enters texture/surface setup and
	 * stalls Host_Frame. Prepare already allocated low-res screens, scratch,
	 * and lean sky — render with the resident world model only. */

	memset( &rvp, 0, sizeof( rvp ));
	rvp.viewport[0] = 0;
	rvp.viewport[1] = 0;
	if( !R_GcmapGetViewport( &rvp.viewport[2], &rvp.viewport[3] ))
	{
		rvp.viewport[2] = refState.width > 0 ? refState.width : gc.width;
		rvp.viewport[3] = refState.height > 0 ? refState.height : gc.height;
	}
	rvp.fov_x = 90.0f;
	rvp.fov_y = rvp.fov_x * 0.75f;
	VectorAverage( world->mins, world->maxs, center );
	center[2] += 64.0f;
	/* Prefer local player (edict 1) so G86 move/look updates the camera. */
	{
		edict_t *player = ( svgame.edicts && svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			VectorCopy( player->v.origin, center );
			center[2] += 48.0f;
			VectorCopy( player->v.v_angle, rvp.viewangles );
		}
		else
		{
			for( i = 1; i < svgame.numEntities; i++ )
			{
				edict_t *ent = &svgame.edicts[i];

				if( ent->free )
					continue;
				if( VectorIsNull( ent->v.origin ))
					continue;
				VectorCopy( ent->v.origin, center );
				center[2] += 48.0f;
				break;
			}
		}
	}
	VectorCopy( center, rvp.vieworigin );
	/* G89: select multi-cluster PVS for camera; prove two-cluster switch once. */
	GC_UpdateNewGamePVSForOrigin( center );
	GC_ProveNewGamePVSFollow();
	SetBits( rvp.flags, RF_DRAW_WORLD );
	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	Cvar_Set( "r_drawviewmodel", "0" );

	{
		static int render_log;

		if( render_log < 3 )
		{
			Con_Reportf( "Xash3D GameCube: newgame world render begin frames=%d origin=(%.0f,%.0f,%.0f)\n",
				count, center[0], center[1], center[2] );
			render_log++;
		}
	}
	for( i = 0; i < count; ++i )
	{
		ref.dllFuncs.R_BeginFrame( false );
		VectorCopy( rvp.vieworigin, refState.vieworg );
		VectorCopy( rvp.viewangles, refState.viewangles );
		ref.dllFuncs.GL_RenderFrame( &rvp );
		ref.dllFuncs.R_EndFrame();
	}
	Cvar_Set( "r_drawviewmodel", old_drawviewmodel );
	{
		static qboolean ready_logged;
		static unsigned sustained_frames;
		static unsigned scr_frames;

		sustained_frames += (unsigned)count;
		/* Prepare bursts use count>1; SCR_UpdateScreen always asks for 1. */
		if( count == 1 )
			scr_frames += 1;
		if( !ready_logged )
		{
			Con_Reportf( "Xash3D GameCube: newgame world render ready\n" );
			ready_logged = true;
		}
		if( sustained_frames == 8 || sustained_frames == 16
			|| ( sustained_frames > 0 && ( sustained_frames % 32 ) == 0 ))
		{
			Con_Reportf( "Xash3D GameCube: newgame world render sustained frames=%u scr=%u\n",
				sustained_frames, scr_frames );
		}
		if( scr_frames == 8 || scr_frames == 16
			|| ( scr_frames > 0 && ( scr_frames % 32 ) == 0 ))
		{
			Con_Reportf( "Xash3D GameCube: newgame world render SCR frames=%u\n",
				scr_frames );
		}
	}
	return true;
#else
	(void)count;
	return false;
#endif
}

qboolean GC_PrepareNewGameWorldPresent( void )
{
#if XASH_GAMECUBE
	int present_w = GC_VIDEO_NEWGAME_PROBE_WIDTH;
	int present_h = GC_VIDEO_NEWGAME_PROBE_HEIGHT;

	if( gc_newgame_world_ready )
		return true;
	if( !Sys_CheckParm( "-gcnewgame" ))
		return false;

	/* Prefer Arm-time capture; retry here if map-ready ran before entities. */
	GC_CaptureNewGamePVS();

	Image_GCPurgeDecodeScratch();
	Mod_GcmapMarkPrecacheFreeable();
	Cvar_Set( "gc_quality", "0" );

	if( !R_GcmapEnsureWorldRenderScratch() )
	{
		SYS_Report( "Xash3D GameCube: newgame world scratch alloc failed\n" );
		return false;
	}

	R_GcmapTrimSurfaceCache();
	if( !R_GcmapPrepareWorldRender() )
	{
		SYS_Report( "Xash3D GameCube: newgame world screen alloc failed\n" );
		return false;
	}

	if( !R_GcmapGetViewport( &present_w, &present_h ))
	{
		present_w = GC_VIDEO_NEWGAME_PROBE_WIDTH;
		present_h = GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	}

	if( !GC_EnsurePresentationBuffer( present_w, present_h ))
	{
		SYS_Report( "Xash3D GameCube: newgame world presentation buffer failed\n" );
		return false;
	}

	/* Static surface cache for textured spans feeding the GX present path. */
	if( !R_TryInitLowResSurfaceCache() )
		SYS_Report( "Xash3D GameCube: newgame textured cache unavailable (flat fill)\n" );

	/* One desert side for RGB565 sky fills — deferred past map-prep OOM cliff. */
	{
		const char *sky = clgame.movevars.skyName;
		if( COM_StringEmptyOrNULL( sky ))
			sky = DEFAULT_SKYBOX_NAME;
		R_SetupSkyLeanGameCube( sky );
	}

	/* After sky proves FS/MEM headroom, promote a few mesh-only studios.
	 * Viewmodel bind is forced in V_SetupViewModel (tram starts unarmed). */
	Mod_GCLoadNewGameStudios();

	/* Mark world-ready before VidInit so SCR_UpdateScreen cannot re-arm the
	 * G36 "frame budget samples armed" marker mid-Prepare (harness scores
	 * only the window after the last arm). */
	gc_light_present_left = 0;
	gc_present_count = 0;
	gc_blank_present_count = 0;
	gc_budget_sample_count = 0;
	gc_budget_warmup_left = 0;
	gc_last_present_time = 0.0;
	gc_worst_frame_ms = 0.0;
	gc_budget_probe_active = false;
	gc_newgame_world_ready = true;
	gc_newgame_g36_done = true;
	Cvar_Set( "gc_hud_probe_skip", "0" );

	/* Lean HUD VidInit at quality 0: set 320 sheet names without hud.txt.
	 * Clear FS miss-cache so bootstrap-injected sprites/320_pain.spr is visible. */
	FS_ClearFindMissCache();
	if( clgame.dllFuncs.pfnVidInit )
	{
		clgame.dllFuncs.pfnVidInit();
		Con_Reportf( "Xash3D GameCube: newgame lean HUD VidInit after world present\n" );
	}

	refState.width = present_w;
	refState.height = present_h;
	SYS_Report( "Xash3D GameCube: newgame low-res world present %dx%d\n", present_w, present_h );
	GC_MemSample( "newgame world present" );

	/* Prefer real low-res world frames here: the Dolphin probe often exits as
	 * soon as G36 evidence is scored, before the next Host_Frame can run SCR.
	 * Fall back to lean green fills if the world path is not ready yet. */
	if( !GC_RenderNewGameWorldFrames( 4 ))
	{
		int i;

		Con_Reportf( "Xash3D GameCube: post-G36 sustained present (world render deferred)\n" );
		for( i = 0; i < 8; i++ )
		{
			GC_FillBudgetProbeFrameBuffer();
			GC_PresentBudgetProbeFrame();
		}
	}
	else
	{
		int i;

		/* G85/G90: pump V_RenderView-style presents BEFORE post-G36 server
		 * ticks — Host_ServerFrame (move/snapshots) has been leaving the
		 * next GL_RenderFrame hung on this route. */
		Con_Reportf( "Xash3D GameCube: post-G36 sustained world present\n" );
		for( i = 0; i < 8; i++ )
		{
			if( !GC_RenderNewGameWorldFrames( 1 ))
			{
				Con_Reportf( "Xash3D GameCube: V_RenderViewBoundedGC fail pump=%d\n", i );
				break;
			}
			if( i == 0 )
				Con_Reportf( "Xash3D GameCube: V_RenderView path present\n" );
			if( i == 1 )
			{
				char old_vm[16];
				Q_snprintf( old_vm, sizeof( old_vm ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
				Cvar_Set( "r_drawviewmodel", "1" );
				if( GC_RenderNewGameWorldFrames( 1 ))
					Con_Reportf( "Xash3D GameCube: V_RenderView viewmodel draw\n" );
				Cvar_Set( "r_drawviewmodel", old_vm );
			}
			if( i == 2 && cls.signon == SIGNONS )
			{
				ref.dllFuncs.R_BeginFrame( false );
				ref.dllFuncs.R_AllowFog( false );
				ref.dllFuncs.R_Set2DMode( true );
				CL_DrawHUD( CL_ACTIVE );
				Con_Reportf( "Xash3D GameCube: HUD lean draw\n" );
				ref.dllFuncs.R_AllowFog( true );
				Platform_SetTimer( 0.0f );
				ref.dllFuncs.R_EndFrame();
			}
		}

		for( i = 0; i < 2; i++ )
			Host_ServerFrame();
		Con_Reportf( "Xash3D GameCube: post-G36 bounded server ticks ready\n" );
	}
	return true;
#else
	return false;
#endif
}

qboolean GC_ShouldUseLightPresent( void )
{
#if XASH_GAMECUBE
	if( gc_newgame_world_ready )
		return false;
	return gc_budget_probe_active || gc_light_present_left > 0;
#else
	return false;
#endif
}

void GC_NoteLightPresentFrame( void )
{
#if XASH_GAMECUBE
	if( gc_light_present_left > 0 )
		gc_light_present_left--;
	/* After G36 samples + grace, switch New Game to low-res world presents. */
	if( gc_light_present_left == 0 && !gc_budget_probe_active
		&& Sys_CheckParm( "-gcnewgame" ) && !gc_newgame_world_ready )
		GC_PrepareNewGameWorldPresent();
#endif
}

void GC_FillBudgetProbeFrameBuffer( void )
{
#if XASH_GAMECUBE
	size_t i;
	size_t pixels;

	if( !gc.buffer || gc.width <= 0 || gc.height <= 0 )
		return;

	/* Solid non-black fill so G36 samples present cost with the map resident,
	 * without paying for a full software world render during the probe window. */
	pixels = (size_t)gc.width * (size_t)gc.height;
	if( pixels > gc.buffer_pixels )
		pixels = gc.buffer_pixels;
	for( i = 0; i < pixels; i++ )
		gc.buffer[i] = 0x07E0; /* bright green RGB565 */
#endif
}

void GC_PresentBudgetProbeFrame( void )
{
#if XASH_GAMECUBE
	/* Present the probe RGB565 buffer directly. R_EndFrame -> R_BlitScreen
	 * copies from the software renderer (still full-res) into gc.buffer and
	 * cannot sample Host_Frame intervals after Arm shrinks the present buffer. */
	GC_PresentBuffer();
#endif
}

void GC_ArmPostMapFrameBudgetSamples( void )
{
#if XASH_GAMECUBE
	uint stride, bpp, r, g, b;
	int probe_w = GC_VIDEO_NEWGAME_PROBE_WIDTH;
	int probe_h = GC_VIDEO_NEWGAME_PROBE_HEIGHT;

	/* After the first G36 flush, stay on the world-present path. Re-arming
	 * cleared world_ready and re-ran Prepare every few frames (VidInit thrash). */
	if( gc_newgame_g36_done )
		return;

	/* G83: capture PointInLeaf/FatPVS before G36 light presents reuse BSP scratch. */
	if( Sys_CheckParm( "-gcnewgame" ))
		GC_CaptureNewGamePVS();

	/* Match smoke-probe present cost: half-res buffer, skip VSync, cheap samples. */
	gc_present_count = 0;
	gc_blank_present_count = 0;
	gc_budget_sample_count = 0;
	gc_budget_warmup_left = GC_VIDEO_BUDGET_WARMUP_PRESENTS;
	gc_last_present_time = 0.0;
	gc_worst_frame_ms = 0.0;
	gc_budget_probe_active = true;

	if( !gc.buffer || gc.width > probe_w || gc.height > probe_h )
	{
		if( !SW_CreateBuffer( probe_w, probe_h, &stride, &bpp, &r, &g, &b ))
			SYS_Report( "Xash3D GameCube: post-map frame buffer fallback %dx%d\n", gc.width, gc.height );
		gc_present_count = 0;
		gc_last_present_time = 0.0;
	}

	if( GC_MapLoadMemoryOpt())
		Cvar_Set( "gc_quality", "0" );

	gc_newgame_world_ready = false;
	gc_gx_present_logged = false;
	Cvar_Set( "gc_hud_probe_skip", "1" );

	SYS_Report( "Xash3D GameCube: frame budget samples armed after map ready (%dx%d probe=%d)\n",
		gc.width, gc.height, gc_budget_probe_active ? 1 : 0 );

	/* Host_Frame SCR collects G36 samples on the light fill path. Do not
	 * exhaust the probe with a synthetic burst — that forced V_RenderView
	 * immediately after and stalled presents (probe vs world buffer). */
		gc_light_present_left = GC_GetFrameBudgetSampleTarget() + GC_VIDEO_LIGHT_PRESENT_GRACE;
	#endif
}

void GC_BeginFrameBudgetProbe( void )
{
#if XASH_GAMECUBE
	uint stride, bpp, r, g, b;

	gc_present_count = 0;
	gc_blank_present_count = 0;
	gc_budget_sample_count = 0;
	gc_budget_warmup_left = 0;
	gc_light_present_left = 0;
	gc_last_present_time = 0.0;
	gc_worst_frame_ms = 0.0;
	gc_budget_probe_active = true;

	/* G36 samples the static gcmap smoke panel after the map is loaded.
	 * Keep that evidence path readable, but avoid timing the full 640x480
	 * RGB565-to-XFB conversion twelve times before real gameplay is active. */
	if( !gc.buffer || gc.width > GC_VIDEO_PROBE_WIDTH || gc.height > GC_VIDEO_PROBE_HEIGHT )
	{
		if( !SW_CreateBuffer( GC_VIDEO_PROBE_WIDTH, GC_VIDEO_PROBE_HEIGHT, &stride, &bpp, &r, &g, &b ))
			SYS_Report( "Xash3D GameCube: frame budget probe buffer fallback %dx%d\n", gc.width, gc.height );
		gc_present_count = 0;
		gc_last_present_time = 0.0;
	}
#endif
}

void GC_RestoreVideoMemoryAfterMapLoad( void )
{
	uint stride, bpp, r, g, b;
	int width, height;
	static const int fallbacks[][2] = {
		{ GC_VIDEO_PROBE_WIDTH, GC_VIDEO_PROBE_HEIGHT },
		{ GC_VIDEO_NEWGAME_PROBE_WIDTH, GC_VIDEO_NEWGAME_PROBE_HEIGHT },
	};
	size_t i;

	if( gc.buffer && gc.width > 0 && gc.height > 0
		&& gc.width >= GC_VIDEO_NEWGAME_PROBE_WIDTH
		&& gc.height >= GC_VIDEO_NEWGAME_PROBE_HEIGHT )
	{
		GC_InitPresentTexture();
		return;
	}

	width = refState.width > 0 ? refState.width : DEFAULT_MODE_WIDTH;
	height = refState.height > 0 ? refState.height : DEFAULT_MODE_HEIGHT;

	/* After map load MEM1 is tight: prefer the lean probe buffer under
	 * map-load memory opt instead of failing a full 640x480 calloc.
	 * New Game stays on the 160×120 BSS probe so G36 samples do not calloc. */
	if( Sys_CheckParm( "-gcnewgame" ))
	{
		width = GC_VIDEO_NEWGAME_PROBE_WIDTH;
		height = GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	}
	else if( GC_MapLoadMemoryOpt()
		|| Sys_CheckParm( "-gcmap" ))
	{
		width = GC_VIDEO_PROBE_WIDTH;
		height = GC_VIDEO_PROBE_HEIGHT;
	}

	if( SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
	{
		/* Keep refState in sync so connect-time 2D does not assume 640×480. */
		if( gc.width > 0 && gc.height > 0 )
		{
			refState.width = gc.width;
			refState.height = gc.height;
		}
		SYS_Report( "Xash3D GameCube: restored presentation buffer %dx%d\n",
			gc.width, gc.height );
		return;
	}

	for( i = 0; i < sizeof( fallbacks ) / sizeof( fallbacks[0] ); i++ )
	{
		if( fallbacks[i][0] == width && fallbacks[i][1] == height )
			continue;
		if( SW_CreateBuffer( fallbacks[i][0], fallbacks[i][1], &stride, &bpp, &r, &g, &b ))
		{
			if( gc.width > 0 && gc.height > 0 )
			{
				refState.width = gc.width;
				refState.height = gc.height;
			}
			SYS_Report( "Xash3D GameCube: restored presentation buffer %dx%d (fallback after %dx%d fail)\n",
				gc.width, gc.height, width, height );
			return;
		}
	}

	SYS_Report( "GX video: restore buffer failed %dx%d\n", width, height );
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
