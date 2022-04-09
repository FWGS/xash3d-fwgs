
#include "gu_local.h"

cvar_t	*gl_texture_lodfunc;
cvar_t	*gl_texture_lodbias;
cvar_t	*gl_texture_lodslope;

cvar_t	*gl_texture_nearest;
cvar_t	*gl_lightmap_nearest;
cvar_t	*gl_keeptjunctions;
cvar_t	*gl_emboss_scale;
cvar_t	*gl_detailscale;
cvar_t	*gl_depthoffset;
cvar_t	*gl_wireframe;
cvar_t	*gl_finish;
cvar_t	*gl_nosort;
cvar_t	*gl_vsync;
cvar_t	*gl_clear;
cvar_t	*gl_test;
cvar_t	*gl_stencilbits;
cvar_t	*gl_subdivide_size;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_norefresh;
cvar_t	*r_lighting_extended;
cvar_t	*r_lighting_modulate;
cvar_t	*r_lighting_ambient;
cvar_t	*r_detailtextures;
cvar_t	*r_drawentities;
cvar_t	*r_adjust_fov;
cvar_t	*r_decals;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_lockpvs;
cvar_t	*r_lockfrustum;
cvar_t	*r_traceglow;
cvar_t	*r_dynamic;
cvar_t	*r_lightmap;
cvar_t	*r_showhull;
cvar_t	*gl_round_down;
cvar_t	*gl_showtextures;
cvar_t	*cl_lightstyle_lerping;

cvar_t	*vid_brightness;
cvar_t	*vid_gamma;
cvar_t	*tracerred;
cvar_t	*tracergreen;
cvar_t	*tracerblue;
cvar_t	*traceralpha;

byte		*r_temppool;

gl_globals_t	tr;
glconfig_t	glConfig;
glstate_t	glState;
glwstate_t	glw_state;

/*
==============
GL_GetProcAddress

defined just for nanogl/glwes, so it don't link to SDL2 directly, nor use dlsym
==============
*/
void GAME_EXPORT *GL_GetProcAddress( const char *name )
{
	return gEngfuncs.GL_GetProcAddress( name );
}

/*
===============
GL_SetDefaultState
===============
*/
static void GL_SetDefaultState( void )
{
	memset( &glState, 0, sizeof( glState ));

	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;

	// init glState struct
	glState.currentTexture = -1;
	glState.texIdentityMatrix = true;
	glState.fogColor = 0;
	glState.fogDensity = 0;
	glState.fogStart = 100.0f;
	glState.fogEnd = 1000.0f;
}

/*
===============
GL_SetDefaults
===============
*/
static void GL_SetDefaults( void )
{
#if 1
/*
	sceGuFinish();
	sceGuSync(0,0);
*/
	sceGuClearColor( GU_COLOR( 0.5f, 0.5f, 0.5f, 1.0f ) );

	sceGuDisable( GU_DEPTH_TEST );
	sceGuDisable( GU_CULL_FACE );
	//sceGuDisable( GU_SCISSOR_TEST );
	sceGuDepthFunc( GU_LEQUAL );
	sceGuColor( 0xffffffff );

	if( glState.stencilEnabled )
	{
		sceGuDisable( GU_STENCIL_TEST );
		/*pglStencilMask( ( GLuint ) ~0 );*/
		sceGuStencilFunc( GU_EQUAL, 0, ~0 );
		sceGuStencilOp( GU_KEEP, GU_INCR, GU_INCR );
	}
	sceGuDepthOffset( 0 );
	
	GL_CleanupAllTextureUnits();

	sceGuDisable( GU_BLEND );
	sceGuDisable( GU_ALPHA_TEST );

	sceGuAlphaFunc( GU_GREATER, DEFAULT_ALPHATEST, 0xff );
	sceGuEnable( GU_TEXTURE_2D );
	sceGuShadeModel( GU_SMOOTH );
	sceGuFrontFace( GU_CCW );
#else
	pglFinish();

	pglClearColor( 0.5f, 0.5f, 0.5f, 1.0f );

	pglDisable( GL_DEPTH_TEST );
	pglDisable( GL_CULL_FACE );
	pglDisable( GL_SCISSOR_TEST );
	pglDepthFunc( GL_LEQUAL );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	if( glState.stencilEnabled )
	{
		pglDisable( GL_STENCIL_TEST );
		pglStencilMask( ( GLuint ) ~0 );
		pglStencilFunc( GL_EQUAL, 0, ~0 );
		pglStencilOp( GL_KEEP, GL_INCR, GL_INCR );
	}

	pglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	pglPolygonOffset( -1.0f, -2.0f );

	GL_CleanupAllTextureUnits();

	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_POLYGON_OFFSET_FILL );
	pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
	pglEnable( GL_TEXTURE_2D );
	pglShadeModel( GL_SMOOTH );
	pglFrontFace( GL_CCW );

	pglPointSize( 1.2f );
	pglLineWidth( 1.2f );
#endif
	GL_Cull( GL_NONE );
}


