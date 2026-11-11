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
    in the current or previous frame is never evicted — the GP may still be
    reading its display list.

Per frame: eye → cluster (BSP walk) → PVS row (re-read only when the cluster
changes) → frustum test per chunk box → GX_CallDispList per material batch.

Vertex format GX_VTXFMT6: POS index16 → s16 xyz, CLR0 RGB565 (baked light),
TEX0 s16 ST, 9 fraction bits. TEV: texture x vertex light x2, then the
material's TEV recipe (r_gx_tevmat.c).

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
#define GC_WD_LOAD_BUDGET	( 512u * 1024u ) /* chunk bytes fetched per frame */
#define GC_WD_MAT_F_WATER	0x01

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
extern qboolean R_GXWorldDLBindTexPair( unsigned tex1, unsigned tex2 );
extern int GC_WorldTexnumByHash( unsigned hash );
extern int R_GXTevMatFlags( int id );
extern void R_GXTevMatAppend( int id, int first_stage, int first_texgen );
extern void R_GXTevMatEndAlphaTest( void );
extern void R_GXFogBegin( void );
extern void R_GXFogEnd( void );

static int gc_wd_state;	/* 0 untried, 1 ready, -1 absent */
static s16 *gc_wd_texnum2;	/* WVT layer-2 texnum per material */

/* Single layer: stage0 = texture x baked light x2. */
static qboolean R_GXWorldDLBind( unsigned texnum )
{
	return R_GXWorldDLBindTex( texnum );
}

/* Per-material TEV: one stage normally; WorldVertexTransition adds a lerp
 * of layer 1 and layer 2 by the vertex alpha (RASA), then the baked light. */
