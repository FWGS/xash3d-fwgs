#include "common.h"
#include "server.h"

static enginefuncs_t *g_eng;
static globalvars_t *g_globals;

#define STUB_STRING( offset ) g_eng->pfnSzFromIndex( offset )
#define STUB_ALLOC_STRING( str ) g_eng->pfnAllocString( str )

static void Stub_Void( void )
{
}

static void Stub_VoidEdict( edict_t *ent )
{
	(void)ent;
}

static void Stub_VoidTwoEdicts( edict_t *a, edict_t *b )
{
	(void)a;
	(void)b;
}

static int Stub_IntZero( void )
{
	return 0;
}

static void Stub_Sys_Error( const char *error )
{
	if( g_eng && error )
		g_eng->pfnAlertMessage( at_error, (char *)error );
}

static const char *Stub_GetGameDescription( void )
{
	return "Half-Life (GameCube stub)";
}

static void Stub_ParseVector( const char *value, vec3_t out )
{
	char buffer[256];
	char *parse;

	VectorClear( out );
	if( !value || !value[0] )
		return;

	Q_strncpy( buffer, value, sizeof( buffer ));
	parse = buffer;
	COM_ParseVector( &parse, out, 3 );
}

static void Stub_FillEntityState( entity_state_t *state, int e, edict_t *ent )
{
	memset( state, 0, sizeof( *state ));

	state->number = e;
	state->entityType = FBitSet( ent->v.flags, FL_CUSTOMENTITY ) ? ENTITY_BEAM : ENTITY_NORMAL;
	VectorCopy( ent->v.origin, state->origin );
	VectorCopy( ent->v.angles, state->angles );
	state->modelindex = ent->v.modelindex;
	state->sequence = ent->v.sequence;
	state->frame = ent->v.frame;
	state->colormap = ent->v.colormap;
	state->skin = ent->v.skin;
	state->solid = ent->v.solid;
	state->effects = ent->v.effects;
	state->scale = ent->v.scale;
	state->rendermode = ent->v.rendermode;
	state->renderamt = ent->v.renderamt;
	state->rendercolor.r = (byte)ent->v.rendercolor[0];
	state->rendercolor.g = (byte)ent->v.rendercolor[1];
	state->rendercolor.b = (byte)ent->v.rendercolor[2];
	state->renderfx = ent->v.renderfx;
	state->movetype = ent->v.movetype;
	state->animtime = ent->v.animtime;
	state->framerate = ent->v.framerate;
	state->body = ent->v.body;
	memcpy( state->controller, ent->v.controller, sizeof( state->controller ));
	memcpy( state->blending, ent->v.blending, sizeof( state->blending ));
	VectorCopy( ent->v.velocity, state->velocity );
	VectorCopy( ent->v.mins, state->mins );
	VectorCopy( ent->v.maxs, state->maxs );
	state->aiment = ent->v.aiment ? g_eng->pfnIndexOfEdict( ent->v.aiment ) : 0;
	state->owner = ent->v.owner ? g_eng->pfnIndexOfEdict( ent->v.owner ) : 0;
	state->friction = ent->v.friction;
	state->gravity = ent->v.gravity;
	state->team = ent->v.team;
	state->health = (int)ent->v.health;
	state->spectator = FBitSet( ent->v.flags, FL_SPECTATOR );
	state->weaponmodel = ent->v.weaponmodel;
	state->gaitsequence = ent->v.gaitsequence;
	VectorCopy( ent->v.basevelocity, state->basevelocity );
}

static void Stub_ApplyClassDefaults( edict_t *ent, const char *classname )
{
	if( !classname || !classname[0] )
		return;

	if( classname[0] == 'f' && !Q_strncmp( classname, "func_", 5 ))
	{
		ent->v.solid = SOLID_BSP;
		ent->v.movetype = MOVETYPE_PUSH;
	}
	else if( !Q_strcmp( classname, "worldspawn" ))
	{
		ent->v.solid = SOLID_BSP;
		ent->v.movetype = MOVETYPE_PUSH;
	}
	else if( !Q_strncmp( classname, "trigger_", 8 ) || !Q_strncmp( classname, "info_", 5 ))
	{
		ent->v.solid = SOLID_NOT;
		ent->v.movetype = MOVETYPE_NONE;
	}
	else if( !Q_strncmp( classname, "light", 5 ))
	{
		ent->v.solid = SOLID_NOT;
		ent->v.movetype = MOVETYPE_NONE;
	}
}

