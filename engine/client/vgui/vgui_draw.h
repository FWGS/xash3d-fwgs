#ifndef VGUI_DRAW_H
#define VGUI_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

//
// vgui_draw.c
//
void VGui_RegisterCvars( void );
qboolean VGui_LoadProgs( HINSTANCE hInstance );
void VGui_Startup( int width, int height );
void VGui_Shutdown( void );
void VGui_Paint( void );
void VGui_RunFrame( void );
void VGui_MouseEvent( int key, int clicks );
void VGui_MWheelEvent( int y );
void VGui_KeyEvent( int key, int down );
void VGui_MouseMove( int x, int y );
qboolean VGui_IsActive( void );
void *VGui_GetPanel( void );
void VGui_ReportTextInput( const char *text );
void VGui_UpdateInternalCursorState( VGUI_DefaultCursor cursorType );

#ifdef __cplusplus
}
#endif

#endif // VGUI_DRAW_H
