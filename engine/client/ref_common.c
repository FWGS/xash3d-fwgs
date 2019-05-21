#include "common.h"
#include "client.h"
#include "library.h"
#include "cl_tent.h"
#include "platform/platform.h"
#include "vid_common.h"

struct ref_state_s ref;
ref_globals_t refState;

convar_t *gl_vsync;
convar_t *gl_showtextures;
convar_t *r_decals;
convar_t *r_adjust_fov;
convar_t *gl_wgl_msaa_samples;

void R_GetTextureParms( int *w, int *h, int texnum )
{
	if( w ) *w = REF_GET_PARM( PARM_TEX_WIDTH, texnum );
	if( h ) *h = REF_GET_PARM( PARM_TEX_HEIGHT, texnum );
}

/*
================
GL_FreeImage

Frees image by name
================
*/
void GL_FreeImage( const char *name )
{
	int	texnum;

	if(( texnum = ref.dllFuncs.GL_FindTexture( name )) != 0 )
		 ref.dllFuncs.GL_FreeTexture( texnum );
}

void GL_RenderFrame( const ref_viewpass_t *rvp )
{
	refState.time      = cl.time;
	refState.oldtime   = cl.oldtime;
	refState.realtime  = host.realtime;
	refState.frametime = host.frametime;

	VectorCopy( rvp->vieworigin, refState.vieworg );
	VectorCopy( rvp->viewangles, refState.viewangles );
	AngleVectors( refState.viewangles, refState.vforward, refState.vright, refState.vup );

	ref.dllFuncs.GL_RenderFrame( rvp );
}

static int pfnEngineGetParm( int parm, int arg )
{
	return CL_RenderGetParm( parm, arg, false ); // prevent recursion
}

static void pfnCbuf_SetOpenGLConfigHack( qboolean set )
{
	host.apply_opengl_config = set;
}

static world_static_t *pfnGetWorld( void )
{
	return &world;
}

static void pfnStudioEvent( const mstudioevent_t *event, const cl_entity_t *e )
{
	clgame.dllFuncs.pfnStudioEvent( event, e );
}

static efrag_t* pfnGetEfragsFreeList( void )
{
	return clgame.free_efrags;
}

static void pfnSetEfragsFreeList( efrag_t *list )
{
	clgame.free_efrags = list;
}

static model_t *pfnGetDefaultSprite( enum ref_defaultsprite_e spr )
{
	switch( spr )
	{
	case REF_DOT_SPRITE: return cl_sprite_dot;
	case REF_CHROME_SPRITE: return cl_sprite_shell;
	default: Host_Error( "GetDefaultSprite: unknown sprite %d\n", spr );
	}
	return NULL;
}

static void *pfnMod_Extradata( int type, model_t *m )
{
	switch( type )
	{
	case mod_alias: return Mod_AliasExtradata( m );
	case mod_studio: return Mod_StudioExtradata( m );
	case mod_sprite: // fallthrough
	case mod_brush: return NULL;
	default: Host_Error( "Mod_Extradata: unknown type %d\n", type );
	}
	return NULL;
}

static model_t *pfnMod_GetCurrentLoadingModel( void )
{
	return loadmodel;
}

static void pfnMod_SetCurrentLoadingModel( model_t *m )
{
	loadmodel = m;
}

static void pfnGetPredictedOrigin( vec3_t v )
{
	VectorCopy( cl.simorg, v );
}

static color24 *pfnCL_GetPaletteColor( int color ) // clgame.palette[color]
{
	return &clgame.palette[color];
}

static void pfnCL_GetScreenInfo( int *width, int *height ) // clgame.scrInfo, ptrs may be NULL
{
	if( width ) *width = clgame.scrInfo.iWidth;
	if( height ) *height = clgame.scrInfo.iHeight;
}

static void pfnSetLocalLightLevel( int level )
{
	cl.local.light_level = level;
}

/*
===============
pfnPlayerInfo

===============
*/
static player_info_t *pfnPlayerInfo( int index )
{
	if( index == -1 ) // special index for menu
		return &gameui.playerinfo;

	if( index < 0 || index > cl.maxclients )
		return NULL;

	return &cl.players[index];
}

