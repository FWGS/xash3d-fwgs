#include "vk_core.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_2d.h"
#include "vk_renderstate.h"
#include "vk_buffer.h"
#include "vk_framectl.h"
#include "vk_brush.h"
#include "vk_scene.h"
#include "vk_cvar.h"
#include "vk_pipeline.h"
#include "vk_render.h"
#include "vk_studio.h"

#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h" // required for ref_api.h
#include "ref_api.h"
#include "crtlib.h"
#include "com_strings.h"
#include "eiface.h"

#include <string.h>
#include <errno.h>

#define XVK_PARSE_VERSION(v) \
	VK_VERSION_MAJOR(v), \
	VK_VERSION_MINOR(v), \
	VK_VERSION_PATCH(v)

#define NULLINST_FUNCS(X) \
	X(vkEnumerateInstanceVersion) \
	X(vkCreateInstance) \

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

const char *resultName(VkResult result) {
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

static const char *validation_layers[] = {
	"VK_LAYER_KHRONOS_validation",
};

static const char* device_extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,

	// Optional: RTX
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	//VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
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
#ifdef _MSC_VER
		__debugbreak();
#else
		__builtin_trap();
#endif
	}
	return VK_FALSE;
}

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
		.apiVersion = vk_core.rtx ? VK_API_VERSION_1_2 : VK_API_VERSION_1_0,
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

static qboolean hasExtension( const VkExtensionProperties *exts, uint32_t num_exts, const char *extension )
{
	for (uint32_t i = 0; i < num_exts; ++i) {
		if (strncmp(exts[i].extensionName, extension, sizeof(exts[i].extensionName)) == 0)
			return true;
	}

	return false;
}

static qboolean deviceSupportsRtx( const VkExtensionProperties *exts, uint32_t num_exts )
{
	for (int i = 1 /* skip swapchain ext */; i < ARRAYSIZE(device_extensions); ++i) {
		if (!hasExtension(exts, num_exts, device_extensions[i]))
			return false;
	}
	return true;
}

static qboolean pickAndCreateDevice( void )
{
	VkPhysicalDevice *physical_devices = NULL;
	uint32_t num_physical_devices = 0;
	uint32_t best_device_index = UINT32_MAX;
	uint32_t queue_index = UINT32_MAX;
	qboolean retval = false;

	XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_devices, physical_devices));

	physical_devices = Mem_Malloc(vk_core.pool, sizeof(VkPhysicalDevice) * num_physical_devices);
	XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_devices, physical_devices));

	gEngine.Con_Reportf("Have %u devices:\n", num_physical_devices);
	for (uint32_t i = 0; i < num_physical_devices; ++i)
	{
		VkQueueFamilyProperties *queue_family_props = NULL;
		uint32_t num_queue_family_properties = 0;
		uint32_t num_device_extensions = 0;
		VkExtensionProperties *extensions;
		VkPhysicalDeviceProperties props;

		// FIXME also pay attention to various device limits. We depend on them implicitly now.

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

			vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, vk_core.surface.surface, &present);

			if (!present)
				continue;

			queue_index = i;
			break;
		}

		XVK_CHECK(vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &num_device_extensions, NULL));
		extensions = Mem_Malloc(vk_core.pool, sizeof(VkExtensionProperties) * num_device_extensions);
		XVK_CHECK(vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &num_device_extensions, extensions));

		gEngine.Con_Reportf( "\tSupported device extensions: %u\n", num_device_extensions);
		for (uint32_t j = 0; j < num_device_extensions; ++j) {
			gEngine.Con_Reportf( "\t\t: %s: %u.%u.%u\n", extensions[j].extensionName, XVK_PARSE_VERSION(extensions[j].specVersion));
		}

		if (vk_core.rtx) {
			if (!deviceSupportsRtx(extensions, num_device_extensions)) {
				gEngine.Con_Printf( S_WARN "Device doesn't support necessary RTX extensions, skipping\n");
				queue_index = UINT32_MAX;
			}
		}

		Mem_Free(extensions);
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
		VkDeviceCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.flags = 0,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queue_info,

			// TODO for now device_extensions array contains one required extension (swapchain)
			// and a bunch of RTX-optional extensions. Use only one if RTX was not requested,
			// and only use all of them if it was.
			.enabledExtensionCount = vk_core.rtx ? ARRAYSIZE(device_extensions) : 1,
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
		retval = true;
	} else {
		gEngine.Con_Printf( S_ERROR "No compatibe Vulkan devices found. Vulkan render will not be available\n" );
	}

	Mem_Free(physical_devices);
	return retval;
}

static const char *presentModeName(VkPresentModeKHR present_mode)
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

