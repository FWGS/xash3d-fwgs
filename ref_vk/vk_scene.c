#include "vk_scene.h"
#include "vk_brush.h"
#include "vk_studio.h"
#include "vk_lightmap.h"
#include "vk_const.h"
#include "vk_render.h"
#include "vk_math.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_sprite.h"
#include "vk_global.h"
#include "vk_beams.h"
#include "vk_light.h"
#include "vk_rtx.h"
#include "vk_textures.h"

#include "com_strings.h"
#include "ref_params.h"
#include "eiface.h"
#include "pm_movevars.h"

#include <stdlib.h> // qsort
#include <memory.h>

typedef struct draw_list_s {
	struct cl_entity_s	*solid_entities[MAX_SCENE_ENTITIES];	// opaque moving or alpha brushes
	vk_trans_entity_t trans_entities[MAX_SCENE_ENTITIES];	// translucent brushes or studio models kek
	struct cl_entity_s	*beam_entities[MAX_SCENE_ENTITIES];
	uint		num_solid_entities;
	uint		num_trans_entities;
	uint		num_beam_entities;
} draw_list_t;

static struct {
	draw_list_t	draw_stack[MAX_SCENE_STACK];
	int		draw_stack_pos;
	draw_list_t	*draw_list;
} g_lists;

vk_global_camera_t g_camera;

void VK_SceneInit( void )
{
	g_lists.draw_list = g_lists.draw_stack;
	g_lists.draw_stack_pos = 0;
}

#define R_ModelOpaque( rm )	( rm == kRenderNormal )
int R_FIXME_GetEntityRenderMode( cl_entity_t *ent )
{
	//int		i, opaque, trans;
	//mstudiotexture_t	*ptexture;
	model_t		*model;
	//studiohdr_t	*phdr;

	/* TODO
	if( ent->player ) // check it for real playermodel
		model = R_StudioSetupPlayerModel( ent->curstate.number - 1 );
	else */ model = ent->model;

	if( R_ModelOpaque( ent->curstate.rendermode ))
	{
		if(( model && model->type == mod_brush ) && FBitSet( model->flags, MODEL_TRANSPARENT ))
			return kRenderTransAlpha;
	}

	/* TODO studio models hack
	ptexture = (mstudiotexture_t *)((byte *)phdr + phdr->textureindex);

	for( opaque = trans = i = 0; i < phdr->numtextures; i++, ptexture++ )
	{
		// ignore chrome & additive it's just a specular-like effect
		if( FBitSet( ptexture->flags, STUDIO_NF_ADDITIVE ) && !FBitSet( ptexture->flags, STUDIO_NF_CHROME ))
			trans++;
		else opaque++;
	}

	// if model is more additive than opaque
	if( trans > opaque )
		return kRenderTransAdd;
	*/
	return ent->curstate.rendermode;
}

// tell the renderer what new map is started
void R_NewMap( void )
{
	const int num_models = gEngine.EngineGetParm( PARM_NUMMODELS, 0 );

	// Existence of cache.data for the world means that we've already have loaded this map
	// and this R_NewMap call is from within loading of a saved game.
	const qboolean is_save_load = !!gEngine.pfnGetModelByIndex( 1 )->cache.data;

	gEngine.Con_Reportf( "R_NewMap, loading save: %d\n", is_save_load );

	// Skip clearing already loaded data if the map hasn't changed.
	if (is_save_load)
		return;

	// TODO should we do something like VK_BrushBeginLoad?
	VK_BrushStatsClear();

	XVK_RenderBufferMapClear();

	VK_ClearLightmap();

	// This is to ensure that we have computed lightstyles properly
	VK_RunLightStyles();

	VK_LightsNewMap();

	if (vk_core.rtx)
		VK_RayNewMap();

	// RTX map loading requires command buffer for building blases
	if (vk_core.rtx)
	{
		const VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		XVK_CHECK(vkBeginCommandBuffer(vk_core.cb, &beginfo));
	}

	// Load all models at once
	gEngine.Con_Reportf( "Num models: %d:\n", num_models );
	for( int i = 0; i < num_models; i++ )
	{
		model_t	*m;
		if(( m = gEngine.pfnGetModelByIndex( i + 1 )) == NULL )
			continue;

		gEngine.Con_Reportf( "  %d: name=%s, type=%d, submodels=%d, nodes=%d, surfaces=%d, nummodelsurfaces=%d\n", i, m->name, m->type, m->numsubmodels, m->numnodes, m->numsurfaces, m->nummodelsurfaces);

		if( m->type != mod_brush )
			continue;

		if (!VK_BrushModelLoad( m, i == 0 ))
		{
			gEngine.Con_Printf( S_ERROR "Couldn't load model %s\n", m->name );
		}
	}

	if (vk_core.rtx)
	{
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &vk_core.cb,
		};

		XVK_CHECK(vkEndCommandBuffer(vk_core.cb));
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
		XVK_CHECK(vkQueueWaitIdle(vk_core.queue));
	}

	// TODO should we do something like VK_BrushEndLoad?
	VK_UploadLightmap();
	XVK_RenderBufferMapFreeze();
	XVK_RenderBufferPrintStats();
	if (vk_core.rtx)
		VK_RayMapLoadEnd();
}

qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	/* if( !r_drawentities->value ) */
	/* 	return false; // not allow to drawing */
	int render_mode;

	if( !clent || !clent->model )
		return false; // if set to invisible, skip

	if( FBitSet( clent->curstate.effects, EF_NODRAW ))
		return false; // done

	render_mode = R_FIXME_GetEntityRenderMode( clent );

	/* TODO
	if( !R_ModelOpaque( clent->curstate.rendermode ) && CL_FxBlend( clent ) <= 0 )
		return true; // invisible

	switch( type )
	{
	case ET_FRAGMENTED:
		r_stats.c_client_ents++;
		break;
	case ET_TEMPENTITY:
		r_stats.c_active_tents_count++;
		break;
	default: break;
	}
	*/

	if( render_mode == kRenderNormal )
	{
		if( g_lists.draw_list->num_solid_entities >= ARRAYSIZE(g_lists.draw_list->solid_entities) )
			return false;

		g_lists.draw_list->solid_entities[g_lists.draw_list->num_solid_entities] = clent;
		g_lists.draw_list->num_solid_entities++;
	}
	else
	{
		if( g_lists.draw_list->num_trans_entities >= ARRAYSIZE(g_lists.draw_list->trans_entities) )
			return false;

		g_lists.draw_list->trans_entities[g_lists.draw_list->num_trans_entities] = (vk_trans_entity_t){ clent, render_mode };
		g_lists.draw_list->num_trans_entities++;
	}

	return true;
}

void R_ProcessEntData( qboolean allocate )
{
	if( !allocate )
	{
		g_lists.draw_list->num_solid_entities = 0;
		g_lists.draw_list->num_trans_entities = 0;
		g_lists.draw_list->num_beam_entities = 0;
	}

	if( gEngine.drawFuncs->R_ProcessEntData )
		gEngine.drawFuncs->R_ProcessEntData( allocate );
}

void R_ClearScreen( void )
{
	g_lists.draw_list->num_solid_entities = 0;
	g_lists.draw_list->num_trans_entities = 0;
	g_lists.draw_list->num_beam_entities = 0;

	// clear the scene befor start new frame
	if( gEngine.drawFuncs->R_ClearScene != NULL )
		gEngine.drawFuncs->R_ClearScene();

}

void R_PushScene( void )
{
	if( ++g_lists.draw_stack_pos >= MAX_SCENE_STACK )
		gEngine.Host_Error( "draw stack overflow\n" );
	g_lists.draw_list = &g_lists.draw_stack[g_lists.draw_stack_pos];
}

void R_PopScene( void )
{
	if( --g_lists.draw_stack_pos < 0 )
		gEngine.Host_Error( "draw stack underflow\n" );
	g_lists.draw_list = &g_lists.draw_stack[g_lists.draw_stack_pos];
}

// clear the render entities before each frame
void R_ClearScene( void )
{
	g_lists.draw_list->num_solid_entities = 0;
	g_lists.draw_list->num_trans_entities = 0;
	g_lists.draw_list->num_beam_entities = 0;
}


