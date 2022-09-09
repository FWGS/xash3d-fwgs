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
static ScePspFMatrix4	projection_view_matrix __attribute__( ( aligned( 16 ) ) );
static frustum_t	projection_view_frustum __attribute__( ( aligned( 16 ) ) );
static frustum_t	clipping_frustum __attribute__( ( aligned( 16 ) ) );

// The temporary working buffers.
static gu_vert_t	work_buffer[2][MAX_CLIPPED_VERTICES] __attribute__( ( aligned( 16 ) ) );

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
		"lv.q		C100,  0(%4)\n"			// C000 = matrix->x
		"lv.q		C110, 16(%4)\n"			// C010 = matrix->y
		"lv.q		C120, 32(%4)\n"			// C020 = matrix->z
		"lv.q		C130, 48(%4)\n"			// C030 = matrix->w
		"vzero.q	C210\n"				// C000 = 0
		"vadd.q		C000, R103, R101\n"		// C000 = R103 + R101                                   ( BOTTOM )
		"vdot.q		S200, C000, C000\n"		// S110 = S100*S100 + S101*S101 + S102*S102 + S103*S103 ( BOTTOM )
		"vadd.q		C010, R103, R100\n"		// C010 = R103 + R100                                   ( LEFT )
		"vdot.q		S201, C010, C010\n"		// S110 = S100*S100 + S101*S101 + S102*S102 + S103*S103 ( LEFT )
		"vsub.q		C020, R103, R100\n"		// C020 = R103 - R100                                   ( RIGHT )
		"vdot.q		S202, C020, C020\n"		// S110 = S100*S100 + S101*S101 + S102*S102 + S103*S103 ( RIGHT )
		"vsub.q		C030, R103, R101\n"		// C030 = R103 - R101                                   ( TOP )
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
qboolean GU_ClipIsRequired( const gu_vert_t* v, size_t vc )
{
	int res;
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"move		$8,   %1\n"			// $8 = &v[0]
		"move		$9,   %2\n"			// $9 = vc
		"li		$10,  20\n"			// $10 = 20( sizeof( gu_vert_t ) )
		"mul		$10,  $10,  $9\n"		// $10 = $10 * $9
		"addu		$10,  $10,  $8\n"		// $10 = $10 + $8
		"addiu		%0,   $0,   1\n"		// res = 1
		"vzero.q	C600\n"				// C600 = [0.0f, 0.0f, 0.0f. 0.0f]
	"0:\n"							// loop
		"lv.s		S610,  8($8)\n"			// S610 = v[i].xyz[0]
		"lv.s		S611,  12($8)\n"		// S611 = v[i].xyz[1]
		"lv.s		S612,  16($8)\n"		// S612 = v[i].xyz[2]
		"vhtfm4.q	C620, M700, C610\n"		// C620 = frustrum * v[i].xyz
		"vcmp.q		LT,   C620, C600\n"		// S620 < 0.0f || S621 < 0.0f || S622 < 0.0f || S623 < 0.0f
		"bvt		4,    1f\n"			// if ( CC[4] == 1 ) jump to exit
		"addiu		$8,   $8,   20\n"		// $8 = $8 + 20( sizeof( gu_vert_t ) )	( delay slot )
		"bne		$10,  $8,   0b\n"		// if ( $10 != $8 ) jump to loop
		"nop\n"						// 								( delay slot )
		"move		%0,   $0\n"			// res = 0
	"1:\n"							// exit
		".set		pop\n"				// Restore assembler option
		:	"=r"( res )
		:	"r"( v ), "r"( vc )
		:	"$8", "$9", "$10"
	);
	return res;
}

