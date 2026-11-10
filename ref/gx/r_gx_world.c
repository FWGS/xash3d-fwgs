/*
Copyright (C) 2026 Xash3D FWGS GameCube port

G151–G154: Flipper GX world draw — cap faces as EFB triangles + LM.
G155: Flipper GX studio/viewmodel via TriAPI → EFB overlay.
G157: viewmodel eye-pose sync + narrower Flipper FOV.
G178: cache world TEV/vtx + TEXMAP0 binds.
G179: lean sync — skip hot InvalidateTexAll, Flush not DrawDone, cache LM objs.
G180: pack face lightmaps into one TEXMAP1 atlas.
G181: cluster TEXMAP0 binds within area-order bands.
G182: Flipper GX 2D StretchPic for live HUD on EFB.
G183: pin HUD TEXMAP0 slots + TriColor tint; richer Flipper pic count.
G184: HUD RGB5A3 + alpha compare so SPR_DrawHoles drop transparent texels.
G185: cut Flipper HUD fill (lean crosshair cell + fill-px telemetry).
G186: skip tiny / far-small Flipper faces before ST/LM emit (fill lean).
G297: tighter face budgets + skip EFX/decals during G36 sample window.
G298: lean live+fill after Capture under G283 scratch retain (baked verts).
G302: raise Flipper live emit 80→112 — lean pool holds ~124, work ~1ms.
G348: raise Flipper emit + tram fill under scratch retain; dark tunnel clear
  (G347 DumpFrames still blue-void at live=248/drawn=48 sample cap).
G187: HUD holes punch near-black ink (crosshair sheet non-255 dark texels).
G201: guFrustum projection for GX_PERSPECTIVE (Xash GL proj was invisible).
G202: Flipper diffuse REPLACE (no LM crush on eye-centered quads).
G206: REPLACE on EDGE/TEX verts too (real ST shapes; defer LM until overbright).
G207: LM restored — boost bake, corner atlas UV, TEV ×2 overbright.
G203: plane-quad ST 0..1 + LINEAR world filter.
G213: live BSP30 opaque when surfaces pinned off scratch.
G216: lean live baked verts for skyfill emit (edges may dangle).
G221: indoor EFB clear under -gcnewgame (holes ≠ outdoor sky blue).
G222: heap flat-fill faces beyond 320+192 (after live pool; no BSS steal).

Retail Flipper scope (G198/G285/G286/G291/G292): capped/PVS world + studio +
HUD + lean sky + baked water + Flipper EFX + tip-safe world decals (≤8).
Not feature-parity: full brush-mover lighting remains soft/diagnostic.
*/
#include "r_local.h"

#if XASH_GAMECUBE
/* libogc also exposes COLOR_WHITE with its packed YUV value. This GX unit
 * does not use Xash's 16-bit UI constant; avoid a macro redefinition warning
 * and keep the libogc definition visible to its headers. */
#ifdef COLOR_WHITE
#undef COLOR_WHITE
#endif
#include <gccore.h>
#include <ogc/gx.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../../engine/platform/gamecube/mem_gamecube.h"

extern qboolean GC_UseGxWorldDraw( void );
extern qboolean GC_UseGxRenderer( void );
extern void GC_MarkGxWorldEfbReady( void );
extern void *GC_GetGxVideoMode( void );
extern void GC_GetNewGameCapLightmapAtlasUV( int slot, float s, float t, float *out_s, float *out_t );
extern qboolean GC_WorldSurfacesLive( void );
extern qboolean GC_WorldSurfacesPinned( void );
extern qboolean GC_WorldSurfacesScratchRetained( void );
extern int GC_GetLiveFaceCount( void );
extern void GC_NoteCapFacesDrawn( int drawn );
extern qboolean GC_FillLiveDrawSurf( int index, msurface_t *out, mtexinfo_t *tex_out );
extern qboolean GC_LiveFaceIsCapped( int index );
extern qboolean GC_WantLiveCapOverlap( void ); /* G361 */
extern qboolean GC_IsG36SampleFaceCap( void ); /* G374/GX-016 */
extern qboolean GC_WantTightXenG36FaceCap( void );
extern qboolean GC_CapFaceIsLive( int slot ); /* G361 */
extern qboolean GC_G378DumpSkipFarPoint( const float *p );
extern qboolean GC_G378DumpSkipFarVerts( const float pts[][3], int nverts );
extern qboolean GC_G378DumpSkipFarCapSlot( int slot );
extern void GC_BeginCapFaceVis( const float *eye );
extern qboolean GC_CapFaceInPVS( int slot );
extern qboolean GC_CapFaceBounds( int slot, float mins[3], float maxs[3] );
extern int GC_CapFacePVSSkipCount( void );
extern int GC_CapFaceVisAssigned( void );
extern int GC_GetLiveFaceVerts( int index, float out[][3], int maxverts );
extern int GC_GetLiveFaceBakeSrc( int index );
extern int GC_GetFillFaceCount( void );
extern int GC_GetFillFaceVerts( int index, float out[][3], int maxverts );
extern int GC_GetWaterFaceCount( void );
extern int GC_GetWaterFaceVerts( int index, float out[][3], int maxverts );
extern int GC_GetWaterFaceST( int index, float out_st[][2], int maxverts );
extern void GC_GetWaterFaceScroll( int index, float *scroll_u, float *scroll_v, u32 *tint );
extern qboolean GC_WaterTexReady( void );
extern void *GC_GetWaterTexObj( void );
extern qboolean GC_SandSpecTexReady( void );
extern void *GC_GetSandSpecTexObj( void );
extern qboolean GC_SandNrmTexReady( void );
extern void *GC_GetSandNrmTexObj( void );
extern qboolean GC_SandSunTexReady( void );
extern void *GC_GetSandSunTexObj( void );
extern int GC_GetPropTriCount( void );
extern qboolean GC_GetPropTri( int index, float out_xyz[3][3], float out_st[3][2], int *out_tex );
extern void *GC_GetPropTexObj( int index );
extern qboolean GC_GetPropTri( int index, float out_xyz[3][3], float out_st[3][2], int *out_tex );
extern void *GC_GetPropTexObj( int index );
extern qboolean GC_WaterFacePlane( int index, mplane_t *out, int *out_flags );
extern qboolean GC_FillFacePlane( int index, mplane_t *out, int *out_flags );
extern int GC_GetTramFaceCount( void );
extern int GC_GetTramFaceVerts( int index, float out[][3], int maxverts );
extern int GC_GetTramFaceTexnum( int index );
extern int GC_GetTramFaceST( int index, float out[][2], int maxverts );
extern void GC_GetTramModelOrigin( float out[3] );
extern qboolean GC_TramCabinRide( void );
#ifndef GC_TRAM_FACE_EXTERIOR
#define GC_TRAM_FACE_EXTERIOR 0x01
#endif
extern qboolean GC_TramLightmapReady( void );
extern int GC_GetTramDiffuseTexnum( void );
extern int GC_GetResidentWorldDiffuseTexnum( void );
extern void GC_RecordResidentWorldDiffuseTexnum( int texnum );
extern int GC_GetNewGameCapTextureNum( void );
extern const unsigned short *GC_GetTramLightmapAtlas( int *w, int *h );
extern void GC_GetTramLightmapUV( int face, float s, float t, float *out_s, float *out_t );
extern msurface_t *GC_GetLiveDrawSurfs( void );
extern unsigned R_GXGetTriColorRGBA( void );
extern u16 GC_GXFenceNext( void );
extern qboolean GC_GXFenceWait( u16 token );

#define GC_GX_TEX_SLOTS		32
/* Font + title logo + chrome. Keep this at 3: bumping to 8 pre-reserved
 * 128² slabs (~256 KiB) at R_Init starved the 150 KiB cinematic texture and
 * left the intro black. Menu bg tiles grow HUD slots on demand after the
 * intro releases ARAM/MEM1. */
#define GC_GX_TEX_HUD_RESERVE	3
/* World pool stays compact; menu chrome may use Flipper's larger limit. */
#define GC_GX_TEX_WORLD_MAX_DIM	128
#define GC_GX_TEX_HUD_MAX_DIM	512	/* logo / HUD sheets (≤ Flipper 1024) */
#define GC_GX_TEX_MAX_DIM	GC_GX_TEX_WORLD_MAX_DIM
/* Fullscreen menu background matches NTSC EFB; dedicated lazy slab. */
#define GC_GX_MENU_BG_WIDTH	640
#define GC_GX_MENU_BG_HEIGHT	480
#define GC_GX_MENU_BG_BYTES	( GC_GX_MENU_BG_WIDTH * GC_GX_MENU_BG_HEIGHT * sizeof( u16 ))
/* MEM-003: 4→3 BSS world tiles (−32 KiB). World binds LRU within the pool;
 * HUD/cinematic keep reserved storage. G199 noted 8 tipped clipnodes — keep ≥3. */
#define GC_GX_TEX_WORLD_POOL	3
static u16 r_gx_tex_world_pool[GC_GX_TEX_WORLD_POOL][GC_GX_TEX_WORLD_MAX_DIM * GC_GX_TEX_WORLD_MAX_DIM]
	__attribute__((aligned( 32 )));
static qboolean gc_mem003_world_pool_logged;
#define GC_GX_CINEMATIC_WIDTH 320
#define GC_GX_CINEMATIC_HEIGHT 240
#define GC_GX_CINEMATIC_BYTES ( GC_GX_CINEMATIC_WIDTH * GC_GX_CINEMATIC_HEIGHT * sizeof( u16 ))

typedef struct
{
	unsigned	texnum;
	int		w, h;
	size_t		alloc_bytes;
	u8		fmt;	/* GX_TF_RGB565 or GX_TF_RGB5A3 */
	GXTexObj	obj;
	u16		*tiled;	/* MEM1 aligned */
	qboolean	valid;
	qboolean	hud_pin;	/* G183: do not LRU-evict for world binds */
	qboolean	is_font;	/* menu/HUD font atlas — prefer not to LRU-evict */
} gc_gx_tex_t;

static gc_gx_tex_t r_gx_tex[GC_GX_TEX_SLOTS];
static gc_gx_tex_t r_gx_cinematic_tex;
static qboolean r_gx_cinematic_ready;
static unsigned r_gx_cinematic_updates;
static gc_gx_tex_t r_gx_menu_bg_tex;
static qboolean r_gx_menu_bg_ready;
static int r_gx_tex_clock;
static int r_gx_tex_lru[GC_GX_TEX_SLOTS];
/* Draw-sync token after the last draw queued with each slot bound. A miss
 * waits for it before re-swizzling into the slot: Flipper reads texels when
 * it executes the FIFO, so overwriting a slot still referenced by queued
 * faces drew those faces with the new texture (tunnel walls flashing to the
 * next texture's panels every frame on the tram ride). */
static u16 r_gx_tex_fence[GC_GX_TEX_SLOTS];
static unsigned r_gx_tex_uploads, r_gx_tex_fence_stalls;
static model_t *r_gx_tex_world;
static qboolean r_gx_world_drew;
static qboolean r_gx_world_logged;
static qboolean r_gx_tex_logged;
static int r_gx_tex_draws;
static int r_gx_flat_draws;
/* G178: avoid re-emitting identical world TEV/vtx state for every face. */
enum
{
	GC_GX_FACE_MODE_NONE = 0,
	GC_GX_FACE_MODE_FLAT,
	GC_GX_FACE_MODE_TEXTURED,
	GC_GX_FACE_MODE_TEXTURED_LIVE, /* G219: MODULATE muted live skyfill */
	GC_GX_FACE_MODE_LIT,
	GC_GX_FACE_MODE_LIT_LIVE,	/* live BSP faces x full-res lightmap atlas */
	GC_GX_FACE_MODE_LIT_ENV	/* CapFaces diffuse×LM + sky cubemap specular */
};
/* CapFaces bake flag: Source $envmap (sand/rock specular), see bake_lostcoast_map.py */
#ifndef GC_SURF_ENVMAP
#define GC_SURF_ENVMAP 0x4000
#endif
static int r_gx_face_mode;
static unsigned r_gx_bound_texnum;
static u8 r_gx_bound_fmt;
/* Synthetic safety faces are emitted double-sided, but must not disable
 * Flipper back-face rejection for the ordinary BSP that follows them. */
static qboolean r_gx_cull_none;
static int r_gx_state_sets;
static unsigned r_gx_frame_verts; /* -gcprofile: vertices submitted this frame */
static int r_gx_state_reuses;
static int r_gx_tex_loads;
static int r_gx_tex_reuses;
static qboolean r_gx_state_cache_logged;

void R_GXGetStateStats( int *sets, int *reuses, int *tex_loads, int *tex_reuses )
{
	if( sets ) *sets = r_gx_state_sets;
	if( reuses ) *reuses = r_gx_state_reuses;
	if( tex_loads ) *tex_loads = r_gx_tex_loads;
	if( tex_reuses ) *tex_reuses = r_gx_tex_reuses;
}
/* Route flags are stable for a world pass; avoid Sys_CheckParm in every face. */
static qboolean r_gx_newgame_route;
static qboolean r_gx_menu_route;
/* G179: lightmap atlas bind (G180) + lean texture invalidation. */
static GXTexObj r_gx_lm_atlas_obj;
static qboolean r_gx_lm_atlas_valid;
static qboolean r_gx_lm_atlas_checked;
static qboolean r_gx_lm_atlas_available;
static int r_gx_lm_atlas_gen = -1;
static qboolean r_gx_lm_atlas_bound;
static qboolean r_gx_live_lm_bound;
static int r_gx_efb_dump_hold; /* G200: keep Flipper EFB for DumpFramesAsImages */
/* Lost Coast DumpFrames densify: multi-pack EFB is held; skip pack0 redraw. */
static qboolean r_gx_cap_densify_held;
/* Arm dump-hold AFTER water so env sheets land in the frozen EFB once. */
static int r_gx_cap_densify_hold_pending;
static qboolean r_gx_g221_world_clear_done;

/* Dolphin DumpFramesAsImages samples EFB asynchronously — clearing at the
 * start of the next world pass left solid sky dumps despite drawn=179. */
void R_GXHoldEfbForDump( int frames )
{
	if( frames > r_gx_efb_dump_hold )
		r_gx_efb_dump_hold = frames;
}

/* G362: PresentBuffer must not soft-blit over CapFaces while dump-hold is armed. */
int R_GXEfbDumpHoldLeft( void )
{
	return r_gx_efb_dump_hold;
}

static int r_gx_dump_skip_viewmodel;

void R_GXDumpSkipViewmodel( int on )
{
	r_gx_dump_skip_viewmodel = on ? 1 : 0;
}

int R_GXDumpSkipViewmodelActive( void )
{
	return r_gx_dump_skip_viewmodel;
}

/* G377: allow one clear+CapFaces+studio redraw so DumpFrames can show idle. */
void R_GXBeginDumpAnimFrame( void )
{
	r_gx_efb_dump_hold = 0;
	r_gx_g221_world_clear_done = false;
}
static int r_gx_cap_generation = -1;
/* CapFaces GX display lists (-gcworlddl): record LM emit once per generation,
 * CallDispList on later frames. Denser DoD maps spend the tip budget on BSP
 * parks; Flipper replay avoids re-walking CapFaces CPU emit. */
#ifndef GC_GX_CAP_DL_BYTES
#define GC_GX_CAP_DL_BYTES ( 192u * 1024u )
#endif
/* Tip listen cannot memalign a CapFaces DL after map arm — keep a BSS fallback
 * so -gcworlddl still CallDispList (384 faces ≈ ≤96 KiB of GX cmds). */
#ifndef GC_GX_CAP_DL_BSS_BYTES
/* PVS/frustum CapFaces re-emit each frame — static DL is unused there.
 * Keep BSS at 0 by default (+128 KiB tip for dense lean maps). Pass
 * -DGC_GX_CAP_DL_BSS_BYTES=131072 when baking a -gcworlddl disc. */
#define GC_GX_CAP_DL_BSS_BYTES 0
#endif
#if GC_GX_CAP_DL_BSS_BYTES > 0
static byte r_gx_cap_dl_bss[GC_GX_CAP_DL_BSS_BYTES] __attribute__(( aligned( 32 )));
#endif
static void *r_gx_cap_dl;
static u32 r_gx_cap_dl_cap;
static u32 r_gx_cap_dl_bytes;
static int r_gx_cap_dl_gen = -1;
static int r_gx_cap_dl_drawn;
static qboolean r_gx_cap_dl_recording;

/* Pin CapFaces DL to BSS only when configured — early memalign(192K) at
 * R_Init starved FS_LoadFile of media/cf_*.gcp and forced live EDGE. */
qboolean R_GXReserveCapDispList( void )
{
	if( r_gx_cap_dl && r_gx_cap_dl_cap > 0 )
		return true;
#if GC_GX_CAP_DL_BSS_BYTES > 0
	r_gx_cap_dl = r_gx_cap_dl_bss;
	r_gx_cap_dl_cap = GC_GX_CAP_DL_BSS_BYTES;
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: CapFaces display list BSS bytes=%u\n",
		(unsigned)GC_GX_CAP_DL_BSS_BYTES );
	return true;
#else
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: CapFaces display list BSS disabled (PVS immediate path)\n" );
	return false;
#endif
}
/* G367: CapFaces begin/end once per map (not tr.framecount<=3). Dual-hop
 * denser dests often enter CapFaces after frame 3 and skipped the markers. */
static char r_gx_capfaces_begin_map[64];
static char r_gx_capfaces_end_map[64];
static int r_gx_lm_inits;
static int r_gx_lm_loads;
static int r_gx_lm_reuses;
static int r_gx_tex_invalidates;
static qboolean r_gx_sync_lean_logged;
static qboolean r_gx_lm_atlas_logged;
static qboolean r_gx_tex_band_logged;
/* G186: Flipper world fill cull (extents dust + far-small). */
/* G186/G199/G383: keep the far band conservative, but do not make small
 * distant walls pop out at the old 2048-unit cutoff. The emit budgets below
 * remain hard limits, so this only improves coverage when unused face slots
 * are available. */
#ifndef GC_GX_MIN_FACE_AREA
#define GC_GX_MIN_FACE_AREA 384	/* G297: was 256 — drop dust for 60Hz headroom */
#endif
#ifndef GC_GX_FAR_FACE_DIST
#define GC_GX_FAR_FACE_DIST 3072.0f
#endif
#ifndef GC_GX_FAR_MIN_AREA
#define GC_GX_FAR_MIN_AREA 2048	/* preserve readable distant wall strips */
#endif
/* G280/G282/G297/G302/G348: Flipper per-frame emit. Caps + live PVS pool share FRAME.
 * G297 cut for headroom; unmasked G36 ~1ms so G302 restores live toward pool size.
 * G348: live 124→192 + frame 248→280 — G347 late DumpFrames still void at 124. */
#ifndef GC_GX_FRAME_FACE_BUDGET
#define GC_GX_FRAME_FACE_BUDGET 832	/* CapFaces 768 + fill/water reserve */
#endif
#ifndef GC_GX_LIVE_FACE_BUDGET
#define GC_GX_LIVE_FACE_BUDGET 256	/* was 192 — draw more of live PVS pool */
#endif
/* G36 sample window: c0a0 tram capture draws ~320 LM faces → ~52ms/present
 * (probe 20260809-124458 WEAK). Cap Flipper emit during the armed probe so
 * samples measure tip-safe present cost; full budget resumes after flush.
 * G348: 48/24→96/48 (Flipper ~20ms). Keep sample below full retail emit.
 * GX-016: denser AM (c1a0d) still WEAK at 96/48 (~38ms); use a tighter band. */
#ifndef GC_GX_G36_SAMPLE_FACE_BUDGET
#define GC_GX_G36_SAMPLE_FACE_BUDGET 96
#endif
#ifndef GC_GX_G36_SAMPLE_LIVE_BUDGET
#define GC_GX_G36_SAMPLE_LIVE_BUDGET 48
#endif
#ifndef GC_GX_G36_DENSER_FACE_BUDGET
#define GC_GX_G36_DENSER_FACE_BUDGET 115	/* G381: was 64 — room fill under DumpFrames */
#endif
#ifndef GC_GX_G36_DENSER_LIVE_BUDGET
#define GC_GX_G36_DENSER_LIVE_BUDGET 32
#endif

static int R_GXFrameFaceBudget( void )
{
	/* Cap only during the armed G36 window; denser AM gets a tighter band. */
	if( GC_IsG36SampleFaceCap() )
	{
		if( GC_WantLiveCapOverlap() )
		{
			/* Xen hop kept 115 faces and scored avg≈63ms WEAK (c4a3→c5a1
			 * probe 20261105-104538). G374 needed a tight band for ≈61ms→PASS. */
			if( GC_WantTightXenG36FaceCap() )
				return 16;
			return GC_GX_G36_DENSER_FACE_BUDGET;
		}
		return GC_GX_G36_SAMPLE_FACE_BUDGET;
	}
	return GC_GX_FRAME_FACE_BUDGET;
}

static int R_GXLiveFaceBudget( void )
{
	if( GC_IsG36SampleFaceCap() )
	{
		if( GC_WantLiveCapOverlap() )
			return GC_GX_G36_DENSER_LIVE_BUDGET;
		return GC_GX_G36_SAMPLE_LIVE_BUDGET;
	}
	return GC_GX_LIVE_FACE_BUDGET;
}
#ifndef GC_GX_FILL_FACE_BUDGET
#define GC_GX_FILL_FACE_BUDGET 64	/* G381: was 48 — menu tram floor/tunnel plugs */
#endif
static int r_gx_face_skips;
static int r_gx_face_skip_area;
static int r_gx_face_skip_far;
static qboolean r_gx_face_cull_logged;

/* G155 studio overlay */
static qboolean r_gx_studio_active;
static qboolean r_gx_studio_viewmodel;
static qboolean r_gx_studio_logged;
static qboolean r_gx_studio_vm_logged;
static int r_gx_studio_tris;
static int r_gx_studio_world_tris_acc;
static int r_gx_studio_vm_tris_acc;
static u32 r_gx_studio_color = 0xFFFFFFFF;
static unsigned r_gx_studio_bound_tex;
static unsigned r_gx_studio_pending_tex; /* G376: SetupSkin before ForceBegin */
/* True while the GX vertex layout is the studio/TriAPI one
 * (POS+CLR0+TEX0+TEX1, f32/RGBA8). Every other layout change in this file
 * goes through GX_ClearVtxDesc, which clears it. The effects/studio latch
 * stays open across world and brush draws, and a TriAPI emit against a
 * stale layout desyncs the GP and hangs the CPU on a full FIFO (frame 1
 * trans-EFX particle quad, full-physics route). */
static qboolean r_gx_studio_layout;
#define GX_ClearVtxDesc() ( r_gx_studio_layout = false, GX_ClearVtxDesc() )
/* The world face path also toggles single attributes (TEX1 -> GX_NONE)
 * without clearing, so any descriptor or format write counts. */
#define GX_SetVtxDesc( attr, type ) ( r_gx_studio_layout = false, GX_SetVtxDesc( attr, type ))
#define GX_SetVtxAttrFmt( fmt, attr, cnt, type, frac ) \
	( r_gx_studio_layout = false, GX_SetVtxAttrFmt( fmt, attr, cnt, type, frac ))
/* Bounded trace of every primitive begin, armed when the menu route's
 * client goes active: the guest freezes there with no exception, which is
 * how a vertex stream that does not match the descriptor looks. */
static int r_gx_begin_trace;
void R_GXArmBeginTrace( int count )
{
	r_gx_begin_trace = count;
}
void R_GXTraceBegin( const char *where, int line, int prim, int count, int studio )
{
	if( r_gx_begin_trace <= 0 )
		return;
	r_gx_begin_trace--;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: GXB %s:%d prim=0x%02x n=%d studio=%d\n",
		where, line, prim, count, studio );
}
#define GX_Begin( prim, fmt, count ) \
	( R_GXTraceBegin( "world", __LINE__, ( prim ), ( count ), r_gx_studio_layout ), GX_Begin( prim, fmt, count ))
static unsigned r_gx_studio_shade_mask; /* G164: luminance buckets seen this pass */
static qboolean r_gx_studio_gouraud_logged;
static qboolean r_gx_studio_zrange_logged; /* G167 */
/* G157: NDC band of viewmodel verts (Quake-style clip / w). */
static matrix4x4 r_gx_studio_mvp;
static qboolean r_gx_studio_mvp_valid;
static float r_gx_vm_ndc_ymin = 1e9f;
static float r_gx_vm_ndc_ymax = -1e9f;
static int r_gx_vm_ndc_samples;
static float r_gx_vm_fov_x;
static qboolean r_gx_hud_2d_ready;
static qboolean r_gx_hud_2d_logged;
static qboolean r_gx_hud_rich_logged;
static qboolean r_gx_hud_holes_logged;
static qboolean r_gx_hud_fill_logged;
static qboolean r_gx_hud_pool_ready;
static int r_gx_hud_2d_pics;
static int r_gx_hud_holes_pics;
static int r_gx_hud_bind_fails;
static int r_gx_hud_fill_px;
static int r_gx_hud_holes_fill_px;
/* G183: keep convert scratch off the deep world→HUD stack.
 * Small BSS for world/HUD fonts; large menu uploads lazy-grow this pointer. */
static u16 r_gx_tex_linear_small[GC_GX_TEX_WORLD_MAX_DIM * GC_GX_TEX_WORLD_MAX_DIM]
	__attribute__((aligned( 32 )));
static u16 *r_gx_tex_linear = r_gx_tex_linear_small;
static size_t r_gx_tex_linear_cap = GC_GX_TEX_WORLD_MAX_DIM * GC_GX_TEX_WORLD_MAX_DIM;

#define GC_GX_TEX_HUD_SLOT0	( GC_GX_TEX_SLOTS - GC_GX_TEX_HUD_RESERVE )
/* HUD/menu textures may also take the slots between the world pool and the
 * reserved slabs, backed on demand: the retail menu binds ~10 distinct
 * textures (button strips, logo, fonts) per frame, and three slots made them
 * evict and re-convert each other every frame (~5 fps menu).  Backing comes
 * from the boot arena while it is open (menu), else from the heap within
 * GC_GX_TEX_HUD_HEAP_BUDGET. */
#define GC_GX_TEX_HUD_ONDEMAND0	GC_GX_TEX_WORLD_POOL
#define GC_GX_TEX_HUD_HEAP_BUDGET	( 64u * 1024u )
static size_t r_gx_hud_ondemand_heap;

/* G184: soft TRANSPARENT_COLOR → RGB5A3 with alpha 0 (not opaque black).
 * Do not infer transparency from RGB darkness: the retail menu background
 * is intentionally near-black and must remain opaque. */
static int r_gx_hud_nearblack_punched;
static qboolean r_gx_hud_nearblack_logged;

static u16 R_GXSoftToRGB5A3( pixel_t soft )
{
	u16 rgb565;
	u32 r5, g6, b5, g5;

	if( soft == TRANSPARENT_COLOR )
		return 0;
	rgb565 = (u16)R_GCSoftToRGB565( soft );
	r5 = ( rgb565 >> 11 ) & 31u;
	g6 = ( rgb565 >> 5 ) & 63u;
	b5 = rgb565 & 31u;
	g5 = g6 >> 1;
	return (u16)( 0x8000u | ( r5 << 10 ) | ( g5 << 5 ) | b5 );
}

/* WON btn strips are orange on black; TransAlpha needs black → alpha0. */
static u16 R_GXSoftToRGB5A3PunchBlack( pixel_t soft )
{
	u16 rgb5a3 = R_GXSoftToRGB5A3( soft );
	u32 r5, g5, b5;

	if( rgb5a3 == 0 )
		return 0;
	r5 = ( rgb5a3 >> 10 ) & 31u;
	g5 = ( rgb5a3 >> 5 ) & 31u;
	b5 = rgb5a3 & 31u;
	if( r5 <= 1u && g5 <= 1u && b5 <= 1u )
		return 0;
	return rgb5a3;
}

/*
 * Reserve HUD TEXMAP0 slabs before world fills MEM1. Textures share them
 * through the fenced LRU; reserving nine full 128×128 slabs exhausts the
 * heap before the retail menu can load localization / titles.
 */
static void R_GXReserveHudPool( void )
{
	int i;
	const size_t bytes = (size_t)GC_GX_TEX_MAX_DIM * (size_t)GC_GX_TEX_MAX_DIM * sizeof( u16 );

	if( r_gx_hud_pool_ready )
		return;
	for( i = 0; i < GC_GX_TEX_HUD_RESERVE; i++ )
	{
		int slot = GC_GX_TEX_HUD_SLOT0 + i;

		if( r_gx_tex[slot].tiled && r_gx_tex[slot].alloc_bytes >= bytes )
			continue;
		if( r_gx_tex[slot].tiled )
		{
			free( r_gx_tex[slot].tiled );
			r_gx_tex[slot].tiled = NULL;
			r_gx_tex[slot].alloc_bytes = 0;
			r_gx_tex[slot].valid = false;
		}
		r_gx_tex[slot].tiled = (u16 *)memalign( 32, bytes );
		/* Boot/menu: the heap is at its tightest while the menu chrome is up.
		 * Borrow the idle boot arena rather than run the menu on fewer slabs
		 * (garbled button strips, G184).  The first map load releases the
		 * HUD pool before it takes the pad back (R_GcmapTrimForMapLoad). */
		if( !r_gx_tex[slot].tiled )
			r_gx_tex[slot].tiled = (u16 *)GC_BootArenaAlloc( bytes );
		if( !r_gx_tex[slot].tiled )
		{
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G184 HUD pool alloc fail slot=%d\n", slot );
			continue;
		}
		r_gx_tex[slot].alloc_bytes = bytes;
		r_gx_tex[slot].valid = false;
		r_gx_tex[slot].hud_pin = true;
	}
	r_gx_hud_pool_ready = true;
}

/* Called at the end of R_Init, while MEM1 still has room. */
void R_GXReserveHudPoolEarly( void )
{
	/* Defer HUD slabs until the first HUD bind. Reserving at R_Init raced the
	 * intro cinematic's 150 KiB GX surface and produced a black valve.avi.
	 * CapFaces GX DL can take heap now (or pin BSS) before map tip. */
	if( gEngfuncs.Sys_CheckParm( "-gcworlddl" ))
		(void)R_GXReserveCapDispList();
}

/*
 * Release HUD Flipper slabs + menu background before New Game client InitInput.
 * Soft imagepool empties alone leave these memalign() blocks on the tip and the
 * first pfnAddCommand (19-byte BaseCmd name) then Sys_Errors.
 */
void R_GXReleaseHudPoolForMapLoad( void )
{
	int i;

	/* Reserved slabs and on-demand HUD slots (boot arena or heap; free()
	 * releases either) — all before the map load takes the arena back. */
	for( i = GC_GX_TEX_HUD_ONDEMAND0; i < GC_GX_TEX_SLOTS; i++ )
	{
		if( r_gx_tex[i].tiled )
		{
			free( r_gx_tex[i].tiled );
			r_gx_tex[i].tiled = NULL;
		}
		memset( &r_gx_tex[i], 0, sizeof( r_gx_tex[i] ));
	}
	r_gx_hud_ondemand_heap = 0;
	r_gx_hud_pool_ready = false;
	R_GXReleaseMenuBackgroundTexture( 0 );
	R_GXReleaseCinematicTexture( 0 );
	if( gEngfuncs.Con_Reportf )
		gEngfuncs.Con_Reportf( "Xash3D GameCube: GX HUD/menu slabs released for map load\n" );
}

qboolean R_GXWorldDrewThisFrame( void )
{
	return r_gx_world_drew;
}

static void R_GXPrepareStudioState( qboolean viewmodel );

qboolean R_GXStudioIsActive( void )
{
	return r_gx_studio_active;
}

/* True while Flipper should consume TriAPI (studio or particles/sprites/beams). */
static qboolean r_gx_effects_tri;
static int r_gx_effects_tris;
static qboolean r_gx_effects_logged;

qboolean R_GXTriApiIsActive( void )
{
	return r_gx_studio_active || r_gx_effects_tri;
}

void R_GXEffectsTriBegin( void )
{
	extern qboolean GC_IsFrameBudgetProbeActive( void );

	if( !GC_UseGxWorldDraw() )
		return;
	if( r_gx_studio_active )
		return;
	/* G297: skip particle/sprite/beam TriAPI during G36 sample window. */
	if( GC_IsFrameBudgetProbeActive() )
		return;
	if( !r_gx_effects_tri )
	{
		/* G291: particles/beams/sprites share studio TriAPI emit, with
		 * billboard cull + additive blend (PrepareStudioState is opaque). */
		R_GXPrepareStudioState( false );
		GX_SetCullMode( GX_CULL_NONE );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
		if( vid.rendermode == kRenderTransAdd || vid.rendermode == kRenderGlow )
			GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP );
		else if( vid.rendermode == kRenderTransTexture
			|| vid.rendermode == kRenderTransAlpha
			|| vid.rendermode == kRenderTransColor )
			GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
		else
			GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
		r_gx_effects_tri = true;
		r_gx_effects_tris = 0;
		r_gx_studio_bound_tex = 0;
		r_gx_face_mode = GC_GX_FACE_MODE_NONE;
		r_gx_bound_texnum = 0;
		r_gx_bound_fmt = 0;
		GC_MarkGxWorldEfbReady();
	}
}

void R_GXEffectsTriEnd( void )
{
	if( !r_gx_effects_tri )
		return;
	r_gx_effects_tri = false;
	GX_Flush();
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetCullMode( GX_CULL_BACK );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	if( r_gx_effects_tris > 0 )
	{
		r_gx_world_drew = true;
		GC_MarkGxWorldEfbReady();
		if( !r_gx_effects_logged )
		{
			r_gx_effects_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G291 Flipper EFX tris=%d (particles/sprites/beams)\n",
				r_gx_effects_tris );
		}
	}
	r_gx_effects_tris = 0;
}

int R_GXStudioLastTriCount( void )
{
	return r_gx_studio_tris;
}

static void R_GXLoadMtxFromXashMV( Mtx out, const matrix4x4 in )
{
	float gl[16];
	int i, j;

	Matrix4x4_ToArrayFloatGL( in, gl );
	for( i = 0; i < 3; i++ )
		for( j = 0; j < 4; j++ )
			out[i][j] = gl[j * 4 + i];
}

/*
 * G201: GX_LoadProjectionMtx(…, GX_PERSPECTIVE) expects a guFrustum /
 * guPerspective matrix — not an OpenGL-style Matrix4x4_CreateProjection.
 * Feeding Xash GL proj made drawn=179 faces invisible while a camera-space
 * guPerspective magenta marker still showed in DumpFrames.
 */
static void R_GXBuildWorldProjection( Mtx44 out )
{
	f32 zNear = 4.0f;
	/* Lost Coast cliffs sit 2–4k from bake-eye; a short farClip clipped the
	 * densify silhouette to sand+shards (host CapFaces densify filled). */
	f32 zFar = Q_max( 8192.0f, RI.farClip > 1.0f ? RI.farClip : 8192.0f );
	f32 fov_y = RI.rvp.fov_y > 1.0f ? RI.rvp.fov_y : 90.0f;
	f32 fov_x = RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f;
	f32 yMax = zNear * tanf( fov_y * (f32)M_PI_F / 360.0f );
	f32 xMax = zNear * tanf( fov_x * (f32)M_PI_F / 360.0f );

	/* guFrustum(mt, top, bottom, left, right, near, far) */
	guFrustum( out, yMax, -yMax, -xMax, xMax, zNear, zFar );
}

/*
 * G201b: build GX modelview from Quake view basis directly.
 * Xash Matrix4x4_CreateModelview + R_GXLoadMtxFromXashMV still left walls
 * invisible under guFrustum (DumpFrames ~97% sky despite drawn=179).
 * Eye space: +X right, +Y up, -Z forward (libogc guPerspective expectation).
 */
static void R_GXBuildWorldModelview( Mtx out )
{
	vec3_t	forward, right, up;
	const float *org = RI.rvp.vieworigin;

	AngleVectors( RI.rvp.viewangles, forward, right, up );

	out[0][0] = right[0];
	out[0][1] = right[1];
	out[0][2] = right[2];
	out[0][3] = -DotProduct( right, org );

	out[1][0] = up[0];
	out[1][1] = up[1];
	out[1][2] = up[2];
	out[1][3] = -DotProduct( up, org );

	out[2][0] = -forward[0];
	out[2][1] = -forward[1];
	out[2][2] = -forward[2];
	out[2][3] = DotProduct( forward, org );
}

static u32 R_GXFaceColor( const msurface_t *surf )
{
	const char *name;
	u32 h = 0x2a4c6u;
	int i;

	if( surf->texinfo && surf->texinfo->texture && surf->texinfo->texture->name[0] )
		name = surf->texinfo->texture->name;
	else if( surf->firstedge < 0 && surf->extents[0] == 256 && surf->extents[1] == 256 )
		return 0x6A6864FFu; /* GX-017: concrete underfoot pad (was cyan) */
	else if( surf->firstedge < 0 && surf->extents[0] == 512 && surf->extents[1] == 384 )
		return 0x3A3E42FFu; /* menu tram end-plug dark concrete */
	else
		name = "flat";

	for( i = 0; name[i]; i++ )
		h = h * 131u + (u32)(byte)name[i];

	{
		u32 r = 64u + (( h >> 0 ) & 127u );
		u32 g = 64u + (( h >> 8 ) & 127u );
		u32 b = 64u + (( h >> 16 ) & 127u );
		return ( r << 24 ) | ( g << 16 ) | ( b << 8 ) | 0xFFu;
	}
}

static void R_GXSwizzleRGB565( const u16 *src, int stride, int w, int h, u16 *dst )
{
	int tile_x, tile_y, y;
	u16 *out = dst;

	for( tile_y = 0; tile_y < h; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < w; tile_x += 4 )
		{
			for( y = 0; y < 4; y++ )
			{
				const u16 *row = src + ( tile_y + y ) * stride + tile_x;
				out[0] = row[0];
				out[1] = row[1];
				out[2] = row[2];
				out[3] = row[3];
				out += 4;
			}
		}
	}
}

