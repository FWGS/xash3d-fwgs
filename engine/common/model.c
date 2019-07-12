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
#include "mathlib.h"
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
byte		*com_studiocache;		// cache for submodels
convar_t		*mod_studiocache;
convar_t		*r_wadtextures;
convar_t		*r_showhull;
model_t		*loadmodel;

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
		if( !mod->name[0] )
			continue; // free slot
		Con_Printf( "%s\n", mod->name );
		nummodels++;
	}

	Con_Printf( "-----------------------------------\n" );
	Con_Printf( "%i total models\n", nummodels );
	Con_Printf( "\n" );
}

/*
================
Mod_FreeUserData
================
*/
static void Mod_FreeUserData( model_t *mod )
{
	// ignore submodels and freed models
	if( !mod->name[0] || mod->name[0] == '*' )
		return;

	if( Host_IsDedicated() )
	{
		if( svgame.physFuncs.Mod_ProcessUserData != NULL )
		{
			// let the server.dll free custom data
			svgame.physFuncs.Mod_ProcessUserData( mod, false, NULL );
		}
	}
#ifndef XASH_DEDICATED
	else
	{
		ref.dllFuncs.Mod_ProcessRenderData( mod, false, NULL );
	}
#endif
}

/*
================
Mod_FreeModel
================
*/
void Mod_FreeModel( model_t *mod )
{
	// already freed?
	if( !mod || !mod->name[0] )
		return;

	if( mod->type != mod_brush || mod->name[0] != '*' )
	{
		Mod_FreeUserData( mod );
		Mem_FreePool( &mod->mempool );
	}

	if( mod->type == mod_brush && FBitSet( mod->flags, MODEL_WORLD ) )
	{
		world.shadowdata = NULL;
		world.deluxedata = NULL;
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
	mod_studiocache = Cvar_Get( "r_studiocache", "1", FCVAR_ARCHIVE, "enables studio cache for speedup tracing hitboxes" );
	r_wadtextures = Cvar_Get( "r_wadtextures", "0", 0, "completely ignore textures in the bsp-file if enabled" );
	r_showhull = Cvar_Get( "r_showhull", "0", 0, "draw collision hulls 1-3" );

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
	int	i;

#ifndef XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
	for( i = 0; i < mod_numknown; i++ )
		Mod_FreeModel( &mod_known[i] );
	mod_numknown = 0;
}

/*
================
Mod_ClearUserData
================
*/
void Mod_ClearUserData( void )
{
	int	i;

	for( i = 0; i < mod_numknown; i++ )
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
			if( mod->mempool || mod->name[0] == '*' )
				mod->needload = NL_PRESENT;
			else mod->needload = NL_NEEDS_LOADED;

			return mod;
		}
	}

	// find a free model slot spot
	for( i = 0, mod = mod_known; i < mod_numknown; i++, mod++ )
		if( !mod->name[0] ) break; // this is a valid spot

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
model_t *Mod_LoadModel( model_t *mod, qboolean crash )
{
	char		tempname[MAX_QPATH];
	fs_offset_t		length = 0;
	qboolean		loaded;
	byte		*buf;
	model_info_t	*p;

	ASSERT( mod != NULL );

	// check if already loaded (or inline bmodel)
	if( mod->mempool || mod->name[0] == '*' )
	{
		mod->needload = NL_PRESENT;
		return mod;
	}

	ASSERT( mod->needload == NL_NEEDS_LOADED );

	// store modelname to show error
	Q_strncpy( tempname, mod->name, sizeof( tempname ));
	COM_FixSlashes( tempname );

	buf = FS_LoadFile( tempname, &length, false );

	if( !buf )
	{
		memset( mod, 0, sizeof( model_t ));

		if( crash ) Host_Error( "Could not load model %s from disk\n", tempname );
		else Con_Printf( S_ERROR "Could not load model %s from disk\n", tempname );

		return NULL;
	}

	Con_Reportf( "loading %s\n", mod->name );
	mod->needload = NL_PRESENT;
	mod->type = mod_bad;
	loadmodel = mod;

	// call the apropriate loader
	switch( *(uint *)buf )
	{
	case IDSTUDIOHEADER:
		Mod_LoadStudioModel( mod, buf, &loaded );
		break;
	case IDSPRITEHEADER:
		Mod_LoadSpriteModel( mod, buf, &loaded, 0 );
		break;
	case IDALIASHEADER:
		// REFTODO: move server-related code here
		loaded = true;
		break;
	case Q1BSP_VERSION:
	case HLBSP_VERSION:
	case QBSP2_VERSION:
		Mod_LoadBrushModel( mod, buf, &loaded );
		// ref.dllFuncs.Mod_LoadModel( mod_brush, mod, buf, &loaded, 0 );
		break;
	default:
		Mem_Free( buf );
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
		}
#ifndef XASH_DEDICATED
		else
		{
			loaded = ref.dllFuncs.Mod_ProcessRenderData( mod, true, buf );
		}
#endif
	}

	if( !loaded )
	{
		Mod_FreeModel( mod );
		Mem_Free( buf );

		if( crash ) Host_Error( "Could not load model %s\n", tempname );
		else Con_Printf( S_ERROR "Could not load model %s\n", tempname );

		return NULL;
	}

	p = &mod_crcinfo[mod - mod_known];
	mod->needload = NL_PRESENT;

	if( FBitSet( p->flags, FCRC_SHOULD_CHECKSUM ))
	{
		CRC32_t	currentCRC;

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
	Mem_Free( buf );

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
	model_t	*mod;

	if( !COM_CheckString( name ))
		return NULL;

	mod = Mod_FindName( name, trackCRC );
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
	int	i;

	// refresh hull data
	SetBits( r_showhull->flags, FCVAR_CHANGED );
#ifndef XASH_DEDICATED
	Mod_ReleaseHullPolygons();
#endif
	// release previois map
	Mod_FreeModel( mod_known );	// world is stuck on slot #0 always

	// we should release all the world submodels
	// and clear studio sequences
	for( i = 1; i < mod_numknown; i++ )
	{
		if( mod_known[i].type == mod_studio )
			mod_known[i].submodels = NULL;
		if( mod_known[i].name[0] == '*' )
			Mod_FreeModel( &mod_known[i] );
		mod_known[i].needload = NL_UNREFERENCED;
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
	model_t	*pworld;

	// already loaded?
	if( !Q_stricmp( mod_known->name, name ))
		return mod_known;

	// free sequence files on studiomodels
	Mod_PurgeStudioCache();

	// load the newmap
	world.loading = true;
	pworld = Mod_FindName( name, false );
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
		if( mod->needload == NL_UNREFERENCED && COM_CheckString( mod->name ))
			Mod_FreeModel( mod );
	}
}

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
void *Mod_Calloc( int number, size_t size )
{
	cache_user_t	*cu;

	if( number <= 0 || size <= 0 ) return NULL;
	cu = (cache_user_t *)Mem_Calloc( com_studiocache, sizeof( cache_user_t ) + number * size );
	cu->data = (void *)cu; // make sure what cu->data is not NULL

	return cu;
}

