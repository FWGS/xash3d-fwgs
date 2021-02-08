#include "vk_scene.h"
#include "vk_brush.h"
#include "vk_studio.h"
#include "vk_lightmap.h"
#include "vk_const.h"
#include "vk_render.h"
#include "vk_math.h"
#include "vk_common.h"
#include "vk_core.h"

#include "com_strings.h"
#include "ref_params.h"
#include "eiface.h"

#include <stdlib.h> // qsort
#include <memory.h>

static struct {
	draw_list_t	draw_stack[MAX_SCENE_STACK];
	int		draw_stack_pos;
	draw_list_t	*draw_list;
} g_lists;

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

	gEngine.Con_Reportf( "R_NewMap\n" );

	VK_ClearLightmap();

	// This is to ensure that we have computed lightstyles properly
	VK_RunLightStyles();

	// TODO should we do something like VK_BrushBeginLoad?
	VK_BrushClear();

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

		if (!VK_LoadBrushModel( m, NULL ))
		{
			gEngine.Con_Printf( S_ERROR "Couldn't load model %s\n", m->name );
		}
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
		/* g_lists.draw_list->num_beam_entities = 0; */
	}

	if( gEngine.drawFuncs->R_ProcessEntData )
		gEngine.drawFuncs->R_ProcessEntData( allocate );
}

void R_ClearScreen( void )
{
	g_lists.draw_list->num_solid_entities = 0;
	g_lists.draw_list->num_trans_entities = 0;
	/* g_lists.draw_list->num_beam_entities = 0; */

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
	/* g_lists.draw_list->num_beam_entities = 0; */
}

// FIXME this is a total garbage. pls avoid adding even more weird local static state
static ref_viewpass_t fixme_rvp;

void FIXME_VK_SceneSetViewPass( const struct ref_viewpass_s *rvp )
{
	fixme_rvp = *rvp;
}

void R_RenderScene( void )
{
	PRINT_NOT_IMPLEMENTED();
}

static float R_GetFarClip( void )
{
	/* FIXME
	if( WORLDMODEL && RI.drawWorld )
		return MOVEVARS->zmax * 1.73f;
	*/
	return 2048.0f;
}

static void R_SetupProjectionMatrix( const ref_viewpass_t *rvp, matrix4x4 m )
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

	yMax = zNear * tan( rvp->fov_y * M_PI_F / 360.0f );
	yMin = -yMax;

	xMax = zNear * tan( rvp->fov_x * M_PI_F / 360.0f );
	xMin = -xMax;

	Matrix4x4_CreateProjection( m, xMax, xMin, yMax, yMin, zNear, zFar );
}

