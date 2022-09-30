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
	.global		BoxOnPlaneSide

// int BoxOnPlaneSide( vec3_t emins, vec3_t emaxs, mplane_t *p );
	.ent		BoxOnPlaneSide
BoxOnPlaneSide:
	############################
	#       a0 - IN  emins     #
	#       a1 - IN  emaxs     #
	#       a2 - IN  p         #
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
