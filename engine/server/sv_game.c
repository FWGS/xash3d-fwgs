/*
sv_game.c - gamedll interaction
Copyright (C) 2008 Uncle Mike

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
#include "server.h"
#include "net_encode.h"
#include "event_flags.h"
#include "library.h"
#include "pm_defs.h"
#include "studio.h"
#include "const.h"
#include "render_api.h"	// modelstate_t
#include "ref_common.h" // decals
#if XASH_GAMECUBE
#include <ogc/system.h> /* SYS_Report — entity-spawn complete must survive EXI flood */
#include "imagelib.h"
#include "platform.h"
#include "gamecube/mem_gamecube.h"
#include "gamecube/dll_gamecube.h"
void CL_GCRelinkEventHooks( void );
void Image_GCPurgeDecodeScratch( void );
void R_GCTrimTextureMips( void );
void R_GXReleaseHudPoolForMapLoad( void );
#endif

// GameAPI functions declarations
static int GAME_EXPORT pfnModelIndex( const char *m );

// fatpvs stuff
static byte fatphs[(MAX_MAP_LEAFS+7)/8];
static byte clientpvs[(MAX_MAP_LEAFS+7)/8];	// for find client in PVS

#if XASH_GAMECUBE
#include <malloc.h>
/* SP edict / baseline / static tables — tip Mem_Calloc of ~707 KiB fatals
 * at SV_LoadProgs before map (HW 20261106: map=NONE route=disc,
 * sv_game.c:6540). Same pattern as gc_singleplayer_packet_entities. */
/* Valve GI max_edicts is 900; sizing BSS for 1200 wasted ~335 KiB of MEM1
 * after SP BSS edicts (end-triple c5a1 leaf tip-OOM 20261106-090852). */
#ifndef GC_SP_EDICT_CAP
#define GC_SP_EDICT_CAP 900
#endif
static edict_t gc_singleplayer_edicts[GC_SP_EDICT_CAP]
	__attribute__((aligned( 32 )));
static entity_state_t gc_singleplayer_baselines[GC_SP_EDICT_CAP]
	__attribute__((aligned( 32 )));
static entity_state_t gc_singleplayer_static_entities[MAX_STATIC_ENTITIES]
	__attribute__((aligned( 32 )));
static qboolean gc_using_static_edicts;

static qboolean SV_IsStaticEdicts( const edict_t *e )
{
	return e == gc_singleplayer_edicts;
}

static qboolean SV_IsStaticBaselines( const entity_state_t *b )
{
	return b == gc_singleplayer_baselines;
}

static qboolean SV_IsStaticStaticEntities( const entity_state_t *s )
{
	return s == gc_singleplayer_static_entities;
}

/* Tip Server Strings pool dies on denser Office maps (c1a1f SV_AllocString
 * 14-byte Host_Error, hardware 20261104). Deduped bump arena keeps class/
 * target/model names off the tip after reclaim fails. */
#define GC_STRING_ARENA_SIZE (96 * 1024)
/* Heap block taken once at the first map load (SV_GCEnsureStringArena), not
 * BSS: boot, the startup video and the menu never allocate server strings. */
static char *gc_string_arena;
static size_t gc_string_arena_used;
static int gc_string_arena_overflows;
static qboolean gc_string_arena_in_spill;

void SV_GCDropSpillStringArena( void )
{
	if( !gc_string_arena || !gc_string_arena_in_spill )
		return;
	gc_string_arena = NULL;
	gc_string_arena_used = 0;
	gc_string_arena_in_spill = false;
	Con_Reportf( "Xash3D GameCube: server string arena spill dropped for map reload\n" );
}

qboolean SV_GCEnsureStringArena( void )
{
	if( gc_string_arena )
		return true;
	/* Retried at every map load until it fits. */
	gc_string_arena = (char *)memalign( 16, GC_STRING_ARENA_SIZE );
	if( gc_string_arena )
	{
		gc_string_arena_in_spill = false;
		Con_Reportf( "Xash3D GameCube: server string arena ready %s\n",
			Q_memprint( GC_STRING_ARENA_SIZE ));
		return true;
	}
	/* Tip MEM1 too thin at boot (listen flash/donner). Park at spill BASE and
	 * pin so ClearRetained cannot let surfedges stomp it (avalanche Guru).
	 * Do NOT append after faces reclaim — that ate donner leafs room
	 * (spill left 195 < mleaf 242, tip-donner-tip128). */
	{
		extern size_t Mod_GCWorldArraySpillUsed( void );
		extern void *Mod_GCWorldArraySpillTryAlloc( size_t bytes, const char *what );
		extern void Mod_GCWorldArraySpillPinBase( const void *ptr, size_t bytes );

		/* Only claim the base slot (used==pin, typically 0 at boot). */
		if( Mod_GCWorldArraySpillUsed() == 0 )
		{
			gc_string_arena = (char *)Mod_GCWorldArraySpillTryAlloc( GC_STRING_ARENA_SIZE,
				"string arena" );
			if( gc_string_arena )
			{
				Mod_GCWorldArraySpillPinBase( gc_string_arena, GC_STRING_ARENA_SIZE );
				gc_string_arena_used = 0;
				gc_string_arena_in_spill = true;
				Con_Reportf( "Xash3D GameCube: server string arena spill %s\n",
					Q_memprint( GC_STRING_ARENA_SIZE ));
				return true;
			}
		}
	}
	Con_Reportf( S_WARN "Xash3D GameCube: server string arena unavailable (%s)\n",
		Q_memprint( GC_STRING_ARENA_SIZE ));
	return false;
}

static char *GC_StringArenaAlloc( const char *value, uint len )
{
	size_t i;
	char *dst;

	if( !gc_string_arena || !value || len == 0 || len > GC_STRING_ARENA_SIZE )
		return NULL;
	/* Dedup scan — classnames/model paths dominate AllocString traffic. */
	for( i = 0; i + len <= gc_string_arena_used; )
	{
		const char *s = &gc_string_arena[i];
		size_t slen = Q_strlen( s ) + 1;

		if( slen == len && !memcmp( s, value, len ))
			return (char *)s;
		i += slen ? slen : 1;
	}
	if( gc_string_arena_used + len > GC_STRING_ARENA_SIZE )
	{
		/* Wrap like the 64-bit str64 pool — prefer live names over fatal. */
		gc_string_arena_used = 0;
		gc_string_arena_overflows++;
		Con_Reportf( "Xash3D GameCube: string arena wrap overflows=%d need=%u\n",
			gc_string_arena_overflows, len );
	}
	dst = &gc_string_arena[gc_string_arena_used];
	memcpy( dst, value, len );
	gc_string_arena_used += len;
	return dst;
}

/* Player CBasePlayer private block when MEM1 malloc fails during -gcnewgame. */
static byte gc_player_private_fallback[4096];
/* G329: CSoundEnt (~2176) is the first large non-player private block
 * (typically edict 2). Cold New Game into denser AM maps (c1a0d) tips
 * libc/MEM1 after G326 keep-client + BSP textures; soft-failing here
 * leaves GetClassPtr on NULL and aborts before entity spawn. */
static byte gc_soundent_private_fallback[4096];
static qboolean gc_soundent_fallback_used;
/* G330: bounded bump slab for map-private blocks. Keep static BSS ≤108 KiB —
 * growing it to 192 KiB OOM'd Zone Engine before c1a1f BSP open. Larger
 * (>1 KiB) privates go to map-arena overflow (GC_ENTITY_OVERFLOW_BYTES). */
/* Keep BSS modest — 160 KiB starved tip and hung c2a5e mid-spawn.
 * 108 KiB + tip-fragment leafs seize left c2a5f slab exhausted at
 * monster_alien_grunt (768) with arena overflow skipped (tail=0).
 * 128 KiB covers that dense ST spawn without re-entering the 160 hang. */
static byte gc_ent_priv_slab[131072] __attribute__((aligned(16)));
/* One bit per aligned block allows arbitrary entity destruction to reclaim
 * space without allocating heap headers or moving live game-DLL objects. */
static byte gc_ent_priv_slab_bits[sizeof( gc_ent_priv_slab ) / 16 / 8];
static size_t gc_ent_priv_slab_used;
static int gc_ent_priv_slab_hits;
static uint16_t gc_ent_priv_slab_size[MAX_EDICTS];
static byte *gc_ent_priv_overflow;
static qboolean gc_ent_priv_overflow_from_tip;
static byte gc_ent_priv_overflow_bits[GC_ENTITY_OVERFLOW_BYTES / 16 / 8];
static size_t gc_ent_priv_overflow_capacity;
static size_t gc_ent_priv_overflow_used;

/* Borrow only the unused tail of this world's retained map arena. The server
 * releases it after all entity private-data destructors have run. */
void SV_GCReservePrivateOverflow( size_t after, size_t bytes )
{
	if( gc_ent_priv_overflow )
		return;
	if( bytes > GC_ENTITY_OVERFLOW_BYTES ) bytes = GC_ENTITY_OVERFLOW_BYTES;
	bytes &= ~(size_t)31;
	if( !bytes ) return;
	gc_ent_priv_overflow_from_tip = false;
	gc_ent_priv_overflow = GC_MapLoadBufferTailAlloc( after, bytes );
	if( !gc_ent_priv_overflow )
	{
		/* c1a2 / c2a5f: arena mid-tail can be ~0 with leafs tip-parked.
		 * Shrink tip reservation (64→16 KiB) with a modest free-heap guard so
		 * dense monster privates (headcrab 688) still allocate when 64+48 KiB
		 * is unavailable (Office Complex overflow-skip at tip~slab-full). */
		size_t try_bytes = bytes;

		Image_GCPurgeDecodeScratch();
		while( try_bytes >= 16u * 1024u && !gc_ent_priv_overflow )
		{
			const size_t guard = 16u * 1024u;

			if( GC_MemHeapFreeBytes() >= try_bytes + guard )
			{
				gc_ent_priv_overflow = (byte *)memalign( 32, try_bytes );
				if( !gc_ent_priv_overflow )
					gc_ent_priv_overflow = (byte *)malloc( try_bytes );
				if( gc_ent_priv_overflow )
				{
					bytes = try_bytes;
					gc_ent_priv_overflow_from_tip = true;
					Con_Reportf( "Xash3D GameCube: entity overflow reserved %u bytes from tip\n",
						(unsigned)bytes );
					break;
				}
			}
			try_bytes >>= 1;
		}
	}
	if( gc_ent_priv_overflow )
	{
		gc_ent_priv_overflow_capacity = bytes;
		memset( gc_ent_priv_overflow_bits, 0, sizeof( gc_ent_priv_overflow_bits ));
		gc_ent_priv_overflow_used = 0;
		if( !gc_ent_priv_overflow_from_tip )
			Con_Reportf( "Xash3D GameCube: entity overflow reserved %u bytes in map arena\n",
				(unsigned)bytes );
	}
}

qboolean SV_GCPrivateOverflowReserved( void )
{
	return gc_ent_priv_overflow != NULL;
}

static qboolean SV_GCPrivateDataIsOverflow( const void *ptr )
{
	const uintptr_t p = (uintptr_t)ptr;
	const uintptr_t base = (uintptr_t)gc_ent_priv_overflow;
	return gc_ent_priv_overflow && p >= base && p - base < gc_ent_priv_overflow_capacity;
}

static void *SV_GCPrivateOverflowAlloc( size_t size )
{
	size_t i, start = 0, run = 0;
	const size_t blocks = gc_ent_priv_overflow_capacity / 16;
	const size_t need = ( size + 15 ) / 16;
	if( !gc_ent_priv_overflow || !need || need > blocks )
		return NULL;
	for( i = 0; i < blocks; i++ )
	{
		if( gc_ent_priv_overflow_bits[i / 8] & ( 1u << ( i & 7 )))
		{
			run = 0;
			continue;
		}
		if( !run ) start = i;
		if( ++run == need )
		{
			for( i = start; i < start + need; i++ )
				gc_ent_priv_overflow_bits[i / 8] |= 1u << ( i & 7 );
			gc_ent_priv_overflow_used += need * 16;
			return gc_ent_priv_overflow + start * 16;
		}
	}
	return NULL;
}

static void SV_GCPrivateOverflowFree( void *ptr, size_t size )
{
	const size_t start = ((byte *)ptr - gc_ent_priv_overflow ) / 16;
	const size_t need = ( size + 15 ) / 16;
	size_t i;
	for( i = start; i < start + need; i++ )
		gc_ent_priv_overflow_bits[i / 8] &= ~( 1u << ( i & 7 ));
	gc_ent_priv_overflow_used -= need * 16;
}

static void *SV_GCPrivateSlabAlloc( size_t size )
{
	size_t start = 0, run = 0, i;
	const size_t blocks = sizeof( gc_ent_priv_slab ) / 16;
	const size_t need = ( size + 15 ) / 16;
	if( !need || need > blocks )
		return NULL;
	for( i = 0; i < blocks; i++ )
	{
		if( gc_ent_priv_slab_bits[i / 8] & ( 1u << ( i & 7 )))
		{
			run = 0;
			continue;
		}
		if( !run ) start = i;
		if( ++run == need )
		{
			for( i = start; i < start + need; i++ )
				gc_ent_priv_slab_bits[i / 8] |= 1u << ( i & 7 );
			gc_ent_priv_slab_used += need * 16;
			return gc_ent_priv_slab + start * 16;
		}
	}
	return NULL;
}

static void SV_GCPrivateSlabFree( void *ptr, size_t size )
{
	const size_t start = ((byte *)ptr - gc_ent_priv_slab ) / 16;
	const size_t need = ( size + 15 ) / 16;
	size_t i;
	for( i = start; i < start + need; i++ )
		gc_ent_priv_slab_bits[i / 8] &= ~( 1u << ( i & 7 ));
	gc_ent_priv_slab_used -= need * 16;
}
/* Entity key/value copies are short-lived during spawn (rewound per ParseEdict).
 * 8 KiB filled mid-flash (entity≈175) — tip-flash-fix7. 32 KiB left
 * host_main_loop unclaimed=0 and hung CHud extended cvars (tip-donner-fix10).
 * 16 KiB covers dense ents; changelevel entities BSS stays 0 for tip. */
static byte gc_kv_slab[16384];
static size_t gc_kv_slab_used;

static qboolean SV_GCPrivateDataIsSlab( const void *ptr )
{
	const uintptr_t p = (uintptr_t)ptr;
	const uintptr_t base = (uintptr_t)gc_ent_priv_slab;
	return p >= base && p - base < sizeof( gc_ent_priv_slab );
}

static char *SV_GCCopyKeyValue( const char *s )
{
	size_t size;
	char *dst;

	if( !s )
		return NULL;
	size = Q_strlen( s ) + 1;
	/* Prefer the per-entity slab first. copystring → Q_malloc can stall
	 * indefinitely on a fragmented tip MEM1 heap during dense spawn
	 * (c2a5f hung at entity 716 func_wall with heap_free already ~8 KiB). */
	if( gc_kv_slab_used + size <= sizeof( gc_kv_slab ))
	{
		dst = (char *)( gc_kv_slab + gc_kv_slab_used );
		gc_kv_slab_used += size;
		memcpy( dst, s, size );
		return dst;
	}
	/* Never fall through to copystring under tip — it can hang rather than
	 * return NULL. Truncate into the remaining slab or drop the value. */
	{
		const size_t left = sizeof( gc_kv_slab ) - gc_kv_slab_used;

		Con_Reportf( "Xash3D GameCube: kv slab full need=%u left=%u — truncating\n",
			(unsigned)size, (unsigned)left );
		if( left >= 2 )
		{
			dst = (char *)( gc_kv_slab + gc_kv_slab_used );
			memcpy( dst, s, left - 1 );
			dst[left - 1] = '\0';
			gc_kv_slab_used = sizeof( gc_kv_slab );
			return dst;
		}
	}
	return "";
}

static qboolean SV_GCKeyValueIsSlab( const void *ptr )
{
	const uintptr_t p = (uintptr_t)ptr;
	const uintptr_t base = (uintptr_t)gc_kv_slab;
	return p >= base && p - base < sizeof( gc_kv_slab );
}
#else
#define SV_GCCopyKeyValue( s ) copystring( s )
#endif

// exports
#if XASH_WIN32
typedef void (__cdecl *LINK_ENTITY_FUNC)( entvars_t *pev );
#else
typedef void (*LINK_ENTITY_FUNC)( entvars_t *pev );
#endif
typedef void (DLLEXPORT *GIVEFNPTRSTODLL)( enginefuncs_t* engfuncs, globalvars_t *pGlobals );