static void Stub_KeyValue( edict_t *ent, KeyValueData *pkvd )
{
	const char *classname;

	if( !pkvd )
		return;

	if( !Q_strcmp( pkvd->szKeyName, "classname" ))
	{
		ent->v.classname = STUB_ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = true;
		return;
	}

	if( !Q_strcmp( pkvd->szKeyName, "origin" ))
	{
		Stub_ParseVector( pkvd->szValue, ent->v.origin );
		pkvd->fHandled = true;
		return;
	}

	if( !Q_strcmp( pkvd->szKeyName, "angles" ))
	{
		Stub_ParseVector( pkvd->szValue, ent->v.angles );
		pkvd->fHandled = true;
		return;
	}

	if( !Q_strcmp( pkvd->szKeyName, "model" ))
	{
		ent->v.model = STUB_ALLOC_STRING( pkvd->szValue );
		ent->v.modelindex = g_eng->pfnPrecacheModel( pkvd->szValue );
		pkvd->fHandled = true;
		return;
	}

	if( !Q_strcmp( pkvd->szKeyName, "targetname" ))
	{
		ent->v.targetname = STUB_ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = true;
		return;
	}

	if( !Q_strcmp( pkvd->szKeyName, "target" ))
	{
		ent->v.target = STUB_ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = true;
		return;
	}

	if( !Q_strcmp( pkvd->szKeyName, "customclass" ))
	{
		classname = pkvd->szValue;
		Stub_ApplyClassDefaults( ent, classname );
		pkvd->fHandled = true;
		return;
	}

	pkvd->fHandled = true;
}

static int Stub_Spawn( edict_t *ent )
{
	const char *classname;

	if( !ent )
		return -1;

	classname = STUB_STRING( ent->v.classname );
	Stub_ApplyClassDefaults( ent, classname );

	if( ent->v.modelindex && ent->v.solid == SOLID_NOT && classname &&
		( classname[0] == 'f' && !Q_strncmp( classname, "func_", 5 )))
	{
		ent->v.solid = SOLID_BSP;
		ent->v.movetype = MOVETYPE_PUSH;
	}

	if( classname && !Q_strcmp( classname, "info_player_start" ))
	{
		ent->v.solid = SOLID_NOT;
		ent->v.movetype = MOVETYPE_NONE;
	}

	return 0;
}

static qboolean Stub_ClientConnect( edict_t *ent, const char *name, const char *address, char reject[128] )
{
	(void)name;
	(void)address;
	(void)reject;
	if( ent )
		SetBits( ent->v.flags, FL_CLIENT );
	return true;
}

static void Stub_ClientPutInServer( edict_t *ent )
{
	if( !ent )
		return;

	ent->v.colormap = g_eng->pfnIndexOfEdict( ent );
	ent->v.team = 0;
	ent->v.fixangle = 1;
	VectorClear( ent->v.punchangle );
	ent->v.health = 100.0f;
	ent->v.max_health = 100.0f;
	ent->v.takedamage = DAMAGE_AIM;
	ent->v.solid = SOLID_SLIDEBOX;
	ent->v.movetype = MOVETYPE_WALK;
	SetBits( ent->v.flags, FL_CLIENT );
	g_eng->pfnSetOrigin( ent, ent->v.origin );
}

static void Stub_SetupVisibility( edict_t *viewent, edict_t *client, byte **pvs, byte **pas )
{
	(void)viewent;
	(void)client;
	if( pvs ) *pvs = NULL;
	if( pas ) *pas = NULL;
}

static int Stub_AddToFullPack( entity_state_t *state, int e, edict_t *ent, edict_t *host, int hostflags, int player, byte *pset )
{
	(void)host;
	(void)hostflags;
	(void)player;

	if( !state || !ent || !g_eng )
		return 0;

	if( pset && !g_eng->pfnCheckVisibility( ent, pset ))
		return 0;

	Stub_FillEntityState( state, e, ent );
	return 1;
}

static void Stub_CreateBaseline( int player, int eindex, entity_state_t *baseline, edict_t *entity, int playermodel, vec3_t player_mins, vec3_t player_maxs )
{
	(void)player;
	(void)playermodel;
	(void)player_mins;
	(void)player_maxs;

	if( !baseline || !entity )
		return;

	Stub_FillEntityState( baseline, eindex, entity );
}

static int Stub_GetHullBounds( int hullnumber, float *mins, float *maxs )
{
	if( hullnumber != 0 )
		return 0;

	if( mins )
	{
		mins[0] = -16.0f;
		mins[1] = -16.0f;
		mins[2] = -36.0f;
	}

	if( maxs )
	{
		maxs[0] = 16.0f;
		maxs[1] = 16.0f;
		maxs[2] = 36.0f;
	}

	return 1;
}

static void Generic_EntityInit( entvars_t *pev )
{
	(void)pev;
}

void EXPORT GiveFnptrsToDll( enginefuncs_t *engfuncs, globalvars_t *pGlobals )
{
	g_eng = engfuncs;
	g_globals = pGlobals;
}

