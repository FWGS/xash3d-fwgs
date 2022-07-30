#include "vk_scene.h"
#include "vk_brush.h"
#include "vk_staging.h"
#include "vk_studio.h"
#include "vk_lightmap.h"
#include "vk_const.h"
#include "vk_render.h"
#include "vk_geometry.h"
#include "vk_math.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_sprite.h"
#include "vk_beams.h"
#include "vk_light.h"
#include "vk_rtx.h"
#include "vk_textures.h"
#include "vk_cvar.h"
#include "vk_materials.h"
#include "camera.h"
#include "vk_mapents.h"
#include "profiler.h"

#include "com_strings.h"
#include "ref_params.h"
#include "eiface.h"
#include "pm_movevars.h"

#include <stdlib.h> // qsort
#include <memory.h>

#define PROFILER_SCOPES(X) \
	X(scene_render, "VK_SceneRender"); \
	X(draw_viewmodel, "draw viewmodel"); \
	X(draw_worldbrush, "draw worldbrush"); \
	X(draw_opaques, "draw opaque entities"); \
	X(draw_opaque_beams, "draw opaque beams"); \
	X(draw_translucent, "draw translucent entities"); \
	X(draw_transparent_beams, "draw transparent beams"); \

#define SCOPE_DECLARE(scope, name) APROF_SCOPE_DECLARE(scope)
PROFILER_SCOPES(SCOPE_DECLARE)
#undef SCOPE_DECLARE

typedef struct vk_trans_entity_s {
	struct cl_entity_s *entity;
	int render_mode;
} vk_trans_entity_t;

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

static void reloadPatches( void ) {
	// Must re-parse map entities to initialize initial values before patching
	XVK_ParseMapEntities();
	// Wadlist doesn't change, so no need to reload materials here
	XVK_ParseMapPatches();

	// Assumes that the map brush model has been loaded

	// Patching does disturb light sources, reinitialize

	// FIXME loading patches will be broken now ;_;
	// ?? VK_LightsLoadMapStaticLights();
}

static void reloadMaterials( void ) {
	// Materials do affect patching, as new materials can be referenced in patch data
	// So we must do the full sequencce
	XVK_ParseMapEntities();
	XVK_ReloadMaterials();
	XVK_ParseMapPatches();

	// Assumes that the map has been loaded

	// Might have loaded new patch data, need to reload lighting data just in case
	// FIXME loading patches will be broken now ;_;
	// ??? VK_LightsLoadMapStaticLights();
}

