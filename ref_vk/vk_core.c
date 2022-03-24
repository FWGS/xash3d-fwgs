
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
    uint32_t vulkan_version;
    PFN_vkGetInstanceProcAddr get_proc_addr;
    VkDebugUtilsMessengerEXT  debug_messenger;
    VkInstance instance;
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
    X(vkCreateInstance)             \
    X(vkDestroyInstance)           


#define INST_FUNC(X) \
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
    qboolean debug = !(gEngine.Sys_CheckParm("-vkdebug") || gEngine.Sys_CheckParm("-gldebug") );
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
    
    loadInstanceFunction(nulllist_funcs, ARRAYSIZE(nulllist_funcs));
    vkEnumerateInstanceVersion =  XVK_INSTANCE_FUNC(vkEnumerateInstanceVersion);
    if (vkEnumerateInstanceVersion) {
        vkEnumerateInstanceVersion(&vk_core.vulkan_version);
    } else {
        vk_core.vulkan_version = VK_MAKE_VERSION(1, 0, 0);
    }
   
    gEngine.Con_Printf("vulkan version: %u.%u.%u\n", XVK_PARSE_VERSION(vk_core.vulkan_version));
    
    {
        VkApplicationInfo app_info ={ 
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_VERSION_1_0,
            .applicationVersion = VK_MAKE_VERSION(0, 0, 0),
            .engineVersion = VK_MAKE_VERSION(0, 0, 0),
            .pApplicationName = "",
            .pEngineName = "xash3d-fwgs",
        };

        VkInstanceCreateInfo instanceCI ={
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &app_info,
        };

        const char** instance_exts = NULL;
        int num_instance_exts = gEngine.VK_GetInstanceExtension(&instance_exts);
        if (num_instance_exts < 0)
        {
            gEngine.Con_Printf(S_ERROR "Init Vulkan Extension Failed\n");
            return false;
        }

        gEngine.Con_Printf("Vulkan Instance extension count: %d", num_instance_exts);
        for (int i = 0; i < num_instance_exts; i++)
        {
            gEngine.Con_Printf("\t%d: %s\n",i, instance_exts[i]);
        }

        instanceCI.enabledExtensionCount = num_instance_exts;
        instanceCI.ppEnabledExtensionNames = instance_exts;

        XVK_CHECK(vkCreateInstance(&instanceCI, NULL, &vk_core.instance));

        loadInstanceFunction(inst_funcs, ARRAYSIZE(inst_funcs));
        if (debug) {
            VkDebugUtilsMessengerCreateInfoEXT dumcie = {
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                .messageSeverity = 0x1111,
                .messageType = 0x07,
                .pfnUserCallback = debugCallback,
            };
                XVK_CHECK(vkCreateDebugUtilsMessengerEXT(vk_core.instance,
            &dumcie, NULL, &vk_core.debug_messenger));
        }
    
        Mem_Free(instance_exts);
    }

	
	initTextures();
    
    return true;
}

void R_VkShutdown( void )
{
  	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);
    vkDestroyInstance(vk_core.instance, NULL);

}
