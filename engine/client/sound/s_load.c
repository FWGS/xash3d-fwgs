/*
s_load.c - sounds managment
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
#include "client.h"
#include "sound.h"
#if XASH_GAMECUBE
#include "gamecube/mem_gamecube.h"
#include "filesystem.h"
#endif

// during registration it is possible to have more sounds
// than could actually be referenced during gameplay,
// because we don't want to free anything until we are
// sure we won't need it.
#define MAX_SFX      8192
#define MAX_SFX_HASH (MAX_SFX/4)

static int      s_numSfx = 0;
static sfx_t    s_knownSfx[MAX_SFX];
static sfx_t    *s_sfxHashList[MAX_SFX_HASH];
static qboolean s_registering = false;
#if XASH_GAMECUBE
/* G118: under MapLoadMemoryOpt, allow small gameplay WAVs until this budget fills.
 * G121: per-file hard cap raised to 16 KiB (pl_gun3); Sound_LoadWAV matches. */
#define GC_SFX_BUDGET_BYTES		(48u * 1024u)
/* G121: stock glock fire is pl_gun3.wav (~13 KiB PCM @ 22 kHz); keep under
 * the session budget while allowing one fire WAV through EV_PlaySound. */
#define GC_SFX_MAX_FILE_BYTES	(16u * 1024u)
/* G124: gentle reclaim for small footsteps; hard retry drops one fire WAV. */
#define GC_SFX_LOAD_HEADROOM_SOFT	(4u * 1024u)
#define GC_SFX_LOAD_HEADROOM_HARD	(8u * 1024u)
#define GC_SFX_LRU_SLOTS		32
#define GC_SOUND_RUNTIME_BUDGETED	BIT( 2 )
static int      s_gcmapSoundSkips = 0;
static size_t   s_gc_sfx_budget_used;
static struct
{
	sfx_t *sfx;
	uint   stamp;
} s_gc_lru[GC_SFX_LRU_SLOTS];
static uint     s_gc_sfx_lru_clock;

static qboolean S_GCAllowRuntimeSoundLoad( void )
{
	if( !GC_MapLoadMemoryOpt() )
		return false;

	/* Keep map/bootstrap lean; allow a capped trickle once in-world. */
	return cls.state == ca_active && !s_registering
		&& s_gc_sfx_budget_used < GC_SFX_BUDGET_BYTES;
}

static qboolean S_GCSfxIsPlaying( const sfx_t *sfx )
{
	int ch;

	for( ch = 0; ch < snd.max_channels; ch++ )
	{
		if( snd.channels[ch].sfx == sfx )
			return true;
	}
	return false;
}

static void S_GCTouchBudgetedLru( sfx_t *sfx )
{
	int i;
	int free_slot = -1;

	if( !sfx )
		return;

	for( i = 0; i < GC_SFX_LRU_SLOTS; i++ )
	{
		if( s_gc_lru[i].sfx == sfx )
		{
			s_gc_lru[i].stamp = ++s_gc_sfx_lru_clock;
			return;
		}
		if( free_slot < 0 && !s_gc_lru[i].sfx )
			free_slot = i;
	}

	if( free_slot < 0 )
		free_slot = 0; /* rare: overwrite oldest slot metadata only */
	s_gc_lru[free_slot].sfx = sfx;
	s_gc_lru[free_slot].stamp = ++s_gc_sfx_lru_clock;
}

static void S_GCClearLruSlot( const sfx_t *sfx )
{
	int i;

	for( i = 0; i < GC_SFX_LRU_SLOTS; i++ )
	{
		if( s_gc_lru[i].sfx == sfx )
		{
			s_gc_lru[i].sfx = NULL;
			s_gc_lru[i].stamp = 0;
			return;
		}
	}
}