void R_RenderScene( void )
{
	PRINT_NOT_IMPLEMENTED();
}

#define WORLDMODEL (gEngine.pfnGetModelByIndex( 1 ))
#define MOVEVARS (gEngine.pfnGetMoveVars())
static float R_GetFarClip( void )
{
	if( WORLDMODEL /* FIXME VK && RI.drawWorld */ )
		return MOVEVARS->zmax * 1.73f;
	return 2048.0f;
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

static void R_SetupModelviewMatrix( matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	Matrix4x4_ConcatRotate( m, -g_camera.viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -g_camera.viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -g_camera.viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -g_camera.vieworg[0], -g_camera.vieworg[1], -g_camera.vieworg[2] );
}

static void R_RotateForEntity( matrix4x4 out, const cl_entity_t *e )
{
	float	scale = 1.0f;

	if( e == gEngine.GetEntityByIndex( 0 ) )
	{
		Matrix4x4_LoadIdentity(out);
		return;
	}

	if( e->model->type != mod_brush && e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	Matrix4x4_CreateFromEntity( out, e->angles, e->origin, scale );
}

// FIXME find a better place for this function
static int R_RankForRenderMode( int rendermode )
{
	switch( rendermode )
	{
	case kRenderTransTexture:
		return 1;	// draw second
	case kRenderTransAdd:
		return 2;	// draw third
	case kRenderGlow:
		return 3;	// must be last!
	}
	return 0;
}

/*
===============
R_TransEntityCompare

Sorting translucent entities by rendermode then by distance

FIXME find a better place for this function
===============
*/
static int R_TransEntityCompare( const void *a, const void *b)
{
	vk_trans_entity_t *tent1, *tent2;
	cl_entity_t	*ent1, *ent2;
	vec3_t		vecLen, org;
	float		dist1, dist2;
	int		rendermode1;
	int		rendermode2;

	tent1 = (vk_trans_entity_t*)a;
	tent2 = (vk_trans_entity_t*)b;

	ent1 = tent1->entity;
	ent2 = tent2->entity;

	rendermode1 = tent1->render_mode;
	rendermode2 = tent2->render_mode;

	// sort by distance
	if( ent1->model->type != mod_brush || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( g_camera.vieworg, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else dist1 = 1000000000;

	if( ent2->model->type != mod_brush || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( g_camera.vieworg, org, vecLen );
		dist2 = DotProduct( vecLen, vecLen );
	}
	else dist2 = 1000000000;

	if( dist1 > dist2 )
		return -1;
	if( dist1 < dist2 )
		return 1;

	// then sort by rendermode
	if( R_RankForRenderMode( rendermode1 ) > R_RankForRenderMode( rendermode2 ))
		return 1;
	if( R_RankForRenderMode( rendermode1 ) < R_RankForRenderMode( rendermode2 ))
		return -1;

	return 0;
}

// FIXME where should this function be
#define RP_NORMALPASS() true // FIXME ???
int CL_FxBlend( cl_entity_t *e ) // FIXME do R_SetupFrustum: , vec3_t vforward )
{
	int	blend = 0;
	float	offset, dist;
	vec3_t	tmp;

	offset = ((int)e->index ) * 363.0f; // Use ent index to de-sync these fx

	switch( e->curstate.renderfx )
	{
	case kRenderFxPulseSlowWide:
		blend = e->curstate.renderamt + 0x40 * sin( gpGlobals->time * 2 + offset );
		break;
	case kRenderFxPulseFastWide:
		blend = e->curstate.renderamt + 0x40 * sin( gpGlobals->time * 8 + offset );
		break;
	case kRenderFxPulseSlow:
		blend = e->curstate.renderamt + 0x10 * sin( gpGlobals->time * 2 + offset );
		break;
	case kRenderFxPulseFast:
		blend = e->curstate.renderamt + 0x10 * sin( gpGlobals->time * 8 + offset );
		break;
	case kRenderFxFadeSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 0 )
				e->curstate.renderamt -= 1;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt > 3 )
				e->curstate.renderamt -= 4;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 255 )
				e->curstate.renderamt += 1;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		if( RP_NORMALPASS( ))
		{
			if( e->curstate.renderamt < 252 )
				e->curstate.renderamt += 4;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeSlow:
		blend = 20 * sin( gpGlobals->time * 4 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFast:
		blend = 20 * sin( gpGlobals->time * 16 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFaster:
		blend = 20 * sin( gpGlobals->time * 36 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerSlow:
		blend = 20 * (sin( gpGlobals->time * 2 ) + sin( gpGlobals->time * 17 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerFast:
		blend = 20 * (sin( gpGlobals->time * 16 ) + sin( gpGlobals->time * 23 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxHologram:
	case kRenderFxDistort:
		VectorCopy( e->origin, tmp );
		VectorSubtract( tmp, g_camera.vieworg, tmp );
		dist = DotProduct( tmp, g_camera.vforward );

		// turn off distance fade
		if( e->curstate.renderfx == kRenderFxDistort )
			dist = 1;

		if( dist <= 0 )
		{
			blend = 0;
		}
		else
		{
			e->curstate.renderamt = 180;
			if( dist <= 100 ) blend = e->curstate.renderamt;
			else blend = (int) ((1.0f - ( dist - 100 ) * ( 1.0f / 400.0f )) * e->curstate.renderamt );
			blend += gEngine.COM_RandomLong( -32, 31 );
		}
		break;
	default:
		blend = e->curstate.renderamt;
		break;
	}

	blend = bound( 0, blend, 255 );

	return blend;
}

// Analagous to R_SetupRefParams, R_SetupFrustum in GL/Soft renderers
static void setupCamera( const ref_viewpass_t *rvp )
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

static void drawEntity( cl_entity_t *ent, int render_mode )
{
	const model_t *mod = ent->model;
	matrix4x4 model;
	float alpha;

	if (!mod)
		return;

	// handle studiomodels with custom rendermodes on texture
	alpha = render_mode == kRenderNormal ? 1.f : CL_FxBlend( ent ) / 255.0f;

	// TODO ref_gl does this earlier (when adding entity), can we too?
	if( alpha <= 0.0f )
		return;

	switch (render_mode) {
		case kRenderNormal:
			VK_RenderStateSetColor( 1.f, 1.f, 1.f, 1.f);
			break;

		case kRenderTransColor:
			// FIXME also zero out texture? use white texture
			VK_RenderStateSetColor(
					ent->curstate.rendercolor.r / 255.f,
					ent->curstate.rendercolor.g / 255.f,
					ent->curstate.rendercolor.b / 255.f,
					ent->curstate.renderamt / 255.f);
			break;

		case kRenderTransAdd:
			VK_RenderStateSetColor( alpha, alpha, alpha, 1.f);
			break;

		case kRenderTransAlpha:
			VK_RenderStateSetColor( 1.f, 1.f, 1.f, 1.f);
			// TODO Q1compat Vector4Set(e_ubo->color, 1.f, 1.f, 1.f, alpha);
			break;

		default:
			VK_RenderStateSetColor( 1.f, 1.f, 1.f, alpha);
	}

	switch (mod->type)
	{
		case mod_brush:
			R_RotateForEntity( model, ent );
			VK_RenderStateSetMatrixModel( model );
			VK_BrushModelDraw( ent, render_mode );
			break;

		case mod_studio:
			VK_RenderStateSetMatrixModel( matrix4x4_identity );
			VK_StudioDrawModel( ent, render_mode );
			break;

		case mod_sprite:
			VK_RenderStateSetMatrixModel( matrix4x4_identity );
			VK_SpriteDrawModel( ent );
			break;

		case mod_alias:
		case mod_bad:
			PRINT_NOT_IMPLEMENTED();
			break;
	}
}

static float g_frametime = 0;
void VK_SceneRender( const ref_viewpass_t *rvp )
{
	int current_pipeline_index = kRenderNormal;

	g_frametime = /*FIXME VK RP_NORMALPASS( )) ? */
		gpGlobals->time - gpGlobals->oldtime
	/* FIXME VK : 0.f */;

	setupCamera( rvp );

	VK_RenderStateSetMatrixProjection( g_camera.projectionMatrix, g_camera.fov_y ); // FIXME why is this in degrees, not in radians? * M_PI_F / 360.0f );
	VK_RenderStateSetMatrixView( g_camera.modelviewMatrix );
	VK_RenderStateSetMatrixModel( matrix4x4_identity );

	VK_RenderDebugLabelBegin( "opaque" );

	// Draw view model
	{
		VK_RenderStateSetColor( 1.f, 1.f, 1.f, 1.f );
		R_RunViewmodelEvents();
		R_DrawViewModel();
	}

	// Draw world brush
	{
		cl_entity_t *world = gEngine.GetEntityByIndex( 0 );
		if( world && world->model )
		{
			//VK_LightsBakePVL( 0 /* FIXME frame number */);

			VK_RenderStateSetColor( 1.f, 1.f, 1.f, 1.f);
			VK_BrushModelDraw( world, kRenderNormal );
		}
	}

	// Draw opaque entities
	for (int i = 0; i < g_lists.draw_list->num_solid_entities; ++i)
	{
		cl_entity_t *ent = g_lists.draw_list->solid_entities[i];
		drawEntity(ent, kRenderNormal);
	}

	// Draw opaque beams
	gEngine.CL_DrawEFX( g_frametime, false );

	VK_RenderDebugLabelEnd();

	VK_RenderDebugLabelBegin( "tranparent" );

	{
		// sort translucents entities by rendermode and distance
		qsort( g_lists.draw_list->trans_entities, g_lists.draw_list->num_trans_entities, sizeof( vk_trans_entity_t ), R_TransEntityCompare );

		// Draw transparent ents
		for (int i = 0; i < g_lists.draw_list->num_trans_entities; ++i)
		{
			const vk_trans_entity_t *ent = g_lists.draw_list->trans_entities + i;
			drawEntity(ent->entity, ent->render_mode);
		}

	}

	// Draw transparent beams
	gEngine.CL_DrawEFX( g_frametime, true );

	VK_RenderDebugLabelEnd();
}

// FIXME better place?
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

/*
================
CL_AddCustomBeam

Add the beam that encoded as custom entity
================
*/
void CL_AddCustomBeam( cl_entity_t *pEnvBeam )
{
	if( g_lists.draw_list->num_beam_entities >= ARRAYSIZE(g_lists.draw_list->beam_entities) )
	{
		gEngine.Con_Printf( S_ERROR "Too many beams %d!\n", g_lists.draw_list->num_beam_entities );
		return;
	}

	if( pEnvBeam )
	{
		g_lists.draw_list->beam_entities[g_lists.draw_list->num_beam_entities] = pEnvBeam;
		g_lists.draw_list->num_beam_entities++;
	}
}

void CL_DrawBeams( int fTrans, BEAM *active_beams )
{
	BEAM	*pBeam;
	int	i, flags;

	// FIXME VK pglDepthMask( fTrans ? GL_FALSE : GL_TRUE );

	// server beams don't allocate beam chains
	// all params are stored in cl_entity_t
	for( i = 0; i < g_lists.draw_list->num_beam_entities; i++ )
	{
		cl_entity_t *currentbeam = g_lists.draw_list->beam_entities[i];
		flags = currentbeam->curstate.rendermode & 0xF0;

		if( fTrans && FBitSet( flags, FBEAM_SOLID ))
			continue;

		if( !fTrans && !FBitSet( flags, FBEAM_SOLID ))
			continue;

		R_BeamDrawCustomEntity( currentbeam, g_frametime );
		// FIXME VK r_stats.c_view_beams_count++;
	}

	// draw temporary entity beams
	for( pBeam = active_beams; pBeam; pBeam = pBeam->next )
	{
		if( fTrans && FBitSet( pBeam->flags, FBEAM_SOLID ))
			continue;

		if( !fTrans && !FBitSet( pBeam->flags, FBEAM_SOLID ))
			continue;

		R_BeamDraw( pBeam, g_frametime );
	}
}
