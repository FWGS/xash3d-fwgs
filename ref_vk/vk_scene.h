#pragma once
#include "vk_const.h"

#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include "ref_params.h"

struct ref_viewpass_s;
struct cl_entity_s;

typedef struct vk_trans_entity_s {
	struct cl_entity_s *entity;
	int render_mode;
} vk_trans_entity_t;

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

// TODO should this be here?
int CL_FxBlend( struct cl_entity_s *e );
struct beam_s;
void CL_DrawBeams( int fTrans, struct beam_s *active_beams );
void CL_AddCustomBeam( struct cl_entity_s *pEnvBeam );
