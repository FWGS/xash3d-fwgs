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

#ifndef BEAM_DEF_H
#define BEAM_DEF_H

#include "xash3d_types.h"

enum 
{
    FBEAM_STARTENTITY = 1 << 0,
    FBEAM_ENDENTITY = 1 << 1,
    FBEAM_FADEIN = 1 << 2,
    FBEAM_FADEOUT = 1<< 3,
    FBEAM_SINENOISE	= 1 << 4,
    FBEAM_SOLID	= 1 << 5,
    FBEAM_SHADEIN = 1 << 6,
    FBEAM_SHADEOUT = 1 << 7,
    FBEAM_STARTVISIBLE = 1 << 28,
    FBEAM_ENDVISIBLE = 1 << 29,
    FBEAM_ISACTIVE = 1 << 30,
    FBEAM_FOREVER = 1 << 31
};

typedef struct beam_s BEAM;

struct beam_s {
	BEAM *                     next;                 /*     0     4 */
	int                        type;                 /*     4     4 */
	int                        flags;                /*     8     4 */
	vec3_t                     source;               /*    12    12 */
	vec3_t                     target;               /*    24    12 */
	vec3_t                     delta;                /*    36    12 */
	float                      t;                    /*    48     4 */
	float                      freq;                 /*    52     4 */
	float                      die;                  /*    56     4 */
	float                      width;                /*    60     4 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	float                      amplitude;            /*    64     4 */
	float                      r;                    /*    68     4 */
	float                      g;                    /*    72     4 */
	float                      b;                    /*    76     4 */
	float                      brightness;           /*    80     4 */
	float                      speed;                /*    84     4 */
	float                      frameRate;            /*    88     4 */
	float                      frame;                /*    92     4 */
	int                        segments;             /*    96     4 */
	int                        startEntity;          /*   100     4 */
	int                        endEntity;            /*   104     4 */
	int                        modelIndex;           /*   108     4 */
	int                        frameCount;           /*   112     4 */
	struct model_s *           pFollowModel;         /*   116     4 */
	struct particle_s *        particles;            /*   120     4 */

	/* size: 124, cachelines: 2, members: 25 */
	/* last cacheline: 60 bytes */
};

#endif
