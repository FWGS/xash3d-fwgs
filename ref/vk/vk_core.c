#include "vk_core.h"

#include "vk_common.h"
#include "vk_textures.h"
#include "vk_overlay.h"
#include "vk_renderstate.h"
#include "vk_staging.h"
#include "vk_framectl.h"
#include "vk_brush.h"
#include "vk_scene.h"
#include "vk_cvar.h"
#include "vk_pipeline.h"
#include "vk_render.h"
#include "vk_geometry.h"
#include "vk_studio.h"
#include "vk_rtx.h"
#include "vk_descriptor.h"
#include "vk_nv_aftermath.h"
#include "vk_devmem.h"
#include "vk_commandpool.h"
#include "r_slows.h"

// FIXME move this rt-specific stuff out
#include "vk_light.h"

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
	DEVICE_FUNCS_RTX(X)
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

static dllfunc_t device_funcs_rtx[] = {
#define X(f) {#f, (void**)&f},
	DEVICE_FUNCS_RTX(X)
#undef X
};

static const char *validation_layers[] = {
	"VK_LAYER_KHRONOS_validation",
};

static const char* device_extensions_req[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static const char* device_extensions_rt[] = {
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME,
};

static const char* device_extensions_nv_checkpoint[] = {
	VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,
	VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME,
};

VkBool32 VKAPI_PTR debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
	void *pUserData) {
	(void)(pUserData);
	(void)(messageTypes);
	(void)(messageSeverity);

	if (Q_strcmp(pCallbackData->pMessageIdName, "VUID-vkMapMemory-memory-00683") == 0)
		return VK_FALSE;

	/* if (messageSeverity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) { */
	/* 	gEngine.Con_Printf(S_WARN "Validation: %s\n", pCallbackData->pMessage); */
	/* } */

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
	const char ** instance_extensions = NULL;
	unsigned int num_instance_extensions = vk_core.debug ? 1 : 0;
	const VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		// TODO support versions 1.0 and 1.1 for simple traditional rendering
		// This would require using older physical device features and props query structures
		// .apiVersion = vk_core.rtx ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1,
		.apiVersion = VK_API_VERSION_1_2,
		.applicationVersion = VK_MAKE_VERSION(0, 0, 0), // TODO
		.engineVersion = VK_MAKE_VERSION(0, 0, 0),
		.pApplicationName = "",
		.pEngineName = "xash3d-fwgs",
	};
	const VkValidationFeatureEnableEXT validation_features[] = {
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
	};
	const VkValidationFeaturesEXT validation_ext = {
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.pEnabledValidationFeatures = validation_features,
		.enabledValidationFeatureCount = COUNTOF(validation_features),
	};
	VkInstanceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.pNext = vk_core.validate ? &validation_ext : NULL,
	};

	int vid_extensions = gEngine.XVK_GetInstanceExtensions(0, NULL);
	if (vid_extensions < 0)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get Vulkan instance extensions\n" );
		return false;
	}

	num_instance_extensions += vid_extensions;

	instance_extensions = Mem_Malloc(vk_core.pool, sizeof(const char*) * num_instance_extensions);
	vid_extensions = gEngine.XVK_GetInstanceExtensions(vid_extensions, instance_extensions);
	if (vid_extensions < 0)
	{
		gEngine.Con_Printf( S_ERROR "Cannot get Vulkan instance extensions\n" );
		Mem_Free((void*)instance_extensions);
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

	if (vk_core.validate)
	{
		create_info.enabledLayerCount = ARRAYSIZE(validation_layers);
		create_info.ppEnabledLayerNames = validation_layers;

		gEngine.Con_Printf(S_WARN "Using Vulkan validation layers, expect severely degraded performance\n");
	}

	// TODO handle errors gracefully -- let it try next renderer
	XVK_CHECK(vkCreateInstance(&create_info, NULL, &vk_core.instance));

	loadInstanceFunctions(instance_funcs, ARRAYSIZE(instance_funcs));

	if (vk_core.debug || vk_core.validate)
	{
		loadInstanceFunctions(instance_debug_funcs, ARRAYSIZE(instance_debug_funcs));

 		if (vk_core.validate) {
			if (vkCreateDebugUtilsMessengerEXT) {
				VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
					.messageSeverity = 0x1111, //:vovka: VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
					.messageType = 0x07,
					.pfnUserCallback = debugCallback,
				};
				XVK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_core.instance, &debug_create_info, NULL, &vk_core.debug_messenger));
			} else {
				gEngine.Con_Printf(S_WARN "Vulkan debug utils messenger is not available\n");
			}
		}
	}

	Mem_Free((void*)instance_extensions);
	return true;
}

