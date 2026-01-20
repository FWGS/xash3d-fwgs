/*
vid_common.h - common implementation of platform-specific vid component
Copyright (C) 2025 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef VID_COMMON
#define VID_COMMON

#include "ref_api.h"

typedef struct vidmode_s
{
	const char *desc;
	int width;
	int height;
} vidmode_t;

typedef struct
{
	void     *context; // handle to GL rendering context
	ref_safegl_context_t safe;
	qboolean software;
} glwstate_t;

extern glwstate_t glw_state;

#define VID_MIN_HEIGHT 200
#define VID_MIN_WIDTH  320

extern convar_t vid_fullscreen;
extern convar_t vid_maximized;
extern convar_t window_width;
extern convar_t window_height;
extern convar_t gl_msaa_samples;

void R_SaveVideoMode( int w, int h, int render_w, int render_h, qboolean maximized );
void VID_SetDisplayTransform( int *render_w, int *render_h );
void VID_CheckChanges( void );
const char *VID_GetModeString( int vid_mode );

#endif // VID_COMMON
