#include "vk_common.h"
#include "vk_mapents.h"
#include "vk_core.h" // TODO we need only pool from there, not the entire vulkan garbage
#include "vk_textures.h"

#include "eiface.h" // ARRAYSIZE
#include "xash3d_mathlib.h"

xvk_map_entities_t g_map_entities;

static unsigned parseEntPropWadList(const char* value, string *out, unsigned bit) {
	int dst_left = sizeof(string) - 2; // ; \0
	char *dst = *out;
	*dst = '\0';
	gEngine.Con_Reportf("WADS: %s\n", value);

	for (; *value;) {
		const char *file_begin = value;

		for (; *value && *value != ';'; ++value) {
			if (*value == '\\' || *value == '/')
				file_begin = value + 1;
		}

		{
			const int len = value - file_begin;
			gEngine.Con_Reportf("WAD: %.*s\n", len, file_begin);

			if (len < dst_left) {
				Q_strncpy(dst, file_begin, len + 1);
				dst += len;
				dst[0] = ';';
				dst++;
				dst[0] = '\0';
				dst_left -= len;
			}
		}

		if (*value) value++;
	}

	gEngine.Con_Reportf("wad list: %s\n", *out);
	return bit;
}

static unsigned parseEntPropFloat(const char* value, float *out, unsigned bit) {
	return (1 == sscanf(value, "%f", out)) ? bit : 0;
}

static unsigned parseEntPropInt(const char* value, int *out, unsigned bit) {
	return (1 == sscanf(value, "%d", out)) ? bit : 0;
}

static unsigned parseEntPropString(const char* value, string *out, unsigned bit) {
	const int len = Q_strlen(value);
	if (len >= sizeof(string))
		gEngine.Con_Printf(S_ERROR, "Map entity value '%s' is too long, max length is %d\n",
			value, sizeof(string));
	Q_strncpy(*out, value, sizeof(*out));
	return bit;
}

static unsigned parseEntPropVec3(const char* value, vec3_t *out, unsigned bit) {
	return (3 == sscanf(value, "%f %f %f", &(*out)[0], &(*out)[1], &(*out)[2])) ? bit : 0;
}

static unsigned parseEntPropRgbav(const char* value, vec3_t *out, unsigned bit) {
	float scale = 1.f;
	const int components = sscanf(value, "%f %f %f %f", &(*out)[0], &(*out)[1], &(*out)[2], &scale);
	if (components == 1) {
		(*out)[2] = (*out)[1] = (*out)[0] = (*out)[0];
		return bit;
	} else if (components == 4) {
		scale /= 255.f;
		(*out)[0] *= scale;
		(*out)[1] *= scale;
		(*out)[2] *= scale;
		return bit;
	} else if (components == 3) {
		(*out)[0] *= scale;
		(*out)[1] *= scale;
		(*out)[2] *= scale;
		return bit;
	}

	return 0;
}

static unsigned parseEntPropClassname(const char* value, class_name_e *out, unsigned bit) {
	if (Q_strcmp(value, "light") == 0) {
		*out = Light;
	} else if (Q_strcmp(value, "light_spot") == 0) {
		*out = LightSpot;
	} else if (Q_strcmp(value, "light_environment") == 0) {
		*out = LightEnvironment;
	} else if (Q_strcmp(value, "worldspawn") == 0) {
		*out = Worldspawn;
	} else {
		*out = Ignored;
	}

	return bit;
}

static void weirdGoldsrcLightScaling( vec3_t intensity ) {
	float l1 = Q_max( intensity[0], Q_max( intensity[1], intensity[2] ) );
	l1 = l1 * l1 / 10;
	VectorScale( intensity, l1, intensity );
}

static void parseAngles( const entity_props_t *props, vk_light_entity_t *le) {
	float angle = props->angle;
	VectorSet( le->dir, 0, 0, 0 );

	if (angle == -1) { // UP
		le->dir[0] = le->dir[1] = 0;
		le->dir[2] = 1;
	} else if (angle == -2) { // DOWN
		le->dir[0] = le->dir[1] = 0;
		le->dir[2] = -1;
	} else {
		if (angle == 0) {
			angle = props->angles[1];
		}

		angle *= M_PI / 180.f;

		le->dir[2] = 0;
		le->dir[0] = cosf(angle);
		le->dir[1] = sinf(angle);
	}

	angle = props->pitch ? props->pitch : props->angles[0];

	angle *= M_PI / 180.f;
	le->dir[2] = sinf(angle);
	le->dir[0] *= cosf(angle);
	le->dir[1] *= cosf(angle);
}

static void parseStopDot( const entity_props_t *props, vk_light_entity_t *le) {
	le->stopdot = props->_cone ? props->_cone : 10;
	le->stopdot2 = Q_max(le->stopdot, props->_cone2);

	le->stopdot = cosf(le->stopdot * M_PI / 180.f);
	le->stopdot2 = cosf(le->stopdot2 * M_PI / 180.f);
}

