/*
r_gx_worlddl.c - full GoldSrc BSP world as baked GX display lists + real PVS

Same GCwd format as lostcoast-gc. Host bake (tools/bake_worlddl_goldsrc.py)
emits media/wd_<map>.gcd: every drawable world face as GX display lists
grouped by (PVS cluster, material), the BSP node tree for point-in-leaf, and
Quake RLE PVS rows.

MEM1 is tight on DoD FULL BSP maps, so the whole .gcd lives in ARAM. Spill
only holds the small per-chunk cache state + a chunk bounce ring. Tree /
chunkdir / PVS rows are fetched from ARAM on demand.

-gcnobspdl disables the path (CapFaces / live emit fallback).
*/

#include "r_local.h"

void R_GXPropDrawHorizon( const float *org );

#if XASH_GAMECUBE

#include <gccore.h>
#include <ogc/gx.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../../engine/platform/gamecube/mem_gamecube.h"

#define GC_WD_MAGIC		0x47437764u /* 'GCwd' */
#define GC_WD_MAT_ENVMAP	0x04
#define GC_WD_MAX_MATS		512
#define GC_WD_MAX_CHUNKS	6144
#define GC_WD_ST_FRAC		9
/* Per-frame ARAM→spill fetch. 256 KiB left early frames sparse; 512 fills
 * the visible PVS faster while still lazy-loading distant chunks. */
#define GC_WD_LOAD_BUDGET	( 512u * 1024u )
#define GC_WD_WARMUP_BUDGET	( 1024u * 1024u )
/* 192 full 32×32 RGB565 pages (384 KiB). Paid for by shrinking gx_fifo
 * 512→128 KiB in vid_gamecube.c (non-network). Enough for a whole view. */
#define GC_WD_LM_SLOTS		192
#define GC_WD_LM_PAGE_BYTES	2048u

typedef struct
{
	u32	magic;
	u16	version, flags;
	u32	n_clusters, n_chunks, n_mats, n_nodes;
	u32	n_planes, ofs_mats, ofs_nodes, ofs_planes;
	u32	ofs_chunkdir, ofs_pvsdir, ofs_pvs, ofs_chunks;
	u32	file_size, max_chunk;
} gc_wd_header_t;

typedef struct
{
	u32	tex_hash;
	u32	hash2;		/* version 2 only; 0 on version 1 */
	u8	tevmat, flags;
	u16	pad;
} gc_wd_mat_t;

typedef struct
{
	u32	ofs, bytes;
	s16	mins[3], maxs[3];
	u16	cluster, pad;
} gc_wd_chunkdir_t;

typedef struct
{
	u16	mat, pad;
	u32	dl_ofs, dl_bytes;
} gc_wd_batch_t;

typedef struct
{
	s32	cache_ofs;
	s32	used_frame;
} gc_wd_slot_t;

extern void *Mod_GCWorldArraySpillTryAlloc( size_t bytes, const char *what );
extern void Mod_GCWorldArraySpillPinBase( const void *ptr, size_t bytes );
extern size_t Mod_GCWorldArraySpillUsed( void );
extern int GC_WorldTexnumByHash( unsigned hash );
extern qboolean R_GXWorldDLBindTex( unsigned texnum );

/* Bump relief and detail live in media/tevmat.gcm (I8 mod2x tiles). Sand
 * uses the real normal map: IndTex warps the sun disc, then the alpha mask
 * punches the wet glints (retail $normalmapalphaenvmapmask). */
#define GC_TEV_MAX		96
#define GC_TEV_DETAIL		5440
static u8 gc_tev_rec[GC_TEV_MAX][8];
static u8 gc_tev_nmat, gc_tev_ndet;
static s8 gc_tev_bound = -1;
/* Detail tiles stay in MEM1. The map ARAM window is the world pack; a
 * second reserve returns that same base and would wipe the BSP tree. */
static u8 gc_tev_store[8 * GC_TEV_DETAIL] __attribute__(( aligned( 32 )));
static GXTexObj gc_tev_obj;
static u8 gc_spec_mask[2048] __attribute__(( aligned( 32 )));
static GXTexObj gc_spec_obj;
static u8 gc_nrm_tex[2048] __attribute__(( aligned( 32 )));
static GXTexObj gc_nrm_obj;
static u8 gc_sun_tex[512] __attribute__(( aligned( 32 )));
static GXTexObj gc_sun_obj;
static qboolean gc_spec_ready;
static qboolean gc_nrm_ready;
static qboolean gc_sun_ready;
static byte *gc_wd_force;
static u32 gc_wd_force_size;
static int gc_wd_tev_gens = 1;
/* Lightmap pages: copied once from ARAM, kept at full 32×32. TEXMAP2. */
static byte gc_wd_lm_store[GC_WD_LM_SLOTS * GC_WD_LM_PAGE_BYTES] __attribute__(( aligned( 32 )));
static u32 gc_wd_lm_stream;	/* file offset of page 0 */
static s32 gc_wd_lm_slot_page[GC_WD_LM_SLOTS];
static s32 gc_wd_lm_slot_frame[GC_WD_LM_SLOTS];
static int gc_wd_lm_miss;
static int gc_wd_lm_binds;
static int gc_wd_chunk_lm;	/* current chunk uses TEXMAP2 lightmap */
static GXTexObj gc_wd_lm_obj;
/* Declared early — LmPage / SetupLit sit above the rest of the WorldDL state. */
static gc_wd_header_t gc_wd_hdr;
static int gc_wd_frame;

static qboolean R_GXWorldDLFetch( u32 file_ofs, void *dst, u32 len );

static void R_GXTevBindDetail( u8 idx )
{
	if( idx >= gc_tev_ndet )
		return;
	if( gc_tev_bound != (s8)idx )
	{
		GX_InitTexObj( &gc_tev_obj, gc_tev_store + (u32)idx * GC_TEV_DETAIL,
			64, 64, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_TRUE );
		GX_InitTexObjLOD( &gc_tev_obj, GX_LIN_MIP_LIN, GX_LINEAR,
			0.0f, 3.0f, 0.0f, GX_FALSE, GX_TRUE, GX_ANISO_1 );
		gc_tev_bound = (s8)idx;
	}
	GX_LoadTexObj( &gc_tev_obj, GX_TEXMAP1 );
}

static int R_GXTevMatFlags( int id )
{
	if( id < 1 || id > (int)gc_tev_nmat )
		return 0;
	return gc_tev_rec[id - 1][0];
}

/*
 * One copy of each page, full 32×32. A hit returns the cached bytes and
 * does not touch ARAM. A miss copies the page once into a slot the GPU
 * is done with (the frame starts with DrawDone). Slots already used this
 * frame are left alone.
 */
static byte *R_GXWorldDLLmPage( u32 page )
{
	int i, victim = -1;
	s32 oldest = 0x7fffffff;

	for( i = 0; i < GC_WD_LM_SLOTS; i++ )
	{
		if( gc_wd_lm_slot_page[i] == (s32)page )
		{
			gc_wd_lm_slot_frame[i] = gc_wd_frame;
			return gc_wd_lm_store + (u32)i * GC_WD_LM_PAGE_BYTES;
		}
		if( gc_wd_lm_slot_frame[i] < gc_wd_frame && gc_wd_lm_slot_frame[i] < oldest )
		{
			oldest = gc_wd_lm_slot_frame[i];
			victim = i;
		}
	}
	if( victim < 0 )
		return NULL;
	if( !R_GXWorldDLFetch( gc_wd_lm_stream + page * GC_WD_LM_PAGE_BYTES,
		gc_wd_lm_store + (u32)victim * GC_WD_LM_PAGE_BYTES, GC_WD_LM_PAGE_BYTES ))
		return NULL;
	if( gc_wd_lm_slot_page[victim] >= 0 )
		GX_InvalidateTexAll();
	gc_wd_lm_slot_page[victim] = (s32)page;
	gc_wd_lm_slot_frame[victim] = gc_wd_frame;
	gc_wd_lm_miss++;
	return gc_wd_lm_store + (u32)victim * GC_WD_LM_PAGE_BYTES;
}

/* Diffuse × lightmap page (TEXMAP2), or diffuse × vertex colour when no page. */
static void R_GXWorldDLSetupLit( int lightmap )
{
	GX_SetNumIndStages( 0 );
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetZCompLoc( GX_TRUE );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	if( gc_wd_hdr.version == 2 )
		GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
	if( lightmap )
	{
		GX_SetNumTexGens( 2 );
		GX_SetNumTevStages( 2 );
		GX_SetTevDirect( GX_TEVSTAGE0 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
		GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC );
		GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
		GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		GX_SetTevDirect( GX_TEVSTAGE1 );
		GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP2, GX_COLORNULL );
		GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO );
		/* ×2 on the LM multiply restores cliff/tower mass now that SpecOn
		 * is needle-tower only (wide plates no longer get the white fill).
		 * Grey-pad luxels stay mid-grey so unset UVs don't fullbright. */
		GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
		GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		return;
	}
	GX_SetNumTexGens( gc_wd_tev_gens );
	GX_SetNumTevStages( 1 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
	GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
		gc_wd_hdr.version == 2 ? GX_CS_SCALE_1 : GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
	GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
}

/* id 0 restores the lit diffuse stage. A detail id adds the sun-lit
 * bump relief (or authored detail) as mod2x on top. */
