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

#ifndef ENTITY_STATE_H
#define ENTITY_STATE_H

#include "xash3d_types.h"
#include "const.h"
#include "pm_info.h"
#include "weaponinfo.h"

#define MAX_LOCAL_WEAPONS 64

enum
{
    ENTITY_NORMAL = 1 << 0,
    ENTITY_BEAM = 1 << 1
};

typedef struct entity_state_s entity_state_t;
typedef struct clientdata_s clientdata_t;
typedef struct local_state_s local_state_t;

struct entity_state_s {
	int                        entityType;           /*     0     4 */
	int                        number;               /*     4     4 */
	float                      msg_time;             /*     8     4 */
	int                        messagenum;           /*    12     4 */
	vec3_t                     origin;               /*    16    12 */
	vec3_t                     angles;               /*    28    12 */
	int                        modelindex;           /*    40     4 */
	int                        sequence;             /*    44     4 */
	float                      frame;                /*    48     4 */
	int                        colormap;             /*    52     4 */
	short int                  skin;                 /*    56     2 */
	short int                  solid;                /*    58     2 */
	int                        effects;              /*    60     4 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	float                      scale;                /*    64     4 */
	byte                       eflags;               /*    68     1 */
	int                        rendermode;           /*    72     4 */
	int                        renderamt;            /*    76     4 */
	color24                    rendercolor;          /*    80     3 */
	int                        renderfx;             /*    84     4 */
	int                        movetype;             /*    88     4 */
	float                      animtime;             /*    92     4 */
	float                      framerate;            /*    96     4 */
	int                        body;                 /*   100     4 */
	byte                       controller[4];        /*   104     4 */
	byte                       blending[4];          /*   108     4 */
	vec3_t                     velocity;             /*   112    12 */
	vec3_t                     mins;                 /*   124    12 */
	/* --- cacheline 2 boundary (128 bytes) was 8 bytes ago --- */
	vec3_t                     maxs;                 /*   136    12 */
	int                        aiment;               /*   148     4 */
	int                        owner;                /*   152     4 */
	float                      friction;             /*   156     4 */
	float                      gravity;              /*   160     4 */
	int                        team;                 /*   164     4 */
	int                        playerclass;          /*   168     4 */
	int                        health;               /*   172     4 */
	qboolean                   spectator;            /*   176     4 */
	int                        weaponmodel;          /*   180     4 */
	int                        gaitsequence;         /*   184     4 */
	vec3_t                     basevelocity;         /*   188    12 */
	/* --- cacheline 3 boundary (192 bytes) was 8 bytes ago --- */
	int                        usehull;              /*   200     4 */
	int                        oldbuttons;           /*   204     4 */
	int                        onground;             /*   208     4 */
	int                        iStepLeft;            /*   212     4 */
	float                      flFallVelocity;       /*   216     4 */
	float                      fov;                  /*   220     4 */
	int                        weaponanim;           /*   224     4 */
	vec3_t                     startpos;             /*   228    12 */
	vec3_t                     endpos;               /*   240    12 */
	float                      impacttime;           /*   252     4 */
	/* --- cacheline 4 boundary (256 bytes) --- */
	float                      starttime;            /*   256     4 */
	int                        iuser1;               /*   260     4 */
	int                        iuser2;               /*   264     4 */
	int                        iuser3;               /*   268     4 */
	int                        iuser4;               /*   272     4 */
	float                      fuser1;               /*   276     4 */
	float                      fuser2;               /*   280     4 */
	float                      fuser3;               /*   284     4 */
	float                      fuser4;               /*   288     4 */
	vec3_t                     vuser1;               /*   292    12 */
	vec3_t                     vuser2;               /*   304    12 */
	vec3_t                     vuser3;               /*   316    12 */
	/* --- cacheline 5 boundary (320 bytes) was 8 bytes ago --- */
	vec3_t                     vuser4;               /*   328    12 */

	/* size: 340, cachelines: 6, members: 62 */
	/* sum members: 336, holes: 2, sum holes: 4 */
	/* last cacheline: 20 bytes */
};

struct clientdata_s {
	vec3_t                     origin;               /*     0    12 */
	vec3_t                     velocity;             /*    12    12 */
	int                        viewmodel;            /*    24     4 */
	vec3_t                     punchangle;           /*    28    12 */
	int                        flags;                /*    40     4 */
	int                        waterlevel;           /*    44     4 */
	int                        watertype;            /*    48     4 */
	vec3_t                     view_ofs;             /*    52    12 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	float                      health;               /*    64     4 */
	int                        bInDuck;              /*    68     4 */
	int                        weapons;              /*    72     4 */
	int                        flTimeStepSound;      /*    76     4 */
	int                        flDuckTime;           /*    80     4 */
	int                        flSwimTime;           /*    84     4 */
	int                        waterjumptime;        /*    88     4 */
	float                      maxspeed;             /*    92     4 */
	float                      fov;                  /*    96     4 */
	int                        weaponanim;           /*   100     4 */
	int                        m_iId;                /*   104     4 */
	int                        ammo_shells;          /*   108     4 */
	int                        ammo_nails;           /*   112     4 */
	int                        ammo_cells;           /*   116     4 */
	int                        ammo_rockets;         /*   120     4 */
	float                      m_flNextAttack;       /*   124     4 */
	/* --- cacheline 2 boundary (128 bytes) --- */
	int                        tfstate;              /*   128     4 */
	int                        pushmsec;             /*   132     4 */
	int                        deadflag;             /*   136     4 */
	char       	               physinfo[MAX_PHYSINFO_STRING];
	/* --- cacheline 6 boundary (384 bytes) was 12 bytes ago --- */
	int                        iuser1;               /*   396     4 */
	int                        iuser2;               /*   400     4 */
	int                        iuser3;               /*   404     4 */
	int                        iuser4;               /*   408     4 */
	float                      fuser1;               /*   412     4 */
	float                      fuser2;               /*   416     4 */
	float                      fuser3;               /*   420     4 */
	float                      fuser4;               /*   424     4 */
	vec3_t                     vuser1;               /*   428    12 */
	vec3_t                     vuser2;               /*   440    12 */
	/* --- cacheline 7 boundary (448 bytes) was 4 bytes ago --- */
	vec3_t                     vuser3;               /*   452    12 */
	vec3_t                     vuser4;               /*   464    12 */

	/* size: 476, cachelines: 8, members: 40 */
	/* last cacheline: 28 bytes */
};

struct local_state_s {
	entity_state_t             playerstate;          /*     0   340 */
	/* --- cacheline 5 boundary (320 bytes) was 20 bytes ago --- */
	clientdata_t               client;               /*   340   476 */
	/* --- cacheline 12 boundary (768 bytes) was 48 bytes ago --- */
	weapon_data_t              weapondata[64];       /*   816  5632 */

	/* size: 6448, cachelines: 101, members: 3 */
	/* last cacheline: 48 bytes */
};

#endif
