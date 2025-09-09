/*
matrixlib.c - internal matrixlib
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "port.h"
#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include "xash3d_mathlib.h"

/*
========================================================================

		Matrix3x4 operations

========================================================================
*/
void Matrix3x4_VectorTransform( const matrix3x4 in, const float v[3], float out[3] )
{
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2] + in[0][3];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2] + in[1][3];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2] + in[2][3];
}

void Matrix3x4_VectorITransform( const matrix3x4 in, const float v[3], float out[3] )
{
	vec3_t	dir;

	dir[0] = v[0] - in[0][3];
	dir[1] = v[1] - in[1][3];
	dir[2] = v[2] - in[2][3];

	out[0] = dir[0] * in[0][0] + dir[1] * in[1][0] + dir[2] * in[2][0];
	out[1] = dir[0] * in[0][1] + dir[1] * in[1][1] + dir[2] * in[2][1];
	out[2] = dir[0] * in[0][2] + dir[1] * in[1][2] + dir[2] * in[2][2];
}

void Matrix3x4_VectorRotate( const matrix3x4 in, const float v[3], float out[3] )
{
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2];
}

void Matrix3x4_VectorIRotate( const matrix3x4 in, const float v[3], float out[3] )
{
	out[0] = v[0] * in[0][0] + v[1] * in[1][0] + v[2] * in[2][0];
	out[1] = v[0] * in[0][1] + v[1] * in[1][1] + v[2] * in[2][1];
	out[2] = v[0] * in[0][2] + v[1] * in[1][2] + v[2] * in[2][2];
}

void Matrix3x4_ConcatTransforms( matrix3x4 out, const matrix3x4 in1, const matrix3x4 in2 )
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] + in1[0][2] * in2[2][3] + in1[0][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] + in1[1][2] * in2[2][3] + in1[1][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] + in1[2][2] * in2[2][3] + in1[2][3];
}

void Matrix3x4_AnglesFromMatrix( const matrix3x4 in, vec3_t out )
{
	float xyDist = sqrt( in[0][0] * in[0][0] + in[1][0] * in[1][0] );

	if( xyDist > 0.001f )
	{
		// enough here to get angles?
		out[0] = RAD2DEG( atan2( -in[2][0], xyDist ));
		out[1] = RAD2DEG( atan2( in[1][0], in[0][0] ));
		out[2] = RAD2DEG( atan2( in[2][1], in[2][2] ));
	}
	else
	{
		// forward is mostly Z, gimbal lock
		out[0] = RAD2DEG( atan2( -in[2][0], xyDist ));
		out[1] = RAD2DEG( atan2( -in[0][1], in[1][1] ));
		out[2] = 0.0f;
	}
}

void Matrix3x4_FromOriginQuat( matrix3x4 out, const vec4_t quaternion, const vec3_t origin )
{
	out[0][0] = 1.0f - 2.0f * quaternion[1] * quaternion[1] - 2.0f * quaternion[2] * quaternion[2];
	out[1][0] = 2.0f * quaternion[0] * quaternion[1] + 2.0f * quaternion[3] * quaternion[2];
	out[2][0] = 2.0f * quaternion[0] * quaternion[2] - 2.0f * quaternion[3] * quaternion[1];

	out[0][1] = 2.0f * quaternion[0] * quaternion[1] - 2.0f * quaternion[3] * quaternion[2];
	out[1][1] = 1.0f - 2.0f * quaternion[0] * quaternion[0] - 2.0f * quaternion[2] * quaternion[2];
	out[2][1] = 2.0f * quaternion[1] * quaternion[2] + 2.0f * quaternion[3] * quaternion[0];

	out[0][2] = 2.0f * quaternion[0] * quaternion[2] + 2.0f * quaternion[3] * quaternion[1];
	out[1][2] = 2.0f * quaternion[1] * quaternion[2] - 2.0f * quaternion[3] * quaternion[0];
	out[2][2] = 1.0f - 2.0f * quaternion[0] * quaternion[0] - 2.0f * quaternion[1] * quaternion[1];

	out[0][3] = origin[0];
	out[1][3] = origin[1];
	out[2][3] = origin[2];
}