static void R_GXTevMatAppend( int id, int a, int b )
{
	u8 flags, didx, aref;
	u16 scale_fp;
	float scale;
	Mtx tm;
	int base_stages;

	(void)a;
	(void)b;
	R_GXWorldDLSetupLit( gc_wd_chunk_lm );
	base_stages = gc_wd_chunk_lm ? 2 : 1;
	if( id < 1 || id > (int)gc_tev_nmat )
		return;
	flags = gc_tev_rec[id - 1][0];
	didx = gc_tev_rec[id - 1][1];
	scale_fp = (u16)( ( gc_tev_rec[id - 1][2] << 8 ) | gc_tev_rec[id - 1][3] );
	aref = gc_tev_rec[id - 1][4];
	/* Detail mod2x on top of Source LM punched white voids into harbor
	 * cliffs. Keep relief only when there is no lightmap page. */
	if( !gc_wd_chunk_lm && ( flags & 0x01 ) && scale_fp && didx < gc_tev_ndet )
	{
		scale = (float)scale_fp / 256.0f;
		R_GXTevBindDetail( didx );
		tm[0][0] = scale; tm[0][1] = 0.0f; tm[0][2] = 0.0f; tm[0][3] = 0.0f;
		tm[1][0] = 0.0f; tm[1][1] = scale; tm[1][2] = 0.0f; tm[1][3] = 0.0f;
		tm[2][0] = 0.0f; tm[2][1] = 0.0f; tm[2][2] = 1.0f; tm[2][3] = 0.0f;
		GX_LoadTexMtxImm( tm, GX_TEXMTX0, GX_MTX2x4 );
		GX_SetNumTexGens( gc_wd_tev_gens > 1 ? 3 : 2 );
		GX_SetTexCoordGen( GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0 );
		GX_SetNumTevStages( base_stages + 1 );
		GX_SetTevOrder( GX_TEVSTAGE0 + base_stages, GX_TEXCOORD2, GX_TEXMAP1, GX_COLORNULL );
		GX_SetTevColorIn( GX_TEVSTAGE0 + base_stages, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO );
		GX_SetTevColorOp( GX_TEVSTAGE0 + base_stages, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE0 + base_stages, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
		GX_SetTevAlphaOp( GX_TEVSTAGE0 + base_stages, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	}
	/* Alpha-test on LM-lit cliff/brush mats cut white sky holes through
	 * solid plates. Keep punch-through only for unlit foliage-style mats. */
	if( ( flags & 0x02 ) && !gc_wd_chunk_lm )
	{
		GX_SetAlphaCompare( GX_GEQUAL, aref ? aref : 128, GX_AOP_AND, GX_ALWAYS, 0 );
		GX_SetZCompLoc( GX_FALSE );
	}
}
static void R_GXTevMatEndAlphaTest( void ) {}
/* Real Flipper fog lives in r_gx_bloom.c (d2_lostcoast env_fog_controller). */
extern void R_GXFogBegin( void );
extern void R_GXFogEnd( void );

static int gc_wd_state;	/* 0 untried, 1 ready, -1 absent */
static char gc_wd_map[64];
static size_t gc_wd_spill_mark;
static gc_wd_mat_t gc_wd_mats_local[GC_WD_MAX_MATS];
static const gc_wd_mat_t *gc_wd_mats = gc_wd_mats_local;
static gc_wd_slot_t *gc_wd_slots;
static s16 *gc_wd_texnum;
static u32 *gc_wd_chunk_bytes;	/* aligned size per chunk — avoids O(n²) ARAM dir fetches */
static u16 *gc_wd_chunk_cluster;
static s16 *gc_wd_chunk_mins;	/* n_chunks * 3 */
static s16 *gc_wd_chunk_maxs;
static u32 *gc_wd_chunk_ofs;
static byte *gc_wd_cache;
static u32 gc_wd_cache_size, gc_wd_cache_head;
static u32 gc_wd_aram;		/* entire file in ARAM */
static file_t *gc_wd_file;
static byte *gc_wd_pvs;
static int gc_wd_pvs_cluster = -2;
static byte gc_wd_scratch[1024] __attribute__(( aligned( 32 )));
/* 64 bytes: 6-byte nodes / 16-byte planes can straddle a 32-byte ARAM line. */
static byte gc_wd_node_tmp[64] __attribute__(( aligned( 32 )));
static byte gc_wd_plane_tmp[64] __attribute__(( aligned( 32 )));

/* Bytes [ofs, ofs+len) of the .gcd → dst (32-aligned). */
static qboolean R_GXWorldDLFetch( u32 file_ofs, void *dst, u32 len )
{
	len = ( len + 31u ) & ~31u;
	if( gc_wd_aram )
	{
		DCInvalidateRange( dst, len );
		GC_AramTransfer( gc_wd_aram + file_ofs, dst, len, false );
		return true;
	}
	if( !gc_wd_file )
		return false;
	gEngfuncs.fsapi->Seek( gc_wd_file, (fs_offset_t)file_ofs, SEEK_SET );
	return gEngfuncs.fsapi->Read( gc_wd_file, dst, len ) > 0;
}

static void R_GXWorldDLUnload( void )
{
	if( gc_wd_file )
		gEngfuncs.fsapi->Close( gc_wd_file );
	gc_wd_file = NULL;
	gc_wd_cache = NULL;
	gc_wd_slots = NULL;
	gc_wd_texnum = NULL;
	gc_wd_pvs = NULL;
	gc_wd_aram = 0;
	gc_wd_pvs_cluster = -2;
}

static void R_GXMatPackLoad( void )
{
	file_t *f;
	byte hdr[8];
	u32 bytes, nmat;
	static qboolean logged;

	gc_tev_nmat = 0;
	gc_tev_ndet = 0;
	gc_tev_bound = -1;
	gc_spec_ready = false;
	gc_nrm_ready = false;
	gc_sun_ready = false;
	f = gEngfuncs.fsapi->Open( "media/tevmat.gcm", "rb", false );
	if( f && gEngfuncs.fsapi->Read( f, hdr, 8 ) == 8
		&& hdr[0] == 'G' && hdr[1] == 'C' && hdr[2] == 'm' && hdr[3] == 't'
		&& hdr[4] == 0 && hdr[5] == 1 )
	{
		nmat = hdr[6];
		gc_tev_ndet = hdr[7];
		if( nmat > 0 && nmat <= GC_TEV_MAX && gc_tev_ndet > 0 && gc_tev_ndet <= 8
			&& gEngfuncs.fsapi->Read( f, gc_tev_rec, nmat * 8u ) == (fs_offset_t)( nmat * 8u ))
		{
			u32 ofs = ( 8u + nmat * 8u + 31u ) & ~31u;

			bytes = (u32)gc_tev_ndet * GC_TEV_DETAIL;
			gEngfuncs.fsapi->Seek( f, (fs_offset_t)ofs, SEEK_SET );
			if( gEngfuncs.fsapi->Read( f, gc_tev_store, bytes ) == (fs_offset_t)bytes )
			{
				DCFlushRange( gc_tev_store, bytes );
				gc_tev_nmat = (u8)nmat;
			}
			else
				gc_tev_ndet = 0;
		}
		gEngfuncs.fsapi->Close( f );
	}
	else if( f )
		gEngfuncs.fsapi->Close( f );
	f = gEngfuncs.fsapi->Open( "media/sand_spec.cmp", "rb", false );
	if( f && gEngfuncs.fsapi->Read( f, gc_spec_mask, sizeof( gc_spec_mask )) == (fs_offset_t)sizeof( gc_spec_mask ))
	{
		DCFlushRange( gc_spec_mask, sizeof( gc_spec_mask ));
		GX_InitTexObj( &gc_spec_obj, gc_spec_mask, 64, 64, GX_TF_CMPR,
			GX_REPEAT, GX_REPEAT, GX_FALSE );
		GX_InitTexObjFilterMode( &gc_spec_obj, GX_LINEAR, GX_LINEAR );
		gc_spec_ready = true;
		gEngfuncs.fsapi->Close( f );
	}
	else if( f )
		gEngfuncs.fsapi->Close( f );
	f = gEngfuncs.fsapi->Open( "media/sand_nrm.cmp", "rb", false );
	if( f && gEngfuncs.fsapi->Read( f, gc_nrm_tex, sizeof( gc_nrm_tex )) == (fs_offset_t)sizeof( gc_nrm_tex ))
	{
		DCFlushRange( gc_nrm_tex, sizeof( gc_nrm_tex ));
		GX_InitTexObj( &gc_nrm_obj, gc_nrm_tex, 64, 64, GX_TF_CMPR,
			GX_REPEAT, GX_REPEAT, GX_FALSE );
		GX_InitTexObjFilterMode( &gc_nrm_obj, GX_LINEAR, GX_LINEAR );
		gc_nrm_ready = true;
		gEngfuncs.fsapi->Close( f );
	}
	else if( f )
		gEngfuncs.fsapi->Close( f );
	f = gEngfuncs.fsapi->Open( "media/sand_sun.cmp", "rb", false );
	if( f && gEngfuncs.fsapi->Read( f, gc_sun_tex, sizeof( gc_sun_tex )) == (fs_offset_t)sizeof( gc_sun_tex ))
	{
		DCFlushRange( gc_sun_tex, sizeof( gc_sun_tex ));
		GX_InitTexObj( &gc_sun_obj, gc_sun_tex, 32, 32, GX_TF_CMPR,
			GX_CLAMP, GX_CLAMP, GX_FALSE );
		GX_InitTexObjFilterMode( &gc_sun_obj, GX_LINEAR, GX_LINEAR );
		gc_sun_ready = true;
		gEngfuncs.fsapi->Close( f );
	}
	else if( f )
		gEngfuncs.fsapi->Close( f );
	if( !logged )
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: tevmat mats=%u details=%u spec=%d nrm=%d sun=%d\n",
			(unsigned)gc_tev_nmat, (unsigned)gc_tev_ndet,
			gc_spec_ready ? 1 : 0, gc_nrm_ready ? 1 : 0, gc_sun_ready ? 1 : 0 );
	}
}