static const VkExtensionProperties *findExtension( const VkExtensionProperties *exts, uint32_t num_exts, const char *extension ) {
	for (uint32_t i = 0; i < num_exts; ++i) {
		if (strncmp(exts[i].extensionName, extension, sizeof(exts[i].extensionName)) == 0)
			return exts + i;
	}
	return NULL;
}

static qboolean deviceSupportsExtensions(const VkExtensionProperties *exts, uint32_t num_exts, const char *check_extensions[], int check_extensions_count) {
	qboolean result = true;
	for (int i = 0; i < check_extensions_count; ++i) {
		if (!findExtension(exts, num_exts, check_extensions[i])) {
			gEngine.Con_Reportf(S_ERROR "Extension %s is not supported\n", check_extensions[i]);
			result = false;
		}
	}
	return result;
}

static void devicePrintExtensionsFromList(const VkExtensionProperties *exts, uint32_t num_exts, const char *print_extensions[], int print_extensions_count) {
	for (int i = 0; i < print_extensions_count; ++i) {
		const VkExtensionProperties *const ext_prop = findExtension(exts, num_exts, print_extensions[i]);
		if (!ext_prop) {
			gEngine.Con_Printf( "\t\t\t%s: N/A\n", print_extensions[i]);
		} else {
			gEngine.Con_Printf( "\t\t\t%s: %u.%u.%u\n", ext_prop->extensionName, XVK_PARSE_VERSION(ext_prop->specVersion));
		}
	}
}

#define MAX_DEVICE_EXTENSIONS 16
static int appendDeviceExtensions(const char** out, int out_count, const char *in_extensions[], int in_extensions_count) {
	for (int i = 0; i < in_extensions_count; ++i) {
		ASSERT(out_count < MAX_DEVICE_EXTENSIONS);
		out[out_count++] = in_extensions[i];
	}
	return out_count;
}

// FIXME this is almost exactly the physical_device_t, reuse
typedef struct {
	VkPhysicalDevice device;
	VkPhysicalDeviceFeatures2 features;
	VkPhysicalDeviceProperties props;
	uint32_t queue_index;
	qboolean anisotropy;
	qboolean ray_tracing;
	qboolean nv_checkpoint;
} vk_available_device_t;

