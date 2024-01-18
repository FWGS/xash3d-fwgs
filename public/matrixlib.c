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

const matrix3x4 m_matrix3x4_identity =
{
{ 1, 0, 0, 0 },	// PITCH	[forward], org[0]
{ 0, 1, 0, 0 },	// YAW	[right]  , org[1]
{ 0, 0, 1, 0 },	// ROLL	[up]     , org[2]
};

/*
========================================================================

		Matrix3x4 operations

========================================================================
*/
void Matrix3x4_VectorTransform( const matrix3x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
		"vhdp.q		S000, C130, C100\n"		// S000 = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2] + in[0][3]
		"vhdp.q		S001, C130, C110\n"		// S001 = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2] + in[1][3]
		"vhdp.q		S002, C130, C120\n"		// S002 = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2] + in[2][3]
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);
#else
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2] + in[0][3];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2] + in[1][3];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2] + in[2][3];
#endif
}

void Matrix3x4_VectorITransform( const matrix3x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
		"vsub.t		C130, C130, R103\n"		// C130 = v - in[][3]
#if 1
		"vtfm3.t	C000, E100, C130\n"		// C000 = E100 * C130
#else
		"vdot.t		S000, C130, R100\n"		// S000 = dir[0] * in[0][0] + dir[1] * in[1][0] + dir[2] * in[2][0]
		"vdot.t		S001, C130, R101\n"		// S001 = dir[0] * in[0][1] + dir[1] * in[1][1] + dir[2] * in[2][1]
		"vdot.t		S002, C130, R102\n"		// S002 = dir[0] * in[0][2] + dir[1] * in[1][2] + dir[2] * in[2][2]
#endif
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);
#else
	vec3_t	dir;

	dir[0] = v[0] - in[0][3];
	dir[1] = v[1] - in[1][3];
	dir[2] = v[2] - in[2][3];

	out[0] = dir[0] * in[0][0] + dir[1] * in[1][0] + dir[2] * in[2][0];
	out[1] = dir[0] * in[0][1] + dir[1] * in[1][1] + dir[2] * in[2][1];
	out[2] = dir[0] * in[0][2] + dir[1] * in[1][2] + dir[2] * in[2][2];
#endif
}

void Matrix3x4_VectorRotate( const matrix3x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
#if 1
		"vtfm3.t	C000, M100, C130\n"		// C000 = M100 * C130
#else
		"vdot.t		S000, C130, C100\n"		// S000 = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2]
		"vdot.t		S001, C130, C110\n"		// S001 = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2]
		"vdot.t		S002, C130, C120\n"		// S002 = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2]
#endif
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);

#else
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2];
#endif
}

void Matrix3x4_VectorIRotate( const matrix3x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
#if 1
		"vtfm3.t	C000, E100, C130\n"		// C000 = E100 * C130
#else
		"vdot.t		S000, C130, R100\n"		// S000 = v[0] * in[0][0] + v[1] * in[1][0] + v[2] * in[2][0]
		"vdot.t		S001, C130, R101\n"		// S001 = v[0] * in[0][1] + v[1] * in[1][1] + v[2] * in[2][1]
		"vdot.t		S002, C130, R102\n"		// S002 = v[0] * in[0][2] + v[1] * in[1][2] + v[2] * in[2][2]
#endif
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);
#else
	out[0] = v[0] * in[0][0] + v[1] * in[1][0] + v[2] * in[2][0];
	out[1] = v[0] * in[0][1] + v[1] * in[1][1] + v[2] * in[2][1];
	out[2] = v[0] * in[0][2] + v[1] * in[1][2] + v[2] * in[2][2];
#endif
}

void Matrix3x4_ConcatTransforms( matrix3x4 out, const matrix3x4 in1, const matrix3x4 in2 )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in1[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in1[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in1[2]
		"vzero.q	C130\n"				// C130 = [0, 0, 0, 0]
		"lv.q		C200,  0 + %2\n"		// C100 = in2[0]
		"lv.q		C210, 16 + %2\n"		// C110 = in2[1]
		"lv.q		C220, 32 + %2\n"		// C120 = in2[2]
		"vidt.q		C230\n"				// C230 = [0, 0, 0, 1]
		"vmmul.q	E000, E100, E200\n"		// E000 = E100 * E200
		"sv.q		C000,  0 + %0\n"		// out[0] = C000
		"sv.q		C010, 16 + %0\n"		// out[1] = C010
		"sv.q		C020, 32 + %0\n"		// out[2] = C020
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in1 ), "m"( *in2 )
	);
