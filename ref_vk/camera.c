#include "camera.h"
#include "vk_common.h"
#include "vk_math.h"

#include "ref_params.h"
#include "pm_movevars.h"

vk_global_camera_t g_camera;

#define WORLDMODEL (gEngine.pfnGetModelByIndex( 1 ))
#define MOVEVARS (gEngine.pfnGetMoveVars())
static float R_GetFarClip( void )
{
	if( WORLDMODEL /* FIXME VK && RI.drawWorld */ )
		return MOVEVARS->zmax * 1.73f;
	return 2048.0f;
}

static void R_SetupModelviewMatrix( matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	Matrix4x4_ConcatRotate( m, -g_camera.viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -g_camera.viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -g_camera.viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -g_camera.vieworg[0], -g_camera.vieworg[1], -g_camera.vieworg[2] );
}

static void R_SetupProjectionMatrix( matrix4x4 m )
{
	float xMin, xMax, yMin, yMax, zNear, zFar;

	/*
	if( RI.drawOrtho )
	{
		const ref_overview_t *ov = gEngfuncs.GetOverviewParms();
		Matrix4x4_CreateOrtho( m, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
		return;
	}
	*/

	const float farClip = R_GetFarClip();

	zNear = 4.0f;
	zFar = Q_max( 256.0f, farClip );

	yMax = zNear * tan( g_camera.fov_y * M_PI_F / 360.0f );
	yMin = -yMax;

	xMax = zNear * tan( g_camera.fov_x * M_PI_F / 360.0f );
	xMin = -xMax;

	Matrix4x4_CreateProjection( m, xMax, xMin, yMax, yMin, zNear, zFar );
}

// Analagous to R_SetupRefParams, R_SetupFrustum in GL/Soft renderers
void R_SetupCamera( const ref_viewpass_t *rvp )
{
	/* FIXME VK unused?
	RI.params = RP_NONE;
	RI.drawWorld = FBitSet( rvp->flags, RF_DRAW_WORLD );
	RI.onlyClientDraw = FBitSet( rvp->flags, RF_ONLY_CLIENTDRAW );
	RI.farClip = 0;

	if( !FBitSet( rvp->flags, RF_DRAW_CUBEMAP ))
		RI.drawOrtho = FBitSet( rvp->flags, RF_DRAW_OVERVIEW );
	else RI.drawOrtho = false;
	*/

	// setup viewport
	g_camera.viewport[0] = rvp->viewport[0];
	g_camera.viewport[1] = rvp->viewport[1];
	g_camera.viewport[2] = rvp->viewport[2];
	g_camera.viewport[3] = rvp->viewport[3];

	// calc FOV
	g_camera.fov_x = rvp->fov_x;
	g_camera.fov_y = rvp->fov_y;

	VectorCopy( rvp->vieworigin, g_camera.vieworg );
	VectorCopy( rvp->viewangles, g_camera.viewangles );
	// FIXME VK unused? VectorCopy( rvp->vieworigin, g_camera.pvsorigin );

#define RP_NORMALPASS() true // FIXME ???
	if( RP_NORMALPASS() && ( gEngine.EngineGetParm( PARM_WATER_LEVEL, 0 ) >= 3 ))
	{
		g_camera.fov_x = atan( tan( DEG2RAD( g_camera.fov_x ) / 2 ) * ( 0.97f + sin( gpGlobals->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
		g_camera.fov_y = atan( tan( DEG2RAD( g_camera.fov_y ) / 2 ) * ( 1.03f - sin( gpGlobals->time * 1.5f ) * 0.03f )) * 2 / (M_PI_F / 180.0f);
	}

	// build the transformation matrix for the given view angles
	AngleVectors( g_camera.viewangles, g_camera.vforward, g_camera.vright, g_camera.vup );

	/* FIXME VK unused?
	if( !r_lockfrustum->value )
	{
		VectorCopy( RI.vieworg, RI.cullorigin );
		VectorCopy( RI.vforward, RI.cull_vforward );
		VectorCopy( RI.vright, RI.cull_vright );
		VectorCopy( RI.vup, RI.cull_vup );
	}
	*/

	/* FIXME VK unused?
	if( RI.drawOrtho )
		GL_FrustumInitOrtho( &RI.frustum, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
	else GL_FrustumInitProj( &g_camera.frustum, 0.0f, R_GetFarClip(), g_camera.fov_x, g_camera.fov_y ); // NOTE: we ignore nearplane here (mirrors only)
	*/

	R_SetupProjectionMatrix( g_camera.projectionMatrix );
	R_SetupModelviewMatrix( g_camera.modelviewMatrix );

	Matrix4x4_Concat( g_camera.worldviewProjectionMatrix, g_camera.projectionMatrix, g_camera.modelviewMatrix );
}

int R_WorldToScreen( const vec3_t point, vec3_t screen )
{
	matrix4x4	worldToScreen;
	qboolean	behind;
	float	w;

	if( !point || !screen )
		return true;

	Matrix4x4_Copy( worldToScreen, g_camera.worldviewProjectionMatrix );
	screen[0] = worldToScreen[0][0] * point[0] + worldToScreen[0][1] * point[1] + worldToScreen[0][2] * point[2] + worldToScreen[0][3];
	screen[1] = worldToScreen[1][0] * point[0] + worldToScreen[1][1] * point[1] + worldToScreen[1][2] * point[2] + worldToScreen[1][3];
	w = worldToScreen[3][0] * point[0] + worldToScreen[3][1] * point[1] + worldToScreen[3][2] * point[2] + worldToScreen[3][3];
	screen[2] = 0.0f; // just so we have something valid here

	if( w < 0.001f )
	{
		screen[0] *= 100000;
		screen[1] *= 100000;
		behind = true;
	}
	else
	{
		float invw = 1.0f / w;
		screen[0] *= invw;
		screen[1] *= invw;
		behind = false;
	}

	return behind;
}

int TriWorldToScreen( const float *world, float *screen )
{
	int	retval;

	retval = R_WorldToScreen( world, screen );

	screen[0] =  0.5f * screen[0] * (float)g_camera.viewport[2];
	screen[1] = -0.5f * screen[1] * (float)g_camera.viewport[3];
	screen[0] += 0.5f * (float)g_camera.viewport[2];
	screen[1] += 0.5f * (float)g_camera.viewport[3];

	return retval;
}
