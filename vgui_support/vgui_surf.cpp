/*
vgui_surf.cpp - main vgui layer
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

#include <ctype.h>
#include "vgui_main.h"

#define ColorIndex( c )((( c ) - '0' ) & 7 )

CEngineSurface :: CEngineSurface( Panel *embeddedPanel ):SurfaceBase( embeddedPanel )
{
	_embeddedPanel = embeddedPanel;
	_drawColor[0] = _drawColor[1] = _drawColor[2] = _drawColor[3] = 255;
	_drawTextColor[0] = _drawTextColor[1] = _drawTextColor[2] = _drawTextColor[3] = 255;

	_surfaceExtents[0] = _surfaceExtents[1] = 0;
	//_surfaceExtents[2] = menu.globals->scrWidth;
	//_surfaceExtents[3] = menu.globals->scrHeight;
	embeddedPanel->getSize(_surfaceExtents[2], _surfaceExtents[3]);
	_drawTextPos[0] = _drawTextPos[1] = 0;
	_hCurrentFont = null;
	_hCurrentCursor = null;
	_translateX = _translateY = 0;
}

CEngineSurface :: ~CEngineSurface( void )
{
	g_api->DrawShutdown ();
}

Panel *CEngineSurface :: getEmbeddedPanel( void )
{
	return _embeddedPanel;
}

bool CEngineSurface :: hasFocus( void )
{
	// What differs when window does not has focus?
	//return host.state != HOST_NOFOCUS;
	return true;
}
	
void CEngineSurface :: setCursor( Cursor *cursor )
{
	_currentCursor = cursor;
	g_api->CursorSelect( (VGUI_DefaultCursor)cursor->getDefaultCursor() );
}

void CEngineSurface :: SetupPaintState( const paintState_t &paintState )
{
	_translateX = paintState.iTranslateX;
	_translateY = paintState.iTranslateY;
	SetScissorRect( paintState.iScissorLeft, paintState.iScissorTop, 
	paintState.iScissorRight, paintState.iScissorBottom );
}

void CEngineSurface :: InitVertex( vpoint_t &vertex, int x, int y, float u, float v )
{
	vertex.point[0] = x + _translateX;
	vertex.point[1] = y + _translateY;
	vertex.coord[0] = u;
	vertex.coord[1] = v;
}

int CEngineSurface :: createNewTextureID( void )
{
	return g_api->GenerateTexture();
}

void CEngineSurface :: drawSetColor( int r, int g, int b, int a )
{
	_drawColor[0] = r;
	_drawColor[1] = g;
	_drawColor[2] = b;
	_drawColor[3] = a;
}

void CEngineSurface :: drawSetTextColor( int r, int g, int b, int a )
{
	_drawTextColor[0] = r;
	_drawTextColor[1] = g;
	_drawTextColor[2] = b;
	_drawTextColor[3] = a;
}

void CEngineSurface :: drawFilledRect( int x0, int y0, int x1, int y1 )
{
	vpoint_t rect[2];
	vpoint_t clippedRect[2];

	if( _drawColor[3] >= 255 ) return;

	InitVertex( rect[0], x0, y0, 0, 0 );
	InitVertex( rect[1], x1, y1, 0, 0 );

	// fully clipped?
	if( !ClipRect( rect[0], rect[1], &clippedRect[0], &clippedRect[1] ))
		return;	

	g_api->SetupDrawingRect( _drawColor );	
	g_api->EnableTexture( false );
	g_api->DrawQuad( &clippedRect[0], &clippedRect[1] );
	g_api->EnableTexture( true );
}

void CEngineSurface :: drawOutlinedRect( int x0, int y0, int x1, int y1 )
{
	if( _drawColor[3] >= 255 ) return;

	drawFilledRect( x0, y0, x1, y0 + 1 );		// top
	drawFilledRect( x0, y1 - 1, x1, y1 );		// bottom
	drawFilledRect( x0, y0 + 1, x0 + 1, y1 - 1 );	// left
	drawFilledRect( x1 - 1, y0 + 1, x1, y1 - 1 );	// right
}
	
void CEngineSurface :: drawSetTextFont( Font *font )
{
	_hCurrentFont = font;
}

void CEngineSurface :: drawSetTextPos( int x, int y )
{
	_drawTextPos[0] = x;
	_drawTextPos[1] = y;
}

void CEngineSurface :: drawPrintText( const char* text, int textLen )
{
	//return;
	static bool hasColor = 0;
	static int numColor = 7;

	if( !text || !_hCurrentFont || _drawTextColor[3] >= 255 )
		return;

	int x = _drawTextPos[0] + _translateX;
	int y = _drawTextPos[1] + _translateY;

	int iTall = _hCurrentFont->getTall();

	int j, iTotalWidth = 0;
	int curTextColor[4];

	//  HACKHACK: allow color strings in VGUI
	if( numColor != 7 )
	{
		for( j = 0; j < 3; j++ ) // grab predefined color
			curTextColor[j] = g_api->GetColor(numColor,j);
    }
    else
    {
		for( j = 0; j < 3; j++ ) // revert default color
			curTextColor[j] = _drawTextColor[j];
	}
	curTextColor[3] = _drawTextColor[3]; // copy alpha

	if( textLen == 1 )
	{
		if( *text == '^' )
		{
			hasColor = true;
			return; // skip '^'
		}
		else if( hasColor && isdigit( *text ))
		{
			numColor = ColorIndex( *text );
			hasColor = false; // handled
			return; // skip colornum
		}
		else hasColor = false;
	}
	for( int i = 0; i < textLen; i++ )
	{
		char ch = g_api->ProcessUtfChar( (unsigned char)text[i] );
		if( !ch )
		{
			continue;
		}

		int abcA,abcB,abcC;
		_hCurrentFont->getCharABCwide( ch, abcA, abcB, abcC );

		iTotalWidth += abcA;
		int iWide = abcB;

		//if( !iswspace( ch ))
		{
			// get the character texture from the cache
			int iTexId = 0;
			float *texCoords = NULL;

			if( !g_FontCache->GetTextureForChar( _hCurrentFont, ch, &iTexId, &texCoords ))
			{
				continue;
			}

			Assert( texCoords != NULL );

			vpoint_t ul, lr;

			ul.point[0] = x + iTotalWidth;
			ul.point[1] = y;
			lr.point[0] = ul.point[0] + iWide;
			lr.point[1] = ul.point[1] + iTall;

			// gets at the texture coords for this character in its texture page
			ul.coord[0] = texCoords[0];
			ul.coord[1] = texCoords[1];
			lr.coord[0] = texCoords[2];
			lr.coord[1] = texCoords[3];

			vpoint_t clippedRect[2];

			if( !ClipRect( ul, lr, &clippedRect[0], &clippedRect[1] ))
				continue;

			drawSetTexture( iTexId );
			g_api->SetupDrawingText( curTextColor );
			g_api->DrawQuad(  &clippedRect[0], &clippedRect[1] ); // draw the letter
		}

		iTotalWidth += iWide + abcC;
	}

	_drawTextPos[0] += iTotalWidth;
}

void CEngineSurface :: drawSetTextureRGBA( int id, const char* rgba, int wide, int tall )
{
	g_api->UploadTexture( id, rgba, wide, tall );
}
	
void CEngineSurface :: drawSetTexture( int id )
{
	g_api->BindTexture( id );
}
	
void CEngineSurface :: drawTexturedRect( int x0, int y0, int x1, int y1 )
{
	vpoint_t rect[2];
	vpoint_t clippedRect[2];

	InitVertex( rect[0], x0, y0, 0, 0 );
	InitVertex( rect[1], x1, y1, 1, 1 );

	// fully clipped?
	if( !ClipRect( rect[0], rect[1], &clippedRect[0], &clippedRect[1] ))
		return;	

	g_api->SetupDrawingImage( _drawColor );	
	g_api->DrawQuad( &clippedRect[0], &clippedRect[1] );
}
	
void CEngineSurface :: pushMakeCurrent( Panel* panel, bool useInsets )
{
	int inSets[4] = { 0, 0, 0, 0 };
	int absExtents[4];
	int clipRect[4];

	if( useInsets )
	{
		panel->getInset( inSets[0], inSets[1], inSets[2], inSets[3] );
	}

	panel->getAbsExtents( absExtents[0], absExtents[1], absExtents[2], absExtents[3] );
	panel->getClipRect( clipRect[0], clipRect[1], clipRect[2], clipRect[3] );

	int i = _paintStack.AddToTail();
	paintState_t &paintState = _paintStack[i];
	paintState.m_pPanel = panel;

	// determine corrected top left origin
	paintState.iTranslateX = inSets[0] + absExtents[0] - _surfaceExtents[0];	
	paintState.iTranslateY = inSets[1] + absExtents[1] - _surfaceExtents[1];

	// setup clipping rectangle for scissoring
	paintState.iScissorLeft = clipRect[0] - _surfaceExtents[0];
	paintState.iScissorTop = clipRect[1] - _surfaceExtents[1];
	paintState.iScissorRight = clipRect[2] - _surfaceExtents[0];
	paintState.iScissorBottom = clipRect[3] - _surfaceExtents[1];

	SetupPaintState( paintState );
}
	
void CEngineSurface :: popMakeCurrent( Panel *panel )
{
	int top = _paintStack.Count() - 1;

	// more pops that pushes?
	Assert( top >= 0 );

	// didn't pop in reverse order of push?
	Assert( _paintStack[top].m_pPanel == panel );

	_paintStack.Remove( top );
	
	if( top > 0 ) SetupPaintState( _paintStack[top-1] );
}

bool CEngineSurface :: setFullscreenMode( int wide, int tall, int bpp )
{
	// NOTE: Xash3D always working in 32-bit mode
	// Skip it now. VGUI cannot change video modes
	/*if( R_DescribeVIDMode( wide, tall ))
	{
		Cvar_SetFloat( "fullscreen", 1.0f );
		return true;
	}*/
	return false;
}
	
void CEngineSurface :: setWindowedMode( void )
{
	// Skip it now. VGUI cannot change video modes
	/*
	Cvar_SetFloat( "fullscreen", 0.0f );
	*/
}