static void Stub_FillAPI( DLL_FUNCTIONS *funcs )
{
	memset( funcs, 0, sizeof( *funcs ));

	funcs->pfnGameInit = Stub_Void;
	funcs->pfnSpawn = Stub_Spawn;
	funcs->pfnThink = Stub_VoidEdict;
	funcs->pfnUse = Stub_VoidTwoEdicts;
	funcs->pfnTouch = Stub_VoidTwoEdicts;
	funcs->pfnBlocked = Stub_VoidTwoEdicts;
	funcs->pfnKeyValue = Stub_KeyValue;
	funcs->pfnSave = (void (*)( edict_t *, SAVERESTOREDATA * ))Stub_VoidEdict;
	funcs->pfnRestore = (int (*)( edict_t *, SAVERESTOREDATA *, int ))Stub_IntZero;
	funcs->pfnSetAbsBox = Stub_VoidEdict;
	funcs->pfnSaveWriteFields = (void (*)( SAVERESTOREDATA *, const char *, void *, TYPEDESCRIPTION *, int ))Stub_Void;
	funcs->pfnSaveReadFields = (void (*)( SAVERESTOREDATA *, const char *, void *, TYPEDESCRIPTION *, int ))Stub_Void;
	funcs->pfnSaveGlobalState = (void (*)( SAVERESTOREDATA * ))Stub_Void;
	funcs->pfnRestoreGlobalState = (void (*)( SAVERESTOREDATA * ))Stub_Void;
	funcs->pfnResetGlobalState = Stub_Void;
	funcs->pfnClientConnect = Stub_ClientConnect;
	funcs->pfnClientDisconnect = Stub_VoidEdict;
	funcs->pfnClientKill = Stub_VoidEdict;
	funcs->pfnClientPutInServer = Stub_ClientPutInServer;
	funcs->pfnClientCommand = Stub_VoidEdict;
	funcs->pfnClientUserInfoChanged = (void (*)( edict_t *, char * ))Stub_VoidEdict;
	funcs->pfnServerActivate = (void (*)( edict_t *, int, int ))Stub_Void;
	funcs->pfnServerDeactivate = Stub_Void;
	funcs->pfnPlayerPreThink = Stub_VoidEdict;
	funcs->pfnPlayerPostThink = Stub_VoidEdict;
	funcs->pfnStartFrame = Stub_Void;
	funcs->pfnParmsNewLevel = Stub_Void;
	funcs->pfnParmsChangeLevel = Stub_Void;
	funcs->pfnGetGameDescription = Stub_GetGameDescription;
	funcs->pfnPlayerCustomization = (void (*)( edict_t *, customization_t * ))Stub_VoidEdict;
	funcs->pfnSpectatorConnect = Stub_VoidEdict;
	funcs->pfnSpectatorDisconnect = Stub_VoidEdict;
	funcs->pfnSpectatorThink = Stub_VoidEdict;
	funcs->pfnSys_Error = Stub_Sys_Error;
	funcs->pfnPM_Move = (void (*)(struct playermove_s *, qboolean))Stub_Void;
	funcs->pfnPM_Init = (void (*)(struct playermove_s *))Stub_Void;
	funcs->pfnPM_FindTextureType = (char (*)(char *))Stub_IntZero;
	funcs->pfnSetupVisibility = Stub_SetupVisibility;
	funcs->pfnUpdateClientData = (void (*)( const edict_t *, int, clientdata_t * ))Stub_Void;
	funcs->pfnAddToFullPack = Stub_AddToFullPack;
	funcs->pfnCreateBaseline = Stub_CreateBaseline;
	funcs->pfnRegisterEncoders = Stub_Void;
	funcs->pfnGetWeaponData = (int (*)( edict_t *, struct weapon_data_s * ))Stub_IntZero;
	funcs->pfnCmdStart = (void (*)( const edict_t *, const struct usercmd_s *, unsigned int ))Stub_Void;
	funcs->pfnCmdEnd = (void (*)( const edict_t * ))Stub_VoidEdict;
	funcs->pfnConnectionlessPacket = (int (*)( const struct netadr_s *, const char *, char *, int * ))Stub_IntZero;
	funcs->pfnGetHullBounds = Stub_GetHullBounds;
	funcs->pfnCreateInstancedBaselines = Stub_Void;
	funcs->pfnInconsistentFile = (int (*)( const edict_t *, const char *, char * ))Stub_IntZero;
	funcs->pfnAllowLagCompensation = Stub_IntZero;
}

int EXPORT GetEntityAPI2( DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion )
{
	if( !pFunctionTable || !interfaceVersion )
		return 0;

	*interfaceVersion = INTERFACE_VERSION;
	Stub_FillAPI( pFunctionTable );
	return 1;
}

int EXPORT GetEntityAPI( DLL_FUNCTIONS *pFunctionTable, int interfaceVersion )
{
	if( !pFunctionTable || interfaceVersion != INTERFACE_VERSION )
		return 0;

	Stub_FillAPI( pFunctionTable );
	return 1;
}

void EXPORT custom( entvars_t *pev )
{
	Generic_EntityInit( pev );
}
