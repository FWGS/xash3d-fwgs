#include "common.h"
#include "client.h"
#include "library.h"
#include "cl_tent.h"
#include "platform/platform.h"
#include "vid_common.h"

struct ref_state_s ref;
ref_globals_t refState;

static const char* r_skyBoxSuffix[SKYBOX_MAX_SIDES] = { "rt", "bk", "lf", "ft", "up", "dn" };

CVAR_DEFINE_AUTO( gl_vsync, "1", FCVAR_ARCHIVE,  "enable vertical syncronization" );
CVAR_DEFINE_AUTO( r_showtextures, "0", FCVAR_CHEAT, "show all uploaded textures" );
CVAR_DEFINE_AUTO( r_adjust_fov, "1", FCVAR_ARCHIVE, "making FOV adjustment for wide-screens" );
CVAR_DEFINE_AUTO( r_decals, "4096", FCVAR_ARCHIVE, "sets the maximum number of decals" );
CVAR_DEFINE_AUTO( gl_msaa_samples, "0", FCVAR_GLCONFIG, "samples number for multisample anti-aliasing" );
CVAR_DEFINE_AUTO( gl_clear, "0", FCVAR_ARCHIVE, "clearing screen after each frame" );
CVAR_DEFINE_AUTO( r_showtree, "0", FCVAR_ARCHIVE, "build the graph of visible BSP tree" );
static CVAR_DEFINE_AUTO( r_refdll, "", FCVAR_RENDERINFO, "choose renderer implementation, if supported" );
static CVAR_DEFINE_AUTO( r_refdll_loaded, "", FCVAR_READ_ONLY, "currently loaded renderer" );

// there is no need to expose whole host and cl structs into the renderer
// but we still need to update timings accurately as possible
// this looks horrible but the only other option would be passing four
// time pointers and then it's looks even worse with dereferences everywhere
#define STATIC_OFFSET_CHECK( s1, s2, field, base, msg ) \
	STATIC_ASSERT( offsetof( s1, field ) == offsetof( s2, field ) - offsetof( s2, base ), msg )
