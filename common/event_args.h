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

#ifndef EVENT_ARGS_H
#define EVENT_ARGS_H

enum
{
	FEVENT_ORIGIN = 1 << 0,
	FEVENT_ANGLES = 1 << 1
};

typedef struct event_args_s event_args_t;

struct event_args_s {
	int                        flags;                /*     0     4 */
	int                        entindex;             /*     4     4 */
	float                      origin[3];            /*     8    12 */
	float                      angles[3];            /*    20    12 */
	float                      velocity[3];          /*    32    12 */
	int                        ducking;              /*    44     4 */
	float                      fparam1;              /*    48     4 */
	float                      fparam2;              /*    52     4 */
	int                        iparam1;              /*    56     4 */
	int                        iparam2;              /*    60     4 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	int                        bparam1;              /*    64     4 */
	int                        bparam2;              /*    68     4 */

	/* size: 72, cachelines: 2, members: 12 */
	/* last cacheline: 8 bytes */
};

#endif
