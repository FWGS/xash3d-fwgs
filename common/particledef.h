/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org>
*/

#ifndef PARTICLE_H
#define PARTICLE_H
#include "xash3d_types.h"

typedef struct particle_s particle_t;

typedef enum {
	pt_static       = 0,
	pt_grav         = 1,
	pt_slowgrav     = 2,
	pt_fire         = 3,
	pt_explode      = 4,
	pt_explode2     = 5,
	pt_blob         = 6,
	pt_blob2        = 7,
	pt_vox_slowgrav = 8,
	pt_vox_grav     = 9,
	pt_clientcustom = 10,
} ptype_t;

struct particle_s {
	vec3_t                     org;                  /*     0    12 */
	short int                  color;                /*    12     2 */
	short int                  packedColor;          /*    14     2 */
	struct particle_s *        next;                 /*    16     4 */
	vec3_t                     vel;                  /*    20    12 */
	float                      ramp;                 /*    32     4 */
	float                      die;                  /*    36     4 */
	ptype_t                    type;                 /*    40     4 */
	void                       (*deathfunc)(struct particle_s *); /*    44     4 */
	void                       (*callback)(struct particle_s *, float); /*    48     4 */
	unsigned char              context;              /*    52     1 */

	/* size: 56, cachelines: 1, members: 11 */
	/* padding: 3 */
	/* last cacheline: 56 bytes */
};

#endif