static edict_t *SV_PEntityOfEntIndex( const int iEntIndex, const qboolean allentities )
{
	if( iEntIndex >= 0 && iEntIndex < GI->max_edicts )
	{
		edict_t *pEdict = SV_EdictNum( iEntIndex );
		qboolean player = allentities ? iEntIndex <= svs.maxclients : iEntIndex < svs.maxclients;

		if( !iEntIndex || FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
			return pEdict; // just get access to array

		if( SV_IsValidEdict( pEdict ) && pEdict->pvPrivateData )
			return pEdict;

		// g-cont: world and clients can be accessed even without private data
		if( SV_IsValidEdict( pEdict ) && player )
			return pEdict;
	}

	return NULL;
}


/*
=============
EntvarsDescription

entavrs table for FindEntityByString
=============
*/
static const TYPEDESCRIPTION gEntvarsDescription[] =
{
	DEFINE_ENTITY_FIELD( classname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( globalname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( model, FIELD_MODELNAME ),
	DEFINE_ENTITY_FIELD( viewmodel, FIELD_MODELNAME ),
	DEFINE_ENTITY_FIELD( weaponmodel, FIELD_MODELNAME ),
	DEFINE_ENTITY_FIELD( target, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( targetname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( netname, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( message, FIELD_STRING ),
	DEFINE_ENTITY_FIELD( noise, FIELD_SOUNDNAME ),
	DEFINE_ENTITY_FIELD( noise1, FIELD_SOUNDNAME ),
	DEFINE_ENTITY_FIELD( noise2, FIELD_SOUNDNAME ),
	DEFINE_ENTITY_FIELD( noise3, FIELD_SOUNDNAME ),
};

/*
=============
SV_SysError

tell the game.dll about system error
=============
*/
void SV_SysError( const char *error_string )
{
	Log_Printf( "FATAL ERROR (shutting down): %s\n", error_string );

	if( svgame.hInstance != NULL )
		svgame.dllFuncs.pfnSys_Error( error_string );
}

/*
=============
SV_Serverinfo

get server infostring
=============
*/
char *SV_Serverinfo( void )
{
	return svs.serverinfo;
}

/*
=============
SV_AngleMod

do modulo on entity angles
=============
*/
static float SV_AngleMod( float ideal, float current, float speed )
{
	float	move;

	current = anglemod( current );

	if( current == ideal ) // already there?
		return current;

	move = ideal - current;

	if( ideal > current )
	{
		if( move >= 180 )
			move = move - 360;
	}
	else
	{
		if( move <= -180 )
			move = move + 360;
	}

	if( move > 0 )
	{
		if( move > speed )
			move = speed;
	}
	else
	{
		if( move < -speed )
			move = -speed;
	}

	return anglemod( current + move );
}

/*
=============
SV_SetMinMaxSize

update entity bounds, relink into world
=============
*/
void SV_SetMinMaxSize( edict_t *e, const float *mins, const float *maxs, qboolean relink )
{
	if( !SV_IsValidEdict( e ))
		return;

	for( int i = 0; i < 3; i++ )
	{
		if( mins[i] > maxs[i] )
		{
			Con_Printf( S_ERROR "%s[%i] has backwards mins/maxs\n", SV_ClassName( e ), NUM_FOR_EDICT( e ));
			if( relink ) SV_LinkEdict( e, false ); // just relink edict and exit
			return;
		}
	}

	VectorCopy( mins, e->v.mins );
	VectorCopy( maxs, e->v.maxs );
	VectorSubtract( maxs, mins, e->v.size );
	if( relink ) SV_LinkEdict( e, false );
}

/*
=============
SV_CopyTraceToGlobal

each trace will share their result into global state
=============
*/
void SV_CopyTraceToGlobal( trace_t *trace )
{
	svgame.globals->trace_allsolid = trace->allsolid;
	svgame.globals->trace_startsolid = trace->startsolid;
	svgame.globals->trace_fraction = trace->fraction;
	svgame.globals->trace_plane_dist = trace->plane.dist;
	svgame.globals->trace_inopen = trace->inopen;
	svgame.globals->trace_inwater = trace->inwater;
	VectorCopy( trace->endpos, svgame.globals->trace_endpos );
	VectorCopy( trace->plane.normal, svgame.globals->trace_plane_normal );
	svgame.globals->trace_hitgroup = trace->hitgroup;
	svgame.globals->trace_flags = 0; // g-cont: always reset config flags when trace is finished

	if( SV_IsValidEdict( trace->ent ))
		svgame.globals->trace_ent = trace->ent;
	else svgame.globals->trace_ent = svgame.edicts;
}

/*
==============
SV_SetModel
==============
*/
void GAME_EXPORT SV_SetModel( edict_t *ent, const char *modelname )
{
	char	name[MAX_QPATH];
	model_t	*mod;
	int	i = 1;

	if( !SV_IsValidEdict( ent ))
	{
		Con_Printf( S_WARN "%s: invalid entity %s\n", __func__, SV_ClassName( ent ));
		return;
	}

	if( !modelname || ((byte)modelname[0] ) <= ' ' )
	{
		Con_Printf( S_WARN "%s: null name\n", __func__ );
		return;
	}

#if XASH_GAMECUBE
	if( ( Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcmenuplaystart" ))
		&& modelname[0] != '*' )
		Con_Reportf( "Xash3D GameCube: SV_SetModel begin %s edict=%d\n",
			modelname, NUM_FOR_EDICT( ent ));
#endif

	if( *modelname == '\\' || *modelname == '/' )
		modelname++;

	Q_strncpy( name, modelname, sizeof( name ));
	COM_FixSlashes( name );

#if XASH_GAMECUBE
	/* Player MDL rebind after precache free OOMs/crashes during the New Game
	 * bootstrap/reconnect window. The client is still assembling its first
	 * complete frame here; attempting a studio load from this callback can
	 * race the map arena and crash before the world-ready handoff.
	 * Keep a default player hull without studio reload or world relink.
	 * Use the real precache slot (not world index 1) so entity frames do not
	 * treat the client as a brush model; clear dangling stub pointers.
	 * Normal gameplay model binding remains available after this handoff.
	 * Peer BBA PutInServer on listenserver hits the same OOM: retail
	 * models/player.mdl is ~4 MiB and FS_LoadFileFromArchive fails under tip
	 * MEM1 (dual IPC 20261107-091111) — never reach SignonReply 2. */
	if( ( Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcmenuplaystart" )
		|| Sys_CheckParm( "-gclistenserver" ) || Sys_CheckParm( "-gcconnect" )
		|| GC_MapLoadMemoryOpt() )
		&& !Q_stricmp( name, "models/player.mdl" ))
	{
		vec3_t mins = { -16.0f, -16.0f, -36.0f };
		vec3_t maxs = { 16.0f, 16.0f, 36.0f };
		int pi;
		int player_index = 0;

		Con_Reportf( "Xash3D GameCube: SV_SetModel player hull-only begin\n" );
		for( pi = 1; pi < MAX_MODELS && sv.model_precache[pi][0]; pi++ )
		{
			if( !Q_stricmp( sv.model_precache[pi], name ))
			{
				player_index = pi;
				break;
			}
		}
		if( !player_index )
			player_index = 1;

		ent->v.modelindex = player_index;
		ent->v.model = 0;
		if( player_index > 1
			&& ( !sv.models[player_index]
				|| sv.models[player_index]->needload == NL_UNREFERENCED
				|| !sv.models[player_index]->name[0]
				|| sv.models[player_index]->type != mod_studio ))
		{
			/* Freed stub / wrong type — never leave a dangling pointer for
			 * AddToFullPack / studio extradata during the first entity frame. */
			sv.models[player_index] = NULL;
		}
		VectorCopy( mins, ent->v.mins );
		VectorCopy( maxs, ent->v.maxs );
		VectorSubtract( maxs, mins, ent->v.size );
		Con_Reportf( "Xash3D GameCube: SV_SetModel player hull-only done index=%d\n",
			player_index );
		return;
	}

	/* G100/G102 lean weapon grant: studio Mod_ForName after changelevel hangs
	 * under MEM1. Bind empty hull without precache scans / MakeString.
	 * Lean -gcnewgame (no -gcfullphysics) hits the same hang on PutInServer
	 * crowbar grant — probe 20260807-013250 stalled at w_crowbar.mdl. */
	if( ( gc_lean_weapon_grant_active || Sys_CheckParm( "-gcchangelevel" )
		|| Sys_CheckParm( "-gclistenserver" ) || Sys_CheckParm( "-gcconnect" )
		|| ( Sys_CheckParm( "-gcmenuplaystart" ) && !Sys_CheckParm( "-gcfullphysics" ))
		|| ( Sys_CheckParm( "-gcnewgame" ) && !Sys_CheckParm( "-gcfullphysics" )))
		&& ( !Q_strnicmp( name, "models/w_", 9 )
			|| !Q_strnicmp( name, "models/v_", 9 )
			|| !Q_strnicmp( name, "models/p_", 9 )))
	{
		/* Do not use world model index 1 as a placeholder. The post-spawn
		 * link/full-pack path treats that as a brush model and can loop after
		 * the low-memory New Game player prime. Keep the weapon entity real but
		 * model-less until a later render-safe reload. */
		ent->v.modelindex = 0;
		ent->v.model = 0;
		ent->v.solid = SOLID_NOT;
		ent->v.movetype = MOVETYPE_NONE;
		VectorClear( ent->v.mins );
		VectorClear( ent->v.maxs );
		VectorClear( ent->v.size );
		Con_Reportf( "Xash3D GameCube: SV_SetModel weapon stub %s\n", name );
		return;
	}

	/* Dense Surface Tension maps (c2a5e) reclaim MEM1 mid-spawn then hang in
	 * Mod_ForName for env_explosion sprites (fexplo.spr edict=271, probe
	 * 20261104-132535). Bind empty — explosions are not needed for physics
	 * continuity / CHANGELEVEL_READY. */
	if( ( Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcchangelevel" )
		|| Sys_CheckParm( "-gcmenuplaystart" ))
		&& !Q_strnicmp( name, "sprites/", 8 )
		&& Sys_CheckParm( "-gcfullphysics" ))
	{
		ent->v.modelindex = 0;
		ent->v.model = 0;
		VectorClear( ent->v.mins );
		VectorClear( ent->v.maxs );
		VectorClear( ent->v.size );
		Con_Reportf( "Xash3D GameCube: SV_SetModel sprite stub %s\n", name );
		return;
	}
#endif

#if XASH_GAMECUBE
	if( GC_MenuRunsServer() && !Q_stricmp( name, "models/scientist.mdl" ))
		Con_Reportf( "Xash3D GameCube: SV_SetModel index lookup begin edict=%d\n",
			NUM_FOR_EDICT( ent ));
#endif
	i = SV_ModelIndex( name );
#if XASH_GAMECUBE
	if( GC_MenuRunsServer() && !Q_stricmp( name, "models/scientist.mdl" ))
		Con_Reportf( "Xash3D GameCube: SV_SetModel index lookup done edict=%d index=%d\n",
			NUM_FOR_EDICT( ent ), i );
#endif
	if( i == 0 )
	{
		if( sv.state == ss_active )
			Con_Printf( S_ERROR "%s: failed to set model %s: world model cannot be changed\n", __func__, name );
		return;
	}

	if( !COM_StringEmptyOrNULL( name ))
	{
		ent->v.model = SV_MakeString( sv.model_precache[i] );
		ent->v.modelindex = i;
		mod = sv.models[i];
#if XASH_GAMECUBE
		/* Precache under memopt leaves .mdl slots as NL_NEEDS_LOADED stubs
		 * (type still mod_bad, cache.data NULL). Promote allowlisted meshes
		 * into studio BSS — never Mod_ForName/disc malloc under tip MEM1
		 * (w_9mmhandgun OOM after c1a1f cast was loaded). */
		if( GC_MapLoadMemoryOpt()
			&& name[0] && !Q_strnicmp( name, "models/", 7 )
			&& Q_stristr( name, ".mdl" )
			&& ( !mod || mod->needload == NL_UNREFERENCED || mod->needload == NL_NEEDS_LOADED
				|| !mod->name[0] || !mod->cache.data
				|| ( mod->type == mod_studio && !mod->cache.data )))
		{
			Con_Reportf( "Xash3D GameCube: SV_SetModel rebinding %s index=%d\n", name, i );
			/* PromoteStudioPath allowlists first — no disc malloc for gibs/w_*. */
			Mod_GCPromoteStudioPath( name );
			mod = Mod_FindName( name, false );
			sv.models[i] = mod;
			if( !Q_stricmp( name, "models/scientist.mdl" )
				|| !Q_stricmp( name, "models/zombie.mdl" )
				|| !Q_stricmp( name, "models/headcrab.mdl" )
				|| !Q_stricmp( name, "models/filecabinet.mdl" ))
				Con_Reportf( "Xash3D GameCube: SV_SetModel rebind done edict=%d mod=%p cache=%p\n",
					NUM_FOR_EDICT( ent ), mod, mod ? mod->cache.data : NULL );
		}
#endif
	}
	else
	{
		// model will be cleared
		ent->v.model = ent->v.modelindex = 0;
		mod = NULL;
	}

	// set the model size
	if( mod && mod->type != mod_studio )
		SV_SetMinMaxSize( ent, mod->mins, mod->maxs, true );
	else SV_SetMinMaxSize( ent, vec3_origin, vec3_origin, true );
#if XASH_GAMECUBE
	if( GC_MenuRunsServer() && !Q_stricmp( name, "models/scientist.mdl" ))
		Con_Reportf( "Xash3D GameCube: SV_SetModel done edict=%d index=%d type=%d\n",
			NUM_FOR_EDICT( ent ), i, mod ? mod->type : -1 );
#endif
}

/*
=============
SV_ConvertTrace

convert trace_t to TraceResult
=============
*/
static void SV_ConvertTrace( TraceResult *dst, trace_t *src )
{
	if( !src || !dst ) return;

	dst->fAllSolid = src->allsolid;
	dst->fStartSolid = src->startsolid;
	dst->fInOpen = src->inopen;
	dst->fInWater = src->inwater;
	dst->flFraction = src->fraction;
	VectorCopy( src->endpos, dst->vecEndPos );
	dst->flPlaneDist = src->plane.dist;
	VectorCopy( src->plane.normal, dst->vecPlaneNormal );
	dst->pHit = src->ent;
	dst->iHitgroup = src->hitgroup;

	// g-cont: always reset config flags when trace is finished
	svgame.globals->trace_flags = 0;
}

/*
=============
SV_CheckClientVisiblity

Check visibility through client camera, portal camera, etc
=============
*/
static qboolean SV_CheckClientVisiblity( sv_client_t *cl, const byte *mask )
{
	vec3_t	vieworg;
	mleaf_t	*leaf;

	if( !mask ) return true; // GoldSrc rules

	// Invasion issues: wrong camera position received in ENGINE_SET_PVS
	if( cl->pViewEntity )
		VectorCopy( cl->pViewEntity->v.origin, vieworg );
	else
		VectorCopy( cl->edict->v.origin, vieworg );

	leaf = Mod_PointInLeaf( vieworg, sv.worldmodel->nodes, sv.worldmodel );

	if( CHECKVISBIT( mask, leaf->cluster ))
		return true; // visible from player view or camera view

	// now check all the portal cameras
	for( int i = 0; i < cl->num_viewents; i++ )
	{
		edict_t	*view = cl->viewentity[i];

		if( !SV_IsValidEdict( view ))
			continue;

		VectorAdd( view->v.origin, view->v.view_ofs, vieworg );
		leaf = Mod_PointInLeaf( vieworg, sv.worldmodel->nodes, sv.worldmodel );

		if( CHECKVISBIT( mask, leaf->cluster ))
			return true; // visible from portal camera view
	}

	// not visible from any viewpoint
	return false;
}

/*
=================
SV_Multicast

Sends the contents of sv.multicast to a subset of the clients,
then clears sv.multicast.

MSG_INIT	write message into signon buffer
MSG_ONE	send to one client (ent can't be NULL)
MSG_ALL	same as broadcast (origin can be NULL)
MSG_PVS	send to clients potentially visible from org
MSG_PHS	send to clients potentially audible from org
=================
*/
#if XASH_GAMECUBE
/* Sound name for a precache index, for the client of a local map route
 * (which never receives the resource list). */
const char *SV_GCSoundPrecacheName( int index )
{
	if( !SV_Active() || index <= 0 || index >= MAX_SOUNDS )
		return NULL;
	return sv.sound_precache[index];
}
#endif

static int SV_Multicast( int dest, const vec3_t origin, const edict_t *ent, qboolean usermessage, qboolean filter )
{
	byte		*mask = NULL;
	int		j, numclients = svs.maxclients;
	sv_client_t	*cl, *current = svs.clients;
	qboolean		reliable = false;
	qboolean		specproxy = false;
	int		numsends = 0;

	// some mods trying to send messages after SV_FinalMessage
	if( !svs.initialized || sv.state == ss_dead )
	{
		MSG_Clear( &sv.multicast );
		return 0;
	}

	switch( dest )
	{
	case MSG_INIT:
		if( sv.state == ss_loading )
		{
			// copy to signon buffer
			MSG_WriteBits( &sv.signon, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
			MSG_Clear( &sv.multicast );
			return 1;
		}
		// intentional fallthrough (in-game MSG_INIT it's a MSG_ALL reliable)
	case MSG_ALL:
		reliable = true;
		// intentional fallthrough
	case MSG_BROADCAST:
		// nothing to sort
		break;
	case MSG_PAS_R:
		reliable = true;
		// intentional fallthrough
	case MSG_PAS:
		if( origin == NULL ) return false;
		// NOTE: GoldSource not using PHS for singleplayer
		Mod_FatPVS( origin, FATPHS_RADIUS, fatphs, world.fatbytes, false, ( svs.maxclients == 1 ), true );
		mask = fatphs; // using the FatPVS like a PHS
		break;
	case MSG_PVS_R:
		reliable = true;
		// intentional fallthrough
	case MSG_PVS:
		if( origin == NULL ) return 0;
		mask = Mod_GetPVSForPoint( origin );
		break;
	case MSG_ONE:
		reliable = true;
		// intentional fallthrough
	case MSG_ONE_UNRELIABLE:
		if( !SV_IsValidEdict( ent )) return 0;
		j = NUM_FOR_EDICT( ent );
		if( j < 1 || j > numclients ) return 0;
		current = svs.clients + (j - 1);
		numclients = 1; // send to one
		break;
	case MSG_SPEC:
		specproxy = reliable = true;
		break;
	default:
		Host_Error( "%s: bad dest: %i\n", __func__, dest );
		return 0;
	}

	// send the data to all relevent clients (or once only)
	for( j = 0, cl = current; j < numclients; j++, cl++ )
	{
		if( cl->state == cs_free || cl->state == cs_zombie )
			continue;

		if( cl->state != cs_spawned && ( !reliable || usermessage ))
			continue;

		if( specproxy && !FBitSet( cl->flags, FCL_HLTV_PROXY ))
			continue;

		if( !cl->edict || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		// reject step sounds while predicting is enabled
		// FIXME: make sure what this code doesn't cutoff something important!!!
		if( filter && cl == sv.current_client && FBitSet( sv.current_client->flags, FCL_PREDICT_MOVEMENT ))
			continue;

		if( SV_IsValidEdict( ent ) && !SV_CheckGroupTrace( ent, cl->edict ))
			continue;

		if( !SV_CheckClientVisiblity( cl, mask ))
			continue;

		if( specproxy ) MSG_WriteBits( &sv.spec_datagram, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
		else if( reliable ) MSG_WriteBits( &cl->netchan.message, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
		else MSG_WriteBits( &cl->datagram, MSG_GetData( &sv.multicast ), MSG_GetNumBitsWritten( &sv.multicast ));
		numsends++;
	}

	MSG_Clear( &sv.multicast );

	return numsends; // just for debug
}

/*
=======================
SV_GetReliableDatagram

Get shared reliable buffer
=======================
*/
static sizebuf_t *SV_GetReliableDatagram( void )
{
	return &sv.reliable_datagram;
}

/*
=======================
SV_RestoreCustomDecal

Let the user spawn decal in game code
=======================
*/
qboolean SV_RestoreCustomDecal( decallist_t *entry, edict_t *pEdict, qboolean adjacent )
{
	if( svgame.physFuncs.pfnRestoreDecal != NULL )
	{
		if( !pEdict ) pEdict = SV_EdictNum( entry->entityIndex );
		// true if decal was sucessfully restored at the game-side
		return svgame.physFuncs.pfnRestoreDecal( entry, pEdict, adjacent );
	}
	return false;
}

/*
=======================
SV_CreateDecal

NOTE: static decals only accepted when game is loading
=======================
*/
void SV_CreateDecal( sizebuf_t *msg, const float *origin, int decalIndex, int entityIndex, int modelIndex, int flags, float scale )
{
	if( msg == &sv.signon && sv.state != ss_loading )
		return;

	// this can happens if serialized map contain 4096 static decals...
	if( MSG_GetNumBytesLeft( msg ) < 20 )
	{
		sv.ignored_world_decals++;
		return;
	}

	// static decals are posters, it's always reliable
	MSG_BeginServerCmd( msg, svc_bspdecal );
	MSG_WriteVec3Coord( msg, origin );
	MSG_WriteWord( msg, decalIndex );
	MSG_WriteShort( msg, entityIndex );
	if( entityIndex > 0 )
		MSG_WriteWord( msg, modelIndex );
	MSG_WriteByte( msg, flags );
	MSG_WriteWord( msg, scale * 4096 );
}

/*
=======================
SV_CreateStaticEntity

NOTE: static entities only accepted when game is loading
=======================
*/
qboolean SV_CreateStaticEntity( sizebuf_t *msg, int index )
{
	entity_state_t	nullstate, *baseline;
	entity_state_t	*state;

	if( index >= ( MAX_STATIC_ENTITIES - 1 ))
	{
		if( !sv.static_ents_overflow )
		{
			Con_Printf( S_WARN "MAX_STATIC_ENTITIES limit exceeded (%d)\n", MAX_STATIC_ENTITIES );
			sv.static_ents_overflow = true;
		}

		sv.ignored_static_ents++; // continue overflowed entities
		return false;
	}

	// this can happens if serialized map contain too many static entities...
	if( MSG_GetNumBytesLeft( msg ) < 50 )
	{
		sv.ignored_static_ents++;
		return false;
	}

	state = &svs.static_entities[index]; // allocate a new one
	memset( &nullstate, 0, sizeof( nullstate ));
	baseline = &nullstate;

	// restore modelindex from modelname (already precached)
	state->modelindex = pfnModelIndex( SV_GetString( state->messagenum ));
	state->entityType = ENTITY_NORMAL; // select delta-encode
	state->number = 0;

	// trying to compress with previous delta's
	int offset = SV_FindBestBaseline( index, &baseline, state, NULL, false );

	MSG_BeginServerCmd( msg, svc_spawnstatic );
	MSG_WriteDeltaEntity( baseline, state, msg, true, DELTA_STATIC, sv.time, offset );

	return true;
}

/*
=================
SV_RestartStaticEnts

Write all the static ents into demo
=================
*/
void SV_RestartStaticEnts( void )
{
	// remove all the static entities on the client
	CL_ClearStaticEntities();

	// resend them again
	for( int i = 0; i < sv.num_static_entities; i++ )
		SV_CreateStaticEntity( &sv.reliable_datagram, i );
}

/*
=================
SV_StartMusic

=================
*/
static void SV_StartMusic( const char *curtrack, const char *looptrack, int position )
{
	MSG_BeginServerCmd( &sv.multicast, svc_stufftext );
	MSG_WriteStringf( &sv.multicast, "music \"%s\" \"%s\" %d\n", curtrack, looptrack, position );
	SV_Multicast( MSG_ALL, NULL, NULL, false, false );
}

/*
=================
SV_RestartAmbientSounds

Write ambient sounds into demo
=================
*/
void SV_RestartAmbientSounds( void )
{
	// TODO: we don't know sounds state on remote server
	// as it's used only for demos, maybe this could be implemented on client side?
#if !XASH_DEDICATED
	soundlist_t	soundInfo[256];
	string		curtrack, looptrack;
	int		nSounds;
	int		position;

	if( !SV_Active( ) || Host_IsDedicated( ))
		return;

	nSounds = S_GetCurrentStaticSounds( soundInfo, 256 );

	for( int i = 0; i < nSounds; i++ )
	{
		soundlist_t *si = &soundInfo[i];

		if( !si->looping || si->entnum == -1 )
			continue;

		S_StopSound( si->entnum, si->channel, si->name );
		SV_StartSound( SV_PEntityOfEntIndex( si->entnum, true ), CHAN_STATIC, si->name, si->volume, si->attenuation, 0, si->pitch );
	}

	// restart soundtrack
	if( S_StreamGetCurrentState( curtrack, sizeof( curtrack ), looptrack, sizeof( looptrack ), &position ))
	{
		SV_StartMusic( curtrack, looptrack, position );
	}
#endif // !XASH_DEDICATED
}

/*
=================
SV_RestartDecals

Write all the decals into demo
=================
*/
void SV_RestartDecals( void )
{
	// TODO: similar to SV_RestartAmbientSounds, this is only used for demo recording
	// and better be reimplemented on client side
#if !XASH_DEDICATED
	decallist_t	*list;
	sizebuf_t		*msg;
	int		numdecals;

	if( !SV_Active( ) || Host_IsDedicated( ))
		return;

	// g-cont. add space for studiodecals if present
	list = (decallist_t *)Z_Calloc( sizeof( decallist_t ) * MAX_RENDER_DECALS * 2 );

	numdecals = ref.dllFuncs.R_CreateDecalList( list );

	// remove decals from map for clean demo recording state (keep permanent)
	ref.dllFuncs.R_ClearAllDecals( false );

	// write decals into reliable datagram
	msg = SV_GetReliableDatagram();

	// restore decals and write them into network message
	for( int i = 0; i < numdecals; i++ )
	{
		decallist_t *entry = &list[i];

		// skip permanent decals - they're map data sent via svc_bspdecal on map load
		if( FBitSet( entry->flags, FDECAL_PERMANENT ))
			continue;

		int modelIndex = SV_PEntityOfEntIndex( entry->entityIndex, true )->v.modelindex;

		// game override
		if( SV_RestoreCustomDecal( entry, SV_PEntityOfEntIndex( entry->entityIndex, true ), false ))
			continue;

		int decalIndex = pfnDecalIndex( entry->name );

		// studiodecals will be restored at game-side
		if( !FBitSet( entry->flags, FDECAL_STUDIO ))
			SV_CreateDecal( msg, entry->position, decalIndex, entry->entityIndex, modelIndex, entry->flags, entry->scale );
	}

	Z_Free( list );
#endif // !XASH_DEDICATED
}

/*
==============
SV_BoxInPVS

check brush boxes in fat pvs
==============
*/
qboolean GAME_EXPORT SV_BoxInPVS( const vec3_t org, const vec3_t absmin, const vec3_t absmax )
{
	if( !Mod_BoxVisible( absmin, absmax, Mod_GetPVSForPoint( org )))
		return false;
	return true;
}

/*
=============
SV_ChangeLevel

Issue changing level
=============
*/
void SV_QueueChangeLevel( const char *level, const char *landname )
{
	uint	flags, smooth = false;
	char	mapname[MAX_QPATH];

#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: QueueChangeLevel begin level=%s landmark=%s frame=%d\n",
		level ? level : "?", landname ? landname : "(none)", sv.framecount );
	/* G208: tram/landmark changelevel OOMs MEM1 (~194 KiB planes) and aborts
	 * G36 after Flipper walls already present. Hold on c0a0 unless the probe
	 * explicitly requested -gcchangelevel. */
	if( Sys_CheckParm( "-gcnewgame" ) && !Sys_CheckParm( "-gcchangelevel" )
		&& !SV_GCAutoCampaignActive( ))
	{
		static qboolean logged;
		if( !logged )
		{
			logged = true;
			Con_Reportf( "Xash3D GameCube: G208 skip changelevel to %s (hold Flipper map)\n",
				level ? level : "?" );
		}
		return;
	}
#endif

	// hold mapname to other place
	Q_strncpy( mapname, level, sizeof( mapname ));
	COM_StripExtension( mapname );

	if( !COM_StringEmptyOrNULL( landname ))
		smooth = true;

	flags = SV_MapIsValid( mapname, landname );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: QueueChangeLevel MapIsValid done flags=0x%x smooth=%d\n",
		flags, smooth ? 1 : 0 );
#endif

	if( FBitSet( flags, MAP_INVALID_VERSION ))
	{
		Con_Printf( S_ERROR "changelevel: %s is invalid or not supported\n", mapname );
		return;
	}

	if( !FBitSet( flags, MAP_IS_EXIST ))
	{
		Con_Printf( S_ERROR "changelevel: map %s doesn't exist\n", mapname );
		return;
	}

	if( smooth && !FBitSet( flags, MAP_HAS_LANDMARK ))
	{
		if( sv_validate_changelevel.value )
		{
			// NOTE: we find valid map but specified landmark it's doesn't exist
			// run simple changelevel like in q1, throw warning
			Con_Printf( S_WARN "changelevel: %s doesn't contain landmark [%s]. smooth transition was disabled\n", mapname, landname );
			smooth = false;
		}
	}

	if( svs.maxclients > 1 )
		smooth = false; // multiplayer doesn't support smooth transition

	if( smooth && !Q_stricmp( sv.name, level ))
	{
		Con_Printf( S_ERROR "can't changelevel with same map. Ignored.\n" );
		return;
	}

	// bad changelevel position invoke enables in one-way transition
	if( sv.framecount < 15 )
	{
		if( sv_validate_changelevel.value )
		{
			Con_Printf( S_WARN "an infinite changelevel was detected and will be disabled until a next save\\restore\n" );
			return; // lock with svs.spawncount here
		}
	}

	SV_SkipUpdates ();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: QueueChangeLevel calling COM_ChangeLevel map=%s smooth=%d\n",
		mapname, smooth ? 1 : 0 );
#endif

	// changelevel will be executed on a next frame
	if( smooth ) COM_ChangeLevel( mapname, landname, sv.background );	// Smoothed Half-Life changelevel
	else COM_ChangeLevel( mapname, NULL, sv.background );		// Classic Quake changlevel
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: QueueChangeLevel COM_ChangeLevel returned map=%s nextstate=%d\n",
		mapname, (int)GameState->nextstate );
#endif
}

/*
==============
SV_WriteEntityPatch

Create entity patch for selected map
==============
*/
void SV_WriteEntityPatch( const char *filename )
{
	int         lumpofs = 0, lumplen = 0;
	byte        buf[MAX_TOKEN] = { 0 }; // 1 kb
	string      bspfilename;
	dlump_t     entities;
	file_t      *f;
	fs_offset_t filelen;

	Q_snprintf( bspfilename, sizeof( bspfilename ), "maps/%s.bsp", filename );

	f = FS_Open( bspfilename, "rb", false );
	if( !f )
		return;

	filelen = FS_Read( f, buf, MAX_TOKEN );

	// check all the lumps and some other errors
	if( !Mod_TestBmodelLumps( f, bspfilename, buf, filelen, true, &entities ))
	{
		FS_Close( f );
		return;
	}

	lumpofs = entities.fileofs;
	lumplen = entities.filelen;

	if( lumplen >= 10 )
	{
		char	*entities = NULL;

		FS_Seek( f, lumpofs, SEEK_SET );
		entities = (char *)Z_Calloc( lumplen + 1 );
		FS_Read( f, entities, lumplen );
		FS_WriteFile( va( "maps/%s.ent", filename ), entities, lumplen );
		Con_Printf( "Write 'maps/%s.ent'\n", filename );
		Mem_Free( entities );
	}

	FS_Close( f );
}

/*
==============
SV_ReadEntityScript

pfnMapIsValid use this
==============
*/
#if XASH_GAMECUBE
static char *sv_gc_ents_borrowed;
#endif

static void SV_FreeEntityScript( char *ents )
{
#if XASH_GAMECUBE
	if( ents && ents == sv_gc_ents_borrowed )
	{
		sv_gc_ents_borrowed = NULL;
		return;
	}
#endif
	Mem_Free( ents );
}

static char *SV_ReadEntityScript( const char *filename, int *flags )
{
	string      bspfilename, entfilename;
	int         lumpofs = 0, lumplen = 0;
	byte        buf[MAX_TOKEN] = { 0 };
	char        *ents = NULL;
	dlump_t     entities;
	size_t      ft1, ft2;
	file_t      *f;
	fs_offset_t filelen;

	*flags = 0;

	Q_snprintf( bspfilename, sizeof( bspfilename ), "maps/%s.bsp", filename );

	f = FS_Open( bspfilename, "rb", false );
	if( !f )
		return NULL;

	SetBits( *flags, MAP_IS_EXIST );
	filelen = FS_Read( f, buf, sizeof( buf ));

	// check all the lumps and some other errors
	if( !Mod_TestBmodelLumps( f, bspfilename, buf, filelen, (host_developer.value) ? false : true, &entities ))
	{
		SetBits( *flags, MAP_INVALID_VERSION );
		FS_Close( f );
		return NULL;
	}

	// after call Mod_TestBmodelLumps we gurantee what map is valid
	lumpofs = entities.fileofs;
	lumplen = entities.filelen;

	// check for entfile too
	Q_snprintf( entfilename, sizeof( entfilename ), "maps/%s.ent", filename );

	// make sure what entity patch is newer than bsp
	ft1 = FS_FileTime( bspfilename, false );
	ft2 = FS_FileTime( entfilename, true );

	if( ft2 != -1 && ft1 < ft2 )
	{
		// grab .ent files only from gamedir
		ents = (char *)FS_LoadFile( entfilename, NULL, true );
	}

	// at least entities should contain "{ "classname" "worldspawn" }\0"
	// for correct spawn the level
	if( !ents && lumplen >= 32 )
	{
		FS_Seek( f, lumpofs, SEEK_SET );
#if XASH_GAMECUBE
		/* The 64 KiB entity text can fail on the fragmented heap at a
		 * changelevel. Validation is brief and the GX world does not use the
		 * soft surface cache, so borrow its store (the transition save takes
		 * it next anyway). */
		if( GC_MenuRunsServer())
		{
			extern byte *R_GCBorrowSurfaceCacheStore( size_t *size );
			size_t store_size = 0;
			byte *store = R_GCBorrowSurfaceCacheStore( &store_size );

			if( store && store_size > (size_t)lumplen )
			{
				memset( store, 0, lumplen + 1 );
				ents = (char *)store;
				sv_gc_ents_borrowed = ents;
			}
		}
		if( !ents )
#endif
		ents = Z_Calloc( lumplen + 1 );
#if XASH_GAMECUBE
		if( ents )
#endif
		FS_Read( f, ents, lumplen );
	}
	FS_Close( f ); // all done

	return ents;
}

/*
==============
SV_MapIsValid

Validate map
==============
*/
uint SV_MapIsValid( const char *filename, const char *landmark_name )
{
	uint	flags = 0;
	char	*pfile;
	char	*ents;

#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: MapIsValid begin map=%s landmark=%s\n",
		filename ? filename : "?",
		COM_StringEmptyOrNULL( landmark_name ) ? "(none)" : landmark_name );
	/* G381: full entity-script Read+Z_Calloc hangs under MEM1 when changelevel
	 * validates the dest while the tram tip world is still resident
	 * (c0a0e→c1a0a MapIsValid begin, no further logs). Existence + lump test
	 * is enough for probe hops; trust -gclandmark when present. */
	if( Sys_CheckParm( "-gcchangelevel" ))
	{
		string	bspfilename;
		byte	buf[MAX_TOKEN];
		dlump_t	entities;
		file_t	*f;
		fs_offset_t filelen;

		Q_snprintf( bspfilename, sizeof( bspfilename ), "maps/%s.bsp", filename );
		f = FS_Open( bspfilename, "rb", false );
		if( !f )
		{
			Con_Reportf( "Xash3D GameCube: MapIsValid lean missing %s\n", bspfilename );
			return 0;
		}
		SetBits( flags, MAP_IS_EXIST );
		filelen = FS_Read( f, buf, sizeof( buf ));
		if( !Mod_TestBmodelLumps( f, bspfilename, buf, filelen,
				( host_developer.value ) ? false : true, &entities ))
		{
			SetBits( flags, MAP_INVALID_VERSION );
			FS_Close( f );
			Con_Reportf( "Xash3D GameCube: MapIsValid lean invalid %s\n", bspfilename );
			return flags;
		}
		FS_Close( f );
		if( !COM_StringEmptyOrNULL( landmark_name ))
			SetBits( flags, MAP_HAS_LANDMARK );
		Con_Reportf( "Xash3D GameCube: MapIsValid lean ready flags=0x%x\n", flags );
		return flags;
	}
#endif
	ents = SV_ReadEntityScript( filename, &flags );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: MapIsValid ents=%s flags=0x%x\n",
		ents ? "ok" : "null", flags );
#endif

	if( ents )
	{
		qboolean	need_landmark;
		char	token[MAX_TOKEN];
		string	check_name;

		need_landmark = !COM_StringEmptyOrNULL( landmark_name );

		if( !need_landmark )
		{
			SV_FreeEntityScript( ents );
#if XASH_GAMECUBE
			Con_Reportf( "Xash3D GameCube: MapIsValid ready (no landmark) flags=0x%x\n", flags );
#endif
			return flags;
		}

		pfile = ents;

		while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
		{
			if( !Q_strcmp( token, "targetname" ))
			{
				// check targetname for landmark entity
				pfile = COM_ParseFile( pfile, check_name, sizeof( check_name ));

				if( !Q_strcmp( landmark_name, check_name ))
				{
					// we found landmark, stop the parsing
					SetBits( flags, MAP_HAS_LANDMARK );
					break;
				}
			}
		}

		SV_FreeEntityScript( ents );
	}

#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: MapIsValid ready flags=0x%x\n", flags );
#endif
	return flags;
}

/*
==============
SV_FreePrivateData

release private edict memory
==============
*/
static void GAME_EXPORT SV_FreePrivateData( edict_t *pEdict )
{
	if( !pEdict || !pEdict->pvPrivateData )
		return;

	// NOTE: new interface can be missing
	if( svgame.dllFuncs2.pfnOnFreeEntPrivateData != NULL )
		svgame.dllFuncs2.pfnOnFreeEntPrivateData( pEdict );

#if XASH_GAMECUBE
	if( pEdict->pvPrivateData == gc_player_private_fallback )
		; /* static New Game player fallback — never free() */
	else if( pEdict->pvPrivateData == gc_soundent_private_fallback )
	{
		gc_soundent_fallback_used = false;
		; /* static New Game CSoundEnt fallback — never free() */
	}
	else if( SV_GCPrivateDataIsSlab( pEdict->pvPrivateData ))
	{
		const int edict_index = NUM_FOR_EDICT( pEdict );
		const size_t size = edict_index >= 0 && edict_index < MAX_EDICTS
			? gc_ent_priv_slab_size[edict_index] : 0;
		if( size )
			SV_GCPrivateSlabFree( pEdict->pvPrivateData, size );
		if( edict_index >= 0 && edict_index < MAX_EDICTS )
			gc_ent_priv_slab_size[edict_index] = 0;
		/* Slab blocks never pass to Mem_Free/free. */
	}
	else if( SV_GCPrivateDataIsOverflow( pEdict->pvPrivateData ))
	{
		const int edict_index = NUM_FOR_EDICT( pEdict );
		const size_t size = edict_index >= 0 && edict_index < MAX_EDICTS
			? gc_ent_priv_slab_size[edict_index] : 0;
		if( size ) SV_GCPrivateOverflowFree( pEdict->pvPrivateData, size );
		if( edict_index >= 0 && edict_index < MAX_EDICTS )
			gc_ent_priv_slab_size[edict_index] = 0;
	}
	else if( Mem_IsAllocatedExt( svgame.mempool, pEdict->pvPrivateData ))
		Mem_Free( pEdict->pvPrivateData );
	else if( Sys_CheckParm( "-gcmap" ) || Sys_CheckParm( "-gcnewgame" ) || GC_MapLoadMemoryOpt() )
		free( pEdict->pvPrivateData );
#else
	if( Mem_IsAllocatedExt( svgame.mempool, pEdict->pvPrivateData ))
		Mem_Free( pEdict->pvPrivateData );
#endif

	pEdict->pvPrivateData = NULL;
}

/*
==============
SV_InitEdict

clear edict for reuse
==============
*/
void SV_InitEdict( edict_t *pEdict )
{
	Assert( pEdict != NULL );

	SV_FreePrivateData( pEdict );
	memset( &pEdict->v, 0, sizeof( entvars_t ));
	pEdict->v.pContainingEntity = pEdict;
	pEdict->v.controller[0] = 0x7F;
	pEdict->v.controller[1] = 0x7F;
	pEdict->v.controller[2] = 0x7F;
	pEdict->v.controller[3] = 0x7F;
	pEdict->free = false;
}

/*
==============
SV_FreeEdict

unlink edict from world and free it
==============
*/
void SV_FreeEdict( edict_t *pEdict )
{
	Assert( pEdict != NULL );
	if( pEdict->free ) return;

	// unlink from world
	SV_UnlinkEdict( pEdict );

	SV_FreePrivateData( pEdict );

	// mark edict as freed
	pEdict->freetime = sv.time;
	pEdict->serialnumber++; // invalidate EHANDLE's
	pEdict->v.solid = SOLID_NOT;
	pEdict->v.flags = 0;
	pEdict->v.model = 0;
	pEdict->v.takedamage = 0;
	pEdict->v.modelindex = 0;
	pEdict->v.nextthink = -1;
	pEdict->v.colormap = 0;
	pEdict->v.frame = 0;
	pEdict->v.scale = 0;
	pEdict->v.gravity = 0;
	pEdict->v.skin = 0;

	VectorClear( pEdict->v.angles );
	VectorClear( pEdict->v.origin );
	pEdict->free = true;
}

/*
==============
SV_AllocEdict

allocate new or reuse existing
==============
*/
edict_t *GAME_EXPORT SV_AllocEdict( void )
{
	edict_t	*e;
	int	i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		e = SV_EdictNum( i );
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if( e->free && ( e->freetime < 2.0f || ( sv.time - e->freetime ) > 0.5f ))
		{
			SV_InitEdict( e );
			return e;
		}
	}

	if( i >= GI->max_edicts )
	{
		Host_Error( "%s: no free edicts (max is %d)\n", __func__, GI->max_edicts );
		return NULL;
	}

	svgame.numEntities++;
	e = SV_EdictNum( i );
	if( !e )
	{
		Host_Error( "%s: bad edict index %d\n", __func__, i );
		return NULL;
	}
	SV_InitEdict( e );

	return e;
}

/*
==============
SV_GetEntityClass

get pointer for entity class
==============
*/
static LINK_ENTITY_FUNC SV_GetEntityClass( const char *pszClassName )
{
	// allocate edict private memory (passed by dlls)
	return (LINK_ENTITY_FUNC)COM_GetProcAddress( svgame.hInstance, pszClassName );
}

/*
==============
SV_AllocPrivateData

allocate private data for a given edict

if customentity is NULL, no "custom" entity EXPORT is being done
if customentity is not NULL, will be set to true if "custom" export
was used to create this entity
==============
*/
static edict_t* SV_AllocPrivateData( edict_t *ent, string_t className, qboolean *customentity )
{
	const char	*pszClassName;
	LINK_ENTITY_FUNC	SpawnEdict;

	pszClassName = SV_GetString( className );

	if( customentity )
		*customentity = false;

	if( !ent )
	{
		// allocate a new one
		ent = SV_AllocEdict();
	}
	else if( ent->free )
	{
		SV_InitEdict( ent ); // re-init edict
	}

	ent->v.classname = className;
	ent->v.pContainingEntity = ent; // re-link

	// allocate edict private memory (passed by dlls)
	SpawnEdict = SV_GetEntityClass( pszClassName );

	if( !SpawnEdict )
	{
		// attempt to create custom entity (Xash3D extension)
		if( svgame.physFuncs.SV_CreateEntity && svgame.physFuncs.SV_CreateEntity( ent, pszClassName ) != -1 )
			return ent;

		if( customentity )
		{
			SpawnEdict = SV_GetEntityClass( "custom" );
			*customentity = SpawnEdict != NULL;
		}

		if( !SpawnEdict )
		{
			Con_Printf( S_ERROR "No spawn function for \"%s\"\n", pszClassName );

			// free entity immediately
			SV_FreeEdict( ent );

			return NULL;
		}
	}

	SpawnEdict( &ent->v );

#if XASH_GAMECUBE
	/* Tip MEM1: brush furniture / pickups can miss a private block without
	 * ending the whole map. Boss/combat NPCs still abort after reclaim. */
	if( ( GC_MapLoadMemoryOpt() || Sys_CheckParm( "-gcmap" ) || Sys_CheckParm( "-gcnewgame" ))
		&& !ent->pvPrivateData )
	{
		const char *cn = pszClassName ? pszClassName : "";
		const qboolean skippable = !Q_strnicmp( cn, "func_", 5 )
			|| !Q_strnicmp( cn, "env_", 4 )
			|| !Q_strnicmp( cn, "light", 5 )
			|| !Q_strnicmp( cn, "info_", 5 )
			|| !Q_strnicmp( cn, "ambient_", 8 )
			|| !Q_strnicmp( cn, "multi_", 6 )
			|| !Q_strnicmp( cn, "trigger_", 8 )
			|| !Q_strnicmp( cn, "path_", 5 )
			|| !Q_strnicmp( cn, "speaker", 7 )
			|| !Q_strnicmp( cn, "item_", 5 )
			|| !Q_strnicmp( cn, "weapon", 6 )
			|| !Q_strnicmp( cn, "ammo_", 5 )
			|| !Q_strnicmp( cn, "xen_", 4 )
			|| !Q_strnicmp( cn, "cycler", 6 )
			|| !Q_strnicmp( cn, "scripted_", 9 )
			|| !Q_strnicmp( cn, "aiscripted_", 11 )
			|| !Q_strnicmp( cn, "monstermaker", 12 )
			|| !Q_strnicmp( cn, "gibshooter", 10 )
			|| !Q_strnicmp( cn, "spark_shower", 12 )
			|| !Q_stricmp( cn, "monster_hevsuit_dead" )
			|| !Q_stricmp( cn, "monster_generic" );

		if( skippable )
		{
			Con_Reportf( S_WARN "Xash3D GameCube: private-data miss %s — skipping entity\n", cn );
			SV_FreeEdict( ent );
			return NULL;
		}

		/* One reclaim pass before aborting a combat NPC — HUD/menu slabs and
		 * decode scratch often still own the tip after G326 client ensure. */
		Image_GCPurgeDecodeScratch();
		R_GCTrimTextureMips();
		R_GXReleaseHudPoolForMapLoad();
		Con_Reportf( S_ERROR "Xash3D GameCube: private-data miss critical %s (heap_free=%s)\n",
			cn, Q_memprint( GC_MemHeapFreeBytes() ));
		SV_FreeEdict( ent );
		/* Short fatal text — soft panel wraps ~25 cols and was hiding the classname. */
		Host_Error( "private-data OOM: %s\n", pszClassName );
		return NULL;
	}
#endif

	return ent;
}

/*
==============
SV_CreateNamedEntity

create specified entity, alloc private data
==============
*/
edict_t* SV_CreateNamedEntity( edict_t *ent, string_t className )
{
	return SV_AllocPrivateData( ent, className, NULL );
}

/*
==============
SV_FreeEdicts

release all the edicts from server
==============
*/
void SV_FreeEdicts( void )
{
	for( int i = 0; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( ent->free ) continue;
		SV_FreeEdict( ent );
	}
#if XASH_GAMECUBE
	/* Only after game-DLL destructors have finished may the world arena be
	 * discarded or reused for the next BSP. */
	if( gc_ent_priv_overflow_used )
		Con_Reportf( S_WARN "Xash3D GameCube: entity overflow still has %u live bytes after edict cleanup\n",
			(unsigned)gc_ent_priv_overflow_used );
	else
	{
		if( gc_ent_priv_overflow_from_tip && gc_ent_priv_overflow )
			free( gc_ent_priv_overflow );
		gc_ent_priv_overflow = NULL;
		gc_ent_priv_overflow_from_tip = false;
		gc_ent_priv_overflow_capacity = 0;
	}
#endif
}

/*
==============
SV_PlaybackReliableEvent

reliable event is must be delivered always
==============
*/
static void SV_PlaybackReliableEvent( sizebuf_t *msg, word eventindex, float delay, event_args_t *args )
{
	event_args_t nullargs;

	memset( &nullargs, 0, sizeof( nullargs ));

	MSG_BeginServerCmd( msg, svc_event_reliable );

	// send event index
	MSG_WriteUBitLong( msg, eventindex, MAX_EVENT_BITS );

	if( delay )
	{
		// send event delay
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteWord( msg, ( delay * 100.0f ));
	}
	else MSG_WriteOneBit( msg, 0 );

	// reliable events not use delta-compression just null-compression
	MSG_WriteDeltaEvent( msg, &nullargs, args );
}

/*
==============
SV_ClassName

template to get edict classname
==============
*/
const char *SV_ClassName( const edict_t *e )
{
	if( !e ) return "(null)";
	if( e->free ) return "freed";
	return SV_GetString( e->v.classname );
}

/*
==============
SV_IsValidCmd

command validation
==============
*/
static qboolean SV_IsValidCmd( const char *pCmd )
{
	size_t	len = Q_strlen( pCmd );

	// valid commands all have a ';' or newline '\n' as their last character
	if( len && ( pCmd[len-1] == '\n' || pCmd[len-1] == ';' ))
		return true;
	return false;
}

/*
==============
SV_ClientFromEdict

get edict that attached to the client structure
==============
*/
sv_client_t *SV_ClientFromEdict( const edict_t *pEdict, qboolean spawned_only )
{
	int	i;

	if( !SV_IsValidEdict( pEdict ))
		return NULL;

	i = NUM_FOR_EDICT( pEdict ) - 1;

	if( i < 0 || i >= svs.maxclients )
		return NULL;

	if( spawned_only )
	{
		if( svs.clients[i].state != cs_spawned )
			return NULL;
	}

	return (svs.clients + i);
}

/*
===============================================================================

	Game Builtin Functions

===============================================================================
*/
/*
=========
pfnPrecacheModel

=========
*/
static int GAME_EXPORT pfnPrecacheModel( const char *s )
{
	qboolean	optional = false;
	int	i;

	if( COM_StringEmptyOrNULL( s ))
	{
		// GoldSrc does Host_Error here, we are returning world as that's safe and doesn't break existing Xash games
		Con_Printf( S_WARN "%s: NULL pointer or empty string as model name, returning world...\n", __func__ );
		return 0;
	}

	if( *s == '!' )
	{
		optional = true;
		s++;
	}

#if XASH_GAMECUBE
	/* G102: weapon Precache() walks Mod_ForName under MEM1 after changelevel.
	 * Reserve a stub index without loading studio data during lean grant. */
	if( gc_lean_weapon_grant_active
		&& ( !Q_strnicmp( s, "models/", 7 )
			|| !Q_strnicmp( s, "sprites/", 8 )))
	{
		i = SV_ModelIndex( s );
		if( i <= 0 )
			i = 1;
		Con_Reportf( "Xash3D GameCube: pfnPrecacheModel grant stub %s -> %d\n", s, i );
		return i;
	}
#endif

	if(( i = SV_ModelIndex( s )) == 0 )
		return 0;

	sv.models[i] = Mod_ForName( sv.model_precache[i], false, true );

	if( !optional )
		SetBits( sv.model_precache_flags[i], RES_FATALIFMISSING );

	return i;
}

/*
=================
pfnModelIndex

=================
*/
static int GAME_EXPORT pfnModelIndex( const char *m )
{
	char	name[MAX_QPATH];

	if( COM_StringEmptyOrNULL( m ))
		return 0;

	if( *m == '\\' || *m == '/' ) m++;
	Q_strncpy( name, m, sizeof( name ));
	COM_FixSlashes( name );

	for( int i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
	{
		if( !Q_stricmp( sv.model_precache[i], name ))
			return i;
	}

	Con_Printf( S_ERROR "Cannot get index for model %s: not precached\n", name );
	return 0;
}

/*
=================
pfnModelFrames

=================
*/
static int GAME_EXPORT pfnModelFrames( int modelIndex )
{
	model_t	*pmodel = SV_ModelHandle( modelIndex );

	if( pmodel != NULL )
		return pmodel->numframes;
	return 1;
}

/*
=================
pfnSetSize

=================
*/
static void GAME_EXPORT pfnSetSize( edict_t *e, const float *rgflMin, const float *rgflMax )
{
	if( !SV_IsValidEdict( e ))
		return;

	SV_SetMinMaxSize( e, rgflMin, rgflMax, true );
}

/*
=================
pfnChangeLevel

=================
*/
static void GAME_EXPORT pfnChangeLevel( const char *level, const char *landmark )
{
	static uint	last_spawncount = 0;
	char		landname[MAX_QPATH];
	char		*text;

	if( COM_StringEmptyOrNULL( level ) || sv.state != ss_active )
		return; // ???

	// make sure we don't issue two changelevels
	if( svs.spawncount == last_spawncount )
		return;
	last_spawncount = svs.spawncount;
	landname[0] ='\0';

#ifdef HACKS_RELATED_HLMODS
	// g-cont. some level-designers wrote landmark name with space
	// and Cmd_TokenizeString separating all the after space as next argument
	// emulate this bug for compatibility
	if( !COM_StringEmptyOrNULL( landmark ))
	{
		text = (char *)landname;
		while( *landmark && ((byte)*landmark) != ' ' )
			*text++ = *landmark++;
		*text = '\0';
	}
#else
	Q_strncpy( landname, landmark, sizeof( landname ));
#endif
	SV_QueueChangeLevel( level, landname );
}

/*
=================
pfnGetSpawnParms

OBSOLETE, UNUSED
=================
*/
static void GAME_EXPORT pfnGetSpawnParms( edict_t *ent )
{
}

/*
=================
pfnSaveSpawnParms

OBSOLETE, UNUSED
=================
*/
static void GAME_EXPORT pfnSaveSpawnParms( edict_t *ent )
{
}

/*
=================
pfnVecToYaw

=================
*/
static float GAME_EXPORT pfnVecToYaw( const float *rgflVector )
{
	return SV_VecToYaw( rgflVector );
}

/*
=================
pfnMoveToOrigin

=================
*/
static void GAME_EXPORT pfnMoveToOrigin( edict_t *ent, const float *pflGoal, float dist, int iMoveType )
{
	if( !pflGoal || !SV_IsValidEdict( ent ))
		return;

	SV_MoveToOrigin( ent, pflGoal, dist, iMoveType );
}

/*
==============
pfnChangeYaw

==============
*/
static void GAME_EXPORT pfnChangeYaw( edict_t* ent )
{
	if( !SV_IsValidEdict( ent ))
		return;

	ent->v.angles[YAW] = SV_AngleMod( ent->v.ideal_yaw, ent->v.angles[YAW], ent->v.yaw_speed );
}

/*
==============
pfnChangePitch

==============
*/
static void GAME_EXPORT pfnChangePitch( edict_t* ent )
{
	if( !SV_IsValidEdict( ent ))
		return;

	ent->v.angles[PITCH] = SV_AngleMod( ent->v.idealpitch, ent->v.angles[PITCH], ent->v.pitch_speed );
}

/*
=========
SV_FindEntityByString

=========
*/
static edict_t *GAME_EXPORT SV_FindEntityByString( edict_t *pStartEdict, const char *pszField, const char *pszValue )
{
	int		e = 0;
	const TYPEDESCRIPTION	*desc = NULL;

	if( COM_StringEmptyOrNULL( pszValue ))
		return svgame.edicts;

	if( pStartEdict ) e = NUM_FOR_EDICT( pStartEdict );

	for( int i = 0; i < ARRAYSIZE( gEntvarsDescription ); i++ )
	{
		if( !Q_strcmp( pszField, gEntvarsDescription[i].fieldName ))
		{
			desc = &gEntvarsDescription[i];
			break;
		}
	}

	if( desc == NULL )
	{
		Con_Printf( S_ERROR "FindEntityByString: field %s not a string\n", pszField );
		return svgame.edicts;
	}

	for( e++; e < svgame.numEntities; e++ )
	{
		edict_t *ed = SV_EdictNum( e );
		const char *t;

		if( !SV_IsValidEdict( ed )) continue;

		if( e <= svs.maxclients && !SV_ClientFromEdict( ed, ( svs.maxclients != 1 )))
			continue;

		switch( desc->fieldType )
		{
		case FIELD_STRING:
		case FIELD_MODELNAME:
		case FIELD_SOUNDNAME:
			t = SV_GetString( *(string_t *)&((byte *)&ed->v)[desc->fieldOffset] );
			if( t != NULL && t != svgame.globals->pStringBase )
			{
				if( !Q_strcmp( t, pszValue ))
					return ed;
			}
			break;
		default:
			ASSERT( 0 );
			break;
		}
	}

	return svgame.edicts;
}

/*
=========
SV_FindGlobalEntity

ripped out from the hl.dll
=========
*/
edict_t *SV_FindGlobalEntity( string_t classname, string_t globalname )
{
	edict_t *pent = SV_FindEntityByString( NULL,  "globalname", SV_GetString( globalname ));

	if( SV_IsValidEdict( pent ))
	{
		// don't spam about error - game code already tell us
		if( Q_strcmp( SV_ClassName( pent ), SV_GetString( classname )))
			pent = NULL;
	}

	return pent;
}

/*
=================
pfnFindEntityInSphere

find the entity in sphere
=================
*/
static edict_t *GAME_EXPORT pfnFindEntityInSphere( edict_t *pStartEdict, const float *org, float flRadius )
{
	int	e = 0;

	flRadius *= flRadius;

	if( SV_IsValidEdict( pStartEdict ))
		e = NUM_FOR_EDICT( pStartEdict );

	for( e++; e < svgame.numEntities; e++ )
	{
		edict_t *ent = SV_EdictNum( e );
		float distSquared = 0.0f;

		if( !SV_IsValidEdict( ent ))
			continue;

		// ignore clients that not in a game
		if( e <= svs.maxclients && !SV_ClientFromEdict( ent, true ))
			continue;

		for( int j = 0; j < 3 && distSquared <= flRadius; j++ )
		{
			float eorg;

			if( org[j] < ent->v.absmin[j] )
				eorg = org[j] - ent->v.absmin[j];
			else if( org[j] > ent->v.absmax[j] )
				eorg = org[j] - ent->v.absmax[j];
			else eorg = 0.0f;

			distSquared += eorg * eorg;
		}

		if( distSquared < flRadius )
			return ent;
	}

	return svgame.edicts;
}

/*
=================
SV_CheckClientPVS

build the new client PVS
=================
*/
static int SV_CheckClientPVS( int check, qboolean bMergePVS )
{
	byte		*pvs;
	vec3_t		vieworg;
	sv_client_t	*cl;
	int		i;
	edict_t		*ent = NULL;

	// cycle to the next one
	check = bound( 1, check, svs.maxclients );

	if( check == svs.maxclients )
		i = 1; // reset cycle
	else i = check + 1;

	for( ;; i++ )
	{
		if( i == ( svs.maxclients + 1 ))
			i = 1;

		ent = SV_EdictNum( i );
		if( i == check ) break; // didn't find anything else

		if( ent->free || !ent->pvPrivateData || FBitSet( ent->v.flags, FL_NOTARGET ))
			continue;

		// anything that is a client, or has a client as an enemy
		break;
	}

	cl = SV_ClientFromEdict( ent, true );
	memset( clientpvs, 0xFF, world.visbytes );

	// get the PVS for the entity
	VectorAdd( ent->v.origin, ent->v.view_ofs, vieworg );
	pvs = Mod_GetPVSForPoint( vieworg );
	if( pvs ) memcpy( clientpvs, pvs, world.visbytes );

	// transition in progress
	if( !cl ) return i;

	// now merge PVS with all the portal cameras
	for( int k = 0; k < cl->num_viewents && bMergePVS; k++ )
	{
		edict_t	*view = cl->viewentity[k];

		if( !SV_IsValidEdict( view ))
			continue;

		VectorAdd( view->v.origin, view->v.view_ofs, vieworg );
		pvs = Mod_GetPVSForPoint( vieworg );

		for( int j = 0; j < world.visbytes && pvs; j++ )
			SetBits( clientpvs[j], pvs[j] );
	}

	return i;
}

/*
=================
pfnFindClientInPVS

=================
*/
static edict_t* GAME_EXPORT pfnFindClientInPVS( edict_t *pEdict )
{
	edict_t	*pClient;
	vec3_t	view;
	float	delta;
	model_t	*mod;
	qboolean	bMergePVS;
	mleaf_t	*leaf;

	if( !SV_IsValidEdict( pEdict ))
		return svgame.edicts;

	delta = ( sv.time - sv.lastchecktime );

	// don't merge visibility for portal entity, only for monsters
	bMergePVS = FBitSet( pEdict->v.flags, FL_MONSTER ) ? true : false;

	// find a new check if on a new frame
	if( delta < 0.0f || delta >= 0.1f )
	{
		sv.lastcheck = SV_CheckClientPVS( sv.lastcheck, bMergePVS );
		sv.lastchecktime = sv.time;
	}

	// return check if it might be visible
	pClient = SV_EdictNum( sv.lastcheck );

	if( !SV_ClientFromEdict( pClient, true ))
		return svgame.edicts;

	mod = SV_ModelHandle( pEdict->v.modelindex );

	// portals & monitors
	// NOTE: this specific break "radiaton tick" in normal half-life. use only as feature
	if( FBitSet( host.features, ENGINE_PHYSICS_PUSHER_EXT ) && mod && mod->type == mod_brush && !FBitSet( mod->flags, MODEL_HAS_ORIGIN ))
	{
		// handle PVS origin for bmodels
		VectorAverage( pEdict->v.mins, pEdict->v.maxs, view );
		VectorAdd( view, pEdict->v.origin, view );
	}
	else
	{
		VectorAdd( pEdict->v.origin, pEdict->v.view_ofs, view );
	}

	leaf = Mod_PointInLeaf( view, sv.worldmodel->nodes, sv.worldmodel );

	if( CHECKVISBIT( clientpvs, leaf->cluster ))
		return pClient; // client which currently in PVS

	return svgame.edicts;
}

/*
=================
pfnEntitiesInPVS

=================
*/
static edict_t *pfnEntitiesInPVS( edict_t *pview )
{
	edict_t	*pchain;
	vec3_t	viewpoint;

	if( !SV_IsValidEdict( pview ))
		return NULL;

	VectorAdd( pview->v.origin, pview->v.view_ofs, viewpoint );
	pchain = SV_EdictNum( 0 );

	for( int i = 1; i < svgame.numEntities; i++ )
	{
		edict_t *pent = SV_EdictNum( i );
		edict_t *ptest;

		if( !SV_IsValidEdict( pent ))
			continue;

		if( pent->v.movetype == MOVETYPE_FOLLOW && SV_IsValidEdict( pent->v.aiment ))
			ptest = pent->v.aiment;
		else ptest = pent;

		if( SV_BoxInPVS( viewpoint, ptest->v.absmin, ptest->v.absmax ))
		{
			pent->v.chain = pchain;
			pchain = pent;
		}
	}

	return pchain;
}

/*
==============
pfnMakeVectors

==============
*/
static void GAME_EXPORT pfnMakeVectors( const float *rgflVector )
{
	AngleVectors( rgflVector, svgame.globals->v_forward, svgame.globals->v_right, svgame.globals->v_up );
}

/*
==============
pfnRemoveEntity

free edict private mem, unlink physics etc
==============
*/
static void GAME_EXPORT pfnRemoveEntity( edict_t *e )
{
	if( !SV_IsValidEdict( e ))
		return;

	// never free client or world entity
	if( NUM_FOR_EDICT( e ) < ( svs.maxclients + 1 ))
	{
		Con_Printf( S_ERROR "can't delete %s\n", ( e == SV_EdictNum( 0 )) ? "world" : "client" );
		return;
	}

	SV_FreeEdict( e );
}

/*
==============
pfnCreateNamedEntity

==============
*/
static edict_t *GAME_EXPORT pfnCreateNamedEntity( string_t className )
{
	return SV_CreateNamedEntity( NULL, className );
}

/*
=============
pfnMakeStatic

move entity to client
=============
*/
static void GAME_EXPORT pfnMakeStatic( edict_t *ent )
{
	entity_state_t	*state;

	if( !SV_IsValidEdict( ent ))
		return;

	// fill the entity state
	state = &svs.static_entities[sv.num_static_entities];	// allocate a new one
	svgame.dllFuncs.pfnCreateBaseline( false, NUM_FOR_EDICT( ent ), state, ent, 0, vec3_origin, vec3_origin );
	state->messagenum = ent->v.model; // member modelname

	if( SV_CreateStaticEntity( &sv.signon, sv.num_static_entities ))
		sv.num_static_entities++;

	// remove at end of the frame
	SetBits( ent->v.flags, FL_KILLME );
}

/*
=============
pfnEntIsOnFloor

legacy builtin
=============
*/
static int GAME_EXPORT pfnEntIsOnFloor( edict_t *e )
{
	if( !SV_IsValidEdict( e ))
		return 0;

	return SV_CheckBottom( e, MOVE_NORMAL );
}

/*
===============
pfnDropToFloor

===============
*/
int GAME_EXPORT pfnDropToFloor( edict_t *e )
{
	qboolean	monsterClip;
	trace_t	trace;

	if( !SV_IsValidEdict( e ))
		return 0;

#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnewgame" ) && e->v.flags & FL_CLIENT )
		Con_Reportf( "Xash3D GameCube: DropToFloor begin player edict=%d\n",
			NUM_FOR_EDICT( e ));
#endif

	monsterClip = FBitSet( e->v.flags, FL_MONSTERCLIP ) ? true : false;
	vec3_t end = Vec3( e->v.origin );
	/* Some GameCube smoke maps place info_player_start above a long shaft.
	 * The stock 256-unit probe misses the actual floor, leaving the player
	 * to fall into a death state before the first gameplay input. */
	float drop_distance = 256.0f;
	if( Sys_CheckParm( "-gcnewgame" ) && FBitSet( e->v.flags, FL_CLIENT ))
		drop_distance = 1024.0f;
	end[2] -= drop_distance;

	trace = SV_Move( e->v.origin, e->v.mins, e->v.maxs, end, MOVE_NORMAL, e, monsterClip );

#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnewgame" ) && e->v.flags & FL_CLIENT )
		Con_Reportf( "Xash3D GameCube: DropToFloor ready frac=%.3f allsolid=%d\n",
			trace.fraction, trace.allsolid ? 1 : 0 );
#endif

	if( trace.allsolid )
		return -1;

	if( trace.fraction == 1.0f )
		return 0;

	VectorCopy( trace.endpos, e->v.origin );
	SV_LinkEdict( e, false );
	SetBits( e->v.flags, FL_ONGROUND );
	e->v.groundentity = trace.ent;

	return 1;
}

/*
===============
pfnWalkMove

===============
*/
static int GAME_EXPORT pfnWalkMove( edict_t *ent, float yaw, float dist, int iMode )
{
	if( !SV_IsValidEdict( ent ))
		return 0;

	if( !FBitSet( ent->v.flags, FL_FLY|FL_SWIM|FL_ONGROUND ))
		return 0;

	yaw = DEG2RAD( yaw );
	vec3_t move = { cos( yaw ) * dist, sin( yaw ) * dist, 0.0f };

	switch( iMode )
	{
	case WALKMOVE_NORMAL:
		return SV_MoveStep( ent, move, true );
	case WALKMOVE_WORLDONLY:
		return SV_MoveTest( ent, move, true );
	case WALKMOVE_CHECKONLY:
		return SV_MoveStep( ent, move, false);
	default:
		Host_Error( "%s: unknown mode %d\n", __func__, iMode );
	}
	return 0;
}

/*
=================
pfnSetOrigin

=================
*/
static void GAME_EXPORT pfnSetOrigin( edict_t *e, const float *rgflOrigin )
{
	if( !SV_IsValidEdict( e ))
		return;

	VectorCopy( rgflOrigin, e->v.origin );
	SV_LinkEdict( e, false );
}

/*
=================
SV_BuildSoundMsg

=================
*/
int SV_BuildSoundMsg( sizebuf_t *msg, edict_t *ent, int chan, const char *sample, int vol, float attn, int flags, int pitch, const vec3_t pos )
{
	int	entityIndex;
	int	sound_idx;
	qboolean	spawn;

	if( vol < 0 || vol > 255 )
	{
		Con_Reportf( S_ERROR "%s: volume = %i\n", __func__, vol );
		vol = bound( 0, vol, 255 );
	}

	if( attn < 0.0f || attn > 4.0f )
	{
		Con_Reportf( S_ERROR "%s: attenuation %g must be in range 0-4\n", __func__, attn );
		attn = bound( 0.0f, attn, 4.0f );
	}

	if( chan < 0 || chan > 7 )
	{
		Con_Reportf( S_ERROR "%s: channel must be in range 0-7\n", __func__ );
		chan = bound( 0, chan, 7 );
	}

	if( pitch < 0 || pitch > 255 )
	{
		Con_Reportf( S_ERROR "%s: pitch = %i\n", __func__, pitch );
		pitch = bound( 0, pitch, 255 );
	}

	if( COM_StringEmptyOrNULL( sample ))
	{
		Con_Reportf( S_ERROR "%s: passed NULL sample\n", __func__ );
		return 0;
	}

	if( sample[0] == '!' && Q_isdigit( sample + 1 ))
	{
		sound_idx = Q_atoi( sample + 1 );

		if( sound_idx >= MAX_SOUNDS_NONSENTENCE )
		{
			SetBits( flags, SND_SENTENCE|SND_SEQUENCE );
			sound_idx -= MAX_SOUNDS_NONSENTENCE;
		}
		else SetBits( flags, SND_SENTENCE );
	}
	else if( sample[0] == '#' && Q_isdigit( sample + 1 ))
	{
		SetBits( flags, SND_SENTENCE|SND_SEQUENCE );
		sound_idx = Q_atoi( sample + 1 );
	}
	else
	{
		// '*' is special symbol to handle stream sounds
		// (CHAN_VOICE but cannot be overriden)
		// originally handled on client side
		if( *sample == '*' )
			chan = CHAN_STREAM;

		// precache_sound can be used twice: cache sounds when loading
		// and return sound index when server is active
		sound_idx = SV_SoundIndex( sample );

		if( !sound_idx )
		{
			Con_Printf( S_ERROR "%s: %s not precached (%d)\n", __func__, sample, sound_idx );
			return 0;
		}
	}

	spawn = FBitSet( flags, SND_RESTORE_POSITION ) ? false : true;

	if( SV_IsValidEdict( ent ))
		entityIndex = NUM_FOR_EDICT( ent );
	else entityIndex = 0; // assume world

	if( vol != 255 ) SetBits( flags, SND_VOLUME );
	if( attn != ATTN_NONE ) SetBits( flags, SND_ATTENUATION );
	if( pitch != PITCH_NORM ) SetBits( flags, SND_PITCH );

	// not sending (because this is out of range)
	ClearBits( flags, SND_RESTORE_POSITION );
	ClearBits( flags, SND_FILTER_CLIENT );
	ClearBits( flags, SND_SPAWNING );

	if( spawn ) MSG_BeginServerCmd( msg, svc_sound );
	else MSG_BeginServerCmd( msg, svc_restoresound );
	MSG_WriteUBitLong( msg, flags, MAX_SND_FLAGS_BITS );
	MSG_WriteUBitLong( msg, sound_idx, MAX_SOUND_BITS );
	MSG_WriteUBitLong( msg, chan, MAX_SND_CHAN_BITS );

	if( FBitSet( flags, SND_VOLUME )) MSG_WriteByte( msg, vol );
	if( FBitSet( flags, SND_ATTENUATION )) MSG_WriteByte( msg, Q_min( attn * 64, 255 ));
	if( FBitSet( flags, SND_PITCH )) MSG_WriteByte( msg, pitch );

	MSG_WriteUBitLong( msg, entityIndex, MAX_ENTITY_BITS );
	MSG_WriteVec3Coord( msg, pos );

	return 1;
}

/*
=================
SV_StartSound

=================
*/
void GAME_EXPORT SV_StartSound( edict_t *ent, int chan, const char *sample, float vol, float attn, int flags, int pitch )
{
	qboolean	filter = false;
	int	msg_dest;
	vec3_t	origin;

	if( !SV_IsValidEdict( ent ))
		return;

	VectorAverage( ent->v.mins, ent->v.maxs, origin );
	VectorAdd( origin, ent->v.origin, origin );

	if( FBitSet( flags, SND_SPAWNING ))
		msg_dest = MSG_INIT;
	else if( chan == CHAN_STATIC )
		msg_dest = MSG_ALL;
	else if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		msg_dest = MSG_ALL;
	else msg_dest = (svs.maxclients <= 1 ) ? MSG_ALL : MSG_PAS_R;

	// always sending stop sound command
	if( FBitSet( flags, SND_STOP ))
		msg_dest = MSG_ALL;

	if( FBitSet( flags, SND_FILTER_CLIENT ))
		filter = true;

	if( SV_BuildSoundMsg( &sv.multicast, ent, chan, sample, vol * 255, attn, flags, pitch, origin ))
	{
#if XASH_GAMECUBE
		if( Sys_CheckParm( "-gcnewgame" ) && sample && sample[0] )
		{
			static int gc_intro_snd_log;
			if( gc_intro_snd_log < 12 )
			{
				Con_Reportf( "Xash3D GameCube: G278 sound chan=%d sample=%s\n",
					chan, sample );
				gc_intro_snd_log++;
			}
		}
#endif
		SV_Multicast( msg_dest, origin, NULL, false, filter );
	}
}

/*
=================
pfnEmitAmbientSound

=================
*/
static void GAME_EXPORT pfnEmitAmbientSound( edict_t *ent, float *pos, const char *sample, float vol, float attn, int flags, int pitch )
{
	int	msg_dest;

	if( sv.state == ss_loading )
		SetBits( flags, SND_SPAWNING );

	if( FBitSet( flags, SND_SPAWNING ))
		msg_dest = MSG_INIT;
	else msg_dest = MSG_ALL;

	// always sending stop sound command
	if( FBitSet( flags, SND_STOP ))
		msg_dest = MSG_ALL;

	if( SV_BuildSoundMsg( &sv.multicast, ent, CHAN_STATIC, sample, vol * 255, attn, flags, pitch, pos ))
	{
#if XASH_GAMECUBE
		if( sample && !Q_strnicmp( sample, "tride/", 6 ))
			Con_Reportf( "Xash3D GameCube: VO emit %s dest=%d flags=%d\n", sample, msg_dest, flags );
#endif
		SV_Multicast( msg_dest, pos, NULL, false, false );
	}
#if XASH_GAMECUBE
	else if( sample && !Q_strnicmp( sample, "tride/", 6 ))
		Con_Reportf( "Xash3D GameCube: VO emit %s not sent\n", sample );
#endif
}

/*
=================
pfnTraceLine

=================
*/
static void GAME_EXPORT pfnTraceLine( const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr )
{
	trace_t	trace;

#if XASH_GAMECUBE
	/* Lean: point-sized ClipMove/SV_Move hung (G120). Hull world-only clip
	 * matches working FillUsercmd aim; entity ClipToLinks still deferred. */
	if( Sys_CheckParm( "-gcnewgame" ) && !Sys_CheckParm( "-gcfullphysics" ))
	{
		vec3_t mins, maxs;

		mins[0] = mins[1] = -16.0f;
		mins[2] = -36.0f;
		maxs[0] = maxs[1] = 16.0f;
		maxs[2] = 36.0f;
		memset( &trace, 0, sizeof( trace ));
		SV_ClipMoveToEntity( SV_EdictNum( 0 ), v1, mins, maxs, v2, &trace );
		if( !SV_IsValidEdict( trace.ent ))
			trace.ent = svgame.edicts;
		SV_ConvertTrace( ptr, &trace );
		return;
	}
#endif
	trace = SV_Move( v1, vec3_origin, vec3_origin, v2, fNoMonsters, pentToSkip, false );
	if( !SV_IsValidEdict( trace.ent ))
		trace.ent = svgame.edicts;
	SV_ConvertTrace( ptr, &trace );
}

/*
=================
pfnTraceToss

=================
*/
static void GAME_EXPORT pfnTraceToss( edict_t *pent, edict_t *pentToIgnore, TraceResult *ptr )
{
	trace_t	trace;

	if( !SV_IsValidEdict( pent ))
		return;

	trace = SV_MoveToss( pent, pentToIgnore );
	SV_ConvertTrace( ptr, &trace );
}

/*
=================
pfnTraceHull

=================
*/
static void GAME_EXPORT pfnTraceHull( const float *v1, const float *v2, int fNoMonsters, int hullNumber, edict_t *pentToSkip, TraceResult *ptr )
{
	trace_t	trace;

	if( hullNumber < 0 || hullNumber > 3 )
		hullNumber = 0;

	trace = SV_Move( v1, sv.worldmodel->hulls[hullNumber].clip_mins, sv.worldmodel->hulls[hullNumber].clip_maxs, v2, fNoMonsters, pentToSkip, false );
	SV_ConvertTrace( ptr, &trace );
}

/*
=============
pfnTraceMonsterHull

=============
*/
static int GAME_EXPORT pfnTraceMonsterHull( edict_t *pEdict, const float *v1, const float *v2, int fNoMonsters, edict_t *pentToSkip, TraceResult *ptr )
{
	qboolean	monsterClip;
	trace_t	trace;

	if( !SV_IsValidEdict( pEdict ))
		return 0;

	monsterClip = FBitSet( pEdict->v.flags, FL_MONSTERCLIP ) ? true : false;
	trace = SV_Move( v1, pEdict->v.mins, pEdict->v.maxs, v2, fNoMonsters, pentToSkip, monsterClip );
	SV_ConvertTrace( ptr, &trace );

	if( trace.allsolid || trace.fraction != 1.0f )
		return true;
	return false;
}

/*
=============
pfnTraceModel

=============
*/
static void GAME_EXPORT pfnTraceModel( const float *v1, const float *v2, int hullNumber, edict_t *pent, TraceResult *ptr )
{
	float	*mins, *maxs;
	model_t	*model;
	trace_t	trace;

	if( !SV_IsValidEdict( pent ))
		return;

	if( hullNumber < 0 || hullNumber > 3 )
		hullNumber = 0;

	mins = sv.worldmodel->hulls[hullNumber].clip_mins;
	maxs = sv.worldmodel->hulls[hullNumber].clip_maxs;
	model = SV_ModelHandle( pent->v.modelindex );

	if( pent->v.solid == SOLID_CUSTOM )
	{
		// NOTE: always goes through custom clipping move
		// even if our callbacks is not initialized
		SV_CustomClipMoveToEntity( pent, v1, mins, maxs, v2, &trace );
	}
	else if( model && model->type == mod_brush )
	{
		int oldmovetype = pent->v.movetype;
		int oldsolid = pent->v.solid;
		pent->v.movetype = MOVETYPE_PUSH;
		pent->v.solid = SOLID_BSP;

		SV_ClipMoveToEntity( pent, v1, mins, maxs, v2, &trace );

		pent->v.movetype = oldmovetype;
		pent->v.solid = oldsolid;
	}
	else
	{
		SV_ClipMoveToEntity( pent, v1, mins, maxs, v2, &trace );
	}

	SV_ConvertTrace( ptr, &trace );
}

/*
=============
pfnTraceTexture

returns texture basename
=============
*/
static const char *pfnTraceTexture( edict_t *pTextureEntity, const float *v1, const float *v2 )
{
	if( !SV_IsValidEdict( pTextureEntity ))
		return NULL;

	return SV_TraceTexture( pTextureEntity, v1, v2 );
}

/*
=============
pfnTraceSphere

OBSOLETE, UNUSED
=============
*/
static void GAME_EXPORT pfnTraceSphere( const float *v1, const float *v2, int fNoMonsters, float radius, edict_t *pentToSkip, TraceResult *ptr )
{
}

/*
=============
pfnGetAimVector

NOTE: speed is unused
=============
*/
static void GAME_EXPORT pfnGetAimVector( edict_t* ent, float speed, float *rgflReturn )
{
	edict_t		*check;
	vec3_t		start, dir, end, bestdir;
	float		bestdist;
	trace_t		tr;

	VectorCopy( svgame.globals->v_forward, rgflReturn );	// assume failure if it returns early

	if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_FAKECLIENT ))
		return;

	VectorCopy( ent->v.origin, start );
	VectorAdd( start, ent->v.view_ofs, start );

	// try sending a trace straight
	VectorCopy( svgame.globals->v_forward, dir );
	VectorMA( start, 2048, dir, end );
	tr = SV_Move( start, vec3_origin, vec3_origin, end, MOVE_NORMAL, ent, false );

	// don't aim at teammate
	if( tr.ent && ( tr.ent->v.takedamage == DAMAGE_AIM || ent->v.team <= 0 || ent->v.team != tr.ent->v.team ))
		return;

	// try all possible entities
	VectorCopy( svgame.globals->v_forward, bestdir );
	if( sv_allow_autoaim.value )
		bestdist = sv_aim.value;
	else bestdist = 0;

	check = SV_EdictNum( 1 ); // start at first client
	for( int i = 1; i < svgame.numEntities; i++, check++ )
	{
		float dist;

		if( check->v.takedamage != DAMAGE_AIM )
			continue;

		if( FBitSet( check->v.flags, FL_FAKECLIENT ))
			continue;

		if( ent->v.team > 0 && ent->v.team == check->v.team )
			continue;

		if( check == ent )
			continue;

		for( int j = 0; j < 3; j++ )
			end[j] = check->v.origin[j] + 0.5f * (check->v.mins[j] + check->v.maxs[j]);

		VectorSubtract( end, start, dir );
		VectorNormalize( dir );
		dist = DotProduct( dir, svgame.globals->v_forward );

		if( dist < bestdist )
			continue; // to far to turn

		tr = SV_Move( start, vec3_origin, vec3_origin, end, MOVE_NORMAL, ent, false );

		if( tr.ent == check )
		{
			// can shoot at this one
			VectorCopy( dir, bestdir );
			bestdist = dist;
		}
	}

	VectorCopy( bestdir, rgflReturn );
}

/*
=========
pfnServerCommand

=========
*/
static void GAME_EXPORT pfnServerCommand( const char* str )
{
	if( !SV_IsValidCmd( str ))
		Con_Printf( S_ERROR "bad server command %s\n", str );
	else Cbuf_AddText( str );
}

/*
=========
pfnServerExecute

=========
*/
static void GAME_EXPORT pfnServerExecute( void )
{
	Cbuf_Execute();
}

/*
=========
pfnClientCommand

=========
*/
void GAME_EXPORT pfnClientCommand( edict_t* pEdict, char* szFmt, ... ) FORMAT_CHECK( 2 );
void GAME_EXPORT pfnClientCommand( edict_t* pEdict, char* szFmt, ... )
{
	sv_client_t	*cl;
	string		buffer;
	va_list		args;

	if( sv.state != ss_active )
		return; // early out

	if(( cl = SV_ClientFromEdict( pEdict, false )) == NULL )
	{
		Con_Printf( S_ERROR "stuffcmd: client is not spawned!\n" );
		return;
	}

	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, MAX_STRING, szFmt, args );
	va_end( args );

	if( SV_IsValidCmd( buffer ))
	{
		MSG_BeginServerCmd( &cl->netchan.message, svc_stufftext );
		MSG_WriteString( &cl->netchan.message, buffer );
	}
	else Con_Printf( S_ERROR "Tried to stuff bad command %s\n", buffer );
}

/*
=================
pfnParticleEffect

Make sure the event gets sent to all clients
=================
*/
static void GAME_EXPORT pfnParticleEffect( const float *org, const float *dir, float color, float count )
{
	int	v;

	if( MSG_GetNumBytesLeft( &sv.datagram ) < 16 )
		return;

	MSG_BeginServerCmd( &sv.datagram, svc_particle );
	MSG_WriteVec3Coord( &sv.datagram, org );
	v = bound( -128, dir[0] * 16.0f, 127 );
	MSG_WriteChar( &sv.datagram, v );
	v = bound( -128, dir[1] * 16.0f, 127 );
	MSG_WriteChar( &sv.datagram, v );
	v = bound( -128, dir[2] * 16.0f, 127 );
	MSG_WriteChar( &sv.datagram, v );
	MSG_WriteByte( &sv.datagram, count );
	MSG_WriteByte( &sv.datagram, color );
	MSG_WriteByte( &sv.datagram, 0 ); // z-vel
}

/*
===============
pfnLightStyle

===============
*/
static void GAME_EXPORT pfnLightStyle( int style, const char* val )
{
	if( style < 0 )
		style = 0;

	if( style >= MAX_LIGHTSTYLES )
	{
		Host_Error( "%s: style: %i >= %d", __func__, style, MAX_LIGHTSTYLES );
		return;
	}

	if( sv.loadgame )
		return; // don't let the world overwrite our restored styles

	SV_SetLightStyle( style, val, 0.0f ); // set correct style
}

/*
=================
pfnDecalIndex

register decal name on client
=================
*/
int GAME_EXPORT pfnDecalIndex( const char *m )
{
	if( COM_StringEmptyOrNULL( m ))
		return -1;

	for( int i = 1; i < MAX_DECALS && host.draw_decals[i][0]; i++ )
	{
		if( !Q_stricmp( host.draw_decals[i], m ))
			return i;
	}

	return -1;
}

static int SV_CanRewriteMessage( int msg_num )
{
	// feature is disabled
	if( !FBitSet( host.bugcomp, BUGCOMP_MESSAGE_REWRITE_FACILITY_FLAG ))
		return 0;

	switch( msg_num )
	{
	case svc_goldsrc_spawnstaticsound:
		return svc_sound;
	}

	return 0;
}

static qboolean SV_RewriteMessage( void )
{
	vec3_t origin;
	const char *sample = NULL;
	float vol, attn;
	int ent, pitch, flags, idx;
	int cmd;

	MSG_SeekToBit( &sv.multicast, svgame.msg_rewrite_pos, SEEK_SET );

	cmd = MSG_ReadCmd( &sv.multicast, NS_SERVER );

	switch( cmd )
	{
	case svc_goldsrc_spawnstaticsound:
		MSG_ReadVec3Coord( &sv.multicast, origin );
		idx   = MSG_ReadShort( &sv.multicast );
		vol   = MSG_ReadByte( &sv.multicast );
		attn  = MSG_ReadByte( &sv.multicast ) / 64.0f;
		ent   = MSG_ReadShort( &sv.multicast );
		pitch = MSG_ReadByte( &sv.multicast );
		flags = MSG_ReadByte( &sv.multicast );

		if( FBitSet( flags, SND_SENTENCE ))
			sample = va( "!%i", idx );
		else if( idx >= 0 && idx < MAX_SOUNDS )
			sample = sv.sound_precache[idx];

		if( COM_StringEmptyOrNULL( sample ))
		{
			Con_Printf( S_ERROR "%s: unrecognized sample in svc_spawnstaticsound, index %d, flags 0x%x\n", __func__, idx, flags );
			return false;
		}

		MSG_SeekToBit( &sv.multicast, svgame.msg_rewrite_pos, SEEK_SET );
		return SV_BuildSoundMsg( &sv.multicast, SV_EdictNum( ent ), CHAN_STATIC, sample, vol, attn, flags, pitch, origin );
	}

	return false;
}

/*
=============
pfnMessageBegin

=============
*/
static void GAME_EXPORT pfnMessageBegin( int msg_dest, int msg_num, const float *pOrigin, edict_t *ed )
{
	int	iSize;

	if( svgame.msg_started )
		Host_Error( "%s: New message started when msg '%s' has not been sent yet\n", __func__, svgame.msg_name );
	svgame.msg_started = true;

	// check range
	msg_num = bound( svc_bad, msg_num, 255 );

	svgame.msg_rewrite_index = 0;
	svgame.msg_rewrite_pos = 0;

	if( msg_num <= svc_lastmsg )
	{
		// check if we should rewrite this message into something else...
		if( SV_CanRewriteMessage( msg_num ))
		{
			svgame.msg_index = -SV_CanRewriteMessage( msg_num );
			svgame.msg_name = svc_goldsrc_strings[msg_num] ? svc_goldsrc_strings[msg_num] : svc_strings[msg_num];
			svgame.msg_rewrite_index = msg_num;
			svgame.msg_rewrite_pos = MSG_TellBit( &sv.multicast );
		}
		else
		{
			svgame.msg_index = -msg_num; // this is a system message
			svgame.msg_name = svc_strings[msg_num];
		}

		if( msg_num == svc_temp_entity )
			iSize = -1; // temp entity have variable size
		else iSize = 0;
	}
	else
	{
		int i;

		// check for existing
		for( i = 1; i < MAX_USER_MESSAGES && svgame.msg[i].name[0]; i++ )
		{
			if( svgame.msg[i].number == msg_num )
				break; // found
		}

		if( i == MAX_USER_MESSAGES )
		{
			Host_Error( "%s: tried to send unregistered message %i\n", __func__, msg_num );
			return;
		}

		svgame.msg_name = svgame.msg[i].name;
		iSize = svgame.msg[i].size;
		svgame.msg_index = i;
	}

	MSG_WriteCmdExt( &sv.multicast, msg_num, NS_SERVER, svgame.msg_name );

	// save message destination
	if( pOrigin ) VectorCopy( pOrigin, svgame.msg_org );
	else VectorClear( svgame.msg_org );

	if( iSize == -1 )
	{
		// variable sized messages sent size as first short
		svgame.msg_size_index = MSG_GetNumBytesWritten( &sv.multicast );
		MSG_WriteWord( &sv.multicast, 0 ); // reserve space for now
	}
	else svgame.msg_size_index = -1; // message has constant size

	svgame.msg_realsize = 0;
	svgame.msg_dest = msg_dest;
	svgame.msg_ent = ed;

	// enable message tracing
	svgame.msg_trace = sv_trace_messages.value != 0 &&
		msg_num > svc_lastmsg &&
		Q_strcmp( svgame.msg_name, "ReqState" );

	if( svgame.msg_trace ) Con_Printf( "^3%s( %i, %s )\n", __func__, msg_dest, svgame.msg_name );
}

/*
=============
pfnMessageEnd

=============
*/
static void GAME_EXPORT pfnMessageEnd( void )
{
	const char	*name = "Unknown";
	float		*org = NULL;
	word realsize;

	if( svgame.msg_name ) name = svgame.msg_name;
	if( !svgame.msg_started ) Host_Error( "%s: called with no active message\n", __func__ );
	svgame.msg_started = false;

	if( MSG_CheckOverflow( &sv.multicast ))
	{
		Con_Printf( S_ERROR "%s: %s has overflow multicast buffer\n", __func__, name );
		MSG_Clear( &sv.multicast );
		return;
	}

	if( svgame.msg_rewrite_index != 0 )
	{
		if( SV_RewriteMessage( ))
		{
			if( MSG_CheckOverflow( &sv.multicast ))
			{
				Con_Printf( S_ERROR "%s: %s has overflow multicast buffer (post-rewrite)\n", __func__, name );
				MSG_Clear( &sv.multicast );
				return;
			}
		}
		else
		{
			Con_Printf( S_ERROR "%s: failed to rewrite message %s\n", __func__, name );
			MSG_Clear( &sv.multicast );
			return;
		}
	}

	// check for system message
	if( svgame.msg_index < 0 )
	{
		if( svgame.msg_size_index != -1 )
		{
			// variable sized message
			if( svgame.msg_realsize > MAX_USERMSG_LENGTH )
			{
				Con_Printf( S_ERROR "%s: %s too long (more than %d bytes)\n", __func__, name, MAX_USERMSG_LENGTH );
				MSG_Clear( &sv.multicast );
				return;
			}
			else if( svgame.msg_realsize < 0 )
			{
				Con_Printf( S_ERROR "%s: %s writes NULL message\n", __func__, name );
				MSG_Clear( &sv.multicast );
				return;
			}

			realsize = LittleShort( svgame.msg_realsize );
			memcpy( &sv.multicast.pData[svgame.msg_size_index], &realsize, sizeof( realsize ));
		}
	}
	else if( svgame.msg[svgame.msg_index].size != -1 )
	{
		int	expsize = svgame.msg[svgame.msg_index].size;
		int	realsize = svgame.msg_realsize;

		// compare sizes
		if( expsize != realsize )
		{
			Con_Printf( S_ERROR "%s: %s expected %i bytes, it written %i. Ignored.\n", __func__, name, expsize, realsize );
			MSG_Clear( &sv.multicast );
			return;
		}
	}
	else if( svgame.msg_size_index != -1 )
	{
		// variable sized message
		if( svgame.msg_realsize > MAX_USERMSG_LENGTH )
		{
			Con_Printf( S_ERROR "%s: %s too long (more than %d bytes)\n", __func__, name, MAX_USERMSG_LENGTH );
			MSG_Clear( &sv.multicast );
			return;
		}
		else if( svgame.msg_realsize < 0 )
		{
			Con_Printf( S_ERROR "%s: %s writes NULL message\n", __func__, name );
			MSG_Clear( &sv.multicast );
			return;
		}

		realsize = LittleShort( svgame.msg_realsize );
		memcpy( &sv.multicast.pData[svgame.msg_size_index], &realsize, sizeof( realsize ));
	}
	else
	{
		// this should never happen
		Con_Printf( S_ERROR "%s: %s have encountered error\n", __func__, name );
		MSG_Clear( &sv.multicast );
		return;
	}

	// update some messages in case their was format was changed and we want to keep backward compatibility
	if( svgame.msg_index < 0 )
	{
		if(( svgame.msg_index == -svc_finale || svgame.msg_index == -svc_cutscene ) && svgame.msg_realsize == 0 )
			MSG_WriteChar( &sv.multicast, 0 ); // write null string
	}

	if( !VectorIsNull( svgame.msg_org )) org = svgame.msg_org;
	svgame.msg_dest = bound( MSG_BROADCAST, svgame.msg_dest, MSG_SPEC );

	SV_Multicast( svgame.msg_dest, org, svgame.msg_ent, true, false );

	if( svgame.msg_trace ) Con_Printf( "^3%s()\n", __func__ );
}

/*
=============
pfnWriteByte

=============
*/
static void GAME_EXPORT pfnWriteByte( int iValue )
{
	if( iValue == -1 ) iValue = 0xFF; // convert char to byte
	MSG_WriteByte( &sv.multicast, (byte)iValue );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %i )\n", __func__, iValue );
	svgame.msg_realsize++;
}

/*
=============
pfnWriteChar

=============
*/
static void GAME_EXPORT pfnWriteChar( int iValue )
{
	MSG_WriteChar( &sv.multicast, (signed char)iValue );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %i )\n", __func__, iValue );
	svgame.msg_realsize++;
}

