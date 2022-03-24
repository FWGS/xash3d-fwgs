
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

typedef struct vulkan_core_s {
    PFN_vkGetInstanceProcAddr get_proc_addr;
    uint32_t vulkan_version;
    VkInstance instance;
    VkDebugUtilsMessengerEXT  debug_messenger;
    poolhandle_t pool;
}vulkan_core_t;

vulkan_core_t vk_core;

const char* validation_layers[] = {
    "VK_LAYER_KHRONOS_validation",
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
    (PFN_##f)vk_core.get_proc_addr(vk_core.instance, #f)


#define DEFINE_FUNC_VAR(f) PFN_##f f

#if 0
typedef struct vk_dll_func_s {
   DEFINE_FUNC_VAR(vkEnumerateInstanceVersion);
   DEFINE_FUNC_VAR(vkCreateInstance);
   DEFINE_FUNC_VAR(vkDestroyInstance);
}vk_dll_func_t;
vk_dll_func_t vk_funcs;
#endif 

#define NULLINST_FUNC(X) \
    X(vkEnumerateInstanceVersion)   \
    X(vkCreateInstance)             


#define INST_FUNC(X) \
X(vkDestroyInstance)                                \
X(vkCreateDebugUtilsMessengerEXT)                   \
X(vkDestroyDebugUtilsMessengerEXT)                  \
X(vkEnumeratePhysicalDevices)                       \
X(vkGetPhysicalDeviceProperties2)                   \ 
X(vkGetPhysicalDeviceFeatures2)                     \
X(vkGetPhysicalDeviceSurfaceSupportKHR)             \
X(vkGetPhysicalDeviceQueueFamilyProperties)         \
X(vkGetPhysicalDeviceMemoryProperties)              \
X(vkCreateDevice)


#define X(f) PFN_##f f = NULL;  
    NULLINST_FUNC(X)
    INST_FUNC(X)
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
        *funcs[i].func = vk_core.get_proc_addr(vk_core.instance,funcs[i].name);  
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

// construct, destruct
qboolean R_VkInit( void )
{
    qboolean debug = !!(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug") );
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
    if (!gEngine.R_Init_Video(REF_VULKAN))  //request vulkan surface
    {
		gEngine.Con_Printf("Init Vulkan Video Failed\n");
        return false;
    }

    vk_core.get_proc_addr = gEngine.VK_GetVkGetInstanceProcAddr();
    if (!vk_core.get_proc_addr) {
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
    
    {
        const char** instance_extension = NULL;
        int num_instance_extension = 0;
        VkApplicationInfo app_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO};
        VkInstanceCreateInfo instanceCI = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        int vid_extension = 0;
        app_info.pEngineName = "xash3d-fwgs";
        app_info.pApplicationName ="VulkanExample";
        app_info.apiVersion = VK_VERSION_1_0;
        app_info.engineVersion = VK_VERSION_1_0;
        app_info.applicationVersion = VK_VERSION_1_0;
       
       // instanceCI.pApplicationInfo = &app_info;

        vid_extension = gEngine.VK_GetInstanceExtension(0, instance_extension);
        if (vid_extension < 0)
        {
            gEngine.Con_Printf(S_ERROR "Init Vulkan Extension Failed\n");
            return false;
        }
        gEngine.Con_Printf(S_WARN "Vid Extension Count: %d\n", vid_extension);
        num_instance_extension += vid_extension;
        if (debug) {
            num_instance_extension += 1;
        }
        instance_extension = Mem_Malloc(vk_core.pool, sizeof(const char*) * num_instance_extension);

        vid_extension = gEngine.VK_GetInstanceExtension(num_instance_extension, instance_extension);
        if (vid_extension < 0 ) {
            gEngine.Con_Printf(S_ERROR "Init Vulkan Extension Failed\n");
            Mem_Free(instance_extension);
            return false;
        }
        if (debug) {
            instance_extension[vid_extension] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }

        gEngine.Con_Printf("Request Instance extension count: %d\n", num_instance_extension);
        for (int i = 0; i < num_instance_extension; i++)
        {
            gEngine.Con_Printf("\t%d: %s\n", i, instance_extension[i]);
        }
        gEngine.Con_Printf("Request Instance extension count: %d\n", num_instance_extension);

        instanceCI.enabledExtensionCount = num_instance_extension;
        instanceCI.ppEnabledExtensionNames = instance_extension;

        if (debug) {
            instanceCI.ppEnabledLayerNames = validation_layers;
            instanceCI.enabledExtensionCount = ARRAYSIZE(validation_layers);
        }
        gEngine.Con_Printf("Load CreateInstance: %x\n", vkCreateInstance);
        // instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        // instanceCI.pApplicationInfo = NULL;
        // instanceCI.enabledExtensionCount = num_instance_extension;
        // instanceCI.ppEnabledExtensionNames = instance_extension;
        // instanceCI.enabledLayerCount = 0;
        // instanceCI.pNext = NULL;
        // instanceCI.flags = 0;
        // instanceCI.ppEnabledLayerNames = NULL; 
        XVK_CHECK(vkCreateInstance(&instanceCI, NULL, &vk_core.instance));
        loadInstanceFunction(inst_funcs, ARRAYSIZE(inst_funcs));
        if (debug) {
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
    
      /// Mem_Free(instance_extension);
    }
    gEngine.Con_Printf("Request Instance extension count: %d\n", 2);

	
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

}
