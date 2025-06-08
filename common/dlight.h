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

#ifndef DLIGHT_H
#define DLIGHT_H
#include "xash3d_types.h"
#include "const.h"

typedef struct dlight_s dlight_t;

struct dlight_s {
	vec3_t                     origin;               /*     0    12 */
	float                      radius;               /*    12     4 */
	color24                    color;                /*    16     3 */

	/* XXX 1 byte hole, try to pack */

	float                      die;                  /*    20     4 */
	float                      decay;                /*    24     4 */
	float                      minlight;             /*    28     4 */
	int                        key;                  /*    32     4 */
	qboolean                   dark;                 /*    36     4 */

	/* size: 40, cachelines: 1, members: 8 */
	/* sum members: 39, holes: 1, sum holes: 1 */
	/* last cacheline: 40 bytes */
};

#endif