#else
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
#endif
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
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C130, %1\n"			// C130 = quaternion
		"lv.s		S300,  0 + %2\n"		// S300 = origin[0]
		"lv.s		S301,  4 + %2\n"		// S301 = origin[1]
		"lv.s		S302,  8 + %2\n"		// S302 = origin[2]
		"vmov.q		C100, C130[ W,  Z, -Y, -X]\n"	// C100 = ( w,  z, -y, -x)
		"vmov.q		C110, C130[-Z,  W,  X, -Y]\n"	// C110 = (-z,  w,  x, -y)
		"vmov.q		C120, C130[ Y, -X,  W, -Z]\n"	// C120 = ( y, -x,  w, -z)
		"vmov.q		C200, C130[ W,  Z, -Y,  X]\n"	// C200 = ( w,  z, -y,  x)
		"vmov.q		C210, C130[-Z,  W,  X,  Y]\n"	// C210 = (-z,  w,  x,  y)
		"vmov.q		C220, C130[ Y, -X,  W,  Z]\n"	// C220 = ( y, -x,  w,  z)
		"vmov.q		C230, C130[-X, -Y, -Z,  W]\n"	// C230 = (-x, -y, -z,  w)
		"vmmul.q	M000, E100, E200\n"		// M000 = E100 * E200
		"vmov.t		R003, C300\n"			// out[x][3] = origin[x]
		"sv.q		C000,  0 + %0\n"		// out[0] = C000
		"sv.q		C010, 16 + %0\n"		// out[1] = C010
		"sv.q		C020, 32 + %0\n"		// out[2] = C020
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *quaternion ), "m"( *origin )
	);
#else
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
#endif
}