/*
===============
pfnGetPlayerState

===============
*/
static entity_state_t *R_StudioGetPlayerState( int index )
{
	if( index < 0 || index >= cl.maxclients )
		return NULL;

	return &cl.frames[cl.parsecountmod].playerstate[index];
}

static int pfnGetStudioModelInterface( int version, struct r_studio_interface_s **ppinterface, struct engine_studio_api_s *pstudio )
{
	return clgame.dllFuncs.pfnGetStudioModelInterface ?
		clgame.dllFuncs.pfnGetStudioModelInterface( version, ppinterface, pstudio ) :
		0;
}

static byte *pfnImage_GetPool( void )
{
	return host.imagepool;
}

static const bpc_desc_t *pfnImage_GetPFDesc( int idx )
{
	return &PFDesc[idx];
}

static void pfnDrawNormalTriangles( void )
{
	clgame.dllFuncs.pfnDrawNormalTriangles();
}

static void pfnDrawTransparentTriangles( void )
{
	clgame.dllFuncs.pfnDrawTransparentTriangles();
}

static screenfade_t *pfnRefGetScreenFade( void )
{
	return &clgame.fade;
}

static ref_api_t gEngfuncs =
{
	pfnEngineGetParm,

	(void*)Cvar_Get,
	(void*)Cvar_FindVarExt,
	Cvar_VariableValue,
	Cvar_VariableString,
	Cvar_SetValue,
	Cvar_Set,
	(void*)Cvar_RegisterVariable,
	Cvar_FullSet,

	Cmd_AddRefCommand,
	Cmd_RemoveCommand,
	Cmd_Argc,
	Cmd_Argv,
	Cmd_Args,

	Cbuf_AddText,
	Cbuf_InsertText,
	Cbuf_Execute,
	pfnCbuf_SetOpenGLConfigHack,

	Con_Printf,
	Con_DPrintf,
	Con_Reportf,

	Con_NPrintf,
	Con_NXPrintf,
	CL_CenterPrint,
	Con_DrawStringLen,
	Con_DrawString,
	CL_DrawCenterPrint,

	CL_GetLocalPlayer,
	CL_GetViewModel,
	CL_GetEntityByIndex,
	R_BeamGetEntity,
	CL_GetWaterEntity,
	CL_AddVisibleEntity,

	Mod_SampleSizeForFace,
	Mod_BoxVisible,
	pfnGetWorld,
	Mod_PointInLeaf,
	Mod_CreatePolygonsForHull,

	R_StudioSlerpBones,
	R_StudioCalcBoneQuaternion,
	R_StudioCalcBonePosition,
	R_StudioGetAnim,
	pfnStudioEvent,

	CL_DrawEFX,
	CL_ThinkParticle,
	R_FreeDeadParticles,
	CL_AllocParticleFast,
	CL_AllocElight,
	pfnGetDefaultSprite,
	R_StoreEfrags,

	Mod_ForName,
	pfnMod_Extradata,
	CL_ModelHandle,
	pfnMod_GetCurrentLoadingModel,
	pfnMod_SetCurrentLoadingModel,

	CL_GetRemapInfoForEntity,
	CL_AllocRemapInfo,
	CL_FreeRemapInfo,
	CL_UpdateRemapInfo,

	CL_ExtraUpdate,
	COM_HashKey,
	Host_Error,
	COM_SetRandomSeed,
	COM_RandomFloat,
	COM_RandomLong,
	pfnRefGetScreenFade,
	CL_TextMessageGet,
	pfnGetPredictedOrigin,
	pfnCL_GetPaletteColor,
	pfnCL_GetScreenInfo,
	pfnSetLocalLightLevel,
	Sys_CheckParm,

	pfnPlayerInfo,
	R_StudioGetPlayerState,
	Mod_CacheCheck,
	Mod_LoadCacheFile,
	Mod_Calloc,
	pfnGetStudioModelInterface,

	_Mem_AllocPool,
	_Mem_FreePool,
	_Mem_Alloc,
	_Mem_Realloc,
	_Mem_Free,

	COM_LoadLibrary,
	COM_FreeLibrary,
	COM_GetProcAddress,

	FS_LoadFile,
	COM_ParseFile,
	FS_FileExists,
	FS_AllowDirectPaths,

	R_Init_Video,
	R_Free_Video,

	GL_SetAttribute,
	GL_GetAttribute,
	GL_GetProcAddress,
	GL_SwapBuffers,

	SW_CreateBuffer,
	SW_LockBuffer,
	SW_UnlockBuffer,

	BuildGammaTable,
	LightToTexGamma,

	CL_GetLightStyle,
	CL_GetDynamicLight,
	CL_GetEntityLight,
	R_FatPVS,
	GL_GetOverviewParms,
	Sys_DoubleTime,

	pfnGetPhysent,
	pfnTraceSurface,
	PM_TraceLine,
	CL_VisTraceLine,
	CL_TraceLine,
	pfnGetMoveVars,

	Image_AddCmdFlags,
	Image_SetForceFlags,
	Image_ClearForceFlags,
	Image_CustomPalette,
	Image_Process,
	FS_LoadImage,
	FS_SaveImage,
	FS_CopyImage,
	FS_FreeImage,
	Image_SetMDLPointer,
	pfnImage_GetPool,
	pfnImage_GetPFDesc,

	pfnDrawNormalTriangles,
	pfnDrawTransparentTriangles,
	&clgame.drawFuncs
};

