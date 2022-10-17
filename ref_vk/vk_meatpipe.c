#include "vk_meatpipe.h"

#include "vk_pipeline.h"

#include "ray_pass.h"
#include "vk_common.h"

#define CHAR4UINT(a,b,c,d) (((d)<<24)|((c)<<16)|((b)<<8)|(a))
static const uint32_t k_meatpipe_magic = CHAR4UINT('M', 'E', 'A', 'T');

typedef struct {
	const byte *data;
	int off, size;
	qboolean error;
} cursor_t;

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
	if (cur.error) { \
		gEngine.Con_Printf(S_ERROR "(off=%d left=%d) " errmsg "\n", cur.off, (cur.size - cur.off), ##__VA_ARGS__); \
		goto finalize; \
	}

#define READ_PTR(size, errmsg, ...) \
	curReadPtr(&cur, size); CUR_ERROR(errmsg, ##__VA_ARGS__)

uint32_t curReadU32(cursor_t *cur) {
	const void *src = curReadPtr(cur, sizeof(uint32_t));
	if (!src)
		return 0;

	uint32_t ret;
	memcpy(&ret, src, sizeof(uint32_t));
	return ret;
}

#define READ_U32(errmsg, ...) \
	curReadU32(&cur); CUR_ERROR(errmsg, ##__VA_ARGS__)

qboolean R_VkMeatpipeLoad(vk_meatpipe_t *out, const char *filename) {
	qboolean ret = false;
	fs_offset_t size = 0;
	byte* const buf = gEngine.fsapi->LoadFile(filename, &size, false);

	if (!buf) {
		gEngine.Con_Printf(S_ERROR "Couldn't read \"%s\"\n", filename);
		return false;
	}

	cursor_t cur = { .data = buf, .off = 0, .size = size };

	int shaders_count = 0;
	VkShaderModule *shaders = NULL;
	int pipelines_count = 0;

	{
		const uint32_t magic = READ_U32("Couldn't read magic");

		if (magic != k_meatpipe_magic) {
			gEngine.Con_Printf(S_ERROR "Meatpipe magic invalid for \"%s\": got %08x expected %08x\n", filename, magic, k_meatpipe_magic);
			goto finalize;
		}
	}

	shaders_count = READ_U32("Couldn't read shaders count");
	shaders = Mem_Malloc(vk_core.pool, sizeof(VkShaderModule) * shaders_count);
	for (int i = 0; i < shaders_count; ++i)
		shaders[i] = VK_NULL_HANDLE;

	for (int i = 0; i < shaders_count; ++i) {
		char name[256];
		Q_snprintf(name, sizeof(name), "%s@%d", filename, i); // TODO serialize origin name

		const int size = READ_U32("Couldn't read shader %s size", name);
		const void *src = READ_PTR(size, "Couldn't read shader %s data", name);
		shaders[i] = R_VkShaderLoadFromMem(src, size, name);
		gEngine.Con_Reportf("%d: loaded %s\n", i, name);
	}

	pipelines_count = READ_U32("Couldn't read pipelines count");
	for (int i = 0; i < pipelines_count; ++i) {
		const uint32_t head = READ_U32("Couldn't read pipeline %d head", i);
		const int name_len = READ_U32("Coulnd't read pipeline %d name len", i);
		const char *name_src = READ_PTR(name_len, "Couldn't read pipeline %d name", i);
		char name[64];
#define MIN(a,b) ((a)<(b)?(a):(b))
		const int name_max = MIN(sizeof(name)-1, name_len);
		memcpy(name, name_src, name_max);
		name[name_max] = '\0';
		gEngine.Con_Reportf("%d: loading pipeline %s\n", i, name);

#define PIPELINE_COMPUTE 1
#define PIPELINE_RAYTRACING 2
#define NO_SHADER 0xffffffff

		switch (head) {
			case PIPELINE_COMPUTE:
			{
				const uint32_t shader_comp = READ_U32("Couldn't read comp shader for %d %s", i, name);
				break;
			}

			case PIPELINE_RAYTRACING:
			{
				const uint32_t shader_rgen = READ_U32("Couldn't read rgen shader for %d %s", i, name);
				const int miss_count = READ_U32("Couldn't read miss count for %d %s", i, name);
				for (int j = 0; j < miss_count; ++j) {
					const uint32_t shader_miss = READ_U32("Couldn't read miss shader %d for %d %s", j, i, name);
				}

				const int hit_count = READ_U32("Couldn't read hit count for %d %s", i, name);
				for (int j = 0; j < hit_count; ++j) {
					const uint32_t closest = READ_U32("Couldn't read closest shader %d for %d %s", j, i, name);
					const uint32_t any = READ_U32("Couldn't read any shader %d for %d %s", j, i, name);
				}
				break;
			}
		}
	}

	ret = true;
finalize:
	for (int i = 0; i < shaders_count; ++i) {
		if (shaders[i] != VK_NULL_HANDLE) {
			R_VkShaderDestroy(shaders[i]);
		}
	}

	if (shaders)
		Mem_Free(shaders);

	Mem_Free(buf);
	return ret;
}

void R_VkMeatpipeDestroy(vk_meatpipe_t *mp) {
}
