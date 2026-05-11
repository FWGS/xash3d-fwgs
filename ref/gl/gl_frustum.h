/*
gl_frustum.cpp - frustum test implementation
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

#ifndef GL_FRUSTUM_H
#define GL_FRUSTUM_H

// don't change this order
#define FRUSTUM_LEFT	0
#define FRUSTUM_RIGHT	1
#define FRUSTUM_BOTTOM	2
#define FRUSTUM_TOP		3
#define FRUSTUM_FAR		4
#define FRUSTUM_NEAR	5
#define FRUSTUM_PLANES	6

typedef struct gl_frustum_s
{
	mplane_t		planes[FRUSTUM_PLANES];
	unsigned int 	clipFlags;
} gl_frustum_t;

void GL_FrustumInitProj( gl_frustum_t *out, float flZNear, float flZFar, float flFovX, float flFovY );
void GL_FrustumInitOrtho( gl_frustum_t *out, float xLeft, float xRight, float yTop, float yBottom, float flZNear, float flZFar );

// cull methods
qboolean GL_FrustumCullBox( const gl_frustum_t *out, const vec3_t mins, const vec3_t maxs, int userClipFlags );
qboolean GL_FrustumCullSphere( const gl_frustum_t *out, const vec3_t centre, float radius, int userClipFlags );

#endif//GL_FRUSTUM_H
