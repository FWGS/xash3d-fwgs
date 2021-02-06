#include "vk_sprite.h"
#include "vk_textures.h"

#include "sprite.h"
#include "xash3d_mathlib.h"
#include "com_strings.h"

#include <memory.h>

static mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe = NULL;
	float		*pintervals, fullinterval;
	int		i, numframes;
	float		targettime;

	ASSERT( pModel != NULL );
	psprite = pModel->cache.data;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		if( frame > psprite->numframes )
			gEngine.Con_Printf( S_WARN "R_GetSpriteFrame: no such frame %d (%s)\n", frame, pModel->name );
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
		fullinterval = pintervals[numframes-1];

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = gpGlobals->time - ((int)( gpGlobals->time / fullinterval )) * fullinterval;

		for( i = 0; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
		}
		pspriteframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == FRAME_ANGLED )
	{
		//int	angleframe = (int)(Q_rint(( RI.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;
		const int	angleframe = (int)(Q_rint(( 0 - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		gEngine.Con_Printf(S_WARN "VK FIXME: %s doesn't know about viewangles\n", __FUNCTION__);

		// e.g. doom-style sprite monsters
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pspriteframe = pspritegroup->frames[angleframe];
	}

	return pspriteframe;
}

void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	mspriteframe_t	*pFrame;

	if( !pSprite || pSprite->type != mod_sprite ) return; // bad model ?
	pFrame = R_GetSpriteFrame( pSprite, currentFrame, 0.0f );

	if( frameWidth ) *frameWidth = pFrame->width;
	if( frameHeight ) *frameHeight = pFrame->height;
	if( numFrames ) *numFrames = pSprite->numframes;
}

typedef struct {
	char sprite_name[MAX_QPATH];
	char group_suffix[8];
	uint r_texFlags;
	int sprite_version;
	float sprite_radius;
} SpriteLoadContext;

static const dframetype_t *VK_SpriteLoadFrame( model_t *mod, const void *pin, mspriteframe_t **ppframe, int num, const SpriteLoadContext *ctx )
{
	dspriteframe_t	pinframe;
	mspriteframe_t	*pspriteframe;
	int		gl_texturenum = 0;
	char		texname[128];
	int		bytes = 1;

	memcpy( &pinframe, pin, sizeof(dspriteframe_t));

	if( ctx->sprite_version == SPRITE_VERSION_32 )
		bytes = 4;

	// build uinque frame name
	if( FBitSet( mod->flags, MODEL_CLIENT )) // it's a HUD sprite
	{
		Q_snprintf( texname, sizeof( texname ), "#HUD/%s(%s:%i%i).spr", ctx->sprite_name, ctx->group_suffix, num / 10, num % 10 );
		gl_texturenum = VK_LoadTexture( texname, pin, pinframe.width * pinframe.height * bytes, ctx->r_texFlags );
	}
	else
	{
		Q_snprintf( texname, sizeof( texname ), "#%s(%s:%i%i).spr", ctx->sprite_name, ctx->group_suffix, num / 10, num % 10 );
		gl_texturenum = VK_LoadTexture( texname, pin, pinframe.width * pinframe.height * bytes, ctx->r_texFlags );
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

	return ( const dframetype_t* )(( const byte* )pin + sizeof( dspriteframe_t ) + pinframe.width * pinframe.height * bytes );
}

static const dframetype_t *VK_SpriteLoadGroup( model_t *mod, const void *pin, mspriteframe_t **ppframe, int framenum, const SpriteLoadContext *ctx )
{
	const dspritegroup_t	*pingroup;
	mspritegroup_t	*pspritegroup;
	const dspriteinterval_t	*pin_intervals;
	float		*poutintervals;
	int		i, groupsize, numframes;
	const void		*ptemp;

	pingroup = (const dspritegroup_t *)pin;
	numframes = pingroup->numframes;

	groupsize = sizeof( mspritegroup_t ) + (numframes - 1) * sizeof( pspritegroup->frames[0] );
	pspritegroup = Mem_Calloc( mod->mempool, groupsize );
	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;
	pin_intervals = (const dspriteinterval_t *)(pingroup + 1);
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
		ptemp = VK_SpriteLoadFrame( mod, ptemp, &pspritegroup->frames[i], framenum * 10 + i, ctx );
	}

	return (const dframetype_t *)ptemp;
}

void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags )
{
	const dsprite_t		*pin;
	const short		*numi = NULL;
	const dframetype_t	*pframetype;
	msprite_t		*psprite;
	int		i;
	SpriteLoadContext ctx = {0};

	pin = buffer;
	psprite = mod->cache.data;

	if( pin->version == SPRITE_VERSION_Q1 || pin->version == SPRITE_VERSION_32 )
		numi = NULL;
	else if( pin->version == SPRITE_VERSION_HL )
		numi = (const short *)(void *)((const byte*)buffer + sizeof( dsprite_hl_t ));

	ctx.r_texFlags = texFlags;
	ctx.sprite_version = pin->version;
	Q_strncpy( ctx.sprite_name, mod->name, sizeof( ctx.sprite_name ));
	COM_StripExtension( ctx.sprite_name );

	if( numi == NULL )
	{
		rgbdata_t	*pal;

		pal = gEngine.FS_LoadImage( "#id.pal", (byte *)&i, 768 );
		pframetype = (const dframetype_t *)(void *)((const byte*)buffer + sizeof( dsprite_q1_t )); // pinq1 + 1
		gEngine.FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else if( *numi == 256 )
	{
		const byte	*src = (const byte *)(numi+1);
		rgbdata_t	*pal;

		// install palette
		switch( psprite->texFormat )
		{
		case SPR_INDEXALPHA:
			pal = gEngine.FS_LoadImage( "#gradient.pal", src, 768 );
			break;
		case SPR_ALPHTEST:
			pal = gEngine.FS_LoadImage( "#masked.pal", src, 768 );
			break;
		default:
			pal = gEngine.FS_LoadImage( "#normal.pal", src, 768 );
			break;
		}

		pframetype = (const dframetype_t *)(void *)(src + 768);
		gEngine.FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else
	{
		gEngine.Con_DPrintf( S_ERROR "%s has wrong number of palette colors %i (should be 256)\n", mod->name, *numi );
		return;
	}

	if( mod->numframes < 1 )
		return;

	for( i = 0; i < mod->numframes; i++ )
	{
		frametype_t frametype = pframetype->type;
		psprite->frames[i].type = (spriteframetype_t)frametype;

		switch( frametype )
		{
		case FRAME_SINGLE:
			Q_strncpy( ctx.group_suffix, "frame", sizeof( ctx.group_suffix ));
			pframetype = VK_SpriteLoadFrame( mod, pframetype + 1, &psprite->frames[i].frameptr, i, &ctx );
			break;
		case FRAME_GROUP:
			Q_strncpy( ctx.group_suffix, "group", sizeof( ctx.group_suffix ));
			pframetype = VK_SpriteLoadGroup( mod, pframetype + 1, &psprite->frames[i].frameptr, i, &ctx );
			break;
		case FRAME_ANGLED:
			Q_strncpy( ctx.group_suffix, "angle", sizeof( ctx.group_suffix ));
			pframetype = VK_SpriteLoadGroup( mod, pframetype + 1, &psprite->frames[i].frameptr, i, &ctx );
			break;
		}
		if( pframetype == NULL ) break; // technically an error
	}

	if( loaded ) *loaded = true;	// done
}

int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	if( !m_pSpriteModel || m_pSpriteModel->type != mod_sprite || !m_pSpriteModel->cache.data )
		return 0;

	return R_GetSpriteFrame( m_pSpriteModel, frame, 0.0f )->gl_texturenum;
}

void Mod_LoadMapSprite( struct model_s *mod, const void *buffer, size_t size, qboolean *loaded )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
