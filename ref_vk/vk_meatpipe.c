#include "vk_meatpipe.h"

#include "vk_pipeline.h"

#include "ray_pass.h"
#include "vk_common.h"

#define MIN(a,b) ((a)<(b)?(a):(b))

#define CHAR4UINT(a,b,c,d) (((d)<<24)|((c)<<16)|((b)<<8)|(a))
static const uint32_t k_meatpipe_magic = CHAR4UINT('M', 'E', 'A', 'T');

typedef struct {
	const byte *data;
	int off, size;
	qboolean error;
} cursor_t;

typedef struct {
	cursor_t cur;
	int shaders_count;
	VkShaderModule *shaders;
} load_context_t;

const void* curReadPtr(cursor_t *cur, int size) {
	const int left = cur->size - cur->off;
	if (left < size) {
		cur->error = true;
		return NULL;
	}

	const void* const ret = cur->data + cur->off;
	cur->off += size;
	return ret;
}

#define CUR_ERROR(errmsg, ...) \
	if (ctx->cur.error) { \
		gEngine.Con_Printf(S_ERROR "(off=%d left=%d) " errmsg "\n", ctx->cur.off, (ctx->cur.size - ctx->cur.off), ##__VA_ARGS__); \
		goto finalize; \
	}

#define CUR_ERROR_RETURN(retval, errmsg, ...) \
	if (ctx->cur.error) { \
		gEngine.Con_Printf(S_ERROR "(off=%d left=%d) " errmsg "\n", ctx->cur.off, (ctx->cur.size - ctx->cur.off), ##__VA_ARGS__); \
		return retval; \
	}

#define READ_PTR(size, errmsg, ...) \
	curReadPtr(&ctx->cur, size); CUR_ERROR(errmsg, ##__VA_ARGS__)

uint32_t curReadU32(cursor_t *cur) {
	const void *src = curReadPtr(cur, sizeof(uint32_t));
	if (!src)
		return 0;

	uint32_t ret;
	memcpy(&ret, src, sizeof(uint32_t));
	return ret;
}

#define READ_U32(errmsg, ...) \
	curReadU32(&ctx->cur); CUR_ERROR(errmsg, ##__VA_ARGS__)

#define READ_U32_RETURN(retval, errmsg, ...) \
	curReadU32(&ctx->cur); CUR_ERROR_RETURN(retval, errmsg, ##__VA_ARGS__)

int curReadStr(cursor_t *cur, char* out, int out_size) {
	const int len = curReadU32(cur);
	if (cur->error)
		return -1;

	const char *src = curReadPtr(cur, len);
	if (cur->error)
		return -1;

	const int max = MIN(out_size, len); \
	memcpy(out, src, max); \
	out[max] = '\0';
	return len;
}

#define READ_STR(out, errmsg, ...) \
	curReadStr(&ctx->cur, out, sizeof(out)); CUR_ERROR(errmsg, ##__VA_ARGS__)

#define NO_SHADER 0xffffffff

extern const ray_pass_layout_t ray_primary_layout_fixme;
extern const ray_pass_layout_t light_direct_poly_layout_fixme;
extern const ray_pass_layout_t light_direct_point_layout_fixme;
extern const ray_pass_layout_t denoiser_layout_fixme;

static ray_pass_layout_t FIXME_getLayoutFor(const char *name) {
	if (strcmp(name, "primary_ray") == 0)
		return ray_primary_layout_fixme;
	if (strcmp(name, "light_direct_poly") == 0)
		return light_direct_poly_layout_fixme;
	if (strcmp(name, "light_direct_point") == 0)
		return light_direct_point_layout_fixme;
	if (strcmp(name, "denoiser") == 0)
		return denoiser_layout_fixme;

	gEngine.Host_Error("Unexpected pass name %s", name);
	return (ray_pass_layout_t){0};
}

static struct ray_pass_s *pipelineLoadCompute(load_context_t *ctx, int i, const char *name) {
	const uint32_t shader_comp = READ_U32_RETURN(NULL, "Couldn't read comp shader for %d %s", i, name);

	if (shader_comp >= ctx->shaders_count) {
		gEngine.Con_Printf(S_ERROR "Pipeline %s shader index out of bounds %d (count %d)\n", name, shader_comp, ctx->shaders_count);
		return NULL;
	}

	const ray_pass_create_compute_t rpcc = {
		.debug_name = name,
		.layout = FIXME_getLayoutFor(name),
		.shader_module = ctx->shaders[shader_comp],
	};

	return RayPassCreateCompute(&rpcc);
}

static struct ray_pass_s *pipelineLoadRT(load_context_t *ctx, int i, const char *name) {
	ray_pass_p ret = NULL;
	ray_pass_create_tracing_t rpct = {
		.debug_name = name,
		.layout = FIXME_getLayoutFor(name),
	};

	// FIXME bounds check shader indices

	const uint32_t shader_rgen = READ_U32("Couldn't read rgen shader for %d %s", i, name);
	rpct.raygen_module = ctx->shaders[shader_rgen];

	rpct.miss_count = READ_U32("Couldn't read miss count for %d %s", i, name);
	if (rpct.miss_count) {
		rpct.miss_module = Mem_Malloc(vk_core.pool, sizeof(VkShaderModule) * rpct.miss_count);
		for (int j = 0; j < rpct.miss_count; ++j) {
			const uint32_t shader_miss = READ_U32("Couldn't read miss shader %d for %d %s", j, i, name);
			rpct.miss_module[j] = ctx->shaders[shader_miss];
		}
	}

	rpct.hit_count = READ_U32("Couldn't read hit count for %d %s", i, name);
	if (rpct.hit_count) {
		ray_pass_hit_group_t *hit = Mem_Malloc(vk_core.pool, sizeof(rpct.hit[0]) * rpct.hit_count);
		rpct.hit = hit;
		for (int j = 0; j < rpct.hit_count; ++j) {
			const uint32_t closest = READ_U32("Couldn't read closest shader %d for %d %s", j, i, name);
			const uint32_t any = READ_U32("Couldn't read any shader %d for %d %s", j, i, name);

			hit[j] = (ray_pass_hit_group_t){
				.closest_module = (closest == NO_SHADER) ? VK_NULL_HANDLE : ctx->shaders[closest],
				.any_module = (any == NO_SHADER) ? VK_NULL_HANDLE : ctx->shaders[any],
			};
		}
	}

	ret = RayPassCreateTracing(&rpct);

finalize:
	if (rpct.hit)
		Mem_Free((void*)rpct.hit);

	if (rpct.miss_module)
		Mem_Free(rpct.miss_module);

	return ret;
}

static struct ray_pass_s *pipelineLoad(load_context_t *ctx, int i) {
	const uint32_t head = READ_U32("Couldn't read pipeline %d head", i);

	char name[64];
	READ_STR(name, "Couldn't read pipeline %d name", i);

	gEngine.Con_Reportf("%d: loading pipeline %s\n", i, name);

#define PIPELINE_COMPUTE 1
#define PIPELINE_RAYTRACING 2

	switch (head) {
		case PIPELINE_COMPUTE:
			return pipelineLoadCompute(ctx, i, name);
		case PIPELINE_RAYTRACING:
			return pipelineLoadRT(ctx, i, name);
		default:
			gEngine.Con_Printf(S_ERROR "Unexpected pipeline type %d\n", head);
			return NULL;
	}

finalize:
	return NULL;
}

qboolean R_VkMeatpipeLoad(vk_meatpipe_t *out, const char *filename) {
	qboolean ret = false;
	fs_offset_t size = 0;
	byte* const buf = gEngine.fsapi->LoadFile(filename, &size, false);

	if (!buf) {
		gEngine.Con_Printf(S_ERROR "Couldn't read \"%s\"\n", filename);
		return false;
	}

	load_context_t context = {
		.cur = { .data = buf, .off = 0, .size = size },
		.shaders_count = 0,
		.shaders = NULL,
	};
	load_context_t *ctx = &context;

	out->passes_count = 0;

	{
		const uint32_t magic = READ_U32("Couldn't read magic");

		if (magic != k_meatpipe_magic) {
			gEngine.Con_Printf(S_ERROR "Meatpipe magic invalid for \"%s\": got %08x expected %08x\n", filename, magic, k_meatpipe_magic);
			goto finalize;
		}
	}

	ctx->shaders_count = READ_U32("Couldn't read shaders count");
	ctx->shaders = Mem_Malloc(vk_core.pool, sizeof(VkShaderModule) * ctx->shaders_count);
	for (int i = 0; i < ctx->shaders_count; ++i) {
		ctx->shaders[i] = VK_NULL_HANDLE;

		char name[64];
		READ_STR(name, "Couldn't read shader %d name", i);

		const int size = READ_U32("Couldn't read shader %s size", name);
		const void *src = READ_PTR(size, "Couldn't read shader %s data", name);

		if (VK_NULL_HANDLE == (ctx->shaders[i] = R_VkShaderLoadFromMem(src, size, name))) {
			gEngine.Con_Printf(S_ERROR "Failed to load shader %d:%s\n", i, name);
			goto finalize;
		}

		gEngine.Con_Reportf("%d: Shader loaded %s\n", i, name);
	}

	out->passes_count = READ_U32("Couldn't read pipelines count");
	out->passes = Mem_Malloc(vk_core.pool, sizeof(out->passes[0]) * out->passes_count);
	for (int i = 0; i < out->passes_count; ++i) {
		if (!(out->passes[i] = pipelineLoad(ctx, i)))
			goto finalize;
	}

	ret = true;
finalize:
	if (!ret) {
		R_VkMeatpipeDestroy(out);
	}

	for (int i = 0; i < ctx->shaders_count; ++i) {
		if (ctx->shaders[i] == VK_NULL_HANDLE)
			break;

		R_VkShaderDestroy(ctx->shaders[i]);
	}

	if (ctx->shaders)
		Mem_Free(ctx->shaders);

	Mem_Free(buf);
	return ret;
}

void R_VkMeatpipeDestroy(vk_meatpipe_t *mp) {
	for (int i = 0; i < mp->passes_count; ++i) {
		if (!mp->passes[i])
			break;

		RayPassDestroy(mp->passes[i]);
	}

	mp->passes_count = 0;
}

void R_VkMeatpipePerform(vk_meatpipe_t *mp, VkCommandBuffer cmdbuf, int frame_set_slot, struct vk_ray_resources_s *res) {
	for (int i = 0; i < mp->passes_count; ++i) {
		RayPassPerform(cmdbuf, frame_set_slot, mp->passes[i], res);
	}
}