/*
===========
S_GCEvictBudgetedSfx

Drop one budgeted cache entry and clear channels that reference it.
===========
*/
static void S_GCEvictBudgetedSfx( sfx_t *sfx )
{
	int ch;

	if( !sfx || !sfx->cache || !FBitSet( sfx->cache->flags, GC_SOUND_RUNTIME_BUDGETED ))
		return;

	Con_Reportf( "Xash3D GameCube: G124 LRU evict sfx %s bytes=%u\n",
		sfx->name, (uint)sfx->cache->size );
	for( ch = 0; ch < snd.max_channels; ch++ )
	{
		if( snd.channels[ch].sfx == sfx )
			snd.channels[ch].sfx = NULL;
	}
	S_GCClearLruSlot( sfx );
	S_FreeSound( sfx );
}

/*
===========
S_GCMakeRoomForBudgetedLoad

G124: reclaim MEM1 from least-recent non-playing budgeted SFX.
Soft path only drops small one-shots (≤ max_victim); hard retry may drop a
retained in-place fire WAV after a failed load.
===========
*/
static size_t S_GCMakeRoomForBudgetedLoad( const sfx_t *keep, size_t want, size_t max_victim )
{
	size_t freed = 0;

	if( want == 0 )
		want = GC_SFX_LOAD_HEADROOM_SOFT;

	while( freed < want )
	{
		sfx_t *victim = NULL;
		uint victim_lru = ~0u;
		uint i;

		for( i = 0; i < s_numSfx; i++ )
		{
			sfx_t *sfx = &s_knownSfx[i];
			uint stamp = 0;
			int slot;

			if( sfx == keep || !sfx->name[0] || !sfx->cache )
				continue;
			if( !FBitSet( sfx->cache->flags, GC_SOUND_RUNTIME_BUDGETED ))
				continue;
			if( S_GCSfxIsPlaying( sfx ))
				continue;
			/* G125: pin preloaded fire + footsteps — eviction then reload
			 * fails FS Find under MEM1 freelist pressure. */
			if( !Q_strnicmp( sfx->name, "player/pl_step", 14 )
				|| !Q_stricmp( sfx->name, "weapons/pl_gun3.wav" ))
				continue;
			if( max_victim && sfx->cache->size > max_victim )
				continue;

			for( slot = 0; slot < GC_SFX_LRU_SLOTS; slot++ )
			{
				if( s_gc_lru[slot].sfx == sfx )
				{
					stamp = s_gc_lru[slot].stamp;
					break;
				}
			}
			if( stamp > victim_lru )
				continue;
			victim_lru = stamp;
			victim = sfx;
		}

		if( !victim )
			break;

		freed += victim->cache->size;
		S_GCEvictBudgetedSfx( victim );
	}

	return freed;
}
#endif

#define SENTENCE_INDEX -99999 // unique sentence index
static string   s_sentenceImmediateName;	// keep dummy sentence name

#if XASH_GAMECUBE
/*
===========
S_AllowNextGameplaySoundLoad / S_DisallowGameplaySoundLoad

G91 legacy hooks. G118 uses a cumulative byte budget for gameplay loads, so
these are no-ops kept for call-site compatibility.
===========
*/
void S_AllowNextGameplaySoundLoad( void )
{
}

void S_DisallowGameplaySoundLoad( void )
{
}

size_t S_GCGameplaySfxBudgetUsed( void )
{
	return s_gc_sfx_budget_used;
}
#endif

/*
=================
S_SoundList_f
=================
*/
void S_SoundList_f( void )
{
	sfx_t	*sfx;
	int	i, totalSfx = 0;
	int	totalSize = 0;

	for( i = 0, sfx = s_knownSfx; i < s_numSfx; i++, sfx++ )
	{
		if( !sfx->name[0] )
			continue;

		wavdata_t *sc = sfx->cache;
		if( sc )
		{
			totalSize += sc->size;

			if( FBitSet( sc->flags, SOUND_LOOPED ))
				Con_Printf( "L" );
			else
				Con_Printf( " " );

			if( sfx->name[0] == '*' || !Q_strncmp( sfx->name, DEFAULT_SOUNDPATH, sizeof( DEFAULT_SOUNDPATH ) - 1 ))
				Con_Printf( " (%2db) %s : %s\n", sc->width * 8, Q_memprint( sc->size ), sfx->name );
			else Con_Printf( " (%2db) %s : " DEFAULT_SOUNDPATH "%s\n", sc->width * 8, Q_memprint( sc->size ), sfx->name );
			totalSfx++;
		}
	}

	Con_Printf( "-------------------------------------------\n" );
	Con_Printf( "%i total sounds\n", totalSfx );
	Con_Printf( "%s total memory\n", Q_memprint( totalSize ));
	Con_Printf( "\n" );
}

