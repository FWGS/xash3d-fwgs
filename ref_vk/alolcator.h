#pragma once
#include <stdint.h>

typedef uint32_t alo_size_t;

struct alo_pool_s;

struct alo_pool_s* aloPoolCreate(alo_size_t size, int expected_allocations, alo_size_t min_alignment);
void aloPoolDestroy(struct alo_pool_s*);

typedef struct {
	alo_size_t offset;
	alo_size_t size;

	int index;
} alo_block_t;

alo_block_t aloPoolAllocate(struct alo_pool_s*, alo_size_t size, alo_size_t alignment);
void aloPoolFree(struct alo_pool_s *pool, int index);
