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
convar_t *r_showtree;
convar_t *gl_msaa_samples;
convar_t *gl_clear;
convar_t *r_refdll;

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
void GAME_EXPORT GL_FreeImage( const char *name )
{
	int	texnum;

	if( !ref.initialized )
		return;

	if(( texnum = ref.dllFuncs.GL_FindTexture( name )) != 0 )
		 ref.dllFuncs.GL_FreeTexture( texnum );
}

void R_UpdateRefState( void )
{
	refState.time      = cl.time;
	refState.oldtime   = cl.oldtime;
	refState.realtime  = host.realtime;
	refState.frametime = host.frametime;
}

void GL_RenderFrame( const ref_viewpass_t *rvp )
{
	R_UpdateRefState();

	VectorCopy( rvp->vieworigin, refState.vieworg );
	VectorCopy( rvp->viewangles, refState.viewangles );

	ref.dllFuncs.GL_RenderFrame( rvp );
}

static intptr_t pfnEngineGetParm( int parm, int arg )
{
	return CL_RenderGetParm( parm, arg, false ); // prevent recursion
}

static world_static_t *pfnGetWorld( void )
{
	return &world;
}

static void pfnStudioEvent( const mstudioevent_t *event, const cl_entity_t *e )
{
	clgame.dllFuncs.pfnStudioEvent( event, e );
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

	if( index < 0 || index >= cl.maxclients )
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

static poolhandle_t pfnImage_GetPool( void )
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

/*
===============
R_DoResetGamma
gamma will be reset for
some type of screenshots
===============
*/
static qboolean R_DoResetGamma( void )
{
	switch( cls.scrshot_action )
	{
	case scrshot_envshot:
	case scrshot_skyshot:
		return true;
	default:
		return false;
	}
}

static qboolean R_Init_Video_( const int type )
{
	host.apply_opengl_config = true;
	Cbuf_AddTextf( "exec %s.cfg", ref.dllFuncs.R_GetConfigName());
	Cbuf_Execute();
	host.apply_opengl_config = false;

	return R_Init_Video( type );
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

	R_Init_Video_,
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
	R_DoResetGamma,

	CL_GetLightStyle,
	CL_GetDynamicLight,
	CL_GetEntityLight,
	R_FatPVS,
	GL_GetOverviewParms,
	Sys_DoubleTime,

	pfnGetPhysent,
	pfnTraceSurface,
	PM_CL_TraceLine,
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
	&clgame.drawFuncs,

	&g_fsapi,
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

	FS_AllowDirectPaths( true );
	if( !(ref.hInstance = COM_LoadLibrary( name, false, true ) ))
	{
		FS_AllowDirectPaths( false );
		Con_Reportf( "R_LoadProgs: can't load renderer library %s: %s\n", name, COM_GetLibraryError() );
		return false;
	}

	FS_AllowDirectPaths( false );

	if( !( GetRefAPI = (REFAPI)COM_GetProcAddress( ref.hInstance, GET_REF_API )) )
	{
		COM_FreeLibrary( ref.hInstance );
		Con_Reportf( "R_LoadProgs: can't find GetRefAPI entry point in %s\n", name );
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

static void R_GetRendererName( char *dest, size_t size, const char *opt )
{
	if( !Q_strstr( opt, "." OS_LIB_EXT ))
	{
		const char *format;

#ifdef XASH_INTERNAL_GAMELIBS
		if( !Q_strcmp( opt, "ref_" ))
			format = "%s";
		else
			format = "ref_%s";
#else
		if( !Q_strcmp( opt, "ref_" ))
			format = OS_LIB_PREFIX "%s." OS_LIB_EXT;
		else
			format = OS_LIB_PREFIX "ref_%s." OS_LIB_EXT;
#endif
		Q_snprintf( dest, size, format, opt );

	}
	else
	{
		// full path
		Q_strncpy( dest, opt, size );
	}
}

static qboolean R_LoadRenderer( const char *refopt )
{
	string refdll;

	R_GetRendererName( refdll, sizeof( refdll ), refopt );

	Con_Printf( "Loading renderer: %s -> %s\n", refopt, refdll );

	if( !R_LoadProgs( refdll ))
	{
		R_Shutdown();
		Sys_Warn( S_ERROR "Can't initialize %s renderer!\n", refdll );
		return false;
	}

	Con_Reportf( "Renderer %s initialized\n", refdll );

	return true;
}

static void SetWidthAndHeightFromCommandLine( void )
{
	int width, height;

	Sys_GetIntFromCmdLine( "-width", &width );
	Sys_GetIntFromCmdLine( "-height", &height );

	if( width < 1 || height < 1 )
	{
		// Not specified or invalid, so don't bother.
		return;
	}

	R_SaveVideoMode( width, height, width, height );
}

static void SetFullscreenModeFromCommandLine( void )
{
#if !XASH_MOBILE_PLATFORM
	if( Sys_CheckParm( "-fullscreen" ))
	{
		Cvar_DirectSet( vid_fullscreen, "1" );
	}
	else if( Sys_CheckParm( "-windowed" ))
	{
		Cvar_DirectSet( vid_fullscreen, "0" );
	}
#endif
}

static void R_CollectRendererNames( void )
{
	// ordering is important!
	static const char *shortNames[] =
	{
#if XASH_REF_GL_ENABLED
		"gl",
#endif
#if XASH_REF_NANOGL_ENABLED
		"gles1",
#endif
#if XASH_REF_GLWES_ENABLED
		"gles2",
#endif
#if XASH_REF_GL4ES_ENABLED
		"gl4es",
#endif
#if XASH_REF_SOFT_ENABLED
		"soft",
#endif
	};

	// ordering is important here too!
	static const char *readableNames[ARRAYSIZE( shortNames )] =
	{
#if XASH_REF_GL_ENABLED
		"OpenGL",
#endif
#if XASH_REF_NANOGL_ENABLED
		"GLES1 (NanoGL)",
#endif
#if XASH_REF_GLWES_ENABLED
		"GLES2 (gl-wes-v2)",
#endif
#if XASH_REF_GL4ES_ENABLED
		"GL4ES",
#endif
#if XASH_REF_SOFT_ENABLED
		"Software",
#endif
	};

	ref.numRenderers = ARRAYSIZE( shortNames );
	ref.shortNames = shortNames;
	ref.readableNames = readableNames;
}

qboolean R_Init( void )
{
	qboolean success = false;
	string requested;

	gl_vsync = Cvar_Get( "gl_vsync", "0", FCVAR_ARCHIVE,  "enable vertical syncronization" );
	gl_showtextures = Cvar_Get( "r_showtextures", "0", FCVAR_CHEAT, "show all uploaded textures" );
	r_adjust_fov = Cvar_Get( "r_adjust_fov", "1", FCVAR_ARCHIVE, "making FOV adjustment for wide-screens" );
	r_decals = Cvar_Get( "r_decals", "4096", FCVAR_ARCHIVE, "sets the maximum number of decals" );
	gl_msaa_samples = Cvar_Get( "gl_msaa_samples", "0", FCVAR_GLCONFIG, "samples number for multisample anti-aliasing" );
	gl_clear = Cvar_Get( "gl_clear", "0", FCVAR_ARCHIVE, "clearing screen after each frame" );
	r_showtree = Cvar_Get( "r_showtree", "0", FCVAR_ARCHIVE, "build the graph of visible BSP tree" );
	r_refdll = Cvar_Get( "r_refdll", "", FCVAR_RENDERINFO, "choose renderer implementation, if supported" );

	// cvars that are expected to exist
	Cvar_Get( "r_speeds", "0", FCVAR_ARCHIVE, "shows renderer speeds" );
	Cvar_Get( "r_fullbright", "0", FCVAR_CHEAT, "disable lightmaps, get fullbright for entities" );
	Cvar_Get( "r_norefresh", "0", 0, "disable 3D rendering (use with caution)" );
	Cvar_Get( "r_dynamic", "1", FCVAR_ARCHIVE, "allow dynamic lighting (dlights, lightstyles)" );
	Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	Cvar_Get( "tracerred", "0.8", 0, "tracer red component weight ( 0 - 1.0 )" );
	Cvar_Get( "tracergreen", "0.8", 0, "tracer green component weight ( 0 - 1.0 )" );
	Cvar_Get( "tracerblue", "0.4", 0, "tracer blue component weight ( 0 - 1.0 )" );
	Cvar_Get( "traceralpha", "0.5", 0, "tracer alpha amount ( 0 - 1.0 )" );

	Cvar_Get( "r_sprite_lerping", "1", FCVAR_ARCHIVE, "enables sprite animation lerping" );
	Cvar_Get( "r_sprite_lighting", "1", FCVAR_ARCHIVE, "enables sprite lighting (blood etc)" );

	Cvar_Get( "r_drawviewmodel", "1", 0, "draw firstperson weapon model" );
	Cvar_Get( "r_glowshellfreq", "2.2", 0, "glowing shell frequency update" );

	// cvars that are expected to exist by client.dll
	// refdll should just get pointer to them
	Cvar_Get( "r_drawentities", "1", FCVAR_CHEAT, "render entities" );
	Cvar_Get( "cl_himodels", "1", FCVAR_ARCHIVE, "draw high-resolution player models in multiplayer" );

	// cvars are created, execute video config
	Cbuf_AddText( "exec video.cfg" );
	Cbuf_Execute();

	// Set screen resolution and fullscreen mode if passed in on command line.
	// this is done after executing video.cfg, as the command line values should take priority.
	SetWidthAndHeightFromCommandLine();
	SetFullscreenModeFromCommandLine();

	R_CollectRendererNames();

	// Priority:
	// 1. Command line `-ref` argument.
	// 2. `ref_dll` cvar.
	// 3. Detected renderers in `DEFAULT_RENDERERS` order.
	requested[0] = 0;

	if( !success && Sys_GetParmFromCmdLine( "-ref", requested ))
		success = R_LoadRenderer( requested );

	if( !success && COM_CheckString( r_refdll->string ))
	{
		Q_strncpy( requested, r_refdll->string, sizeof( requested ));
		success = R_LoadRenderer( requested );
	}

	if( !success )
	{
		int i;

		for( i = 0; i < ref.numRenderers && !success; i++ )
		{
			// skip renderer that was requested but failed to load
			if( !Q_strcmp( requested, ref.shortNames[i] ))
				continue;

			success = R_LoadRenderer( ref.shortNames[i] );
		}
	}

	if( !success )
	{
		Host_Error( "Can't initialize any renderer. Check your video drivers!" );
		return false;
	}

	SCR_Init();

	return true;
}
