#include "bitarray.h"

#include "vk_core.h"

bit_array_t bitArrayCreate(uint32_t size) {
	size = (size + 31) / 32;
	bit_array_t ret = {
		.size = size,
		.bits = Mem_Malloc(vk_core.pool, size * sizeof(uint32_t))
	};
	bitArrayClear(&ret);
	return ret;
}

void bitArrayDestroy(bit_array_t *ba) {
	if (ba->bits)
		Mem_Free(ba->bits);
	ba->bits = NULL;
	ba->size = 0;
}

void bitArrayClear(bit_array_t *ba) {
	memset(ba->bits, 0, ba->size * sizeof(uint32_t));
}

qboolean bitArrayCheckOrSet(bit_array_t *ba, uint32_t index) {
	const uint32_t offset = index / 32;
	ASSERT(offset < ba->size);

	uint32_t* bits = ba->bits + offset;

	const uint32_t bit = 1u << (index % 32);
	if ((*bits) & bit)
		return false;

	(*bits) |= bit;
	return true;
}
