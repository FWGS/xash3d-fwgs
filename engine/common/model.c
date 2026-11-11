/*
model.c - modelloader
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "common.h"
#include "mod_local.h"
#include "sprite.h"
#include "xash3d_mathlib.h"
#include "alias.h"
#include "studio.h"
#include "wadfile.h"
#include "world.h"
#include "enginefeatures.h"
#include "client.h"
#include "server.h"

static model_info_t	mod_crcinfo[MAX_MODELS];
static model_t	mod_known[MAX_MODELS];
static int	mod_numknown = 0;
poolhandle_t      com_studiocache;		// cache for submodels
#if XASH_GAMECUBE
#include "gamecube/mem_gamecube.h"
#include "platform/platform.h"
void FS_ClearFindMissCache( void );
qboolean GC_IsNewGameWorldReady( void );
void R_GXStudioInvalidateBindings( void );
unsigned GC_GetNewGamePresentCount( void );

static qboolean Mod_GCIsNewGameRoute( void )
{
	return Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcmenuplaystart" );
}

/* Real MDLs live in the 1 MiB studio BSS arena (lean meshes + viewweapons).
 * Stubbing left GetEyePosition null and stalled denser maps during spawn —
 * the GameCube can hold the early-campaign cast without that compromise. */
/* 1 MiB studio BSS — fill it. Lean meshes + *t skins keep HL1 looking right. */
#define GC_REAL_STUDIO_MAX_NPC    20
#define GC_REAL_STUDIO_MAX_VIEW   5
#define GC_REAL_STUDIO_MAX_BYTES  ( 1000 * 1024 )
#define GC_REAL_STUDIO_MAX_NPC_MENU   16
#define GC_REAL_STUDIO_MAX_BYTES_MENU ( 1000 * 1024 )

static int    gc_real_studio_npc;
static int    gc_real_studio_view;
static size_t gc_real_studio_bytes;
static char   gc_landmark_viewmodel[64];
static model_t *gc_pinned_viewmodels[GC_REAL_STUDIO_MAX_VIEW];
static int     gc_pinned_viewmodel_count;
static int     gc_deferred_studio_attempts;
/* Keep the complete lean c0a0 set and its resident T.mdl headers out of the
 * fragmented MEM1 heap.  The meshes alone are ~384 KiB; their retained skin
 * metadata brings the live set to roughly 0.75 MiB. */
/* Was 1 MiB → 184 KiB for bootstrap CDF headroom. G381: scientist.mdl lean
 * is ~171 KiB; with v_crowbar (~46 KiB) that already exceeds 184, so hallway
 * soft skins never got a BSS mesh (heap zip OOM → stub SetModel). 384 KiB
 * holds crowbar + scientist + barney (~343 KiB) for deferred T.mdl attach;
 * tip-safe c1a0a→c1a0d must stay CHANGELEVEL_READY after this bump. The DoD
 * listen build also needs room for a team weapon after bootstrap crowbar and
 * handgun meshes, so reserve the same bounded 384 KiB for the live loadout. */
#if XASH_DODGC_NETWORK
#define GC_STUDIO_BSS_BYTES (384 * 1024)
#else
#define GC_STUDIO_BSS_BYTES (384 * 1024)
#endif
static byte    gc_studio_bss[GC_STUDIO_BSS_BYTES] __attribute__((aligned( 32 )));
static size_t  gc_studio_bss_bump;
static int     gc_studio_bss_owners; /* resident cache.data slices */
/* Pre-entity promote: mesh only — G376 skin uploads tip MEM1 and crash
 * mid-spawn (c1a1f ~entity 96). Skins re-promote after MAP_READY. */
static qboolean gc_studio_pre_entity_mesh_only;

qboolean Mod_GCStudioPreEntityMeshOnly( void )
{
	return gc_studio_pre_entity_mesh_only;
}

byte *Mod_GCStudioBssAlloc( size_t size );

/* Menu New Game: read a studio sequence-group file (scientist01.mdl ...)
 * straight into the studio arena. The heap path needed the file twice
 * (FS_LoadFile + cache copy, ~130 KiB) and failed during the c0a0 walks. */
byte *Mod_GCStudioArenaLoadFile( const char *path, fs_offset_t *length )
{
	file_t *f = FS_Open( path, "rb", false );
	fs_offset_t flen, total = 0, got;
	byte *slot;

	*length = 0;
	if( !f )
		return NULL;
	flen = FS_FileLength( f );
	slot = flen > 0 ? Mod_GCStudioBssAlloc( (size_t)flen ) : NULL;
	if( !slot )
	{
		FS_Close( f );
		return NULL;
	}
	if( GC_PrefetchRead( path, slot, (unsigned int)flen )
		|| GC_AssetCacheRead( path, slot, (unsigned int)flen ))
	{
		FS_Close( f );
		*length = flen;
		return slot;
	}
	while( total < flen && ( got = FS_Read( f, slot + total, flen - total )) > 0 )
		total += got;
	FS_Close( f );
	if( total != flen )
		return NULL; /* bump hole, rare short read */
	GC_AssetCacheStore( path, slot, (unsigned int)flen );
	*length = flen;
	Con_Reportf( "Xash3D GameCube: studio seqgroup '%s' in arena (%s) bump=%s\n",
		path, Q_memprint( (size_t)flen ), Q_memprint( gc_studio_bss_bump ));
	return slot;
}

/* Menu New Game sequence groups (scientist01..07.mdl, barney01..03.mdl, up
 * to 65 KiB each) live in a few fixed arena slots reused least-recently
 * used. Bump-allocating every group filled the 1 MiB studio arena by c0a0b
 * ("LoadCacheFile: can't load models/scientist04.mdl"). Reusing a slot
 * clears its previous owner's cache_user_t, so Cache_Check reloads it. */
#define GC_SEQGROUP_SLOTS     4
#define GC_SEQGROUP_SLOT_SIZE ( 66 * 1024 )
static struct
{
	byte         *buf;
	cache_user_t *owner;
	unsigned     used;
} gc_seqgroup_slot[GC_SEQGROUP_SLOTS];
static unsigned gc_seqgroup_clock;

static int Mod_GCSeqGroupSlotOf( const void *ptr )
{
	int i;

	for( i = 0; i < GC_SEQGROUP_SLOTS; i++ )
	{
		if( gc_seqgroup_slot[i].buf && (const byte *)ptr >= gc_seqgroup_slot[i].buf
			&& (const byte *)ptr < gc_seqgroup_slot[i].buf + GC_SEQGROUP_SLOT_SIZE )
			return i;
	}
	return -1;
}

byte *Mod_GCSeqGroupLoad( const char *path, cache_user_t *cu, fs_offset_t *length )
{
	file_t *f = NULL;
	fs_offset_t flen, total = 0, got;
	unsigned int cached = 0;
	int i, pick = -1;

	*length = 0;
	/* Animation slots are recycled least-recently-used, so the same group is
	 * read many times per map. After the first read it comes from ARAM. */
	if( GC_AssetCacheLookup( path, &cached ) && cached > 0 && cached <= GC_SEQGROUP_SLOT_SIZE )
		flen = (fs_offset_t)cached;
	else
	{
		cached = 0;
		f = FS_Open( path, "rb", false );
		if( !f )
			return NULL;
		flen = FS_FileLength( f );
	}
	if( flen <= 0 || flen > GC_SEQGROUP_SLOT_SIZE )
	{
		if( f )
			FS_Close( f );
		return NULL;
	}
	for( i = 0; i < GC_SEQGROUP_SLOTS && pick < 0; i++ )
	{
		if( !gc_seqgroup_slot[i].buf )
		{
			gc_seqgroup_slot[i].buf = Mod_GCStudioBssAlloc( GC_SEQGROUP_SLOT_SIZE );
			if( gc_seqgroup_slot[i].buf )
				pick = i;
			break;
		}
		if( !gc_seqgroup_slot[i].owner )
			pick = i;
	}
	if( pick < 0 )
	{
		for( i = 0; i < GC_SEQGROUP_SLOTS; i++ )
		{
			if( gc_seqgroup_slot[i].buf && ( pick < 0 || gc_seqgroup_slot[i].used < gc_seqgroup_slot[pick].used ))
				pick = i;
		}
	}
	if( pick < 0 )
	{
		if( f )
			FS_Close( f );
		return NULL;
	}
	if( gc_seqgroup_slot[pick].owner && Mod_GCSeqGroupSlotOf( gc_seqgroup_slot[pick].owner->data ) == pick )
		gc_seqgroup_slot[pick].owner->data = NULL;
	gc_seqgroup_slot[pick].owner = NULL;
	if( cached && GC_AssetCacheRead( path, gc_seqgroup_slot[pick].buf, cached ))
		total = flen;
	else
	{
		if( !f )
			f = FS_Open( path, "rb", false );
		if( !f )
			return NULL;
		while( total < flen && ( got = FS_Read( f, gc_seqgroup_slot[pick].buf + total, flen - total )) > 0 )
			total += got;
		if( total == flen )
			GC_AssetCacheStore( path, gc_seqgroup_slot[pick].buf, (unsigned int)flen );
	}
	if( f )
		FS_Close( f );
	if( total != flen )
		return NULL;
	gc_seqgroup_slot[pick].owner = cu;
	gc_seqgroup_slot[pick].used = ++gc_seqgroup_clock;
	cu->data = gc_seqgroup_slot[pick].buf;
	*length = flen;
	Con_Reportf( "Xash3D GameCube: studio seqgroup '%s' in slot %d (%s)%s\n", path, pick,
		Q_memprint( (size_t)flen ), cached ? " from ARAM" : "" );
	return gc_seqgroup_slot[pick].buf;
}

void Mod_GCSeqGroupTouch( const void *ptr )
{
	int i = Mod_GCSeqGroupSlotOf( ptr );

	if( i >= 0 )
		gc_seqgroup_slot[i].used = ++gc_seqgroup_clock;
}