static void R_GXWorldDLLoad( const char *mapbase )
{
	char path[96];
	file_t *f;
	u32 tables, try_sz, i, done;
	byte *tbl;
	byte *bounce;

	gc_wd_state = -1;
	R_GXWorldDLUnload();
	if( gEngfuncs.Sys_CheckParm( "-gcnobspdl" ))
		return;
	Q_snprintf( path, sizeof( path ), "media/wd_%s.gcd", mapbase );
	gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL open %s\n", path );
	f = gEngfuncs.fsapi->Open( path, "rb", false );
	if( !f )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL missing %s\n", path );
		return;
	}
	if( gEngfuncs.fsapi->Read( f, &gc_wd_hdr, sizeof( gc_wd_hdr )) != sizeof( gc_wd_hdr )
		|| gc_wd_hdr.magic != GC_WD_MAGIC
		|| ( gc_wd_hdr.version != 1 && gc_wd_hdr.version != 2 )
		|| gc_wd_hdr.n_mats > GC_WD_MAX_MATS || gc_wd_hdr.n_chunks > GC_WD_MAX_CHUNKS )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL pack reject %s\n", path );
		gEngfuncs.fsapi->Close( f );
		return;
	}

	/* slots + texnum + pvs + compact chunkdir mirror (bytes/cluster/mins/maxs/ofs) */
	tables = gc_wd_hdr.n_chunks * sizeof( gc_wd_slot_t )
		+ gc_wd_hdr.n_mats * sizeof( s16 )
		+ (( gc_wd_hdr.n_clusters + 7 ) >> 3 ) + 64
		+ gc_wd_hdr.n_chunks * ( sizeof( u32 ) * 2 + sizeof( u16 ) + sizeof( s16 ) * 6 );
	tbl = (byte *)Mod_GCWorldArraySpillTryAlloc( tables, "world DL tables" );
	if( !tbl )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL no spill for tables %u Kb\n",
			tables / 1024u );
		gEngfuncs.fsapi->Close( f );
		return;
	}
	gc_wd_slots = (gc_wd_slot_t *)tbl;
	gc_wd_texnum = (s16 *)( gc_wd_slots + gc_wd_hdr.n_chunks );
	gc_wd_pvs = (byte *)( gc_wd_texnum + gc_wd_hdr.n_mats );
	{
		byte *p = (byte *)( gc_wd_pvs + (( gc_wd_hdr.n_clusters + 7 ) >> 3 ) + 64 );

		p = (byte *)(((uintptr_t)p + 3u ) & ~3u );
		gc_wd_chunk_bytes = (u32 *)p;
		p += gc_wd_hdr.n_chunks * sizeof( u32 );
		gc_wd_chunk_ofs = (u32 *)p;
		p += gc_wd_hdr.n_chunks * sizeof( u32 );
		gc_wd_chunk_cluster = (u16 *)p;
		p += gc_wd_hdr.n_chunks * sizeof( u16 );
		gc_wd_chunk_mins = (s16 *)p;
		p += gc_wd_hdr.n_chunks * 3 * sizeof( s16 );
		gc_wd_chunk_maxs = (s16 *)p;
	}
	for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
	{
		gc_wd_slots[i].cache_ofs = -1;
		gc_wd_slots[i].used_frame = -1000;
	}
	for( i = 0; i < gc_wd_hdr.n_mats; i++ )
		gc_wd_texnum[i] = -1;

	/* Material table + chunkdir compact mirror from the open file (pre-ARAM).
	 * Version 1 records are 8 bytes. Version 2 (Lost Coast) records are
	 * hash, hash2, tev, flags, pad — 12 bytes. */
	gEngfuncs.fsapi->Seek( f, (fs_offset_t)gc_wd_hdr.ofs_mats, SEEK_SET );
	if( gc_wd_hdr.version == 2 )
		gEngfuncs.fsapi->Read( f, gc_wd_mats_local, gc_wd_hdr.n_mats * sizeof( gc_wd_mat_t ));
	else
	{
		for( i = 0; i < gc_wd_hdr.n_mats; i++ )
		{
			u8 rec[8];

			if( gEngfuncs.fsapi->Read( f, rec, 8 ) != 8 )
				break;
			gc_wd_mats_local[i].tex_hash = *(u32 *)rec;
			gc_wd_mats_local[i].hash2 = 0;
			gc_wd_mats_local[i].tevmat = rec[4];
			gc_wd_mats_local[i].flags = rec[5];
			gc_wd_mats_local[i].pad = *(u16 *)( rec + 6 );
		}
	}
	gEngfuncs.fsapi->Seek( f, (fs_offset_t)gc_wd_hdr.ofs_chunkdir, SEEK_SET );
	for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
	{
		gc_wd_chunkdir_t d;

		if( gEngfuncs.fsapi->Read( f, &d, sizeof( d )) != (fs_offset_t)sizeof( d ))
			break;
		gc_wd_chunk_ofs[i] = d.ofs;
		gc_wd_chunk_bytes[i] = ( d.bytes + 31u ) & ~31u;
		gc_wd_chunk_cluster[i] = d.cluster;
		gc_wd_chunk_mins[i * 3 + 0] = d.mins[0];
		gc_wd_chunk_mins[i * 3 + 1] = d.mins[1];
		gc_wd_chunk_mins[i * 3 + 2] = d.mins[2];
		gc_wd_chunk_maxs[i * 3 + 0] = d.maxs[0];
		gc_wd_chunk_maxs[i * 3 + 1] = d.maxs[1];
		gc_wd_chunk_maxs[i * 3 + 2] = d.maxs[2];
	}

	/* Prefer a larger ring so PVS-visible chunks stay resident while the eye
	 * turns — still lazy (spill), never omit faces for a densify cheat.
	 * CapFaces listen map props (lean sherman/jagd/…) need ~256 KiB of the
	 * same spill after WorldDL; leave headroom (ava-prop-2 tip≈6 KiB heap). */
	gc_wd_cache = NULL;
	{
		const unsigned cache_hi =
			( gEngfuncs.Sys_CheckParm( "-gcworldrender" )
				&& gEngfuncs.Sys_CheckParm( "-gcplayerinput" ))
			? ( 192u * 1024u ) : ( 512u * 1024u );

		for( try_sz = cache_hi; try_sz >= 96u * 1024u; try_sz -= 32u * 1024u )
		{
			gc_wd_cache = (byte *)Mod_GCWorldArraySpillTryAlloc( try_sz, "world DL chunk cache" );
			if( gc_wd_cache )
				break;
		}
	}
	if( !gc_wd_cache || try_sz < gc_wd_hdr.max_chunk * 2u )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL no spill for chunk cache\n" );
		gEngfuncs.fsapi->Close( f );
		return;
	}
	gc_wd_cache_size = try_sz;
	gc_wd_cache_head = 0;
	Mod_GCWorldArraySpillPinBase( tbl, (size_t)(( gc_wd_cache + try_sz ) - tbl ));
	gc_wd_spill_mark = Mod_GCWorldArraySpillUsed();

	/* Whole pack → ARAM so tree/chunkdir/PVS/chunks share one store. */
	gc_wd_aram = GC_AramMapReserve( gc_wd_hdr.file_size );
	bounce = gc_wd_cache;
	done = 0;
	if( gc_wd_aram )
	{
		gEngfuncs.fsapi->Seek( f, 0, SEEK_SET );
		while( done < gc_wd_hdr.file_size )
		{
			u32 n = gc_wd_hdr.file_size - done;

			if( n > 64u * 1024u )
				n = 64u * 1024u;
			if( n > gc_wd_cache_size )
				n = gc_wd_cache_size;
			if( gEngfuncs.fsapi->Read( f, bounce, n ) != (fs_offset_t)n )
				break;
			DCFlushRange( bounce, ( n + 31u ) & ~31u );
			GC_AramTransfer( gc_wd_aram + done, bounce, ( n + 31u ) & ~31u, true );
			done += n;
		}
		if( done < gc_wd_hdr.file_size )
			gc_wd_aram = 0;
	}
	if( gc_wd_aram )
		gEngfuncs.fsapi->Close( f );
	else
		gc_wd_file = f;

	/* Lightmap page stream sits after the last padded chunk blob. */
	gc_wd_lm_stream = 0;
	gc_wd_chunk_lm = 0;
	if( gc_wd_hdr.version == 2 && gc_wd_hdr.n_chunks > 0 )
	{
		u32 last_end = 0;

		for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
		{
			u32 end = gc_wd_chunk_ofs[i] + gc_wd_chunk_bytes[i];

			if( end > last_end )
				last_end = end;
		}
		gc_wd_lm_stream = gc_wd_hdr.ofs_chunks + (( last_end + 31u ) & ~31u );
		for( i = 0; i < GC_WD_LM_SLOTS; i++ )
		{
			gc_wd_lm_slot_page[i] = -1;
			gc_wd_lm_slot_frame[i] = -1000;
		}
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: world DL lightmap pages stream@%u slots=%d (%u Kb)\n",
			(unsigned)gc_wd_lm_stream, GC_WD_LM_SLOTS,
			(unsigned)( GC_WD_LM_SLOTS * GC_WD_LM_PAGE_BYTES / 1024u ));
	}

	gc_wd_tev_gens = gc_wd_hdr.version == 2 ? 2 : 1;
	gc_wd_pvs_cluster = -2;
	/* The tail of the chunk cache is the overflow bounce. A frustum chunk
	 * that does not fit the shore budget still draws from here. */
	{
		u32 tail = ( gc_wd_hdr.max_chunk + 31u ) & ~31u;

		gc_wd_force = NULL;
		gc_wd_force_size = 0;
		if( tail >= 4096u && gc_wd_cache_size > tail + 96u * 1024u )
		{
			gc_wd_cache_size -= tail;
			gc_wd_force = gc_wd_cache + gc_wd_cache_size;
			gc_wd_force_size = tail;
		}
	}
	R_GXMatPackLoad();
	gc_wd_state = 1;
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: world DL %s v%u clusters=%u chunks=%u mats=%u cache=%u Kb file=%u Kb (%s)\n",
		path, gc_wd_hdr.version, gc_wd_hdr.n_clusters, gc_wd_hdr.n_chunks, gc_wd_hdr.n_mats,
		gc_wd_cache_size / 1024u, gc_wd_hdr.file_size / 1024u,
		gc_wd_aram ? "ARAM" : "disc" );
}