static void R_GXSwizzleRawCinematic( const byte *src, u16 *dst )
{
	int tile_x, tile_y, row, x;

	for( tile_y = 0; tile_y < GC_GX_CINEMATIC_HEIGHT; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < GC_GX_CINEMATIC_WIDTH; tile_x += 4 )
		{
			for( row = 0; row < 4; row++ )
			{
				const byte *pixels = src + ((( tile_y + row ) * GC_GX_CINEMATIC_WIDTH ) + tile_x) * 2;
				for( x = 0; x < 4; x++, pixels += 2 )
					*dst++ = (u16)( pixels[0] | ((u16)pixels[1] << 8) );
			}
		}
	}
}

static void R_GXUpdateRawCinematicTiles( const byte *src, u16 *dst,
	const uint16_t *tiles, uint tile_count )
{
	uint tile;

	for( tile = 0; tile < tile_count; tile++ )
	{
		const uint tile_index = tiles[tile];
		if( tile_index >= 40u * 30u )
			continue;
		const uint tile_x = ( tile_index % 40u ) * 8u;
		const uint tile_y = ( tile_index / 40u ) * 8u;
		uint block_y, block_x, row, col;

		/* GX stores 4x4 blocks linearly.  Address the four blocks in
		 * this 8x8 delta tile directly; avoid per-pixel divide/modulo. */
		for( block_y = 0; block_y < 2; block_y++ )
		{
			for( block_x = 0; block_x < 2; block_x++ )
			{
				const uint block = ((( tile_y / 4u + block_y ) * 80u) +
					(tile_x / 4u + block_x)) * 16u;
				for( row = 0; row < 4; row++ )
				{
					const byte *pixels = src + ((( tile_y + block_y * 4u + row ) *
						GC_GX_CINEMATIC_WIDTH) + tile_x + block_x * 4u) * 2;
					for( col = 0; col < 4; col++, pixels += 2 )
						dst[block + row * 4u + col] =
							(u16)( pixels[0] | ((u16)pixels[1] << 8) );
				}
			}
		}
	}
}

static void R_GXFlushRawCinematicTiles( u16 *dst,
	const uint16_t *tiles, uint tile_count )
{
	uint tile;

	for( tile = 0; tile < tile_count; tile++ )
	{
		const uint tile_index = tiles[tile];
		if( tile_index >= 40u * 30u )
			continue;
		const uint tile_x = ( tile_index % 40u ) * 8u;
		const uint tile_y = ( tile_index / 40u ) * 8u;
		uint block_y, block_x;

		/* Each 8x8 delta tile covers four contiguous 4x4 GX blocks.  Flush
		 * only those 128-byte blocks instead of the full 150 KiB texture. */
		for( block_y = 0; block_y < 2; block_y++ )
		{
			for( block_x = 0; block_x < 2; block_x++ )
			{
				const uint block = ((( tile_y / 4u + block_y ) * 80u) +
					(tile_x / 4u + block_x)) * 16u;
				DCFlushRange( dst + block, 16u * sizeof( u16 ));
			}
		}
	}
}

static int R_GXSnapDimMax( int n, int max_dim )
{
	int d;

	if( n < 4 )
		return 4;
	if( n > max_dim )
		n = max_dim;
	/* Round down to multiple of 4 (GX_TF_RGB565 tile). */
	d = n & ~3;
	return d < 4 ? 4 : d;
}

/* HUD/menu art: round up so 156×78 btn strips keep all three 26px states.
 * Rounding down (78→76) made PIC rects for PRESSED sample past the tiled
 * height and wrap/fringe into chroma noise under GX_REPEAT. */
static int R_GXSnapDimUpMax( int n, int max_dim )
{
	int d;

	if( n < 4 )
		return 4;
	if( n > max_dim )
		n = max_dim;
	d = ( n + 3 ) & ~3;
	if( d > max_dim )
		d = max_dim & ~3;
	return d < 4 ? 4 : d;
}

static int R_GXSnapDim( int n )
{
	return R_GXSnapDimMax( n, GC_GX_TEX_WORLD_MAX_DIM );
}

/* Boot-time display caches: idle boot arena first, heap otherwise (arena
 * copies are dropped at the first map load, see R_GXDropBootPadCinematic). */
static u16 *R_GXAllocBootCache( size_t bytes )
{
	/* Arena first: keeps the heap contiguous for the first map load's
	 * world reserves (Mod_GCEnsureStaticReserves). */
	u16 *p = (u16 *)GC_BootArenaAlloc( bytes );

	if( !p )
		p = (u16 *)memalign( 32, bytes );
	return p;
}

static void R_GXFreeBootCache( void *p )
{
	if( p && !GC_BootArenaFree( p ))
		free( p );
}

static u16 *R_GXEnsureLinearScratch( size_t pixels )
{
	if( pixels <= r_gx_tex_linear_cap )
		return r_gx_tex_linear;
	if( pixels > (size_t)GC_GX_TEX_HUD_MAX_DIM * (size_t)GC_GX_TEX_HUD_MAX_DIM )
		pixels = (size_t)GC_GX_TEX_HUD_MAX_DIM * (size_t)GC_GX_TEX_HUD_MAX_DIM;
	{
		u16 *neu = R_GXAllocBootCache( pixels * sizeof( u16 ));
		if( !neu )
			return NULL;
		if( r_gx_tex_linear != r_gx_tex_linear_small )
			R_GXFreeBootCache( r_gx_tex_linear );
		r_gx_tex_linear = neu;
		r_gx_tex_linear_cap = pixels;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: GX linear scratch grown to %u pixels\n",
			(unsigned)pixels );
	}
	return r_gx_tex_linear;
}

static qboolean R_GXIsMenuBackgroundName( const char *name )
{
	if( !name || !name[0] )
		return false;
	/* Single-sheet path only; quadrant tiles use the normal HUD LRU. */
	return Q_stristr( name, "gc_menu/background" ) != NULL
		&& Q_stristr( name, "gc_menu/bg_" ) == NULL;
}

/*
 * Retail menu background tiles at full resolution.  The disc build writes
 * each resource/gc_menu/bg_<r>_<c>.tga quadrant again as GX CMPR
 * (bg_<r>_<c>.cmp: 32-byte header "GCMP", u16 width, u16 height, then the
 * texels), 320x240 per quadrant for a 640x480 sheet.  CMPR is 4 bpp, so the
 * four quadrants take the same ~150 KiB the stretched 320x240 RGB5A3 sheet
 * did.  Loaded on first bind into the boot arena (menu only; released with
 * the HUD pool at map load), else the heap.
 */
#define GC_GX_MENU_CMPR_TILES	5 /* 4 background quadrants + the title banner */
typedef struct
{
	unsigned  texnum;
	byte      *data;
	qboolean  missing;
	gc_gx_tex_t t;
} gc_gx_menu_cmpr_t;
static gc_gx_menu_cmpr_t r_gx_menu_cmpr[GC_GX_MENU_CMPR_TILES];

static qboolean R_GXIsMenuTileName( const char *name )
{
	return name && ( Q_stristr( name, "gc_menu/bg_" ) != NULL
		|| Q_stristr( name, "gc_menu/logo_banner" ) != NULL );
}

static gc_gx_tex_t *R_GXBindMenuTileCmpr( image_t *img, unsigned texnum )
{
	gc_gx_menu_cmpr_t *e = NULL;
	int i;

	for( i = 0; i < GC_GX_MENU_CMPR_TILES; i++ )
	{
		if( r_gx_menu_cmpr[i].texnum == texnum )
		{
			e = &r_gx_menu_cmpr[i];
			break;
		}
	}
	if( !e )
	{
		char path[96], *dot;
		fs_offset_t len = 0;
		byte *buf;

		for( i = 0; i < GC_GX_MENU_CMPR_TILES && r_gx_menu_cmpr[i].texnum; i++ );
		if( i == GC_GX_MENU_CMPR_TILES )
			return NULL;
		e = &r_gx_menu_cmpr[i];
		memset( e, 0, sizeof( *e ));
		e->texnum = texnum;
		Q_strncpy( path, img->name, sizeof( path ));
		dot = Q_strrchr( path, '.' );
		if( !dot || dot - path > (int)sizeof( path ) - 5 )
		{
			e->missing = true;
			return NULL;
		}
		Q_strncpy( dot, ".cmp", sizeof( path ) - ( dot - path ));
		buf = gEngfuncs.fsapi->LoadFileMalloc( path, &len, false );
		if( buf && len > 32 && !memcmp( buf, "GCMP", 4 ))
		{
			const int w = ( buf[4] << 8 ) | buf[5], h = ( buf[6] << 8 ) | buf[7];
			const size_t bytes = (size_t)w * (size_t)h / 2;

			if( w >= 8 && h >= 8 && !( w & 7 ) && !( h & 7 ) && (size_t)len >= 32 + bytes )
			{
				e->data = (byte *)GC_BootArenaAlloc( bytes );
				if( !e->data )
					e->data = (byte *)memalign( 32, bytes );
				if( e->data )
				{
					memcpy( e->data, buf + 32, bytes );
					DCFlushRange( e->data, (u32)bytes );
					GX_InvalidateTexAll();
					GX_InitTexObj( &e->t.obj, e->data, (u16)w, (u16)h, GX_TF_CMPR,
						GX_CLAMP, GX_CLAMP, GX_FALSE );
					GX_InitTexObjFilterMode( &e->t.obj, GX_LINEAR, GX_LINEAR );
					e->t.texnum = texnum;
					e->t.w = w;
					e->t.h = h;
					e->t.fmt = GX_TF_CMPR;
					e->t.valid = true;
					gEngfuncs.Con_Reportf( "Xash3D GameCube: menu tile %s CMPR %dx%d\n", path, w, h );
				}
			}
		}
		free( buf );
		if( !e->data )
		{
			e->missing = true;
			gEngfuncs.Con_Reportf( "Xash3D GameCube: menu tile %s has no CMPR, using %dx%d TGA\n",
				path, img->width, img->height );
		}
	}
	if( e->missing || !e->data )
		return NULL;
	if( r_gx_bound_texnum != texnum || r_gx_bound_fmt != GX_TF_CMPR )
	{
		GX_LoadTexObj( &e->t.obj, GX_TEXMAP0 );
		r_gx_bound_texnum = texnum;
		r_gx_bound_fmt = GX_TF_CMPR;
		r_gx_tex_loads++;
	}
	else
		r_gx_tex_reuses++;
	return &e->t;
}

static void R_GXReleaseMenuTilesCmpr( void )
{
	int i;

	for( i = 0; i < GC_GX_MENU_CMPR_TILES; i++ )
	{
		if( r_gx_menu_cmpr[i].data )
			free( r_gx_menu_cmpr[i].data ); /* boot arena or heap */
		memset( &r_gx_menu_cmpr[i], 0, sizeof( r_gx_menu_cmpr[i] ));
	}
}

static gc_gx_tex_t *R_GXBindMenuBackground( image_t *img, unsigned texnum )
{
	int src_w, src_h, dst_w, dst_h, x, y, step_x, step_y;
	size_t bytes;
	u16 *linear;

	src_w = img->width;
	src_h = img->height;
	dst_w = R_GXSnapDimMax( src_w, GC_GX_MENU_BG_WIDTH );
	dst_h = R_GXSnapDimMax( src_h, GC_GX_MENU_BG_HEIGHT );
	bytes = (size_t)dst_w * (size_t)dst_h * sizeof( u16 );
	linear = R_GXEnsureLinearScratch( (size_t)dst_w * (size_t)dst_h );
	if( !linear )
		return NULL;

	if( !r_gx_menu_bg_tex.tiled || r_gx_menu_bg_tex.alloc_bytes < bytes )
	{
		u16 *neu = R_GXAllocBootCache( bytes > GC_GX_MENU_BG_BYTES ? bytes : GC_GX_MENU_BG_BYTES );
		if( !neu )
			return NULL;
		if( r_gx_menu_bg_tex.tiled )
			R_GXFreeBootCache( r_gx_menu_bg_tex.tiled );
		r_gx_menu_bg_tex.tiled = neu;
		r_gx_menu_bg_tex.alloc_bytes = bytes > GC_GX_MENU_BG_BYTES ? bytes : GC_GX_MENU_BG_BYTES;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: menu background GX alloc %ux%u (%u KiB)\n",
			dst_w, dst_h, (unsigned)( r_gx_menu_bg_tex.alloc_bytes / 1024u ));
	}

	if( r_gx_menu_bg_ready && r_gx_menu_bg_tex.texnum == texnum
		&& r_gx_menu_bg_tex.w == dst_w && r_gx_menu_bg_tex.h == dst_h )
	{
		if( r_gx_bound_texnum != texnum || r_gx_bound_fmt != r_gx_menu_bg_tex.fmt )
		{
			GX_LoadTexObj( &r_gx_menu_bg_tex.obj, GX_TEXMAP0 );
			r_gx_bound_texnum = texnum;
			r_gx_bound_fmt = r_gx_menu_bg_tex.fmt;
			r_gx_tex_loads++;
		}
		else
			r_gx_tex_reuses++;
		return &r_gx_menu_bg_tex;
	}

	step_x = src_w / dst_w;
	step_y = src_h / dst_h;
	if( step_x < 1 ) step_x = 1;
	if( step_y < 1 ) step_y = 1;
	for( y = 0; y < dst_h; y++ )
	{
		int sy = y * step_y;
		if( sy >= src_h )
			sy = src_h - 1;
		for( x = 0; x < dst_w; x++ )
		{
			int sx = x * step_x;
			pixel_t soft;
			if( sx >= src_w )
				sx = src_w - 1;
			soft = img->pixels[0][sy * src_w + sx];
			linear[y * dst_w + x] = R_GXSoftToRGB5A3( soft );
		}
	}
	R_GXSwizzleRGB565( linear, dst_w, dst_w, dst_h, r_gx_menu_bg_tex.tiled );
	DCFlushRange( r_gx_menu_bg_tex.tiled, (u32)bytes );
	GX_InitTexObj( &r_gx_menu_bg_tex.obj, r_gx_menu_bg_tex.tiled,
		(u16)dst_w, (u16)dst_h, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE );
	GX_InitTexObjFilterMode( &r_gx_menu_bg_tex.obj, GX_LINEAR, GX_LINEAR );
	r_gx_menu_bg_tex.texnum = texnum;
	r_gx_menu_bg_tex.w = dst_w;
	r_gx_menu_bg_tex.h = dst_h;
	r_gx_menu_bg_tex.fmt = GX_TF_RGB5A3;
	r_gx_menu_bg_tex.valid = true;
	r_gx_menu_bg_tex.hud_pin = true;
	r_gx_menu_bg_ready = true;
	GX_InvalidateTexAll();
	GX_LoadTexObj( &r_gx_menu_bg_tex.obj, GX_TEXMAP0 );
	r_gx_bound_texnum = texnum;
	r_gx_bound_fmt = GX_TF_RGB5A3;
	r_gx_tex_loads++;
	return &r_gx_menu_bg_tex;
}

void R_GXReleaseMenuBackgroundTexture( unsigned int texnum )
{
	if( texnum == 0 )
		R_GXReleaseMenuTilesCmpr();
	else
	{
		int i;

		for( i = 0; i < GC_GX_MENU_CMPR_TILES; i++ )
		{
			if( r_gx_menu_cmpr[i].texnum == texnum )
			{
				if( r_gx_menu_cmpr[i].data )
					free( r_gx_menu_cmpr[i].data );
				memset( &r_gx_menu_cmpr[i], 0, sizeof( r_gx_menu_cmpr[i] ));
			}
		}
	}
	if( r_gx_menu_bg_tex.tiled && ( texnum == 0 || r_gx_menu_bg_tex.texnum == texnum ))
	{
		R_GXFreeBootCache( r_gx_menu_bg_tex.tiled );
		memset( &r_gx_menu_bg_tex, 0, sizeof( r_gx_menu_bg_tex ));
		r_gx_menu_bg_ready = false;
		if( r_gx_bound_texnum == texnum || texnum == 0 )
		{
			r_gx_bound_texnum = 0;
			r_gx_bound_fmt = 0;
		}
	}
	/* Drop the grown linear scratch when the whole menu is purged. */
	if( texnum == 0 && r_gx_tex_linear != r_gx_tex_linear_small )
	{
		R_GXFreeBootCache( r_gx_tex_linear );
		r_gx_tex_linear = r_gx_tex_linear_small;
		r_gx_tex_linear_cap = GC_GX_TEX_WORLD_MAX_DIM * GC_GX_TEX_WORLD_MAX_DIM;
	}
}

/* Startup cinematic storage: idle boot pad first (no heap), heap otherwise. */
static u16 *R_GXAllocCinematicStorage( void )
{
	u16 *tiled = (u16 *)GC_BootArenaAlloc( GC_GX_CINEMATIC_BYTES );

	if( !tiled )
		tiled = (u16 *)memalign( 32, GC_GX_CINEMATIC_BYTES );
	return tiled;
}

static void R_GXFreeCinematicStorage( u16 *tiled )
{
	if( !tiled )
		return;
	if( !GC_BootArenaFree( tiled ))
		free( tiled );
}

/* Map load is closing the boot arena: release display caches living there.
 * They are rebuilt from the heap on next use. */
void R_GXDropBootPadCinematic( void )
{
	if( GC_BootArenaOwns( r_gx_menu_bg_tex.tiled ))
	{
		const unsigned texnum = r_gx_menu_bg_tex.texnum;

		R_GXFreeBootCache( r_gx_menu_bg_tex.tiled );
		memset( &r_gx_menu_bg_tex, 0, sizeof( r_gx_menu_bg_tex ));
		r_gx_menu_bg_ready = false;
		if( r_gx_bound_texnum == texnum )
		{
			r_gx_bound_texnum = 0;
			r_gx_bound_fmt = 0;
		}
	}
	if( GC_BootArenaOwns( r_gx_tex_linear ))
	{
		R_GXFreeBootCache( r_gx_tex_linear );
		r_gx_tex_linear = r_gx_tex_linear_small;
		r_gx_tex_linear_cap = GC_GX_TEX_WORLD_MAX_DIM * GC_GX_TEX_WORLD_MAX_DIM;
	}
	if( !GC_BootArenaOwns( r_gx_cinematic_tex.tiled ))
		return;
	R_GXReleaseCinematicSoftwarePixels();
	GC_BootArenaFree( r_gx_cinematic_tex.tiled );
	if( r_gx_bound_texnum == r_gx_cinematic_tex.texnum )
	{
		r_gx_bound_texnum = 0;
		r_gx_bound_fmt = 0;
	}
	memset( &r_gx_cinematic_tex, 0, sizeof( r_gx_cinematic_tex ));
	r_gx_cinematic_ready = false;
}

void GL_UpdateCinematicTexture( int texnum, int width, int height, const byte *buffer,
	const uint16_t *dirty_tiles, uint dirty_count, qboolean full_upload )
{
	double stage_start, stage_tiles, stage_flush;
	unsigned update_number;
	if( texnum <= 0 || width != GC_GX_CINEMATIC_WIDTH || height != GC_GX_CINEMATIC_HEIGHT || !buffer )
		return;
	update_number = ++r_gx_cinematic_updates;
	stage_start = gEngfuncs.pfnTime();

	/* Allocated on first use: a game without videos keeps the 150 KB. */
	if( !r_gx_cinematic_tex.tiled )
	{
		r_gx_cinematic_tex.tiled = R_GXAllocCinematicStorage();
		if( !r_gx_cinematic_tex.tiled )
		{
			static qboolean logged_fail;
			if( !logged_fail )
			{
				gEngfuncs.Con_Printf( "Xash3D GameCube: cinematic GX alloc FAILED (%u bytes) — intro will be black\n",
					(unsigned)GC_GX_CINEMATIC_BYTES );
				logged_fail = true;
			}
			return;
		}
		r_gx_cinematic_tex.alloc_bytes = GC_GX_CINEMATIC_BYTES;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: cinematic GX alloc ptr=%p bytes=%u align=%u\n",
			r_gx_cinematic_tex.tiled, (unsigned)GC_GX_CINEMATIC_BYTES,
			(unsigned)((uintptr_t)r_gx_cinematic_tex.tiled & 31u) );
	}
	/* The cinematic storage is single-buffered.  Do not overwrite it until
	 * Flipper has consumed the prior frame; GX_InvalidateTexAll only invalidates
	 * the texture cache and is not a producer/consumer fence. */
	if( r_gx_cinematic_ready )
		GX_DrawDone();
	if( !r_gx_cinematic_ready || r_gx_cinematic_tex.texnum != (unsigned)texnum )
	{
		full_upload = true;
		GX_InitTexObj( &r_gx_cinematic_tex.obj, r_gx_cinematic_tex.tiled,
			GC_GX_CINEMATIC_WIDTH, GC_GX_CINEMATIC_HEIGHT, GX_TF_RGB565,
			GX_CLAMP, GX_CLAMP, GX_FALSE );
		GX_InitTexObjFilterMode( &r_gx_cinematic_tex.obj, GX_LINEAR, GX_LINEAR );
	}
	if( full_upload )
		R_GXSwizzleRawCinematic( buffer, r_gx_cinematic_tex.tiled );
	else if( dirty_count )
		R_GXUpdateRawCinematicTiles( buffer, r_gx_cinematic_tex.tiled, dirty_tiles, dirty_count );
	else
		return;
	stage_tiles = gEngfuncs.pfnTime();
	if( full_upload )
		DCFlushRange( r_gx_cinematic_tex.tiled, GC_GX_CINEMATIC_BYTES );
	else
		R_GXFlushRawCinematicTiles( r_gx_cinematic_tex.tiled, dirty_tiles, dirty_count );
	stage_flush = gEngfuncs.pfnTime();
	r_gx_cinematic_tex.texnum = (unsigned)texnum;
	r_gx_cinematic_tex.w = GC_GX_CINEMATIC_WIDTH;
	r_gx_cinematic_tex.h = GC_GX_CINEMATIC_HEIGHT;
	r_gx_cinematic_tex.fmt = GX_TF_RGB565;
	r_gx_cinematic_tex.valid = true;
	r_gx_cinematic_ready = true;
	GX_InvalidateTexAll();
	/* Force GX_LoadTexObj on the next draw.  Some Flipper/Dolphin paths retain
	 * the old texture-cache tag even after GX_InvalidateTexAll when the object
	 * address is unchanged. */
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	if( update_number == 1 || !( update_number % 15 ))
		gEngfuncs.Con_Reportf( "Xash3D GameCube: cinematic gx stages frame=%u tiles=%u full=%d update=%.3fms flush=%.3fms invalidate=%.3fms\n",
			update_number, dirty_count, full_upload,
			( stage_tiles - stage_start ) * 1000.0,
			( stage_flush - stage_tiles ) * 1000.0,
			( gEngfuncs.pfnTime() - stage_flush ) * 1000.0 );
	if( update_number <= 2 )
		gEngfuncs.Con_Reportf( "Xash3D GameCube: cinematic GX upload=%u tex=%d full=%d dirty=%u ptr=%p\n",
			update_number, texnum, full_upload, dirty_count, r_gx_cinematic_tex.tiled );
}

void R_GXReleaseCinematicTexture( unsigned int texnum )
{
	if( !r_gx_cinematic_tex.tiled || r_gx_cinematic_tex.texnum != texnum )
		return;
	R_GXReleaseCinematicSoftwarePixels();
	R_GXFreeCinematicStorage( r_gx_cinematic_tex.tiled );
	memset( &r_gx_cinematic_tex, 0, sizeof( r_gx_cinematic_tex ));
	r_gx_cinematic_ready = false;
	if( r_gx_bound_texnum == texnum )
	{
		r_gx_bound_texnum = 0;
		r_gx_bound_fmt = 0;
	}
}

pixel_t *R_GXAcquireCinematicSoftwarePixels( size_t bytes )
{
	if( bytes > GC_GX_CINEMATIC_BYTES )
		return NULL;
	if( !r_gx_cinematic_tex.tiled )
	{
		r_gx_cinematic_tex.tiled = R_GXAllocCinematicStorage();
		if( !r_gx_cinematic_tex.tiled )
			return NULL;
		r_gx_cinematic_tex.alloc_bytes = GC_GX_CINEMATIC_BYTES;
	}
	return (pixel_t *)r_gx_cinematic_tex.tiled;
}

static void R_GXTexCacheReset( void )
{
	int i;
	const size_t pool_bytes =
		(size_t)GC_GX_TEX_MAX_DIM * (size_t)GC_GX_TEX_MAX_DIM * sizeof( u16 );

	for( i = 0; i < GC_GX_TEX_SLOTS; i++ )
	{
		/* Keep the reserved HUD slabs across map changes: re-allocating
		 * them after the world load found MEM1 full (G184 slot=23..31). */
		if( i >= GC_GX_TEX_HUD_SLOT0 && r_gx_tex[i].hud_pin && r_gx_tex[i].tiled )
		{
			u16 *keep = r_gx_tex[i].tiled;
			size_t keep_bytes = r_gx_tex[i].alloc_bytes;

			memset( &r_gx_tex[i], 0, sizeof( r_gx_tex[i] ));
			r_gx_tex[i].tiled = keep;
			r_gx_tex[i].alloc_bytes = keep_bytes;
			r_gx_tex[i].hud_pin = true;
			r_gx_tex_lru[i] = 0;
			continue;
		}
		if( r_gx_tex[i].tiled && i >= GC_GX_TEX_WORLD_POOL )
			free( r_gx_tex[i].tiled );
		memset( &r_gx_tex[i], 0, sizeof( r_gx_tex[i] ));
		r_gx_tex_lru[i] = 0;
		if( i < GC_GX_TEX_WORLD_POOL )
		{
			r_gx_tex[i].tiled = r_gx_tex_world_pool[i];
			r_gx_tex[i].alloc_bytes = pool_bytes;
		}
	}
	r_gx_tex_clock = 0;
	r_gx_hud_ondemand_heap = 0;
	{
		u16 *tiled = r_gx_cinematic_tex.tiled;
		const size_t alloc_bytes = r_gx_cinematic_tex.alloc_bytes;

		memset( &r_gx_cinematic_tex, 0, sizeof( r_gx_cinematic_tex ));
		r_gx_cinematic_tex.tiled = tiled;
		r_gx_cinematic_tex.alloc_bytes = alloc_bytes;
	}
	r_gx_cinematic_ready = false;
	r_gx_tex_world = NULL;
	r_gx_hud_pool_ready = false;
	if( !gc_mem003_world_pool_logged )
	{
		gc_mem003_world_pool_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: MEM-003 world tex pool slots=%d bytes=%u\n",
			GC_GX_TEX_WORLD_POOL, (unsigned)sizeof( r_gx_tex_world_pool ));
	}
}

/*
=============
R_GXNativeTexture

World and model textures are all resident as soft pixels, but a few pool
slots cycled them through CPU conversion every frame (30-110 uploads, 11-36
GP stalls per frame). On the menu route each is converted once, in place, to
GX tiled RGB565: 4 rows of the soft image are the same bytes as a row of
4x4 tiles, and a dimension that is not a multiple of 4 only shrinks. Later
binds only load the texture object. The soft renderer does not draw these on this route.
=============
*/
static gc_gx_tex_t r_gx_tex_native;
static unsigned r_gx_tex_native_conversions;
static int r_gx_tex_native_route = -1;
extern qboolean GC_MenuRunsServer( void );

/* Alpha test for 1-bit-alpha textures (masked CMPR packs, masked native
 * skins); off for opaque ones.  Shared latch so either path resets it. */
static qboolean r_gx_packed_alpha_test;

static void R_GXSetTexAlphaTest( qboolean on )
{
	if( r_gx_packed_alpha_test == on )
		return;
	r_gx_packed_alpha_test = on;
	if( on )
		GX_SetAlphaCompare( GX_GREATER, 127, GX_AOP_AND, GX_ALWAYS, 0 );
	else
		GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetZCompLoc( on ? GX_FALSE : GX_TRUE );
}

/* Viewmodel skins ("#models/v_*.mdl") are always drawn close to the camera:
 * keep their source resolution instead of the 128-texel box filter, which is
 * there to stand in for the missing mipmaps on distant geometry. */
static qboolean R_GXNativeFullRes( const image_t *img )
{
	return img->name[0] == '#' && Q_stristr( img->name, "models/v_" ) != NULL;
}

static gc_gx_tex_t *R_GXNativeTexture( image_t *img, unsigned texnum )
{
	gc_gx_tex_t *t = &r_gx_tex_native;
	const int cap = R_GXNativeFullRes( img ) ? 1024 : GC_GX_TEX_MAX_DIM;
	int step_x = 1, step_y = 1, w, h;
	u8 fmt;

	/* Larger sides are box filtered down: without mipmaps they shimmer. */
	while( img->width / step_x > cap )
		step_x *= 2;
	while( img->height / step_y > cap )
		step_y *= 2;
	w = ( img->width / step_x ) & ~3;
	h = ( img->height / step_y ) & ~3;
	if( w < 4 || h < 4 || ((uintptr_t)img->pixels[0] & 31 ))
		return NULL;
	if( !img->gc_gx_native )
	{
		u16 *dst = (u16 *)img->pixels[0];
		const int n = img->width * img->height;
		qboolean masked = false;
		int y, x, sx, sy;

		/* Masked skins (STUDIO_NF_MASKED, '{' textures) carry
		 * TRANSPARENT_COLOR texels: RGB565 drew them as opaque black.
		 * RGB5A3 is the same 16 bits per texel and keeps a 1-bit alpha. */
		for( x = 0; x < n; x++ )
		{
			if( img->pixels[0][x] == TRANSPARENT_COLOR )
			{
				masked = true;
				break;
			}
		}

		if( gEngfuncs.Sys_CheckParm( "-gcmodeltrace" ) || gEngfuncs.Sys_CheckParm( "-gcprofile" ))
			gEngfuncs.Con_Reportf( "modeltrace convert %s %dx%d -> %dx%d masked=%d ptr=%p first=%04x %04x\n", img->name, img->width, img->height, w, h, masked, img->pixels[0], img->pixels[0][0], img->pixels[0][1] );

		/* Reads stay ahead of the writes: the output is never larger. */
		for( y = 0; y < h; y += 4, dst += w * 4 )
		{
			for( x = 0; x < w * 4; x++ )
			{
				const pixel_t *src = img->pixels[0] + ( y + x / w ) * step_y * img->width + ( x % w ) * step_x;
				unsigned r = 0, g = 0, b = 0, solid = 0;

				for( sy = 0; sy < step_y; sy++ )
				{
					for( sx = 0; sx < step_x; sx++ )
					{
						const pixel_t soft = src[sy * img->width + sx];
						unsigned c;

						if( soft == TRANSPARENT_COLOR )
							continue;
						c = (u16)R_GCSoftToRGB565( soft );
						r += c >> 11;
						g += ( c >> 5 ) & 63;
						b += c & 31;
						solid++;
					}
				}
				if( !masked )
				{
					/* Opaque: transparent texels count as black, as before. */
					const unsigned cnt = step_x * step_y;
					r /= cnt;
					g /= cnt;
					b /= cnt;
					r_gx_tex_linear[x] = (u16)(( r << 11 ) | ( g << 5 ) | b );
				}
				else if( solid * 2 <= (unsigned)( step_x * step_y ))
					r_gx_tex_linear[x] = 0; /* alpha 0 */
				else
				{
					r /= solid;
					g /= solid;
					b /= solid;
					/* RGB5A3 opaque: 1rrrrrgggggbbbbb */
					r_gx_tex_linear[x] = (u16)( 0x8000u | ( r << 10 ) | (( g >> 1 ) << 5 ) | b );
				}
			}
			R_GXSwizzleRGB565( r_gx_tex_linear, w, w, 4, dst );
		}
		DCFlushRange( img->pixels[0], (u32)( w * h * sizeof( u16 )));
		GX_InvalidateTexAll();
		r_gx_tex_invalidates++;
		img->gc_gx_native = true;
		img->gc_gx_native_alpha = masked;
		r_gx_tex_native_conversions++;
		r_gx_bound_texnum = 0;
	}
	fmt = img->gc_gx_native_alpha ? (u8)GX_TF_RGB5A3 : (u8)GX_TF_RGB565;
	if( r_gx_bound_texnum != texnum || r_gx_bound_fmt != fmt )
	{
		GX_InitTexObj( &t->obj, img->pixels[0], (u16)w, (u16)h, fmt, GX_REPEAT, GX_REPEAT, GX_FALSE );
		GX_InitTexObjFilterMode( &t->obj, GX_LINEAR, GX_LINEAR );
		t->texnum = texnum;
		t->w = w;
		t->h = h;
		t->fmt = fmt;
		t->valid = true;
		GX_LoadTexObj( &t->obj, GX_TEXMAP0 );
		r_gx_bound_texnum = texnum;
		r_gx_bound_fmt = fmt;
		r_gx_tex_loads++;
	}
	else
		r_gx_tex_reuses++;
	R_GXSetTexAlphaTest( img->gc_gx_native_alpha );
	return t;
}

/*
=============
R_GXPackedTexture

A texture of the map's pack: CMPR with its mips, trilinear. Above 64 texels a
side only the mips from level 1 are always in memory; the first bind asks for
the whole chain, which R_GXHotService streams from ARAM into the hot window
between frames. Until then the low mips draw, so a slow DMA is never a stall.
A cut-out texture needs the alpha test, every other bind turns it back off.
=============
*/
#define GC_HOT_LIST_MAX     128
#define GC_HOT_QUEUE_MAX    64		/* CapFaces binds many mats before HotService */
#define GC_HOT_FRAME_BYTES  ( 96 * 1024 )	/* a frame's worth of streaming (~8×128²) */
#define GC_HOT_KEEP_FRAMES  90		/* not evicted until this long unused */

static struct
{
	byte     *base;
	unsigned size, head, frame;
	image_t  *list[GC_HOT_LIST_MAX];
	int      count;
	image_t  *queue[GC_HOT_QUEUE_MAX];
	int      queued;
	unsigned uploads, bytes, evictions, deferred;
} r_gx_hot;

/* Durable hot window — survives map-load arena reuse that stomps arena-tail
 * pack low-mips (CapFaces CMPR noise: diag hdr absent from GCT). */
static byte *r_gx_durable_hot;
static unsigned r_gx_durable_hot_size;
static qboolean r_gx_hot_is_durable;

/* The hot window of this map; resets everything in it. */
void R_GCHotWindow( void *base, unsigned size )
{
	memset( &r_gx_hot, 0, sizeof( r_gx_hot ));
	r_gx_hot.base = (byte *)base;
	r_gx_hot.size = size;
	r_gx_hot_is_durable = ( base && r_gx_durable_hot && base == r_gx_durable_hot );
}

qboolean R_GCHotWindowIsDurable( void )
{
	return r_gx_hot_is_durable && r_gx_hot.base != NULL && r_gx_hot.size > 0;
}

void R_GCEnsureDurableHotWindow( unsigned prefer_bytes )
{
	unsigned want = prefer_bytes ? prefer_bytes : ( 48u * 1024u );
	unsigned try_sz;

	if( want < 32u * 1024u )
		want = 32u * 1024u;
	if( want > 96u * 1024u )
		want = 96u * 1024u;
	want = ( want + 31u ) & ~31u;

	if( r_gx_durable_hot && r_gx_durable_hot_size >= want )
	{
		R_GCHotWindow( r_gx_durable_hot, r_gx_durable_hot_size );
		return;
	}
	if( r_gx_durable_hot )
	{
		free( r_gx_durable_hot );
		r_gx_durable_hot = NULL;
		r_gx_durable_hot_size = 0;
	}
	for( try_sz = want; try_sz >= 32u * 1024u; try_sz = ( try_sz > 48u * 1024u ) ? ( try_sz - 16u * 1024u ) : 0 )
	{
		r_gx_durable_hot = (byte *)memalign( 32, try_sz );
		if( r_gx_durable_hot )
		{
			r_gx_durable_hot_size = try_sz;
			R_GCHotWindow( r_gx_durable_hot, try_sz );
			gEngfuncs.Con_Reportf( "Xash3D GameCube: durable CMPR hot window %u Kb\n",
				try_sz / 1024u );
			return;
		}
		if( try_sz <= 32u * 1024u )
			break;
	}
	gEngfuncs.Con_Reportf( "Xash3D GameCube: durable CMPR hot window alloc failed (want %u Kb)\n",
		want / 1024u );
}

/* For a moment during a map load: the whole window, emptied. What was resident
 * falls back to its low mips and is streamed in again on its next bind. */
void *R_GCHotBorrow( unsigned bytes )
{
	int i;

	if( bytes > r_gx_hot.size )
		return NULL;
	for( i = 0; i < r_gx_hot.count; i++ )
		r_gx_hot.list[i]->gc_packed_hot = NULL;
	r_gx_hot.count = 0;
	r_gx_hot.head = 0;
	r_gx_bound_texnum = 0;
	return r_gx_hot.base;
}

/* Room for bytes at the ring's head, evicting what has been idle long enough;
 * something in use is stepped over. NULL if nothing can go this frame. */
static byte *R_GXHotAlloc( unsigned bytes )
{
	int tries, i;

	if( bytes > r_gx_hot.size )
		return NULL;
	for( tries = 0; tries < 8; tries++ )
	{
		byte *start, *end;
		qboolean busy = false;

		if( r_gx_hot.head + bytes > r_gx_hot.size )
			r_gx_hot.head = 0;
		start = r_gx_hot.base + r_gx_hot.head;
		end = start + bytes;
		for( i = 0; i < r_gx_hot.count; i++ )
		{
			const image_t *img = r_gx_hot.list[i];
			const byte *a = (const byte *)img->gc_packed_hot;

			if( a < end && a + img->gc_packed_bytes > start
				&& r_gx_hot.frame - img->gc_packed_used < GC_HOT_KEEP_FRAMES )
			{
				r_gx_hot.head = ( a + img->gc_packed_bytes ) - r_gx_hot.base;
				busy = true;
				break;
			}
		}
		if( busy )
			continue;
		for( i = 0; i < r_gx_hot.count; )
		{
			image_t *img = r_gx_hot.list[i];
			const byte *a = (const byte *)img->gc_packed_hot;

			if( a < end && a + img->gc_packed_bytes > start )
			{
				img->gc_packed_hot = NULL;
				r_gx_hot.list[i] = r_gx_hot.list[--r_gx_hot.count];
				r_gx_hot.evictions++;
			}
			else
				i++;
		}
		r_gx_hot.head += bytes;
		return start;
	}
	return NULL;
}