static void R_UnloadProgs( void )
{
	if( !ref.hInstance ) return;

	// deinitialize renderer
	ref.dllFuncs.R_Shutdown();

	Cvar_FullSet( "host_refloaded", "0", FCVAR_READ_ONLY );

	COM_FreeLibrary( ref.hInstance );
	ref.hInstance = NULL;

	memset( &refState, 0, sizeof( refState ));
	memset( &ref.dllFuncs, 0, sizeof( ref.dllFuncs ));

	Cvar_Unlink( FCVAR_RENDERINFO | FCVAR_GLCONFIG );
	Cmd_Unlink( CMD_REFDLL );
}

static void CL_FillTriAPIFromRef( triangleapi_t *dst, const ref_interface_t *src )
{
	dst->version           = TRI_API_VERSION;
	dst->Begin             = src->Begin;
	dst->RenderMode        = TriRenderMode;
	dst->End               = src->End;
	dst->Color4f           = TriColor4f;
	dst->Color4ub          = TriColor4ub;
	dst->TexCoord2f        = src->TexCoord2f;
	dst->Vertex3f          = src->Vertex3f;
	dst->Vertex3fv         = src->Vertex3fv;
	dst->Brightness        = TriBrightness;
	dst->CullFace          = TriCullFace;
	dst->SpriteTexture     = TriSpriteTexture;
	dst->WorldToScreen     = TriWorldToScreen;
	dst->Fog               = src->Fog;
	dst->ScreenToWorld     = src->ScreenToWorld;
	dst->GetMatrix         = src->GetMatrix;
	dst->BoxInPVS          = TriBoxInPVS;
	dst->LightAtPoint      = TriLightAtPoint;
	dst->Color4fRendermode = TriColor4fRendermode;
	dst->FogParams         = src->FogParams;
}

