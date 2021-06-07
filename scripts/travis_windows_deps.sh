# SDL2 for VC prebuilt
curl http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC

set -eux

curl -L --show-error --output vulkan_sdk.exe https://vulkan.lunarg.com/sdk/download/$VULKAN_SDK_VERSION/windows/vulkan_sdk.exe
date
echo "Installing Vulkan SDK $VULKAN_SDK_VERSION"
start /wait vulkan_sdk.exe /S
date
echo "Installed"

export VULKAN_SDK=C:/VulkanSDK/$VULKAN_SDK_VERSION
ls -la C:/
ls -la C:/VulkanSDK
ls -la $VULKAN_SDK
ls -la $VULKAN_SDK/Bin/glslc.exe
