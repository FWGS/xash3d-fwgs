/*
vgl_shim.h - vitaGL custom immediate mode shim
Copyright (C) 2023 fgsfds

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#pragma once

// max verts in a single frame
#define VGL_MAX_VERTS 32768

int VGL_ShimInit( void );
void VGL_ShimInstall( void );
void VGL_ShimShutdown( void );
void VGL_ShimEndFrame( void );
