#pragma once

#include "xash3d_types.h"

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;

qboolean VK_BrushInit( void );
void VK_BrushShutdown( void );
qboolean VK_LoadBrushModel( struct model_s *mod, const byte *buffer );
void VK_BrushRender( const struct ref_viewpass_s *rvp, struct draw_list_s *draw_list );
void VK_BrushClear( void );
