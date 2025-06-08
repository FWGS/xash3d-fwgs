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

#ifndef QFONT_H
#define QFONT_H
#include "xash3d_types.h"

#define NUM_GLYPHS 256

typedef struct qfont_s qfont_t;
typedef struct charinfo_s charinfo;

struct charinfo_s {
	short                    startoffset;          /*     0     2 */
	short                    charwidth;            /*     2     2 */

	/* size: 4, cachelines: 1, members: 2 */
	/* last cacheline: 4 bytes */
};

struct qfont_s {
	int                        width;                /*     0     4 */
	int                        height;               /*     4     4 */
	int                        rowcount;             /*     8     4 */
	int                        rowheight;            /*    12     4 */
	charinfo                   fontinfo[256];        /*    16  1024 */
	/* --- cacheline 16 boundary (1024 bytes) was 16 bytes ago --- */
	byte                       data[4];              /*  1040     4 */

	/* size: 1044, cachelines: 17, members: 6 */
	/* last cacheline: 20 bytes */
};

#endif