static void R_SetupModelviewMatrix( const ref_viewpass_t* rvp, matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	Matrix4x4_ConcatRotate( m, -rvp->viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -rvp->viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -rvp->viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -rvp->vieworigin[0], -rvp->vieworigin[1], -rvp->vieworigin[2] );
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
static vec3_t R_TransEntityCompare_vieworg; // F
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
		VectorSubtract( R_TransEntityCompare_vieworg, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else dist1 = 1000000000;

	if( ent2->model->type != mod_brush || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( R_TransEntityCompare_vieworg, org, vecLen );
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
static int CL_FxBlend( cl_entity_t *e, const vec3_t vieworg ) // FIXME do R_SetupFrustum: , vec3_t vforward )
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
	/* FIXME
	case kRenderFxHologram:
	case kRenderFxDistort:
		VectorCopy( e->origin, tmp );
		VectorSubtract( tmp, vieworg, tmp );
		dist = DotProduct( tmp, vforward );

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
	*/
	default:
		blend = e->curstate.renderamt;
		break;
	}

	blend = bound( 0, blend, 255 );

	return blend;
}

static void prepareMatrix( const ref_viewpass_t *rvp, matrix4x4 worldview, matrix4x4 projection, matrix4x4 mvp )
{
	matrix4x4 tmp;

	// Vulkan has Y pointing down, and z should end up in (0, 1)
	const matrix4x4 vk_proj_fixup = {
		{1, 0, 0, 0},
		{0, -1, 0, 0},
		{0, 0, .5, 0},
		{0, 0, .5, 1}
	};
	R_SetupProjectionMatrix( rvp, tmp );
	Matrix4x4_Concat( projection, vk_proj_fixup, tmp );

	R_SetupModelviewMatrix( rvp, worldview );
	Matrix4x4_Concat( mvp, projection, worldview);
}

static int drawEntity( cl_entity_t *ent, int render_mode, int ubo_index, const matrix4x4 mvp )
{
	const model_t *mod = ent->model;
	matrix4x4 model, ent_mvp;
	uniform_data_t *ubo = getUniformSlot(ubo_index);
	float alpha;

	if (!mod)
		return 0;

	// handle studiomodels with custom rendermodes on texture
	alpha = render_mode == kRenderNormal ? 1.f : CL_FxBlend( ent, fixme_rvp.vieworigin ) / 255.0f;

	// TODO ref_gl does this earlier, can we too?
	if( alpha <= 0.0f )
		return 0;

	// TODO sort by entity type?
	// TODO other entity types
	if (mod->type != mod_brush )
		return 0;

	switch (render_mode) {
		case kRenderNormal:
			Vector4Set(ubo->color, 1.f, 1.f, 1.f, 1.f);
			break;

		case kRenderTransColor:
			// FIXME also zero out texture? use white texture
			Vector4Set(ubo->color,
					ent->curstate.rendercolor.r / 255.f,
					ent->curstate.rendercolor.g / 255.f,
					ent->curstate.rendercolor.b / 255.f,
					ent->curstate.renderamt / 255.f);
			break;

		case kRenderTransAdd:
			Vector4Set(ubo->color, alpha, alpha, alpha, 1.f);
			break;

		case kRenderTransAlpha:
			Vector4Set(ubo->color, 1.f, 1.f, 1.f, 1.f);
			// TODO Q1compat Vector4Set(e_ubo->color, 1.f, 1.f, 1.f, alpha);
			break;

		default:
			Vector4Set(ubo->color, 1.f, 1.f, 1.f, alpha);
	}

	R_RotateForEntity( model, ent );
	Matrix4x4_Concat( ent_mvp, mvp, model );
	Matrix4x4_ToArrayFloatGL( ent_mvp, (float*)ubo->mvp);

	VK_BrushDrawModel( mod, render_mode, ubo_index );
	return 1;
}

void VK_SceneRender( void )
{
	matrix4x4 worldview, projection, mvp;
	int current_pipeline_index = kRenderNormal;
	int ubo_index = 0;

	if (!VK_BrushRenderBegin())
		return;

	prepareMatrix( &fixme_rvp, worldview, projection, mvp );
	{
		const model_t *world = gEngine.pfnGetModelByIndex( 1 );
		if (world)
		{
			uniform_data_t *ubo = getUniformSlot(ubo_index);
			Matrix4x4_ToArrayFloatGL( mvp, (float*)ubo->mvp );
			Vector4Set(ubo->color, 1.f, 1.f, 1.f, 1.f);
			VK_BrushDrawModel( world, kRenderNormal, ubo_index );
			ubo_index++;
		}
	}

	// Draw opaque entities
	for (int i = 0; i < g_lists.draw_list->num_solid_entities; ++i)
	{
		cl_entity_t *ent = g_lists.draw_list->solid_entities[i];
		ubo_index += drawEntity(ent, kRenderNormal, ubo_index, mvp);
	}

	{
		if (vk_core.debug) {
			VkDebugUtilsLabelEXT label = {
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
				.pLabelName = "transparent",
			};
			vkCmdBeginDebugUtilsLabelEXT(vk_core.cb, &label);
		}

		// sort translucents entities by rendermode and distance
		VectorCopy( fixme_rvp.vieworigin, R_TransEntityCompare_vieworg );
		qsort( g_lists.draw_list->trans_entities, g_lists.draw_list->num_trans_entities, sizeof( vk_trans_entity_t ), R_TransEntityCompare );

		// Draw transparent ents
		for (int i = 0; i < g_lists.draw_list->num_trans_entities; ++i)
		{
			const vk_trans_entity_t *ent = g_lists.draw_list->trans_entities + i;
			ubo_index += drawEntity(ent->entity, ent->render_mode, ubo_index, mvp);
		}

		if (vk_core.debug)
			vkCmdEndDebugUtilsLabelEXT(vk_core.cb);
	}
}
