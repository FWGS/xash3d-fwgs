#pragma once

#include "vk_common.h"

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;

void VK_StudioInit( void );
void VK_StudioShutdown( void );

void Mod_LoadStudioModel( model_t *mod, const void *buffer, qboolean *loaded );
void Mod_StudioLoadTextures( model_t *mod, void *data );

void VK_StudioDrawModel( cl_entity_t *ent, int render_mode, int ubo_index );
