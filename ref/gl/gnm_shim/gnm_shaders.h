/*
gnm_shaders.h - precompiled shaders of gnm_shim
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
#ifndef GNM_SHADERS_H
#define GNM_SHADERS_H

#include <stddef.h>
#include <stdint.h>

// variant bits of fixed function shaders
#define GNM_FF_COLOR      ( 1 << 0 )
#define GNM_FF_TEXCOORD0  ( 1 << 1 )
#define GNM_FF_TEXCOORD1  ( 1 << 2 )
#define GNM_FF_ALPHA_TEST ( 1 << 3 )
#define GNM_FF_FOG        ( 1 << 4 )

#define GNM_FF_VS_COUNT 8  // attribute bits only
#define GNM_FF_PS_COUNT 32 // attribute and feature bits

typedef struct gnm_shader_blob_s
{
	const uint8_t *data;
	size_t size;
} gnm_shader_blob_t;

extern const gnm_shader_blob_t gnm_shader_clear_vs;
extern const gnm_shader_blob_t gnm_shader_clear_ps;
extern const gnm_shader_blob_t gnm_shader_ff_vs[GNM_FF_VS_COUNT];
extern const gnm_shader_blob_t gnm_shader_ff_ps[GNM_FF_PS_COUNT];

#endif // GNM_SHADERS_H
