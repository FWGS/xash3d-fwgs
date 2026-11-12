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

#if XASH_GAMECUBE

#include <gccore.h>
#include <ogc/gx.h>
#include <string.h>
#include <math.h>
#include "../../engine/platform/gamecube/mem_gamecube.h"

#define GC_WD_MAGIC		0x47437764u /* 'GCwd' */
#define GC_WD_MAX_MATS		512
#define GC_WD_MAX_CHUNKS	6144
#define GC_WD_ST_FRAC		9
/* Per-frame ARAM→spill fetch. 256 KiB left early frames sparse; 512 fills
 * the visible PVS faster while still lazy-loading distant chunks. */
#define GC_WD_LOAD_BUDGET	( 512u * 1024u )
#define GC_WD_WARMUP_BUDGET	( 1024u * 1024u )

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

static int R_GXTevMatFlags( int id ) { (void)id; return 0; }
static void R_GXTevMatAppend( int id, int a, int b ) { (void)id; (void)a; (void)b; }
static void R_GXTevMatEndAlphaTest( void ) {}
static void R_GXFogBegin( void ) {}
static void R_GXFogEnd( void ) {}

static int gc_wd_state;	/* 0 untried, 1 ready, -1 absent */
static char gc_wd_map[64];
static gc_wd_header_t gc_wd_hdr;
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
static int gc_wd_frame;
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
		|| gc_wd_hdr.magic != GC_WD_MAGIC || gc_wd_hdr.version != 1
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

	/* Material table + chunkdir compact mirror from the open file (pre-ARAM). */
	gEngfuncs.fsapi->Seek( f, (fs_offset_t)gc_wd_hdr.ofs_mats, SEEK_SET );
	gEngfuncs.fsapi->Read( f, gc_wd_mats_local, gc_wd_hdr.n_mats * sizeof( gc_wd_mat_t ));
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

	gc_wd_pvs_cluster = -2;
	gc_wd_state = 1;
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: world DL %s clusters=%u chunks=%u mats=%u cache=%u Kb file=%u Kb (%s)\n",
		path, gc_wd_hdr.n_clusters, gc_wd_hdr.n_chunks, gc_wd_hdr.n_mats,
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
		if( gc_wd_slots[k].used_frame >= gc_wd_frame - 1 )
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
	gc_wd_frame++;
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
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_POS, GX_POS_XYZ, GX_S16, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_CLR0, GX_CLR_RGB, GX_RGB565, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX0, GX_TEX_ST, GX_S16, GC_WD_ST_FRAC );
	GX_SetNumIndStages( 0 );
	GX_SetNumTexGens( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetNumTevStages( 1 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	/* SCALE_2 with a real WT pool (192 KiB) — SCALE_4 blew the open street
	 * white (ava-play99-61); earlier SCALE_2 crush was under a 16 KiB stub
	 * pool. Keep RASC*TEXC; frustum remains opt-in for speed. */
	GX_SetTevColorIn( GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO );
	GX_SetTevColorOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV );
	GX_SetTevAlphaIn( GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA );
	GX_SetTevAlphaOp( GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
	R_GXFogBegin();

	/* Full world first: draw every PVS-visible chunk. Frustum cull is a later
	 * speed knob (ava-play99-60 sat at 20 chunks with waiting=0 while the
	 * alley still looked starved). Lazy-load still gates on budget/cache. */
	{
		/* Opt-in speed: -gcworlddlfrustum. Default = full PVS (lazy budget). */
		const qboolean frustum_cull = gEngfuncs.Sys_CheckParm( "-gcworlddlfrustum" )
			&& !gEngfuncs.Sys_CheckParm( "-gcworlddlfull" );
		int pvs_hits = 0, frustum_drop = 0;

		for( i = 0; i < gc_wd_hdr.n_chunks; i++ )
		{
			const gc_wd_batch_t *b;
			byte *chunk;
			u32 nb, k;
			const u16 cl = gc_wd_chunk_cluster[i];

			if( !( gc_wd_pvs[cl >> 3] & ( 1 << ( cl & 7 ))))
				continue;
			pvs_hits++;
			if( frustum_cull && !R_GXWorldDLBoxVisible( i, normals, org ))
			{
				frustum_drop++;
				continue;
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
				drawn++;
			}
			chunks++;
		}
		if( !logged || ( gc_wd_frame & 255 ) == 0 )
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: world DL pvs=%d frustum_drop=%d cull=%d\n",
				pvs_hits, frustum_drop, frustum_cull ? 1 : 0 );
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
			"Xash3D GameCube: world DL frame=%d cluster=%d chunks=%d batches=%d waiting=%d bind_miss=%d\n",
			gc_wd_frame, cluster, chunks, drawn, waiting, bind_miss );
	}
	return drawn;
}

#endif /* XASH_GAMECUBE */