void Matrix3x4_CreateFromEntity( matrix3x4 out, const vec3_t angles, const vec3_t origin, float scale )
{
	float	angle, sr, sp, sy, cr, cp, cy;

	if( angles[ROLL] )
	{
		angle = angles[YAW] * (M_PI2 / 360.0f);
		SinCos( angle, &sy, &cy );
		angle = angles[PITCH] * (M_PI2 / 360.0f);
		SinCos( angle, &sp, &cp );
		angle = angles[ROLL] * (M_PI2 / 360.0f);
		SinCos( angle, &sr, &cr );

		out[0][0] = (cp*cy) * scale;
		out[0][1] = (sr*sp*cy+cr*-sy) * scale;
		out[0][2] = (cr*sp*cy+-sr*-sy) * scale;
		out[0][3] = origin[0];
		out[1][0] = (cp*sy) * scale;
		out[1][1] = (sr*sp*sy+cr*cy) * scale;
		out[1][2] = (cr*sp*sy+-sr*cy) * scale;
		out[1][3] = origin[1];
		out[2][0] = (-sp) * scale;
		out[2][1] = (sr*cp) * scale;
		out[2][2] = (cr*cp) * scale;
		out[2][3] = origin[2];
	}
	else if( angles[PITCH] )
	{
		angle = angles[YAW] * (M_PI2 / 360.0f);
		SinCos( angle, &sy, &cy );
		angle = angles[PITCH] * (M_PI2 / 360.0f);
		SinCos( angle, &sp, &cp );

		out[0][0] = (cp*cy) * scale;
		out[0][1] = (-sy) * scale;
		out[0][2] = (sp*cy) * scale;
		out[0][3] = origin[0];
		out[1][0] = (cp*sy) * scale;
		out[1][1] = (cy) * scale;
		out[1][2] = (sp*sy) * scale;
		out[1][3] = origin[1];
		out[2][0] = (-sp) * scale;
		out[2][1] = 0.0f;
		out[2][2] = (cp) * scale;
		out[2][3] = origin[2];
	}
	else if( angles[YAW] )
	{
		angle = angles[YAW] * (M_PI2 / 360.0f);
		SinCos( angle, &sy, &cy );

		out[0][0] = (cy) * scale;
		out[0][1] = (-sy) * scale;
		out[0][2] = 0.0f;
		out[0][3] = origin[0];
		out[1][0] = (sy) * scale;
		out[1][1] = (cy) * scale;
		out[1][2] = 0.0f;
		out[1][3] = origin[1];
		out[2][0] = 0.0f;
		out[2][1] = 0.0f;
		out[2][2] = scale;
		out[2][3] = origin[2];
	}
	else
	{
		out[0][0] = scale;
		out[0][1] = 0.0f;
		out[0][2] = 0.0f;
		out[0][3] = origin[0];
		out[1][0] = 0.0f;
		out[1][1] = scale;
		out[1][2] = 0.0f;
		out[1][3] = origin[1];
		out[2][0] = 0.0f;
		out[2][1] = 0.0f;
		out[2][2] = scale;
		out[2][3] = origin[2];
	}
}

/*
==================
Matrix3x4_TransformAABB
==================
*/
void Matrix3x4_TransformAABB( const matrix3x4 world, const vec3_t mins, const vec3_t maxs, vec3_t absmin, vec3_t absmax )
{
	vec3_t	localCenter, localExtents;
	vec3_t	worldCenter, worldExtents;

	VectorAverage( mins, maxs, localCenter );
	VectorSubtract( maxs, localCenter, localExtents );

	Matrix3x4_VectorTransform( world, localCenter, worldCenter );
	worldExtents[0] = DotProductFabs( localExtents, world[0] );	// auto-transposed!
	worldExtents[1] = DotProductFabs( localExtents, world[1] );
	worldExtents[2] = DotProductFabs( localExtents, world[2] );

	VectorSubtract( worldCenter, worldExtents, absmin );
	VectorAdd( worldCenter, worldExtents, absmax );
}

/*
========================================================================

		Matrix4x4 operations

========================================================================
*/
void Matrix4x4_VectorTransform( const matrix4x4 in, const float v[3], float out[3] )
{
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2] + in[0][3];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2] + in[1][3];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2] + in[2][3];
}