/*
=============
pfnWriteShort

=============
*/
static void GAME_EXPORT pfnWriteShort( int iValue )
{
	MSG_WriteShort( &sv.multicast, (short)iValue );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %i )\n", __func__, iValue );
	svgame.msg_realsize += 2;
}

/*
=============
pfnWriteLong

=============
*/
static void GAME_EXPORT pfnWriteLong( int iValue )
{
	MSG_WriteLong( &sv.multicast, iValue );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %i )\n", __func__, iValue );
	svgame.msg_realsize += 4;
}

/*
=============
pfnWriteAngle

this is low-res angle
=============
*/
static void GAME_EXPORT pfnWriteAngle( float flValue )
{
	int	iAngle = ((int)(( flValue ) * 256 / 360) & 255);

	MSG_WriteChar( &sv.multicast, iAngle );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %f )\n", __func__, flValue );
	svgame.msg_realsize += 1;
}

/*
=============
pfnWriteCoord

=============
*/
static void GAME_EXPORT pfnWriteCoord( float flValue )
{
	MSG_WriteCoord( &sv.multicast, flValue );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %f )\n", __func__, flValue );
	svgame.msg_realsize += 2;
}

/*
=============
pfnWriteString

=============
*/
static void GAME_EXPORT pfnWriteString( const char *src )
{
	MSG_WriteString( &sv.multicast, src );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %s )\n", __func__, src );

	// NOTE: some messages with constant string length can be marked as known sized
	svgame.msg_realsize += Q_strlen( src ) + 1;
}

