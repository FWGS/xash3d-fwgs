#include "vk_descriptor.h"

#include "eiface.h" // ARRAYSIZE

descriptor_pool_t vk_desc;

qboolean VK_DescriptorInit( void )
{
	int max_desc_sets = 0;

	VkDescriptorPoolSize dps[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_TEXTURES,
		}, {
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = ARRAYSIZE(vk_desc.ubo_sets),
		/*
		}, {
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
#if RTX
		}, {
			.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1,
#endif
		*/
		},
	};
	VkDescriptorPoolCreateInfo dpci = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pPoolSizes = dps,
		.poolSizeCount = ARRAYSIZE(dps),
	};

	for (int i = 0; i < ARRAYSIZE(dps); ++i)
		max_desc_sets += dps[i].descriptorCount;

	dpci.maxSets = max_desc_sets;

	XVK_CHECK(vkCreateDescriptorPool(vk_core.device, &dpci, NULL, &vk_desc.pool));

	{
		const int num_sets = MAX_TEXTURES;
		// ... TODO find better place for this; this should be per-pipeline/shader
		VkDescriptorSetLayoutBinding bindings[] = { {
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = &vk_core.default_sampler,
		}};
		VkDescriptorSetLayoutCreateInfo dslci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = ARRAYSIZE(bindings),
			.pBindings = bindings,
		};
		VkDescriptorSetLayout* tmp_layouts = Mem_Malloc(vk_core.pool, sizeof(VkDescriptorSetLayout) * num_sets);
		VkDescriptorSetAllocateInfo dsai = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = vk_desc.pool,
			.descriptorSetCount = num_sets,
			.pSetLayouts = tmp_layouts,
		};
		XVK_CHECK(vkCreateDescriptorSetLayout(vk_core.device, &dslci, NULL, &vk_desc.one_texture_layout));
		for (int i = 0; i < num_sets; ++i)
			tmp_layouts[i] = vk_desc.one_texture_layout;

		XVK_CHECK(vkAllocateDescriptorSets(vk_core.device, &dsai, vk_desc.sets));

		Mem_Free(tmp_layouts);
	}

	{
		const int num_sets = ARRAYSIZE(vk_desc.ubo_sets);
		// ... TODO find better place for this; this should be per-pipeline/shader
		VkDescriptorSetLayoutBinding bindings[] = { {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 1,
				// TODO we use these sets for both vertex-only and fragment-only bindings; improve
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		}};
		VkDescriptorSetLayoutCreateInfo dslci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = ARRAYSIZE(bindings),
			.pBindings = bindings,
		};
		VkDescriptorSetLayout* tmp_layouts = Mem_Malloc(vk_core.pool, sizeof(VkDescriptorSetLayout) * num_sets);
		VkDescriptorSetAllocateInfo dsai = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = vk_desc.pool,
			.descriptorSetCount = num_sets,
			.pSetLayouts = tmp_layouts,
		};
		XVK_CHECK(vkCreateDescriptorSetLayout(vk_core.device, &dslci, NULL, &vk_desc.one_uniform_buffer_layout));
		for (int i = 0; i < num_sets; ++i)
				tmp_layouts[i] = vk_desc.one_uniform_buffer_layout;

		XVK_CHECK(vkAllocateDescriptorSets(vk_core.device, &dsai, vk_desc.ubo_sets));

		Mem_Free(tmp_layouts);
	}

	return true;
}

void VK_DescriptorShutdown( void )
{
	vkDestroyDescriptorPool(vk_core.device, vk_desc.pool, NULL);
	vkDestroyDescriptorSetLayout(vk_core.device, vk_desc.one_texture_layout, NULL);
	vkDestroyDescriptorSetLayout(vk_core.device, vk_desc.one_uniform_buffer_layout, NULL);
}