qboolean R_GXWorldDLActive( void )
{
	model_t *world = WORLDMODEL;
	char base[64];

	if( !world || !world->name[0] )
		return false;
	COM_FileBase( world->name, base, sizeof( base ));
	if( gc_wd_state != 0 && ( Q_stricmp( base, gc_wd_map )
		|| ( gc_wd_state == 1 && Mod_GCWorldArraySpillUsed() < gc_wd_spill_mark )))
		gc_wd_state = 0;
	if( gc_wd_state == 0 )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL Active load map=%s\n", base );
		Q_strncpy( gc_wd_map, base, sizeof( gc_wd_map ));
		R_GXWorldDLLoad( base );
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL Active done state=%d\n", gc_wd_state );
	}
	return gc_wd_state == 1;
}

static int R_GXWorldDLEyeCluster( const float *org )
{
	int idx = 0;
	u32 guard = 0;

	while( idx >= 0 )
	{
		u32 nofs, pofs;
		u16 plane_i;
		s16 c0, c1;
		float d;

		if( (u32)idx >= gc_wd_hdr.n_nodes || ++guard > gc_wd_hdr.n_nodes + 8u )
			return -1;
		nofs = gc_wd_hdr.ofs_nodes + (u32)idx * 6u;
		if( !R_GXWorldDLFetch( nofs & ~31u, gc_wd_node_tmp, 64 ))
			return -1;
		plane_i = *(const u16 *)( gc_wd_node_tmp + ( nofs & 31u ));
		c0 = *(const s16 *)( gc_wd_node_tmp + ( nofs & 31u ) + 2 );
		c1 = *(const s16 *)( gc_wd_node_tmp + ( nofs & 31u ) + 4 );
		if( plane_i >= gc_wd_hdr.n_planes )
			return -1;
		pofs = gc_wd_hdr.ofs_planes + (u32)plane_i * 16u;
		if( !R_GXWorldDLFetch( pofs & ~31u, gc_wd_plane_tmp, 64 ))
			return -1;
		{
			const float *p = (const float *)( gc_wd_plane_tmp + ( pofs & 31u ));

			d = p[0] * org[0] + p[1] * org[1] + p[2] * org[2] - p[3];
		}
		idx = ( d >= 0.0f ) ? c0 : c1;
	}
	return idx == -1 ? -1 : -idx - 2;
}

static void R_GXWorldDLLoadPVS( int cluster )
{
	const u32 visbytes = ( gc_wd_hdr.n_clusters + 7 ) >> 3;
	u32 dir_ofs, row, out = 0, in, base;

	gc_wd_pvs_cluster = cluster;
	memset( gc_wd_pvs, 0xff, visbytes );
	if( cluster < 0 )
		return;
	dir_ofs = gc_wd_hdr.ofs_pvsdir + (u32)cluster * 4u;
	base = dir_ofs & ~31u;
	if( !R_GXWorldDLFetch( base, gc_wd_scratch, 32 ))
		return;
	row = *(const u32 *)( gc_wd_scratch + ( dir_ofs - base ));
	if( row == 0xffffffffu )
		return;
	row += gc_wd_hdr.ofs_pvs;
	base = row & ~31u;
	if( !R_GXWorldDLFetch( base, gc_wd_scratch, sizeof( gc_wd_scratch )))
		return;
	in = row - base;
	memset( gc_wd_pvs, 0, visbytes );
	while( out < visbytes )
	{
		if( in + 2 > sizeof( gc_wd_scratch ))
		{
			row = base + in;
			base = row & ~31u;
			if( !R_GXWorldDLFetch( base, gc_wd_scratch, sizeof( gc_wd_scratch )))
				break;
			in = row - base;
		}
		if( gc_wd_scratch[in] )
			gc_wd_pvs[out++] = gc_wd_scratch[in++];
		else
		{
			out += gc_wd_scratch[in + 1];
			in += 2;
		}
	}
}

static const float *gc_wd_sort_org;

static float R_GXWorldDLChunkD2( u32 i )
{
	float d = 0.0f;
	int a;

	for( a = 0; a < 3; a++ )
	{
		const float c = gc_wd_sort_org[a];
		const float mn = (float)gc_wd_chunk_mins[i * 3 + a];
		const float mx = (float)gc_wd_chunk_maxs[i * 3 + a];
		const float t = c < mn ? mn : ( c > mx ? mx : c );
		const float e = c - t;

		d += e * e;
	}
	/* Nearby ground stays first. Chunks whose center is behind the eye
	 * would otherwise fill the cache with the bank we are standing past
	 * and leave the harbor shore out. */
	if( d > ( 400.0f * 400.0f ))
	{
		const float cx = ( (float)gc_wd_chunk_mins[i * 3 + 0] + (float)gc_wd_chunk_maxs[i * 3 + 0] ) * 0.5f;
		const float cy = ( (float)gc_wd_chunk_mins[i * 3 + 1] + (float)gc_wd_chunk_maxs[i * 3 + 1] ) * 0.5f;
		const float fwd = ( cx - gc_wd_sort_org[0] ) * RI.cull_vforward[0]
			+ ( cy - gc_wd_sort_org[1] ) * RI.cull_vforward[1];

		if( fwd < 0.0f )
			d += 10000000000.0f;
		else
		{
			const float hz = (float)gc_wd_chunk_maxs[i * 3 + 2]
				- (float)gc_wd_chunk_mins[i * 3 + 2];
			const float cz = ( (float)gc_wd_chunk_mins[i * 3 + 2]
				+ (float)gc_wd_chunk_maxs[i * 3 + 2] ) * 0.5f;

			/* Prefer harbor stacks / cliff faces in the sort. */
			if( hz > 480.0f && cz > 0.0f && d > ( 600.0f * 600.0f ))
			{
				const float dist = sqrtf( d );
				const float pulled = dist - 1000.0f;

				d = pulled > 1.0f ? pulled * pulled : 1.0f;
			}
		}
	}
	return d;
}

static int R_GXWorldDLChunkCmp( const void *pa, const void *pb )
{
	const float da = R_GXWorldDLChunkD2( *(const u16 *)pa );
	const float db = R_GXWorldDLChunkD2( *(const u16 *)pb );

	return ( da > db ) - ( da < db );
}

/*
 * PVS test for an entity box. The world DL now streams its BSP nodes and planes
 * from ARAM instead of keeping them resident, so walking the tree per entity
 * would cost a DMA per node. Until a resident node copy exists every entity
 * counts as visible here; brush entities are still frustum culled by the caller.
 */
qboolean R_GXWorldDLEntityBoxVisible( const vec3_t mins, const vec3_t maxs )
{
	(void)mins;
	(void)maxs;
	return true;
}

static byte *R_GXWorldDLResident( u32 i, u32 *budget )
{
	const u32 bytes = gc_wd_chunk_bytes[i];
	u32 head, k;

	if( gc_wd_slots[i].cache_ofs >= 0 )
		return gc_wd_cache + gc_wd_slots[i].cache_ofs;
	if( bytes > *budget || bytes > gc_wd_cache_size || bytes == 0 )
		return NULL;
	head = gc_wd_cache_head;
	if( head + bytes > gc_wd_cache_size )
		head = 0;
	for( k = 0; k < gc_wd_hdr.n_chunks; k++ )
	{
		const s32 o = gc_wd_slots[k].cache_ofs;
		u32 kb;

		if( o < 0 )
			continue;
		kb = gc_wd_chunk_bytes[k];
		if( (u32)o + kb <= head || (u32)o >= head + bytes )
			continue;
		/* DrawDone at the start of the frame retired last frame's lists.
		 * Locking those too left the cache full of the shore, so the tall
		 * brushes never got a slot and the towers stayed the prop slivers. */
		if( gc_wd_slots[k].used_frame == gc_wd_frame )
			return NULL;
	}
	for( k = 0; k < gc_wd_hdr.n_chunks; k++ )
	{
		const s32 o = gc_wd_slots[k].cache_ofs;
		u32 kb;

		if( o < 0 )
			continue;
		kb = gc_wd_chunk_bytes[k];
		if( (u32)o + kb <= head || (u32)o >= head + bytes )
			continue;
		gc_wd_slots[k].cache_ofs = -1;
	}
	if( !R_GXWorldDLFetch( gc_wd_hdr.ofs_chunks + gc_wd_chunk_ofs[i], gc_wd_cache + head, bytes ))
		return NULL;
	gc_wd_slots[i].cache_ofs = (s32)head;
	gc_wd_cache_head = head + bytes;
	*budget -= bytes;
	return gc_wd_cache + head;
}

/* Cache miss. The chunk is inside the frustum, so draw it from the tail
 * bounce instead of skipping it. Caller retires the GP before reuse. */
