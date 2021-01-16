#pragma once
#include "vk_core.h"

qboolean createBuffer(vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);
void destroyBuffer(vk_buffer_t *buf);