/*
=============
pfnWriteEntity

=============
*/
static void GAME_EXPORT pfnWriteEntity( int iValue )
{
	if( iValue < 0 || iValue >= svgame.numEntities )
		Host_Error( "%s: invalid entnumber %i\n", __func__, iValue );
	MSG_WriteShort( &sv.multicast, (short)iValue );
	if( svgame.msg_trace ) Con_Printf( "\t^3%s( %i )\n", __func__, iValue );
	svgame.msg_realsize += 2;
}

/*
=============
pfnCvar_RegisterServerVariable

standard path to register game variable
=============
*/
static void GAME_EXPORT pfnCvar_RegisterServerVariable( cvar_t *variable )
{
	if( variable != NULL )
	{
		SetBits( variable->flags, FCVAR_EXTDLL );
		Cvar_RegisterVariable( (convar_t *)variable );
	}
}

/*
=============
pfnAlertMessage

=============
*/
static void pfnAlertMessage( ALERT_TYPE type, char *szFmt, ... ) FORMAT_CHECK( 2 );
static void GAME_EXPORT pfnAlertMessage( ALERT_TYPE type, char *szFmt, ... )
{
	char	buffer[2048];
	va_list	args;

	if( type == at_logged && svs.maxclients > 1 )
	{
		va_start( args, szFmt );
		Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
		va_end( args );
		Log_Printf( "%s", buffer );
		return;
	}

	if( host_developer.value <= DEV_NONE )
		return;

	// g-cont: some mods have wrong aiconsole messages that crash the engine
	if( type == at_aiconsole && host_developer.value < DEV_EXTENDED )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	// check message for pass
	switch( type )
	{
	case at_notice:
		Con_Printf( S_NOTE "%s", buffer );
		break;
	case at_console:
		Con_Printf( "%s", buffer );
		break;
	case at_aiconsole:
		Con_DPrintf( "%s", buffer );
		break;
	case at_warning:
		Con_Printf( S_WARN "%s", buffer );
		break;
	case at_error:
		Con_Printf( S_ERROR "%s", buffer );
		break;
	}
}