/*
=================
R_RenderInfo_f
=================
*/
void R_RenderInfo_f( void )
{
	gEngfuncs.Con_Printf( "\n" );
	gEngfuncs.Con_Printf( "HARDWARE RENDER\n");
	gEngfuncs.Con_Printf( "MAX_TEXTURE_SIZE: %i\n", glConfig.max_texture_size );
	gEngfuncs.Con_Printf( "MODE: %ix%i\n", gpGlobals->width, gpGlobals->height );
	gEngfuncs.Con_Printf( "VERTICAL SYNC: %s\n", gl_vsync->value ? "enabled" : "disabled" );
	gEngfuncs.Con_Printf( "Color %d bits, Alpha %d bits, Depth %d bits, Stencil %d bits\n", glConfig.color_bits,
		glConfig.alpha_bits, glConfig.depth_bits, glConfig.stencil_bits );
}

void GL_InitExtensions( void )
{
//	int colorBits[3];

	glConfig.color_bits = 5 + 5 + 5;
	glConfig.alpha_bits = 1;
	glConfig.stencil_bits = ( int )gl_stencilbits->value;
	glState.stencilEnabled = true;
	glConfig.max_texture_size = 256;
	
	// get our various GL strings
	gEngfuncs.Con_Reportf( "^3Video^7: PSP HW\n" );

	gEngfuncs.Cvar_Get( "gl_max_size", va( "%i", glConfig.max_texture_size ), 0, "opengl texture max dims" );
/*
	gEngfuncs.Image_AddCmdFlags( IL_DDS_HARDWARE );
*/
	R_RenderInfo_f();

	tr.framecount = tr.visframecount = 1;
	glw_state.initialized = true;
}

void GL_ClearExtensions( void )
{
	// now all extensions are disabled
	glw_state.initialized = false;
}

//=======================================================================

