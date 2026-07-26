/*
menu_stub.c - Menu stub implementation for GameCube port
Copyright (C) 2026 xash3d-gc contributors

This file provides stub implementations for the menu module that
are used when no real menu implementation is available.
*/

#include "common.h"
#include "menu_int.h"

// Stub UI_FUNCTIONS - all functions return default values
static int Stub_pfnVidInit( void )
{
	return 1;  // Success
}

static void Stub_pfnInit( void )
{
	// Do nothing
}

static void Stub_pfnShutdown( void )
{
	// Do nothing
}

static void Stub_pfnRedraw( float flTime )
{
	(void)flTime;
	// Do nothing
}

static void Stub_pfnKeyEvent( int key, int down )
{
	(void)key;
	(void)down;
	// Do nothing
}

static void Stub_pfnMouseMove( int x, int y )
{
	(void)x;
	(void)y;
	// Do nothing
}

static void Stub_pfnSetActiveMenu( int active )
{
	(void)active;
	// Do nothing
}

static void Stub_pfnAddServerToList( struct netadr_s adr, const char *info )
{
	(void)adr;
	(void)info;
	// Do nothing
}

static void Stub_pfnGetCursorPos( int *pos_x, int *pos_y )
{
	if( pos_x ) *pos_x = 0;
	if( pos_y ) *pos_y = 0;
}

static void Stub_pfnSetCursorPos( int pos_x, int pos_y )
{
	(void)pos_x;
	(void)pos_y;
	// Do nothing
}

static void Stub_pfnShowCursor( int show )
{
	(void)show;
	// Do nothing
}

static void Stub_pfnCharEvent( int key )
{
	(void)key;
	// Do nothing
}

static int Stub_pfnMouseInRect( void )
{
	return 0;  // Mouse not in rect
}

static int Stub_pfnIsVisible( void )
{
	return 0;  // Not visible
}

static int Stub_pfnCreditsActive( void )
{
	return 0;  // Not active
}

static void Stub_pfnFinalCredits( void )
{
	// Do nothing
}

// Stub UI_FUNCTIONS structure
static UI_FUNCTIONS g_stub_ui_functions = {
	.pfnVidInit = Stub_pfnVidInit,
	.pfnInit = Stub_pfnInit,
	.pfnShutdown = Stub_pfnShutdown,
	.pfnRedraw = Stub_pfnRedraw,
	.pfnKeyEvent = Stub_pfnKeyEvent,
	.pfnMouseMove = Stub_pfnMouseMove,
	.pfnSetActiveMenu = Stub_pfnSetActiveMenu,
	.pfnAddServerToList = Stub_pfnAddServerToList,
	.pfnGetCursorPos = Stub_pfnGetCursorPos,
	.pfnSetCursorPos = Stub_pfnSetCursorPos,
	.pfnShowCursor = Stub_pfnShowCursor,
	.pfnCharEvent = Stub_pfnCharEvent,
	.pfnMouseInRect = Stub_pfnMouseInRect,
	.pfnIsVisible = Stub_pfnIsVisible,
	.pfnCreditsActive = Stub_pfnCreditsActive,
	.pfnFinalCredits = Stub_pfnFinalCredits,
};

// Stub UI_EXTENDED_FUNCTIONS structure
static UI_EXTENDED_FUNCTIONS g_stub_ui_extended_functions = {
	// Extended functions not implemented in stub
};

// GetMenuAPI - returns stub UI_FUNCTIONS
int EXPORT GetMenuAPI( UI_FUNCTIONS *pFunctionTable, ui_enginefuncs_t* engfuncs, ui_globalvars_t *pGlobals )
{
	(void)engfuncs;
	(void)pGlobals;

	if( !pFunctionTable )
		return 0;

	// Copy stub functions to the provided table
	memcpy( pFunctionTable, &g_stub_ui_functions, sizeof( UI_FUNCTIONS ) );

	return 1;  // Success
}

// UIEXTENEDEDAPI - extended API (not implemented in stub)
int EXPORT GetExtAPI( int version, UI_EXTENDED_FUNCTIONS *pFunctionTable, ui_extendedfuncs_t *engfuncs )
{
	(void)version;
	(void)pFunctionTable;
	(void)engfuncs;
	return 0;  // Not implemented
}