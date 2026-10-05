#!/usr/bin/env python3
"""Exercise the engine's menu viewport and input/drawing transforms with a C compiler."""
from pathlib import Path
import os
import subprocess
import tempfile
from test_touch_sticks import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'engine/client/dll_int/cl_gameui.c').read_text()
STUBS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define GAME_EXPORT
#define true 1
#define false 0
#define kRenderTransTexture 2
#define Q_max(a,b) ((a)>(b)?(a):(b))
#define Q_min(a,b) ((a)<(b)?(a):(b))
#define bound(a,b,c) Q_min(Q_max(a,b),c)
typedef int qboolean;
static int ui_origin_x,ui_origin_y;
static float inset[4];
static int drawn_x,drawn_y,drawn_w,drawn_h,mouse_x,mouse_y,cursor_x,cursor_y;
static int text_x,text_y,text_w,text_h;
struct {int width,height;} refState;
struct {void *hWnd;} host;
struct globals {int scrWidth,scrHeight;} globals;
struct {
 void *hInstance; struct globals *globals;
 struct {int scissor;unsigned char textColor[4];} ds;
 struct {void (*pfnMouseMove)(int,int);void (*pfnGetCursorPos)(int*,int*);void (*pfnSetCursorPos)(int,int);} dllFuncs;
} gameui;
static void fill(int mode,int x,int y,int w,int h,int r,int g,int b,int a) {
 drawn_x=x;drawn_y=y;drawn_w=w;drawn_h=h;
}
struct {struct {void (*FillRGBA)(int,int,int,int,int,int,int,int,int);} dllFuncs;} ref;
static void mouse(int x,int y) {mouse_x=x;mouse_y=y;}
static void getcursor(int *x,int *y) {if(x)*x=cursor_x;if(y)*y=cursor_y;}
static void setcursor(int x,int y) {cursor_x=x;cursor_y=y;}
static float IOS_GetTouchInsets(void *window,float *l,float *t,float *r,float *b) {
 *l=inset[0];*t=inset[1];*r=inset[2];*b=inset[3];return 874;
}
#define Con_DPrintf(...) ((void)0)
static void CL_EnableScissor(int *s,int x,int y,int w,int h) {fill(0,x,y,w,h,0,0,0,0);}
static int Con_DrawString(int x,int y,const char *s,unsigned char *c) {drawn_x=x;drawn_y=y;return 19;}
#define MakeRGBA(c,r,g,b,a) ((void)0)
static void Key_SetTextInputRect(int x,int y,int w,int h) {text_x=x;text_y=y;text_w=w;text_h=h;}
'''
TESTS = r'''
int main(void) {
 gameui.hInstance=&gameui;gameui.globals=&globals;
 gameui.dllFuncs.pfnMouseMove=mouse;gameui.dllFuncs.pfnGetCursorPos=getcursor;gameui.dllFuncs.pfnSetCursorPos=setcursor;
 ref.dllFuncs.FillRGBA=fill;
 const int sizes[][2]={{874,402},{812,375},{667,375},{1024,768}};
 for(int i=0;i<4;i++) {
  refState.width=sizes[i][0];refState.height=sizes[i][1];
  for(int orientation=0;orientation<3;orientation++) {
   inset[0]=orientation==0?0:59.0f/refState.width;
   inset[1]=0;inset[2]=orientation==2?inset[0]:0;inset[3]=orientation==0?0:21.0f/refState.height;
   UI_UpdateViewport();
   assert(globals.scrWidth>0&&globals.scrHeight>0);
   assert(ui_origin_x+globals.scrWidth<=refState.width);
   assert(ui_origin_y+globals.scrHeight<=refState.height);
#if XASH_IOS
   if(orientation) {assert(ui_origin_x>=59);assert(globals.scrHeight<=refState.height-21);}
#else
   assert(ui_origin_x==0&&ui_origin_y==0);
   assert(globals.scrWidth==refState.width&&globals.scrHeight==refState.height);
#endif
   // A tap at the drawn checkbox must reach the menu at its original coordinates.
   pfnFillRGBA(40,180,16,16,255,255,255,255);
   UI_MouseMove(drawn_x+8,drawn_y+8);
   assert(mouse_x==48&&mouse_y==188);
   pfnPIC_EnableScissor(0,0,globals.scrWidth+100,globals.scrHeight+100);
   assert(drawn_x==ui_origin_x&&drawn_y==ui_origin_y);
   assert(drawn_w==globals.scrWidth&&drawn_h==globals.scrHeight);
   assert(UI_DrawConsoleString(40,180,"label")==59);
   assert(drawn_x==40+ui_origin_x&&drawn_y==180+ui_origin_y);
   UI_SetCursorPos(80+ui_origin_x,190+ui_origin_y);
   int x=0,y=0;UI_GetCursorPos(&x,&y);
   assert(x==80+ui_origin_x&&y==190+ui_origin_y);
   UI_GetCursorPos(NULL,NULL);
   pfnSetTextInputRect(40,180,16,16);
   assert(text_x==40+ui_origin_x&&text_y==180+ui_origin_y);
   pfnSetTextInputRect(0,0,0,0);assert(text_x==0&&text_y==0);
   assert(!UI_UpdateViewport());
  }
 }
 gameui.globals=NULL;assert(!UI_UpdateViewport());
 puts("PASS: menu safe bounds, checkbox tap alignment, scissor, text, cursor, keyboard rect, rotation and desktop behavior");
}
'''

def main():
    funcs='\n'.join(function(SOURCE,n) for n in ['UI_UpdateViewport','UI_MouseMove','UI_GetCursorPos','UI_SetCursorPos','pfnFillRGBA','pfnPIC_EnableScissor','UI_DrawConsoleString','pfnSetTextInputRect'])
    with tempfile.TemporaryDirectory(prefix='xash-menu-') as temp:
        source=Path(temp)/'menu.c';source.write_text(STUBS+funcs+TESTS)
        for ios in [0,1]:
            binary=Path(temp)/f'menu-{ios}'
            subprocess.run([os.environ.get('CC','cc'),'-std=c99',f'-DXASH_IOS={ios}',str(source),'-lm','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__': main()