/*
=============
pfnEngineFprintf

OBSOLETE, UNUSED
=============
*/
static void GAME_EXPORT pfnEngineFprintf( FILE *pfile, char *szFmt, ... )
{
}

/*
=============
pfnBuildSoundMsg

Customizable sound message
=============
*/
static void GAME_EXPORT pfnBuildSoundMsg( edict_t *pSource, int chan, const char *samp, float fvol, float attn, int fFlags, int pitch, int msg_dest, int msg_type, const float *pOrigin, edict_t *pSend )
{
	pfnMessageBegin( msg_dest, msg_type, pOrigin, pSend );
	SV_BuildSoundMsg( &sv.multicast, pSource, chan, samp, fvol * 255, attn, fFlags, pitch, pOrigin );
	pfnMessageEnd();
}

/*
=============
pfnPvAllocEntPrivateData

=============
*/
static void *GAME_EXPORT pfnPvAllocEntPrivateData( edict_t *pEdict, long cb )
{
	Assert( pEdict != NULL );

	SV_FreePrivateData( pEdict );

	if( cb > 0 )
	{
		size_t size = (cb + 15) & ~15;

		// a poke646 have memory corrupt in somewhere - this is trashed last sixteen bytes :(
#if XASH_GAMECUBE
		/* Soft-fail under GameCube map-load pressure. Mem_Calloc Host_Errors and
		 * -gcnewgame ClientPutInServer needs a recoverable miss so we can skip
		 * GetClassPtr linking against a NULL private block. */
		if( Sys_CheckParm( "-gcmap" ) || Sys_CheckParm( "-gcnewgame" )
			|| Sys_CheckParm( "-gcmenuplaystart" ) || GC_MapLoadMemoryOpt() )
		{
			qboolean used_fallback = false;

			/* Reserve a stable static block for the large CSoundEnt private
			 * object so sound-event tracking remains available under MEM1 load. */
			if( !pEdict->pvPrivateData
				&& ( Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcmenuplaystart" ))
				&& !gc_soundent_fallback_used
				&& NUM_FOR_EDICT( pEdict ) != 1
				&& size >= 1800 && size <= sizeof( gc_soundent_private_fallback ))
			{
				memset( gc_soundent_private_fallback, 0, sizeof( gc_soundent_private_fallback ));
				pEdict->pvPrivateData = gc_soundent_private_fallback;
				gc_soundent_fallback_used = true;
				used_fallback = true;
				Con_Reportf( "Xash3D GameCube: G369 ent private data using static soundent fallback size=%zu edict=%d\n",
					size, NUM_FOR_EDICT( pEdict ));
			}
			/* Avoid libc malloc during map-load sessions. On c0a0 it stalled
			 * indefinitely when the fragmented MEM1 heap reached its tip. */
			/* Small map entities use the bounded private-data slab first. Reuse
			 * holes left by destroyed entities before requesting heap memory. */
			/* Prefer slab/overflow up through 2 KiB so GetClassPtr never sees
			 * NULL mid-construction when tip heap is empty (c1a1f ~entity 408). */
			if( !pEdict->pvPrivateData && size <= 2048
				&& (( pEdict->pvPrivateData = SV_GCPrivateSlabAlloc( size )) != NULL
					|| ( pEdict->pvPrivateData = SV_GCPrivateOverflowAlloc( size )) != NULL ))
			{
				const int edict_index = NUM_FOR_EDICT( pEdict );
				memset( pEdict->pvPrivateData, 0, size );
				gc_ent_priv_slab_hits++;
				if( edict_index >= 0 && edict_index < MAX_EDICTS )
					gc_ent_priv_slab_size[edict_index] = (uint16_t)size;
				used_fallback = true;
				if( gc_ent_priv_slab_hits <= 3 || ( gc_ent_priv_slab_hits & 15 ) == 0 )
					Con_Reportf( "Xash3D GameCube: G330 ent private slab size=%zu edict=%d hits=%d used=%zu overflow=%zu\n",
						size, edict_index, gc_ent_priv_slab_hits, gc_ent_priv_slab_used, gc_ent_priv_overflow_used );
			}
			/* Never fall back to libc malloc here: tip MEM1 Q_malloc can
			 * spin forever once the heap is fragmented (c0a0 / c2a5f). Soft
			 * NULL lets SV_AllocPrivateData skip skippable classes. */
			if( !pEdict->pvPrivateData )
			{
				Con_Reportf( S_WARN "Xash3D GameCube: ent private slab+overflow miss size=%zu edict=%d classname=%s\n",
					size, NUM_FOR_EDICT( pEdict ), SV_ClassName( pEdict ));
			}
			/* Player spawn must not return NULL — GetClassPtr crashes. Keep a
			 * static fallback for the single-player client edict under New Game. */
			if( !pEdict->pvPrivateData
				&& NUM_FOR_EDICT( pEdict ) == 1 && size <= sizeof( gc_player_private_fallback ))
			{
				memset( gc_player_private_fallback, 0, sizeof( gc_player_private_fallback ));
				pEdict->pvPrivateData = gc_player_private_fallback;
				used_fallback = true;
				Con_Reportf( "Xash3D GameCube: ent private data using static player fallback size=%zu\n",
					size );
			}
			if( pEdict->pvPrivateData )
			{
				if( !used_fallback )
					memset( pEdict->pvPrivateData, 0, size );
				/* Only reserved client edicts can host CBasePlayer. CSoundEnt is
				 * also larger than 1800 bytes and is commonly allocated on edict 2. */
				if( size >= 1800 && NUM_FOR_EDICT( pEdict ) >= 1
					&& NUM_FOR_EDICT( pEdict ) <= svs.maxclients )
				{
					extern edict_t *gc_hl_player_priv_edict;
					extern void *gc_hl_player_priv_ptr;
					gc_hl_player_priv_edict = pEdict;
					gc_hl_player_priv_ptr = pEdict->pvPrivateData;
				}
				if(( Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcmenuplaystart" )) && size >= 1024 )
					Con_Reportf( "Xash3D GameCube: ent private data malloc size=%zu edict=%d classname=%s\n",
						size, NUM_FOR_EDICT( pEdict ), SV_ClassName( pEdict ));
			}
			else
			{
				Con_Reportf( S_ERROR "Xash3D GameCube: ent private data malloc failed size=%zu edict=%d classname=%s\n",
					size, NUM_FOR_EDICT( pEdict ), SV_ClassName( pEdict ));
			}
		}
		else
#endif
			pEdict->pvPrivateData = Mem_Calloc( svgame.mempool, size );
	}

	return pEdict->pvPrivateData;
}

/*
=============
pfnPvEntPrivateData

we already have copy of this function in 'enginecallback.h' :-)
=============
*/
static void *GAME_EXPORT pfnPvEntPrivateData( edict_t *pEdict )
{
	if( pEdict )
		return pEdict->pvPrivateData;
	return NULL;
}


static struct str64_s
{
	size_t maxstringarray;
	qboolean allowdup;
	qboolean dynamic;
	char *staticstringarray;
	char *pstringarray;
	char *pstringarraystatic;
	char *pstringbase;
	char *poldstringbase;
	char *plast;
	size_t maxalloc;
	size_t numdups;
	size_t numoverflows;
	size_t totalalloc;
} str64 MAYBE_UNUSED;

/*
==================
SV_EmptyStringPool

Free strings on server stop. Reset string pointer on 64 bits
==================
*/
void SV_EmptyStringPool( qboolean clear_stats )
{
#if XASH_64BIT
	if( str64.dynamic ) // switch only after array fill (more space for multiplayer games)
	{
		str64.pstringbase = str64.pstringarray;
	}
	else
	{
		str64.pstringbase = str64.poldstringbase = str64.pstringarraystatic;
		str64.plast = str64.pstringbase + 1;
	}

	if( clear_stats )
	{
		str64.maxalloc = 0;
		str64.totalalloc = 0;
		str64.numdups = 0;
		str64.numoverflows = 0;
	}
#endif // !XASH_64BIT
#if XASH_GAMECUBE && !XASH_64BIT
	gc_string_arena_used = 0;
	if( clear_stats )
		gc_string_arena_overflows = 0;
#endif
}

/*
===============
SV_SetStringArrayMode

use different arrays on 64 bit platforms
set dynamic after complete server spawn
this helps not to lose strings that belongs to static game part
===============
*/
void SV_SetStringArrayMode( qboolean dynamic )
{
#if XASH_64BIT
	Con_Reportf( "%s(%d) %d\n", __func__, dynamic, str64.dynamic );

	if( dynamic == str64.dynamic )
		return;

	str64.dynamic = dynamic;

	SV_EmptyStringPool( false );
#endif // !XASH_64BIT
}

#if XASH_AMD64 && XASH_LINUX && !XASH_ANDROID
#define USE_MMAP 1
#include <sys/mman.h>
#endif

/*
==================
SV_AllocStringPool

alloc string pool on 32bit platforms
alloc string array near the server library on 64bit platforms if possible
alloc string array somewhere if not (SV_MakeString will not work. Always call SV_AllocString instead, or crash)
this case need patched game dll with SV_MakeString checking ptrdiff size
==================
*/
static void SV_AllocStringPool( void )
{
#if XASH_64BIT
	void *ptr = NULL;
	string lenstr;

	Con_Reportf( "%s()\n", __func__ );
	if( Sys_GetParmFromCmdLine( "-str64alloc", lenstr ))
	{
		str64.maxstringarray = Q_atoi( lenstr );
		if( str64.maxstringarray < 1024 || str64.maxstringarray >= INT_MAX )
			str64.maxstringarray = 65536 * ceil( GI->max_edicts / 1024.0f );
	}
	else str64.maxstringarray = 65536 * ceil( GI->max_edicts / 1024.0f );
	if( Sys_CheckParm( "-str64dup" ) )
		str64.allowdup = true;

#if USE_MMAP
	{
		uint flags;
		size_t pagesize = sysconf( _SC_PAGESIZE );
		int arrlen = (str64.maxstringarray * 2) & ~(pagesize - 1);
		void *base = svgame.dllFuncs.pfnGameInit;
		void *start = svgame.hInstance - arrlen;

#if defined(MAP_ANON)
		flags = MAP_ANON | MAP_PRIVATE;
#elif defined(MAP_ANONYMOUS)
		flags = MAP_ANONYMOUS | MAP_PRIVATE;
#endif

		while( start - base > INT_MIN )
		{
			void *mapptr = mmap((void*)((unsigned long)start & ~(pagesize - 1)), arrlen, PROT_READ | PROT_WRITE, flags, 0, 0 );
			if( mapptr && mapptr != (void*)-1 && mapptr - base > INT_MIN && mapptr - base < INT_MAX )
			{
				ptr = mapptr;
				break;
			}
			if( mapptr ) munmap( mapptr, arrlen );
			start -= arrlen;
		}

		if( !ptr )
		{
			start = base;
			while( start - base < INT_MAX )
			{
				void *mapptr = mmap((void*)((unsigned long)start & ~(pagesize - 1)), arrlen, PROT_READ | PROT_WRITE, flags, 0, 0 );
				if( mapptr && mapptr != (void*)-1  && mapptr - base > INT_MIN && mapptr - base < INT_MAX )
				{
					ptr = mapptr;
					break;
				}
				if( mapptr ) munmap( mapptr, arrlen );
				start += arrlen;
			}
		}


		if( ptr )
		{
			Con_Reportf( "%s: Allocated string array near the server library: %p %p\n", __func__, base, ptr );
		}
		else
		{
			Con_Reportf( "%s: Failed to allocate string array near the server library!\n", __func__ );
			ptr = str64.staticstringarray = Mem_Calloc( host.mempool, str64.maxstringarray * 2 );
		}
	}
#else // !USE_MMAP
	ptr = str64.staticstringarray = Mem_Calloc( host.mempool, str64.maxstringarray * 2 );
#endif // !USE_MMAP

	str64.pstringarray = ptr;
	str64.pstringarraystatic = (byte*)ptr + str64.maxstringarray;
	str64.pstringbase = str64.poldstringbase = ptr;
	str64.plast = (byte*)ptr + 1;
	svgame.globals->pStringBase = ptr;
#else // !XASH_64BIT
	svgame.globals->pStringBase = "";
#endif // !XASH_64BIT

#if XASH_GAMECUBE
	svgame.stringspool = Mem_AllocPoolExt( "Server Strings", MEM_SMALL_ALLOC_OPT );
#else
	svgame.stringspool = Mem_AllocPool( "Server Strings" );
#endif
}

static void SV_FreeStringPool( void )
{
#if XASH_64BIT
	Con_Reportf( "%s()\n", __func__ );

#if USE_MMAP
	if( str64.pstringarray != str64.staticstringarray )
		munmap( str64.pstringarray, (str64.maxstringarray * 2) & ~(sysconf( _SC_PAGESIZE ) - 1) );
	else
#endif // USE_MMAP
	{
		Mem_Free( str64.staticstringarray );
	}
#else // !XASH_64BIT
	Mem_FreePool( &svgame.stringspool );
#endif // !XASH_64BIT
}

/*
============
SV_ProcessString

Process newly allocated string
pass NULL pointer to dst to get required length incl. null terminator
============
*/
static uint SV_ProcessString( char *dst, const char *src )
{
	const char *p;
	uint i = 0;

	p = src;

	while( *p )
	{
		if( *p == '\\' )
		{
			char replace = 0;

			switch( p[1] )
			{
			case 'n': replace = '\n'; break;
			// GoldSrc doesn't replace these symbols
			// but old hack in pfnWriteString did
			case 'r': replace = '\r'; break;
			case 't': replace = '\t'; break;
			}

			if( replace )
			{
				if( dst )
					dst[i] = replace;
				i++;
				p += 2;
				continue;
			}
		}

		if( dst )
			dst[i] = *p;
		i++;
		p++;
	}

	// null terminator
	if( dst )
		dst[i] = '\0';
	i++;

	return i;
}

/*
=============
SV_AllocString

allocate new engine string
on 64bit platforms find in array string if deduplication enabled (default)
if not found, add to array
use -str64dup to disable deduplication, -str64alloc to set array size
=============
*/
string_t GAME_EXPORT SV_AllocString( const char *szValue )
{
#if XASH_GAMECUBE && !XASH_64BIT
	/* Classnames/model names recur throughout retail maps. A separate heap
	 * allocation per occurrence exhausted Server Strings during c5a1 spawn.
	 * Escaped values still take the normal normalization path below. */
	if( !svgame.physFuncs.pfnAllocString && !strchr( szValue, '\\' ))
	{
		const char *existing = Mem_FindPoolString( svgame.stringspool, szValue );
		size_t i;

		if( existing )
			return existing - svgame.globals->pStringBase;
		for( i = 0; gc_string_arena && i < gc_string_arena_used; )
		{
			const char *s = &gc_string_arena[i];
			size_t slen = Q_strlen( s ) + 1;

			if( !Q_strcmp( s, szValue ))
				return s - svgame.globals->pStringBase;
			i += slen ? slen : 1;
		}
	}
#endif
	uint len = SV_ProcessString( NULL, szValue );
	char *processed_string = NULL;
	char *dupe_string = NULL;
	qboolean found_dupe = false;

	(void)dupe_string;
	(void)found_dupe;
#if XASH_GAMECUBE && !XASH_64BIT
	/* Prefer the BSS arena whenever tip MEM1 is already thin. Mem_Calloc →
	 * Q_malloc can spin forever on a fragmented heap before soft-NULL
	 * (c2a5e hung at entity 256 path_track after reclaim with ~13 KiB free). */
	if( GC_MapLoadMemoryOpt() && GC_MemHeapFreeBytes() < ( 48u * 1024u ) && len <= 512 )
	{
		char temp[512];
		uint use_len = len;

		if( use_len > sizeof( temp ))
			use_len = sizeof( temp );
		memset( temp, 0, use_len );
		SV_ProcessString( temp, szValue );
		processed_string = GC_StringArenaAlloc( temp, (uint)( Q_strlen( temp ) + 1 ));
		if( processed_string )
			return processed_string - svgame.globals->pStringBase;
	}
#endif
	processed_string = Mem_Calloc( svgame.stringspool, len );
	if( !processed_string )
	{
#if XASH_GAMECUBE && !XASH_64BIT
		/* Tip soft-NULL after reclaim — stage into BSS arena (c1a1f 14-byte
		 * Host_Error, hardware 20261104). Process into a stack temp first. */
		{
			char temp[512];
			uint use_len = len;

			if( use_len > sizeof( temp ))
				use_len = sizeof( temp );
			memset( temp, 0, use_len );
			SV_ProcessString( temp, szValue );
			processed_string = GC_StringArenaAlloc( temp, (uint)( Q_strlen( temp ) + 1 ));
			if( processed_string )
			{
				Con_Reportf( "Xash3D GameCube: AllocString arena fallback '%s' len=%u used=%zu\n",
					temp, (uint)( Q_strlen( temp ) + 1 ), gc_string_arena_used );
				return processed_string - svgame.globals->pStringBase;
			}
			Host_Error( "%s: unable to allocate %u bytes for server string '%s'\n", __func__, len, szValue );
		}
#elif XASH_GAMECUBE
		Host_Error( "%s: unable to allocate %u bytes for server string '%s'\n", __func__, len, szValue );
#endif
		return 0;
	}

	SV_ProcessString( processed_string, szValue );

	if( svgame.physFuncs.pfnAllocString != NULL )
	{
		string_t i = svgame.physFuncs.pfnAllocString( processed_string );
		Mem_Free( processed_string );
		return i;
	}

#if XASH_64BIT
	if( !str64.allowdup )
	{
		for( dupe_string = str64.poldstringbase + 1; dupe_string < str64.plast; dupe_string += Q_strlen( dupe_string ) + 1 )
		{
			if( !Q_strcmp( dupe_string, processed_string ))
			{
				found_dupe = true;
				break;
			}
		}
	}

	if( !found_dupe )
	{
		if( str64.plast - str64.poldstringbase + len + 1 > str64.maxstringarray )
		{
			str64.plast = str64.pstringbase + 1;
			str64.poldstringbase = str64.pstringbase;
			str64.numoverflows++;
		}

		//MsgDev( D_NOTE, "SV_AllocString: %ld %s\n", str64.plast - svgame.globals->pStringBase, processed_string );
		Q_strncpy( str64.plast, processed_string, str64.maxstringarray - (str64.plast - str64.poldstringbase) );
		str64.totalalloc += len;

		dupe_string = str64.plast;
		str64.plast += len;
	}
	else
	{
		str64.numdups++;
		//MsgDev( D_NOTE, "SV_AllocString: dup %ld %s\n", dupe_string - svgame.globals->pStringBase, processed_string );
	}

	if( dupe_string - str64.pstringarray > str64.maxalloc )
		str64.maxalloc = dupe_string - str64.pstringarray;

	Mem_Free( processed_string );

	return dupe_string - svgame.globals->pStringBase;
#else // !XASH_64BIT
	return processed_string - svgame.globals->pStringBase;
#endif // !XASH_64BIT
}

void SV_PrintStr64Stats_f( void )
{
#if XASH_64BIT
	Con_Printf( "====================\n" );
	Con_Printf( "64 bit string pool statistics\n" );
	Con_Printf( "====================\n" );
	Con_Printf( "string array size: %zu\n", str64.maxstringarray );
	Con_Printf( "total alloc %zu\n", str64.totalalloc );
	Con_Printf( "maximum array usage: %zu\n", str64.maxalloc );
	Con_Printf( "overflow counter: %zu\n", str64.numoverflows );
	Con_Printf( "dup string counter: %zu\n", str64.numdups );
#else // !XASH_64BIT
	Con_Printf( "Not implemented\n" );
#endif // !XASH_64BIT
}

/*
=============
SV_MakeString

make constant string
=============
*/
string_t SV_MakeString( const char *szValue )
{
	if( svgame.physFuncs.pfnMakeString != NULL )
		return svgame.physFuncs.pfnMakeString( szValue );
#if XASH_64BIT
	{
		long long ptrdiff = szValue - svgame.globals->pStringBase;
		if( ptrdiff > INT_MAX || ptrdiff < INT_MIN )
			return SV_AllocString(szValue);
		else
			return (int)ptrdiff;
	}
#else // !XASH_64BIT
	return szValue - svgame.globals->pStringBase;
#endif // !XASH_64BIT
}

/*
=============
SV_GetString

=============
*/
const char *GAME_EXPORT SV_GetString( string_t iString )
{
	if( svgame.physFuncs.pfnGetString != NULL )
		return svgame.physFuncs.pfnGetString( iString );
	return (svgame.globals->pStringBase + iString);
}

/*
=============
pfnGetVarsOfEnt

=============
*/
static entvars_t *GAME_EXPORT pfnGetVarsOfEnt( edict_t *pEdict )
{
	if( pEdict )
		return &pEdict->v;
	return NULL;
}

/*
=============
pfnPEntityOfEntOffset

=============
*/
static edict_t *GAME_EXPORT pfnPEntityOfEntOffset( int iEntOffset )
{
	return (edict_t *)((byte *)svgame.edicts + iEntOffset);
}

/*
=============
pfnEntOffsetOfPEntity

=============
*/
static int GAME_EXPORT pfnEntOffsetOfPEntity( const edict_t *pEdict )
{
	return (byte *)pEdict - (byte *)svgame.edicts;
}

/*
=============
pfnIndexOfEdict

=============
*/
int GAME_EXPORT pfnIndexOfEdict( const edict_t *pEdict )
{
	if( !pEdict ) return 0; // world ?

	int number = NUM_FOR_EDICT( pEdict );
	if( number < 0 || number > GI->max_edicts )
		Host_Error( "bad entity number %d\n", number );
	return number;
}

/*
=============
pfnPEntityOfEntIndex

=============
*/
static edict_t *pfnPEntityOfEntIndexBroken( int iEntIndex )
{
	// have to be bug-compatible with GoldSrc in this function
	return SV_PEntityOfEntIndex( iEntIndex, false );
}

/*
=============
pfnPEntityOfEntIndexAllEntities

=============
*/
static edict_t *GAME_EXPORT pfnPEntityOfEntIndexAllEntities( int iEntIndex )
{
	return SV_PEntityOfEntIndex( iEntIndex, true );
}

/*
=============
pfnFindEntityByVars

debug thing
=============
*/
static edict_t *GAME_EXPORT pfnFindEntityByVars( entvars_t *pvars )
{
	// don't pass invalid arguments
	if( !pvars ) return NULL;

	for( int i = 0; i < GI->max_edicts; i++ )
	{
		edict_t *pEdict = SV_EdictNum( i );

		// g-cont: we should compare pointers
		if( &pEdict->v == pvars )
			return pEdict; // found it
	}

	return NULL;
}

/*
=============
pfnGetModelPtr

returns pointer to a studiomodel
=============
*/
static void *pfnGetModelPtr( edict_t *pEdict )
{
	model_t	*mod;

	if( !SV_IsValidEdict( pEdict ))
		return NULL;

	mod = SV_ModelHandle( pEdict->v.modelindex );
	return Mod_StudioExtradata( mod );
}

/*
=============
SV_SendUserReg

=============
*/
void SV_SendUserReg( sizebuf_t *msg, sv_user_message_t *user )
{
	MSG_BeginServerCmd( msg, svc_usermessage );
	MSG_WriteByte( msg, user->number );
	MSG_WriteWord( msg, (word)user->size );
	MSG_WriteString( msg, user->name );
}

/*
=============
pfnRegUserMsg

=============
*/
static int GAME_EXPORT pfnRegUserMsg( const char *pszName, int iSize )
{
	int	i;

	if( COM_StringEmptyOrNULL( pszName ))
		return svc_bad;

	if( Q_strlen( pszName ) >= sizeof( svgame.msg[0].name ))
	{
		Con_Printf( S_ERROR "%s: too long name %s\n", __func__, pszName );
		return svc_bad; // force error
	}

	if( iSize > MAX_USERMSG_LENGTH )
	{
		Con_Printf( S_ERROR "%s: %s has too big size %i\n", __func__, pszName, iSize );
		return svc_bad; // force error
	}

	// make sure what size inrange
	iSize = bound( -1, iSize, MAX_USERMSG_LENGTH );

	// message 0 is reserved for svc_bad
	for( i = 1; i < MAX_USER_MESSAGES && svgame.msg[i].name[0]; i++ )
	{
		// see if already registered
		if( !Q_strcmp( svgame.msg[i].name, pszName ))
			return svgame.msg[i].number;
	}

	if( i == MAX_USER_MESSAGES )
	{
		Con_Printf( S_ERROR "%s: user messages limit exceeded\n", __func__ );
		return svc_bad;
	}

	// register new message
	Q_strncpy( svgame.msg[i].name, pszName, sizeof( svgame.msg[i].name ));
	svgame.msg[i].number = svc_lastmsg + i;
	svgame.msg[i].size = iSize;

	if( sv.state == ss_active )
	{
		// tell the client about new user message
		SV_SendUserReg( &sv.multicast, &svgame.msg[i] );
		SV_Multicast( MSG_ALL, NULL, NULL, false, false );
	}

	return svgame.msg[i].number;
}

/*
=============
pfnAnimationAutomove

OBSOLETE, UNUSED
=============
*/
static void GAME_EXPORT pfnAnimationAutomove( const edict_t* pEdict, float flTime )
{
}

/*
=============
pfnGetBonePosition

=============
*/
static void GAME_EXPORT pfnGetBonePosition( const edict_t* pEdict, int iBone, float *rgflOrigin, float *rgflAngles )
{
	if( !SV_IsValidEdict( pEdict ))
		return;
	Mod_GetBonePosition( pEdict, iBone, rgflOrigin, rgflAngles );
}

/*
=============
pfnFunctionFromName

=============
*/
static void *GAME_EXPORT pfnFunctionFromName( const char *pName )
{
	return COM_FunctionFromName_SR( svgame.hInstance, pName );
}

/*
=============
pfnNameForFunction

=============
*/
static const char *GAME_EXPORT pfnNameForFunction( void *function )
{
	return COM_NameForFunction( svgame.hInstance, function );
}

/*
=============
pfnClientPrintf

=============
*/
static void GAME_EXPORT pfnClientPrintf( edict_t* pEdict, PRINT_TYPE ptype, const char *szMsg )
{
	sv_client_t	*client;

	if(( client = SV_ClientFromEdict( pEdict, false )) == NULL )
	{
		Con_Printf( "tried to sprint to a non-client\n" );
		return;
	}

	if( FBitSet( client->flags, FCL_FAKECLIENT ))
		return;

	switch( ptype )
	{
	case print_console:
	case print_chat:
		SV_ClientPrintf( client, "%s", szMsg );
		break;
	case print_center:
		MSG_BeginServerCmd( &client->netchan.message, svc_centerprint );
		MSG_WriteString( &client->netchan.message, szMsg );
		break;
	}
}

/*
=============
pfnServerPrint

print to the server console
=============
*/
static void GAME_EXPORT pfnServerPrint( const char *szMsg )
{
	if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		SV_BroadcastPrintf( NULL, "%s", szMsg );
	else Con_Printf( "%s", szMsg );
}

/*
=============
pfnGetAttachment

=============
*/
static void GAME_EXPORT pfnGetAttachment( const edict_t *pEdict, int iAttachment, float *rgflOrigin, float *rgflAngles )
{
	if( !SV_IsValidEdict( pEdict ))
		return;
	Mod_StudioGetAttachment( pEdict, iAttachment, rgflOrigin, rgflAngles );
}

/*
=============
pfnCrosshairAngle

=============
*/
static void GAME_EXPORT pfnCrosshairAngle( const edict_t *pClient, float pitch, float yaw )
{
	sv_client_t	*client;

	if(( client = SV_ClientFromEdict( pClient, true )) == NULL )
		return;

	// fakeclients ignores it silently
	if( FBitSet( client->flags, FCL_FAKECLIENT ))
		return;

	if( pitch > 180.0f ) pitch -= 360;
	if( pitch < -180.0f ) pitch += 360;
	if( yaw > 180.0f ) yaw -= 360;
	if( yaw < -180.0f ) yaw += 360;

	MSG_BeginServerCmd( &client->netchan.message, svc_crosshairangle );
	MSG_WriteChar( &client->netchan.message, pitch * 5 );
	MSG_WriteChar( &client->netchan.message, yaw * 5 );
}

/*
=============
pfnSetView

=============
*/
static void GAME_EXPORT pfnSetView( const edict_t *pClient, const edict_t *pViewent )
{
	sv_client_t	*client;
	int		viewEnt;

	if( !SV_IsValidEdict( pClient ))
		return;

	if(( client = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "%s: not a client!\n", __func__ );
		return;
	}

	if( !SV_IsValidEdict( pViewent ) || pClient == pViewent )
		client->pViewEntity = NULL; // just reset viewentity
	else client->pViewEntity = (edict_t *)pViewent;

	// fakeclients ignore to send client message (but can see into the trigger_camera through the PVS)
	if( FBitSet( client->flags, FCL_FAKECLIENT ))
		return;

	if( client->pViewEntity )
		viewEnt = NUM_FOR_EDICT( client->pViewEntity );
	else viewEnt = NUM_FOR_EDICT( client->edict );

	MSG_BeginServerCmd( &client->netchan.message, svc_setview );
	MSG_WriteWord( &client->netchan.message, viewEnt );
}

/*
=============
pfnStaticDecal

=============
*/
static void GAME_EXPORT pfnStaticDecal( const float *origin, int decalIndex, int entityIndex, int modelIndex )
{
	SV_CreateDecal( &sv.signon, origin, decalIndex, entityIndex, modelIndex, FDECAL_PERMANENT, 1.0f );
}

/*
=============
pfnIsDedicatedServer

=============
*/
static int GAME_EXPORT pfnIsDedicatedServer( void )
{
	return Host_IsDedicated();
}

/*
=============
pfnGetPlayerWONId

OBSOLETE, UNUSED
=============
*/
static uint GAME_EXPORT pfnGetPlayerWONId( edict_t *e )
{
	return (uint)-1;
}

/*
=============
pfnIsMapValid

vaild map must contain one info_player_deatchmatch
=============
*/
int GAME_EXPORT pfnIsMapValid( char *filename )
{
	uint	flags = SV_MapIsValid( filename, NULL );

	if( FBitSet( flags, MAP_IS_EXIST ))
		return true;
	return false;
}

/*
=============
pfnCvar_RegisterEngineVariable

use with precaution: this cvar will NOT unlinked
after game.dll is unloaded
=============
*/
static void GAME_EXPORT pfnCvar_RegisterEngineVariable( cvar_t *variable )
{
	Cvar_RegisterVariable( (convar_t *)variable );
}

/*
=============
pfnFadeClientVolume

=============
*/
static void GAME_EXPORT pfnFadeClientVolume( const edict_t *pEdict, int fadePercent, int fadeOutSeconds, int holdTime, int fadeInSeconds )
{
	sv_client_t	*cl;

	if(( cl = SV_ClientFromEdict( pEdict, true )) == NULL )
		return;

	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
		return;

	MSG_BeginServerCmd( &cl->netchan.message, svc_soundfade );
	MSG_WriteByte( &cl->netchan.message, fadePercent );
	MSG_WriteByte( &cl->netchan.message, holdTime );
	MSG_WriteByte( &cl->netchan.message, fadeOutSeconds );
	MSG_WriteByte( &cl->netchan.message, fadeInSeconds );
}

/*
=============
pfnSetClientMaxspeed

fakeclients can be changed speed to
=============
*/
static void GAME_EXPORT pfnSetClientMaxspeed( const edict_t *pEdict, float fNewMaxspeed )
{
	sv_client_t	*cl;

	// not spawned clients allowed
	if(( cl = SV_ClientFromEdict( pEdict, false )) == NULL )
		return;

	// GoldSrc doesn't bound the value to the movevar here
	fNewMaxspeed = bound( -svgame.movevars.maxspeed, fNewMaxspeed, svgame.movevars.maxspeed );

	// There isn't any reference to "maxspd" anywhere except some commented-out code in SDK
	Info_SetValueForKeyf( cl->physinfo, "maxspd", MAX_INFO_STRING, "%.f", fNewMaxspeed );

	// cl->edict is NULL once the client has dropped, but game dlls (e.g. Marine Bot)
	// may still call this for its edict, so write to the edict we've been given
	((edict_t *)pEdict)->v.maxspeed = fNewMaxspeed;
}

/*
=============
pfnRunPlayerMove

=============
*/
static void GAME_EXPORT pfnRunPlayerMove( edict_t *pClient, const float *viewangles, float fmove, float smove, float upmove, word buttons, byte impulse, byte msec )
{
	sv_client_t	*cl, *oldcl;
	uint		seed;

	if(( cl = SV_ClientFromEdict( pClient, true )) == NULL )
		return;

	if( !FBitSet( cl->flags, FCL_FAKECLIENT ))
		return; // only fakeclients allows

	oldcl = sv.current_client;

	sv.current_client = SV_ClientFromEdict( pClient, true );
	sv.current_client->timebase = (sv.time + sv.frametime) - ((double)msec / 1000.0);

	usercmd_t cmd =
	{
		.viewangles = Vec3( viewangles ),
		.forwardmove = fmove,
		.sidemove = smove,
		.upmove = upmove,
		.buttons = buttons,
		.impulse = impulse,
		.msec = msec,
	};

	seed = COM_RandomLong( 0, 0x7fffffff ); // full range

	SV_RunCmd( cl, &cmd, seed );

	cl->lastcmd = cmd;
	sv.current_client = oldcl;
}

/*
=============
pfnNumberOfEntities

returns actual entity count
=============
*/
int GAME_EXPORT pfnNumberOfEntities( void )
{
	int	total = 0;

	for( int i = 0; i < svgame.numEntities; i++ )
	{
		if( svgame.edicts[i].free )
			continue;
		total++;
	}

	return total;
}

/*
=============
pfnGetInfoKeyBuffer

=============
*/
static char *GAME_EXPORT pfnGetInfoKeyBuffer( edict_t *e )
{
	sv_client_t	*cl;

	// NULL passes localinfo
	if( !SV_IsValidEdict( e ))
		return svs.localinfo;

	// world passes serverinfo
	if( e == svgame.edicts )
		return svs.serverinfo;

	// userinfo for specified edict
	if(( cl = SV_ClientFromEdict( e, false )) != NULL )
		return cl->userinfo;

	return (char*)""; // assume error
}

/*
=============
pfnSetValueForKey

=============
*/
static void GAME_EXPORT pfnSetValueForKey( char *infobuffer, char *key, char *value )
{
	if( infobuffer == svs.localinfo )
		Info_SetValueForStarKey( infobuffer, key, value, MAX_LOCALINFO_STRING );
	else if( infobuffer == svs.serverinfo )
		Info_SetValueForStarKey( infobuffer, key, value, MAX_SERVERINFO_STRING );
	else Con_Printf( S_ERROR "can't set client keys with SetValueForKey\n" );
}

/*
=============
pfnSetClientKeyValue

=============
*/
static void GAME_EXPORT pfnSetClientKeyValue( int clientIndex, char *infobuffer, char *key, char *value )
{
	sv_client_t	*cl;

	if( infobuffer == svs.localinfo || infobuffer == svs.serverinfo )
		return;

	clientIndex -= 1;

	if( !svs.clients || clientIndex < 0 || clientIndex >= svs.maxclients )
		return;

	// value not changed?
	if( !Q_strcmp( Info_ValueForKey( infobuffer, key ), value ))
		return;

	cl = &svs.clients[clientIndex];

	Info_SetValueForStarKey( infobuffer, key, value, MAX_INFO_STRING );
	SetBits( cl->flags, FCL_RESEND_USERINFO );
	cl->next_sendinfotime = 0.0;	// send immediately
}

/*
=============
pfnGetPhysicsKeyValue

=============
*/
static const char *GAME_EXPORT pfnGetPhysicsKeyValue( const edict_t *pClient, const char *key )
{
	sv_client_t	*cl;

	// pfnUserInfoChanged passed
	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "%s: tried to a non-client!\n", __func__ );
		return "";
	}

	return Info_ValueForKey( cl->physinfo, key );
}

/*
=============
pfnSetPhysicsKeyValue

=============
*/
static void GAME_EXPORT pfnSetPhysicsKeyValue( const edict_t *pClient, const char *key, const char *value )
{
	sv_client_t	*cl;

	// pfnUserInfoChanged passed
	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "%s: tried to a non-client!\n", __func__ );
		return;
	}

	Info_SetValueForKey( cl->physinfo, key, value, MAX_INFO_STRING );
}

