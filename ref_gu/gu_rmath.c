/*
gl_rmath.c - renderer mathlib
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

#include "gu_local.h"
#include "xash3d_mathlib.h"

/*
========================================================================

	       Matrix4x4 operations (private to renderer)

========================================================================
*/
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 )
{
#if 1
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in1[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in1[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in1[2]
		"lv.q		C130, 48 + %1\n"		// C130 = in1[3]
		"lv.q		C200,  0 + %2\n"		// C200 = in2[0]
		"lv.q		C210, 16 + %2\n"		// C210 = in2[1]
		"lv.q		C220, 32 + %2\n"		// C220 = in2[2]
		"lv.q		C230, 48 + %2\n"		// C230 = in2[3]
		"vmmul.q	E000, E100, E200\n"		// E000 = E100 * E200
		"sv.q		C000,  0 + %0\n"		// out[0] = C000
		"sv.q		C010, 16 + %0\n"		// out[1] = C010
		"sv.q		C020, 32 + %0\n"		// out[2] = C020
		"sv.q		C030, 48 + %0\n"		// out[3] = C030
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in1 ), "m"( *in2 )
	);
#else
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0] + in1[0][3] * in2[3][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1] + in1[0][3] * in2[3][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2] + in1[0][3] * in2[3][2];
	out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] + in1[0][2] * in2[2][3] + in1[0][3] * in2[3][3];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0] + in1[1][3] * in2[3][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1] + in1[1][3] * in2[3][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2] + in1[1][3] * in2[3][2];
	out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] + in1[1][2] * in2[2][3] + in1[1][3] * in2[3][3];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0] + in1[2][3] * in2[3][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1] + in1[2][3] * in2[3][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2] + in1[2][3] * in2[3][2];
	out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] + in1[2][2] * in2[2][3] + in1[2][3] * in2[3][3];
	out[3][0] = in1[3][0] * in2[0][0] + in1[3][1] * in2[1][0] + in1[3][2] * in2[2][0] + in1[3][3] * in2[3][0];
	out[3][1] = in1[3][0] * in2[0][1] + in1[3][1] * in2[1][1] + in1[3][2] * in2[2][1] + in1[3][3] * in2[3][1];
	out[3][2] = in1[3][0] * in2[0][2] + in1[3][1] * in2[1][2] + in1[3][2] * in2[2][2] + in1[3][3] * in2[3][2];
	out[3][3] = in1[3][0] * in2[0][3] + in1[3][1] * in2[1][3] + in1[3][2] * in2[2][3] + in1[3][3] * in2[3][3];
#endif
}

/*
================
Matrix4x4_CreateProjection

NOTE: produce quake style world orientation
================
*/
void Matrix4x4_CreateProjection( matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar )
{
	out[0][0] = ( 2.0f * zNear ) / ( xMax - xMin );
	out[1][1] = ( 2.0f * zNear ) / ( yMax - yMin );
	out[2][2] = -( zFar + zNear ) / ( zFar - zNear );
	out[3][3] = out[0][1] = out[1][0] = out[3][0] = out[0][3] = out[3][1] = out[1][3] = 0.0f;

	out[2][0] = 0.0f;
	out[2][1] = 0.0f;
	out[0][2] = ( xMax + xMin ) / ( xMax - xMin );
	out[1][2] = ( yMax + yMin ) / ( yMax - yMin );
	out[3][2] = -1.0f;
	out[2][3] = -( 2.0f * zFar * zNear ) / ( zFar - zNear );
}

void Matrix4x4_CreateOrtho( matrix4x4 out, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar )
{
	out[0][0] = 2.0f / (xRight - xLeft);
	out[1][1] = 2.0f / (yTop - yBottom);
	out[2][2] = -2.0f / (zFar - zNear);
	out[3][3] = 1.0f;
	out[0][1] = out[0][2] = out[1][0] = out[1][2] = out[3][0] = out[3][1] = out[3][2] = 0.0f;

	out[2][0] = 0.0f;
	out[2][1] = 0.0f;
	out[0][3] = -(xRight + xLeft) / (xRight - xLeft);
	out[1][3] = -(yTop + yBottom) / (yTop - yBottom);
	out[2][3] = -(zFar + zNear) / (zFar - zNear);
}

/*
================
Matrix4x4_CreateModelview

NOTE: produce quake style world orientation
================
*/
void Matrix4x4_CreateModelview( matrix4x4 out )
{
	out[0][0] = out[1][1] = out[2][2] = 0.0f;
	out[3][0] = out[0][3] = 0.0f;
	out[3][1] = out[1][3] = 0.0f;
	out[3][2] = out[2][3] = 0.0f;
	out[3][3] = 1.0f;
	out[1][0] = out[0][2] = out[2][1] = 0.0f;
	out[2][0] = out[0][1] = -1.0f;
	out[1][2] = 1.0f;
}

void Matrix4x4_ToFMatrix4( const matrix4x4 in, ScePspFMatrix4 *out )
{
	// transpose matrix
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C000,  0 + %1\n"		// C000 = in->x
		"lv.q		C010, 16 + %1\n"		// C010 = in->y
		"lv.q		C020, 32 + %1\n"		// C020 = in->z
		"lv.q		C030, 48 + %1\n"		// C030 = in->w
		"sv.q		R000,  0 + %0\n"		// out->x = R000
		"sv.q		R001, 16 + %0\n"		// out->y = R010
		"sv.q		R002, 32 + %0\n"		// out->z = R020
		"sv.q		R003, 48 + %0\n"		// out->w = R030
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in )
	);
}

