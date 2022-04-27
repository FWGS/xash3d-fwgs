/*
vgui_main.h - vgui main header
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

In addition, as a special exception, the author gives permission
to link the code of this program with VGUI library developed by
Valve, L.L.C ("Valve"). You must obey the GNU General Public License
in all respects for all of the code used other than VGUI library.
If you modify this file, you may extend this exception to your
version of the file, but you are not obligated to do so. If
you do not wish to do so, delete this exception statement
from your version.

*/
#ifndef VGUI_MAIN_H
#define VGUI_MAIN_H

#ifdef _WIN32
#include <windows.h>
#else
#include <string.h>
#endif

#include <assert.h>

#include "vgui_api.h"

#include<VGUI.h>
#include<VGUI_App.h>
#include<VGUI_Font.h>
#include<VGUI_Panel.h>
#include<VGUI_Cursor.h>
#include<VGUI_SurfaceBase.h>
#include<VGUI_InputSignal.h>
#include<VGUI_MouseCode.h>
#include<VGUI_KeyCode.h>

namespace vgui_support
{
extern vguiapi_t *g_api;

using namespace vgui;

struct PaintStack
{
	Panel	*m_pPanel;
	int	iTranslateX;
	int	iTranslateY;
	int	iScissorLeft;
	int	iScissorRight;
	int	iScissorTop;
	int	iScissorBottom;
};

class CEngineSurface : public SurfaceBase
{
private:

	// point translation for current panel
	int		_translateX;
	int		_translateY;

	// the size of the window to draw into
	int		_surfaceExtents[4];

	void SetupPaintState( const PaintStack *paintState );
	void InitVertex( vpoint_t &vertex, int x, int y, float u, float v );
public:
	CEngineSurface( Panel *embeddedPanel );
	~CEngineSurface();
public:
	virtual Panel *getEmbeddedPanel( void );
	virtual bool setFullscreenMode( int wide, int tall, int bpp );
	virtual void setWindowedMode( void );
	virtual void setTitle( const char *title ) { }
	virtual void createPopup( Panel* embeddedPanel ) { }
	virtual bool isWithin( int x, int y ) { return true; }
	virtual bool hasFocus( void );
	// now it's not abstract class, yay
	virtual void GetMousePos(int &x, int &y) {
		g_api->GetCursorPos(&x, &y);
	}
	void drawPrintChar(int x, int y, int wide, int tall, float s0, float t0, float s1, float t1, int color[]);
protected:
	virtual int createNewTextureID( void );
	virtual void drawSetColor( int r, int g, int b, int a );
	virtual void drawSetTextColor( int r, int g, int b, int a );
	virtual void drawFilledRect( int x0, int y0, int x1, int y1 );
	virtual void drawOutlinedRect( int x0,int y0,int x1,int y1 );
	virtual void drawSetTextFont( Font *font );
	virtual void drawSetTextPos( int x, int y );
	virtual void drawPrintText( const char* text, int textLen );
	virtual void drawSetTextureRGBA( int id, const char* rgba, int wide, int tall );
	virtual void drawSetTexture( int id );
	virtual void drawTexturedRect( int x0, int y0, int x1, int y1 );
	virtual bool createPlat( void ) { return false; }
	virtual bool recreateContext( void ) { return false; }
	virtual void setCursor( Cursor* cursor );
	virtual void pushMakeCurrent( Panel* panel, bool useInsets );
	virtual void popMakeCurrent( Panel* panel );

	// not used in engine instance
	virtual void enableMouseCapture( bool state ) { }
	virtual void invalidate( Panel *panel ) { }
	virtual void setAsTopMost( bool state ) { }
	virtual void applyChanges( void ) { }
	virtual void swapBuffers( void ) { }
protected:
	Cursor* _hCurrentCursor;
	int _drawTextPos[2];
	int _drawColor[4];
	int _drawTextColor[4];
	friend class App;
	friend class Panel;
};

// initialize VGUI::App as external (part of engine)
class CEngineApp : public App
{
public:
	CEngineApp( bool externalMain = true ) : App( externalMain ) { }
	virtual void main( int argc, char* argv[] ) { } // stub
};

//
// vgui_input.cpp
//
void *VGui_GetPanel( void );
void VGui_Paint( void );
void VGUI_Mouse(VGUI_MouseAction action, int code);
void VGUI_Key(VGUI_KeyAction action, VGUI_KeyCode code);
void VGUI_MouseMove(int x, int y);
void VGUI_TextInput(const char *text);

//
// vgui_clip.cpp
//
void EnableScissor( qboolean enable );
void SetScissorRect( int left, int top, int right, int bottom );
qboolean ClipRect( const vpoint_t &inUL, const vpoint_t &inLR, vpoint_t *pOutUL, vpoint_t *pOutLR );

extern CEngineSurface	*surface;
extern Panel *root;
}
using namespace vgui_support;
#endif//VGUI_MAIN_H
