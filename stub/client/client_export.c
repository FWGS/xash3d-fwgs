#include "common.h"
#if !XASH_GAMECUBE_HLSDK_STATIC
#include "client.h"
#endif
#include "gamecube/dll_gamecube.h"

#if XASH_GAMECUBE_HLSDK_STATIC
extern void EXPORT Initialize( void );
extern void EXPORT HUD_VidInit( void );
extern void EXPORT HUD_Init( void );
extern void EXPORT HUD_Shutdown( void );
extern void EXPORT HUD_Redraw( void );
extern void EXPORT HUD_UpdateClientData( void );
extern void EXPORT HUD_Reset( void );
extern void EXPORT HUD_PlayerMove( void );
extern void EXPORT HUD_PlayerMoveInit( void );
extern void EXPORT HUD_PlayerMoveTexture( void );
extern void EXPORT HUD_ConnectionlessPacket( void );
extern void EXPORT HUD_GetHullBounds( void );
extern void EXPORT HUD_Frame( void );
extern void EXPORT HUD_PostRunCmd( void );
extern void EXPORT HUD_Key_Event( void );
extern void EXPORT HUD_AddEntity( void );
extern void EXPORT HUD_CreateEntities( void );
extern void EXPORT HUD_StudioEvent( void );
extern void EXPORT HUD_TxferLocalOverrides( void );
extern void EXPORT HUD_ProcessPlayerState( void );
extern void EXPORT HUD_TxferPredictionData( void );
extern void EXPORT HUD_TempEntUpdate( void );
extern void EXPORT HUD_DrawNormalTriangles( void );
extern void EXPORT HUD_DrawTransparentTriangles( void );
extern void EXPORT HUD_GetUserEntity( void );
extern void EXPORT Demo_ReadBuffer( void );
extern void EXPORT CAM_Think( void );
extern void EXPORT CL_IsThirdPerson( void );
extern void EXPORT CL_CameraOffset( void );
extern void EXPORT CL_CreateMove( void );
extern void EXPORT IN_ActivateMouse( void );
extern void EXPORT IN_DeactivateMouse( void );
extern void EXPORT IN_MouseEvent( void );
extern void EXPORT IN_Accumulate( void );
extern void EXPORT IN_ClearStates( void );
extern void EXPORT V_CalcRefdef( void );
extern void EXPORT KB_Find( void );
extern void EXPORT HUD_GetStudioModelInterface( void );
extern void EXPORT HUD_DirectorMessage( void );
extern void EXPORT HUD_VoiceStatus( void );
#else
extern void EXPORT GetClientAPI( cldll_func_t *funcs );
#endif

static dllexport_t gamecube_client_exports[] =
{
#if XASH_GAMECUBE_HLSDK_STATIC
	{ "Initialize", (void *)Initialize },
	{ "HUD_VidInit", (void *)HUD_VidInit },
	{ "HUD_Init", (void *)HUD_Init },
	{ "HUD_Shutdown", (void *)HUD_Shutdown },
	{ "HUD_Redraw", (void *)HUD_Redraw },
	{ "HUD_UpdateClientData", (void *)HUD_UpdateClientData },
	{ "HUD_Reset", (void *)HUD_Reset },
	{ "HUD_PlayerMove", (void *)HUD_PlayerMove },
	{ "HUD_PlayerMoveInit", (void *)HUD_PlayerMoveInit },
	{ "HUD_PlayerMoveTexture", (void *)HUD_PlayerMoveTexture },
	{ "HUD_ConnectionlessPacket", (void *)HUD_ConnectionlessPacket },
	{ "HUD_GetHullBounds", (void *)HUD_GetHullBounds },
	{ "HUD_Frame", (void *)HUD_Frame },
	{ "HUD_PostRunCmd", (void *)HUD_PostRunCmd },
	{ "HUD_Key_Event", (void *)HUD_Key_Event },
	{ "HUD_AddEntity", (void *)HUD_AddEntity },
	{ "HUD_CreateEntities", (void *)HUD_CreateEntities },
	{ "HUD_StudioEvent", (void *)HUD_StudioEvent },
	{ "HUD_TxferLocalOverrides", (void *)HUD_TxferLocalOverrides },
	{ "HUD_ProcessPlayerState", (void *)HUD_ProcessPlayerState },
	{ "HUD_TxferPredictionData", (void *)HUD_TxferPredictionData },
	{ "HUD_TempEntUpdate", (void *)HUD_TempEntUpdate },
	{ "HUD_DrawNormalTriangles", (void *)HUD_DrawNormalTriangles },
	{ "HUD_DrawTransparentTriangles", (void *)HUD_DrawTransparentTriangles },
	{ "HUD_GetUserEntity", (void *)HUD_GetUserEntity },
	{ "Demo_ReadBuffer", (void *)Demo_ReadBuffer },
	{ "CAM_Think", (void *)CAM_Think },
	{ "CL_IsThirdPerson", (void *)CL_IsThirdPerson },
	{ "CL_CameraOffset", (void *)CL_CameraOffset },
	{ "CL_CreateMove", (void *)CL_CreateMove },
	{ "IN_ActivateMouse", (void *)IN_ActivateMouse },
	{ "IN_DeactivateMouse", (void *)IN_DeactivateMouse },
	{ "IN_MouseEvent", (void *)IN_MouseEvent },
	{ "IN_Accumulate", (void *)IN_Accumulate },
	{ "IN_ClearStates", (void *)IN_ClearStates },
	{ "V_CalcRefdef", (void *)V_CalcRefdef },
	{ "KB_Find", (void *)KB_Find },
	{ "HUD_GetStudioModelInterface", (void *)HUD_GetStudioModelInterface },
	{ "HUD_DirectorMessage", (void *)HUD_DirectorMessage },
	{ "HUD_VoiceStatus", (void *)HUD_VoiceStatus },
#else
	{ "GetClientAPI", (void *)GetClientAPI },
#endif
	{ NULL, NULL },
};

int setup_gamecube_client_exports( void )
{
	int ret = 0;

	ret |= dll_register( "cl_dlls/client_gamecube_ppc.so", gamecube_client_exports );
	ret |= dll_register( "cl_dlls/client.dll", gamecube_client_exports );
	ret |= dll_register( "cl_dlls/client.so", gamecube_client_exports );
	ret |= dll_register( "client.so", gamecube_client_exports );
	ret |= dll_register( "client", gamecube_client_exports );

	return ret;
}
