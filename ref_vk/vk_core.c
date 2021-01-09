#include "vk_common.h"
#include "vk_textures.h"

#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h" // required for ref_api.h
#include "ref_api.h"
#include "crtlib.h"
#include "com_strings.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct vulkan_core_s {
	PFN_vkGetInstanceProcAddr get_proc_addr;
	uint32_t vulkan_version;
	VkInstance instance;
} vulkan_core_t;

vulkan_core_t vk_core = {0};

#define XVK_PARSE_VERSION(v) \
	VK_VERSION_MAJOR(v), \
	VK_VERSION_MINOR(v), \
	VK_VERSION_PATCH(v)

#define XVK_INSTANCE_FUNC(f) \
	((PFN_ ##f)vk_core.get_proc_addr(vk_core.instance, #f))

static PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;

qboolean R_VkInit( void )
{

	if( !gEngine.R_Init_Video( REF_VULKAN )) // request Vulkan surface
	{
		gEngine.Con_Printf( S_ERROR "Cannot initialize Vulkan video" );
		return false;
	}

	// TODO VkInstance create ...
	vk_core.get_proc_addr = gEngine.VK_GetVkGetInstanceProcAddr();
	if (!vk_core.get_proc_addr)
	{
			gEngine.Con_Printf( S_ERROR "Cannot get vkGetInstanceProcAddr address" );
			return false;
	}

	vkEnumerateInstanceVersion = XVK_INSTANCE_FUNC(vkEnumerateInstanceVersion);
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
		const char **instance_exts = NULL;
		const int num_instance_exts = gEngine.VK_GetInstanceExtensions(&instance_exts);
		if (num_instance_exts < 0)
		{
			gEngine.Con_Printf( S_ERROR "Cannot get Vulkan instance extensions" );
			return false;
		}

		gEngine.Con_Reportf("Vulkan instance extensions: %d\n", num_instance_exts);
		for (int i = 0; i < num_instance_exts; ++i)
		{
			gEngine.Con_Reportf("\t%d: %s\n", i, instance_exts[i]);
		}

		Mem_Free(instance_exts);
	}

	initTextures();

	return true;
}

void R_VkShutdown( void )
{
	gEngine.Con_Printf("VK FIXME: %s\n", __FUNCTION__);

	// TODO destroy everything
	//vkDestroyInstance(vk_core.instance, NULL);
}

