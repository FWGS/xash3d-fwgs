/*
vgui_int.cpp - vgui dll interaction
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

#include "common.h"
#include "client.h"
#include "const.h"
#include "vgui_draw.h"
#include "vgui_main.h"

Panel		*rootPanel = NULL;
CEngineSurface	*engSurface = NULL;
CEngineApp	staticApp, *engApp;

void CEngineApp :: setCursorPos( int x, int y )
{
	POINT pt;

	pt.x = x;
	pt.y = y;

	ClientToScreen( (HWND)host.hWnd, &pt );

	::SetCursorPos( pt.x, pt.y );
}
	
void CEngineApp :: getCursorPos( int &x,int &y )
{
	POINT	pt;

	// find mouse movement
	::GetCursorPos( &pt );
	ScreenToClient((HWND)host.hWnd, &pt );

	x = pt.x;
	y = pt.y;
}

void VGui_RunFrame( void )
{
	if( GetModuleHandle( "fraps32.dll" ) || GetModuleHandle( "fraps64.dll" ))
		host.force_draw_version = true;
	else host.force_draw_version = false;
}

void VGui_SetRootPanelSize( void )
{
	if( rootPanel != NULL )
		rootPanel->setBounds( 0, 0, gameui.globals->scrWidth, gameui.globals->scrHeight );
}

void VGui_Startup( void )
{
	if( engSurface ) return;

	engApp = (CEngineApp *)App::getInstance();
	engApp->reset();
	engApp->setMinimumTickMillisInterval( 0 ); // paint every frame

	rootPanel = new Panel( 0, 0, 320, 240 ); // size will be changed in VGui_SetRootPanelSize
	rootPanel->setPaintBorderEnabled( false );
	rootPanel->setPaintBackgroundEnabled( false );
	rootPanel->setPaintEnabled( false );
	rootPanel->setCursor( engApp->getScheme()->getCursor( Scheme::scu_none ));

	engSurface = new CEngineSurface( rootPanel );

	VGui_SetRootPanelSize ();
	VGUI_DrawInit ();
}

void VGui_Shutdown( void )
{
	delete rootPanel;
	delete engSurface;
	engSurface = NULL;
	rootPanel = NULL;
}

void VGui_Paint( int paintAll )
{
	int	extents[4];

	if( cls.state != ca_active || !rootPanel )
		return;

	VGui_SetRootPanelSize ();
	rootPanel->repaint();
	EnableScissor( true );

	if( cls.key_dest == key_game )
	{
		App::getInstance()->externalTick();
	}

	if( paintAll )
	{
		// paint everything
		rootPanel->paintTraverse();
	}
	else
	{
		rootPanel->getAbsExtents( extents[0], extents[1], extents[2], extents[3] );
		VGui_ViewportPaintBackground( extents );
	}

	EnableScissor( false );
}

void VGui_ViewportPaintBackground( int extents[4] )
{
	// not used
}

void *VGui_GetPanel( void )
{
	return (void *)rootPanel;
}