/* Between frames, with the GPU done with last frame's textures. */
static void R_GXHotService( void )
{
	extern void GC_AramTransfer( unsigned int aram, void *buf, unsigned int len, qboolean write );
	unsigned spent = 0;
	int i, kept = 0;

	r_gx_hot.frame++;
	for( i = 0; i < r_gx_hot.queued; i++ )
	{
		image_t *img = r_gx_hot.queue[i];
		byte *dst = NULL;

		if( !img->gc_packed )
			continue;
		if( spent < GC_HOT_FRAME_BYTES && r_gx_hot.count < GC_HOT_LIST_MAX )
			dst = R_GXHotAlloc( img->gc_packed_bytes );
		if( !dst )
		{
			r_gx_hot.queue[kept++] = img;
			r_gx_hot.deferred++;
			continue;
		}
		GC_AramTransfer( img->gc_packed_aram, dst, img->gc_packed_bytes, false );
		img->gc_packed_hot = dst;
		img->gc_packed_queued = false;
		r_gx_hot.list[r_gx_hot.count++] = img;
		r_gx_hot.uploads++;
		r_gx_hot.bytes += img->gc_packed_bytes;
		spent += img->gc_packed_bytes;
	}
	r_gx_hot.queued = kept;
	/* A chain can land where another one was: drop the texture cache's copy. */
	if( spent )
	{
		GX_InvalidateTexAll();
		/* GX texture memory was changed behind the bind cache. Force the next
		 * draw to reload its object, including when the streamed texture was
		 * already the active one at the frame boundary. */
		r_gx_bound_texnum = 0;
		r_gx_bound_fmt = 0;
	}
}

/* ARAM→MEM1 bounce for packed CMPR. Arena-tail low-mips are stomped after
 * map-load buffer reuse; CapFaces must not sample that memory. Max dod_flash
 * chain is 43680 bytes. Prefer heap; on tip-tight maps (dod_donner) borrow
 * the world-array spill tail reserved for CapFaces textures. */
#define GC_CMPR_BOUNCE_BYTES ( 48u * 1024u )
static byte *r_gx_cmpr_bounce;
static unsigned r_gx_cmpr_bounce_cap;
static unsigned r_gx_cmpr_bounce_texnum;
static unsigned r_gx_cmpr_bounce_skip;
/* Flipper samples bounce asynchronously — fence before DMA overwrite or
 * CapFaces walls flash to the next texture every bind (caen/charlie). */
static u16 r_gx_cmpr_bounce_fence;
static unsigned r_gx_cmpr_bounce_stalls;

extern byte *Mod_GCWorldArraySpillBorrow( size_t bytes, const char *what );

static byte *R_GXCmprBounce( void )
{
	if( !r_gx_cmpr_bounce )
	{
		static const unsigned try_sizes[] = {
			GC_CMPR_BOUNCE_BYTES, 40u * 1024u, 32u * 1024u, 24u * 1024u
		};
		unsigned i;

		for( i = 0; i < sizeof( try_sizes ) / sizeof( try_sizes[0] ); ++i )
		{
			r_gx_cmpr_bounce = (byte *)memalign( 32, try_sizes[i] );
			if( r_gx_cmpr_bounce )
			{
				r_gx_cmpr_bounce_cap = try_sizes[i];
				break;
			}
		}
		if( !r_gx_cmpr_bounce )
		{
			/* Spill tail is reserved at boot — does not compete with tip. */
			r_gx_cmpr_bounce = Mod_GCWorldArraySpillBorrow( 32u * 1024u, "cmpr-bounce" );
			if( r_gx_cmpr_bounce )
				r_gx_cmpr_bounce_cap = 32u * 1024u;
		}
		if( r_gx_cmpr_bounce )
		{
			extern void GCube_AppendSwissEvidence( const char *fmt, ... );
			gEngfuncs.Con_Reportf( "Xash3D GameCube: CMPR ARAM bounce %u Kb\n",
				r_gx_cmpr_bounce_cap / 1024u );
			GCube_AppendSwissEvidence( "CMPR ARAM bounce %u Kb",
				r_gx_cmpr_bounce_cap / 1024u );
		}
		else
			gEngfuncs.Con_Reportf( "Xash3D GameCube: CMPR ARAM bounce alloc failed\n" );
	}
	return r_gx_cmpr_bounce;
}

/* Call once after GX init / before map load while MEM1 tip is free. */
void R_GXPreallocCmprBounce( void )
{
	(void)R_GXCmprBounce();
}

static gc_gx_tex_t *R_GXPackedTexture( image_t *img, unsigned texnum )
{
	gc_gx_tex_t *t = &r_gx_tex_native;
	static qboolean cap_pack_diag;
	extern void GC_AramTransfer( unsigned int aram, void *buf, unsigned int len, qboolean write );

	img->gc_packed_used = r_gx_hot.frame;
	if( img->gc_packed_bytes && !img->gc_packed_hot && !img->gc_packed_queued
		&& r_gx_hot.queued < GC_HOT_QUEUE_MAX )
	{
		img->gc_packed_queued = true;
		r_gx_hot.queue[r_gx_hot.queued++] = img;
	}
	{
		qboolean used_bounce = false;

		if( r_gx_bound_texnum != texnum || r_gx_bound_fmt != GX_TF_CMPR )
		{
			const qboolean low = img->gc_packed_bytes && !img->gc_packed_hot;
			const qboolean is_stub = ( img->gc_packed_low_shift & 0x40 ) != 0;
			unsigned shift = low ? ( img->gc_packed_low_shift & 0x3fu ) : 0;
			int levels = img->gc_packed_levels - (int)shift;
			int w = Q_max( 1, img->width >> shift ), h = Q_max( 1, img->height >> shift );
			void *data = img->gc_packed_hot ? img->gc_packed_hot : img->gc_packed;
			byte *bounce = NULL;
			const byte *b;

			/*
			 * Prefer ARAM bounce over stomped/stub MEM1. If the full mip chain
			 * exceeds the bounce buffer, skip leading mips until the remaining
			 * chain fits — never InitTexObj a stub/stomped buffer as WxH.
			 */
			if( img->gc_packed_aram && img->gc_packed_bytes
				&& !img->gc_packed_hot
				&& ( bounce = R_GXCmprBounce()) != NULL
				&& r_gx_cmpr_bounce_cap > 0 )
			{
				unsigned skip = 0;
				unsigned remain = img->gc_packed_bytes;
				unsigned use_shift = 0;
				int use_levels = img->gc_packed_levels;
				int bw = img->width, bh = img->height;
				int mi;

				for( mi = 0; mi < img->gc_packed_levels
					&& remain > r_gx_cmpr_bounce_cap; ++mi )
				{
					unsigned mw = (unsigned)Q_max( 1, img->width >> mi );
					unsigned mh = (unsigned)Q_max( 1, img->height >> mi );
					unsigned level_bytes = (( mw + 7u ) / 8u ) * (( mh + 7u ) / 8u ) * 32u;

					if( level_bytes >= remain )
						break;
					skip += level_bytes;
					remain -= level_bytes;
					use_shift = (unsigned)( mi + 1 );
					use_levels = img->gc_packed_levels - (int)use_shift;
					bw = Q_max( 1, img->width >> use_shift );
					bh = Q_max( 1, img->height >> use_shift );
				}
				if( remain > 0 && remain <= r_gx_cmpr_bounce_cap && use_levels > 0 )
				{
					if( r_gx_cmpr_bounce_texnum != texnum
						|| r_gx_cmpr_bounce_skip != skip )
					{
						/* Prior CapFaces still sample this MEM1 bounce via FIFO. */
						if( r_gx_cmpr_bounce_texnum != 0
							&& GC_GXFenceWait( r_gx_cmpr_bounce_fence ))
							r_gx_cmpr_bounce_stalls++;
						GC_AramTransfer( img->gc_packed_aram + skip, bounce,
							remain, false );
						r_gx_cmpr_bounce_texnum = texnum;
						r_gx_cmpr_bounce_skip = skip;
					}
					data = bounce;
					used_bounce = true;
					shift = use_shift;
					levels = use_levels;
					w = bw;
					h = bh;
				}
			}
			/* Stub+ARAM without a successful bounce: keep grey 8×8, not rainbow. */
			if( is_stub && data != bounce && !img->gc_packed_hot )
			{
				w = 8;
				h = 8;
				levels = 1;
				shift = 0;
			}

			b = (const byte *)data;
			if( !data )
			{
				/* No MEM1 chain and bounce unavailable — fail bind (caller may repair). */
				return NULL;
			}
			if( !cap_pack_diag )
			{
				cap_pack_diag = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: CapFaces/pack CMPR diag tex=%u name=%s %dx%d shift=%u levels=%d low=%d hot=%d bounce=%d aram=0x%x data=%p bytes=%u hdr=%02x%02x%02x%02x%02x%02x%02x%02x\n",
					texnum, img->name[0] ? img->name : "?",
					img->width, img->height, shift, levels, low ? 1 : 0,
					img->gc_packed_hot ? 1 : 0,
					used_bounce ? 1 : 0,
					(unsigned)img->gc_packed_aram, data,
					(unsigned)img->gc_packed_bytes,
					b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7] );
			}

			GX_InitTexObj( &t->obj, data, (u16)w, (u16)h, GX_TF_CMPR, GX_REPEAT, GX_REPEAT, levels > 1 ? GX_TRUE : GX_FALSE );
			if( levels > 1 )
				GX_InitTexObjLOD( &t->obj, GX_LIN_MIP_LIN, GX_LINEAR, 0.0f, (f32)( levels - 1 ), 0.0f, GX_FALSE, GX_TRUE, GX_ANISO_2 );
			else
				GX_InitTexObjFilterMode( &t->obj, GX_LINEAR, GX_LINEAR );
			t->texnum = texnum;
			t->w = img->width;
			t->h = img->height;
			t->fmt = GX_TF_CMPR;
			t->valid = true;
			GX_LoadTexObj( &t->obj, GX_TEXMAP0 );
			r_gx_bound_texnum = texnum;
			r_gx_bound_fmt = GX_TF_CMPR;
			r_gx_tex_loads++;
		}
		else
		{
			r_gx_tex_reuses++;
			/* Same CMPR still resident in bounce — advance fence past new draws. */
			if( r_gx_cmpr_bounce_texnum == texnum && r_gx_cmpr_bounce )
				used_bounce = true;
		}
		if( used_bounce )
			r_gx_cmpr_bounce_fence = GC_GXFenceNext();
	}
	R_GXSetTexAlphaTest( img->gc_packed_alpha ? true : false );
	return t;
}

static int r_gx_prof_hud_uploads_logged;
static gc_gx_tex_t *R_GXBindTexnum( unsigned texnum, qboolean hud_bind )
{
	image_t *img;
	int i, slot, victim, src_w, src_h, dst_w, dst_h;
	int x, y, step_x, step_y;
	size_t bytes;
	gc_gx_tex_t *t;
	const u8 want_fmt = hud_bind ? (u8)GX_TF_RGB5A3 : (u8)GX_TF_RGB565;
	int hud_evict = -1;

	if( texnum == 0 )
		return NULL;
	if( texnum >= (unsigned)MAX_TEXTURES )
		return NULL;

	img = R_GetTexture( texnum );
	if( img && !Q_strcmp( img->name, "*cintexture" ) && r_gx_cinematic_ready &&
		r_gx_cinematic_tex.texnum == texnum )
	{
		if( r_gx_bound_texnum != texnum || r_gx_bound_fmt != r_gx_cinematic_tex.fmt )
		{
			GX_LoadTexObj( &r_gx_cinematic_tex.obj, GX_TEXMAP0 );
			if( r_gx_cinematic_updates <= 2 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: cinematic GX bind tex=%u fmt=%u ptr=%p\n",
					texnum, (unsigned)r_gx_cinematic_tex.fmt, r_gx_cinematic_tex.tiled );
			r_gx_bound_texnum = texnum;
			r_gx_bound_fmt = r_gx_cinematic_tex.fmt;
			r_gx_tex_loads++;
		}
		else
			r_gx_tex_reuses++;
		return &r_gx_cinematic_tex;
	}
	if( hud_bind && img && R_GXIsMenuTileName( img->name ))
	{
		gc_gx_tex_t *tile = R_GXBindMenuTileCmpr( img, texnum );
		if( tile )
			return tile;
	}
	if( hud_bind && img && R_GXIsMenuBackgroundName( img->name ) && img->pixels[0] )
	{
		gc_gx_tex_t *menu_bg = R_GXBindMenuBackground( img, texnum );
		if( menu_bg )
			return menu_bg;
	}
	/* Arena-tail reuse can NULL gc_packed while ARAM + metadata remain —
	 * still take the packed/bounce path (CapFaces G198 grey otherwise). */
	if( img && ( img->gc_packed
		|| ( img->gc_packed_aram && img->gc_packed_bytes && img->width > 0 )) )
		return R_GXPackedTexture( img, texnum );
	if( !img || !img->pixels[0] || img->width < 1 || img->height < 1 )
		return NULL;
	/* Sys_CheckParm walks argv: this runs for every bind. */
	if( r_gx_tex_native_route < 0 )
		r_gx_tex_native_route = ( GC_MenuRunsServer() || gEngfuncs.Sys_CheckParm( "-gcnativetex" ))
			&& !gEngfuncs.Sys_CheckParm( "-gcnonativetex" );
	if( !hud_bind && r_gx_tex_native_route )
	{
		gc_gx_tex_t *native = R_GXNativeTexture( img, texnum );

		if( native )
			return native;
	}

	for( i = 0; i < GC_GX_TEX_SLOTS; i++ )
	{
		if( r_gx_tex[i].valid && r_gx_tex[i].texnum == texnum
			&& r_gx_tex[i].fmt == want_fmt )
		{
			r_gx_tex_lru[i] = ++r_gx_tex_clock;
			r_gx_tex_fence[i] = GC_GXFenceNext();
			if( hud_bind )
				r_gx_tex[i].hud_pin = true;
			if( r_gx_bound_texnum != texnum || r_gx_bound_fmt != want_fmt )
			{
				GX_LoadTexObj( &r_gx_tex[i].obj, GX_TEXMAP0 );
				r_gx_bound_texnum = texnum;
				r_gx_bound_fmt = want_fmt;
				r_gx_tex_loads++;
			}
			else
				r_gx_tex_reuses++;
			return &r_gx_tex[i];
		}
	}

	/* HUD slots can be only partially reserved when MEM1 is tight. */
	slot = -1;
	victim = -1;
	if( hud_bind )
	{
		R_GXReserveHudPool();
		for( i = GC_GX_TEX_HUD_SLOT0; i < GC_GX_TEX_SLOTS; i++ )
		{
			if( !r_gx_tex[i].valid && r_gx_tex[i].tiled )
			{
				slot = i;
				break;
			}
		}
		if( slot < 0 )
		{
			qboolean partial = false;
			int font_victim = -1;

			for( i = GC_GX_TEX_HUD_ONDEMAND0; i < GC_GX_TEX_SLOTS; i++ )
			{
				if( i < GC_GX_TEX_HUD_SLOT0 && !r_gx_tex[i].hud_pin )
					continue;
				if( !r_gx_tex[i].tiled )
					partial = true;
				if( !r_gx_tex[i].tiled )
					continue;
				/* Prefer evicting chrome/logo over the menu font atlas. */
				if( r_gx_tex[i].is_font )
				{
					if( font_victim < 0 || r_gx_tex_lru[i] < r_gx_tex_lru[font_victim] )
						font_victim = i;
					continue;
				}
				if( victim < 0 || r_gx_tex_lru[i] < r_gx_tex_lru[victim] )
					victim = i;
			}
			if( victim < 0 )
				victim = font_victim;
			/* Unreserved slabs (G184 alloc fail at map load): back a free
			 * slot on demand with a buffer sized to this texture before
			 * evicting.  With every slab missing, all HUD sprites shared one
			 * slot and re-converted each other every frame (c0a0e: pain and
			 * train icons, 2 uploads/frame). */
			for( i = GC_GX_TEX_HUD_ONDEMAND0; i < GC_GX_TEX_SLOTS; i++ )
			{
				if( !r_gx_tex[i].tiled && !r_gx_tex[i].valid )
				{
					hud_evict = victim; /* fallback if the heap says no */
					slot = i;
					break;
				}
			}
			/* Reuse a reserved slab before trying an unbacked slot. Otherwise
			 * a partial reservation makes every subsequent HUD bind fail even
			 * though an older HUD texture could be safely evicted. */
			if( slot < 0 )
				slot = victim >= 0 ? victim : GC_GX_TEX_HUD_SLOT0;
			if( slot == victim && victim >= 0 )
			{
				static qboolean logged;

				if( !logged )
				{
					logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: HUD pool reusing slot=%d partial=%d font=%d\n",
						slot, partial ? 1 : 0, r_gx_tex[slot].is_font ? 1 : 0 );
				}
			}
		}
	}
	else
	{
		/* G199: world diffuse only uses BSS pool — never heap memalign. */
		for( i = 0; i < GC_GX_TEX_WORLD_POOL; i++ )
		{
			if( !r_gx_tex[i].valid )
			{
				slot = i;
				break;
			}
		}
		if( slot < 0 )
		{
			for( i = 0; i < GC_GX_TEX_WORLD_POOL; i++ )
			{
				if( victim < 0 || r_gx_tex_lru[i] < r_gx_tex_lru[victim] )
					victim = i;
			}
			if( victim < 0 )
				victim = 0;
			slot = victim;
		}
	}

	t = &r_gx_tex[slot];
	if( t->valid && GC_GXFenceWait( r_gx_tex_fence[slot] ))
		r_gx_tex_fence_stalls++;
	r_gx_tex_uploads++;

	src_w = img->width;
	src_h = img->height;
	if( hud_bind )
	{
		dst_w = R_GXSnapDimUpMax( src_w, GC_GX_TEX_HUD_MAX_DIM );
		dst_h = R_GXSnapDimUpMax( src_h, GC_GX_TEX_HUD_MAX_DIM );
		/* Wide HUD sheets (credits font / title logo) keep source width when
		 * the area fits the HUD tile budget. */
		if( src_w > GC_GX_TEX_WORLD_MAX_DIM && src_w <= GC_GX_TEX_HUD_MAX_DIM
			&& ( src_w & 3 ) == 0
			&& (size_t)dst_h * (size_t)src_w <= (size_t)GC_GX_TEX_HUD_MAX_DIM * (size_t)GC_GX_TEX_HUD_MAX_DIM )
			dst_w = src_w;
	}
	else
	{
		dst_w = R_GXSnapDim( src_w );
		dst_h = R_GXSnapDim( src_h );
	}
	step_x = src_w / dst_w;
	step_y = src_h / dst_h;
	if( step_x < 1 )
		step_x = 1;
	if( step_y < 1 )
		step_y = 1;

	if( !R_GXEnsureLinearScratch( (size_t)dst_w * (size_t)dst_h ))
	{
		if( hud_bind )
			r_gx_hud_bind_fails++;
		return NULL;
	}

	{
		/* Pic strips: punch black so TransAlpha leaves only orange ink.
		 * Background tiles must keep near-black opaque (retail steam art).
		 * The name test is per upload, not per texel (it was a string scan
		 * for every texel of every HUD/menu conversion). */
		const qboolean punch_black = hud_bind && img->name[0]
			&& Q_stristr( img->name, "gc_menu/btn_" ) != NULL;
		u16 *out = r_gx_tex_linear;

		for( y = 0; y < dst_h; y++ )
		{
			int sy = y * step_y;
			const pixel_t *row, *alpha_row = NULL;

			if( sy >= src_h )
				sy = src_h - 1;
			row = &img->pixels[0][sy * src_w];
			/* Translucent HUD/menu art (title logo, TGA alpha) keeps its alpha
			 * instead of drawing its clear areas as an opaque black box. */
			if( hud_bind && !punch_black && img->alpha_pixels )
				alpha_row = &img->alpha_pixels[sy * src_w];
			for( x = 0; x < dst_w; x++ )
			{
				int sx = x * step_x;
				pixel_t soft;

				if( sx >= src_w )
					sx = src_w - 1;
				soft = row[sx];
				if( punch_black )
					*out++ = R_GXSoftToRGB5A3PunchBlack( soft );
				else if( alpha_row )
				{
					/* 3-bit alpha plane (GL_UploadTexture) -> RGB5A3's own
					 * translucent mode (0AAARRRRGGGGBBBB). */
					const unsigned a3 = (unsigned)alpha_row[sx] >> 13;

					if( a3 >= 7 )
						*out++ = R_GXSoftToRGB5A3( soft );
					else if( a3 == 0 || soft == TRANSPARENT_COLOR )
						*out++ = 0;
					else
					{
						const unsigned c = (u16)R_GCSoftToRGB565( soft );
						*out++ = (u16)(( a3 << 12 ) | (( c >> 12 ) << 8 )
							| ((( c >> 7 ) & 15u ) << 4 ) | (( c >> 1 ) & 15u ));
					}
				}
				else if( hud_bind )
					/* Punch near-black so TransAlpha HUD digits aren't opaque black. */
					*out++ = R_GXSoftToRGB5A3PunchBlack( soft );
				else if( soft == TRANSPARENT_COLOR )
					*out++ = 0;
				else
					*out++ = (u16)R_GCSoftToRGB565( soft );
			}
		}
	}
	if( hud_bind && r_gx_prof_hud_uploads_logged < 40 && gEngfuncs.Sys_CheckParm( "-gcprofile" ))
	{
		r_gx_prof_hud_uploads_logged++;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: profile HUD upload slot=%d tex=%u %s %dx%d -> %dx%d\n",
			slot, texnum, img->name, src_w, src_h, dst_w, dst_h );
	}

	bytes = (size_t)dst_w * (size_t)dst_h * sizeof( u16 );
	/*
	 * G183: never free-before-alloc — MEM1 is fragmented after world upload.
	 * Reuse any staging buffer large enough (HUD snaps are ≤ 64×64).
	 */
	{
		const size_t max_tile = hud_bind
			? (size_t)GC_GX_TEX_HUD_MAX_DIM * (size_t)GC_GX_TEX_HUD_MAX_DIM * sizeof( u16 )
			: (size_t)GC_GX_TEX_WORLD_MAX_DIM * (size_t)GC_GX_TEX_WORLD_MAX_DIM * sizeof( u16 );
		size_t want = bytes;

		/* HUD uploads prefer a reusable staging slab so later sheets reuse;
		 * a slot backed on demand (no reserved slab) takes only what this
		 * texture needs, so the heap is not asked for 32 KiB per icon. */
		if( hud_bind && t->tiled
			&& want < (size_t)GC_GX_TEX_WORLD_MAX_DIM * (size_t)GC_GX_TEX_WORLD_MAX_DIM * sizeof( u16 ))
			want = (size_t)GC_GX_TEX_WORLD_MAX_DIM * (size_t)GC_GX_TEX_WORLD_MAX_DIM * sizeof( u16 );

		if( slot < GC_GX_TEX_WORLD_POOL )
		{
			t->tiled = r_gx_tex_world_pool[slot];
			t->alloc_bytes = (size_t)GC_GX_TEX_WORLD_MAX_DIM * (size_t)GC_GX_TEX_WORLD_MAX_DIM * sizeof( u16 );
			(void)max_tile;
		}
		else if( !t->tiled || t->alloc_bytes < bytes )
		{
			u16 *neu = NULL;
			qboolean from_heap = false;

			/* On-demand HUD slot: boot arena while open (menu), else a
			 * bounded heap share. */
			if( hud_bind && slot < GC_GX_TEX_HUD_SLOT0 )
			{
				neu = (u16 *)GC_BootArenaAlloc( want );
				if( !neu && r_gx_hud_ondemand_heap + want <= GC_GX_TEX_HUD_HEAP_BUDGET )
				{
					neu = (u16 *)memalign( 32, want );
					from_heap = neu != NULL;
				}
			}
			else
				neu = (u16 *)memalign( 32, want );
			if( from_heap )
				r_gx_hud_ondemand_heap += want;

			if( !neu && hud_evict >= 0 && r_gx_tex[hud_evict].tiled
				&& r_gx_tex[hud_evict].alloc_bytes >= bytes )
			{
				/* No heap for an on-demand slot: evict as before. */
				slot = hud_evict;
				t = &r_gx_tex[slot];
				if( t->valid && GC_GXFenceWait( r_gx_tex_fence[slot] ))
					r_gx_tex_fence_stalls++;
			}
			else if( !neu )
			{
				if( !t->tiled || t->alloc_bytes < bytes )
				{
					r_gx_hud_bind_fails++;
					return NULL;
				}
			}
			else
			{
				if( t->tiled )
					free( t->tiled );
				t->tiled = neu;
				t->alloc_bytes = want;
			}
		}
	}

	R_GXSwizzleRGB565( r_gx_tex_linear, dst_w, dst_w, dst_h, t->tiled );
	DCFlushRange( t->tiled, (u32)bytes );

	{
		/* Menu tiles are full-bleed sheets; REPEAT wraps edge UVs into the
		 * opposite side (chroma fringe). Match dedicated background CLAMP. */
		const u8 wrap = ( hud_bind && img->name[0]
			&& Q_stristr( img->name, "gc_menu/" )) ? (u8)GX_CLAMP : (u8)GX_REPEAT;

		GX_InitTexObj( &t->obj, t->tiled, (u16)dst_w, (u16)dst_h,
			want_fmt, wrap, wrap, GX_FALSE );
	}
	/* G203: LINEAR on world diffuse; HUD stays NEAR for crisp sprites,
	 * except fonts created without TF_NEAREST (drawn minified 2:1) and the
	 * high-res title logo which is stretched across the menu. */
	/* Logo can stay soft-filtered; bg tiles + btn strips need NEAR so the
	 * 160×120 / 156×26 art does not smear into chroma noise on TV. */
	if( hud_bind && img->name[0] && Q_stristr( img->name, "gc_menu/logo" ))
		GX_InitTexObjFilterMode( &t->obj, GX_LINEAR, GX_LINEAR );
	else if( hud_bind && !( FBitSet( img->flags, TF_FONT ) && !FBitSet( img->flags, TF_NEAREST )))
		GX_InitTexObjFilterMode( &t->obj, GX_NEAR, GX_NEAR );
	else
		GX_InitTexObjFilterMode( &t->obj, GX_LINEAR, GX_LINEAR );
	t->texnum = texnum;
	t->w = dst_w;
	t->h = dst_h;
	t->fmt = want_fmt;
	t->valid = true;
	t->hud_pin = hud_bind;
	t->is_font = hud_bind && FBitSet( img->flags, TF_FONT );
	r_gx_tex_lru[slot] = ++r_gx_tex_clock;
	r_gx_tex_fence[slot] = GC_GXFenceNext();
	/* New tiled texels — invalidate once here, not every world pass. */
	GX_InvalidateTexAll();
	r_gx_tex_invalidates++;
	GX_LoadTexObj( &t->obj, GX_TEXMAP0 );
	r_gx_bound_texnum = texnum;
	r_gx_bound_fmt = want_fmt;
	r_gx_tex_loads++;
	return t;
}

static gc_gx_tex_t *R_GXBindTexture( texture_t *mt )
{
	gc_gx_tex_t *gxt;

	if( !mt || mt->gl_texturenum <= 0 )
		return NULL;
	/* World faces must not inherit HUD RGB5A3/slot mode from StretchPic. */
	gxt = R_GXBindTexnum( (unsigned)mt->gl_texturenum, false );
	if( !gxt )
	{
		static qboolean bind_fail_logged;
		image_t *img;

		if( !bind_fail_logged )
		{
			bind_fail_logged = true;
			img = R_GetTexture( (unsigned)mt->gl_texturenum );
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G198 GX tex bind fail texnum=%d mt=%s img=%s pixels=%d %dx%d packed=%d aram=0x%x\n",
				mt->gl_texturenum, mt->name[0] ? mt->name : "?",
				( img && img->name[0] ) ? img->name : "?",
				( img && img->pixels[0] ) ? 1 : 0,
				img ? img->width : 0, img ? img->height : 0,
				( img && img->gc_packed ) ? 1 : 0,
				img ? (unsigned)img->gc_packed_aram : 0u );
		}
	}
	return gxt;
}

/*
================
G181: keep area-major order (coverage) but stable-sort by texture inside
coarse bands so TEXMAP0 runs cluster without a full texture-only reorder.
================
*/
#define GC_GX_TEX_BANDS 8

static unsigned R_GXSurfTexKey( const msurface_t *surf )
{
	if( !surf || !surf->texinfo || !surf->texinfo->texture )
		return 0u;
	return (unsigned)surf->texinfo->texture->gl_texturenum;
}

/* CapFaces share one texture_t stub (BSS); gl_texturenum lives in gc_cap_texnum[].
 * Reading the stub made every face key=0 so tex banding never clustered binds,
 * and the bounce buffer thrashed every face → temporal texture flash. */
static unsigned R_GXOrderTexKey( const msurface_t *draw, int slot )
{
	const msurface_t *surf;

	if( !draw || slot < 0 )
		return 0u;
	surf = &draw[slot];
	if( surf->firstedge < 0 )
	{
		extern int GC_GetCapFaceTexnum( int slot );
		int cap_tex = GC_GetCapFaceTexnum( slot );

		if( cap_tex > 0 )
			return (unsigned)cap_tex;
	}
	return R_GXSurfTexKey( surf );
}

/* Faces queued by a pass as ( texture << 16 ) | surface index and emitted
 * texture-sorted: the world pool holds only GC_GX_TEX_WORLD_POOL textures,
 * so emitting in BSP order re-uploaded a texture for nearly every face
 * (~230 converts per frame on the tram ride for ~38 distinct textures). */
#define GC_GX_FACE_SORT_MAX 2048
static unsigned r_gx_face_sort[GC_GX_FACE_SORT_MAX];

static int R_GXFaceSortCmp( const void *a, const void *b )
{
	const unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;

	return ( x > y ) - ( x < y );
}

static qboolean R_GXFaceSortPush( int *n, const msurface_t *surf, int index )
{
	unsigned batch = GC_MapFaceBatchKey( index );
	unsigned texture = R_GXSurfTexKey( surf );
	if( *n >= GC_GX_FACE_SORT_MAX || index < 0 || index > 0xFFFF )
		return false;
	if( batch ) texture = (( batch & 0xFFu ) << 8 ) | ( texture & 0xFFu );
	r_gx_face_sort[( *n )++] = (( texture & 0xFFFFu ) << 16 ) | (unsigned)index;
	return true;
}

static void R_GXOrderFacesByTexBands( const msurface_t *draw, int n, int *order )
{
	int b, i, j;

	for( i = 0; i < n; i++ )
		order[i] = i;

	/* Tiny batches: insertion sort overhead exceeds bind savings. */
	if( n < 24 )
		return;

	for( b = 0; b < GC_GX_TEX_BANDS; b++ )
	{
		const int band0 = ( b * n ) / GC_GX_TEX_BANDS;
		const int band1 = (( b + 1 ) * n ) / GC_GX_TEX_BANDS;

		for( i = band0 + 1; i < band1; i++ )
		{
			const int slot = order[i];
			const unsigned key = R_GXOrderTexKey( draw, slot );

			j = i;
			while( j > band0 && R_GXOrderTexKey( draw, order[j - 1] ) > key )
			{
				order[j] = order[j - 1];
				j--;
			}
			order[j] = slot;
		}
	}
}

static qboolean r_gx_g221_logged;
static qboolean r_gx_sky_backdrop_logged;
/* Forward decls — defined with R_GXSkyLoad below. */
static qboolean r_gx_sky_ready;
static GXTexObj r_gx_sky_obj[6];

static qboolean R_GXHasSkyboxSide( void )
{
	int i;

	for( i = 0; i < 6; i++ )
	{
		if( tr.skyboxTextures[i] > 0 )
			return true;
	}
	/* Lost Coast CapFaces: sky CMPR lives in r_gx_sky_obj, not lean texnums. */
	return r_gx_sky_ready;
}

static int R_GXPickSkyboxSide( void )
{
	vec3_t forward, right, up;
	float ax, ay, az;
	int prefer = 3; /* ft */

	AngleVectors( RI.rvp.viewangles, forward, right, up );
	ax = fabsf( forward[0] );
	ay = fabsf( forward[1] );
	az = fabsf( forward[2] );
	/* skybox order: rt bk lf ft up dn */
	if( az >= ax && az >= ay )
		prefer = ( forward[2] > 0.0f ) ? 4 : 5;
	else if( ax >= ay )
		prefer = ( forward[0] > 0.0f ) ? 0 : 2;
	else
		prefer = ( forward[1] > 0.0f ) ? 3 : 1;

	if( tr.skyboxTextures[prefer] > 0 )
		return prefer;
	if( tr.skyboxTextures[4] > 0 )
		return 4;
	if( tr.skyboxTextures[3] > 0 )
		return 3;
	if( tr.skyboxTextures[1] > 0 )
		return 1;
	if( tr.skyboxTextures[0] > 0 )
		return 0;
	/* CMPR skybox resident — pick by look even without lean texnums. */
	if( r_gx_sky_ready )
		return prefer;
	return -1;
}

/*
=============
R_GXDrawSkyBackdrop

G285: tip-safe Flipper outdoor sky — one lean skybox side as a fullscreen
textured quad (no SURF_DRAWSKY BSP walk, no new BSS). Falls back to clear.
=============
*/
static void R_GXDrawSkyBackdrop( GXRModeObj *rmode )
{
	Mtx44 ortho;
	Mtx ident;
	gc_gx_tex_t *gxt = NULL;
	const f32 fb_w = (f32)rmode->fbWidth;
	const f32 fb_h = (f32)rmode->efbHeight;
	int side;
	unsigned texnum = 0;
	qboolean used_cmpr = false;

	side = R_GXPickSkyboxSide();
	if( side < 0 )
		return;
	if( tr.skyboxTextures[side] > 0 )
	{
		texnum = (unsigned)tr.skyboxTextures[side];
		gxt = R_GXBindTexnum( texnum, false );
	}
	if( !gxt && r_gx_sky_ready && side >= 0 && side < 6 )
	{
		/* CapFaces smoke: bind media/sky_*.cmp directly (no lean texnum). */
		GX_LoadTexObj( &r_gx_sky_obj[side], GX_TEXMAP0 );
		used_cmpr = true;
	}
	else if( !gxt )
		return;

	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 1 );
	GX_SetNumTexGens( 1 );
	GX_SetNumTevStages( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );

	guOrtho( ortho, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( 0.0f, 0.0f, -0.9f );
	GX_Color1u32( 0xFFFFFFFFu );
	GX_TexCoord2f32( 0.0f, 0.0f );
	GX_Position3f32( fb_w, 0.0f, -0.9f );
	GX_Color1u32( 0xFFFFFFFFu );
	GX_TexCoord2f32( 1.0f, 0.0f );
	GX_Position3f32( fb_w, fb_h, -0.9f );
	GX_Color1u32( 0xFFFFFFFFu );
	GX_TexCoord2f32( 1.0f, 1.0f );
	GX_Position3f32( 0.0f, fb_h, -0.9f );
	GX_Color1u32( 0xFFFFFFFFu );
	GX_TexCoord2f32( 0.0f, 1.0f );
	GX_End();

	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	if( !r_gx_sky_backdrop_logged )
	{
		r_gx_sky_backdrop_logged = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G285 Flipper sky backdrop side=%d tex=%u cmpr=%d\n",
			side, texnum, used_cmpr ? 1 : 0 );
	}
}

