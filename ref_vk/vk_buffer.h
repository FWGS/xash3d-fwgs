#pragma once

#include "vk_core.h"
#include "vk_devmem.h"
#include "alolcator.h"

typedef struct vk_buffer_s {
	vk_devmem_t devmem;
	VkBuffer buffer;

	void *mapped;
	uint32_t size;
} vk_buffer_t;

qboolean VK_BufferCreate(const char *debug_name, vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);
void VK_BufferDestroy(vk_buffer_t *buf);

VkDeviceAddress XVK_BufferGetDeviceAddress(VkBuffer buffer);


typedef struct {
	alo_ring_t ring;
	uint32_t frame_offsets[2];
} r_flipping_buffer_t;

void R_FlippingBuffer_Init(r_flipping_buffer_t *flibuf, uint32_t size);
void R_FlippingBuffer_Clear(r_flipping_buffer_t *flibuf);
uint32_t R_FlippingBuffer_Alloc(r_flipping_buffer_t* flibuf, uint32_t size, uint32_t align);
void R_FlippingBuffer_Flip(r_flipping_buffer_t* flibuf);


typedef struct {
	r_flipping_buffer_t dynamic;
	uint32_t static_size;
	uint32_t static_offset;
} r_debuffer_t;

typedef enum {
	LifetimeStatic, LifetimeDynamic,
} r_lifetime_t;

void R_DEBuffer_Init(r_debuffer_t *debuf, uint32_t static_size, uint32_t dynamic_size);
uint32_t R_DEBuffer_Alloc(r_debuffer_t* debuf, r_lifetime_t lifetime, uint32_t size, uint32_t align);
void R_DEBuffer_Flip(r_debuffer_t* debuf);
