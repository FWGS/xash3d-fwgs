/*
mod_sprite.c - sprite loading
Copyright (C) 2010 Uncle Mike
Copyright (C) 2019 a1batross

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
#include "sprite.h"
#include "studio.h"
#include "mod_local.h"
#include "swaplib.h"

le_struct_begin( dsprite_q1_swap )
	le_struct_field( dsprite_q1_t, ident )
	le_struct_field( dsprite_q1_t, version )
	le_struct_field( dsprite_q1_t, type )
	le_struct_field( dsprite_q1_t, boundingradius )
	le_struct_array( dsprite_q1_t, bounds, 2 )
	le_struct_field( dsprite_q1_t, numframes )
	le_struct_field( dsprite_q1_t, beamlength )
	le_struct_field( dsprite_q1_t, synctype )
le_struct_end();

le_struct_begin( dsprite_hl_swap )
	le_struct_field( dsprite_hl_t, ident )
	le_struct_field( dsprite_hl_t, version )
	le_struct_field( dsprite_hl_t, type )
	le_struct_field( dsprite_hl_t, texFormat )
	le_struct_field( dsprite_hl_t, boundingradius )
	le_struct_array( dsprite_hl_t, bounds, 2 )
	le_struct_field( dsprite_hl_t, numframes )
	le_struct_field( dsprite_hl_t, facetype )
	le_struct_field( dsprite_hl_t, synctype )
le_struct_end();

le_struct_begin( dspriteframe_swap )
	le_struct_array( dspriteframe_t, origin, 2 )
	le_struct_field( dspriteframe_t, width )
	le_struct_field( dspriteframe_t, height )
le_struct_end();

static byte *Mod_SwapSpriteFrame( byte *p, byte *end, int bytes )
{
	dspriteframe_t frame;

	if( p + sizeof( frame ) > end )
		return NULL;

	memcpy( &frame, p, sizeof( frame ));
	le_struct_swap( dspriteframe_swap, &frame );
	memcpy( p, &frame, sizeof( frame ));
	p += sizeof( frame );

	// skip pixel data
	if( frame.width < 0 || frame.height < 0 )
		return NULL;

	uint64_t pixels = (uint64_t)frame.width * frame.height * bytes;

	if( pixels > (size_t)( end - p ))
		return NULL;

	p += pixels;
	return p;
}

static byte *Mod_SwapSpriteGroup( byte *p, byte *end, int bytes, int min_frames )
{
	dspritegroup_t group;

	if( p + sizeof( group ) > end )
		return NULL;

	memcpy( &group, p, sizeof( group ));
	group.numframes = LittleLong( group.numframes );
	memcpy( p, &group, sizeof( group ));
	p += sizeof( group );

	// swap intervals
	int numframes = group.numframes;

	if( numframes < min_frames || (size_t)numframes > (size_t)( end - p ) / sizeof( dspriteinterval_t ))
		return NULL;

	for( int i = 0; i < numframes; i++ )
	{
		dspriteinterval_t interval;

		memcpy( &interval, p, sizeof( interval ));
		interval.interval = LittleFloat( interval.interval );
		memcpy( p, &interval, sizeof( interval ));
		p += sizeof( interval );
	}

	// swap each frame in the group
	for( int i = 0; i < numframes; i++ )
	{
		p = Mod_SwapSpriteFrame( p, end, bytes );
		if( !p )
			return NULL;
	}

	return p;
}

static qboolean Mod_SwapSprite( void *buffer, size_t buffersize, int *out_version )
{
	byte *end = (byte *)buffer + buffersize;
	int numframes;
	byte *p;

	if( buffersize < sizeof( dsprite_t ))
		return false;

	// peek at ident + version before full swap
	int version = LittleLong(((dsprite_t *)buffer)->version );

	switch( version )
	{
	case SPRITE_VERSION_Q1:
	case SPRITE_VERSION_32:
		if( buffersize < sizeof( dsprite_q1_t ))
			return false;
		le_struct_swap( dsprite_q1_swap, buffer );
		numframes = ((dsprite_q1_t *)buffer)->numframes;
		p = (byte *)buffer + sizeof( dsprite_q1_t );
		break;
	case SPRITE_VERSION_HL:
	{
		if( buffersize < sizeof( dsprite_hl_t ))
			return false;
		le_struct_swap( dsprite_hl_swap, buffer );
		numframes = ((dsprite_hl_t *)buffer)->numframes;
		p = (byte *)buffer + sizeof( dsprite_hl_t );

		// HL sprites have a palette count (short) + palette data
		if( p + sizeof( short ) > end )
			return false;

		short numi = LittleShort( *(short *)p );
		*(short *)p = numi;
		p += sizeof( short );

		if( numi < 0 || p + numi * 3 > end )
			return false;

		p += numi * 3;
		break;
	}
	default:
		return false;
	}

	if( numframes < 0 )
		return false;

	*out_version = version;
	int bytes = ( version == SPRITE_VERSION_32 ) ? 4 : 1;

	// swap all frames
	for( int i = 0; i < numframes; i++ )
	{
		dframetype_t frametype;

		if( p + sizeof( frametype ) > end )
			return false;

		memcpy( &frametype, p, sizeof( frametype ));
		frametype.type = LittleLong( frametype.type );
		memcpy( p, &frametype, sizeof( frametype ));
		p += sizeof( frametype );

		switch( frametype.type )
		{
		case FRAME_SINGLE:
			p = Mod_SwapSpriteFrame( p, end, bytes );
			break;
		case FRAME_GROUP:
			p = Mod_SwapSpriteGroup( p, end, bytes, 1 );
			break;
		case FRAME_ANGLED:
			// angled groups are indexed by 0..7 at render time
			p = Mod_SwapSpriteGroup( p, end, bytes, 8 );
			break;
		default:
			return false;
		}

		if( !p )
			return false;
	}

	return true;
}

/*
====================
Mod_LoadSpriteModel

load sprite model
====================
*/
void Mod_LoadSpriteModel( model_t *mod, void *buffer, size_t buffersize, qboolean *loaded )
{
	msprite_t *psprite;

	if( loaded )
		*loaded = false;

	int version;
	if( !Mod_SwapSprite( buffer, buffersize, &version ))
	{
		Con_DPrintf( S_ERROR "%s: %s is not a valid sprite\n", __func__, mod->name );
		return;
	}

	mod->type = mod_sprite;
	char poolname[MAX_VA_STRING];
	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );
	mod->mempool = Mem_AllocPool( poolname );

	if( version == SPRITE_VERSION_Q1 || version == SPRITE_VERSION_32 )
	{
		dsprite_q1_t *pinq1 = buffer;

		if( pinq1->numframes == 0 )
		{
			Con_DPrintf( S_ERROR "%s: %s has no frames\n", __func__, mod->name );
			return;
		}

		size_t size = sizeof( msprite_t ) + ( pinq1->numframes - 1 ) * sizeof( psprite->frames );

		psprite = Mem_Calloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinq1->type;
		psprite->texFormat = SPR_ADDITIVE;
		psprite->numframes = mod->numframes = pinq1->numframes;
		psprite->facecull = SPR_CULL_FRONT;
		float radius = floorf( pinq1->boundingradius );
		if( radius >= (float)INT_MAX )
			psprite->radius = INT_MAX;
		else if( radius < 0.0f || IS_NAN( radius ))
			psprite->radius = 0;
		else
			psprite->radius = (int)radius;
		psprite->synctype = pinq1->synctype;

		// LadyHavoc: hack to allow sprites to be non-fullbright
		if( Q_strchr( mod->name, '!' ))
			psprite->texFormat = SPR_ALPHTEST;

		mod->maxs[0] = mod->maxs[1] = pinq1->bounds[0] * 0.5f;
		mod->mins[0] = mod->mins[1] = -mod->maxs[0];
		mod->maxs[2] = pinq1->bounds[1] * 0.5f;
		mod->mins[2] = -mod->maxs[2];
	}
	else // if( version == SPRITE_VERSION_HL )
	{
		dsprite_hl_t *pinhl = buffer;

		if( pinhl->numframes == 0 )
		{
			Con_DPrintf( S_WARN "%s: %s has no frames\n", __func__, mod->name );
			return;
		}

		size_t size = sizeof( msprite_t ) + ( pinhl->numframes - 1 ) * sizeof( psprite->frames );

		psprite = Mem_Calloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinhl->type;
		psprite->texFormat = pinhl->texFormat;
		psprite->numframes = mod->numframes = pinhl->numframes;
		psprite->facecull = pinhl->facetype;
		psprite->radius = pinhl->boundingradius;
		psprite->synctype = pinhl->synctype;

		mod->maxs[0] = mod->maxs[1] = pinhl->bounds[0] * 0.5f;
		mod->mins[0] = mod->mins[1] = -mod->maxs[0];
		mod->maxs[2] = pinhl->bounds[1] * 0.5f;
		mod->mins[2] = -mod->maxs[2];
	}

	if( loaded )
		*loaded = true;	// done

	if( Host_IsDedicated() )
	{
		// skip frames loading
		psprite->numframes = 0;
		return;
	}

#if !XASH_DEDICATED
	Mod_SpriteLoadTextures( mod, buffer );
#endif
}

#if XASH_LLVM_LIBFUZZER
int EXPORT Fuzz_Mod_LoadSpriteModel( const uint8_t *Data, size_t Size );
int EXPORT Fuzz_Mod_LoadSpriteModel( const uint8_t *Data, size_t Size )
{
	model_t mod = { .name = "#internal.spr", .needload = NL_NEEDS_LOADED };
	qboolean loaded = false;
	byte *buf;

	if( Size == 0 )
		return 0;

	Memory_Init();

	// dedicated mode exercises the parser/validator without needing a renderer
	host.type = HOST_DEDICATED;
	host.mempool = Mem_AllocPool( "fuzzing pool" );

	// the loader byteswaps the buffer in place, so hand it a writable copy
	buf = Mem_Malloc( host.mempool, Size );
	memcpy( buf, Data, Size );

	Mod_LoadSpriteModel( &mod, buf, Size, &loaded );

	if( mod.mempool )
		Mem_FreePool( &mod.mempool );
	Mem_FreePool( &host.mempool );

	return 0;
}
#endif // XASH_LLVM_LIBFUZZER