qboolean Mod_GCIsStudioBssCache( const void *ptr )
{
	const byte *p = (const byte *)ptr;

	if( !ptr )
		return false;
	return ( p >= gc_studio_bss && p < gc_studio_bss + GC_STUDIO_BSS_BYTES );
}

byte *Mod_GCStudioBssAlloc( size_t size )
{
	size_t align_off;

	if( size == 0 || size > GC_STUDIO_BSS_BYTES )
		return NULL;
	align_off = ( gc_studio_bss_bump + 31u ) & ~31u;
	if( align_off + size > GC_STUDIO_BSS_BYTES )
		return NULL;
	gc_studio_bss_bump = align_off + size;
	gc_studio_bss_owners++;
	if( Sys_CheckParm( "-gcmodeltrace" ))
		Con_Reportf( "modeltrace alloc %p size=%u bump=%u\n", gc_studio_bss + align_off, (unsigned)size, (unsigned)gc_studio_bss_bump );
	return gc_studio_bss + align_off;
}

/* Roll back an unsuccessful read only while its slice is still last. */
static void Mod_GCStudioBssRollbackLast( byte *slot, size_t size, size_t previous_bump )
{
	size_t offset;

	if( !Mod_GCIsStudioBssCache( slot ) || gc_studio_bss_owners <= 0 )
		return;
	offset = (size_t)( slot - gc_studio_bss );
	if( size > GC_STUDIO_BSS_BYTES - offset || previous_bump > offset )
		return;
	if( gc_studio_bss_bump == offset + size )
	{
		gc_studio_bss_bump = previous_bump;
		gc_studio_bss_owners--;
	}
}

/*
 * G381: grow the last BSS studio slice in place (mesh → mesh+T.mdl headers).
 * Tip CapFaces leaves heap empty; scientist mesh already occupies the bump so
 * a second Mod_GCStudioBssAlloc(merged) fails and we used to keep mesh-only
 * (white NPC / HUD sprite bind).
 */
byte *Mod_GCStudioBssGrowLast( void *ptr, size_t old_size, size_t new_size )
{
	byte *p = (byte *)ptr;
	size_t off;
	size_t old_end;

	if( !ptr || old_size == 0 || new_size < old_size || new_size > GC_STUDIO_BSS_BYTES )
		return NULL;
	if( !Mod_GCIsStudioBssCache( ptr ))
		return NULL;
	off = (size_t)( p - gc_studio_bss );
	old_end = off + old_size;
	if( off + new_size > GC_STUDIO_BSS_BYTES )
		return NULL;
	/* Deferred T.mdl headers: lean load reserved a pad so bump > old_end and
	 * later companions (scientist) sit above that pad. Fit in-place. */
	if( off + new_size <= gc_studio_bss_bump )
		return p;
	/* Accept either exact bump or 32-byte padded bump from Alloc. */
	if( gc_studio_bss_bump != old_end
		&& gc_studio_bss_bump != (( old_end + 31u ) & ~31u ))
		return NULL;
	gc_studio_bss_bump = off + new_size;
	return p;
}

/* After embedded soft-skin upload, only the mesh+texhdr slice stays live.
 * Shrink the bump so Xen companions (islave ~145 KiB) still fit — c4a3 left
 * nihilanth's 420 KiB read pinned and starved the arena (probe 030354).
 * Soft-skin pixels must live in the studio skin overflow (r_image.c), not
 * this bump — TrimLast would otherwise clobber them. */
void Mod_GCStudioBssTrimLast( void *ptr, size_t kept )
{
	byte *p = (byte *)ptr;
	size_t off, new_bump;

	if( !ptr || kept == 0 || !Mod_GCIsStudioBssCache( ptr ))
		return;
	off = (size_t)( p - gc_studio_bss );
	if( off >= gc_studio_bss_bump || off + kept > gc_studio_bss_bump )
		return;
	new_bump = ( off + kept + 31u ) & ~31u;
	if( new_bump >= gc_studio_bss_bump )
		return;
	Con_Reportf( "Xash3D GameCube: studio BSS trim kept=%s bump %s -> %s\n",
		Q_memprint( kept ), Q_memprint( gc_studio_bss_bump ), Q_memprint( new_bump ));
	gc_studio_bss_bump = new_bump;
}

static qboolean Mod_GCIsPinnedViewModel( const model_t *mod )
{
	int i;

	if( !mod )
		return false;
	for( i = 0; i < gc_pinned_viewmodel_count; i++ )
	{
		if( gc_pinned_viewmodels[i] == mod )
			return true;
	}
	return false;
}

static void Mod_GCPinViewModel( model_t *mod )
{
	int i;

	if( !mod || Mod_GCIsPinnedViewModel( mod ))
		return;
	if( gc_pinned_viewmodel_count >= GC_REAL_STUDIO_MAX_VIEW )
		return;
	for( i = 0; i < gc_pinned_viewmodel_count; i++ )
	{
		if( gc_pinned_viewmodels[i] == mod )
			return;
	}
	gc_pinned_viewmodels[gc_pinned_viewmodel_count++] = mod;
	Con_Reportf( "Xash3D GameCube: G156 pinned viewmodel %s\n",
		mod->name[0] ? mod->name : "?" );
	/* G506 presentation gate also accepts this as G105 on non-tram maps
	 * where deferred crowbar promote may not run. */
	if( Mod_GCIsNewGameRoute() && mod->name[0]
		&& ( Q_stristr( mod->name, "v_crowbar" )
			|| Q_stristr( mod->name, "v_9mmhandgun" )))
	{
		static qboolean g105_from_pin_logged;

		if( !g105_from_pin_logged )
		{
			g105_from_pin_logged = true;
			Con_Reportf( "Xash3D GameCube: G105 landmark viewmodel ready %s\n",
				mod->name );
			GCube_AppendSwissEvidence( "G105 landmark viewmodel ready %s", mod->name );
		}
	}
}

static qboolean Mod_GCStudioAlreadyResident( model_t *mod )
{
	studiohdr_t *hdr;

	if( !mod || !mod->cache.data )
		return false;
	hdr = (studiohdr_t *)mod->cache.data;
	return ( hdr->numbodyparts > 0 ) ? true : false;
}

static qboolean Mod_GCStudioNameAllowed( const char *name, qboolean *is_viewmodel )
{
	char bare[64];
	static const char *npcs[] = {
		"scientist", "barney", "gman", "player", "roach",
		"headcrab", "houndeye", "zombie",
		"scientist01", "scientist02", "scientist03",
		/* Menu New Game intro props + monster_furniture (c1a1f uses filecabinet). */
		"forklift", "loader", "filecabinet",
		/* Campaign cast — lean mesh + *t skin atlas stays under 1 MiB BSS. */
		"bullsquid", "agrunt", "islave", "hgrunt", "tentacle2", "garg",
		"apache", "osprey", "ichthyosaur", "leech", "barnacle",
		/* Xen's final encounters require these studio headers for boss AI,
		 * animation and visible alien controllers. */
		"nihilanth", "big_mom", "baby_headcrab", "controller",
		/* Tiny lean world-weapon stub for Flipper studio draw admit. */
		"w_crowbar",
		NULL
	};
	static const char *views[] = {
		"v_crowbar", "v_9mmhandgun", "v_9mmar", "v_shotgun", "v_357",
		/* DoD CapFaces listen — team loadout viewmodels (lean-injected). */
		"v_garand", "v_mp40", "v_98k", "v_tommy",
		"v_colt", "v_luger", "v_amerk", "v_spade",
		NULL
	};
	int i;

	if( is_viewmodel )
		*is_viewmodel = false;
	if( !name || !name[0] )
		return false;

	COM_FileBase( name, bare, sizeof( bare ));
	for( i = 0; npcs[i]; i++ )
	{
		if( !Q_stricmp( bare, npcs[i] ))
			return true;
	}
	for( i = 0; views[i]; i++ )
	{
		if( !Q_stricmp( bare, views[i] ))
		{
			if( is_viewmodel )
				*is_viewmodel = true;
			return true;
		}
	}
	return false;
}

static qboolean Mod_GCAllowRealStudioLoad( const char *name, size_t filesize )
{
	qboolean is_view = false;

	/* Retail Flipper + New Game probe — allowlisted meshes only. */
	if( !Mod_GCStudioNameAllowed( name, &is_view ))
		return false;
	/* Menu New Game keeps the whole intro cast resident in the 1 MiB studio
	 * BSS arena (scientist + barney + forklift + view meshes ~0.6 MiB). */
	const int max_npc = GC_MenuRunsServer() ? GC_REAL_STUDIO_MAX_NPC_MENU : GC_REAL_STUDIO_MAX_NPC;
	const size_t max_bytes = GC_MenuRunsServer() ? GC_REAL_STUDIO_MAX_BYTES_MENU : GC_REAL_STUDIO_MAX_BYTES;

	if( is_view )
	{
		if( gc_real_studio_view >= GC_REAL_STUDIO_MAX_VIEW )
			return false;
	}
	else if( gc_real_studio_npc >= max_npc )
		return false;
	/* The internally textured Nihilanth is approximately 420 KiB. */
	if( filesize < sizeof( studiohdr_t ) || filesize > ( 448 * 1024 ))
		return false;
	if( gc_real_studio_bytes + filesize > max_bytes )
		return false;
	return true;
}

static void Mod_GCNoteRealStudioLoaded( const char *name, size_t filesize )
{
	qboolean is_view = false;

	Mod_GCStudioNameAllowed( name, &is_view );
	if( is_view )
		gc_real_studio_view++;
	else
		gc_real_studio_npc++;
	gc_real_studio_bytes += filesize;
	Con_Reportf( "Xash3D GameCube: real studio loaded '%s' (%s) npc=%d view=%d budget=%s/%s\n",
		name, Q_memprint( filesize ), gc_real_studio_npc, gc_real_studio_view,
		Q_memprint( gc_real_studio_bytes ), Q_memprint( GC_REAL_STUDIO_MAX_BYTES ));
}

