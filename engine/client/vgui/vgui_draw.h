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

#define VGUI_MAX_TEXTURES	2048	// a half of total textures count

extern rgba_t g_color_table[8];	// for colored strings support
extern convar_t *vgui_colorstrings;

// VGUI generic vertex
typedef struct
{
	vec2_t	point;
	vec2_t	coord;
	byte	color[4];
} vpoint_t;

//
// vgui_backend.c
//

void VGUI_DrawInit( void );
void VGUI_DrawShutdown( void );
void VGUI_SetupDrawingRect( int *pColor );
void VGUI_SetupDrawingImage( int *pColor );
void VGUI_BindTexture( int id );
void VGUI_EnableTexture( qboolean enable );
void VGUI_UploadTexture( int id, const char *buffer, int width, int height );
LONG VGUI_SurfaceWndProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
void VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr );
void VGUI_DrawBuffer( const vpoint_t *buffer, int numVerts );
int VGUI_GenerateTexture( void );
void *VGui_GetPanel( void );

#ifdef __cplusplus
void EnableScissor( qboolean enable );
void SetScissorRect( int left, int top, int right, int bottom );
qboolean ClipRect( const vpoint_t &inUL, const vpoint_t &inLR, vpoint_t *pOutUL, vpoint_t *pOutLR );
#endif

//
// gl_vidnt.c
//
qboolean R_DescribeVIDMode( int width, int height );

//
// vgui_int.c
//
void VGui_Startup( void );
void VGui_Shutdown( void );
void *VGui_GetPanel( void );
void VGui_Paint( int paintAll );
void VGui_RunFrame( void );
void VGui_ViewportPaintBackground( int extents[4] );

#ifdef __cplusplus
}
#endif
#endif//VGUI_DRAW_H