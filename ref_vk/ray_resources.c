#include "ray_resources.h"
#include "vk_core.h"

#include "shaders/ray_interop.h" // FIXME temp for type validation

#include <stdlib.h>

#define MAX_BARRIERS 16
#define MAX_RESOURCES 32
#define MAX_NAME 32

typedef struct vk_named_resource_t {
	char name[MAX_NAME];
	int count;
	ray_resource_desc_t desc_fixme;
} vk_named_resource_t;

static struct {
	vk_named_resource_t named[MAX_RESOURCES];
	ray_resource_t resources[RayResource__COUNT];
} g_resources;

static int findSlot(const char* name) {
	// Find the exact match if exists
	// There might be gaps, so we need to check everything
	for (int i = 0; i < MAX_RESOURCES; ++i) {
		if (strcmp(g_resources.named[i].name, name) == 0)
			return i;
	}

	// Find first free slot
	for (int i = 0; i < MAX_RESOURCES; ++i) {
		if (!g_resources.named[i].name[0])
			return i;
	}

	return -1;
}

qboolean R_VkResourceSetExternal(const char* name, VkDescriptorType type, vk_descriptor_value_t value, int count, ray_resource_desc_t desc, const xvk_image_t *image) {
	if (strlen(name) >= MAX_NAME)
		return false;

	const int index = findSlot(name);
	if (index < 0)
		return false;

	vk_named_resource_t *const r = g_resources.named + index;
	ray_resource_t *const rr = g_resources.resources + index;

	if (!r->name[0]) {
		strncpy(r->name, name, sizeof(r->name));
		rr->type = type;
		r->count = count;
		r->desc_fixme = desc;
	} else {
		ASSERT(rr->type == type);
		ASSERT(r->count == count);
		ASSERT(r->desc_fixme.type == desc.type);
		ASSERT(r->desc_fixme.image_format == desc.image_format);
	}

	rr->value = value;
	rr->image = image;
	memset(&rr->read, 0, sizeof(rr->read));
	memset(&rr->write, 0, sizeof(rr->write));

	return true;
}

void RayResourcesFill(VkCommandBuffer cmdbuf, ray_resources_fill_t fill) {
	VkImageMemoryBarrier image_barriers[MAX_BARRIERS];
	int image_barriers_count = 0;
	VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_NONE_KHR;

	for (int i = 0; i < fill.count; ++i) {
		const qboolean write = fill.indices[i] < 0;
		const int index = abs(fill.indices[i]) - 1;
		ray_resource_t *const res = g_resources.resources + index;

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

#if 0
static const struct {
	ray_resource_binding_desc_fixme_t desc;
} fixme_descs[] = {

#define FIXME_DESC_BUFFER(name_, semantic_, count_) \
	{ \
		.type = ResourceBuffer, \
		.desc.semantic = (RayResource_##semantic_ + 1), \
		.desc.count = count_, \
	}

	// External
	FIXME_DESC_BUFFER("ubo", ubo, 1),
	FIXME_DESC_BUFFER("tlas", tlas, 1),
	FIXME_DESC_BUFFER("kusochki", kusochki, 1),
	FIXME_DESC_BUFFER("indices", indices, 1),
	FIXME_DESC_BUFFER("vertices", vertices, 1),
	FIXME_DESC_BUFFER("lights", lights, 1),
	FIXME_DESC_BUFFER("light_clusters", light_clusters, 1),
	FIXME_DESC_BUFFER("light_grid", light_clusters, 1),

#define FIXME_DESC_IMAGE(name_, semantic_, image_format_, count_) \
	{ \
		.type = ResourceImage, \
		.image_format = image_format_, \
		.desc.semantic = (RayResource_##semantic_ + 1), \
		.desc.count = count_, \
	}

	FIXME_DESC_IMAGE("textures", all_textures, VK_FORMAT_UNDEFINED, MAX_TEXTURES),
	FIXME_DESC_IMAGE("skybox", skybox, VK_FORMAT_UNDEFINED, 1),
	FIXME_DESC_IMAGE("dest", denoised,  VK_FORMAT_UNDEFINED,1),

	// Internal, temporary
#define DECLARE_IMAGE(_, name_, format_) \
	{ \
		.type = ResourceImage, \
		.image_format = format_, \
		.desc.semantic = (RayResource_##name_ + 1), \
		.desc.count = 1, \
	},
#define rgba8 VK_FORMAT_R8G8B8A8_UNORM
#define rgba32f VK_FORMAT_R32G32B32A32_SFLOAT
#define rgba16f VK_FORMAT_R16G16B16A16_SFLOAT
	RAY_PRIMARY_OUTPUTS(DECLARE_IMAGE)
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(DECLARE_IMAGE)
	RAY_LIGHT_DIRECT_POINT_OUTPUTS(DECLARE_IMAGE)
};
#endif

/*
ray_resource_binding_desc_fixme_t RayResouceGetBindingForName_FIXME(const char *name, ray_resource_desc_t desc) {
	for (int i = 0; i < MAX_RESOURCES; ++i) {
		const vk_named_resource_t *const res = g_resources.named + i;
		if (strcmp(name, res->name) != 0)
			continue;

		if (res->desc_fixme.type != desc.type) {
			gEngine.Con_Printf(S_ERROR "Incompatible resource types for name %s: want %d, have %d\n", name, desc.type, res->desc_fixme.type);
			break;
		}

		if (res->desc_fixme.type == ResourceImage && res->desc_fixme.image_format != VK_FORMAT_UNDEFINED && desc.image_format != res->desc_fixme.image_format) {
			gEngine.Con_Printf(S_ERROR "Incompatible image formats for name %s: want %d, have %d\n", name, desc.image_format, res->desc_fixme.image_format);
			break;
		}

		return (ray_resource_binding_desc_fixme_t){i, res->count};
	}

	return (ray_resource_binding_desc_fixme_t){-1,-1};
}
*/
