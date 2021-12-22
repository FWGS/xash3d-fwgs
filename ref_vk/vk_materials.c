#include "vk_materials.h"
#include "vk_textures.h"
#include "vk_mapents.h"
#include "vk_const.h"

#include <stdio.h>

static const xvk_material_t k_default_material = {
		.tex_base_color = -1,
		.tex_metalness = 0,
		.tex_roughness = 0,
		.tex_normalmap = 0,

		.metalness = 0.f,
		.roughness = 1.f,
		.base_color = { 1.f, 1.f, 1.f },

		.set = false,
};

static struct {
	xvk_material_t materials[MAX_TEXTURES];
} g_materials;

static int loadTexture( const char *filename, qboolean force_reload ) {
	const int tex_id = force_reload ? XVK_LoadTextureReplace( filename, NULL, 0, 0 ) : VK_LoadTexture( filename, NULL, 0, 0 );
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
		.tex_normalmap = 0,
	};
	int current_material_index = -1;
	qboolean force_reload = false;
	qboolean create = false;

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
			current_material = k_default_material;
			current_material_index = -1;
			force_reload = false;
			create = false;
			continue;
		}

		if (key[0] == '}') {
			if (current_material_index < 0)
				continue;

			// If there's no explicit basecolor_map value, use the "for" target texture
			if (current_material.tex_base_color == -1)
				current_material.tex_base_color = current_material_index;

			gEngine.Con_Reportf("Creating%s material for texture %s(%d)\n", create?" new":"",
				findTexture(current_material_index)->name, current_material_index);

			g_materials.materials[current_material_index] = current_material;
			g_materials.materials[current_material_index].set = true;
			continue;
		}

		pos = COM_ParseFile(pos, value, sizeof(value));
		if (!pos)
			break;

		if (Q_stricmp(key, "for") == 0) {
			current_material_index = XVK_FindTextureNamedLike(value);
			create = false;
		} else if (Q_stricmp(key, "new") == 0) {
			current_material_index = XVK_CreateDummyTexture(value);
			create = true;
		} else if (Q_stricmp(key, "force_reload") == 0) {
			force_reload = Q_atoi(value) != 0;
		} else {
			char texture_path[256];
			int *tex_id_dest = NULL;
			if (Q_stricmp(key, "basecolor_map") == 0) {
				tex_id_dest = &current_material.tex_base_color;
			} else if (Q_stricmp(key, "normal_map") == 0) {
				tex_id_dest = &current_material.tex_normalmap;
			} else if (Q_stricmp(key, "metal_map") == 0) {
				tex_id_dest = &current_material.tex_metalness;
			} else if (Q_stricmp(key, "roughness_map") == 0) {
				tex_id_dest = &current_material.tex_roughness;
			} else if (Q_stricmp(key, "roughness") == 0) {
				sscanf(value, "%f", &current_material.roughness);
			} else if (Q_stricmp(key, "metalness") == 0) {
				sscanf(value, "%f", &current_material.metalness);
			} else if (Q_stricmp(key, "base_color") == 0) {
				sscanf(value, "%f %f %f", &current_material.base_color[0], &current_material.base_color[1], &current_material.base_color[2]);
			} else {
				gEngine.Con_Printf(S_ERROR "Unknown material key %s\n", key);
				continue;
			}

			if (value[0] == '/') {
				// Path relative to valve/pbr dir
				Q_snprintf(texture_path, sizeof(texture_path), "pbr%s", value);
			} else {
				// Path relative to current material.mat file
				Q_snprintf(texture_path, sizeof(texture_path), "%.*s%s", (int)(path_end - path_begin), path_begin, value);
			}

			if (tex_id_dest) {
				const int tex_id = loadTexture(texture_path, force_reload);
				if (tex_id < 0) {
					gEngine.Con_Printf(S_ERROR "Failed to load texture \"%s\" for key \"%s\"\n", value, key);
					continue;
				}

				*tex_id_dest = tex_id;
			}
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
		*mat = k_default_material;

		if (tex)
			mat->tex_base_color = i;
	}

	loadMaterialsFromFile( "pbr/materials.mat" );
	loadMaterialsFromFile( "pbr/models/materials.mat" );
	loadMaterialsFromFile( "pbr/sprites/materials.mat" );

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