static qboolean initSurface( void )
{
	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_present_modes, vk_core.surface.present_modes));
	vk_core.surface.present_modes = Mem_Malloc(vk_core.pool, sizeof(*vk_core.surface.present_modes) * vk_core.surface.num_present_modes);
	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_present_modes, vk_core.surface.present_modes));

	gEngine.Con_Printf("Supported surface present modes: %u\n", vk_core.surface.num_present_modes);
	for (uint32_t i = 0; i < vk_core.surface.num_present_modes; ++i)
	{
		gEngine.Con_Reportf("\t%u: %s (%u)\n", i, presentModeName(vk_core.surface.present_modes[i]), vk_core.surface.present_modes[i]);
	}

	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_surface_formats, vk_core.surface.surface_formats));
	vk_core.surface.surface_formats = Mem_Malloc(vk_core.pool, sizeof(*vk_core.surface.surface_formats) * vk_core.surface.num_surface_formats);
	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_surface_formats, vk_core.surface.surface_formats));

	gEngine.Con_Reportf("Supported surface formats: %u\n", vk_core.surface.num_surface_formats);
	for (uint32_t i = 0; i < vk_core.surface.num_surface_formats; ++i)
	{
		// TODO symbolicate
		gEngine.Con_Reportf("\t%u: %u %u\n", i, vk_core.surface.surface_formats[i].format, vk_core.surface.surface_formats[i].colorSpace);
	}

	return true;
}

static qboolean createCommandPool( void ) {
	VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	};

	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandBufferCount = 1,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	};

	XVK_CHECK(vkCreateCommandPool(vk_core.device, &cpci, NULL, &vk_core.command_pool));
	cbai.commandPool = vk_core.command_pool;
	XVK_CHECK(vkAllocateCommandBuffers(vk_core.device, &cbai, &vk_core.cb));

	return true;
}

// ... FIXME actual numbers
#define MAX_TEXTURES 4096

static qboolean initDescriptorPool( void )
{
	VkDescriptorPoolSize dps[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_TEXTURES,
		}, {
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
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
		.maxSets = MAX_TEXTURES + 1,
	};

	XVK_CHECK(vkCreateDescriptorPool(vk_core.device, &dpci, NULL, &vk_core.descriptor_pool.pool));

	{
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
		VkDescriptorSetLayout* tmp_layouts = Mem_Malloc(vk_core.pool, sizeof(VkDescriptorSetLayout) * MAX_DESC_SETS);
		VkDescriptorSetAllocateInfo dsai = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = vk_core.descriptor_pool.pool,
			.descriptorSetCount = MAX_DESC_SETS,
			.pSetLayouts = tmp_layouts,
		};
		XVK_CHECK(vkCreateDescriptorSetLayout(vk_core.device, &dslci, NULL, &vk_core.descriptor_pool.one_texture_layout));
		for (int i = 0; i < (int)MAX_DESC_SETS; ++i)
				tmp_layouts[i] = vk_core.descriptor_pool.one_texture_layout;

		XVK_CHECK(vkAllocateDescriptorSets(vk_core.device, &dsai, vk_core.descriptor_pool.sets));

		Mem_Free(tmp_layouts);
	}

	{
		const int num_sets = ARRAYSIZE(vk_core.descriptor_pool.ubo_sets);
		// ... TODO find better place for this; this should be per-pipeline/shader
		VkDescriptorSetLayoutBinding bindings[] = { {
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		}};
		VkDescriptorSetLayoutCreateInfo dslci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = ARRAYSIZE(bindings),
			.pBindings = bindings,
		};
		VkDescriptorSetLayout* tmp_layouts = Mem_Malloc(vk_core.pool, sizeof(VkDescriptorSetLayout) * num_sets);
		VkDescriptorSetAllocateInfo dsai = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = vk_core.descriptor_pool.pool,
			.descriptorSetCount = num_sets,
			.pSetLayouts = tmp_layouts,
		};
		XVK_CHECK(vkCreateDescriptorSetLayout(vk_core.device, &dslci, NULL, &vk_core.descriptor_pool.one_uniform_buffer_layout));
		for (int i = 0; i < num_sets; ++i)
				tmp_layouts[i] = vk_core.descriptor_pool.one_uniform_buffer_layout;

		XVK_CHECK(vkAllocateDescriptorSets(vk_core.device, &dsai, vk_core.descriptor_pool.ubo_sets));

		Mem_Free(tmp_layouts);
	}

	return true;
}