static void addLightEntity( const entity_props_t *props, unsigned have_fields ) {
	const int index = g_map_entities.num_lights;
	vk_light_entity_t *le = g_map_entities.lights + index;
	unsigned expected_fields = 0;

	if (g_map_entities.num_lights == ARRAYSIZE(g_map_entities.lights)) {
		gEngine.Con_Printf(S_ERROR "Too many lights entities in map\n");
		return;
	}

	*le = (vk_light_entity_t){0};

	switch (props->classname) {
		case Light:
			le->type = LightTypePoint;
			expected_fields = Field_origin;
			break;

		case LightSpot:
			if ((have_fields & Field__sky) && props->_sky != 0) {
				le->type = LightTypeEnvironment;
				expected_fields = Field__cone | Field__cone2;
			} else {
				le->type = LightTypeSpot;
				expected_fields = Field_origin | Field__cone | Field__cone2;
			}
			parseAngles(props, le);
			parseStopDot(props, le);
			break;

		case LightEnvironment:
			le->type = LightTypeEnvironment;
			parseAngles(props, le);
			parseStopDot(props, le);

			if (g_map_entities.single_environment_index == NoEnvironmentLights) {
				g_map_entities.single_environment_index = index;
			} else {
				g_map_entities.single_environment_index = MoreThanOneEnvironmentLight;
			}
			break;
	}

	if (have_fields & Field_target)
		Q_strcpy(le->target_entity, props->target);

	if ((have_fields & expected_fields) != expected_fields) {
		gEngine.Con_Printf(S_ERROR "Missing some fields for light entity\n");
		return;
	}

	VectorCopy(props->origin, le->origin);

	if ( (have_fields & Field__light) == 0 )
	{
		// same as qrad
		VectorSet(le->color, 300, 300, 300);
	} else {
		VectorCopy(props->_light, le->color);
	}

	if ( (have_fields & Field_style) != 0) {
		le->style = props->style;
	}

	if (le->type != LightEnvironment) {
		//gEngine.Con_Reportf("Pre scaling: %f %f %f ", values._light[0], values._light[1], values._light[2]);
		weirdGoldsrcLightScaling(le->color);
		//gEngine.Con_Reportf("post scaling: %f %f %f\n", values._light[0], values._light[1], values._light[2]);
	}

	gEngine.Con_Reportf("Added light %d: %s targetname=%s color=(%f %f %f) origin=(%f %f %f) style=%d dir=(%f %f %f) stopdot=(%f %f)\n",
		g_map_entities.num_lights,
		le->type == LightTypeEnvironment ? "environment" : le->type == LightTypeSpot ? "spot" : "point",
		props->targetname,
		le->color[0], le->color[1], le->color[2],
		le->origin[0], le->origin[1], le->origin[2],
		le->style,
		le->dir[0], le->dir[1], le->dir[2],
		le->stopdot, le->stopdot2);

	g_map_entities.num_lights++;
}

static void addTargetEntity( const entity_props_t *props ) {
	xvk_mapent_target_t *target = g_map_entities.targets + g_map_entities.num_targets;

	gEngine.Con_Reportf("Adding target entity %s at (%f, %f, %f)\n",
		props->targetname, props->origin[0], props->origin[1], props->origin[2]);

	if (g_map_entities.num_targets == MAX_MAPENT_TARGETS) {
		gEngine.Con_Printf(S_ERROR "Too many map target entities\n");
		return;
	}

	Q_strcpy(target->targetname, props->targetname);
	VectorCopy(props->origin, target->origin);

	++g_map_entities.num_targets;
}

static void readWorldspawn( const entity_props_t *props ) {
	Q_strcpy(g_map_entities.wadlist, props->wad);
}

static void addPatchSurface( const entity_props_t *props, uint32_t have_fields ) {
	const model_t* const map = gEngine.pfnGetModelByIndex( 1 );
	const int num_surfaces = map->numsurfaces;
	xvk_patch_surface_t *psurf = NULL;
	if (props->_xvk_surface_id < 0 || props->_xvk_surface_id >= num_surfaces) {
		gEngine.Con_Printf(S_ERROR "Incorrect patch for surface_index %d where numsurfaces=%d\n", props->_xvk_surface_id, num_surfaces);
		return;
	}

	if (!g_map_entities.patch.surfaces) {
		g_map_entities.patch.surfaces = Mem_Malloc(vk_core.pool, num_surfaces * sizeof(xvk_patch_surface_t));
		for (int i = 0; i < num_surfaces; ++i) {
			g_map_entities.patch.surfaces[i].flags = Patch_Surface_NoPatch;
			g_map_entities.patch.surfaces[i].tex_id = -1;
			g_map_entities.patch.surfaces[i].tex = NULL;
		}
	}

	psurf = g_map_entities.patch.surfaces + props->_xvk_surface_id;

	if (have_fields & Field__xvk_texture) {
		if (props->_xvk_texture[0] == '\0') {
			gEngine.Con_Reportf("Patch: surface %d removed\n", props->_xvk_surface_id);
			psurf->flags = Patch_Surface_Delete;
			return;
		} else {
			const int tex_id = VK_FindTexture( props->_xvk_texture );
			gEngine.Con_Reportf("Patch for surface %d with texture \"%s\" -> %d\n", props->_xvk_surface_id, props->_xvk_texture, tex_id);
			psurf->tex_id = tex_id;

			// Find texture_t for this index
			for (int i = 0; i < map->numtextures; ++i) {
				const texture_t* const tex = map->textures[i];
				if (tex->gl_texturenum == tex_id) {
					psurf->tex = tex;
					break;
				}
			}

			psurf->flags |= Patch_Surface_Texture;
		}
	}

	if (have_fields & Field__light) {
		VectorCopy(props->_light, psurf->emissive);
		psurf->flags |= Patch_Surface_Emissive;
		gEngine.Con_Reportf("Patch for surface %d: assign emissive %f %f %f\n", props->_xvk_surface_id,
			psurf->emissive[0],
			psurf->emissive[1],
			psurf->emissive[2]
		);
	}
}

