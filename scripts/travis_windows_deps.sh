# SDL2 for VC prebuilt
curl http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC

curl -L --show-error --output vulkan_sdk.exe https://vulkan.lunarg.com/sdk/download/$VULKAN_SDK_VERSION/windows/vulkan_sdk.exe
echo "Installing Vulkan SDK $VULKAN_SDK_VERSION"
runas /user:administrator vulkan_sdk.exe /S
echo "Installed"
