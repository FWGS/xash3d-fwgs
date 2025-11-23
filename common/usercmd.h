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

#ifndef USER_CMD_H
#define USER_CMD_H
#include "xash3d_types.h"

typedef struct usercmd_s usercmd_t;

struct usercmd_s {
	short int                  lerp_msec;            /*     0     2 */
	byte                       msec;                 /*     2     1 */

	/* XXX 1 byte hole, try to pack */

	vec3_t                     viewangles;           /*     4    12 */
	float                      forwardmove;          /*    16     4 */
	float                      sidemove;             /*    20     4 */
	float                      upmove;               /*    24     4 */
	byte                       lightlevel;           /*    28     1 */

	/* XXX 1 byte hole, try to pack */

	short unsigned int         buttons;              /*    30     2 */
	byte                       impulse;              /*    32     1 */
	byte                       weaponselect;         /*    33     1 */

	/* XXX 2 bytes hole, try to pack */

	int                        impact_index;         /*    36     4 */
	vec3_t                     impact_position;      /*    40    12 */

	/* size: 52, cachelines: 1, members: 12 */
	/* sum members: 48, holes: 3, sum holes: 4 */
	/* last cacheline: 52 bytes */
};

#endif

