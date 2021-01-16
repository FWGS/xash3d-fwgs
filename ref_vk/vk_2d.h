#pragma once

#include "xash3d_types.h"

void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty );
void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum );
void R_DrawTileClear( int texnum, int x, int y, int w, int h );
void CL_FillRGBA( float x, float y, float w, float h, int r, int g, int b, int a );
void CL_FillRGBABlend( float x, float y, float w, float h, int r, int g, int b, int a );

qboolean initVk2d( void );
void deinitVk2d( void );
void vk2dBegin( void );
void vk2dEnd( void );
