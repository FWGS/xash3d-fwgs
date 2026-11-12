/*
r_gx_worlddl.c - full Source BSP world as baked GX display lists + real PVS

Replaces the lean CapFaces subset for Lost Coast. The host bake
(lostcoast-gc tools/bake_worlddl.py) emits media/wd_<map>.gcd: every drawable
world face and displacement as GX display lists grouped by (PVS cluster,
material), the BSP node tree for point-in-leaf, and the Source RLE PVS rows.

Memory (MEM1 is full, so nothing here is new BSS beyond small tables):
  - tree + material table + chunk directory (~116 KiB): world array spill
  - PVS rows + display-list chunks (~1.8 MiB): ARAM, uploaded once at load
    (falls back to reading the disc file on demand when ARAM has no room)
  - chunk cache: the rest of the world array spill, FIFO ring. A chunk drawn
    earlier this frame stays put (the sky pass already queued it). Last
    frame's chunks are free once GX_DrawDone has retired that frame, so a
    new eye can replace the coast with the ground under the camera.

Per frame: eye → cluster (BSP walk) → PVS row (re-read only when the cluster
changes) → frustum test per chunk box → GX_CallDispList per material batch.

Vertex format GX_VTXFMT6 (file version 2), GX attribute order:
POS index16 → s16 xyz, NRM s16 xyz (frac 14), CLR0 RGBA8 (baked light +
blend alpha), TEX0 s16 ST (frac 9), TEX1 s16 lightmap ST (frac 8).
TEV: texture x vertex light, or x the chunk's cached lightmap page. A page
is copied out of ARAM the first time it is drawn and kept at full
32×32. Materials with an envmap get a second additive specular pass. Then
the material's TEV recipe (r_gx_tevmat.c).

-gcnobspdl disables the path (CapFaces fallback).
*/

#include "r_local.h"

#if XASH_GAMECUBE

#include <gccore.h>
#include <ogc/gx.h>
#include <string.h>
#include <math.h>
#include "../../engine/platform/gamecube/mem_gamecube.h"

#define GC_WD_MAGIC		0x47437764u /* 'GCwd' */
#define GC_WD_MAX_MATS		384
#define GC_WD_MAX_CHUNKS	3072
#define GC_WD_ST_FRAC		9
#define GC_WD_LOAD_BUDGET	( 768u * 1024u ) /* chunk bytes fetched per frame */
#define GC_WD_MAT_F_WATER	0x01
#define GC_WD_MAT_F_ENVMAP	0x04

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
	u32	tex2_hash;	/* WorldVertexTransition $basetexture2, 0 = none */
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

/* Per-chunk cache state (parallel to the directory). */
typedef struct
{
	s32	cache_ofs;	/* -1 = not resident */
	s32	used_frame;
} gc_wd_slot_t;

extern void *Mod_GCWorldArraySpillTryAlloc( size_t bytes, const char *what );
extern void Mod_GCWorldArraySpillPinBase( const void *ptr, size_t bytes );
extern size_t Mod_GCWorldArraySpillUsed( void );
extern int GC_WorldTexnumByHash( unsigned hash );
extern qboolean R_GXWorldDLBindTex( unsigned texnum );
extern void R_GXHotSetBias( int bias );
extern qboolean R_GXWorldDLBindTexPair( unsigned tex1, unsigned tex2 );
extern int GC_WorldTexnumByHash( unsigned hash );
extern int R_GXTevMatFlags( int id );
extern void R_GXTevMatAppend( int id, int first_stage, int first_texgen );
extern void R_GXTevMatEndAlphaTest( void );
extern void R_GXFogBegin( void );
extern void R_GXFogEnd( void );
extern void R_GXLoadWorldNormalMtx( void );

static int gc_wd_state;	/* 0 untried, 1 ready, -1 absent */

/* 3D skybox (Source sky_camera), from the "SKY1" block after the header. */
static struct
{
	qboolean	valid;
	qboolean	pvs_ready;
	float		origin[3];
	float		scale;
	int		cluster;
	u8		fog_rgb[3];
	u8		fog_on;
	float		fog_start, fog_end;
} gc_wd_sky;
static byte gc_wd_sky_pvs[512];	/* PVS row of the sky camera's cluster */
static s16 *gc_wd_texnum2;	/* WVT layer-2 texnum per material */

/* Single layer: stage0 = texture x baked light x2. */
static qboolean R_GXWorldDLBind( unsigned texnum )
{
	return R_GXWorldDLBindTex( texnum );
}

/* Display-list attributes, in GX's fixed order. */
static void R_GXWorldDLBindVtx( void )
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_INDEX16 );
	GX_SetVtxDesc( GX_VA_NRM, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_POS, GX_POS_XYZ, GX_S16, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 6 ); /* 3 bytes: cache is scarce */
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX0, GX_TEX_ST, GX_S16, GC_WD_ST_FRAC );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX1, GX_TEX_ST, GX_U8, 8 );
}