/*
=================
GL_InitCommands
=================
*/
void GL_InitCommands( void )
{
	r_speeds = gEngfuncs.Cvar_Get( "r_speeds", "0", FCVAR_ARCHIVE, "shows renderer speeds" );
	r_fullbright = gEngfuncs.Cvar_Get( "r_fullbright", "0", FCVAR_CHEAT, "disable lightmaps, get fullbright for entities" );
	r_norefresh = gEngfuncs.Cvar_Get( "r_norefresh", "0", 0, "disable 3D rendering (use with caution)" );
	r_lighting_extended = gEngfuncs.Cvar_Get( "r_lighting_extended", "0", FCVAR_ARCHIVE, "allow to get lighting from world and bmodels" ); // disabled
	r_lighting_modulate = gEngfuncs.Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	r_lighting_ambient = gEngfuncs.Cvar_Get( "r_lighting_ambient", "0.3", FCVAR_ARCHIVE, "map ambient lighting scale" );
	r_novis = gEngfuncs.Cvar_Get( "r_novis", "0", 0, "ignore vis information (perfomance test)" );
	r_nocull = gEngfuncs.Cvar_Get( "r_nocull", "0", 0, "ignore frustrum culling (perfomance test)" );
	r_detailtextures = gEngfuncs.Cvar_Get( "r_detailtextures", "0", FCVAR_ARCHIVE, "enable detail textures support, use '2' for autogenerate detail.txt" );  // disabled
	r_lockpvs = gEngfuncs.Cvar_Get( "r_lockpvs", "0", FCVAR_CHEAT, "lockpvs area at current point (pvs test)" );
	r_lockfrustum = gEngfuncs.Cvar_Get( "r_lockfrustum", "0", FCVAR_CHEAT, "lock frustrum area at current point (cull test)" );
	r_dynamic = gEngfuncs.Cvar_Get( "r_dynamic", "1", FCVAR_ARCHIVE, "allow dynamic lighting (dlights, lightstyles)" );
	r_traceglow = gEngfuncs.Cvar_Get( "r_traceglow", "0", FCVAR_ARCHIVE, "cull flares behind models" );  // disabled
	r_lightmap = gEngfuncs.Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	r_drawentities = gEngfuncs.Cvar_Get( "r_drawentities", "1", FCVAR_CHEAT, "render entities" );
	r_decals = gEngfuncs.pfnGetCvarPointer( "r_decals", 0 );
	r_showhull = gEngfuncs.pfnGetCvarPointer( "r_showhull", 0 );

	gl_texture_nearest = gEngfuncs.Cvar_Get( "gl_texture_nearest", "0", FCVAR_GLCONFIG, "disable texture filter" );
	gl_lightmap_nearest = gEngfuncs.Cvar_Get( "gl_lightmap_nearest", "0", FCVAR_GLCONFIG, "disable lightmap filter" );
	gl_vsync = gEngfuncs.pfnGetCvarPointer( "gl_vsync", 0 );	
	gl_detailscale = gEngfuncs.Cvar_Get( "gl_detailscale", "4.0", FCVAR_GLCONFIG, "default scale applies while auto-generate list of detail textures" );
	gl_texture_lodfunc = gEngfuncs.Cvar_Get( "gl_texture_lodfunc", "2", FCVAR_GLCONFIG, "LOD func for mipmapped textures" );
	gl_texture_lodbias = gEngfuncs.Cvar_Get( "gl_texture_lodbias", "-6.0", FCVAR_GLCONFIG, "LOD bias for mipmapped textures (perfomance|quality)" );
	gl_texture_lodslope = gEngfuncs.Cvar_Get( "gl_texture_lodslope", "0.3", FCVAR_GLCONFIG, "LOD slope for mipmapped textures" );
	gl_keeptjunctions = gEngfuncs.Cvar_Get( "gl_keeptjunctions", "1", FCVAR_GLCONFIG, "removing tjuncs causes blinking pixels" );
	gl_emboss_scale = gEngfuncs.Cvar_Get( "gl_emboss_scale", "0", FCVAR_GLCONFIG|FCVAR_LATCH, "fake bumpmapping scale" );
	gl_showtextures = gEngfuncs.pfnGetCvarPointer( "r_showtextures", 0 );
	gl_finish = gEngfuncs.Cvar_Get( "gl_finish", "0", FCVAR_GLCONFIG, "use glFinish instead of glFlush" );
	gl_nosort = gEngfuncs.Cvar_Get( "gl_nosort", "1", FCVAR_GLCONFIG, "disable sorting of translucent surfaces" );
	gl_clear = gEngfuncs.pfnGetCvarPointer( "gl_clear", 0 );
	gl_test = gEngfuncs.Cvar_Get( "gl_test", "0", 0, "engine developer cvar for quick testing new features" );
	gl_wireframe = gEngfuncs.Cvar_Get( "gl_wireframe", "0", FCVAR_GLCONFIG|FCVAR_SPONLY, "show wireframe overlay" );
	gl_stencilbits = gEngfuncs.Cvar_Get( "gl_stencilbits", "8", FCVAR_GLCONFIG|FCVAR_READ_ONLY, "pixelformat stencil bits (0 - auto)" );
	gl_subdivide_size = gEngfuncs.Cvar_Get( "gl_subdivide_size", "256.0", FCVAR_GLCONFIG, "the division value for the sky brushes" );
	gl_round_down = gEngfuncs.Cvar_Get( "gl_round_down", "2", FCVAR_GLCONFIG|FCVAR_READ_ONLY, "round texture sizes to nearest POT value" );
	// these cvar not used by engine but some mods requires this
	gl_depthoffset = gEngfuncs.Cvar_Get( "gl_depthoffset", "256.0", FCVAR_GLCONFIG, "depth offset for decals" );

	// make sure gl_vsync is checked after vid_restart
	SetBits( gl_vsync->flags, FCVAR_CHANGED );

	vid_gamma = gEngfuncs.pfnGetCvarPointer( "gamma", 0 );
	vid_brightness = gEngfuncs.pfnGetCvarPointer( "brightness", 0 );

	tracerred = gEngfuncs.Cvar_Get( "tracerred", "0.8", 0, "tracer red component weight ( 0 - 1.0 )" );
	tracergreen = gEngfuncs.Cvar_Get( "tracergreen", "0.8", 0, "tracer green component weight ( 0 - 1.0 )" );
	tracerblue = gEngfuncs.Cvar_Get( "tracerblue", "0.4", 0, "tracer blue component weight ( 0 - 1.0 )" );
	traceralpha = gEngfuncs.Cvar_Get( "traceralpha", "0.5", 0, "tracer alpha amount ( 0 - 1.0 )" );

	cl_lightstyle_lerping = gEngfuncs.pfnGetCvarPointer( "cl_lightstyle_lerping", 0 );

	gEngfuncs.Cmd_AddCommand( "r_info", R_RenderInfo_f, "display renderer info" );
	gEngfuncs.Cmd_AddCommand( "timerefresh", SCR_TimeRefresh_f, "turn quickly and print rendering statistcs" );
}

/*
=================
GL_RemoveCommands
=================
*/
void GL_RemoveCommands( void )
{
	gEngfuncs.Cmd_RemoveCommand( "r_info" );
}

/*
===============
R_Init
===============
*/
qboolean R_Init( void )
{
	if( glw_state.initialized )
		return true;

	GL_InitCommands();
	GL_InitRandomTable();

	GL_SetDefaultState();

	// create the window and set up the context
	if( !gEngfuncs.R_Init_Video( REF_GL )) // request GL context
	{
		GL_RemoveCommands();
		gEngfuncs.R_Free_Video();
// Why? Host_Error again???
//		gEngfuncs.Host_Error( "Can't initialize video subsystem\nProbably driver was not installed" );
		return false;
	}

	r_temppool = Mem_AllocPool( "Render Zone" );

	GL_SetDefaults();
	R_InitImages();
	R_SpriteInit();
	R_StudioInit();
	R_AliasInit();
	R_ClearDecals();
	R_ClearScene();

	return true;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown( void )
{
	if( !glw_state.initialized )
		return;

	GL_RemoveCommands();
	R_ShutdownImages();

	Mem_FreePool( &r_temppool );

	// shut down OS specific OpenGL stuff like contexts, etc.
	gEngfuncs.R_Free_Video();
}

void GL_SetupAttributes( int safegl )
{

}
