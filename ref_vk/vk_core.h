#include "xash3d_types.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

qboolean R_VkInit( void );
void R_VkShutdown( void );

// FIXME load from embedded static structs
VkShaderModule loadShader(const char *filename);

typedef struct physical_device_s {
	VkPhysicalDevice device;
	VkPhysicalDeviceMemoryProperties memory_properties;
	VkPhysicalDeviceProperties properties;
} physical_device_t;

typedef struct vulkan_core_s {
	uint32_t vulkan_version;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;

	byte *pool;

	qboolean debug;

	VkSurfaceKHR surface;

	physical_device_t physical_device;
	VkDevice device;
	VkQueue queue;

	VkRenderPass render_pass;

	struct {
		VkSurfaceCapabilitiesKHR surface_caps;
		VkSwapchainCreateInfoKHR create_info;
		VkSwapchainKHR swapchain;
		uint32_t num_images;
		VkImage *images;
		VkImageView *image_views;
		VkFramebuffer *framebuffers;
	} swapchain;
} vulkan_core_t;

extern vulkan_core_t vk_core;

const char *resultName(VkResult result);

// TODO make this not fatal: devise proper error handling strategies
// FIXME Host_Error does not cause process to exit, we need to handle this manually
#define XVK_CHECK(f) do { \
		const VkResult result = f; \
		if (result != VK_SUCCESS) { \
			gEngine.Host_Error( S_ERROR "%s:%d " #f " failed (%d): %s\n", \
				__FILE__, __LINE__, result, resultName(result)); \
		} \
	} while(0)

#define DEVICE_FUNCS(X) \
	X(vkGetDeviceQueue) \
	X(vkCreateSwapchainKHR) \
	X(vkGetSwapchainImagesKHR) \
	X(vkDestroySwapchainKHR) \
	X(vkCreateImageView) \
	X(vkCreateFramebuffer) \
	X(vkCreateRenderPass) \
	X(vkCreatePipelineLayout) \
	X(vkCreateGraphicsPipelines) \
	X(vkCreateShaderModule) \

#define X(f) extern PFN_##f f;
	DEVICE_FUNCS(X)
#undef X