static void R_GXWorldDLLightMul( int stage, int lightmap, int texcoord )
{
	GX_SetTevDirect( GX_TEVSTAGE0 + stage );
	if( lightmap )
	{
		GX_SetTevOrder( GX_TEVSTAGE0 + stage, GX_TEXCOORD0 + texcoord, GX_TEXMAP2, GX_COLORNULL );
		GX_SetTevColorIn( GX_TEVSTAGE0 + stage, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_ZERO );
	}
	else
	{
		GX_SetTevOrder( GX_TEVSTAGE0 + stage, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
		GX_SetTevColorIn( GX_TEVSTAGE0 + stage, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO );
	}
	GX_SetTevColorOp( GX_TEVSTAGE0 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE0 + stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
	GX_SetTevAlphaOp( GX_TEVSTAGE0 + stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
}

/* One stage normally. WorldVertexTransition lerps layer 2 (scaled down —
 * the old stage added the blend alpha as a colour, which is the white
 * sand patches) by vertex alpha, then multiplies by baked light or by
 * the chunk's lightmap page. Returns the first free stage. */
static int R_GXWorldDLSetupTev( int blend, int lightmap )
{
	GX_SetNumIndStages( 0 );
	if( !blend )
	{
		GX_SetNumTexGens( lightmap ? 2 : 1 );
		GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
		if( lightmap )
			GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
		if( !lightmap )
		{
			GX_SetNumTevStages( 1 );
			GX_SetTevDirect( GX_TEVSTAGE0 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
			GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
			GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
			GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
			GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
			return 1;
		}
		GX_SetNumTevStages( 2 );
		GX_SetTevDirect( GX_TEVSTAGE0 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
		GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC );
		GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
		GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		R_GXWorldDLLightMul( 1, 1, 1 );
		return 2;
	}
	/* Layer 2 at 3/4 before the lerp. Full brightness plus the old
	 * "add the alpha" stage clipped the second sand albedo to white. */
	GX_SetTevKColor( GX_KCOLOR0, (GXColor){ 192, 192, 192, 255 } );
	GX_SetNumTexGens( lightmap ? 3 : 2 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	if( lightmap )
		GX_SetTexCoordGen( GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
	GX_SetNumTevStages( 4 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
	GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC );
	GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
	GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevDirect( GX_TEVSTAGE1 );
	GX_SetTevKColorSel( GX_TEVSTAGE1, GX_TEV_KCSEL_K0 );
	GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL );
	GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO );
	GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0 );
	GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
	GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevDirect( GX_TEVSTAGE2 );
	GX_SetTevOrder( GX_TEVSTAGE2, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevColorIn( GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_C0, GX_CC_RASA, GX_CC_ZERO );
	GX_SetTevColorOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
	GX_SetTevAlphaOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	R_GXWorldDLLightMul( 3, lightmap, 2 );
	return 4;
}
static char gc_wd_map[64];
static gc_wd_header_t gc_wd_hdr;
static byte *gc_wd_res;		/* file bytes [0, ofs_pvsdir) */
static size_t gc_wd_spill_mark;
static const gc_wd_mat_t *gc_wd_mats;
static const byte *gc_wd_nodes;
static const float *gc_wd_planes;
static const gc_wd_chunkdir_t *gc_wd_dir;
static gc_wd_slot_t *gc_wd_slots;
static s16 *gc_wd_texnum;	/* per material: -1 unresolved, 0 missing */
static byte *gc_wd_cache;
static u32 gc_wd_cache_size, gc_wd_cache_head;
/* Lightmap pages stay in MEM1 at full 32×32 RGB565. The disc file is
 * copied to ARAM once at load; a page is copied here the first time it
 * is drawn and kept until a different page needs the slot. 192 pages is
 * a whole view (the ride peaks around 150). 512×2 KiB for every page on
 * the map is 1 MiB, which does not fit beside the chunk cache. */
#define GC_WD_LM_SLOTS		192
#define GC_WD_LM_PAGE_BYTES	2048u
static byte gc_wd_lm_store[GC_WD_LM_SLOTS * GC_WD_LM_PAGE_BYTES] __attribute__(( aligned( 32 )));
static u32 gc_wd_lm_stream;	/* file offset of page 0 */
static s32 gc_wd_lm_slot_page[GC_WD_LM_SLOTS];
static s32 gc_wd_lm_slot_frame[GC_WD_LM_SLOTS];
static int gc_wd_lm_miss;	/* pages copied from ARAM this frame */
/* Optional second cache segment: the idle AVI frame buffer (150 KiB). The
 * cache is one virtual range [0, gc_wd_cache_size); offsets at or above
 * gc_wd_seg1_start live in the second buffer. A chunk never straddles. */
extern void *GC_AviStaticFrameBorrow( unsigned *bytes, unsigned *generation );
static byte *gc_wd_seg1;
static u32 gc_wd_seg1_start;
static unsigned gc_wd_seg1_gen;

static byte *R_GXWorldDLCachePtr( u32 ofs )
{
	if( gc_wd_seg1 && ofs >= gc_wd_seg1_start )
		return gc_wd_seg1 + ( ofs - gc_wd_seg1_start );
	return gc_wd_cache + ofs;
}
static u32 gc_wd_aram;		/* ARAM base of file bytes [ofs_pvsdir, end) */
static file_t *gc_wd_file;	/* disc fallback when ARAM is unavailable */
static byte *gc_wd_pvs;		/* decompressed row of gc_wd_pvs_cluster */
static int gc_wd_pvs_cluster = -2;
static int gc_wd_frame;
static int gc_wd_last_waiting;
static int gc_wd_last_chunks;
static byte gc_wd_scratch[1024] __attribute__(( aligned( 32 )));

/* Bytes [ofs, ofs+len) of the streamed part of the file → dst (32-aligned). */
static qboolean R_GXWorldDLFetch( u32 file_ofs, void *dst, u32 len )
{
	len = ( len + 31u ) & ~31u;
	if( gc_wd_aram )
	{
		DCInvalidateRange( dst, len );
		GC_AramTransfer( gc_wd_aram + ( file_ofs - gc_wd_hdr.ofs_pvsdir ), dst, len, false );
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
	gc_wd_res = NULL;
	gc_wd_cache = NULL;
	gc_wd_aram = 0;
	gc_wd_pvs_cluster = -2;
}

static void R_GXWorldDLLoad( const char *mapbase )
{
	char path[96];
	file_t *f;
	u32 res_bytes, stream_bytes, tables, try_sz, i;
	byte *tbl;

	gc_wd_state = -1;
	R_GXWorldDLUnload();
	if( gEngfuncs.Sys_CheckParm( "-gcnobspdl" ))
		return;
	Q_snprintf( path, sizeof( path ), "media/wd_%s.gcd", mapbase );
	f = gEngfuncs.fsapi->Open( path, "rb", false );
	if( !f )
		return;
	if( gEngfuncs.fsapi->Read( f, &gc_wd_hdr, sizeof( gc_wd_hdr )) != sizeof( gc_wd_hdr )
		|| gc_wd_hdr.magic != GC_WD_MAGIC || gc_wd_hdr.version != 2
		|| gc_wd_hdr.n_mats > GC_WD_MAX_MATS || gc_wd_hdr.n_chunks > GC_WD_MAX_CHUNKS )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL pack reject %s\n", path );
		gEngfuncs.fsapi->Close( f );
		return;
	}
	res_bytes = gc_wd_hdr.ofs_pvsdir;
	stream_bytes = gc_wd_hdr.file_size - gc_wd_hdr.ofs_pvsdir;
	tables = gc_wd_hdr.n_chunks * sizeof( gc_wd_slot_t )
		+ gc_wd_hdr.n_mats * sizeof( s16 )
		+ gc_wd_hdr.n_mats * sizeof( s16 ) /* layer-2 texnums */
		+ (( gc_wd_hdr.n_clusters + 7 ) >> 3 ) + 64;
	gc_wd_res = (byte *)Mod_GCWorldArraySpillTryAlloc( res_bytes, "world DL tree+directory" );
	tbl = (byte *)Mod_GCWorldArraySpillTryAlloc( tables, "world DL tables" );
	if( !gc_wd_res || !tbl )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL no spill for resident %u Kb\n",
			( res_bytes + tables ) / 1024u );
		gEngfuncs.fsapi->Close( f );
		return;
	}
	gEngfuncs.fsapi->Seek( f, 0, SEEK_SET );
	if( gEngfuncs.fsapi->Read( f, gc_wd_res, res_bytes ) != (fs_offset_t)res_bytes )
	{
		gEngfuncs.fsapi->Close( f );
		return;
	}
	gc_wd_mats = (const gc_wd_mat_t *)( gc_wd_res + gc_wd_hdr.ofs_mats );
	memset( &gc_wd_sky, 0, sizeof( gc_wd_sky ));
	if( gc_wd_hdr.ofs_mats >= 96 && !memcmp( gc_wd_res + 64, "SKY1", 4 )
		&& (( gc_wd_hdr.n_clusters + 7 ) >> 3 ) <= sizeof( gc_wd_sky_pvs ))
	{
		const byte *sk = gc_wd_res + 64;

		memcpy( gc_wd_sky.origin, sk + 4, 12 );
		memcpy( &gc_wd_sky.scale, sk + 16, 4 );
		memcpy( &gc_wd_sky.cluster, sk + 20, 4 );
		memcpy( gc_wd_sky.fog_rgb, sk + 24, 3 );
		gc_wd_sky.fog_on = sk[27];
		memcpy( &gc_wd_sky.fog_start, sk + 28, 4 );
		memcpy( &gc_wd_sky.fog_end, sk + 32, 4 );
		gc_wd_sky.valid = gc_wd_sky.cluster >= 0 && gc_wd_sky.scale >= 1.0f
			&& !gEngfuncs.Sys_CheckParm( "-gcno3dsky" );
	}
	gc_wd_nodes = gc_wd_res + gc_wd_hdr.ofs_nodes;
	gc_wd_planes = (const float *)( gc_wd_res + gc_wd_hdr.ofs_planes );
	gc_wd_dir = (const gc_wd_chunkdir_t *)( gc_wd_res + gc_wd_hdr.ofs_chunkdir );
	gc_wd_slots = (gc_wd_slot_t *)tbl;
	gc_wd_texnum = (s16 *)( gc_wd_slots + gc_wd_hdr.n_chunks );
	gc_wd_texnum2 = gc_wd_texnum + gc_wd_hdr.n_mats;
	gc_wd_pvs = (byte *)( gc_wd_texnum2 + gc_wd_hdr.n_mats );
	for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
	{
		gc_wd_slots[i].cache_ofs = -1;
		gc_wd_slots[i].used_frame = -1000;
	}
	for( i = 0; i < gc_wd_hdr.n_mats; i++ )
	{
		gc_wd_texnum[i] = -1;
		gc_wd_texnum2[i] = -1;
	}

	/* Chunk cache: everything the spill still has, largest first. */
	gc_wd_cache = NULL;
	for( try_sz = 1024u * 1024u; try_sz >= 192u * 1024u; try_sz -= 64u * 1024u )
	{
		gc_wd_cache = (byte *)Mod_GCWorldArraySpillTryAlloc( try_sz, "world DL chunk cache" );
		if( gc_wd_cache )
			break;
	}
	if( !gc_wd_cache || try_sz < gc_wd_hdr.max_chunk * 2u )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: world DL no spill for chunk cache\n" );
		gEngfuncs.fsapi->Close( f );
		gc_wd_res = NULL;
		return;
	}
	gc_wd_cache_size = try_sz;
	gc_wd_cache_head = 0;
	Mod_GCWorldArraySpillPinBase( gc_wd_res, (size_t)(( gc_wd_cache + try_sz ) - gc_wd_res ));
	gc_wd_spill_mark = Mod_GCWorldArraySpillUsed();

	/* Stream part → ARAM once; the cache doubles as the upload bounce. */
	gc_wd_aram = GC_AramMapReserve( stream_bytes );
	if( gc_wd_aram )
	{
		u32 done = 0;

		gEngfuncs.fsapi->Seek( f, (fs_offset_t)gc_wd_hdr.ofs_pvsdir, SEEK_SET );
		while( done < stream_bytes )
		{
			u32 n = stream_bytes - done;

			if( n > 64u * 1024u )
				n = 64u * 1024u;
			if( gEngfuncs.fsapi->Read( f, gc_wd_cache, n ) != (fs_offset_t)n )
				break;
			DCFlushRange( gc_wd_cache, ( n + 31u ) & ~31u );
			GC_AramTransfer( gc_wd_aram + done, gc_wd_cache, ( n + 31u ) & ~31u, true );
			done += n;
		}
		if( done < stream_bytes )
			gc_wd_aram = 0;
	}
	if( gc_wd_aram )
		gEngfuncs.fsapi->Close( f );
	else
		gc_wd_file = f; /* disc streaming fallback */

	/* Pages live in their own cache. The chunk cache stays whole. */
	if( gc_wd_hdr.n_chunks > 0 )
	{
		const gc_wd_chunkdir_t *last = &gc_wd_dir[gc_wd_hdr.n_chunks - 1];

		gc_wd_lm_stream = gc_wd_hdr.ofs_chunks + last->ofs + (( last->bytes + 31u ) & ~31u );
		for( i = 0; i < GC_WD_LM_SLOTS; i++ )
		{
			gc_wd_lm_slot_page[i] = -1;
			gc_wd_lm_slot_frame[i] = -1000;
		}
	}
	/* Second segment: the idle AVI frame buffer, appended to the virtual range. */
	gc_wd_seg1 = NULL;
	if( !gEngfuncs.Sys_CheckParm( "-gcwdsmallcache" ))
	{
		unsigned seg_bytes = 0;
		byte *seg = (byte *)GC_AviStaticFrameBorrow( &seg_bytes, &gc_wd_seg1_gen );

		seg_bytes &= ~31u;
		if( seg && !( (size_t)seg & 31u ) && seg_bytes >= gc_wd_hdr.max_chunk )
		{
			gc_wd_seg1 = seg;
			gc_wd_seg1_start = gc_wd_cache_size;
			gc_wd_cache_size += seg_bytes;
		}
	}
	gc_wd_pvs_cluster = -2;
	gc_wd_state = 1;
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: world DL %s clusters=%u chunks=%u mats=%u resident=%u Kb cache=%u Kb stream=%u Kb (%s)\n",
		path, gc_wd_hdr.n_clusters, gc_wd_hdr.n_chunks, gc_wd_hdr.n_mats,
		( res_bytes + tables ) / 1024u, gc_wd_cache_size / 1024u, stream_bytes / 1024u,
		gc_wd_aram ? "ARAM" : "disc" );
}

