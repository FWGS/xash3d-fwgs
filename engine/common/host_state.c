/*
host_cmd.c - dedicated and normal host
Copyright (C) 2017 Uncle Mike

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
#include "platform/platform.h"

static jmp_buf g_abortframe;

void COM_InitHostState( void )
{
	memset( GameState, 0, sizeof( game_status_t ));
}

static void Host_SetState( host_state_t newState, qboolean clearNext )
{
	if( clearNext )
		GameState->nextstate = newState;
	GameState->curstate = newState;

	if( clearNext && newState == STATE_RUNFRAME )
	{
		// states finished here
		GameState->backgroundMap = false;
		GameState->loadGame = false;
		GameState->newGame = false;
	}
}

static void Host_SetNextState( host_state_t nextState )
{
	ASSERT( GameState->curstate == STATE_RUNFRAME );
	GameState->nextstate = nextState;
}

void COM_NewGame( char const *pMapName )
{
	if( GameState->nextstate != STATE_RUNFRAME )
		return;

	if( UI_CreditsActive( ))
		return;

	Q_strncpy( GameState->levelName, pMapName, sizeof( GameState->levelName ));
	Host_SetNextState( STATE_LOAD_LEVEL );

	GameState->backgroundMap = false;
	GameState->landmarkName[0] = 0;
	GameState->loadGame = false;
	GameState->newGame = true;

	if( !SV_Active( ))
		CL_Disconnect( ); // disconnect from current online game

	SV_ShutdownGame(); // exit from current game
}

void COM_LoadLevel( char const *pMapName, qboolean background )
{
	if( GameState->nextstate != STATE_RUNFRAME )
		return;

	if( UI_CreditsActive( ))
		return;

	Q_strncpy( GameState->levelName, pMapName, sizeof( GameState->levelName ));
	Host_SetNextState( STATE_LOAD_LEVEL );

	GameState->backgroundMap = background;
	GameState->landmarkName[0] = 0;
	GameState->loadGame = false;
	GameState->newGame = false;

	if( !SV_Active( ))
		CL_Disconnect( ); // disconnect from current online game

	SV_ShutdownGame(); // exit from current game
}

void COM_LoadGame( char const *pMapName )
{
	if( GameState->nextstate != STATE_RUNFRAME )
		return;

	if( UI_CreditsActive( ))
		return;

	Q_strncpy( GameState->levelName, pMapName, sizeof( GameState->levelName ));
	Host_SetNextState( STATE_LOAD_GAME );
	GameState->backgroundMap = false;
	GameState->newGame = false;
	GameState->loadGame = true;

	if( !SV_Active( ))
		CL_Disconnect( ); // disconnect from current online game
}

void COM_ChangeLevel( char const *pNewLevel, char const *pLandmarkName, qboolean background )
{
	if( GameState->nextstate != STATE_RUNFRAME )
		return;

	if( UI_CreditsActive( ))
		return;

	Q_strncpy( GameState->levelName, pNewLevel, sizeof( GameState->levelName ));
	GameState->backgroundMap = background;

	if( !COM_StringEmptyOrNULL( pLandmarkName ))
	{
		Q_strncpy( GameState->landmarkName, pLandmarkName, sizeof( GameState->landmarkName ));
		GameState->loadGame = true;
	}
	else
	{
		GameState->landmarkName[0] = 0;
		GameState->loadGame = false;
	}

	Host_SetNextState( STATE_CHANGELEVEL );
	GameState->newGame = false;
}

static void Host_ShutdownGame( void )
{
	SV_ShutdownGame();

	switch( GameState->nextstate )
	{
	case STATE_LOAD_GAME:
	case STATE_LOAD_LEVEL:
		Host_SetState( GameState->nextstate, true );
		break;
	default:
		Host_SetState( STATE_RUNFRAME, true );
		break;
	}
}

static void Host_RunFrame( double time )
{
	// at this time, we don't need to get events from OS on dedicated
#if !XASH_DEDICATED
	Platform_RunEvents();
#endif // XASH_DEDICATED

	// engine main frame
	Host_Frame( time );

	switch( GameState->nextstate )
	{
	case STATE_RUNFRAME:
		break;
	case STATE_LOAD_GAME:
	case STATE_LOAD_LEVEL:
		SCR_BeginLoadingPlaque( GameState->backgroundMap );
		// intentionally fallthrough
	case STATE_GAME_SHUTDOWN:
		Host_SetState( STATE_GAME_SHUTDOWN, false );
		break;
	case STATE_CHANGELEVEL:
		SCR_BeginLoadingPlaque( GameState->backgroundMap );
		Host_SetState( GameState->nextstate, true );
		break;
	default:
		Host_SetState( STATE_RUNFRAME, true );
		break;
	}
}

/*
================
Host_AbortCurrentFrame

aborts the current host frame and goes on with the next one
================
*/
void Host_AbortCurrentFrame( void )
{
	longjmp( g_abortframe, 1 );
}

void COM_Frame( double time )
{
	int	loopCount = 0;

	if( setjmp( g_abortframe ))
		return;

	while( 1 )
	{
		int	oldState = GameState->curstate;

		// execute the current state (and transition to the next state if not in STATE_RUNFRAME)
		switch( GameState->curstate )
		{
		case STATE_LOAD_LEVEL:
			SV_ExecLoadLevel();
			Host_SetState( STATE_RUNFRAME, true );
			break;
		case STATE_LOAD_GAME:
			SV_ExecLoadGame();
			Host_SetState( STATE_RUNFRAME, true );
			break;
		case STATE_CHANGELEVEL:
			SV_ExecChangeLevel();
			Host_SetState( STATE_RUNFRAME, true );
			break;
		case STATE_RUNFRAME:
			Host_RunFrame( time );
			break;
		case STATE_GAME_SHUTDOWN:
			Host_ShutdownGame();
			break;
		}

		if( oldState == STATE_RUNFRAME )
			break;

		if(( GameState->curstate == oldState ) || ( ++loopCount > 8 ))
			Sys_Error( "state infinity loop!\n" );
	}
}
