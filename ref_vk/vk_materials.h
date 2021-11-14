#pragma once

typedef struct {
	int base_color;
	int roughness;
	int metalness;
	int normalmap;
} xvk_material_t;

void XVK_ReloadMaterials( void );

xvk_material_t* XVK_GetMaterialForTextureIndex( int tex_index );
