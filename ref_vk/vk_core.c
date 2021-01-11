#include "vk_common.h"
#include "vk_textures.h"

#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h" // required for ref_api.h
#include "ref_api.h"
#include "crtlib.h"
#include "com_strings.h"
#include "eiface.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define XVK_PARSE_VERSION(v) \
	VK_VERSION_MAJOR(v), \
	VK_VERSION_MINOR(v), \
	VK_VERSION_PATCH(v)

#define NULLINST_FUNCS(X) \
	X(vkEnumerateInstanceVersion) \
	X(vkCreateInstance) \

#define INSTANCE_FUNCS(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceProperties2) \
	X(vkGetPhysicalDeviceFeatures2) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkCreateDevice) \
	X(vkGetDeviceProcAddr) \
	X(vkDestroyDevice) \

#define INSTANCE_DEBUG_FUNCS(X) \
	X(vkCreateDebugUtilsMessengerEXT) \
	X(vkDestroyDebugUtilsMessengerEXT) \

#define DEVICE_FUNCS(X) \
	X(vkGetDeviceQueue) \
	X(vkCreateSwapchainKHR) \
	X(vkGetSwapchainImagesKHR) \
	X(vkDestroySwapchainKHR) \

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

#define X(f) PFN_##f f = NULL;
	NULLINST_FUNCS(X)
	INSTANCE_FUNCS(X)
	INSTANCE_DEBUG_FUNCS(X)
	DEVICE_FUNCS(X)
#undef X

static dllfunc_t nullinst_funcs[] = {
#define X(f) {#f, (void**)&f},
	NULLINST_FUNCS(X)
#undef X
};

static dllfunc_t instance_funcs[] = {
#define X(f) {#f, (void**)&f},
	INSTANCE_FUNCS(X)
#undef X
};

static dllfunc_t instance_debug_funcs[] = {
#define X(f) {#f, (void**)&f},
	INSTANCE_DEBUG_FUNCS(X)
#undef X
};

static dllfunc_t device_funcs[] = {
#define X(f) {#f, (void**)&f},
	DEVICE_FUNCS(X)
#undef X
};

static const char *resultName(VkResult result) {
	switch (result) {
	case VK_SUCCESS: return "VK_SUCCESS";
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_EVENT_SET: return "VK_EVENT_SET";
	case VK_EVENT_RESET: return "VK_EVENT_RESET";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
	case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
	case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
	case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
	case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
	case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
	case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
	case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
		return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
	case VK_ERROR_NOT_PERMITTED_EXT: return "VK_ERROR_NOT_PERMITTED_EXT";
	case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
	case VK_THREAD_IDLE_KHR: return "VK_THREAD_IDLE_KHR";
	case VK_THREAD_DONE_KHR: return "VK_THREAD_DONE_KHR";
	case VK_OPERATION_DEFERRED_KHR: return "VK_OPERATION_DEFERRED_KHR";
	case VK_OPERATION_NOT_DEFERRED_KHR: return "VK_OPERATION_NOT_DEFERRED_KHR";
	case VK_PIPELINE_COMPILE_REQUIRED_EXT: return "VK_PIPELINE_COMPILE_REQUIRED_EXT";
	default: return "UNKNOWN";
	}
}

// TODO make this not fatal: devise proper error handling strategies
#define XVK_CHECK(f) do { \
		const VkResult result = f; \
		if (result != VK_SUCCESS) { \
			gEngine.Host_Error( S_ERROR "%s:%d " #f " failed (%d): %s\n", \
				__FILE__, __LINE__, result, resultName(result)); \
		} \
	} while(0)

static const char *validation_layers[] = {
	"VK_LAYER_KHRONOS_validation",
};

VkBool32 debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
	void *pUserData) {
	(void)(pUserData);
	(void)(messageTypes);
	(void)(messageSeverity);

	// TODO better messages, not only errors, what are other arguments for, ...
	if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		gEngine.Con_Printf(S_ERROR "Validation: %s\n", pCallbackData->pMessage);
#ifdef DEBUG
#ifdef _MSC_VER
		__debugbreak();
#else
		__builtin_trap();
#endif
#endif
	}
	return VK_FALSE;
}

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

	struct {
		VkSurfaceCapabilitiesKHR surface_caps;
		VkSwapchainCreateInfoKHR create_info;
		VkSwapchainKHR swapchain;
		uint32_t num_images;
		VkImage *images;
	} swapchain;
} vulkan_core_t;

