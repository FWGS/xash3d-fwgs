# SDL2 for VC prebuilt
curl http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC

curl -L --show-error --output VulkanSDK.exe https://vulkan.lunarg.com/sdk/download/$VULKAN_SDK_VERSION/windows/VulkanSDK-$VULKAN_SDK_VERSION-Installer.exe?Human=true
runas /user:administrator VulkanSDK.exe /S
