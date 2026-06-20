#include "common.h"
#include "client.h"
#include "build.h"
#include "pm_shared.h"

static int Stub_Initialize( cl_enginefunc_t *pEnginefuncs, int iVersion )
{
	(void)pEnginefuncs;
	(void)iVersion;
	return 1;
}

static void Stub_Void( void )
{
}

static void Stub_PlayerMove( struct playermove_s *pm, int server )
{
	PM_Move( pm, server );
}

static void Stub_PlayerMoveInit( struct playermove_s *pm )
{
	PM_Init( pm );
}

static int Stub_Int( void )
{
	return 1;
}

static int Stub_Redraw( float flTime, int intermission )
{
	(void)flTime;
	(void)intermission;
	return 1;
}

static int Stub_UpdateClientData( client_data_t *cdata, float flTime )
{
	(void)cdata;
	(void)flTime;
	return 1;
}

static char Stub_PlayerMoveTexture( char *name )
{
	return PM_FindTextureType( name );
}

static void Stub_CreateMove( float frametime, struct usercmd_s *cmd, int active )
{
	(void)frametime;
	(void)active;
	if( cmd )
		memset( cmd, 0, sizeof( *cmd ));
}

static void *Stub_KB_Find( const char *name )
{
	(void)name;
	return NULL;
}

static int Stub_AddEntity( int type, cl_entity_t *ent, const char *modelname )
{
	(void)type;
	(void)ent;
	(void)modelname;
	return 0;
}

static int Stub_ConnectionlessPacket( const struct netadr_s *net_from, const char *args, char *buffer, int *size )
{
	(void)net_from;
	(void)args;
	(void)buffer;
	(void)size;
	return 0;
}

static int Stub_GetHullBounds( int hullnumber, float *mins, float *maxs )
{
	(void)hullnumber;
	if( mins ) VectorClear( mins );
	if( maxs ) VectorClear( maxs );
	return 0;
}

static void Stub_Frame( double time )
{
	(void)time;
}

static int Stub_Key_Event( int eventcode, int keynum, const char *pszCurrentBinding )
{
	(void)eventcode;
	(void)keynum;
	(void)pszCurrentBinding;
	return 0;
}

static void Stub_TempEntUpdate( double frametime, double client_time, double cl_gravity, struct tempent_s **ppTempEntFree, struct tempent_s **ppTempEntActive, int ( *Callback_AddVisibleEntity )( cl_entity_t *pEntity ), void ( *Callback_TempEntPlaySound )( struct tempent_s *pTemp, float damp ))
{
	(void)frametime;
	(void)client_time;
	(void)cl_gravity;
	(void)ppTempEntFree;
	(void)ppTempEntActive;
	(void)Callback_AddVisibleEntity;
	(void)Callback_TempEntPlaySound;
}

static cl_entity_t *Stub_GetUserEntity( int index )
{
	(void)index;
	return NULL;
}

void EXPORT GetClientAPI( cldll_func_t *funcs )
{
	memset( funcs, 0, sizeof( *funcs ));

	funcs->pfnInitialize = Stub_Initialize;
	funcs->pfnInit = Stub_Void;
	funcs->pfnVidInit = Stub_Int;
	funcs->pfnRedraw = Stub_Redraw;
	funcs->pfnUpdateClientData = Stub_UpdateClientData;
	funcs->pfnReset = Stub_Void;
	funcs->pfnPlayerMove = Stub_PlayerMove;
	funcs->pfnPlayerMoveInit = Stub_PlayerMoveInit;
	funcs->pfnPlayerMoveTexture = Stub_PlayerMoveTexture;
	funcs->IN_ActivateMouse = Stub_Void;
	funcs->IN_DeactivateMouse = Stub_Void;
	funcs->IN_MouseEvent = (void (*)(int))Stub_Void;
	funcs->IN_ClearStates = Stub_Void;
	funcs->IN_Accumulate = Stub_Void;
	funcs->CL_CreateMove = Stub_CreateMove;
	funcs->CL_IsThirdPerson = Stub_Int;
	funcs->CL_CameraOffset = (void (*)(float *))Stub_Void;
	funcs->KB_Find = Stub_KB_Find;
	funcs->CAM_Think = Stub_Void;
	funcs->pfnCalcRefdef = (void (*)(ref_params_t *))Stub_Void;
	funcs->pfnAddEntity = Stub_AddEntity;
	funcs->pfnCreateEntities = Stub_Void;
	funcs->pfnDrawNormalTriangles = Stub_Void;
	funcs->pfnDrawTransparentTriangles = Stub_Void;
	funcs->pfnStudioEvent = (void (*)(const struct mstudioevent_s *, const cl_entity_t *))Stub_Void;
	funcs->pfnPostRunCmd = (void (*)(struct local_state_s *, struct local_state_s *, usercmd_t *, int, double, unsigned int))Stub_Void;
	funcs->pfnShutdown = Stub_Void;
	funcs->pfnTxferLocalOverrides = (void (*)(entity_state_t *, const clientdata_t *))Stub_Void;
	funcs->pfnProcessPlayerState = (void (*)(entity_state_t *, const entity_state_t *))Stub_Void;
	funcs->pfnTxferPredictionData = (void (*)(entity_state_t *, const entity_state_t *, clientdata_t *, const clientdata_t *, weapon_data_t *, const weapon_data_t *))Stub_Void;
	funcs->pfnDemo_ReadBuffer = (void (*)(int, byte *))Stub_Void;
	funcs->pfnConnectionlessPacket = Stub_ConnectionlessPacket;
	funcs->pfnGetHullBounds = Stub_GetHullBounds;
	funcs->pfnFrame = Stub_Frame;
	funcs->pfnKey_Event = Stub_Key_Event;
	funcs->pfnTempEntUpdate = Stub_TempEntUpdate;
	funcs->pfnGetUserEntity = Stub_GetUserEntity;
}
