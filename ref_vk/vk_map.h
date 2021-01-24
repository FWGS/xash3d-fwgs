#pragma once

#include "xash3d_types.h"

struct ref_viewpass_s;

qboolean VK_MapInit( void );
void VK_MapShutdown( void );
void FIXME_VK_MapSetViewPass( const struct ref_viewpass_s *rvp );
void VK_MapRender( void );

void R_NewMap( void );
void R_RenderScene( void );
