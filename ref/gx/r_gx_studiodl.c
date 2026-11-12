/*
r_gx_studiodl.c - static studio mapmodel GX display lists (GCsd)

Bake (tools/bake_studio_dl.py) emits media/sd_<map>.gsd: rest-pose skinned
geometry in model space, one entry per (model path, body, skin). The pack
lives in ARAM; MEM1 only holds a small bounce for the entry being drawn.

Same tris / UVs / studio skins as the CPU path — skips bone skin + FIFO
rebuild. Animated players / viewmodels stay on r_studio.c.

-gcnostudiodl disables the path.
*/

#include "r_local.h"

#if XASH_GAMECUBE

#include <gccore.h>
#include <ogc/gx.h>
#include <math.h>
#include <string.h>
#include "../../engine/platform/gamecube/mem_gamecube.h"

#define GC_SD_MAGIC		0x47437364u /* 'GCsd' */
#define GC_SD_ST_FRAC		9
#define GC_SD_MAX_ENTRIES	128
#define STUDIO_NF_MASKED_SD	0x0040

typedef struct
{
	u32	magic;
	u16	version;
	u16	n_entries;
	u32	file_size;
	u32	max_entry;
	u32	reserved[4];
} gc_sd_header_t; /* 32 bytes */

typedef struct
{
	u32	name_hash;
	u16	body, skin;
	u32	ofs, bytes;
	s16	mins[3], maxs[3];
	u16	pad[2];
} gc_sd_dir_t;

typedef struct
{
	u16	tex, flags;
	u32	dl_ofs, dl_bytes;
} gc_sd_batch_t;

extern void *Mod_GCWorldArraySpillTryAlloc( size_t bytes, const char *what );
extern void Mod_GCWorldArraySpillPinBase( const void *ptr, size_t bytes );
extern size_t Mod_GCWorldArraySpillUsed( void );

static void R_GXStudioDLAlphaTest( qboolean on )
{
	if( on )
		GX_SetAlphaCompare( GX_GREATER, 127, GX_AOP_AND, GX_ALWAYS, 0 );
	else
		GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetZCompLoc( on ? GX_FALSE : GX_TRUE );
}

static int gc_sd_state; /* 0 idle, 1 ready, -1 failed */
static char gc_sd_map[64];
static gc_sd_header_t gc_sd_hdr;
static gc_sd_dir_t gc_sd_dir[GC_SD_MAX_ENTRIES];
static u32 gc_sd_aram;
static byte *gc_sd_bounce;
static u32 gc_sd_bounce_size;
static size_t gc_sd_spill_mark;
static int gc_sd_draws;
static int gc_sd_misses;

static unsigned R_GXStudioDLHashName( const char *name )
{
	unsigned h = 2166136261u;
	const char *p;

	if( !name )
		return 0;
	for( p = name; *p; p++ )
	{
		unsigned c = (unsigned char)*p;

		if( c >= 'A' && c <= 'Z' )
			c += 32;
		if( c == '\\' )
			c = '/';
		h ^= c;
		h *= 16777619u;
	}
	return h;
}

static qboolean R_GXStudioDLFetch( u32 file_ofs, void *dst, u32 len )
{
	if( !gc_sd_aram || !dst || !len )
		return false;
	len = ( len + 31u ) & ~31u;
	GC_AramTransfer( gc_sd_aram + file_ofs, dst, len, false );
	return true;
}