/*
=============
pfnGetPhysicsInfoString

=============
*/
static const char *GAME_EXPORT pfnGetPhysicsInfoString( const edict_t *pClient )
{
	sv_client_t	*cl;

	// pfnUserInfoChanged passed
	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
	{
		Con_Printf( S_ERROR "%s: tried to a non-client!\n", __func__ );
		return "";
	}

	return cl->physinfo;
}

/*
=============
pfnPrecacheEvent

register or returns already registered event id
a type of event is ignored at this moment
=============
*/
static word GAME_EXPORT pfnPrecacheEvent( int type, const char *psz )
{
	return (word)SV_EventIndex( psz );
}

/*
=============
pfnPlaybackEvent

=============
*/
void GAME_EXPORT SV_PlaybackEventFull( int flags, const edict_t *pInvoker, word eventindex, float delay, float *origin,
	float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 )
{
	sv_client_t	*cl;
	event_state_t	*es;
	event_args_t	args;
	event_info_t	*ei = NULL;
	int		j, bestslot;
	int		invokerIndex;
	byte		*mask = NULL;
	vec3_t		pvspoint;

	if( FBitSet( flags, FEV_CLIENT ))
		return;	// someone stupid joke

	// first check event for out of bounds
	if( eventindex < 1 || eventindex >= MAX_EVENTS )
	{
#if XASH_GAMECUBE
		if( Sys_CheckParm( "-gcfullphysics" ))
			Con_Reportf( "Xash3D GameCube: G121 PlaybackEvent invalid eventindex=%u flags=0x%x\n",
				(unsigned)eventindex, flags );
#endif
		Con_Printf( S_ERROR "%s: invalid eventindex %i\n", __func__, eventindex );
		return;
	}

	// check event for precached
	if( COM_StringEmptyOrNULL( sv.event_precache[eventindex] ))
	{
#if XASH_GAMECUBE
		if( Sys_CheckParm( "-gcfullphysics" ))
			Con_Reportf( "Xash3D GameCube: G121 PlaybackEvent not precached index=%u\n",
				(unsigned)eventindex );
#endif
		Con_Printf( S_ERROR "%s: event %i was not precached\n", __func__, eventindex );
		return;
	}

	memset( &args, 0, sizeof( args ));

	if( origin && !VectorIsNull( origin ))
	{
		VectorCopy( origin, args.origin );
		args.flags |= FEVENT_ORIGIN;
	}

	if( angles && !VectorIsNull( angles ))
	{
		VectorCopy( angles, args.angles );
		args.flags |= FEVENT_ANGLES;
	}

	// copy other parms
	args.fparam1 = fparam1;
	args.fparam2 = fparam2;
	args.iparam1 = iparam1;
	args.iparam2 = iparam2;
	args.bparam1 = bparam1;
	args.bparam2 = bparam2;

	VectorClear( pvspoint );

	if( SV_IsValidEdict( pInvoker ))
	{
		// add the view_ofs to avoid problems with crossed contents line
		VectorAdd( pInvoker->v.origin, pInvoker->v.view_ofs, pvspoint );
		args.entindex = invokerIndex = NUM_FOR_EDICT( pInvoker );

		// g-cont. allow 'ducking' param for all entities
		args.ducking = FBitSet( pInvoker->v.flags, FL_DUCKING ) ? true : false;

		// this will be send only for reliable event
		if( !FBitSet( args.flags, FEVENT_ORIGIN ))
			VectorCopy( pInvoker->v.origin, args.origin );

		// this will be send only for reliable event
		if( !FBitSet( args.flags, FEVENT_ANGLES ))
			VectorCopy( pInvoker->v.angles, args.angles );
	}
	else
	{
		VectorCopy( args.origin, pvspoint );
		args.entindex = 0;
		invokerIndex = -1;
	}

	if( !FBitSet( flags, FEV_GLOBAL ) && VectorIsNull( pvspoint ))
	{
		Con_DPrintf( S_ERROR "%s: not a FEV_GLOBAL event missing origin. Ignored.\n", sv.event_precache[eventindex] );
		return;
	}

	// check event for some user errors
	if( FBitSet( flags, FEV_NOTHOST|FEV_HOSTONLY ))
	{
		if( !SV_ClientFromEdict( pInvoker, true ))
		{
			const char *ev_name = sv.event_precache[eventindex];

			if( FBitSet( flags, FEV_NOTHOST ))
			{
				Con_DPrintf( S_WARN "%s: specified FEV_NOTHOST when invoker not a client\n", ev_name );
				ClearBits( flags, FEV_NOTHOST );
			}

			if( FBitSet( flags, FEV_HOSTONLY ))
			{
				Con_DPrintf( S_WARN "%s: specified FEV_HOSTONLY when invoker not a client\n", ev_name );
				ClearBits( flags, FEV_HOSTONLY );
			}
		}
	}

	SetBits( flags, FEV_SERVER );		// it's a server event!
	if( delay < 0.0f ) delay = 0.0f;	// fixup negative delays

	// setup pvs cluster for invoker
	if( !FBitSet( flags, FEV_GLOBAL ))
	{
		Mod_FatPVS( pvspoint, FATPHS_RADIUS, fatphs, world.fatbytes, false, ( svs.maxclients == 1 ), true );
		mask = fatphs; // using the FatPVS like a PHS
	}

	// process all the clients
	cl = svs.clients;
	for( int slot = 0; slot < svs.maxclients; slot++, cl++ )
	{
		if( cl->state != cs_spawned || !cl->edict || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		if( SV_IsValidEdict( pInvoker ) && !SV_CheckGroupTrace( pInvoker, cl->edict ))
			continue;

		if( SV_IsValidEdict( pInvoker ))
		{
#if XASH_GAMECUBE
			/* G121: lean FatPVS/PHS after changelevel can exclude the local
			 * shooter; always deliver the invoker's own weapon events. */
			if( cl->edict != pInvoker && !SV_CheckClientVisiblity( cl, mask ))
				continue;
#else
			if( !SV_CheckClientVisiblity( cl, mask ))
				continue;
#endif
		}

		// a1ba: GoldSrc never cleans up host_client pointer (similar to sv.current_client)
		// so it's always points at some client and in singleplayer this check always succeedes
		// in Xash, however, sv.current_client might be reset and set to NULL
		// this is especially dangerous when weapons play events in Think functions
		//
		// IMHO, it doesn't make sense to me to compare it against current client when we have
		// invoker edict pointer but to preserve behaviour check for them both
		//
		// if it breaks some mods, probably sv.current_client semantics must be reworked to match GoldSrc
#if XASH_GAMECUBE
		/* G121: deliver FEV_NOTHOST weapon events to the local client so
		 * EV_FireGlock can play stock pl_gun3 through the standard path
		 * (client weapon-event prediction is not proven under MEM1 yet). */
		if( FBitSet( flags, FEV_NOTHOST ) && ( cl == sv.current_client || cl->edict == pInvoker )
			&& FBitSet( cl->flags, FCL_LOCAL_WEAPONS ) && !Sys_CheckParm( "-gcfullphysics" ))
			continue;
#else
		if( FBitSet( flags, FEV_NOTHOST ) && ( cl == sv.current_client || cl->edict == pInvoker ) && FBitSet( cl->flags, FCL_LOCAL_WEAPONS ))
			continue;	// will be played on client side
#endif

		if( FBitSet( flags, FEV_HOSTONLY ) && cl->edict != pInvoker )
			continue;	// sending only to invoker

		// all checks passed, send the event
#if XASH_GAMECUBE
		if( Sys_CheckParm( "-gcfullphysics" ))
			CL_GCRelinkEventHooks();
		if( Sys_CheckParm( "-gcfullphysics" ) && eventindex > 0 )
		{
			static qboolean gc_g121_event_logged;
			const char *evname = sv.event_precache[eventindex];

			if( !gc_g121_event_logged )
			{
				gc_g121_event_logged = true;
				Con_Reportf( "Xash3D GameCube: G121 PlaybackEvent deliver index=%u name=%s flags=0x%x client=%d\n",
					(unsigned)eventindex, evname ? evname : "(null)", flags,
					cl ? (int)( cl - svs.clients ) : -1 );
			}
		}
#endif
		// reliable event
		if( FBitSet( flags, FEV_RELIABLE ))
		{
			// skipping queue, write direct into reliable datagram
			SV_PlaybackReliableEvent( &cl->netchan.message, eventindex, delay, &args );
			continue;
		}

		// unreliable event (stores in queue)
		es = &cl->events;
		bestslot = -1;

		if( FBitSet( flags, FEV_UPDATE ))
		{
			for( j = 0; j < MAX_EVENT_QUEUE; j++ )
			{
				ei = &es->ei[j];

				if( ei->index == eventindex && invokerIndex != -1 && invokerIndex == ei->entity_index )
				{
					bestslot = j;
					break;
				}
			}
		}

		if( bestslot == -1 )
		{
			for( j = 0; j < MAX_EVENT_QUEUE; j++ )
			{
				ei = &es->ei[j];

				if( ei->index == 0 )
				{
					// found an empty slot
					bestslot = j;
					break;
				}
			}
		}

		// no slot found for this player, oh well
		if( bestslot == -1 ) continue;

		// add event to queue
		ei->index = eventindex;
		ei->fire_time = delay;
		ei->entity_index = invokerIndex;
		ei->packet_index = -1;
		ei->flags = flags;
		ei->args = args;
	}
}

/*
=============
pfnGetCurrentPlayer

=============
*/
static int GAME_EXPORT pfnGetCurrentPlayer( void )
{
	int	idx = sv.current_client - svs.clients;

	if( idx < 0 || idx >= svs.maxclients )
		return -1;
	return idx;
}

/*
=============
pfnSetFatPVS

The client will interpolate the view position,
so we can't use a single PVS point
=============
*/
static byte *GAME_EXPORT pfnSetFatPVS( const float *org )
{
	static byte fatpvs[(MAX_MAP_LEAFS+7)/8];
	qboolean	fullvis = false, merge = false;

	if( !sv.worldmodel->visdata || sv_novis.value || !org || CL_DisableVisibility( ))
		fullvis = true;

	if( FBitSet( sv.hostflags, SVF_MERGE_VISIBILITY ))
		merge = true;

	Mod_FatPVS( org, FATPVS_RADIUS, fatpvs, world.fatbytes, merge, fullvis, false );

	return fatpvs;
}

/*
=============
pfnSetFatPHS

The client will interpolate the hear position,
so we can't use a single PHS point
=============
*/
static byte *GAME_EXPORT pfnSetFatPAS( const float *org )
{
	qboolean	fullvis = false, merge = false;

	if( !sv.worldmodel->visdata || sv_novis.value || !org || CL_DisableVisibility( ))
		fullvis = true;

	if( FBitSet( sv.hostflags, SVF_MERGE_VISIBILITY ))
		merge = true;

	Mod_FatPVS( org, FATPHS_RADIUS, fatphs, world.fatbytes, merge, fullvis, true );

	return fatphs;
}

/*
=============
Mod_HeadnodeVisible
=============
*/
static qboolean Mod_HeadnodeVisible( model_t *mod, mnode_t *node, const byte *visbits, int *lastleaf )
{
	if( !node || node->contents == CONTENTS_SOLID )
		return false;

	if( node->contents < 0 )
	{
		if( !CHECKVISBIT( visbits, ((mleaf_t *)node)->cluster ))
			return false;

		if( lastleaf )
			*lastleaf = ((mleaf_t *)node)->cluster;
		return true;
	}

	if( Mod_HeadnodeVisible( mod, node_child( node, 0, mod ), visbits, lastleaf ))
		return true;

	if( Mod_HeadnodeVisible( mod, node_child( node, 1, mod ), visbits, lastleaf ))
		return true;

	return false;
}

/*
=============
pfnCheckVisibility

=============
*/
static int GAME_EXPORT pfnCheckVisibility( const edict_t *ent, byte *pset )
{
	int	i;
	qboolean large_leafs = FBitSet( sv.worldmodel->flags, MODEL_QBSP2 );

	if( !SV_IsValidEdict( ent ))
		return 0;

	// vis not set - fullvis enabled
	if( !pset ) return 1;

	if( FBitSet( ent->v.flags, FL_CUSTOMENTITY ) && ent->v.owner && FBitSet( ent->v.owner->v.flags, FL_CLIENT ))
		ent = ent->v.owner;	// upcast beams to my owner

	if( ent->headnode < 0 )
	{
		// check individual leafs
		for( i = 0; i < ent->num_leafs; i++ )
		{
			if( large_leafs )
			{
				if( CHECKVISBIT( pset, ent->leafnums32[i] ))
					return 1;	// visible passed by leaf
			}
			else
			{
				if( CHECKVISBIT( pset, ent->leafnums16[i] ))
					return 1;	// visible passed by leaf
			}
		}

		return 0;
	}
	else
	{
		int leafnum;

		for( i = 0; i < MAX_ENT_LEAFS( large_leafs ); i++ )
		{
			if( large_leafs )
				leafnum = ent->leafnums32[i];
			else
				leafnum = ent->leafnums16[i];
			if( leafnum == -1 ) break;

			if( CHECKVISBIT( pset, leafnum ))
				return 1;	// visible passed by leaf
		}

		// too many leafs for individual check, go by headnode
		if( !Mod_HeadnodeVisible( sv.worldmodel, &sv.worldmodel->nodes[ent->headnode], pset, &leafnum ))
			return 0;

		if( large_leafs )
			((edict_t *)ent)->leafnums32[ent->num_leafs] = leafnum;
		else
			((edict_t *)ent)->leafnums16[ent->num_leafs] = leafnum;

		((edict_t *)ent)->num_leafs = (ent->num_leafs + 1) % MAX_ENT_LEAFS( large_leafs );

		return 2;	// visible passed by headnode
	}
}

/*
=============
pfnCanSkipPlayer

=============
*/
static int GAME_EXPORT pfnCanSkipPlayer( const edict_t *player )
{
	sv_client_t	*cl;

	if(( cl = SV_ClientFromEdict( player, false )) == NULL )
		return false;

	return FBitSet( cl->flags, FCL_LOCAL_WEAPONS ) ? true : false;
}

/*
=============
pfnSetGroupMask

=============
*/
static void GAME_EXPORT pfnSetGroupMask( int mask, int op )
{
	svs.groupmask = mask;
	svs.groupop = op;
}

/*
=============
pfnCreateInstancedBaseline

=============
*/
static int GAME_EXPORT pfnCreateInstancedBaseline( int classname, struct entity_state_s *baseline )
{
	if( !baseline || sv.num_instanced >= MAX_CUSTOM_BASELINES )
		return 0;

	// g-cont. must sure that classname is really allocated
	sv.instanced[sv.num_instanced].classname = SV_CopyString( SV_GetString( classname ));
	sv.instanced[sv.num_instanced].baseline = *baseline;
	sv.num_instanced++;

	return sv.num_instanced;
}

/*
=============
pfnEndSection

=============
*/
static void GAME_EXPORT pfnEndSection( const char *pszSection )
{
	if( !Q_stricmp( "oem_end_credits", pszSection ))
		Host_Credits ();
	else Cbuf_AddText( "\ndisconnect\n" );
}

/*
=============
pfnGetPlayerUserId

=============
*/
static int GAME_EXPORT pfnGetPlayerUserId( edict_t *e )
{
	sv_client_t	*cl;

	if(( cl = SV_ClientFromEdict( e, false )) == NULL )
		return -1;
	return cl->userid;
}

/*
=============
pfnGetPlayerStats

=============
*/
static void GAME_EXPORT pfnGetPlayerStats( const edict_t *pClient, int *ping, int *packet_loss )
{
	sv_client_t	*cl;

	if( packet_loss ) *packet_loss = 0;
	if( ping ) *ping = 0;

	if(( cl = SV_ClientFromEdict( pClient, false )) == NULL )
		return;

	if( packet_loss ) *packet_loss = cl->packet_loss;
	if( ping ) *ping = cl->latency * 1000;
}

static void GAME_EXPORT Cmd_AddServerCommand( const char *cmd_name, xcommand_t function )
{
	Cmd_AddCommandEx( cmd_name, function, "server command", CMD_SERVERDLL, __func__ );
}

/*
=============
pfnForceUnmodified

=============
*/
static void GAME_EXPORT pfnForceUnmodified( FORCE_TYPE type, float *mins, float *maxs, const char *filename )
{
	if( COM_StringEmptyOrNULL( filename ))
		return;

	if( sv.state == ss_loading )
	{
		for( int i = 0; i < MAX_MODELS; i++ )
		{
			consistency_t *pc = &sv.consistency_list[i];

			if( !pc->filename )
			{
				if( mins ) VectorCopy( mins, pc->mins );
				if( maxs ) VectorCopy( maxs, pc->maxs );
				pc->filename = SV_CopyString( filename );
				pc->check_type = type;
				return;
			}
			else if( !Q_strcmp( filename, pc->filename ))
				return;
		}
		Host_Error( "MAX_MODELS limit exceeded (%d)\n", MAX_MODELS );
	}
	else
	{
		for( int i = 0; i < MAX_MODELS; i++ )
		{
			consistency_t *pc = &sv.consistency_list[i];
			if( !pc->filename ) continue;

			if( !Q_strcmp( filename, pc->filename ))
				return;
		}
		Con_Printf( S_ERROR "Failed to enforce consistency for %s: was not precached\n", filename );
	}
}

/*
=============
pfnVoice_GetClientListening

=============
*/
static qboolean GAME_EXPORT pfnVoice_GetClientListening( int iReceiver, int iSender )
{
	iReceiver -= 1;
	iSender -= 1;

	if( iReceiver < 0 || iReceiver >= svs.maxclients || iSender < 0 || iSender >= svs.maxclients )
		return false;

	return (FBitSet( svs.clients[iSender].listeners, BIT( iReceiver )) != 0 );
}

/*
=============
pfnVoice_SetClientListening

=============
*/
static qboolean GAME_EXPORT pfnVoice_SetClientListening( int iReceiver, int iSender, qboolean bListen )
{
	iReceiver -= 1;
	iSender -= 1;

	if( iReceiver < 0 || iReceiver >= svs.maxclients || iSender < 0 || iSender >= svs.maxclients )
		return false;

	if( bListen ) SetBits( svs.clients[iSender].listeners, BIT( iReceiver ));
	else ClearBits( svs.clients[iSender].listeners, BIT( iReceiver ));

	return true;
}

/*
=============
pfnGetPlayerAuthId

These function must returns cd-key hashed value
but Xash3D currently doesn't have any security checks
return nullstring for now
=============
*/
static const char *GAME_EXPORT pfnGetPlayerAuthId( edict_t *e )
{
	return SV_GetClientIDString( SV_ClientFromEdict( e, false ));
}

/*
=============
pfnQueryClientCvarValue

request client cvar value
=============
*/
static void GAME_EXPORT pfnQueryClientCvarValue( const edict_t *player, const char *cvarName )
{
	sv_client_t *cl;

	if( COM_StringEmptyOrNULL( cvarName ))
		return;

	if(( cl = SV_ClientFromEdict( player, false )) != NULL )
	{
		MSG_BeginServerCmd( &cl->netchan.message, svc_querycvarvalue );
		MSG_WriteString( &cl->netchan.message, cvarName );
	}
	else
	{
		if( svgame.dllFuncs2.pfnCvarValue )
			svgame.dllFuncs2.pfnCvarValue( player, "Bad Player" );
		Con_Printf( S_ERROR "%s: tried to send to a non-client!\n", __func__ );
	}
}

/*
=============
pfnQueryClientCvarValue2

request client cvar value (bugfixed)
=============
*/
static void GAME_EXPORT pfnQueryClientCvarValue2( const edict_t *player, const char *cvarName, int requestID )
{
	sv_client_t *cl;

	if( COM_StringEmptyOrNULL( cvarName ))
		return;

	if(( cl = SV_ClientFromEdict( player, false )) != NULL )
	{
		MSG_BeginServerCmd( &cl->netchan.message, svc_querycvarvalue2 );
		MSG_WriteLong( &cl->netchan.message, requestID );
		MSG_WriteString( &cl->netchan.message, cvarName );
	}
	else
	{
		if( svgame.dllFuncs2.pfnCvarValue2 )
			svgame.dllFuncs2.pfnCvarValue2( player, requestID, cvarName, "Bad Player" );
		Con_Printf( S_ERROR "%s: tried to send to a non-client!\n", __func__ );
	}
}

/*
=============
pfnEngineStub

extended iface stubs
=============
*/
static int GAME_EXPORT pfnGetLocalizedStringLength( const char *label )
{
	return 0;
}


/*
=============
pfnRegisterTutorMessageShown

only exists in PlayStation version
=============
*/
static void GAME_EXPORT pfnRegisterTutorMessageShown( int mid )
{
}

/*
=============
pfnGetTimesTutorMessageShown

only exists in PlayStation version
=============
*/
static int GAME_EXPORT pfnGetTimesTutorMessageShown( int mid )
{
	return 0;
}

static void GAME_EXPORT pfnGetGameDir( char *out )
{
	char rootdir[MAX_SYSPATH];

	if( !out )
		return;

	if( !FBitSet( host.bugcomp, BUGCOMP_GET_GAME_DIR_FULL_PATH ))
	{
		Q_strncpy( out, GI->gamefolder, 256 );
	}
	else
	{
		// in GoldSrc pre-1.1.1.1, it's a full path to game directory, limited by 256 characters
		// however the full path might easily overflow that limitation
		// here we check if it would overflow and just return game folder in that case
		if( !g_fsapi.GetRootDirectory( rootdir, sizeof( rootdir ))
			|| Q_snprintf( out, 256, "%s/%s", rootdir, GI->gamefolder ) < 0 )
		{
			Q_strncpy( out, GI->gamefolder, 256 );
		}
	}
}

static cvar_t* GAME_EXPORT SV_CvarGetPointer( const char *szVarName )
{
	cvar_t *result = (cvar_t *)Cvar_FindVar( szVarName );

	if( !result )
		Con_DPrintf( S_WARN "%s: server tried to get non-existent cvar \"%s\"\n", __func__, szVarName );
	
	return result;
}

// engine callbacks
static enginefuncs_t gEngfuncs =
{
	pfnPrecacheModel,
	SV_SoundIndex,
	SV_SetModel,
	pfnModelIndex,
	pfnModelFrames,
	pfnSetSize,
	pfnChangeLevel,
	pfnGetSpawnParms,
	pfnSaveSpawnParms,
	pfnVecToYaw,
	VectorAngles,
	pfnMoveToOrigin,
	pfnChangeYaw,
	pfnChangePitch,
	SV_FindEntityByString,
	SV_LightForEntity,
	pfnFindEntityInSphere,
	pfnFindClientInPVS,
	pfnEntitiesInPVS,
	pfnMakeVectors,
	AngleVectors,
	SV_AllocEdict,
	pfnRemoveEntity,
	pfnCreateNamedEntity,
	pfnMakeStatic,
	pfnEntIsOnFloor,
	pfnDropToFloor,
	pfnWalkMove,
	pfnSetOrigin,
	SV_StartSound,
	pfnEmitAmbientSound,
	pfnTraceLine,
	pfnTraceToss,
	pfnTraceMonsterHull,
	pfnTraceHull,
	pfnTraceModel,
	pfnTraceTexture,
	pfnTraceSphere,
	pfnGetAimVector,
	pfnServerCommand,
	pfnServerExecute,
	pfnClientCommand,
	pfnParticleEffect,
	pfnLightStyle,
	pfnDecalIndex,
	SV_PointContents,
	pfnMessageBegin,
	pfnMessageEnd,
	pfnWriteByte,
	pfnWriteChar,
	pfnWriteShort,
	pfnWriteLong,
	pfnWriteAngle,
	pfnWriteCoord,
	pfnWriteString,
	pfnWriteEntity,
	pfnCvar_RegisterServerVariable,
	Cvar_VariableValue,
	Cvar_VariableString,
	Cvar_SetValue,
	Cvar_Set,
	pfnAlertMessage,
	pfnEngineFprintf,
	pfnPvAllocEntPrivateData,
	pfnPvEntPrivateData,
	SV_FreePrivateData,
	SV_GetString,
	SV_AllocString,
	pfnGetVarsOfEnt,
	pfnPEntityOfEntOffset,
	pfnEntOffsetOfPEntity,
	pfnIndexOfEdict,
	pfnPEntityOfEntIndexBroken,
	pfnFindEntityByVars,
	pfnGetModelPtr,
	pfnRegUserMsg,
	pfnAnimationAutomove,
	pfnGetBonePosition,
	pfnFunctionFromName,
	pfnNameForFunction,
	pfnClientPrintf,
	pfnServerPrint,
	Cmd_Args,
	Cmd_Argv,
	(void*)Cmd_Argc,
	pfnGetAttachment,
	CRC32_Init,
	CRC32_ProcessBuffer,
	CRC32_ProcessByte,
	CRC32_Final,
	COM_RandomLong,
	COM_RandomFloat,
	pfnSetView,
	Sys_FloatTime,
	pfnCrosshairAngle,
	COM_LoadFileForMe,
	COM_FreeFile,
	pfnEndSection,
	pfnCompareFileTime,
	pfnGetGameDir,
	pfnCvar_RegisterEngineVariable,
	pfnFadeClientVolume,
	pfnSetClientMaxspeed,
	SV_FakeConnect,
	pfnRunPlayerMove,
	pfnNumberOfEntities,
	pfnGetInfoKeyBuffer,
	Info_ValueForKey,
	pfnSetValueForKey,
	pfnSetClientKeyValue,
	pfnIsMapValid,
	pfnStaticDecal,
	SV_GenericIndex,
	pfnGetPlayerUserId,
	pfnBuildSoundMsg,
	pfnIsDedicatedServer,
	SV_CvarGetPointer,
	pfnGetPlayerWONId,
	(void*)Info_RemoveKey,
	pfnGetPhysicsKeyValue,
	pfnSetPhysicsKeyValue,
	pfnGetPhysicsInfoString,
	pfnPrecacheEvent,
	SV_PlaybackEventFull,
	pfnSetFatPVS,
	pfnSetFatPAS,
	pfnCheckVisibility,
	Delta_SetField,
	Delta_UnsetField,
	Delta_AddEncoder,
	pfnGetCurrentPlayer,
	pfnCanSkipPlayer,
	Delta_FindField,
	Delta_SetFieldByIndex,
	Delta_UnsetFieldByIndex,
	pfnSetGroupMask,
	pfnCreateInstancedBaseline,
	(void*)Cvar_DirectSet,
	pfnForceUnmodified,
	pfnGetPlayerStats,
	Cmd_AddServerCommand,
	pfnVoice_GetClientListening,
	pfnVoice_SetClientListening,
	pfnGetPlayerAuthId,
	pfnSequenceGet,
	pfnSequencePickSentence,
	COM_FileSize,
	Sound_GetApproxWavePlayLen,
	pfnIsCareerMatch,
	pfnGetLocalizedStringLength,
	pfnRegisterTutorMessageShown,
	pfnGetTimesTutorMessageShown,
	pfnProcessTutorMessageDecayBuffer,
	pfnConstructTutorMessageDecayBuffer,
	pfnResetTutorMessageDecayData,
	pfnQueryClientCvarValue,
	pfnQueryClientCvarValue2,
	COM_CheckParm,
	pfnPEntityOfEntIndexAllEntities,
};

static void SV_FreeKeyValueString( const char *value )
{
#if XASH_GAMECUBE
	if( SV_GCKeyValueIsSlab( value ))
		return;
#endif
	Mem_Free( (void *)value );
}

static void SV_FreeKeyValueStrings( KeyValueData *kvd, int numpairs )
{
	for( int i = 0; i < numpairs; i++ )
	{
		SV_FreeKeyValueString( kvd[i].szKeyName );
		SV_FreeKeyValueString( kvd[i].szValue );
	}
}

#if XASH_GAMECUBE
static qboolean SV_GCMapSmokeRoute( void )
{
	return GC_MapLoadMemoryOpt();
}

/* Per-entity spawn breadcrumbs. Fullphysics denser maps (c5a1 ~600 ents)
 * emit ~1300 OSReports and drop MAP_READY / entity lump spawn ready from the
 * EXI FIFO (probe 20261105-100000). Keep sparse progress only. */
static qboolean SV_GCMapEntityDiagVerbose( void )
{
	if( Sys_CheckParm( "-gcfullphysics" ))
		return false;
	return SV_GCMapSmokeRoute();
}

/* G433: Replace policy-based entity inhibition with measured budgeting
 * Check if entity should be inhibited based on memory budget.
 * Returns true if entity should be inhibited. */
static qboolean SV_GCMapCannotFitEntity( const char *classname, size_t entity_index )
{
	size_t entity_size;
	qboolean over_budget;
	
	if( COM_StringEmpty( classname ))
		return false;
	
	/* Check if we're already over budget */
	over_budget = GC_MemBudgetExceeded();
	
	if( over_budget )
	{
		/* Estimate entity memory usage */
		entity_size = GC_EntityEstimateSize();
		
		/* Check if we can afford to spawn this entity */
		if( !GC_MemBudgetEnforce( entity_size, "entity_spawn" ))
		{
			/* Do not silently omit gameplay entities: the caller aborts this map load. */
			if( entity_index >= 90 && ( entity_index & 63 ) == 0 )
				Con_Reportf( S_WARN "Xash3D GameCube: entity budget failure entity=%d classname=%s\n",
					entity_index, classname );
			return true;
		}
	}
	return false;
}
#endif

/*
====================
SV_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
====================
*/
static qboolean SV_ParseEdict( char **pfile, edict_t *ent, int entity_index )
{
	KeyValueData	pkvd[256]; // per one entity
	qboolean		adjust_origin = false, customentity;
	int		numpairs = 0;
	const char	*classname = NULL;

#if XASH_GAMECUBE
	/* KeyValue strings are temporary: the normal path frees them after each
	 * callback. The fallback slab has the same lifetime, one entity at a time. */
	gc_kv_slab_used = 0;
#endif

	// go through all the dictionary pairs
	while( 1 )
	{
		string	keyname;
		char	value[2048];
		int len;

		// parse key
		if(( *pfile = COM_ParseFile( *pfile, keyname, sizeof( keyname ))) == NULL )
			Host_Error( "%s: EOF without closing brace\n", __func__ );

		if( keyname[0] == '}' )
			break; // end of desc

		// parse value
		if(( *pfile = COM_ParseFile( *pfile, value, sizeof( value ))) == NULL )
			Host_Error( "%s: EOF without closing brace\n", __func__ );

		if( value[0] == '}' )
			Host_Error( "%s: closing brace without data\n", __func__ );

#if XASH_GAMECUBE
		if( SV_GCMapEntityDiagVerbose() && entity_index >= 700 && ( numpairs & 3 ) == 0 )
			Con_Reportf( "Xash3D GameCube: entity kv pair=%d entity=%d key=%s\n",
				numpairs, entity_index, keyname );
#endif

		// ignore attempts to set empty key or value
		// "wad" field is already handled
		if( !keyname[0] || !value[0] || !Q_strcmp( keyname, "wad" ))
			continue;

		// keynames with a leading underscore are used for
		// utility comments and are immediately discarded by engine
		if( FBitSet( world.flags, FWORLD_SKYSPHERE ) && keyname[0] == '_' )
			continue;

		// classname must be first
		if( !Q_strcmp( keyname, "classname" ))
		{
			KeyValueData kvd = {
				.szClassName = NULL,
				.szKeyName = keyname,
				.szValue = value,
				.fHandled = false
			};

			// don't allow double classnames
			if( classname != NULL )
				continue;

			svgame.dllFuncs.pfnKeyValue( ent, &kvd );

			// ideally, all game dlls should handle classname.
			// throw an error for now, improve the logic if it causes
			// compatibility issues with Xash-based games
			if( !kvd.fHandled )
				Host_Error( "%s: game didn't handled \"%s\" classname\n", __func__,	value );

			// this lets game dll override custom entity classname
			// to something bogus that's exported in game dll
			classname = SV_GetString( ent->v.classname );
			continue;
		}

		// GoldSrc removes trailing spaces
		// but does this after sucking out classname
		// which doesn't have similar check
		for( len = Q_strlen( keyname ); len > 0 && keyname[len - 1] == ' '; len-- )
			keyname[len - 1] = '\0';

		// create keyvalue strings
		pkvd[numpairs].szClassName = (char*)""; // unknown at this moment
		pkvd[numpairs].szKeyName = SV_GCCopyKeyValue( keyname );
		pkvd[numpairs].szValue = SV_GCCopyKeyValue( value );
		pkvd[numpairs].fHandled = false;
		if( !pkvd[numpairs].szKeyName || !pkvd[numpairs].szValue )
		{
			SV_FreeKeyValueStrings( pkvd, numpairs + 1 );
			Host_Error( "%s: unable to store keyvalues for entity %d\n", __func__, entity_index );
			return false;
		}
		numpairs++;

		if( numpairs >= ARRAYSIZE( pkvd ))
		{
			if( classname )
				Con_Printf( S_ERROR "%s: too many keyvalue pairs for %s!\n", __func__, classname );
			else Con_Printf( S_ERROR "%s: too many keyvalue pairs!\n", __func__ );
			break;
		}
	}

	if( classname == NULL )
	{
		// release allocated strings
		SV_FreeKeyValueStrings( pkvd, numpairs );
		Host_Error( "%s: entity %d has no classname\n", __func__, entity_index );
		return false;
	}


#if XASH_GAMECUBE
	/* Budget telemetry only — aborting mid-map omits the rest of HL1. Soft-skip
	 * happens via private-data miss below when tip MEM1 truly cannot serve. */
	if( SV_GCMapSmokeRoute() && SV_GCMapCannotFitEntity( classname, entity_index )
		&& ( entity_index & 31 ) == 0 )
		Con_Reportf( S_WARN "Xash3D GameCube: entity budget pressure entity=%d class=%s\n",
			entity_index, classname );
#endif

#if XASH_GAMECUBE
	if( SV_GCMapEntityDiagVerbose() && entity_index >= 700 )
		Con_Reportf( "Xash3D GameCube: entity private begin=%d class=%s pairs=%d\n",
			entity_index, classname, numpairs );
#endif
	ent = SV_AllocPrivateData( ent, ent->v.classname, &customentity );
#if XASH_GAMECUBE
	if( SV_GCMapEntityDiagVerbose() && entity_index >= 700 )
		Con_Reportf( "Xash3D GameCube: entity private end=%d class=%s ok=%d\n",
			entity_index, classname, SV_IsValidEdict( ent ) && !FBitSet( ent->v.flags, FL_KILLME ));
#endif

	if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME ))
	{
		/* Soft-miss under memopt already freed the edict and logged — keep
		 * parsing so one tip-heap brush miss does not unload the whole map. */
#if XASH_GAMECUBE
		if( GC_MapLoadMemoryOpt() )
		{
			SV_FreeKeyValueStrings( pkvd, numpairs );
			Con_Reportf( S_WARN "Xash3D GameCube: entity %d (%s) skipped after private-data miss\n",
				entity_index, classname );
			return true;
		}
#endif
		SV_FreeKeyValueStrings( pkvd, numpairs );
		Host_Error( "%s: entity %d (%s) rejected during private-data initialization (free=%d flags=%x)\n",
			__func__, entity_index, classname, ent ? ent->free : -1, ent ? ent->v.flags : 0 );
		return false;
	}

	if( customentity )
	{
		KeyValueData kvd = {
			.szClassName = (char *)"custom",
			.szKeyName = (char *)"customclass",
			.szValue = (char *)classname,
			.fHandled = false
		};

		svgame.dllFuncs.pfnKeyValue( ent, &kvd );
		// no fHandled check, GoldSrc behavior
	}