static void R_GXWorldDLSetupTev( int blend )
{
	GX_SetNumIndStages( 0 );
	if( !blend )
	{
		GX_SetNumTexGens( 1 );
		GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
		GX_SetNumTevStages( 1 );
		GX_SetTevDirect( GX_TEVSTAGE0 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
		GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
		GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV ); /* baked light is already display-referred */
		GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
		GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
		return;
	}
	GX_SetNumTexGens( 2 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetNumTevStages( 3 );
	/* s0: layer 1 */
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
	GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC );
	GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO );
	GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	/* s1: lerp(layer1, layer2, vertex alpha) */
	GX_SetTevDirect( GX_TEVSTAGE1 );
	GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR0A0 );
	GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_TEXC, GX_CC_RASA );
	GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
	GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	/* s2: × baked light x2 */
	GX_SetTevDirect( GX_TEVSTAGE2 );
	GX_SetTevOrder( GX_TEVSTAGE2, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevColorIn( GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC, GX_CC_CPREV );
	GX_SetTevColorOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV ); /* baked light is already display-referred */
	GX_SetTevAlphaIn( GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
	GX_SetTevAlphaOp( GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
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
		|| gc_wd_hdr.magic != GC_WD_MAGIC || gc_wd_hdr.version != 1
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

/* Make chunk i resident. Returns its base, or NULL when it must wait. */
static byte *R_GXWorldDLResident( u32 i, u32 *budget )
{
	const gc_wd_chunkdir_t *d = &gc_wd_dir[i];
	const u32 bytes = ( d->bytes + 31u ) & ~31u;
	u32 head, k;

	if( gc_wd_slots[i].cache_ofs >= 0 )
		return gc_wd_cache + gc_wd_slots[i].cache_ofs;
	if( bytes > *budget || bytes > gc_wd_cache_size )
		return NULL;
	head = gc_wd_cache_head;
	if( head + bytes > gc_wd_cache_size )
		head = 0;
	/* Evict what the new chunk overlaps — unless the GP may still read it. */
	for( k = 0; k < gc_wd_hdr.n_chunks; k++ )
	{
		const s32 o = gc_wd_slots[k].cache_ofs;
		u32 kb;

		if( o < 0 )
			continue;
		kb = ( gc_wd_dir[k].bytes + 31u ) & ~31u;
		if( (u32)o + kb <= head || (u32)o >= head + bytes )
			continue;
		if( gc_wd_slots[k].used_frame >= gc_wd_frame - 1 )
			return NULL;
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
	if( !R_GXWorldDLFetch( gc_wd_hdr.ofs_chunks + d->ofs, gc_wd_cache + head, bytes ))
		return NULL;
	gc_wd_slots[i].cache_ofs = (s32)head;
	gc_wd_cache_head = head + bytes;
	*budget -= bytes;
	return gc_wd_cache + head;
}

/* Same frustum as CapFaces densify (FOV+5°, −16u plane slack). */
extern void R_GXBuildFrustum( void );
extern qboolean R_GXBoxOutsideFrustum( const vec3_t mins, const vec3_t maxs );

/*
 * Draw the PVS-visible world. The caller has cleared colour/Z and loaded the
 * world projection + modelview (R_GXSetupWorld3DState). Returns batches drawn.
 */
int R_GXWorldDLDraw( void )
{
	static qboolean logged;
	const float *org = RI.rvp.vieworigin;
	u32 budget = GC_WD_LOAD_BUDGET;
	u32 i;
	int cluster, drawn = 0, chunks = 0, waiting = 0, cur_mat = -1;
	int pvs_hit = 0, frustum_reject = 0;
	int pass, use_frustum = 1;
	const qboolean dump_still = gEngfuncs.Sys_CheckParm( "-gcdumpframes" )
		&& !gEngfuncs.Sys_CheckParm( "-gcvst" );
	const u32 frame_budget = dump_still ? ( 2u * 1024u * 1024u ) : GC_WD_LOAD_BUDGET;

	if( gc_wd_state != 1 )
		return 0;
	gc_wd_frame++;
	budget = frame_budget;
	cluster = R_GXWorldDLEyeCluster( org );
	/* Eye in solid / outside the tree: keep the last good row. */
	if( cluster >= 0 && cluster != gc_wd_pvs_cluster )
		R_GXWorldDLLoadPVS( cluster );
	else if( gc_wd_pvs_cluster == -2 )
		R_GXWorldDLLoadPVS( -1 );
	R_GXBuildFrustum();
	/* Dump-eye: keep CapFaces frustum (+5°). Wider FOV pulled midband land
	 * through the water slab (mae 47→64). PVS-only floods the 512 KiB cache. */

	/* Pipe: indexed s16 positions, baked RGB565 light, s16 ST. */
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_INDEX16 );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_POS, GX_POS_XYZ, GX_S16, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX0, GX_TEX_ST, GX_S16, GC_WD_ST_FRAC );
	GX_SetNumIndStages( 0 );
	GX_SetNumTexGens( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetNumTevStages( 1 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	/* texture x baked light x2 (Source lightmaps are overbright). */
	GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
	GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV ); /* baked light is already display-referred */
	GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
	GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
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
			budget = frame_budget;
		}
		for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
		{
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
			GX_SetArray( GX_VA_POS, chunk + *(const u32 *)( chunk + 4 ), 6 );
			b = (const gc_wd_batch_t *)( chunk + 8 );
			for( k = 0; k < nb; k++, b++ )
			{
				if( b->mat >= gc_wd_hdr.n_mats )
					continue;
				if( (int)b->mat != cur_mat )
				{
					const gc_wd_mat_t *m = &gc_wd_mats[b->mat];
					const int blend = m->tex2_hash != 0;
					int ok;

					if( gc_wd_texnum[b->mat] < 0 )
						gc_wd_texnum[b->mat] = (s16)GC_WorldTexnumByHash( m->tex_hash );
					if( gc_wd_texnum[b->mat] <= 0 )
						continue;
					if( blend )
					{
						if( gc_wd_texnum2[b->mat] < 0 )
							gc_wd_texnum2[b->mat] = (s16)GC_WorldTexnumByHash( m->tex2_hash );
						ok = gc_wd_texnum2[b->mat] > 0 && R_GXWorldDLBindTexPair(
							(unsigned)gc_wd_texnum[b->mat], (unsigned)gc_wd_texnum2[b->mat] );
					}
					else
						ok = R_GXWorldDLBind( (unsigned)gc_wd_texnum[b->mat] );
					if( !ok )
						continue;
					R_GXWorldDLSetupTev( blend );
					/* Source material recipe on top (detail / alphatest). */
					R_GXTevMatAppend( R_GXTevMatFlags( m->tevmat ) ? m->tevmat : 0, blend ? 3 : 1,
						blend ? 2 : 1 );
					cur_mat = (int)b->mat;
				}
				GX_CallDispList( chunk + b->dl_ofs, b->dl_bytes );
				drawn++;
			}
			chunks++;
		}
	}
	R_GXFogEnd();
	R_GXTevMatEndAlphaTest();
	GX_SetNumTevStages( 1 );
	GX_SetNumTexGens( 1 );

	gc_wd_last_waiting = waiting;
	gc_wd_last_chunks = chunks;
	if( !logged || ( gc_wd_frame & 255 ) == 0 || ( chunks == 0 && pvs_hit > 0 ))
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: world DL frame=%d cluster=%d chunks=%d batches=%d waiting=%d pvs=%d frustum_rej=%d%s\n",
			gc_wd_frame, cluster, chunks, drawn, waiting, pvs_hit, frustum_reject,
			use_frustum ? "" : " (frustum bypass)" );
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
