# SDL2 sources. We will build our own version
curl http://libsdl.org/release/SDL2-$SDL_VERSION.zip -o SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_src

# ref_vk required Vulkan SDK
wget -qO - https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-focal.list https://packages.lunarg.com/vulkan/lunarg-vulkan-focal.list
sudo apt update
[ "$ARCH" = "i386" ] && SUFFIX=":i386" || SUFFIX=""
sudo apt install -y vulkan-sdk"$SUFFIX"