static byte *R_GXWorldDLForce( u32 i )
{
	const u32 bytes = gc_wd_chunk_bytes[i];

	if( !gc_wd_force || bytes == 0 || bytes > gc_wd_force_size )
		return NULL;
	GX_DrawDone();
	if( !R_GXWorldDLFetch( gc_wd_hdr.ofs_chunks + gc_wd_chunk_ofs[i], gc_wd_force, bytes ))
		return NULL;
	return gc_wd_force;
}

static void R_GXWorldDLFrustum( vec3_t normals[4] )
{
	vec3_t fwd, right, up;
	const float fx = ( RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f ) * (float)M_PI_F / 360.0f;
	const float fy = ( RI.rvp.fov_y > 1.0f ? RI.rvp.fov_y : 90.0f ) * (float)M_PI_F / 360.0f;
	const float sx = sinf( fx ), cx = cosf( fx ), sy = sinf( fy ), cy = cosf( fy );
	int i;

	AngleVectors( RI.rvp.viewangles, fwd, right, up );
	for( i = 0; i < 3; i++ )
	{
		normals[0][i] = fwd[i] * sx + right[i] * cx;
		normals[1][i] = fwd[i] * sx - right[i] * cx;
		normals[2][i] = fwd[i] * sy + up[i] * cy;
		normals[3][i] = fwd[i] * sy - up[i] * cy;
	}
}

static qboolean R_GXWorldDLBoxVisible( u32 i, vec3_t normals[4], const float *org )
{
	int p, a;
	const s16 *mins = &gc_wd_chunk_mins[i * 3];
	const s16 *maxs = &gc_wd_chunk_maxs[i * 3];

	for( p = 0; p < 4; p++ )
	{
		float dist = 0.0f;

		for( a = 0; a < 3; a++ )
			dist += normals[p][a] * (( normals[p][a] >= 0.0f ? (float)maxs[a] : (float)mins[a] ) - org[a] );
		if( dist < -8.0f )
			return false;
	}
	return true;
}

/* Harbor industrial stacks: tall and narrow. Wide cliff plates must stay on
 * the shore pass — SpecOn fill turns them into a white sheet. */
static qboolean R_GXWorldDLIsTower( u32 i )
{
	const float hx = (float)gc_wd_chunk_maxs[i * 3 + 0]
		- (float)gc_wd_chunk_mins[i * 3 + 0];
	const float hy = (float)gc_wd_chunk_maxs[i * 3 + 1]
		- (float)gc_wd_chunk_mins[i * 3 + 1];
	const float hz = (float)gc_wd_chunk_maxs[i * 3 + 2]
		- (float)gc_wd_chunk_mins[i * 3 + 2];
	const float cz = ( (float)gc_wd_chunk_mins[i * 3 + 2]
		+ (float)gc_wd_chunk_maxs[i * 3 + 2] ) * 0.5f;
	const float horiz = hx > hy ? hx : hy;
	float dx, dy, dz;

	/* Needle industrial stacks only. horiz < 0.80·hz matched 1k-wide cliff
	 * volumes (SpecOn → white vertical wash on the harbor wall). */
	if( hz <= 480.0f || cz <= 32.0f || !gc_wd_sort_org )
		return false;
	if( horiz > 280.0f || horiz > hz * 0.40f )
		return false;
	dx = ( (float)gc_wd_chunk_mins[i * 3 + 0] + (float)gc_wd_chunk_maxs[i * 3 + 0] ) * 0.5f
		- gc_wd_sort_org[0];
	dy = ( (float)gc_wd_chunk_mins[i * 3 + 1] + (float)gc_wd_chunk_maxs[i * 3 + 1] ) * 0.5f
		- gc_wd_sort_org[1];
	dz = cz - gc_wd_sort_org[2];
	return ( dx * dx + dy * dy + dz * dz ) > ( 520.0f * 520.0f );
}

/* Lost Coast sky_camera is at (0, 0, -5108) with scale 16. The tower row
 * lives in that volume. Drawing it here, scaled onto the horizon, is the
 * distant land. The sky cube above stays the existing pass.
 * One bounce is not enough: the GP reads the position array when it
 * reaches the display list, and the next ARAM fetch overwrites it first.
 * That tore the distant chunks into a sheet over the sky. Four slots,
 * retired with DrawDone before a slot is reused. */
#define GC_WD_SKY_RING	4
static byte gc_wd_sky_ring[GC_WD_SKY_RING][4096] __attribute__(( aligned( 32 )));
static int gc_wd_sky_ring_i;
static int gc_wd_sky_ring_n;

static byte *R_GXWorldDLSkySlot( void )
{
	byte *slot;

	if( gc_wd_sky_ring_n >= GC_WD_SKY_RING )
	{
		GX_DrawDone();
		gc_wd_sky_ring_n = 0;
	}
	slot = gc_wd_sky_ring[gc_wd_sky_ring_i & ( GC_WD_SKY_RING - 1 )];
	gc_wd_sky_ring_i++;
	gc_wd_sky_ring_n++;
	return slot;
}

static void R_GXWorldDLLoadView( Mtx mv, Mtx44 proj, const float *org, f32 z_near, f32 z_far )
{
	vec3_t forward, right, up;
	f32 fov_y = RI.rvp.fov_y > 1.0f ? RI.rvp.fov_y : 90.0f;
	f32 fov_x = RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f;
	f32 y_max, x_max;

	AngleVectors( RI.rvp.viewangles, forward, right, up );
	mv[0][0] = right[0];
	mv[0][1] = right[1];
	mv[0][2] = right[2];
	mv[0][3] = -DotProduct( right, org );
	mv[1][0] = up[0];
	mv[1][1] = up[1];
	mv[1][2] = up[2];
	mv[1][3] = -DotProduct( up, org );
	mv[2][0] = -forward[0];
	mv[2][1] = -forward[1];
	mv[2][2] = -forward[2];
	mv[2][3] = DotProduct( forward, org );
	y_max = z_near * tanf( fov_y * 3.14159265f / 360.0f );
	x_max = z_near * tanf( fov_x * 3.14159265f / 360.0f );
	guFrustum( proj, y_max, -y_max, -x_max, x_max, z_near, z_far );
}

/* Lost Coast env_sun, same direction as the sky disc. View space, for the
 * specular light. The normal matrix is the view rotation: the bake stores
 * world-space normals. */
static void R_GXWorldDLSpecLight( void )
{
	vec3_t forward, right, up, world, sun;
	Mtx nrm;
	GXLightObj lit;
	const float yaw = 137.0f * 3.14159265f / 180.0f;
	const float el = 10.0f * 3.14159265f / 180.0f;
	float len;
	GXColor sun_col = { 253, 236, 198, 255 };

	world[0] = cosf( el ) * cosf( yaw );
	world[1] = cosf( el ) * sinf( yaw );
	world[2] = sinf( el );
	AngleVectors( RI.rvp.viewangles, forward, right, up );
	sun[0] = DotProduct( right, world );
	sun[1] = DotProduct( up, world );
	sun[2] = -DotProduct( forward, world );
	len = sqrtf( sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2] );
	if( len > 0.001f )
	{
		sun[0] /= len;
		sun[1] /= len;
		sun[2] /= len;
	}
	/* Parallel sun. A specular lobe against a 10-degree sun is black on flat
	 * sand (the whole beach shares one half-angle). Diffuse N·L uses the
	 * baked normals, so slopes facing the sun pick up the warm light. */
	memset( &lit, 0, sizeof( lit ));
	GX_InitLightPos( &lit, sun[0] * 1000000.0f, sun[1] * 1000000.0f, sun[2] * 1000000.0f );
	GX_InitLightColor( &lit, sun_col );
	GX_InitLightAttn( &lit, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f );
	GX_LoadLightObj( &lit, GX_LIGHT0 );
	nrm[0][0] = right[0]; nrm[0][1] = right[1]; nrm[0][2] = right[2]; nrm[0][3] = 0.0f;
	nrm[1][0] = up[0];    nrm[1][1] = up[1];    nrm[1][2] = up[2];    nrm[1][3] = 0.0f;
	nrm[2][0] = -forward[0]; nrm[2][1] = -forward[1]; nrm[2][2] = -forward[2]; nrm[2][3] = 0.0f;
	GX_LoadNrmMtxImm( nrm, GX_PNMTX0 );
}

/* indtex: wet-sand IndTex path. Towers/detail use N·L fill only — sand_nrm
 * on industrial brushes made the skyline read as dark slivers. */