static int enumerateDevices( vk_available_device_t **available_devices ) {
	VkPhysicalDevice *physical_devices = NULL;
	uint32_t num_physical_devices = 0;
	vk_available_device_t *this_device = NULL;

	XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_devices, physical_devices));
	physical_devices = Mem_Malloc(vk_core.pool, sizeof(VkPhysicalDevice) * num_physical_devices);
	XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_devices, physical_devices));
	gEngine.Con_Reportf("Have %u devices:\n", num_physical_devices);

	vk_core.num_devices = num_physical_devices;
	vk_core.devices = Mem_Calloc( vk_core.pool, num_physical_devices * sizeof( *vk_core.devices ));

	*available_devices = Mem_Malloc(vk_core.pool, num_physical_devices * sizeof(vk_available_device_t));
	this_device = *available_devices;
	for (uint32_t i = 0; i < num_physical_devices; ++i) {
		uint32_t queue_index = VK_QUEUE_FAMILY_IGNORED;
		VkPhysicalDeviceProperties props;
		VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,};

		// FIXME also pay attention to various device limits. We depend on them implicitly now.

		vkGetPhysicalDeviceProperties(physical_devices[i], &props);

		// Store devices list in vk_core.devices for pfnGetRenderDevices
		vk_core.devices[i].vendorID = props.vendorID;
		vk_core.devices[i].deviceID = props.deviceID;
		switch( props.deviceType )
		{
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_INTERGRATED_GPU;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_DISCRETE_GPU;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_VIRTUAL_GPU;
			break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_CPU;
			break;
		default:
			vk_core.devices[i].deviceType = REF_DEVICE_TYPE_OTHER;
			break;
		}
		Q_strncpy( vk_core.devices[i].deviceName, props.deviceName, sizeof( vk_core.devices[i].deviceName ));

		gEngine.Con_Printf("\t%u: %04x:%04x %d %s %u.%u.%u %u.%u.%u\n",
			i, props.vendorID, props.deviceID, props.deviceType, props.deviceName,
			XVK_PARSE_VERSION(props.driverVersion), XVK_PARSE_VERSION(props.apiVersion));

		{
			uint32_t num_queue_family_properties = 0;
			VkQueueFamilyProperties *queue_family_props = NULL;
			vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &num_queue_family_properties, queue_family_props);
			queue_family_props = Mem_Malloc(vk_core.pool, sizeof(VkQueueFamilyProperties) * num_queue_family_properties);
			vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &num_queue_family_properties, queue_family_props);

			// Find queue family that supports needed properties
			for (uint32_t j = 0; j < num_queue_family_properties; ++j) {
				VkBool32 supports_present = 0;
				const qboolean supports_graphics = !!(queue_family_props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT);
				const qboolean supports_compute = !!(queue_family_props[j].queueFlags & VK_QUEUE_COMPUTE_BIT);
				vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, vk_core.surface.surface, &supports_present);

				gEngine.Con_Reportf("\t\tQueue %d/%d present: %d graphics: %d compute: %d\n", j, num_queue_family_properties, supports_present, supports_graphics, supports_compute);

				if (!supports_present)
					continue;

				// ray tracing needs compute
				// also, by vk spec graphics queue must support compute
				if (!supports_graphics || !supports_compute)
					continue;

				queue_index = j;
				break;
			}

			Mem_Free(queue_family_props);
		}

		if (queue_index == VK_QUEUE_FAMILY_IGNORED) {
			gEngine.Con_Printf( S_WARN "\t\tSkipping this device as compatible queue (which has both compute and graphics and also can present) not found\n" );
			continue;
		}

		{
			uint32_t num_device_extensions = 0;
			VkExtensionProperties *extensions;

			XVK_CHECK(vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &num_device_extensions, NULL));
			extensions = Mem_Malloc(vk_core.pool, sizeof(VkExtensionProperties) * num_device_extensions);
			XVK_CHECK(vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &num_device_extensions, extensions));

			gEngine.Con_Reportf( "\t\tSupported device extensions: %u\n", num_device_extensions);
			devicePrintExtensionsFromList(extensions, num_device_extensions, device_extensions_req, ARRAYSIZE(device_extensions_req));
			devicePrintExtensionsFromList(extensions, num_device_extensions, device_extensions_rt, ARRAYSIZE(device_extensions_rt));
			devicePrintExtensionsFromList(extensions, num_device_extensions, device_extensions_nv_checkpoint, ARRAYSIZE(device_extensions_nv_checkpoint));

			vkGetPhysicalDeviceFeatures2(physical_devices[i], &features);
			this_device->anisotropy = features.features.samplerAnisotropy;
			gEngine.Con_Printf("\t\tAnistoropy supported: %d\n", this_device->anisotropy);

			this_device->ray_tracing = deviceSupportsExtensions(extensions, num_device_extensions, device_extensions_rt, ARRAYSIZE(device_extensions_rt));
			gEngine.Con_Printf("\t\tRay tracing supported: %d\n", this_device->ray_tracing);

			this_device->nv_checkpoint = vk_core.debug && deviceSupportsExtensions(extensions, num_device_extensions, device_extensions_nv_checkpoint, ARRAYSIZE(device_extensions_nv_checkpoint));
			gEngine.Con_Printf("\t\tNV checkpoints supported: %d\n", this_device->nv_checkpoint);

			Mem_Free(extensions);
		}

		this_device->device = physical_devices[i];
		this_device->queue_index = queue_index;
		this_device->features = features;
		this_device->props = props;
		++this_device;
	}

	Mem_Free(physical_devices);

	return this_device - *available_devices;
}