vulkan_core_t vk_core = {0};

static void loadInstanceFunctions(dllfunc_t *funcs, int count)
{
	for (int i = 0; i < count; ++i)
	{
		*funcs[i].func = vkGetInstanceProcAddr(vk_core.instance, funcs[i].name);
		if (!*funcs[i].func)
		{
			gEngine.Con_Printf( S_WARN "Function %s was not loaded\n", funcs[i].name);
		}
	}
}

static void loadDeviceFunctions(dllfunc_t *funcs, int count)
{
	for (int i = 0; i < count; ++i)
	{
		*funcs[i].func = vkGetDeviceProcAddr(vk_core.device, funcs[i].name);
		if (!*funcs[i].func)
		{
			gEngine.Con_Printf( S_WARN "Function %s was not loaded\n", funcs[i].name);
		}
	}
}

static qboolean createInstance( void )
{
	const char **instance_extensions = NULL;
	unsigned int num_instance_extensions = vk_core.debug ? 1 : 0;
	VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_API_VERSION_1_0,
		.applicationVersion = VK_MAKE_VERSION(0, 0, 0), // TODO
		.engineVersion = VK_MAKE_VERSION(0, 0, 0),
		.pApplicationName = "",
		.pEngineName = "xash3d-fwgs",
	};
	VkInstanceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
	};

	int vid_extensions = gEngine.VK_GetInstanceExtensions(0, NULL);
	if (vid_extensions < 0)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get Vulkan instance extensions\n" );
		return false;
	}

	num_instance_extensions += vid_extensions;

	instance_extensions = Mem_Malloc(vk_core.pool, sizeof(const char*) * num_instance_extensions);
	vid_extensions = gEngine.VK_GetInstanceExtensions(vid_extensions, instance_extensions);
	if (vid_extensions < 0)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get Vulkan instance extensions\n" );
		Mem_Free(instance_extensions);
		return false;
	}

	if (vk_core.debug)
	{
		instance_extensions[vid_extensions] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}

	gEngine.Con_Reportf("Requesting instance extensions: %d\n", num_instance_extensions);
	for (int i = 0; i < num_instance_extensions; ++i)
	{
		gEngine.Con_Reportf("\t%d: %s\n", i, instance_extensions[i]);
	}

	create_info.enabledExtensionCount = num_instance_extensions;
	create_info.ppEnabledExtensionNames = instance_extensions;

	if (vk_core.debug)
	{
		create_info.enabledLayerCount = ARRAYSIZE(validation_layers);
		create_info.ppEnabledLayerNames = validation_layers;

		gEngine.Con_Printf(S_WARN "Using Vulkan validation layers, expect severely degraded performance\n");
	}

	// TODO handle errors gracefully -- let it try next renderer
	XVK_CHECK(vkCreateInstance(&create_info, NULL, &vk_core.instance));

	loadInstanceFunctions(instance_funcs, ARRAYSIZE(instance_funcs));

	if (vk_core.debug)
	{
		loadInstanceFunctions(instance_debug_funcs, ARRAYSIZE(instance_debug_funcs));

		if (vkCreateDebugUtilsMessengerEXT)
		{
			VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
				.messageSeverity = 0x1111, //:vovka: VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
				.messageType = 0x07,
				.pfnUserCallback = debugCallback,
			};
			XVK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_core.instance, &debug_create_info, NULL, &vk_core.debug_messenger));
		} else
		{
			gEngine.Con_Printf(S_WARN "Vulkan debug utils messenger is not available\n");
		}
	}

	Mem_Free(instance_extensions);
	return true;
}

