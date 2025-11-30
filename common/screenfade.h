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

#ifndef SCREEN_FADE_H
#define SCREEN_FADE_H
#include "xash3d_types.h"

typedef struct screenfade_s screenfade_t;

struct screenfade_s {
	float                      fadeSpeed;            /*     0     4 */
	float                      fadeEnd;              /*     4     4 */
	float                      fadeTotalEnd;         /*     8     4 */
	float                      fadeReset;            /*    12     4 */
	byte                       fader;                /*    16     1 */
	byte                       fadeg;                /*    17     1 */
	byte                       fadeb;                /*    18     1 */
	byte                       fadealpha;            /*    19     1 */
	int                        fadeFlags;            /*    20     4 */

	/* size: 24, cachelines: 1, members: 9 */
	/* last cacheline: 24 bytes */
};

#endif
