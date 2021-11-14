#include "vk_materials.h"
#include "vk_textures.h"
#include "vk_mapents.h"
#include "vk_const.h"

static struct {
	xvk_material_t materials[MAX_TEXTURES];
} g_materials;

static int findTextureNamedLike( const char *texture_name ) {
	const model_t *map = gEngine.pfnGetModelByIndex( 1 );
	string texname;
	int tex_id;

	// Try bsp texture first
	tex_id = XVK_TextureLookupF("#%s:%s.mip", map->name, texture_name);

	if (!tex_id) {
		const char *wad = g_map_entities.wadlist;
		for (; *wad;) {
			const char *const wad_end = Q_strchr(wad, ';');
			tex_id = XVK_TextureLookupF("%.*s/%s.mip", wad_end - wad, wad, texture_name);
			if (tex_id)
				break;
			wad = wad_end + 1;
		}
	}

	return tex_id ? tex_id : -1;
}
static int loadTextureF( const char *fmt, ... ) {
	int tex_id = 0;
	char buffer[1024];
	va_list argptr;

	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	tex_id = VK_LoadTexture( buffer, NULL, 0, 0);
	gEngine.Con_Reportf("Loading texture %s => %d\n", buffer, tex_id);
	return tex_id ? tex_id : -1;
}

static void loadMaterialsFromFile( const char *filename ) {
	fs_offset_t size;
	const char *const path_begin = filename;
	const char *path_end = Q_strrchr(filename, '/');
	byte *data = gEngine.COM_LoadFile( filename, 0, false );
	char *pos = data;
	xvk_material_t current_material = {
		.base_color = -1,
		.metalness = tglob.blackTexture,
		.roughness = tglob.whiteTexture,
		.normalmap = tglob.grayTexture,
	};
	int current_material_index = -1;

	if ( !data )
		return;

	if ( !path_end )
		path_end = path_begin;
	else
		path_end++;

	for (;;) {
		char key[1024];
		char value[1024];

		pos = COM_ParseFile(pos, key, sizeof(key));
		ASSERT(Q_strlen(key) < sizeof(key));
		if (!pos)
			break;

		if (key[0] == '{') {
			current_material = (xvk_material_t){
				.base_color = -1,
				.metalness = tglob.blackTexture,
				.roughness = tglob.whiteTexture,
				.normalmap = tglob.grayTexture,
			};
			continue;
		}

		if (key[0] == '}') {
			if (current_material_index >= 0 && current_material.base_color >= 0) {
				g_materials.materials[current_material_index] = current_material;
			}
			continue;
		}

		pos = COM_ParseFile(pos, value, sizeof(value));
		if (!pos)
			break;

		if (Q_stricmp(key, "for") == 0) {
			current_material_index = findTextureNamedLike(value);
		} else if (Q_stricmp(key, "basecolor_map") == 0) {
			if ((current_material.base_color = loadTextureF("%.*s%s", path_end - path_begin, path_begin, value)) < 0) {
				gEngine.Con_Printf(S_ERROR "Failed to load basecolor_map texture %s\n", value);
			}
		} else if (Q_stricmp(key, "normal_map") == 0) {
			if ((current_material.normalmap = loadTextureF("%.*s%s", path_end - path_begin, path_begin, value)) < 0) {
				gEngine.Con_Printf(S_ERROR "Failed to load normal_map texture %s\n", value);
				current_material.normalmap = tglob.grayTexture;
			}
		} else if (Q_stricmp(key, "metal_map") == 0) {
			if ((current_material.metalness = loadTextureF("%.*s%s", path_end - path_begin, path_begin, value)) < 0) {
				gEngine.Con_Printf(S_ERROR "Failed to load metal_map texture %s\n", value);
				current_material.metalness = tglob.blackTexture;
			}
		} else if (Q_stricmp(key, "roughness_map") == 0) {
			if ((current_material.roughness = loadTextureF("%.*s%s", path_end - path_begin, path_begin, value)) < 0) {
				gEngine.Con_Printf(S_ERROR "Failed to load roughness_map texture %s\n", value);
				current_material.roughness = tglob.whiteTexture;
			}
		} else {
			gEngine.Con_Printf(S_ERROR "Unknown material key %s\n", key);
		}
	}

	Mem_Free( data );
}

void XVK_ReloadMaterials( void ) {
	for (int i = 0; i < MAX_TEXTURES; ++i) {
		xvk_material_t *const mat = g_materials.materials + i;
		const vk_texture_t *const tex = findTexture( i );

		if (!tex) {
			mat->base_color = -1;
			continue;
		}

		mat->base_color = i;
		mat->metalness = tglob.blackTexture;
		mat->roughness = tglob.whiteTexture;
		mat->normalmap = tglob.grayTexture;
	}

	loadMaterialsFromFile( "pbr/materials.mat" );
	// TODO map-specific
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