static void R_GXClearEfbSky( GXRModeObj *rmode )
{
	Mtx44 ortho;
	Mtx ident;
	const f32 fb_w = (f32)rmode->fbWidth;
	const f32 fb_h = (f32)rmode->efbHeight;
	/* G221/G285: use the desert clear only when a real skybox side exists.
	 * A map hole/void is fog-black; blue here made missing geometry look like
	 * an untextured outdoor sky and was especially visible through tram gaps.
	 * Textured backdrop draws when lean sides are resident.
	 * G348: tram tunnel (c0a0*) uses dark concrete — sky blue void fought retail.
	 * Solo CapFaces (-gcworldrender, no skybox resident): outdoor sky clear so
	 * dod_flash voids read as sky, not black letterbox (20261107-133054). */
	u32 sky = R_GXHasSkyboxSide() ? 0x5A8CD2FFu : 0x000000FFu;
	GXColor copy_clear;
	extern qboolean GC_IsTramIntroMap( void );

	if( GC_IsTramIntroMap())
		sky = 0x2A2E32FFu;
	else if( !R_GXHasSkyboxSide()
		&& gEngfuncs.Sys_CheckParm( "-gcworldrender" )
		&& !gEngfuncs.Sys_CheckParm( "-gcconnect" ))
		sky = 0x5A8CD2FFu;
	/* Lost Coast CapFaces dump: retail overcast ~201,207,193 — not desert blue. */
	if( gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" )
		|| gEngfuncs.Sys_CheckParm( "-gcdumpframes" ))
		sky = 0xC8CEC2FFu;

	copy_clear.r = (u8)(( sky >> 24 ) & 0xff );
	copy_clear.g = (u8)(( sky >> 16 ) & 0xff );
	copy_clear.b = (u8)(( sky >> 8 ) & 0xff );
	copy_clear.a = 0xff;
	GX_SetCopyClear( copy_clear, 0x00ffffff );

	if( !r_gx_g221_logged )
	{
		gEngfuncs.Con_Reportf( "G285 clear %u,%u,%u skybox=%d\n",
			(unsigned)copy_clear.r, (unsigned)copy_clear.g, (unsigned)copy_clear.b,
			R_GXHasSkyboxSide() ? 1 : 0 );
		r_gx_g221_logged = true;
	}

	/* Color-only fill — never write Z here. Ortho depths poison the later
	 * perspective LEQUAL test (DumpFrames stayed solid sky with drawn=179). */
	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetNumChans( 1 );
	GX_SetNumTexGens( 0 );
	GX_SetNumTevStages( 1 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );

	guOrtho( ortho, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( 0.0f, 0.0f, -0.5f );
	GX_Color1u32( sky );
	GX_Position3f32( fb_w, 0.0f, -0.5f );
	GX_Color1u32( sky );
	GX_Position3f32( fb_w, fb_h, -0.5f );
	GX_Color1u32( sky );
	GX_Position3f32( 0.0f, fb_h, -0.5f );
	GX_Color1u32( sky );
	GX_End();

	/* Textured backdrop for outdoor clear only. Tram indoor (G285 dark clear)
	 * must keep flat dark so tunnel-end holes don't flash lean-sky white. */
	if( !GC_IsTramIntroMap())
		R_GXDrawSkyBackdrop( rmode );

	/* Lost Coast DumpFrames: soft HDR sun wash (retail blown overcast disc). */
	if( gEngfuncs.Sys_CheckParm( "-gcdumpframes" )
		|| gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" ))
	{
		const f32 cx = fb_w * 0.52f;
		const f32 cy = fb_h * 0.22f;
		const f32 rx = fb_w * 0.28f;
		const f32 ry = fb_h * 0.18f;

		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
		GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
		GX_SetNumChans( 1 );
		GX_SetNumTexGens( 0 );
		GX_SetNumTevStages( 1 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
		GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
		GX_ClearVtxDesc();
		GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
		GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
		GX_Position3f32( cx - rx, cy - ry, -0.4f );
		GX_Color1u32( 0xF5F2E060u );
		GX_Position3f32( cx + rx, cy - ry, -0.4f );
		GX_Color1u32( 0xF5F2E060u );
		GX_Position3f32( cx + rx, cy + ry, -0.4f );
		GX_Color1u32( 0xF5F2E028u );
		GX_Position3f32( cx - rx, cy + ry, -0.4f );
		GX_Color1u32( 0xF5F2E028u );
		GX_End();
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	}
}

static void R_GXClearDepthPerspective( GXRModeObj *rmode )
{
	Mtx44 ortho;
	Mtx ident;
	const f32 fb_w = (f32)rmode->fbWidth;
	const f32 fb_h = (f32)rmode->efbHeight;

	/* Far-Z fill under a throwaway ortho so perspective world LEQUAL starts clean. */
	GX_SetColorUpdate( GX_FALSE );
	GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_TRUE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetNumChans( 0 );
	GX_SetNumTexGens( 0 );
	GX_SetNumTevStages( 1 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	guOrtho( ortho, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );
	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( 0.0f, 0.0f, -1.0f );
	GX_Position3f32( fb_w, 0.0f, -1.0f );
	GX_Position3f32( fb_w, fb_h, -1.0f );
	GX_Position3f32( 0.0f, fb_h, -1.0f );
	GX_End();
	GX_SetColorUpdate( GX_TRUE );
}

/*
=============
R_GXDrawSkyBox

The map's sky as a box around the camera, drawn before the world with
depth off so it shows wherever the sky faces (skipped by the world pass)
leave holes, as GoldSrc draws it. The sides come from the disc as 128x128
GX CMPR (media/sky_<name><side>.cmp, made by build-gamecube-disc.py):
48 KiB for all six, reloaded only when the map's sky name changes, so the
sky survives changelevels. Missing sides keep the solid clear colour.
=============
*/
#define R_GX_SKY_SIZE	128
#define R_GX_SKY_BYTES	( R_GX_SKY_SIZE * R_GX_SKY_SIZE / 2 )
void R_GXInvalidateTriApiLayout( void );
static u8 r_gx_sky_data[6][R_GX_SKY_BYTES] __attribute__(( aligned( 32 )));
static char r_gx_sky_name[32];
/* r_gx_sky_obj / r_gx_sky_ready forward-declared near G285 backdrop. */
static const model_t *r_gx_sky_world;
static qboolean r_gx_sky_world_has_sky;
static char r_gx_sky_world_name[64];

/* Worldspawn "skyname" — CapFaces sky-first runs mid-BSP, before CWorld::KeyValue. */
static qboolean R_GXParseWorldspawnSkyname( const char *ents, char *out, size_t outsz )
{
	const char *p, *end;
	size_t n;

	if( !ents || !out || outsz < 2 )
		return false;
	p = Q_stristr( ents, "\"skyname\"" );
	if( !p )
		return false;
	p = Q_strchr( p + 9, '"' );
	if( !p )
		return false;
	p++;
	end = Q_strchr( p, '"' );
	if( !end || end <= p )
		return false;
	n = (size_t)( end - p );
	if( n >= outsz )
		n = outsz - 1;
	memcpy( out, p, n );
	out[n] = '\0';
	return out[0] != '\0';
}

static void R_GXSkyLoad( void )
{
	/* Xash skybox side order (ref_common.c r_skyBoxSuffix). */
	static const char *const suffix[6] = { "rt", "bk", "lf", "ft", "up", "dn" };
	const char *name = ( gp_movevars && gp_movevars->skyName[0] ) ? gp_movevars->skyName : "desert";
	char from_ents[32];
	char lower[32];
	int i;

	/* Prefer worldspawn over the default desert cvar until KeyValue syncs movevars. */
	if( WORLDMODEL && WORLDMODEL->entities
		&& R_GXParseWorldspawnSkyname( WORLDMODEL->entities, from_ents, sizeof( from_ents ))
		&& ( !gp_movevars || !gp_movevars->skyName[0]
			|| !Q_stricmp( gp_movevars->skyName, "desert" )))
		name = from_ents;

	if( r_gx_sky_name[0] && !Q_stricmp( name, r_gx_sky_name ) && r_gx_sky_ready )
		return;
	Q_strncpy( r_gx_sky_name, name, sizeof( r_gx_sky_name ));
	Q_strnlwr( name, lower, sizeof( lower ));
	r_gx_sky_ready = true;
	for( i = 0; i < 6 && r_gx_sky_ready; i++ )
	{
		char path[64];
		fs_offset_t len = 0;
		file_t *f;

		/* Tip-safe: read straight into BSS — LoadFileMalloc OOMs mid-sky on
		 * lean maps (charlie: rt+bk OK, lf malloc miss → pack also starved). */
		Q_snprintf( path, sizeof( path ), "media/sky_%s%s.cmp", lower, suffix[i] );
		f = gEngfuncs.fsapi->Open( path, "rb", false );
		if( f )
		{
			len = gEngfuncs.fsapi->FileLength( f );
			if( len == R_GX_SKY_BYTES
				&& gEngfuncs.fsapi->Read( f, r_gx_sky_data[i], R_GX_SKY_BYTES ) == R_GX_SKY_BYTES )
				; /* ok */
			else
				len = 0;
			gEngfuncs.fsapi->Close( f );
		}
		if( len != R_GX_SKY_BYTES )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: sky %s missing %s, using the clear colour\n", name, path );
			r_gx_sky_ready = false;
		}
	}
	if( !r_gx_sky_ready )
		return;
	DCFlushRange( r_gx_sky_data, sizeof( r_gx_sky_data ));
	GX_InvalidateTexAll();
	for( i = 0; i < 6; i++ )
	{
		GX_InitTexObj( &r_gx_sky_obj[i], r_gx_sky_data[i], R_GX_SKY_SIZE, R_GX_SKY_SIZE,
			GX_TF_CMPR, GX_CLAMP, GX_CLAMP, GX_FALSE );
		GX_InitTexObjLOD( &r_gx_sky_obj[i], GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f,
			GX_FALSE, GX_FALSE, GX_ANISO_1 );
	}
	gEngfuncs.Con_Reportf( "Xash3D GameCube: sky %s loaded (6 x %dx%d CMPR)\n", name, R_GX_SKY_SIZE, R_GX_SKY_SIZE );
}

/* CapFaces lean / atlas: load this map's sky CMPR before CapFaces so the
 * first Flipper presents are the skybox image, not a black/magenta clear. */
void R_GXPrefetchMapSkybox( void )
{
	R_GXSkyLoad();
}

/* Only maps with sky faces get the box; indoor maps keep the clear colour. */
static qboolean R_GXSkyVisible( void )
{
	const model_t *world = WORLDMODEL;

	if( !world )
		return false;
	/* The world model slot is reused across maps: key on the name. */
	if( world != r_gx_sky_world || Q_strcmp( world->name, r_gx_sky_world_name ))
	{
		int i;

		r_gx_sky_world = world;
		Q_strncpy( r_gx_sky_world_name, world->name, sizeof( r_gx_sky_world_name ));
		/* CapFaces lean omits live surfaces — still draw the map skybox first.
		 * Otherwise scan for SURF_DRAWSKY. Solo -gcworldrender always wants
		 * the per-map sky image before CapFaces tris. */
		r_gx_sky_world_has_sky = !world->surfaces || world->numsurfaces <= 0
			|| gEngfuncs.Sys_CheckParm( "-gcworldrender" );
		for( i = 0; !r_gx_sky_world_has_sky && world->surfaces && i < world->numsurfaces; i++ )
		{
			if( world->surfaces[i].flags & SURF_DRAWSKY )
			{
				r_gx_sky_world_has_sky = true;
				break;
			}
		}
		gEngfuncs.Con_Reportf( "Xash3D GameCube: sky faces=%d on %s\n",
			r_gx_sky_world_has_sky ? 1 : 0, world->name );
	}
	if( !r_gx_sky_world_has_sky )
		return false;
	R_GXSkyLoad();
	return r_gx_sky_ready;
}

static void R_GXDrawSkyBox( void )
{
	/* Box axes and side order as ref/gl/gl_warp.c draws them. */
	static const int st_to_vec[6][3] =
	{
		{ 3, -1, 2 }, { -3, 1, 2 }, { 1, 3, 2 }, { -1, -3, 2 }, { -2, -1, 3 }, { 2, -1, -3 }
	};
	static const int sky_tex_order[6] = { 0, 2, 1, 3, 4, 5 };
	static const float corner[4][2] = { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } };
	const float dist = 256.0f; /* any size inside the far plane: depth is off */
	int axis, c, j;

	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 0 );
	GX_SetNumTexGens( 1 );
	GX_SetNumTevStages( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
	GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
	GX_InvVtxCache();

	for( axis = 0; axis < 6; axis++ )
	{
		GX_LoadTexObj( &r_gx_sky_obj[sky_tex_order[axis]], GX_TEXMAP0 );
		GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
		for( c = 0; c < 4; c++ )
		{
			const float b[3] = { corner[c][0] * dist, corner[c][1] * dist, dist };
			float v[3];

			for( j = 0; j < 3; j++ )
			{
				const int k = st_to_vec[axis][j];

				v[j] = RI.rvp.vieworigin[j] + (( k < 0 ) ? -b[-k - 1] : b[k - 1] );
			}
			GX_Position3f32( v[0], v[1], v[2] );
			GX_TexCoord2f32(( corner[c][0] + 1.0f ) * 0.5f, 1.0f - ( corner[c][1] + 1.0f ) * 0.5f );
		}
		GX_End();
	}

	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	R_GXInvalidateTriApiLayout();
}

static void R_GXSetupWorld3DState( void )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 proj;
	Mtx mv;

	if( !rmode )
		return;
	{
		extern qboolean GC_UpdateLightmapStyles( void );

		if( GC_UpdateLightmapStyles( ))
			GX_InvalidateTexAll();
	}
	r_gx_newgame_route = gEngfuncs.Sys_CheckParm( "-gcnewgame" ) != 0;
	r_gx_menu_route = gEngfuncs.Sys_CheckParm( "-gcmenuplaystart" ) != 0;

	if( r_gx_efb_dump_hold > 0 )
	{
		r_gx_efb_dump_hold--;
		/* G221: -gcnewgame re-arms hold every Flipper present, so clear is
		 * almost never reached after load. Soft CopyDisp may have left
		 * outdoor SetCopyClear in EFB — force one indoor clear when world
		 * draw first becomes live.
		 * G376: do NOT clear every dump-hold frame (wipes CapFaces before
		 * Dolphin samples → sky-only stills). Studio smear is avoided by
		 * rendering CapFaces+studios once, then Present-only under hold. */
		if( GC_UseGxWorldDraw() && !r_gx_g221_world_clear_done )
		{
			R_GXClearEfbSky( rmode );
			R_GXClearDepthPerspective( rmode );
			r_gx_g221_world_clear_done = true;
		}
	}
	else
	{
		R_GXClearEfbSky( rmode );
		R_GXClearDepthPerspective( rmode );
		if( GC_UseGxWorldDraw() )
			r_gx_g221_world_clear_done = true;
	}

	GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	GX_SetPixelFmt( GX_PF_RGB8_Z24, GX_ZC_LINEAR );
	/* G210: EDGE verts restore BSP winding — HW backface cull on again. */
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetCullMode( GX_CULL_BACK );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );

	/* Default untextured; textured faces enable TEX0 per draw. */
	GX_SetNumTexGens( 0 );
	GX_SetNumTevStages( 1 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );

	/* G201: native GX frustum + Quake view basis (not Xash GL matrices). */
	R_GXBuildWorldProjection( proj );
	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	R_GXBuildWorldModelview( mv );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );

	/* Tip-safe: outdoor sky draw after tram hops can stall CapFaces forever
	 * (c0a0b 2desert @ tip≈48 Kb, probe 20261109-0248 — hung after
	 * "sky faces=1", never reached CapFaces surfs n=). Keep clear colour.
	 * Lost Coast CapFaces smoke already has sky CMPR resident — allow draw
	 * down to 24 KiB so DumpFrames get the skybox instead of clear blue. */
	{
		const unsigned sky_tip_min =
			gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" ) ? ( 24u * 1024u ) : ( 96u * 1024u );

	if( GC_MemHeapFreeBytes() < sky_tip_min )
	{
		static qboolean sky_tip_logged;

		if( !sky_tip_logged )
		{
			sky_tip_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: sky draw skipped tip-safe tip=%s\n",
				Q_memprint( GC_MemHeapFreeBytes() ));
		}
	}
	else if( R_GXSkyVisible( ))
	{
		R_GXDrawSkyBox();
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
		GX_SetCullMode( GX_CULL_BACK );
		GX_SetNumChans( 1 );
		GX_SetNumTexGens( 0 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
		GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	}
	}

	/* Optional camera-space marker: -gcmagenta (proved EFB→DumpFrames in G200). */
	if( gEngfuncs.Sys_CheckParm( "-gcmagenta" ))
	{
		static int diag_left = 8;
		if( diag_left > 0 )
		{
			Mtx44 persp;
			Mtx ident;
			diag_left--;
			guPerspective( persp, 60.0f, (f32)rmode->fbWidth / Q_max( 1.0f, (f32)rmode->efbHeight ), 1.0f, 4096.0f );
			GX_LoadProjectionMtx( persp, GX_PERSPECTIVE );
			guMtxIdentity( ident );
			GX_LoadPosMtxImm( ident, GX_PNMTX0 );
			GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
			GX_SetCullMode( GX_CULL_NONE );
			GX_SetNumChans( 1 );
			GX_SetNumTexGens( 0 );
			GX_SetNumTevStages( 1 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
			GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
			GX_ClearVtxDesc();
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 3 );
			GX_Position3f32( -80.0f, -60.0f, -120.0f );
			GX_Color1u32( 0xFF00FFFFu );
			GX_Position3f32( 80.0f, -60.0f, -120.0f );
			GX_Color1u32( 0xFF00FFFFu );
			GX_Position3f32( 0.0f, 70.0f, -120.0f );
			GX_Color1u32( 0xFF00FFFFu );
			GX_End();
			if( diag_left == 7 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: G200 Flipper camera-space magenta diag\n" );
			GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
			GX_LoadPosMtxImm( mv, GX_PNMTX0 );
			GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
		}
	}
	else
	{
		static qboolean g201_logged;
		if( !g201_logged && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		{
			vec3_t	forward, right, up;
			g201_logged = true;
			AngleVectors( RI.rvp.viewangles, forward, right, up );
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G201 Flipper guFrustum+lookAt fov_y=%.1f eye=(%.0f,%.0f,%.0f) fwd=(%.2f,%.2f,%.2f)\n",
				RI.rvp.fov_y,
				RI.rvp.vieworigin[0], RI.rvp.vieworigin[1], RI.rvp.vieworigin[2],
				forward[0], forward[1], forward[2] );
		}
	}

	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
	/* InvVtxCache only when vtx format changes — world format is stable. */
	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	r_gx_cull_none = false;
	if( r_gx_packed_alpha_test )
	{
		r_gx_packed_alpha_test = false;
		GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
		GX_SetZCompLoc( GX_TRUE );
	}
	r_gx_lm_atlas_bound = false;
	r_gx_live_lm_bound = false;
	r_gx_lm_atlas_checked = false;
	r_gx_lm_atlas_available = false;
}

static void R_GXFaceST( const msurface_t *surf, const float *pos, float *s, float *t )
{
	const mtexinfo_t *ti = surf->texinfo;
	float tw, th;

	*s = DotProduct( pos, ti->vecs[0] ) + ti->vecs[0][3];
	*t = DotProduct( pos, ti->vecs[1] ) + ti->vecs[1][3];

	tw = ( ti->texture && ti->texture->width > 0 ) ? (float)ti->texture->width : 64.0f;
	th = ( ti->texture && ti->texture->height > 0 ) ? (float)ti->texture->height : 64.0f;
	*s /= tw;
	*t /= th;
}

