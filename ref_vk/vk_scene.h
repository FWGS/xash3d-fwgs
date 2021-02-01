#pragma once
#include "vk_const.h"

#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include "ref_params.h"

struct ref_viewpass_s;
struct cl_entity_s;

typedef struct draw_list_s {
	struct cl_entity_s	*solid_entities[MAX_SCENE_ENTITIES];	// opaque moving or alpha brushes
	//cl_entity_t	*trans_entities[MAX_DRAW_ENTITIES];	// translucent brushes
	//cl_entity_t	*beam_entities[MAX_DRAW_ENTITIES];
	uint		num_solid_entities;
	//uint		num_trans_entities;
	//uint		num_beam_entities;
} draw_list_t;

void VK_SceneInit( void );
void FIXME_VK_SceneSetViewPass( const struct ref_viewpass_s *rvp );
void VK_SceneRender( void );

qboolean VK_LoadBrushModel( model_t *mod, const byte *buffer );
qboolean R_AddEntity( struct cl_entity_s *clent, int type );
void R_ProcessEntData( qboolean allocate );
void R_ClearScreen( void );
void R_ClearScene( void );
void R_PushScene( void );
void R_PopScene( void );

void R_NewMap( void );
void R_RenderScene( void );
