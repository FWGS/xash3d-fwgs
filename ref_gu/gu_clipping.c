/*
gu_clipping.c - software clipping implimentation
Copyright (C) 2021 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*

Based on clipping system from PSP Quake by Peter Mackay and Chris Swindle.

*/

#include "gu_local.h"

#define MAX_CLIPPED_VERTICES	32

#define CPLANE_BOTTOM		0
#define CPLANE_LEFT		1
#define CPLANE_RIGHT		2
#define CPLANE_TOP		3
#define MAX_CPLANES		4

// Types.
typedef ScePspFVector4	plane_type;
typedef plane_type	frustum_t[MAX_CPLANES];

// Transformed frustum.
static ScePspFMatrix4	projection_view_matrix __attribute__(( aligned( 16 )));
static frustum_t	projection_view_frustum __attribute__(( aligned( 16 )));
static frustum_t	clipping_frustum __attribute__(( aligned( 16 )));

// The temporary working buffers.
static gu_vert_t	work_buffer[2][MAX_CLIPPED_VERTICES] __attribute__(( aligned( 16 )));

/*
=================
GU_ClipGetFrustum
=================
*/
_inline void GU_ClipGetFrustum(const ScePspFMatrix4 *matrix, frustum_t* frustum)
{
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"vzero.q	C210\n"				// set zero vector
		"lv.q		C100,  0(%4)\n"			// C000 = matrix->x
		"lv.q		C110, 16(%4)\n"			// C010 = matrix->y
		"lv.q		C120, 32(%4)\n"			// C020 = matrix->z
		"lv.q		C130, 48(%4)\n"			// C030 = matrix->w
		"vadd.q		C000, R103, R101\n"		// C000 = R103 + R101                                   ( BOTTOM )
		"vadd.q		C010, R103, R100\n"		// C010 = R103 + R100                                   ( LEFT )
		"vsub.q		C020, R103, R100\n"		// C020 = R103 - R100                                   ( RIGHT )
		"vsub.q		C030, R103, R101\n"		// C030 = R103 - R101                                   ( TOP )
		"vdot.q		S200, C000, C000\n"		// S110 = S100*S100 + S101*S101 + S102*S102 + S103*S103 ( BOTTOM )
		"vdot.q		S201, C010, C010\n"		// S110 = S100*S100 + S101*S101 + S102*S102 + S103*S103 ( LEFT )
		"vdot.q		S202, C020, C020\n"		// S110 = S100*S100 + S101*S101 + S102*S102 + S103*S103 ( RIGHT )
		"vdot.q		S203, C030, C030\n"		// S110 = S100*S100 + S101*S101 + S102*S102 + S103*S103 ( TOP )
		"vcmp.q		EZ,   C200\n"			// CC[0] = ( C200 == 0.0f )
		"vrsq.q		C200, C200\n"			// C200 = 1.0 / sqrt( C200 )
		"vcmovt.q	C200, C210, 0\n"		// if ( CC[*] ) C200 = C210
		"vscl.q		C000, C000, S200\n"		// C000 = C000 * S200                                   ( BOTTOM )
		"vscl.q		C010, C010, S201\n"		// C010 = C010 * S201                                   ( LEFT )
		"vscl.q		C020, C020, S202\n"		// C020 = C020 * S202                                   ( RIGHT )
		"vscl.q		C030, C030, S203\n"		// C030 = C030 * S203                                   ( TOP )
		"sv.q		C000, %0\n"			// Store plane from register
		"sv.q		C010, %1\n"			// Store plane from register
		"sv.q		C020, %2\n"			// Store plane from register
		"sv.q		C030, %3\n"			// Store plane from register
		".set		pop\n"				// Restore assembler option
		:	"=m"( ( *frustum )[CPLANE_BOTTOM] ),
			"=m"( ( *frustum )[CPLANE_LEFT] ),
			"=m"( ( *frustum )[CPLANE_RIGHT] ),
			"=m"( ( *frustum )[CPLANE_TOP] )
		:	"r"( matrix ) /* addr */
	);
}

