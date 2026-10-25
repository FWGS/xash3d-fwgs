/*
gnm_gl.h - OpenGL ES 2.0 subset over GNM for ref_gl on PS4
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

// included by ref_gl code, so nothing from opengnm and no stdbool here
struct gnm_dev_api_s;

// api: the device of the engine, from R_GetWindowHandle( REF_WINDOW_TYPE_GNM )
int PS4_GL_Init( const struct gnm_dev_api_s *api );
// called before the engine presents the frame
void PS4_GL_EndFrame( void );
// called while the device is still open
void PS4_GL_Shutdown( void );
void *PS4_GL_GetProcAddress( const char *name );

#endif // GNM_GL_H