/*
=============
Mod_GCLoadStudioFile

Read an MDL via FS (prefer tiny gc_studio/ mirror — retail models/ is too large
for reliable ISO9660 lookup after map prep).

G287/G289: stream into BSS bump arena (crowbar+handgun lean meshes) when heap
cannot serve tens of KiB after map prep.
=============
*/
static byte *Mod_GCLoadStudioFile( const char *model_path, fs_offset_t *length, qboolean *from_bss )
{
	char bare[64];
	char mirror[MAX_QPATH];
	byte *buf;
	byte *slot;
	file_t *f;
	fs_offset_t flen, total, got;
	size_t previous_bump;

	*length = 0;
	if( from_bss )
		*from_bss = false;
	COM_FileBase( model_path, bare, sizeof( bare ));
	Q_snprintf( mirror, sizeof( mirror ), "gc_studio/%s.mdl", bare );

	/* A model read once this boot stays in ARAM (raw file bytes, before the
	 * in-place byte swap), so an arena reset, a team pick or a changelevel
	 * reloads it by DMA instead of from disc. */
	{
		unsigned int cached = 0;

		if( GC_AssetCacheLookup( mirror, &cached ) && cached >= sizeof( studiohdr_t )
			&& cached <= GC_STUDIO_BSS_BYTES )
		{
			if( !Mod_GCAllowRealStudioLoad( model_path, (size_t)cached ))
			{
				Con_Reportf( "Xash3D GameCube: deferred studio budget skip '%s' (%s)\n",
					model_path, Q_memprint( (size_t)cached ));
				return NULL;
			}
			previous_bump = gc_studio_bss_bump;
			slot = Mod_GCStudioBssAlloc( (size_t)cached );
			if( slot && GC_AssetCacheRead( mirror, slot, cached ))
			{
				*length = (fs_offset_t)cached;
				if( from_bss )
					*from_bss = true;
				Con_Reportf( "Xash3D GameCube: studio '%s' from ARAM (%s) bump=%s\n",
					model_path, Q_memprint( (size_t)cached ), Q_memprint( gc_studio_bss_bump ));
				return slot;
			}
			if( slot )
				Mod_GCStudioBssRollbackLast( slot, (size_t)cached, previous_bump );
		}
	}

	if( FS_FileExists( mirror, false ))
	{
		f = FS_Open( mirror, "rb", false );
		if( f )
		{
			flen = FS_FileLength( f );
			/* Check limits before reserving a persistent arena slice. A
			 * budget rejection after the read cannot free that slice once
			 * other models have been allocated. */
			if( flen >= (fs_offset_t)sizeof( studiohdr_t ) &&
				!Mod_GCAllowRealStudioLoad( model_path, (size_t)flen ))
			{
				FS_Close( f );
				Con_Reportf( "Xash3D GameCube: deferred studio budget skip '%s' (%s)\n",
					model_path, Q_memprint( (size_t)flen ));
				return NULL;
			}
			slot = NULL;
			previous_bump = gc_studio_bss_bump;
			if( flen >= (fs_offset_t)sizeof( studiohdr_t )
				&& flen <= (fs_offset_t)GC_STUDIO_BSS_BYTES )
				slot = Mod_GCStudioBssAlloc( (size_t)flen );
			if( slot )
			{
				total = 0;
				while( total < flen )
				{
					got = FS_Read( f, slot + total, flen - total );
					if( got <= 0 )
						break;
					total += got;
				}
				FS_Close( f );
				if( total == flen )
				{
					GC_AssetCacheStore( mirror, slot, (unsigned int)flen );
					*length = flen;
					if( from_bss )
						*from_bss = true;
					Con_Reportf( "Xash3D GameCube: G289 studio BSS read '%s' via %s (%s) bump=%s\n",
						model_path, mirror, Q_memprint( (size_t)flen ),
						Q_memprint( gc_studio_bss_bump ));
					return slot;
				}
				Con_Reportf( "Xash3D GameCube: G289 studio BSS short '%s' read=%li want=%li\n",
					mirror, (long)total, (long)flen );
				/* Only release the last allocation: another owner may have
				 * acquired a later slice while the file was being read. */
				Mod_GCStudioBssRollbackLast( slot, (size_t)flen, previous_bump );
			}
			else
			{
				FS_Close( f );
				if( flen > (fs_offset_t)GC_STUDIO_BSS_BYTES )
					Con_Reportf( "Xash3D GameCube: G289 studio too large for BSS '%s' (%s)\n",
						mirror, Q_memprint( (size_t)flen ));
			}
		}
	}

	/* G381: CapFaces/pad tex decode can leave scratch holes that block the
	 * 170 KiB scientist zip malloc (mirror miss buf=0). Purge before heap read. */
	Image_GCPurgeDecodeScratch();
	buf = FS_LoadFileMalloc( mirror, length, false );
	if( buf && *length >= (fs_offset_t)sizeof( studiohdr_t ))
	{
		GC_AssetCacheStore( mirror, buf, (unsigned int)*length );
		Con_Reportf( "Xash3D GameCube: deferred studio read '%s' via %s (%s)\n",
			model_path, mirror, Q_memprint( (size_t)*length ));
		return buf;
	}
	Con_Reportf( "Xash3D GameCube: deferred studio mirror miss '%s' buf=%p len=%li exists=%d\n",
		mirror, buf, (long)*length, FS_FileExists( mirror, false ) ? 1 : 0 );
	if( buf )
	{
		free( buf );
		buf = NULL;
	}
	*length = 0;

	buf = FS_LoadFileMalloc( model_path, length, false );
	if( buf && *length >= (fs_offset_t)sizeof( studiohdr_t ))
	{
		Con_Reportf( "Xash3D GameCube: deferred studio read '%s' via models/ (%s)\n",
			model_path, Q_memprint( (size_t)*length ));
		return buf;
	}
	if( buf )
	{
		free( buf );
		buf = NULL;
	}
	*length = 0;
	return NULL;
}

const char *Mod_GCLandmarkViewModelPath( void )
{
	return gc_landmark_viewmodel[0] ? gc_landmark_viewmodel : NULL;
}

qboolean Mod_GCPromoteStudioPath( const char *path )
{
	model_t *mod;
	byte *buf;
	fs_offset_t length = 0;
	qboolean loaded = false;
	qboolean is_view = false;
	qboolean from_bss = false;

	if( !path || !path[0] )
		return false;

	/* Reject before any FS/disc I/O — tip MEM1 cannot absorb random w_*.mdl. */
	if( !Mod_GCStudioNameAllowed( path, &is_view ))
		return false;

	/* G156: reuse resident mesh — never re-read 130KB from disc under MEM1. */
	mod = Mod_FindName( path, false );
	if( !mod )
		return false;
	if( Mod_GCStudioAlreadyResident( mod ))
	{
		/* Lost Coast CapFaces already pinned the retail 357 — don't let
		 * crowbar/handgun deferred promotes steal the viewent pin. */
		if( is_view && !( Sys_CheckParm( "-gccapfacessmoke" )
				&& gc_landmark_viewmodel[0]
				&& Q_stristr( gc_landmark_viewmodel, "v_357" )
				&& !Q_stristr( path, "v_357" )))
			Mod_GCPinViewModel( mod );
		return true;
	}
	else if( Mod_GCIsPinnedViewModel( mod ))
	{
		/* Already pinned - nothing to do */
		return true;
	}

	buf = Mod_GCLoadStudioFile( path, &length, &from_bss );
	if( !buf || length < (fs_offset_t)sizeof( studiohdr_t ))
	{
		Con_Reportf( "Xash3D GameCube: deferred studio skip '%s' read=%li\n",
			path, (long)length );
		if( buf && !from_bss )
			free( buf );
		return false;
	}

	if( !Mod_GCAllowRealStudioLoad( path, (size_t)length ))
	{
		Con_Reportf( "Xash3D GameCube: deferred studio budget skip '%s' (%s)\n",
			path, Q_memprint( (size_t)length ));
		if( !from_bss )
			free( buf );
		return false;
	}

	mod = Mod_FindName( path, true );
	if( !mod )
	{
		Con_Reportf( "Xash3D GameCube: deferred studio skip '%s' (not registered)\n", path );
		if( !from_bss )
			free( buf );
		return false;
	}

	if( Mod_GCStudioAlreadyResident( mod ))
	{
		if( !from_bss )
			free( buf );
		Mod_GCStudioNameAllowed( path, &is_view );
		if( is_view )
			Mod_GCPinViewModel( mod );
		return true;
	}

	/* G287/G289: ensure we have a valid cache buffer before proceeding */
	if( !buf )
	{
		Con_Reportf( "Xash3D GameCube: deferred studio skip '%s' null buffer\n", path );
		return false;
	}

	mod->cache.data = NULL;
	mod->mempool = 0;
	mod->needload = NL_PRESENT;
	mod->type = mod_studio;

	Image_GCPurgeDecodeScratch();
	Mod_LoadStudioModel( mod, buf, (size_t)length, &loaded );
	/* BSS bump slot is never free()'d — cache may point at it or a calloc copy. */
	if( !from_bss )
		free( buf );
	Image_GCPurgeDecodeScratch();

	if( loaded )
	{
		size_t kept = length;
		studiohdr_t *loaded_hdr = (studiohdr_t *)mod->cache.data;

		if( loaded_hdr && loaded_hdr->length > 0 && (size_t)loaded_hdr->length < kept )
			kept = (size_t)loaded_hdr->length;
		Mod_GCNoteRealStudioLoaded( path, kept );
		Mod_GCStudioNameAllowed( path, &is_view );
		if( is_view && !( Sys_CheckParm( "-gccapfacessmoke" )
				&& gc_landmark_viewmodel[0]
				&& Q_stristr( gc_landmark_viewmodel, "v_357" )
				&& !Q_stristr( path, "v_357" )))
			Mod_GCPinViewModel( mod );
		R_GXStudioInvalidateBindings();
		return true;
	}

	Con_Reportf( S_WARN "Xash3D GameCube: deferred studio promote failed '%s'\n", path );
	return false;
}