#define REF_CLIENT_CHECK( field ) \
	STATIC_OFFSET_CHECK( ref_client_t, client_t, field, time, "broken ref_client_t offset" ); \
	STATIC_ASSERT_( szchk_##__LINE__, sizeof(((ref_client_t *)0)->field ) == sizeof( cl.field ), "broken ref_client_t size" )
#define REF_HOST_CHECK( field ) \
	STATIC_OFFSET_CHECK( ref_host_t, host_parm_t, field, realtime, "broken ref_client_t offset" ); \
	STATIC_ASSERT_( szchk_##__LINE__, sizeof(((ref_host_t *)0)->field ) == sizeof( host.field ), "broken ref_client_t size" )

REF_CLIENT_CHECK( time );
REF_CLIENT_CHECK( oldtime );
REF_CLIENT_CHECK( viewentity );
REF_CLIENT_CHECK( playernum );
REF_CLIENT_CHECK( maxclients );
REF_CLIENT_CHECK( models );
REF_CLIENT_CHECK( paused );
REF_CLIENT_CHECK( simorg );
REF_HOST_CHECK( realtime );
REF_HOST_CHECK( frametime );
REF_HOST_CHECK( features );

static qboolean CheckSkybox( const char *name, char out[SKYBOX_MAX_SIDES][MAX_STRING] )
{
	static const char *skybox_ext[3] = { "dds", "tga", "bmp" };
	static const char *skybox_delim[2] = { "", "_" }; // no space for HL style, underscore for Q1 style
	int	i;

	// search for skybox images
	for( i = 0; i <	ARRAYSIZE( skybox_ext ); i++ )
	{
		int j;

		for( j = 0; j < ARRAYSIZE( skybox_delim ); j++ )
		{
			int k, num_checked_sides = 0;

			for( k = 0; k < SKYBOX_MAX_SIDES; k++ )
			{
				char sidename[MAX_VA_STRING];

				Q_snprintf( sidename, sizeof( sidename ), "%s%s%s.%s", name, skybox_delim[j], r_skyBoxSuffix[k], skybox_ext[i] );
				if( g_fsapi.FileExists( sidename, false ))
				{
					Q_strncpy( out[k], sidename, sizeof( out[k] ));
					num_checked_sides++;
				}
			}

			if( num_checked_sides == SKYBOX_MAX_SIDES )
				return true; // image exists
		}
	}

	return false;
}

void R_SetupSky( const char *name )
{
	string loadname;
	char sidenames[SKYBOX_MAX_SIDES][MAX_STRING];
	int skyboxTextures[SKYBOX_MAX_SIDES] = { 0 };
	int i, len;
	qboolean result;

	if( !COM_CheckString( name ))
	{
		ref.dllFuncs.R_SetupSky( NULL ); // unload skybox
		return;
	}

	Q_snprintf( loadname, sizeof( loadname ), "gfx/env/%s", name );
	COM_StripExtension( loadname );

	// kill the underline suffix to find them manually later
	len = Q_strlen( loadname );

	if( loadname[len - 1] == '_' )
		loadname[len - 1] = '\0';
	result = CheckSkybox( loadname, sidenames );

	// to prevent infinite recursion if default skybox was missed
	if( !result && Q_stricmp( name, DEFAULT_SKYBOX_NAME ))
	{
		Con_Reportf( S_WARN "missed or incomplete skybox '%s'\n", name );
		R_SetupSky( DEFAULT_SKYBOX_NAME ); // force to default
		return;
	}

	ref.dllFuncs.R_SetupSky( NULL ); // unload skybox
	Con_DPrintf( "SKY:  " );

	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		skyboxTextures[i] = ref.dllFuncs.GL_LoadTexture( sidenames[i], NULL, 0, TF_CLAMP|TF_SKY );

		if( !skyboxTextures[i] )
			break;

		Con_DPrintf( "%s%s%s", name, r_skyBoxSuffix[i], i != 5 ? ", " : ". " );
	}

	if( i == SKYBOX_MAX_SIDES )
	{
		SetBits( world.flags, FWORLD_CUSTOM_SKYBOX );
		Con_DPrintf( "done\n" );
		ref.dllFuncs.R_SetupSky( skyboxTextures );
		return; // loaded
	}

	Con_DPrintf( "^2failed\n" );
	for( i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		if( skyboxTextures[i] )
			ref.dllFuncs.GL_FreeTexture( skyboxTextures[i] );
	}
}

void GAME_EXPORT GL_FreeImage( const char *name )
{
	int	texnum;

	if( !ref.initialized )
		return;

	if(( texnum = ref.dllFuncs.GL_FindTexture( name )) != 0 )
		 ref.dllFuncs.GL_FreeTexture( texnum );
}

void GL_RenderFrame( const ref_viewpass_t *rvp )
{
	VectorCopy( rvp->vieworigin, refState.vieworg );
	VectorCopy( rvp->viewangles, refState.viewangles );

	ref.dllFuncs.GL_RenderFrame( rvp );
}

static intptr_t pfnEngineGetParm( int parm, int arg )
{
	return CL_RenderGetParm( parm, arg, false ); // prevent recursion
}

static cvar_t *pfnCvar_Get( const char *szName, const char *szValue, int flags, const char *description )
{
	return (cvar_t *)Cvar_Get( szName, szValue, flags | FCVAR_REFDLL, description );
}

static void pfnCvar_RegisterVariable( convar_t *var )
{
	SetBits( var->flags, FCVAR_REFDLL );
	Cvar_RegisterVariable( var );
}

static void pfnCvar_FullSet( const char *var_name, const char *value, int flags )
{
	Cvar_FullSet( var_name, value, flags | FCVAR_REFDLL );
}

static int Cmd_AddRefCommand( const char *cmd_name, xcommand_t function, const char *description )
{
	return Cmd_AddCommandEx( cmd_name, function, description, CMD_REFDLL, __func__ );
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
	default: Host_Error( "%s: unknown sprite %d\n", __func__, spr );
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
	default: Host_Error( "%s: unknown type %d\n", __func__, type );
	}
	return NULL;
}

