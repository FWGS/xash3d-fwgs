#include "common.h"
#include "menu_int.h"
#include "keydefs.h"

static ui_enginefuncs_t *g_ui_eng;
static ui_globalvars_t *g_ui_globals;
static qboolean g_menu_active;
static int g_menu_selection;

typedef struct menu_item_s
{
	const char *title;
	const char *description;
	const char *command;
} menu_item_t;

static const menu_item_t g_menu_items[] =
{
	{ "New Game", "Start a new single player game.", "newgame\n" },
	{ "Load Game", "Load a previously saved game.", "menu_loadgame\n" },
	{ "Options", "Change game settings, configure controls.", "menu_options\n" },
};

static void Menu_DrawLine( int x, int y, const char *text, int r, int g, int b )
{
	int width = 0, height = 0;

	if( !g_ui_eng || !text )
		return;

	g_ui_eng->pfnDrawSetTextColor( r, g, b, 255 );
	g_ui_eng->pfnDrawConsoleStringLen( text, &width, &height );
	g_ui_eng->pfnDrawConsoleString( x, y, text );
}

static void Menu_ActivateSelection( void )
{
	if( !g_ui_eng )
		return;

	switch( g_menu_selection )
	{
	case 0:
		g_menu_active = false;
		g_ui_eng->pfnSetKeyDest( key_game );
		g_ui_eng->pfnClientCmd( 1, g_menu_items[g_menu_selection].command );
		break;
	case 1:
		g_ui_eng->pfnClientCmd( 1, g_menu_items[g_menu_selection].command );
		break;
	case 2:
		g_ui_eng->pfnClientCmd( 1, g_menu_items[g_menu_selection].command );
		break;
	default:
		break;
	}
}

static int Menu_VidInit( void )
{
	return 1;
}

static void Menu_Init( void )
{
	g_menu_selection = 0;
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

	{
		int base_x = ( 70 * g_ui_globals->scrWidth ) / 640;
		int desc_x = ( 192 * g_ui_globals->scrWidth ) / 640;
		int base_y = ( 249 * g_ui_globals->scrHeight ) / 480;
		int step_y = ( 31 * g_ui_globals->scrHeight ) / 480;
		size_t i;

		for( i = 0; i < ARRAYSIZE( g_menu_items ); ++i )
		{
			int y = base_y + ( step_y * (int)i );
			qboolean selected = ( i == (size_t)g_menu_selection );

			Menu_DrawLine( base_x, y, g_menu_items[i].title,
				selected ? 255 : 224,
				selected ? 196 : 170,
				selected ? 32 : 48 );
			Menu_DrawLine( desc_x, y, g_menu_items[i].description, 96, 96, 96 );
		}
	}
}

static void Menu_KeyEvent( int key, int down )
{
	if( !down || !g_menu_active )
		return;

	switch( key )
	{
	case K_UPARROW:
	case K_DPAD_UP:
		g_menu_selection--;
		if( g_menu_selection < 0 )
			g_menu_selection = (int)ARRAYSIZE( g_menu_items ) - 1;
		break;
	case K_DOWNARROW:
	case K_DPAD_DOWN:
		g_menu_selection++;
		if( g_menu_selection >= (int)ARRAYSIZE( g_menu_items ))
			g_menu_selection = 0;
		break;
	case K_ENTER:
	case K_A_BUTTON:
	case K_START_BUTTON:
		Menu_ActivateSelection();
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
	g_menu_selection = 0;

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
