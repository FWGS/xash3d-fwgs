#include "vk_render.h"

#include "vk_core.h"
#include "vk_buffer.h"
#include "vk_const.h"
#include "vk_common.h"

#include "eiface.h"
#include "xash3d_mathlib.h"

static struct {
	vk_buffer_t buffer;
	uint32_t buffer_free_offset;

	vk_buffer_t uniform_buffer;
	uint32_t uniform_unit_size;

	struct {
		int align_holes_size;
	} stat;
} g_render;

static struct {
	VkPipeline current_pipeline;
} g_render_state;

uniform_data_t *VK_RenderGetUniformSlot(int index)
{
	ASSERT(index >= 0);
	ASSERT(index < MAX_UNIFORM_SLOTS);
	return (uniform_data_t*)(((uint8_t*)g_render.uniform_buffer.mapped) + (g_render.uniform_unit_size * index));
}

qboolean VK_RenderInit( void )
{
	// TODO Better estimates
	const uint32_t vertex_buffer_size = MAX_BUFFER_VERTICES * sizeof(float) * (3 + 3 + 2 + 2);
	const uint32_t index_buffer_size = MAX_BUFFER_INDICES * sizeof(uint16_t);
	const uint32_t ubo_align = Q_max(4, vk_core.physical_device.properties.limits.minUniformBufferOffsetAlignment);

	g_render.uniform_unit_size = ((sizeof(uniform_data_t) + ubo_align - 1) / ubo_align) * ubo_align;

	// TODO device memory and friends (e.g. handle mobile memory ...)

	if (!createBuffer(&g_render.buffer, vertex_buffer_size + index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createBuffer(&g_render.uniform_buffer, g_render.uniform_unit_size * MAX_UNIFORM_SLOTS, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	{
		VkDescriptorBufferInfo dbi = {
			.buffer = g_render.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(uniform_data_t),
		};
		VkWriteDescriptorSet wds[] = { {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &dbi,
			.dstSet = vk_core.descriptor_pool.ubo_sets[0], // FIXME
		}};
		vkUpdateDescriptorSets(vk_core.device, ARRAYSIZE(wds), wds, 0, NULL);
	}

	return true;
}

void VK_RenderShutdown( void )
{
	destroyBuffer( &g_render.buffer );
	destroyBuffer( &g_render.uniform_buffer );
}

void VK_RenderBindBuffers( void )
{
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(vk_core.cb, 0, 1, &g_render.buffer.buffer, &offset);
	vkCmdBindIndexBuffer(vk_core.cb, g_render.buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
}

void VK_RenderBindUniformBufferWithIndex( VkPipelineLayout pipeline_layout, int index )
{
	const uint32_t dynamic_offset[] = { g_render.uniform_unit_size * index };

	vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, vk_core.descriptor_pool.ubo_sets, ARRAYSIZE(dynamic_offset), dynamic_offset);
}

vk_buffer_alloc_t VK_RenderBufferAlloc( uint32_t unit_size, uint32_t count )
{
	const uint32_t offset = ALIGN_UP(g_render.buffer_free_offset, unit_size);
	const uint32_t alloc_size = unit_size * count;
	vk_buffer_alloc_t ret = {0};
	if (offset + alloc_size > g_render.buffer.size) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate %u bytes aligned at %u from buffer; only %u are left",
				alloc_size, unit_size, g_render.buffer.size - offset);
		return (vk_buffer_alloc_t){0};
	}

	ret.buffer_offset_in_units = offset / unit_size;
	ret.ptr = g_render.buffer.mapped + offset;

	g_render.stat.align_holes_size += offset - g_render.buffer_free_offset;
	g_render.buffer_free_offset = offset + alloc_size;
	return ret;
}

void VK_RenderBufferClearAll( void )
{
	g_render.buffer_free_offset = 0;
	g_render.stat.align_holes_size = 0;
}

void VK_RenderBufferPrintStats( void )
{
	gEngine.Con_Reportf("Buffer usage: %uKiB of (%uKiB); holes: %u bytes\n",
		g_render.buffer_free_offset / 1024,
		g_render.buffer.size / 1024,
		g_render.stat.align_holes_size);
}
