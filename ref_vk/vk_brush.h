#pragma once

#include "xash3d_types.h"
#include "vk_render.h"

struct ref_viewpass_s;
struct draw_list_s;
struct model_s;
struct cl_entity_s;

typedef struct vk_brush_model_s {
	vk_render_model_t render_model;
	int num_water_surfaces;
} vk_brush_model_t;

qboolean VK_BrushInit( void );
void VK_BrushShutdown( void );

qboolean VK_BrushModelLoad( struct model_s *mod, qboolean map);
void VK_BrushModelDestroy( struct model_s *mod );

void VK_BrushModelDraw( const struct cl_entity_s *ent, int render_mode );
void VK_BrushStatsClear( void );