static void R_GXWorldDLSpecOn( u16 strength, int indtex )
{
	GXColor mat;
	u16 s = strength ? strength : 96;

	if( s > 200 )
		s = 200;
	/* Flat sand meets the sun at about N·L 0.17. Push the warm lift so the
	 * specular pass reads next to the diffuse in the framedump. */
	mat.r = (u8)( s * 255 / 200 );
	mat.g = (u8)( s * 230 / 200 );
	mat.b = (u8)( s * 175 / 200 );
	mat.a = 255;
	{
		static qboolean logged;

		if( !logged )
		{
			logged = true;
			gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL specular strength=%u indtex=%d\n",
				(unsigned)strength, indtex );
		}
	}
	R_GXWorldDLSpecLight();
	GX_SetBlendMode( GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
	GX_SetChanCtrl( GX_COLOR0A0, GX_ENABLE, GX_SRC_REG, GX_SRC_REG,
		GX_LIGHT0, GX_DF_CLAMP, GX_AF_NONE );
	{
		GXColor black = { 0, 0, 0, 255 };

		GX_SetChanAmbColor( GX_COLOR0A0, black );
	}
	GX_SetChanMatColor( GX_COLOR0A0, mat );
	/* Retail wet sand: sun disc looked up through the normal map, then
	 * gated by the normal-map alpha. N·L keeps the broad warm lift. */
	if( indtex && gc_nrm_ready && gc_sun_ready && gc_spec_ready && s >= 96 )
	{
		f32 imtx[2][3];
		Mtx tm;

		tm[0][0] = 8.0f; tm[0][1] = 0.0f; tm[0][2] = 0.0f; tm[0][3] = 0.0f;
		tm[1][0] = 0.0f; tm[1][1] = 8.0f; tm[1][2] = 0.0f; tm[1][3] = 0.0f;
		tm[2][0] = 0.0f; tm[2][1] = 0.0f; tm[2][2] = 1.0f; tm[2][3] = 0.0f;
		GX_LoadTexMtxImm( tm, GX_TEXMTX0, GX_MTX2x4 );
		GX_SetNumTexGens( 2 );
		GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
		GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0 );
		GX_LoadTexObj( &gc_nrm_obj, GX_TEXMAP3 );
		GX_LoadTexObj( &gc_sun_obj, GX_TEXMAP4 );
		/* Spec disc on TEXMAP5 — TEXMAP2 is the streamed lightmap page. */
		GX_LoadTexObj( &gc_spec_obj, GX_TEXMAP5 );
		GX_SetNumIndStages( 1 );
		GX_SetIndTexOrder( GX_INDTEXSTAGE0, GX_TEXCOORD1, GX_TEXMAP3 );
		GX_SetIndTexCoordScale( GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1 );
		/* Wider warp so the normal map moves the glint across the disc. */
		imtx[0][0] = 0.70f; imtx[0][1] = 0.0f; imtx[0][2] = 0.0f;
		imtx[1][0] = 0.0f; imtx[1][1] = 0.70f; imtx[1][2] = 0.0f;
		GX_SetIndTexMatrix( GX_ITM_0, imtx, 0 );
		GX_SetNumTevStages( 2 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP4, GX_COLOR0A0 );
		GX_SetTevIndWarp( GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_TRUE, GX_TRUE, GX_ITM_0 );
		GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
		GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA );
		GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP5, GX_COLORNULL );
		GX_SetTevDirect( GX_TEVSTAGE1 );
		GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO );
		GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
		GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	}
	else
	{
		GX_SetNumIndStages( 0 );
		GX_SetNumTexGens( 1 );
		GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
		GX_SetNumTevStages( 1 );
		/* Tower/detail fill: N·L × 2 so sparse industrial brushes read as
		 * one warm surface instead of a dark wire against the sky. */
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
		GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC );
		GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA );
		GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		GX_SetTevDirect( GX_TEVSTAGE0 );
		if( gc_spec_ready && s >= 96 )
		{
			/* TEXMAP5 — never TEXMAP2; that slot holds the chunk lightmap. */
			GX_LoadTexObj( &gc_spec_obj, GX_TEXMAP5 );
			GX_SetNumTevStages( 2 );
			GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP5, GX_COLOR0A0 );
			GX_SetTevDirect( GX_TEVSTAGE1 );
			GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
			GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
			GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
			GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		}
	}
}