void Matrix4x4_VectorITransform( const matrix4x4 in, const float v[3], float out[3] )
{
	vec3_t	dir;

	dir[0] = v[0] - in[0][3];
	dir[1] = v[1] - in[1][3];
	dir[2] = v[2] - in[2][3];

	out[0] = dir[0] * in[0][0] + dir[1] * in[1][0] + dir[2] * in[2][0];
	out[1] = dir[0] * in[0][1] + dir[1] * in[1][1] + dir[2] * in[2][1];
	out[2] = dir[0] * in[0][2] + dir[1] * in[1][2] + dir[2] * in[2][2];
}

void Matrix4x4_VectorRotate( const matrix4x4 in, const float v[3], float out[3] )
{
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2];
}

void Matrix4x4_VectorIRotate( const matrix4x4 in, const float v[3], float out[3] )
{
	out[0] = v[0] * in[0][0] + v[1] * in[1][0] + v[2] * in[2][0];
	out[1] = v[0] * in[0][1] + v[1] * in[1][1] + v[2] * in[2][1];
	out[2] = v[0] * in[0][2] + v[1] * in[1][2] + v[2] * in[2][2];
}

void Matrix4x4_ConcatTransforms( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 )
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] + in1[0][2] * in2[2][3] + in1[0][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] + in1[1][2] * in2[2][3] + in1[1][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] + in1[2][2] * in2[2][3] + in1[2][3];
}

void Matrix4x4_CreateFromEntity( matrix4x4 out, const vec3_t angles, const vec3_t origin, float scale )
{
	float	angle, sr, sp, sy, cr, cp, cy;

	if( angles[ROLL] )
	{
		angle = angles[YAW] * (M_PI2 / 360.0f);
		SinCos( angle, &sy, &cy );
		angle = angles[PITCH] * (M_PI2 / 360.0f);
		SinCos( angle, &sp, &cp );
		angle = angles[ROLL] * (M_PI2 / 360.0f);
		SinCos( angle, &sr, &cr );

		out[0][0] = (cp*cy) * scale;
		out[0][1] = (sr*sp*cy+cr*-sy) * scale;
		out[0][2] = (cr*sp*cy+-sr*-sy) * scale;
		out[0][3] = origin[0];
		out[1][0] = (cp*sy) * scale;
		out[1][1] = (sr*sp*sy+cr*cy) * scale;
		out[1][2] = (cr*sp*sy+-sr*cy) * scale;
		out[1][3] = origin[1];
		out[2][0] = (-sp) * scale;
		out[2][1] = (sr*cp) * scale;
		out[2][2] = (cr*cp) * scale;
		out[2][3] = origin[2];
		out[3][0] = 0.0f;
		out[3][1] = 0.0f;
		out[3][2] = 0.0f;
		out[3][3] = 1.0f;
	}
	else if( angles[PITCH] )
	{
		angle = angles[YAW] * (M_PI2 / 360.0f);
		SinCos( angle, &sy, &cy );
		angle = angles[PITCH] * (M_PI2 / 360.0f);
		SinCos( angle, &sp, &cp );

		out[0][0] = (cp*cy) * scale;
		out[0][1] = (-sy) * scale;
		out[0][2] = (sp*cy) * scale;
		out[0][3] = origin[0];
		out[1][0] = (cp*sy) * scale;
		out[1][1] = (cy) * scale;
		out[1][2] = (sp*sy) * scale;
		out[1][3] = origin[1];
		out[2][0] = (-sp) * scale;
		out[2][1] = 0.0f;
		out[2][2] = (cp) * scale;
		out[2][3] = origin[2];
		out[3][0] = 0.0f;
		out[3][1] = 0.0f;
		out[3][2] = 0.0f;
		out[3][3] = 1.0f;
	}
	else if( angles[YAW] )
	{
		angle = angles[YAW] * (M_PI2 / 360.0f);
		SinCos( angle, &sy, &cy );

		out[0][0] = (cy) * scale;
		out[0][1] = (-sy) * scale;
		out[0][2] = 0.0f;
		out[0][3] = origin[0];
		out[1][0] = (sy) * scale;
		out[1][1] = (cy) * scale;
		out[1][2] = 0.0f;
		out[1][3] = origin[1];
		out[2][0] = 0.0f;
		out[2][1] = 0.0f;
		out[2][2] = scale;
		out[2][3] = origin[2];
		out[3][0] = 0.0f;
		out[3][1] = 0.0f;
		out[3][2] = 0.0f;
		out[3][3] = 1.0f;
	}
	else
	{
		out[0][0] = scale;
		out[0][1] = 0.0f;
		out[0][2] = 0.0f;
		out[0][3] = origin[0];
		out[1][0] = 0.0f;
		out[1][1] = scale;
		out[1][2] = 0.0f;
		out[1][3] = origin[1];
		out[2][0] = 0.0f;
		out[2][1] = 0.0f;
		out[2][2] = scale;
		out[2][3] = origin[2];
		out[3][0] = 0.0f;
		out[3][1] = 0.0f;
		out[3][2] = 0.0f;
		out[3][3] = 1.0f;
	}
}

