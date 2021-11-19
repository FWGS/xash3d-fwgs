#pragma once

#include "xash3d_types.h"

typedef struct {
	int base_color;
	int roughness;
	int metalness;
	int normalmap;
	qboolean set;
} xvk_material_t;

void XVK_ReloadMaterials( void );

xvk_material_t* XVK_GetMaterialForTextureIndex( int tex_index );
