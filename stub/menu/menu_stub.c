#include "common.h"
#include "menu_int.h"
#include "keydefs.h"

static ui_enginefuncs_t *g_ui_eng;
static ui_globalvars_t *g_ui_globals;
static qboolean g_menu_active;

static void Menu_DrawLine( int y, const char *text )
{
	int width = 0, height = 0;

	if( !g_ui_eng || !text )
		return;

	g_ui_eng->pfnDrawSetTextColor( 255, 255, 255, 255 );
	g_ui_eng->pfnDrawConsoleStringLen( text, &width, &height );
	g_ui_eng->pfnDrawConsoleString(
		( g_ui_globals->scrWidth - width ) / 2,
		y,
		text );
}

static int Menu_VidInit( void )
{
	return 1;
}

static void Menu_Init( void )
{
}

static void Menu_Shutdown( void )
{
	g_menu_active = false;
}

static void Menu_Redraw( float flTime )
{
	(void)flTime;

	if( !g_menu_active || !g_ui_eng || !g_ui_globals )
		return;

	g_ui_eng->pfnFillRGBA( 0, 0, g_ui_globals->scrWidth, g_ui_globals->scrHeight, 0, 0, 96, 255 );
	Menu_DrawLine( g_ui_globals->scrHeight / 2 - 36, "XASH3D GAMECUBE" );
	Menu_DrawLine( g_ui_globals->scrHeight / 2, "Press START or A for New Game" );
	Menu_DrawLine( g_ui_globals->scrHeight / 2 + 18, "Press B for Console" );
}

static void Menu_StartGame( void )
{
	if( !g_ui_eng )
		return;

	g_menu_active = false;
	g_ui_eng->pfnSetKeyDest( key_game );
	g_ui_eng->pfnClientCmd( 1, "newgame\n" );
}

static void Menu_KeyEvent( int key, int down )
{
	if( !down || !g_menu_active )
		return;

	switch( key )
	{
	case K_ENTER:
	case K_A_BUTTON:
	case K_START_BUTTON:
		Menu_StartGame();
		break;
	case K_ESCAPE:
	case K_B_BUTTON:
		if( g_ui_eng )
		{
			g_menu_active = false;
			g_ui_eng->pfnSetKeyDest( key_console );
			g_ui_eng->pfnClientCmd( 1, "toggleconsole\n" );
		}
		break;
	default:
		break;
	}
}

static void Menu_SetActiveMenu( int active )
{
	g_menu_active = active ? true : false;

	if( !g_ui_eng )
		return;

	if( g_menu_active )
	{
		g_ui_eng->pfnKeyClearStates();
		g_ui_eng->pfnSetKeyDest( key_menu );
	}
	else
	{
		g_ui_eng->pfnSetKeyDest( key_game );
	}
}

static void Menu_MouseMove( int x, int y )
{
	(void)x;
	(void)y;
}

static void Menu_AddServerToList( netadr_t adr, const char *info )
{
	(void)adr;
	(void)info;
}

static void Menu_GetCursorPos( int *pos_x, int *pos_y )
{
	if( pos_x ) *pos_x = 0;
	if( pos_y ) *pos_y = 0;
}

static void Menu_SetCursorPos( int pos_x, int pos_y )
{
	(void)pos_x;
	(void)pos_y;
}

static void Menu_ShowCursor( int show )
{
	(void)show;
}

static void Menu_CharEvent( int key )
{
	(void)key;
}

static int Menu_MouseInRect( void )
{
	return 0;
}

static int Menu_IsVisible( void )
{
	return g_menu_active ? 1 : 0;
}

static int Menu_CreditsActive( void )
{
	return 0;
}

static void Menu_FinalCredits( void )
{
}

int EXPORT GetMenuAPI( UI_FUNCTIONS *pFunctionTable, ui_enginefuncs_t *engfuncs, ui_globalvars_t *pGlobals )
{
	if( !pFunctionTable || !engfuncs || !pGlobals )
		return 0;

	g_ui_eng = engfuncs;
	g_ui_globals = pGlobals;

	pFunctionTable->pfnVidInit = Menu_VidInit;
	pFunctionTable->pfnInit = Menu_Init;
	pFunctionTable->pfnShutdown = Menu_Shutdown;
	pFunctionTable->pfnRedraw = Menu_Redraw;
	pFunctionTable->pfnKeyEvent = Menu_KeyEvent;
	pFunctionTable->pfnMouseMove = Menu_MouseMove;
	pFunctionTable->pfnSetActiveMenu = Menu_SetActiveMenu;
	pFunctionTable->pfnAddServerToList = Menu_AddServerToList;
	pFunctionTable->pfnGetCursorPos = Menu_GetCursorPos;
	pFunctionTable->pfnSetCursorPos = Menu_SetCursorPos;
	pFunctionTable->pfnShowCursor = Menu_ShowCursor;
	pFunctionTable->pfnCharEvent = Menu_CharEvent;
	pFunctionTable->pfnMouseInRect = Menu_MouseInRect;
	pFunctionTable->pfnIsVisible = Menu_IsVisible;
	pFunctionTable->pfnCreditsActive = Menu_CreditsActive;
	pFunctionTable->pfnFinalCredits = Menu_FinalCredits;

	return 1;
}
