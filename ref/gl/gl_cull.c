/*
gl_cull.c - render culling routines
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gl_local.h"
#include "entity_types.h"

/*
=============================================================

FRUSTUM AND PVS CULLING

=============================================================
*/
/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs )
{
	return GL_FrustumCullBox( &RI.frustum, mins, maxs, 0 );
}

/*
=============
R_CullModel
=============
*/
int R_CullModel( cl_entity_t *e, const vec3_t absmin, const vec3_t absmax )
{
	if( e == gEngfuncs.GetViewModel() )
	{
		if( ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
			return 1;

		if( RP_NORMALPASS() && !ENGINE_GET_PARM( PARM_THIRDPERSON ) && CL_IsViewEntityLocalPlayer())
			return 0;

		return 1;
	}

	if( R_CullBox( absmin, absmax ))
		return 1;

	return 0;
}

/*
=================
R_CullSurface

cull invisible surfaces
=================
*/
int R_CullSurface( msurface_t *surf, gl_frustum_t *frustum, uint clipflags )
{
	cl_entity_t	*e = RI.currententity;

	if( !surf || !surf->texinfo || !surf->texinfo->texture )
		return CULL_OTHER;

	if( r_nocull.value )
		return CULL_VISIBLE;

	// world surfaces can be culled by vis frame too
	if( RI.currententity == gEngfuncs.GetEntityByIndex( 0 ) && surf->visframe != tr.framecount )
		return CULL_VISFRAME;

	// only static ents can be culled by frustum
	if( !R_StaticEntity( e )) frustum = NULL;

	if( !VectorIsNull( surf->plane->normal ))
	{
		float	dist;

		// can use normal.z for world (optimisation)
		if( RI.drawOrtho )
		{
			vec3_t	orthonormal;

			if( e == gEngfuncs.GetEntityByIndex( 0 ) ) orthonormal[2] = surf->plane->normal[2];
			else Matrix4x4_VectorRotate( RI.objectMatrix, surf->plane->normal, orthonormal );
			dist = orthonormal[2];
		}
		else dist = PlaneDiff( tr.modelorg, surf->plane );

		if( glState.faceCull == GL_FRONT )
		{
			if( FBitSet( surf->flags, SURF_PLANEBACK ))
			{
				if( dist >= -BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
			else
			{
				if( dist <= BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
		}
		else if( glState.faceCull == GL_BACK )
		{
			if( FBitSet( surf->flags, SURF_PLANEBACK ))
			{
				if( dist <= BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
			else
			{
				if( dist >= -BACKFACE_EPSILON )
					return CULL_BACKSIDE; // wrong side
			}
		}
	}

	if( frustum && GL_FrustumCullBox( frustum, surf->info->mins, surf->info->maxs, clipflags ))
		return CULL_FRUSTUM;

	return CULL_VISIBLE;
}
