#include "vk_studio.h"
#include "vk_common.h"

static struct {
} g_studio;

void VK_StudioInit( void );
void VK_StudioShutdown( void );

void Mod_LoadStudioModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	PRINT_NOT_IMPLEMENTED_ARGS("(%s)", mod->name);
}

void Mod_StudioLoadTextures( model_t *mod, void *data )
{
	PRINT_NOT_IMPLEMENTED_ARGS("(%s)", mod->name);
}
