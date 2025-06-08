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

#ifndef CL_ENTITY_H
#define CL_ENTITY_H
#include "xash3d_types.h"
#include "event_args.h"
#include "entity_state.h"

#define HISTORY_MAX 64
#define HISTORY_MASK (HISTORY_MAX - 1)

typedef struct mouth_s mouth_t;
typedef struct efrag_s efrag_t;
typedef struct cl_entity_s cl_entity_t;
typedef struct latchedvars_s latchedvars_t;
typedef struct position_history_s position_history_t;

struct mouth_s {
	byte                       mouthopen;            /*     0     1 */
	byte                       sndcount;             /*     1     1 */

	/* XXX 2 bytes hole, try to pack */

	int                        sndavg;               /*     4     4 */

	/* size: 8, cachelines: 1, members: 3 */
	/* sum members: 6, holes: 1, sum holes: 2 */
	/* last cacheline: 8 bytes */
};

struct efrag_s {
	struct mleaf_s *           leaf;                 /*     0     4 */
	struct efrag_s *           leafnext;             /*     4     4 */
	struct cl_entity_s *       entity;               /*     8     4 */
	struct efrag_s *           entnext;              /*    12     4 */

	/* size: 16, cachelines: 1, members: 4 */
	/* last cacheline: 16 bytes */
};

struct position_history_s {
	float                      animtime;             /*     0     4 */
	vec3_t                     origin;               /*     4    12 */
	vec3_t                     angles;               /*    16    12 */

	/* size: 28, cachelines: 1, members: 3 */
	/* last cacheline: 28 bytes */
};

struct latchedvars_s {
	float                      prevanimtime;         /*     0     4 */
	float                      sequencetime;         /*     4     4 */
	byte                       prevseqblending[2];   /*     8     2 */

	/* XXX 2 bytes hole, try to pack */

	vec3_t                     prevorigin;           /*    12    12 */
	vec3_t                     prevangles;           /*    24    12 */
	int                        prevsequence;         /*    36     4 */
	float                      prevframe;            /*    40     4 */
	byte                       prevcontroller[4];    /*    44     4 */
	byte                       prevblending[2];      /*    48     2 */

	/* size: 52, cachelines: 1, members: 9 */
	/* sum members: 48, holes: 1, sum holes: 2 */
	/* padding: 2 */
	/* last cacheline: 52 bytes */
};

struct cl_entity_s {
	int                        index;                /*     0     4 */
	qboolean                   player;               /*     4     4 */
	entity_state_t             baseline;             /*     8   340 */
	/* --- cacheline 5 boundary (320 bytes) was 28 bytes ago --- */
	entity_state_t             prevstate;            /*   348   340 */
	/* --- cacheline 10 boundary (640 bytes) was 48 bytes ago --- */
	entity_state_t             curstate;             /*   688   340 */
	/* --- cacheline 16 boundary (1024 bytes) was 4 bytes ago --- */
	int                        current_position;     /*  1028     4 */
	position_history_t         ph[HISTORY_MAX];      /*  1032  1792 */
	/* --- cacheline 44 boundary (2816 bytes) was 8 bytes ago --- */
	mouth_t                    mouth;                /*  2824     8 */
	latchedvars_t              latched;              /*  2832    52 */
	/* --- cacheline 45 boundary (2880 bytes) was 4 bytes ago --- */
	float                      lastmove;             /*  2884     4 */
	vec3_t                     origin;               /*  2888    12 */
	vec3_t                     angles;               /*  2900    12 */
	vec3_t                     attachment[4];     /*  2912    48 */
	/* --- cacheline 46 boundary (2944 bytes) was 16 bytes ago --- */
	int                        trivial_accept;       /*  2960     4 */
	struct model_s *           model;                /*  2964     4 */
	struct efrag_s *           efrag;                /*  2968     4 */
	struct mnode_s *           topnode;              /*  2972     4 */
	float                      syncbase;             /*  2976     4 */
	int                        visframe;             /*  2980     4 */
	colorVec                   cvFloorColor;         /*  2984    16 */

	/* size: 3000, cachelines: 47, members: 20 */
	/* last cacheline: 56 bytes */
};

#endif