/*
=================
S_CreateDefaultSound
=================
*/
static wavdata_t *S_CreateDefaultSound( void )
{
	uint samples = SOUND_DMA_SPEED;
	uint channels = 1;
	uint width = 2;
#if XASH_GAMECUBE
	qboolean tinyFallback = GC_MapLoadMemoryOpt();
#endif
	size_t size = samples * width * channels;

#if XASH_GAMECUBE
	if( tinyFallback )
	{
		samples = 512;
		size = samples * width * channels;
	}
#endif

	wavdata_t *sc = Mem_Calloc( sndpool, sizeof( wavdata_t ) + size );

	sc->width = width;
	sc->channels = channels;
	sc->rate = SOUND_DMA_SPEED;
	sc->samples = samples;
	sc->size = size;

#if XASH_GAMECUBE
	if( tinyFallback )
	{
		sc->rate = SOUND_11k;
		Con_Reportf( "Xash3D GameCube: tiny default sound fallback samples=%u bytes=%u\n",
			samples, (uint)size );
	}
#endif

	return sc;
}

/*
=================
S_LoadSound
=================
*/
wavdata_t *S_LoadSound( sfx_t *sfx )
{
	wavdata_t	*sc = NULL;

	if( !sfx ) return NULL;

	// see if still in memory
	if( sfx->cache )
	{
#if XASH_GAMECUBE
		if( FBitSet( sfx->cache->flags, GC_SOUND_RUNTIME_BUDGETED ))
			S_GCTouchBudgetedLru( sfx );
#endif
		return sfx->cache;
	}

	if( COM_StringEmptyOrNULL( sfx->name ))
		return NULL;

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && Q_stricmp( sfx->name, "*default" )
		&& !S_GCAllowRuntimeSoundLoad() )
	{
		if( s_gcmapSoundSkips < 8 )
			Con_Reportf( "Xash3D GameCube: sound load skipped for map-load memopt %s\n", sfx->name );
		s_gcmapSoundSkips++;
		return NULL;
	}
	if( S_GCAllowRuntimeSoundLoad() )
	{
		/* Drop finished button10 only; do not rotate other pl_step* here —
		 * preload needs all four resident, and soft eviction was discarding
		 * them one-by-one before the next RegisterSound. */
		if( !Q_strnicmp( sfx->name, "player/pl_step", 14 ))
		{
			uint i;
			for( i = 0; i < s_numSfx; i++ )
			{
				sfx_t *other = &s_knownSfx[i];
				if( other->cache && !Q_stricmp( other->name, "buttons/button10.wav" ))
					S_GCEvictBudgetedSfx( other );
			}
		}
		else
		{
			/* G125: weapon fire needs a ~13 KiB FS file buffer — reclaim every
			 * small budgeted SFX (preloaded steps) before the first attempt. */
			size_t want = GC_SFX_LOAD_HEADROOM_SOFT;
			if( !Q_strnicmp( sfx->name, "weapons/", 8 ))
				want = 16u * 1024u;
			{
				size_t reclaimed = S_GCMakeRoomForBudgetedLoad( sfx, want, 8u * 1024u );
				if( reclaimed )
					Con_Reportf( "Xash3D GameCube: G125 LRU reclaimed=%u before %s\n",
						(uint)reclaimed, sfx->name );
			}
		}
		Con_Reportf( "Xash3D GameCube: sound load allowed for gameplay sfx %s\n", sfx->name );
	}
