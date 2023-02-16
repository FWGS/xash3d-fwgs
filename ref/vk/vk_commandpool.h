#include "vk_core.h"

typedef struct {
	VkCommandPool pool;
	VkCommandBuffer *buffers;
	int buffers_count;
} vk_command_pool_t;

vk_command_pool_t R_VkCommandPoolCreate( int count );
void R_VkCommandPoolDestroy( vk_command_pool_t *pool );
