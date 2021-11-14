#include "vk_materials.h"
#include "vk_textures.h"
#include "vk_const.h"

static struct {
	xvk_material_t materials[MAX_TEXTURES];
} g_materials;

void XVK_ReloadMaterials( void ) {
	for (int i = 0; i < MAX_TEXTURES; ++i) {
		xvk_material_t *const mat = g_materials.materials + i;
		const vk_texture_t *const tex = findTexture( i );

		if (tex) {
			mat->base_color = i;
			mat->metalness = tglob.blackTexture;
			mat->roughness = tglob.whiteTexture;
			mat->normalmap = tglob.blackTexture;
		} else {
			mat->base_color = -1;
		}
	}
}

xvk_material_t* XVK_GetMaterialForTextureIndex( int tex_index ) {
	xvk_material_t *mat = NULL;
	ASSERT(tex_index >= 0);
	ASSERT(tex_index < MAX_TEXTURES);

	mat = g_materials.materials + tex_index;
	if (mat->base_color >= 0)
		return mat;

	return NULL;
}