qboolean R_GXWorldDLActive( void )
{
	model_t *world = WORLDMODEL;
	char base[64];

	if( !world || !world->name[0] )
		return false;
	COM_FileBase( world->name, base, sizeof( base ));
	/* New map, or the spill was rewound under us: load again. */
	if( gc_wd_state != 0 && ( Q_stricmp( base, gc_wd_map )
		|| ( gc_wd_state == 1 && Mod_GCWorldArraySpillUsed() < gc_wd_spill_mark )))
		gc_wd_state = 0;
	if( gc_wd_state == 0 )
	{
		Q_strncpy( gc_wd_map, base, sizeof( gc_wd_map ));
		R_GXWorldDLLoad( base );
	}
	return gc_wd_state == 1;
}

static int R_GXWorldDLEyeCluster( const float *org )
{
	int idx = 0;

	while( idx >= 0 )
	{
		const byte *n = gc_wd_nodes + idx * 6;
		const float *p = gc_wd_planes + ( *(const u16 *)n ) * 4;
		const float d = p[0] * org[0] + p[1] * org[1] + p[2] * org[2] - p[3];

		idx = ( d >= 0.0f ) ? *(const s16 *)( n + 2 ) : *(const s16 *)( n + 4 );
	}
	return idx == -1 ? -1 : -idx - 2;
}

