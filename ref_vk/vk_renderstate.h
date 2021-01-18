#pragma once

#include "xash3d_types.h"

void GL_SetRenderMode( int renderMode );
void TriColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
void R_AllowFog( qboolean allow );
void R_Set2DMode( qboolean enable );
void R_BeginFrame( qboolean clearScene );
void R_RenderScene( void );
void R_EndFrame( void );
