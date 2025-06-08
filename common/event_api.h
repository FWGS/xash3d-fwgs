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

#ifndef EVENT_API_H
#define EVENT_API_H

#define EVENT_API_VERSION 1
typedef struct event_api_s event_api_t;

struct event_api_s {
	int                        version;              /*     0     4 */

	/* XXX 4 bytes hole, try to pack */

	void                       (*EV_PlaySound)(int, float *, int, const char  *, float, float, int, int); /*     8     8 */
	void                       (*EV_StopSound)(int, int, const char  *); /*    16     8 */
	int                        (*EV_FindModelIndex)(const char  *); /*    24     8 */
	int                        (*EV_IsLocal)(int);   /*    32     8 */
	int                        (*EV_LocalPlayerDucking)(void); /*    40     8 */
	void                       (*EV_LocalPlayerViewheight)(float *); /*    48     8 */
	void                       (*EV_LocalPlayerBounds)(int, float *, float *); /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	int                        (*EV_IndexFromTrace)(struct pmtrace_s *); /*    64     8 */
	struct physent_s *         (*EV_GetPhysent)(int); /*    72     8 */
	void                       (*EV_SetUpPlayerPrediction)(int, int); /*    80     8 */
	void                       (*EV_PushPMStates)(void); /*    88     8 */
	void                       (*EV_PopPMStates)(void); /*    96     8 */
	void                       (*EV_SetSolidPlayers)(int); /*   104     8 */
	void                       (*EV_SetTraceHull)(int); /*   112     8 */
	void                       (*EV_PlayerTrace)(float *, float *, int, int, struct pmtrace_s *); /*   120     8 */
	/* --- cacheline 2 boundary (128 bytes) --- */
	void                       (*EV_WeaponAnimation)(int, int); /*   128     8 */
	short unsigned int         (*EV_PrecacheEvent)(int, const char  *); /*   136     8 */
	void                       (*EV_PlaybackEvent)(int, const struct edict_s  *, short unsigned int, float, float *, float *, float, float, int, int, int, int); /*   144     8 */
	const char  *              (*EV_TraceTexture)(int, float *, float *); /*   152     8 */
	void                       (*EV_StopAllSounds)(int, int); /*   160     8 */
	void                       (*EV_KillEvents)(int, const char  *); /*   168     8 */

	// Xash3D extensions
	void                       (*EV_PlayerTraceExt)(float *, float *, int, int (*)(struct physent_s *), struct pmtrace_s *); /*   176     8 */
	const char  *              (*EV_SoundForIndex)(int); /*   184     8 */
	/* --- cacheline 3 boundary (192 bytes) --- */
	struct msurface_s *        (*EV_TraceSurface)(int, float *, float *); /*   192     8 */
	struct movevars_s *        (*EV_GetMovevars)(void); /*   200     8 */
	struct pmtrace_s *         (*EV_VisTraceLine)(float *, float *, int); /*   208     8 */
	struct physent_s *         (*EV_GetVisent)(int); /*   216     8 */
	int                        (*EV_TestLine)(const vec_t  *, const vec_t  *, int); /*   224     8 */
	void                       (*EV_PushTraceBounds)(int, const float  *, const float  *); /*   232     8 */
	void                       (*EV_PopTraceBounds)(void); /*   240     8 */

	/* size: 248, cachelines: 4, members: 31 */
	/* sum members: 244, holes: 1, sum holes: 4 */
	/* last cacheline: 56 bytes */
};

#endif