static void CL_ExtraUpdate( void )
{
	clgame.dllFuncs.IN_Accumulate();
	S_ExtraUpdate();
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

static qboolean R_Init_Video_( const int type )
{
	host.apply_opengl_config = true;
	Cbuf_AddTextf( "exec %s.cfg", ref.dllFuncs.R_GetConfigName());
	Cbuf_Execute();
	host.apply_opengl_config = false;

	return R_Init_Video( type );
}

static mleaf_t *pfnMod_PointInLeaf( const vec3_t p, mnode_t *node )
{
	// FIXME: get rid of this on next RefAPI update
	return Mod_PointInLeaf( p, node, cl.models[1] );
}

static const ref_api_t gEngfuncs =
{
	pfnEngineGetParm,

	pfnCvar_Get,
	(void*)Cvar_FindVarExt,
	Cvar_VariableValue,
	Cvar_VariableString,
	Cvar_SetValue,
	Cvar_Set,
	pfnCvar_RegisterVariable,
	pfnCvar_FullSet,

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

	R_BeamGetEntity,
	CL_GetWaterEntity,
	CL_AddVisibleEntity,

	Mod_SampleSizeForFace,
	Mod_BoxVisible,
	pfnMod_PointInLeaf,
	R_DrawWorldHull,
	R_DrawModelHull,

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

	CL_EntitySetRemapColors,
	CL_GetRemapInfoForEntity,

	CL_ExtraUpdate,
	Host_Error,
	COM_SetRandomSeed,
	COM_RandomFloat,
	COM_RandomLong,
	pfnRefGetScreenFade,
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

	R_FatPVS,
	GL_GetOverviewParms,
	Sys_DoubleTime,

	pfnGetPhysent,
	pfnTraceSurface,
	PM_CL_TraceLine,
	CL_VisTraceLine,
	CL_TraceLine,

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
	pfnImage_GetPFDesc,

	pfnDrawNormalTriangles,
	pfnDrawTransparentTriangles,
	&clgame.drawFuncs,

	&g_fsapi,

	R_GetWindowHandle,
};

static void R_UnloadProgs( void )
{
	if( !ref.hInstance ) return;

	// deinitialize renderer
	ref.dllFuncs.R_Shutdown();

	Cvar_FullSet( "host_refloaded", "0", FCVAR_READ_ONLY );

	Cvar_Unlink( FCVAR_RENDERINFO | FCVAR_GLCONFIG | FCVAR_REFDLL );
	Cmd_Unlink( CMD_REFDLL );

	COM_FreeLibrary( ref.hInstance );
	ref.hInstance = NULL;

	memset( &refState, 0, sizeof( refState ));
	memset( &ref.dllFuncs, 0, sizeof( ref.dllFuncs ));
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
	static ref_api_t gpEngfuncs;
	REFAPI GetRefAPI; // single export

	if( ref.hInstance ) R_UnloadProgs();

	FS_AllowDirectPaths( true );
	if( !( ref.hInstance = COM_LoadLibrary( name, false, true )))
	{
		FS_AllowDirectPaths( false );
		Con_Reportf( "%s: can't load renderer library %s: %s\n", __func__, name, COM_GetLibraryError() );
		return false;
	}

	FS_AllowDirectPaths( false );

	if( !( GetRefAPI = (REFAPI)COM_GetProcAddress( ref.hInstance, GET_REF_API )))
	{
		Con_Reportf( "%s: can't find GetRefAPI entry point in %s\n", __func__, name );
		return false;
	}

	// make local copy of engfuncs to prevent overwrite it with user dll
	gpEngfuncs = gEngfuncs;

	if( GetRefAPI( REF_API_VERSION, &ref.dllFuncs, &gpEngfuncs, &refState ) != REF_API_VERSION )
	{
		Con_Reportf( "%s: can't init renderer API: wrong version\n", __func__ );
		return false;
	}

	refState.developer = host_developer.value;

	if( !ref.dllFuncs.R_Init( ))
	{
		Con_Reportf( "%s: can't init renderer!\n", __func__ ); //, ref.dllFuncs.R_GetInitError() );
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
#ifdef XASH_INTERNAL_GAMELIBS
	#define FMT1 "%s"
	#define FMT2 "ref_%s"
#else
	#define FMT1 OS_LIB_PREFIX "%s." OS_LIB_EXT
	#define FMT2 OS_LIB_PREFIX "ref_%s." OS_LIB_EXT
#endif
		if( !Q_strncmp( opt, "ref_", 4 ))
			Q_snprintf( dest, size, FMT1, opt );
		else
			Q_snprintf( dest, size, FMT2, opt );
#undef FMT1
#undef FMT2
	}
	else
	{
		// full path
		Q_strncpy( dest, opt, size );
	}
}

static qboolean R_LoadRenderer( const char *refopt, qboolean quiet )
{
	string refdll;

	R_GetRendererName( refdll, sizeof( refdll ), refopt );

	Con_Printf( "Loading renderer: %s -> %s\n", refopt, refdll );

	if( !R_LoadProgs( refdll ))
	{
		R_Shutdown();
		if( !quiet )
			Sys_Warn( S_ERROR "Can't initialize %s renderer!\n", refdll );
		return false;
	}

	Cvar_FullSet( "r_refdll_loaded", refopt, FCVAR_READ_ONLY );
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

	R_SaveVideoMode( width, height, width, height, false );
}

static void SetFullscreenModeFromCommandLine( void )
{
	if( Sys_CheckParm( "-borderless" ))
		Cvar_DirectSet( &vid_fullscreen, "2" );
	else if( Sys_CheckParm( "-fullscreen" ))
		Cvar_DirectSet( &vid_fullscreen, "1" );
	else if( Sys_CheckParm( "-windowed" ))
		Cvar_DirectSet( &vid_fullscreen, "0" );
}

static void R_CollectRendererNames( void )
{
	// ordering is important!
	static const char *short_names[] =
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
#if XASH_REF_GLES3COMPAT_ENABLED
		"gles3compat",
#endif
#if XASH_REF_SOFT_ENABLED
		"soft",
#endif
	};

	// ordering is important here too!
	static const char *long_names[ARRAYSIZE( short_names )] =
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
#if XASH_REF_GLES3COMPAT_ENABLED
		"GLES3 (gl2_shim)",
#endif
#if XASH_REF_SOFT_ENABLED
		"Software",
#endif
	};

	ref.num_renderers = ARRAYSIZE( short_names );
	ref.short_names = short_names;
	ref.long_names = long_names;
}