qboolean R_VkInit( void )
{
	// FIXME !!!! handle initialization errors properly: destroy what has already been created

	vk_core.debug = !!(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug"));
	vk_core.rtx = !!(gEngine.Sys_CheckParm("-rtx"));

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

	vk_core.surface.surface = gEngine.VK_CreateSurface(vk_core.instance);
	if (!vk_core.surface.surface)
	{
		gEngine.Con_Printf( S_ERROR "Cannot create Vulkan surface\n" );
		return false;
	}

	if (!pickAndCreateDevice())
		return false;

	if (!initSurface())
		return false;

	if (!createCommandPool())
		return false;

	if (!createBuffer(&vk_core.staging, 16 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	{
		VkSamplerCreateInfo sci = {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,//CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,//CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.anisotropyEnable = VK_FALSE,
			.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.minLod = 0.f,
			.maxLod = 16.,
		};
		XVK_CHECK(vkCreateSampler(vk_core.device, &sci, NULL, &vk_core.default_sampler));
	}

	if (!VK_PipelineInit())
		return false;

	// TODO ...
	if (!initDescriptorPool())
		return false;

	VK_LoadCvars();

	if (!VK_FrameCtlInit())
		return false;

	if (!VK_RenderInit())
		return false;

	VK_StudioInit();

	VK_SceneInit();

	initTextures();

	// All below need render_pass

	if (!initVk2d())
		return false;

	if (!VK_BrushInit())
		return false;

	return true;
}

void R_VkShutdown( void )
{
	VK_BrushShutdown();
	VK_StudioShutdown();
	deinitVk2d();

	VK_RenderShutdown();

	VK_FrameCtlShutdown();

	destroyTextures();

	VK_PipelineShutdown();

	vkDestroyDescriptorPool(vk_core.device, vk_core.descriptor_pool.pool, NULL);
	vkDestroyDescriptorSetLayout(vk_core.device, vk_core.descriptor_pool.one_texture_layout, NULL);
	vkDestroyDescriptorSetLayout(vk_core.device, vk_core.descriptor_pool.one_uniform_buffer_layout, NULL);
	vkDestroySampler(vk_core.device, vk_core.default_sampler, NULL);
	destroyBuffer(&vk_core.staging);

	vkDestroyCommandPool(vk_core.device, vk_core.command_pool, NULL);

	vkDestroyDevice(vk_core.device, NULL);

	if (vk_core.debug_messenger)
	{
		vkDestroyDebugUtilsMessengerEXT(vk_core.instance, vk_core.debug_messenger, NULL);
	}

	Mem_Free(vk_core.surface.present_modes);
	Mem_Free(vk_core.surface.surface_formats);
	vkDestroySurfaceKHR(vk_core.instance, vk_core.surface.surface, NULL);
	vkDestroyInstance(vk_core.instance, NULL);
	Mem_FreePool(&vk_core.pool);
}

VkShaderModule loadShader(const char *filename) {
	fs_offset_t size = 0;
	VkShaderModuleCreateInfo smci = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	};
	VkShaderModule shader;
	byte* buf = gEngine.COM_LoadFile( filename, &size, false);

	if (!buf)
	{
		gEngine.Host_Error( S_ERROR "Cannot open shader file \"%s\"\n", filename);
	}

	if ((size % 4 != 0) || (((uintptr_t)buf & 3) != 0)) {
		gEngine.Host_Error( S_ERROR "size %zu or buf %p is not aligned to 4 bytes as required by SPIR-V/Vulkan spec", size, buf);
	}

	smci.codeSize = size;
	//smci.pCode = (const uint32_t*)buf;
	memcpy(&smci.pCode, &buf, sizeof(void*));

	XVK_CHECK(vkCreateShaderModule(vk_core.device, &smci, NULL, &shader));
	Mem_Free(buf);
	return shader;
}

VkSemaphore createSemaphore( void ) {
	VkSemaphore sema;
	VkSemaphoreCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.flags = 0,
	};
	XVK_CHECK(vkCreateSemaphore(vk_core.device, &sci, NULL, &sema));
	return sema;
}

void destroySemaphore(VkSemaphore sema) {
	vkDestroySemaphore(vk_core.device, sema, NULL);
}

VkFence createFence( void ) {
	VkFence fence;
	VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = 0,
	};
	XVK_CHECK(vkCreateFence(vk_core.device, &fci, NULL, &fence));
	return fence;
}

void destroyFence(VkFence fence) {
	vkDestroyFence(vk_core.device, fence, NULL);
}

static uint32_t findMemoryWithType(uint32_t type_index_bits, VkMemoryPropertyFlags flags) {
	for (uint32_t i = 0; i < vk_core.physical_device.memory_properties.memoryTypeCount; ++i) {
		if (!(type_index_bits & (1 << i)))
			continue;

		if ((vk_core.physical_device.memory_properties.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}

	return UINT32_MAX;
}

device_memory_t allocateDeviceMemory(VkMemoryRequirements req, VkMemoryPropertyFlags props) {
	// TODO coalesce allocations, ...
	device_memory_t ret = {0};
	VkMemoryAllocateInfo mai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = req.size,
		.memoryTypeIndex = findMemoryWithType(req.memoryTypeBits, props),
	};
	XVK_CHECK(vkAllocateMemory(vk_core.device, &mai, NULL, &ret.device_memory));
	return ret;
}

void freeDeviceMemory(device_memory_t *mem)
{
	vkFreeMemory(vk_core.device, mem->device_memory, NULL);
}
