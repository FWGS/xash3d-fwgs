/*
cl_sprite.c - sprite loading
Copyright (C) 2010 Uncle Mike

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
#include "sprite.h"
#include "ref_common.h"

static char  sprite_name[MAX_QPATH];
static char  group_suffix[8];
static uint  r_texFlags = 0;
static int   sprite_version;

/*
================
R_GetSpriteFrame

assume pModel is valid
================
*/
mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw )
{
	msprite_t      *psprite;
	mspritegroup_t *pspritegroup;
	mspriteframe_t *pspriteframe = NULL;
	float          *pintervals, fullinterval;
	int             i, numframes;
	float           targettime;

	if( !pModel )
		return NULL;

	psprite = pModel->cache.data;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		if( frame > psprite->numframes )
			Con_Printf( S_WARN "%s: no such frame %d (%s)\n", __func__, frame, pModel->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == SPR_SINGLE )
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == SPR_GROUP )
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes - 1];

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = cl.time - ((int)( cl.time / fullinterval )) * fullinterval;

		for( i = 0; i < ( numframes - 1 ); i++ )
		{
			if( pintervals[i] > targettime )
				break;
		}
		pspriteframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == FRAME_ANGLED )
	{
		int angleframe = (int)( Q_rint(( refState.viewangles[1] - yaw + 45.0f ) / 360 * 8 ) - 4 ) & 7;

		// e.g. doom-style sprite monsters
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pspriteframe = pspritegroup->frames[angleframe];
	}

	return pspriteframe;
}

void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	mspriteframe_t *pFrame;

	if( !pSprite || pSprite->type != mod_sprite )
		return;

	pFrame = R_GetSpriteFrame( pSprite, currentFrame, 0.0f );

	if( frameWidth )
		*frameWidth = pFrame->width;
	if( frameHeight )
		*frameHeight = pFrame->height;
	if( numFrames )
		*numFrames = pSprite->numframes;
}

int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	if( !m_pSpriteModel || m_pSpriteModel->type != mod_sprite || !m_pSpriteModel->cache.data )
		return 0;

	return R_GetSpriteFrame( m_pSpriteModel, frame, 0.0f )->gl_texturenum;
}

static void Mod_SpriteTextureReplacementReport( const char *modelname, int gl_texturenum, const char *foundpath )
{
	if( host_allow_materials.value != 2.0f )
		return;

	if( gl_texturenum > 0 )
		Con_Printf( "Looking for %s tex replacement..." S_GREEN "OK (%s)\n", modelname, foundpath );
	else if( gl_texturenum < 0 )
		Con_Printf( "Looking for %s tex replacement..." S_YELLOW "MISS (%s)\n", modelname, foundpath );
	else
		Con_Printf( "Looking for %s tex replacement..." S_RED "FAIL (%s)\n", modelname, foundpath );
}

static qboolean Mod_SpriteSearchForTextureReplacement( char *out, size_t size, const char *modelname, const char *fmt, ... )
{
	va_list ap;
	int ret;

	va_start( ap, fmt );
	ret = Q_vsnprintf( out, size, fmt, ap );
	va_end( ap );

	if( ret < 0 )
	{
		Mod_SpriteTextureReplacementReport( modelname, -1, "overflow" );
		return false;
	}

	if( FS_FileExists( out, false ))
		return true;

	Mod_SpriteTextureReplacementReport( modelname, -1, out );
	return false;
}

static const byte *Mod_SpriteLoadFrame( model_t *mod, const void *pin, mspriteframe_t **ppframe, int num )
{
	dspriteframe_t pinframe;
	mspriteframe_t *pspriteframe;
	int gl_texturenum = 0;
	char texname[128];
	int bytes = 1;

	memcpy( &pinframe, pin, sizeof( dspriteframe_t ));

	if( sprite_version == SPRITE_VERSION_32 )
		bytes = 4;

	// build unique frame name
	if( FBitSet( mod->flags, MODEL_CLIENT )) // it's a HUD sprite
	{
		Q_snprintf( texname, sizeof( texname ), "#HUD/%s(%s:%i%i).spr", sprite_name, group_suffix, num / 10, num % 10 );
		gl_texturenum = ref.dllFuncs.GL_LoadTexture( texname, pin, pinframe.width * pinframe.height * bytes, r_texFlags );
	}
	else
	{
		// partial HD-textures support
		if( Mod_AllowMaterials( ))
		{
			if( Mod_SpriteSearchForTextureReplacement( texname, sizeof( texname ), sprite_name, "materials/%s/%s%i%i.tga", sprite_name, group_suffix, num / 10, num % 10 ))
			{
				gl_texturenum = ref.dllFuncs.GL_LoadTexture( texname, NULL, 0, r_texFlags );
				Mod_SpriteTextureReplacementReport( sprite_name, gl_texturenum, texname );
			}
		}

		if( gl_texturenum == 0 )
		{
			Q_snprintf( texname, sizeof( texname ), "#%s(%s:%i%i).spr", sprite_name, group_suffix, num / 10, num % 10 );
			gl_texturenum = ref.dllFuncs.GL_LoadTexture( texname, pin, pinframe.width * pinframe.height * bytes, r_texFlags );
		}
	}

	// setup frame description
	pspriteframe = Mem_Malloc( mod->mempool, sizeof( mspriteframe_t ));
	pspriteframe->width = pinframe.width;
	pspriteframe->height = pinframe.height;
	pspriteframe->up = pinframe.origin[1];
	pspriteframe->left = pinframe.origin[0];
	pspriteframe->down = pinframe.origin[1] - pinframe.height;
	pspriteframe->right = pinframe.width + pinframe.origin[0];
	pspriteframe->gl_texturenum = gl_texturenum;
	*ppframe = pspriteframe;

	return (const byte *)pin + sizeof( dspriteframe_t ) + pinframe.width * pinframe.height * bytes;
}

