/*
vgui_draw.h - vgui draw methods
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

#ifndef VGUI_DRAW_H
#define VGUI_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "port.h"

//
// vgui_draw.c
//
void VGui_Startup( const char *clientlib, int width, int height );
void VGui_Shutdown( void );
void VGui_Paint( void );
void VGui_RunFrame( void );
void VGui_KeyEvent( int key, int down );
void VGui_MouseMove( int x, int y );
qboolean VGui_IsActive( void );
void *VGui_GetPanel( void );
void VGui_ReportTextInput( const char *text );
#ifdef __cplusplus
}
#endif
#endif//VGUI_DRAW_H