qboolean R_Init( void )
{
	qboolean success = false;
	string requested_cmdline;
	string requested_cvar;

	Cvar_RegisterVariable( &gl_vsync );
	Cvar_RegisterVariable( &r_showtextures );
	Cvar_RegisterVariable( &r_adjust_fov );
	Cvar_RegisterVariable( &r_decals );
	Cvar_RegisterVariable( &gl_msaa_samples );
	Cvar_RegisterVariable( &gl_clear );
	Cvar_RegisterVariable( &r_showtree );
	Cvar_RegisterVariable( &r_refdll );
	Cvar_RegisterVariable( &r_refdll_loaded );

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
	Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "compatibility cvar, does nothing" );
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
	requested_cmdline[0] = 0;
	requested_cvar[0] = 0;

	if( Sys_GetParmFromCmdLine( "-ref", requested_cmdline ))
		success = R_LoadRenderer( requested_cmdline, false );

	if( !success && COM_CheckString( r_refdll.string ) && Q_stricmp( requested_cmdline, r_refdll.string ))
	{
		Q_strncpy( requested_cvar, r_refdll.string, sizeof( requested_cvar ));

		// do not show scary messages to user if renderer set in config cannot be loaded
		// as game data could be copied from one platform to another, where this renderer
		// might not be supported (ref_gl on Android for example)
		success = R_LoadRenderer( requested_cvar, !host_developer.value );
	}

	if( !success )
	{
		int i;

		for( i = 0; i < ref.num_renderers; i++ )
		{
			// skip renderer that was requested but failed to load
			if( !Q_strcmp( requested_cmdline, ref.short_names[i] ))
				continue;

			if( !Q_strcmp( requested_cvar, ref.short_names[i] ))
				continue;

			// do not show bruteforcing attempts, however, warn user about falling back
			// to software mode
			if( !Q_strcmp( "soft", ref.short_names[i] ) && !host_developer.value )
				Sys_Warn( "Can't initialize any hardware accelerated renderer. Falling back to software rendering...\n" );

			success = R_LoadRenderer( ref.short_names[i], !host_developer.value );

			if( success )
			{
				// remember last valid renderer
				Cvar_DirectSet( &r_refdll, ref.short_names[i] );
				break;
			}
		}
	}

	if( !success )
	{
		Sys_Error( "Can't initialize any renderer. Check your video drivers!\n" );
		return false;
	}

	SCR_Init();

	return true;
}
