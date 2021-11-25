#include "vk_materials.h"
#include "vk_textures.h"
#include "vk_mapents.h"
#include "vk_const.h"

#include <stdio.h>

static struct {
	xvk_material_t materials[MAX_TEXTURES];
} g_materials;

static int findTextureNamedLike( const char *texture_name ) {
	const model_t *map = gEngine.pfnGetModelByIndex( 1 );
	string texname;

	// Try texture name as-is first
	int tex_id = XVK_TextureLookupF("%s", texture_name);

	// Try bsp name
	if (!tex_id)
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

static int loadTexture( const char *filename ) {
	const int tex_id = VK_LoadTexture( filename, NULL, 0, 0);
	gEngine.Con_Reportf("Loading texture %s => %d\n", filename, tex_id);
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
		.normalmap = 0,
	};
	int current_material_index = -1;

	gEngine.Con_Reportf("Loading materials from %s\n", filename);

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
				.normalmap = 0,
			};
			continue;
		}

		if (key[0] == '}') {
			if (current_material_index >= 0) {
				if (current_material.base_color == -1)
					current_material.base_color = current_material_index;
				g_materials.materials[current_material_index] = current_material;
				g_materials.materials[current_material_index].set = true;
			}
			continue;
		}

		pos = COM_ParseFile(pos, value, sizeof(value));
		if (!pos)
			break;

		if (Q_stricmp(key, "for") == 0) {
			current_material_index = findTextureNamedLike(value);
		} else {
			char texture_path[256];
			int *tex_id_dest;
			int tex_id = loadTexture(texture_path);
			if (Q_stricmp(key, "basecolor_map") == 0) {
				tex_id_dest = &current_material.base_color;
			} else if (Q_stricmp(key, "normal_map") == 0) {
				tex_id_dest = &current_material.normalmap;
			} else if (Q_stricmp(key, "metal_map") == 0) {
				tex_id_dest = &current_material.metalness;
			} else if (Q_stricmp(key, "roughness_map") == 0) {
				tex_id_dest = &current_material.roughness;
			} else {
				gEngine.Con_Printf(S_ERROR "Unknown material key %s\n", key);
				continue;
			}

			if (value[0] == '/') {
				// Path relative to valve/pbr dir
				Q_snprintf(texture_path, sizeof(texture_path), "pbr%s", value);
			} else {
				// Path relative to current material.mat file
				Q_snprintf(texture_path, sizeof(texture_path), "%.*s%s", path_end - path_begin, path_begin, value);
			}

			tex_id = loadTexture(texture_path);
			if (tex_id < 0) {
				gEngine.Con_Printf(S_ERROR "Failed to load texture \"%s\" for key \"%s\"\n", value, key);
				continue;
			}

			*tex_id_dest = tex_id;
		}
	}

	Mem_Free( data );
}

static void loadMaterialsFromFileF( const char *fmt, ... ) {
	char buffer[256];
	va_list argptr;

	va_start( argptr, fmt );
	vsnprintf( buffer, sizeof buffer, fmt, argptr );
	va_end( argptr );

	loadMaterialsFromFile( buffer );
}

void XVK_ReloadMaterials( void ) {
	for (int i = 0; i < MAX_TEXTURES; ++i) {
		xvk_material_t *const mat = g_materials.materials + i;
		const vk_texture_t *const tex = findTexture( i );

		if (!tex) {
			mat->base_color = -1;
			break;
		}

		mat->base_color = i;
		mat->metalness = tglob.blackTexture;
		mat->roughness = tglob.whiteTexture;
		mat->normalmap = 0;
		mat->set = false;
	}

	loadMaterialsFromFile( "pbr/materials.mat" );
	loadMaterialsFromFile( "pbr/models/materials.mat" );

	{
		const char *wad = g_map_entities.wadlist;
		for (; *wad;) {
			const char *const wad_end = Q_strchr(wad, ';');
			loadMaterialsFromFileF("pbr/%.*s/materials.mat", wad_end - wad, wad);
			wad = wad_end + 1;
		}
	}

	{
		const model_t *map = gEngine.pfnGetModelByIndex( 1 );
		loadMaterialsFromFileF("pbr/%s/materials.mat", map->name);
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
