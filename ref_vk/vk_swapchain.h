#include "vk_core.h"

// TODO: move render pass and depth format away from this
qboolean R_VkSwapchainInit( VkRenderPass pass, VkFormat depth_format );
void R_VkSwapchainShutdown( void );

typedef struct {
	uint32_t index;
	uint32_t width, height;
	VkFramebuffer framebuffer; // TODO move out
	VkImage image;
	VkImageView view;
} r_vk_swapchain_framebuffer_t;

r_vk_swapchain_framebuffer_t R_VkSwapchainAcquire(  VkSemaphore semaphore, VkFence fence );

void R_VkSwapchainPresent( uint32_t index, VkSemaphore done );