#endif

	// load it from disk
	if( Q_stricmp( sfx->name, "*default" ))
	{
		// load it from disk
		if( s_warn_late_precache.value > 0 && cls.state == ca_active )
			Con_Printf( S_WARN "%s: late precache of %s\n", __func__, sfx->name );

		if( sfx->name[0] == '*' )
			sc = FS_LoadSound( sfx->name + 1, NULL, 0 );
		else
			sc = FS_LoadSound( sfx->name, NULL, 0 );
	}

	if( !sc )
	{
#if XASH_GAMECUBE
		if( GC_MapLoadMemoryOpt() && Q_stricmp( sfx->name, "*default" )
			&& S_GCAllowRuntimeSoundLoad() )
		{
			size_t reclaimed = S_GCMakeRoomForBudgetedLoad( sfx, 16u * 1024u, 8u * 1024u );
			FS_ClearFindMissCache();
			if( reclaimed )
				Con_Reportf( "Xash3D GameCube: G125 LRU retry reclaimed=%u for %s\n",
					(uint)reclaimed, sfx->name );
			if( sfx->name[0] == '*' )
				sc = FS_LoadSound( sfx->name + 1, NULL, 0 );
			else
				sc = FS_LoadSound( sfx->name, NULL, 0 );
			if( sc )
				Con_Reportf( "Xash3D GameCube: G125 load retry ok for %s\n", sfx->name );
		}
		if( !sc && GC_MapLoadMemoryOpt() && Q_stricmp( sfx->name, "*default" ))
		{
			Con_Reportf( "Xash3D GameCube: sound fallback skipped for map-load memopt %s\n", sfx->name );
			return NULL;
		}
#endif
		if( !sc )
			sc = S_CreateDefaultSound();
	}

	if( sc->rate < SOUND_11k ) // some bad sounds
		Sound_Process( &sc, SOUND_11k, sc->width, sc->channels, SOUND_RESAMPLE );
	else if( sc->rate > SOUND_11k && sc->rate < SOUND_22k ) // some bad sounds
		Sound_Process( &sc, SOUND_22k, sc->width, sc->channels, SOUND_RESAMPLE );
	else if( sc->rate > SOUND_22k && sc->rate != SOUND_44k ) // some bad sounds
		Sound_Process( &sc, SOUND_44k, sc->width, sc->channels, SOUND_RESAMPLE );

#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt() && Q_stricmp( sfx->name, "*default" ))
	{
		int peak = 0;

		if( sc->size > GC_SFX_MAX_FILE_BYTES
			|| s_gc_sfx_budget_used + sc->size > GC_SFX_BUDGET_BYTES )
		{
			Con_Reportf( "Xash3D GameCube: gameplay sfx budget refused name=%s size=%u used=%u cap=%u\n",
				sfx->name, (uint)sc->size, (uint)s_gc_sfx_budget_used, (uint)GC_SFX_BUDGET_BYTES );
			FS_FreeSound( sc );
			return NULL;
		}

		s_gc_sfx_budget_used += sc->size;
		SetBits( sc->flags, GC_SOUND_RUNTIME_BUDGETED );
		S_GCTouchBudgetedLru( sfx );

		if( sc->width == 2 )
		{
			const int16_t *samples = (const int16_t *)sc->buffer;
			for( size_t i = 0; i < sc->size / sizeof( *samples ); i++ )
			{
				int value = samples[i];
				if( value < 0 ) value = -value;
				if( value > peak ) peak = value;
			}
		}
		else
		{
			for( size_t i = 0; i < sc->size; i++ )
			{
				int value = abs((signed char)sc->buffer[i] );
				if( value > peak ) peak = value;
			}
		}
		Con_Reportf( "Xash3D GameCube: gameplay sound decoded samples=%u bytes=%u rate=%u width=%u channels=%u peak=%d budget_used=%u cap=%u\n",
			sc->samples, (uint)sc->size, sc->rate, sc->width, sc->channels, peak,
			(uint)s_gc_sfx_budget_used, (uint)GC_SFX_BUDGET_BYTES );
	}
