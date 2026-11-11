/*
r_gx_tevmat.c - Source material features as Flipper TEV recipes

Lost Coast's world uses LightmappedGeneric / WorldVertexTransition. The host
bake (lostcoast-gc tools/bake_tevmat.py) classifies every material and writes
media/tevmat.gcm; CapFaces face byte 127 carries the material id (1..N, 0 =
legacy). This file turns a material id into extra TEV stages appended after
the stock diffuse x lightmap stages:

  $detail (+$detailscale, $detailblendfactor, blend mode 0 = mod2x)
      one stage, one texgen: prev * detail * 2. The detail UV is the diffuse
      UV through a scale texture matrix, so no extra vertex data. The blend
      factor is pre-baked into the I8 texel (0.5 + (d - 0.5) * f), and mips
      fade it to neutral 0.5 with distance like Source's detail fade.

  $alphatest (+$alphatestreference)
      GX alpha compare on the CMPR 1-bit alpha, Z written after texture.

File layout (big-endian):
  0  "GCmt"  u16 version(1)  u8 nmat  u8 ndetail
  8  nmat x { u8 flags, u8 detail_idx, u16 detail_scale_8_8, u8 alpharef, u8 pad[3] }
  .. pad to 32
  .. ndetail x GC_TEVMAT_DETAIL_BYTES  (64x64 I8, mips 64/32/16/8, GX tiled)
*/

#include "r_local.h"

#if XASH_GAMECUBE

#include <gccore.h>
#include <ogc/gx.h>
#include <string.h>

#define GC_TEVMAT_MAX_MATS	96
#define GC_TEVMAT_MAX_DETAIL	4
#define GC_TEVMAT_DETAIL_SIDE	64
#define GC_TEVMAT_DETAIL_BYTES	( 4096 + 1024 + 256 + 64 )

#define GC_TEVMAT_F_DETAIL	0x01
#define GC_TEVMAT_F_ALPHATEST	0x02

typedef struct
{
	u8	flags;
	u8	detail_idx;
	u16	detail_scale; /* 8.8 */
	u8	alpharef;
} gc_tevmat_t;

static gc_tevmat_t gc_tevmat[GC_TEVMAT_MAX_MATS];
static int gc_tevmat_count;
static int gc_tevmat_ndetail;
static int gc_tevmat_state; /* 0 = untried, 1 = ready, -1 = absent */
/* Detail texels live in the idle soft Z scratch after the bloom copy
 * (r_gcmap.c layout) — no new BSS. A soft Z clear (menu, mode change) wipes
 * them, so a guard word in front triggers a reload. */
extern void *R_GcmapBorrowSoftZ( unsigned offset, unsigned bytes );
#define GC_TEVMAT_BORROW_OFS	( 96u * 1024u )
#define GC_TEVMAT_GUARD	0x47436D74u /* 'GCmt' */
static u8 *gc_tevmat_store;
#define gc_tevmat_detail_data( i ) ( gc_tevmat_store + 32 + (i) * GC_TEVMAT_DETAIL_BYTES )
static GXTexObj gc_tevmat_detail_obj[GC_TEVMAT_MAX_DETAIL];
static qboolean gc_tevmat_alpha_on;

void R_GXTevMatEndAlphaTest( void );

static void R_GXTevMatLoad( void )
{
	file_t *f;
	byte hdr[8];
	byte rec[8];
	int i, nmat, ndet;
	fs_offset_t pos;

	gc_tevmat_state = -1;
	gc_tevmat_count = 0;
	gc_tevmat_ndetail = 0;
	if( gEngfuncs.Sys_CheckParm( "-gcnotevmat" ))
		return;
	gc_tevmat_store = (u8 *)R_GcmapBorrowSoftZ( GC_TEVMAT_BORROW_OFS,
		32 + GC_TEVMAT_MAX_DETAIL * GC_TEVMAT_DETAIL_BYTES );
	if( !gc_tevmat_store )
		return;
	f = gEngfuncs.fsapi->Open( "media/tevmat.gcm", "rb", false );
	if( !f )
		return;
	if( gEngfuncs.fsapi->Read( f, hdr, 8 ) != 8 || memcmp( hdr, "GCmt", 4 )
		|| hdr[4] != 0 || hdr[5] != 1 )
	{
		gEngfuncs.fsapi->Close( f );
		gEngfuncs.Con_Reportf( "Xash3D GameCube: TEV material table reject\n" );
		return;
	}
	nmat = hdr[6];
	ndet = hdr[7];
	if( ndet > GC_TEVMAT_MAX_DETAIL )
		ndet = GC_TEVMAT_MAX_DETAIL;
	for( i = 0; i < nmat; i++ )
	{
		if( gEngfuncs.fsapi->Read( f, rec, 8 ) != 8 )
			break;
		if( i >= GC_TEVMAT_MAX_MATS )
			continue;
		gc_tevmat[i].flags = rec[0];
		gc_tevmat[i].detail_idx = rec[1];
		gc_tevmat[i].detail_scale = (u16)(( rec[2] << 8 ) | rec[3] );
		gc_tevmat[i].alpharef = rec[4];
		if( ( gc_tevmat[i].flags & GC_TEVMAT_F_DETAIL ) && rec[1] >= ndet )
			gc_tevmat[i].flags &= (u8)~GC_TEVMAT_F_DETAIL;
	}
	gc_tevmat_count = nmat < GC_TEVMAT_MAX_MATS ? nmat : GC_TEVMAT_MAX_MATS;
	pos = ( 8 + (fs_offset_t)hdr[6] * 8 + 31 ) & ~(fs_offset_t)31;
	gEngfuncs.fsapi->Seek( f, pos, SEEK_SET );
	for( i = 0; i < ndet; i++ )
	{
		if( gEngfuncs.fsapi->Read( f, gc_tevmat_detail_data( i ), GC_TEVMAT_DETAIL_BYTES )
			!= GC_TEVMAT_DETAIL_BYTES )
			break;
		DCFlushRange( gc_tevmat_detail_data( i ), GC_TEVMAT_DETAIL_BYTES );
		GX_InitTexObj( &gc_tevmat_detail_obj[i], gc_tevmat_detail_data( i ),
			GC_TEVMAT_DETAIL_SIDE, GC_TEVMAT_DETAIL_SIDE, GX_TF_I8,
			GX_REPEAT, GX_REPEAT, GX_TRUE );
		GX_InitTexObjLOD( &gc_tevmat_detail_obj[i], GX_LIN_MIP_LIN, GX_LINEAR,
			0.0f, 3.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1 );
		gc_tevmat_ndetail = i + 1;
	}
	gEngfuncs.fsapi->Close( f );
	*(u32 *)gc_tevmat_store = GC_TEVMAT_GUARD;
	for( i = 0; i < gc_tevmat_count; i++ )
	{
		if( ( gc_tevmat[i].flags & GC_TEVMAT_F_DETAIL )
			&& gc_tevmat[i].detail_idx >= gc_tevmat_ndetail )
			gc_tevmat[i].flags &= (u8)~GC_TEVMAT_F_DETAIL;
	}
	gc_tevmat_state = 1;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: TEV material table mats=%d detail=%d\n",
		gc_tevmat_count, gc_tevmat_ndetail );
}

