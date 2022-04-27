/*
vgui_int.c - vgui dll interaction
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

#include "vgui_main.h"
#include "xash3d_types.h"
namespace vgui_support {

vguiapi_t *g_api;

Panel	*rootpanel = NULL;
CEngineSurface	*surface = NULL;
CEngineApp          staticApp;

void VGui_Startup( int width, int height )
{
	if( rootpanel )
	{
		rootpanel->setSize( width, height );
		return;
	}

	rootpanel = new Panel;
	rootpanel->setSize( width, height );
	rootpanel->setPaintBorderEnabled( false );
	rootpanel->setPaintBackgroundEnabled( false );
	rootpanel->setVisible( true );
	rootpanel->setCursor( new Cursor( Cursor::dc_none ));

	staticApp.start();
	staticApp.setMinimumTickMillisInterval( 0 );

	surface = new CEngineSurface( rootpanel );
	rootpanel->setSurfaceBaseTraverse( surface );


	//ASSERT( rootpanel->getApp() != NULL );
	//ASSERT( rootpanel->getSurfaceBase() != NULL );

	g_api->DrawInit ();
}

void VGui_Shutdown( void )
{
	staticApp.stop();

	delete rootpanel;
	delete surface;

	rootpanel = NULL;
	surface = NULL;
}

void VGui_Paint( void )
{
	int w, h;

	//if( cls.state != ca_active || !rootpanel )
	//	return;
	if( !g_api->IsInGame() || !rootpanel )
		return;

	// setup the base panel to cover the screen
	Panel *pVPanel = surface->getEmbeddedPanel();
	if( !pVPanel ) return;
	//SDL_GetWindowSize(host.hWnd, &w, &h);
	//host.input_enabled = rootpanel->isVisible();
	rootpanel->getSize(w, h);
	EnableScissor( true );

	staticApp.externalTick ();

	pVPanel->setBounds( 0, 0, w, h );
	pVPanel->repaint();

	// paint everything 
	pVPanel->paintTraverse();

	EnableScissor( false );
}
void *VGui_GetPanel( void )
{
	return (void *)rootpanel;
}
}

#ifdef INTERNAL_VGUI_SUPPORT
#define InitAPI InitVGUISupportAPI
#endif

extern "C" EXPORT void InitAPI(vguiapi_t * api)
{
	g_api = api;
	g_api->Startup = VGui_Startup;
	g_api->Shutdown = VGui_Shutdown;
	g_api->GetPanel = VGui_GetPanel;
	g_api->Paint = VGui_Paint;
	g_api->Mouse = VGUI_Mouse;
	g_api->MouseMove = VGUI_MouseMove;
	g_api->Key = VGUI_Key;
	g_api->TextInput = VGUI_TextInput;
}