static void R_GXWorldDLLoadPVS( int cluster )
{
	const u32 visbytes = ( gc_wd_hdr.n_clusters + 7 ) >> 3;
	u32 dir_ofs, row, out = 0, in, base;

	gc_wd_pvs_cluster = cluster;
	memset( gc_wd_pvs, 0xff, visbytes ); /* no row: everything visible */
	if( cluster < 0 )
		return;
	dir_ofs = gc_wd_hdr.ofs_pvsdir + (u32)cluster * 4u;
	base = dir_ofs & ~31u;
	if( !R_GXWorldDLFetch( base, gc_wd_scratch, 32 ))
		return;
	row = *(const u32 *)( gc_wd_scratch + ( dir_ofs - base ));
	if( row == 0xffffffffu )
		return;
	/* RLE row: non-zero byte = literal, zero byte + run length = zeros. */
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
			/* Refill the window at the current input position. */
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
			u32 run = gc_wd_scratch[in + 1];

			if( run > visbytes - out )
				run = visbytes - out;
			out += run;
			in += 2;
		}
	}
	/* Empty decompress → treat as full-vis so a bad ARAM window cannot
	 * black out the ride (host PVS for cl=1937 has 2400+ bits set). */
	{
		u32 nz = 0, i;

		for( i = 0; i < visbytes; i++ )
			nz |= gc_wd_pvs[i];
		if( !nz )
			memset( gc_wd_pvs, 0xff, visbytes );
	}
}

