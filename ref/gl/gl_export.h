#pragma once
/*
gl_export.h - opengl definition
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef GL_EXPORT_H
#define GL_EXPORT_H
#ifndef APIENTRY
#define APIENTRY
#endif

#include "imdraw.h"
#include "ref_api.h"
#include <unordered_map>
#include <sky_client/sky_client.h>

int EXPORT GetRefAPI(int version, ref_interface_t* funcs, ref_api_t* engfuncs, ref_globals_t* globals);


#endif//GL_EXPORT_H