static void R_GXWorldDLSpecOff( void )
{
	GX_SetNumIndStages( 0 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevDirect( GX_TEVSTAGE1 );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	R_GXWorldDLSetupLit( gc_wd_chunk_lm );
	/* SpecOn leaves TEXMAP5/IndTex dirty — restore diffuse×LM TEV and the
	 * chunk lightmap on TEXMAP2 for the next lit batch. */
	if( gc_wd_chunk_lm )
		GX_LoadTexObj( &gc_wd_lm_obj, GX_TEXMAP2 );
}

static void R_GXWorldDLEmitChunk( byte *chunk, int *cur_mat )
{
	const gc_wd_batch_t *b;
	u32 nb, k;
	int spec = 0;

	nb = *(const u16 *)( chunk + 2 );
	GX_SetArray( GX_VA_POS, chunk + *(const u32 *)( chunk + 4 ), 6 );
	b = (const gc_wd_batch_t *)( chunk + 16 );
	for( k = 0; k < nb; k++, b++ )
	{
		const gc_wd_mat_t *m;
		int env;

		if( b->mat >= gc_wd_hdr.n_mats )
			continue;
		m = &gc_wd_mats[b->mat];
		if( (int)b->mat != *cur_mat )
		{
			if( gc_wd_texnum[b->mat] < 0 )
				gc_wd_texnum[b->mat] = (s16)GC_WorldTexnumByHash( m->tex_hash );
			if( gc_wd_texnum[b->mat] <= 0 )
				continue;
			if( spec )
			{
				R_GXWorldDLSpecOff();
				spec = 0;
			}
			if( !R_GXWorldDLBindTex( (unsigned)gc_wd_texnum[b->mat] ))
				continue;
			*cur_mat = (int)b->mat;
		}
		env = gc_wd_hdr.version == 2 && ( m->flags & GC_WD_MAT_ENVMAP );
		GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
		if( env )
		{
			R_GXWorldDLSpecOn( m->pad, 1 );
			spec = 1;
			GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
			R_GXWorldDLSpecOff();
			spec = 0;
		}
	}
	if( spec )
		R_GXWorldDLSpecOff();
}

static void R_GXWorldDLClearDepth( void )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 ortho;
	Mtx ident;
	f32 fb_w, fb_h;

	if( !rmode )
		return;
	fb_w = (f32)rmode->fbWidth;
	fb_h = (f32)rmode->efbHeight;
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

static void R_GXWorldDLRestoreHarbor( Mtx mv, Mtx44 proj, const float *org )
{
	/* zmax*1.73 stops near 7 km. The refinery brushes sit past that and were
	 * clipped to nothing, leaving the prop detail as the only tower. */
	{
		f32 zfar = RI.farClip > 1.0f ? RI.farClip : 4096.0f;

		if( zfar < 16384.0f )
			zfar = 16384.0f;
		R_GXWorldDLLoadView( mv, proj, org, 4.0f, zfar );
	}
	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_INDEX16 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_POS, GX_POS_XYZ, GX_S16, 0 );
	GX_SetVtxDesc( GX_VA_NRM, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 6 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX0, GX_TEX_ST, GX_S16, GC_WD_ST_FRAC );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX1, GX_TEX_ST, GX_U8, 8 );
	GX_SetNumTexGens( 2 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
	GX_SetNumTevStages( 1 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
	GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
	GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
}

static void R_GXWorldDLDrawSkyline( void )
{
	Mtx mv, trans, scale, placed, sky;
	Mtx44 proj;
	int i, n = 0, cur_mat = -1;
	const float *org = RI.rvp.vieworigin;

	if( !gc_wd_aram || gc_wd_hdr.version != 2 )
		return;
	/* The frame-start DrawDone retired last frame's slots. */
	gc_wd_sky_ring_n = 0;
	/* Scale the sky volume out to the horizon. Viewing it from inside
	 * the volume stretches the near ground across the sky. The distant
	 * wall chunks are wide dark quads, not the ridge, so they stay out. */
	cur_mat = -1;
	R_GXWorldDLLoadView( mv, proj, org, 4.0f, 131072.0f );
	guMtxTrans( trans, 0.0f, 0.0f, 5108.0f );
	guMtxScale( scale, 16.0f, 16.0f, 16.0f );
	guMtxConcat( scale, trans, placed );
	guMtxConcat( mv, placed, sky );
	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	GX_LoadPosMtxImm( sky, GX_PNMTX0 );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	for( i = 0; i < (int)gc_wd_hdr.n_chunks; i++ )
	{
		const float cz = ( (float)gc_wd_chunk_mins[i * 3 + 2]
			+ (float)gc_wd_chunk_maxs[i * 3 + 2] ) * 0.5f;
		const float hz = (float)gc_wd_chunk_maxs[i * 3 + 2]
			- (float)gc_wd_chunk_mins[i * 3 + 2];
		const float cx = ( (float)gc_wd_chunk_mins[i * 3 + 0]
			+ (float)gc_wd_chunk_maxs[i * 3 + 0] ) * 0.5f;
		const float cy = ( (float)gc_wd_chunk_mins[i * 3 + 1]
			+ (float)gc_wd_chunk_maxs[i * 3 + 1] ) * 0.5f;
		const u32 bytes = gc_wd_chunk_bytes[i];

		if( cz > -4000.0f || hz < 80.0f )
			continue;
		if( cx * cx + cy * cy < ( 500.0f * 500.0f ))
			continue;
		if( bytes == 0 || bytes > 4096 )
			continue;
		{
			byte *slot = R_GXWorldDLSkySlot();

			if( !R_GXWorldDLFetch( gc_wd_hdr.ofs_chunks + gc_wd_chunk_ofs[i], slot, bytes ))
				continue;
			R_GXWorldDLEmitChunk( slot, &cur_mat );
			/* Pale industrial fill — without it the skyline reads as dark
			 * prop slivers instead of one tower surface on the horizon. */
			if( hz >= 100.0f )
			{
				/* Stronger fill so horizon stacks read as one glowing
				 * surface (frame_256 tower p90 ~255 vs ~229 without). */
				R_GXWorldDLSpecOn( 220, 0 );
				R_GXWorldDLEmitChunk( slot, &cur_mat );
				R_GXWorldDLSpecOff();
				cur_mat = -1;
			}
		}
		n++;
	}
	/* Far props draw once after the world, in this view, so a nearer cliff
	 * wins and the mesh is not stamped twice into the bloom. */
	R_GXWorldDLClearDepth();
	R_GXWorldDLRestoreHarbor( mv, proj, org );
	{
		static qboolean logged;

		if( !logged )
		{
			logged = true;
			gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL skyline chunks=%d\n", n );
		}
	}
}

int R_GXWorldDLDraw( void )
{
	static qboolean logged;
	static qboolean logged_eye;
	float org_local[3];
	const float *org = RI.rvp.vieworigin;
	vec3_t normals[4];
	u32 budget;
	u32 i;
	int cluster, drawn = 0, chunks = 0, waiting = 0, cur_mat = -1, bind_miss = 0;

	if( gc_wd_state != 1 )
		return 0;
	/* Retire last frame's display lists before this frame fills the FIFO.
	 * Without it the second present waits forever on the GP. */
	if( gc_wd_frame > 0 )
		GX_DrawDone();
	gc_wd_frame++;
	gc_wd_lm_miss = 0;
	gc_wd_lm_binds = 0;
	gc_wd_chunk_lm = 0;
	/* First ~90 frames: pull a denser visible set so CapFaces listen does not
	 * open on a starved alley; after warmup keep 512 KiB/frame lazy stream. */
	budget = ( gc_wd_frame <= 90 ) ? GC_WD_WARMUP_BUDGET : GC_WD_LOAD_BUDGET;
	/* CapFaces wall-aim normally seeds vieworigin; World DL skips CapFaces so
	 * early frames can sit at (0,0,0) / solid → cluster=-1 → all-vis. Prefer a
	 * non-degenerate eye, then fall back to world AABB center from mins/maxs. */
	if(( !org[0] && !org[1] && !org[2] ) || !isfinite( org[0] ))
	{
		model_t *world = WORLDMODEL;

		if( world )
		{
			org_local[0] = 0.5f * ( world->mins[0] + world->maxs[0] );
			org_local[1] = 0.5f * ( world->mins[1] + world->maxs[1] );
			org_local[2] = 0.5f * ( world->mins[2] + world->maxs[2] ) + 64.0f;
			org = org_local;
		}
	}
	cluster = R_GXWorldDLEyeCluster( org );
	if( cluster < 0 && WORLDMODEL )
	{
		/* Retry from AABB center when the live eye is in solid. */
		model_t *world = WORLDMODEL;

		org_local[0] = 0.5f * ( world->mins[0] + world->maxs[0] );
		org_local[1] = 0.5f * ( world->mins[1] + world->maxs[1] );
		org_local[2] = 0.5f * ( world->mins[2] + world->maxs[2] ) + 64.0f;
		cluster = R_GXWorldDLEyeCluster( org_local );
		if( cluster >= 0 )
			org = org_local;
	}
	if( !logged_eye )
	{
		logged_eye = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: world DL eye=(%.0f,%.0f,%.0f) cluster=%d\n",
			org[0], org[1], org[2], cluster );
	}
	if( cluster >= 0 && cluster != gc_wd_pvs_cluster )
		R_GXWorldDLLoadPVS( cluster );
	else if( gc_wd_pvs_cluster == -2 )
		R_GXWorldDLLoadPVS( -1 );
	R_GXWorldDLFrustum( normals );

	GX_SetCullMode( GX_CULL_NONE );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_INDEX16 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_POS, GX_POS_XYZ, GX_S16, 0 );
	if( gc_wd_hdr.version == 2 )
	{
		/* Lost Coast bake: POS index, NRM s8 frac 6, CLR0 RGBA8,
		 * TEX0 s16 frac 9, TEX1 lightmap u8 frac 8. 15 bytes. */
		GX_SetVtxDesc( GX_VA_NRM, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
		GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 6 );
		GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
		GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX0, GX_TEX_ST, GX_S16, GC_WD_ST_FRAC );
		GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX1, GX_TEX_ST, GX_U8, 8 );
	}
	else
	{
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
		GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_CLR0, GX_CLR_RGB, GX_RGB565, 0 );
		GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX0, GX_TEX_ST, GX_S16, GC_WD_ST_FRAC );
	}
	R_GXWorldDLSetupLit( 0 );
	R_GXFogBegin();
	R_GXWorldDLDrawSkyline();
	gc_wd_chunk_lm = 0;
	R_GXWorldDLSetupLit( 0 );

	/* Full world first: draw every PVS-visible chunk. Frustum cull is a later
	 * speed knob (ava-play99-60 sat at 20 chunks with waiting=0 while the
	 * alley still looked starved). Lazy-load still gates on budget/cache. */
	{
		/* Chunks outside the camera go. -gcworlddlfull draws the whole PVS. */
		const qboolean frustum_cull = !gEngfuncs.Sys_CheckParm( "-gcworlddlfull" );
		int pvs_hits = 0, frustum_drop = 0, tower_chunks = 0, forced_n = 0;
		static u16 order[GC_WD_MAX_CHUNKS];
		int norder = 0;
		int oi, pass;
		u32 tower_budget = 288u * 1024u;
		u32 tower_grant, shore_budget;

		for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
		{
			const u16 cl = gc_wd_chunk_cluster[i];

			if( !( gc_wd_pvs[cl >> 3] & ( 1 << ( cl & 7 ))))
				continue;
			pvs_hits++;
			if( frustum_cull && !R_GXWorldDLBoxVisible( i, normals, org ))
			{
				frustum_drop++;
				continue;
			}
			if( norder < GC_WD_MAX_CHUNKS )
				order[norder++] = (u16)i;
		}
		/* Spend the cache on the ground in front of the camera. Index order
		 * filled it with whatever chunk id came first and left the shore out. */
		gc_wd_sort_org = org;
		qsort( order, (size_t)norder, sizeof( order[0] ), R_GXWorldDLChunkCmp );
		/* Shore first so cliffs/boats lock cache slots; towers take the
		 * reserved leftover (+ Force). Towers-first starved the cliff plates
		 * when IsTower matched ~12 harbor chunks (gaps on the right wall). */
		if( tower_budget > budget / 3u )
			tower_budget = budget / 3u;
		tower_grant = tower_budget;
		shore_budget = budget - tower_grant;

		for( pass = 0; pass < 2; pass++ )
		{
		u32 *bud;
		const qboolean want_tower = ( pass == 1 );

		if( pass == 1 )
			tower_budget = tower_grant + ( shore_budget ); /* unused shore bytes */
		bud = want_tower ? &tower_budget : &shore_budget;

		for( oi = 0; oi < norder; oi++ )
		{
			const gc_wd_batch_t *b;
			byte *chunk;
			u32 nb, k;
			qboolean forced = false;

			i = order[oi];
			if( R_GXWorldDLIsTower( i ) != want_tower )
				continue;
			chunk = R_GXWorldDLResident( i, bud );
			if( !chunk )
			{
				chunk = R_GXWorldDLForce( i );
				forced = chunk != NULL;
			}
			if( !chunk )
			{
				waiting++;
				continue;
			}
			if( !forced )
				gc_wd_slots[i].used_frame = gc_wd_frame;
			nb = *(const u16 *)( chunk + 2 );
			GX_SetArray( GX_VA_POS, chunk + *(const u32 *)( chunk + 4 ), 6 );
			/* Version 2 header is 16 bytes: npos, nbatches, pos_ofs, lm_ofs. */
			{
				u32 lm_ofs = 0;
				byte *lm_page = NULL;
				const int prev_lm = gc_wd_chunk_lm;

				gc_wd_chunk_lm = 0;
				/* v2 header: u16 npos, u16 nbatches, u32 pos_ofs, u32 lm_ofs
				 * (page index + 1) at +8; bytes 12–15 are pad before batches. */
				if( gc_wd_hdr.version == 2 )
					lm_ofs = *(const u32 *)( chunk + 8 );
				if( lm_ofs && gc_wd_lm_stream )
				{
					lm_page = R_GXWorldDLLmPage( lm_ofs - 1u );
					if( lm_page )
					{
						GX_InitTexObj( &gc_wd_lm_obj, lm_page, 32, 32, GX_TF_RGB565,
							GX_CLAMP, GX_CLAMP, GX_FALSE );
						GX_InitTexObjFilterMode( &gc_wd_lm_obj, GX_LINEAR, GX_LINEAR );
						GX_LoadTexObj( &gc_wd_lm_obj, GX_TEXMAP2 );
						gc_wd_chunk_lm = 1;
						gc_wd_lm_binds++;
					}
				}
				/* Lit TEV changes with the page — rebind materials next batch. */
				if( prev_lm != gc_wd_chunk_lm )
					cur_mat = -1;
			}
			b = (const gc_wd_batch_t *)( chunk + ( gc_wd_hdr.version == 2 ? 16 : 8 ));
			for( k = 0; k < nb; k++, b++ )
			{
				const gc_wd_mat_t *m;

				if( b->mat >= gc_wd_hdr.n_mats )
					continue;
				m = &gc_wd_mats[b->mat];
				if( (int)b->mat != cur_mat )
				{
					if( gc_wd_texnum[b->mat] < 0 )
						gc_wd_texnum[b->mat] = (s16)GC_WorldTexnumByHash( m->tex_hash );
					/* Do not permanently cache a failed bind — tip-lean may stub
					 * before the WT pack finishes streaming into the pool. */
					if( gc_wd_texnum[b->mat] <= 0 )
					{
						static int miss_logged;
						int again = GC_WorldTexnumByHash( m->tex_hash );

						if( again > 0 )
							gc_wd_texnum[b->mat] = (s16)again;
						else
						{
							bind_miss++;
							if( miss_logged < 8 )
							{
								miss_logged++;
								gEngfuncs.Con_Reportf(
									"Xash3D GameCube: world DL mat bind miss mat=%u hash=0x%08x\n",
									(unsigned)b->mat, (unsigned)m->tex_hash );
							}
							continue;
						}
					}
					if( !R_GXWorldDLBindTex( (unsigned)gc_wd_texnum[b->mat] ))
						continue;
					R_GXTevMatAppend( R_GXTevMatFlags( m->tevmat ) ? m->tevmat : 0, 1, 1 );
					cur_mat = (int)b->mat;
				}
				GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
				/* Spec fill only on wet sand (IndTex) and harbor towers.
				 * Detail/normal mats on cliffs used to SpecOn too and blew
				 * the shore into a white sheet (cliff bright_frac 0.45 vs
				 * ref 0.17) while stacks still read as prop slivers. */
				if( gc_wd_hdr.version == 2
					&& ( ( m->flags & GC_WD_MAT_ENVMAP )
						|| R_GXWorldDLIsTower( i ) ))
				{
					const int env = ( m->flags & GC_WD_MAT_ENVMAP ) != 0;
					/* Towers: warm lift for glow, short of a white sheet. */
					u16 spec_s = env ? m->pad : 140;

					R_GXWorldDLSpecOn( spec_s, env );
					GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
					R_GXWorldDLSpecOff();
					R_GXTevMatAppend( R_GXTevMatFlags( m->tevmat ) ? m->tevmat : 0, 1, 1 );
				}
				drawn++;
			}
			chunks++;
			if( want_tower )
				tower_chunks++;
			if( forced )
			{
				forced_n++;
				GX_DrawDone();
			}
		}
		}
		if( !logged || ( gc_wd_frame & 255 ) == 0 )
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: world DL pvs=%d frustum_drop=%d cull=%d towers=%d forced=%d\n",
				pvs_hits, frustum_drop, frustum_cull ? 1 : 0, tower_chunks, forced_n );
	}
	R_GXFogEnd();
	R_GXTevMatEndAlphaTest();
	GX_SetNumTevStages( 1 );
	GX_SetNumTexGens( 1 );
	/* Opaque world DLs must not leave alpha-test armed for studio/HUD. */
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetZCompLoc( GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );

	/* GX-native shot marks — sticky pool (WorldDL has no live msurfaces). */
	{
		extern int GC_WorldDLShotMarkFramesLeft( void );
		extern void GC_WorldDLShotMarkTick( void );
		extern int GC_WorldDLStickyShotMarkCount( void );
		extern qboolean GC_WorldDLStickyShotMark( int i, float *pos, float *normal );
		const int sticky_n = GC_WorldDLStickyShotMarkCount();
		const int pulse = GC_WorldDLShotMarkFramesLeft();

		if( drawn > 0 && org && sticky_n > 0 )
		{
			static qboolean mark_logged;
			int mi;
			vec3_t mark, nrm, right, up, p0, p1, p2, p3;
			const float s = 6.0f;

			GX_SetCullMode( GX_CULL_NONE );
			GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
			GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
			GX_SetNumChans( 1 );
			GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
				GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
			GX_SetNumTexGens( 0 );
			GX_SetNumTevStages( 1 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
			GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
			GX_ClearVtxDesc();
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
			GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );

			for( mi = 0; mi < sticky_n; mi++ )
			{
				if( !GC_WorldDLStickyShotMark( mi, mark, nrm ))
					continue;
				if( fabsf( nrm[2] ) < 0.9f )
				{
					right[0] = nrm[1];
					right[1] = -nrm[0];
					right[2] = 0.0f;
				}
				else
				{
					right[0] = 1.0f;
					right[1] = 0.0f;
					right[2] = 0.0f;
				}
				VectorNormalize( right );
				CrossProduct( nrm, right, up );
				VectorNormalize( up );
				VectorScale( right, s, right );
				VectorScale( up, s, up );
				VectorMA( mark, 0.5f, nrm, mark );
				VectorSubtract( mark, right, p0 ); VectorAdd( p0, up, p0 );
				VectorAdd( mark, right, p1 ); VectorAdd( p1, up, p1 );
				VectorAdd( mark, right, p2 ); VectorSubtract( p2, up, p2 );
				VectorSubtract( mark, right, p3 ); VectorSubtract( p3, up, p3 );
				GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
				GX_Position3f32( p0[0], p0[1], p0[2] ); GX_Color4u8( 28, 22, 16, 210 );
				GX_Position3f32( p1[0], p1[1], p1[2] ); GX_Color4u8( 28, 22, 16, 210 );
				GX_Position3f32( p2[0], p2[1], p2[2] ); GX_Color4u8( 28, 22, 16, 210 );
				GX_Position3f32( p3[0], p3[1], p3[2] ); GX_Color4u8( 28, 22, 16, 210 );
				GX_End();
			}
			/* Restore Flipper state — leaving LEQUAL/no-ZWrite + PASSCLR
			 * punched holes through the viewmodel (ava-play99-104). */
			GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
			GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
			GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
			GX_SetCullMode( GX_CULL_BACK );
			GX_SetNumTexGens( 1 );
			GX_SetNumTevStages( 1 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
			GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
			if( !mark_logged )
			{
				mark_logged = true;
				if( GC_WorldDLStickyShotMark( 0, mark, nrm ))
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: world DL GX shot mark at (%.0f,%.0f,%.0f) sticky=%d\n",
						mark[0], mark[1], mark[2], sticky_n );
			}
			if( pulse > 0 )
				GC_WorldDLShotMarkTick();
		}
	}

	if( !logged || ( gc_wd_frame & 255 ) == 0 )
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: world DL frame=%d cluster=%d chunks=%d batches=%d waiting=%d bind_miss=%d lm_bind=%d lm_miss=%d\n",
			gc_wd_frame, cluster, chunks, drawn, waiting, bind_miss,
			gc_wd_lm_binds, gc_wd_lm_miss );
	}
	return drawn;
}