/*
=================
GU_ClipBeginFrame

Calculate the clipping frustum for static objects
=================
*/
void GU_ClipBeginFrame( void )
{
	// Get the projection matrix.
	sceGumMatrixMode( GU_PROJECTION );
	ScePspFMatrix4	proj;
	sceGumStoreMatrix( &proj );

	// Get the view matrix.
	sceGumMatrixMode( GU_VIEW );
	ScePspFMatrix4	view;
	sceGumStoreMatrix( &view );

	// Restore matrix mode.
	sceGumMatrixMode( GU_MODEL );

	// Combine the two matrices (multiply projection by view).
	gumMultMatrix( &projection_view_matrix, &proj, &view );

	// Calculate and cache the clipping frustum.
	GU_ClipGetFrustum( &projection_view_matrix, &projection_view_frustum );

	__asm__ volatile(
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C700, %4\n"			// Load plane into register
		"lv.q		C710, %5\n"			// Load plane into register
		"lv.q		C720, %6\n"			// Load plane into register
		"lv.q		C730, %7\n"			// Load plane into register
		"sv.q		C700, %0\n"			// Store plane from register
		"sv.q		C710, %1\n"			// Store plane from register
		"sv.q		C720, %2\n"			// Store plane from register
		"sv.q		C730, %3\n"			// Store plane from register
		".set		pop\n"				// Restore assembler option
		:	"=m"( clipping_frustum[CPLANE_BOTTOM] ),
			"=m"( clipping_frustum[CPLANE_LEFT] ),
			"=m"( clipping_frustum[CPLANE_RIGHT] ),
			"=m"( clipping_frustum[CPLANE_TOP] )
		:	"m"( projection_view_frustum[CPLANE_BOTTOM] ),
			"m"( projection_view_frustum[CPLANE_LEFT] ),
			"m"( projection_view_frustum[CPLANE_RIGHT] ),
			"m"( projection_view_frustum[CPLANE_TOP] )
	);
}

/*
=================
GU_ClipBeginBrush

Calculate the clipping frustum for dynamic objects
=================
*/
void GU_ClipBeginBrush( void )
{
	// Get the model matrix.
	ScePspFMatrix4	model_matrix;
	sceGumStoreMatrix( &model_matrix );

	// Combine the matrices (multiply projection-view by model).
	ScePspFMatrix4	projection_view_model_matrix;
	gumMultMatrix( &projection_view_model_matrix, &projection_view_matrix, &model_matrix );

	// Calculate the clipping frustum.
	GU_ClipGetFrustum( &projection_view_model_matrix, &clipping_frustum );

	__asm__ volatile(
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C700, %0\n"			// Load plane into register
		"lv.q		C710, %1\n"			// Load plane into register
		"lv.q		C720, %2\n"			// Load plane into register
		"lv.q		C730, %3\n"			// Load plane into register
		".set		pop\n"				// Restore assembler option
		::	"m"( clipping_frustum[CPLANE_BOTTOM] ),
			"m"( clipping_frustum[CPLANE_LEFT] ),
			"m"( clipping_frustum[CPLANE_RIGHT] ),
			"m"( clipping_frustum[CPLANE_TOP] )
	);
}

/*
=================
GU_ClipEndBrush

Restore the clipping frustum
=================
*/
void GU_ClipEndBrush( void )
{
	__asm__ volatile(
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C700, %4\n"			// Load plane into register
		"lv.q		C710, %5\n"			// Load plane into register
		"lv.q		C720, %6\n"			// Load plane into register
		"lv.q		C730, %7\n"			// Load plane into register
		"sv.q		C700, %0\n"			// Store plane from register
		"sv.q		C710, %1\n"			// Store plane from register
		"sv.q		C720, %2\n"			// Store plane from register
		"sv.q		C730, %3\n"			// Store plane from register
		".set		pop\n"				// Restore assembler option
		:	"=m"( clipping_frustum[CPLANE_BOTTOM] ),
			"=m"( clipping_frustum[CPLANE_LEFT] ),
			"=m"( clipping_frustum[CPLANE_RIGHT] ),
			"=m"( clipping_frustum[CPLANE_TOP] )
		:	"m"( projection_view_frustum[CPLANE_BOTTOM] ),
			"m"( projection_view_frustum[CPLANE_LEFT] ),
			"m"( projection_view_frustum[CPLANE_RIGHT] ),
			"m"( projection_view_frustum[CPLANE_TOP] )
	);
}