static void parseEntities( char *string ) {
	unsigned have_fields = 0;
	entity_props_t values;
	char *pos = string;
	//gEngine.Con_Reportf("ENTITIES: %s\n", pos);
	for (;;) {
		char key[1024];
		char value[1024];

		pos = COM_ParseFile(pos, key, sizeof(key));
		ASSERT(Q_strlen(key) < sizeof(key));
		if (!pos)
			break;

		if (key[0] == '{') {
			have_fields = None;
			values = (entity_props_t){0};
			continue;
		} else if (key[0] == '}') {
			const int target_fields = Field_targetname | Field_origin;
			if ((have_fields & target_fields) == target_fields)
				addTargetEntity( &values );
			switch (values.classname) {
				case Light:
				case LightSpot:
				case LightEnvironment:
					addLightEntity( &values, have_fields );
					break;

				case Worldspawn:
					readWorldspawn( &values );
					break;

				case Unknown:
					if (have_fields & Field__xvk_surface_id) {
						addPatchSurface( &values, have_fields );
					}
					break;
				case Ignored:
					// Skip
					break;
			}
			continue;
		}

		pos = COM_ParseFile(pos, value, sizeof(value));
		ASSERT(Q_strlen(value) < sizeof(value));
		if (!pos)
			break;

#define READ_FIELD(num, type, name, kind) \
		if (Q_strcmp(key, #name) == 0) { \
			const unsigned bit = parseEntProp##kind(value, &values.name, Field_##name); \
			if (bit == 0) { \
				gEngine.Con_Printf( S_ERROR "Error parsing entity property " #name ", invalid value: %s\n", value); \
			} else have_fields |= bit; \
		} else
		ENT_PROP_LIST(READ_FIELD)
		{
			//gEngine.Con_Reportf("Unknown field %s with value %s\n", key, value);
		}
#undef CHECK_FIELD
	}
}

const xvk_mapent_target_t *findTargetByName(const char *name) {
	for (int i = 0; i < g_map_entities.num_targets; ++i) {
		const xvk_mapent_target_t *target = g_map_entities.targets + i;
		if (Q_strcmp(name, target->targetname) == 0)
			return target;
	}

	return NULL;
}

static void orientSpotlights( void ) {
	// Patch spotlight directions based on target entities
	for (int i = 0; i < g_map_entities.num_lights; ++i) {
		vk_light_entity_t *const light = g_map_entities.lights + i;
		const xvk_mapent_target_t *target;

		if (light->type != LightSpot && light->type != LightTypeEnvironment)
			continue;

		if (light->target_entity[0] == '\0')
			continue;

		target = findTargetByName(light->target_entity);
		if (!target) {
			gEngine.Con_Printf(S_ERROR "Couldn't find target entity '%s' for spot light %d\n", light->target_entity, i);
			continue;
		}

		VectorSubtract(target->origin, light->origin, light->dir);
		VectorNormalize(light->dir);

		gEngine.Con_Reportf("Light %d patched direction towards '%s': %f %f %f\n", i, target->targetname,
			light->dir[0], light->dir[1], light->dir[2]);
	}
}

static void parsePatches( const model_t *const map ) {
	char filename[256];
	byte *data;

	if (g_map_entities.patch.surfaces) {
		Mem_Free(g_map_entities.patch.surfaces);
		g_map_entities.patch.surfaces = NULL;
	}

	Q_snprintf(filename, sizeof(filename), "luchiki/%s.patch", map->name);
	gEngine.Con_Reportf("Loading patches from file \"%s\"\n", filename);
	data = gEngine.COM_LoadFile( filename, 0, false );
	if (!data) {
		gEngine.Con_Reportf("No patch file \"%s\"\n", filename);
		return;
	}

	parseEntities( (char*)data );
	Mem_Free(data);
}

void XVK_ParseMapEntities( void ) {
	const model_t* const map = gEngine.pfnGetModelByIndex( 1 );

	ASSERT(map);

	g_map_entities.num_targets = 0;
	g_map_entities.num_lights = 0;
	g_map_entities.single_environment_index = NoEnvironmentLights;

	parseEntities( map->entities );
	parsePatches( map );

	orientSpotlights();
}
