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
*/

#ifndef VGUI_MAIN_H
#define VGUI_MAIN_H

#include<VGUI.h>
#include<VGUI_App.h>
#include<VGUI_Font.h>
#include<VGUI_Panel.h>
#include<VGUI_Cursor.h>
#include<VGUI_SurfaceBase.h>
#include<VGUI_InputSignal.h>
#include<VGUI_MouseCode.h>
#include<VGUI_KeyCode.h>

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
	void InitVertex( vpoint_t &vertex, int x, int y, float u, float v );
public:
	CEngineSurface( Panel *embeddedPanel );
	~CEngineSurface();	
public:
	// not used in engine instance
	virtual bool setFullscreenMode( int wide, int tall, int bpp ) { return false; }
	virtual void setWindowedMode( void ) { }
	virtual void setTitle( const char *title ) { }
	virtual void createPopup( Panel* embeddedPanel ) { }
	virtual bool isWithin( int x, int y ) { return true; }
	void SetupPaintState( const PaintStack *paintState );
#ifdef NEW_VGUI_DLL
	virtual void GetMousePos( int &x, int &y ) { }
#endif
	virtual bool hasFocus( void ) { return true; }
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
	virtual void drawPrintChar( int x, int y, int wide, int tall, float s0, float t0, float s1, float t1, int color[4] );
	virtual void addCharToBuffer( const vpoint_t *ul, const vpoint_t *lr, int color[4] );
	virtual void setCursor( Cursor* cursor );
	virtual void pushMakeCurrent( Panel* panel, bool useInsets );
	virtual void popMakeCurrent( Panel* panel );
	// not used in engine instance
	virtual bool createPlat( void ) { return false; }
	virtual bool recreateContext( void ) { return false; }
	virtual void enableMouseCapture( bool state ) { }
	virtual void invalidate( Panel *panel ) { }
	virtual void setAsTopMost( bool state ) { }
	virtual void applyChanges( void ) { }
	virtual void swapBuffers( void ) { }
	virtual void flushBuffer( void );
protected:
	int _drawTextPos[2];
	int _drawColor[4];
	int _drawTextColor[4];
	int _translateX, _translateY;
	int _currentTexture;
	Panel *currentPanel;
};

// initialize VGUI::App as external (part of engine)
class CEngineApp : public App
{
public:
	virtual void main( int argc, char* argv[] ) { }
	virtual void setCursorPos( int x, int y );	// we need to recompute abs position to window
	virtual void getCursorPos( int &x,int &y );
protected: 
	virtual void platTick(void) { }
};

extern Panel		*rootPanel;
extern CEngineSurface	*engSurface;
extern CEngineApp		*engApp;

//
// vgui_input.cpp
//
void VGUI_InitCursors( void );
void VGUI_CursorSelect( Cursor *cursor );
void VGUI_ActivateCurrentCursor( void );

#endif//VGUI_MAIN_H