static void devicePrintMemoryInfo(const VkPhysicalDeviceMemoryProperties *props, const VkPhysicalDeviceMemoryBudgetPropertiesEXT *budget) {
	gEngine.Con_Printf("Memory heaps: %d\n", props->memoryHeapCount);
	for (int i = 0; i < (int)props->memoryHeapCount; ++i) {
		const VkMemoryHeap* const heap = props->memoryHeaps + i;
		gEngine.Con_Printf("  %d: size=%dMb used=%dMb avail=%dMb device_local=%d\n", i,
			(int)(heap->size / (1024 * 1024)),
			(int)(budget->heapUsage[i] / (1024 * 1024)),
			(int)(budget->heapBudget[i] / (1024 * 1024)),
			!!(heap->flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT));
	}

	gEngine.Con_Printf("Memory types: %d\n", props->memoryTypeCount);
	for (int i = 0; i < (int)props->memoryTypeCount; ++i) {
		const VkMemoryType* const type = props->memoryTypes + i;
		gEngine.Con_Printf("  %d: bit=0x%x heap=%d flags=%c%c%c%c%c\n", i,
			(1 << i),
			type->heapIndex,
			type->propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? 'D' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? 'V' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? 'C' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? '$' : '.',
			type->propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ? 'L' : '.'
		);
	}
}

