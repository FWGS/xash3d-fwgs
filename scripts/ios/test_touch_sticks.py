#!/usr/bin/env python3
"""Compile the engine's actual analog input functions in a small regression harness.

Requires an existing C compiler; no Python packages or iOS runtime are needed.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r'^[^\n;]*\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source, re.MULTILINE)
    if not match:
        raise ValueError('Function definition not found: ' + name)
    start = match.start()
    opening = match.end() - 1
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    source = (ROOT / 'engine/client/input/in_touch.c').read_text()
    declarations = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define Q_strncpy strncpy
#define TOUCH_FL_UNPRIVILEGED 1024
#define FBitSet(a,b) ((a)&(b))
#define Q_max(a,b) ((a)>(b)?(a):(b))
#define Q_min(a,b) ((a)<(b)?(a):(b))
#define bound(a,b,c) Q_min(Q_max(a,b),c)
#define false 0
#define true 1
typedef int qboolean;
enum { touch_movejoy=7, touch_lookjoy, touch_crouch, event_down=20,event_up, state_none=0, key_game=0 };
typedef int touchEventType;
typedef struct touch_button_s {
 int type,finger,flags,crouched; float x1,y1,x2,y2,stick_x,stick_y,stick_start_x,stick_start_y;
 char texture[64]; struct touch_button_s *next;
} touch_button_t;
int command_count,filtered_count; char last_command[32];
void Cbuf_AddText(const char *s) { command_count++; strcpy(last_command,s); }
void Cbuf_AddFilteredText(const char *s) { filtered_count++; Cbuf_AddText(s); }
struct {
 int move_finger,look_finger,state,move_stick,look_stick,precision,clientonly;
 float forward,side,pitch,yaw,look_side,look_forward;
 touch_button_t *move_button, *crouchmodebutton;
 int crouch_toggle_mode,configchanged; struct {touch_button_t *first;} list_user;
} touch;
struct { float value; } touch_stick_deadzone={.12}, touch_lookjoy_speed={60}, touch_lookjoy_curve={2}, m_pitch={.022}, touch_pitch={90}, touch_yaw={120},
 touch_precise_amount={.5},touch_enable={1},touch_crouch_toggle={0};
struct { float frametime,realframetime; } host;
struct { int key_dest; } cls;
struct {float value;} look_filter={0},m_yaw={.022};
struct {float lastpitch,lastyaw;} inputstate;
static void Platform_MouseMove(float *x,float *y) {*x=*y=0;}
static void IN_GyroFinalizeMove(float *f,float *s,float *p,float *y) {}
static void Joy_FinalizeMove(float *f,float *s,float *p,float *y) {}

'''
    tests = r'''
static float turn(int fps)
{
 float total=0;
 host.realframetime=1.0/fps; host.frametime=.005;
 for(int i=0;i<fps;i++) {float f=0,s=0,p=0,y=0; IN_CollectInput(&f,&s,&p,&y,1); total+=y;}
 return total;
}
int main(void)
{
 touch_button_t b={.type=touch_movejoy,.finger=3,.x1=0,.y1=0,.x2=2,.y2=2};
 float s,f;
 Touch_StickVector(&b,1,1,&s,&f); assert(s==0 && f==0);
 Touch_StickVector(&b,1.1,1,&s,&f); assert(s==0 && f==0);
 Touch_StickVector(&b,1.56,1,&s,&f); assert(fabsf(s-.5f)<.00001);
 Touch_StickVector(&b,2,1,&s,&f); assert(s==1 && f==0);
 Touch_StickVector(&b,1,0,&s,&f); assert(s==0 && f==1);
 Touch_StickVector(&b,3,-1,&s,&f);
 assert(fabsf(s*s+f*f-1)<.00001 && fabsf(b.stick_x*b.stick_x+b.stick_y*b.stick_y-1)<.00001);
 touch.move_finger=3; touch.forward=f; touch.side=s; touch.move_button=&b;
 Touch_ReleaseStick(&b);
 assert(touch.move_finger==-1 && touch.forward==0 && touch.side==0 && b.finger==-1);
 touch.look_finger=4; touch.look_stick=1; touch.look_side=1;
 assert(fabsf(turn(30)*3+180)<.001);
 assert(fabsf(turn(60)*3+180)<.001);
 assert(fabsf(turn(120)*3+180)<.001);
 touch_lookjoy_speed.value=30; assert(fabsf(turn(60)*6.6f+198)<.001); touch_lookjoy_speed.value=60;
 touch.precision=1; assert(fabsf(turn(60)*3+90)<.001); touch.precision=0;
 // Half deflection has quarter angular speed, with the same diagonal direction.
 touch.look_side=.5; assert(fabsf(turn(60)*3+45)<.001);
 touch.look_side=.5; touch.look_forward=.5;
 float pf=0,ps=0,pp=0,py=0;
 host.realframetime=1.0/60;
 IN_CollectInput(&pf,&ps,&pp,&py,1);
 assert(fabsf(pp/py-.75f)<.0001);
 m_pitch.value=-.022; float inverted=0,iyaw=0;
 IN_CollectInput(&pf,&ps,&inverted,&iyaw,1);
 assert(fabsf(pp+inverted)<.0001 && fabsf(iyaw-py)<.0001);
 m_pitch.value=.022;
 touch_lookjoy_curve.value=1; touch.look_side=.5; touch.look_forward=0;
 assert(fabsf(turn(60)*3+90)<.001); touch_lookjoy_curve.value=2;
 touch_yaw.value=60; touch.look_side=1;
 assert(fabsf(turn(60)*3+90)<.001); touch_yaw.value=120;
 // Mouse smoothing cannot add lag or leave a tail on the rate stick.
 look_filter.value=1;
 assert(fabsf(turn(60)*3+180)<.001);
 assert(inputstate.lastpitch==0&&inputstate.lastyaw==0);
 look_filter.value=0;
 b.type=touch_lookjoy; b.finger=4;
 Touch_StartLookStick(&b,1.5,.5);
 assert(touch.look_side==0 && touch.look_forward==0 && turn(60)==0);
 Touch_StickVector(&b,1.5,.5,&s,&f); assert(s==0&&f==0);
 Touch_StickVector(&b,1.5,0,&s,&f); assert(s==0&&f>0);
 Touch_StickVector(&b,3.5,-1.5,&s,&f); assert(fabsf(s*s+f*f-1)<.0001);
 touch.look_side=1;
 Touch_ReleaseStick(&b);
 look_filter.value=1;
 assert(touch.look_finger==-1 && touch.look_side==0 && turn(60)==0);
 look_filter.value=0;
 touch.look_stick=0; touch.pitch=2; touch.yaw=-3;
 float lf=0,ls=0,lp=0,ly=0; Touch_GetMove(&lf,&ls,&lp,&ly);
 assert(lp==2&&ly==-3&&touch.pitch==0&&touch.yaw==0);
 Touch_GetMove(&lf,&ls,&lp,&ly); assert(lp==2&&ly==-3);
 touch.look_finger=4; touch.look_stick=1; touch.look_side=1; cls.key_dest=1;
 assert(turn(60)==0); cls.key_dest=0; touch.state=1; assert(turn(60)==0);
 touch_button_t crouch={.type=touch_crouch,.finger=5}, label={0};
 touch.list_user.first=&crouch; touch.crouchmodebutton=&label;
 Touch_CrouchEvent(&crouch,event_down);
 assert(crouch.crouched && !strcmp(last_command,"+duck\n"));
 Touch_CrouchEvent(&crouch,event_up);
 assert(!crouch.crouched && !strcmp(last_command,"-duck\n"));
 touch_crouch_toggle.value=1; Touch_UpdateCrouchMode();
 assert(!strcmp(label.texture,"#Crouch: Toggle"));
 Touch_CrouchEvent(&crouch,event_down); int before=command_count;
 Touch_CrouchEvent(&crouch,event_up); assert(crouch.crouched && command_count==before);
 Touch_CrouchEvent(&crouch,event_down); assert(!crouch.crouched);
 Touch_CrouchEvent(&crouch,event_down); assert(crouch.crouched);
 touch_crouch_toggle.value=0; Touch_UpdateCrouchMode();
 assert(!crouch.crouched && !strcmp(last_command,"-duck\n"));
 assert(!strcmp(label.texture,"#Crouch: Hold") && touch.configchanged);
 crouch.flags=TOUCH_FL_UNPRIVILEGED;
 Touch_CrouchEvent(&crouch,event_down); Touch_ReleaseStick(&crouch);
 assert(!crouch.crouched && crouch.finger==-1 && filtered_count==2);
 puts("PASS: crouch hold/release, toggle on/off, mode-change reset, editor label, removal reset, filtered command routing");
 puts("PASS: dead zone, proportional movement, radial clamp, release reset, neutral pickup, radial curve, inversion, sensitivity, real-time look at 30/60/120 fps, legacy swipe isolation, mouse-filter isolation, precision, menu/editor suppression");
}
'''
    code = declarations + '\n'.join(function(source, name) for name in
                                     ('Touch_SetCrouch', 'Touch_CrouchEvent', 'Touch_ResetCrouch', 'Touch_UpdateCrouchMode',
                                      'Touch_StickVector', 'Touch_StartLookStick', 'Touch_ReleaseStick', 'Touch_GetMove', 'Touch_GetLookStickMove')) + function((ROOT / 'engine/client/input/input.c').read_text(), 'IN_CollectInput') + tests
    with tempfile.TemporaryDirectory(prefix='touch-tests-') as directory:
        path = Path(directory)
        (path / 'sticks.c').write_text(code)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Werror',
                        str(path / 'sticks.c'), '-lm', '-o', str(path / 'sticks')], check=True)
        subprocess.run([str(path / 'sticks')], check=True)


if __name__ == '__main__':
    main()