/*
 * One copy of each page, full 32×32. A hit returns the cached bytes and
 * does not touch ARAM. A miss copies the page once into a slot the GPU
 * is done with (the frame starts with DrawDone). Slots already used this
 * frame are left alone, so nothing is streamed again until the view
 * actually needs a different page.
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
	/* Only a replaced slot can have stale texels. The first fill of a
	 * slot has never been sampled. */
	if( gc_wd_lm_slot_page[victim] >= 0 )
		GX_InvalidateTexAll();
	gc_wd_lm_slot_page[victim] = (s32)page;
	gc_wd_lm_slot_frame[victim] = gc_wd_frame;
	gc_wd_lm_miss++;
	return gc_wd_lm_store + (u32)victim * GC_WD_LM_PAGE_BYTES;
}

/* Place a chunk so it does not straddle the spill/AVI seam. */
static u32 R_GXWorldDLPlace( u32 head, u32 bytes )
{
	if( head + bytes > gc_wd_cache_size )
		head = 0;
	if( gc_wd_seg1 && head < gc_wd_seg1_start && head + bytes > gc_wd_seg1_start )
	{
		head = gc_wd_seg1_start;
		if( head + bytes > gc_wd_cache_size )
			head = 0;
	}
	return head;
}

/* Make chunk i resident. Returns its base, or NULL when it must wait.
 * Chunks already drawn this frame (including the sky pass) are pinned.
 * Everything older can be replaced: Draw() has retired the previous GP
 * frame, and refusing that replacement is what left the monastery floor
 * in the waiting set while the coast stayed resident. */
static byte *R_GXWorldDLResident( u32 i, u32 *budget )
{
	const gc_wd_chunkdir_t *d = &gc_wd_dir[i];
	const u32 bytes = ( d->bytes + 31u ) & ~31u;
	u32 head, k, spins;

	if( gc_wd_slots[i].cache_ofs >= 0 )
		return R_GXWorldDLCachePtr( (u32)gc_wd_slots[i].cache_ofs );
	if( bytes == 0 || bytes > *budget || bytes > gc_wd_cache_size )
		return NULL;
	head = gc_wd_cache_head;
	for( spins = 0; spins < 8; spins++ )
	{
		u32 block = 0;
		int pinned = 0;

		head = R_GXWorldDLPlace( head, bytes );
		if( gc_wd_seg1 && head < gc_wd_seg1_start && head + bytes > gc_wd_seg1_start )
			return NULL;
		for( k = 0; k < gc_wd_hdr.n_chunks; k++ )
		{
			const s32 o = gc_wd_slots[k].cache_ofs;
			u32 kb;

			if( o < 0 || gc_wd_slots[k].used_frame < gc_wd_frame )
				continue;
			kb = ( gc_wd_dir[k].bytes + 31u ) & ~31u;
			if( (u32)o + kb <= head || (u32)o >= head + bytes )
				continue;
			pinned = 1;
			if( (u32)o + kb > block )
				block = (u32)o + kb;
		}
		if( pinned )
		{
			/* Step past this frame's lists and try the next gap. */
			head = block > head ? block : head + 32u;
			if( head >= gc_wd_cache_size )
				head = 0;
			continue;
		}
		for( k = 0; k < gc_wd_hdr.n_chunks; k++ )
		{
			const s32 o = gc_wd_slots[k].cache_ofs;
			u32 kb;

			if( o < 0 )
				continue;
			kb = ( gc_wd_dir[k].bytes + 31u ) & ~31u;
			if( (u32)o + kb <= head || (u32)o >= head + bytes )
				continue;
			gc_wd_slots[k].cache_ofs = -1;
		}
		if( !R_GXWorldDLFetch( gc_wd_hdr.ofs_chunks + d->ofs, R_GXWorldDLCachePtr( head ), bytes ))
			return NULL;
		gc_wd_slots[i].cache_ofs = (s32)head;
		gc_wd_cache_head = head + bytes;
		*budget -= bytes;
		return R_GXWorldDLCachePtr( head );
	}
	gc_wd_cache_head = head;
	return NULL;
}

/* Same frustum as CapFaces densify (FOV+5°, −16u plane slack). */
extern void R_GXBuildFrustum( void );
extern qboolean R_GXBoxOutsideFrustum( const vec3_t mins, const vec3_t maxs );

/*
 * Draw the PVS-visible world. The caller has cleared colour/Z and loaded the
 * world projection + modelview (R_GXSetupWorld3DState). Returns batches drawn.
 */
/* Additive sun sheen on envmap materials (sand). Normals are in the display
 * list; the light is the Lost Coast env_sun (yaw 137, 10° up). */
