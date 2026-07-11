#include "common.h"
#include "client.h"
#include "gamecube/dll_gamecube.h"

#if XASH_GAMECUBE_HLSDK_STATIC
#define GC_CLIENT_SYMBOL( name ) gamecube_hlsdk_client_##name

extern int EXPORT GC_CLIENT_SYMBOL( Initialize )( cl_enginefunc_t *pEnginefuncs, int iVersion );
extern int EXPORT GC_CLIENT_SYMBOL( HUD_VidInit )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_Init )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_Shutdown )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_Redraw )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_UpdateClientData )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_Reset )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_PlayerMove )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_PlayerMoveInit )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_PlayerMoveTexture )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_ConnectionlessPacket )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_GetHullBounds )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_Frame )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_PostRunCmd )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_Key_Event )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_AddEntity )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_CreateEntities )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_StudioEvent )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_TxferLocalOverrides )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_ProcessPlayerState )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_TxferPredictionData )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_TempEntUpdate )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_DrawNormalTriangles )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_DrawTransparentTriangles )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_GetUserEntity )( void );
extern void EXPORT GC_CLIENT_SYMBOL( Demo_ReadBuffer )( void );
extern void EXPORT GC_CLIENT_SYMBOL( CAM_Think )( void );
extern void EXPORT GC_CLIENT_SYMBOL( CL_IsThirdPerson )( void );
extern void EXPORT GC_CLIENT_SYMBOL( CL_CameraOffset )( void );
extern void EXPORT GC_CLIENT_SYMBOL( CL_CreateMove )( void );
extern void EXPORT GC_CLIENT_SYMBOL( IN_ActivateMouse )( void );
extern void EXPORT GC_CLIENT_SYMBOL( IN_DeactivateMouse )( void );
extern void EXPORT GC_CLIENT_SYMBOL( IN_MouseEvent )( void );
extern void EXPORT GC_CLIENT_SYMBOL( IN_Accumulate )( void );
extern void EXPORT GC_CLIENT_SYMBOL( IN_ClearStates )( void );
extern void EXPORT GC_CLIENT_SYMBOL( V_CalcRefdef )( void );
extern void EXPORT GC_CLIENT_SYMBOL( KB_Find )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_GetStudioModelInterface )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_DirectorMessage )( void );
extern void EXPORT GC_CLIENT_SYMBOL( HUD_VoiceStatus )( void );

static int EXPORT GC_Client_Initialize_Bridge( cl_enginefunc_t *pEnginefuncs, int iVersion )
{
	Con_Reportf( "Xash3D GameCube: client bridge Initialize begin version=%d\n", iVersion );
	int ret = GC_CLIENT_SYMBOL( Initialize )( pEnginefuncs, iVersion );
	Con_Reportf( "Xash3D GameCube: client bridge Initialize end ret=%d\n", ret );
	return ret;
}

static int EXPORT GC_Client_HUD_VidInit_Bridge( void )
{
	Con_Reportf( "Xash3D GameCube: client bridge HUD_VidInit begin\n" );
	int ret = GC_CLIENT_SYMBOL( HUD_VidInit )();
	Con_Reportf( "Xash3D GameCube: client bridge HUD_VidInit end ret=%d\n", ret );
	if( ret )
	{
		extern void GC_ArmPostMapFrameBudgetSamples( void );
		GC_ArmPostMapFrameBudgetSamples();
	}
	return ret;
}

static void EXPORT GC_Client_HUD_Init_Bridge( void )
{
	Con_Reportf( "Xash3D GameCube: client bridge HUD_Init begin\n" );
	GC_CLIENT_SYMBOL( HUD_Init )();
	Con_Reportf( "Xash3D GameCube: client bridge HUD_Init end\n" );
}
#else
extern void EXPORT GetClientAPI( cldll_func_t *funcs );
#endif