void Matrix3x4_CreateFromEntity( matrix3x4 out, const vec3_t angles, const vec3_t origin, float scale )
{
#if 0/*XASH_PSP*/ /* performance not tested */ /* BUG */
	if( angles[ROLL] )
	{
		__asm__ (
			".set		push\n"				// save assembler option
			".set		noreorder\n"			// suppress reordering
			"lv.s		S100,  0 + %1\n"		// S100 = angles[PITCH]
			"lv.s		S101,  4 + %1\n"		// S101 = angles[YAW]
			"lv.s		S102,  8 + %1\n"		// S102 = angles[ROLL]
			"lv.s		S003,  0 + %2\n"		// S003 = out[0][3] = origin[0]
			"lv.s		S013,  4 + %2\n"		// S013 = out[1][3] = origin[1]
			"lv.s		S023,  8 + %2\n"		// S023 = out[2][3] = origin[2]
			"lv.s		S130, %3\n"			// S130 = scale
			/**/
			"vfim.s		S120, 0.0111111111111111\n"	// S121 = 0.0111111111111111 const ( 2 / 180 )
			"vscl.t		C100, C100, S120\n"		// C100 = C100 * S120 = angles * ( 2 / 180 )
			/**/
			"vsin.t		C110, C100\n"			// C110 = sin( C100 ) P Y R
			"vcos.t		C120, C100\n"			// C120 = cos( C100 ) P Y R
			"vneg.t		C100, C110\n"			// C100 = -C110 = -sin( C100 )
			/**/
			"vmul.s		S000, S120, S121\n"		// S000 = S120 * S121 = out[0][0] = ( cp * cy )
			"vmul.s		S011, S112, S110\n"		// S011 = S112 * S110 = ( sr * sp )
			"vmul.s		S001, S011, S121\n"		// S001 = S011 * S121 = ( sr * sp * cy )
			"vmul.s		S103, S122, S101\n"		// S001 = S122 * S101 = ( cr * -sy )
			"vadd.s		S001, S001, S103\n"		// S001 = S001 + S103 = out[0][1] = ( sr * sp * cy +  cr * -sy )
			"vmul.s		S012, S122, S110\n"		// S002 = S122 * S110 = ( cr * sp )
			"vmul.s		S002, S012, S121\n"		// S002 = S012 * S121 = ( cr * sp * cy )
			"vmul.s		S103, S102, S101\n"		// S002 = S102 * S101 = ( -sr * -sy )
			"vadd.s		S002, S002, S103\n"		// S002 = S002 + S103 = out[0][2] = ( cr * sp * cy + -sr * -sy )
			/**/
			"vmul.s		S010, S120, S111\n"		// S010 = S120 * S111 = out[1][0] = ( cp * sy )
			"vmul.s		S011, S011, S111\n"		// S001 = S011 * S111 = ( sr * sp * sy )
			"vmul.s		S103, S122, S121\n"		// S001 = S122 * S121 = ( cr * cy )
			"vadd.s		S011, S011, S103\n"		// S011 = S011 + S103 = out[1][1] = ( sr * sp * sy +  cr * cy )
			"vmul.s		S012, S012, S111\n"		// S012 = S012 * S111 = ( cr * sp * sy )
			"vmul.s		S103, S102, S121\n"		// S103 = S102 * S121 = ( -sr * cy )
			"vadd.s		S012, S012, S103\n"		// S012 = S012 + S103 = out[1][2] = ( cr * sp * sy + -sr * cy )
			/**/
			"vmov.s		S020, S101\n"			// S020 = S101        = out[2][0] = ( -sp )
			"vmul.s		S021, S112, S120\n"		// S021 = S112 * S120 = out[2][1] = ( sr * cp )
			"vmul.s		S022, S122, S120\n"		// S021 = S122 * S120 = out[2][2] = ( cr * cp )
			/**/
			"vmscl.t	E000, E000, S130\n"		// E000 = E000 * S103 = out(3) * scale
			/**/
			"sv.q		C000,  0 + %0\n"		// out[0] = C000
			"sv.q		C010, 16 + %0\n"		// out[1] = C010
			"sv.q		C020, 32 + %0\n"		// out[2] = C020
			".set		pop\n"				// restore assembler option
			: "=m"( *out )
			: "m"( *angles ), "m"( *origin ), "m"( scale )
		);
	}
	else if( angles[PITCH] )
	{
		__asm__ (
			".set		push\n"				// save assembler option
			".set		noreorder\n"			// suppress reordering
			"lv.s		S100,  0 + %1\n"		// S100 = angles[PITCH]
			"lv.s		S101,  4 + %1\n"		// S101 = angles[YAW]
			"lv.s		S003,  0 + %2\n"		// S003 = out[0][3] = origin[0]
			"lv.s		S013,  4 + %2\n"		// S013 = out[1][3] = origin[1]
			"lv.s		S023,  8 + %2\n"		// S023 = out[2][3] = origin[2]
			"lv.s		S130, %3\n"			// S130 = scale
			/**/
			"vfim.s		S120, 0.0111111111111111\n"	// S121 = 0.0111111111111111 const ( 2 / 180 )
			"vscl.p		C100, C100, S120\n"		// C100 = C100 * S120 = angles * ( 2 / 180 )
			/**/
			"vsin.p		C110, C100\n"			// C110 = sin( C100 ) P Y
			"vcos.p		C120, C100\n"			// C120 = cos( C100 ) P Y
			"vneg.p		C100, C110\n"			// C100 = -C110 = -sin( C100 )
			/**/
			"vmul.s		S000, S120, S121\n"		// S000 = S120 * S121 = out[0][0] = ( cp * cy )
			"vmov.s		S001, S101\n"			// S001 = S101        = out[0][1] = ( -sy )
			"vmul.s		S002, S110, S121\n"		// S001 = S110 * S121 = out[0][2] = ( sp * cy )
			/**/
			"vmul.s		S010, S120, S111\n"		// S010 = S120 * S111 = out[1][0] = ( cp * sy )
			"vmov.s		S011, S121\n"			// S011 = S121        = out[1][1] = ( cy )
			"vmul.s		S012, S110, S111\n"		// S012 = S110 * S111 = out[1][2] = ( sp * sy )
			/**/
			"vmov.s		S020, S100\n"			// S020 = S100 = out[2][0] = ( -sp )
			"vzero.s	S021\n"				// S021        = out[2][1] = 0.0f
			"vmov.s		S022, S120\n"			// S022 = S120 = out[2][2] = ( cp )
			/**/
			"vmscl.t	E000, E000, S130\n"		// E000 = E000 * S103 = out(3) * scale
			/**/
			"sv.q		C000,  0 + %0\n"		// out[0] = C000
			"sv.q		C010, 16 + %0\n"		// out[1] = C010
			"sv.q		C020, 32 + %0\n"		// out[2] = C020
			".set		pop\n"				// restore assembler option
			: "=m"( *out )
			: "m"( *angles ), "m"( *origin ), "m"( scale )
		);
	}
	else if( angles[YAW] )
	{
		__asm__ (
			".set		push\n"				// save assembler option
			".set		noreorder\n"			// suppress reordering
			"lv.s		S101,  4 + %1\n"		// S101 = angles[YAW]
			"lv.s		S003,  0 + %2\n"		// S003 = out[0][3] = origin[0]
			"lv.s		S013,  4 + %2\n"		// S013 = out[1][3] = origin[1]
			"lv.s		S023,  8 + %2\n"		// S023 = out[2][3] = origin[2]
			"lv.s		S130, %3\n"			// S130 = scale
			/**/
			"vfim.s		S120, 0.0111111111111111\n"	// S121 = 0.0111111111111111 const ( 2 / 180 )
			"vmul.s		S101, S101, S120\n"		// S101 = S101 * S120 = angles[YAW] * ( 2 / 180 )
			/**/
			"vsin.s		S111, S101\n"			// S111 = sin( S101 ) Y
			"vcos.s		S121, S101\n"			// S121 = cos( S101 ) Y
			/**/
			"vzero.p	R002\n"				// S002 = 0.0f S012 = 0.0f
			"vzero.p	C020\n"				// S020 = 0.0f S021 = 0.0f
			"vmov.s		S000, S121\n"			// S000 = S121 = out[0][0] = ( cy )
			"vneg.s		S001, S111\n"			// S001 = S111 = out[0][1] = ( -sy )
			"vmov.s		S010, S111\n"			// S010 = S111 = out[1][0] = ( sy )
			"vmov.s		S011, S121\n"			// S011 = S121 = out[1][1] = ( cy )
			"vone.s		S022\n"					// S022        = out[2][2] = 1.0f
			/**/
			"vmscl.t	E000, E000, S130\n"		// E000 = E000 * S103 = out(3) * scale
			/**/
			"sv.q		C000,  0 + %0\n"		// out[0] = C000
			"sv.q		C010, 16 + %0\n"		// out[1] = C010
			"sv.q		C020, 32 + %0\n"		// out[2] = C020
			".set		pop\n"				// restore assembler option
			: "=m"( *out )
			: "m"( *angles ), "m"( *origin ), "m"( scale )
		);
	}
	else
	{
		__asm__ (
			".set		push\n"				// save assembler option
			".set		noreorder\n"			// suppress reordering
			"lv.s		S003,  0 + %1\n"		// S003 = out[0][3] = origin[0]
			"lv.s		S013,  4 + %1\n"		// S013 = out[1][3] = origin[1]
			"lv.s		S023,  8 + %1\n"		// S023 = out[2][3] = origin[2]
			"lv.s		S130, %2\n"			// S130 = scale
			/**/
			"vzero.t	C000\n"				// C000 = [0.0f, 0.0f, 0.0f]
			"vzero.t	C010\n"				// C010 = [0.0f, 0.0f, 0.0f]
			"vzero.t	C020\n"				// C020 = [0.0f, 0.0f, 0.0f]
			"vmov.s		S000, S130\n"			// S000 = S130 = out[0][0] = scale
			"vmov.s		S011, S130\n"			// S011 = S130 = out[1][1] = scale
			"vmov.s		S022, S130\n"			// S022 = S130 = out[2][2] = scale
			/**/
			"sv.q		C000,  0 + %0\n"		// out[0] = C000
			"sv.q		C010, 16 + %0\n"		// out[1] = C010
			"sv.q		C020, 32 + %0\n"		// out[2] = C020
			".set		pop\n"				// restore assembler option
			: "=m"( *out )
			: "m"( *origin ), "m"( scale )
		);
	}
#else
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
#endif
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
	worldExtents[0] = DotProductAbs( localExtents, world[0] );	// auto-transposed!
	worldExtents[1] = DotProductAbs( localExtents, world[1] );
	worldExtents[2] = DotProductAbs( localExtents, world[2] );

	VectorSubtract( worldCenter, worldExtents, absmin );
	VectorAdd( worldCenter, worldExtents, absmax );
}