static void R_GXStudioDLBuildView( Mtx out )
{
	vec3_t forward, right, up;
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

static void R_GXStudioDLMtxFromEntity( Mtx out, cl_entity_t *e )
{
	matrix3x4 ent;
	vec3_t origin, angles;
	int i, j;

	VectorCopy( e->origin, origin );
	VectorCopy( e->angles, angles );
	/* Match R_StudioSetUpTransform Quake pitch fix for world studios. */
	if( !FBitSet( gp_host->features, ENGINE_COMPENSATE_QUAKE_BUG ))
		angles[PITCH] = -angles[PITCH];
	Matrix3x4_CreateFromEntity( ent, angles, origin, 1.0f );
	for( i = 0; i < 3; i++ )
		for( j = 0; j < 4; j++ )
			out[i][j] = ent[i][j];
}

static void R_GXStudioDLLoad( const char *map )
{
	char path[128];
	file_t *f;
	byte *bounce;
	u32 done, try_sz;
	u32 n;

	gc_sd_state = -1;
	gc_sd_aram = 0;
	gc_sd_bounce = NULL;
	gc_sd_bounce_size = 0;
	gc_sd_draws = 0;
	gc_sd_misses = 0;
	memset( &gc_sd_hdr, 0, sizeof( gc_sd_hdr ));
	memset( gc_sd_dir, 0, sizeof( gc_sd_dir ));

	if( gEngfuncs.Sys_CheckParm( "-gcnostudiodl" ))
		return;

	Q_snprintf( path, sizeof( path ), "media/sd_%s.gsd", map );
	f = gEngfuncs.fsapi->Open( path, "rb", true );
	if( !f )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: studio DL no %s\n", path );
		return;
	}
	if( gEngfuncs.fsapi->Read( f, &gc_sd_hdr, sizeof( gc_sd_hdr )) != (fs_offset_t)sizeof( gc_sd_hdr ))
	{
		gEngfuncs.fsapi->Close( f );
		return;
	}
	if( gc_sd_hdr.magic != GC_SD_MAGIC || gc_sd_hdr.version != 1
		|| gc_sd_hdr.n_entries == 0 || gc_sd_hdr.n_entries > GC_SD_MAX_ENTRIES
		|| gc_sd_hdr.file_size < 32u + 32u * gc_sd_hdr.n_entries )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: studio DL bad header %s\n", path );
		gEngfuncs.fsapi->Close( f );
		return;
	}
	n = gc_sd_hdr.n_entries;
	gEngfuncs.fsapi->Seek( f, 32, SEEK_SET );
	if( gEngfuncs.fsapi->Read( f, gc_sd_dir, n * sizeof( gc_sd_dir_t ))
		!= (fs_offset_t)( n * sizeof( gc_sd_dir_t )))
	{
		gEngfuncs.fsapi->Close( f );
		return;
	}

	try_sz = gc_sd_hdr.max_entry;
	if( try_sz < 4096u )
		try_sz = 4096u;
	if( try_sz > 96u * 1024u )
		try_sz = 96u * 1024u;
	try_sz = ( try_sz + 31u ) & ~31u;
	gc_sd_bounce = (byte *)Mod_GCWorldArraySpillTryAlloc( try_sz, "studio DL bounce" );
	if( !gc_sd_bounce )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: studio DL no bounce\n" );
		gEngfuncs.fsapi->Close( f );
		return;
	}
	gc_sd_bounce_size = try_sz;
	Mod_GCWorldArraySpillPinBase( gc_sd_bounce, try_sz );
	gc_sd_spill_mark = Mod_GCWorldArraySpillUsed();

	gc_sd_aram = GC_AramMapReserve( gc_sd_hdr.file_size );
	bounce = gc_sd_bounce;
	done = 0;
	if( gc_sd_aram )
	{
		gEngfuncs.fsapi->Seek( f, 0, SEEK_SET );
		while( done < gc_sd_hdr.file_size )
		{
			u32 chunk = gc_sd_hdr.file_size - done;

			if( chunk > gc_sd_bounce_size )
				chunk = gc_sd_bounce_size;
			if( gEngfuncs.fsapi->Read( f, bounce, chunk ) != (fs_offset_t)chunk )
				break;
			DCFlushRange( bounce, ( chunk + 31u ) & ~31u );
			GC_AramTransfer( gc_sd_aram + done, bounce, ( chunk + 31u ) & ~31u, true );
			done += chunk;
		}
		if( done < gc_sd_hdr.file_size )
			gc_sd_aram = 0;
	}
	gEngfuncs.fsapi->Close( f );
	if( !gc_sd_aram )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: studio DL ARAM failed %s\n", path );
		return;
	}

	gc_sd_state = 1;
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: studio DL %s entries=%u file=%u Kb max=%u (%s)\n",
		path, gc_sd_hdr.n_entries, gc_sd_hdr.file_size / 1024u,
		gc_sd_hdr.max_entry, "ARAM" );
}

qboolean R_GXStudioDLActive( void )
{
	model_t *world = WORLDMODEL;
	char base[64];

	if( !world || !world->name[0] )
		return false;
	if( gEngfuncs.Sys_CheckParm( "-gcnostudiodl" ))
		return false;
	COM_FileBase( world->name, base, sizeof( base ));
	if( gc_sd_state != 0 && ( Q_stricmp( base, gc_sd_map )
		|| ( gc_sd_state == 1 && Mod_GCWorldArraySpillUsed() < gc_sd_spill_mark )))
		gc_sd_state = 0;
	if( gc_sd_state == 0 )
	{
		Q_strncpy( gc_sd_map, base, sizeof( gc_sd_map ));
		R_GXStudioDLLoad( base );
	}
	return gc_sd_state == 1;
}

static int R_GXStudioDLFind( unsigned name_hash, int body, int skin )
{
	int i, soft = -1;

	for( i = 0; i < (int)gc_sd_hdr.n_entries; i++ )
	{
		if( gc_sd_dir[i].name_hash != name_hash )
			continue;
		if( (int)gc_sd_dir[i].body == body && (int)gc_sd_dir[i].skin == skin )
			return i;
		if( (int)gc_sd_dir[i].body == body && soft < 0 )
			soft = i;
		if( soft < 0 && (int)gc_sd_dir[i].body == 0 && (int)gc_sd_dir[i].skin == 0 )
			soft = i;
	}
	return soft;
}