#ifdef HACKS_RELATED_HLMODS
	// chemical existence have broked changelevels
	if( !Q_stricmp( GI->gamefolder, "ce" ))
	{
	 	if( !Q_stricmp( sv.name, "ce08_02" ) && !Q_stricmp( classname, "info_player_start_force" ))
			adjust_origin = true;
	}
#endif

	for( int i = 0; i < numpairs; i++ )
	{
		char *keyname, *value;
		char temp[MAX_VA_STRING];

#if 0 // this is stupid bug in GoldSrc, disable
		if( !Q_strcmp( pkvd[i].szValue, classname ))
			continue;
#endif

		if( !Q_strcmp( pkvd[i].szKeyName, "angle" ))
		{
			float	flYawAngle = Q_atof( pkvd[i].szValue );

			SV_FreeKeyValueString( pkvd[i].szKeyName ); // will be replaced with 'angles'
			SV_FreeKeyValueString( pkvd[i].szValue );
			pkvd[i].szKeyName = SV_GCCopyKeyValue( "angles" );
			keyname = pkvd[i].szKeyName;

			if( flYawAngle >= 0.0f )
			{
				Q_snprintf( temp, sizeof( temp ), "%g %g %g", ent->v.angles[0], flYawAngle, ent->v.angles[2] );
				pkvd[i].szValue = SV_GCCopyKeyValue( temp );
			}
			else if( flYawAngle == -1.0f )
				pkvd[i].szValue = SV_GCCopyKeyValue( "-90 0 0" );
			else if( flYawAngle == -2.0f )
				pkvd[i].szValue = SV_GCCopyKeyValue( "90 0 0" );
			else pkvd[i].szValue = SV_GCCopyKeyValue( "0 0 0" ); // technically an error
			value = pkvd[i].szValue;
		}
		else if( adjust_origin && !Q_strcmp( pkvd[i].szKeyName, "origin" ))
		{
			char   *pstart = pkvd[i].szValue;
			vec3_t origin;

			COM_ParseVector( &pstart, origin, 3 );
			SV_FreeKeyValueString( pkvd[i].szValue );

			Q_snprintf( temp, sizeof( temp ), "%g %g %g", origin[0], origin[1], origin[2] - 16.0f );
			pkvd[i].szValue = SV_GCCopyKeyValue( temp );
			value = pkvd[i].szValue;
		}

		// do not leak memory if game overwritten these pointers
		keyname = pkvd[i].szKeyName;
		value = pkvd[i].szValue;
		if( !keyname || !value )
		{
			SV_FreeKeyValueString( keyname );
			SV_FreeKeyValueString( value );
			Host_Error( "%s: unable to transform keyvalues for entity %d\n", __func__, entity_index );
			return false;
		}

		pkvd[i].szClassName = (char *)classname;
		svgame.dllFuncs.pfnKeyValue( ent, &pkvd[i] );

		SV_FreeKeyValueString( keyname );
		SV_FreeKeyValueString( value );
	}
	return true;
}

