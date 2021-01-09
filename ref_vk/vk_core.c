#include "vk_common.h"
#include "vk_textures.h"

#include "xash3d_types.h"
#include "cvardef.h"
#include "const.h" // required for ref_api.h
#include "ref_api.h"
#include "crtlib.h"
#include "com_strings.h"

qboolean R_VkInit( void )
{
	if( !gEngine.R_Init_Video( REF_VULKAN )) // request Vulkan surface
	{
		gEngine.Con_Printf( S_ERROR "Cannot initialize Vulkan video" );
		return false;
	}

	// TODO VkInstance create ...
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
}