static qboolean createDevice( void ) {
	void *head = NULL;
	vk_available_device_t *available_devices;
	const int num_available_devices = enumerateDevices( &available_devices );
	char unique_deviceID[16];
	const qboolean is_target_device = vk_device_target_id && Q_stricmp(vk_device_target_id->string, "") && num_available_devices > 0;
	qboolean is_target_device_found = false;

	for (int i = 0; i < num_available_devices; ++i) {
		const vk_available_device_t *candidate_device = available_devices + i;
		// Skip non-target device
		Q_snprintf( unique_deviceID, sizeof( unique_deviceID ), "%04x:%04x", candidate_device->props.vendorID, candidate_device->props.deviceID );
		if (is_target_device && !is_target_device_found && Q_stricmp(vk_device_target_id->string, unique_deviceID)) {
			if (i == num_available_devices-1) {
				gEngine.Con_Printf("Not found device %s, start on %s. Please set a valid device.\n", vk_device_target_id->string, unique_deviceID);
			} else {
				gEngine.Con_Printf("Skip device %s, because selected %s\n", unique_deviceID, vk_device_target_id->string);
				continue;
			}
		} else {
			is_target_device_found = true;
		}

		if (candidate_device->ray_tracing && !CVAR_TO_BOOL(vk_only)) {
			vk_core.rtx = true;
		}

		VkPhysicalDeviceAccelerationStructureFeaturesKHR accel_feature = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
			.pNext = head,
			.accelerationStructure = VK_TRUE,
		};
		head = &accel_feature;
		VkPhysicalDevice16BitStorageFeatures sixteen_bit_feature = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
			.pNext = head,
			.storageBuffer16BitAccess = VK_TRUE,
		};
		head = &sixteen_bit_feature;
		VkPhysicalDeviceVulkan12Features vk12_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = head,
			.shaderSampledImageArrayNonUniformIndexing = VK_TRUE, // Needed for texture sampling in closest hit shader
			.storageBuffer8BitAccess = VK_TRUE,
			.uniformAndStorageBuffer8BitAccess = VK_TRUE,
			.bufferDeviceAddress = VK_TRUE,
		};
		head = &vk12_features;
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_feature = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
			.pNext = head,
			.rayTracingPipeline = VK_TRUE,
			// TODO .rayTraversalPrimitiveCulling = VK_TRUE,
		};
		head = &ray_tracing_pipeline_feature;
		VkPhysicalDeviceRayQueryFeaturesKHR ray_query_pipeline_feature = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
			.pNext = head,
			.rayQuery = VK_TRUE,
		};

		if (vk_core.rtx) {
			head = &ray_query_pipeline_feature;
		} else {
			head = NULL;
		}

		VkPhysicalDeviceFeatures2 features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = head,
			.features.samplerAnisotropy = candidate_device->features.features.samplerAnisotropy,
			.features.shaderInt16 = true,
		};
		head = &features;

		VkDeviceDiagnosticsConfigCreateInfoNV diag_config_nv = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV,
			.pNext = head,
			.flags = VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV
		};

		if (candidate_device->nv_checkpoint)
			head = &diag_config_nv;

		const float queue_priorities[1] = {1.f};
		VkDeviceQueueCreateInfo queue_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.flags = 0,
			.queueFamilyIndex = candidate_device->queue_index,
			.queueCount = ARRAYSIZE(queue_priorities),
			.pQueuePriorities = queue_priorities,
		};

		const char* device_extensions[MAX_DEVICE_EXTENSIONS];
		int device_extensions_count = 0;
		device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_req, ARRAYSIZE(device_extensions_req));
		if (vk_core.rtx)
			device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_rt, ARRAYSIZE(device_extensions_rt));
		if (candidate_device->nv_checkpoint)
			device_extensions_count = appendDeviceExtensions(device_extensions, device_extensions_count, device_extensions_nv_checkpoint, ARRAYSIZE(device_extensions_nv_checkpoint));

		VkDeviceCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = head,
			.flags = 0,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queue_info,
			.enabledExtensionCount = device_extensions_count,
			.ppEnabledExtensionNames = device_extensions,
		};

		{
			vk_core.physical_device.memory_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
			vk_core.physical_device.memory_properties2.pNext = &vk_core.physical_device.memory_budget;
			vk_core.physical_device.memory_budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
			vk_core.physical_device.memory_budget.pNext = NULL;
			vkGetPhysicalDeviceMemoryProperties2(candidate_device->device, &vk_core.physical_device.memory_properties2);
		}

		gEngine.Con_Printf("Trying device #%d: %04x:%04x %d %s %u.%u.%u %u.%u.%u\n",
			i, candidate_device->props.vendorID, candidate_device->props.deviceID, candidate_device->props.deviceType, candidate_device->props.deviceName,
			XVK_PARSE_VERSION(candidate_device->props.driverVersion), XVK_PARSE_VERSION(candidate_device->props.apiVersion));

		devicePrintMemoryInfo(&vk_core.physical_device.memory_properties2.memoryProperties, &vk_core.physical_device.memory_budget);

		{
			const VkResult result = vkCreateDevice(candidate_device->device, &create_info, NULL, &vk_core.device);
			if (result != VK_SUCCESS) {
				gEngine.Con_Printf( S_ERROR "%s:%d vkCreateDevice failed (%d): %s\n",
					__FILE__, __LINE__, result, R_VkResultName(result));
				continue;
			}
		}
		vk_core.nv_checkpoint = candidate_device->nv_checkpoint;

		vk_core.physical_device.device = candidate_device->device;
		vk_core.physical_device.anisotropy_enabled = features.features.samplerAnisotropy;
		vk_core.physical_device.properties = candidate_device->props;

		loadDeviceFunctions(device_funcs, ARRAYSIZE(device_funcs));

		if (vk_core.rtx)
		{
			loadDeviceFunctions(device_funcs_rtx, ARRAYSIZE(device_funcs_rtx));
			vk_core.physical_device.properties2.pNext = &vk_core.physical_device.properties_accel;
			vk_core.physical_device.properties_accel.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
			vk_core.physical_device.properties_accel.pNext = &vk_core.physical_device.properties_ray_tracing_pipeline;
			vk_core.physical_device.properties_ray_tracing_pipeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
			vk_core.physical_device.properties_ray_tracing_pipeline.pNext = NULL;
		}

		// TODO should we check Vk version first?
		vk_core.physical_device.properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		vkGetPhysicalDeviceProperties2(vk_core.physical_device.device, &vk_core.physical_device.properties2);

		if (vk_core.rtx) {
			//g_rtx.sbt_record_size = ALIGN_UP(vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize, vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleAlignment);
			vk_core.physical_device.sbt_record_size = ALIGN_UP(vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize, vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupBaseAlignment);
		}

		vkGetDeviceQueue(vk_core.device, 0, 0, &vk_core.queue);
		return true;
	}

	gEngine.Con_Printf( S_ERROR "No compatibe Vulkan devices found. Vulkan render will not be available\n" );
	return false;
}
static qboolean initSurface( void )
{
	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_present_modes, vk_core.surface.present_modes));
	vk_core.surface.present_modes = Mem_Malloc(vk_core.pool, sizeof(*vk_core.surface.present_modes) * vk_core.surface.num_present_modes);
	XVK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_present_modes, vk_core.surface.present_modes));

	gEngine.Con_Printf("Supported surface present modes: %u\n", vk_core.surface.num_present_modes);
	for (uint32_t i = 0; i < vk_core.surface.num_present_modes; ++i)
	{
		gEngine.Con_Reportf("\t%u: %s (%u)\n", i, R_VkPresentModeName(vk_core.surface.present_modes[i]), vk_core.surface.present_modes[i]);
	}

	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_surface_formats, vk_core.surface.surface_formats));
	vk_core.surface.surface_formats = Mem_Malloc(vk_core.pool, sizeof(*vk_core.surface.surface_formats) * vk_core.surface.num_surface_formats);
	XVK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_core.surface.num_surface_formats, vk_core.surface.surface_formats));

	gEngine.Con_Reportf("Supported surface formats: %u\n", vk_core.surface.num_surface_formats);
	for (uint32_t i = 0; i < vk_core.surface.num_surface_formats; ++i)
	{
		gEngine.Con_Reportf("\t%u: %s(%u) %s(%u)\n", i,
			R_VkFormatName(vk_core.surface.surface_formats[i].format), vk_core.surface.surface_formats[i].format,
			R_VkColorSpaceName(vk_core.surface.surface_formats[i].colorSpace), vk_core.surface.surface_formats[i].colorSpace);
	}

	return true;
}