static int R_GXWorldDLSpecular( void )
{
	vec3_t forward, right, up, sun;
	GXLightObj light;
	const float yaw = 137.0f * 3.14159265f / 180.0f;
	const float el = 10.0f * 3.14159265f / 180.0f;
	u32 i;
	int drawn = 0;

	R_GXLoadWorldNormalMtx();
	AngleVectors( RI.rvp.viewangles, forward, right, up );
	sun[0] = cosf( el ) * cosf( yaw );
	sun[1] = cosf( el ) * sinf( yaw );
	sun[2] = sinf( el );
	GX_InitLightColor( &light, (GXColor){ 253, 236, 198, 255 } );
	GX_InitSpecularDir( &light,
		DotProduct( sun, right ),
		DotProduct( sun, up ),
		-DotProduct( sun, forward ) );
	GX_InitLightShininess( &light, 8.0f );
	GX_LoadLightObj( &light, GX_LIGHT0 );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_ENABLE, GX_SRC_REG, GX_SRC_REG,
		GX_LIGHT0, GX_DF_CLAMP, GX_AF_SPEC );
	GX_SetChanAmbColor( GX_COLOR0A0, (GXColor){ 0, 0, 0, 0 } );
	GX_SetBlendMode( GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_NOOP );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetNumIndStages( 0 );
	GX_SetNumTexGens( 0 );
	GX_SetNumTevStages( 1 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	R_GXWorldDLBindVtx();

	for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
	{
		const gc_wd_batch_t *b;
		byte *chunk;
		u32 nb, k;
		s32 ofs;

		ofs = gc_wd_slots[i].cache_ofs;
		if( ofs < 0 || gc_wd_slots[i].used_frame != gc_wd_frame )
			continue;
		chunk = R_GXWorldDLCachePtr( (u32)ofs );
		nb = *(const u16 *)( chunk + 2 );
		GX_SetArray( GX_VA_POS, chunk + *(const u32 *)( chunk + 4 ), 6 );
		b = (const gc_wd_batch_t *)( chunk + 16 );
		for( k = 0; k < nb; k++, b++ )
		{
			const gc_wd_mat_t *m;
			u8 s;

			if( b->mat >= gc_wd_hdr.n_mats )
				continue;
			m = &gc_wd_mats[b->mat];
			if( !( m->flags & GC_WD_MAT_F_ENVMAP ) || ( m->flags & GC_WD_MAT_F_WATER ))
				continue;
			s = (u8)( m->pad & 0xff );
			if( s < 48 )
				s = 48;
			if( s > 96 )
				s = 96;
			GX_SetChanMatColor( GX_COLOR0A0, (GXColor){ s,
				(u8)( (unsigned)s * 9u / 10u ),
				(u8)( (unsigned)s * 7u / 10u ), 255 } );
			GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
			drawn++;
		}
	}

	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	return drawn;
}

/* Chunk indices ordered near to far (counting sort over distance bands from
 * the eye to each chunk box). */
#define GC_WD_BANDS	12
static u16 gc_wd_order[GC_WD_MAX_CHUNKS];

static int R_GXWorldDLBand( const gc_wd_chunkdir_t *d, const float *org )
{
	static const float edge[GC_WD_BANDS - 1] = {
		256.0f, 512.0f, 768.0f, 1024.0f, 1536.0f, 2048.0f, 3072.0f, 4096.0f,
		6144.0f, 8192.0f, 12288.0f
	};
	float d2 = 0.0f;
	int a, b;

	for( a = 0; a < 3; a++ )
	{
		float v = 0.0f;

		if( org[a] < (float)d->mins[a] )
			v = (float)d->mins[a] - org[a];
		else if( org[a] > (float)d->maxs[a] )
			v = org[a] - (float)d->maxs[a];
		d2 += v * v;
	}
	for( b = 0; b < GC_WD_BANDS - 1; b++ )
		if( d2 < edge[b] * edge[b] )
			return b;
	return GC_WD_BANDS - 1;
}

static void R_GXWorldDLSortByDistance( const float *org )
{
	u32 count[GC_WD_BANDS + 1];
	u32 i;
	int b;

	memset( count, 0, sizeof( count ));
	for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
		count[R_GXWorldDLBand( &gc_wd_dir[i], org ) + 1]++;
	for( b = 0; b < GC_WD_BANDS; b++ )
		count[b + 1] += count[b];
	for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
		gc_wd_order[count[R_GXWorldDLBand( &gc_wd_dir[i], org )]++] = (u16)i;
}