const matrix4x4 m_matrix4x4_identity =
{
{ 1, 0, 0, 0 },	// PITCH
{ 0, 1, 0, 0 },	// YAW
{ 0, 0, 1, 0 },	// ROLL
{ 0, 0, 0, 1 },	// ORIGIN
};

/*
========================================================================

		Matrix4x4 operations

========================================================================
*/
void Matrix4x4_VectorTransform( const matrix4x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
		"vhdp.q		S000, C130, C100\n"		// S000 = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2] + in[0][3]
		"vhdp.q		S001, C130, C110\n"		// S001 = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2] + in[1][3]
		"vhdp.q		S002, C130, C120\n"		// S002 = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2] + in[2][3]
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);
#else
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2] + in[0][3];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2] + in[1][3];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2] + in[2][3];
#endif
}

void Matrix4x4_VectorITransform( const matrix4x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
		"vsub.t		C130, C130, R103\n"		// C130 = v - in[][3]
#if 1
		"vtfm3.t	C000, E100, C130\n"		// C000 = E100 * C130
#else
		"vdot.t		S000, C130, R100\n"		// S000 = dir[0] * in[0][0] + dir[1] * in[1][0] + dir[2] * in[2][0]
		"vdot.t		S001, C130, R101\n"		// S001 = dir[0] * in[0][1] + dir[1] * in[1][1] + dir[2] * in[2][1]
		"vdot.t		S002, C130, R102\n"		// S002 = dir[0] * in[0][2] + dir[1] * in[1][2] + dir[2] * in[2][2]
#endif
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);
#else
	vec3_t	dir;

	dir[0] = v[0] - in[0][3];
	dir[1] = v[1] - in[1][3];
	dir[2] = v[2] - in[2][3];

	out[0] = dir[0] * in[0][0] + dir[1] * in[1][0] + dir[2] * in[2][0];
	out[1] = dir[0] * in[0][1] + dir[1] * in[1][1] + dir[2] * in[2][1];
	out[2] = dir[0] * in[0][2] + dir[1] * in[1][2] + dir[2] * in[2][2];