static void R_GXStudioDLSetupVtx( void )
{
	GX_DrawDone();
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_INDEX16 );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_POS, GX_POS_XYZ, GX_S16, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_CLR0, GX_CLR_RGB, GX_RGB565, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT6, GX_VA_TEX0, GX_TEX_ST, GX_S16, GC_SD_ST_FRAC );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_SetNumTexGens( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetNumTevStages( 1 );
	GX_SetTevDirect( GX_TEVSTAGE0 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	/* Match Flipper world-studio REPLACE (texture only). */
	GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
	GX_SetCullMode( GX_CULL_BACK );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_InvVtxCache();
}

/*
=============
R_GXStudioDLTryDraw

Draw a static mapmodel from the GCsd pack. Returns true if handled
(caller must skip CPU skin / mesh emit).
=============
*/
qboolean R_GXStudioDLTryDraw( cl_entity_t *e, model_t *mod )
{
	unsigned hash;
	int idx, bi;
	u32 bytes;
	byte *entry;
	u16 n_pos, n_batches;
	u32 pos_ofs;
	const gc_sd_batch_t *batch;
	Mtx view, model, mv;
	Mtx44 proj;
	studiohdr_t *hdr;
	mstudiotexture_t *ptexture;
	f32 z_near = 4.0f, z_far;
	f32 fov_y, fov_x, y_max, x_max;
	int skin, body;
	static qboolean logged;

	if( !e || !mod || !mod->name[0] )
		return false;
	if( !GC_UseGxWorldDraw())
		return false;
	if( e == tr.viewent || e->player )
		return false;
	if( !R_GXStudioDLActive())
		return false;

	hash = R_GXStudioDLHashName( mod->name );
	body = e->curstate.body;
	skin = e->curstate.skin;
	idx = R_GXStudioDLFind( hash, body, skin );
	if( idx < 0 )
	{
		gc_sd_misses++;
		return false;
	}

	bytes = gc_sd_dir[idx].bytes;
	if( bytes == 0 || bytes > gc_sd_bounce_size )
		return false;
	if( !R_GXStudioDLFetch( gc_sd_dir[idx].ofs, gc_sd_bounce, bytes ))
		return false;
	entry = gc_sd_bounce;
	n_pos = *(const u16 *)( entry + 0 );
	n_batches = *(const u16 *)( entry + 2 );
	pos_ofs = *(const u32 *)( entry + 4 );
	if( n_pos == 0 || n_batches == 0 || pos_ofs >= bytes )
		return false;

	hdr = (studiohdr_t *)gEngfuncs.Mod_Extradata( mod_studio, mod );
	if( !hdr || hdr->numtextures <= 0 || hdr->textureindex <= 0 )
		return false;
	ptexture = (mstudiotexture_t *)((byte *)hdr + hdr->textureindex );

	R_GXStudioForceBegin( false );
	R_GXStudioDLBuildView( view );
	R_GXStudioDLMtxFromEntity( model, e );
	guMtxConcat( view, model, mv );

	z_far = RI.farClip > 1.0f ? RI.farClip : 4096.0f;
	fov_y = RI.rvp.fov_y > 1.0f ? RI.rvp.fov_y : 90.0f;
	fov_x = RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f;
	y_max = z_near * tanf( fov_y * 3.14159265f / 360.0f );
	x_max = z_near * tanf( fov_x * 3.14159265f / 360.0f );
	guFrustum( proj, y_max, -y_max, -x_max, x_max, z_near, z_far );
	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );
	GX_SetCurrentMtx( GX_PNMTX0 );

	R_GXStudioDLSetupVtx();
	GX_SetArray( GX_VA_POS, entry + pos_ofs, 6 );

	batch = (const gc_sd_batch_t *)( entry + 8 );
	for( bi = 0; bi < (int)n_batches; bi++, batch++ )
	{
		int tex_i = (int)batch->tex;
		int texnum;

		if( batch->dl_bytes == 0 || batch->dl_ofs + batch->dl_bytes > bytes )
			continue;
		if( tex_i < 0 || tex_i >= hdr->numtextures )
			tex_i = 0;
		/* batch->tex is already the resolved texture index from the bake. */
		texnum = ptexture[tex_i].index;
		if( texnum <= 0 )
			texnum = tr.whiteTexture;
		R_GXStudioDLAlphaTest( ( batch->flags & STUDIO_NF_MASKED_SD ) != 0 );
		R_GXStudioBindTexnum( (unsigned)texnum );
		GX_CallDispList( entry + batch->dl_ofs, batch->dl_bytes );
	}

	R_GXStudioEnd();
	R_GXStudioInvalidateBindings(); /* VTXFMT6 → next CPU studio must relayout */
	gc_sd_draws++;
	r_stats.c_studio_models_drawn++;
	if( !logged )
	{
		logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: studio DL draw %s body=%d batches=%u pos=%u\n",
			mod->name, body, n_batches, n_pos );
	}
	return true;
}

#else /* !XASH_GAMECUBE */

qboolean R_GXStudioDLActive( void )
{
	return false;
}

qboolean R_GXStudioDLTryDraw( cl_entity_t *e, model_t *mod )
{
	(void)e;
	(void)mod;
	return false;
}

#endif
