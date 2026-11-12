/*
r_gx_bloom.c - Flipper post bloom (Twilight Princess style)

Cheap, hardware-native glow for the Lost Coast CapFaces path:

  1. GX_CopyTex the finished EFB through the copy unit's 2x box filter into
     a half-res I8 (luma) texture. 320x240 I8 = 75 KiB, borrowed from the idle soft Z
     scratch: no heap, no new BSS.
  2. Draw one fullscreen quad that samples that texture with offset taps.
     (8 texcoords, bilinear), averages them in TEV, subtracts a threshold,
     scales, and multiplies by a warm tint.
  3. Additive blend (ONE, ONE) back onto the EFB, Z off.

No scratch EFB regions, no extra copies, one texture: well inside Flipper's
budget (one EFB copy + one 640x480 fill at 10 TEV stages).

Enabled for -gccapfacessmoke (Lost Coast) or -gcbloom; -gcnobloom disables.
*/

#include "r_local.h"

#if XASH_GAMECUBE

#include <gccore.h>
#include <ogc/gx.h>
#include <string.h>
#include <math.h>

#define GC_BLOOM_MAX_BYTES ( 96u * 1024u )

/* The copy lands in the idle soft Z scratch (r_gcmap.c) — no new BSS. */
extern void *R_GcmapBorrowSoftZ( unsigned offset, unsigned bytes );
static GXTexObj gc_bloom_tex;

/* Bright surfaces across the frame, not only the sun disc. 232 left
 * everything but the sky under the floor. 64 lets the water, hulls
 * and walls feed the glow; pixels under that stay dark. */
#define GC_BLOOM_THRESHOLD 64 /* 0..255 luma floor */
#define GC_BLOOM_TINT_R 255
#define GC_BLOOM_TINT_G 236
#define GC_BLOOM_TINT_B 196
#define GC_BLOOM_TINT_A 120    /* overall strength, applied via tint scale */

static int gc_bloom_enabled = -1;

qboolean R_GXBloomEnabled( void )
{
	if( gc_bloom_enabled < 0 )
	{
		gc_bloom_enabled = 0;
		if( !gEngfuncs.Sys_CheckParm( "-gcnobloom" )
			&& ( gEngfuncs.Sys_CheckParm( "-gccapfacessmoke" )
				|| gEngfuncs.Sys_CheckParm( "-gcbloom" )))
			gc_bloom_enabled = 1;
	}
	return gc_bloom_enabled ? true : false;
}

/*
 * Apply bloom to the current EFB. Leaves GX in a 2D ortho state; the caller
 * must restore its 3D pipe (projection, VAT/desc, TEV, blend, Z).
 * Returns true if the pass ran.
 */