#endif

	sfx->cache = sc;

	return sfx->cache;
}

/*
==================
S_FindName

==================
*/
sfx_t *S_FindName( const char *pname, qboolean *pfInCache )
{
	sfx_t	*sfx;
	uint	i;
	string	name;

	if( COM_StringEmptyOrNULL( pname ) || !snd.initialized )
		return NULL;

	if( Q_strlen( pname ) >= sizeof( sfx->name ))
		return NULL;

	Q_strncpy( name, pname, sizeof( name ));
	COM_FixSlashes( name );

	// see if already loaded
	uint hash = COM_HashKey( name, MAX_SFX_HASH );
	for( sfx = s_sfxHashList[hash]; sfx; sfx = sfx->hashNext )
	{
		if( !Q_strcmp( sfx->name, name ))
		{
			if( pfInCache )
			{
				// indicate whether or not sound is currently in the cache.
				*pfInCache = ( sfx->cache != NULL ) ? true : false;
			}
			// prolonge registration
			sfx->servercount = cl.servercount;
			return sfx;
		}
	}

	// find a free sfx slot spot
	for( i = 0, sfx = s_knownSfx; i < s_numSfx; i++, sfx++)
		if( !sfx->name[0] ) break; // free spot

	if( i == s_numSfx )
	{
		if( s_numSfx == MAX_SFX )
			return NULL;
		s_numSfx++;
	}

	sfx = &s_knownSfx[i];
	memset( sfx, 0, sizeof( *sfx ));
	if( pfInCache ) *pfInCache = false;
	Q_strncpy( sfx->name, name, sizeof( sfx->name ));
	sfx->servercount = cl.servercount;
	sfx->hashValue = COM_HashKey( sfx->name, MAX_SFX_HASH );

	// link it in
	sfx->hashNext = s_sfxHashList[sfx->hashValue];
	s_sfxHashList[sfx->hashValue] = sfx;

	return sfx;
}

/*
==================
S_FreeSound
==================
*/
void S_FreeSound( sfx_t *sfx )
{
	if( !sfx || !sfx->name[0] )
		return;

	// de-link it from the hash tree
	sfx_t **prev = &s_sfxHashList[sfx->hashValue];
	while( 1 )
	{
		sfx_t *hashSfx = *prev;
		if( !hashSfx )
			break;

		if( hashSfx == sfx )
		{
			*prev = hashSfx->hashNext;
			break;
		}
		prev = &hashSfx->hashNext;
	}

	if( clgame.soundFuncs.pfnS_FreeSound )
	{
#if XASH_GAMECUBE
		if( sfx->cache && FBitSet( sfx->cache->flags, GC_SOUND_RUNTIME_BUDGETED ))
		{
			if( sfx->cache->size > s_gc_sfx_budget_used )
				s_gc_sfx_budget_used = 0;
			else s_gc_sfx_budget_used -= sfx->cache->size;
		}
#endif
		clgame.soundFuncs.pfnS_FreeSound( sfx, sfx - s_knownSfx );
		return;
	}

#if XASH_GAMECUBE
	if( sfx->cache && FBitSet( sfx->cache->flags, GC_SOUND_RUNTIME_BUDGETED ))
	{
		if( sfx->cache->size > s_gc_sfx_budget_used )
			s_gc_sfx_budget_used = 0;
		else s_gc_sfx_budget_used -= sfx->cache->size;
	}
#endif
	if( sfx->cache )
		FS_FreeSound( sfx->cache );
	memset( sfx, 0, sizeof( *sfx ));
}