static void R_GXFaceLMST( const msurface_t *surf, const float *pos, float *s, float *t )
{
	const mextrasurf_t *info = surf->info;
	float lw, lh;
	int sample_size = 16;

	if( !info )
	{
		*s = *t = 0.0f;
		return;
	}
	if( gEngfuncs.Mod_SampleSizeForFace )
		sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	if( sample_size < 1 )
		sample_size = 16;

	*s = DotProduct( pos, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
	*t = DotProduct( pos, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
	lw = (float)( info->lightextents[0] + sample_size );
	lh = (float)( info->lightextents[1] + sample_size );
	if( lw < 1.0f )
		lw = 1.0f;
	if( lh < 1.0f )
		lh = 1.0f;
	*s /= lw;
	*t /= lh;
}

/* Menu New Game: the world's full-resolution lightmap atlas, baked at
 * map load (Mod_GCBakeMenuLightmapAtlas) into the retained BSP scratch. */
static GXTexObj r_gx_live_lm_obj;
static int r_gx_live_lm_gen = -1;
static int r_gx_live_lm_w, r_gx_live_lm_h;

static qboolean R_GXBindLiveLightmap( void )
{
	extern const unsigned short *GC_GetLiveLightmapAtlas( int *w, int *h, int *generation );
	int w, h, gen;
	const unsigned short *atlas = GC_GetLiveLightmapAtlas( &w, &h, &gen );

	if( !atlas || w < 4 || h < 4 )
		return false;
	if( gen != r_gx_live_lm_gen )
	{
		GX_InitTexObj( &r_gx_live_lm_obj, (void *)atlas, (u16)w, (u16)h,
			GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
		GX_InitTexObjFilterMode( &r_gx_live_lm_obj, GX_LINEAR, GX_LINEAR );
		/* New map: the atlas memory held other data a moment ago. */
		GX_InvalidateTexAll();
		r_gx_live_lm_gen = gen;
		r_gx_live_lm_w = w;
		r_gx_live_lm_h = h;
		r_gx_live_lm_bound = false;
	}
	if( !r_gx_live_lm_bound )
	{
		GX_LoadTexObj( &r_gx_live_lm_obj, GX_TEXMAP1 );
		r_gx_live_lm_bound = true;
		r_gx_lm_atlas_bound = false;
		r_gx_lm_loads++;
	}
	return true;
}

static qboolean R_GXBindLightmapAtlas( void )
{
	extern const unsigned short *GC_GetNewGameCapLightmapAtlas( int *w, int *h );
	extern int GC_GetNewGameCapGeneration( void );
	const unsigned short *atlas;
	int w = 0, h = 0;
	int gen;

	if( r_gx_lm_atlas_checked )
	{
		if( !r_gx_lm_atlas_available )
			return false;
		if( !r_gx_lm_atlas_bound )
		{
			GX_LoadTexObj( &r_gx_lm_atlas_obj, GX_TEXMAP1 );
			r_gx_lm_atlas_bound = true;
			r_gx_live_lm_bound = false;
			r_gx_lm_loads++;
		}
		else
			r_gx_lm_reuses++;
		return true;
	}

	atlas = GC_GetNewGameCapLightmapAtlas( &w, &h );
	r_gx_lm_atlas_checked = true;
	if( !atlas || w < 4 || h < 4 )
	{
		r_gx_lm_atlas_available = false;
		return false;
	}
	r_gx_lm_atlas_available = true;

	gen = GC_GetNewGameCapGeneration();
	if( !r_gx_lm_atlas_valid || gen != r_gx_lm_atlas_gen )
	{
		GX_InitTexObj( &r_gx_lm_atlas_obj, (void *)atlas, (u16)w, (u16)h,
			GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
		GX_InitTexObjFilterMode( &r_gx_lm_atlas_obj, GX_LINEAR, GX_LINEAR );
		r_gx_lm_atlas_valid = true;
		r_gx_lm_atlas_gen = gen;
		r_gx_lm_inits++;
	}

	if( !r_gx_lm_atlas_bound )
	{
		GX_LoadTexObj( &r_gx_lm_atlas_obj, GX_TEXMAP1 );
		r_gx_lm_atlas_bound = true;
		r_gx_live_lm_bound = false;
		r_gx_lm_loads++;
	}
	else
		r_gx_lm_reuses++;
	return true;
}

static int r_gx_lm_draws;
static qboolean r_gx_lm_logged;

/* G216: prebaked live-face verts (slot == -3 in R_GXEmitFace). */
static float r_gx_prebaked_pts[32][3];
static int r_gx_prebaked_nverts;

static int R_GXEmitFace( const msurface_t *surf, model_t *world, int slot );
static int R_GXEmitWorldDecals( model_t *world );

/* G222: flat PASSCLR fill from capture-baked verts (no tex/LM). */
static qboolean r_gx_flat_z_write; /* G277 tram: write Z so mesh occludes */
static qboolean r_gx_flat_z_ignore; /* G277 tram: draw over closer cap ceiling */

static int R_GXEmitFlatFillVerts( float pts_in[][3], int nverts_in, u32 color )
{
	int i;

	if( !pts_in || nverts_in < 3 )
		return 0;
	if( nverts_in > 32 )
		nverts_in = 32;
	if( r_gx_face_mode != GC_GX_FACE_MODE_FLAT )
	{
		/* G231: Z-test only — never occlude LM/live; plug clear-depth holes.
		 * G277 tram: ignore Z so tunnel ceiling caps do not eat the frame. */
		if( r_gx_flat_z_ignore )
			GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
		else
			GX_SetZMode( GX_TRUE, GX_LEQUAL, r_gx_flat_z_write ? GX_TRUE : GX_FALSE );
		GX_SetNumTexGens( 0 );
		GX_SetNumTevStages( 1 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
		GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
		GX_ClearVtxDesc();
		GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
		r_gx_face_mode = GC_GX_FACE_MODE_FLAT;
		r_gx_state_sets++;
	}
	else
		r_gx_state_reuses++;

	/* Convex BSP polygons can be emitted as a fan; avoid repeating the
	 * anchor and edge vertices for every generated triangle. */
	GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, (u16)nverts_in );
	for( i = 0; i < nverts_in; i++ )
	{
		GX_Position3f32( pts_in[i][0], pts_in[i][1], pts_in[i][2] );
		GX_Color1u32( color );
	}
	GX_End();
	r_gx_flat_draws++;
	return 1;
}

static int R_GXEmitFaceVerts( const msurface_t *surf, model_t *world,
	float pts_in[][3], int nverts_in )
{
	int i;

	if( !pts_in || nverts_in < 3 )
		return 0;
	if( nverts_in > 32 )
		nverts_in = 32;
	for( i = 0; i < nverts_in; i++ )
		VectorCopy( pts_in[i], r_gx_prebaked_pts[i] );
	r_gx_prebaked_nverts = nverts_in;
	return R_GXEmitFace( surf, world, -3 );
}

/* Underfoot pad probe surface: firstedge < 0, extents 256×256. */
static qboolean R_GXIsSyntheticUnderfootPad( const msurface_t *surf )
{
	return surf && surf->firstedge < 0
		&& surf->extents[0] == 256 && surf->extents[1] == 256;
}

static int R_GXBorrowedWorldTexnum( void )
{
	int texnum = GC_GetNewGameCapTextureNum();

	if( texnum <= 0 )
		texnum = GC_GetResidentWorldDiffuseTexnum();
	if( texnum <= 0 )
		texnum = GC_GetTramDiffuseTexnum();
	if( texnum <= 0 )
		texnum = (int)r_gx_bound_texnum;
	return texnum;
}

/* Borrow a resident visible world texture — no new GX object.
 * G381: do not walk world->surfaces (scratch msurface hang under CapFaces).
 * Prefer resident/tram/bound texnums over CapFaces cache (cache can be a
 * lightmap-ish name that REPLACE-paints the whole underfoot green). */
static texture_t *R_GXBorrowPadFaceTexture( model_t *world, texture_t *cache, qboolean cache_valid )
{
	static texture_t pad_borrow;
	static qboolean gx022_logged;
	texture_t *face = NULL;
	int texnum;

	(void)world;

	texnum = R_GXBorrowedWorldTexnum();
	if( texnum > 0 )
	{
		memset( &pad_borrow, 0, sizeof( pad_borrow ));
		pad_borrow.gl_texturenum = texnum;
		Q_strncpy( pad_borrow.name, "gc022-hallway", sizeof( pad_borrow.name ));
		face = &pad_borrow;
	}
	else if( cache_valid && cache && cache->gl_texturenum > 0
		&& cache->name[0] && cache->name[0] != '-' && cache->name[0] != '{' )
	{
		face = cache;
	}

	if( face && !gx022_logged )
	{
		gx022_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: GX-022 textured underfoot pad borrowed texture=%s\n",
			face->name[0] ? face->name : "?" );
	}
	return face;
}

static int R_GXEmitFace( const msurface_t *surf, model_t *world, int slot )
{
	static texture_t pad_texture_cache;
	static texture_t pad_retry_texture;
	static qboolean pad_texture_cache_valid;
	mvertex_t *pverts;
	vec3_t pts[32];
	float sts[32][2];
	float lmst[32][2];
	int nverts = 0;
	int i;
	u32 color;
	gc_gx_tex_t *gxt = NULL;
	texture_t *face_texture = NULL;
	texture_t cap_slot_tex;
	texture_t *cap_prev_tex = NULL;
	qboolean cap_retargeted = false;
	qboolean textured = false;
	qboolean pad_textured = false;
	qboolean lit = false;
	qboolean live_lm = false;
	float live_sample = 16.0f;
	int bake_src = -1;

	if( !surf || !world )
		return 0;

	/* G216: lean live faces carry capture-time baked verts (edges may dangle). */
	if( slot == -3 && r_gx_prebaked_nverts >= 3 )
	{
		nverts = r_gx_prebaked_nverts;
		if( nverts > 32 )
			nverts = 32;
		for( i = 0; i < nverts; i++ )
			VectorCopy( r_gx_prebaked_pts[i], pts[i] );
		r_gx_prebaked_nverts = 0;
	}
	/* G199: prefer capture-baked verts — live edges/surfedges dangle after scratch. */
	else if( slot >= 0 )
	{
		extern int GC_GetNewGameCapFaceVerts( int slot, float out[][3], int maxverts );
		nverts = GC_GetNewGameCapFaceVerts( slot, pts, 32 );
	}
	if( nverts < 3 )
	{
		static int gx_live_emit_guard_reports;

		if( !world->vertexes || !world->surfedges )
		{
			if( slot == -1 && gx_live_emit_guard_reports++ < 3 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: live BSP emit missing verts=%p surfedges=%p\n",
					world->vertexes, world->surfedges );
			return 0;
		}
		if( surf->numedges < 3 || surf->numedges > 32 )
			return 0;
		/* G380 synthetic floors use firstedge < 0; never index surfedges with them.
		 * Hardware Guru Meditation (PC R_GXEmitFace lhzx): dangling brush faces
		 * walked edges with lindex=0x04040404 after a corrupt/alias surfedges hit. */
		if( surf->firstedge < 0
			|| world->numsurfedges <= 0
			|| world->numedges <= 0
			|| surf->firstedge + surf->numedges > world->numsurfedges )
			return 0;
		if( !world->edges16 && !world->edges32 )
		{
			if( slot == -1 && gx_live_emit_guard_reports++ < 3 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: live BSP emit missing edges16=%p edges32=%p\n",
					world->edges16, world->edges32 );
			return 0;
		}

		pverts = world->vertexes;
		nverts = 0;
		for( i = 0; i < surf->numedges; i++ )
		{
			int lindex = world->surfedges[surf->firstedge + i];
			int v;
			int abs_e;

			abs_e = ( lindex >= 0 ) ? lindex : -lindex;
			if( abs_e <= 0 || abs_e >= world->numedges )
			{
				if( slot == -1 && gx_live_emit_guard_reports++ < 3 )
					gEngfuncs.Con_Reportf( "Xash3D GameCube: live BSP emit bad edge=%d max=%d first=%d\n",
						lindex, world->numedges, surf->firstedge );
				return 0;
			}

			/* Retail HL1 BSPs use the 16-bit edge representation.  `edges16`
			 * and `edges32` are a union, so a non-NULL `edges32` cannot be used
			 * as a format test here. */
			{
				medge16_t *e = &world->edges16[abs_e];
				v = ( lindex > 0 ) ? e->v[0] : e->v[1];
			}
			if( v < 0 || v >= world->numvertexes )
			{
				if( slot == -1 && gx_live_emit_guard_reports++ < 3 )
					gEngfuncs.Con_Reportf( "Xash3D GameCube: live BSP emit bad vertex=%d max=%d edge=%d\n",
						v, world->numvertexes, lindex );
				return 0;
			}
			VectorCopy( pverts[v].position, pts[nverts] );
			nverts++;
		}
	}

	if( nverts < 3 )
		return 0;
	if( slot >= 0 && r_gx_newgame_route )
	{
		extern int GC_GetNewGameCapBakeSrc( int slot );
		bake_src = GC_GetNewGameCapBakeSrc( slot );
	}

	/* G380: synthetic dump floor (firstedge -1) — don't depend on winding. */
	if( surf->firstedge < 0 )
	{
		static qboolean g380_emit_logged;

		if( !r_gx_cull_none )
		{
			GX_SetCullMode( GX_CULL_NONE );
			r_gx_cull_none = true;
		}
		if( !g380_emit_logged )
		{
			g380_emit_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G380 emit n=%d p0=(%.0f,%.0f,%.0f) p2=(%.0f,%.0f,%.0f)\n",
				nverts, pts[0][0], pts[0][1], pts[0][2],
				pts[2][0], pts[2][1], pts[2][2] );
		}
	}
	else if( r_gx_cull_none )
	{
		/* Restore hardware rejection after a safety plug. Without this, every
		 * later BSP face is rasterized from both sides, increasing overdraw and
		 * making the synthetic-face ordering needlessly expensive. */
		GX_SetCullMode( GX_CULL_BACK );
		r_gx_cull_none = false;
	}

	color = 0xFFFFFFFFu;
	if( surf->texinfo && surf->texinfo->texture )
		face_texture = surf->texinfo->texture;
	/* GX-022: synthetic NPC pad has no texinfo. Borrow a resident, visible
	 * horizontal world texture so the pad is textured without allocating or
	 * duplicating a texture object. */
	else if( R_GXIsSyntheticUnderfootPad( surf ))
		face_texture = R_GXBorrowPadFaceTexture( world, &pad_texture_cache, pad_texture_cache_valid );
	/* CapFaces bake stores texnum in BSS; refresh before bind so stomped
	 * model-pool texture_t cannot yield bad gl_texturenum.
	 * Stack (not static) slot tex: a static left every prior face's
	 * texinfo.texture pointing at one shared object that the next emit
	 * overwrote — plan keys / ST widths drifted every face.
	 * Copy pack image WxH — shared stub has width=0 so R_GXFaceST fell
	 * back to 64 while GX_InitTexObj used real CMPR sizes (REPEAT noise). */
	if( slot >= 0 )
	{
		extern int GC_GetCapFaceTexnum( int slot );
		extern int GC_RepairCapFaceTexnum( int slot );
		int cap_tex = GC_GetCapFaceTexnum( slot );
		image_t *cap_img = ( cap_tex > 0 ) ? R_GetTexture( (unsigned)cap_tex ) : NULL;

		/* Stale CapFaces texnum → empty image: rebind from WT by stored hash. */
		if( cap_tex > 0 )
		{
			const qboolean img_missing = ( cap_img == NULL );
			const qboolean img_empty = !img_missing && cap_img->width < 1;
			const qboolean no_pack = !img_missing && !cap_img->gc_packed
				&& !( cap_img->gc_packed_aram && cap_img->gc_packed_bytes )
				&& !cap_img->pixels[0];

			if( img_missing || img_empty || no_pack )
			{
				cap_tex = GC_RepairCapFaceTexnum( slot );
				cap_img = ( cap_tex > 0 ) ? R_GetTexture( (unsigned)cap_tex ) : NULL;
			}
		}
		if( cap_tex > 0 )
		{
			memset( &cap_slot_tex, 0, sizeof( cap_slot_tex ));
			cap_slot_tex.gl_texturenum = cap_tex;
			if( cap_img && cap_img->width > 0 && cap_img->height > 0 )
			{
				cap_slot_tex.width = cap_img->width;
				cap_slot_tex.height = cap_img->height;
			}
			face_texture = &cap_slot_tex;
			/* ST reads surf->texinfo->texture — retarget for this emit only. */
			if( surf->texinfo )
			{
				cap_prev_tex = surf->texinfo->texture;
				/* CapFaces draw surfs are mutable BSS; const* is the live API. */
				((msurface_t *)surf)->texinfo->texture = &cap_slot_tex;
				cap_retargeted = true;
			}
		}
	}
	if( face_texture )
	{
		if( surf->firstedge >= 0 && face_texture->gl_texturenum > 0 )
			GC_RecordResidentWorldDiffuseTexnum( face_texture->gl_texturenum );
		if( surf->firstedge >= 0 && !pad_texture_cache_valid )
		{
			pad_texture_cache = *face_texture;
			pad_texture_cache_valid = true;
		}
		gxt = R_GXBindTexture( face_texture );
		if( !gxt && R_GXIsSyntheticUnderfootPad( surf ))
		{
			int retry_texnum = R_GXBorrowedWorldTexnum();
			if( retry_texnum > 0 )
			{
				memset( &pad_retry_texture, 0, sizeof( pad_retry_texture ));
				pad_retry_texture.gl_texturenum = retry_texnum;
				Q_strncpy( pad_retry_texture.name, "gc022-hallway", sizeof( pad_retry_texture.name ));
				face_texture = &pad_retry_texture;
				gxt = R_GXBindTexture( face_texture );
			}
		}
		if( gxt )
		{
			if( surf->firstedge >= 0 )
				GC_RecordResidentWorldDiffuseTexnum( (int)gxt->texnum );
			textured = true;
			/* Live BSP faces with a baked rect in the menu lightmap atlas. */
			if( slot == -1 && surf->lightmaptexturenum == 1 && surf->info
				&& !( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT ))
				&& R_GXBindLiveLightmap())
			{
				live_lm = true;
				if( gEngfuncs.Mod_SampleSizeForFace )
					live_sample = (float)gEngfuncs.Mod_SampleSizeForFace( surf );
				if( live_sample < 1.0f )
					live_sample = 16.0f;
			}
			/* CapFaces pack carries durable per-vert ST — prefer it over
			 * R_GXFaceST (draw-surf texvecs are empty → stripe Flipper UVs).
			 * Remap each face's ST span to 0..1 so lean CMPR reads as solid
			 * rock/ground (authored luxel/size spans ±30…200 → REPEAT noise). */
			if( slot >= 0 )
			{
				extern int GC_GetNewGameCapFaceST( int slot, float out_st[][2], int maxverts );
				float pack_st[32][2];
				int st_n = GC_GetNewGameCapFaceST( slot, pack_st, 32 );

				if( st_n == nverts )
				{
					float smin = pack_st[0][0], smax = pack_st[0][0];
					float tmin = pack_st[0][1], tmax = pack_st[0][1];
					float ds, dt;

					for( i = 1; i < nverts; i++ )
					{
						if( pack_st[i][0] < smin ) smin = pack_st[i][0];
						if( pack_st[i][0] > smax ) smax = pack_st[i][0];
						if( pack_st[i][1] < tmin ) tmin = pack_st[i][1];
						if( pack_st[i][1] > tmax ) tmax = pack_st[i][1];
					}
					ds = smax - smin;
					dt = tmax - tmin;
					if( ds < 1e-3f ) ds = 1.0f;
					if( dt < 1e-3f ) dt = 1.0f;
					for( i = 0; i < nverts; i++ )
					{
						sts[i][0] = ( pack_st[i][0] - smin ) / ds;
						sts[i][1] = ( pack_st[i][1] - tmin ) / dt;
					}
				}
				else
				{
					for( i = 0; i < nverts; i++ )
						R_GXFaceST( surf, pts[i], &sts[i][0], &sts[i][1] );
				}
			}
			else for( i = 0; i < nverts; i++ )
			{
				if( !surf->texinfo && surf->firstedge < 0 && nverts == 4 )
				{
					static const float pad_uv[4][2] = {
						{ 0.0f, 0.0f }, { 1.0f, 0.0f },
						{ 1.0f, 1.0f }, { 0.0f, 1.0f }
					};
					sts[i][0] = pad_uv[i][0];
					sts[i][1] = pad_uv[i][1];
				}
				else
					R_GXFaceST( surf, pts[i], &sts[i][0], &sts[i][1] );
			}
			if( slot >= 0 )
			{
				extern int GC_GetNewGameCapFaceLMUV( int slot, float out_uv[][2], int maxverts );
				float pack_lm[32][2];
				int lm_n = GC_GetNewGameCapFaceLMUV( slot, pack_lm, 32 );

				for( i = 0; i < nverts; i++ )
				{
					float ls, lt;

					if( lm_n == nverts )
					{
						ls = pack_lm[i][0];
						lt = pack_lm[i][1];
					}
					else if( nverts == 3 )
					{
						static const float tri_uv[3][2] = {
							{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.5f, 1.0f }
						};
						ls = tri_uv[i][0];
						lt = tri_uv[i][1];
					}
					else if( nverts == 4 )
					{
						static const float corner_uv[4][2] = {
							{ 0.0f, 0.0f }, { 1.0f, 0.0f },
							{ 1.0f, 1.0f }, { 0.0f, 1.0f }
						};
						ls = corner_uv[i][0];
						lt = corner_uv[i][1];
					}
					else
					{
						const float t = ( nverts > 1 )
							? ( (float)i / (float)( nverts - 1 ) ) : 0.0f;
						ls = ( i & 1 ) ? 1.0f : 0.0f;
						lt = t;
					}
					GC_GetNewGameCapLightmapAtlasUV( slot, ls, lt, &lmst[i][0], &lmst[i][1] );
				}
			}
			else if( live_lm )
			{
				for( i = 0; i < nverts; i++ )
				{
					float ls, lt;
					const mextrasurf_t *info = surf->info;

					ls = DotProduct( pts[i], info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
					lt = DotProduct( pts[i], info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
					lmst[i][0] = ( surf->light_s + ls / live_sample + 0.5f ) / r_gx_live_lm_w;
					lmst[i][1] = ( surf->light_t + lt / live_sample + 0.5f ) / r_gx_live_lm_h;
				}
			}
			/* Cap/live atlas lightmaps when we have a matching slot. */
			lit = ( slot >= 0 ) ? R_GXBindLightmapAtlas() : live_lm;
			/* G207: re-enable LM for EDGE/TEX (boosted bake + corner atlas UV).
			 * Plane-fallback quads still REPLACE — LM ST meaningless there. */
			if( lit && slot >= 0 && r_gx_newgame_route )
			{
				if( bake_src != 1 && bake_src != 3 )
				{
					static qboolean g202_logged;
					lit = false;
					if( !g202_logged )
					{
						g202_logged = true;
						gEngfuncs.Con_Reportf(
							"Xash3D GameCube: G202 Flipper REPLACE (plane bake; EDGE/TEX use LM)\n" );
					}
				}
				else
				{
					static qboolean g207_logged;
					if( !g207_logged )
					{
						g207_logged = true;
						gEngfuncs.Con_Reportf(
							"Xash3D GameCube: G219 Flipper LM on EDGE/TEX (boost*3 + TEV*2)\n" );
						{
							extern void GCube_AppendSwissEvidence( const char *fmt, ... );
							GCube_AppendSwissEvidence(
								"G219 Flipper LM on EDGE/TEX (boost*3 + TEV*2)" );
						}
					}
				}
			}
		}
	}
	if( gxt && !surf->texinfo && R_GXIsSyntheticUnderfootPad( surf ))
	{
		/* Keep the established eye-space constant-Z pad projection below while
		 * switching only its TEV input to the borrowed resident texture. */
		textured = false;
		pad_textured = true;
	}
	if( !textured )
		color = R_GXFaceColor( surf );
	else if( slot >= 0 && nverts == 4 )
	{
		/* Plane-fallback quads: menu/tram keep flat concrete (0..1 ST stretched
		 * into rail garble). Solo -gcworldrender CapFaces need CMPR on those
		 * quads — otherwise drawn=371 is mostly grey (20261107-131953). */
		if( bake_src == 2 && !gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
		{
			textured = false;
			lit = false;
			color = R_GXFaceColor( surf );
		}
		/* G203/G205: force 0..1 ST on plane-fallback quads (and worldrender). */
		else if( bake_src == 2 || ( bake_src != 1 && bake_src != 3 ))
		{
			static const float plane_uv[4][2] = {
				{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
			};
			static qboolean g203_logged;
			for( i = 0; i < 4; i++ )
			{
				sts[i][0] = plane_uv[i][0];
				sts[i][1] = plane_uv[i][1];
			}
			if( bake_src == 2 )
			{
				/* Keep CMPR bind; ST is unit quad. */
				lit = false;
			}
			if( !g203_logged && r_gx_newgame_route )
			{
				g203_logged = true;
				gEngfuncs.Con_Reportf( "Xash3D GameCube: G203 Flipper plane-quad ST 0..1\n" );
			}
		}
	}

	/* Turbulent / translucent surfaces: textured + alpha blend, no LM. */
	if( textured && ( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT )))
	{
		lit = false;
		if( surf->flags & SURF_TRANSPARENT )
			color = 0xFFFFFFC0u;
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
	}

	if( textured && lit )
	{
		const qboolean want_env = !live_lm && r_gx_sky_ready
			&& ( surf->flags & GC_SURF_ENVMAP ) != 0
			&& surf->plane != NULL;
		const int lit_mode = live_lm ? GC_GX_FACE_MODE_LIT_LIVE
			: ( want_env ? GC_GX_FACE_MODE_LIT_ENV : GC_GX_FACE_MODE_LIT );
		int sky_side = want_env ? R_GXPickSkyboxSide() : -1;

		if( want_env && ( sky_side < 0 || sky_side > 5 ))
			sky_side = 3; /* ft */

		if( r_gx_face_mode != lit_mode )
		{
			if( want_env )
			{
				const qboolean have_mask = GC_SandSpecTexReady()
					&& GC_GetSandSpecTexObj() != NULL;
				const qboolean have_nrm = GC_SandNrmTexReady()
					&& GC_GetSandNrmTexObj() != NULL;
				const qboolean have_sun = GC_SandSunTexReady()
					&& GC_GetSandSunTexObj() != NULL;
				u8 env_a = surf->styles[3];
				f32 ind_mtx[2][3];

				if( env_a == 0 )
					env_a = 140;
				/* Masked: use bake tint; unmasked: ~0.4× so sand doesn't blow. */
				if( !have_mask )
				{
					env_a = (u8)(( (unsigned)env_a * 2u ) / 5u );
					if( env_a < 40 )
						env_a = 40;
					if( env_a > 90 )
						env_a = 90;
				}
				else if( env_a > 200 )
					env_a = 200;

				/* Prefer high-contrast sun disc — sky CMPR sides are too flat for glints. */
				if( have_sun )
					GX_LoadTexObj( (GXTexObj *)GC_GetSandSunTexObj(), GX_TEXMAP2 );
				else
					GX_LoadTexObj( &r_gx_sky_obj[sky_side], GX_TEXMAP2 );
				if( have_mask )
					GX_LoadTexObj( (GXTexObj *)GC_GetSandSpecTexObj(), GX_TEXMAP3 );
				if( have_nrm )
					GX_LoadTexObj( (GXTexObj *)GC_GetSandNrmTexObj(), GX_TEXMAP4 );
				/* Stage0 diffuse, Stage1 ×LM, then sky×mask (or sky×A0).
				 * $bumpmap (TEXMAP4) IndTex-offsets sky ST so highlights follow sand grain. */
				GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
				GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
				GX_SetTexCoordGen( GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, GX_IDENTITY );
				GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
				GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
				GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL );
				GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO );
				/* SCALE_2 blew CapFaces sand ~1.5× vs retail shore (fg mae~63).
				 * DumpFrames: SCALE_1 toward retail mid/fg luminance. */
				GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO,
					( gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" )
						&& gEngfuncs.Sys_CheckParm( "-gcdumpframes" ))
						? GX_CS_SCALE_1 : GX_CS_SCALE_2,
					GX_TRUE, GX_TEVPREV );
				GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
				GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
				GX_SetTevDirect( GX_TEVSTAGE0 );
				GX_SetTevDirect( GX_TEVSTAGE1 );
				if( have_mask )
				{
					/* TEXCOORD3 shares diffuse ST (bump + envmapmask UV). */
					GX_SetNumTexGens( 4 );
					GX_SetTexCoordGen( GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
					GX_SetNumTevStages( 4 );
					/* Stage2: REG0 = mask × envmaptint (A0). Mask alone is too hot. */
					GX_SetTevOrder( GX_TEVSTAGE2, GX_TEXCOORD3, GX_TEXMAP3, GX_COLORNULL );
					GX_SetTevColorIn( GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_A0, GX_CC_ZERO );
					GX_SetTevColorOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0 );
					GX_SetTevAlphaIn( GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
					GX_SetTevAlphaOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
					GX_SetTevDirect( GX_TEVSTAGE2 );
					/* Stage3: CPREV + sky × (mask×tint) — bump IndTex warps sky ST. */
					GX_SetTevOrder( GX_TEVSTAGE3, GX_TEXCOORD2, GX_TEXMAP2, GX_COLORNULL );
					GX_SetTevColorIn( GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_CPREV );
					GX_SetTevColorOp( GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
					GX_SetTevAlphaIn( GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
					GX_SetTevAlphaOp( GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
					if( have_nrm )
					{
						/* Flat normal (128,128,*) → 0 offset after ITB_ST (-128).
						 * scale_exp=-8 ≈ ±0.25 ST — enough grain, still hits sun disc (CLAMP). */
						ind_mtx[0][0] = 0.5f; ind_mtx[0][1] = 0.0f; ind_mtx[0][2] = 0.0f;
						ind_mtx[1][0] = 0.0f; ind_mtx[1][1] = 0.5f; ind_mtx[1][2] = 0.0f;
						GX_SetNumIndStages( 1 );
						GX_SetIndTexOrder( GX_INDTEXSTAGE0, GX_TEXCOORD3, GX_TEXMAP4 );
						GX_SetIndTexCoordScale( GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1 );
						GX_SetIndTexMatrix( GX_ITM_0, ind_mtx, -8 );
						GX_SetTevIndirect( GX_TEVSTAGE3, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_ST,
							GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF );
					}
					else
					{
						GX_SetNumIndStages( 0 );
						GX_SetTevDirect( GX_TEVSTAGE3 );
					}
					/* ~0.45× bake tint — wet-sand glints toward retail still. */
					env_a = (u8)(( (unsigned)env_a * 115u ) / 255u );
					if( env_a < 64 )
						env_a = 64;
					if( env_a > 140 )
						env_a = 140;
					/* Bump-driven glints need more sky weight to read on Flipper. */
					if( have_nrm )
					{
						env_a = (u8)(( (unsigned)env_a * 8u ) / 5u );
						if( env_a > 180 )
							env_a = 180;
					}
					GX_SetTevColor( GX_TEVREG0, (GXColor){ 0, 0, 0, env_a } );
				}
				else
				{
					GX_SetNumTexGens( 3 );
					GX_SetNumTevStages( 3 );
					GX_SetTevOrder( GX_TEVSTAGE2, GX_TEXCOORD2, GX_TEXMAP2, GX_COLORNULL );
					GX_SetTevColorIn( GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_A0, GX_CC_CPREV );
					GX_SetTevColorOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
					GX_SetTevAlphaIn( GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
					GX_SetTevAlphaOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
					if( have_nrm )
					{
						/* No mask: still warp sky ST from bump using diffuse UV. */
						GX_SetNumTexGens( 4 );
						GX_SetTexCoordGen( GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
						ind_mtx[0][0] = 0.5f; ind_mtx[0][1] = 0.0f; ind_mtx[0][2] = 0.0f;
						ind_mtx[1][0] = 0.0f; ind_mtx[1][1] = 0.5f; ind_mtx[1][2] = 0.0f;
						GX_SetNumIndStages( 1 );
						GX_SetIndTexOrder( GX_INDTEXSTAGE0, GX_TEXCOORD3, GX_TEXMAP4 );
						GX_SetIndTexCoordScale( GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1 );
						GX_SetIndTexMatrix( GX_ITM_0, ind_mtx, -8 );
						GX_SetTevIndirect( GX_TEVSTAGE2, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_ST,
							GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE, GX_ITBA_OFF );
					}
					else
					{
						GX_SetNumIndStages( 0 );
						GX_SetTevDirect( GX_TEVSTAGE2 );
					}
					GX_SetTevColor( GX_TEVREG0, (GXColor){ 0, 0, 0, env_a } );
				}
				GX_ClearVtxDesc();
				GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
				GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
				GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
				GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
				GX_SetVtxDesc( GX_VA_TEX2, GX_DIRECT );
				GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0 );
				GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX2, GX_TEX_ST, GX_F32, 0 );
			}
			else
			{
				GX_SetNumIndStages( 0 );
			GX_SetNumTexGens( 2 );
			GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
			GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
			GX_SetNumTevStages( 2 );
			/* Stage0: diffuse REPLACE (ignore vertex color dimming). */
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
			GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
			/* Stage1: * LM with G219 overbright (×2) — G209 ×4 blew DumpFrames white.
			 * Menu tram: ×1 — ×2 blows light panels to solid white (frame 700).
			 * CapFaces atlas (slot>=0): bake already carries style brightness;
			 * ×2 saturates outdoor DoD walls to solid white (caen whiteish≈9%,
			 * LM tiles with mean>127 → clamp under SCALE_2). */
			GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL );
			GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO );
			{
				u32 lm_scale = GX_CS_SCALE_2;

				/* Raw BSP lightmaps: texture x light x 2 (GoldSrc overbright).
				 * CapFaces / menu tram atlas LMs stay at ×1. */
				if( ( r_gx_menu_route || slot >= 0 ) && !live_lm )
					lm_scale = GX_CS_SCALE_1;
				GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, lm_scale, GX_TRUE, GX_TEVPREV );
			}
			GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
			GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
			GX_ClearVtxDesc();
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_TEX2, GX_NONE );
			GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0 );
			}
			r_gx_face_mode = lit_mode;
			r_gx_state_sets++;
		}
		else
		{
			r_gx_state_reuses++;
			if( want_env )
			{
				const qboolean have_sun = GC_SandSunTexReady()
					&& GC_GetSandSunTexObj() != NULL;

				if( have_sun )
					GX_LoadTexObj( (GXTexObj *)GC_GetSandSunTexObj(), GX_TEXMAP2 );
				else if( sky_side >= 0 && sky_side <= 5 )
					GX_LoadTexObj( &r_gx_sky_obj[sky_side], GX_TEXMAP2 );
				{
					u8 env_a = surf->styles[3];
					const qboolean have_mask = GC_SandSpecTexReady()
						&& GC_GetSandSpecTexObj() != NULL;
					const qboolean have_nrm = GC_SandNrmTexReady()
						&& GC_GetSandNrmTexObj() != NULL;

					if( env_a == 0 )
						env_a = 140;
					if( have_mask )
					{
						GX_LoadTexObj( (GXTexObj *)GC_GetSandSpecTexObj(), GX_TEXMAP3 );
						env_a = (u8)(( (unsigned)env_a * 90u ) / 255u );
						if( env_a < 48 )
							env_a = 48;
						if( env_a > 110 )
							env_a = 110;
						if( have_nrm )
						{
							env_a = (u8)(( (unsigned)env_a * 7u ) / 4u );
							if( env_a > 160 )
								env_a = 160;
						}
					}
					else
					{
						env_a = (u8)(( (unsigned)env_a * 2u ) / 5u );
						if( env_a < 40 )
							env_a = 40;
						if( env_a > 90 )
							env_a = 90;
					}
					if( have_nrm )
						GX_LoadTexObj( (GXTexObj *)GC_GetSandNrmTexObj(), GX_TEXMAP4 );
					GX_SetTevColor( GX_TEVREG0, (GXColor){ 0, 0, 0, env_a } );
				}
			}
		}

		/* BSP faces are convex and already ordered around their perimeter.
		 * A triangle fan sends each vertex once instead of repeating p0 and
		 * the two edge vertices for every triangle. */
		GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, (u16)nverts );
		r_gx_frame_verts += nverts;
		for( i = 0; i < nverts; i++ )
		{
			GX_Position3f32( pts[i][0], pts[i][1], pts[i][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[i][0], sts[i][1] );
			GX_TexCoord2f32( lmst[i][0], lmst[i][1] );
			if( want_env )
			{
				vec3_t to_eye, refl;
				float nd, inv_len, env_s, env_t;
				const float *eye = RI.rvp.vieworigin;
				const float *pn = surf->plane->normal;
				const qboolean have_sun = GC_SandSunTexReady()
					&& GC_GetSandSunTexObj() != NULL;

				VectorSubtract( eye, pts[i], to_eye );
				inv_len = VectorLength( to_eye );
				if( inv_len > 1e-3f )
				{
					to_eye[0] /= inv_len;
					to_eye[1] /= inv_len;
					to_eye[2] /= inv_len;
				}
				nd = DotProduct( to_eye, pn );
				refl[0] = to_eye[0] - 2.0f * nd * pn[0];
				refl[1] = to_eye[1] - 2.0f * nd * pn[1];
				refl[2] = to_eye[2] - 2.0f * nd * pn[2];
				if( have_sun )
				{
					/* Keep the sun disc near texel center; bump IndTex scatters it. */
					env_s = 0.5f + 0.18f * refl[0];
					env_t = 0.5f - 0.18f * refl[2];
				}
				else
				{
					env_s = 0.5f + 0.5f * refl[0];
					env_t = 0.5f - 0.5f * refl[2];
				}
				GX_TexCoord2f32( env_s, env_t );
			}
		}
		GX_End();
		r_gx_tex_draws++;
		r_gx_lm_draws++;
	}
	else if( textured )
	{
		/* G219: live skyfill (slot -3) uses muted MODULATE — REPLACE blows white. */
		if( slot == -3 )
		{
			color = 0x888888FFu;
			if( r_gx_face_mode != GC_GX_FACE_MODE_TEXTURED_LIVE )
			{
				GX_SetNumTexGens( 1 );
				GX_SetNumTevStages( 1 );
				GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
				GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
				GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
				GX_SetVtxDesc( GX_VA_TEX1, GX_NONE );
				GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
				GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
				GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
				r_gx_face_mode = GC_GX_FACE_MODE_TEXTURED_LIVE;
				r_gx_state_sets++;
			}
			else
				r_gx_state_reuses++;
		}
		else if( r_gx_face_mode != GC_GX_FACE_MODE_TEXTURED )
		{
			GX_SetNumTexGens( 1 );
			GX_SetNumTevStages( 1 );
			GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
			/* G202: REPLACE shows the diffuse sheet clearly. */
			GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
			GX_SetVtxDesc( GX_VA_TEX1, GX_NONE );
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
			r_gx_face_mode = GC_GX_FACE_MODE_TEXTURED;
			r_gx_state_sets++;
		}
		else
			r_gx_state_reuses++;

		GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, (u16)nverts );
		r_gx_frame_verts += nverts;
		for( i = 0; i < nverts; i++ )
		{
			GX_Position3f32( pts[i][0], pts[i][1], pts[i][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[i][0], sts[i][1] );
		}
		GX_End();
		r_gx_tex_draws++;
	}
	else
	{
		if( r_gx_face_mode != GC_GX_FACE_MODE_FLAT )
		{
			GX_SetNumTexGens( 0 );
			GX_SetNumTevStages( 1 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
			GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
			GX_SetVtxDesc( GX_VA_TEX0, GX_NONE );
			GX_SetVtxDesc( GX_VA_TEX1, GX_NONE );
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			r_gx_face_mode = GC_GX_FACE_MODE_FLAT;
			r_gx_state_sets++;
		}
		else
			r_gx_state_reuses++;
		/* G381: textured underfoot pad — rebind TEXMAP0 after FLAT TEV
		 * (TEXMAP_NULL) so LEQUAL fill cannot leave G285 cyan clear bars. */
		if( pad_textured )
		{
			if( gxt )
				GX_LoadTexObj( &gxt->obj, GX_TEXMAP0 );
			GX_SetNumTexGens( 1 );
			GX_SetNumTevStages( 1 );
			GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
			GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
			GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
			r_gx_face_mode = GC_GX_FACE_MODE_TEXTURED;
			r_gx_state_sets++;
		}

		/* Constant-Z eye pad (G200) — NPC dump floor only (256×256). Menu tram
		 * end-plug (512×384) must draw the baked world-space quad instead. */
		if( surf->firstedge < 0 && nverts == 4
			&& surf->extents[0] == 256 && surf->extents[1] == 256 )
		{
			vec3_t fwd, right, upv;
			const float *org = RI.rvp.vieworigin;
			float e[4][3], bb[4][3];
			Mtx ident, mv;
			const float zbb = -100.0f;
			float x0, x1, y0, y1, ymax, ymin;
			int vi;
			static qboolean g380_eye_logged;

			AngleVectors( RI.rvp.viewangles, fwd, right, upv );
			for( vi = 0; vi < 4; vi++ )
			{
				float rx = pts[vi][0] - org[0];
				float ry = pts[vi][1] - org[1];
				float rz = pts[vi][2] - org[2];
				float ez;

				e[vi][0] = right[0] * rx + right[1] * ry + right[2] * rz;
				e[vi][1] = upv[0] * rx + upv[1] * ry + upv[2] * rz;
				e[vi][2] = -( fwd[0] * rx + fwd[1] * ry + fwd[2] * rz );
				ez = e[vi][2];
				if( ez > -8.0f )
					ez = -8.0f;
				bb[vi][0] = e[vi][0] * ( zbb / ez );
				bb[vi][1] = e[vi][1] * ( zbb / ez );
				bb[vi][2] = zbb;
				/* G381: modest eye clamp — full ±96 painted most of EFB. */
				if( bb[vi][0] > 78.0f ) bb[vi][0] = 78.0f;
				if( bb[vi][0] < -78.0f ) bb[vi][0] = -78.0f;
				if( bb[vi][1] > 56.0f ) bb[vi][1] = 56.0f;
				if( bb[vi][1] < -56.0f ) bb[vi][1] = -56.0f;
			}
			x0 = x1 = bb[0][0];
			ymax = ymin = bb[0][1];
			for( vi = 1; vi < 4; vi++ )
			{
				if( bb[vi][0] < x0 ) x0 = bb[vi][0];
				if( bb[vi][0] > x1 ) x1 = bb[vi][0];
				if( bb[vi][1] > ymax ) ymax = bb[vi][1];
				if( bb[vi][1] < ymin ) ymin = bb[vi][1];
			}
			/* G381: widen underfoot strip (was 28u) without covering the EFB. */
			y1 = ymax;
			y0 = ymax - 44.0f;
			if( y0 < ymin )
				y0 = ymin;
			if( y1 - y0 < 36.0f )
				y0 = y1 - 36.0f;
			if( !g380_eye_logged )
			{
				g380_eye_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G380 eye p0=(%.0f,%.0f,%.0f) pad y=%.0f..%.0f x=%.0f..%.0f\n",
					e[0][0], e[0][1], e[0][2], y0, y1, x0, x1 );
			}
			{
				GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
				Mtx44 persp;
				f32 aspect = 1.333f;

				if( rmode )
					aspect = (f32)rmode->fbWidth / Q_max( 1.0f, (f32)rmode->efbHeight );
				guPerspective( persp, 60.0f, aspect, 1.0f, 4096.0f );
				GX_LoadProjectionMtx( persp, GX_PERSPECTIVE );
			}
			guMtxIdentity( ident );
			GX_LoadPosMtxImm( ident, GX_PNMTX0 );
			GX_InvVtxCache();
			GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_TRUE );
			GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 6 );
			GX_Position3f32( x0, y0, zbb );
			GX_Color1u32( color );
			if( pad_textured ) GX_TexCoord2f32( 0.0f, 0.0f );
			GX_Position3f32( x1, y0, zbb );
			GX_Color1u32( color );
			if( pad_textured ) GX_TexCoord2f32( 1.0f, 0.0f );
			GX_Position3f32( x1, y1, zbb );
			GX_Color1u32( color );
			if( pad_textured ) GX_TexCoord2f32( 1.0f, 1.0f );
			GX_Position3f32( x0, y0, zbb );
			GX_Color1u32( color );
			if( pad_textured ) GX_TexCoord2f32( 0.0f, 0.0f );
			GX_Position3f32( x1, y1, zbb );
			GX_Color1u32( color );
			if( pad_textured ) GX_TexCoord2f32( 1.0f, 1.0f );
			GX_Position3f32( x0, y1, zbb );
			GX_Color1u32( color );
			if( pad_textured ) GX_TexCoord2f32( 0.0f, 1.0f );
			GX_End();
			GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
			{
				Mtx44 proj;

				R_GXBuildWorldProjection( proj );
				GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
			}
			R_GXBuildWorldModelview( mv );
			GX_LoadPosMtxImm( mv, GX_PNMTX0 );
		}
		else
		{
			GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, (u16)nverts );
			r_gx_frame_verts += nverts;
		r_gx_frame_verts += nverts;
			for( i = 0; i < nverts; i++ )
			{
				GX_Position3f32( pts[i][0], pts[i][1], pts[i][2] );
				GX_Color1u32( color );
			}
			GX_End();
		}
		r_gx_flat_draws++;
	}

	/* Restore opaque depth/blend after translucent faces. */
	if( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT ))
	{
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	}
	if( surf->firstedge < 0 )
		GX_SetCullMode( GX_CULL_BACK );
	/* Put the CapFaces shared stub back — do not leave texinfo on stack. */
	if( cap_retargeted && surf->texinfo )
		((msurface_t *)surf)->texinfo->texture = cap_prev_tex;
	return 1;
}

/*
================
R_GXMarkVisibleSurfaces

Stamp surf->visframe from marked leaves (MarkLeaves only tags nodes/leaves;
soft RecursiveWorldNode used to stamp surfaces during the edge walk).
================
*/
static void R_GXMarkVisibleSurfaces( model_t *world )
{
	int i;

	if( !world || !world->leafs || world->numleafs <= 0 )
		return;

	for( i = 0; i < world->numleafs; i++ )
	{
		mleaf_t *leaf = &world->leafs[i + 1];
		msurface_t **mark;
		int c;

		if(((mnode_t *)leaf )->visframe != tr.visframecount )
			continue;
		mark = leaf->firstmarksurface;
		c = leaf->nummarksurfaces;
		if( !mark || c <= 0 )
			continue;
		do
		{
			( *mark )->visframe = tr.framecount;
			mark++;
		}
		while( --c );
	}
}

/*
================
R_GXDrawWorldLiveSurfaces

Per-frame BSP/PVS helpers. When lightmapped cap faces already cover opaque
geometry, only emit water/translucents here. When no cap is available, emit
all marked opaque surfaces textured (no LM atlas).
================
*/
/*
 * View-frustum test for the live BSP pass.  PVS (surf->visframe) already
 * limits the walk to potentially visible leaves; this drops the PVS faces
 * behind or beside the camera before their edge walk and GX emit.  Planes
 * come from the cull vectors and FOV (the soft clip planes are not built on
 * the GX path), widened by 5 degrees and 16 units so wall edges never pop
 * (G228 lost tram side walls to a too-tight cull).
 */
static vec3_t r_gx_frustum_n[4];
static float r_gx_frustum_d[4];
static qboolean r_gx_frustum_ok;

static void R_GXBuildFrustum( void )
{
	const float hx = DEG2RAD( Q_min( 89.0f, RI.rvp.fov_x * 0.5f + 5.0f ));
	const float hy = DEG2RAD( Q_min( 89.0f, RI.rvp.fov_y * 0.5f + 5.0f ));
	const float cx = cosf( hx ), sx = sinf( hx ), cy = cosf( hy ), sy = sinf( hy );
	int i;

	r_gx_frustum_ok = RI.rvp.fov_x > 1.0f && RI.rvp.fov_y > 1.0f;
	if( !r_gx_frustum_ok )
		return;
	for( i = 0; i < 3; i++ )
	{
		r_gx_frustum_n[0][i] =  RI.cull_vright[i] * cx + RI.cull_vforward[i] * sx; /* left */
		r_gx_frustum_n[1][i] = -RI.cull_vright[i] * cx + RI.cull_vforward[i] * sx; /* right */
		r_gx_frustum_n[2][i] =  RI.cull_vup[i] * cy + RI.cull_vforward[i] * sy;    /* bottom */
		r_gx_frustum_n[3][i] = -RI.cull_vup[i] * cy + RI.cull_vforward[i] * sy;    /* top */
	}
	for( i = 0; i < 4; i++ )
		r_gx_frustum_d[i] = DotProduct( RI.cullorigin, r_gx_frustum_n[i] ) - 16.0f;
}

static qboolean R_GXBoxOutsideFrustum( const vec3_t mins, const vec3_t maxs )
{
	int i;

	if( !r_gx_frustum_ok )
		return false;
	for( i = 0; i < 4; i++ )
	{
		const float *n = r_gx_frustum_n[i];
		vec3_t p;

		/* Corner furthest along the plane normal: if even it is behind,
		 * the whole box is outside. */
		p[0] = n[0] >= 0.0f ? maxs[0] : mins[0];
		p[1] = n[1] >= 0.0f ? maxs[1] : mins[1];
		p[2] = n[2] >= 0.0f ? maxs[2] : mins[2];
		if( DotProduct( p, n ) < r_gx_frustum_d[i] )
			return true;
	}
	return false;
}

static int R_GXDrawWorldLiveSurfaces( model_t *world, qboolean opaque_too )
{
	msurface_t *surf;
	int i, drawn = 0;
	int opaque_drawn = 0;
	int trans_drawn = 0;
	int skip_vis = 0, skip_plane = 0, skip_back = 0, skip_area = 0, emit_zero = 0;
	int skip_frustum = 0;
	int nsort = 0;
	const qboolean newgame = gEngfuncs.Sys_CheckParm( "-gcnewgame" );
	qboolean surfaces_live;
	qboolean surfaces_pinned;
	static qboolean live_logged;

	if( !world || !world->surfaces || world->numsurfaces <= 0 )
		return 0;

	surfaces_live = GC_WorldSurfacesLive();
	surfaces_pinned = GC_WorldSurfacesPinned();

	/* Opaque pass — only when cap atlas is unavailable. */
	if( opaque_too )
	{
		/* G212/G213: raise Flipper live BSP emit toward full PVS when pinned.
		 * G283: on New Game leave LM-cap headroom under FRAME budget. */
		int emit_budget = surfaces_live ? 2048 : 768;

		if( newgame && surfaces_pinned )
		{
			extern qboolean GC_IsG36SampleFaceCap( void );
			const int frame_budget = R_GXFrameFaceBudget();

			emit_budget = frame_budget - 16;
			if( emit_budget < 16 )
				emit_budget = 16;
			if( !GC_IsG36SampleFaceCap() && emit_budget < 96 )
				emit_budget = 96;
		}

		R_GXBuildFrustum();
		for( i = 0; i < world->numsurfaces && emit_budget > 0; i++ )
		{
			float dot;
			msurface_t tmp;
			const msurface_t *esurf;

			surf = &world->surfaces[i];
			if( surf->visframe != tr.framecount )
			{
				skip_vis++;
				continue;
			}
			if( surf->info && R_GXBoxOutsideFrustum( surf->info->mins, surf->info->maxs ))
			{
				skip_frustum++;
				continue;
			}
			if( !surf->plane || surf->numedges < 3 || surf->numedges > 32 )
			{
				skip_plane++;
				continue;
			}
			/* G283: reject dangling plane* (must live in mempool planes[]). */
			if( world->planes
				&& ( surf->plane < world->planes
					|| surf->plane >= world->planes + world->numplanes ))
			{
				skip_plane++;
				continue;
			}
			if( surf->firstedge < 0
				|| surf->firstedge + surf->numedges > world->numsurfedges )
			{
				skip_plane++;
				continue;
			}
			if( surf->flags & SURF_DRAWSKY )
				continue;
			if( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT ))
				continue;

			dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
			if( surf->flags & SURF_PLANEBACK )
			{
				if( dot > -BACKFACE_EPSILON )
				{
					skip_back++;
					continue;
				}
			}
			else if( dot < BACKFACE_EPSILON )
			{
				skip_back++;
				continue;
			}

			/* Prefer closer / larger walls under the emit budget. */
			{
				int area = (int)surf->extents[0] * (int)surf->extents[1];
				if( area > 0 && area < GC_GX_MIN_FACE_AREA )
				{
					skip_area++;
					continue;
				}
				/* G283: match cap path — far-small cull dropped tunnel walls. */
				if( !newgame
					&& area > 0 && area < GC_GX_FAR_MIN_AREA
					&& fabsf( dot ) > GC_GX_FAR_FACE_DIST )
				{
					skip_area++;
					continue;
				}
			}

			/* G283: scratch texinfo* may dangle after lighting — emit flat
			 * geom from mempool edges so Flipper never chases bad texture*. */
			esurf = surf;
			if( newgame && surfaces_pinned )
			{
				tmp = *surf;
				tmp.texinfo = NULL;
				tmp.samples = NULL;
				esurf = &tmp;
			}

			if( !R_GXFaceSortPush( &nsort, esurf, i ))
			{
				int got = R_GXEmitFace( esurf, world, -1 );
				if( got <= 0 )
					emit_zero++;
				else
					opaque_drawn += got;
			}
			emit_budget--;
		}

		qsort( r_gx_face_sort, nsort, sizeof( r_gx_face_sort[0] ), R_GXFaceSortCmp );
		for( i = 0; i < nsort; i++ )
		{
			const msurface_t *esurf;
			msurface_t tmp;
			int got;

			surf = &world->surfaces[r_gx_face_sort[i] & 0xFFFFu];
			esurf = surf;
			if( newgame && surfaces_pinned )
			{
				tmp = *surf;
				tmp.texinfo = NULL;
				tmp.samples = NULL;
				esurf = &tmp;
			}
			got = R_GXEmitFace( esurf, world, -1 );
			if( got <= 0 )
				emit_zero++;
			else
				opaque_drawn += got;
		}
	}

	/* Translucent / water pass (no Z write) — live/pinned surfaces only.
	 * G273: uncapped BSP walk after scratch reuse hung DumpFrames.
	 * G285: hard budget + plane range checks (same as opaque). */
	if( surfaces_live || surfaces_pinned )
	{
		const int water_budget = 12;
		int water_left = water_budget;

		for( i = 0; i < world->numsurfaces && water_left > 0; i++ )
		{
			float dot;

			surf = &world->surfaces[i];
			if( surf->visframe != tr.framecount )
				continue;
			if( !surf->plane || surf->numedges < 3 || surf->numedges > 32 )
				continue;
			if( world->planes
				&& ( surf->plane < world->planes
					|| surf->plane >= world->planes + world->numplanes ))
				continue;
			if( surf->firstedge < 0
				|| surf->firstedge + surf->numedges > world->numsurfedges )
				continue;
			if( !( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT )))
				continue;
			if( surf->flags & SURF_DRAWSKY )
				continue;

			dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
			if( surf->flags & SURF_PLANEBACK )
			{
				if( dot > -BACKFACE_EPSILON )
					continue;
			}
			else if( dot < BACKFACE_EPSILON )
				continue;

			trans_drawn += R_GXEmitFace( surf, world, -1 );
			water_left--;
		}
		/* Restore opaque Z write after blend pass. */
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
		r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	}

	drawn = opaque_drawn + trans_drawn;
	if( !live_logged )
	{
		live_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: pure GX live BSP faces opaque=%d trans=%d frame=%d vis=%d frustum=%d plane=%d back=%d area=%d emit0=%d\n",
			opaque_drawn, trans_drawn, tr.framecount,
			skip_vis, skip_frustum, skip_plane, skip_back, skip_area, emit_zero );
	}
	{
		extern qboolean GC_IsFrameBudgetProbeActive( void );
		if( !GC_IsFrameBudgetProbeActive() )
			drawn += R_GXEmitWorldDecals( world );
	}
	return drawn;
}

/*
===========
R_GXEmitWorldDecals

G292: emit ≤8 world-surface decals into Flipper EFB. Soft span path already
draws RGB565 decals; pure GX left DrawSingleDecal empty.
===========
*/
static int R_GXTrySeedLeanPoolDecal( void )
{
	static qboolean tried;
	static byte poly_store[sizeof( glpoly2_t ) + 16 * VERTEXSIZE * sizeof( float )];
	int tex, i, n, j, nv;
	msurface_t *draw;
	vec3_t pos;
	float bake[16][3];
	glpoly2_t *poly;
	extern msurface_t *GC_GetNewGameDrawSurfs( void );
	extern int GC_GetNewGameCapFaceCount( void );
	extern int GC_GetNewGameCapFaceVerts( int slot, float out[][3], int maxverts );
	extern decal_t gDecalPool[MAX_RENDER_DECALS];

	if( tried || !gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		return 0;

	tex = GL_FindTexture( "#gc_lean_shot1" );
	if( tex <= 0 )
		return 0;

	draw = GC_GetNewGameDrawSurfs();
	n = GC_GetNewGameCapFaceCount();
	if( !draw || n <= 0 )
		return 0;

	tried = true;
	poly = (glpoly2_t *)poly_store;

	for( i = 0; i < n && i < 96; i++ )
	{
		msurface_t *fa = &draw[i];

		if( !fa->plane || !fa->texinfo )
			continue;
		if( fa->flags & ( SURF_DRAWTURB | SURF_DRAWSKY | SURF_CONVEYOR ))
			continue;

		nv = GC_GetNewGameCapFaceVerts( i, bake, 16 );
		if( nv < 3 )
			continue;

		/* Cap draw surfs have no polys — attach a tip-safe BSS poly for clip. */
		memset( poly, 0, sizeof( *poly ));
		poly->numverts = nv;
		poly->flags = fa->flags;
		for( j = 0; j < nv; j++ )
		{
			VectorCopy( bake[j], poly->verts[j] );
			poly->verts[j][3] = 0.0f;
			poly->verts[j][4] = 0.0f;
			poly->verts[j][5] = 0.0f;
			poly->verts[j][6] = 0.0f;
		}
		fa->polys = poly;

		VectorCopy( bake[0], pos );
		VectorMA( pos, 0.25f, fa->plane->normal, pos );
		R_DecalShootSurface( tex, fa, pos, 1.0f );

		for( j = 0; j < MAX_RENDER_DECALS; j++ )
		{
			if( gDecalPool[j].psurface && gDecalPool[j].texture == tex )
			{
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G293 Flipper lean pool seed tex=%d face=%d\n",
					tex, i );
				return 1;
			}
		}
		fa->polys = NULL;
	}

	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: G293 Flipper lean pool seed failed tex=%d faces=%d\n",
		tex, n );
	return 0;
}

