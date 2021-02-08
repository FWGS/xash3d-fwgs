#pragma once

#include "vk_common.h"

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;

void VK_StudioInit( void );
void VK_StudioShutdown( void );
void VK_StudioRender( const struct ref_viewpass_s *rvp, struct draw_list_s *draw_list );

void Mod_LoadStudioModel( model_t *mod, const void *buffer, qboolean *loaded );
void Mod_StudioLoadTextures( model_t *mod, void *data );