int R_GXWorldDLDraw( void )
{
	static qboolean logged;
	const float *org = RI.rvp.vieworigin;
	u32 budget = GC_WD_LOAD_BUDGET;
	u32 oi;
	int cluster, drawn = 0, chunks = 0, waiting = 0, cur_mat = -1, use_lm = 0;
	int pvs_hit = 0, frustum_reject = 0;
	int pass, use_frustum = 1;
	const qboolean dump_still = gEngfuncs.Sys_CheckParm( "-gcdumpframes" )
		&& !gEngfuncs.Sys_CheckParm( "-gcvst" );
	const u32 frame_budget = dump_still ? ( 2u * 1024u * 1024u ) : GC_WD_LOAD_BUDGET;

	if( gc_wd_state != 1 )
		return 0;
	/* Retire the previous frame before any cache line is reused. */
	GX_DrawDone();
	gc_wd_frame++;
	gc_wd_lm_miss = 0;
	/* A movie claimed the borrowed AVI buffer: everything cached there is gone. */
	if( gc_wd_seg1 )
	{
		unsigned gen = 0;

		(void)GC_AviStaticFrameBorrow( NULL, &gen );
		if( gen != gc_wd_seg1_gen )
		{
			u32 k;

			gc_wd_seg1_gen = gen;
			for( k = 0; k < gc_wd_hdr.n_chunks; k++ )
				if( gc_wd_slots[k].cache_ofs >= (s32)gc_wd_seg1_start )
					gc_wd_slots[k].cache_ofs = -1;
		}
	}
	budget = frame_budget;
	cluster = R_GXWorldDLEyeCluster( org );
	/* Eye in solid / outside the tree: keep the last good row. */
	if( cluster >= 0 && cluster != gc_wd_pvs_cluster )
		R_GXWorldDLLoadPVS( cluster );
	else if( gc_wd_pvs_cluster == -2 )
		R_GXWorldDLLoadPVS( -1 );
	R_GXWorldDLSortByDistance( org );
	R_GXBuildFrustum();
	/* Dump-eye: keep CapFaces frustum (+5°). Wider FOV pulled midband land
	 * through the water slab (mae 47→64). PVS-only floods the 512 KiB cache. */

	/* Pipe: indexed positions, direct normals, baked RGBA, diffuse + lightmap ST. */
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	R_GXWorldDLBindVtx();
	R_GXFogBegin();

	/* Pass 0: PVS + frustum. Pass 1 (VST only): PVS-only when frustum ate
	 * most of the PVS set (monastery park cl=1937). Dump-eye must NOT bypass
	 * — PVS-only floods the 512 KiB cache (waiting~539) and starves cove geo. */
	for( pass = 0; pass < 2; pass++ )
	{
		if( pass == 1 )
		{
			if( dump_still
				|| !use_frustum
				|| pvs_hit <= 0
				|| frustum_reject < ( pvs_hit * 3 ) / 4
				|| chunks >= 8 )
				break;
			use_frustum = 0;
			drawn = 0;
			chunks = 0;
			waiting = 0;
			cur_mat = -1;
			use_lm = 0;
			budget = frame_budget;
		}
		for( oi = 0; oi < gc_wd_hdr.n_chunks; oi++ )
		{
			/* Nearest chunks first: when the cache cannot hold the whole
			 * visible set, what is left waiting is the far distance. */
			const u32 i = gc_wd_order[oi];
			const gc_wd_chunkdir_t *d = &gc_wd_dir[i];
			const gc_wd_batch_t *b;
			byte *chunk;
			u32 nb, k;

			if( !( gc_wd_pvs[d->cluster >> 3] & ( 1 << ( d->cluster & 7 ))))
				continue;
			if( pass == 0 )
				pvs_hit++;
			/* Always keep the eye cluster's own chunk — never frustum it away. */
			if( use_frustum
				&& !( cluster >= 0 && (int)d->cluster == cluster ))
			{
				vec3_t bmins, bmaxs;

				bmins[0] = (float)d->mins[0];
				bmins[1] = (float)d->mins[1];
				bmins[2] = (float)d->mins[2];
				bmaxs[0] = (float)d->maxs[0];
				bmaxs[1] = (float)d->maxs[1];
				bmaxs[2] = (float)d->maxs[2];
				if( R_GXBoxOutsideFrustum( bmins, bmaxs ))
				{
					if( pass == 0 )
						frustum_reject++;
					continue;
				}
			}
			chunk = R_GXWorldDLResident( i, &budget );
			if( !chunk )
			{
				waiting++;
				continue;
			}
			gc_wd_slots[i].used_frame = gc_wd_frame;
			nb = *(const u16 *)( chunk + 2 );
			{
				const u32 lm_ofs = *(const u32 *)( chunk + 8 );
				static GXTexObj lm_obj;

				use_lm = 0;
				if( lm_ofs )
				{
					/* lm_ofs is page index + 1 in the separate page stream. */
					byte *page = R_GXWorldDLLmPage( lm_ofs - 1u );

					if( page )
					{
						use_lm = 1;
						GX_InitTexObj( &lm_obj, page, 32, 32,
							GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
						GX_InitTexObjFilterMode( &lm_obj, GX_LINEAR, GX_LINEAR );
						GX_LoadTexObj( &lm_obj, GX_TEXMAP2 );
					}
				}
			}
			{
				float cx = 0.5f * ( (float)d->mins[0] + (float)d->maxs[0] );
				float cy = 0.5f * ( (float)d->mins[1] + (float)d->maxs[1] );
				float cz = 0.5f * ( (float)d->mins[2] + (float)d->maxs[2] );
				float dx = cx - org[0], dy = cy - org[1], dz = cz - org[2];
				float d2 = dx * dx + dy * dy + dz * dz;
				int bias = 1;

				if( d2 < 600.0f * 600.0f )
					bias = 48;
				else if( d2 < 1200.0f * 1200.0f )
					bias = 16;
				else if( d2 < 2000.0f * 2000.0f )
					bias = 4;
				R_GXHotSetBias( bias );
			}
			GX_SetArray( GX_VA_POS, chunk + *(const u32 *)( chunk + 4 ), 6 );
			b = (const gc_wd_batch_t *)( chunk + 16 );
			for( k = 0; k < nb; k++, b++ )
			{
				int key;

				if( b->mat >= gc_wd_hdr.n_mats )
					continue;
				key = (int)b->mat | ( use_lm ? 0x4000 : 0 );
				if( key != cur_mat )
				{
					const gc_wd_mat_t *m = &gc_wd_mats[b->mat];
					int blend = m->tex2_hash != 0;
					int ok;
					int first;

					if( gc_wd_texnum[b->mat] < 0 )
						gc_wd_texnum[b->mat] = (s16)GC_WorldTexnumByHash( m->tex_hash );
					if( gc_wd_texnum[b->mat] <= 0 )
						continue;
					if( blend )
					{
						if( gc_wd_texnum2[b->mat] < 0 )
							gc_wd_texnum2[b->mat] = (s16)GC_WorldTexnumByHash( m->tex2_hash );
						/* Layer 2 is a streamed stub until HotService promotes it:
						 * never drop the batch for it — fall back to layer 1. */
						ok = gc_wd_texnum2[b->mat] > 0 && R_GXWorldDLBindTexPair(
							(unsigned)gc_wd_texnum[b->mat], (unsigned)gc_wd_texnum2[b->mat] );
						blend = ok;
						if( !ok )
							ok = R_GXWorldDLBind( (unsigned)gc_wd_texnum[b->mat] );
					}
					else
						ok = R_GXWorldDLBind( (unsigned)gc_wd_texnum[b->mat] );
					if( !ok )
						continue;
					first = R_GXWorldDLSetupTev( blend, use_lm );
					/* Source material recipe on top (detail / alphatest). */
					R_GXTevMatAppend( R_GXTevMatFlags( m->tevmat ) ? m->tevmat : 0, first,
						blend ? ( use_lm ? 3 : 2 ) : ( use_lm ? 2 : 1 ) );
					cur_mat = key;
				}
				GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
				drawn++;
			}
			chunks++;
		}
	}
	R_GXFogEnd();
	{
		int spec = R_GXWorldDLSpecular();
		static double pace_t;

		if( gc_wd_frame >= 64 && ( gc_wd_frame % 32 ) == 0 )
		{
			double now = gEngfuncs.pfnTime();

			if( pace_t > 0.0 )
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: world DL pace frame=%d dt=%.2fms chunks=%d batches=%d spec=%d\n",
					gc_wd_frame, ( now - pace_t ) * 1000.0 / 32.0,
					chunks, drawn, spec );
			pace_t = now;
		}
	}
	R_GXTevMatEndAlphaTest();
	GX_SetNumTevStages( 1 );
	GX_SetNumTexGens( 1 );

	gc_wd_last_waiting = waiting;
	gc_wd_last_chunks = chunks;
	if( !logged || ( gc_wd_frame & 63 ) == 0 || ( chunks == 0 && pvs_hit > 0 ))
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: world DL frame=%d cluster=%d chunks=%d batches=%d waiting=%d pvs=%d frustum_rej=%d lm_miss=%d%s\n",
			gc_wd_frame, cluster, chunks, drawn, waiting, pvs_hit, frustum_reject,
			gc_wd_lm_miss, use_frustum ? "" : " (frustum bypass)" );
	}
	return drawn;
}

