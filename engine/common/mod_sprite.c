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
	dspriteframe_t *frame;

	if( p + sizeof( *frame ) > end )
		return NULL;

	frame = (dspriteframe_t *)p;
	le_struct_swap( dspriteframe_swap, frame );
	p += sizeof( *frame );

	// skip pixel data
	if( p + frame->width * frame->height * bytes > end )
		return NULL;

	p += frame->width * frame->height * bytes;
	return p;
}

static byte *Mod_SwapSpriteGroup( byte *p, byte *end, int bytes )
{
	dspritegroup_t *group;

	if( p + sizeof( *group ) > end )
		return NULL;

	group = (dspritegroup_t *)p;
	group->numframes = LittleLong( group->numframes );
	p += sizeof( *group );

	// swap intervals
	int numframes = group->numframes;

	if( p + numframes * sizeof( dspriteinterval_t ) > end )
		return NULL;

	for( int i = 0; i < numframes; i++ )
	{
		dspriteinterval_t *interval = (dspriteinterval_t *)p;
		interval->interval = LittleFloat( interval->interval );
		p += sizeof( *interval );
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
	int version, numframes, bytes;
	byte *p;

	if( buffersize < sizeof( dsprite_t ))
		return false;

	// peek at ident + version before full swap
	version = LittleLong(((dsprite_t *)buffer)->version );

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
		p += sizeof( short ) + numi * 3;
		break;
	}
	default:
		return false;
	}

	*out_version = version;
	bytes = ( version == SPRITE_VERSION_32 ) ? 4 : 1;

	// swap all frames
	for( int i = 0; i < numframes && p && p < end; i++ )
	{
		dframetype_t *frametype;

		if( p + sizeof( *frametype ) > end )
			return false;

		frametype = (dframetype_t *)p;
		frametype->type = LittleLong( frametype->type );
		p += sizeof( *frametype );

		switch( frametype->type )
		{
		case FRAME_SINGLE:
			p = Mod_SwapSpriteFrame( p, end, bytes );
			break;
		case FRAME_GROUP:
		case FRAME_ANGLED:
			p = Mod_SwapSpriteGroup( p, end, bytes );
			break;
		default:
			return false;
		}
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
	char poolname[MAX_VA_STRING];
	int version;

	if( loaded )
		*loaded = false;

	if( !Mod_SwapSprite( buffer, buffersize, &version ))
	{
		Con_DPrintf( S_ERROR "%s: %s is not a valid sprite\n", __func__, mod->name );
		return;
	}

	mod->type = mod_sprite;
	Q_snprintf( poolname, sizeof( poolname ), "^2%s^7", mod->name );
	mod->mempool = Mem_AllocPool( poolname );

	if( version == SPRITE_VERSION_Q1 || version == SPRITE_VERSION_32 )
	{
		dsprite_q1_t *pinq1 = buffer;
		size_t size;

		if( pinq1->numframes == 0 )
		{
			Con_DPrintf( S_ERROR "%s: %s has no frames\n", __func__, mod->name );
			return;
		}

		size = sizeof( msprite_t ) + ( pinq1->numframes - 1 ) * sizeof( psprite->frames );

		psprite = Mem_Calloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinq1->type;
		psprite->texFormat = SPR_ADDITIVE;
		psprite->numframes = mod->numframes = pinq1->numframes;
		psprite->facecull = SPR_CULL_FRONT;
		psprite->radius = pinq1->boundingradius;
		psprite->synctype = pinq1->synctype;

		// LadyHavoc: hack to allow sprites to be non-fullbright
		if( Q_strchr( mod->name, '!' ))
			psprite->texFormat = SPR_ALPHTEST;

		mod->mins[0] = mod->mins[1] = -pinq1->bounds[0] * 0.5f;
		mod->maxs[0] = mod->maxs[1] = pinq1->bounds[0] * 0.5f;
		mod->mins[2] = -pinq1->bounds[1] * 0.5f;
		mod->maxs[2] = pinq1->bounds[1] * 0.5f;
	}
	else // if( version == SPRITE_VERSION_HL )
	{
		dsprite_hl_t *pinhl = buffer;
		size_t size;

		if( pinhl->numframes == 0 )
		{
			Con_DPrintf( S_WARN "%s: %s has no frames\n", __func__, mod->name );
			return;
		}

		size = sizeof( msprite_t ) + ( pinhl->numframes - 1 ) * sizeof( psprite->frames );

		psprite = Mem_Calloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinhl->type;
		psprite->texFormat = pinhl->texFormat;
		psprite->numframes = mod->numframes = pinhl->numframes;
		psprite->facecull = pinhl->facetype;
		psprite->radius = pinhl->boundingradius;
		psprite->synctype = pinhl->synctype;

		mod->mins[0] = mod->mins[1] = -pinhl->bounds[0] * 0.5f;
		mod->maxs[0] = mod->maxs[1] = pinhl->bounds[0] * 0.5f;
		mod->mins[2] = -pinhl->bounds[1] * 0.5f;
		mod->maxs[2] = pinhl->bounds[1] * 0.5f;
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