qboolean Mod_GCEnsureLandmarkViewModel( const char *model_path )
{
	qboolean ok;
	model_t *mod;

	/* Landmark Deploy on retail Flipper too — not only -gcnewgame probes. */
	if( !model_path || !model_path[0] )
		return false;

	/* Fast path: already resident + pinned. */
	mod = Mod_FindName( model_path, false );
	if( Mod_GCStudioAlreadyResident( mod ))
	{
		Mod_GCPinViewModel( mod );
		Q_strncpy( gc_landmark_viewmodel, model_path, sizeof( gc_landmark_viewmodel ));
		return true;
	}

	FS_ClearFindMissCache();
	Image_GCPurgeDecodeScratch();
	ok = Mod_GCPromoteStudioPath( model_path );
	if( ok )
		Q_strncpy( gc_landmark_viewmodel, model_path, sizeof( gc_landmark_viewmodel ));
	return ok;
}

/*
=============
Mod_GCRebindPromotedStudios

G371: after deferred NPC promote, point sv.models[] slots at the resident
mesh. SV_SetModel during spawn often left NULL/stub handles, so EmitBrush
saw studio_seen=0 despite real studio loaded.
=============
*/
void Mod_GCRebindPromotedStudios( void )
{
	int i;
	int rebound = 0;

	if( !SV_Active() )
		return;
	for( i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
	{
		const char *name = sv.model_precache[i];
		model_t *mod;
		qboolean is_view = false;

		if( !name || Q_strnicmp( name, "models/", 7 ) || !Q_stristr( name, ".mdl" ))
			continue;
		if( !Mod_GCStudioNameAllowed( name, &is_view ))
			continue;
		mod = Mod_FindName( name, false );
		if( !Mod_GCStudioAlreadyResident( mod ))
			continue;
		if( sv.models[i] == mod )
			continue;
		sv.models[i] = mod;
		rebound++;
	}
	if( rebound > 0 )
	{
		R_GXStudioInvalidateBindings();
		Con_Reportf( "Xash3D GameCube: G371 rebound promoted studios slots=%d\n", rebound );
	}
}

/*
=============
Mod_GCPromoteLeanNpcStudios

G375: DumpFrames often holds EFB before present≥24 (G371 gate). Force the
lean NPC mesh promote so denser stills can capture scientists/barneys.
=============
*/
void Mod_GCPromoteLeanNpcStudios( void )
{
	static const char *promote_lean_npc[] = {
		/* Headcrab is the smallest deferred NPC.  Promote it first while the
		 * post-map heap still has a contiguous read/decode window; loading it
		 * after scientist/barney made the ZIP read fail despite the compact
		 * gc_studio mirror being present. */
		"models/headcrab.mdl",
		"models/scientist.mdl",
		"models/barney.mdl",
		NULL
	};
	int i;

	if( !Mod_GCIsNewGameRoute() && !GC_IsNewGameWorldReady() )
		return;

	FS_ClearFindMissCache();
	Image_GCPurgeDecodeScratch();
	for( i = 0; promote_lean_npc[i]; i++ )
		Mod_GCPromoteStudioPath( promote_lean_npc[i] );
	Con_Reportf( "Xash3D GameCube: G375 dump NPC studios try npc=%d view=%d budget=%s\n",
		gc_real_studio_npc, gc_real_studio_view, Q_memprint( gc_real_studio_bytes ));
	Mod_GCRebindPromotedStudios();
}

/*
=============
Mod_GCTryDeferredStudios

Attempt to load deferred studios after map prep when memory is available.
=============
*/
void Mod_GCTryDeferredStudios( void )
{
	/* Try to load additional deferred studios if budget allows. */
	static const char *promote_full[] = {
		"models/v_9mmhandgun.mdl",
		"models/v_9mmar.mdl",
		"models/v_shotgun.mdl",
		"models/v_357.mdl",
		"models/headcrab.mdl",
		"models/zombie.mdl",
		NULL
	};
	/* Lean: handgun + crowbar at present=1. NPCs at later presents (G371)
	 * once Flipper MEM has coalesced — scientist/barney SetModel alone left
	 * stubs (studio_seen=0). */
	static const char *promote_lean[] = {
		"models/v_9mmhandgun.mdl",
		"models/w_crowbar.mdl",
		NULL
	};
	static const char *promote_lean_npc[] = {
		/* Read the smallest deferred NPC before the larger cast meshes consume
		 * the last contiguous MEM1 archive-read window. */
		"models/headcrab.mdl",
		"models/scientist.mdl",
		"models/barney.mdl",
		NULL
	};
	const char **promote;
	int i;
	unsigned presents;

	if( !Mod_GCIsNewGameRoute() && !GC_IsNewGameWorldReady() )
		return;

	FS_ClearFindMissCache();
	Image_GCPurgeDecodeScratch();
	/* G105: the direct new-game gameplay route needs a real first-person
	 * mesh before the presentation probe can bind cl.viewent.  Crowbar is
	 * deliberately promoted first because it is the guaranteed starter
	 * weapon and is included in the compact gc_studio allowlist.
	 * Lean -gcnewgame (probe 20260807-023917) previously skipped this under
	 * a -gcfullphysics-only gate and only loaded v_9mmhandgun — G506 WEAK. */
	if( Mod_GCIsNewGameRoute() )
	{
		if( Mod_GCEnsureLandmarkViewModel( "models/v_crowbar.mdl" ))
			Con_Reportf( "Xash3D GameCube: G105 landmark viewmodel ready models/v_crowbar.mdl\n" );
		else if( Mod_GCEnsureLandmarkViewModel( "models/v_9mmhandgun.mdl" ))
			Con_Reportf( "Xash3D GameCube: G105 landmark viewmodel ready models/v_9mmhandgun.mdl\n" );
		else
			Con_Reportf( S_WARN "Xash3D GameCube: G105 landmark viewmodel unavailable\n" );
	}

	presents = GC_GetNewGamePresentCount();
	/* Menu New Game loads the cast at precache (Mod_LoadModel). */
	if( GC_MenuRunsServer() && presents >= 24 )
		return;
	if( Mod_GCIsNewGameRoute() && !Sys_CheckParm( "-gcfullphysics" )
		&& presents >= 24 )
	{
		/* G371: post-present NPC meshes — denser AM scientists/barneys. */
		promote = promote_lean_npc;
		for( i = 0; promote[i]; i++ )
			Mod_GCPromoteStudioPath( promote[i] );
		Con_Reportf( "Xash3D GameCube: G371 deferred NPC studios try npc=%d view=%d budget=%s presents=%u\n",
			gc_real_studio_npc, gc_real_studio_view, Q_memprint( gc_real_studio_bytes ),
			presents );
		Mod_GCRebindPromotedStudios();
		Mod_GCUploadDeferredStudioSkins();
		return;
	}

	promote = ( Mod_GCIsNewGameRoute() && !Sys_CheckParm( "-gcfullphysics" ))
		? promote_lean : promote_full;
	for( i = 0; promote[i]; i++ )
		Mod_GCPromoteStudioPath( promote[i] );

	Con_Reportf( "Xash3D GameCube: deferred studios try npc=%d view=%d budget=%s\n",
		gc_real_studio_npc, gc_real_studio_view, Q_memprint( gc_real_studio_bytes ));
	Mod_GCRebindPromotedStudios();
	/* Quality-0 spawn skips companion/view soft skins — upload once tip recovers. */
	Mod_GCUploadDeferredStudioSkins();
}

/*
=============
Mod_GCLoadStartupStudios

Load minimal studios needed for initial map (e.g., player viewweapon).
Runs before map load to avoid late allocation stalls.
=============
*/
void Mod_GCLoadStartupStudios( void )
{
	static const char *promote[] = {
		"models/v_crowbar.mdl",
		"models/v_9mmhandgun.mdl",
		NULL
	};
	int i;

	if( !Mod_GCIsNewGameRoute() && !GC_IsNewGameWorldReady() )
		return;

	FS_ClearFindMissCache();
	Image_GCPurgeDecodeScratch();

	for( i = 0; promote[i]; i++ )
		Mod_GCPromoteStudioPath( promote[i] );

	Con_Reportf( "Xash3D GameCube: startup studios loaded npc=%d view=%d budget=%s\n",
		gc_real_studio_npc, gc_real_studio_view, Q_memprint( gc_real_studio_bytes ));
}

/*
=============
Mod_GCLoadNewGameStudios

Prepare-time attempt (often tip-starved). Prefer Mod_GCTryDeferredStudios after
a few Flipper presents when MEM1 has coalesced (G287).
=============
*/
void Mod_GCLoadNewGameStudios( void )
{
	/* Crowbar first — handgun is 134 KiB and starves the tip at prepare. */
	static const char *promote[] = {
		"models/v_crowbar.mdl",
		"models/roach.mdl",
		NULL
	};
	int i;

	FS_ClearFindMissCache();
	Image_GCPurgeDecodeScratch();

	for( i = 0; promote[i]; i++ )
		Mod_GCPromoteStudioPath( promote[i] );

	Con_Reportf( "Xash3D GameCube: deferred studio done npc=%d view=%d budget=%s\n",
		gc_real_studio_npc, gc_real_studio_view, Q_memprint( gc_real_studio_bytes ));
}

/*
=============
Mod_GCLoadPreEntityStudios

After the world BSP has settled (and FatPVS extras are reclaimed), load the
allowlisted cast into the studio BSS arena so MonsterInit can read eye
positions and bone controllers. Deferring until post-present left c1a1f
hanging inside the second zombie Spawn with a null studiohdr.
=============
*/
void Mod_GCLoadPreEntityStudios( void )
{
	/* c1a1f cast first (proven tip-safe ~0.5 MiB with skins). Heavier
	 * campaign meshes (gman/houndeye/forklift) promote on SetModel demand
	 * so pre-entity does not push studio BSS into the crash cliff. */
#if XASH_DODGC_NETWORK
	/* DoD has no HL1 NPC cast, and these generic pistol meshes are not used by
	 * its rifleman spawn. Defer all player weapons until the class is selected. */
	static const char *promote[] = { NULL };
#else
	/* Lost Coast CapFaces: retail 357 only — HL1 cast starved BSS (~370 KiB)
	 * and left v_357 malloc-fail (probe 131844). */
	static const char *promote_lostcoast[] = {
		"models/v_357.mdl",
		NULL
	};
	static const char *promote[] = {
		"models/v_crowbar.mdl",
		"models/w_crowbar.mdl",
		/* Hallway cast before filler NPCs — scientist/barney soft skins need
		 * BSS-resident meshes before headcrab/filecabinet fill the bump. */
		"models/scientist.mdl",
		"models/barney.mdl",
		"models/roach.mdl",
		"models/headcrab.mdl",
		"models/zombie.mdl",
		"models/filecabinet.mdl",
		NULL
	};
#endif
	int i;
	const char **list;

	/* -gcmap / session memopt — same gate as the call site, explicit so a
	 * stale outer check cannot skip the cast. */
	if( !GC_MapLoadMemoryOpt() && !Sys_CheckParm( "-gcmap" )
		&& !Sys_CheckParm( "-gcnewgame" ))
		return;

	/* Xen maps need this bounded arena for their own actors. Loading the
	 * unrelated intro cast first left c4a1d with only 17 KiB after its alien
	 * controllers and could exhaust it before Nihilanth or Gonarch spawn. */
	if( !Q_strnicmp( sv.name, "c4a", 3 ))
	{
		Con_Reportf( "Xash3D GameCube: pre-entity studios reserved for Xen map %s\n", sv.name );
		return;
	}

	FS_ClearFindMissCache();
	Image_GCPurgeDecodeScratch();
#if XASH_DODGC_NETWORK
	list = promote;
#else
	list = Sys_CheckParm( "-gccapfacessmoke" ) ? promote_lostcoast : promote;
#endif
	/* Lost Coast CapFaces needs textured 357 — mesh-only hung GX studio draw. */
	gc_studio_pre_entity_mesh_only = !Sys_CheckParm( "-gccapfacessmoke" );
	for( i = 0; list[i]; i++ )
		Mod_GCPromoteStudioPath( list[i] );
	gc_studio_pre_entity_mesh_only = false;
	Mod_GCRebindPromotedStudios();
	if( Sys_CheckParm( "-gccapfacessmoke" ))
		Mod_GCUploadDeferredStudioSkins();
	Con_Reportf( "Xash3D GameCube: pre-entity studios npc=%d view=%d budget=%s bump=%s\n",
		gc_real_studio_npc, gc_real_studio_view, Q_memprint( gc_real_studio_bytes ),
		Q_memprint( gc_studio_bss_bump ));
}


static qboolean Mod_GCMapVerboseModelLoad( const char *name )
{
	return false;
}

void Mod_PrintWorldStats_f( void )
{
	Con_Printf( "worldmodel %s: loaded\n", mod_known->name );
}
#endif
CVAR_DEFINE( mod_studiocache, "r_studiocache", "1", FCVAR_ARCHIVE, "enables studio cache for speedup tracing hitboxes" );
CVAR_DEFINE_AUTO( r_wadtextures, "0", FCVAR_LATCH, "completely ignore textures in the bsp-file if enabled" );
CVAR_DEFINE_AUTO( r_showhull, "0", 0, "draw collision hulls 1-3" );
CVAR_DEFINE_AUTO( r_allow_wad3_luma, "0", FCVAR_LATCH|FCVAR_ARCHIVE, "allow usage of luma textures in wad3 (tilde textures)" );

/*
===============================================================================

			MOD COMMON UTILS

===============================================================================
*/
/*
================
Mod_Modellist_f
================
*/
static void Mod_Modellist_f( void )
{
	int	i, nummodels;
	model_t	*mod;

	Con_Printf( "\n" );
	Con_Printf( "-----------------------------------\n" );

	for( i = nummodels = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		const char *color_str;

		if( mod->needload == NL_UNREFERENCED )
			continue; // free slot

		switch( mod->type )
		{
		case mod_alias:
			color_str = S_YELLOW;
			break;
		case mod_studio:
			color_str = S_GREEN;
			break;
		case mod_sprite:
			color_str = S_MAGENTA;
			break;
		case mod_brush:
			color_str = mod->name[0] == '*' ? S_CYAN : S_BLUE;
			break;
		default:
			color_str = S_RED;
			break;
		}

		Con_Printf( "%3d:\t%s%s\n" S_DEFAULT, i, color_str, mod->name );
		nummodels++;
	}

	Con_Printf( "-----------------------------------\n" );
	Con_Printf( "%i total models, %i total allocated slots\n", nummodels, mod_numknown );
	Con_Printf( "\n" );
}

static void Mod_UnloadRenderData( model_t *mod )
{
#if !XASH_DEDICATED
	// loader failed before renderer got a chance to process this model
	if( mod->type == mod_bad )
		return;

	switch( mod->type )
	{
	case mod_sprite:
		Mod_SpriteUnloadTextures( mod->cache.data );
		break;
	default:
		break;
	}

	ref.dllFuncs.Mod_ProcessRenderData( mod, false, NULL, 0 );
#endif
}

/*
================
Mod_FreeUserData
================
*/
static void Mod_FreeUserData( model_t *mod )
{
	// ignore submodels and freed models
	if( mod->needload == NL_UNREFERENCED || mod->name[0] == '*' )
		return;

	if( Host_IsDedicated() )
	{
		if( svgame.physFuncs.Mod_ProcessUserData != NULL )
		{
			// let the server.dll free custom data
			svgame.physFuncs.Mod_ProcessUserData( mod, false, NULL );
		}
	}
	else
	{
		Mod_UnloadRenderData( mod );
	}
}

#if XASH_GAMECUBE
qboolean R_GCIsMapLoadStaticArena( const void *ptr );

static void Mod_FreeLoadBuffer( void *buf )
{
	if( !buf )
		return;
	/* Map-load staging / retained BSP scratch are never Mem_ pool blocks. */
	if( GC_IsMapLoadBuffer( buf ) || R_GCIsMapLoadStaticArena( buf ))
		return;
	if( GC_ReleaseMapLoadBuffer( buf ))
		return;
	if( Mod_GCIsRetainedBspScratch( buf ))
	{
		Mod_GCClearRetainedBspScratch();
		return;
	}
	free( buf );
}

void Mod_ReleaseBrushSourceBuffer( void *buf )
{
	if( !buf )
		return;
	/* The BSP loader calls this while it is about to re-arm the same arena as
	 * scratch.  Drop the borrow without discarding the arena; the old-world
	 * destructor performs the subsequent discard explicitly. */
	if( GC_IsMapLoadBuffer( buf ))
	{
		GC_ReleaseMapLoadBuffer( buf );
		return;
	}
	Mod_FreeLoadBuffer( buf );
	GC_DiscardMapLoadBuffer();
}
#else
#define Mod_FreeLoadBuffer( buf ) Mem_Free( buf )
#endif

/*
================
Mod_FreeModel
================
*/
void Mod_FreeModel( model_t *mod )
{
	// already freed?
	if( !mod || mod->needload == NL_UNREFERENCED )
		return;

#if XASH_GAMECUBE
	/* G156: landmark viewweapons stay resident for Flipper / Deploy rebinds. */
	if( Mod_GCIsPinnedViewModel( mod ))
	{
		Con_Reportf( "Xash3D GameCube: G156 skip free pinned viewmodel %s\n",
			mod->name[0] ? mod->name : "?" );
		mod->needload = NL_PRESENT;
		return;
	}
#endif

	if( mod->type != mod_brush || mod->name[0] != '*' )
	{
		Mod_FreeUserData( mod );
#if XASH_GAMECUBE
		if( mod->type == mod_brush )
		{
			Con_Reportf( "Xash3D GameCube: FreeModel brush userdata done %s tip=%s\n",
				mod->name[0] ? mod->name : "?", Q_memprint( GC_MemHeapFreeBytes() ));
			Mod_GameCubeFreeMallocSurfaces( mod );
			Con_Reportf( "Xash3D GameCube: FreeModel malloc surfaces done %s tip=%s\n",
				mod->name[0] ? mod->name : "?", Q_memprint( GC_MemHeapFreeBytes() ));
		}
		if( mod->type == mod_brush && mod->cache.data )
		{
			/* The world BSP may have retained the map-load arena as renderer
			 * scratch.  This path is model teardown (unlike the staging helper),
			 * so clear that ownership before returning the arena. */
			if( Mod_GCIsRetainedBspScratch( mod->cache.data ))
				Mod_GCClearRetainedBspScratch();
			Mod_ReleaseBrushSourceBuffer( mod->cache.data );
			GC_DiscardMapLoadBuffer();
			mod->cache.data = NULL;
			Con_Reportf( "Xash3D GameCube: FreeModel brush source released %s tip=%s\n",
				mod->name[0] ? mod->name : "?", Q_memprint( GC_MemHeapFreeBytes() ));
		}
		/* Mesh-only New Game studios keep cache on malloc (mempool == 0).
		 * G287 BSS-resident crowbar must not be passed to free(). */
		if( mod->type == mod_studio && !mod->mempool && mod->cache.data )
		{
			if( Mod_GCIsStudioBssCache( mod->cache.data ))
			{
				if( gc_studio_bss_owners > 0 )
					gc_studio_bss_owners--;
				if( gc_studio_bss_owners <= 0 )
				{
					gc_studio_bss_owners = 0;
					gc_studio_bss_bump = 0;
				}
				mod->cache.data = NULL;
			}
			else
			{
				free( mod->cache.data );
				mod->cache.data = NULL;
			}
		}
#endif
#if XASH_GAMECUBE
			/* Retail tram hop tip≈18 KiB (probe 235602): Mem_FreePool walks the
			 * world zone and never returns after lean tex unload. Drop the pool
			 * handle without walking — malloc surfaces already freed above. */
			if( mod->type == mod_brush && FBitSet( mod->flags, MODEL_WORLD )
				&& GC_MemHeapFreeBytes() < 48u * 1024u )
			{
				Con_Reportf( "Xash3D GameCube: FreeModel skip world Mem_FreePool tip=%s\n",
					Q_memprint( GC_MemHeapFreeBytes() ));
				mod->mempool = 0;
			}
			else if( !( mod->type == mod_sprite && mod->mempool == host.mempool ))
				Mem_FreePool( &mod->mempool );
#else
		Mem_FreePool( &mod->mempool );
#endif
	}

	if( mod->type == mod_brush && FBitSet( mod->flags, MODEL_WORLD ) )
	{
		world.version = 0;
		world.shadowdata = NULL;
		world.deluxedata = NULL;

		// data already freed by Mem_FreePool above
		world.hull_models = NULL;
		world.compressed_phs = NULL;
		world.phsofs = NULL;
	}

	memset( mod, 0, sizeof( *mod ));
}

/*
===============================================================================

			MODEL INITIALIZE\SHUTDOWN

===============================================================================
*/
/*
================
Mod_Init
================
*/
void Mod_Init( void )
{
	com_studiocache = Mem_AllocPool( "Studio Cache" );
	if( !com_studiocache )
		return;
	Cvar_RegisterVariable( &mod_studiocache );
	Cvar_RegisterVariable( &r_wadtextures );
	Cvar_RegisterVariable( &r_showhull );
	Cvar_RegisterVariable( &r_allow_wad3_luma );

	Cmd_AddCommand( "mapstats", Mod_PrintWorldStats_f, "show stats for currently loaded map" );
	Cmd_AddCommand( "modellist", Mod_Modellist_f, "display loaded models list" );

	Mod_ResetStudioAPI ();
	Mod_InitStudioHull ();
}

/*
================
Mod_FreeAll
================
*/
void Mod_FreeAll( void )
{
#if !XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
#if XASH_GAMECUBE
	/* Full purge — drop G156 pins so FreeModel can reclaim viewweapons. */
	gc_pinned_viewmodel_count = 0;
	memset( gc_pinned_viewmodels, 0, sizeof( gc_pinned_viewmodels ));
#endif
	for( int i = 0; i < mod_numknown; i++ )
		Mod_FreeModel( &mod_known[i] );
#if XASH_GAMECUBE
	gc_real_studio_npc = 0;
	gc_real_studio_view = 0;
	gc_real_studio_bytes = 0;
	gc_deferred_studio_attempts = 0;
	gc_studio_bss_bump = 0;
	gc_studio_bss_owners = 0;
	gc_landmark_viewmodel[0] = '\0';
#endif
	mod_numknown = 0;
}

/*
================
Mod_ClearUserData
================
*/
void Mod_ClearUserData( void )
{
	for( int i = 0; i < mod_numknown; i++ )
		Mod_FreeUserData( &mod_known[i] );
}

/*
================
Mod_Shutdown
================
*/
void Mod_Shutdown( void )
{
	Mod_FreeAll();
	Mem_FreePool( &com_studiocache );
}

/*
===============================================================================

			MODELS MANAGEMENT

===============================================================================
*/
/*
==================
Mod_FindName

never return NULL
==================
*/
model_t *Mod_FindName( const char *filename, qboolean trackCRC )
{
	char	modname[MAX_QPATH];
	model_t	*mod;
	int	i;

	Q_strncpy( modname, filename, sizeof( modname ));

	// search the currently loaded models
	for( i = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		if( !Q_stricmp( mod->name, modname ))
		{
			/* Mesh-only GC studios keep cache on calloc (mempool==0). Treat
			 * any model with cache.data as already loaded so Mod_ForName does
			 * not downgrade them back to gcmap stubs. */
			if( mod->mempool || mod->cache.data || mod->name[0] == '*' )
				mod->needload = NL_PRESENT;
			else
				mod->needload = NL_NEEDS_LOADED;

			return mod;
		}
	}

	// find a free model slot spot
	for( i = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
	{
		if( mod->needload == NL_UNREFERENCED )
			break; // this is a valid spot
	}

	if( i == mod_numknown )
	{
		if( mod_numknown == MAX_MODELS )
			Host_Error( "MAX_MODELS limit exceeded (%d)\n", MAX_MODELS );
		mod_numknown++;
	}

	// copy name, so model loader can find model file
	Q_strncpy( mod->name, modname, sizeof( mod->name ));
	if( trackCRC ) mod_crcinfo[i].flags = FCRC_SHOULD_CHECKSUM;
	else mod_crcinfo[i].flags = 0;
	mod->needload = NL_NEEDS_LOADED;
	mod_crcinfo[i].initialCRC = 0;

	return mod;
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
static model_t *Mod_LoadModel( model_t *mod, qboolean crash )
{
	char		tempname[MAX_QPATH];
	char		loadname[MAX_QPATH];
	fs_offset_t		length = 0;
	qboolean		loaded, loaded2 = false;

	if( !mod )
	{
		Host_Error( "%s: mod == NULL\n", __func__ );
		return NULL;
	}

	// check if already loaded (or inline bmodel)
	/* Mesh-only New Game studios use calloc cache with mempool==0 — still loaded. */
	if( mod->mempool || mod->cache.data || mod->name[0] == '*' )
	{
		mod->needload = NL_PRESENT;
		return mod;
	}

	if( mod->needload != NL_NEEDS_LOADED )
	{
		Host_Error( "%s: trying to load model not marked for loading (%d)\n", __func__, mod->needload );
		return NULL;
	}

	// store modelname to show error
	Q_strncpy( tempname, mod->name, sizeof( tempname ));
	COM_FixSlashes( tempname );
	Q_strncpy( loadname, tempname, sizeof( loadname ));

#if XASH_GAMECUBE
	if( Mod_GCMapVerboseModelLoad( tempname ))
	{
		Con_Reportf( "Xash3D GameCube: Mod_LoadModel request mod='%s' temp='%s'\n", mod->name, tempname );
	}
#endif

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && !world.loading )
	{
		const char *ext = COM_FileExtension( tempname );

		if( ext && ( !Q_stricmp( ext, "mdl" ) || !Q_stricmp( ext, "spr" )) )
		{
			/* Monster AI reads studiohdr during Spawn (eye position, controllers).
			 * Promote allowlisted meshes into the studio BSS arena now for every
			 * memopt route — not only the retail menu server. */
			qboolean is_view = false;

			if( !Q_stricmp( ext, "mdl" )
				&& Mod_GCStudioNameAllowed( tempname, &is_view )
				&& Mod_GCPromoteStudioPath( tempname ))
				return mod;
			mod->needload = NL_NEEDS_LOADED;
			return mod;
		}
	}
#endif

	byte *buf = NULL;
#if XASH_GAMECUBE
	/* Prefer the early-reserved contiguous buffer for world BSPs so retail New
	 * Game is not blocked by heap fragmentation after menu/client churn.
	 * DoD retail maps keep a large LUMP_LIGHTING (~1 MiB). Loading the full BSP
	 * into the 3.75 MiB arena leaves too little high-slot for msurface blocks
	 * (dod_flash 9801 surfs ≈1.72 MiB → Mem_Alloc fatal with BBA). Always
	 * compact-load without LUMP_LIGHTING when present — G154 reloads samples
	 * from disc into ARAM for CapFaces. Full image only when there is no LM. */
	if( !Q_strnicmp( loadname, "maps/", 5 ))
	{
		fs_offset_t filesize = FS_FileSize( loadname, false );

		if( filesize > (fs_offset_t)sizeof( uint ))
		{
			file_t *f = FS_Open( loadname, "rb", false );
			dheader_t hdr;
			int light_ofs = 0, light_len = 0;
			size_t compact_need = (size_t)filesize;
			qboolean prefer_compact = false;

			if( f && FS_Read( f, &hdr, sizeof( hdr )) == (fs_offset_t)sizeof( hdr ))
			{
				const int ver = LittleLong( hdr.version );

				if( ver == HLBSP_VERSION || ver == Q1BSP_VERSION )
				{
					light_ofs = LittleLong( hdr.lumps[LUMP_LIGHTING].fileofs );
					light_len = LittleLong( hdr.lumps[LUMP_LIGHTING].filelen );
				}
			}

			if( light_len > 0 && light_ofs > 0
				&& (size_t)light_ofs + (size_t)light_len <= (size_t)filesize )
			{
				compact_need = (size_t)filesize - (size_t)light_len;
				prefer_compact = compact_need < (size_t)filesize;
			}

			if( f && prefer_compact
				&& ( buf = GC_BorrowMapLoadBuffer( compact_need )) != NULL )
			{
				size_t out = 0;
				byte chunk[4096];
				fs_offset_t pos = 0;
				qboolean ok = true;
				int i;

				FS_Seek( f, 0, SEEK_SET );
				while( pos < filesize && ok )
				{
					size_t want;
					fs_offset_t got;

					if( light_len > 0 && pos >= light_ofs
						&& pos < (fs_offset_t)( light_ofs + light_len ))
					{
						pos = (fs_offset_t)( light_ofs + light_len );
						FS_Seek( f, pos, SEEK_SET );
						continue;
					}

					want = sizeof( chunk );
					if( light_len > 0 && pos < light_ofs
						&& pos + (fs_offset_t)want > light_ofs )
						want = (size_t)( light_ofs - pos );
					if( pos + (fs_offset_t)want > filesize )
						want = (size_t)( filesize - pos );
					if( out + want > compact_need )
					{
						ok = false;
						break;
					}

					got = FS_Read( f, chunk, want );
					if( got != (fs_offset_t)want )
					{
						ok = false;
						break;
					}
					memcpy( buf + out, chunk, want );
					out += want;
					pos += (fs_offset_t)want;
				}

				if( ok && out == compact_need && out >= sizeof( dheader_t ))
				{
					dheader_t *dh = (dheader_t *)buf;

					dh->lumps[LUMP_LIGHTING].fileofs = 0;
					dh->lumps[LUMP_LIGHTING].filelen = 0;
					for( i = 0; i < HEADER_LUMPS; i++ )
					{
						const int ofs = LittleLong( dh->lumps[i].fileofs );
						const int len = LittleLong( dh->lumps[i].filelen );

						if( i == LUMP_LIGHTING || len <= 0 )
							continue;
						if( ofs > light_ofs )
							dh->lumps[i].fileofs = LittleLong( ofs - light_len );
					}
					length = (fs_offset_t)out;
					Con_Reportf( "Xash3D GameCube: map load compact (no lighting) %s → %s (G154 disc LM)\n",
						Q_memprint( (size_t)filesize ), Q_memprint( out ));
				}
				else
				{
					GC_ReleaseMapLoadBuffer( buf );
					buf = NULL;
					length = 0;
					Con_Reportf( S_ERROR "Xash3D GameCube: compact map load failed need=%s out=%lu\n",
						Q_memprint( compact_need ), (unsigned long)out );
				}
			}
			else if( f && ( buf = GC_BorrowMapLoadBuffer( (size_t)filesize )) != NULL )
			{
				FS_Seek( f, 0, SEEK_SET );
				length = FS_Read( f, buf, filesize );
				if( length == filesize )
				{
					Con_Reportf( "Xash3D GameCube: map load using reserved buffer %s\n",
						Q_memprint( (size_t)filesize ));
				}
				else
				{
					GC_ReleaseMapLoadBuffer( buf );
					buf = NULL;
					length = 0;
				}
			}

			if( f )
				FS_Close( f );
		}
	}
	if( !buf )
#endif
	{
		// Check file exists before attempting to load
		if( !FS_FileExists( loadname, false ))
		{
#if XASH_GAMECUBE
			if( GC_MapLoadMemoryOpt() && ( !Q_strncmp( loadname, "maps", 4 ) || !Q_strncmp( loadname, "models", 6 )))
			{
				fs_offset_t filesize = FS_FileSize( loadname, false );

				Con_Reportf( "Xash3D GameCube: model file missing mod='%s' path='%s' size=%li\n",
					mod->name, loadname, (long)filesize );
			}
#endif
			memset( mod, 0, sizeof( model_t ));

			if( crash ) Host_Error( "Could not load model %s from disk\n", loadname );
			else Con_Printf( S_ERROR "Could not load model %s from disk\n", loadname );

			return NULL;
		}

		buf = FS_LoadFile( loadname, &length, false );
	}

	if( !buf || length < sizeof( uint ))
	{
#if XASH_GAMECUBE
		if( GC_MapLoadMemoryOpt() && ( !Q_strncmp( loadname, "maps", 4 ) || !Q_strncmp( loadname, "models", 6 )))
		{
			fs_offset_t filesize = FS_FileSize( loadname, false );
			qboolean exists = FS_FileExists( loadname, false );

			Con_Reportf( "Xash3D GameCube: model file failed mod='%s' path='%s' exists=%d size=%li length=%li buf=%p\n",
				mod->name, loadname, exists, (long)filesize, (long)length, (void *)buf );
		}
#endif
		memset( mod, 0, sizeof( model_t ));

		if( crash ) Host_Error( "Could not load model %s from disk\n", loadname );
		else Con_Printf( S_ERROR "Could not load model %s from disk\n", loadname );

		return NULL;
	}

	Con_Reportf( "loading %s\n", mod->name );
	mod->needload = NL_PRESENT;
	mod->type = mod_bad;

	// call the apropriate loader
	switch( *(uint *)buf )
	{
	case LittleLong( IDSTUDIOHEADER ):
		Mod_LoadStudioModel( mod, buf, length, &loaded );
		break;
	case LittleLong( IDSPRITEHEADER ):
		Mod_LoadSpriteModel( mod, buf, length, &loaded );
		break;
	case LittleLong( IDALIASHEADER ):
		Mod_LoadAliasModel( mod, buf, &loaded );
		break;
	case LittleLong( Q1BSP_VERSION ):
	case LittleLong( HLBSP_VERSION ):
	case LittleLong( QBSP2_VERSION ):
		Mod_LoadBrushModel( mod, buf, length, &loaded );
		break;
	default:
		Mod_FreeLoadBuffer( buf );
		if( crash ) Host_Error( "%s has unknown format\n", tempname );
		else Con_Printf( S_ERROR "%s has unknown format\n", tempname );
		return NULL;
	}

	if( loaded )
	{
		if( world.loading )
			SetBits( mod->flags, MODEL_WORLD ); // mark worldmodel

		if( Host_IsDedicated() )
		{
			if( svgame.physFuncs.Mod_ProcessUserData != NULL )
			{
				// let the server.dll load custom data
				svgame.physFuncs.Mod_ProcessUserData( mod, true, buf );
			}
			loaded2 = true;
		}
#if !XASH_DEDICATED
		else
		{
			loaded2 = ref.dllFuncs.Mod_ProcessRenderData( mod, true, buf, length );
		}
#endif
	}

	if( mod->type == mod_alias )
	{
		aliashdr_t *hdr = mod->cache.data;
		if( hdr ) // clean up temporary pointer after passing the alias model to the renderer
			hdr->pposeverts = NULL;
	}

	if( !loaded || !loaded2 )
	{
		Mod_FreeModel( mod );
		Mod_FreeLoadBuffer( buf );

		if( crash ) Host_Error( "Could not load model %s\n", tempname );
		else Con_Printf( S_ERROR "Could not load model %s\n", tempname );

		return NULL;
	}

	model_info_t *p = &mod_crcinfo[mod - mod_known];
	mod->needload = NL_PRESENT;

	if( FBitSet( p->flags, FCRC_SHOULD_CHECKSUM ))
	{
		uint32_t currentCRC;

		CRC32_Init( &currentCRC );
		CRC32_ProcessBuffer( &currentCRC, buf, length );
		currentCRC = CRC32_Final( currentCRC );

		if( FBitSet( p->flags, FCRC_CHECKSUM_DONE ))
		{
			if( currentCRC != p->initialCRC )
				Host_Error( "%s has a bad checksum\n", tempname );
		}
		else
		{
			SetBits( p->flags, FCRC_CHECKSUM_DONE );
			p->initialCRC = currentCRC;
		}
	}
#if XASH_GAMECUBE
	if( mod->type != mod_brush || mod->cache.data != buf )
#endif
	{
#if XASH_GAMECUBE
		/* Don't free map load buffer - it's managed by GC system */
		if( !GC_IsMapLoadBuffer( buf ) && !R_GCIsMapLoadStaticArena( buf ))
#endif
			Mod_FreeLoadBuffer( buf );
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName( const char *name, qboolean crash, qboolean trackCRC )
{
	if( COM_StringEmptyOrNULL( name ))
		return NULL;

	model_t *mod = Mod_FindName( name, trackCRC );
	return Mod_LoadModel( mod, crash );
}

/*
==================
Mod_PurgeStudioCache

free studio cache on change level
==================
*/
static void Mod_PurgeStudioCache( void )
{
	// refresh hull data
	SetBits( r_showhull.flags, FCVAR_CHANGED );
#if !XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
#if XASH_GAMECUBE
	/* G156: unpin viewmodels before freeing to avoid dangling references. */
	gc_pinned_viewmodel_count = 0;
	memset( gc_pinned_viewmodels, 0, sizeof( gc_pinned_viewmodels ));
#endif
	// release previois map
	Mod_FreeModel( mod_known );	// world is stuck on slot #0 always
#if XASH_GAMECUBE
	/* The world source may be retained as BSP scratch without being present in
	 * cache.data on every load route.  At this point the old model is fully
	 * detached, so force the arena ownership transition before the next map's
	 * FS_LoadFile attempt. */
	Mod_GCClearRetainedBspScratch();
	GC_DiscardMapLoadBuffer();
	Con_Reportf( "Xash3D GameCube: changelevel old world map-load arena released\n" );
#endif

	// we should release all the world submodels
	// and clear studio sequences
	for( int i = 1; i < mod_numknown; i++ )
	{
		if( mod_known[i].needload == NL_UNREFERENCED )
			continue;

		if( mod_known[i].type == mod_studio )
			mod_known[i].submodels = NULL;

		if( mod_known[i].name[0] == '*' )
			Mod_FreeModel( &mod_known[i] );
		else
			mod_known[i].needload = NL_FREE_UNUSED;
	}

	Mem_EmptyPool( com_studiocache );
	Mod_ClearStudioCache();
}

/* Purge the old world before a caller reserves the next BSP buffer. */
void Mod_PurgeForMapLoad( void )
{
	Mod_PurgeStudioCache();
}

/*
==================
Mod_LoadWorld

Loads in the map and all submodels
==================
*/
model_t *Mod_LoadWorld( const char *name, qboolean preload )
{
	// already loaded?
	if( !Q_stricmp( mod_known->name, name ))
		return mod_known;

#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: Mod_LoadWorld begin name=%s preload=%d\n",
		name ? name : "?", preload ? 1 : 0 );
#endif
	// free sequence files on studiomodels
	Mod_PurgeStudioCache();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: Mod_LoadWorld after purge name=%s\n",
		name ? name : "?" );
#endif

	// load the newmap
	world.loading = true;
	model_t *pworld = Mod_FindName( name, false );
	if( preload ) Mod_LoadModel( pworld, true );
#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() )
	{
		fs_offset_t filesize = FS_FileSize( name, false );
		qboolean exists = FS_FileExists( name, false );

		Con_Reportf( "Xash3D GameCube: Mod_LoadWorld request='%s' registered='%s' exists=%d size=%li preload=%d\n",
			name, pworld ? pworld->name : "(null)", exists, (long)filesize, preload );
	}
	Con_Reportf( "Xash3D GameCube: Mod_LoadWorld ready name=%s\n",
		name ? name : "?" );
#endif
	world.loading = false;

	if( pworld != mod_known )
	{
		Host_Error( "%s: soft assert world model registration mismatch (%p != %p)\n",
			__func__, (void *)pworld, (void *)mod_known );
		return NULL;
	}

	return pworld;
}

/*
==================
Mod_FreeUnused

Purge all unused models
==================
*/
void Mod_FreeUnused( void )
{
	model_t	*mod;
	int	i;

	// never tries to release worldmodel
	for( i = 1, mod = &mod_known[1]; i < mod_numknown; i++, mod++ )
	{
		if( mod->needload == NL_FREE_UNUSED )
			Mod_FreeModel( mod );
	}
}

#if XASH_GAMECUBE
/*
================
Mod_GCReleaseStudioArena

Menu route, before a map loads: the studio arena is a bump allocator that
only rewinds when every model in it is gone. Models that stayed (the pinned
view models) kept it from ever rewinding while the others were reloaded on
top, and the intro ride ended at 1020 of 1024 KB. Release all of them here;
the new map loads its own set during the LOADING hold.
================
*/
void Mod_GCReleaseStudioArena( void )
{
	const size_t before = gc_studio_bss_bump;
	model_t *mod;
	int i, freed = 0;

	if( !GC_MenuRunsServer( ))
		return;
	gc_pinned_viewmodel_count = 0;
	memset( gc_pinned_viewmodels, 0, sizeof( gc_pinned_viewmodels ));
	for( i = 1, mod = &mod_known[1]; i < mod_numknown; i++, mod++ )
	{
		if( mod->needload != NL_UNREFERENCED && mod->type == mod_studio
			&& mod->cache.data && Mod_GCIsStudioBssCache( mod->cache.data ))
		{
			/* Mod_FreeModel → UnloadRenderData hangs on the first mid-tram
			 * BSS studio (v_crowbar, probe 20261105-053602) even with
			 * texture-table bounds checks. Abandon the BSS payload and keep
			 * the mod slot (name/type) so the next map can reload it;
			 * memset'ing the slot left c0a0a silent after MAP_READY. */
			Con_Reportf( "Xash3D GameCube: studio arena abandon '%s'\n",
				mod->name[0] ? mod->name : "?" );
			mod->submodels = NULL;
			mod->cache.data = NULL;
			if( mod->mempool )
				Mem_FreePool( &mod->mempool );
			mod->needload = NL_UNREFERENCED;
			freed++;
		}
	}
	gc_real_studio_npc = 0;
	gc_real_studio_view = 0;
	gc_real_studio_bytes = 0;
	gc_deferred_studio_attempts = 0;
	gc_studio_bss_owners = 0;
	gc_studio_bss_bump = 0;
	/* The animation slots are in the arena too: a slot kept across this reset
	 * wrote the next animation file over the new map's skins. Two are placed
	 * first, so a full arena still has animations; the others come on demand. */
	memset( gc_seqgroup_slot, 0, sizeof( gc_seqgroup_slot ));
	gc_seqgroup_slot[0].buf = Mod_GCStudioBssAlloc( GC_SEQGROUP_SLOT_SIZE );
	gc_seqgroup_slot[1].buf = Mod_GCStudioBssAlloc( GC_SEQGROUP_SLOT_SIZE );
	Con_Reportf( "Xash3D GameCube: studio arena released models=%d bytes=%u\n", freed, (uint)before );
	GC_AramReport( "studio-release" );
}

void Mod_GcmapMarkPrecacheFreeable( void )
{
	model_t *mod;
	int i, marked;

	if( !GC_MapLoadMemoryOpt())
		return;

	marked = 0;
	for( i = 1, mod = &mod_known[1]; i < mod_numknown; i++, mod++ )
	{
		if( mod->needload == NL_UNREFERENCED )
			continue;
		if( mod->name[0] == '*' )
			continue;
		/* Never a world BSP: after a changelevel the current level need not
		 * sit in slot 0, and freeing it unpinned its live surfaces. */
		if( mod->type == mod_brush && !Q_strnicmp( mod->name, "maps/", 5 ))
			continue;
		if( mod->needload == NL_PRESENT )
		{
			/* Keep New Game deferred real studio meshes for world present.
			 * Menu New Game: the intro cast is resident in the studio arena
			 * and referenced by both sv.models and cl.models; freeing it let
			 * a later promote reuse the slot for another model. */
			if(( Sys_CheckParm( "-gcnewgame" ) || GC_MenuRunsServer()) && mod->type == mod_studio )
			{
				studiohdr_t *hdr = (studiohdr_t *)mod->cache.data;
				if( hdr && hdr->numbodyparts > 0 )
					continue;
			}
			mod->needload = NL_FREE_UNUSED;
			marked++;
		}
	}

	Mem_EmptyPool( com_studiocache );
	Mod_ClearStudioCache();
	Mod_FreeUnused();
	Con_Reportf( "Xash3D GameCube: gcmap released %d precache models for world render\n", marked );
}
#endif

/*
===============================================================================

			MODEL ROUTINES

===============================================================================
*/
/*
===============
Mod_Calloc

===============
*/
void *GAME_EXPORT Mod_Calloc( int number, size_t size )
{
	if( number <= 0 || size <= 0 )
		return NULL;

	cache_user_t *cu = (cache_user_t *)Mem_Calloc( com_studiocache, sizeof( cache_user_t ) + number * size );
	cu->data = (void *)cu; // make sure that cu->data is not NULL

	return cu;
}

/*
===============
Mod_CacheCheck

===============
*/
void *GAME_EXPORT Mod_CacheCheck( cache_user_t *c )
{
	if( !c->data )
		return NULL;

#if XASH_GAMECUBE
	/* Sequence groups loaded into the studio arena stay resident until
	 * their slot is reused (which clears c->data). */
	if( Mod_GCIsStudioBssCache( c->data ))
	{
		Mod_GCSeqGroupTouch( c->data );
		return c->data;
	}
#endif
	if( !Mem_IsAllocatedExt( com_studiocache, c->data ))
		return NULL;

	return c->data;
}

/*
===============
Mod_LoadCacheFile

===============
*/
void GAME_EXPORT Mod_LoadCacheFile( const char *filename, cache_user_t *cu )
{
	char	modname[MAX_QPATH];
	fs_offset_t	size;

	Assert( cu != NULL );

	if( COM_StringEmptyOrNULL( filename ))
		return;

	Q_strncpy( modname, filename, sizeof( modname ));
	COM_FixSlashes( modname );

	byte *buf = NULL;
#if XASH_GAMECUBE
	/* Menu New Game: the client studio renderer's sequence groups go to
	 * the studio arena like R_StudioGetAnim's (one copy, no heap). */
	if( GC_MenuRunsServer() && ( buf = Mod_GCSeqGroupLoad( modname, cu, &size )) != NULL )
		cu->data = buf;
	else
#endif
	{
	buf = FS_LoadFile( modname, &size, false );
	if( !buf || !size ) Host_Error( "LoadCacheFile: ^1can't load %s^7\n", filename );
	cu->data = Mem_Malloc( com_studiocache, size );
	memcpy( cu->data, buf, size );
	Mem_Free( buf );
	}

	// this handles when studio model renderer tries to load sequence files on it's own
	// which is what they always do in HLSDK
#if XASH_BIG_ENDIAN
	if( size >= sizeof( int ) && LittleLong( IDSEQGRPHEADER ) == *(uint *)cu->data )
	{
		studiohdr_t *phdr = (studiohdr_t *)REF_GET_PARM( PARM_GET_STUDIO_HDR, 0 );
		if( !phdr )
			return;

		mstudioseqdesc_t *pseq = (mstudioseqdesc_t *)((byte *)phdr + phdr->seqindex );

		for( int i = 0; i < phdr->numseq; i++ )
		{
			if( pseq[i].seqgroup == 0 )
				continue;

			mstudioseqgroup_t *pgrp = (mstudioseqgroup_t *)((byte *)phdr + phdr->seqgroupindex ) + pseq[i].seqgroup;

			// assuming filename passes seqgroup's name
			if( !Q_stricmp( pgrp->name, filename ))
				Mod_SwapStudioSeqGroupAnims( phdr, &pseq[i], (byte *)cu->data );
		}
	}
#endif
}

/*
==================
Mod_ValidateCRC

==================
*/
qboolean Mod_ValidateCRC( const char *name, uint32_t crc )
{
	model_t *mod = Mod_FindName( name, true );
	model_info_t *p = &mod_crcinfo[mod - mod_known];

	if( !FBitSet( p->flags, FCRC_CHECKSUM_DONE ))
		return true;
	if( p->initialCRC == crc )
		return true;
	return false;
}

/*
==================
Mod_NeedCRC

==================
*/
void Mod_NeedCRC( const char *name, qboolean needCRC )
{
	model_t *mod = Mod_FindName( name, true );
	model_info_t *p = &mod_crcinfo[mod - mod_known];

	if( needCRC ) SetBits( p->flags, FCRC_SHOULD_CHECKSUM );
	else ClearBits( p->flags, FCRC_SHOULD_CHECKSUM );
}

#if XASH_LLVM_LIBFUZZER

static const uint8_t *fuzz_data;
static size_t fuzz_size;

static byte *Fuzz_LoadFile( const char *path, fs_offset_t *filesizeptr, qboolean gamedironly )
{
	byte *buf = Mem_Malloc( host.mempool, fuzz_size );
	memcpy( buf, fuzz_data, fuzz_size );
	*filesizeptr = fuzz_size;
	return buf;
}

int EXPORT Fuzz_Mod_LoadModel( const uint8_t *Data, size_t Size );
int EXPORT Fuzz_Mod_LoadModel( const uint8_t *Data, size_t Size )
{
	model_t mod = { .name = "test", .needload = NL_NEEDS_LOADED };

	Memory_Init();

	host.type = HOST_DEDICATED;
	host.mempool = Mem_AllocPool( "fuzzing pool" );
	fuzz_data = Data;
	fuzz_size = Size;
	refState.draw_surfaces = NULL;

	g_fsapi.LoadFile = Fuzz_LoadFile;

	if( Mod_LoadModel( &mod, false ) && mod.mempool )
		Mem_FreePool( &mod.mempool );

	Mem_FreePool( &host.mempool );

	return 0;
}

#endif // XASH_LLVM_LIBFUZZER
