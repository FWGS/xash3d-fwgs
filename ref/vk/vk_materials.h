#pragma once

#include "xash3d_types.h"

typedef struct {
	int tex_base_color;
	int tex_roughness;
	int tex_metalness;
	int tex_normalmap;

	vec3_t base_color;
	float roughness;
	float metalness;

	qboolean set;
} xvk_material_t;

void XVK_ReloadMaterials( void );

xvk_material_t* XVK_GetMaterialForTextureIndex( int tex_index );
