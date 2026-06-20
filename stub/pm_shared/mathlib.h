#pragma once
#ifndef MATHLIB_H
#define MATHLIB_H

#include <math.h>
#include "xash3d_types.h"

typedef float vec_t;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_PI_F
#define M_PI_F ((float)M_PI)
#endif

extern vec3_t vec3_origin;

#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorSubtract(a,b,c) {(c)[0]=(a)[0]-(b)[0];(c)[1]=(a)[1]-(b)[1];(c)[2]=(a)[2]-(b)[2];}
#define VectorAdd(a,b,c) {(c)[0]=(a)[0]+(b)[0];(c)[1]=(a)[1]+(b)[1];(c)[2]=(a)[2]+(b)[2];}
#define VectorCopy(a,b) {(b)[0]=(a)[0];(b)[1]=(a)[1];(b)[2]=(a)[2];}
#define VectorClear(a) {(a)[0]=0.0f;(a)[1]=0.0f;(a)[2]=0.0f;}

float anglemod( float a );
void AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up );
void AngleVectorsTranspose( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up );
void AngleMatrix( const vec3_t angles, float (*matrix)[4] );
void AngleIMatrix( const vec3_t angles, float matrix[3][4] );
void NormalizeAngles( float *angles );
void InterpolateAngles( float *start, float *end, float *output, float frac );
float AngleBetweenVectors( const vec3_t v1, const vec3_t v2 );
void VectorTransform( const vec3_t in1, float in2[3][4], vec3_t out );
int VectorCompare( const vec3_t v1, const vec3_t v2 );
void VectorMA( const vec3_t veca, float scale, const vec3_t vecb, vec3_t vecc );
float Length( const vec3_t v );
float Distance( const vec3_t v1, const vec3_t v2 );
float VectorNormalize( vec3_t v );
void VectorInverse( vec3_t v );
void VectorScale( const vec3_t in, vec_t scale, vec3_t out );
int Q_log2( int val );
void VectorMatrix( vec3_t forward, vec3_t right, vec3_t up );
void VectorAngles( const vec3_t forward, vec3_t angles );

#endif /* MATHLIB_H */
