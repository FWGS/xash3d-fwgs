#ifndef REF_VULKAN_H
#define REF_VULKAN_H

// Define Vulkan handles without depending on vulkan.h
#ifndef VULKAN_H_
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;
#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__) ) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
#else
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;
#endif

VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR)

#undef VK_DEFINE_HANDLE
#undef VK_DEFINE_NON_DISPATCHABLE_HANDLE
#endif // ifndef VULKAN_H_

int XVK_GetInstanceExtensions( unsigned int count, const char **pNames );
void *XVK_GetVkGetInstanceProcAddr( void );
VkSurfaceKHR XVK_CreateSurface( VkInstance instance );

#endif /* REF_VULKAN_H */
