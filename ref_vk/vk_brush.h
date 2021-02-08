#pragma once

#include "xash3d_types.h"

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;

qboolean VK_BrushInit( void );
void VK_BrushShutdown( void );
qboolean VK_LoadBrushModel( struct model_s *mod, const byte *buffer );
qboolean VK_BrushRenderBegin( void );
void VK_BrushDrawModel( const struct model_s *mod, int render_mode, int ubo_index );
void VK_BrushClear( void );