static const byte *Mod_SpriteLoadGroup( model_t *mod, const void *pin, mspriteframe_t **ppframe, int framenum )
{
	const dspritegroup_t    *pingroup;
	mspritegroup_t          *pspritegroup;
	const dspriteinterval_t *pin_intervals;
	float      *poutintervals;
	int         i, groupsize, numframes;
	const void *ptemp;

	pingroup = (const dspritegroup_t *)pin;
	numframes = pingroup->numframes;

	groupsize = sizeof( mspritegroup_t ) + ( numframes - 1 ) * sizeof( pspritegroup->frames[0] );
	pspritegroup = Mem_Calloc( mod->mempool, groupsize );
	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;
	pin_intervals = (const dspriteinterval_t *)( pingroup + 1 );
	poutintervals = Mem_Calloc( mod->mempool, numframes * sizeof( float ));
	pspritegroup->intervals = poutintervals;

	for( i = 0; i < numframes; i++ )
	{
		*poutintervals = pin_intervals->interval;
		if( *poutintervals <= 0.0f )
			*poutintervals = 1.0f; // set error value
		poutintervals++;
		pin_intervals++;
	}

	ptemp = (const void *)pin_intervals;
	for( i = 0; i < numframes; i++ )
	{
		ptemp = Mod_SpriteLoadFrame( mod, ptemp, &pspritegroup->frames[i], framenum * 10 + i );
	}

	return ptemp;
}

void Mod_SpriteLoadTextures( model_t *mod, const void *buffer )
{
	const dsprite_t *pin = buffer;
	const short     *numi = NULL;
	const byte      *pframetype;
	msprite_t       *psprite = mod->cache.data;
	int i;

	if( pin->version == SPRITE_VERSION_Q1 || pin->version == SPRITE_VERSION_32 )
		numi = NULL;
	else if( pin->version == SPRITE_VERSION_HL )
		numi = (const short *)((const byte *)buffer + sizeof( dsprite_hl_t ));

	r_texFlags = mod->numtexinfo;
	sprite_version = pin->version;
	Q_strncpy( sprite_name, mod->name, sizeof( sprite_name ));
	COM_StripExtension( sprite_name );

	if( numi == NULL )
	{
		rgbdata_t *pal;

		pal = FS_LoadImage( "#id.pal", (byte *)&i, 768 );
		pframetype = (const byte *)buffer + sizeof( dsprite_q1_t );
		FS_FreeImage( pal );
	}
	else if( *numi <= 256 )
	{
		const byte *src = (const byte *)( numi + 1 );
		rgbdata_t  *pal;
		size_t      pal_bytes = *numi * 3;

		// install palette
		switch( psprite->texFormat )
		{
		case SPR_INDEXALPHA:
			pal = FS_LoadImage( "#gradient.pal", src, pal_bytes );
			break;
		case SPR_ALPHTEST:
			pal = FS_LoadImage( "#masked.pal", src, pal_bytes );
			break;
		default:
			pal = FS_LoadImage( "#texgamma.pal", src, pal_bytes );
			break;
		}

		pframetype = (const byte *)( src + pal_bytes );
		FS_FreeImage( pal );
	}
	else
	{
		Con_DPrintf( S_ERROR "%s has wrong number of palette colors %i (should be less or equal than 256)\n", mod->name, *numi );
		return;
	}

	if( mod->numframes < 1 )
		return;

	for( i = 0; i < mod->numframes; i++ )
	{
		frametype_t  frametype;
		dframetype_t dframetype;

		memcpy( &dframetype, pframetype, sizeof( dframetype ));
		frametype = dframetype.type;
		psprite->frames[i].type = (spriteframetype_t)frametype;

		switch( frametype )
		{
		case FRAME_SINGLE:
			Q_strncpy( group_suffix, "frame", sizeof( group_suffix ));
			pframetype = Mod_SpriteLoadFrame( mod, pframetype + sizeof( dframetype_t ), &psprite->frames[i].frameptr, i );
			break;
		case FRAME_GROUP:
			Q_strncpy( group_suffix, "group", sizeof( group_suffix ));
			pframetype = Mod_SpriteLoadGroup( mod, pframetype + sizeof( dframetype_t ), &psprite->frames[i].frameptr, i );
			break;
		case FRAME_ANGLED:
			Q_strncpy( group_suffix, "angle", sizeof( group_suffix ));
			pframetype = Mod_SpriteLoadGroup( mod, pframetype + sizeof( dframetype_t ), &psprite->frames[i].frameptr, i );
			break;
		}
		if( pframetype == NULL )
			break;
	}
}

void Mod_SpriteUnloadTextures( void *data )
{
	msprite_t *psprite = data;
	int i;

	if( !data )
		return;

	// release all textures
	for( i = 0; i < psprite->numframes; i++ )
	{
		if( !psprite->frames[i].frameptr )
			continue;

		if( psprite->frames[i].type == SPR_SINGLE )
		{
			ref.dllFuncs.GL_FreeTexture( psprite->frames[i].frameptr->gl_texturenum );
		}
		else
		{
			mspritegroup_t *pspritegroup = (mspritegroup_t *)psprite->frames[i].frameptr;
			int j;

			for( j = 0; j < pspritegroup->numframes; j++ )
			{
				if( pspritegroup->frames[j] )
					ref.dllFuncs.GL_FreeTexture( pspritegroup->frames[j]->gl_texturenum );
			}
		}
	}
}