/*
=====================
S_BeginRegistration

=====================
*/
void S_BeginRegistration( void )
{
	snd.have_ambient_sfx = false;

	// check for automatic ambient sounds
	for( int i = 0; i < NUM_AMBIENTS; i++ )
	{
		if( !GI->ambientsound[i][0] )
			continue;	// empty slot

		snd.ambient_sfx[i] = S_RegisterSound( GI->ambientsound[i] );
		if( snd.ambient_sfx[i] )
			snd.have_ambient_sfx = true; // allow auto-ambients
	}

	s_registering = true;
}

/*
=====================
S_EndRegistration

=====================
*/
void S_EndRegistration( void )
{
	sfx_t	*sfx;
	int	i;

	if( !s_registering || !snd.initialized )
		return;

	// free any sounds not from this registration sequence
	for( i = 0, sfx = s_knownSfx; i < s_numSfx; i++, sfx++ )
	{
		if( !sfx->name[0] || !Q_stricmp( sfx->name, "*default" ))
			continue; // don't release default sound

		if( sfx->servercount != cl.servercount )
			S_FreeSound( sfx ); // don't need this sound
	}

	// load everything in
	for( i = 0, sfx = s_knownSfx; i < s_numSfx; i++, sfx++ )
	{
		if( !sfx->name[0] )
			continue;
		S_LoadSound( sfx );
	}
	s_registering = false;
}

/*
==================
S_RegisterSound

==================
*/
sound_t S_RegisterSound( const char *name )
{
	sfx_t	*sfx;

	if( COM_StringEmptyOrNULL( name ) || !snd.initialized )
		return -1;

	if( S_TestSoundChar( name, '!' ))
	{
		Q_strncpy( s_sentenceImmediateName, name, sizeof( s_sentenceImmediateName ));
		return SENTENCE_INDEX;
	}

	// some stupid mappers used leading '/' or '\' in path to models or sounds
	if( name[0] == '/' || name[0] == '\\' ) name++;
	if( name[0] == '/' || name[0] == '\\' ) name++;

	sfx = S_FindName( name, NULL );
	if( !sfx ) return -1;

	sfx->servercount = cl.servercount;
	if( !s_registering ) S_LoadSound( sfx );

	return sfx - s_knownSfx;
}

sfx_t *S_GetSfxByHandle( sound_t handle )
{
	if( !snd.initialized )
		return NULL;

	// create new sfx
	if( handle == SENTENCE_INDEX )
		return S_FindName( s_sentenceImmediateName, NULL );

	if( handle < 0 || handle >= s_numSfx )
		return NULL;

	return &s_knownSfx[handle];
}

/*
=================
S_InitSounds
=================
*/
void S_InitSounds( void )
{
#if XASH_GAMECUBE
	s_gc_sfx_budget_used = 0;
	s_gcmapSoundSkips = 0;
	s_gc_sfx_lru_clock = 0;
	memset( s_gc_lru, 0, sizeof( s_gc_lru ));
#endif
	// create unused 0-entry
	Q_strncpy( s_knownSfx->name, "*default", sizeof( s_knownSfx->name ));
	s_knownSfx->hashValue = COM_HashKey( s_knownSfx->name, MAX_SFX_HASH );
	s_knownSfx->hashNext = s_sfxHashList[s_knownSfx->hashValue];
	s_sfxHashList[s_knownSfx->hashValue] = s_knownSfx;
	s_knownSfx->cache = S_CreateDefaultSound();
	s_numSfx = 1;
}

/*
=================
S_FreeSounds
=================
*/
void S_FreeSounds( void )
{
	sfx_t	*sfx;
	int	i;

	if( !snd.initialized )
		return;

	// stop all sounds
	S_StopAllSounds( true );

	// free all sounds
	for( i = 0, sfx = s_knownSfx; i < s_numSfx; i++, sfx++ )
		S_FreeSound( sfx );


	memset( s_knownSfx, 0, sizeof( s_knownSfx ));
	memset( s_sfxHashList, 0, sizeof( s_sfxHashList ));

	s_numSfx = 0;
}