#endif
}

void Matrix4x4_VectorRotate( const matrix4x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
#if 1
		"vtfm3.t	C000, M100, C130\n"		// C000 = M100 * C130
#else
		"vdot.t		S000, C130, C100\n"		// S000 = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2]
		"vdot.t		S001, C130, C110\n"		// S001 = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2]
		"vdot.t		S002, C130, C120\n"		// S002 = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2]
#endif
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);
#else
	out[0] = v[0] * in[0][0] + v[1] * in[0][1] + v[2] * in[0][2];
	out[1] = v[0] * in[1][0] + v[1] * in[1][1] + v[2] * in[1][2];
	out[2] = v[0] * in[2][0] + v[1] * in[2][1] + v[2] * in[2][2];
#endif
}

void Matrix4x4_VectorIRotate( const matrix4x4 in, const float v[3], float out[3] )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in[2]
		"lv.s		S130,  0 + %2\n"		// S130 = v[0]
		"lv.s		S131,  4 + %2\n"		// S131 = v[1]
		"lv.s		S132,  8 + %2\n"		// S132 = v[2]
#if 1
		"vtfm3.t	C000, E100, C130\n"		// C000 = E100 * C130
#else
		"vdot.t		S000, C130, R100\n"		// S000 = v[0] * in[0][0] + v[1] * in[1][0] + v[2] * in[2][0]
		"vdot.t		S001, C130, R101\n"		// S001 = v[0] * in[0][1] + v[1] * in[1][1] + v[2] * in[2][1]
		"vdot.t		S002, C130, R102\n"		// S002 = v[0] * in[0][2] + v[1] * in[1][2] + v[2] * in[2][2]
#endif
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in ), "m"( *v )
	);
#else
	out[0] = v[0] * in[0][0] + v[1] * in[1][0] + v[2] * in[2][0];
	out[1] = v[0] * in[0][1] + v[1] * in[1][1] + v[2] * in[2][1];
	out[2] = v[0] * in[0][2] + v[1] * in[1][2] + v[2] * in[2][2];
#endif

}

void Matrix4x4_ConcatTransforms( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C100,  0 + %1\n"		// C100 = in1[0]
		"lv.q		C110, 16 + %1\n"		// C110 = in1[1]
		"lv.q		C120, 32 + %1\n"		// C120 = in1[2]
		"vzero.q	C130\n"				// C130 = [0, 0, 0, 0]
		"lv.q		C200,  0 + %2\n"		// C100 = in2[0]
		"lv.q		C210, 16 + %2\n"		// C110 = in2[1]
		"lv.q		C220, 32 + %2\n"		// C120 = in2[2]
		"vidt.q		C230\n"				// C230 = [0, 0, 0, 1]
		"vmmul.q	E000, E100, E200\n"		// E000 = E100 * E200
		"sv.q		C000,  0 + %0\n"		// out[0] = C000
		"sv.q		C010, 16 + %0\n"		// out[1] = C010
		"sv.q		C020, 32 + %0\n"		// out[2] = C020
		".set		pop\n"				// restore assembler option
		: "=m"( *out )
		: "m"( *in1 ), "m"( *in2 )
	);
#else
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
#endif
}

