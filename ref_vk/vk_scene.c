#include "vk_scene.h"
#include "vk_brush.h"
#include "vk_lightmap.h"
#include "vk_const.h"
#include "vk_common.h"

#include "com_strings.h"
#include "ref_params.h"
#include "eiface.h"

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

// tell the renderer what new map is started
void R_NewMap( void )
{
	const int num_models = gEngine.EngineGetParm( PARM_NUMMODELS, 0 );

	gEngine.Con_Reportf( "R_NewMap\n" );

	VK_ClearLightmap();

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

	if( !clent || !clent->model )
		return false; // if set to invisible, skip

	if( FBitSet( clent->curstate.effects, EF_NODRAW ))
		return false; // done

	/* TODO
#define R_ModelOpaque( rm )	( rm == kRenderNormal )
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

	/* FIXME
#define R_OpaqueEntity(ent) (R_GetEntityRenderMode( ent ) == kRenderNormal)
	if( R_OpaqueEntity( clent ))
	{
		// opaque
		if( tr.draw_list->num_solid_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->solid_entities[tr.draw_list->num_solid_entities] = clent;
		tr.draw_list->num_solid_entities++;
	}
	else
	{
		// translucent
		if( tr.draw_list->num_trans_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->trans_entities[tr.draw_list->num_trans_entities] = clent;
		tr.draw_list->num_trans_entities++;
	}
	*/

	// FIXME for now consider all of them opaque
	if( g_lists.draw_list->num_solid_entities >= ARRAYSIZE(g_lists.draw_list->solid_entities) )
		return false;

	g_lists.draw_list->solid_entities[g_lists.draw_list->num_solid_entities] = clent;
	g_lists.draw_list->num_solid_entities++;

	return true;
}

void R_ProcessEntData( qboolean allocate )
{
	if( !allocate )
	{
		g_lists.draw_list->num_solid_entities = 0;
		/* g_lists.draw_list->num_trans_entities = 0; */
		/* g_lists.draw_list->num_beam_entities = 0; */
	}

	if( gEngine.drawFuncs->R_ProcessEntData )
		gEngine.drawFuncs->R_ProcessEntData( allocate );
}

void R_ClearScreen( void )
{
	g_lists.draw_list->num_solid_entities = 0;
	/* g_lists.draw_list->num_trans_entities = 0; */
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
	/* g_lists.draw_list->num_trans_entities = 0; */
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
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

void VK_SceneRender( void )
{
	VK_BrushRender( &fixme_rvp, g_lists.draw_list );
}
