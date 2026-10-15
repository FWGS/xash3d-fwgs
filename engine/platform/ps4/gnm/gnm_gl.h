/*
gnm_gl.h - OpenGL ES 2.0 subset over GNM for ref_gles3compat on PS4
Copyright (C) 2026 sergiopoverony

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef GNM_GL_H
#define GNM_GL_H

#include <stdbool.h>

// opens the display and creates the context, returns the screen size;
// flip_front is true by default (-gnmnoflip turns it off): the viewport flips Y, which inverts winding;
// mipmaps: 0 - off, 1 - power of two textures only, 2 - all textures;
// debug: 0 - quiet, 1 - log submits and texture uploads, 2 - also every draw;
// stats: frame timing in the log
bool PS4_GL_Init( int *width, int *height, bool flip_front, int mipmaps, int debug, bool stats );
void PS4_GL_Shutdown( void );
void PS4_GL_SwapBuffers( void );
void *PS4_GL_GetProcAddress( const char *name );

#endif // GNM_GL_H
