
#include "physint.h"
#include "vk_common.h"
#include "vk_textures.h"

#include "cvardef.h"
#include "const.h"
#include "ref_api.h"
#include "crtlib.h"
#include "com_strings.h"
#include "xash3d_types.h"
#include "eiface.h"
#include <stdint.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct physical_device_s {
    VkPhysicalDevice device;
    VkPhysicalDeviceMemoryProperties memory_properties;
}physics_device_t;

typedef struct vulkan_core_s {
    uint32_t vulkan_version;
    VkInstance instance;
    VkDebugUtilsMessengerEXT  debug_messenger;
    poolhandle_t pool;
    qboolean debug;

    VkSurfaceKHR surface;
    physics_device_t physical_devices;
    VkDevice device;
    VkQueue graphics_queue;
}vulkan_core_t;

vulkan_core_t vk_core;

static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

const char* validation_layers[] = {
    "VK_LAYER_LUNARG_standard_validation",
};

static const char* resultName(VkResult result) {
    #define CASE_STR(c)  case c : return #c;
    switch (result) {
        CASE_STR(VK_SUCCESS) 
        default: return "UNKNOWN";
    }
    #undef CASE_STR
}

#define XVK_PARSE_VERSION(v) \
    VK_VERSION_MAJOR(v),    \
    VK_VERSION_MINOR(v),    \
    VK_VERSION_PATCH(v)


#define XVK_INSTANCE_FUNC(f) \
    (PFN_##f)vkGetInstanceProcAddr(vk_core.instance, #f)


#define DEFINE_FUNC_VAR(f) PFN_##f f


#define NULLINST_FUNC(X) \
    X(vkEnumerateInstanceVersion)   \
    X(vkCreateInstance)             


#define INST_FUNC(X)                                    \
    X(vkDestroyInstance)                                \
    X(vkEnumeratePhysicalDevices)                       \
    X(vkGetPhysicalDeviceProperties2)                   \ 
    X(vkGetPhysicalDeviceFeatures2)                     \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)             \
    X(vkGetPhysicalDeviceQueueFamilyProperties)         \
    X(vkGetPhysicalDeviceMemoryProperties)              \
    X(vkCreateDevice)                                   \
    X(vkGetDeviceProcAddr)  

#define INST_DEBUG_FUNC(X) \
    X(vkCreateDebugUtilsMessengerEXT)                   \
    X(vkDestroyDebugUtilsMessengerEXT)                  

#define DEVICE_FUNCS(X)  \   
    X(vkGetDeviceQueue)     


#define X(f) PFN_##f f = NULL;  
    NULLINST_FUNC(X)
    INST_FUNC(X)
    INST_DEBUG_FUNC(X)
    DEVICE_FUNCS(X)
#undef X

static dllfunc_t nulllist_funcs[] = {
#define X(f) {#f, (void**)&f},
    NULLINST_FUNC(X)
#undef X
};

static dllfunc_t inst_funcs[] = {
#define X(f) {#f, (void**)&f},
    INST_FUNC(X)
#undef X
};

static dllfunc_t debug_funcs[] = {
#define X(f) {#f, (void**)&f},
    INST_DEBUG_FUNC(X)
#undef X
};

static dllfunc_t device_funcs[] = {
#define X(f) {#f, (void**)&f},
    DEVICE_FUNCS(X)
#undef X
};

#define XVK_CHECK(f)                                                            \
    do {                                                                        \
        const VkResult _f = (f);                                                \
        if(_f != VK_SUCCESS){                                                   \
            gEngine.Host_Error(S_ERROR "%s:%d " #f " failed (%d): %s\n",        \
            __FILE__, __LINE__,  f, resultName(_f));                            \
        }                                                                       \
    }while(0)


static void loadInstanceFunction(dllfunc_t *funcs, int count) 
{
    for (int i= 0; i < count; i++) {
        *funcs[i].func = vkGetDeviceProcAddr(vk_core.device, funcs[i].name);  
        if (!*funcs[i].func) {
            gEngine.Con_Printf(S_WARN "Function: %s was not load\n",
            funcs[i].name);
        }
    }
}