/* Feature flags for a CapFaces material id (0 = none / legacy pack). */
int R_GXTevMatFlags( int id )
{
	/* Soft Z scratch was cleared under us: texels are gone, read them again. */
	if( gc_tevmat_state == 1 && *(u32 *)gc_tevmat_store != GC_TEVMAT_GUARD )
	{
		gc_tevmat_state = 0;
		GX_InvalidateTexAll();
	}
	if( gc_tevmat_state == 0 )
		R_GXTevMatLoad();
	if( gc_tevmat_state != 1 || id <= 0 || id > gc_tevmat_count )
		return 0;
	return gc_tevmat[id - 1].flags;
}

/*
 * Append this material's stages after the stock lit setup.
 * In:  stages [0, first_stage) and texgens [0, first_texgen) already describe
 *      diffuse x lightmap (+ env). TEXCOORD0 must be the diffuse ST.
 * Uses GX_TEXMAP5 and GX_TEXMTX0. Sets NumTevStages / NumTexGens itself.
 */
void R_GXTevMatAppend( int id, int first_stage, int first_texgen )
{
	const int flags = R_GXTevMatFlags( id );
	int stage = first_stage;
	int texgen = first_texgen;

	if( flags & GC_TEVMAT_F_DETAIL )
	{
		const gc_tevmat_t *m = &gc_tevmat[id - 1];
		const f32 s = (f32)m->detail_scale / 256.0f;
		Mtx tm;

		guMtxScale( tm, s, s, 1.0f );
		GX_LoadTexMtxImm( tm, GX_TEXMTX0, GX_MTX2x4 );
		GX_SetTexCoordGen( GX_TEXCOORD0 + texgen, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0 );
		GX_LoadTexObj( &gc_tevmat_detail_obj[m->detail_idx], GX_TEXMAP5 );
		/* mod2x: prev * detail * 2 (detail texel is pre-biased around 0.5). */
		GX_SetTevOrder( GX_TEVSTAGE0 + stage, GX_TEXCOORD0 + texgen, GX_TEXMAP5, GX_COLORNULL );
		GX_SetTevColorIn( GX_TEVSTAGE0 + stage, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO );
		GX_SetTevColorOp( GX_TEVSTAGE0 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2,
			GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE0 + stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
		GX_SetTevAlphaOp( GX_TEVSTAGE0 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1,
			GX_TRUE, GX_TEVPREV );
		GX_SetTevDirect( GX_TEVSTAGE0 + stage );
		stage++;
		texgen++;
	}
	GX_SetNumTevStages( stage );
	GX_SetNumTexGens( texgen );

	if( flags & GC_TEVMAT_F_ALPHATEST )
	{
		const u8 ref = gc_tevmat[id - 1].alpharef ? gc_tevmat[id - 1].alpharef : 128;

		GX_SetAlphaCompare( GX_GEQUAL, ref, GX_AOP_AND, GX_ALWAYS, 0 );
		GX_SetZCompLoc( GX_FALSE );
		gc_tevmat_alpha_on = true;
	}
	else
		R_GXTevMatEndAlphaTest();
}

/* Call before any face mode that must not inherit an alpha-tested state. */
void R_GXTevMatEndAlphaTest( void )
{
	if( !gc_tevmat_alpha_on )
		return;
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetZCompLoc( GX_TRUE );
	gc_tevmat_alpha_on = false;
}

/* New map / pack generation: re-read the table. */
void R_GXTevMatReset( void )
{
	gc_tevmat_state = 0;
}

#endif /* XASH_GAMECUBE */
