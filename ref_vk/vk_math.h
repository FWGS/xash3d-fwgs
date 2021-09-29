#pragma once

#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include <string.h>
#include "xash3d_mathlib.h"

void Matrix4x4_ToArrayFloatGL( const matrix4x4 in, float out[16] );
void Matrix4x4_FromArrayFloatGL( matrix4x4 out, const float in[16] );
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_ConcatScale( matrix4x4 out, float x );
void Matrix4x4_ConcatScale3( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateScale( matrix4x4 out, float x );
void Matrix4x4_CreateScale3( matrix4x4 out, float x, float y, float z );
void Matrix4x4_CreateProjection(matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar);
void Matrix4x4_CreateOrtho(matrix4x4 m, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar);
void Matrix4x4_CreateModelview( matrix4x4 out );
