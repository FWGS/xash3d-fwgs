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
convar_t *gl_wgl_msaa_samples;
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
void GL_FreeImage( const char *name )
{
	int	texnum;

	if( !ref.initialized )
		return;

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
	Cbuf_AddText( va( "exec %s.cfg", ref.dllFuncs.R_GetConfigName()));
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
	if( !Q_strstr( opt, va( ".%s", OS_LIB_EXT )))
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
		Q_strcpy( dest, opt );
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
	if ( Sys_CheckParm("-fullscreen") )
	{
		Cvar_Set( "fullscreen", "1" );
	}
	else if ( Sys_CheckParm( "-windowed" ) )
	{
		Cvar_Set( "fullscreen", "0" );
	}
#endif
}

void R_CollectRendererNames( void )
{
	const char *renderers[] = DEFAULT_RENDERERS;
	int i;

	ref.numRenderers = 0;

	for( i = 0; i < DEFAULT_RENDERERS_LEN; i++ )
	{
		string temp;
		void *dll, *pfn;

		R_GetRendererName( temp, sizeof( temp ), renderers[i] );

		dll = COM_LoadLibrary( temp, false, true );
		if( !dll )
		{
			Con_Reportf( "R_CollectRendererNames: can't load library %s: %s\n", temp, COM_GetLibraryError() );
			continue;
		}

		pfn = COM_GetProcAddress( dll, GET_REF_API );
		if( !pfn )
		{
			Con_Reportf( "R_CollectRendererNames: can't find API entry point in %s\n", temp );
			COM_FreeLibrary( dll );
			continue;
		}

		Q_strncpy( ref.shortNames[i], renderers[i], sizeof( ref.shortNames[i] ));

		pfn = COM_GetProcAddress( dll, GET_REF_HUMANREADABLE_NAME );
		if( !pfn ) // just in case
		{
			Con_Reportf( "R_CollectRendererNames: can't find GetHumanReadableName export in %s\n", temp );
			Q_strncpy( ref.readableNames[i], renderers[i], sizeof( ref.readableNames[i] ));
		}
		else
		{
			REF_HUMANREADABLE_NAME GetHumanReadableName = (REF_HUMANREADABLE_NAME)pfn;

			GetHumanReadableName( ref.readableNames[i], sizeof( ref.readableNames[i] ));
		}

		Con_Printf( "Found renderer %s: %s\n", ref.shortNames[i], ref.readableNames[i] );

		ref.numRenderers++;
		COM_FreeLibrary( dll );
	}
}

qboolean R_Init( void )
{
	qboolean success = false;
	string refopt;

	gl_vsync = Cvar_Get( "gl_vsync", "0", FCVAR_ARCHIVE,  "enable vertical syncronization" );
	gl_showtextures = Cvar_Get( "r_showtextures", "0", FCVAR_CHEAT, "show all uploaded textures" );
	r_adjust_fov = Cvar_Get( "r_adjust_fov", "1", FCVAR_ARCHIVE, "making FOV adjustment for wide-screens" );
	r_decals = Cvar_Get( "r_decals", "4096", FCVAR_ARCHIVE, "sets the maximum number of decals" );
	gl_wgl_msaa_samples = Cvar_Get( "gl_wgl_msaa_samples", "0", FCVAR_GLCONFIG, "samples number for multisample anti-aliasing" );
	gl_clear = Cvar_Get( "gl_clear", "0", FCVAR_ARCHIVE, "clearing screen after each frame" );
	r_showtree = Cvar_Get( "r_showtree", "0", FCVAR_ARCHIVE, "build the graph of visible BSP tree" );
	r_refdll = Cvar_Get( "r_refdll", "", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "choose renderer implementation, if supported" );

	// cvars are created, execute video config
	Cbuf_AddText( "exec video.cfg" );
	Cbuf_Execute();

	// Set screen resolution and fullscreen mode if passed in on command line.
	// this is done after executing video.cfg, as the command line values should take priority.
	SetWidthAndHeightFromCommandLine();
	SetFullscreenModeFromCommandLine();

	R_CollectRendererNames();

	// command line have priority
	if( !Sys_GetParmFromCmdLine( "-ref", refopt ) )
	{
		// r_refdll is set to empty by default, so we can change hardcoded defaults just in case
		Q_strncpy( refopt, COM_CheckString( r_refdll->string ) ?
			r_refdll->string : DEFAULT_ACCELERATED_RENDERER, sizeof( refopt ) );
	}

	if( !(success = R_LoadRenderer( refopt )))
	{
		// check if we are tried to load default accelearated renderer already
		// and if not, load it first
		if( Q_strcmp( refopt, DEFAULT_ACCELERATED_RENDERER ) )
		{
			success = R_LoadRenderer( refopt );
		}

		// software renderer is the last chance...
		if( !success )
		{
			success = R_LoadRenderer( DEFAULT_SOFTWARE_RENDERER );
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