/*
=================
GU_ClipLoadFrustum

Load the clipping frustum ( native )
=================
*/
#if 0
void GU_ClipLoadFrustum( const mplane_t *plane )
{
	__asm__ volatile(
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"ulv.q		C700, %4\n"			// Load plane into register
		"ulv.q		C710, %5\n"			// Load plane into register
		"ulv.q		C720, %6\n"			// Load plane into register
		"ulv.q		C730, %7\n"			// Load plane into register
		"vneg.q		R703, R703\n"			// R703 = -R703 = -dist
		"sv.q		C700, %0\n"			// Store plane from register
		"sv.q		C710, %1\n"			// Store plane from register
		"sv.q		C720, %2\n"			// Store plane from register
		"sv.q		C730, %3\n"			// Store plane from register
		".set		pop\n"				// Restore assembler option
		:	"=m"( clipping_frustum[CPLANE_BOTTOM] ),
			"=m"( clipping_frustum[CPLANE_LEFT] ),
			"=m"( clipping_frustum[CPLANE_RIGHT] ),
			"=m"( clipping_frustum[CPLANE_TOP] )
		:	"m"( plane[FRUSTUM_BOTTOM] ),
			"m"( plane[FRUSTUM_LEFT] ),
			"m"( plane[FRUSTUM_RIGHT] ),
			"m"( plane[FRUSTUM_TOP] )

	);
}
#endif

/*
=================
GU_ClipIsRequired

Is clipping required?
=================
*/
int GU_ClipIsRequired( gu_vert_t* uv, int uvc )
{
	int		result = 1;
	gu_vert_t	*uv_end = uv + ( uvc - 1 );

	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"vzero.q	C600\n"				// C600 = [0.0f, 0.0f, 0.0f. 0.0f]
	"0:\n"							// loop
		"lv.s		S610,  8(%1)\n"			// S610 = v[i].xyz[0]
		"lv.s		S611,  12(%1)\n"		// S611 = v[i].xyz[1]
		"lv.s		S612,  16(%1)\n"		// S612 = v[i].xyz[2]
		"vhtfm4.q	C620, M700, C610\n"		// C620 = frustrum * v[i].xyz
		"vcmp.q		LT,   C620, C600\n"		// S620 < 0.0f || S621 < 0.0f || S622 < 0.0f || S623 < 0.0f
		"bvt		4,    1f\n"			// if ( CC[4] == 1 ) jump to exit
		"nop\n"						// 								( delay slot )
		"bne		%1,   %2,   0b\n"		// if ( $10 != $8 ) jump to loop
		"addiu		%1,   %1,   %3\n"		// $8 = $8 + sizeof( gu_vert_t )				( delay slot )
		"move		%0,   $0\n"			// res = 0
	"1:\n"							// exit
		".set		pop\n"				// Restore assembler option
		:	"=r"( result ), "+r"( uv )
		:	"r"( uv_end ),
			"n"(sizeof( gu_vert_t ))
		:	"$8"
	);
	return result;
}

