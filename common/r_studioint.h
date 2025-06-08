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

#ifndef R_STUDIO_INTERFACE_H
#define R_STUDIO_INTERFACE_H

#define SV_BLENDING_INTERFACE_VERSION 1
#define STUDIO_INTERFACE_VERSION 1

typedef struct engine_studio_api_s engine_studio_api_t;
typedef struct server_studio_api_s server_studio_api_t;
typedef struct r_studio_interface_s r_studio_interface_t;
typedef struct sv_blending_interface_s sv_blending_interface_t;

struct engine_studio_api_s {
	void *                     (*Mem_Calloc)(int, size_t); /*     0     4 */
	void *                     (*Cache_Check)(struct cache_user_s *); /*     4     4 */
	void                       (*LoadCacheFile)(char *, struct cache_user_s *); /*     8     4 */
	struct model_s *           (*Mod_ForName)(const char  *, int); /*    12     4 */
	void *                     (*Mod_Extradata)(struct model_s *); /*    16     4 */
	struct model_s *           (*GetModelByIndex)(int); /*    20     4 */
	struct cl_entity_s *       (*GetCurrentEntity)(void); /*    24     4 */
	struct player_info_s *     (*PlayerInfo)(int);   /*    28     4 */
	struct entity_state_s *    (*GetPlayerState)(int); /*    32     4 */
	struct cl_entity_s *       (*GetViewEntity)(void); /*    36     4 */
	void                       (*GetTimes)(int *, double *, double *); /*    40     4 */
	struct cvar_s *            (*GetCvar)(const char  *); /*    44     4 */
	void                       (*GetViewInfo)(float *, float *, float *, float *); /*    48     4 */
	struct model_s *           (*GetChromeSprite)(void); /*    52     4 */
	void                       (*GetModelCounters)(int * *, int * *); /*    56     4 */
	void                       (*GetAliasScale)(float *, float *); /*    60     4 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	float * * * *              (*StudioGetBoneTransform)(void); /*    64     4 */
	float * * * *              (*StudioGetLightTransform)(void); /*    68     4 */
	float * * *                (*StudioGetAliasTransform)(void); /*    72     4 */
	float * * *                (*StudioGetRotationMatrix)(void); /*    76     4 */
	void                       (*StudioSetupModel)(int, void * *, void * *); /*    80     4 */
	int                        (*StudioCheckBBox)(void); /*    84     4 */
	void                       (*StudioDynamicLight)(struct cl_entity_s *, struct alight_s *); /*    88     4 */
	void                       (*StudioEntityLight)(struct alight_s *); /*    92     4 */
	void                       (*StudioSetupLighting)(struct alight_s *); /*    96     4 */
	void                       (*StudioDrawPoints)(void); /*   100     4 */
	void                       (*StudioDrawHulls)(void); /*   104     4 */
	void                       (*StudioDrawAbsBBox)(void); /*   108     4 */
	void                       (*StudioDrawBones)(void); /*   112     4 */
	void                       (*StudioSetupSkin)(void *, int); /*   116     4 */
	void                       (*StudioSetRemapColors)(int, int); /*   120     4 */
	struct model_s *           (*SetupPlayerModel)(int); /*   124     4 */
	/* --- cacheline 2 boundary (128 bytes) --- */
	void                       (*StudioClientEvents)(void); /*   128     4 */
	int                        (*GetForceFaceFlags)(void); /*   132     4 */
	void                       (*SetForceFaceFlags)(int); /*   136     4 */
	void                       (*StudioSetHeader)(void *); /*   140     4 */
	void                       (*SetRenderModel)(struct model_s *); /*   144     4 */
	void                       (*SetupRenderer)(int); /*   148     4 */
	void                       (*RestoreRenderer)(void); /*   152     4 */
	void                       (*SetChromeOrigin)(void); /*   156     4 */
	int                        (*IsHardware)(void);  /*   160     4 */
	void                       (*GL_StudioDrawShadow)(void); /*   164     4 */
	void                       (*GL_SetRenderMode)(int); /*   168     4 */
	void                       (*StudioSetRenderamt)(int); /*   172     4 */
	void                       (*StudioSetCullState)(int); /*   176     4 */
	void                       (*StudioRenderShadow)(int, float *, float *, float *, float *); /*   180     4 */

	/* size: 184, cachelines: 3, members: 46 */
	/* last cacheline: 56 bytes */
};

struct server_studio_api_s {
	void *                     (*Mem_Calloc)(int, size_t); /*     0     4 */
	void *                     (*Cache_Check)(struct cache_user_s *); /*     4     4 */
	void                       (*LoadCacheFile)(char *, struct cache_user_s *); /*     8     4 */
	void *                     (*Mod_Extradata)(struct model_s *); /*    12     4 */

	/* size: 16, cachelines: 1, members: 4 */
	/* last cacheline: 16 bytes */
};

struct r_studio_interface_s {
	int                        version;              /*     0     4 */
	int                        (*StudioDrawModel)(int); /*     4     4 */
	int                        (*StudioDrawPlayer)(int, struct entity_state_s *); /*     8     4 */

	/* size: 12, cachelines: 1, members: 3 */
	/* last cacheline: 12 bytes */
};

struct sv_blending_interface_s {
	int                        version;              /*     0     4 */
	void                       (*SV_StudioSetupBones)(struct model_s *, float, int, const vec_t  *, const vec_t  *, const unsigned char  *, const unsigned char  *, int, const edict_t  *); /*     4     4 */

	/* size: 8, cachelines: 1, members: 2 */
	/* last cacheline: 8 bytes */
};

#endif