void Matrix4x4_ConvertToEntity( const matrix4x4 in, vec3_t angles, vec3_t origin )
{
	float xyDist = sqrt( in[0][0] * in[0][0] + in[1][0] * in[1][0] );

	// enough here to get angles?
	if( xyDist > 0.001f )
	{
		angles[0] = RAD2DEG( atan2( -in[2][0], xyDist ));
		angles[1] = RAD2DEG( atan2( in[1][0], in[0][0] ));
		angles[2] = RAD2DEG( atan2( in[2][1], in[2][2] ));
	}
	else	// forward is mostly Z, gimbal lock
	{
		angles[0] = RAD2DEG( atan2( -in[2][0], xyDist ));
		angles[1] = RAD2DEG( atan2( -in[0][1], in[1][1] ));
		angles[2] = 0.0f;
	}

	origin[0] = in[0][3];
	origin[1] = in[1][3];
	origin[2] = in[2][3];
}

void Matrix4x4_TransformPositivePlane( const matrix4x4 in, const vec3_t normal, float d, vec3_t out, float *dist )
{
	float	scale = sqrt( in[0][0] * in[0][0] + in[0][1] * in[0][1] + in[0][2] * in[0][2] );
	float	iscale = 1.0f / scale;

	out[0] = (normal[0] * in[0][0] + normal[1] * in[0][1] + normal[2] * in[0][2]) * iscale;
	out[1] = (normal[0] * in[1][0] + normal[1] * in[1][1] + normal[2] * in[1][2]) * iscale;
	out[2] = (normal[0] * in[2][0] + normal[1] * in[2][1] + normal[2] * in[2][2]) * iscale;
	*dist = d * scale + ( out[0] * in[0][3] + out[1] * in[1][3] + out[2] * in[2][3] );
}

void Matrix4x4_Invert_Simple( matrix4x4 out, const matrix4x4 in1 )
{
	// we only support uniform scaling, so assume the first row is enough
	// (note the lack of sqrt here, because we're trying to undo the scaling,
	// this means multiplying by the inverse scale twice - squaring it, which
	// makes the sqrt a waste of time)
	float	scale = 1.0f / (in1[0][0] * in1[0][0] + in1[0][1] * in1[0][1] + in1[0][2] * in1[0][2]);

	// invert the rotation by transposing and multiplying by the squared
	// recipricol of the input matrix scale as described above
	out[0][0] = in1[0][0] * scale;
	out[0][1] = in1[1][0] * scale;
	out[0][2] = in1[2][0] * scale;
	out[1][0] = in1[0][1] * scale;
	out[1][1] = in1[1][1] * scale;
	out[1][2] = in1[2][1] * scale;
	out[2][0] = in1[0][2] * scale;
	out[2][1] = in1[1][2] * scale;
	out[2][2] = in1[2][2] * scale;

	// invert the translate
	out[0][3] = -(in1[0][3] * out[0][0] + in1[1][3] * out[0][1] + in1[2][3] * out[0][2]);
	out[1][3] = -(in1[0][3] * out[1][0] + in1[1][3] * out[1][1] + in1[2][3] * out[1][2]);
	out[2][3] = -(in1[0][3] * out[2][0] + in1[1][3] * out[2][1] + in1[2][3] * out[2][2]);

	// don't know if there's anything worth doing here
	out[3][0] = 0.0f;
	out[3][1] = 0.0f;
	out[3][2] = 0.0f;
	out[3][3] = 1.0f;
}