static void loadDeviceFunction(dllfunc_t *funcs, int count) 
{
    for (int i= 0; i < count; i++) {
        *funcs[i].func = vkGetInstanceProcAddr(vk_core.instance,funcs[i].name);  
        if (!*funcs[i].func) {
            gEngine.Con_Printf(S_WARN "Function: %s was not load\n",
            funcs[i].name);
        }
    }
}


static VkBool32 debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void * userData)
{
    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        gEngine.Con_Printf(S_ERROR "Validation: %s\n", data->pMessage);
    }

    return VK_FALSE;
}

static qboolean createInstance(void)
{
    const char** instance_extension = NULL;
    unsigned int num_instance_extension = vk_core.debug ? 1 : 0;
    VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo instanceCI = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    int vid_extension = 0;
    app_info.pEngineName = "xash3d-fwgs";
    app_info.pApplicationName ="VulkanExample";
    app_info.apiVersion = VK_VERSION_1_0;
    app_info.engineVersion = VK_VERSION_1_0;
    app_info.applicationVersion = VK_VERSION_1_0;
    app_info.pNext = NULL;

 //   instanceCI.pApplicationInfo = &app_info;

    vid_extension = gEngine.VK_GetInstanceExtension(0, instance_extension);
    if (vid_extension < 0)
    {
        gEngine.Con_Printf(S_ERROR "Init Vulkan Extension Failed\n");
        return false;
    }
    num_instance_extension += vid_extension;

    instance_extension = Mem_Malloc(vk_core.pool, sizeof(const char*) * num_instance_extension);
    vid_extension = gEngine.VK_GetInstanceExtension(num_instance_extension, instance_extension);
    if (vid_extension < 0 ) {
        gEngine.Con_Printf(S_ERROR "Init Vulkan Extension Failed\n");
        Mem_Free(instance_extension);
        return false;
    }
    if (vk_core.debug) {
        instance_extension[vid_extension] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        instanceCI.ppEnabledLayerNames = validation_layers;
        instanceCI.enabledExtensionCount = ARRAYSIZE(validation_layers);
    }

    gEngine.Con_Printf("Request Instance extension count: %d\n", num_instance_extension);
    for (int i = 0; i < num_instance_extension; i++)
    {
        gEngine.Con_Printf("\t%d: %s\n", i, instance_extension[i]);
    }
    
    instanceCI.enabledExtensionCount = num_instance_extension;
    instanceCI.ppEnabledExtensionNames = instance_extension;

    XVK_CHECK(vkCreateInstance(&instanceCI, NULL, &vk_core.instance));
    loadInstanceFunction(inst_funcs, ARRAYSIZE(inst_funcs));

    if (vk_core.debug) {
        loadInstanceFunction(debug_funcs, ARRAYSIZE(debug_funcs));
        if (vkCreateDebugUtilsMessengerEXT) {
            VkDebugUtilsMessengerCreateInfoEXT dumcie = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity = 0x1111,
                .messageType = 0x07,
                .pfnUserCallback = debugCallback,
            };
            
            XVK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_core.instance,
            &dumcie, NULL, &vk_core.debug_messenger));
        }else {
            gEngine.Con_Printf(S_WARN "validation unavailable\n");
        }
    }

    Mem_Free(instance_extension);
    return true;
}