qboolean createDevice( void )
{
	VkPhysicalDevice *physical_devices = NULL;
	uint32_t num_physical_devices = 0;
	uint32_t best_device_index = UINT32_MAX;
	uint32_t queue_index = UINT32_MAX;

	XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_devices, physical_devices));

	physical_devices = Mem_Malloc(vk_core.pool, sizeof(VkPhysicalDevice) * num_physical_devices);
	XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_devices, physical_devices));

	gEngine.Con_Reportf("Have %u devices:\n", num_physical_devices);
	for (uint32_t i = 0; i < num_physical_devices; ++i)
	{
		VkQueueFamilyProperties *queue_family_props = NULL;
		uint32_t num_queue_family_properties = 0;
		VkPhysicalDeviceProperties props;

		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &num_queue_family_properties, queue_family_props);
		queue_family_props = Mem_Malloc(vk_core.pool, sizeof(VkQueueFamilyProperties) * num_queue_family_properties);
		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &num_queue_family_properties, queue_family_props);

		vkGetPhysicalDeviceProperties(physical_devices[i], &props);
		gEngine.Con_Reportf("\t%u: %04x:%04x %d %s %u.%u.%u %u.%u.%u\n",
			i, props.vendorID, props.deviceID, props.deviceType, props.deviceName,
			XVK_PARSE_VERSION(props.driverVersion), XVK_PARSE_VERSION(props.apiVersion));

		for (uint32_t j = 0; j < num_queue_family_properties; ++j)
		{
			VkBool32 present = 0;
			if (!(queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
				continue;

			vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, vk_core.surface, &present);

			if (!present)
				continue;

			queue_index = i;
			break;
		}

		Mem_Free(queue_family_props);

		// TODO pick the best device
		// For now we'll pick the first one that has graphics and can present to the surface
		if (queue_index < num_queue_family_properties)
		{
			best_device_index = i;
			break;
		}
	}

	if (best_device_index < num_physical_devices)
	{
		float prio = 1.f;
		VkDeviceQueueCreateInfo queue_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.flags = 0,
			.queueFamilyIndex = queue_index,
			.queueCount = 1,
			.pQueuePriorities = &prio,
		};
		const char *device_extensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		};
		VkDeviceCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.flags = 0,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queue_info,
			.enabledExtensionCount = ARRAYSIZE(device_extensions),
			.ppEnabledExtensionNames = device_extensions,
		};

		vk_core.physical_device.device = physical_devices[best_device_index];
		vkGetPhysicalDeviceMemoryProperties(vk_core.physical_device.device, &vk_core.physical_device.memory_properties);

		vkGetPhysicalDeviceProperties(vk_core.physical_device.device, &vk_core.physical_device.properties);
		gEngine.Con_Printf("Picked device #%u: %04x:%04x %d %s %u.%u.%u %u.%u.%u\n",
			best_device_index, vk_core.physical_device.properties.vendorID, vk_core.physical_device.properties.deviceID, vk_core.physical_device.properties.deviceType, vk_core.physical_device.properties.deviceName,
			XVK_PARSE_VERSION(vk_core.physical_device.properties.driverVersion), XVK_PARSE_VERSION(vk_core.physical_device.properties.apiVersion));

		// TODO allow it to fail gracefully
		XVK_CHECK(vkCreateDevice(vk_core.physical_device.device, &create_info, NULL, &vk_core.device));

		loadDeviceFunctions(device_funcs, ARRAYSIZE(device_funcs));

		vkGetDeviceQueue(vk_core.device, 0, 0, &vk_core.queue);
	}

	Mem_Free(physical_devices);
	return true;
}

const char *presentModeName(VkPresentModeKHR present_mode)
{
	switch (present_mode)
	{
		case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR";
		case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR";
		case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR";
		case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
		case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR: return "VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR";
		case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return "VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR";
		default: return "UNKNOWN";
	}
}

