#include "camera.h"
#include "vk_math.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_brush.h"
#include "vk_light.h"

#include "pm_defs.h"
#include "pmtrace.h"

static const char *renderModeName( int rendermode ) {
	switch (rendermode) {
		case kRenderNormal: return "kRenderNormal";
		case kRenderTransColor: return "kRenderTransColor";
		case kRenderTransTexture: return "kRenderTransTexture";
		case kRenderGlow: return "kRenderGlow";
		case kRenderTransAlpha: return "kRenderTransAlpha";
		case kRenderTransAdd: return "kRenderTransAdd";
		default: return "UNKNOWN";
	}
}

void XVK_CameraDebugPrintCenterEntity( void ) {
	vec3_t vec_end;
	pmtrace_t trace;
	const msurface_t *surf;
	char buf[1024], *p = buf, *end = buf + sizeof(buf);
	const physent_t *physent = NULL;
	const cl_entity_t *ent = NULL;

	VectorMA(g_camera.vieworg, 1e6, g_camera.vforward, vec_end);

	trace = gEngine.CL_TraceLine( g_camera.vieworg, vec_end, PM_NORMAL );
	surf = gEngine.EV_TraceSurface( Q_max(trace.ent, 0), g_camera.vieworg, vec_end );

	if (trace.ent > 0) {
		physent = gEngine.EV_GetPhysent( trace.ent );
	}

	ent = gEngine.GetEntityByIndex( (physent && physent->info > 0) ? physent->info : 0 );

	p += Q_snprintf(p, end - p,
		"^\n"
		"cam.origin: %.03f %.03f %.03f"
		" hit: %.03f %.03f %.03f"
		// TODO cam dir
		"\n",
		g_camera.vieworg[0], g_camera.vieworg[1], g_camera.vieworg[2],
		trace.endpos[0], trace.endpos[1], trace.endpos[2]
	);

	p += Q_snprintf(p, end - p,
		"entity index: %d, name: %s\n",
		(physent && physent->info > 0) ? physent->info : -1,
		(ent && ent->model) ? ent->model->name : "N/A");

	if (ent) {
		p += Q_snprintf(p, end - p,
			"ent type: %d, rendermode: %d(%s)\n",
			ent->curstate.entityType,
			ent->curstate.rendermode,
			renderModeName(ent->curstate.rendermode));
	}

	if (surf && ent && ent->model && ent->model->surfaces) {
		const int surface_index = surf - ent->model->surfaces;
		const texture_t *current_tex = R_TextureAnimation(ent, surf, NULL);
		const int tex_id = current_tex->gl_texturenum;
		const vk_texture_t* const texture = findTexture( tex_id );
		const texture_t *tex = surf->texinfo->texture;

		p += Q_snprintf(p, end - p,
			"surface index: %d; texture: %s(%d)\n",
			surface_index, texture ? texture->name : "NONE", tex_id
		);

		if (tex->anim_total > 0 && tex->anim_next) {
			tex = tex->anim_next;
			p += Q_snprintf(p, end - p,
				"anim textures chain (%d):\n", tex->anim_total);
			for (int i = 0; i < tex->anim_total && tex; ++i) {
				const vk_texture_t *vkt = findTexture(tex->gl_texturenum);
				p += Q_snprintf(p, end - p,
					"%d: %s(%d)%s\n", i, vkt ? vkt->name : "NONE", tex->gl_texturenum, tex == current_tex ? " <-" : "   ");
				tex = tex->anim_next;
			}
		}
	}

	{
		const int cell_raw[3] = {
			floor(trace.endpos[0] / LIGHT_GRID_CELL_SIZE),
			floor(trace.endpos[1] / LIGHT_GRID_CELL_SIZE),
			floor(trace.endpos[2] / LIGHT_GRID_CELL_SIZE),
		};
		const int light_cell[3] = {
			cell_raw[0] - g_lights.map.grid_min_cell[0],
			cell_raw[1] - g_lights.map.grid_min_cell[1],
			cell_raw[2] - g_lights.map.grid_min_cell[2],
		};
		const int cell_index = RT_LightCellIndex( light_cell );

		const vk_lights_cell_t *cell = (cell_index >= 0 && cell_index < MAX_LIGHT_CLUSTERS) ? g_lights.cells + cell_index : NULL;
		p += Q_snprintf(p, end - p,
			"light raw=(%d, %d, %d) cell=(%d, %d, %d) index=%d poly=%d point=%d\n",
			cell_raw[0],
			cell_raw[1],
			cell_raw[2],
			light_cell[0],
			light_cell[1],
			light_cell[2],
			cell_index,
			cell ? cell->num_polygons : -1,
			cell ? cell->num_point_lights : -1);

		if (cell && cell->num_polygons > 0) {
			p += Q_snprintf(p, end - p, "poly:");
			for (int i = 0; i < cell->num_polygons; ++i) {
				p += Q_snprintf(p, end - p, " %d", cell->polygons[i]);
			}
			p += Q_snprintf(p, end - p, "\n");
		}

		if (cell && cell->num_point_lights > 0) {
			p += Q_snprintf(p, end - p, "point:");
			for (int i = 0; i < cell->num_point_lights; ++i) {
				p += Q_snprintf(p, end - p, " %d", cell->point_lights[i]);
			}
			p += Q_snprintf(p, end - p, "\n");
		}
	}

	gEngine.CL_CenterPrint(buf, 0.5f);
}