/* Sky camera for the 3D skybox pass; false when the pack has none. */
qboolean R_GXWorldDLSkyCamera( float *origin, float *scale )
{
	if( gc_wd_state != 1 || !gc_wd_sky.valid )
		return false;
	origin[0] = gc_wd_sky.origin[0];
	origin[1] = gc_wd_sky.origin[1];
	origin[2] = gc_wd_sky.origin[2];
	*scale = gc_wd_sky.scale;
	return true;
}

/*
 * 3D skybox: the small-scale backdrop geometry around the sky_camera (far
 * towers, town, sea). The caller has loaded a projection with the given
 * near/far and a modelview of view x scale x translate(-sky origin), so the
 * same display lists draw 16x larger around the eye. Chunks come from the
 * sky camera cluster's own PVS row; the caller clears Z afterwards so the
 * real map always draws in front. Returns batches drawn.
 */
int R_GXWorldDLDrawSky( float znear, float zfar )
{
	static qboolean logged;
	u32 budget = GC_WD_LOAD_BUDGET;
	u32 i;
	int drawn = 0, chunks = 0, cur_mat = -1;

	if( gc_wd_state != 1 || !gc_wd_sky.valid )
		return 0;
	if( !gc_wd_sky.pvs_ready )
	{
		/* Borrow the row decoder once, then force the eye row to reload. */
		R_GXWorldDLLoadPVS( gc_wd_sky.cluster );
		memcpy( gc_wd_sky_pvs, gc_wd_pvs, ( gc_wd_hdr.n_clusters + 7 ) >> 3 );
		gc_wd_pvs_cluster = -2;
		gc_wd_sky.pvs_ready = true;
	}
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	R_GXWorldDLBindVtx();
	if( gc_wd_sky.fog_on && gc_wd_sky.fog_end > gc_wd_sky.fog_start )
	{
		const GXColor fog = { gc_wd_sky.fog_rgb[0], gc_wd_sky.fog_rgb[1], gc_wd_sky.fog_rgb[2], 255 };

		/* sky_camera fog distances are in main-world units = this pass's eye space. */
		GX_SetFog( GX_FOG_PERSP_LIN, gc_wd_sky.fog_start, gc_wd_sky.fog_end, znear, zfar, fog );
	}
	for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
	{
		const gc_wd_chunkdir_t *d = &gc_wd_dir[i];
		const gc_wd_batch_t *b;
		byte *chunk;
		u32 nb, k;

		if( !( gc_wd_sky_pvs[d->cluster >> 3] & ( 1 << ( d->cluster & 7 ))))
			continue;
		chunk = R_GXWorldDLResident( i, &budget );
		if( !chunk )
			continue;
		gc_wd_slots[i].used_frame = gc_wd_frame + 1; /* the main pass bumps the frame */
		nb = *(const u16 *)( chunk + 2 );
		GX_SetArray( GX_VA_POS, chunk + *(const u32 *)( chunk + 4 ), 6 );
		b = (const gc_wd_batch_t *)( chunk + 16 );
		for( k = 0; k < nb; k++, b++ )
		{
			if( b->mat >= gc_wd_hdr.n_mats )
				continue;
			if( (int)b->mat != cur_mat )
			{
				const gc_wd_mat_t *m = &gc_wd_mats[b->mat];
				int first;

				if( gc_wd_texnum[b->mat] < 0 )
					gc_wd_texnum[b->mat] = (s16)GC_WorldTexnumByHash( m->tex_hash );
				if( gc_wd_texnum[b->mat] <= 0
					|| !R_GXWorldDLBind( (unsigned)gc_wd_texnum[b->mat] ))
					continue;
				first = R_GXWorldDLSetupTev( 0, 0 );
				R_GXTevMatAppend( 0, first, 1 );
				cur_mat = (int)b->mat;
			}
			GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
			drawn++;
		}
		chunks++;
	}
	{
		const GXColor none = { 0, 0, 0, 0 };

		GX_SetFog( GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, none );
	}
	R_GXTevMatEndAlphaTest();
	GX_SetNumTevStages( 1 );
	GX_SetNumTexGens( 1 );
	if( !logged )
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: world DL 3D skybox cluster=%d scale=%.0f chunks=%d batches=%d\n",
			gc_wd_sky.cluster, gc_wd_sky.scale, chunks, drawn );
	}
	return drawn;
}

/* DumpFrames hold should wait until the visible WorldDL set is resident. */
int R_GXWorldDLLastWaiting( void )
{
	return gc_wd_last_waiting;
}

int R_GXWorldDLLastChunks( void )
{
	return gc_wd_last_chunks;
}

#endif /* XASH_GAMECUBE */