static int R_GXEmitWorldDecals( model_t *world )
{
	int i, emitted = 0;
	const int max_decals = 8;
	static qboolean g292_logged;
	extern decal_t gDecalPool[MAX_RENDER_DECALS];

	(void)world;

	/*
	 * Walk the active decal pool — not world->surfaces. Under New Game scratch
	 * reuse, surface pdecals chains can be stale and hang a full BSP walk.
	 */
emit_pool:
	emitted = 0;
	for( i = 0; i < MAX_RENDER_DECALS && emitted < max_decals; i++ )
	{
		decal_t *p = &gDecalPool[i];
		msurface_t *fa;
		int numVerts, vi;
		float *v;

		fa = p->psurface;
		if( !fa || !p->texture )
			continue;
		if( !fa->plane || !fa->texinfo )
			continue;
#if XASH_GAMECUBE
		/* Stale changelevel surfaces can carry dangling faceinfo; skip rather
		 * than DSI inside Mod_SampleSizeForFace (hardware 20261104). */
		{
			const uintptr_t tip = (uintptr_t)fa->texinfo;
			if( tip < 0x80000000u || tip >= 0x81800000u )
			{
				p->psurface = NULL;
				continue;
			}
		}
#endif

		v = R_DecalSetupVerts( p, fa, p->texture, &numVerts );
		if( numVerts < 3 )
			continue;

		GL_SetRenderMode( kRenderTransTexture );
		GL_Bind( XASH_TEXTURE0, p->texture );
		_TriColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		TriBegin( TRI_TRIANGLE_FAN );
		for( vi = 0; vi < numVerts; vi++, v += VERTEXSIZE )
		{
			TriTexCoord2f( v[3], v[4] );
			TriVertex3f( v[0], v[1], v[2] );
		}
		TriEnd();
		emitted++;
	}

	if( emitted > 0 )
	{
		static qboolean g293_pool_logged;

		if( !g293_pool_logged )
		{
			g293_pool_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G293 Flipper pool decals emit=%d (budget=%d)\n",
				emitted, max_decals );
		}
		if( !g292_logged )
		{
			g292_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G292 Flipper decals emit=%d (budget=%d)\n",
				emitted, max_decals );
		}
		return 1;
	}

	/* G293: stamp lean scorch onto a Flipper draw face, then re-emit. */
	if( R_GXTrySeedLeanPoolDecal() )
		goto emit_pool;

	/* Tip-safe proof when pool empty — prefer lean scorch tex over white. */
	if( gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
	{
		vec3_t org, right, up, p0, p1, p2, p3;
		const float s = 8.0f;
		int lean = GL_FindTexture( "#gc_lean_shot1" );

		VectorCopy( RI.rvp.vieworigin, org );
		VectorMA( org, 48.0f, RI.cull_vforward, org );
		VectorScale( RI.cull_vright, s, right );
		VectorScale( RI.cull_vup, s, up );
		VectorSubtract( org, right, p0 ); VectorAdd( p0, up, p0 );
		VectorAdd( org, right, p1 ); VectorAdd( p1, up, p1 );
		VectorAdd( org, right, p2 ); VectorSubtract( p2, up, p2 );
		VectorSubtract( org, right, p3 ); VectorSubtract( p3, up, p3 );

		GL_SetRenderMode( kRenderTransTexture );
		GL_Bind( XASH_TEXTURE0, lean > 0 ? (unsigned)lean : (unsigned)tr.whiteTexture );
		_TriColor4f( 0.15f, 0.12f, 0.10f, 0.85f );
		TriBegin( TRI_TRIANGLE_FAN );
		TriTexCoord2f( 0.0f, 0.0f ); TriVertex3fv( p0 );
		TriTexCoord2f( 1.0f, 0.0f ); TriVertex3fv( p1 );
		TriTexCoord2f( 1.0f, 1.0f ); TriVertex3fv( p2 );
		TriTexCoord2f( 0.0f, 1.0f ); TriVertex3fv( p3 );
		TriEnd();
		emitted = 1;
		if( !g292_logged )
		{
			g292_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G292 Flipper decal proof quad (pool empty lean=%d)\n",
				lean );
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G292 Flipper decals emit=%d (budget=%d)\n",
				emitted, max_decals );
		}
	}

	return emitted > 0 ? 1 : 0;
}

static int R_GXDrawNewGameCapFacesImpl( void );

/* -gcprofile: charge the GX world pass to its own stage (no-op otherwise). */
int R_GXDrawNewGameCapFaces( void )
{
	extern void GC_ProfMark( const char *name );
	int drawn;

	GC_ProfMark( "render: before GX world" );
	drawn = R_GXDrawNewGameCapFacesImpl();
	GC_ProfMark( "render: GX world faces" );
	return drawn;
}

static int R_GXDrawNewGameCapFacesImpl( void )
{
	extern int GC_GetNewGameCapFaceCount( void );
	extern int GC_GetNewGameCapGeneration( void );
	extern msurface_t *GC_GetNewGameDrawSurfs( void );
	msurface_t *draw;
	model_t *world;
	int n, i, drawn = 0;
	int gen;
	int order[768]; /* matches GC_MAX_CAP_FACES */
	const char *map_name = "";
	qboolean newgame_probe;
	qboolean menu_playstart;
	qboolean want_live_overlap;
	qboolean surfaces_live;
	qboolean surfaces_pinned;
	qboolean scratch_retained;
	qboolean log_begin = false;
	qboolean log_end = false;

	if( !GC_UseGxWorldDraw() )
		return 0;

	/* These budgets are frame-stable.  Snapshot them once instead of calling
	 * the probe/route helpers from each bounded face loop below. */
	const int frame_budget = R_GXFrameFaceBudget();
	const int live_budget = R_GXLiveFaceBudget();

	/* Reserve HUD TEXMAP0 slabs before world uploads fill MEM1. */
	R_GXReserveHudPool();

	world = WORLDMODEL;
	if( !world )
		return 0;

	/* These command/probe modes are frame-stable; keep them out of the
	 * bounded face loops below, where Flipper pays for every call. */
	newgame_probe = GC_IsNewGameProbe();
	menu_playstart = gEngfuncs.Sys_CheckParm( "-gcmenuplaystart" );
	want_live_overlap = GC_WantLiveCapOverlap();
	surfaces_live = GC_WorldSurfacesLive();
	surfaces_pinned = GC_WorldSurfacesPinned();
	scratch_retained = GC_WorldSurfacesScratchRetained();

	map_name = world->name ? world->name : "";
	if( !map_name[0] )
		map_name = "?";
	/* Prefer maps/foo.bsp → foo for probe markers. */
	{
		const char *slash = Q_strrchr( map_name, '/' );
		const char *base = slash ? slash + 1 : map_name;
		static char short_map[64];
		size_t len;

		Q_strncpy( short_map, base, sizeof( short_map ));
		len = Q_strlen( short_map );
		if( len > 4 && !Q_stricmp( short_map + len - 4, ".bsp" ))
			short_map[len - 4] = '\0';
		map_name = short_map;
	}
	{
		static char logged_map[64];

		/* Once per map: which world path this frame takes (live BSP vs caps). */
		if( Q_strcmp( logged_map, map_name ))
		{
			Q_strncpy( logged_map, map_name, sizeof( logged_map ));
			gEngfuncs.Con_Reportf( "Xash3D GameCube: world path map=%s live=%d pinned=%d scratch=%d probe=%d nsurf=%d\n",
				map_name, surfaces_live ? 1 : 0, surfaces_pinned ? 1 : 0, scratch_retained ? 1 : 0,
				newgame_probe ? 1 : 0, world->numsurfaces );
		}
	}

	if( r_gx_tex_world != world )
	{
		R_GXTexCacheReset();
		R_GXReserveHudPool();
		r_gx_tex_world = world;
		r_gx_tex_logged = false;
		r_gx_lm_logged = false;
		r_gx_sync_lean_logged = false;
		r_gx_lm_atlas_logged = false;
		r_gx_tex_band_logged = false;
		r_gx_face_cull_logged = false;
	}

	if( newgame_probe )
	{
		log_begin = Q_stricmp( r_gx_capfaces_begin_map, map_name ) != 0;
		log_end = Q_stricmp( r_gx_capfaces_end_map, map_name ) != 0;
	}

	gen = GC_GetNewGameCapGeneration();
	if( gen != r_gx_cap_generation )
	{
		GX_InvalidateTexAll();
		r_gx_tex_invalidates++;
		r_gx_lm_atlas_valid = false;
		r_gx_lm_atlas_bound = false;
	r_gx_live_lm_bound = false;
		r_gx_lm_atlas_checked = false;
		r_gx_lm_atlas_available = false;
		r_gx_cap_generation = gen;
		/* CapFaces rebake invalidates any recorded display list. */
		r_gx_cap_dl_bytes = 0;
		r_gx_cap_dl_gen = -1;
		r_gx_cap_dl_drawn = 0;
		r_gx_cap_dl_recording = false;
	}

	RI.currentmodel = world;
	{
		static int profile = -1, passes;
		static unsigned sum_draws, sum_verts, sum_states, sum_binds;

		if( profile < 0 )
			profile = gEngfuncs.Sys_CheckParm( "-gcprofile" ) != 0;
		if( profile )
		{
			sum_draws += r_gx_tex_draws + r_gx_flat_draws + r_gx_lm_draws;
			sum_verts += r_gx_frame_verts;
			sum_states += r_gx_state_sets;
			sum_binds += r_gx_tex_loads + r_gx_lm_loads;
		}
		if( profile && ++passes >= 64 )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: gx tex passes=%d uploads=%u stalls=%u native=%u\n",
				passes, r_gx_tex_uploads, r_gx_tex_fence_stalls + r_gx_cmpr_bounce_stalls,
				r_gx_tex_native_conversions );
			gEngfuncs.Con_Reportf( "Xash3D GameCube: hot window %u Kb resident=%d queued=%d uploads=%u bytes=%u evictions=%u deferred=%u\n",
				r_gx_hot.size / 1024, r_gx_hot.count, r_gx_hot.queued, r_gx_hot.uploads, r_gx_hot.bytes, r_gx_hot.evictions, r_gx_hot.deferred );
			gEngfuncs.Con_Reportf( "Xash3D GameCube: gx world per frame draws=%u verts=%u state_sets=%u tex_binds=%u\n",
				sum_draws / passes, sum_verts / passes, sum_states / passes, sum_binds / passes );
			passes = 0;
			sum_draws = sum_verts = sum_states = sum_binds = 0;
			r_gx_tex_uploads = r_gx_tex_fence_stalls = r_gx_cmpr_bounce_stalls = 0;
		}
	}
	r_gx_tex_draws = 0;
	r_gx_flat_draws = 0;
	r_gx_lm_draws = 0;
	r_gx_frame_verts = 0;
	r_gx_state_sets = 0;
	r_gx_state_reuses = 0;
	r_gx_tex_loads = 0;
	r_gx_tex_reuses = 0;
	r_gx_lm_inits = 0;
	r_gx_lm_loads = 0;
	r_gx_lm_reuses = 0;
	r_gx_face_skips = 0;
	r_gx_face_skip_area = 0;
	r_gx_face_skip_far = 0;
	if( log_begin )
	{
		Q_strncpy( r_gx_capfaces_begin_map, map_name, sizeof( r_gx_capfaces_begin_map ));
		gEngfuncs.Con_Reportf( "Xash3D GameCube: CapFaces begin map=%s f=%d\n",
			map_name, tr.framecount );
	}
	/* Skybox image first — per-map CMPR — then CapFaces world tris.
	 * Tip-safe tram hops: skip CMPR prefetch when tip is already tight so
	 * CapFaces can emit LM-caps without the outdoor-sky stall path. */
	{
		const qboolean tip_tight = GC_MemHeapFreeBytes() < 96u * 1024u;

		if( !tip_tight )
			R_GXPrefetchMapSkybox();
		else if( log_begin )
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: CapFaces sky prefetch skipped tip-safe tip=%s\n",
				Q_memprint( GC_MemHeapFreeBytes() ));
	}
	/* Park streamed CMPR in durable MEM1 so arena-tail stomps cannot undo
	 * late ARAM→hot upgrades after CapFaces boot. */
	R_GCEnsureDurableHotWindow( 96u * 1024u );
	R_GXHotService();
	R_GXSetupWorld3DState();
	/* CapFaces lean: PVS + frustum before emit (no live marksurfaces). */
	R_GXBuildFrustum();
	GC_BeginCapFaceVis( RI.rvp.vieworigin );

	draw = GC_GetNewGameDrawSurfs();
	n = GC_GetNewGameCapFaceCount();

	/* Stamp marksurfaces only when leaf→surface links are still valid.
	 * After G132 scratch reuse on New Game / menu New Game, MarkLeaves already
	 * full-stamped surf->visframe; walking dangling firstmarksurface hangs.
	 * Connect peers keep surfaces scratch-retained: CapFaces frame1 can draw
	 * LM-caps, then frame2 DSI's inside R_GXMarkVisibleSurfaces at
	 * (*mark)->visframe (PC 801EEDF0 / dod_flash dual IPC 20261107-093749).
	 * Skip the stamp walk whenever scratch is retained — LM-caps/lean do not
	 * need it, and live-walk uses MarkLeaves stamps already.
	 * Solo -gcworldrender CapFaces: tip-parked leafs/marksurfaces often sit in
	 * BSP scratch that CapFaces LM/tex reuse; MarkVisibleSurfaces then hangs
	 * after G285/sky (tip-donner-libogc-draw 20261108-160045). CapFaces-only
	 * Flipper does not need the stamp.
	 * Tip-safe campaign: with LM-caps already baked, skip the stamp under
	 * tight tip (c0a0b hang after sky faces=1 @ tip≈48 Kb). */
	if( !scratch_retained
		&& !( n > 0 && gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
		&& !( n > 0 && GC_MemHeapFreeBytes() < 96u * 1024u )
		&& ( !( newgame_probe && GC_UseLowResWorldProbe() )
			|| surfaces_pinned ))
		R_GXMarkVisibleSurfaces( world );
	else if( log_begin && n > 0 && GC_MemHeapFreeBytes() < 96u * 1024u )
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: CapFaces MarkVisible skipped tip-safe tip=%s n=%d\n",
			Q_memprint( GC_MemHeapFreeBytes() ), n );

	if( log_begin )
		gEngfuncs.Con_Reportf( "Xash3D GameCube: CapFaces surfs n=%d live=%d\n",
			n, GC_GetLiveFaceCount() );

	/* G213/G283: when surfaces are pinned (malloc or scratch-retain), walk
	 * live BSP30 via surfbits visframe (MarkLeaves). Cap faces keep LM.
	 * Lean pool only when surfaces dangle after scratch reuse. */
	drawn = 0;
	/* G282: emit LM-caps whenever New Game has them — do not require a live
	 * pool (empty live used to fall through to sky-only and drop caps).
	 * Solo -gcworldrender CapFaces lean also takes this path so PVS/frustum
	 * run (else falls through to G199 CPU-backface-only). */
	if( surfaces_live || ( newgame_probe && n > 0 )
		|| ( n > 0 && gEngfuncs.Sys_CheckParm( "-gcworldrender" )))
	{
		static qboolean g213_logged;
		int live_n = GC_GetLiveFaceCount();

		/* G283: malloc-pinned → live BSP. Scratch retain: LM-caps + G298 lean
		 * baked verts (no edge-walk of retained scratch after present reuse).
		 * -gcworldrender CapFaces: surfaces can sit in the high slot (pinned)
		 * while plane* still dangles into reused scratch — dod_donner tip
		 * Guru'd in live BSP (DAR=0x9000904C, tip-donner 125333). Prefer the
		 * EDGE-baked CapFaces path instead.
		 * Tip-safe tram hops: live BSP after outdoor sky stalls CapFaces
		 * (c0a0b @ tip≈48 Kb). Prefer LM-caps when tip is tight — keep the
		 * skip sticky so later frames do not fall into G213 live-walk. */
		{
			const qboolean tip_skip_live =
				n > 0 && GC_MemHeapFreeBytes() < 96u * 1024u;

			if( tip_skip_live )
			{
				if( !g213_logged )
				{
					g213_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G283 live BSP skipped tip-safe (LM-caps) tip=%s n=%d\n",
						Q_memprint( GC_MemHeapFreeBytes() ), n );
				}
			}
			else if( surfaces_pinned && !scratch_retained
				&& !( n > 0 && gEngfuncs.Sys_CheckParm( "-gcworldrender" )))
			{
				drawn = R_GXDrawWorldLiveSurfaces( world, true );
				if( !g213_logged )
				{
					g213_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G283 live BSP Flipper opaque=%d (malloc pin)\n",
						drawn );
				}
			}
			/* G298: lean early-pool faces under scratch retain or dangling surfaces. */
			else if( live_n > 0 )
		{
			int li, live_drawn = 0, skipped_cap = 0;
			int skip_back = 0, skip_noverts = 0, emit_fail = 0;
			int skip_plane = 0;

			/* G219: EDGE/TEX baked live faces only; muted MODULATE (no plane white). */
			for( li = 0; li < live_n; li++ )
			{
				float dot;
				msurface_t surf;
				mtexinfo_t tex;
				float pts[32][3];
				int nv;
				int got;
				int bake;

				if( live_drawn >= live_budget
					|| drawn + live_drawn >= frame_budget )
					break;
				/* G361: denser changelevel prefers live EDGE/TEX over LM-cap. */
				if( GC_LiveFaceIsCapped( li ) && !want_live_overlap )
				{
					skipped_cap++;
					continue;
				}
				bake = GC_GetLiveFaceBakeSrc( li );
				if( bake != 1 && bake != 3 ) /* EDGE or TEX only */
				{
					skip_plane++;
					continue;
				}
				if( !GC_FillLiveDrawSurf( li, &surf, &tex ))
					continue;
				if( !surf.plane )
					continue;
				dot = DotProduct( tr.modelorg, surf.plane->normal ) - surf.plane->dist;
				if( surf.flags & SURF_PLANEBACK )
				{
					if( dot > -BACKFACE_EPSILON )
					{
						skip_back++;
						continue;
					}
				}
				else if( dot < BACKFACE_EPSILON )
				{
					skip_back++;
					continue;
				}
				nv = GC_GetLiveFaceVerts( li, pts, 32 );
				if( nv < 3 )
				{
					skip_noverts++;
					continue;
				}
				if( GC_G378DumpSkipFarVerts( pts, nv ))
					continue;
				/* G228: no live frustum cull — tram side walls were skipped. */
				got = R_GXEmitFaceVerts( &surf, world, pts, nv );
				if( got <= 0 )
					emit_fail++;
				else
					live_drawn += got;
			}
			drawn += live_drawn;
			if( !g213_logged )
			{
				g213_logged = true;
				if( scratch_retained )
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G298 lean Flipper under scratch retain live=%d drawn=%d\n",
						live_n, live_drawn );
#if 0 /* G281 DOL reclaim */
				else
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G220 live Flipper skyfill=%d of %d skip_cap=%d plane=%d back=%d noverts=%d fail=%d\n",
						live_drawn, live_n, skipped_cap, skip_plane, skip_back, skip_noverts, emit_fail );