static qboolean createDevice(void)
{
    VkPhysicalDevice* physical_devices = NULL;
    uint32_t num_physical_device = 0u;
    uint32_t best_device_index =UINT32_MAX;
    uint32_t queue_index = UINT32_MAX;
    float prio = 1.f;
    XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_device, NULL));

    physical_devices = Mem_Malloc(vk_core.pool, sizeof(VkPhysicalDevice)* num_physical_device);
    XVK_CHECK(vkEnumeratePhysicalDevices(vk_core.instance, &num_physical_device, physical_devices));

    for (uint32_t i = 0; i < num_physical_device; ++i) {
        VkQueueFamilyProperties* queue_family_properties =NULL;
        uint32_t num_queue_family_prperties =0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &num_queue_family_prperties, NULL);
        queue_family_properties = Mem_Malloc(vk_core.pool, sizeof(VkQueueFamilyProperties)* num_queue_family_prperties);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &num_queue_family_prperties, queue_family_properties);
        for (uint32_t j = 0; j < num_queue_family_prperties; ++j) {
            VkBool32 present =false;
            if (!(queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, vk_core.surface, &present);
            if (!present) {
                continue;
            }
            queue_index = i;
            break;
        }
        /// todo select more device
        if (queue_index < num_queue_family_prperties ) {
            best_device_index = i;
            break;
        }
        Mem_Free(queue_family_properties);
    }

    gEngine.Con_Printf("Request Instance extension count: %d\n", 1);
    if (best_device_index < num_physical_device) {
          const char* device_extension[] ={
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };
        VkDeviceQueueCreateInfo queue_info ={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        VkDeviceCreateInfo create_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_index;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &prio;
    
        create_info.queueCreateInfoCount =1;
        create_info.pQueueCreateInfos = &queue_info;
        create_info.enabledExtensionCount = ARRAYSIZE(device_extension);
        create_info.ppEnabledExtensionNames= device_extension;

        gEngine.Con_Printf("Request Instance extension count: %d\n", 4);
        vk_core.physical_devices.device = physical_devices[best_device_index];
        vkGetPhysicalDeviceMemoryProperties(vk_core.physical_devices.device, &vk_core.physical_devices.memory_properties);
        XVK_CHECK(vkCreateDevice( vk_core.physical_devices.device, &create_info, NULL, &vk_core.device));
        loadDeviceFunction(device_funcs, ARRAYSIZE(device_funcs));
        vkGetDeviceQueue(vk_core.device, 0, 0, &vk_core.graphics_queue);
    }


    Mem_Free(physical_devices);
    gEngine.Con_Printf("VK FIXME: %s init succe\n", __FUNCTION__);
    return true;
}
// construct, destruct
qboolean R_VkInit( void )
{
    gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
    vk_core.debug = !!(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug"));
    if (!gEngine.R_Init_Video(REF_VULKAN))  //request vulkan surface
    {
		gEngine.Con_Printf("Init Vulkan Video Failed\n");
        return false;
    }

    vkGetInstanceProcAddr = gEngine.VK_GetVkGetInstanceProcAddr();
    if (!vkGetInstanceProcAddr) {
        gEngine.Con_Printf(S_ERROR "Init Vulkan Extension Failed\n");
        return false;
    }
    vk_core.pool = Mem_AllocPool("Vulkan Pool");
    loadInstanceFunction(nulllist_funcs, ARRAYSIZE(nulllist_funcs));
    vkEnumerateInstanceVersion =  XVK_INSTANCE_FUNC(vkEnumerateInstanceVersion);
    if (vkEnumerateInstanceVersion) {
        vkEnumerateInstanceVersion(&vk_core.vulkan_version);
    } else {
        vk_core.vulkan_version = VK_MAKE_VERSION(1, 0, 0);
    }
   
    gEngine.Con_Printf("vulkan version: %u.%u.%u\n", XVK_PARSE_VERSION(vk_core.vulkan_version));
    if(!createInstance()){
        return false;
    }
    vk_core.surface = gEngine.VK_CreateSurface(vk_core.instance);
    if (!vk_core.surface) {
        gEngine.Con_Printf(S_ERROR "Create Surface Failed\n");
        return false;
    }
    if (!createDevice()) {
        return false;
    }

	initTextures();
    
    return true;
}

void R_VkShutdown( void )
{
  	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
    if (vk_core.debug_messenger) {
      vkDestroyDebugUtilsMessengerEXT(vk_core.instance, vk_core.debug_messenger, NULL);
      vk_core.debug_messenger = VK_NULL_HANDLE;
    }
    vkDestroyInstance(vk_core.instance, NULL);
    vk_core.instance = VK_NULL_HANDLE;

    Mem_FreePool(&vk_core.pool);

}
