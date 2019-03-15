#include "common.h"
#include "client.h"
#include "library.h"
#include "cl_tent.h"

struct ref_state_s ref;
ref_globals_t refState;

convar_t *gl_vsync;
convar_t *gl_showtextures;
convar_t *r_decals;
convar_t *r_adjust_fov;
convar_t *gl_wgl_msaa_samples;

void R_GetTextureParms( int *w, int *h, int texnum )
{
	if( w ) *w = RENDER_GET_PARM( PARM_TEX_WIDTH, texnum );
	if( h ) *h = RENDER_GET_PARM( PARM_TEX_HEIGHT, texnum );
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

static int TriGetRenderMode( void )
{
	return clgame.ds.renderMode;
}

static int pfnRefRenderGetParm( int parm, int arg )
{
	return CL_RenderGetParm( parm, arg, false ); // prevent recursion
}

static ref_api_t gEngfuncs =
{
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
	dst->SpriteTexture     = src->SpriteTexture;
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
		Mod_UnloadSpriteModel( mod );
	}
	memset( clgame.sprites, 0, sizeof( clgame.sprites ));

	R_UnloadProgs();
	ref.initialized = false;
}

qboolean R_Init( void )
{
	char refdll[64];

	refdll[0] = 0;

	if( !Sys_GetParmFromCmdLine( "-ref", refdll ) )
	{
		Q_snprintf( refdll, sizeof( refdll ), "%s%s.%s",
#ifdef OS_LIB_PREFIX
			OS_LIB_PREFIX,
#else
			"",
#endif
			DEFAULT_RENDERER, OS_LIB_EXT );
	}

	gl_vsync = Cvar_Get( "gl_vsync", "0", FCVAR_ARCHIVE,  "enable vertical syncronization" );
	gl_showtextures = Cvar_Get( "gl_showtextures", "0", FCVAR_CHEAT, "show all uploaded textures" );
	r_adjust_fov = Cvar_Get( "r_adjust_fov", "1", FCVAR_ARCHIVE, "making FOV adjustment for wide-screens" );
	r_decals = Cvar_Get( "r_decals", "4096", FCVAR_ARCHIVE, "sets the maximum number of decals" );

	if( !R_LoadProgs( refdll ))
	{
		R_Shutdown();
		Host_Error( "Can't initialize %s renderer!\n", refdll );
		return false;
	}

	SCR_Init();

	return true;
}
