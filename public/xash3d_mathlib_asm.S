/*
xash3d_mathlib_asm.S - internal mathlib ASM ver.
Copyright (C) 2022 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "build.h"
#if XASH_PSP
#include "as_reg_compat.h"
	.set		noreorder
	.set		noat

	.text
	.align		4
	.global		QuaternionSlerp
	.global		BoxOnPlaneSide

// void QuaternionSlerp( const vec4_t p, const vec4_t q, float t, vec4_t qt );
	.ent		QuaternionSlerp
QuaternionSlerp:
	############################
	#       a0  - IN  *p       #
	#       a1  - IN  *q       #
	#       f12 - IN  t        #
	#       a2  - IN  *qt      #
	############################
	mfc1		$t0, $f12
	mtv		$t0, S031			// S031 = sclq = t
	li		$t0, 0x358637bd			// t0 = EPSILON = 0.000001f
	mtv		$t0, S033			// S002 = t0 = EPSILON
	lv.q		C010, 0($a0)			// C010 = p
	lv.q		C020, 0($a1)			// C020 = q

	// QuaternionAlign
	vsub.q		C100, C010, C020		// C100 = p[*] - q[*]
	vadd.q		C110, C010, C020		// C110 = p[*] + q[*]
	vdot.q		S000, C100, C100		// S000 = a += (p[*] - q[*]) * (p[*] - q[*])
	vdot.q		S001, C110, C110		// S001 = b += (p[*] + q[*]) * (p[*] + q[*])
	vcmp.s		GT, S000, S001			// CC[0] = a > b
	vcmovt.q	C020, C020[-X,-Y,-Z,-W], 0	// if CC[0] q = -q
	// ***

	// QuaternionSlerpNoAlign
	vdot.q		S000, C010, C020		// S000 = cosom += p[*] * q[*]
	vadd.s		S001, S000[1], S000		// S002 = 1.0f + cosom
	vcmp.s		LE, S001, S033			// CC[0] = ( 1.0f + cosom ) <= 0.000001f
	bvt		0, Lqs2				// if CC[0] goto Lqs2
	vocp.s		S030, S031			// S030 = sclp = 1.0f - t                       (delay slot)
	vsub.s		S001, S000[1], S000		// S002 = 1.0f - cosom
	vcmp.s		LE, S001, S033			// CC[0] = ( 1.0f - cosom ) <= 0.000001f
	bvt		0, Lqs1
	nop

	// acos
	vcst.s		S001, VFPU_SQRT1_2		// S001 = VFPU_SQRT1_2 = 1 / sqrt(2)
	vcmp.s		LT, S000[|x|], S001		// CC[0] = abs(cosom) < (1 / sqrt(2))
	vasin.s		S032, S000[|x|]			// S032 = asin(abs(cosom))
	bvtl		0, Lqs0				// if CC[0] goto Lqs0
	vocp.s		S032, S032			// S032 = 1 - S032 = acos(abs(cosom)) = omega   (bvtl delay slot)
	vmul.s		S001, S000, S000		// S001 = cosom * cosom
	vocp.s		S001[0:1], S001			// S001 = 1 - S001[0:1]
	vsqrt.s		S001, S001			// S001 = sqrt(S001)
	vasin.s		S032, S001			// S032 = asin(S001)  = acos(abs(cosom)) = omega
	// ***
Lqs0:
	vscl.p		C030, C030, S032		// S030 = S030 * S032 = sclp * omega
							// S031 = S031 * S032 = sclq * omega
	vsin.t		C030, C030			// S030 = sin(S030)   = sin(sclp * omega) = sclp
							// S031 = sin(S031)   = sin(sclq * omega) = sclq
							// S032 = sin(S032)   = sin(omega)
	vrcp.s		S032, S032			// S032 = 1.0f / S032 = 1 / sin(omega) = sinom
	vscl.p		C030, C030, S032		// S030 = S030 * S032 = sin(sclp * omega) / sinom
							// S031 = S031 * S032 = sin(sclq * omega) / sinom
Lqs1:
	vscl.q		C010, C010, S030		// C010 = p[*4] * sclp
	vscl.q		C020, C020, S031		// C020 = qt[*4] * sclq
	b		LqsEnd				// goto LqsEnd
	vadd.q		C000, C010, C020		// S000 = qt[0] = sclp * p[0] + sclq * qt[0]    (delay slot)
							// S001 = qt[1] = sclp * p[1] + sclq * qt[1]
							// S002 = qt[2] = sclp * p[2] + sclq * qt[2]
							// S003 = qt[3] = sclp * p[3] + sclq * qt[3]
Lqs2:
	vmov.q		C000, C020[-Y,X,-W,Z]		// S000 = qt[0] = -q[1];
							// S001 = qt[1] = q[0];
							// S002 = qt[2] = -q[3];
							// S003 = qt[3] = q[2];
	vsin.p		C030, C030			// S030 = sclp = sin(( 1.0f - t ) * ( 0.5f * M_PI_F ))
							// S031 = sclq = sin( t * ( 0.5f * M_PI_F ))
	vscl.t		C010, C010, S030		// C000 = p[*3] * sclp
	vscl.t		C020, C000, S031		// C030 = qt[*3] * sclq
	vadd.t		C000, C010, C020		// S000 = qt[0] = sclp * p[0] + sclq * qt[0]
							// S001 = qt[1] = sclp * p[1] + sclq * qt[1]
							// S002 = qt[2] = sclp * p[2] + sclq * qt[2]
							// S003 = qt[3]
LqsEnd:
	sv.q		C000, 0($a2)
	jr		$ra
	.end		QuaternionSlerp




// int BoxOnPlaneSide( vec3_t emins, vec3_t emaxs, mplane_t *p );
	.ent		BoxOnPlaneSide
BoxOnPlaneSide:
	############################
	#       a0 - IN  *emins    #
	#       a1 - IN  *emaxs    #
	#       a2 - IN  *p        #
	#       v0 - OUT sides     #
	############################
	lbu		$v0, 17($a2)		// p->signbits
	sltiu		$v1, $v0, 8		// if(signbits > 8)
	beq		$v1, $zero, LSetSides	// jump to LSetSides
	vzero.p		C030			// set zero vector
	sll		$v1, $v0, 2
	la		$v0, Ljmptab
	addu		$v0, $v0, $v1
	lw		$v0, 0($v0)
	lv.s		S000, 0($a2)		// p->normal[0]
	lv.s		S001, 4($a2)		// p->normal[1]
	jr		$v0
	lv.s		S002, 8($a2)		// p->normal[2]
Lcase0:
	lv.s		S010, 0($a1)		// emaxs[0]
	lv.s		S011, 4($a1)		// emaxs[1]
	lv.s		S012, 8($a1)		// emaxs[2]
	lv.s		S020, 0($a0)		// emins[0]
	lv.s		S021, 4($a0)		// emins[1]
	b		LDotProduct
	lv.s		S022, 8($a0)		// emins[2]
Lcase1:
	lv.s		S010, 0($a0)		// emins[0]
	lv.s		S011, 4($a1)		// emaxs[1]
	lv.s		S012, 8($a1)		// emaxs[2]
	lv.s		S020, 0($a1)		// emaxs[0]
	lv.s		S021, 4($a0)		// emins[1]
	b		LDotProduct
	lv.s		S022, 8($a0)		// emins[2]
Lcase2:
	lv.s		S010, 0($a1)		// emaxs[0]
	lv.s		S011, 4($a0)		// emins[1]
	lv.s		S012, 8($a1)		// emaxs[2]
	lv.s		S020, 0($a0)		// emins[0]
	lv.s		S021, 4($a1)		// emaxs[1]
	b		LDotProduct
	lv.s		S022, 8($a0)		// emins[2]
Lcase3:
	lv.s		S010, 0($a0)		// emins[0]
	lv.s		S011, 4($a0)		// emins[1]
	lv.s		S012, 8($a1)		// emaxs[2]
	lv.s		S020, 0($a1)		// emaxs[0]
	lv.s		S021, 4($a1)		// emaxs[1]
	b		LDotProduct
	lv.s		S022, 8($a0)		// emins[2]
Lcase4:
	lv.s		S010, 0($a1)		// emaxs[0]
	lv.s		S011, 4($a1)		// emaxs[1]
	lv.s		S012, 8($a0)		// emins[2]
	lv.s		S020, 0($a0)		// emins[0]
	lv.s		S021, 4($a0)		// emins[1]
	b		LDotProduct
	lv.s		S022, 8($a1)		// emaxs[2]
Lcase5:
	lv.s		S010, 0($a0)		// emins[0]
	lv.s		S011, 4($a1)		// emaxs[1]
	lv.s		S012, 8($a0)		// emins[2]
	lv.s		S020, 0($a1)		// emaxs[0]
	lv.s		S021, 4($a0)		// emins[1]
	b		LDotProduct
	lv.s		S022, 8($a1)		// emaxs[2]
Lcase6:
	lv.s		S010, 0($a1)		// emaxs[0]
	lv.s		S011, 4($a0)		// emins[1]
	lv.s		S012, 8($a0)		// emins[2]
	lv.s		S020, 0($a0)		// emins[0]
	lv.s		S021, 4($a1)		// emaxs[1]
	b		LDotProduct
	lv.s		S022, 8($a1)		// emaxs[2]
Lcase7:
	lv.s		S010, 0($a0)		// emins[0]
	lv.s		S011, 4($a0)		// emins[1]
	lv.s		S012, 8($a0)		// emins[2]
	lv.s		S020, 0($a1)		// emaxs[0]
	lv.s		S021, 4($a1)		// emaxs[1]
	lv.s		S022, 8($a1)		// emaxs[2]
LDotProduct:
	vdot.t		S030, C000, C010	// S030 = C000 * C010
	vdot.t		S031, C000, C020	// S031 = C000 * C020
LSetSides:
	lv.s		S013, 12($a2)		// p->dist
	vcmp.s		LT, S030, S013		// S030 < S013
	bvt		0, LDist2		// if ( CC[0] == 1 ) jump to LDist2
	li		$v0, 0			// sides = 0
	li		$v0, 1			// sides = 1
LDist2:
	vcmp.s		GE, S031, S013		// S031 >= S013
	bvt		0, LEnd			// if ( CC[0] == 1 ) jump to LEnd
	nop
	ori		$v0, $v0, 2
LEnd:
	jr		$ra
	nop
	.end		BoxOnPlaneSide

	.section	.rodata
	.align		4
Ljmptab:
	.word		Lcase0, Lcase1, Lcase2, Lcase3, Lcase4, Lcase5, Lcase6, Lcase7
#endif // XASH_PSP
