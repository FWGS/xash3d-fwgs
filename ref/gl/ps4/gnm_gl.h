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

#include "gnm_dev.h"

// The functions are exported by ref_gles3compat, the engine finds them by name (platform/ps4/vid_gnm.c):
// the engine owns the display and the device (gnm_dev.h), the renderer only draws.

// api: the device of the engine, already open;
// flip_front is true by default (-gnmnoflip turns it off): the viewport flips Y, which inverts winding;
// mipmaps: 0 - off, 1 - power of two textures only, 2 - all textures
bool PS4_GL_Init( const gnm_dev_api_t *api, bool flip_front, int mipmaps );
// called before the engine presents the frame
void PS4_GL_EndFrame( void );
// called while the device is still open
void PS4_GL_Shutdown( void );
void *PS4_GL_GetProcAddress( const char *name );

#endif // GNM_GL_H