static dllexport_t gamecube_client_exports[] =
{
#if XASH_GAMECUBE_HLSDK_STATIC
	{ "Initialize", (void *)GC_Client_Initialize_Bridge },
	{ "HUD_VidInit", (void *)GC_Client_HUD_VidInit_Bridge },
	{ "HUD_Init", (void *)GC_Client_HUD_Init_Bridge },
	{ "HUD_Shutdown", (void *)GC_CLIENT_SYMBOL( HUD_Shutdown ) },
	{ "HUD_Redraw", (void *)GC_CLIENT_SYMBOL( HUD_Redraw ) },
	{ "HUD_UpdateClientData", (void *)GC_CLIENT_SYMBOL( HUD_UpdateClientData ) },
	{ "HUD_Reset", (void *)GC_CLIENT_SYMBOL( HUD_Reset ) },
	{ "HUD_PlayerMove", (void *)GC_CLIENT_SYMBOL( HUD_PlayerMove ) },
	{ "HUD_PlayerMoveInit", (void *)GC_CLIENT_SYMBOL( HUD_PlayerMoveInit ) },
	{ "HUD_PlayerMoveTexture", (void *)GC_CLIENT_SYMBOL( HUD_PlayerMoveTexture ) },
	{ "HUD_ConnectionlessPacket", (void *)GC_CLIENT_SYMBOL( HUD_ConnectionlessPacket ) },
	{ "HUD_GetHullBounds", (void *)GC_CLIENT_SYMBOL( HUD_GetHullBounds ) },
	{ "HUD_Frame", (void *)GC_CLIENT_SYMBOL( HUD_Frame ) },
	{ "HUD_PostRunCmd", (void *)GC_CLIENT_SYMBOL( HUD_PostRunCmd ) },
	{ "HUD_Key_Event", (void *)GC_CLIENT_SYMBOL( HUD_Key_Event ) },
	{ "HUD_AddEntity", (void *)GC_CLIENT_SYMBOL( HUD_AddEntity ) },
	{ "HUD_CreateEntities", (void *)GC_CLIENT_SYMBOL( HUD_CreateEntities ) },
	{ "HUD_StudioEvent", (void *)GC_CLIENT_SYMBOL( HUD_StudioEvent ) },
	{ "HUD_TxferLocalOverrides", (void *)GC_CLIENT_SYMBOL( HUD_TxferLocalOverrides ) },
	{ "HUD_ProcessPlayerState", (void *)GC_CLIENT_SYMBOL( HUD_ProcessPlayerState ) },
	{ "HUD_TxferPredictionData", (void *)GC_CLIENT_SYMBOL( HUD_TxferPredictionData ) },
	{ "HUD_TempEntUpdate", (void *)GC_CLIENT_SYMBOL( HUD_TempEntUpdate ) },
	{ "HUD_DrawNormalTriangles", (void *)GC_CLIENT_SYMBOL( HUD_DrawNormalTriangles ) },
	{ "HUD_DrawTransparentTriangles", (void *)GC_CLIENT_SYMBOL( HUD_DrawTransparentTriangles ) },
	{ "HUD_GetUserEntity", (void *)GC_CLIENT_SYMBOL( HUD_GetUserEntity ) },
	{ "Demo_ReadBuffer", (void *)GC_CLIENT_SYMBOL( Demo_ReadBuffer ) },
	{ "CAM_Think", (void *)GC_CLIENT_SYMBOL( CAM_Think ) },
	{ "CL_IsThirdPerson", (void *)GC_CLIENT_SYMBOL( CL_IsThirdPerson ) },
	{ "CL_CameraOffset", (void *)GC_CLIENT_SYMBOL( CL_CameraOffset ) },
	{ "CL_CreateMove", (void *)GC_CLIENT_SYMBOL( CL_CreateMove ) },
	{ "IN_ActivateMouse", (void *)GC_CLIENT_SYMBOL( IN_ActivateMouse ) },
	{ "IN_DeactivateMouse", (void *)GC_CLIENT_SYMBOL( IN_DeactivateMouse ) },
	{ "IN_MouseEvent", (void *)GC_CLIENT_SYMBOL( IN_MouseEvent ) },
	{ "IN_Accumulate", (void *)GC_CLIENT_SYMBOL( IN_Accumulate ) },
	{ "IN_ClearStates", (void *)GC_CLIENT_SYMBOL( IN_ClearStates ) },
	{ "V_CalcRefdef", (void *)GC_CLIENT_SYMBOL( V_CalcRefdef ) },
	{ "KB_Find", (void *)GC_CLIENT_SYMBOL( KB_Find ) },
	{ "HUD_GetStudioModelInterface", (void *)GC_CLIENT_SYMBOL( HUD_GetStudioModelInterface ) },
	{ "HUD_DirectorMessage", (void *)GC_CLIENT_SYMBOL( HUD_DirectorMessage ) },
	{ "HUD_VoiceStatus", (void *)GC_CLIENT_SYMBOL( HUD_VoiceStatus ) },
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