void Matrix4x4_FromFMatrix4( matrix4x4 out, ScePspFMatrix4 *in )
{
	// transpose matrix
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C000,  0 + %1\n"		// C000 = in->x
		"lv.q		C010, 16 + %1\n"		// C010 = in->y
		"lv.q		C020, 32 + %1\n"		// C020 = in->z
		"lv.q		C030, 48 + %1\n"		// C030 = in->w
		"sv.q		R000,  0 + %0\n"		// out->x = R000
		"sv.q		R001, 16 + %0\n"		// out->y = R010
		"sv.q		R002, 32 + %0\n"		// out->z = R020
		"sv.q		R003, 48 + %0\n"		// out->w = R030
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in )
	);
}

void Matrix4x4_CreateTranslate( matrix4x4 out, float x, float y, float z )
{
	out[0][0] = 1.0f;
	out[0][1] = 0.0f;
	out[0][2] = 0.0f;
	out[0][3] = x;
	out[1][0] = 0.0f;
	out[1][1] = 1.0f;
	out[1][2] = 0.0f;
	out[1][3] = y;
	out[2][0] = 0.0f;
	out[2][1] = 0.0f;
	out[2][2] = 1.0f;
	out[2][3] = z;
	out[3][0] = 0.0f;
	out[3][1] = 0.0f;
	out[3][2] = 0.0f;
	out[3][3] = 1.0f;
}

void Matrix4x4_CreateRotate( matrix4x4 out, float angle, float x, float y, float z )
{
	float	len, c, s;

	len = x * x + y * y + z * z;
	if( len != 0.0f ) len = 1.0f / sqrt( len );
	x *= len;
	y *= len;
	z *= len;

	angle *= (-M_PI_F / 180.0f);
	SinCos( angle, &s, &c );

	out[0][0]=x * x + c * (1 - x * x);
	out[0][1]=x * y * (1 - c) + z * s;
	out[0][2]=z * x * (1 - c) - y * s;
	out[0][3]=0.0f;
	out[1][0]=x * y * (1 - c) - z * s;
	out[1][1]=y * y + c * (1 - y * y);
	out[1][2]=y * z * (1 - c) + x * s;
	out[1][3]=0.0f;
	out[2][0]=z * x * (1 - c) + y * s;
	out[2][1]=y * z * (1 - c) - x * s;
	out[2][2]=z * z + c * (1 - z * z);
	out[2][3]=0.0f;
	out[3][0]=0.0f;
	out[3][1]=0.0f;
	out[3][2]=0.0f;
	out[3][3]=1.0f;
}

void Matrix4x4_CreateScale( matrix4x4 out, float x )
{
	out[0][0] = x;
	out[0][1] = 0.0f;
	out[0][2] = 0.0f;
	out[0][3] = 0.0f;
	out[1][0] = 0.0f;
	out[1][1] = x;
	out[1][2] = 0.0f;
	out[1][3] = 0.0f;
	out[2][0] = 0.0f;
	out[2][1] = 0.0f;
	out[2][2] = x;
	out[2][3] = 0.0f;
	out[3][0] = 0.0f;
	out[3][1] = 0.0f;
	out[3][2] = 0.0f;
	out[3][3] = 1.0f;
}

void Matrix4x4_CreateScale3( matrix4x4 out, float x, float y, float z )
{
	out[0][0] = x;
	out[0][1] = 0.0f;
	out[0][2] = 0.0f;
	out[0][3] = 0.0f;
	out[1][0] = 0.0f;
	out[1][1] = y;
	out[1][2] = 0.0f;
	out[1][3] = 0.0f;
	out[2][0] = 0.0f;
	out[2][1] = 0.0f;
	out[2][2] = z;
	out[2][3] = 0.0f;
	out[3][0] = 0.0f;
	out[3][1] = 0.0f;
	out[3][2] = 0.0f;
	out[3][3] = 1.0f;
}

void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z )
{
	matrix4x4 base, temp;

	Matrix4x4_Copy( base, out );
	Matrix4x4_CreateTranslate( temp, x, y, z );
	Matrix4x4_Concat( out, base, temp );
}

void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z )
{
	matrix4x4 base, temp;

	Matrix4x4_Copy( base, out );
	Matrix4x4_CreateRotate( temp, angle, x, y, z );
	Matrix4x4_Concat( out, base, temp );
}

void Matrix4x4_ConcatScale( matrix4x4 out, float x )
{
	matrix4x4	base, temp;

	Matrix4x4_Copy( base, out );
	Matrix4x4_CreateScale( temp, x );
	Matrix4x4_Concat( out, base, temp );
}

void Matrix4x4_ConcatScale3( matrix4x4 out, float x, float y, float z )
{
	matrix4x4  base, temp;

	Matrix4x4_Copy( base, out );
	Matrix4x4_CreateScale3( temp, x, y, z );
	Matrix4x4_Concat( out, base, temp );
}