void VK_SceneInit( void )
{
	PROFILER_SCOPES(APROF_SCOPE_INIT);

	g_lists.draw_list = g_lists.draw_stack;
	g_lists.draw_stack_pos = 0;
	if (vk_core.rtx) {
		gEngine.Cmd_AddCommand("vk_rtx_reload_materials", reloadMaterials, "Reload PBR materials");
		gEngine.Cmd_AddCommand("vk_rtx_reload_patches", reloadPatches, "Reload patches (does not update surface deletion)");
	}
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
void R_NewMap( void ) {
	const int num_models = gEngine.EngineGetParm( PARM_NUMMODELS, 0 );
	const model_t *const map = gEngine.pfnGetModelByIndex( 1 );

	// Existence of cache.data for the world means that we've already have loaded this map
	// and this R_NewMap call is from within loading of a saved game.
	const qboolean is_save_load = !!gEngine.pfnGetModelByIndex( 1 )->cache.data;

	gEngine.Con_Reportf( "R_NewMap, loading save: %d\n", is_save_load );

	// Skip clearing already loaded data if the map hasn't changed.
	if (is_save_load)
		return;

	// TODO should we do something like VK_BrushBeginLoad?
	VK_BrushStatsClear();

	R_GeometryBuffer_MapClear();

	VK_ClearLightmap();

	// This is to ensure that we have computed lightstyles properly
	VK_RunLightStyles();

	XVK_SetupSky( gEngine.pfnGetMoveVars()->skyName );

	if (vk_core.rtx)
		VK_RayNewMap();

	// Load light entities and patch data prior to loading map brush model
	XVK_ParseMapEntities();

	// Load PBR materials (depends on wadlist from parsed map entities)
	XVK_ReloadMaterials();

	// Parse patch data
	// Depends on loaded materials. Must preceed loading brush models.
	XVK_ParseMapPatches();

	// Need parsed map entities, and also should happen before brush model loading
	RT_LightsNewMapBegin(map);

	// RTX map loading requires command buffer for building blases
	if (vk_core.rtx)
	{
		//ASSERT(!"Not implemented");
		const VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		XVK_CHECK(vkBeginCommandBuffer(vk_core.upload_pool.buffers[0], &beginfo));
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

		if (!VK_BrushModelLoad( vk_core.upload_pool.buffers[0], m, i == 0 ))
		{
			gEngine.Con_Printf( S_ERROR "Couldn't load model %s\n", m->name );
		}
	}

	// Load static map lights
	// Reads surfaces from loaded brush models (must happen after all brushes are loaded)
	RT_LightsNewMapEnd(map);

	if (!vk_core.rtx) {
		// FIXME this is a workaround for uploading staging for non-rtx mode. In rtx mode things get naturally uploaded deep in VK_BrushModelLoad.
		// FIXME there shouldn't be this difference. Ideally, rtx would only continue with also building BLASes, but uploading part should be the same.
		R_VkStagingFlushSync();
	} else {
		R_VKStagingMarkEmpty_FIXME();
	}

	if (vk_core.rtx)
	{
		//ASSERT(!"Not implemented");
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &vk_core.upload_pool.buffers[0],
		};

		XVK_CHECK(vkEndCommandBuffer(vk_core.upload_pool.buffers[0]));
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
		XVK_CHECK(vkQueueWaitIdle(vk_core.queue));
	}

	// TODO should we do something like VK_BrushEndLoad?
	VK_UploadLightmap();
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
			VK_BrushModelDraw( ent, render_mode, model );
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

void VK_SceneRender( const ref_viewpass_t *rvp ) {
	APROF_SCOPE_BEGIN_EARLY(scene_render);
	const cl_entity_t* const local_player = gEngine.GetLocalPlayer();

	g_frametime = /*FIXME VK RP_NORMALPASS( )) ? */
	gpGlobals->time - gpGlobals->oldtime
	/* FIXME VK : 0.f */;

	R_SetupCamera( rvp );

	VK_RenderStateSetMatrixProjection( g_camera.projectionMatrix, g_camera.fov_y ); // FIXME why is this in degrees, not in radians? * M_PI_F / 360.0f );
	VK_RenderStateSetMatrixView( g_camera.modelviewMatrix );
	VK_RenderStateSetMatrixModel( matrix4x4_identity );

	VK_RenderDebugLabelBegin( "opaque" );

	// Draw view model
	{
		APROF_SCOPE_BEGIN(draw_viewmodel);
		VK_RenderStateSetColor( 1.f, 1.f, 1.f, 1.f );
		R_RunViewmodelEvents();
		R_DrawViewModel();
		APROF_SCOPE_END(draw_viewmodel);
	}

	// Draw world brush
	{
		APROF_SCOPE_BEGIN(draw_worldbrush);
		cl_entity_t *world = gEngine.GetEntityByIndex( 0 );
		if( world && world->model )
		{
			//VK_LightsBakePVL( 0 /* FIXME frame number */);

			VK_RenderStateSetColor( 1.f, 1.f, 1.f, 1.f);
			VK_BrushModelDraw( world, kRenderNormal, NULL );
		}
		APROF_SCOPE_END(draw_worldbrush);
	}

	{
		// Draw flashlight for local player
		if( FBitSet( local_player->curstate.effects, EF_DIMLIGHT )) {
			RT_LightAddFlashlight(local_player, true);
		}
	}

	APROF_SCOPE_BEGIN(draw_opaques);
	// Draw opaque entities
	for (int i = 0; i < g_lists.draw_list->num_solid_entities; ++i)
	{
		cl_entity_t *ent = g_lists.draw_list->solid_entities[i];
		drawEntity(ent, kRenderNormal);

		// Draw flashlight for other players
		if( FBitSet( ent->curstate.effects, EF_DIMLIGHT ) && ent != local_player) {
			RT_LightAddFlashlight(ent, false);
		}
	}
	APROF_SCOPE_END(draw_opaques);

	// Draw opaque beams
	APROF_SCOPE_BEGIN(draw_opaque_beams);
	gEngine.CL_DrawEFX( g_frametime, false );
	APROF_SCOPE_END(draw_opaque_beams);

	VK_RenderDebugLabelEnd();

	VK_RenderDebugLabelBegin( "tranparent" );

	{
		APROF_SCOPE_BEGIN(draw_translucent);
		// sort translucents entities by rendermode and distance
		qsort( g_lists.draw_list->trans_entities, g_lists.draw_list->num_trans_entities, sizeof( vk_trans_entity_t ), R_TransEntityCompare );

		// Draw transparent ents
		for (int i = 0; i < g_lists.draw_list->num_trans_entities; ++i)
		{
			const vk_trans_entity_t *ent = g_lists.draw_list->trans_entities + i;
			drawEntity(ent->entity, ent->render_mode);
		}
		APROF_SCOPE_END(draw_translucent);
	}

	// Draw transparent beams
	APROF_SCOPE_BEGIN(draw_transparent_beams);
	gEngine.CL_DrawEFX( g_frametime, true );
	APROF_SCOPE_END(draw_transparent_beams);

	VK_RenderDebugLabelEnd();

	if (ui_infotool->value > 0)
		XVK_CameraDebugPrintCenterEntity();

	APROF_SCOPE_END(scene_render);
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