/*
=================
GU_Clip2Plane

Clips a polygon against a plane.
=================
*/
_inline void GU_Clip2Plane( plane_type *plane, gu_vert_t *uv, size_t uvc, gu_vert_t *cv, size_t *cvc )
{
	__asm__ (
		".set		push\n"				// save assembler option
		".set		noreorder\n"			// suppress reordering
		"move		$8,   %1\n"			// $8 = uv[0] is S
		"addiu		$9,   $8,   20\n" 		// $9 = uv[1] is P
		"move		$10,  %2\n"			// $10 = uvc
		"li		$11,  20\n"			// $11 = 20( sizeof( gu_vert_t ) )
		"mul		$11,  $11,  $10\n"		// $11 = $11 * $10
		"addu		$11,  $11,  $8\n"		// $11 = $11 + $8
		"move		$12,  %3\n"			// $12 = &cv[0]
		"move		%0,   $0\n"			// cvc = 0
		"ulv.q		C100, %4\n"			// Load plane into register
		"vzero.s	S110\n"				// Set zero for cmp
	"0:\n"							// loop
		"lv.s		S000,  0($8)\n"			// Load vertex S TEX(4b) U into register
		"lv.s		S001,  4($8)\n"			// Load vertex S TEX(4b) V into register
		"lv.s		S010,  8($8)\n"			// Load vertex S XYZ(4b) X into register
		"lv.s		S011,  12($8)\n"		// Load vertex S XYZ(4b) Y into register
		"lv.s		S012,  16($8)\n"		// Load vertex S XYZ(4b) Z into register
		"lv.s		S020,  0($9)\n"			// Load vertex P TEX(4b) U into register
		"lv.s		S021,  4($9)\n"			// Load vertex P TEX(4b) V into register
		"lv.s		S030,  8($9)\n"			// Load vertex P XYZ(4b) X into register
		"lv.s		S031,  12($9)\n"		// Load vertex P XYZ(4b) Y into register
		"lv.s		S032,  16($9)\n"		// Load vertex P XYZ(4b) Z into register
		"vhdp.q		S111, C010, C100\n"		// S111 = plane * S (S DIST)
		"vhdp.q		S112, C030, C100\n"		// S112 = plane * P (P DIST)
		"vcmp.s		LE,   S111, S110\n"		// (S dist <= 0.0f)
		"bvt		0,    2f\n"			// if ( CC[0] == 1 ) jump to 2f
		"nop\n"						// 								( delay slot )
		"vcmp.s		LE,   S112, S110\n"		// (P dist <= 0.0f)
		"bvt		0,    1f\n"			// if ( CC[0] == 1 ) jump to 1f
		"nop\n"						// 								( delay slot )
		"sv.s		S020,  0($12)\n"		// cv->uv[0]  = C020 TEX U
		"sv.s		S021,  4($12)\n"		// cv->uv[1]  = C021 TEX V
		"sv.s		S030,  8($12)\n"		// cv->xyz[0] = C030 XYZ X
		"sv.s		S031, 12($12)\n"		// cv->xyz[1] = C031 XYZ Y
		"sv.s		S032, 16($12)\n"		// cv->xyz[2] = C032 XYZ Z
		"addiu		$12,  $12,  20\n"		// $12 = $12 + 20( sizeof( gu_vert_t ) )
		"j		3f\n"				// jump to next
		"addiu		%0,   %0,	1\n"		// cvc += 1							( delay slot )
	"1:\n"
		"vsub.s		S112, S111, S112\n"		// S112 = ( S dist - P dist )
		"vdiv.s		S112, S111, S112\n"		// S112 = S111 / S112
		"vsub.p		C120, C020, C000\n"		// C120 = C020(P TEX) - C000(S TEX)
		"vsub.t		C130, C030, C010\n"		// C130 = C030(P XYZ) - C010(S XYZ)
		"vscl.p		C120, C120, S112\n"		// C120 = C020 * S112
		"vscl.t		C130, C130, S112\n"		// C130 = C030 * S112
		"vadd.p		C120, C120, C000\n"		// C120 = C120 + C000(S TEX)
		"vadd.t		C130, C130, C010\n"		// C130 = C130 + C010(S XYZ)
		"sv.s		S120,  0($12)\n"		// cv->uv[0]  = S120 TEX U
		"sv.s		S121,  4($12)\n"		// cv->uv[1]  = S121 TEX V
		"sv.s		S130,  8($12)\n"		// cv->xyz[0] = S130 XYZ X
		"sv.s		S131, 12($12)\n"		// cv->xyz[1] = S131 XYZ Y
		"sv.s		S132, 16($12)\n"		// cv->xyz[2] = S132 XYZ Z
		"addiu		$12,  $12,  20\n"		// $12 = $12 + 20( sizeof( gu_vert_t ) )
		"j		3f\n"				// jump to next
		"addiu		%0,   %0,	1\n"		// cvc += 1							( delay slot )
	"2:\n"
		"vcmp.s		LE,   S112, S110\n"		// (P dist <= 0.0f)
		"bvt		0,    3f\n"			// if ( CC[0] == 1 ) jump to next
		"nop\n"						// 								( delay slot )
		"vsub.s		S112, S111, S112\n"		// S112 = ( S dist - P dist )
		"vdiv.s		S112, S111, S112\n"		// S112 = S111 / S112
		"vsub.p		C120, C020, C000\n"		// C120 = C020(P TEX) - C000(S TEX)
		"vsub.t		C130, C030, C010\n"		// C130 = C030(P XYZ) - C010(S XYZ)
		"vscl.p		C120, C120, S112\n"		// C120 = C020 * S112
		"vscl.t		C130, C130, S112\n"		// C130 = C030 * S112
		"vadd.p		C120, C120, C000\n"		// C120 = C120 + C000(S TEX)
		"vadd.t		C130, C130, C010\n"		// C130 = C130 + C010(S XYZ)
		"sv.s		S120,  0($12)\n"		// cv->uv[0]  = S120 TEX U
		"sv.s		S121,  4($12)\n"		// cv->uv[1]  = S121 TEX V
		"sv.s		S130,  8($12)\n"		// cv->xyz[0] = S130 XYZ X
		"sv.s		S131, 12($12)\n"		// cv->xyz[1] = S131 XYZ Y
		"sv.s		S132, 16($12)\n"		// cv->xyz[2] = S132 XYZ Z
		"addiu		$12,  $12,  20\n"		// $12 = $12 + 20( sizeof( gu_vert_t ) )
		"addiu		%0,   %0,	1\n"		// cvc += 1
		"sv.s		S020,  0($12)\n"		// cv->uv[0]  = S020 TEX U
		"sv.s		S021,  4($12)\n"		// cv->uv[1]  = S021 TEX V
		"sv.s		S030,  8($12)\n"		// cv->xyz[0] = S030 XYZ X
		"sv.s		S031, 12($12)\n"		// cv->xyz[1] = S031 XYZ Y
		"sv.s		S032, 16($12)\n"		// cv->xyz[2] = S032 XYZ Z
		"addiu		$12,  $12,  20\n"		// $12 = $12 + 20( sizeof( gu_vert_t ) )
		"addiu		%0,   %0,	1\n"		// cvc += 1
	"3:\n"							// next
		"addiu		$8,   $8,   20\n"		// $8 = $8 + 20( sizeof( gu_vert_t ) )
		"addiu		$9,   $8,   20\n" 		// $9 = $8 + 20( sizeof( gu_vert_t ) )
		"bne		$9,   $11,  4f\n"		// if ( $11 != $9 ) jump to next
		"nop\n"
		"move		$9,   %1\n"			// $9 = &uv[0] set P
	"4:\n"
		"bne		$11,  $8,   0b\n"		// if ( $11 != $8 ) jump to loop
		"nop\n"						// 								( delay slot )
		".set		pop\n"				// Restore assembler option
		:	"+r"( *cvc )
		:	"r"( uv ), "r"( uvc ), "r"( cv ), "m"( *plane )
		:	"$8", "$9", "$10", "$11", "$12"
	);
}

/*
=================
GU_Clip

Clips a polygon against the frustum
=================
*/
void GU_Clip( gu_vert_t *uv, size_t uvc, gu_vert_t **cv, size_t* cvc )
{
	size_t	vc;

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
	if ( !cvc ) return;
	extGuEndPacket(( void * )( *cv + *cvc ));
}