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
void FS_ClearFindMissCache( void );
static poolhandle_t gc_gcmap_stubpool;

/* New Game only: a few real MDLs (NPCs/viewweapons) instead of empty stubs.
 * Mesh-only (no studio texel upload) — skins bind white under quality 0. */
#define GC_REAL_STUDIO_MAX_NPC    4
#define GC_REAL_STUDIO_MAX_VIEW   2
#define GC_REAL_STUDIO_MAX_BYTES  (400 * 1024)

static int    gc_real_studio_npc;
static int    gc_real_studio_view;
static size_t gc_real_studio_bytes;

static qboolean Mod_GCStudioNameAllowed( const char *name, qboolean *is_viewmodel )
{
	char bare[64];
	static const char *npcs[] = {
		"scientist", "barney", "gman", "player", "roach",
		"headcrab", "houndeye", "zombie",
		"scientist01", "scientist02", "scientist03",
		NULL
	};
	static const char *views[] = {
		"v_crowbar", "v_9mmhandgun", "v_9mmar", "v_shotgun", "v_357",
		"w_crowbar", "w_9mmhandgun",
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

	if( !Sys_CheckParm( "-gcnewgame" ))
		return false;
	if( !Mod_GCStudioNameAllowed( name, &is_view ))
		return false;
	if( is_view )
	{
		if( gc_real_studio_view >= GC_REAL_STUDIO_MAX_VIEW )
			return false;
	}
	else if( gc_real_studio_npc >= GC_REAL_STUDIO_MAX_NPC )
		return false;
	if( filesize < sizeof( studiohdr_t ) || filesize > ( 220 * 1024 ))
		return false;
	if( gc_real_studio_bytes + filesize > GC_REAL_STUDIO_MAX_BYTES )
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
=============
*/
static byte *Mod_GCLoadStudioFile( const char *model_path, fs_offset_t *length )
{
	char bare[64];
	char mirror[MAX_QPATH];
	byte *buf;

	*length = 0;
	COM_FileBase( model_path, bare, sizeof( bare ));
	Q_snprintf( mirror, sizeof( mirror ), "gc_studio/%s.mdl", bare );

	buf = FS_LoadFileMalloc( mirror, length, false );
	if( buf && *length >= (fs_offset_t)sizeof( studiohdr_t ))
	{
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

/*
=============
Mod_GCLoadNewGameStudios

Promote allowlisted stub MDLs to lean mesh+skin studios after map prep / netchan
are past the MEM1 cliff (same deferral idea as lean skybox).
=============
*/
void Mod_GCLoadNewGameStudios( void )
{
	/* Tiny world NPC first (roach ~7KB); gman (~76KB) fails libc malloc after
	 * crowbars on GC. Tram PVS often has solids=0 — force-draw in low-res. */
	static const char *promote[] = {
		"models/v_crowbar.mdl",
		"models/roach.mdl",
		"models/w_crowbar.mdl",
		NULL
	};
	int i;

	if( !Sys_CheckParm( "-gcnewgame" ))
		return;

	FS_ClearFindMissCache();
	Image_GCPurgeDecodeScratch();

	for( i = 0; promote[i]; i++ )
	{
		model_t *mod;
		byte *buf;
		fs_offset_t length = 0;
		qboolean loaded = false;
		studiohdr_t *hdr;

		buf = Mod_GCLoadStudioFile( promote[i], &length );
		if( !buf || length < (fs_offset_t)sizeof( studiohdr_t ))
		{
			Con_Reportf( "Xash3D GameCube: deferred studio skip '%s' read=%li\n",
				promote[i], (long)length );
			if( buf )
				free( buf );
			continue;
		}

		/* Keep embedded skins for allowlisted New Game studios (roach/crowbar
		 * are tiny). Soft path uploads paletted texels then drops the blob. */

		if( !Mod_GCAllowRealStudioLoad( promote[i], (size_t)length ))
		{
			Con_Reportf( "Xash3D GameCube: deferred studio budget skip '%s' (%s)\n",
				promote[i], Q_memprint( (size_t)length ));
			free( buf );
			continue;
		}

		mod = Mod_FindName( promote[i], false );
		if( !mod )
		{
			Con_Reportf( "Xash3D GameCube: deferred studio skip '%s' (not registered)\n",
				promote[i] );
			free( buf );
			continue;
		}

		hdr = (studiohdr_t *)mod->cache.data;
		if( hdr && hdr->numbodyparts > 0 )
		{
			free( buf );
			continue;
		}

		mod->cache.data = NULL;
		if( mod->mempool == gc_gcmap_stubpool )
			mod->mempool = 0;
		mod->needload = NL_PRESENT;
		mod->type = mod_studio;

		Image_GCPurgeDecodeScratch();
		Mod_LoadStudioModel( mod, buf, (size_t)length, &loaded );
		free( buf );
		Image_GCPurgeDecodeScratch();

		if( loaded )
		{
			size_t kept = length;
			studiohdr_t *loaded_hdr = (studiohdr_t *)mod->cache.data;

			if( loaded_hdr && loaded_hdr->length > 0 && (size_t)loaded_hdr->length < kept )
				kept = (size_t)loaded_hdr->length;
			Mod_GCNoteRealStudioLoaded( promote[i], kept );
		}
		else
		{
			Con_Reportf( S_WARN "Xash3D GameCube: deferred studio promote failed '%s'\n", promote[i] );
			Mod_LoadStudioGcmapStub( mod, &loaded );
		}
	}

	Con_Reportf( "Xash3D GameCube: deferred studio done npc=%d view=%d budget=%s\n",
		gc_real_studio_npc, gc_real_studio_view, Q_memprint( gc_real_studio_bytes ));
}

static qboolean Mod_GCMapVerboseModelLoad( const char *name )
{
	if( !GC_MapLoadMemoryOpt())
		return false;
	if( !name )
		return false;

	/* High-traffic campaign chapters precache hundreds of weapon/item stubs.
	 * Keep OSReport readable and avoid probe slowdowns by logging only the
	 * world BSP and unusual failure paths, not every successful stub load. */
	if( !Q_strnicmp( name, "maps/", 5 ))
		return true;
	return false;
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
static void Mod_FreeLoadBuffer( void *buf )
{
	if( !buf )
		return;
	if( GC_ReleaseMapLoadBuffer( buf ))
		return;
	Mem_Free( buf );
}

void Mod_ReleaseBrushSourceBuffer( void *buf )
{
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

	if( mod->type != mod_brush || mod->name[0] != '*' )
	{
		Mod_FreeUserData( mod );
#if XASH_GAMECUBE
		if( mod->type == mod_brush )
			Mod_GameCubeFreeMallocSurfaces( mod );
		if( mod->type == mod_brush && mod->cache.data )
		{
			Mod_FreeLoadBuffer( mod->cache.data );
			mod->cache.data = NULL;
		}
		/* Mesh-only New Game studios keep cache on malloc (mempool == 0). */
		if( mod->type == mod_studio && !mod->mempool && mod->cache.data )
		{
			free( mod->cache.data );
			mod->cache.data = NULL;
		}
#endif
#if XASH_GAMECUBE
		if( mod->mempool != gc_gcmap_stubpool )
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
#if XASH_GAMECUBE
	gc_gcmap_stubpool = Mem_AllocPool( "GCMap Model Stub Pool" );
#endif
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
	for( int i = 0; i < mod_numknown; i++ )
		Mod_FreeModel( &mod_known[i] );
#if XASH_GAMECUBE
	if( gc_gcmap_stubpool )
		Mem_EmptyPool( gc_gcmap_stubpool );
	gc_real_studio_npc = 0;
	gc_real_studio_view = 0;
	gc_real_studio_bytes = 0;
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
#if XASH_GAMECUBE
	Mem_FreePool( &gc_gcmap_stubpool );
#endif
}

#if XASH_GAMECUBE
poolhandle_t Mod_GameCubeSharedModelStubPool( void )
{
	return gc_gcmap_stubpool;
}
#endif

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
	if( GC_MapLoadMemoryOpt())
	{
		const char *ext = COM_FileExtension( tempname );

		if( ext && !Q_stricmp( ext, "mdl" ))
		{
			/* Real meshes are promoted after map prep in Mod_GCLoadNewGameStudios. */
			mod->needload = NL_PRESENT;
			Mod_LoadStudioGcmapStub( mod, &loaded );
			if( !loaded )
			{
				if( crash ) Host_Error( "Could not load model %s\n", tempname );
				else Con_Printf( S_ERROR "Could not load model %s\n", tempname );
				return NULL;
			}

			if( world.loading )
				SetBits( mod->flags, MODEL_WORLD );

			return mod;
		}

		if( ext && !Q_stricmp( ext, "spr" ))
		{
			mod->needload = NL_PRESENT;
			Mod_LoadSpriteGcmapStub( mod, &loaded );
			if( !loaded )
			{
				if( crash ) Host_Error( "Could not load model %s\n", tempname );
				else Con_Printf( S_ERROR "Could not load model %s\n", tempname );
				return NULL;
			}

			return mod;
		}
	}
#endif

	byte *buf = NULL;
#if XASH_GAMECUBE
	/* Prefer the early-reserved contiguous buffer for world BSPs so retail New
	 * Game is not blocked by heap fragmentation after menu/client churn. */
	if( !Q_strnicmp( loadname, "maps/", 5 ))
	{
		fs_offset_t filesize = FS_FileSize( loadname, false );

		if( filesize > (fs_offset_t)sizeof( uint ))
		{
			buf = GC_BorrowMapLoadBuffer( (size_t)filesize );
			if( buf )
			{
				file_t *f = FS_Open( loadname, "rb", false );

				if( f )
				{
					length = FS_Read( f, buf, filesize );
					FS_Close( f );
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
				else
				{
					GC_ReleaseMapLoadBuffer( buf );
					buf = NULL;
				}
			}
		}
	}
	if( !buf )
#endif
		buf = FS_LoadFile( loadname, &length, false );

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
			Mod_FreeLoadBuffer( buf );

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
	// release previois map
	Mod_FreeModel( mod_known );	// world is stuck on slot #0 always

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

	// free sequence files on studiomodels
	Mod_PurgeStudioCache();

	// load the newmap
	world.loading = true;
	model_t *pworld = Mod_FindName( name, false );
#if XASH_GAMECUBE
	if( GC_MapLoadMemoryOpt())
	{
		fs_offset_t filesize = FS_FileSize( name, false );
		qboolean exists = FS_FileExists( name, false );

		Con_Reportf( "Xash3D GameCube: Mod_LoadWorld request='%s' registered='%s' exists=%d size=%li preload=%d\n",
			name, pworld ? pworld->name : "(null)", exists, (long)filesize, preload );
	}
#endif
	if( preload ) Mod_LoadModel( pworld, true );
	world.loading = false;

	ASSERT( pworld == mod_known );

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
		if( mod->needload == NL_PRESENT )
		{
			/* Keep New Game deferred real studio meshes for world present. */
			if( Sys_CheckParm( "-gcnewgame" ) && mod->type == mod_studio )
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
#if XASH_GAMECUBE
	GC_MemSample( "post-precache free" );
#endif
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

	byte *buf = FS_LoadFile( modname, &size, false );
	if( !buf || !size ) Host_Error( "LoadCacheFile: ^1can't load %s^7\n", filename );
	cu->data = Mem_Malloc( com_studiocache, size );
	memcpy( cu->data, buf, size );
	Mem_Free( buf );

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

#if XASH_ENGINE_TESTS

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

#endif // XASH_ENGINE_TESTS
