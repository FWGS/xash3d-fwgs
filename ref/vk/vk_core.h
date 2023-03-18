#pragma once
#include "vk_common.h"

#include "xash3d_types.h"
#include "com_strings.h" // S_ERROR

#include "vk_nv_aftermath.h" // TODO remove explicit usage in XVK_CHECK

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

qboolean R_VkInit( void );
void R_VkShutdown( void );

VkSemaphore R_VkSemaphoreCreate( void );
void R_VkSemaphoreDestroy(VkSemaphore sema);

VkFence R_VkFenceCreate( qboolean signaled );
void R_VkFenceDestroy(VkFence fence);

// TODO move all these to vk_device.{h,c} or something
typedef struct physical_device_s {
	VkPhysicalDevice device;
	VkPhysicalDeviceMemoryProperties2 memory_properties2;
	VkPhysicalDeviceMemoryBudgetPropertiesEXT memory_budget;
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceProperties2 properties2;
	VkPhysicalDeviceAccelerationStructurePropertiesKHR properties_accel;
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR properties_ray_tracing_pipeline;
	qboolean anisotropy_enabled;
	uint32_t sbt_record_size;
} physical_device_t;

typedef struct vulkan_core_s {
	uint32_t vulkan_version;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;

	poolhandle_t pool;

	// TODO store important capabilities that affect render code paths
	// (as rtx, dedicated gpu memory, bindless, etc) separately in a struct
	qboolean debug, validate, rtx, nv_checkpoint;
	struct {
		VkSurfaceKHR surface;
		uint32_t num_surface_formats;
		VkSurfaceFormatKHR *surface_formats;

		uint32_t num_present_modes;
		VkPresentModeKHR *present_modes;
	} surface;

	physical_device_t physical_device;
	VkDevice device;
	VkQueue queue;

	unsigned int num_devices;
	ref_device_t *devices;
} vulkan_core_t;

extern vulkan_core_t vk_core;

const char *R_VkResultName(VkResult result);
const char *R_VkPresentModeName(VkPresentModeKHR present_mode);
const char *R_VkFormatName(VkFormat format);
const char *R_VkColorSpaceName(VkColorSpaceKHR colorspace);

#define SET_DEBUG_NAME(object, type, name) \
do { \
	if (vk_core.debug) { \
		VkDebugUtilsObjectNameInfoEXT duoni = { \
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, \
			.objectHandle = (uint64_t)object, \
			.objectType = type, \
			.pObjectName = name, \
		}; \
		XVK_CHECK(vkSetDebugUtilsObjectNameEXT(vk_core.device, &duoni)); \
	} \
} while (0)

#define SET_DEBUG_NAMEF(object, type, fmt, ...) \
do { \
	if (vk_core.debug) { \
		char buffer[1024]; \
		VkDebugUtilsObjectNameInfoEXT duoni = { \
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, \
			.objectHandle = (uint64_t)object, \
			.objectType = type, \
			.pObjectName = buffer, \
		}; \
		Q_snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
		XVK_CHECK(vkSetDebugUtilsObjectNameEXT(vk_core.device, &duoni)); \
	} \
} while (0)

#define DEBUG_BEGIN(cmdbuf, msg) \
	do { \
		if (vk_core.debug) { \
			const VkDebugUtilsLabelEXT label = { \
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, \
				.pLabelName = msg, \
			}; \
			vkCmdBeginDebugUtilsLabelEXT(cmdbuf, &label); \
			DEBUG_NV_CHECKPOINTF(cmdbuf, "begin %s", msg); \
		} \
	} while(0)

#define DEBUG_BEGINF(cmdbuf, fmt, ...) \
	do { \
		if (vk_core.debug) { \
			char buf[128]; \
			snprintf(buf, sizeof(buf), fmt, ##__VA_ARGS__); \
			const VkDebugUtilsLabelEXT label = { \
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, \
				.pLabelName = buf, \
			}; \
			vkCmdBeginDebugUtilsLabelEXT(cmdbuf, &label); \
			DEBUG_NV_CHECKPOINTF(cmdbuf, "begin " fmt, ##__VA_ARGS__); \
		} \
	} while(0)

#define DEBUG_END(cmdbuf) \
	do { \
		if (vk_core.debug) { \
			vkCmdEndDebugUtilsLabelEXT(cmdbuf); \
			DEBUG_NV_CHECKPOINTF(cmdbuf, "end "); /* TODO: find corresponding begin */ \
		} \
	} while(0)

// TODO make this not fatal: devise proper error handling strategies
// FIXME Host_Error does not cause process to exit, we need to handle this manually
#define XVK_CHECK(f) do { \
		const VkResult result = f; \
		if (result != VK_SUCCESS) { \
			gEngine.Con_Printf( S_ERROR "%s:%d " #f " failed (%d): %s\n", \
				__FILE__, __LINE__, result, R_VkResultName(result)); \
			DEBUG_NV_CHECKPOINT_DUMP(); \
			gEngine.Host_Error( S_ERROR "%s:%d " #f " failed (%d): %s\n", \
				__FILE__, __LINE__, result, R_VkResultName(result)); \
		} \
	} while(0)

#define INSTANCE_FUNCS(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceProperties2) \
	X(vkGetPhysicalDeviceFeatures2) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceMemoryProperties2) \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceFormatProperties) \
	X(vkCreateDevice) \
	X(vkGetDeviceProcAddr) \
	X(vkDestroyDevice) \
	X(vkDestroySurfaceKHR) \
	X(vkEnumerateDeviceExtensionProperties) \