qboolean createSwapchain( void )
{
	VkSwapchainCreateInfoKHR *create_info = &vk_core.swapchain.create_info;

	uint32_t num_surface_formats = 0;
	VkSurfaceFormatKHR *surface_formats = NULL;

	uint32_t num_present_modes = 0;
	VkPresentModeKHR *present_modes = NULL;

	const uint32_t prev_num_images = vk_core.swapchain.num_images;

	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_core.physical_device.device, vk_core.surface, &num_present_modes, present_modes));
	present_modes = Mem_Malloc(vk_core.pool, sizeof(*present_modes) * num_present_modes);
	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_core.physical_device.device, vk_core.surface, &num_present_modes, present_modes));

	gEngine.Con_Reportf("Supported surface present modes: %u\n", num_present_modes);
	for (uint32_t i = 0; i < num_present_modes; ++i)
	{
		gEngine.Con_Reportf("\t%u: %s (%u)\n", i, presentModeName(present_modes[i]), present_modes[i]);
	}

	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_core.physical_device.device, vk_core.surface, &num_surface_formats, surface_formats));
	surface_formats = Mem_Malloc(vk_core.pool, sizeof(*surface_formats) * num_surface_formats);
	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_core.physical_device.device, vk_core.surface, &num_surface_formats, surface_formats));

	gEngine.Con_Reportf("Supported surface formats: %u\n", num_surface_formats);
	for (uint32_t i = 0; i < num_surface_formats; ++i)
	{
		// TODO symbolicate
		gEngine.Con_Reportf("\t%u: %u %u\n", surface_formats[i].format, surface_formats[i].colorSpace);
	}

	XVK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_core.physical_device.device, vk_core.surface, &vk_core.swapchain.surface_caps));

	create_info->sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info->pNext = NULL;
	create_info->surface = vk_core.surface;
	create_info->imageFormat = VK_FORMAT_B8G8R8A8_SRGB; // TODO get from surface_formats
	create_info->imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR; // TODO get from surface_formats
	create_info->imageExtent.width = vk_core.swapchain.surface_caps.currentExtent.width;
	create_info->imageExtent.height = vk_core.swapchain.surface_caps.currentExtent.height;
	create_info->imageArrayLayers = 1;
	create_info->imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	create_info->imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info->preTransform = vk_core.swapchain.surface_caps.currentTransform;
	create_info->compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	create_info->presentMode = VK_PRESENT_MODE_FIFO_KHR; // TODO caps, MAILBOX is better
	//create_info->presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; // TODO caps, MAILBOX is better
	create_info->clipped = VK_TRUE;
	create_info->oldSwapchain = vk_core.swapchain.swapchain;

	create_info->minImageCount = vk_core.swapchain.surface_caps.minImageCount + 3;
	if (vk_core.swapchain.surface_caps.maxImageCount && create_info->minImageCount > vk_core.swapchain.surface_caps.maxImageCount)
		create_info->minImageCount = vk_core.swapchain.surface_caps.maxImageCount;

	XVK_CHECK(vkCreateSwapchainKHR(vk_core.device, create_info, NULL, &vk_core.swapchain.swapchain));

	Mem_Free(present_modes);
	Mem_Free(surface_formats);

	vk_core.swapchain.num_images = 0;
	XVK_CHECK(vkGetSwapchainImagesKHR(vk_core.device, vk_core.swapchain.swapchain, &vk_core.swapchain.num_images, NULL));
	if (prev_num_images != vk_core.swapchain.num_images)
	{
		if (vk_core.swapchain.images)
			Mem_Free(vk_core.swapchain.images);

		vk_core.swapchain.images = Mem_Malloc(vk_core.pool, sizeof(*vk_core.swapchain.images) * vk_core.swapchain.num_images);
	}
	XVK_CHECK(vkGetSwapchainImagesKHR(vk_core.device, vk_core.swapchain.swapchain, &vk_core.swapchain.num_images, vk_core.swapchain.images));

	return true;
}

qboolean R_VkInit( void )
{
	vk_core.debug = !!(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug"));

	if( !gEngine.R_Init_Video( REF_VULKAN )) // request Vulkan surface
	{
		gEngine.Con_Printf( S_ERROR "Cannot initialize Vulkan video\n" );
		return false;
	}

	vkGetInstanceProcAddr = gEngine.VK_GetVkGetInstanceProcAddr();
	if (!vkGetInstanceProcAddr)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get vkGetInstanceProcAddr address\n" );
		return false;
	}

	vk_core.pool = Mem_AllocPool("Vulkan pool");

	loadInstanceFunctions(nullinst_funcs, ARRAYSIZE(nullinst_funcs));

	if (vkEnumerateInstanceVersion)
	{
		vkEnumerateInstanceVersion(&vk_core.vulkan_version);
	}
	else
	{
		vk_core.vulkan_version = VK_MAKE_VERSION(1, 0, 0);
	}

	gEngine.Con_Printf( "Vulkan version %u.%u.%u\n", XVK_PARSE_VERSION(vk_core.vulkan_version));

	if (!createInstance())
		return false;

	vk_core.surface = gEngine.VK_CreateSurface(vk_core.instance);
	if (!vk_core.surface)
	{
		gEngine.Con_Printf( S_ERROR "Cannot create Vulkan surface\n" );
		// FIXME destroy surface
		return false;
	}

	if (!createDevice())
		return false;

	if (!createSwapchain())
		return false;

	initTextures();

	return true;
}

void R_VkShutdown( void )
{
	vkDestroySwapchainKHR(vk_core.device, vk_core.swapchain.swapchain, NULL);

	vkDestroyDevice(vk_core.device, NULL);

	if (vk_core.debug_messenger)
	{
		vkDestroyDebugUtilsMessengerEXT(vk_core.instance, vk_core.debug_messenger, NULL);
	}

	// TODO destroy surface

	vkDestroyInstance(vk_core.instance, NULL);

	Mem_FreePool(&vk_core.pool);
}