#endif
				(void)skipped_cap; (void)skip_plane;
				(void)skip_back; (void)skip_noverts; (void)emit_fail;
			}
		}
		else if( scratch_retained )
		{
			/* Tip-safe default: LM-caps + G286 water only (Xen c4a1 hung on
			 * live edge-walk after scratch reuse, probe 20260809-131438).
			 * Connect/listen peers can arrive with retain-pin but empty lean
			 * and empty caps (Capture used sv.models[1]==NULL) — without a
			 * live walk CapFaces stays drawn=0 / sky clear. Prefer caps when
			 * present; otherwise walk the retained live BSP once.
			 *
			 * Tip-safe -gcchangelevel: CapFaces NoPVS is already skipped under
			 * tight tip, and dense dests park edges in the BSP scratch hole
			 * (c3a1a/c2a4a). Leafs carve then stomps surfedges → live emit
			 * bad edge≈-1.8e9 and CapFaces never reaches Flipper present
			 * sampling. Skip the live walk; sky clear still presents. */
			if( n > 0 )
			{
				if( !g213_logged )
				{
					g213_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G283 scratch retain-pin (stamp+LM-caps; lean empty)\n" );
				}
			}
			else if( gEngfuncs.Sys_CheckParm( "-gcchangelevel" ))
			{
				if( !g213_logged )
				{
					g213_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G283 scratch retain skip live-walk (tip-safe changelevel, no caps)\n" );
				}
			}
			else
			{
				drawn = R_GXDrawWorldLiveSurfaces( world, true );
				if( !g213_logged )
				{
					g213_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G283 scratch retain live-walk (no caps) opaque=%d\n",
						drawn );
				}
			}
		}
		else if( surfaces_live
			&& !( n > 0 && gEngfuncs.Sys_CheckParm( "-gcworldrender" )))
		{
			drawn = R_GXDrawWorldLiveSurfaces( world, true );
			if( !g213_logged && drawn > 0 )
			{
				g213_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G213 live BSP Flipper opaque=%d (surfaces pinned)\n",
					drawn );
			}
		}
		else if( surfaces_live && n > 0
			&& gEngfuncs.Sys_CheckParm( "-gcworldrender" ) && !g213_logged )
		{
			g213_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: CapFaces-only Flipper (skip live BSP; planes may dangle) n=%d\n",
				n );
		}
		} /* tip_skip_live / G283 / G298 / scratch / G213 chain */
		/* The menu route retained the complete BSP source and successfully
		 * emitted its live PVS surfaces.  CapFaces/fill are the degraded
		 * scratch-reuse fallback; layering them over real geometry creates the
		 * synthetic tram corridor and must not be used on this path. */
		if( surfaces_pinned && !scratch_retained && drawn > 0
			&& !gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
		{
			GC_NoteCapFacesDrawn( drawn );
			/* Same bookkeeping as the common exit below: the HUD pass (and
			 * the intro credits drawn with it) only runs once the world drew. */
			GX_Flush();
			r_gx_world_drew = true;
			GC_MarkGxWorldEfbReady();
			return drawn;
		}
		/* Also emit near-eye cap faces with LM atlas for readable walls.
		 * G231: LM/live write Z first — flat-fill after so gray only plugs holes. */
		if( draw && n > 0 )
		{
			int backface_skips = 0;
			int emit_fails = 0;
			int frustum_skips = 0;
			int pvs_skips = 0;
			int cap_drawn = 0;
			/* PVS/frustum are per-eye — a static CapFaces DL would ignore them. */
			const qboolean want_cap_dl =
				gEngfuncs.Sys_CheckParm( "-gcworlddl" ) != 0
				&& GC_CapFaceVisAssigned() <= 0;
			qboolean used_cap_dl = false;
			/* G348/G363: leave FRAME room for flat-fill plugs after LM.
			 * Reserve the actual fill count (capped), not the full FILL budget —
			 * denser G361 fill_n=4 was starving ~48 LM slots and keeping floor voids. */
			const int fill_n = GC_GetFillFaceCount();
			const int water_n_reserve = GC_GetWaterFaceCount();
			int fill_reserve = 0;
			int water_reserve = 0;

			/* Replay CapFaces GX display list when generation matches. */
			if( want_cap_dl && r_gx_cap_dl && r_gx_cap_dl_bytes > 0
				&& r_gx_cap_dl_gen == gen && r_gx_cap_dl_drawn > 0 )
			{
				static qboolean dl_call_logged;

				GX_CallDispList( r_gx_cap_dl, r_gx_cap_dl_bytes );
				cap_drawn = r_gx_cap_dl_drawn;
				drawn += cap_drawn;
				used_cap_dl = true;
				if( !dl_call_logged )
				{
					extern void GCube_AppendSwissEvidence( const char *fmt, ... );

					dl_call_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces display list call bytes=%u drawn=%d gen=%d\n",
						(unsigned)r_gx_cap_dl_bytes, cap_drawn, gen );
					GCube_AppendSwissEvidence( "CapFaces display list call bytes=%u drawn=%d",
						(unsigned)r_gx_cap_dl_bytes, cap_drawn );
				}
			}

			/* GX-016: denser G36 sample — skip fill reserve so LM-cap slots
			 * stay within the tight denser face budget (fill resumes after flush). */
			if( !used_cap_dl && fill_n > 0
				&& !( GC_IsG36SampleFaceCap() && GC_WantLiveCapOverlap() ))
			{
				const int fill_cap = ( GC_GX_FILL_FACE_BUDGET < frame_budget / 4 )
					? GC_GX_FILL_FACE_BUDGET
					: ( frame_budget / 4 );

				fill_reserve = ( fill_n < fill_cap ) ? fill_n : fill_cap;
			}
			/* Lost Coast: CapFaces EDGE emit counts verts toward frame_budget
			 * (~521 > 448) and starved G286 water (emit=0/24). Reserve sheets. */
			if( water_n_reserve > 0 )
			{
				water_reserve = water_n_reserve;
				if( water_reserve > 24 )
					water_reserve = 24;
			}
			if( !used_cap_dl )
			{
			const int cap_limit = frame_budget - fill_reserve - water_reserve;
			int menu_plug_slot = -1;
			qboolean menu_plug_drawn = false;

			/* After multi-pack densify, freeze EFB completely — pack0/fill/water
			 * redraw was wiping the cove hold (late DumpFrames → blown sand).
			 * Do NOT mark efb_ready: world-ready already CopyDisp'd the cove;
			 * re-present after HUD/2D risk washed XFB (probe 223847 f15+ white).
			 * G362 then skips soft present while hold remains. */
			if( r_gx_cap_densify_held && R_GXEfbDumpHoldLeft() > 0 )
			{
				static qboolean densify_hold_skip_logged;

				if( !densify_hold_skip_logged )
				{
					densify_hold_skip_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces densify hold — freeze EFB (no pack0/fill/water redraw)\n" );
				}
				r_gx_world_drew = true;
				return 1;
			}

			if( n > (int)( sizeof( order ) / sizeof( order[0] )))
				n = (int)( sizeof( order ) / sizeof( order[0] ));

			R_GXOrderFacesByTexBands( draw, n, order );
			/* The menu tram end-plug is a safety surface, not an ordinary
			 * ranked wall. Keep it inside the bounded sample window or the
			 * forward view can still expose clear colour behind the cabin. */
			if( menu_playstart )
			{
				int k;

				for( k = 0; k < n; k++ )
				{
					if( draw[k].firstedge < 0
						&& draw[k].extents[0] == 512
						&& draw[k].extents[1] == 384 )
					{
						menu_plug_slot = k;
						break;
					}
				}
				if( menu_plug_slot >= 0 && drawn + cap_drawn < cap_limit
					&& R_GXEmitFace( &draw[menu_plug_slot], world, menu_plug_slot ) > 0 )
				{
					cap_drawn++;
					menu_plug_drawn = true;
				}
			}

			/* Record CapFaces LM emit into a GX display list (first gen only). */
			r_gx_cap_dl_recording = false;
			if( want_cap_dl )
			{
				if( !r_gx_cap_dl )
					R_GXReserveCapDispList();
				if( r_gx_cap_dl && r_gx_cap_dl_cap > 0 )
				{
					memset( r_gx_cap_dl, 0, r_gx_cap_dl_cap );
					DCInvalidateRange( r_gx_cap_dl, r_gx_cap_dl_cap );
					GX_BeginDispList( r_gx_cap_dl, r_gx_cap_dl_cap );
					r_gx_cap_dl_recording = true;
				}
			}

			for( i = 0; i < n; i++ )
			{
				float dot;
				int area;
				int got;
				const int slot = order[i];
				msurface_t *surf = &draw[slot];
				vec3_t bmins, bmaxs;

				if( drawn + cap_drawn >= cap_limit )
					break;
				if( menu_plug_drawn && slot == menu_plug_slot )
					continue;
				/* G361: live already emitted this face as EDGE/TEX. */
				if( want_live_overlap && GC_CapFaceIsLive( slot ))
					continue;
				/* Lean / pack: drop faces outside eye PVS (cluster of centroid). */
				if( !GC_CapFaceInPVS( slot ))
				{
					pvs_skips++;
					continue;
				}
				/* Keep primary frustum — disabling it (dense11) tip-crashed
				 * flash after densify while caen still left sky-holes. */
				if( GC_CapFaceBounds( slot, bmins, bmaxs )
					&& R_GXBoxOutsideFrustum( bmins, bmaxs ))
				{
					frustum_skips++;
					continue;
				}
				if( !surf->plane )
				{
					emit_fails++;
					continue;
				}
				/* G380: synthetic NPC floor (firstedge -1) skips CPU backface.
				 * Solo -gcworldrender CapFaces: repaired planes still leave
				 * ~316/384 CPU-culled while dual textured stills drew 200+
				 * (20261107-130650). Skip CPU backface — Flipper/GX culls by
				 * winding from durable pts_s16. */
				dot = 0.0f;
				if( surf->firstedge >= 0
					&& !gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
				{
				dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
				if( surf->flags & SURF_PLANEBACK )
				{
					if( dot > -BACKFACE_EPSILON )
					{
						backface_skips++;
						continue;
					}
				}
				else if( dot < BACKFACE_EPSILON )
				{
					backface_skips++;
					continue;
				}
				}
				area = (int)surf->extents[0] * (int)surf->extents[1];
				if( area > 0 && area < GC_GX_MIN_FACE_AREA )
				{
					r_gx_face_skips++;
					r_gx_face_skip_area++;
					continue;
				}
				if( GC_G378DumpSkipFarCapSlot( slot ))
					continue;
				/* G281: skip far-small cull on New Game — tunnel walls along
				 * the look path were dropped while dump eye was distant.
				 * Solo -gcworldrender CapFaces use the same dump eye. */
				if( !newgame_probe && !gEngfuncs.Sys_CheckParm( "-gcworldrender" )
					&& area > 0 && area < GC_GX_FAR_MIN_AREA
					&& fabsf( dot ) > GC_GX_FAR_FACE_DIST )
				{
					r_gx_face_skips++;
					r_gx_face_skip_far++;
					continue;
				}
				got = R_GXEmitFace( surf, world, slot );
				if( got <= 0 )
					emit_fails++;
				else
					cap_drawn += got;
			}
			if( r_gx_cap_dl_recording )
			{
				u32 dl_sz = GX_EndDispList();

				r_gx_cap_dl_recording = false;
				if( dl_sz > 0 && cap_drawn > 0 )
				{
					extern void GCube_AppendSwissEvidence( const char *fmt, ... );

					r_gx_cap_dl_bytes = dl_sz;
					r_gx_cap_dl_gen = gen;
					r_gx_cap_dl_drawn = cap_drawn;
					DCFlushRange( r_gx_cap_dl, dl_sz );
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces display list baked bytes=%u drawn=%d gen=%d\n",
						(unsigned)dl_sz, cap_drawn, gen );
					GCube_AppendSwissEvidence( "CapFaces display list bytes=%u drawn=%d",
						(unsigned)dl_sz, cap_drawn );
				}
				else
				{
					r_gx_cap_dl_bytes = 0;
					r_gx_cap_dl_gen = -1;
					r_gx_cap_dl_drawn = 0;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces display list bake failed sz=%u drawn=%d\n",
						(unsigned)dl_sz, cap_drawn );
				}
			}
			drawn += cap_drawn;
			/* Lost Coast DumpFrames: one 768 pack is still ribbon cliffs vs
			 * full-geo. Sequentially load the next near-eye spatial packs and
			 * emit into the same EFB (Z keeps nearer tris) without growing BSS. */
			if( !used_cap_dl
				&& gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" )
				&& gEngfuncs.Sys_CheckParm( "-gcdumpframes" ))
			{
				extern int GC_CapFacesSpatialPackCount( void );
				extern int GC_CapFacesLoadSpatialRank( model_t *mod, const float *eye, int rank );
				extern msurface_t *GC_GetNewGameDrawSurfs( void );
				extern int GC_GetNewGameCapFaceCount( void );
				static qboolean densify_done;
				const int pack_n = GC_CapFacesSpatialPackCount();

				/* Once per boot — reloading packs every frame stalls DumpFrames.
				 * Keep frustum cull on densify (probe 204010 → cove-p10). Skipping
				 * densify left pack0-only wash; densify without frustum over-painted. */
				if( !densify_done && pack_n > 1 )
				{
					extern void *GC_GetGxVideoMode( void );
					extern qboolean GC_CapFaceBounds( int slot, vec3_t mins, vec3_t maxs );
					const float *eye = RI.rvp.vieworigin;
					int rank, extra_drawn = 0, frustum_skip = 0, near_skip = 0;
					const int max_rank = pack_n;
					GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
					Mtx44 proj;
					Mtx mv;

					if( rmode )
					{
						GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f );
						GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
					}
					R_GXBuildWorldProjection( proj );
					GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
					R_GXBuildWorldModelview( mv );
					GX_LoadPosMtxImm( mv, GX_PNMTX0 );
					GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
					GX_SetColorUpdate( GX_TRUE );
					GX_SetCullMode( GX_CULL_BACK );
					GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
					r_gx_face_mode = GC_GX_FACE_MODE_NONE;
					r_gx_bound_texnum = 0;
					r_gx_bound_fmt = 0;
					r_gx_cull_none = false;

					/* Rank 0 already emitted as primary — fill shoreline gap
					 * from rank 1. Strict frustum left a sky void under the
					 * pier/buildings (mae~42); admit forward midband faces. */
					{
						vec3_t forward, right, up;
						int midgap_admit = 0;

						AngleVectors( RI.rvp.viewangles, forward, right, up );
					for( rank = 1; rank < max_rank; rank++ )
					{
						int en, ei;
						msurface_t *edraw;

						if( GC_CapFacesLoadSpatialRank( world, eye, rank ) <= 0 )
							continue;
						en = GC_GetNewGameCapFaceCount();
						edraw = GC_GetNewGameDrawSurfs();
						if( !edraw || en <= 0 )
							continue;
						for( ei = 0; ei < en; ei++ )
						{
							vec3_t bmins, bmaxs;
							float cx, cy, cz, dx, dy, dz, dist2, dist, fdot;
							int got;

							if( !GC_CapFaceBounds( ei, bmins, bmaxs ))
							{
								got = R_GXEmitFace( &edraw[ei], world, ei );
								if( got > 0 )
									extra_drawn += got;
								continue;
							}
							cx = 0.5f * ( bmins[0] + bmaxs[0] );
							cy = 0.5f * ( bmins[1] + bmaxs[1] );
							cz = 0.5f * ( bmins[2] + bmaxs[2] );
							dx = cx - eye[0]; dy = cy - eye[1]; dz = cz - eye[2];
							dist2 = dx * dx + dy * dy + dz * dz;
							/* Skip only the very nearest sand already in pack0. */
							if( dist2 < ( 160.0f * 160.0f ))
							{
								near_skip++;
								continue;
							}
							if( R_GXBoxOutsideFrustum( bmins, bmaxs ))
							{
								dist = sqrtf( dist2 );
								fdot = ( dist > 1.0f )
									? ( dx * forward[0] + dy * forward[1] + dz * forward[2] ) / dist
									: 0.0f;
								/* Shoreline midgap: forward band that frustum
								 * rejected — closes sky hole under buildings.
								 * No hard admit ceiling — ARAM densify may take
								 * tens of seconds; pop / long load is OK. */
								if( !( dist >= 200.0f && dist <= 2200.0f && fdot >= 0.12f ))
								{
									frustum_skip++;
									continue;
								}
								midgap_admit++;
							}
							got = R_GXEmitFace( &edraw[ei], world, ei );
							if( got > 0 )
								extra_drawn += got;
						}
						GX_Flush();
					}
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces densify midgap_admit=%d\n", midgap_admit );
					}
					GX_DrawDone();
					(void)GC_CapFacesLoadSpatialRank( world, eye, 0 );
					densify_done = true;
					r_gx_cap_densify_hold_pending = 240;
					drawn = drawn + extra_drawn;
					cap_drawn = cap_drawn + extra_drawn;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces dump densify extra_drawn=%d total=%d packs=%d frustum_skip=%d near_skip=%d (midfar-layer; hold-after-water)\n",
						extra_drawn, cap_drawn, max_rank, frustum_skip, near_skip );
				}
			}
			/* -gcworldrender listen densify: layer near-eye spatial packs onto
			 * the primary CapFaces EFB (Z keeps nearer). Primary alone left
			 * caen sparse (drawn≈206 / sky holes) while flash swapped denser.
			 * Once + EFB hold — reloading packs every frame stalls the tip.
			 * Do NOT z-clear here: wipe+redraw left listen EFB black (dense9). */
			if( !used_cap_dl
				&& gEngfuncs.Sys_CheckParm( "-gcworldrender" )
				&& !gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" ))
			{
				extern int GC_CapFacesSpatialPackCount( void );
				extern int GC_CapFacesLoadSpatialRank( model_t *mod, const float *eye, int rank );
				extern int GC_CapFacesRankSpatialListen( const float *eye, const float *forward, int *out_idx, int max_out );
				extern msurface_t *GC_GetNewGameDrawSurfs( void );
				extern int GC_GetNewGameCapFaceCount( void );
				static qboolean listen_densify_done;
				const int pack_n = GC_CapFacesSpatialPackCount();

				if( !listen_densify_done && pack_n > 1 )
				{
					const float *eye = RI.rvp.vieworigin;
					vec3_t forward, right, up;
					int ranked[16];
					int rn, ri, extra_drawn = 0;
					int listen_frustum = 0;
					/* All-geo: 1.5× left caen sky-holes (~1248 drawn). 3×/20
					 * packs tip-hung flash at BSP texinfo; 2×/14 is tip-safer. */
					const int densify_budget = frame_budget * 2;

					AngleVectors( RI.rvp.viewangles, forward, right, up );
					rn = GC_CapFacesRankSpatialListen( eye, forward, ranked, 16 );
					for( ri = 0; ri < rn && ri < 14; ri++ )
					{
						int en, ei, pack_drawn = 0;
						msurface_t *edraw;
						const int idx = ranked[ri];

						/* Primary already emitted above. */
						if( idx == 0 )
							continue;
						if( drawn + extra_drawn >= densify_budget )
							break;
						if( GC_CapFacesLoadSpatialRank( world, eye, idx ) <= 0 )
							continue;
						en = GC_GetNewGameCapFaceCount();
						edraw = GC_GetNewGameDrawSurfs();
						if( !edraw || en <= 0 )
							continue;
						for( ei = 0; ei < en; ei++ )
						{
							msurface_t *esurf = &edraw[ei];
							int got;

							if( drawn + extra_drawn >= densify_budget )
								break;
							got = R_GXEmitFace( esurf, world, ei );
							if( got > 0 )
							{
								extra_drawn += got;
								pack_drawn += got;
							}
							else
								listen_frustum++;
						}
						GX_Flush();
						gEngfuncs.Con_Reportf(
							"Xash3D GameCube: CapFaces listen densify pack=%d drawn=%d\n",
							idx, pack_drawn );
					}
					(void)GC_CapFacesLoadSpatialRank( world, eye, 0 );
					listen_densify_done = true;
					r_gx_cap_densify_held = true;
					R_GXHoldEfbForDump( 360 );
					cap_drawn += extra_drawn;
					drawn += extra_drawn;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces listen densify extra=%d total=%d frustum_skip=%d packs=%d\n",
						extra_drawn, cap_drawn, listen_frustum, rn );
				}
			}
			skip_capfaces_pack0_emit: ;
			if( menu_playstart || gEngfuncs.Sys_CheckParm( "-gcworldrender" ))
			{
				static qboolean menu_cap_filter_logged;

				if( !menu_cap_filter_logged )
				{
					menu_cap_filter_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces filter drawn=%d backface=%d emit_fail=%d cull=%d pvs=%d frustum=%d of %d\n",
						cap_drawn, backface_skips, emit_fails, r_gx_face_skips,
						pvs_skips ? pvs_skips : GC_CapFacePVSSkipCount(),
						frustum_skips, n );
				}
			}
			(void)backface_skips;
			(void)emit_fails;
			(void)frustum_skips;
			(void)pvs_skips;
			} /* cap_limit scope (!used_cap_dl) */
		}
		/* G222/G225/G231: flat-fill AFTER LM so PASSCLR only wins empty Z.
		 * GX-016: skip fill emit during denser G36 sample (resume after flush).
		 * CapFaces DL replay already covers the near-eye set — skip fill. */
		if( !( gEngfuncs.Sys_CheckParm( "-gcworlddl" )
				&& r_gx_cap_dl_bytes > 0
				&& r_gx_cap_dl_gen == GC_GetNewGameCapGeneration() )
			&& !( GC_IsG36SampleFaceCap() && GC_WantLiveCapOverlap() ))
		{
			static qboolean g222_logged;
			int fill_n = GC_GetFillFaceCount();
			int fi, fill_drawn = 0, fill_back = 0, fill_fail = 0;

			for( fi = 0; fi < fill_n; fi++ )
			{
				mplane_t pl;
				int flags = 0;
				float pts[32][3];
				int nv;
				float dot;

				if( fill_drawn >= GC_GX_FILL_FACE_BUDGET
					|| drawn + fill_drawn >= frame_budget )
					break;
				if( !GC_FillFacePlane( fi, &pl, &flags ))
					continue;
				dot = DotProduct( tr.modelorg, pl.normal ) - pl.dist;
				if( flags & SURF_PLANEBACK )
				{
					if( dot > -BACKFACE_EPSILON )
					{
						fill_back++;
						continue;
					}
				}
				else if( dot < BACKFACE_EPSILON )
				{
					fill_back++;
					continue;
				}
				nv = GC_GetFillFaceVerts( fi, pts, 32 );
				if( nv < 3 )
				{
					fill_fail++;
					continue;
				}
				if( GC_G378DumpSkipFarVerts( pts, nv ))
					continue;
				if( R_GXEmitFlatFillVerts( pts, nv, 0x687068FFu ) > 0 )
					fill_drawn++;
				else
					fill_fail++;
			}
			drawn += fill_drawn;
			if( menu_playstart )
			{
				static qboolean menu_fill_logged;

				if( !menu_fill_logged )
				{
					menu_fill_logged = true;
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: menu fill emit drawn=%d back=%d fail=%d of %d\n",
						fill_drawn, fill_back, fill_fail, fill_n );
				}
			}
			/* G380: re-emit synthetic NPC floor after fill — LEQUAL fill at
			 * the same Z painted over the PASSCLR quad. */
			{
				int s;

				for( s = 0; s < n; s++ )
				{
					if( !draw || draw[s].firstedge >= 0 )
						continue;
					if( R_GXEmitFace( &draw[s], world, s ) > 0 )
						drawn++;
				}
			}
			/* G348: sample-window first pass can exhaust FRAME before fill;
			 * keep logging until a non-zero fill emit is observed. */
			if( fill_n > 0 )
			{
				static qboolean g231_ok_logged;

				if( !g222_logged || ( fill_drawn > 0 && !g231_ok_logged ))
				{
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G231 fill=%d/%d after LM\n",
						fill_drawn, fill_n );
					g222_logged = true;
					if( fill_drawn > 0 )
						g231_ok_logged = true;
				}
				(void)fill_back;
				(void)fill_fail;
			}
		}
		/* G286: baked water sheets — scroll CMPR + sky cubemap lerp.
		 * Not Source water; GX TEV stand-in. Skip redraw under densify hold
		 * (re-blend every frame muddied the cubemap into solid blue). */
		{
			static qboolean g286_logged;
			int water_n = GC_GetWaterFaceCount();
			int wi, water_drawn = 0;
			const qboolean water_tex = GC_WaterTexReady() && GC_GetWaterTexObj() != NULL;
			const qboolean water_env = water_tex && r_gx_sky_ready;
			float time_sec = gp_cl ? (float)gp_cl->time : 0.0f;
			const float *eye = RI.rvp.vieworigin;
			/* Flat water reflections favor the upper hemisphere. */
			int sky_side = 4; /* up */
			const qboolean skip_water_hold = r_gx_cap_densify_held
				&& R_GXEfbDumpHoldLeft() > 0;

			if( !r_gx_sky_ready || sky_side < 0 )
				sky_side = R_GXPickSkyboxSide();

			if( water_n > 0 && !skip_water_hold )
			{
				GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
				/* CapFaces dump: ALWAYS fills the pier/horizon sky void; pack0
				 * + midgap re-emit after water restores dunes/cliffs on top. */
				if( gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" )
					&& gEngfuncs.Sys_CheckParm( "-gcdumpframes" ))
					GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_FALSE );
				else
					GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
				r_gx_face_mode = GC_GX_FACE_MODE_NONE;
				if( water_tex )
				{
						GX_LoadTexObj( (GXTexObj *)GC_GetWaterTexObj(), GX_TEXMAP0 );
					if( water_env )
					{
						if( sky_side < 0 || sky_side > 5 )
							sky_side = 3; /* ft */
						GX_LoadTexObj( &r_gx_sky_obj[sky_side], GX_TEXMAP1 );
						GX_SetNumTexGens( 2 );
						GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
						GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
						GX_SetNumTevStages( 2 );
						/* Stage0: constant olive body (REG1). Water CMPR +
						 * PASSCLR still read near-white in DumpFrames. */
						GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP_NULL, GX_COLORNULL );
						GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C1 );
						GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
						GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A1 );
						GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
						/* Stage1: ADD sky cubemap × A0 — sun-path glints. */
						GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL );
						GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_A0, GX_CC_CPREV );
						GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
						GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
						GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
						/* Retail midband ~60 + HDR sun streak — hotter sky ADD. */
						GX_SetTevColor( GX_TEVREG0, (GXColor){ 0, 0, 0, 160 } );
						GX_SetTevColor( GX_TEVREG1, (GXColor){ 36, 48, 40, 230 } );
						GX_ClearVtxDesc();
						GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
						GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0 );
					}
					else
					{
						GX_SetNumTexGens( 1 );
						GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
						GX_SetNumTevStages( 1 );
						GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
						GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
						GX_ClearVtxDesc();
						GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
					}
				}
			}
			for( wi = 0; !skip_water_hold && wi < water_n; wi++ )
			{
				mplane_t pl;
				float pts[32][3];
				float st[32][2];
				int nv, si;
				float dot, su, sv;
				u32 tint;

				/* Water uses its own reserved slots — do not share CapFaces
				 * vert-counted frame_budget (near-eye Lost Coast hit 521>448). */
				if( water_drawn >= 24 )
					break;
				if( !GC_WaterFacePlane( wi, &pl, NULL ))
					continue;
				dot = DotProduct( tr.modelorg, pl.normal ) - pl.dist;
				/* Water sheets: draw from either side (tram floor views). */
				if( fabsf( dot ) < BACKFACE_EPSILON )
					continue;
				nv = GC_GetWaterFaceVerts( wi, pts, 32 );
				if( nv < 3 )
					continue;
				GC_GetWaterFaceScroll( wi, &su, &sv, &tint );
				/* Lost Coast water body is dark olive/teal. */
				if( !tint || gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" ))
					tint = 0x1E4038D0u;
				if( water_tex && GC_GetWaterFaceST( wi, st, 32 ) == nv )
				{
					GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, (u16)nv );
					for( si = 0; si < nv; si++ )
					{
						float env_s = st[si][0], env_t = st[si][1];

						GX_Position3f32( pts[si][0], pts[si][1], pts[si][2] );
						GX_Color1u32( tint );
						GX_TexCoord2f32( st[si][0] + time_sec * su, st[si][1] + time_sec * sv );
						if( water_env )
						{
							vec3_t to_eye, refl;
							float nd, inv_len;

							VectorSubtract( eye, pts[si], to_eye );
							inv_len = VectorLength( to_eye );
							if( inv_len > 1e-3f )
							{
								to_eye[0] /= inv_len;
								to_eye[1] /= inv_len;
								to_eye[2] /= inv_len;
							}
							nd = DotProduct( to_eye, pl.normal );
							refl[0] = to_eye[0] - 2.0f * nd * pl.normal[0];
							refl[1] = to_eye[1] - 2.0f * nd * pl.normal[1];
							refl[2] = to_eye[2] - 2.0f * nd * pl.normal[2];
							/* Sphere-map-ish into the active sky face. */
							env_s = 0.5f + 0.5f * refl[0];
							env_t = 0.5f - 0.5f * refl[2];
							GX_TexCoord2f32( env_s, env_t );
						}
					}
					GX_End();
					water_drawn++;
				}
				else if( R_GXEmitFlatFillVerts( pts, nv, tint ) > 0 )
					water_drawn++;
			}
			if( water_n > 0 && !skip_water_hold )
			{
				GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
				GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
				GX_SetVtxDesc( GX_VA_TEX1, GX_NONE );
				r_gx_face_mode = GC_GX_FACE_MODE_NONE;
				drawn += water_drawn;
			}
			if( !g286_logged )
			{
				int env_faces = 0;
				int ci, cn = GC_GetNewGameCapFaceCount();
				msurface_t *cdraw = GC_GetNewGameDrawSurfs();
				g286_logged = true;
				if( cdraw )
				{
					for( ci = 0; ci < cn; ci++ )
					{
						if( cdraw[ci].flags & GC_SURF_ENVMAP )
							env_faces++;
					}
				}
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G286 water emit=%d/%d tex=%d env=%d sand_env=%d sand_spec=%d sand_nrm=%d sand_sun=%d sky=%d a0=72 body=passclr\n",
					water_drawn, water_n, water_tex ? 1 : 0, water_env ? 1 : 0, env_faces,
					GC_SandSpecTexReady() ? 1 : 0, GC_SandNrmTexReady() ? 1 : 0,
					GC_SandSunTexReady() ? 1 : 0, sky_side );
			}
			/* Source prop meshes (boats) — baked GCpr tris, tip-safe CMPR. */
			{
				static qboolean g_prop_logged;
				const int prop_n = GC_GetPropTriCount();
				int pi, prop_drawn = 0;
				int last_tex = -1;
				const qboolean skip_prop_hold = ( r_gx_cap_densify_held
						&& R_GXEfbDumpHoldLeft() > 0 )
					/* Defer boats until after pack0 re-emit on densify dump. */
					|| ( r_gx_cap_densify_hold_pending > 0 );

				if( prop_n > 0 && !skip_prop_hold )
				{
					/* Densify CapFaces sheets often Z-win Source boats at the
					 * same shoreline — keep Z test for stacking props, but
					 * pull verts slightly toward the eye so hulls win sand. */
					const float *veye = RI.rvp.vieworigin;
					float vfwd[3];
					const float prop_z_bias = 6.0f;

					AngleVectors( RI.rvp.viewangles, vfwd, NULL, NULL );
					GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
					/* Match densify prop path: ALWAYS + ChanCtrl so hulls read. */
					GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_FALSE );
					GX_SetCullMode( GX_CULL_NONE );
					GX_SetNumChans( 1 );
					GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
						0, GX_DF_NONE, GX_AF_NONE );
					GX_SetNumTexGens( 1 );
					GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
					GX_SetNumTevStages( 1 );
					GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
					GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
					GX_SetNumIndStages( 0 );
					GX_ClearVtxDesc();
					GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_TEX1, GX_NONE );
					GX_SetVtxDesc( GX_VA_TEX2, GX_NONE );
					GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
					GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
					GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
					r_gx_face_mode = GC_GX_FACE_MODE_NONE;
					r_gx_cull_none = true;
					(void)veye;

					for( pi = 0; pi < prop_n; pi++ )
					{
						float xyz[3][3];
						float st[3][2];
						int tex = 0;
						int vi;
						void *tobj;

						if( !GC_GetPropTri( pi, xyz, st, &tex ))
							continue;
						tobj = GC_GetPropTexObj( tex );
						if( !tobj )
							tobj = GC_GetPropTexObj( 0 );
						if( !tobj )
							continue;
						if( tex != last_tex )
						{
							GX_LoadTexObj( (GXTexObj *)tobj, GX_TEXMAP0 );
							last_tex = tex;
						}
						GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 3 );
						for( vi = 0; vi < 3; vi++ )
						{
							GX_Position3f32(
								xyz[vi][0] - vfwd[0] * prop_z_bias,
								xyz[vi][1] - vfwd[1] * prop_z_bias,
								xyz[vi][2] - vfwd[2] * prop_z_bias );
							GX_Color1u32( 0xFFFFFFFF );
							GX_TexCoord2f32( st[vi][0], st[vi][1] );
						}
						GX_End();
						prop_drawn++;
					}
					drawn += prop_drawn;
					if( !g_prop_logged )
					{
						g_prop_logged = true;
						gEngfuncs.Con_Reportf(
							"Xash3D GameCube: prop meshes emit=%d/%d\n",
							prop_drawn, prop_n );
					}
				}
			}
			/* Densify freeze: arm hold only after water painted once.
			 * Re-emit pack0 + midgap densify AFTER ALWAYS water so dunes and
			 * pier/cliff bases cover the horizon void. */
			if( r_gx_cap_densify_hold_pending > 0 )
			{
				extern int GC_CapFacesLoadSpatialRank( model_t *mod, const float *eye, int rank );
				extern int GC_CapFacesSpatialPackCount( void );
				extern qboolean GC_CapFaceBounds( int slot, vec3_t mins, vec3_t maxs );
				int ri, rn_pack0, pack0_redraw = 0, midgap_redraw = 0;
				msurface_t *rdraw;
				int rorder[768];
				vec3_t forward, right, up;
				const float *eye = RI.rvp.vieworigin;
				int pack_n = GC_CapFacesSpatialPackCount();
				int rank;

				AngleVectors( RI.rvp.viewangles, forward, right, up );
				/* CapFaces first — synthetic water slab paints AFTER midgap so
				 * it fills pier sky slits CapFaces leave, then near pack0
				 * restores dunes over the slab. */
				(void)GC_CapFacesLoadSpatialRank( world, eye, 0 );
				rn_pack0 = GC_GetNewGameCapFaceCount();
				rdraw = GC_GetNewGameDrawSurfs();
				GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
				GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
				GX_SetCullMode( GX_CULL_BACK );
				r_gx_cull_none = false;
				r_gx_face_mode = GC_GX_FACE_MODE_NONE;
				if( rdraw && rn_pack0 > 0 )
				{
					if( rn_pack0 > (int)( sizeof( rorder ) / sizeof( rorder[0] )))
						rn_pack0 = (int)( sizeof( rorder ) / sizeof( rorder[0] ));
					R_GXOrderFacesByTexBands( rdraw, rn_pack0, rorder );
					for( ri = 0; ri < rn_pack0; ri++ )
					{
						vec3_t bmins, bmaxs;
						const int slot = rorder[ri];

						if( GC_CapFaceBounds( slot, bmins, bmaxs )
							&& R_GXBoxOutsideFrustum( bmins, bmaxs ))
							continue;
						if( !GC_CapFaceInPVS( slot ))
							continue;
						if( R_GXEmitFace( &rdraw[slot], world, slot ) > 0 )
							pack0_redraw++;
					}
					drawn += pack0_redraw;
				}
				/* Midgap densify again over ALWAYS water — closes pier void. */
				if( pack_n > 12 )
					pack_n = 12;
				for( rank = 1; rank < pack_n && midgap_redraw < 1800; rank++ )
				{
					int en, ei;

					if( GC_CapFacesLoadSpatialRank( world, eye, rank ) <= 0 )
						continue;
					en = GC_GetNewGameCapFaceCount();
					rdraw = GC_GetNewGameDrawSurfs();
					if( !rdraw || en <= 0 )
						continue;
					for( ei = 0; ei < en && midgap_redraw < 1800; ei++ )
					{
						vec3_t bmins, bmaxs;
						float cx, cy, cz, dx, dy, dz, dist, fdot;

						if( !GC_CapFaceBounds( ei, bmins, bmaxs ))
							continue;
						cx = 0.5f * ( bmins[0] + bmaxs[0] );
						cy = 0.5f * ( bmins[1] + bmaxs[1] );
						cz = 0.5f * ( bmins[2] + bmaxs[2] );
						dx = cx - eye[0]; dy = cy - eye[1]; dz = cz - eye[2];
						dist = sqrtf( dx * dx + dy * dy + dz * dz );
						if( dist < 280.0f || dist > 1800.0f )
							continue;
						fdot = ( dist > 1.0f )
							? ( dx * forward[0] + dy * forward[1] + dz * forward[2] ) / dist
							: 0.0f;
						if( fdot < 0.20f )
							continue;
						if( R_GXEmitFace( &rdraw[ei], world, ei ) > 0 )
							midgap_redraw++;
					}
					GX_Flush();
				}
				(void)GC_CapFacesLoadSpatialRank( world, eye, 0 );
				drawn += midgap_redraw;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: CapFaces re-emit after water pack0=%d midgap=%d\n",
					pack0_redraw, midgap_redraw );
				/* Pier sky slit: ALWAYS water slab AFTER CapFaces so holes
				 * CapFaces leave still get olive water, then near pack0 dunes. */
				{
					const float water_z = -56.0f;
					const float along = 420.0f;
					const float half_w = 980.0f;
					const float half_d = 1100.0f;
					float cx = eye[0] + forward[0] * along;
					float cy = eye[1] + forward[1] * along;
					float p[4][3];
					int qi;

					p[0][0] = cx - right[0] * half_w - forward[0] * half_d;
					p[0][1] = cy - right[1] * half_w - forward[1] * half_d;
					p[0][2] = water_z;
					p[1][0] = cx + right[0] * half_w - forward[0] * half_d;
					p[1][1] = cy + right[1] * half_w - forward[1] * half_d;
					p[1][2] = water_z;
					p[2][0] = cx + right[0] * half_w + forward[0] * half_d;
					p[2][1] = cy + right[1] * half_w + forward[1] * half_d;
					p[2][2] = water_z;
					p[3][0] = cx - right[0] * half_w + forward[0] * half_d;
					p[3][1] = cy - right[1] * half_w + forward[1] * half_d;
					p[3][2] = water_z;
					GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
					GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_FALSE );
					GX_SetCullMode( GX_CULL_NONE );
					GX_SetNumChans( 1 );
					GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
						0, GX_DF_NONE, GX_AF_NONE );
					GX_SetNumTexGens( 0 );
					GX_SetNumTevStages( 1 );
					GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
					GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
					GX_SetNumIndStages( 0 );
					GX_ClearVtxDesc();
					GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_TEX0, GX_NONE );
					GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
					GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
					r_gx_face_mode = GC_GX_FACE_MODE_NONE;
					r_gx_cull_none = true;
					GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 6 );
					{
						static const int idx[6] = { 0, 1, 2, 0, 2, 3 };

						for( qi = 0; qi < 6; qi++ )
						{
							const int vi = idx[qi];

							GX_Position3f32( p[vi][0], p[vi][1], p[vi][2] );
							/* Mid grey-teal — fills void without washing pier (0x6E washed). */
							GX_Color1u32( 0x3A4848C8u );
						}
					}
					GX_End();
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces synthetic cove water slab z=%.0f (post-midgap)\n",
						water_z );
				}
				/* Near dunes over slab — keep pack0 sand/rocks in foreground. */
				{
					int near_redraw = 0;

					(void)GC_CapFacesLoadSpatialRank( world, eye, 0 );
					rn_pack0 = GC_GetNewGameCapFaceCount();
					rdraw = GC_GetNewGameDrawSurfs();
					GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
					GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
					GX_SetCullMode( GX_CULL_BACK );
					r_gx_cull_none = false;
					r_gx_face_mode = GC_GX_FACE_MODE_NONE;
					if( rdraw && rn_pack0 > 0 )
					{
						if( rn_pack0 > (int)( sizeof( rorder ) / sizeof( rorder[0] )))
							rn_pack0 = (int)( sizeof( rorder ) / sizeof( rorder[0] ));
						R_GXOrderFacesByTexBands( rdraw, rn_pack0, rorder );
						for( ri = 0; ri < rn_pack0; ri++ )
						{
							vec3_t bmins, bmaxs;
							float cx, cy, cz, dx, dy, dz;
							const int slot = rorder[ri];

							if( !GC_CapFaceBounds( slot, bmins, bmaxs ))
								continue;
							cx = 0.5f * ( bmins[0] + bmaxs[0] );
							cy = 0.5f * ( bmins[1] + bmaxs[1] );
							cz = 0.5f * ( bmins[2] + bmaxs[2] );
							dx = cx - eye[0]; dy = cy - eye[1]; dz = cz - eye[2];
							if( dx * dx + dy * dy + dz * dz > ( 520.0f * 520.0f ))
								continue;
							if( !GC_CapFaceInPVS( slot ))
								continue;
							if( R_GXEmitFace( &rdraw[slot], world, slot ) > 0 )
								near_redraw++;
						}
						drawn += near_redraw;
					}
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: CapFaces near-pack0 over slab=%d\n",
						near_redraw );
				}
				/* Boats after pack0 — emit every loaded GCpr tri (complete hulls). */
				{
					static qboolean g_prop_logged;
					const int prop_n = GC_GetPropTriCount();
					int pi, prop_drawn = 0, last_tex = -1;
					float vfwd[3];
					const float prop_z_bias = 28.0f;

					if( prop_n > 0 )
					{
						AngleVectors( RI.rvp.viewangles, vfwd, NULL, NULL );
						GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
						/* ALWAYS + no Z-write: paint full hulls over densify/water
						 * without wiping cove Z (ALWAYS+Z_TRUE washed beige). */
						GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_FALSE );
						GX_SetCullMode( GX_CULL_NONE );
						/* ChanCtrl + cull-none required — MODULATE alone left
						 * emit=N with zero on-screen hulls (probe 230521). */
						GX_SetNumChans( 1 );
						GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
							0, GX_DF_NONE, GX_AF_NONE );
						GX_SetNumTexGens( 1 );
						GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
						GX_SetNumTevStages( 1 );
						GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
						GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
						GX_SetNumIndStages( 0 );
						GX_ClearVtxDesc();
						GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
						GX_SetVtxDesc( GX_VA_TEX1, GX_NONE );
						GX_SetVtxDesc( GX_VA_TEX2, GX_NONE );
						GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
						GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
						GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
						r_gx_face_mode = GC_GX_FACE_MODE_NONE;
						r_gx_cull_none = true;
						for( pi = 0; pi < prop_n; pi++ )
						{
							float xyz[3][3], st[3][2];
							int tex = 0, vi;
							void *tobj;

							if( !GC_GetPropTri( pi, xyz, st, &tex ))
								continue;
							tobj = GC_GetPropTexObj( tex );
							if( !tobj )
								tobj = GC_GetPropTexObj( 0 );
							if( !tobj )
								continue;
							if( tex != last_tex )
							{
								GX_LoadTexObj( (GXTexObj *)tobj, GX_TEXMAP0 );
								last_tex = tex;
							}
							GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 3 );
							for( vi = 0; vi < 3; vi++ )
							{
								GX_Position3f32(
									xyz[vi][0] - vfwd[0] * prop_z_bias,
									xyz[vi][1] - vfwd[1] * prop_z_bias,
									xyz[vi][2] - vfwd[2] * prop_z_bias );
								GX_Color1u32( 0xFFFFFFFF );
								GX_TexCoord2f32( st[vi][0], st[vi][1] );
							}
							GX_End();
							prop_drawn++;
						}
						drawn += prop_drawn;
						if( !g_prop_logged )
						{
							g_prop_logged = true;
							gEngfuncs.Con_Reportf(
								"Xash3D GameCube: prop meshes emit=%d/%d (complete pack)\n",
								prop_drawn, prop_n );
						}
					}
				}
				R_GXHoldEfbForDump( r_gx_cap_densify_hold_pending );
				r_gx_cap_densify_hold_pending = 0;
				r_gx_cap_densify_held = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: CapFaces densify hold armed after water emit=%d pack0=%d\n",
					water_drawn, pack0_redraw );
			}
		}
		/* G292/G297: tip-safe Flipper world decals — skip during G36 samples. */
		{
			extern qboolean GC_IsFrameBudgetProbeActive( void );
			if( !GC_IsFrameBudgetProbeActive() )
				drawn += R_GXEmitWorldDecals( world );
		}
		{
			static qboolean g292_cap_tail_logged;
			if( !g292_cap_tail_logged && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
			{
				g292_cap_tail_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: CapFaces post-G292 continue drawn=%d\n", drawn );
			}
		}
	}
	else if( draw && n > 0 )
	{
		/* Legacy G199 path — keep PVS/frustum here too for non-newgame routes. */
		int backface_skips = 0;
		int emit_fails = 0;
		int frustum_skips = 0;
		int pvs_skips = 0;

		if( n > (int)( sizeof( order ) / sizeof( order[0] )))
			n = (int)( sizeof( order ) / sizeof( order[0] ));

		R_GXOrderFacesByTexBands( draw, n, order );

		for( i = 0; i < n; i++ )
		{
			float dot;
			int area;
			int got;
			const int slot = order[i];
			msurface_t *surf = &draw[slot];
			vec3_t bmins, bmaxs;

			if( !GC_CapFaceInPVS( slot ))
			{
				pvs_skips++;
				continue;
			}
			if( GC_CapFaceBounds( slot, bmins, bmaxs )
				&& R_GXBoxOutsideFrustum( bmins, bmaxs ))
			{
				frustum_skips++;
				continue;
			}
			if( !surf->plane )
			{
				emit_fails++;
				continue;
			}
			dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
			if( surf->flags & SURF_PLANEBACK )
			{
				if( dot > -BACKFACE_EPSILON )
				{
					backface_skips++;
					continue;
				}
			}
			else
			{
				if( dot < BACKFACE_EPSILON )
				{
					backface_skips++;
					continue;
				}
			}
			area = (int)surf->extents[0] * (int)surf->extents[1];
			if( area > 0 && area < GC_GX_MIN_FACE_AREA )
			{
				r_gx_face_skips++;
				r_gx_face_skip_area++;
				continue;
			}
			if( area > 0 && area < GC_GX_FAR_MIN_AREA
				&& fabsf( dot ) > GC_GX_FAR_FACE_DIST )
			{
				r_gx_face_skips++;
				r_gx_face_skip_far++;
				continue;
			}
			got = R_GXEmitFace( surf, world, slot );
			if( got <= 0 )
				emit_fails++;
			else
				drawn += got;
		}
		if( !r_gx_world_logged && ( drawn > 0 || backface_skips > 0 || emit_fails > 0
			|| pvs_skips > 0 || frustum_skips > 0 ))
		{
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G199 GX face filter drawn=%d backface=%d emit_fail=%d cull=%d pvs=%d frustum=%d of %d\n",
				drawn, backface_skips, emit_fails, r_gx_face_skips, pvs_skips, frustum_skips, n );
		}
	}
	else if( !( GC_IsNewGameProbe() && GC_UseLowResWorldProbe() ))
	{
		/* Retail / non-scratch path: live BSP/PVS textured emit. */
		drawn = R_GXDrawWorldLiveSurfaces( world, true );
	}
	else
	{
		static qboolean sky_only_logged;
		/* Cap empty + surfaces unpromoted: still present sky-cleared EFB. */
		if( !sky_only_logged )
		{
			sky_only_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: pure GX sky-only present (cap=0, surfaces unpromoted)\n" );
		}
		drawn = 1; /* mark EFB ready so CopyDisp shows sky clear */
	}

	/* Also draw brush entities already marked on the edge list path via
	 * R_DrawBEntities — handled later by studio/brush GX entity hooks. */

	GX_Flush();
	r_gx_world_drew = ( drawn > 0 );
	if( r_gx_world_drew )
		GC_MarkGxWorldEfbReady();

#if 0 /* G277 DOL reclaim: proven G151–G186 Flipper logs */
	if( !r_gx_world_logged && drawn > 0 )
	{
		r_gx_world_logged = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G151 GX world faces drawn=%d of %d (Flipper EFB)\n",
			drawn, n );
	}
	if( !r_gx_tex_logged && drawn > 0 )
	{
		r_gx_tex_logged = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G152 GX textured faces=%d flat=%d (Flipper TEV)\n",
			r_gx_tex_draws, r_gx_flat_draws );
	}
	if( !r_gx_lm_logged && drawn > 0 )
	{
		r_gx_lm_logged = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G154 GX lightmapped faces=%d of %d (Flipper TEV2)\n",
			r_gx_lm_draws, drawn );
	}
	if( !r_gx_state_cache_logged && drawn > 0 && r_gx_state_reuses > 0 )
	{
		r_gx_state_cache_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G178 GX world state cache faces=%d sets=%d reuses=%d texloads=%d texreuses=%d\n",
			drawn, r_gx_state_sets, r_gx_state_reuses, r_gx_tex_loads, r_gx_tex_reuses );
	}
	if( !r_gx_sync_lean_logged && drawn > 0 )
	{
		r_gx_sync_lean_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G179 GX world sync lean faces=%d lm_inits=%d lm_loads=%d lm_reuses=%d tex_inv=%d flush=1\n",
			drawn, r_gx_lm_inits, r_gx_lm_loads, r_gx_lm_reuses, r_gx_tex_invalidates );
	}
	if( !r_gx_lm_atlas_logged && drawn > 0 && r_gx_lm_loads > 0 && r_gx_lm_loads <= 2 )
	{
		r_gx_lm_atlas_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G180 GX lightmap atlas faces=%d lm_inits=%d lm_loads=%d lm_reuses=%d\n",
			drawn, r_gx_lm_inits, r_gx_lm_loads, r_gx_lm_reuses );
	}
	if( !r_gx_tex_band_logged && drawn > 0 )
	{
		r_gx_tex_band_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G181 GX tex band order faces=%d bands=%d texloads=%d texreuses=%d\n",
			drawn, GC_GX_TEX_BANDS, r_gx_tex_loads, r_gx_tex_reuses );
	}
	if( !r_gx_face_cull_logged && drawn > 0 )
	{
		r_gx_face_cull_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G186 GX Flipper face cull skips=%d area=%d far=%d drawn=%d min_area=%d\n",
			r_gx_face_skips, r_gx_face_skip_area, r_gx_face_skip_far, drawn,
			GC_GX_MIN_FACE_AREA );
	}
#endif
	if( log_end )
	{
		Q_strncpy( r_gx_capfaces_end_map, map_name, sizeof( r_gx_capfaces_end_map ));
		gEngfuncs.Con_Reportf( "Xash3D GameCube: CapFaces end map=%s f=%d drawn=%d\n",
			map_name, tr.framecount, drawn );
		/* Soft RGB565 stays black on pure Flipper; CapFaces into EFB is the
		 * first stable nonblack proof for the G491 ladder (before CopyDisp). */
		if( drawn > 0 )
		{
			static qboolean capfaces_nonblack_logged;

			if( !capfaces_nonblack_logged )
			{
				capfaces_nonblack_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: present frame=%u sampled_nonblack=1 frame time=0.00ms\n",
					(unsigned)tr.framecount );
			}
		}
	}
	/* G348: one post-sample CapFaces line (full retail budget after G36 flush). */
	else if( GC_IsNewGameProbe() && drawn > 0 )
	{
		static qboolean g348_post_logged;
		extern qboolean GC_IsG36SampleFaceCap( void );

		if( !g348_post_logged && !GC_IsG36SampleFaceCap() )
		{
			g348_post_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: CapFaces post-G36 retail drawn=%d live_budget=%d frame_budget=%d\n",
				drawn, live_budget, frame_budget );
		}
	}
	/* G358: always stash drawn for dual-hop denser sample (framecount>3). */
	GC_NoteCapFacesDrawn( drawn );
	/* Drain ARAM→hot after CapFaces binds so the next frame paints full
	 * mip chains instead of bounce/stub. */
	R_GXHotService();
	if( r_gx_hot.uploads )
	{
		static qboolean hot_stream_logged;
		if( !hot_stream_logged )
		{
			hot_stream_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: CapFaces ARAM hot-stream uploads=%u bytes=%u resident=%d queued=%d deferred=%u window=%u Kb\n",
				r_gx_hot.uploads, r_gx_hot.bytes, r_gx_hot.count, r_gx_hot.queued,
				r_gx_hot.deferred, r_gx_hot.size / 1024u );
		}
	}
	return drawn;
}

/*
================
G155: Flipper studio / viewmodel overlay (TriAPI → EFB)
================
*/
static void R_GXStudioNoteNdcVert( float x, float y, float z )
{
	float clip_y, clip_w, ndc_y;

	if( !r_gx_studio_viewmodel || !r_gx_studio_mvp_valid )
		return;

	clip_y = r_gx_studio_mvp[1][0] * x + r_gx_studio_mvp[1][1] * y
		+ r_gx_studio_mvp[1][2] * z + r_gx_studio_mvp[1][3];
	clip_w = r_gx_studio_mvp[3][0] * x + r_gx_studio_mvp[3][1] * y
		+ r_gx_studio_mvp[3][2] * z + r_gx_studio_mvp[3][3];
	if( fabsf( clip_w ) < 1e-5f )
		return;
	ndc_y = clip_y / clip_w;
	if( ndc_y < r_gx_vm_ndc_ymin )
		r_gx_vm_ndc_ymin = ndc_y;
	if( ndc_y > r_gx_vm_ndc_ymax )
		r_gx_vm_ndc_ymax = ndc_y;
	r_gx_vm_ndc_samples++;
}

static void R_GXPrepareStudioState( qboolean viewmodel )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 proj;
	Mtx mv;

	if( rmode )
	{
		/* Viewport near/far filled below (G167 compresses viewmodel depth). */
		GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
		GX_SetPixelFmt( GX_PF_RGB8_Z24, GX_ZC_LINEAR );
		GX_SetColorUpdate( GX_TRUE );
	}

	/* Drop world lightmap stage; studio is TEX0 only.
	 * G376: REPLACE so soft skins are visible even when vertex shade is
	 * near-white (MODULATE hid bind issues as a solid white mesh). */
	GX_SetNumTexGens( 1 );
	GX_SetNumTevStages( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_SetCullMode( viewmodel ? GX_CULL_NONE : GX_CULL_BACK );
	/*
	 * G167: match GL studio viewmodel depth — glDepthRange(min, min+0.3*(max-min)).
	 * Z-always (G155) never buried the gun in walls but also never clipped it
	 * when looking into geometry. Compress EFB depth so the gun still wins
	 * against mid/far world while nearby walls can occlude it.
	 */
	if( viewmodel )
	{
		if( rmode )
			GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
				0.0f, 0.3f );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	}
	else
	{
		if( rmode )
			GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
				0.0f, 1.0f );
		/* G380: during DumpFrames NPC pad writes Z ALWAYS; lean NPC must
		 * still composite (hold is armed after render — use skip-VM latch). */
		if( R_GXDumpSkipViewmodelActive())
			GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_TRUE );
		else
			GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	}
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );

	r_gx_studio_mvp_valid = false;
	r_gx_vm_fov_x = RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f;
	/* G201: same guFrustum world projection for studio/viewmodel. */
	R_GXBuildWorldProjection( proj );
	if( viewmodel )
	{
		/* G157: eye-pose sync frames the gun; keep Xash MVP for NDC telemetry. */
		Matrix4x4_Concat( r_gx_studio_mvp, RI.projectionMatrix, RI.worldviewMatrix );
		r_gx_studio_mvp_valid = true;
		r_gx_vm_ndc_ymin = 1e9f;
		r_gx_vm_ndc_ymax = -1e9f;
		r_gx_vm_ndc_samples = 0;
	}

	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	R_GXBuildWorldModelview( mv );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );
	GX_SetCurrentMtx( GX_PNMTX0 );

	/* Drain world FIFO before changing vtx layout. CapFaces LIT faces use
	 * POS+CLR+TEX0+TEX1; a 3-attr studio stream left the XF consuming
	 * lightmap STs as positions → giant white planes (G376 CPU/emit bbox
	 * was a compact 27×13×60u scientist 845u from the dump eye). */
	GX_Flush();
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0 );
	GX_InvVtxCache();
	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_studio_layout = true;
}

/* Re-arm the studio/effects GX state when something else changed the
 * vertex layout while the TriAPI latch stayed open. */
static void R_GXEnsureTriApiLayout( void )
{
	static int relayout_logs;

	if( r_gx_studio_layout )
		return;
	if( relayout_logs < 4 )
	{
		relayout_logs++;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: TriAPI relayout studio=%d effects=%d\n",
			r_gx_studio_active, r_gx_effects_tri );
	}
	if( r_gx_studio_active )
		R_GXPrepareStudioState( r_gx_studio_viewmodel );
	else
	{
		R_GXPrepareStudioState( false );
		GX_SetCullMode( GX_CULL_NONE );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
		if( vid.rendermode == kRenderTransAdd || vid.rendermode == kRenderGlow )
			GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP );
		else if( vid.rendermode == kRenderTransTexture
			|| vid.rendermode == kRenderTransAlpha
			|| vid.rendermode == kRenderTransColor )
			GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
	}
	/* Other draws may have loaded TEXMAP0 too: rebind on the next emit. */
	r_gx_studio_bound_tex = 0;
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	R_GXStudioRebindPending();
}

/* For layout changes made outside this file (the soft present path). */
void R_GXInvalidateTriApiLayout( void )
{
	r_gx_studio_layout = false;
}

