/*
dedicated.c - stubs for dedicated server
Copyright (C) 2018 a1batross, mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/ 
#if XASH_DEDICATED
#include "common.h"
#include "mathlib.h"
#include "ref_api.h"

ref_globals_t refState;

const char *svc_strings[256] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_changing",
	"svc_version",
	"svc_setview",
	"svc_sound",
	"svc_time",
	"svc_print",
	"svc_stufftext",
	"svc_setangle",
	"svc_serverdata",
	"svc_lightstyle",
	"svc_updateuserinfo",
	"svc_deltatable",
	"svc_clientdata",
	"svc_stopsound",
	"svc_updatepings",
	"svc_particle",
	"svc_restoresound",
	"svc_spawnstatic",
	"svc_event_reliable",
	"svc_spawnbaseline",
	"svc_temp_entity",
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_event",
	"svc_soundindex",
	"svc_ambientsound",
	"svc_intermission",
	"svc_modelindex",
	"svc_cdtrack",
	"svc_serverinfo",
	"svc_eventindex",
	"svc_weaponanim",
	"svc_bspdecal",
	"svc_roomtype",
	"svc_addangle",
	"svc_usermessage",
	"svc_packetentities",
	"svc_deltapacketentities",
	"svc_chokecount",
	"svc_resourcelist",
	"svc_deltamovevars",
	"svc_customization",
	"svc_unused46",
	"svc_crosshairangle",
	"svc_soundfade",
	"svc_unused49",
	"svc_unused50",
	"svc_director",
	"svc_studiodecal",
	"svc_unused53",
	"svc_unused54",
	"svc_unused55",
	"svc_unused56",
	"svc_querycvarvalue",
	"svc_querycvarvalue2",
	"svc_unused59",
	"svc_unused60",
	"svc_unused61",
	"svc_unused62",
	"svc_unused63",
};

void CL_ProcessFile( qboolean successfully_received, const char *filename )
{

}

int CL_Active( void )
{
	return false;
}

qboolean CL_Initialized( void )
{
	return false;
}

qboolean CL_IsInGame( void )
{
	return true;	// always active for dedicated servers
}

qboolean CL_IsInMenu( void )
{
	return false;
}

qboolean CL_IsInConsole( void )
{
	return false;
}

qboolean CL_IsIntermission( void )
{
	return false;
}

qboolean CL_IsPlaybackDemo( void )
{
	return false;
}

qboolean CL_IsRecordDemo( void )
{
	return false;
}


qboolean CL_DisableVisibility( void )
{
	return false;
}

qboolean CL_IsBackgroundDemo( void )
{
	return false;
}

qboolean CL_IsBackgroundMap( void )
{
	return false;
}

void CL_Init( void )
{

}

void Key_Init( void )
{

}

void IN_Init( void )
{

}

void CL_Drop( void )
{

}

void CL_ClearEdicts( void )
{

}

void Key_SetKeyDest(int key_dest)
{

}

void UI_SetActiveMenu( qboolean fActive )
{

}

void CL_WriteMessageHistory( void )
{

}

void Host_ClientBegin( void )
{

}

void Host_ClientFrame( void )
{

}

void Host_InputFrame( void )
{
	Cbuf_Execute( );
}

void VID_InitDefaultResolution( void )
{
	
}

void Con_Init( void )
{
	
}

void R_ClearAllDecals( void )
{

}
int R_CreateDecalList( struct decallist_s *pList )
{
	return 0;
}

void S_StopSound(int entnum, int channel, const char *soundname)
{

}

int S_GetCurrentStaticSounds( soundlist_t *pout, int size )
{
	return 0;
}

int CL_GetMaxClients( void )
{
	return 0;
}

void IN_TouchInitConfig( void )
{

}

void CL_Disconnect( void )
{

}

void CL_Shutdown( void )
{

}

void R_ClearStaticEntities( void )
{

}

void Host_Credits( void )
{

}

qboolean UI_CreditsActive( void )
{
	return false;
}

void S_StopBackgroundTrack( void )
{

}

void SCR_BeginLoadingPlaque( qboolean is_background )
{

}

int S_GetCurrentDynamicSounds( soundlist_t *pout, int size )
{
	return 0;
}

void S_StopAllSounds( qboolean ambient )
{

}

void Con_NPrintf( int idx, const char *fmt, ... )
{

}

void Con_NXPrintf( struct  con_nprint_s *info, const char *fmt, ... )
{

}

const byte *GL_TextureData( unsigned int texnum )
{
	return NULL;
}

void SCR_CheckStartupVids( void )
{

}

void Sys_SetClipboardData( const char *text, size_t size )
{

}

void CL_StopPlayback( void )
{

}

void CL_ClearStaticEntities( void )
{

}

void UI_ShowConnectionWarning( void )
{

}

void CL_Crashed( void )
{
}

#endif // XASH_DEDICATED