void Matrix4x4_CreateFromEntity( matrix4x4 out, const vec3_t angles, const vec3_t origin, float scale )
{
#if 0/*XASH_PSP*/ /* performance not tested */ /* BUG */
	if( angles[ROLL] )
	{
		__asm__ (
			".set			push\n"				// save assembler option
			".set			noreorder\n"			// suppress reordering
			"lv.s			S100,  0 + %1\n"		// S100 = angles[PITCH]
			"lv.s			S101,  4 + %1\n"		// S101 = angles[YAW]
			"lv.s			S102,  8 + %1\n"		// S102 = angles[ROLL]
			"lv.s			S003,  0 + %2\n"		// S003 = out[0][3] = origin[0]
			"lv.s			S013,  4 + %2\n"		// S013 = out[1][3] = origin[1]
			"lv.s			S023,  8 + %2\n"		// S023 = out[2][3] = origin[2]
			"lv.s			S130, %3\n"			// S130 = scale
			/**/
			"vfim.s			S120, 0.0111111111111111\n"	// S121 = 0.0111111111111111 const ( 2 / 180 )
			"vscl.t			C100, C100, S120\n"		// C100 = C100 * S120 = angles * ( 2 / 180 )
			/**/
			"vsin.t			C110, C100\n"			// C110 = sin( C100 ) P Y R
			"vcos.t			C120, C100\n"			// C120 = cos( C100 ) P Y R
			"vneg.t			C100, C110\n"			// C100 = -C110 = -sin( C100 )
			/**/
			"vmul.s			S000, S120, S121\n"		// S000 = S120 * S121 = out[0][0] = ( cp * cy )
			"vmul.s			S011, S112, S110\n"		// S011 = S112 * S110 = ( sr * sp )
			"vmul.s			S001, S011, S121\n"		// S001 = S011 * S121 = ( sr * sp * cy )
			"vmul.s			S103, S122, S101\n"		// S001 = S122 * S101 = ( cr * -sy )
			"vadd.s			S001, S001, S103\n"		// S001 = S001 + S103 = out[0][1] = ( sr * sp * cy +  cr * -sy )
			"vmul.s			S012, S122, S110\n"		// S002 = S122 * S110 = ( cr * sp )
			"vmul.s			S002, S012, S121\n"		// S002 = S012 * S121 = ( cr * sp * cy )
			"vmul.s			S103, S102, S101\n"		// S002 = S102 * S101 = ( -sr * -sy )
			"vadd.s			S002, S002, S103\n"		// S002 = S002 + S103 = out[0][2] = ( cr * sp * cy + -sr * -sy )
			/**/
			"vmul.s			S010, S120, S111\n"		// S010 = S120 * S111 = out[1][0] = ( cp * sy )
			"vmul.s			S011, S011, S111\n"		// S001 = S011 * S111 = ( sr * sp * sy )
			"vmul.s			S103, S122, S121\n"		// S001 = S122 * S121 = ( cr * cy )
			"vadd.s			S011, S011, S103\n"		// S011 = S011 + S103 = out[1][1] = ( sr * sp * sy +  cr * cy )
			"vmul.s			S012, S012, S111\n"		// S012 = S012 * S111 = ( cr * sp * sy )
			"vmul.s			S103, S102, S121\n"		// S103 = S102 * S121 = ( -sr * cy )
			"vadd.s			S012, S012, S103\n"		// S012 = S012 + S103 = out[1][2] = ( cr * sp * sy + -sr * cy )
			/**/
			"vmov.s			S020, S101\n"			// S020 = S101        = out[2][0] = ( -sp )
			"vmul.s			S021, S112, S120\n"		// S021 = S112 * S120 = out[2][1] = ( sr * cp )
			"vmul.s			S022, S122, S120\n"		// S021 = S122 * S120 = out[2][2] = ( cr * cp )
			/**/
			"vmscl.t		E000, E000, S130\n"		// E000 = E000 * S103 = out(3) * scale
			"vidt.q			C030\n"				// C030 = [0.0f, 0.0f, 0.0f, 1.0f]
			/**/
			"sv.q			C000,  0 + %0\n"		// out[0] = C000
			"sv.q			C010, 16 + %0\n"		// out[1] = C010
			"sv.q			C020, 32 + %0\n"		// out[2] = C020
			"sv.q			C030, 48 + %0\n"		// out[3] = C030
			".set			pop\n"				// restore assembler option
			: "=m"( *out )
			: "m"( *angles ), "m"( *origin ), "m"( scale )
		);
	}
	else if( angles[PITCH] )
	{
		__asm__ (
			".set			push\n"				// save assembler option
			".set			noreorder\n"			// suppress reordering
			"lv.s			S100,  0 + %1\n"		// S100 = angles[PITCH]
			"lv.s			S101,  4 + %1\n"		// S101 = angles[YAW]
			"lv.s			S003,  0 + %2\n"		// S003 = out[0][3] = origin[0]
			"lv.s			S013,  4 + %2\n"		// S013 = out[1][3] = origin[1]
			"lv.s			S023,  8 + %2\n"		// S023 = out[2][3] = origin[2]
			"lv.s			S130, %3\n"			// S130 = scale
			/**/
			"vfim.s			S120, 0.0111111111111111\n"	// S121 = 0.0111111111111111 const ( 2 / 180 )
			"vscl.p			C100, C100, S120\n"		// C100 = C100 * S120 = angles * ( 2 / 180 )
			/**/
			"vsin.p			C110, C100\n"			// C110 = sin( C100 ) P Y
			"vcos.p			C120, C100\n"			// C120 = cos( C100 ) P Y
			"vneg.p			C100, C110\n"			// C100 = -C110 = -sin( C100 )
			/**/
			"vmul.s			S000, S120, S121\n"		// S000 = S120 * S121 = out[0][0] = ( cp * cy )
			"vmov.s			S001, S101\n"			// S001 = S101        = out[0][1] = ( -sy )
			"vmul.s			S002, S110, S121\n"		// S001 = S110 * S121 = out[0][2] = ( sp * cy )
			/**/
			"vmul.s			S010, S120, S111\n"		// S010 = S120 * S111 = out[1][0] = ( cp * sy )
			"vmov.s			S011, S121\n"			// S011 = S121        = out[1][1] = ( cy )
			"vmul.s			S012, S110, S111\n"		// S012 = S110 * S111 = out[1][2] = ( sp * sy )
			/**/
			"vmov.s			S020, S100\n"			// S020 = S100 = out[2][0] = ( -sp )
			"vzero.s		S021\n"				// S021        = out[2][1] = 0.0f
			"vmov.s			S022, S120\n"			// S022 = S120 = out[2][2] = ( cp )
			/**/
			"vmscl.t		E000, E000, S130\n"		// E000 = E000 * S103 = out(3) * scale
			"vidt.q			C030\n"				// C030 = [0.0f, 0.0f, 0.0f, 1.0f]
			/**/
			"sv.q			C000,  0 + %0\n"		// out[0] = C000
			"sv.q			C010, 16 + %0\n"		// out[1] = C010
			"sv.q			C020, 32 + %0\n"		// out[2] = C020
			"sv.q			C030, 48 + %0\n"		// out[3] = C030
			".set			pop\n"				// restore assembler option
			: "=m"( *out )
			: "m"( *angles ), "m"( *origin ), "m"( scale )
		);
	}
	else if( angles[YAW] )
	{
		__asm__ (
			".set			push\n"				// save assembler option
			".set			noreorder\n"			// suppress reordering
			"lv.s			S101,  4 + %1\n"		// S101 = angles[YAW]
			"lv.s			S003,  0 + %2\n"		// S003 = out[0][3] = origin[0]
			"lv.s			S013,  4 + %2\n"		// S013 = out[1][3] = origin[1]
			"lv.s			S023,  8 + %2\n"		// S023 = out[2][3] = origin[2]
			"lv.s			S130, %3\n"			// S130 = scale
			/**/
			"vfim.s			S120, 0.0111111111111111\n"	// S121 = 0.0111111111111111 const ( 2 / 180 )
			"vmul.s			S101, S101, S120\n"		// S101 = S101 * S120 = angles[YAW] * ( 2 / 180 )
			/**/
			"vsin.s			S111, S101\n"			// S111 = sin( S101 ) Y
			"vcos.s			S121, S101\n"			// S121 = cos( S101 ) Y
			/**/
			"vzero.p		R002\n"				// S002 = 0.0f S012 = 0.0f
			"vzero.p		C020\n"				// S020 = 0.0f S021 = 0.0f
			"vmov.s			S000, S121\n"			// S000 = S121 = out[0][0] = ( cy )
			"vneg.s			S001, S111\n"			// S001 = S111 = out[0][1] = ( -sy )
			"vmov.s			S010, S111\n"			// S010 = S111 = out[1][0] = ( sy )
			"vmov.s			S011, S121\n"			// S011 = S121 = out[1][1] = ( cy )
			"vone.s			S022\n"				// S022        = out[2][2] = 1.0f
			/**/
			"vmscl.t		E000, E000, S130\n"		// E000 = E000 * S103 = out(3) * scale
			"vidt.q			C030\n"				// C030 = [0.0f, 0.0f, 0.0f, 1.0f]
			/**/
			"sv.q			C000,  0 + %0\n"		// out[0] = C000
			"sv.q			C010, 16 + %0\n"		// out[1] = C010
			"sv.q			C020, 32 + %0\n"		// out[2] = C020
			"sv.q			C030, 48 + %0\n"		// out[3] = C030
			".set			pop\n"					// restore assembler option
			: "=m"( *out )
			: "m"( *angles ), "m"( *origin ), "m"( scale )
		);
	}
	else
	{
		__asm__ (
			".set			push\n"				// save assembler option
			".set			noreorder\n"			// suppress reordering
			"lv.s			S003,  0 + %1\n"		// S003 = out[0][3] = origin[0]
			"lv.s			S013,  4 + %1\n"		// S013 = out[1][3] = origin[1]
			"lv.s			S023,  8 + %1\n"		// S023 = out[2][3] = origin[2]
			"lv.s			S130, %2\n"			// S130 = scale
			/**/
			"vzero.t		C000\n"				// C000 = [0.0f, 0.0f, 0.0f]
			"vzero.t		C010\n"				// C010 = [0.0f, 0.0f, 0.0f]
			"vzero.t		C020\n"				// C020 = [0.0f, 0.0f, 0.0f]
			"vidt.q			C030\n"				// C030 = [0.0f, 0.0f, 0.0f, 1.0f]
			"vmov.s			S000, S130\n"			// S000 = S130 = out[0][0] = scale
			"vmov.s			S011, S130\n"			// S011 = S130 = out[1][1] = scale
			"vmov.s			S022, S130\n"			// S022 = S130 = out[2][2] = scale
			/**/
			"sv.q			C000,  0 + %0\n"		// out[0] = C000
			"sv.q			C010, 16 + %0\n"		// out[1] = C010
			"sv.q			C020, 32 + %0\n"		// out[2] = C020
			"sv.q			C030, 48 + %0\n"		// out[3] = C030
			".set			pop\n"				// restore assembler option
			: "=m"( *out )
			: "m"( *origin ), "m"( scale )
		);
	}
#else
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
#endif
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
#if XASH_PSP
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"mfc1		$8, %4\n"			// FPU->CPU
		"mtv		$8, S210\n"			// CPU->VFPU S210 = d
		"lv.q		C100,  0 + %2\n"		// C100 = in[0]
		"lv.q		C110, 16 + %2\n"		// C110 = in[1]
		"lv.q		C120, 32 + %2\n"		// C120 = in[2]
		"lv.s		S200,  0 + %3\n"		// S200 = normal[0]
		"lv.s		S201,  4 + %3\n"		// S201 = normal[1]
		"lv.s		S202,  8 + %3\n"		// S202 = normal[2]
		"vdot.t		S211, C100, C100\n"		// S211 = C100 * C100
		"vsqrt.s	S211, S211\n"			// S211 = sqrt( S211 )
		"vrcp.s		S212, S211\n"			// S212 = 1 / S211
		"vtfm3.t	C000, M100, C200\n"		// C000 = M100 * C200
		"vscl.t		C000, C000, S212\n"		// C000 = C000 * S211
		"vmul.s		S003, S210, S211\n"		// S003 = S210 * S211
		"vdot.t		S010, R103,	C000\n"		// S010 = R103 * C000
		"vadd.s		S003, S003, S010\n"		// S003 = S003 + S010
		"sv.s		S000,  0 + %0\n"		// out[0] = S000
		"sv.s		S001,  4 + %0\n"		// out[1] = S001
		"sv.s		S002,  8 + %0\n"		// out[2] = S002
		"sv.s		S003, %1\n"			// dist = S003
		".set		pop\n"				// restore assembler option
		: "=m"( *out ), "=m"( *dist )
		: "m"( *in ), "m"( *normal ), "f"( d )
		: "$8"
	);
#else
	float	scale = sqrt( in[0][0] * in[0][0] + in[0][1] * in[0][1] + in[0][2] * in[0][2] );
	float	iscale = 1.0f / scale;

	out[0] = (normal[0] * in[0][0] + normal[1] * in[0][1] + normal[2] * in[0][2]) * iscale;
	out[1] = (normal[0] * in[1][0] + normal[1] * in[1][1] + normal[2] * in[1][2]) * iscale;
	out[2] = (normal[0] * in[2][0] + normal[1] * in[2][1] + normal[2] * in[2][2]) * iscale;
	*dist = d * scale + ( out[0] * in[0][3] + out[1] * in[1][3] + out[2] * in[2][3] );
#endif
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