qboolean R_GXBloomApply( void *rmode_ptr )
{
	GXRModeObj *rmode = (GXRModeObj *)rmode_ptr;
	static qboolean logged;
	Mtx44 ortho;
	Mtx ident;
	u16 efb_w, efb_h, bw, bh;
	u8 *gc_bloom_buf;
	f32 fb_w, fb_h, du, dv;
	GXColor thresh, tint, zero;
	int i;
	/* Close ring keeps a hull or a window above the floor. The far pair
	 * spreads that glow onto the water and the walls. Threshold is per
	 * tap: averaging first was washing small brights out against the
	 * dark water, so only the sun disc survived. */
#define GC_BLOOM_TAPS 6
	static const f32 taps[GC_BLOOM_TAPS][2] = {
		{  3.0f,  0.0f }, { -3.0f,  0.0f }, {  0.0f,  3.0f }, {  0.0f, -3.0f },
		{ 14.0f, 10.0f }, { -14.0f, -10.0f },
	};

	if( !rmode || !R_GXBloomEnabled())
		return false;

	efb_w = rmode->fbWidth & ~15u;
	efb_h = rmode->efbHeight & ~7u;
	bw = efb_w / 2;
	bh = efb_h / 2;
	if( bw == 0 || bh == 0 || (u32)bw * bh > GC_BLOOM_MAX_BYTES )
		return false;
	gc_bloom_buf = (u8 *)R_GcmapBorrowSoftZ( 0, (u32)bw * bh );
	if( !gc_bloom_buf )
		return false;

	/* 1) EFB -> half-res luma through the copy box filter. */
	GX_SetTexCopySrc( 0, 0, efb_w, efb_h );
	GX_SetTexCopyDst( bw, bh, GX_TF_I8, GX_TRUE );
	DCInvalidateRange( gc_bloom_buf, (u32)bw * bh );
	GX_CopyTex( gc_bloom_buf, GX_FALSE );
	GX_PixModeSync();
	GX_InitTexObj( &gc_bloom_tex, gc_bloom_buf, bw, bh, GX_TF_I8,
		GX_CLAMP, GX_CLAMP, GX_FALSE );
	GX_InitTexObjLOD( &gc_bloom_tex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f,
		GX_FALSE, GX_FALSE, GX_ANISO_1 );
	GX_InvalidateTexAll();
	GX_LoadTexObj( &gc_bloom_tex, GX_TEXMAP0 );

	/* 2) 2D pipe: ortho over the full EFB. */
	fb_w = (f32)efb_w;
	fb_h = (f32)efb_h;
	GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	guOrtho( ortho, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );
	GX_SetCurrentMtx( GX_PNMTX0 );

	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetBlendMode( GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP );
	GX_SetNumChans( 0 );
	GX_SetNumTexGens( GC_BLOOM_TAPS );
	for( i = 0; i < GC_BLOOM_TAPS; i++ )
		GX_SetTexCoordGen( GX_TEXCOORD0 + i, GX_TG_MTX2x4, GX_TG_TEX0 + i, GX_IDENTITY );

	/* Each tap: clamp(tex - threshold), then add tap/8 into reg2.
	 * Last stage is reg2 * tint * 4, added onto the frame. */
	thresh.r = thresh.g = thresh.b = GC_BLOOM_THRESHOLD;
	thresh.a = 0;
	tint.r = (u8)(( GC_BLOOM_TINT_R * GC_BLOOM_TINT_A ) / 255 );
	tint.g = (u8)(( GC_BLOOM_TINT_G * GC_BLOOM_TINT_A ) / 255 );
	tint.b = (u8)(( GC_BLOOM_TINT_B * GC_BLOOM_TINT_A ) / 255 );
	tint.a = 255;
	zero.r = zero.g = zero.b = zero.a = 0;
	GX_SetTevColor( GX_TEVREG0, thresh );
	GX_SetTevColor( GX_TEVREG1, tint );
	GX_SetTevColor( GX_TEVREG2, zero );
	GX_SetNumTevStages( GC_BLOOM_TAPS * 2 + 1 );
	for( i = 0; i < GC_BLOOM_TAPS; i++ )
	{
		const u8 sub = (u8)( GX_TEVSTAGE0 + i * 2 );
		const u8 acc = (u8)( sub + 1 );

		GX_SetTevOrder( sub, GX_TEXCOORD0 + i, GX_TEXMAP0, GX_COLORNULL );
		GX_SetTevColorIn( sub, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC );
		GX_SetTevColorOp( sub, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( sub, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
		GX_SetTevAlphaOp( sub, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );

		GX_SetTevOrder( acc, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL );
		GX_SetTevKColorSel( acc, GX_TEV_KCSEL_1_8 );
		GX_SetTevColorIn( acc, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_C2 );
		GX_SetTevColorOp( acc, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG2 );
		GX_SetTevAlphaIn( acc, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
		GX_SetTevAlphaOp( acc, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	}
	{
		const u8 fin = (u8)( GX_TEVSTAGE0 + GC_BLOOM_TAPS * 2 );

		GX_SetTevOrder( fin, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL );
		GX_SetTevColorIn( fin, GX_CC_ZERO, GX_CC_C2, GX_CC_C1, GX_CC_ZERO );
		GX_SetTevColorOp( fin, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( fin, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
		GX_SetTevAlphaOp( fin, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	}

	/* Vertex stream: POS + one texcoord per tap (VTXFMT7 is unused elsewhere). */
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	for( i = 0; i < GC_BLOOM_TAPS; i++ )
	{
		GX_SetVtxDesc( GX_VA_TEX0 + i, GX_DIRECT );
		GX_SetVtxAttrFmt( GX_VTXFMT7, GX_VA_TEX0 + i, GX_TEX_ST, GX_F32, 0 );
	}
	GX_SetVtxAttrFmt( GX_VTXFMT7, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_InvVtxCache();

	du = 1.0f / (f32)bw;
	dv = 1.0f / (f32)bh;
	{
		static const f32 corner[4][4] = {
			/* x, y, u, v (unit) */
			{ 0.0f, 0.0f, 0.0f, 0.0f },
			{ 1.0f, 0.0f, 1.0f, 0.0f },
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			{ 0.0f, 1.0f, 0.0f, 1.0f },
		};
		int c, t;

		GX_Begin( GX_QUADS, GX_VTXFMT7, 4 );
		for( c = 0; c < 4; c++ )
		{
			GX_Position3f32( corner[c][0] * fb_w, corner[c][1] * fb_h, 0.0f );
			for( t = 0; t < GC_BLOOM_TAPS; t++ )
				GX_TexCoord2f32( corner[c][2] + taps[t][0] * du,
					corner[c][3] + taps[t][1] * dv );
		}
		GX_End();
	}

	/* Leave a sane default blend/TEV for whoever draws next. */
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumTevStages( 1 );
	GX_SetNumTexGens( 1 );
	GX_SetNumChans( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	for( i = 0; i < GC_BLOOM_TAPS * 2 + 1; i++ )
		GX_SetTevKColorSel( (u8)( GX_TEVSTAGE0 + i ), GX_TEV_KCSEL_1 );
	GX_ClearVtxDesc();

	if( !logged )
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: GX bloom pass %ux%u I8 taps=%d thresh=%d tint=%d,%d,%d\n",
			bw, bh, GC_BLOOM_TAPS, GC_BLOOM_THRESHOLD, tint.r, tint.g, tint.b );
	}
	return true;
}

/*
 * Sun disc (Source env_sun) — a soft radial fan at the sun's projected
 * position, drawn into the sky before the world. The bloom pass turns it
 * into the retail HDR glow; without bloom it is still a round soft sun
 * instead of screen-fixed quads. Expects the caller's 2D sky state:
 * ortho over the EFB, Z off, POS+CLR0 vertex desc, PASSCLR.
 *
 * d2_lostcoast: env_sun angles "0 317 0" pitch -10 → towards the sun is
 * yaw 137, 10 degrees up; rendercolor 253 236 198.
 */
#define GC_SUN_YAW	137.0f
#define GC_SUN_ELEV	10.0f
#define GC_SUN_SEGS	20

static void R_GXSunFan( f32 cx, f32 cy, f32 rx, f32 ry, u32 inner, u32 outer )
{
	int i;

	GX_Begin( GX_TRIANGLEFAN, GX_VTXFMT0, GC_SUN_SEGS + 2 );
	GX_Position3f32( cx, cy, -0.40f );
	GX_Color1u32( inner );
	for( i = 0; i <= GC_SUN_SEGS; i++ )
	{
		const f32 a = (f32)i * ( 2.0f * (f32)M_PI_F / (f32)GC_SUN_SEGS );

		GX_Position3f32( cx + cosf( a ) * rx, cy + sinf( a ) * ry, -0.40f );
		GX_Color1u32( outer );
	}
	GX_End();
}

qboolean R_GXBloomDrawSun( void *rmode_ptr )
{
	GXRModeObj *rmode = (GXRModeObj *)rmode_ptr;
	vec3_t fwd, right, up, sun;
	f32 fb_w, fb_h, df, tx, ty, cx, cy, r;
	const f32 yaw = GC_SUN_YAW * (f32)M_PI_F / 180.0f;
	const f32 el = GC_SUN_ELEV * (f32)M_PI_F / 180.0f;
	f32 fov_x = RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f;
	f32 fov_y = RI.rvp.fov_y > 1.0f ? RI.rvp.fov_y : 90.0f;

	if( !rmode || !R_GXBloomEnabled())
		return false;
	fb_w = (f32)rmode->fbWidth;
	fb_h = (f32)rmode->efbHeight;
	sun[0] = cosf( el ) * cosf( yaw );
	sun[1] = cosf( el ) * sinf( yaw );
	sun[2] = sinf( el );
	AngleVectors( RI.rvp.viewangles, fwd, right, up );
	df = DotProduct( sun, fwd );
	if( df < 0.15f )
		return true; /* sun behind / far off-axis: nothing to draw */
	tx = DotProduct( sun, right ) / df / tanf( fov_x * (f32)M_PI_F / 360.0f );
	ty = DotProduct( sun, up ) / df / tanf( fov_y * (f32)M_PI_F / 360.0f );
	cx = fb_w * 0.5f * ( 1.0f + tx );
	cy = fb_h * 0.5f * ( 1.0f - ty );
	r = fb_h * 0.5f / tanf( fov_y * (f32)M_PI_F / 360.0f ); /* pixels per unit tan */

	/* Self-contained 2D colour state (the sky clear left ortho + identity). */
	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
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
	GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP );
	GX_SetCullMode( GX_CULL_NONE );
	/* Wide overcast halo, then the hot disc (env_sun size 8 ≈ 6 degrees).
	 * Compact 0.18×r underexposed sky (probe 172956 mae~43). */
	/* Banding fix: three hard fans read as stepped rings. Draw a stack of
	 * thin, low-alpha fans that add up (SRC_ALPHA, ONE) to a smooth falloff,
	 * then let GX dither hide the remaining 8-bit steps. */
	GX_SetDither( GX_TRUE );
	{
		const int layers = 14;
		int i;

		for( i = 0; i < layers; i++ )
		{
			const f32 t = (f32)i / (f32)( layers - 1 ); /* 0 outer → 1 core */
			const f32 rr = r * ( 0.52f - 0.48f * t * t );
			const u32 a = (u32)( 14.0f + 40.0f * t * t * t );
			const u32 rgb = ( t < 0.7f ) ? 0xFDECC6u : 0xFFF8E8u;

			R_GXSunFan( cx, cy, rr, rr, ( rgb << 8 ) | a, ( rgb << 8 ) | 0u );
		}
	}
	GX_SetDither( GX_FALSE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	return true;
}

/*
 * Distance fog (Source env_fog_controller) as Flipper hardware fog.
 * d2_lostcoast: fogcolor 151 166 181, fogstart 1600, fogend 50000.
 * Applied to lit world faces only; Begin/End are cheap flag checks.
 */
static qboolean gc_fog_on;

void R_GXFogBegin( void )
{
	const GXColor fog = { 151, 166, 181, 255 };
	f32 zfar;

	if( gc_fog_on || !R_GXBloomEnabled())
		return;
	zfar = Q_max( 8192.0f, RI.farClip > 1.0f ? RI.farClip : 8192.0f );
	GX_SetFog( GX_FOG_PERSP_LIN, 1600.0f, 50000.0f, 4.0f, zfar, fog );
	gc_fog_on = true;
}

void R_GXFogEnd( void )
{
	const GXColor none = { 0, 0, 0, 0 };

	if( !gc_fog_on )
		return;
	GX_SetFog( GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, none );
	gc_fog_on = false;
}

#endif /* XASH_GAMECUBE */
