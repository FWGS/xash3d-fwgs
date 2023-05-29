/*
gl_dbghulls.c - loading & handling world and brushmodels
Copyright (C) 2016 Uncle Mike

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
#include "mod_local.h"

#define list_entry( ptr, type, member ) \
	((type *)((char *)(ptr) - (size_t)(&((type *)0)->member)))

// iterate over each entry in the list
#define list_for_each_entry( pos, head, member )			\
	for( pos = list_entry( (head)->next, winding_t, member );	\
	     &pos->member != (head);				\
	     pos = list_entry( pos->member.next, winding_t, member ))

// REFTODO: rewrite in triapi
void R_DrawWorldHull( void )
{
	hull_model_t	*hull = &WORLD->hull_models[0];
	winding_t		*poly;
	int		i;

	if( FBitSet( r_showhull->flags, FCVAR_CHANGED ))
	{
		int val = bound( 0, (int)r_showhull->value, 3 );
		if( val ) gEngfuncs.Mod_CreatePolygonsForHull( val );
		ClearBits( r_showhull->flags, FCVAR_CHANGED );
	}

	if( !r_showhull->value )
		return;

	pglDisable( GL_TEXTURE_2D );

	list_for_each_entry( poly, &hull->polys, chain )
	{
		srand((unsigned int)poly);
		pglColor3f( rand() % 256 / 255.0, rand() % 256 / 255.0, rand() % 256 / 255.0 );
		pglBegin( GL_POLYGON );
		for( i = 0; i < poly->numpoints; i++ )
			pglVertex3fv( poly->p[i] );
		pglEnd();
	}
	pglEnable( GL_TEXTURE_2D );
}

void R_DrawModelHull( void )
{
	hull_model_t	*hull;
	winding_t		*poly;
	int		i;

	if( !r_showhull->value )
		return;

	if( !RI.currentmodel || RI.currentmodel->name[0] != '*' )
		return;

	i = atoi( RI.currentmodel->name + 1 );
	if( i < 1 || i >= WORLD->num_hull_models )
		return;

	hull = &WORLD->hull_models[i];

	pglPolygonOffset( 1.0f, 2.0 );
	pglEnable( GL_POLYGON_OFFSET_FILL );
	pglDisable( GL_TEXTURE_2D );
	list_for_each_entry( poly, &hull->polys, chain )
	{
		srand((unsigned int)poly);
		pglColor3f( rand() % 256 / 255.0, rand() % 256 / 255.0, rand() % 256 / 255.0 );
		pglBegin( GL_POLYGON );
		for( i = 0; i < poly->numpoints; i++ )
			pglVertex3fv( poly->p[i] );
		pglEnd();
	}
	pglEnable( GL_TEXTURE_2D );
	pglDisable( GL_POLYGON_OFFSET_FILL );
}