static qboolean R_LoadProgs( const char *name )
{
	extern triangleapi_t gTriApi;
	static ref_api_t gpEngfuncs;
	REFAPI GetRefAPI; // single export

	if( ref.hInstance ) R_UnloadProgs();

#ifdef XASH_INTERNAL_GAMELIBS
	FS_AllowDirectPaths( true );
	if( !(ref.hInstance = COM_LoadLibrary( name, false, true ) ))
	{
		return false;
	}
#else
	if( !(ref.hInstance = COM_LoadLibrary( name, false, true ) ))
	{
		FS_AllowDirectPaths( true );
		if( !(ref.hInstance = COM_LoadLibrary( name, false, true ) ))
		{
			FS_AllowDirectPaths( false );
			return false;
		}

	}
#endif

	FS_AllowDirectPaths( false );

	if( ( GetRefAPI = (REFAPI)COM_GetProcAddress( ref.hInstance, "GetRefAPI" )) == NULL )
	{
		COM_FreeLibrary( ref.hInstance );
		Con_Reportf( "R_LoadProgs: can't init renderer API\n" );
		ref.hInstance = NULL;
		return false;
	}

	// make local copy of engfuncs to prevent overwrite it with user dll
	memcpy( &gpEngfuncs, &gEngfuncs, sizeof( gpEngfuncs ));

	if( !GetRefAPI( REF_API_VERSION, &ref.dllFuncs, &gpEngfuncs, &refState ))
	{
		COM_FreeLibrary( ref.hInstance );
		Con_Reportf( "R_LoadProgs: can't init renderer API: wrong version\n" );
		ref.hInstance = NULL;
		return false;
	}

	refState.developer = host_developer.value;

	if( !ref.dllFuncs.R_Init( ) )
	{
		COM_FreeLibrary( ref.hInstance );
		Con_Reportf( "R_LoadProgs: can't init renderer!\n" ); //, ref.dllFuncs.R_GetInitError() );
		ref.hInstance = NULL;
		return false;
	}

	Cvar_FullSet( "host_refloaded", "1", FCVAR_READ_ONLY );
	ref.initialized = true;

	// initialize TriAPI callbacks
	CL_FillTriAPIFromRef( &gTriApi, &ref.dllFuncs );

	return true;
}

void R_Shutdown( void )
{
	int i;
	model_t *mod;

	// release SpriteTextures
	for( i = 1, mod = clgame.sprites; i < MAX_CLIENT_SPRITES; i++, mod++ )
	{
		if( !mod->name[0] ) continue;
		Mod_FreeModel( mod );
	}
	memset( clgame.sprites, 0, sizeof( clgame.sprites ));

	// correctly free all models before render unload
	// change this if need add online render changing
	Mod_FreeAll();
	R_UnloadProgs();
	ref.initialized = false;
}

void R_GetRendererName( char *dest, size_t size, const char *refdll )
{
#ifdef XASH_INTERNAL_GAMELIBS
    Q_snprintf( dest, size, "%sref_%s%c%s",
#ifdef OS_LIB_PREFIX
        OS_LIB_PREFIX,
#else
        "",
#endif
#ifndef NO_LIB_EXT
        refdll, '.', OS_LIB_EXT
#else
		refdll, '\0', ""
#endif
	);
#else
 	Q_snprintf(dest, size, "ref_%s", refdll);
#endif
}

qboolean R_Init( void )
{
	string refopt, refdll;

	if( !Sys_GetParmFromCmdLine( "-ref", refopt ) )
	{
		// compile-time defaults
		R_GetRendererName( refdll, sizeof( refdll ), DEFAULT_RENDERER );
		Con_Printf( "Loading default renderer: %s\n", refdll );
	}
	else if( !Q_strstr( refopt, va( ".%s", OS_LIB_EXT ) ) )
	{
		// shortened renderer name
		R_GetRendererName( refdll, sizeof( refdll ), refopt );
		Con_Printf( "Loading renderer by short name: %s\n", refdll );
	}
	else
	{
		// full path
		Q_strcpy( refdll, refopt );
		Con_Printf( "Loading renderer: %s\n", refdll );
	}

	gl_vsync = Cvar_Get( "gl_vsync", "0", FCVAR_ARCHIVE,  "enable vertical syncronization" );
	gl_showtextures = Cvar_Get( "gl_showtextures", "0", FCVAR_CHEAT, "show all uploaded textures" );
	r_adjust_fov = Cvar_Get( "r_adjust_fov", "1", FCVAR_ARCHIVE, "making FOV adjustment for wide-screens" );
	r_decals = Cvar_Get( "r_decals", "4096", FCVAR_ARCHIVE, "sets the maximum number of decals" );
	gl_wgl_msaa_samples = Cvar_Get( "gl_wgl_msaa_samples", "0", FCVAR_GLCONFIG, "samples number for multisample anti-aliasing" );

	if( !R_LoadProgs( refdll ))
	{
		R_Shutdown();
		Host_Error( "Can't initialize %s renderer!\n", refdll );
		return false;
	}

	Con_Reportf( "Renderer %s initialized\n", refdll );

	SCR_Init();

	return true;
}
