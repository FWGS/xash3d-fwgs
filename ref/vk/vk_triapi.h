#pragma once

#include "xash3d_types.h"

void TriBegin( int mode );

void TriTexCoord2f( float u, float v );
void TriColor4f( float r, float g, float b, float a );
//void TriColor4ub( byte r, byte g, byte b, byte a );

// Emits next vertex
void TriVertex3fv( const float *v );
void TriVertex3f( float x, float y, float z );

void TriEnd( void );
