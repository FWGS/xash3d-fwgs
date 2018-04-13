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

#define Assert(x)

#ifdef _WIN32
#include <windows.h>
#else
#include <string.h>
#endif

#include "vgui_api.h"
#include "utlvector.h"
#include "utlrbtree.h"

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

class FontCache
{
public:
	FontCache();
	~FontCache() { }

	// returns a texture ID and a pointer to an array of 4 texture coords for the given character & font
	// uploads more texture if necessary
	bool GetTextureForChar( Font *font, char ch, int *textureID, float **texCoords );
private:
	// NOTE: If you change this, change s_pFontPageSize
	enum
	{
		FONT_PAGE_SIZE_16,
		FONT_PAGE_SIZE_32,
		FONT_PAGE_SIZE_64,
		FONT_PAGE_SIZE_128,
		FONT_PAGE_SIZE_COUNT
	};

	// a single character in the cache
	typedef unsigned short HCacheEntry;
	struct CacheEntry_t
	{
		Font		*font;
		char		ch;
		byte		page;
		float		texCoords[4];

		HCacheEntry	nextEntry;	// doubly-linked list for use in the LRU
		HCacheEntry	prevEntry;
	};
	
	// a single texture page
	struct Page_t
	{
		short		textureID;
		short		fontHeight;
		short		wide, tall;	// total size of the page
		short		nextX, nextY;	// position to draw any new character positions
	};

	// allocates a new page for a given character
	bool AllocatePageForChar( int charWide, int charTall, int &pageIndex, int &drawX, int &drawY, int &twide, int &ttall );

	// Computes the page size given a character height
	int ComputePageType( int charTall ) const;

	static bool CacheEntryLessFunc( const CacheEntry_t &lhs, const CacheEntry_t &rhs );

	// cache
	typedef CUtlVector<Page_t> FontPageList_t;

	CUtlRBTree<CacheEntry_t, HCacheEntry> m_CharCache;
	FontPageList_t m_PageList;
	int m_pCurrPage[FONT_PAGE_SIZE_COUNT];
	HCacheEntry m_LRUListHeadIndex;

	static int s_pFontPageSize[FONT_PAGE_SIZE_COUNT];
};

class CEngineSurface : public SurfaceBase
{
private:
	struct paintState_t
	{
		Panel	*m_pPanel;
		int	iTranslateX;
		int	iTranslateY;
		int	iScissorLeft;
		int	iScissorRight;
		int	iScissorTop;
		int	iScissorBottom;
	};

	// point translation for current panel
	int		_translateX;
	int		_translateY;

	// the size of the window to draw into
	int		_surfaceExtents[4];

	CUtlVector <paintState_t> _paintStack;

	void SetupPaintState( const paintState_t &paintState );
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
	Font* _hCurrentFont;
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
void VGUI_InitCursors( void );
void VGUI_CursorSelect( Cursor *cursor );
void VGUI_ActivateCurrentCursor( void );
void *VGui_GetPanel( void );
void VGui_RunFrame( void );
void VGui_Paint( void );
void VGUI_Mouse(VGUI_MouseAction action, int code);
void VGUI_Key(VGUI_KeyAction action, VGUI_KeyCode code);
void VGUI_MouseMove(int x, int y);
//
// vgui_clip.cpp
//
void EnableScissor( qboolean enable );
void SetScissorRect( int left, int top, int right, int bottom );
qboolean ClipRect( const vpoint_t &inUL, const vpoint_t &inLR, vpoint_t *pOutUL, vpoint_t *pOutLR );

extern FontCache *g_FontCache;


extern CEngineSurface	*surface;
extern Panel *root;
}
using namespace vgui_support;
#endif//VGUI_MAIN_H
