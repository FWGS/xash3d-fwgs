/*
gl_helper.h - gu helper
Copyright (C) 2021 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef GU_HELPER_H
#define GU_HELPER_H
#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef APIENTRY_LINKAGE
#define APIENTRY_LINKAGE extern
#endif

// GL types wrapping
typedef uint		GLenum;
typedef byte		GLboolean;
typedef uint		GLbitfield;
typedef void		GLvoid;
typedef signed char GLbyte;
typedef short		GLshort;
typedef int			GLint;
typedef byte		GLubyte;
typedef word		GLushort;
typedef uint		GLuint;
typedef int			GLsizei;
typedef float		GLfloat;
typedef float		GLclampf;
typedef double		GLdouble;
typedef double		GLclampd;
typedef int			GLintptrARB;
typedef int			GLsizeiptrARB;
typedef char		GLcharARB;
typedef uint		GLhandleARB;
typedef float		GLmatrix[16];

// GL_Cull wrapping
#define GL_NONE						0
#define GL_FRONT					GU_CW + 1
#define GL_BACK						GU_CCW + 1

// color functions wrapping
#define GUCOLOR4F( r, g, b, a )		GU_COLOR( ( r ), ( g ), ( b ), ( a ) )
#define GUCOLOR4FV( v )				GU_COLOR( ( v )[0], ( v )[1], ( v )[2], ( v )[3] )
#define GUCOLOR4UB( r, g, b, a )	GU_RGBA( ( r ), ( g ), ( b ), ( a ) )
#define GUCOLOR4UBV( v )			GU_RGBA( ( v )[0], ( v )[1], ( v )[2], ( v )[3] )
#define GUCOLOR3F( r, g, b )		GU_COLOR( ( r ), ( g ), ( b ), 1.0f )
#define GUCOLOR3FV( v )				GU_COLOR( ( v )[0], ( v )[1], ( v )[2], 1.0f )
#define GUCOLOR3UB( r, g, b )		GU_RGBA( ( r ), ( g ), ( b ), 255 )
#define GUCOLOR3UBV( v )			GU_RGBA( ( v )[0], ( v )[1], ( v )[2], 255 )

// blend function wrapping
#define GUBLEND1					0xffffffff
#define GUBLEND0					0x00000000

#endif // GU_HELPER_H