// TODO modules
/*
typedef struct r_vk_module_s {
	qboolean (*init)(void);
	void (*destroy)(void);

	// TODO next: dependecies, refcounts, ...
} r_vk_module_t;

#define LIST_MODULES(X) ...

=>
extern const r_vk_module_t vk_instance_module;
...
extern const r_vk_module_t vk_rtx_module;
...

=>
static const r_vk_module_t *const modules[] = {
	&vk_instance_module,
	&vk_device_module,
	&vk_aftermath_module,
	&vk_texture_module,
	...
	&vk_rtx_module,
	...
};
*/

qboolean R_VkInit( void )
{
	// FIXME !!!! handle initialization errors properly: destroy what has already been created

	vk_core.validate = !!gEngine.Sys_CheckParm("-vkvalidate");
	vk_core.debug = vk_core.validate || !!(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug"));
	vk_core.rtx = false;
	VK_LoadCvars();

	R_SlowsInit();

	if( !gEngine.R_Init_Video( REF_VULKAN )) // request Vulkan surface
	{
		gEngine.Con_Printf( S_ERROR "Cannot initialize Vulkan video\n" );
		return false;
	}

	vkGetInstanceProcAddr = gEngine.XVK_GetVkGetInstanceProcAddr();
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

	vk_core.surface.surface = gEngine.XVK_CreateSurface(vk_core.instance);
	if (!vk_core.surface.surface)
	{
		gEngine.Con_Printf( S_ERROR "Cannot create Vulkan surface\n" );
		return false;
	}

#if USE_AFTERMATH
	if (!VK_AftermathInit()) {
		gEngine.Con_Printf( S_ERROR "Cannot initialize Nvidia Nsight Aftermath SDK\n" );
	}
#endif

	if (!createDevice())
		return false;

	VK_LoadCvarsAfterInit();

	if (!initSurface())
		return false;

	if (!VK_DevMemInit())
		return false;

	if (!R_VkStagingInit())
		return false;

	if (!VK_PipelineInit())
		return false;

	// TODO ...
	if (!VK_DescriptorInit())
		return false;

	if (!VK_FrameCtlInit())
		return false;

	if (!R_GeometryBuffer_Init())
		return false;

	if (!VK_RenderInit())
		return false;

	VK_StudioInit();

	VK_SceneInit();

	initTextures();

	// All below need render_pass

	if (!R_VkOverlay_Init())
		return false;

	if (!VK_BrushInit())
		return false;

	if (vk_core.rtx)
	{
		if (!VK_RayInit())
			return false;

		// FIXME move all this to rt-specific modules
		VK_LightsInit();
	}

	return true;
}

void R_VkShutdown( void ) {
	XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

	if (vk_core.rtx)
	{
		VK_LightsShutdown();
		VK_RayShutdown();
	}

	VK_BrushShutdown();
	VK_StudioShutdown();
	R_VkOverlay_Shutdown();

	VK_RenderShutdown();
	R_GeometryBuffer_Shutdown();

	VK_FrameCtlShutdown();

	destroyTextures();

	VK_PipelineShutdown();

	VK_DescriptorShutdown();

	R_VkStagingShutdown();

	VK_DevMemDestroy();

	vkDestroyDevice(vk_core.device, NULL);

#if USE_AFTERMATH
	VK_AftermathShutdown();
#endif

	if (vk_core.debug_messenger)
	{
		vkDestroyDebugUtilsMessengerEXT(vk_core.instance, vk_core.debug_messenger, NULL);
	}

	Mem_Free(vk_core.surface.present_modes);
	Mem_Free(vk_core.surface.surface_formats);
	vkDestroySurfaceKHR(vk_core.instance, vk_core.surface.surface, NULL);
	vkDestroyInstance(vk_core.instance, NULL);
	Mem_FreePool(&vk_core.pool);

	gEngine.R_Free_Video();
}

VkSemaphore R_VkSemaphoreCreate( void ) {
	VkSemaphore sema;
	VkSemaphoreCreateInfo sci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.flags = 0,
	};
	XVK_CHECK(vkCreateSemaphore(vk_core.device, &sci, NULL, &sema));
	return sema;
}

void R_VkSemaphoreDestroy(VkSemaphore sema) {
	vkDestroySemaphore(vk_core.device, sema, NULL);
}

VkFence R_VkFenceCreate( qboolean signaled ) {
	VkFence fence;
	const VkFenceCreateInfo fci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
	};
	XVK_CHECK(vkCreateFence(vk_core.device, &fci, NULL, &fence));
	return fence;
}

void R_VkFenceDestroy(VkFence fence) {
	vkDestroyFence(vk_core.device, fence, NULL);
}
