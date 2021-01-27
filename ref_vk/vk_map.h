#pragma once

#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"

struct ref_viewpass_s;

qboolean VK_MapInit( void );
void VK_MapShutdown( void );
void FIXME_VK_MapSetViewPass( const struct ref_viewpass_s *rvp );
void VK_MapRender( void );

qboolean VK_LoadBrushModel( model_t *mod, const byte *buffer );
qboolean R_AddEntity( struct cl_entity_s *clent, int type );
void R_ProcessEntData( qboolean allocate );
void R_ClearScreen( void );
void R_PushScene( void );
void R_PopScene( void );

void R_NewMap( void );
void R_RenderScene( void );
