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

typedef struct vulkan_core_s {
	PFN_vkGetInstanceProcAddr get_proc_addr;
	uint32_t vulkan_version;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;

	byte *pool;
} vulkan_core_t;

vulkan_core_t vk_core = {0};

#define XVK_PARSE_VERSION(v) \
	VK_VERSION_MAJOR(v), \
	VK_VERSION_MINOR(v), \
	VK_VERSION_PATCH(v)

#define XVK_INSTANCE_FUNC(f) \
	((PFN_ ##f)vk_core.get_proc_addr(vk_core.instance, #f))

#define NULLINST_FUNCS(X) \
	X(vkEnumerateInstanceVersion) \
	X(vkCreateInstance) \

#define INSTANCE_FUNCS(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties2) \
	X(vkGetPhysicalDeviceFeatures2) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkCreateDevice) \

#define INSTANCE_DEBUG_FUNCS(X) \
	X(vkCreateDebugUtilsMessengerEXT) \
	X(vkDestroyDebugUtilsMessengerEXT) \

#define X(f) PFN_##f f = NULL;
	NULLINST_FUNCS(X)
	INSTANCE_FUNCS(X)
	INSTANCE_DEBUG_FUNCS(X)
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

static void loadInstanceFunctions(dllfunc_t *funcs, int count)
{
	for (int i = 0; i < count; ++i)
	{
		*funcs[i].func = vk_core.get_proc_addr(vk_core.instance, funcs[i].name);
		if (!*funcs[i].func)
		{
			gEngine.Con_Printf( S_WARN "Function %s was not loaded\n", funcs[i].name);
		}
	}
}

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

qboolean R_VkInit( void )
{
	const qboolean debug = !!(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug"));

	if( !gEngine.R_Init_Video( REF_VULKAN )) // request Vulkan surface
	{
		gEngine.Con_Printf( S_ERROR "Cannot initialize Vulkan video" );
		return false;
	}

	vk_core.get_proc_addr = gEngine.VK_GetVkGetInstanceProcAddr();
	if (!vk_core.get_proc_addr)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get vkGetInstanceProcAddr address" );
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

	{
		const char **instance_extensions = NULL;
		unsigned int num_instance_extensions = debug ? 1 : 0;
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

		if (debug)
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

		if (debug)
		{
			create_info.enabledLayerCount = ARRAYSIZE(validation_layers);
			create_info.ppEnabledLayerNames = validation_layers;

			gEngine.Con_Printf(S_WARN "Using Vulkan validation layers, expect severely degraded performance\n");
		}

		// TODO handle errors gracefully -- let it try next renderer
		XVK_CHECK(vkCreateInstance(&create_info, NULL, &vk_core.instance));

		loadInstanceFunctions(instance_funcs, ARRAYSIZE(instance_funcs));

		if (debug)
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
	}

	initTextures();

	return true;
}

void R_VkShutdown( void )
{
	if (vk_core.debug_messenger)
	{
		vkDestroyDebugUtilsMessengerEXT(vk_core.instance, vk_core.debug_messenger, NULL);
	}

	vkDestroyInstance(vk_core.instance, NULL);
}