qboolean Matrix4x4_Invert_Full( matrix4x4 out, const matrix4x4 in1 )
{
	float	*temp;
	float	*r[4];
	float	rtemp[4][8];
	float	m[4];
	float	s;

	r[0] = rtemp[0];
	r[1] = rtemp[1];
	r[2] = rtemp[2];
	r[3] = rtemp[3];

	r[0][0] = in1[0][0];
	r[0][1] = in1[0][1];
	r[0][2] = in1[0][2];
	r[0][3] = in1[0][3];
	r[0][4] = 1.0f;
	r[0][5] =	0.0f;
	r[0][6] =	0.0f;
	r[0][7] = 0.0f;

	r[1][0] = in1[1][0];
	r[1][1] = in1[1][1];
	r[1][2] = in1[1][2];
	r[1][3] = in1[1][3];
	r[1][5] = 1.0f;
	r[1][4] =	0.0f;
	r[1][6] =	0.0f;
	r[1][7] = 0.0f;

	r[2][0] = in1[2][0];
	r[2][1] = in1[2][1];
	r[2][2] = in1[2][2];
	r[2][3] = in1[2][3];
	r[2][6] = 1.0f;
	r[2][4] =	0.0f;
	r[2][5] =	0.0f;
	r[2][7] = 0.0f;

	r[3][0] = in1[3][0];
	r[3][1] = in1[3][1];
	r[3][2] = in1[3][2];
	r[3][3] = in1[3][3];
	r[3][4] =	0.0f;
	r[3][5] = 0.0f;
	r[3][6] = 0.0f;
	r[3][7] = 1.0f;

	if( fabs( r[3][0] ) > fabs( r[2][0] ))
	{
		temp = r[3];
		r[3] = r[2];
		r[2] = temp;
	}

	if( fabs( r[2][0] ) > fabs( r[1][0] ))
	{
		temp = r[2];
		r[2] = r[1];
		r[1] = temp;
	}

	if( fabs( r[1][0] ) > fabs( r[0][0] ))
	{
		temp = r[1];
		r[1] = r[0];
		r[0] = temp;
	}

	if( r[0][0] )
	{
		m[1] = r[1][0] / r[0][0];
		m[2] = r[2][0] / r[0][0];
		m[3] = r[3][0] / r[0][0];

		s = r[0][1];
		r[1][1] -= m[1] * s;
		r[2][1] -= m[2] * s;
		r[3][1] -= m[3] * s;

		s = r[0][2];
		r[1][2] -= m[1] * s;
		r[2][2] -= m[2] * s;
		r[3][2] -= m[3] * s;

		s = r[0][3];
		r[1][3] -= m[1] * s;
		r[2][3] -= m[2] * s;
		r[3][3] -= m[3] * s;

		s = r[0][4];
		if( s )
		{
			r[1][4] -= m[1] * s;
			r[2][4] -= m[2] * s;
			r[3][4] -= m[3] * s;
		}

		s = r[0][5];
		if( s )
		{
			r[1][5] -= m[1] * s;
			r[2][5] -= m[2] * s;
			r[3][5] -= m[3] * s;
		}

		s = r[0][6];
		if( s )
		{
			r[1][6] -= m[1] * s;
			r[2][6] -= m[2] * s;
			r[3][6] -= m[3] * s;
		}

		s = r[0][7];
		if( s )
		{
			r[1][7] -= m[1] * s;
			r[2][7] -= m[2] * s;
			r[3][7] -= m[3] * s;
		}

		if( fabs( r[3][1] ) > fabs( r[2][1] ))
		{
			temp = r[3];
			r[3] = r[2];
			r[2] = temp;
		}

		if( fabs( r[2][1] ) > fabs( r[1][1] ))
		{
			temp = r[2];
			r[2] = r[1];
			r[1] = temp;
		}

		if( r[1][1] )
		{
			m[2] = r[2][1] / r[1][1];
			m[3] = r[3][1] / r[1][1];
			r[2][2] -= m[2] * r[1][2];
			r[3][2] -= m[3] * r[1][2];
			r[2][3] -= m[2] * r[1][3];
			r[3][3] -= m[3] * r[1][3];

			s = r[1][4];
			if( s )
			{
				r[2][4] -= m[2] * s;
				r[3][4] -= m[3] * s;
			}

			s = r[1][5];
			if( s )
			{
				r[2][5] -= m[2] * s;
				r[3][5] -= m[3] * s;
			}

			s = r[1][6];
			if( s )
			{
				r[2][6] -= m[2] * s;
				r[3][6] -= m[3] * s;
			}

			s = r[1][7];
			if( s )
			{
				r[2][7] -= m[2] * s;
				r[3][7] -= m[3] * s;
			}

			if( fabs( r[3][2] ) > fabs( r[2][2] ))
			{
				temp = r[3];
				r[3] = r[2];
				r[2] = temp;
			}

			if( r[2][2] )
			{
				m[3] = r[3][2] / r[2][2];
				r[3][3] -= m[3] * r[2][3];
				r[3][4] -= m[3] * r[2][4];
				r[3][5] -= m[3] * r[2][5];
				r[3][6] -= m[3] * r[2][6];
				r[3][7] -= m[3] * r[2][7];

				if( r[3][3] )
				{
					s = 1.0f / r[3][3];
					r[3][4] *= s;
					r[3][5] *= s;
					r[3][6] *= s;
					r[3][7] *= s;

					m[2] = r[2][3];
					s = 1.0f / r[2][2];
					r[2][4] = s * (r[2][4] - r[3][4] * m[2]);
					r[2][5] = s * (r[2][5] - r[3][5] * m[2]);
					r[2][6] = s * (r[2][6] - r[3][6] * m[2]);
					r[2][7] = s * (r[2][7] - r[3][7] * m[2]);

					m[1] = r[1][3];
					r[1][4] -= r[3][4] * m[1];
					r[1][5] -= r[3][5] * m[1];
					r[1][6] -= r[3][6] * m[1];
					r[1][7] -= r[3][7] * m[1];

					m[0] = r[0][3];
					r[0][4] -= r[3][4] * m[0];
					r[0][5] -= r[3][5] * m[0];
					r[0][6] -= r[3][6] * m[0];
					r[0][7] -= r[3][7] * m[0];

					m[1] = r[1][2];
					s = 1.0f / r[1][1];
					r[1][4] = s * (r[1][4] - r[2][4] * m[1]);
					r[1][5] = s * (r[1][5] - r[2][5] * m[1]);
					r[1][6] = s * (r[1][6] - r[2][6] * m[1]);
					r[1][7] = s * (r[1][7] - r[2][7] * m[1]);

					m[0] = r[0][2];
					r[0][4] -= r[2][4] * m[0];
					r[0][5] -= r[2][5] * m[0];
					r[0][6] -= r[2][6] * m[0];
					r[0][7] -= r[2][7] * m[0];

					m[0] = r[0][1];
					s = 1.0f / r[0][0];
					r[0][4] = s * (r[0][4] - r[1][4] * m[0]);
					r[0][5] = s * (r[0][5] - r[1][5] * m[0]);
					r[0][6] = s * (r[0][6] - r[1][6] * m[0]);
					r[0][7] = s * (r[0][7] - r[1][7] * m[0]);

					out[0][0]	= r[0][4];
					out[0][1]	= r[0][5];
					out[0][2]	= r[0][6];
					out[0][3]	= r[0][7];
					out[1][0]	= r[1][4];
					out[1][1]	= r[1][5];
					out[1][2]	= r[1][6];
					out[1][3]	= r[1][7];
					out[2][0]	= r[2][4];
					out[2][1]	= r[2][5];
					out[2][2]	= r[2][6];
					out[2][3]	= r[2][7];
					out[3][0]	= r[3][4];
					out[3][1]	= r[3][5];
					out[3][2]	= r[3][6];
					out[3][3]	= r[3][7];

					return true;
				}
			}
		}
	}
	return false;
}
