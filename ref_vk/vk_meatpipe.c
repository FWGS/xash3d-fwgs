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
		gEngine.Con_Printf(S_ERROR "(off=%d left=%d) " errmsg "\n", #__VA_ARGS__); \
		goto finalize; \
	}

#define READ_PTR(size, errmsg, ...) \
	curReadPtr(&cur, size); CUR_ERROR(errmsg, #__VA_ARGS__)

uint32_t curReadU32(cursor_t *cur) {
	const void *src = curReadPtr(cur, sizeof(uint32_t));
	if (!src)
		return 0;

	uint32_t ret;
	memcpy(&ret, src, sizeof(uint32_t));
	return ret;
}

#define READ_U32(errmsg, ...) \
	curReadU32(&cur); CUR_ERROR(errmsg, #__VA_ARGS__)

qboolean R_VkMeatpipeLoad(vk_meatpipe_t *out, const char *filename) {
	qboolean ret = false;
	fs_offset_t size = 0;
	byte* const buf = gEngine.fsapi->LoadFile(filename, &size, false);

	if (!buf) {
		gEngine.Con_Printf(S_ERROR "Couldn't read \"%s\"\n", filename);
		return false;
	}

	cursor_t cur = { .data = buf, .off = 0, .size = size };

	const uint32_t magic = READ_U32("Couldn't read magic");

	if (magic != k_meatpipe_magic) {
		gEngine.Con_Printf(S_ERROR "Meatpipe magic invalid for \"%s\": got %08x expected %08x\n", filename, magic, k_meatpipe_magic);
		goto finalize;
	}

	const int shaders_count = READ_U32("Couldn't read shaders count");
	VkShaderModule *shaders = Mem_Malloc(vk_core.pool, sizeof(VkShaderModule) * shaders_count);
	for (int i = 0; i < shaders_count; ++i)
		shaders[i] = VK_NULL_HANDLE;

	for (int i = 0; i < shaders_count; ++i) {
		char name[256];
		Q_snprintf(name, sizeof(name), "%s@%d", filename, i); // TODO serialize origin name
		const int size = READ_U32("Couldn't read shader %s size", name);
		const void *src = READ_PTR(size, "Couldn't read shader %s data", name);
		shaders[i] = R_VkShaderLoadFromMem(src, size, name);
		gEngine.Con_Reportf("%d: loaded %s into %p\n", i, name, shaders[i]);
	}

	ret = true;
finalize:
	for (int i = 0; i < shaders_count; ++i) {
		if (shaders[i] != VK_NULL_HANDLE) {
			R_VkShaderDestroy(shaders[i]);
		}
	}
	Mem_Free(buf);
	return ret;
}

void R_VkMeatpipeDestroy(vk_meatpipe_t *mp) {
}
