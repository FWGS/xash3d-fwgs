#pragma once
#include "vk_core.h"

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
		VkImage image;
		VkImageView image_view;
		device_memory_t device_memory;
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
	int		skyboxTextures[6];	// skybox sides
	int		cinTexture;      	// cinematic texture

	int		skytexturenum;	// this not a gl_texturenum!
	int		skyboxbasenum;	// start with 5800
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

#define VK_LoadTextureInternal( name, pic, flags ) VK_LoadTextureFromBuffer( name, pic, flags, false )

typedef struct {
	// FIXME better memory allocation
	// OCHEN PLOHO
	device_memory_t devmem;
	VkImage image;
	VkImageView view;
} vk_image_t;

vk_image_t VK_ImageCreate(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage);
void VK_ImageDestroy(vk_image_t *img);
