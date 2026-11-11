/*
r_gx_bloom.c - Flipper post bloom (Twilight Princess style)

Cheap, hardware-native glow for the Lost Coast CapFaces path:

  1. GX_CopyTex the finished EFB through the copy unit's 2x box filter into
     a half-res I8 (luma) texture. 320x240 I8 = 75 KiB, BSS only, no heap.
  2. Draw one fullscreen quad that samples that texture with 8 offset taps
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

#define GC_BLOOM_MAX_W 320
#define GC_BLOOM_MAX_H 304 /* PAL efb 608/2 */

/* I8 tiles are 8x4; 32-byte aligned for the copy unit. */
static u8 gc_bloom_buf[GC_BLOOM_MAX_W * GC_BLOOM_MAX_H] __attribute__(( aligned( 32 )));
static GXTexObj gc_bloom_tex;

/* Retail Lost Coast HDR: bright sky / sun disc / water glints bloom warm. */
#define GC_BLOOM_THRESHOLD 168 /* 0..255 luma floor */
#define GC_BLOOM_TINT_R 255
#define GC_BLOOM_TINT_G 236
#define GC_BLOOM_TINT_B 196
#define GC_BLOOM_TINT_A 150    /* overall strength, applied via tint scale */

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
	f32 fb_w, fb_h, du, dv;
	GXColor thresh, tint;
	int i;
	/* Tap ring in half-res texels: 4 axis taps near, 4 diagonals farther. */
	static const f32 taps[8][2] = {
		{  1.5f,  0.0f }, { -1.5f,  0.0f }, {  0.0f,  1.5f }, {  0.0f, -1.5f },
		{  3.5f,  3.5f }, { -3.5f,  3.5f }, {  3.5f, -3.5f }, { -3.5f, -3.5f },
	};

	if( !rmode || !R_GXBloomEnabled())
		return false;

	efb_w = rmode->fbWidth & ~15u;
	efb_h = rmode->efbHeight & ~7u;
	bw = efb_w / 2;
	bh = efb_h / 2;
	if( bw == 0 || bh == 0 || bw > GC_BLOOM_MAX_W || bh > GC_BLOOM_MAX_H )
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
	GX_SetNumTexGens( 8 );
	for( i = 0; i < 8; i++ )
		GX_SetTexCoordGen( GX_TEXCOORD0 + i, GX_TG_MTX2x4, GX_TG_TEX0 + i, GX_IDENTITY );

	/* TEV: s0..s7 accumulate tap/8, s8 subtract threshold (x4), s9 tint. */
	thresh.r = thresh.g = thresh.b = GC_BLOOM_THRESHOLD;
	thresh.a = 0;
	tint.r = (u8)(( GC_BLOOM_TINT_R * GC_BLOOM_TINT_A ) / 255 );
	tint.g = (u8)(( GC_BLOOM_TINT_G * GC_BLOOM_TINT_A ) / 255 );
	tint.b = (u8)(( GC_BLOOM_TINT_B * GC_BLOOM_TINT_A ) / 255 );
	tint.a = 255;
	GX_SetTevColor( GX_TEVREG0, thresh );
	GX_SetTevColor( GX_TEVREG1, tint );
	GX_SetNumTevStages( 10 );
	for( i = 0; i < 8; i++ )
	{
		const u8 st = (u8)( GX_TEVSTAGE0 + i );

		GX_SetTevOrder( st, GX_TEXCOORD0 + i, GX_TEXMAP0, GX_COLORNULL );
		GX_SetTevKColorSel( st, GX_TEV_KCSEL_1_8 );
		GX_SetTevColorIn( st, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST,
			i == 0 ? GX_CC_ZERO : GX_CC_CPREV );
		GX_SetTevColorOp( st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
		GX_SetTevAlphaOp( st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	}
	/* s8: out = clamp(4 * (avg - threshold)) — the bright pass. */
	GX_SetTevOrder( GX_TEVSTAGE8, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL );
	GX_SetTevColorIn( GX_TEVSTAGE8, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV );
	GX_SetTevColorOp( GX_TEVSTAGE8, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE8, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
	GX_SetTevAlphaOp( GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	/* s9: out = bright * tint. */
	GX_SetTevOrder( GX_TEVSTAGE9, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL );
	GX_SetTevColorIn( GX_TEVSTAGE9, GX_CC_ZERO, GX_CC_CPREV, GX_CC_C1, GX_CC_ZERO );
	GX_SetTevColorOp( GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE9, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
	GX_SetTevAlphaOp( GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );

	/* Vertex stream: POS + TEX0..7 direct (VTXFMT7 is unused elsewhere). */
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	for( i = 0; i < 8; i++ )
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
			for( t = 0; t < 8; t++ )
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
	for( i = 0; i < 8; i++ )
		GX_SetTevKColorSel( (u8)( GX_TEVSTAGE0 + i ), GX_TEV_KCSEL_1 );
	GX_ClearVtxDesc();

	if( !logged )
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: GX bloom pass %ux%u I8 taps=8 thresh=%d tint=%d,%d,%d\n",
			bw, bh, GC_BLOOM_THRESHOLD, tint.r, tint.g, tint.b );
	}
	return true;
}

#endif /* XASH_GAMECUBE */
