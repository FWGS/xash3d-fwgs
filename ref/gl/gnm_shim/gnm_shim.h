/*
gnm_shim.h - fixed function OpenGL over GNM for ref_gl on PS4
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
#ifndef GNM_SHIM_H
#define GNM_SHIM_H

// included by ref_gl code, so nothing from opengnm and no stdbool here
struct gnm_dev_api_s;

// api: the device of the engine, from R_GetWindowHandle( REF_WINDOW_TYPE_GNM )
int GNM_ShimInit( const struct gnm_dev_api_s *api );
// called before the engine presents the frame
void GNM_ShimEndFrame( void );
// called while the device is still open
void GNM_ShimShutdown( void );
void *GNM_ShimGetProcAddress( const char *name );

#endif // GNM_SHIM_H