#define INSTANCE_DEBUG_FUNCS(X) \
	X(vkCreateDebugUtilsMessengerEXT) \
	X(vkDestroyDebugUtilsMessengerEXT) \
	X(vkCmdBeginDebugUtilsLabelEXT) \
	X(vkCmdEndDebugUtilsLabelEXT) \
	X(vkCmdInsertDebugUtilsLabelEXT) \
	X(vkSetDebugUtilsObjectNameEXT) \

#define DEVICE_FUNCS(X) \
	X(vkGetDeviceQueue) \
	X(vkCreateSwapchainKHR) \
	X(vkGetSwapchainImagesKHR) \
	X(vkDestroySwapchainKHR) \
	X(vkCreateImageView) \
	X(vkCreateFramebuffer) \
	X(vkCreateRenderPass) \
	X(vkCreatePipelineCache) \
	X(vkDestroyPipelineCache) \
	X(vkCreatePipelineLayout) \
	X(vkCreateGraphicsPipelines) \
	X(vkCreateShaderModule) \
	X(vkCreateCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkCreateBuffer) \
	X(vkGetBufferMemoryRequirements) \
	X(vkAllocateMemory) \
	X(vkBindBufferMemory) \
	X(vkMapMemory) \
	X(vkUnmapMemory) \
	X(vkDestroyBuffer) \
	X(vkFreeMemory) \
	X(vkAcquireNextImageKHR) \
	X(vkCmdBeginRenderPass) \
	X(vkCmdExecuteCommands) \
	X(vkCmdEndRenderPass) \
	X(vkEndCommandBuffer) \
	X(vkQueueSubmit) \
	X(vkQueuePresentKHR) \
	X(vkWaitForFences) \
	X(vkResetFences) \
	X(vkCreateSemaphore) \
	X(vkDestroySemaphore) \
	X(vkCreateFence) \
	X(vkDestroyFence) \
	X(vkBeginCommandBuffer) \
	X(vkCmdBindPipeline) \
	X(vkCmdBindVertexBuffers) \
	X(vkCmdDraw) \
	X(vkDestroyCommandPool) \
	X(vkDestroyImageView) \
	X(vkDestroyFramebuffer) \
	X(vkDestroyRenderPass) \
	X(vkDestroyShaderModule) \
	X(vkDestroyPipeline) \
	X(vkDestroyPipelineLayout) \
	X(vkCreateImage) \
	X(vkGetImageMemoryRequirements) \
	X(vkBindImageMemory) \
	X(vkCmdPipelineBarrier) \
	X(vkCmdCopyBufferToImage) \
	X(vkCmdCopyBuffer) \
	X(vkQueueWaitIdle) \
	X(vkDeviceWaitIdle) \
	X(vkDestroyImage) \
	X(vkCmdBindDescriptorSets) \
	X(vkCreateSampler) \
	X(vkDestroySampler) \
	X(vkCreateDescriptorPool) \
	X(vkDestroyDescriptorPool) \
	X(vkCreateDescriptorSetLayout) \
	X(vkAllocateDescriptorSets) \
	X(vkUpdateDescriptorSets) \
	X(vkDestroyDescriptorSetLayout) \
	X(vkCmdSetViewport) \
	X(vkCmdSetScissor) \
	X(vkCmdUpdateBuffer) \
	X(vkCmdBindIndexBuffer) \
	X(vkCmdDrawIndexed) \
	X(vkCmdPushConstants) \
	X(vkCreateComputePipelines) \
	X(vkCmdDispatch) \
	X(vkCmdBlitImage) \
	X(vkCmdClearColorImage) \
	X(vkCmdCopyImage) \
	X(vkGetImageSubresourceLayout) \
	X(vkCmdSetCheckpointNV) \
	X(vkGetQueueCheckpointDataNV) \
	X(vkCreateQueryPool) \
	X(vkDestroyQueryPool) \
	X(vkCmdResetQueryPool) \
	X(vkCmdWriteTimestamp) \
	X(vkGetQueryPoolResults) \
	X(vkGetCalibratedTimestampsEXT) \

#define DEVICE_FUNCS_RTX(X) \
	X(vkGetAccelerationStructureBuildSizesKHR) \
	X(vkCreateAccelerationStructureKHR) \
	X(vkGetBufferDeviceAddress) \
	X(vkCmdBuildAccelerationStructuresKHR) \
	X(vkDestroyAccelerationStructureKHR) \
	X(vkGetAccelerationStructureDeviceAddressKHR) \
	X(vkCmdTraceRaysKHR) \
	X(vkCreateRayTracingPipelinesKHR) \
	X(vkGetRayTracingShaderGroupHandlesKHR) \

#define X(f) extern PFN_##f f;
	DEVICE_FUNCS(X)
	DEVICE_FUNCS_RTX(X)
	INSTANCE_FUNCS(X)
	INSTANCE_DEBUG_FUNCS(X)
#undef X
