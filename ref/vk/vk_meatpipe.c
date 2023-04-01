#include "vk_meatpipe.h"

#include "vk_pipeline.h"
#include "ray_resources.h"

#include "ray_pass.h"
#include "vk_common.h"

#define MIN(a,b) ((a)<(b)?(a):(b))

#define CHAR4UINT(a,b,c,d) (((d)<<24)|((c)<<16)|((b)<<8)|(a))
static const uint32_t k_meatpipe_magic = CHAR4UINT('M', 'E', 'A', 'T');

typedef struct cursor_t {
	const byte *data;
	int off, size;
	qboolean error;
} cursor_t;

typedef struct load_context_t {
	cursor_t cur;

	int shaders_count;
	VkShaderModule *shaders;

	vk_meatpipe_t meatpipe;
} load_context_t;

typedef struct vk_meatpipe_pass_s {
	ray_pass_p pass;
	int write_from;
	int resource_count;
	int *resource_map;
} vk_meatpipe_pass_t;

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

#define READ_STR_RETURN(retval, out, errmsg, ...) \
	curReadStr(&ctx->cur, out, sizeof(out)); CUR_ERROR_RETURN(retval, errmsg, ##__VA_ARGS__)

#define NO_SHADER 0xffffffff

static struct ray_pass_s *pipelineLoadCompute(load_context_t *ctx, int i, const char *name, const ray_pass_layout_t *layout) {
	const uint32_t shader_comp = READ_U32_RETURN(NULL, "Couldn't read comp shader for %d %s", i, name);

	if (shader_comp >= ctx->shaders_count) {
		gEngine.Con_Printf(S_ERROR "Pipeline %s shader index out of bounds %d (count %d)\n", name, shader_comp, ctx->shaders_count);
		return NULL;
	}

	const ray_pass_create_compute_t rpcc = {
		.debug_name = name,
		.layout = *layout,
		.shader_module = ctx->shaders[shader_comp],
	};

	return RayPassCreateCompute(&rpcc);
}

static struct ray_pass_s *pipelineLoadRT(load_context_t *ctx, int i, const char *name, const ray_pass_layout_t *layout) {
	ray_pass_p ret = NULL;
	ray_pass_create_tracing_t rpct = {
		.debug_name = name,
		.layout = *layout,
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

#define MAX_BINDINGS 32
static qboolean readBindings(load_context_t *ctx, VkDescriptorSetLayoutBinding *bindings, vk_meatpipe_pass_t* pass ) {
	pass->resource_map = NULL;
	int write_from = -1;
	const int count = READ_U32("Coulnd't read bindings count");

	if (count > MAX_BINDINGS) {
		gEngine.Con_Printf(S_ERROR "Too many binding (%d), max: %d\n", count, MAX_BINDINGS);
		goto finalize;
	}

	pass->resource_map = Mem_Malloc(vk_core.pool, sizeof(int) * count);

	for (int i = 0; i < count; ++i) {
		const uint32_t header = READ_U32("Couldn't read header for binding %d", i);
		const uint32_t res_index = READ_U32("Couldn't read res index for binding %d", i);
		const uint32_t stages = READ_U32("Couldn't read stages for binding %d", i);

		if (res_index >= ctx->meatpipe.resources_count) {
			gEngine.Con_Printf(S_ERROR "Resource %d is out of bound %d for binding %d", res_index, ctx->meatpipe.resources_count, i);
			goto finalize;
		}

		vk_meatpipe_resource_t *res = ctx->meatpipe.resources + res_index;

#define BINDING_WRITE_BIT 0x80000000u
#define BINDING_CREATE_BIT 0x40000000u
		const qboolean write = !!(header & BINDING_WRITE_BIT);
		const qboolean create = !!(header & BINDING_CREATE_BIT);
		const uint32_t descriptor_set = (header >> 8) & 0xffu;
		const uint32_t binding = header & 0xffu;

		if (write && write_from < 0)
			write_from = i;

		if (!write && write_from >= 0) {
			gEngine.Con_Printf(S_ERROR "Unsorted non-write binding found at %d(%s), writable started at %d\n",
				i, res->name, write_from);
			goto finalize;
		}

		const char *name = res->name;

		bindings[i] = (VkDescriptorSetLayoutBinding){
			.binding = binding,
			.descriptorType = res->descriptor_type,
			.descriptorCount = res->count,
			.stageFlags = stages,
			.pImmutableSamplers = NULL,
		};

		pass->resource_map[i] = res_index;

		if (write)
			res->flags |= MEATPIPE_RES_WRITE;

		if (create)
			res->flags |= MEATPIPE_RES_CREATE;

		gEngine.Con_Reportf("Binding %d: %s ds=%d b=%d s=%08x res=%d type=%d write=%d\n",
			i, name, descriptor_set, binding, stages, res_index, res->descriptor_type, write);
	}

	pass->write_from = write_from;
	pass->resource_count = count;
	return true;

finalize:
	if (pass->resource_map)
		Mem_Free(pass->resource_map);

	pass->resource_map = NULL;
	return false;
}

static qboolean readAndCreatePass(load_context_t *ctx, int i) {
	VkDescriptorSetLayoutBinding bindings[MAX_BINDINGS];
	ray_pass_layout_t layout = {
		.bindings = bindings,
		.push_constants = {0},
	};

	vk_meatpipe_pass_t *pass = ctx->meatpipe.passes + i;
	memset(pass, 0, sizeof(*pass));

	const uint32_t type = READ_U32("Couldn't read pipeline %d type", i);

	char name[64];
	READ_STR(name, "Couldn't read pipeline %d name", i);

	gEngine.Con_Reportf("%d: loading pipeline %s\n", i, name);

	if (!readBindings(ctx, bindings, pass)) {
		gEngine.Con_Printf(S_ERROR "Couldn't read bindings for pipeline %s\n", name);
		return false;
	}

	layout.bindings_count = pass->resource_count;
	layout.write_from = pass->write_from;

#define PIPELINE_COMPUTE 1
#define PIPELINE_RAYTRACING 2

	switch (type) {
		case PIPELINE_COMPUTE:
			pass->pass = pipelineLoadCompute(ctx, i, name, &layout);
			break;
		case PIPELINE_RAYTRACING:
			pass->pass = pipelineLoadRT(ctx, i, name, &layout);
			break;
		default:
			gEngine.Con_Printf(S_ERROR "Unexpected pipeline type %d\n", type);
	}

	if (pass->pass)
		return true;

finalize:
	if (pass->resource_map)
		Mem_Free(pass->resource_map);
	return false;
}

static qboolean readResources(load_context_t *ctx) {
	ctx->meatpipe.resources_count = READ_U32("Couldn't read resources count");
	ctx->meatpipe.resources = Mem_Malloc(vk_core.pool, sizeof(ctx->meatpipe.resources[0]) * ctx->meatpipe.resources_count);

	for (int i = 0; i < ctx->meatpipe.resources_count; ++i) {
		vk_meatpipe_resource_t *res = ctx->meatpipe.resources + i;
		*res = (vk_meatpipe_resource_t){0};

		READ_STR(res->name, "Couldn't read resource %d name", i);

		res->descriptor_type = READ_U32("Couldn't read resource %d:%s type", i, res->name);
		res->count = READ_U32("Couldn't read resource %d:%s count", i, res->name);

		const qboolean is_image = res->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || res->descriptor_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		if (is_image) {
			res->image_format = READ_U32("Couldn't read image format for res %d:%s", i, res->name);
			res->prev_frame_index_plus_1 = READ_U32("Couldn't read resource %d:%s previous frame index", i, res->name);
		}

		gEngine.Con_Reportf("Resource %d:%s = %08x is_image=%d image_format=%08x count=%d\n",
			i, res->name, res->descriptor_type, is_image, res->image_format, res->count);
	}

	return true;
finalize:
	return false;
}

static qboolean readAndLoadShaders(load_context_t *ctx) {
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

	return true;
finalize:
	return false;
}

vk_meatpipe_t* R_VkMeatpipeCreateFromFile(const char *filename) {
	vk_meatpipe_t *ret = NULL;
	fs_offset_t size = 0;
	byte* const buf = gEngine.fsapi->LoadFile(filename, &size, false);

	if (!buf) {
		gEngine.Con_Printf(S_ERROR "Couldn't read \"%s\"\n", filename);
		return NULL;
	}

	load_context_t context = {
		.cur = { .data = buf, .off = 0, .size = size },
		.shaders_count = 0,
		.shaders = NULL,
		.meatpipe = (vk_meatpipe_t){0},
	};
	load_context_t *ctx = &context;

	{
		const uint32_t magic = READ_U32("Couldn't read magic");

		if (magic != k_meatpipe_magic) {
			gEngine.Con_Printf(S_ERROR "Meatpipe magic invalid for \"%s\": got %08x expected %08x\n", filename, magic, k_meatpipe_magic);
			goto finalize;
		}
	}

	if (!readResources(ctx))
		goto finalize;

	if (!readAndLoadShaders(ctx))
		goto finalize;

	ctx->meatpipe.passes_count = READ_U32("Couldn't read pipelines count");
	ctx->meatpipe.passes = Mem_Malloc(vk_core.pool, sizeof(ctx->meatpipe.passes[0]) * ctx->meatpipe.passes_count);
	for (int i = 0; i < ctx->meatpipe.passes_count; ++i) {
		if (!readAndCreatePass(ctx, i)) {
			for (int j = 0; j < i; ++j) {
				RayPassDestroy(ctx->meatpipe.passes[j].pass);
				Mem_Free(ctx->meatpipe.passes[j].resource_map);
			}
			goto finalize;
		}
	}

	// Loading successful, allocate and fill
	ret = Mem_Malloc(vk_core.pool, sizeof(*ret));
	memcpy(ret, &ctx->meatpipe, sizeof(*ret));
	ctx->meatpipe.resources = NULL;

finalize:
	for (int i = 0; i < ctx->shaders_count; ++i) {
		if (ctx->shaders[i] == VK_NULL_HANDLE)
			break;

		R_VkShaderDestroy(ctx->shaders[i]);
	}

	if (ctx->shaders)
		Mem_Free(ctx->shaders);

	if (ctx->meatpipe.resources)
		Mem_Free(ctx->meatpipe.resources);

	Mem_Free(buf);
	return ret;
}

void R_VkMeatpipeDestroy(vk_meatpipe_t *mp) {
	for (int i = 0; i < mp->passes_count; ++i) {
		vk_meatpipe_pass_t *pass = mp->passes + i;
		RayPassDestroy(pass->pass);
		Mem_Free(pass->resource_map);
	}

	Mem_Free(mp->passes);
	Mem_Free(mp->resources);
	Mem_Free(mp);
}

void R_VkMeatpipePerform(vk_meatpipe_t *mp, struct vk_combuf_s *combuf, vk_meatpipe_perfrom_args_t args) {
	for (int i = 0; i < mp->passes_count; ++i) {
		const vk_meatpipe_pass_t *pass = mp->passes + i;
		RayPassPerform(pass->pass, combuf,
			(ray_pass_perform_args_t){
				.frame_set_slot = args.frame_set_slot,
				.width = args.width,
				.height = args.height,
				.resources = args.resources,
				.resources_map = pass->resource_map,
			}
		);
	}
}