/*
=================
GU_Clip2Plane

Clips a polygon against a plane.
=================
*/
_inline void GU_Clip2Plane( plane_type *plane, gu_vert_t *uv, int uvc, gu_vert_t *cv, int *cvc )
{
	gu_vert_t	*uv_end = uv + ( uvc - 1 );
	gu_vert_t	*cv_start = cv;

	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"vzero.q	C000\n"				// set zero vector
		"lv.q		C010,  0(%3)\n"			// plane
		"lv.s		S200,  8(%2)\n"			// Load vertex P XYZ(4b) X into register
		"lv.s		S201, 12(%2)\n"			// Load vertex P XYZ(4b) Y into register
		"lv.s		S202, 16(%2)\n"			// Load vertex P XYZ(4b) Z into register
		"lv.s		S210,  0(%2)\n"			// Load vertex P TEX(4b) U into register
		"lv.s		S211,  4(%2)\n"			// Load vertex P TEX(4b) V into register
		"vhdp.q		S212, C200, C010\n"		// distance P  -> dP
	"0:\n"
		"lv.s		S220,  8(%1)\n"			// Load vertex S XYZ(4b) X into register
		"lv.s		S221, 12(%1)\n"			// Load vertex S XYZ(4b) Y into register
		"lv.s		S222, 16(%1)\n"			// Load vertex S XYZ(4b) Z into register
		"lv.s		S230,  0(%1)\n"			// Load vertex S TEX(4b) U into register
		"lv.s		S231,  4(%1)\n"			// Load vertex S TEX(4b) V into register
		"vhdp.q		S232, C220, C010\n"		// distance S -> dS
		"vcmp.s		GT, S212, S000\n"		// if (dP <= 0)
		"bvf		0, 1f\n"			// goto 1:
		"nop\n"
		"sv.s		S210,  0(%0)\n"			// cv->uv[0]  = C020 U
		"sv.s		S211,  4(%0)\n"			// cv->uv[1]  = C021 V
		"sv.s		S200,  8(%0)\n"			// cv->xyz[0] = C030 X
		"sv.s		S201, 12(%0)\n"			// cv->xyz[1] = C031 Y
		"sv.s		S202, 16(%0)\n"			// cv->xyz[2] = C032 Z
		"addiu		%0, %0, 20\n"
	"1:\n"
		"vmul.s		S020, S232, S212\n"		// (dS * dP)
		"vcmp.s		LT, S020, S000\n"		// if (dS * dP < 0)
		"bvf		0, 2f\n"			// goto 2:
		"vsub.s		S021, S232, S212\n"		// (dS - dP)
		"vrcp.s		S021, S021\n"
		"vmul.s		S021, S021, S232\n"		// R = dS / ( dS - dP )
#if 0
		"vsub.t		C200, C220, C200\n"		// (S - P) XYZ
		"vsub.p		C210, C230, C210\n"		// (S - P) UV
		"vscl.t		C200, C200, S021\n"		// ((S - P) * R) XYZ
		"vscl.p		C210, C210, S021\n"		// ((S - P) * R) UV
		"vsub.t		C200, C220, C200\n"		// (S - (S - P) * R) XYZ
		"vsub.p		C210, C230, C210\n"		// (S - (S - P) * R) UV
#else
		"vsub.t		C200, C200, C220\n"		// (P - S) XYZ
		"vsub.p		C210, C210, C230\n"		// (P - S) UV
		"vscl.t		C200, C200, S021\n"		// ((P - S) * R) XYZ
		"vscl.p		C210, C210, S021\n"		// ((P - S) * R) UV
		"vadd.t		C200, C220, C200\n"		// (S + (P - S) * R) XYZ
		"vadd.p		C210, C230, C210\n"		// (S + (P - S) * R) UV
#endif
		"sv.s		S210,  0(%0)\n"			// cv->uv[0]  = S210 U
		"sv.s		S211,  4(%0)\n"			// cv->uv[1]  = S211 V
		"sv.s		S200,  8(%0)\n"			// cv->xyz[0] = S200 X
		"sv.s		S201, 12(%0)\n"			// cv->xyz[1] = S201 Y
		"sv.s		S202, 16(%0)\n"			// cv->xyz[2] = S202 Z
		"addiu		%0, %0, %4\n"			// cv + sizeof(gu_vert_t)
	"2:\n"
		"vmov.t		C200, C220\n"			// P = S XYZ
		"vmov.t		C210, C230\n"			// P = S UV and dS
		"bne		%1, %2, 0b\n"			// if (uv != uv_end) goto 0:
		"addiu		%1, %1, %4\n"			// uv + sizeof(gu_vert_t)						( delay slot )
		".set		pop\n"				// suppress reordering
		:	"+r"( cv ), "+r"( uv )
		:	"r"( uv_end ), "r"( plane ),
			"n"( sizeof( gu_vert_t ))
		:	"memory"
	) ;
	*cvc = ( cv - cv_start );
}

/*
=================
GU_Clip

Clips a polygon against the frustum
=================
*/
void GU_Clip( gu_vert_t *uv, int uvc, gu_vert_t **cv, int* cvc )
{
	size_t		vc;

	if ( !uvc )	// no vertices to clip?
		gEngfuncs.Host_Error( "GU_Clip: calling clip with zero vertices!" );

	vc = uvc;
	*cvc = 0;

	GU_Clip2Plane( &clipping_frustum[CPLANE_BOTTOM], uv,             vc, work_buffer[0], &vc );
	if ( !vc ) return;
	GU_Clip2Plane( &clipping_frustum[CPLANE_LEFT],   work_buffer[0], vc, work_buffer[1], &vc );
	if ( !vc ) return;
	GU_Clip2Plane( &clipping_frustum[CPLANE_RIGHT],  work_buffer[1], vc, work_buffer[0], &vc );
	if ( !vc ) return;
	*cv = extGuBeginPacket( NULL ); // uncached
	GU_Clip2Plane( &clipping_frustum[CPLANE_TOP],    work_buffer[0], vc, *cv, cvc );
	if (!( *cvc )) return;
	extGuEndPacket(( void * )( *cv + *cvc ));
}