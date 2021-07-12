#!/bin/bash

curl http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC

curl -L --show-error --output vulkan_sdk.exe https://vulkan.lunarg.com/sdk/download/$VULKAN_SDK_VERSION/windows/vulkan_sdk.exe
7z x -ovulkan_sdk vulkan_sdk.exe