/*
================
SV_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.
================
*/
static void SV_LoadFromFile( const char *mapname, char *entities )
{
	char	token[2048];
	qboolean	create_world = true;
	edict_t	*ent;

	Assert( entities != NULL );
#if XASH_GAMECUBE
	SV_GCCampaignScanEntities( mapname, entities );
	gc_soundent_fallback_used = false;
	gc_ent_priv_slab_used = 0;
	memset( gc_ent_priv_slab_bits, 0, sizeof( gc_ent_priv_slab_bits ));
	gc_ent_priv_slab_hits = 0;
	memset( gc_ent_priv_slab_size, 0, sizeof( gc_ent_priv_slab_size ));
	gc_kv_slab_used = 0;
#endif

	// user dll can override spawn entities function (Xash3D extension)
	if( !svgame.physFuncs.SV_LoadEntities || !svgame.physFuncs.SV_LoadEntities( mapname, entities ))
	{
		int inhibited = 0;
		int entity_index = 0;

		// parse ents
		while(( entities = COM_ParseFile( entities, token, sizeof( token ))) != NULL )
		{
			int spawn_result;

			if( token[0] != '{' )
				Host_Error( "%s: found %s when expecting {\n", __func__, token );

#if XASH_GAMECUBE
			/* Key/value copies are only live for this entity's parse+spawn.
			 * Without a per-entity rewind the 8 KiB slab fills mid-map and
			 * copystring stalls forever on fragmented tip (tip-flash-fix2
			 * hung after edict≈186 / mip trim; dod_donner finished earlier). */
			gc_kv_slab_used = 0;
#endif
			if( create_world )
			{
				create_world = false;
				ent = SV_EdictNum( 0 ); // already initialized
			}
			else ent = SV_AllocEdict();
#if XASH_GAMECUBE
			if( SV_GCMapEntityDiagVerbose() && entity_index >= 400 )
				Con_Reportf( "Xash3D GameCube: entity alloc end=%d edict=%d\n",
					entity_index, NUM_FOR_EDICT( ent ));
			if( SV_GCMapEntityDiagVerbose() && (( entity_index >= 200 && entity_index <= 210 ) || entity_index <= 20 ))
				Con_Reportf( "Xash3D GameCube: entity loop edict done=%d edict=%d\n", entity_index, NUM_FOR_EDICT( ent ));
			/* Sparse under fullphysics (every 64) so MAP_READY can clear EXI. */
			if( SV_GCMapSmokeRoute()
				&& ( entity_index & ( Sys_CheckParm( "-gcfullphysics" ) ? 63 : 31 )) == 0 )
				Con_Reportf( "Xash3D GameCube: entity spawn progress=%d\n", entity_index );
			if( SV_GCMapSmokeRoute() && entity_index >= 160 && entity_index <= 220 )
				Con_Reportf( "Xash3D GameCube: entity alloc/parse begin=%d edict=%d heap_free=%s\n",
					entity_index, NUM_FOR_EDICT( ent ), Q_memprint( GC_MemHeapFreeBytes() ));
#endif

			if( SV_GCMapEntityDiagVerbose() && entity_index >= 400 )
				Con_Reportf( "Xash3D GameCube: entity parse begin=%d\n", entity_index );
			if( !SV_ParseEdict( &entities, ent, entity_index++ ))
			{
				Host_Error( "%s: entity %d failed to parse or initialize; aborting map load instead of silently omitting it\n",
					__func__, entity_index - 1 );
				return;
			}
			if( SV_GCMapEntityDiagVerbose() && entity_index >= 400 )
				Con_Reportf( "Xash3D GameCube: entity parse end=%d class=%s\n",
					entity_index - 1, SV_ClassName( ent ));
			if( SV_GCMapEntityDiagVerbose() && (( entity_index - 1 ) & 31 ) == 0 )
				Con_Reportf( "Xash3D GameCube: entity parsed=%d classname=%s\n",
					entity_index - 1, SV_ClassName( ent ));

#if XASH_GAMECUBE
			if( SV_GCMapEntityDiagVerbose() && entity_index >= 400 )
				Con_Reportf( "Xash3D GameCube: entity loading pulse begin=%d class=%s\n",
					entity_index - 1, SV_ClassName( ent ));
			GC_LoadingOverlayPulse();
			if( SV_GCMapEntityDiagVerbose() && entity_index >= 400 )
				Con_Reportf( "Xash3D GameCube: entity loading pulse end=%d class=%s\n",
					entity_index - 1, SV_ClassName( ent ));
			if( SV_GCMapEntityDiagVerbose() )
			{
				const char *spawn_class = SV_ClassName( ent );
				if( entity_index == 257 )
					Con_Reportf( "Xash3D GameCube: entity budget check begin=%d class=%s\n",
						entity_index - 1, spawn_class );

				if( entity_index >= 64 && entity_index <= 90 )
					Con_Reportf( "Xash3D GameCube: entity spawn parsed=%d class=%s edict=%d\n",
						entity_index - 1, spawn_class, NUM_FOR_EDICT( ent ));

				if( SV_GCMapCannotFitEntity( spawn_class, entity_index )
					&& ( entity_index & 31 ) == 0 )
					Con_Reportf( S_WARN "Xash3D GameCube: spawn budget pressure entity=%d class=%s\n",
						entity_index - 1, spawn_class );
				if( entity_index == 257 )
					Con_Reportf( "Xash3D GameCube: entity budget check end=%d class=%s\n",
						entity_index - 1, spawn_class );
			}
			else if( SV_GCMapSmokeRoute() && Sys_CheckParm( "-gcfullphysics" ))
			{
				const char *spawn_class = SV_ClassName( ent );

				if( SV_GCMapCannotFitEntity( spawn_class, entity_index )
					&& ( entity_index & 63 ) == 0 )
					Con_Reportf( S_WARN "Xash3D GameCube: spawn budget pressure entity=%d class=%s\n",
						entity_index - 1, spawn_class );
			}
#endif

			if( deathmatch.value && FBitSet( host.bugcomp, BUGCOMP_SPAWNFLAG_NOT_DEATHMATCH ) && FBitSet( ent->v.spawnflags, SF_NOT_DEATHMATCH ))
			{
				SV_FreeEdict( ent );
				inhibited++;
				continue;
			}

#if XASH_GAMECUBE
			if( SV_GCMapEntityDiagVerbose() && entity_index >= 64 && entity_index <= 90 )
				Con_Reportf( "Xash3D GameCube: entity spawn call begin=%d class=%s\n",
					entity_index - 1, SV_ClassName( ent ));
			if( SV_GCMapEntityDiagVerbose() && entity_index == 257 )
				Con_Reportf( "Xash3D GameCube: entity spawn call begin=%d class=%s\n",
					entity_index - 1, SV_ClassName( ent ));
			/* tip-flash hang window after func_tank / mip-trim (~160–190). */
			if( SV_GCMapSmokeRoute() && entity_index >= 160 && entity_index <= 220 )
				Con_Reportf( "Xash3D GameCube: entity spawn call begin=%d class=%s heap_free=%s\n",
					entity_index - 1, SV_ClassName( ent ), Q_memprint( GC_MemHeapFreeBytes() ));
#endif
			spawn_result = svgame.dllFuncs.pfnSpawn( ent );
#if XASH_GAMECUBE
			if( SV_GCMapEntityDiagVerbose() && entity_index >= 64 && entity_index <= 90 )
				Con_Reportf( "Xash3D GameCube: entity spawn call end=%d class=%s result=%d\n",
					entity_index - 1, SV_ClassName( ent ), spawn_result );
			if( SV_GCMapEntityDiagVerbose() && entity_index == 257 )
				Con_Reportf( "Xash3D GameCube: entity spawn call end=%d class=%s result=%d\n",
					entity_index - 1, SV_ClassName( ent ), spawn_result );
			if( SV_GCMapSmokeRoute() && entity_index >= 160 && entity_index <= 220 )
				Con_Reportf( "Xash3D GameCube: entity spawn call end=%d class=%s result=%d\n",
					entity_index - 1, SV_ClassName( ent ), spawn_result );
#endif
			if( spawn_result == -1 )
			{
				// game rejected the spawn
				if( !FBitSet( ent->v.flags, FL_KILLME ))
				{
					SV_FreeEdict( ent );
					inhibited++;
				}
			}
#if XASH_GAMECUBE
			else if( SV_GCMapEntityDiagVerbose() && entity_index >= 90 && ( entity_index & 15 ) == 0 )
			{
				Con_Reportf( "Xash3D GameCube: entity spawn done=%d classname=%s\n",
					entity_index - 1, SV_ClassName( ent ));
			}
			else if( SV_GCMapEntityDiagVerbose() && entity_index >= 400 )
			{
				Con_Reportf( "Xash3D GameCube: entity spawn done=%d classname=%s\n",
					entity_index - 1, SV_ClassName( ent ));
			}
#endif
		}

		Con_DPrintf( "\n%i entities inhibited\n", inhibited );
#if XASH_GAMECUBE
		/* Menu New Game: which studio-model actors this map spawned. */
		if( GC_MenuRunsServer())
		{
			int e, n = 0;

			for( e = 1; e < svgame.numEntities && n < 32; e++ )
			{
				edict_t *ed = SV_EdictNum( e );
				const char *cn, *mdl;

				if( ed->free )
					continue;
				cn = SV_ClassName( ed );
				mdl = SV_GetString( ed->v.model );
				if( Q_strnicmp( cn, "monster_", 8 ) && Q_strnicmp( cn, "cycler", 6 )
					&& Q_stricmp( cn, "env_model" ) && !Q_stristr( mdl, ".mdl" ))
					continue;
				n++;
				Con_Reportf( "Xash3D GameCube: npc sv map=%s edict=%d class=%s name=%s model=%s org=%.0f %.0f %.0f seq=%d fr=%.2f move=%d solid=%d think=%.2f priv=%d\n",
					sv.name, e, cn, SV_GetString( ed->v.targetname ), mdl,
					ed->v.origin[0], ed->v.origin[1], ed->v.origin[2], ed->v.sequence,
					ed->v.framerate, ed->v.movetype, ed->v.solid, ed->v.nextthink,
					ed->pvPrivateData ? 1 : 0 );
			}
		}
		Con_Reportf( "Xash3D GameCube: entity spawn complete; game-rejected=%d\n", inhibited );
		SYS_Report( "Xash3D GameCube: entity spawn complete; game-rejected=%d\n", inhibited );
#endif
	}

	// reset world origin and angles for some reason
	VectorClear( svgame.edicts->v.origin );
	VectorClear( svgame.edicts->v.angles );
}

/*
==============
SpawnEntities

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.
==============
*/
void SV_SpawnEntities( const char *mapname )
{
	edict_t	*ent;

	// reset misc parms
	Cvar_Reset( "sv_zmax" );
	Cvar_Reset( "sv_wateramp" );
	Cvar_Reset( "sv_wateralpha" );

	// reset sky parms
	Cvar_Reset( "sv_skycolor_r" );
	Cvar_Reset( "sv_skycolor_g" );
	Cvar_Reset( "sv_skycolor_b" );
	Cvar_Reset( "sv_skyvec_x" );
	Cvar_Reset( "sv_skyvec_y" );
	Cvar_Reset( "sv_skyvec_z" );
	Cvar_Reset( "sv_skyname" );

	ent = SV_EdictNum( 0 );
	if( ent->free ) SV_InitEdict( ent );
	ent->v.model = SV_MakeString( sv.model_precache[1] );
	ent->v.modelindex = WORLD_INDEX; // world model
	ent->v.solid = SOLID_BSP;
	ent->v.movetype = MOVETYPE_PUSH;
	svgame.movevars.fog_settings = 0;

	svgame.globals->maxEntities = GI->max_edicts;
	svgame.globals->mapname = SV_MakeString( sv.name );
	svgame.globals->startspot = SV_MakeString( sv.startspot );
	svgame.globals->time = sv.time;

#if XASH_GAMECUBE
	/* G329/G369: reclaim ImageLib + Capture-time FatPVS extras before
	 * CSoundEnt so tram/denser BSP can finish entity spawn. */
	if( Sys_CheckParm( "-gcmap" ) || Sys_CheckParm( "-gcnewgame" )
		|| Sys_CheckParm( "-gcmenuplaystart" ) || GC_MapLoadMemoryOpt() )
	{
		Image_GCPurgeDecodeScratch();
		if( host.imagepool )
			Mem_EmptyPool( host.imagepool );
		GC_ReclaimFatPVSBeforeEntitySpawn();
		gc_soundent_fallback_used = false;
		Con_Reportf( "Xash3D GameCube: pre-entity memory trim\n" );
		GC_MemSample( "pre-entity spawn" );
		/* Promote allowlisted NPC/view meshes while MEM1 is still coherent.
		 * Spawn reads studiohdr for eye positions; stubs hung denser maps. */
		Mod_GCLoadPreEntityStudios();
	}
#endif

	// spawn the rest of the entities on the map
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: entity lump spawn begin\n" );
#endif
	SV_LoadFromFile( mapname, sv.worldmodel->entities );
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: entity lump spawn ready\n" );
	SYS_Report( "Xash3D GameCube: entity lump spawn ready\n" );
	GCube_AppendSwissEvidence( "entity lump spawn ready" );
	GCube_ArmSwissQuit();
	if( Sys_CheckParm( "-gcnewgame" ))
	{
		SV_GCPlaceNewGameTrackTrains();
		GC_BakeDeferredNewGameCapFaces();
	}
	/* Menu: place *12 then bake CapFaces + tram cabin while BSP edges live.
	 * When the real server runs, func_tracktrain places itself on its path. */
	if( Sys_CheckParm( "-gcmenuplaystart" ))
	{
		if( !GC_MenuRunsServer() )
			SV_GCPlaceNewGameTrackTrains();
		GC_BakeMenuNewGameCapFacesNoPVS();
	}
	if( Sys_CheckParm( "-gcmap" ))
		GC_MemSample( "entity spawn" );
#endif
}

void SV_UnloadProgs( void )
{
	pending_cvar_t *pending_cvars_list;

	if( !svgame.hInstance )
		return;

	SV_DeactivateServer ();
	Delta_Shutdown ();
	/// TODO: reenable this when
	/// SV_UnloadProgs will be disabled
	//Mod_ClearUserData ();

	pending_cvars_list = Cvar_PrepareToUnlink( FCVAR_EXTDLL );

	if( svgame.dllFuncs2.pfnGameShutdown != NULL )
		svgame.dllFuncs2.pfnGameShutdown ();

	// now we can unload cvars
	Cvar_FullSet( "host_gameloaded", "0", FCVAR_READ_ONLY );

	// free entity baselines (GameCube SP may own BSS — never Z_Free those)
#if XASH_GAMECUBE
	if( gc_using_static_edicts )
	{
		gc_using_static_edicts = false;
		svs.static_entities = NULL;
		svs.baselines = NULL;
	}
	else
#endif
	{
		Z_Free( svs.static_entities );
		Z_Free( svs.baselines );
		svs.baselines = NULL;
	}

	// remove server cmds
	SV_KillOperatorCommands();

	// must unlink all game cvars,
	// before pointers on them will be lost...
	Cvar_UnlinkPendingCvars( pending_cvars_list );
	Cmd_Unlink( CMD_SERVERDLL );

	SV_FreeStringPool();

	Mod_ResetStudioAPI ();

	COM_FreeLibrary( svgame.hInstance );
	Mem_FreePool( &svgame.mempool );
	memset( &svgame, 0, sizeof( svgame ));
}

qboolean SV_LoadProgs( const char *name )
{
	int			version;
	static APIFUNCTION		GetEntityAPI;
	static APIFUNCTION2		GetEntityAPI2;
	static GIVEFNPTRSTODLL	GiveFnptrsToDll;
	static NEW_DLL_FUNCTIONS_FN	GiveNewDllFuncs;
	static enginefuncs_t	gpEngfuncs;
	static globalvars_t		gpGlobals;
	static playermove_t		gpMove;
	qboolean init_entity_api = false;

	if( svgame.hInstance )
		return true;

	// fill it in
	svgame.pmove = &gpMove;
	svgame.globals = &gpGlobals;
	svgame.mempool = Mem_AllocPool( "Server Edicts Zone" );

	svgame.hInstance = COM_LoadLibrary( name, true, false );
	if( !svgame.hInstance )
	{
		Mem_FreePool(&svgame.mempool);
		return false;
	}

	// make sure what new dll functions is cleared
	memset( &svgame.dllFuncs2, 0, sizeof( svgame.dllFuncs2 ));

	// make sure what physic functions is cleared
	memset( &svgame.physFuncs, 0, sizeof( svgame.physFuncs ));

	// revert fix for pfnPEntityOfEntIndex to be compatible with GoldSrc
	// games that rely on this bug
	if( FBitSet( host.bugcomp, BUGCOMP_PENTITYOFENTINDEX_FLAG ))
		gEngfuncs.pfnPEntityOfEntIndex = pfnPEntityOfEntIndexBroken;

	// make local copy of engfuncs to prevent overwrite it with bots.dll
	gpEngfuncs = gEngfuncs;

	GetEntityAPI = (APIFUNCTION)COM_GetProcAddress( svgame.hInstance, "GetEntityAPI" );
	GetEntityAPI2 = (APIFUNCTION2)COM_GetProcAddress( svgame.hInstance, "GetEntityAPI2" );
	GiveNewDllFuncs = (NEW_DLL_FUNCTIONS_FN)COM_GetProcAddress( svgame.hInstance, "GetNewDLLFunctions" );

	if( !GetEntityAPI && !GetEntityAPI2 )
	{
		COM_PushLibraryError( "missing GetEntityAPI and GetEntityAPI2 exports" );
		COM_FreeLibrary( svgame.hInstance );
		Con_Printf( S_ERROR "%s: failed to get address of GetEntityAPI proc\n", __func__ );
		svgame.hInstance = NULL;
		Mem_FreePool( &svgame.mempool );
		return false;
	}

	GiveFnptrsToDll = (GIVEFNPTRSTODLL)COM_GetProcAddress( svgame.hInstance, "GiveFnptrsToDll" );

	if( !GiveFnptrsToDll )
	{
		COM_PushLibraryError( "missing GiveFnptrsToDll export" );
		COM_FreeLibrary( svgame.hInstance );
		Con_Printf( S_ERROR "%s: failed to get address of GiveFnptrsToDll proc\n", __func__ );
		svgame.hInstance = NULL;
		Mem_FreePool( &svgame.mempool );
		return false;
	}

	GiveFnptrsToDll( &gpEngfuncs, svgame.globals );

	// get extended callbacks
	if( GiveNewDllFuncs )
	{
		version = NEW_DLL_FUNCTIONS_VERSION;

		if( !GiveNewDllFuncs( &svgame.dllFuncs2, &version ))
		{
			if( version != NEW_DLL_FUNCTIONS_VERSION )
				Con_Printf( S_WARN "%s: new interface version %i should be %i\n", __func__, NEW_DLL_FUNCTIONS_VERSION, version );
			memset( &svgame.dllFuncs2, 0, sizeof( svgame.dllFuncs2 ));
		}
	}

	version = INTERFACE_VERSION;

	if( GetEntityAPI2 && GetEntityAPI2( &svgame.dllFuncs, &version ))
	{
		if( INTERFACE_VERSION == version )
		{
			init_entity_api = true;
			Con_Reportf( "%s: ^2initailized extended EntityAPI ^7ver. %i\n", __func__, version );
		}
		else Con_Printf( S_WARN "%s: interface version %i should be %i\n", __func__, INTERFACE_VERSION, version );
	}

	if( !init_entity_api && GetEntityAPI && GetEntityAPI( &svgame.dllFuncs, version ))
	{
		init_entity_api = true;
		Con_Reportf( "%s: ^2initailized legacy EntityAPI ^7ver. %i\n", __func__, version );
	}

	if( !init_entity_api )
	{
		COM_PushLibraryError( "can't init entity API" );
		COM_FreeLibrary( svgame.hInstance );
		Con_Printf( S_ERROR "%s: couldn't get entity API\n", __func__ );
		svgame.hInstance = NULL;
		Mem_FreePool( &svgame.mempool );
		return false;
	}

#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs operators begin\n" );
#endif
	SV_InitOperatorCommands();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs operators ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs studio api begin\n" );
#endif
	Mod_InitStudioAPI();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs studio api ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs physics api begin\n" );
#endif

	if( !SV_InitPhysicsAPI( ))
	{
		Con_Printf( S_WARN "%s: couldn't get physics API\n", __func__ );
	}
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs physics api ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs save restore begin\n" );
#endif

	// grab function SV_SaveGameComment
	SV_InitSaveRestore ();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs save restore ready\n" );
#endif

	svgame.globals->pStringBase = ""; // setup string base

	svgame.globals->maxEntities = GI->max_edicts;
	svgame.globals->maxClients = svs.maxclients;
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs edicts alloc begin\n" );
	gc_using_static_edicts = false;
	/* SP BSS when max_edicts fits the cap (valve gameinfo is typically
	 * 900–1200). Avoid tip Mem_Calloc of ~707 KiB (HW 20261106 map=NONE). */
	/* Solo listen (−gclistenserver, maxclients≤4) reuses the SP BSS pool so
	 * tip Mem_Calloc of edicts/baselines cannot OOM under menu-skip boots. */
	if( GI->max_edicts > 0 && GI->max_edicts <= GC_SP_EDICT_CAP
		&& ( svs.maxclients <= 1
			|| ( svs.maxclients <= 4 && Sys_CheckParm( "-gclistenserver" )) ) )
	{
		memset( gc_singleplayer_edicts, 0, sizeof( edict_t ) * (size_t)GI->max_edicts );
		memset( gc_singleplayer_baselines, 0, sizeof( entity_state_t ) * (size_t)GI->max_edicts );
		memset( gc_singleplayer_static_entities, 0, sizeof( gc_singleplayer_static_entities ));
		svgame.edicts = gc_singleplayer_edicts;
		svs.baselines = gc_singleplayer_baselines;
		svs.static_entities = gc_singleplayer_static_entities;
		gc_using_static_edicts = true;
		Con_Reportf( "Xash3D GameCube: SP BSS edicts max=%d clients=%d bytes=%s baselines=%s statics=%s\n",
			GI->max_edicts, svs.maxclients,
			Q_memprint( sizeof( edict_t ) * (size_t)GI->max_edicts ),
			Q_memprint( sizeof( entity_state_t ) * (size_t)GI->max_edicts ),
			Q_memprint( sizeof( gc_singleplayer_static_entities )));
	}
	else
#endif
	{
	svgame.edicts = Mem_Calloc( svgame.mempool, sizeof( edict_t ) * GI->max_edicts );
	svs.static_entities = Z_Calloc( sizeof( entity_state_t ) * MAX_STATIC_ENTITIES );
	svs.baselines = Z_Calloc( sizeof( entity_state_t ) * GI->max_edicts );
	}
	/* Soft-fail tip OOM (size>=48 KiB) can return NULL here; writing
	 * edicts[i].free then DSI's (DAR=0, Guru Meditation). */
	if( !svgame.edicts || !svs.static_entities || !svs.baselines )
	{
		Con_Printf( S_ERROR "%s: out of memory allocating edicts/baselines\n", __func__ );
		if( svgame.edicts )
		{
#if XASH_GAMECUBE
			if( !SV_IsStaticEdicts( svgame.edicts ))
#endif
			Mem_Free( svgame.edicts );
			svgame.edicts = NULL;
		}
		if( svs.static_entities )
		{
#if XASH_GAMECUBE
			if( !SV_IsStaticStaticEntities( svs.static_entities ))
#endif
			Z_Free( svs.static_entities );
			svs.static_entities = NULL;
		}
		if( svs.baselines )
		{
#if XASH_GAMECUBE
			if( !SV_IsStaticBaselines( svs.baselines ))
#endif
			Z_Free( svs.baselines );
			svs.baselines = NULL;
		}
#if XASH_GAMECUBE
		gc_using_static_edicts = false;
#endif
		COM_FreeLibrary( svgame.hInstance );
		svgame.hInstance = NULL;
		Mem_FreePool( &svgame.mempool );
		return false;
	}
	svgame.numEntities = svs.maxclients + 1; // clients + world

	for( int i = 0; i < GI->max_edicts; i++ )
		svgame.edicts[i].free = true; // mark all edicts as freed

	Cvar_FullSet( "host_gameloaded", "1", FCVAR_READ_ONLY );
	SV_AllocStringPool();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs edicts alloc ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs game description begin\n" );
#endif

	// fire once
	Con_Printf( "Dll loaded for game ^2\"%s\"\n", svgame.dllFuncs.pfnGetGameDescription( ));
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs game description ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs game init begin\n" );
#endif

	// all done, initialize game
	svgame.dllFuncs.pfnGameInit();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs game init ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs client move begin\n" );
#endif

	// initialize pm_shared
	SV_InitClientMove();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs client move ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs delta begin\n" );
#endif

	Delta_Init ();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs delta ready\n" );
	Con_Reportf( "Xash3D GameCube: server progs encoders begin\n" );
#endif

	// register custom encoders
	svgame.dllFuncs.pfnRegisterEncoders();
#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: server progs encoders ready\n" );
#endif

	return true;
}
