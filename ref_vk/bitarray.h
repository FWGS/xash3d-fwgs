#pragma once

#include <xash3d_types.h>

typedef struct {
	uint32_t size;
	uint32_t *bits;
} bit_array_t;

bit_array_t bitArrayCreate(uint32_t size);
void bitArrayDestroy(bit_array_t *ba);
void bitArrayClear(bit_array_t *ba);

// Returns true if wasn't set
qboolean bitArrayCheckOrSet(bit_array_t *ba, uint32_t index);