/* Sample a streamed WorldDL lightmap under a world point. Uses the eye
 * cluster already resolved for this frame (no BSP ARAM walk) and prefers
 * chunks already resident so props share the world LM stream. */
qboolean R_GXWorldDLSampleLight( const float *org, byte rgb[3] )
{
	int cluster, i, found = -1, x, y, tile, lx, ly;
	u32 lm_ofs = 0;
	byte hdr[32];
	byte *page;
	byte *resident;
	u16 c;
	float u, v, denom, best_d2 = 1.0e30f;

	if( !org || !rgb || !R_GXWorldDLActive() || !gc_wd_lm_stream
		|| !gc_wd_chunk_cluster || !gc_wd_chunk_ofs || !gc_wd_slots )
		return false;
	cluster = gc_wd_pvs_cluster;
	if( cluster < 0 )
		return false;
	for( i = 0; i < (int)gc_wd_hdr.n_chunks; i++ )
	{
		const s16 *imn;
		const s16 *imx;
		float cx, cy, dx, dy, d2;

		if( gc_wd_chunk_cluster[i] != (u16)cluster )
			continue;
		imn = &gc_wd_chunk_mins[i * 3];
		imx = &gc_wd_chunk_maxs[i * 3];
		if( org[0] >= (float)imn[0] - 64.0f && org[0] <= (float)imx[0] + 64.0f
			&& org[1] >= (float)imn[1] - 64.0f && org[1] <= (float)imx[1] + 64.0f )
		{
			found = i;
			break;
		}
		cx = 0.5f * ( (float)imn[0] + (float)imx[0] );
		cy = 0.5f * ( (float)imn[1] + (float)imx[1] );
		dx = org[0] - cx;
		dy = org[1] - cy;
		d2 = dx * dx + dy * dy;
		if( d2 < best_d2 )
		{
			best_d2 = d2;
			found = i;
		}
	}
	if( found < 0 )
		return false;
	resident = ( gc_wd_slots[found].cache_ofs >= 0 )
		? ( gc_wd_cache + gc_wd_slots[found].cache_ofs ) : NULL;
	if( resident )
		lm_ofs = ( gc_wd_hdr.version == 2 ) ? *(const u32 *)( resident + 8 ) : 0;
	else
	{
		if( !R_GXWorldDLFetch( gc_wd_hdr.ofs_chunks + gc_wd_chunk_ofs[found],
			hdr, sizeof( hdr )))
			return false;
		if( gc_wd_hdr.version == 2 )
			lm_ofs = *(const u32 *)( hdr + 8 );
	}
	if( !lm_ofs )
		return false;
	page = R_GXWorldDLLmPage( lm_ofs - 1u );
	if( !page )
		return false;
	{
		const s16 *imn = &gc_wd_chunk_mins[found * 3];
		const s16 *imx = &gc_wd_chunk_maxs[found * 3];

		denom = (float)imx[0] - (float)imn[0];
		u = denom > 1.0f ? ( org[0] - (float)imn[0] ) / denom : 0.5f;
		denom = (float)imx[1] - (float)imn[1];
		v = denom > 1.0f ? ( org[1] - (float)imn[1] ) / denom : 0.5f;
	}
	if( u < 0.0f ) u = 0.0f;
	else if( u > 1.0f ) u = 1.0f;
	if( v < 0.0f ) v = 0.0f;
	else if( v > 1.0f ) v = 1.0f;
	x = (int)( u * 31.0f + 0.5f );
	y = (int)( v * 31.0f + 0.5f );
	if( x < 0 ) x = 0;
	else if( x > 31 ) x = 31;
	if( y < 0 ) y = 0;
	else if( y > 31 ) y = 31;
	tile = ( y >> 2 ) * 8 + ( x >> 2 );
	lx = x & 3;
	ly = y & 3;
	c = *(const u16 *)( page + (u32)tile * 32u + (u32)( ly * 4 + lx ) * 2u );
	rgb[0] = (byte)(( ( c >> 11 ) & 31 ) * 255 / 31 );
	rgb[1] = (byte)(( ( c >> 5 ) & 63 ) * 255 / 63 );
	rgb[2] = (byte)(( c & 31 ) * 255 / 31 );
	return true;
}

#endif /* XASH_GAMECUBE */
