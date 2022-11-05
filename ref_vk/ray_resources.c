#include "ray_resources.h"
#include "vk_core.h"

#include <stdlib.h>

#define MAX_BARRIERS 16

void RayResourcesFill(VkCommandBuffer cmdbuf, ray_resources_fill_t fill) {
	VkImageMemoryBarrier image_barriers[MAX_BARRIERS];
	int image_barriers_count = 0;
	VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_NONE_KHR;

	for (int i = 0; i < fill.count; ++i) {
		const qboolean write = fill.indices[i] < 0;
		const int index = abs(fill.indices[i]) - 1;
		ray_resource_t *const res = fill.resources->resources + index;

		ASSERT(index >= 0);
		ASSERT(index < RayResource__COUNT);

		if (res->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			if (write) {
				// No reads are happening
				ASSERT(res->read.pipelines == 0);

				res->write = (ray_resource_state_t) {
					.access_mask = VK_ACCESS_SHADER_WRITE_BIT,
					.image_layout = VK_IMAGE_LAYOUT_GENERAL,
					.pipelines = fill.dest_pipeline,
				};

				image_barriers[image_barriers_count++] = (VkImageMemoryBarrier) {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.image = res->image->image,
					.srcAccessMask = 0,
					.dstAccessMask = res->write.access_mask,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = res->write.image_layout,
					.subresourceRange = (VkImageSubresourceRange) {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					},
				};

			} else {
				// Write happened
				ASSERT(res->write.pipelines != 0);


				// No barrier was issued
				if (!(res->read.pipelines & fill.dest_pipeline)) {
					res->read.access_mask = VK_ACCESS_SHADER_READ_BIT;
					res->read.pipelines |= fill.dest_pipeline;
					res->read.image_layout = VK_IMAGE_LAYOUT_GENERAL;

					src_stage_mask |= res->write.pipelines;

					image_barriers[image_barriers_count++] = (VkImageMemoryBarrier) {
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
						.image = res->image->image,
						.srcAccessMask = res->write.access_mask,
						.dstAccessMask = res->read.access_mask,
						.oldLayout = res->write.image_layout,
						.newLayout = res->read.image_layout,
						.subresourceRange = (VkImageSubresourceRange) {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1,
						},
					};
				}
			}
			fill.out_values[i].image = (VkDescriptorImageInfo) {
				.imageLayout = write ? res->write.image_layout : res->read.image_layout,
				.imageView = res->image->view,
				.sampler = VK_NULL_HANDLE,
			};
		} else {
			fill.out_values[i] = res->value;
		}
	}

	if (image_barriers_count) {
		if (!src_stage_mask)
			src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		vkCmdPipelineBarrier(cmdbuf,
			src_stage_mask,
			fill.dest_pipeline,
			0, 0, NULL, 0, NULL, image_barriers_count, image_barriers);
	}
}

#define FIXME_DESC(name_, semantic_, count_) \
	{ .name = name_, \
	  .desc.semantic = (RayResource_##semantic_ + 1), \
	  .desc.count = count_, \
	}

static const struct {
	const char *name;
	ray_resource_binding_desc_fixme_t desc;
} fixme_descs[] = {
	// External
	FIXME_DESC("ubo", ubo, 1),
	FIXME_DESC("tlas", tlas, 1),
	FIXME_DESC("kusochki", kusochki, 1),
	FIXME_DESC("indices", indices, 1),
	FIXME_DESC("vertices", vertices, 1),
	FIXME_DESC("textures", all_textures, MAX_TEXTURES),
	FIXME_DESC("skybox", skybox, 1),
	FIXME_DESC("lights", lights, 1),
	FIXME_DESC("light_clusters", light_clusters, 1),
	FIXME_DESC("light_grid", light_clusters, 1),
	FIXME_DESC("dest", denoised, 1),

	// Internal, temporary
	FIXME_DESC("base_color_a", base_color_a, 1),
	FIXME_DESC("position_t", position_t, 1),
	FIXME_DESC("normals_gs", normals_gs, 1),
	FIXME_DESC("material_rmxx", material_rmxx, 1),
	FIXME_DESC("emissive", emissive, 1),
	FIXME_DESC("light_poly_diffuse", light_poly_diffuse, 1),
	FIXME_DESC("light_poly_specular", light_poly_specular, 1),
	FIXME_DESC("light_point_diffuse", light_point_diffuse, 1),
	FIXME_DESC("light_point_specular", light_point_specular, 1),
};

const ray_resource_binding_desc_fixme_t *RayResouceGetBindingForName_FIXME(const char *name) {
	for (int i = 0; i < COUNTOF(fixme_descs); ++i) {
		if (strcmp(name, fixme_descs[i].name) == 0)
			return &fixme_descs[i].desc;
	}
	return NULL;
}