/*
===============
Mod_CacheCheck

===============
*/
void *Mod_CacheCheck( cache_user_t *c )
{
	return Cache_Check( com_studiocache, c );
}

/*
===============
Mod_LoadCacheFile

===============
*/
void Mod_LoadCacheFile( const char *filename, cache_user_t *cu )
{
	char	modname[MAX_QPATH];
	fs_offset_t	size;
	byte	*buf;

	Assert( cu != NULL );

	if( !COM_CheckString( filename ))
		return;

	Q_strncpy( modname, filename, sizeof( modname ));
	COM_FixSlashes( modname );

	buf = FS_LoadFile( modname, &size, false );
	if( !buf || !size ) Host_Error( "LoadCacheFile: ^1can't load %s^7\n", filename );
	cu->data = Mem_Malloc( com_studiocache, size );
	memcpy( cu->data, buf, size );
	Mem_Free( buf );
}

/*
===============
Mod_AliasExtradata

===============
*/
void *Mod_AliasExtradata( model_t *mod )
{
	if( mod && mod->type == mod_alias )
		return mod->cache.data;
	return NULL;
}

/*
===============
Mod_StudioExtradata

===============
*/
void *Mod_StudioExtradata( model_t *mod )
{
	if( mod && mod->type == mod_studio )
		return mod->cache.data;
	return NULL;
}

/*
==================
Mod_ValidateCRC

==================
*/
qboolean Mod_ValidateCRC( const char *name, CRC32_t crc )
{
	model_info_t	*p;
	model_t		*mod;

	mod = Mod_FindName( name, true );
	p = &mod_crcinfo[mod - mod_known];

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
	model_t		*mod;
	model_info_t	*p;

	mod = Mod_FindName( name, true );
	p = &mod_crcinfo[mod - mod_known];

	if( needCRC ) SetBits( p->flags, FCRC_SHOULD_CHECKSUM );
	else ClearBits( p->flags, FCRC_SHOULD_CHECKSUM );
}


/*
==================
Mod_Handle

==================
*/
model_t *GAME_EXPORT Mod_Handle( int handle )
{
	if( handle < 0 || handle >= MAX_MODELS )
	{
		Con_Reportf( "Mod_Handle: bad handle #%i\n", handle );
		return NULL;
	}
	return &mod_known[handle];
}
