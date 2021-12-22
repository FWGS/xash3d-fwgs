#pragma once
#include "vk_core.h"
#include "vk_image.h"

#include "xash3d_types.h"
#include "const.h"
#include "render_api.h"
#include "com_image.h"

typedef struct vk_texture_s
{
	char name[256];
	int width, height;
	texFlags_t flags;
	uint texnum;

	struct {
		xvk_image_t image;
		VkDescriptorSet descriptor;
	} vk;

	uint hashValue;
	struct vk_texture_s	*nextHash;
} vk_texture_t;

#define MAX_LIGHTMAPS	256

typedef struct vk_textures_global_s
{
	int		defaultTexture;   	// use for bad textures
	int		particleTexture;
	int		whiteTexture;
	int		grayTexture;
	int		blackTexture;
	int		solidskyTexture;	// quake1 solid-sky layer
	int		alphaskyTexture;	// quake1 alpha-sky layer
	int		lightmapTextures[MAX_LIGHTMAPS];
	int		dlightTexture;	// custom dlight texture
	int		cinTexture;      	// cinematic texture

	int		skytexturenum;	// this not a gl_texturenum!
	int		skyboxbasenum;	// start with 5800 FIXME remove this, lewa says this is a GL1 hack

	qboolean fCustomSkybox; // TODO do we need this for anything?

	vk_texture_t skybox_cube;
	vk_texture_t cubemap_placeholder;
} vk_textures_global_t;

// TODO rename this consistently
extern vk_textures_global_t tglob;

// Helper functions
void initTextures( void );
void destroyTextures( void );
vk_texture_t *findTexture(int index);

// Public API functions
int		VK_FindTexture( const char *name );
const char*	VK_TextureName( unsigned int texnum );
const byte*	VK_TextureData( unsigned int texnum );
int		VK_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int		VK_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int		VK_LoadTextureArray( const char **names, int flags );
int		VK_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );
void		VK_FreeTexture( unsigned int texnum );
int VK_LoadTextureFromBuffer( const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update );

int	XVK_LoadTextureReplace( const char *name, const byte *buf, size_t size, int flags );

int XVK_TextureLookupF( const char *fmt, ...);

#define VK_LoadTextureInternal( name, pic, flags ) VK_LoadTextureFromBuffer( name, pic, flags, false )

void XVK_SetupSky( const char *skyboxname );

// Tries to find a texture by its short name
// Full names depend on map name, wad name, etc. This function tries them all.
// Returns -1 if not found
int XVK_FindTextureNamedLike( const char *texture_name );

int XVK_CreateDummyTexture( const char *name );
