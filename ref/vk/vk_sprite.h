#pragma once

#include "vk_common.h"

void R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite );
int R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame );
void Mod_LoadMapSprite( struct model_s *mod, const void *buffer, size_t size, qboolean *loaded );
void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags );

void R_VkSpriteDrawModel( cl_entity_t *e, float blend );

qboolean R_SpriteInit(void);