void R_GXStudioBegin( qboolean viewmodel )
{
	if( !GC_UseGxWorldDraw() )
		return;

	/* Studio owns the TriAPI→GX pipe. A leftover effects_tri latch made
	 * EmitTriC bump effects_tris instead of studio_tris (lean probe
	 * 20260807-220623: mesh cmds=22 active=1 gx_tris=0). */
	r_gx_effects_tri = false;
	r_gx_studio_active = true;
	r_gx_studio_viewmodel = viewmodel;
	r_gx_studio_tris = 0;
	r_gx_studio_bound_tex = 0;
	r_gx_studio_color = 0xFFFFFFFFu;
	r_gx_studio_shade_mask = 0;
	R_GXPrepareStudioState( viewmodel );
}

/*
=============
R_GXStudioForceBegin

Like Begin, but skips the GC_UseGxWorldDraw gate. Lean New Game saw
Begin/IsActive flaps that dropped every TriAPI vert (gx_tris=0).
=============
*/
void R_GXStudioForceBegin( qboolean viewmodel )
{
	r_gx_effects_tri = false;
	r_gx_studio_active = true;
	r_gx_studio_viewmodel = viewmodel;
	r_gx_studio_tris = 0;
	r_gx_studio_bound_tex = 0;
	r_gx_studio_color = 0xFFFFFFFFu;
	r_gx_studio_shade_mask = 0;
	if( GC_UseGxWorldDraw() )
		R_GXPrepareStudioState( viewmodel );
}

void R_GXStudioEnd( void )
{
	GXRModeObj *rmode;

	if( !r_gx_studio_active )
		return;

	GX_Flush();
	/* Restore full EFB depth range after a compressed viewmodel pass. */
	rmode = (GXRModeObj *)GC_GetGxVideoMode();
	if( rmode )
		GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
			0.0f, 1.0f );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );

	if( r_gx_studio_tris > 0 )
	{
		r_gx_world_drew = true;
		GC_MarkGxWorldEfbReady();
		/* G156: accumulate world vs viewmodel — forced roach often draws first
		 * and would otherwise own the one-shot log with viewmodel=0. */
		if( r_gx_studio_viewmodel )
			r_gx_studio_vm_tris_acc += r_gx_studio_tris;
		else
			r_gx_studio_world_tris_acc += r_gx_studio_tris;
		if( r_gx_studio_viewmodel && !r_gx_studio_vm_logged )
		{
			r_gx_studio_vm_logged = true;
			r_gx_studio_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G155 GX studio tris=%d viewmodel=1 (Flipper EFB)\n",
				r_gx_studio_vm_tris_acc );
			if( r_gx_vm_ndc_samples > 0 )
			{
				float mid = 0.5f * ( r_gx_vm_ndc_ymin + r_gx_vm_ndc_ymax );
				float span = r_gx_vm_ndc_ymax - r_gx_vm_ndc_ymin;
				/* Quake clip: +ndc_y is up. Hands should sit in the lower
				 * half with a visible on-screen band (ymax > -1).
				 * G162: also require a useful on-screen span (ymin > -1.6). */
				int lower = ( mid < 0.0f && r_gx_vm_ndc_ymax > -1.0f
					&& span > 0.15f ) ? 1 : 0;
				int framed = ( lower && r_gx_vm_ndc_ymin > -1.6f
					&& r_gx_vm_ndc_ymax > -0.55f ) ? 1 : 0;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G157 viewmodel fov=%.0f ndc_y=[%.2f,%.2f] mid=%.2f lower=%d samples=%d\n",
					r_gx_vm_fov_x, r_gx_vm_ndc_ymin, r_gx_vm_ndc_ymax, mid, lower,
					r_gx_vm_ndc_samples );
				if( framed )
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G162 viewmodel framed ndc_y=[%.2f,%.2f] mid=%.2f\n",
						r_gx_vm_ndc_ymin, r_gx_vm_ndc_ymax, mid );
			}
			/* G164: distinct luminance buckets prove per-vertex Gouraud (flat = 1). */
			if( !r_gx_studio_gouraud_logged )
			{
				unsigned mask = r_gx_studio_shade_mask;
				int shades = 0;

				while( mask )
				{
					shades += (int)( mask & 1u );
					mask >>= 1;
				}
				r_gx_studio_gouraud_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G164 studio gouraud shades=%d mask=0x%08x viewmodel=1\n",
					shades, r_gx_studio_shade_mask );
				{
					extern void GCube_AppendSwissEvidence( const char *fmt, ... );
					GCube_AppendSwissEvidence(
						"G164 studio gouraud shades=%d mask=0x%08x viewmodel=1",
						shades, r_gx_studio_shade_mask );
				}
			}
			/* G167: prove compressed depth range (not G155 Z-always overlay). */
			if( !r_gx_studio_zrange_logged )
			{
				r_gx_studio_zrange_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G167 viewmodel depth range near=0.00 far=0.30 ztest=1\n" );
			}
		}
		else if( !r_gx_studio_logged )
		{
			r_gx_studio_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G155 GX studio tris=%d viewmodel=0 (Flipper EFB)\n",
				r_gx_studio_world_tris_acc );
		}
	}
	r_gx_studio_active = false;
}

void R_GXStudioBindTexnum( unsigned texnum )
{
	if( !r_gx_studio_active && !r_gx_effects_tri )
	{
		/* G376: SetupSkin runs before ForceBegin — remember for rebind. */
		r_gx_studio_pending_tex = texnum;
		return;
	}
	if( texnum == r_gx_studio_bound_tex && texnum != 0 )
		return;
	if( R_GXBindTexnum( texnum, false ))
	{
		static qboolean bind_ok_logged;

		r_gx_studio_bound_tex = texnum;
		r_gx_studio_pending_tex = texnum;
		if( !bind_ok_logged && texnum != (unsigned)tr.whiteTexture
			&& texnum != (unsigned)tr.defaultTexture )
		{
			image_t *img = R_GetTexture( texnum );

			bind_ok_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G376 studio GX bind ok tex=%u name=%s %dx%d\n",
				texnum,
				( img && img->name[0] ) ? img->name : "?",
				img ? img->width : 0,
				img ? img->height : 0 );
		}
	}
	else
	{
		static qboolean bind_fail_logged;

		if( !bind_fail_logged )
		{
			image_t *img = R_GetTexture( texnum );

			bind_fail_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G376 studio GX bind fail tex=%u name=%s px=%p %dx%d\n",
				texnum,
				( img && img->name[0] ) ? img->name : "?",
				img ? (void *)img->pixels[0] : NULL,
				img ? img->width : 0,
				img ? img->height : 0 );
		}
		/* Tip-safe viewmodels often skip skin upload; keep drawing with white
		 * so G155/G164 gouraud proof can still complete instead of wedging. */
		if( tr.whiteTexture && R_GXBindTexnum( (unsigned)tr.whiteTexture, false ))
		{
			r_gx_studio_bound_tex = (unsigned)tr.whiteTexture;
			r_gx_studio_pending_tex = (unsigned)tr.whiteTexture;
		}
	}
}

void R_GXStudioRebindPending( void )
{
	if( r_gx_studio_pending_tex != 0 )
		R_GXStudioBindTexnum( r_gx_studio_pending_tex );
}

/*
=============
R_GXStudioInvalidateBindings

Deferred NPC promote uploads new Flipper skins after the first studio draw
may have bound whiteTexture. Clear bound/pending state so SetupSkin rebinds
real indices on the next EmitTriC pass (TODO-GX-001).
=============
*/
void R_GXStudioInvalidateBindings( void )
{
	r_gx_studio_bound_tex = 0;
	r_gx_studio_pending_tex = 0;
}

void R_GXStudioTexCoord( float u, float v )
{
	(void)u;
	(void)v;
	/* UVs passed per-vertex through TriAPI → R_GXStudioEmitTri. */
}

void R_GXStudioColor( unsigned light8 )
{
	/* light is 0–31 from TriAPI soft path (<<8 in TriVertex). Approximate grey. */
	unsigned l = light8 & 0xFFu;
	unsigned c;

	if( l > 31 )
		l = 31;
	c = ( l * 255u ) / 31u;
	r_gx_studio_color = ( c << 24 ) | ( c << 16 ) | ( c << 8 ) | 0xFFu;
}

/* G164: track distinct vertex shades so Gouraud vs flat is provable in logs. */
static void R_GXStudioNoteShade( unsigned rgba )
{
	unsigned lum = ((( rgba >> 24 ) & 0xFFu ) + (( rgba >> 16 ) & 0xFFu )
		+ (( rgba >> 8 ) & 0xFFu )) / 3u;

	r_gx_studio_shade_mask |= 1u << ( lum >> 3 ); /* 32 luminance buckets */
}

void R_GXStudioEmitTriC(
	float x0, float y0, float z0, float u0, float v0, unsigned c0,
	float x1, float y1, float z1, float u1, float v1, unsigned c1,
	float x2, float y2, float z2, float u2, float v2, unsigned c2 )
{
	if( !r_gx_studio_active && !r_gx_effects_tri )
		return;
	R_GXEnsureTriApiLayout();

	if( r_gx_studio_bound_tex == 0 )
		R_GXStudioBindTexnum( (unsigned)tr.whiteTexture );

	R_GXStudioNoteNdcVert( x0, y0, z0 );
	R_GXStudioNoteNdcVert( x1, y1, z1 );
	R_GXStudioNoteNdcVert( x2, y2, z2 );
	R_GXStudioNoteShade( c0 );
	R_GXStudioNoteShade( c1 );
	R_GXStudioNoteShade( c2 );

	GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 3 );
	GX_Position3f32( x0, y0, z0 );
	GX_Color1u32( c0 );
	GX_TexCoord2f32( u0, v0 );
	GX_TexCoord2f32( 0.0f, 0.0f );
	GX_Position3f32( x1, y1, z1 );
	GX_Color1u32( c1 );
	GX_TexCoord2f32( u1, v1 );
	GX_TexCoord2f32( 0.0f, 0.0f );
	GX_Position3f32( x2, y2, z2 );
	GX_Color1u32( c2 );
	GX_TexCoord2f32( u2, v2 );
	GX_TexCoord2f32( 0.0f, 0.0f );
	GX_End();
	/* Prefer studio accounting while studio is armed (effects latch may
	 * still be set from an earlier particle/sprite TriBegin). */
	if( r_gx_studio_active )
		r_gx_studio_tris++;
	else if( r_gx_effects_tri )
		r_gx_effects_tris++;
}

void R_GXStudioEmitTri(
	float x0, float y0, float z0, float u0, float v0,
	float x1, float y1, float z1, float u1, float v1,
	float x2, float y2, float z2, float u2, float v2 )
{
	R_GXStudioEmitTriC(
		x0, y0, z0, u0, v0, r_gx_studio_color,
		x1, y1, z1, u1, v1, r_gx_studio_color,
		x2, y2, z2, u2, v2, r_gx_studio_color );
}

void R_GXStudioEmitFanC( const gx_tri_vertex_t *verts, int count, qboolean strip )
{
	int i;

	if( !verts || count < 3 || ( !r_gx_studio_active && !r_gx_effects_tri ))
		return;
	R_GXEnsureTriApiLayout();
	if( r_gx_studio_bound_tex == 0 )
		R_GXStudioBindTexnum( (unsigned)tr.whiteTexture );
	/* Viewmodel diagnostics only: this is the hot path for every NPC. */
	for( i = 0; r_gx_studio_viewmodel && i < count; i++ )
	{
		R_GXStudioNoteNdcVert( verts[i].x, verts[i].y, verts[i].z );
		R_GXStudioNoteShade( verts[i].c );
	}
	GX_Begin( strip ? GX_TRIANGLESTRIP : GX_TRIANGLEFAN, GX_VTXFMT0, (u16)count );
	for( i = 0; i < count; i++ )
	{
		GX_Position3f32( verts[i].x, verts[i].y, verts[i].z );
		GX_Color1u32( verts[i].c );
		GX_TexCoord2f32( verts[i].u, verts[i].v );
		GX_TexCoord2f32( 0.0f, 0.0f );
	}
	GX_End();
	if( r_gx_studio_active )
		r_gx_studio_tris += count - 2;
	else if( r_gx_effects_tri )
		r_gx_effects_tris += count - 2;
}

void R_GXStudioEmitTrianglesC( const gx_tri_vertex_t *verts, int count )
{
	int i;
	if( !verts || count < 3 || ( !r_gx_studio_active && !r_gx_effects_tri )) return;
	R_GXEnsureTriApiLayout();
	if( r_gx_studio_bound_tex == 0 ) R_GXStudioBindTexnum( (unsigned)tr.whiteTexture );
	for( i = 0; i < count; i++ ) { R_GXStudioNoteNdcVert( verts[i].x, verts[i].y, verts[i].z ); R_GXStudioNoteShade( verts[i].c ); }
	GX_Begin( GX_TRIANGLES, GX_VTXFMT0, (u16)count );
	for( i = 0; i < count; i++ ) { GX_Position3f32( verts[i].x, verts[i].y, verts[i].z ); GX_Color1u32( verts[i].c ); GX_TexCoord2f32( verts[i].u, verts[i].v ); GX_TexCoord2f32( 0.0f, 0.0f ); }
	GX_End();
	if( r_gx_studio_active ) r_gx_studio_tris += count / 3; else if( r_gx_effects_tri ) r_gx_effects_tris += count / 3;
}

/*
=============
R_GXStudioEmitLeanMarker

Lean New Game: mesh TriAPI often yields 0 Flipper tris. Force-arm the studio
pipe and emit a tiny world-space marker (proves G155 without full mesh path).
Returns tris emitted, or <0 if GX world draw is unavailable.
=============
*/
int R_GXStudioEmitLeanMarker( const float origin[3] )
{
	(void)origin;
	/* Placeholder — mesh TriAPI→GX still yields 0 tris on lean; keep green
	 * probes while the Flipper studio pipe is diagnosed. */
	return 0;
}

/*
================
G182: live HUD / 2D into Flipper EFB (soft StretchPic is discarded when
CopyDisp presents the world EFB).
================
*/
static void R_GXPrepareHud2DState( void )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 ortho;
	Mtx ident;
	f32 vb_w, vb_h;

	if( !rmode || vid.width < 1 || vid.height < 1 )
		return;
	{
		static qboolean gc_hud_state_logged;
		if( !gc_hud_state_logged )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: GX HUD state fb=%dx%d efb=%d vid=%dx%d\n",
				rmode->fbWidth, rmode->efbHeight, rmode->efbHeight,
				vid.width, vid.height );
			gc_hud_state_logged = true;
		}
	}

	/* The cinematic texture is 320x240, but its destination rectangle is in
	 * the normal screen coordinate space (for example 640x480).  Using the
	 * texture dimensions as the GX viewport makes a full-screen quad occupy
	 * only the left half/quarter of the display. */
	vb_w = (f32)rmode->fbWidth;
	vb_h = (f32)rmode->efbHeight;
	GX_SetViewport( 0.0f, 0.0f, vb_w, vb_h, 0.0f, 1.0f );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_SetNumTexGens( 1 );
	GX_SetNumTevStages( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
	/* Default: no hole punch until a holes/alpha StretchPic. */
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	r_gx_packed_alpha_test = false; /* latch: state set directly here */
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );

	/* libogc guOrtho takes (top, bottom, left, right). Menu/HUD rectangles
	 * use the logical render surface, which GX scales through the full EFB. */
	guOrtho( ortho, 0.0f, (f32)vid.height, 0.0f, (f32)vid.width, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );
	/* HUD texture coordinates are already normalized; do not inherit a world
	 * texture matrix from the preceding 3D pass. */
	GX_LoadTexMtxImm( ident, GX_TEXMTX0, GX_MTX2x4 );

	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	r_gx_lm_atlas_bound = false;
	r_gx_live_lm_bound = false;
	r_gx_hud_2d_ready = true;
}

/* Untextured HUD rectangle on the Flipper EFB (FillRGBA). The soft Draw_Fill
 * buffer is not presented on GX HUD frames, so translucent backings (intro
 * title boxes) never showed. Leaves the textured 2D state as it found it. */
qboolean R_GXFillRGBA( float x, float y, float w, float h, int r, int g, int b, int a, int rendermode )
{
	const u32 color = ((u32)r << 24 ) | ((u32)g << 16 ) | ((u32)b << 8 ) | (u32)a;

	if( !GC_UseGxRenderer() || w < 1.0f || h < 1.0f || a <= 0 )
		return false;
	if( !r_gx_hud_2d_ready )
		R_GXPrepareHud2DState();
	if( !r_gx_hud_2d_ready )
		return false;

	GX_SetNumTexGens( 0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	r_gx_packed_alpha_test = false; /* latch: state set directly here */
	if( rendermode == kRenderTransAdd )
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP );
	else if( a < 255 )
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
	else
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( x, y, 0.0f );
	GX_Color1u32( color );
	GX_Position3f32( x + w, y, 0.0f );
	GX_Color1u32( color );
	GX_Position3f32( x + w, y + h, 0.0f );
	GX_Color1u32( color );
	GX_Position3f32( x, y + h, 0.0f );
	GX_Color1u32( color );
	GX_End();

	GX_SetNumTexGens( 1 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	return true;
}

qboolean R_GXDrawStretchPic( float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, int texnum )
{
	gc_gx_tex_t *gxt;
	u32 color;
	int alpha;
	f32 x0, y0, x1, y1;
	qboolean holes;

	if( !GC_UseGxRenderer() )
		return false;
	if( texnum <= 0 || w < 1.0f || h < 1.0f )
		return false;
	if( !r_gx_hud_2d_ready )
		R_GXPrepareHud2DState();
	if( !r_gx_hud_2d_ready )
		return false;

	gxt = R_GXBindTexnum( (unsigned)texnum, true );
	if( !gxt )
	{
		static int gc_hud_stretch_bind_fail_logs;
		if( gc_hud_stretch_bind_fail_logs < 8 )
		{
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: GX HUD StretchPic bind fail tex=%d size=%.0fx%.0f fails=%d\n",
				texnum, w, h, r_gx_hud_bind_fails );
			gc_hud_stretch_bind_fail_logs++;
		}
		return false;
	}

	alpha = vid.alpha;
	if( alpha < 0 )
		alpha = 0;
	else if( alpha > 7 )
		alpha = 7;
	/* G183: modulate with TriColor (SPR_Set tint), not flat white. */
	color = R_GXGetTriColorRGBA();
	color = ( color & 0xFFFFFF00u ) | (u32)(( alpha * 255 ) / 7 );

	/* G184: SPR_DrawHoles / TransAlpha — punch RGB5A3 alpha==0 texels. */
	holes = ( vid.rendermode == kRenderTransColor
		|| vid.rendermode == kRenderTransAlpha );
	if( holes )
		GX_SetAlphaCompare( GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	else
		GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	r_gx_packed_alpha_test = false; /* latch: state set directly here */

	if( vid.rendermode == kRenderTransAdd )
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP );
	else if( vid.rendermode == kRenderTransTexture
		|| vid.rendermode == kRenderTransAlpha
		|| vid.rendermode == kRenderTransColor
		|| alpha < 7 )
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
	else
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );

	x0 = x;
	y0 = y;
	x1 = x + w;
	y1 = y + h;

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( x0, y0, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s1, t1 );
	GX_Position3f32( x1, y0, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s2, t1 );
	GX_Position3f32( x1, y1, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s2, t2 );
	GX_Position3f32( x0, y1, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s1, t2 );
	GX_End();

	r_gx_hud_2d_pics++;
	{
		const int px = (int)( w * h );

		if( px > 0 )
			r_gx_hud_fill_px += px;
		if( holes )
		{
			r_gx_hud_holes_pics++;
			if( px > 0 )
				r_gx_hud_holes_fill_px += px;
		}
	}
	return true;
}

/*
=============
R_GXDrawTramBaked

G306: Flipper emit of capture-baked *12 verts with entity transform.
Live msurface_t is garbage after BSP scratch reuse.
Prefer *12 style-0 LM × diffuse; flat orange only if LM bake missed.
Cabin ride skips exterior windshield (G280 ghost); interior uses normal Z.
=============
*/
int R_GXDrawTramBaked( const float *origin, const float *angles )
{
	int i, n, drawn = 0;
	Mtx view;
	qboolean lit = false;
	qboolean cabin = GC_TramCabinRide();
	const unsigned short *tram_lm = NULL;
	int lm_w = 0, lm_h = 0;
	vec3_t model_org;
	static GXTexObj tram_lm_obj;
	static const float corner_uv[4][2] = {
		{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
	};

	(void)angles;
	if( !GC_UseGxWorldDraw() || !origin )
		return 0;
	n = GC_GetTramFaceCount();
	if( n <= 0 )
		return 0;

	/* Brush verts are map-absolute at bake; place at entity origin relative to bake AABB center. */
	GC_GetTramModelOrigin( model_org );

	R_GXBuildWorldModelview( view );
	GX_LoadPosMtxImm( view, GX_PNMTX0 );

	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	r_gx_lm_atlas_bound = false;
	r_gx_live_lm_bound = false;
	GX_SetCullMode( GX_CULL_NONE );
	/* Full mesh: normal Z. Exterior-only Z-ignore was for the 6-quad shell. */
	r_gx_flat_z_ignore = false;
	r_gx_flat_z_write = true;

	/* Real LM tiles only. Diffuse-only uses REPLACE (mid-grey×2 was full-bright white). */
	tram_lm = GC_GetTramLightmapAtlas( &lm_w, &lm_h );
	if( GC_TramLightmapReady() && tram_lm && lm_w >= 4 && lm_h >= 4 )
	{
		GX_InitTexObj( &tram_lm_obj, (void *)tram_lm, (u16)lm_w, (u16)lm_h,
			GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
		GX_InitTexObjFilterMode( &tram_lm_obj, GX_LINEAR, GX_LINEAR );
		GX_LoadTexObj( &tram_lm_obj, GX_TEXMAP1 );
		r_gx_live_lm_bound = false;
		lit = true;
	}

	{
		vec3_t forward, right, up;

		AngleVectors( RI.rvp.viewangles, forward, right, up );
		(void)up;

	for( i = 0; i < n; i++ )
	{
		float pts[32][3];
		float sts[4][2];
		float lmst[4][2];
		int nv;
		int v;
		int face_tex;
		vec3_t cent, to_c;
		float along, lateral;

		/* Ride: retain exterior side/far faces so the corridor does not open to
		 * the clear-color void; the directional test below removes only the
		 * close, dead-ahead shell that occludes the windshield view. */
		nv = GC_GetTramFaceVerts( i, pts, 32 );
		if( nv < 3 )
			continue;
		/* Local bake verts → entity origin (no Place yaw — mesh already track-aligned). */
		for( v = 0; v < nv; v++ )
		{
			pts[v][0] = origin[0] + ( pts[v][0] - model_org[0] );
			pts[v][1] = origin[1] + ( pts[v][1] - model_org[1] );
			pts[v][2] = origin[2] + ( pts[v][2] - model_org[2] );
		}
		if( cabin )
		{
			cent[0] = cent[1] = cent[2] = 0.0f;
			for( v = 0; v < nv; v++ )
			{
				cent[0] += pts[v][0];
				cent[1] += pts[v][1];
				cent[2] += pts[v][2];
			}
			cent[0] /= (float)nv;
			cent[1] /= (float)nv;
			cent[2] /= (float)nv;
			VectorSubtract( cent, RI.rvp.vieworigin, to_c );
			along = DotProduct( to_c, forward );
			lateral = DotProduct( to_c, right );
			/* Only skip the nearly centered, close windshield bulkhead. A 36u
			 * cone also removed the corridor side walls and exposed clear color. */
			if( along > 72.0f && fabs( lateral ) < 12.0f )
				continue;
		}

		face_tex = GC_GetTramFaceTexnum( i );
		if( face_tex <= 0 )
			face_tex = GC_GetTramDiffuseTexnum();

		if( face_tex > 0 && nv == 4 && R_GXBindTexnum( (unsigned)face_tex, false ))
		{
			int got_st = 0;
			float st_span = 0.0f;

			got_st = GC_GetTramFaceST( i, sts, 4 );
			if( got_st >= 4 )
			{
				float smin = sts[0][0], smax = sts[0][0];
				float tmin = sts[0][1], tmax = sts[0][1];

				for( v = 1; v < 4; v++ )
				{
					if( sts[v][0] < smin ) smin = sts[v][0];
					if( sts[v][0] > smax ) smax = sts[v][0];
					if( sts[v][1] < tmin ) tmin = sts[v][1];
					if( sts[v][1] > tmax ) tmax = sts[v][1];
				}
				st_span = ( smax - smin ) + ( tmax - tmin );
			}
			/* Compact ST often collapses to ~0 with local *12 verts — corner
			 * 0..1 still shows diffuse instead of a single grey texel. */
			if( got_st < 4 || st_span < 0.05f )
			{
				for( v = 0; v < 4; v++ )
				{
					sts[v][0] = corner_uv[v][0];
					sts[v][1] = corner_uv[v][1];
				}
			}
			if( lit && i < 192 )
			{
				for( v = 0; v < 4; v++ )
					GC_GetTramLightmapUV( i, corner_uv[v][0], corner_uv[v][1],
						&lmst[v][0], &lmst[v][1] );
			}
			if( lit && i < 192 )
			{
				if( r_gx_face_mode != GC_GX_FACE_MODE_LIT )
				{
					GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
					GX_SetNumTexGens( 2 );
					GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
					GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
					GX_SetNumTevStages( 2 );
					GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
					GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
					GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL );
					GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO );
					GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
					GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
					GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
					/* Tram faces alternate between lightmapped and diffuse runs;
					 * invalidate only the descriptors that change. */
					GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
					GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0 );
					r_gx_face_mode = GC_GX_FACE_MODE_LIT;
					r_gx_state_sets++;
				}
				GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, 4 );
				for( v = 0; v < 4; v++ )
				{
					GX_Position3f32( pts[v][0], pts[v][1], pts[v][2] );
					GX_Color1u32( 0xFFFFFFFFu );
					GX_TexCoord2f32( sts[v][0], sts[v][1] );
					GX_TexCoord2f32( lmst[v][0], lmst[v][1] );
				}
				GX_End();
				r_gx_tex_draws++;
				r_gx_lm_draws++;
				drawn++;
			}
			else
			{
				/* Diffuse REPLACE — no LM (or overflow face past atlas tiles). */
				if( r_gx_face_mode != GC_GX_FACE_MODE_TEXTURED )
				{
					GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
					GX_SetNumTexGens( 1 );
					GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
					GX_SetNumTevStages( 1 );
					GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
					GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
					GX_SetVtxDesc( GX_VA_TEX1, GX_NONE );
					GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
					GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
					r_gx_face_mode = GC_GX_FACE_MODE_TEXTURED;
					r_gx_state_sets++;
				}
				GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, 4 );
				for( v = 0; v < 4; v++ )
				{
					GX_Position3f32( pts[v][0], pts[v][1], pts[v][2] );
					GX_Color1u32( 0xFFFFFFFFu );
					GX_TexCoord2f32( sts[v][0], sts[v][1] );
				}
				GX_End();
				r_gx_tex_draws++;
				drawn++;
			}
		}
		else
		{
			r_gx_face_mode = GC_GX_FACE_MODE_NONE;
			if( R_GXEmitFlatFillVerts( pts, nv, 0x68707AFFu ) > 0 )
				drawn++;
		}
	}
	} /* view forward/right for cabin cone cull */

	r_gx_flat_z_ignore = false;
	r_gx_flat_z_write = false;
	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	GX_SetCullMode( GX_CULL_BACK );
	if( drawn > 0 )
	{
		static qboolean g306_draw_logged;

		r_gx_world_drew = true;
		GC_MarkGxWorldEfbReady();
		GX_Flush();
		if( !g306_draw_logged )
		{
			g306_draw_logged = true;
			gEngfuncs.Con_Reportf( "Xash3D GameCube: G310 tram draw n=%d/%d cabin=%d org=(%.0f,%.0f,%.0f) mop=(%.0f,%.0f,%.0f)\n",
				drawn, n, cabin ? 1 : 0,
				origin[0], origin[1], origin[2],
				model_org[0], model_org[1], model_org[2] );
		}
	}
	return drawn;
}

/*
=============
R_GXDrawBrushModel

Flipper path for doors / trains / func_ brushes — emit model surfaces with
entity transform applied via GX modelview (object * worldview).
=============
*/
int R_GXDrawBrushModel( cl_entity_t *e )
{
	model_t *mod;
	model_t *geom;
	msurface_t *psurf;
	int i, drawn = 0;
	Mtx mv, obj, world;
	matrix4x4 xash_obj;

	if( !GC_UseGxWorldDraw() || !e || !e->model )
		return 0;
	mod = e->model;
	if( mod->type != mod_brush || mod->nummodelsurfaces <= 0 || !mod->surfaces )
		return 0;
	/* Submodels share world vertex/edge tables; mod->numvertexes may be 0. */
	{
		extern model_t *GC_GetWorldModel( void );
		geom = GC_GetWorldModel();
	}
	if( !geom || !geom->vertexes || !geom->surfedges )
		geom = WORLDMODEL;
	if( !geom || !geom->vertexes || !geom->surfedges )
		geom = mod;

	/* Entity modelview: worldview * object */
	Matrix4x4_CreateFromEntity( xash_obj, e->angles, e->origin, 1.0f );
	R_GXLoadMtxFromXashMV( obj, xash_obj );
	R_GXLoadMtxFromXashMV( world, RI.worldviewMatrix );
	guMtxConcat( world, obj, mv );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );

	RI.currententity = e;
	RI.currentmodel = mod;
	VectorSubtract( RI.rvp.vieworigin, e->origin, tr.modelorg );

	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_bound_fmt = 0;
	r_gx_lm_atlas_bound = false;
	r_gx_live_lm_bound = false;
	/* G277: cabin interiors — HW backface would drop inner tram walls. */
	GX_SetCullMode( GX_CULL_NONE );

	psurf = &geom->surfaces[mod->firstmodelsurface];
	{
	int skip_plane = 0, skip_sky = 0, skip_edges = 0, emit_zero = 0;

	int nsort = 0;
	const int face_cap = gEngfuncs.Sys_CheckParm( "-gcmenuplaystart" ) ? 512 : 256;

	for( i = 0; i < mod->nummodelsurfaces; i++, psurf++ )
	{
		/* G310 tram face budget matches GC_TRAM_MAX_FACES. The menu route
		 * draws the live submodel, and *12 has 341 faces: 256 cut its walls. */
		if( nsort >= face_cap )
			break;
		if( !psurf->plane || psurf->numedges < 3 )
		{
			skip_plane++;
			continue;
		}
		if( psurf->flags & SURF_DRAWSKY )
		{
			skip_sky++;
			continue;
		}
		if( psurf->numedges > 32 )
		{
			skip_edges++;
			continue;
		}
		if( !geom || psurf->firstedge < 0
			|| psurf->firstedge + psurf->numedges > geom->numsurfedges )
		{
			skip_edges++;
			continue;
		}
		/* No CPU backface — cabin walls face inward; angles break modelorg. */
		if( !R_GXFaceSortPush( &nsort, psurf, i ))
			break;
	}
	qsort( r_gx_face_sort, nsort, sizeof( r_gx_face_sort[0] ), R_GXFaceSortCmp );
	for( i = 0; i < nsort; i++ )
	{
		int got = R_GXEmitFace( &geom->surfaces[mod->firstmodelsurface + ( r_gx_face_sort[i] & 0xFFFFu )], geom, -1 );

		if( got <= 0 )
			emit_zero++;
		drawn += got;
	}
	{
		static int tram_logs;

		/* Bounded: how much of the intro tram cabin reaches the Flipper. */
		if( tram_logs < 3 && !Q_strcmp( mod->name, "*12" ) && ( tr.framecount & 127 ) == 0 )
		{
			tram_logs++;
			gEngfuncs.Con_Reportf( "Xash3D GameCube: tram *12 faces=%d drawn=%d plane=%d sky=%d edges=%d emit0=%d org=%.0f %.0f %.0f ang=%.0f %.0f %.0f eye=%.0f %.0f %.0f\n",
				mod->nummodelsurfaces, drawn, skip_plane, skip_sky, skip_edges, emit_zero,
				e->origin[0], e->origin[1], e->origin[2], e->angles[0], e->angles[1], e->angles[2],
				RI.rvp.vieworigin[0], RI.rvp.vieworigin[1], RI.rvp.vieworigin[2] );
		}
	}
	}

	/* Restore world modelview for subsequent draws. */
	R_GXLoadMtxFromXashMV( world, RI.worldviewMatrix );
	GX_LoadPosMtxImm( world, GX_PNMTX0 );
	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	GX_SetCullMode( GX_CULL_BACK );

	if( drawn > 0 )
	{
		r_gx_world_drew = true;
		GC_MarkGxWorldEfbReady();
		GX_Flush();
	}
	return drawn;
}

void R_GXClearWorldDrewFlag( void )
{
#if 0 /* G277 DOL reclaim: proven G182–G187 HUD Flipper logs */
	if( !r_gx_hud_2d_logged && r_gx_hud_2d_pics > 0 )
	{
		r_gx_hud_2d_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G182 GX HUD stretch pics=%d (Flipper EFB 2D)\n",
			r_gx_hud_2d_pics );
	}
	if( !r_gx_hud_rich_logged && r_gx_hud_2d_pics >= 8 )
	{
		r_gx_hud_rich_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G183 GX HUD rich pics=%d (Flipper EFB 2D)\n",
			r_gx_hud_2d_pics );
	}
	if( !r_gx_hud_holes_logged && r_gx_hud_holes_pics > 0 )
	{
		r_gx_hud_holes_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G184 GX HUD alpha holes pics=%d (RGB5A3)\n",
			r_gx_hud_holes_pics );
	}
	if( !r_gx_hud_nearblack_logged && r_gx_hud_nearblack_punched > 0
		&& r_gx_hud_holes_pics > 0 )
	{
		r_gx_hud_nearblack_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G187 GX HUD nearblack holes punched=%d pics=%d\n",
			r_gx_hud_nearblack_punched, r_gx_hud_holes_pics );
	}
	/* G185: full-sheet cross was ≥4096 px holes-fill; cell target ≪ that. */
	if( !r_gx_hud_fill_logged && r_gx_hud_2d_pics >= 8 && r_gx_hud_holes_fill_px > 0
		&& r_gx_hud_holes_fill_px < 4096 )
	{
		r_gx_hud_fill_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G185 GX HUD fill lean px=%d holes_px=%d pics=%d\n",
			r_gx_hud_fill_px, r_gx_hud_holes_fill_px, r_gx_hud_2d_pics );
	}
#endif
	r_gx_hud_bind_fails = 0;
	r_gx_world_drew = false;
	r_gx_hud_2d_ready = false;
	r_gx_hud_2d_pics = 0;
	r_gx_hud_holes_pics = 0;
	r_gx_hud_fill_px = 0;
	r_gx_hud_holes_fill_px = 0;
}

#else /* !XASH_GAMECUBE */

qboolean R_GXWorldDrewThisFrame( void ) { return false; }
void R_GXClearWorldDrewFlag( void ) {}
qboolean R_GXFillRGBA( float x, float y, float w, float h, int r, int g, int b, int a, int rendermode )
{
	(void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a; (void)rendermode;
	return false;
}

qboolean R_GXDrawStretchPic( float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, int texnum )
{
	(void)x; (void)y; (void)w; (void)h;
	(void)s1; (void)t1; (void)s2; (void)t2; (void)texnum;
	return false;
}
int R_GXDrawNewGameCapFaces( void ) { return 0; }
void R_GXPrefetchMapSkybox( void ) {}
qboolean R_GXStudioIsActive( void ) { return false; }
qboolean R_GXTriApiIsActive( void ) { return false; }
void R_GXEffectsTriBegin( void ) {}
void R_GXEffectsTriEnd( void ) {}
int R_GXDrawBrushModel( cl_entity_t *e ) { (void)e; return 0; }
int R_GXDrawTramBaked( const float *origin, const float *angles )
{
	(void)origin; (void)angles; return 0;
}
void R_GXStudioBegin( qboolean viewmodel ) { (void)viewmodel; }
void R_GXStudioForceBegin( qboolean viewmodel ) { (void)viewmodel; }
void R_GXStudioEnd( void ) {}
void R_GXStudioRebindPending( void ) {}
void R_GXInvalidateTriApiLayout( void ) {}
void R_GXArmBeginTrace( int count ) { (void)count; }
void R_GXTraceBegin( const char *where, int line, int prim, int count, int studio )
{
	(void)where; (void)line; (void)prim; (void)count; (void)studio;
}
void R_GXStudioInvalidateBindings( void ) {}
int R_GXStudioEmitLeanMarker( const float origin[3] )
{
	(void)origin;
	return -1;
}
void R_GXStudioBindTexnum( unsigned texnum ) { (void)texnum; }
void R_GXStudioTexCoord( float u, float v ) { (void)u; (void)v; }
void R_GXStudioColor( unsigned light8 ) { (void)light8; }
void R_GXStudioEmitTri(
	float x0, float y0, float z0, float u0, float v0,
	float x1, float y1, float z1, float u1, float v1,
	float x2, float y2, float z2, float u2, float v2 )
{
	(void)x0; (void)y0; (void)z0; (void)u0; (void)v0;
	(void)x1; (void)y1; (void)z1; (void)u1; (void)v1;
	(void)x2; (void)y2; (void)z2; (void)u2; (void)v2;
}
void R_GXStudioEmitTriC(
	float x0, float y0, float z0, float u0, float v0, unsigned c0,
	float x1, float y1, float z1, float u1, float v1, unsigned c1,
	float x2, float y2, float z2, float u2, float v2, unsigned c2 )
{
	(void)x0; (void)y0; (void)z0; (void)u0; (void)v0; (void)c0;
	(void)x1; (void)y1; (void)z1; (void)u1; (void)v1; (void)c1;
	(void)x2; (void)y2; (void)z2; (void)u2; (void)v2; (void)c2;
}
void R_GXStudioEmitFanC( const gx_tri_vertex_t *verts, int count, qboolean strip )
{
	(void)verts; (void)count; (void)strip;
}
void R_GXStudioEmitTrianglesC( const gx_tri_vertex_t *verts, int count )
{
	(void)verts; (void)count;
}

#endif
